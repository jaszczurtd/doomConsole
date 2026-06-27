/*
 * Temporary JaszczurHAL backend for the early porting stages.
 *
 * Etap 2 only needed the Doom startup path to link without Pico SDK main()
 * ownership or pico-extras video/audio backends. HAL-backed audio and display
 * now live in their own backend files; this file keeps the remaining small
 * stubs while input and music are ported.
 */

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <hal/hal_system.h>

#include "doomtype.h"
#include "i_video.h"
#include "i_joystick.h"
#include "i_sound.h"
#include "picodoom.h"
#include "r_local.h"
#include "doomstat.h"
#include "d_main.h"
#include "net_client.h"
#include "w_wad.h"
#include "tiny_huff.h"
#include "image_decoder.h"
#include "jaszczurhal/doom_boot_log.h"

#define DOOM_RENDER_DIAG_MAGIC 0x44524447u /* DRDG */
#define DOOM_RENDER_DIAG_VERSION 1u
#define HAL_PATCH_DECODER_HWORDS 2048u
#define HAL_PATCH_DECODER_TMP_BYTES 1024u
#define HAL_PATCH_COLUMN_CACHE_SLOTS 32u
#define HAL_PATCH_COLUMN_CACHE_HEIGHT 257u
#define HAL_FLAT_DECODER_HWORDS 512u
#define HAL_FLAT_DECODER_TMP_BYTES 512u
#define HAL_PLANE_QUEUE_MAX 1536u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t boot_count;
    uint32_t updates;
    uint32_t frame;
    uint32_t millis;
    uint32_t free_heap;
    uint16_t columns;
    uint16_t planes;
    uint16_t masked_columns;
    int16_t last_x;
    int16_t last_yl;
    int16_t last_yh;
    uint8_t phase;
    uint8_t last_type;
    uint8_t gamestate_value;
    uint8_t gameaction_value;
    uint8_t abort_kind;
    uint8_t reserved0[3];
    uint32_t abort_caller;
    uint32_t assert_file;
    uint32_t assert_func;
    uint32_t assert_expr;
    int32_t assert_line;
} doom_render_diag_t;

static volatile doom_render_diag_t s_render_diag
    __attribute__((section(".noinit"), used));
static uint8_t s_work_area[4096];
static uint16_t s_patch_decoder[HAL_PATCH_DECODER_HWORDS];
static uint8_t s_patch_decoder_tmp[HAL_PATCH_DECODER_TMP_BYTES];
static uint8_t s_patch_prefix_lengths[256];
static uint8_t
    s_patch_column_cache[HAL_PATCH_COLUMN_CACHE_SLOTS]
                        [HAL_PATCH_COLUMN_CACHE_HEIGHT];
static int32_t s_patch_column_cache_lump[HAL_PATCH_COLUMN_CACHE_SLOTS];
static uint8_t s_patch_column_cache_col[HAL_PATCH_COLUMN_CACHE_SLOTS];
static uint16_t s_patch_column_cache_height[HAL_PATCH_COLUMN_CACHE_SLOTS];
static uint32_t s_patch_column_cache_age[HAL_PATCH_COLUMN_CACHE_SLOTS];
static uint32_t s_patch_column_cache_clock;
static uint8_t s_patch_column_cache_valid[HAL_PATCH_COLUMN_CACHE_SLOTS];
static uint16_t s_flat_decoder[HAL_FLAT_DECODER_HWORDS];
static uint8_t s_flat_decoder_tmp[HAL_FLAT_DECODER_TMP_BYTES];
static uint8_t s_flat_prefix_lengths[256];
static int s_flat_cache_picnum = -1;
static uint16_t s_plane_queue_x[HAL_PLANE_QUEUE_MAX];
static uint8_t s_plane_queue_yl[HAL_PLANE_QUEUE_MAX];
static uint8_t s_plane_queue_yh[HAL_PLANE_QUEUE_MAX];
static uint8_t s_plane_queue_fd[HAL_PLANE_QUEUE_MAX];
static uint8_t s_plane_queue_done[(HAL_PLANE_QUEUE_MAX + 7u) / 8u];
static uint16_t s_plane_queue_count;
static uint8_t s_plane_span_top[SCREENWIDTH];
static uint8_t s_plane_span_bottom[SCREENWIDTH];
static uint32_t s_render_frame;
static uint16_t s_debug_columns;
static uint16_t s_debug_planes;
static uint16_t s_debug_plane_drops;
static uint16_t s_debug_masked_columns;
static uint16_t s_debug_patch_cache_hits;
static uint16_t s_debug_patch_cache_misses;

#if JASZCZURHAL_PORT
extern void DoomRenderSpriteDiag_Get(uint16_t *seen, uint16_t *projected,
                                     uint16_t *queued, uint16_t *drawn);
#endif

typedef struct {
    int lump;
    const patch_t *patch;
    const uint16_t *col_offsets;
    uint32_t data_index;
    uint16_t width;
    uint8_t height;
    uint8_t encoding;
    bool valid;
} hal_patch_cache_t;

static hal_patch_cache_t s_patch_cache = { .lump = -1 };

unsigned int joywait = 0;

void I_InitJoystick(void) {}
void I_ShutdownJoystick(void) {}
void I_UpdateJoystick(void) {}
void I_BindJoystickVariables(void) {}

void pd_init(void) {}

extern void DoomVideo_Core1Poll(void);
extern void DoomVideo_WaitForAsyncFlush(void);
extern void DoomVideo_GetAsyncFlushStats(uint32_t *flushes, uint32_t *waits,
                                         uint32_t *wait_us);
extern void DoomRenderOcclusionDiag_Get(uint16_t *columns, uint16_t *clipped);

void pd_core1_loop(void) { DoomVideo_Core1Poll(); }

static bool render_diag_valid(void)
{
    return s_render_diag.magic == DOOM_RENDER_DIAG_MAGIC &&
           s_render_diag.version == DOOM_RENDER_DIAG_VERSION;
}

static void render_diag_touch(uint8_t phase)
{
    if (!render_diag_valid()) {
        s_render_diag.magic = DOOM_RENDER_DIAG_MAGIC;
        s_render_diag.version = DOOM_RENDER_DIAG_VERSION;
    }

    s_render_diag.phase = phase;
    s_render_diag.frame = s_render_frame;
    s_render_diag.millis = hal_millis();
    s_render_diag.updates++;
    s_render_diag.columns = s_debug_columns;
    s_render_diag.planes = s_debug_planes;
    s_render_diag.masked_columns = s_debug_masked_columns;
    s_render_diag.gamestate_value = (uint8_t)gamestate;
    s_render_diag.gameaction_value = (uint8_t)gameaction;
}

void DoomRenderDiag_ReportRetained(void)
{
    if (!render_diag_valid() || s_render_diag.updates == 0u) {
        DoomBootLog_Printf("DOOM [render] retained: empty\n");
        return;
    }

    DoomBootLog_Printf("DOOM [render] retained: boot=%lu frame=%lu "
                       "phase=%u cols=%u planes=%u masked=%u "
                       "last=(type=%u x=%d y=%d..%d) gs=%u ga=%u "
                       "t=%lu free_heap=%lu updates=%lu abort=%u "
                       "caller=0x%08lx\n",
                       (unsigned long)s_render_diag.boot_count,
                       (unsigned long)s_render_diag.frame,
                       (unsigned int)s_render_diag.phase,
                       (unsigned int)s_render_diag.columns,
                       (unsigned int)s_render_diag.planes,
                       (unsigned int)s_render_diag.masked_columns,
                       (unsigned int)s_render_diag.last_type,
                       (int)s_render_diag.last_x,
                       (int)s_render_diag.last_yl,
                       (int)s_render_diag.last_yh,
                       (unsigned int)s_render_diag.gamestate_value,
                       (unsigned int)s_render_diag.gameaction_value,
                       (unsigned long)s_render_diag.millis,
                       (unsigned long)s_render_diag.free_heap,
                       (unsigned long)s_render_diag.updates,
                       (unsigned int)s_render_diag.abort_kind,
                       (unsigned long)s_render_diag.abort_caller);
    if (s_render_diag.abort_kind == 2u && s_render_diag.assert_file != 0u) {
        DoomBootLog_Printf("DOOM [render] retained assert: %s:%ld %s: %s\n",
                           (const char *)(uintptr_t)s_render_diag.assert_file,
                           (long)s_render_diag.assert_line,
                           s_render_diag.assert_func != 0u
                               ? (const char *)(uintptr_t)s_render_diag.assert_func
                               : "?",
                           s_render_diag.assert_expr != 0u
                               ? (const char *)(uintptr_t)s_render_diag.assert_expr
                               : "?");
    }
}

void DoomRenderDiag_StartRun(void)
{
    const uint32_t boot_count =
        render_diag_valid() ? s_render_diag.boot_count + 1u : 1u;

    memset((void *)&s_render_diag, 0, sizeof(s_render_diag));
    s_render_diag.magic = DOOM_RENDER_DIAG_MAGIC;
    s_render_diag.version = DOOM_RENDER_DIAG_VERSION;
    s_render_diag.boot_count = boot_count;
    s_render_diag.last_x = -1;
    s_render_diag.last_yl = -1;
    s_render_diag.last_yh = -1;
}

void DoomRenderDiag_MarkPhase(uint8_t phase)
{
    render_diag_touch(phase);
}

static void render_diag_record_abort(uint8_t kind, const char *file, int line,
                                     const char *func, const char *expr,
                                     void *caller)
{
    render_diag_touch(8u);
    s_render_diag.abort_kind = kind;
    s_render_diag.abort_caller = (uint32_t)(uintptr_t)caller;
    s_render_diag.assert_file = (uint32_t)(uintptr_t)file;
    s_render_diag.assert_func = (uint32_t)(uintptr_t)func;
    s_render_diag.assert_expr = (uint32_t)(uintptr_t)expr;
    s_render_diag.assert_line = line;
}

static void __attribute__((noreturn)) abort_halt_loop(void)
{
    for (;;) {
        DoomBootLog_Flush();
        hal_delay_ms(100u);
    }
}

void __attribute__((noreturn)) abort(void)
{
    void *caller = __builtin_return_address(0);

    render_diag_record_abort(1u, NULL, 0, NULL, NULL, caller);
    DoomBootLog_Printf("\n[fatal] abort caller=%p frame=%lu phase=%u "
                       "cols=%u planes=%u masked=%u free_heap=%lu\n",
                       caller, (unsigned long)s_render_diag.frame,
                       (unsigned int)s_render_diag.phase,
                       (unsigned int)s_render_diag.columns,
                       (unsigned int)s_render_diag.planes,
                       (unsigned int)s_render_diag.masked_columns,
                       (unsigned long)hal_get_free_heap());
    abort_halt_loop();
}

void __attribute__((noreturn))
__assert_func(const char *file, int line, const char *func, const char *expr)
{
    void *caller = __builtin_return_address(0);

    render_diag_record_abort(2u, file, line, func, expr, caller);
    DoomBootLog_Printf("\n[fatal] assert %s:%d %s: %s caller=%p "
                       "frame=%lu phase=%u\n",
                       file != NULL ? file : "?", line,
                       func != NULL ? func : "?", expr != NULL ? expr : "?",
                       caller, (unsigned long)s_render_diag.frame,
                       (unsigned int)s_render_diag.phase);
    abort_halt_loop();
}

void __attribute__((noreturn)) __assert(const char *file, int line,
                                        const char *expr)
{
    __assert_func(file, line, NULL, expr);
}

static int active_view_height(void)
{
    if (viewheight > 0 && viewheight <= MAIN_VIEWHEIGHT) {
        return viewheight;
    }
    return MAIN_VIEWHEIGHT;
}

static pixel_t debug_color(uint8_t base, bool use_current_colormap)
{
#if !NO_USE_DC_COLORMAP
    if (use_current_colormap && dc_colormap != NULL) {
        return dc_colormap[base];
    }
#else
    if (use_current_colormap && colormaps != NULL) {
        return colormaps[(int)dc_colormap_index * 256 + base];
    }
#endif

    if (colormaps != NULL) {
        return colormaps[base];
    }
    return base;
}

static const lighttable_t *current_column_colormap(void)
{
#if !NO_USE_DC_COLORMAP
    if (dc_colormap != NULL) {
        return dc_colormap;
    }
#else
    if (colormaps != NULL && dc_colormap_index >= 0) {
        return colormaps + (int)dc_colormap_index * 256;
    }
#endif

    if (colormaps != NULL) {
        return colormaps + 6 * 256;
    }
    return NULL;
}

static bool load_patch_decoder(int lump)
{
    if (s_patch_cache.valid && s_patch_cache.lump == lump) {
        return true;
    }

    const patch_t *patch = W_CacheLumpNum(lump, PU_CACHE);
    if (patch == NULL) {
        return false;
    }

    const uint32_t decoder_hwords = patch_decoder_size_needed(patch);
    if (decoder_hwords > HAL_PATCH_DECODER_HWORDS) {
        DoomBootLog_Printf("[render] patch decoder too small: lump=%d need=%lu\n",
                           lump, (unsigned long)decoder_hwords);
        return false;
    }

    uint32_t data_index = 3u + (uint32_t)patch_has_extra(patch);
    const uint8_t *decoder_source = patch + data_index * 2u + 1u;
    data_index += patch[data_index * 2u];

    th_bit_input bi;
    th_bit_input_init(&bi, decoder_source);

    const uint8_t encoding = (uint8_t)th_read_bits(&bi, 1);
    uint16_t *decoder_end = NULL;

    if (encoding == 0u) {
        if (th_bit(&bi)) {
            decoder_end = th_read_simple_decoder(&bi, s_patch_decoder,
                                                 HAL_PATCH_DECODER_HWORDS,
                                                 s_patch_decoder_tmp,
                                                 HAL_PATCH_DECODER_TMP_BYTES);
        } else {
            decoder_end = read_raw_pixels_decoder(&bi, s_patch_decoder,
                                                  HAL_PATCH_DECODER_HWORDS,
                                                  s_patch_decoder_tmp,
                                                  HAL_PATCH_DECODER_TMP_BYTES);
        }
    } else if (encoding == 1u) {
        decoder_end = read_raw_pixels_decoder_c3(&bi, s_patch_decoder,
                                                 HAL_PATCH_DECODER_HWORDS,
                                                 s_patch_decoder_tmp,
                                                 HAL_PATCH_DECODER_TMP_BYTES);
    } else {
        return false;
    }

    if (decoder_end == NULL ||
        decoder_end > s_patch_decoder + HAL_PATCH_DECODER_HWORDS) {
        DoomBootLog_Printf("[render] patch decoder overflow: lump=%d\n", lump);
        return false;
    }

    th_make_prefix_length_table(s_patch_decoder, s_patch_prefix_lengths);

    const uint16_t width = (uint16_t)patch_width(patch);
    s_patch_cache.lump = lump;
    s_patch_cache.patch = patch;
    s_patch_cache.col_offsets = &((const uint16_t *)patch)[data_index];
    s_patch_cache.data_index = (data_index + width) * 2u + 2u;
    s_patch_cache.width = width;
    s_patch_cache.height = (uint8_t)patch_height(patch);
    s_patch_cache.encoding = encoding;
    s_patch_cache.valid = true;
    return true;
}

static uint8_t decode_patch_pixel8(th_bit_input *bi)
{
    if (s_patch_decoder[0] == 0u) {
        return 0;
    }
    if (s_patch_decoder[0] == 1u) {
        return *(const uint8_t *)(s_patch_decoder + 1);
    }
    return th_decode_table_special(s_patch_decoder, s_patch_prefix_lengths, bi);
}

static uint16_t decode_patch_pixel16(th_bit_input *bi)
{
    if (s_patch_decoder[0] == 0u) {
        return 0;
    }
    if (s_patch_decoder[0] == 1u) {
        return s_patch_decoder[1];
    }
    return th_decode_table_special_16(s_patch_decoder, s_patch_prefix_lengths,
                                      bi);
}

static bool resolve_patch_source(texturecolumn_t source, int *lump_out,
                                 uint16_t *col_out)
{
    if (source.real_id >= 0) {
        if (whd_textures == NULL ||
            whd_textures[source.real_id].patch_count != 0) {
            return false;
        }
        source.real_id = -(int16_t)whd_textures[source.real_id].patch0;
    }

    *lump_out = -(int)source.real_id;
    *col_out = source.col;
    return true;
}

static uint8_t decode_flat_pixel8(th_bit_input *bi)
{
    if (s_flat_decoder[0] == 0u) {
        return 0;
    }
    if (s_flat_decoder[0] == 1u) {
        return *(const uint8_t *)(s_flat_decoder + 1);
    }
    return th_decode_table_special(s_flat_decoder, s_flat_prefix_lengths, bi);
}

static int translate_flat_picnum(int picnum)
{
#if USE_WHD
    if (whd_specialtoflat != NULL && whd_flattospecial[picnum] != 0xffu) {
        picnum = whd_specialtoflat[whd_flattranslation[whd_flattospecial[picnum]]];
    }
    return picnum;
#else
    return flat_translation(picnum);
#endif
}

static uint8_t *load_flat_pixels(int picnum)
{
    picnum = translate_flat_picnum(picnum);
    if (s_flat_cache_picnum == picnum) {
        return s_work_area;
    }

    const uint8_t *source = W_CacheLumpNum(firstflat + picnum, PU_STATIC);
    if (source == NULL) {
        return NULL;
    }

    th_bit_input bi;
    th_bit_input_init(&bi, source);

    uint16_t *decoder_end;
    if (th_bit(&bi)) {
        decoder_end = th_read_simple_decoder(&bi, s_flat_decoder,
                                             HAL_FLAT_DECODER_HWORDS,
                                             s_flat_decoder_tmp,
                                             HAL_FLAT_DECODER_TMP_BYTES);
    } else {
        decoder_end = read_raw_pixels_decoder(&bi, s_flat_decoder,
                                              HAL_FLAT_DECODER_HWORDS,
                                              s_flat_decoder_tmp,
                                              HAL_FLAT_DECODER_TMP_BYTES);
    }

    if (decoder_end == NULL ||
        decoder_end > s_flat_decoder + HAL_FLAT_DECODER_HWORDS) {
        DoomBootLog_Printf("[render] flat decoder overflow: pic=%d\n", picnum);
        return NULL;
    }

    th_make_prefix_length_table(s_flat_decoder, s_flat_prefix_lengths);

    const bool have_same_columns = th_bit(&bi) != 0;
    if (!have_same_columns) {
        for (int i = 0; i < 4096; ++i) {
            s_work_area[i] = decode_flat_pixel8(&bi);
        }
    } else {
        for (int x = 0; x < 64; ++x) {
            uint8_t *dest = s_work_area + x * 64;
            if (th_bit(&bi)) {
                const unsigned bits = bitcount8((uint8_t)x);
                const unsigned src_x = bits ? th_read_bits(&bi, bits) : 0u;
                if (src_x >= (unsigned)x) {
                    return NULL;
                }
                memcpy(dest, s_work_area + src_x * 64, 64);
            } else {
                for (int y = 0; y < 64; ++y) {
                    dest[y] = decode_flat_pixel8(&bi);
                }
            }
        }
    }

    s_flat_cache_picnum = picnum;
    return s_work_area;
}

static const lighttable_t *plane_colormap_for(const visplane_t *pl,
                                              fixed_t distance)
{
    int map_index;

    if (colormaps == NULL) {
        return NULL;
    }

    if (fixedcolormap) {
        map_index = fixedcolormap;
    } else {
        int light = (pl->lightlevel >> LIGHTSEGSHIFT) + extralight;
        if (light < 0) {
            light = 0;
        } else if (light >= LIGHTLEVELS) {
            light = LIGHTLEVELS - 1;
        }

        const unsigned distance_index = (unsigned)distance >> LIGHTZSHIFT;
        const int startmap =
            ((LIGHTLEVELS - 1 - light) * 2) * NUMCOLORMAPS / LIGHTLEVELS;
        const int scale = (SCREENWIDTH / 4) / (int)(distance_index + 1u);
        map_index = startmap - scale;
        if (map_index < 0) {
            map_index = 0;
        } else if (map_index >= NUMCOLORMAPS) {
            map_index = NUMCOLORMAPS - 1;
        }
    }

    return colormaps + map_index * 256;
}

static bool plane_queue_done(unsigned index)
{
    return (s_plane_queue_done[index >> 3u] & (1u << (index & 7u))) != 0u;
}

static void plane_queue_mark_done(unsigned index)
{
    s_plane_queue_done[index >> 3u] |= (uint8_t)(1u << (index & 7u));
}

static void debug_plane_drop(void)
{
    if (s_debug_plane_drops != UINT16_MAX) {
        ++s_debug_plane_drops;
    }
}

static bool queue_flat_vertical(int x, int yl, int yh, int fd_num)
{
    if ((unsigned)x >= SCREENWIDTH || fd_num < 0 || fd_num > UINT8_MAX) {
        return false;
    }

    const int limit_y = active_view_height();
    if (yl < 0) {
        yl = 0;
    }
    if (yh >= limit_y) {
        yh = limit_y - 1;
    }
    if (yl > yh) {
        return true;
    }
    if (s_plane_queue_count >= HAL_PLANE_QUEUE_MAX) {
        debug_plane_drop();
        return false;
    }

    const unsigned index = s_plane_queue_count++;
    s_plane_queue_x[index] = (uint16_t)x;
    s_plane_queue_yl[index] = (uint8_t)yl;
    s_plane_queue_yh[index] = (uint8_t)yh;
    s_plane_queue_fd[index] = (uint8_t)fd_num;
    return true;
}

static void draw_flat_span(const visplane_t *pl, const uint8_t *flat,
                           int y, int x1, int x2)
{
    const fixed_t rel_height =
        pl->height >= viewz ? pl->height - viewz : viewz - pl->height;
    const fixed_t distance = FixedMul(rel_height, yslope[y]);
    const lighttable_t *map = plane_colormap_for(pl, distance);
    if (map == NULL) {
        return;
    }

    const fixed_t xstep = FixedMul(distance, basexscale);
    const fixed_t ystep = FixedMul(distance, baseyscale);
    const angle_t angle = (viewangle + x_to_viewangle(x1)) >> ANGLETOFINESHIFT;
    const fixed_t length = FixedMul(distance, distscale(x1));
    const fixed_t xfrac = viewx + FixedMul(finecosine(angle), length);
    const fixed_t yfrac = -viewy - FixedMul(finesine(angle), length);

    uint32_t position = ((uint32_t)(yfrac << 10) & 0xffff0000u) |
                        ((uint32_t)(xfrac >> 6) & 0x0000ffffu);
    const uint32_t step = ((uint32_t)(ystep << 10) & 0xffff0000u) |
                          ((uint32_t)(xstep >> 6) & 0x0000ffffu);
    pixel_t *dest = I_VideoBuffer + y * SCREENWIDTH + x1;

    for (int x = x1; x <= x2; ++x) {
        const uint32_t spot = ((position >> 4) & 0x0fc0u) |
                              (position >> 26);
        *dest++ = map[flat[spot]];
        position += step;
    }
}

static bool render_queued_plane_fd(int fd_num)
{
    if (lastvisplane == NULL || fd_num < 0 ||
        fd_num >= (int)(lastvisplane - visplanes)) {
        return false;
    }

    const visplane_t *pl = &visplanes[fd_num];
    if (pl->picnum == skyflatnum) {
        return false;
    }

    uint8_t *flat = load_flat_pixels(pl->picnum);
    if (flat == NULL) {
        return false;
    }

    memset(s_plane_span_top, 0xff, sizeof(s_plane_span_top));

    int min_x = SCREENWIDTH;
    int max_x = -1;
    int min_y = active_view_height();
    int max_y = -1;

    for (unsigned i = 0; i < s_plane_queue_count; ++i) {
        if (plane_queue_done(i) || s_plane_queue_fd[i] != (uint8_t)fd_num) {
            continue;
        }

        const int x = s_plane_queue_x[i];
        const int yl = s_plane_queue_yl[i];
        const int yh = s_plane_queue_yh[i];

        s_plane_span_top[x] = (uint8_t)yl;
        s_plane_span_bottom[x] = (uint8_t)yh;
        if (x < min_x) {
            min_x = x;
        }
        if (x > max_x) {
            max_x = x;
        }
        if (yl < min_y) {
            min_y = yl;
        }
        if (yh > max_y) {
            max_y = yh;
        }
        plane_queue_mark_done(i);
    }

    if (max_x < min_x || max_y < min_y) {
        return true;
    }

    for (int y = min_y; y <= max_y; ++y) {
        int x = min_x;
        while (x <= max_x) {
            while (x <= max_x &&
                   (s_plane_span_top[x] == 0xffu ||
                    y < s_plane_span_top[x] ||
                    y > s_plane_span_bottom[x])) {
                ++x;
            }
            if (x > max_x) {
                break;
            }

            const int x1 = x;
            do {
                ++x;
            } while (x <= max_x &&
                     s_plane_span_top[x] != 0xffu &&
                     y >= s_plane_span_top[x] &&
                     y <= s_plane_span_bottom[x]);

            draw_flat_span(pl, flat, y, x1, x - 1);
        }
    }

    return true;
}

void DoomHAL_RenderQueuedPlanes(void)
{
    memset(s_plane_queue_done, 0, sizeof(s_plane_queue_done));

    for (unsigned i = 0; i < s_plane_queue_count; ++i) {
        if (plane_queue_done(i)) {
            continue;
        }

        const int fd_num = s_plane_queue_fd[i];
        if (lastvisplane == NULL || fd_num >= (int)(lastvisplane - visplanes)) {
            plane_queue_mark_done(i);
            continue;
        }

        const int target_picnum =
            translate_flat_picnum(visplanes[fd_num].picnum);
        if (load_flat_pixels(visplanes[fd_num].picnum) == NULL) {
            plane_queue_mark_done(i);
            continue;
        }

        for (unsigned j = i; j < s_plane_queue_count; ++j) {
            if (plane_queue_done(j)) {
                continue;
            }

            const int queued_fd = s_plane_queue_fd[j];
            if (lastvisplane == NULL ||
                queued_fd >= (int)(lastvisplane - visplanes)) {
                plane_queue_mark_done(j);
                continue;
            }

            if (translate_flat_picnum(visplanes[queued_fd].picnum) !=
                target_picnum) {
                continue;
            }

            (void)render_queued_plane_fd(queued_fd);
        }
    }

    s_plane_queue_count = 0;
}

static bool decode_patch_column_uncached(texturecolumn_t source, uint8_t *pixels,
                                         int *height_out)
{
    int lump;
    uint16_t col;
    if (!resolve_patch_source(source, &lump, &col)) {
        return false;
    }

    if (!load_patch_decoder(lump) || s_patch_cache.height == 0u) {
        return false;
    }
    if (s_patch_cache.height > HAL_PATCH_COLUMN_CACHE_HEIGHT) {
        DoomBootLog_Printf("[render] patch column too tall: lump=%d h=%u\n",
                           lump, (unsigned int)s_patch_cache.height);
        return false;
    }

    if (col >= s_patch_cache.width) {
        return false;
    }

    uint16_t col_offset = s_patch_cache.col_offsets[col];
    if ((col_offset >> 8) == 0xffu) {
        col = col_offset & 0xffu;
        if (col >= s_patch_cache.width) {
            return false;
        }
        col_offset = s_patch_cache.col_offsets[col];
    }

    th_bit_input bi;
    if (patch_byte_addressed(s_patch_cache.patch)) {
        th_bit_input_init(&bi, s_patch_cache.patch + s_patch_cache.data_index +
                                  col_offset);
    } else {
        th_bit_input_init_bit_offset(&bi, s_patch_cache.patch +
                                     s_patch_cache.data_index, col_offset);
    }

    uint8_t prev_pixel = 0;
    for (int y = 0; y < s_patch_cache.height; ++y) {
        if (s_patch_cache.encoding == 0u) {
            pixels[y] = decode_patch_pixel8(&bi);
        } else {
            const uint16_t decoded = decode_patch_pixel16(&bi);
            if (decoded < 256u) {
                pixels[y] = (uint8_t)decoded;
            } else {
                const uint16_t delta = decoded & 0xffu;
                if (delta >= 7u) {
                    return false;
                }
                pixels[y] = (uint8_t)(prev_pixel + (int)delta - 3);
            }
            prev_pixel = pixels[y];
        }
    }

    if (s_patch_cache.height < 128u) {
        const uint8_t edge = pixels[s_patch_cache.height - 1u];
        for (int y = s_patch_cache.height; y < 128; ++y) {
            pixels[y] = edge;
        }
        pixels[127] = pixels[0];
    }

    *height_out = s_patch_cache.height;
    return true;
}

static bool decode_patch_column(texturecolumn_t source,
                                const uint8_t **pixels_out, int *height_out)
{
    int lump;
    uint16_t col;
    if (!resolve_patch_source(source, &lump, &col)) {
        return false;
    }

    if (++s_patch_column_cache_clock == 0u) {
        for (unsigned i = 0; i < HAL_PATCH_COLUMN_CACHE_SLOTS; ++i) {
            s_patch_column_cache_age[i] = 0u;
        }
        s_patch_column_cache_clock = 1u;
    }

    for (unsigned slot = 0; slot < HAL_PATCH_COLUMN_CACHE_SLOTS; ++slot) {
        if (s_patch_column_cache_valid[slot] &&
            s_patch_column_cache_lump[slot] == lump &&
            s_patch_column_cache_col[slot] == (uint8_t)col) {
            s_patch_column_cache_age[slot] = s_patch_column_cache_clock;
            *pixels_out = s_patch_column_cache[slot];
            *height_out = s_patch_column_cache_height[slot];
            if (s_debug_patch_cache_hits != UINT16_MAX) {
                ++s_debug_patch_cache_hits;
            }
            return true;
        }
    }

    unsigned slot = 0;
    uint32_t oldest_age = UINT32_MAX;
    for (unsigned i = 0; i < HAL_PATCH_COLUMN_CACHE_SLOTS; ++i) {
        if (!s_patch_column_cache_valid[i]) {
            slot = i;
            break;
        }
        if (s_patch_column_cache_age[i] < oldest_age) {
            oldest_age = s_patch_column_cache_age[i];
            slot = i;
        }
    }

    int decoded_height = 0;
    if (!decode_patch_column_uncached(source, s_patch_column_cache[slot],
                                     &decoded_height)) {
        return false;
    }

    s_patch_column_cache_lump[slot] = lump;
    s_patch_column_cache_col[slot] = (uint8_t)col;
    s_patch_column_cache_height[slot] = (uint16_t)decoded_height;
    s_patch_column_cache_age[slot] = s_patch_column_cache_clock;
    s_patch_column_cache_valid[slot] = 1u;
    *pixels_out = s_patch_column_cache[slot];
    *height_out = decoded_height;
    if (s_debug_patch_cache_misses != UINT16_MAX) {
        ++s_debug_patch_cache_misses;
    }
    return true;
}

static bool draw_textured_vertical_pixels(int x, int yl, int yh,
                                          fixed_t texturemid,
                                          fixed_t fracstep,
                                          const lighttable_t *map,
                                          const uint8_t *source_pixels,
                                          int source_height)
{
    const int limit_y = active_view_height();
    if (map == NULL || source_pixels == NULL || source_height <= 0) {
        return false;
    }

    if ((unsigned)x >= SCREENWIDTH) {
        return true;
    }
    if (yl < 0) {
        yl = 0;
    }
    if (yh >= limit_y) {
        yh = limit_y - 1;
    }
    if (yl > yh) {
        return true;
    }

    pixel_t *dest = I_VideoBuffer + yl * SCREENWIDTH + x;
    if (fracstep == 0) {
        fracstep = FRACUNIT;
    }
    fixed_t frac = texturemid + (yl - centery) * fracstep;

    for (int y = yl; y <= yh; ++y) {
        int source_y = frac >> FRACBITS;
        if (source_height == 128) {
            source_y &= 127;
        } else {
            if (source_y < 0) {
                source_y = 0;
            } else if (source_y >= source_height) {
                source_y = source_height - 1;
            }
        }
        *dest = map[source_pixels[source_y]];
        dest += SCREENWIDTH;
        frac += fracstep;
    }

    return true;
}

static bool draw_textured_vertical(int x, int yl, int yh, fixed_t texturemid,
                                   fixed_t fracstep, texturecolumn_t source)
{
    const lighttable_t *map = current_column_colormap();
    const uint8_t *source_pixels = NULL;
    int source_height = 0;

    if (!decode_patch_column(source, &source_pixels, &source_height)) {
        return false;
    }

    return draw_textured_vertical_pixels(x, yl, yh, texturemid, fracstep, map,
                                         source_pixels, source_height);
}

static void draw_debug_vertical(int x, int yl, int yh, pixel_t color)
{
    const int limit_y = active_view_height();

    if ((unsigned)x >= SCREENWIDTH) {
        return;
    }
    if (yl < 0) {
        yl = 0;
    }
    if (yh >= limit_y) {
        yh = limit_y - 1;
    }
    if (yl > yh) {
        return;
    }

    pixel_t *dest = I_VideoBuffer + yl * SCREENWIDTH + x;
    for (int y = yl; y <= yh; ++y) {
        *dest = color;
        dest += SCREENWIDTH;
    }
}

static uint8_t column_base_color(pd_column_type type)
{
    uint16_t seed = (uint16_t)(dc_source.real_id * 17u + dc_source.col * 5u);

    switch (type) {
    case PDCOL_SKY:
        return (uint8_t)(0x70u + (dc_x & 0x0f));
    case PDCOL_TOP:
        return (uint8_t)(0x30u + (seed & 0x1f));
    case PDCOL_MID:
        return (uint8_t)(0x50u + (seed & 0x1f));
    case PDCOL_BOTTOM:
        return (uint8_t)(0x80u + (seed & 0x1f));
    case PDCOL_MASKED:
        return (uint8_t)(0xb0u + (seed & 0x1f));
    default:
        return (uint8_t)(0x20u + (seed & 0x1f));
    }
}

void pd_begin_frame(void)
{
    DoomVideo_WaitForAsyncFlush();

    hal_alive_mark();
    hal_stack_guard_check();

    if (s_render_frame > 0 &&
        (s_render_frame <= 8 || (s_render_frame % 70u) == 0u)) {
        uint16_t sprite_seen = 0;
        uint16_t sprite_projected = 0;
        uint16_t sprite_queued = 0;
        uint16_t sprite_drawn = 0;
        uint16_t occluder_columns = 0;
        uint16_t occluder_clipped = 0;
        uint32_t async_flushes = 0;
        uint32_t async_waits = 0;
        uint32_t async_wait_us = 0;
#if JASZCZURHAL_PORT
        DoomRenderSpriteDiag_Get(&sprite_seen, &sprite_projected,
                                 &sprite_queued, &sprite_drawn);
        DoomRenderOcclusionDiag_Get(&occluder_columns, &occluder_clipped);
        DoomVideo_GetAsyncFlushStats(&async_flushes, &async_waits,
                                     &async_wait_us);
#endif
        DoomBootLog_Printf("[render] frame=%lu cols=%u planes=%u pdrop=%u masked=%u "
                           "pcache=%u/%u spr=%u/%u/%u/%u "
                           "occ=%u/%u flush=%lu/%lu/%lu free_heap=%lu\n",
                           (unsigned long)s_render_frame, s_debug_columns,
                           s_debug_planes, s_debug_plane_drops,
                           s_debug_masked_columns,
                           s_debug_patch_cache_hits,
                           s_debug_patch_cache_misses,
                           sprite_seen, sprite_projected, sprite_queued,
                           sprite_drawn,
                           occluder_columns, occluder_clipped,
                           (unsigned long)async_flushes,
                           (unsigned long)async_waits,
                           (unsigned long)(async_wait_us / 1000u),
                           (unsigned long)hal_get_free_heap());
    }

    memset(I_VideoBuffer, 0, SCREENWIDTH * active_view_height());
    reset_framedrawables();
    s_debug_columns = 0;
    s_debug_planes = 0;
    s_debug_plane_drops = 0;
    s_debug_masked_columns = 0;
    s_debug_patch_cache_hits = 0;
    s_debug_patch_cache_misses = 0;
    s_plane_queue_count = 0;
    ++s_render_frame;
    s_render_diag.free_heap = hal_get_free_heap();
    render_diag_touch(1u);
}

void pd_add_column(pd_column_type type)
{
    const uint8_t base = column_base_color(type);
    const pixel_t color = debug_color(base, true);
    const bool textured =
        draw_textured_vertical(dc_x, dc_yl, dc_yh, dc_texturemid, dc_iscale,
                               dc_source);

    s_render_diag.last_type = (uint8_t)type;
    s_render_diag.last_x = (int16_t)dc_x;
    s_render_diag.last_yl = (int16_t)dc_yl;
    s_render_diag.last_yh = (int16_t)dc_yh;
    render_diag_touch(2u);
    if (!textured) {
        draw_debug_vertical(dc_x, dc_yl, dc_yh, color);
    }
    if (s_debug_columns != UINT16_MAX) {
        ++s_debug_columns;
    }
}

void pd_add_masked_columns(uint8_t *ys, int seg_count)
{
    const pixel_t color = debug_color(column_base_color(PDCOL_MASKED), true);

    for (int i = 0; i < seg_count; ++i) {
        const fixed_t texturemid =
            dc_texturemid - ((fixed_t)ys[i * 3 + 2] << FRACBITS);
        const bool textured =
            draw_textured_vertical(dc_x, ys[i * 3], ys[i * 3 + 1],
                                   texturemid, dc_iscale, dc_source);

        s_render_diag.last_type = (uint8_t)PDCOL_MASKED;
        s_render_diag.last_x = (int16_t)dc_x;
        s_render_diag.last_yl = (int16_t)ys[i * 3];
        s_render_diag.last_yh = (int16_t)ys[i * 3 + 1];
        render_diag_touch(3u);
        if (!textured) {
            draw_debug_vertical(dc_x, ys[i * 3], ys[i * 3 + 1], color);
        }
    }
    if (s_debug_masked_columns != UINT16_MAX) {
        ++s_debug_masked_columns;
    }
}

void pd_add_plane_column(int x, int yl, int yh, fixed_t scale, int floor, int fd_num)
{
    (void)scale;

    const uint8_t base =
        (uint8_t)((floor ? 0x40u : 0x90u) + ((uint8_t)fd_num * 7u & 0x1fu));
    const bool queued = queue_flat_vertical(x, yl, yh, fd_num);

    s_render_diag.last_type = (uint8_t)(floor ? PDCOL_FLOOR : PDCOL_CEILING);
    s_render_diag.last_x = (int16_t)x;
    s_render_diag.last_yl = (int16_t)yl;
    s_render_diag.last_yh = (int16_t)yh;
    render_diag_touch(4u);
    if (!queued) {
        draw_debug_vertical(x, yl, yh, debug_color(base, false));
    }
    if (s_debug_planes != UINT16_MAX) {
        ++s_debug_planes;
    }
}
uint8_t *pd_get_work_area(uint32_t *size)
{
    s_flat_cache_picnum = -1;
    if (size != NULL) *size = sizeof(s_work_area);
    return s_work_area;
}
#if PICO_ON_DEVICE
void pd_start_save_pause(void) {}
void pd_end_save_pause(void) {}
#endif

static const snddevice_t s_stub_music_devices[] = { SNDDEVICE_SB };

boolean drone = false;

static boolean music_stub_init(void) { return false; }
static void music_stub_shutdown(void) {}
static void music_stub_set_volume(int volume) { (void)volume; }
static void music_stub_pause(void) {}
static void music_stub_resume(void) {}
static void *music_stub_register(should_be_const void *data, int len)
{
    (void)data;
    (void)len;
    return NULL;
}
static void music_stub_unregister(void *handle) { (void)handle; }
static void music_stub_play(void *handle, boolean looping)
{
    (void)handle;
    (void)looping;
}
static void music_stub_stop(void) {}
static boolean music_stub_is_playing(void) { return false; }
static void music_stub_poll(void) {}

const music_module_t music_opl_module = {
    s_stub_music_devices,
    1,
    music_stub_init,
    music_stub_shutdown,
    music_stub_set_volume,
    music_stub_pause,
    music_stub_resume,
    music_stub_register,
    music_stub_unregister,
    music_stub_play,
    music_stub_stop,
    music_stub_is_playing,
    music_stub_poll,
};

void I_SetOPLDriverVer(opl_driver_ver_t ver) { (void)ver; }
void I_OPL_DevMessages(char *result, size_t result_len)
{
    if (result != NULL && result_len > 0) result[0] = '\0';
}

void th_bit_overrun(th_bit_input *bi) { (void)bi; }
void I_Endoom(should_be_const byte *data) { (void)data; }
void piconet_stop(void) {}
