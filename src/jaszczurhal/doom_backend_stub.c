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

#include <JaszczurHAL.h>

#include <hal/system/hal_sync.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#include <hardware/structs/sio.h>

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
#include "doom_main_config.h"

// Dual-core wall/sky/midtex column rendering.  When enabled, pd_add_column()
// enqueues column descriptors during BSP instead of decoding+drawing inline;
// the queue is drained (Stage 1: on core0; Stage 3: split across both cores)
// before the plane phase.  Default OFF keeps the inline single-core path and
// adds zero SRAM.
// Larger decoded-column cache to raise the cross-frame hit rate (wall decode
// from flash dominates render time).  Most Doom columns are 64/128 px tall, so
// cache compact slots for that common case and keep one full-height scratch
// column for rare 256 px textures instead of making every slot 257 bytes.
// Per-frame clear value for the view framebuffer.  Normally 0 (black).  Can be
// set to a distinctive palette index (e.g. 251) to make genuinely UNDRAWN
// pixels visible on screen and countable in `black` for debugging.

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
static uint16_t
    s_patch_decoder[HAL_PATCH_COLUMN_CACHE_CORES][HAL_PATCH_DECODER_HWORDS];
static uint8_t
    s_patch_decoder_tmp[HAL_PATCH_COLUMN_CACHE_CORES]
                       [HAL_PATCH_DECODER_TMP_BYTES];
static uint8_t s_patch_prefix_lengths[HAL_PATCH_COLUMN_CACHE_CORES][256];
static uint8_t
    s_patch_column_cache[HAL_PATCH_COLUMN_CACHE_SLOTS]
                        [HAL_PATCH_COLUMN_CACHE_HEIGHT];
#if DOOM_DUAL_CORE_COLUMNS
static uint8_t
    s_patch_column_uncached[HAL_PATCH_COLUMN_CACHE_CORES]
                            [HAL_PATCH_COLUMN_MAX_HEIGHT];
#else
static uint8_t s_patch_column_uncached[HAL_PATCH_COLUMN_MAX_HEIGHT];
#endif
static int32_t s_patch_column_cache_lump[HAL_PATCH_COLUMN_CACHE_SLOTS];
static uint16_t s_patch_column_cache_col[HAL_PATCH_COLUMN_CACHE_SLOTS];
static uint16_t s_patch_column_cache_height[HAL_PATCH_COLUMN_CACHE_SLOTS];
static uint32_t s_patch_column_cache_age[HAL_PATCH_COLUMN_CACHE_SLOTS];
static uint32_t s_patch_column_cache_clock[HAL_PATCH_COLUMN_CACHE_CORES];
static uint8_t
    s_patch_column_cache_hash[HAL_PATCH_COLUMN_CACHE_CORES]
                             [HAL_PATCH_COLUMN_CACHE_HASH_SIZE];
static uint8_t s_patch_column_cache_valid[HAL_PATCH_COLUMN_CACHE_SLOTS];
#if !DOOM_DUAL_CORE_COLUMNS
static int32_t s_patch_tall_cache_lump[HAL_PATCH_TALL_CACHE_SLOTS];
static uint16_t s_patch_tall_cache_col[HAL_PATCH_TALL_CACHE_SLOTS];
static uint16_t s_patch_tall_cache_height[HAL_PATCH_TALL_CACHE_SLOTS];
static uint32_t s_patch_tall_cache_age[HAL_PATCH_TALL_CACHE_SLOTS];
static uint8_t s_patch_tall_cache_valid[HAL_PATCH_TALL_CACHE_SLOTS];
#endif
static uint16_t s_flat_decoder[HAL_FLAT_DECODER_HWORDS];
static uint8_t s_flat_decoder_tmp[HAL_FLAT_DECODER_TMP_BYTES];
static uint8_t s_flat_prefix_lengths[256];
static int s_flat_cache_picnum = -1;
static uint16_t s_plane_queue_x[HAL_PLANE_QUEUE_MAX];
static uint16_t s_plane_queue_yl[HAL_PLANE_QUEUE_MAX];
static uint16_t s_plane_queue_yh[HAL_PLANE_QUEUE_MAX];
static uint8_t s_plane_queue_fd[HAL_PLANE_QUEUE_MAX];
static uint8_t s_plane_queue_done[(HAL_PLANE_QUEUE_MAX + 7u) / 8u];
static uint16_t s_plane_queue_count;
static volatile uint8_t s_plane_render_pending;
static volatile uint8_t s_plane_render_busy;
static volatile uint32_t s_plane_render_async_count;
static volatile uint32_t s_plane_render_wait_count;
static volatile uint32_t s_plane_render_wait_us;
static uint16_t s_plane_span_top[SCREENWIDTH];
static uint16_t s_plane_span_bottom[SCREENWIDTH];

#if DOOM_DUAL_CORE_COLUMNS
// Bounded column queue (flush-on-full).  Captures everything pd_add_column()
// would draw during BSP, so the decode+draw can be deferred and split across
// cores.  Entry holds exactly what draw_textured_vertical_map() needs.
typedef struct {
    fixed_t texturemid;
    fixed_t iscale;
    texturecolumn_t source;
    const lighttable_t *map;
    int16_t x;
    int16_t yl; // may be negative before clamping; keep signed
    int16_t yh;
    uint8_t tile;
    uint8_t type;
} doom_col_entry_t;
static doom_col_entry_t s_col_queue[DOOM_COL_QUEUE_MAX];
static uint16_t s_col_queue_count;
static volatile uint8_t s_col_render_pending;
static volatile uint8_t s_col_render_busy;
static volatile uint32_t s_col_render_async_count;
static volatile uint32_t s_col_render_wait_count;
static volatile uint32_t s_col_render_wait_us;
#endif

static uint32_t s_render_frame;
static uint16_t s_debug_columns;
static uint16_t s_debug_planes;
static uint16_t s_debug_plane_drops;
static uint16_t s_debug_masked_columns;
static uint16_t s_debug_patch_cache_hits;
static uint16_t s_debug_patch_cache_misses;
static uint16_t s_debug_patch_cache_uncached_tall;
static uint16_t s_debug_patch_cache_tall_hits;
static uint16_t s_debug_patch_tall_cache_resets;
static uint16_t s_debug_patch_tall_cache_evictions;
static uint16_t s_debug_patch_tall_height_le160;
static uint16_t s_debug_patch_tall_height_le192;
static uint16_t s_debug_patch_tall_height_le224;
static uint16_t s_debug_patch_tall_height_gt224;
static uint16_t s_debug_patch_tall_height_max;
static uint16_t s_debug_col_queue_queued;
static uint16_t s_debug_col_queue_inline;
static uint16_t s_debug_col_queue_left;
static uint16_t s_debug_col_queue_right;
static uint16_t s_debug_tex_fail; // columns drawn as debug color (decode failed)
static uint16_t s_debug_flat_fail; // visplanes skipped because the flat failed
static uint16_t s_debug_plane_overwrite; // plane columns with >1 range (deferred)
static uint32_t s_fps_last_ms;    // wall-clock at previous render log
static uint32_t s_fps_last_frame; // frame number at previous render log

#if JASZCZURHAL_PORT
extern void DoomRenderSpriteDiag_Get(uint16_t *seen, uint16_t *projected,
                                     uint16_t *queued, uint16_t *drawn);
extern void DoomRenderSpriteSegDiag_Get(uint16_t *overflow, uint8_t *seg_max);
extern void DoomRenderTimeDiag_Get(uint32_t *bsp_us, uint32_t *planes_us,
                                   uint32_t *masked_us, uint32_t *psprite_us,
                                   uint32_t *hud_us);
void DoomHAL_RenderQueuedPlanesAsyncStats(uint32_t *done, uint32_t *waits,
                                          uint32_t *wait_us);
#endif

typedef struct {
    int lump;
    const patch_t *patch;
    const uint16_t *col_offsets;
    uint32_t data_index;
    uint16_t width;
    uint16_t height; // up to 256; uint8_t would truncate 256 -> 0
    uint8_t encoding;
    bool valid;
} hal_patch_cache_t;

static hal_patch_cache_t s_patch_cache[HAL_PATCH_COLUMN_CACHE_CORES] = {
    { .lump = -1 }
};

unsigned int joywait = 0;

void I_InitJoystick(void) {}
void I_ShutdownJoystick(void) {}
void I_UpdateJoystick(void) {}
void I_BindJoystickVariables(void) {}

void pd_init(void) {}

extern void DoomVideo_Core1Poll(void);
extern void DoomVideo_WaitForAsyncFlush(void);
extern void DoomVideo_BeginRenderFrame(void);
extern void DoomVideo_GetAsyncFlushStats(uint32_t *flushes, uint32_t *waits,
                                         uint32_t *wait_us);
extern void DoomVideo_GetDoubleBufferStats(uint32_t *swaps, uint32_t *waits,
                                           uint32_t *wait_us);
extern void DoomVideo_GetAsyncHudStats(uint32_t *done, uint32_t *waits,
                                       uint32_t *wait_us);
extern void DoomRenderOcclusionDiag_Get(uint16_t *columns, uint16_t *clipped);

static void plane_render_async_poll(void);
static void col_render_async_poll(void);
static void col_render_async_stats(uint32_t *done, uint32_t *waits,
                                   uint32_t *wait_us);

void pd_core1_loop(void)
{
    DoomVideo_Core1Poll();
    col_render_async_poll();
    plane_render_async_poll();
}

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
        deb("DOOM [render] retained: empty\n");
        return;
    }

    deb("DOOM [render] retained: boot=%lu frame=%lu "
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
        deb("DOOM [render] retained assert: %s:%ld %s: %s\n",
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

// Authoritative build-config proof, straight from the linked binary: reports the
// REAL compiled sizes (sizeof), not just macro values, so the boot log removes any
// doubt about which column-render variant is actually running.
void DoomHAL_LogRenderConfig(void)
{
    deb("[boot] rendercfg: DUAL_CORE=%d ASYNC_PLANES=%d SYNC_FLUSH=%d "
        "ASYNC_HUD=%d DBUF=%d flush_lines=%u flushbuf=%uB "
        "slots=%u (%u/core) height=%uB cache_bytes=%u tall=%u/%uB colqueue_bytes=%u",
        (int)DOOM_DUAL_CORE_COLUMNS,
        (int)DOOM_RENDER_ASYNC_PLANES,
        (int)DOOM_VIDEO_SYNC_FLUSH,
        (int)DOOM_RENDER_ASYNC_HUD,
        (int)DOOM_VIDEO_DOUBLE_BUFFER,
        (unsigned)DOOM_FLUSH_LINES,
        (unsigned)(SCREENWIDTH * 2u * DOOM_FLUSH_LINES *
                   DOOM_FLUSH_PIPELINE_BUFFERS),
        (unsigned)HAL_PATCH_COLUMN_CACHE_SLOTS,
        (unsigned)(HAL_PATCH_COLUMN_CACHE_SLOTS / HAL_PATCH_COLUMN_CACHE_CORES),
        (unsigned)HAL_PATCH_COLUMN_CACHE_HEIGHT,
        (unsigned)sizeof(s_patch_column_cache),
        (unsigned)HAL_PATCH_TALL_CACHE_SLOTS,
        (unsigned)HAL_PATCH_TALL_CACHE_HEIGHT,
#if DOOM_DUAL_CORE_COLUMNS
        (unsigned)sizeof(s_col_queue));
#else
        0u);
#endif
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
        hal_delay_ms(100u);
    }
}

void __attribute__((noreturn)) abort(void)
{
    void *caller = __builtin_return_address(0);

    render_diag_record_abort(1u, NULL, 0, NULL, NULL, caller);
    derr("\n[fatal] abort caller=%p frame=%lu phase=%u "
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
    derr("\n[fatal] assert %s:%d %s: %s caller=%p "
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
#if DOOM_HIGHRES_SCENE
    return viewheight > 0 ? viewheight : SCREENHEIGHT;
#else
    if (viewheight > 0 && viewheight <= MAIN_VIEWHEIGHT) {
        return viewheight;
    }
    return MAIN_VIEWHEIGHT;
#endif
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

static bool load_patch_decoder(unsigned core, int lump)
{
    hal_patch_cache_t *cache = &s_patch_cache[core];
    uint16_t *decoder = s_patch_decoder[core];
    uint8_t *decoder_tmp = s_patch_decoder_tmp[core];
    uint8_t *prefix_lengths = s_patch_prefix_lengths[core];

    if (cache->valid && cache->lump == lump) {
        return true;
    }

    const patch_t *patch = W_CacheLumpNum(lump, PU_CACHE);
    if (patch == NULL) {
        return false;
    }

    const uint32_t decoder_hwords = patch_decoder_size_needed(patch);
    if (decoder_hwords > HAL_PATCH_DECODER_HWORDS) {
        deb("[render] patch decoder too small: lump=%d need=%lu\n",
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
            decoder_end = th_read_simple_decoder(&bi, decoder,
                                                 HAL_PATCH_DECODER_HWORDS,
                                                 decoder_tmp,
                                                 HAL_PATCH_DECODER_TMP_BYTES);
        } else {
            decoder_end = read_raw_pixels_decoder(&bi, decoder,
                                                  HAL_PATCH_DECODER_HWORDS,
                                                  decoder_tmp,
                                                  HAL_PATCH_DECODER_TMP_BYTES);
        }
    } else if (encoding == 1u) {
        decoder_end = read_raw_pixels_decoder_c3(&bi, decoder,
                                                 HAL_PATCH_DECODER_HWORDS,
                                                 decoder_tmp,
                                                 HAL_PATCH_DECODER_TMP_BYTES);
    } else {
        return false;
    }

    if (decoder_end == NULL || decoder_end > decoder + HAL_PATCH_DECODER_HWORDS) {
        deb("[render] patch decoder overflow: lump=%d\n", lump);
        return false;
    }

    th_make_prefix_length_table(decoder, prefix_lengths);

    const uint16_t width = (uint16_t)patch_width(patch);
    cache->lump = lump;
    cache->patch = patch;
    cache->col_offsets = &((const uint16_t *)patch)[data_index];
    cache->data_index = (data_index + width) * 2u + 2u;
    cache->width = width;
    cache->height = (uint16_t)patch_height(patch);
    cache->encoding = encoding;
    cache->valid = true;
    return true;
}

static uint8_t decode_patch_pixel8(unsigned core, th_bit_input *bi)
{
    const uint16_t *decoder = s_patch_decoder[core];
    const uint8_t *prefix_lengths = s_patch_prefix_lengths[core];

    if (decoder[0] == 0u) {
        return 0;
    }
    if (decoder[0] == 1u) {
        return *(const uint8_t *)(decoder + 1);
    }
    return th_decode_table_special(decoder, prefix_lengths, bi);
}

static uint16_t decode_patch_pixel16(unsigned core, th_bit_input *bi)
{
    const uint16_t *decoder = s_patch_decoder[core];
    const uint8_t *prefix_lengths = s_patch_prefix_lengths[core];

    if (decoder[0] == 0u) {
        return 0;
    }
    if (decoder[0] == 1u) {
        return decoder[1];
    }
    return th_decode_table_special_16(decoder, prefix_lengths, bi);
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

static void patch_tall_cache_reset(void);

static uint8_t *load_flat_pixels(int picnum)
{
    picnum = translate_flat_picnum(picnum);
    if (s_flat_cache_picnum == picnum) {
        return s_work_area;
    }

    patch_tall_cache_reset();

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
        deb("[render] flat decoder overflow: pic=%d\n", picnum);
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

void DoomHAL_RenderQueuedPlanes(void);

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
        // Queue full: flush (render) what is queued so far instead of dropping
        // columns.  Safe because BSP runs front-to-back, so a column's
        // floorclip/ceilingclip (and thus its emitted plane span) is already
        // final by the time it is queued, and visible plane spans never overlap
        // per pixel.  A visplane split across the flush boundary simply renders
        // in two passes (flat served from cache).  No holes, no extra memory.
        DoomHAL_RenderQueuedPlanes();
        if (s_plane_queue_count >= HAL_PLANE_QUEUE_MAX) {
            // RenderQueuedPlanes resets the count; reaching here would be a bug.
            debug_plane_drop();
            return false;
        }
    }

    const unsigned index = s_plane_queue_count++;
    s_plane_queue_x[index] = (uint16_t)x;
    s_plane_queue_yl[index] = (uint16_t)yl;
    s_plane_queue_yh[index] = (uint16_t)yh;
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
        if (s_debug_flat_fail != UINT16_MAX) {
            ++s_debug_flat_fail;
        }
        return false;
    }

    // A single visplane can be visible in TWO disjoint y-ranges within the same
    // column (e.g. the same floor seen near the bottom and again across a pit /
    // ledge in open areas).  Without R_CheckPlane (NO_VISPLANE_GUTS) both ranges
    // share one fd, and the per-column span array holds only one -> the other
    // range was overwritten and left undrawn (sentinel/"black" floor holes).
    // Fix: render in multiple passes; a column that already has a span this pass
    // defers its extra range to the next pass instead of overwriting it.
    bool more_passes = true;
    while (more_passes) {
        for (int i = 0; i < SCREENWIDTH; ++i) {
            s_plane_span_top[i] = UINT16_MAX;
        }

        int min_x = SCREENWIDTH;
        int max_x = -1;
        int min_y = active_view_height();
        int max_y = -1;
        bool drew_any = false;
        more_passes = false;

        for (unsigned i = 0; i < s_plane_queue_count; ++i) {
            if (plane_queue_done(i) || s_plane_queue_fd[i] != (uint8_t)fd_num) {
                continue;
            }

            const int x = s_plane_queue_x[i];
            if (s_plane_span_top[x] != UINT16_MAX) {
                // Second range for this column: handle in a later pass.
                more_passes = true;
                if (s_debug_plane_overwrite != UINT16_MAX) {
                    ++s_debug_plane_overwrite;
                }
                continue;
            }

            const int yl = s_plane_queue_yl[i];
            const int yh = s_plane_queue_yh[i];

            s_plane_span_top[x] = (uint16_t)yl;
            s_plane_span_bottom[x] = (uint16_t)yh;
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
            drew_any = true;
        }

        if (!drew_any) {
            break; // safety: nothing left to scatter
        }
        if (max_x < min_x || max_y < min_y) {
            continue;
        }

        for (int y = min_y; y <= max_y; ++y) {
            int x = min_x;
            while (x <= max_x) {
                while (x <= max_x &&
                       (s_plane_span_top[x] == UINT16_MAX ||
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
                         s_plane_span_top[x] != UINT16_MAX &&
                         y >= s_plane_span_top[x] &&
                         y <= s_plane_span_bottom[x]);

                draw_flat_span(pl, flat, y, x1, x - 1);
            }
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
            if (s_debug_flat_fail != UINT16_MAX) {
                ++s_debug_flat_fail;
            }
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

void DoomHAL_RenderQueuedPlanesAsyncStart(void)
{
#if DOOM_RENDER_ASYNC_PLANES
    for (;;) {
        hal_critical_section_enter();
        const bool idle =
            s_plane_render_pending == 0u && s_plane_render_busy == 0u;
        if (idle) {
            s_plane_render_pending = 1u;
            s_plane_render_busy = 1u;
        }
        hal_critical_section_exit();
        if (idle) {
            return;
        }
        hal_delay_us(50u);
    }
#else
    DoomHAL_RenderQueuedPlanes();
#endif
}

void DoomHAL_RenderQueuedPlanesAsyncWait(void)
{
#if DOOM_RENDER_ASYNC_PLANES
    bool waited = false;
    const uint32_t wait_start = hal_micros();
    for (;;) {
        hal_critical_section_enter();
        const bool busy =
            s_plane_render_pending != 0u || s_plane_render_busy != 0u;
        hal_critical_section_exit();
        if (!busy) {
            if (waited) {
                const uint32_t elapsed = hal_micros() - wait_start;
                hal_critical_section_enter();
                ++s_plane_render_wait_count;
                s_plane_render_wait_us += elapsed;
                hal_critical_section_exit();
            }
            return;
        }
        waited = true;
        hal_delay_us(50u);
    }
#endif
}

void DoomHAL_RenderQueuedPlanesAsyncStats(uint32_t *done, uint32_t *waits,
                                          uint32_t *wait_us)
{
    hal_critical_section_enter();
    *done = s_plane_render_async_count;
    *waits = s_plane_render_wait_count;
    *wait_us = s_plane_render_wait_us;
    hal_critical_section_exit();
}

static void plane_render_async_poll(void)
{
#if DOOM_RENDER_ASYNC_PLANES
    hal_critical_section_enter();
    const bool pending = s_plane_render_pending != 0u;
    if (pending) {
        s_plane_render_pending = 0u;
    }
    hal_critical_section_exit();

    if (!pending) {
        return;
    }

    DoomHAL_RenderQueuedPlanes();

    hal_critical_section_enter();
    s_plane_render_busy = 0u;
    ++s_plane_render_async_count;
    hal_critical_section_exit();
#endif
}

static bool decode_patch_column_uncached(unsigned core, texturecolumn_t source,
                                         uint8_t *pixels, int *height_out)
{
    hal_patch_cache_t *cache = &s_patch_cache[core];
    int lump;
    uint16_t col;
    if (!resolve_patch_source(source, &lump, &col)) {
        return false;
    }

    if (!load_patch_decoder(core, lump) || cache->height == 0u) {
        return false;
    }
    if (cache->height > HAL_PATCH_COLUMN_MAX_HEIGHT) {
        deb("[render] patch column too tall: lump=%d h=%u\n",
                           lump, (unsigned int)cache->height);
        return false;
    }

    if (col >= cache->width) {
        return false;
    }

    uint16_t col_offset = cache->col_offsets[col];
    if ((col_offset >> 8) == 0xffu) {
        col = col_offset & 0xffu;
        if (col >= cache->width) {
            return false;
        }
        col_offset = cache->col_offsets[col];
    }

    th_bit_input bi;
    if (patch_byte_addressed(cache->patch)) {
        th_bit_input_init(&bi, cache->patch + cache->data_index + col_offset);
    } else {
        th_bit_input_init_bit_offset(&bi, cache->patch + cache->data_index,
                                     col_offset);
    }

    uint8_t prev_pixel = 0;
    for (int y = 0; y < cache->height; ++y) {
        if (cache->encoding == 0u) {
            pixels[y] = decode_patch_pixel8(core, &bi);
        } else {
            const uint16_t decoded = decode_patch_pixel16(core, &bi);
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

    if (cache->height < 128u) {
        const uint8_t edge = pixels[cache->height - 1u];
        for (int y = cache->height; y < 128; ++y) {
            pixels[y] = edge;
        }
        pixels[127] = pixels[0];
    }

    *height_out = cache->height;
    return true;
}

// Decode `count` pixels of one patch column (skipping `skip` leading pixels)
// into dest[0..count).  Reuses the per-core single-patch decoder; the current
// core's s_patch_cache entry is loaded for this patch's lump.
static bool decode_composite_patch_run(unsigned core, int patch_num,
                                       uint8_t pcol, int skip, int count,
                                       uint8_t *dest)
{
    hal_patch_cache_t *cache = &s_patch_cache[core];
    framedrawable_t *fd = lookup_patch(patch_num - firstspritelump);
    if (fd == NULL) {
        return false;
    }
    int lump;
    uint16_t rcol;
    if (!resolve_patch_source(make_drawcolumn(fd, pcol), &lump, &rcol)) {
        return false;
    }
    if (!load_patch_decoder(core, lump) || cache->height == 0u) {
        return false;
    }
    if (rcol >= cache->width) {
        return false;
    }

    uint16_t col_offset = cache->col_offsets[rcol];
    if ((col_offset >> 8) == 0xffu) {
        rcol = col_offset & 0xffu;
        if (rcol >= cache->width) {
            return false;
        }
        col_offset = cache->col_offsets[rcol];
    }

    th_bit_input bi;
    if (patch_byte_addressed(cache->patch)) {
        th_bit_input_init(&bi, cache->patch + cache->data_index + col_offset);
    } else {
        th_bit_input_init_bit_offset(&bi, cache->patch + cache->data_index,
                                     col_offset);
    }

    if (cache->encoding == 0u) {
        for (int j = 0; j < skip; ++j) {
            (void)decode_patch_pixel8(core, &bi);
        }
        for (int j = 0; j < count; ++j) {
            dest[j] = decode_patch_pixel8(core, &bi);
        }
    } else {
        uint8_t prev_pixel = 0;
        for (int j = 0; j < skip; ++j) {
            const uint16_t p = decode_patch_pixel16(core, &bi);
            if (p < 256u) {
                prev_pixel = (uint8_t)p;
            } else {
                const uint16_t d = p & 0xffu;
                if (d >= 7u) {
                    return false;
                }
                prev_pixel = (uint8_t)(prev_pixel + (int)d - 3);
            }
        }
        for (int j = 0; j < count; ++j) {
            const uint16_t p = decode_patch_pixel16(core, &bi);
            if (p < 256u) {
                dest[j] = (uint8_t)p;
            } else {
                const uint16_t d = p & 0xffu;
                if (d >= 7u) {
                    return false;
                }
                dest[j] = (uint8_t)(prev_pixel + (int)d - 3);
            }
            prev_pixel = dest[j];
        }
    }
    return true;
}

// Composite (multi-patch) texture column decode for one column, ported from
// draw_composite_columns() in pd_render.cpp but specialised to a single column
// (no batch render_cols list, no min/max y clipping - the whole column is
// decoded).  memcpy segments copy within this column's own pixel buffer, so a
// single-column decode is exact, not approximate.
static bool decode_composite_column_uncached(unsigned core, int texture_num,
                                             uint8_t col, uint8_t *pixels,
                                             int *height_out)
{
    if (whd_textures == NULL) {
        return false;
    }
    const whdtexture_t *tex = &whd_textures[texture_num];
    const unsigned w = tex->width;
    unsigned hh = tex->height ? (unsigned)tex->height : 256u;
    if (hh > HAL_PATCH_COLUMN_MAX_HEIGHT || col >= w) {
        return false;
    }

    const int pc = tex->patch_count;
    const uint8_t *patch_table =
        &((const uint8_t *)whd_textures)[tex->metdata_offset];
    const uint8_t *metadata = patch_table + pc * 2;

    // Skip the single-patch run table (handled by pd_add_column2()).
    unsigned xx = 0;
    while (xx < w) {
        const unsigned b = *metadata++;
        xx += (b & 0x7fu) + 1u;
        if (b & 0x80u) {
            metadata += 2;
        }
    }

    // Walk composite ranges until we find the one covering this column.
    unsigned base = 0;
    while (base < w) {
        const unsigned limit = base + *metadata++ + 1u;
        if (metadata[0] != 0xffu) {
            if (col >= base && col < limit) {
                int y = 0;
                for (;;) {
                    const int type = metadata[0];
                    const int m1 = metadata[1];
                    if (type & WHD_COL_SEG_EXPLICIT_Y) {
                        y = metadata[2];
                        metadata++;
                    }
                    const int length = 1 + (m1 & 0x7f);
                    int count = length;
                    if (y < 0) {
                        y = 0;
                    }
                    if (y + count > (int)hh) {
                        count = (int)hh - y;
                    }
                    if (type & WHD_COL_SEG_MEMCPY) {
                        const int src_y = metadata[2];
                        if (count > 0 && src_y >= 0) {
                            if (type & WHD_COL_SEG_MEMCPY_IS_BACKWARDS) {
                                for (int yy = count - 1; yy >= 0; --yy) {
                                    pixels[y + yy] = pixels[src_y + yy];
                                }
                            } else {
                                for (int yy = 0; yy < count; ++yy) {
                                    pixels[y + yy] = pixels[src_y + yy];
                                }
                            }
                        }
                        metadata += 3;
                    } else {
                        const int local_patch = type & 0xf;
                        const int patch_num =
                            patch_table[local_patch * 2] |
                            (patch_table[local_patch * 2 + 1] << 8);
                        const uint8_t pcol =
                            (uint8_t)((int)col + (int)metadata[2] - (int)base);
                        const int skip = metadata[3];
                        if (count > 0 &&
                            !decode_composite_patch_run(core, patch_num, pcol,
                                                        skip, count,
                                                        pixels + y)) {
                            return false;
                        }
                        metadata += 4;
                    }
                    y += length;
                    if (m1 & 0x80) {
                        break;
                    }
                }
                if (hh != 128u) {
                    pixels[127] = pixels[0];
                    if (hh < HAL_PATCH_COLUMN_MAX_HEIGHT && hh > 0u) {
                        pixels[hh] = pixels[hh - 1u];
                    }
                }
                *height_out = (int)hh;
                return true;
            }
            // Column not in this range: skip its segments.
            int last;
            do {
                last = metadata[1] & 0x80;
                const int has_y =
                    (metadata[0] & WHD_COL_SEG_EXPLICIT_Y) ? 1 : 0;
                if (metadata[0] & 0x80) {
                    metadata += 3 + has_y;
                } else {
                    metadata += 4 + has_y;
                }
            } while (!last);
        } else {
            metadata += 2;
        }
        base = limit;
    }
    return false;
}

static unsigned patch_column_cache_hash(int lump, uint16_t col)
{
    uint32_t key = (uint32_t)lump;
    key ^= (uint32_t)col * 40503u;
    key *= 2654435761u;
    key ^= key >> 16;
    return key & (HAL_PATCH_COLUMN_CACHE_HASH_SIZE - 1u);
}

#if DOOM_DUAL_CORE_COLUMNS
static unsigned patch_column_cache_core(void)
{
    return (unsigned)(sio_hw->cpuid & 1u);
}
#else
static unsigned patch_column_cache_core(void)
{
    return 0u;
}
#endif

static unsigned patch_column_cache_range_start(unsigned core)
{
#if DOOM_DUAL_CORE_COLUMNS
    return core * (HAL_PATCH_COLUMN_CACHE_SLOTS / HAL_PATCH_COLUMN_CACHE_CORES);
#else
    (void)core;
    return 0u;
#endif
}

static unsigned patch_column_cache_range_end(unsigned core)
{
#if DOOM_DUAL_CORE_COLUMNS
    if (core + 1u == HAL_PATCH_COLUMN_CACHE_CORES) {
        return HAL_PATCH_COLUMN_CACHE_SLOTS;
    }
    return (core + 1u) *
           (HAL_PATCH_COLUMN_CACHE_SLOTS / HAL_PATCH_COLUMN_CACHE_CORES);
#else
    (void)core;
    return HAL_PATCH_COLUMN_CACHE_SLOTS;
#endif
}

static uint8_t *patch_column_cache_scratch(unsigned core)
{
#if DOOM_DUAL_CORE_COLUMNS
    return s_patch_column_uncached[core];
#else
    (void)core;
    return s_patch_column_uncached;
#endif
}

static bool patch_column_cache_slot_matches(unsigned slot, int lump,
                                            uint16_t col)
{
    return s_patch_column_cache_valid[slot] &&
           s_patch_column_cache_lump[slot] == lump &&
           s_patch_column_cache_col[slot] == col;
}

static void patch_tall_cache_record_height(int height)
{
    if (height <= 160) {
        if (s_debug_patch_tall_height_le160 != UINT16_MAX) {
            ++s_debug_patch_tall_height_le160;
        }
    } else if (height <= 192) {
        if (s_debug_patch_tall_height_le192 != UINT16_MAX) {
            ++s_debug_patch_tall_height_le192;
        }
    } else if (height <= 224) {
        if (s_debug_patch_tall_height_le224 != UINT16_MAX) {
            ++s_debug_patch_tall_height_le224;
        }
    } else if (s_debug_patch_tall_height_gt224 != UINT16_MAX) {
        ++s_debug_patch_tall_height_gt224;
    }
    if ((unsigned)height > s_debug_patch_tall_height_max) {
        s_debug_patch_tall_height_max = (uint16_t)height;
    }
}

#if DOOM_DUAL_CORE_COLUMNS
static void patch_tall_cache_reset_counted(bool counted)
{
    (void)counted;
}

static void patch_tall_cache_reset(void)
{
}

static bool patch_tall_cache_lookup(unsigned core, int lump, uint16_t col,
                                    const uint8_t **pixels_out,
                                    int *height_out)
{
    (void)core;
    (void)lump;
    (void)col;
    (void)pixels_out;
    (void)height_out;
    return false;
}

static const uint8_t *patch_tall_cache_insert(unsigned core, int lump,
                                              uint16_t col,
                                              const uint8_t *pixels,
                                              int height)
{
    (void)core;
    (void)lump;
    (void)col;
    patch_tall_cache_record_height(height);
    return pixels;
}
#else
static uint8_t *patch_tall_cache_pixels(unsigned slot)
{
    return s_work_area + slot * HAL_PATCH_TALL_CACHE_HEIGHT;
}

static void patch_tall_cache_reset_counted(bool counted)
{
    bool had_entries = false;
    for (unsigned slot = 0; slot < HAL_PATCH_TALL_CACHE_SLOTS; ++slot) {
        if (s_patch_tall_cache_valid[slot]) {
            had_entries = true;
            break;
        }
    }
    if (counted && had_entries &&
        s_debug_patch_tall_cache_resets != UINT16_MAX) {
        ++s_debug_patch_tall_cache_resets;
    }
    memset(s_patch_tall_cache_valid, 0, sizeof(s_patch_tall_cache_valid));
}

static void patch_tall_cache_reset(void)
{
    patch_tall_cache_reset_counted(true);
}

static bool patch_tall_cache_lookup(unsigned core, int lump, uint16_t col,
                                    const uint8_t **pixels_out,
                                    int *height_out)
{
    for (unsigned slot = 0; slot < HAL_PATCH_TALL_CACHE_SLOTS; ++slot) {
        if (s_patch_tall_cache_valid[slot] &&
            s_patch_tall_cache_lump[slot] == lump &&
            s_patch_tall_cache_col[slot] == col) {
            s_patch_tall_cache_age[slot] = s_patch_column_cache_clock[core];
            *pixels_out = patch_tall_cache_pixels(slot);
            *height_out = s_patch_tall_cache_height[slot];
            if (s_debug_patch_cache_hits != UINT16_MAX) {
                ++s_debug_patch_cache_hits;
            }
            if (s_debug_patch_cache_tall_hits != UINT16_MAX) {
                ++s_debug_patch_cache_tall_hits;
            }
            return true;
        }
    }
    return false;
}

static const uint8_t *patch_tall_cache_insert(unsigned core, int lump,
                                              uint16_t col,
                                              const uint8_t *pixels,
                                              int height)
{
    patch_tall_cache_record_height(height);

    if ((unsigned)height > HAL_PATCH_TALL_CACHE_HEIGHT) {
        return pixels;
    }

    unsigned slot = 0;
    uint32_t oldest_age = UINT32_MAX;
    bool found_free = false;
    for (unsigned i = 0; i < HAL_PATCH_TALL_CACHE_SLOTS; ++i) {
        if (!s_patch_tall_cache_valid[i]) {
            slot = i;
            found_free = true;
            break;
        }
        if (s_patch_tall_cache_age[i] < oldest_age) {
            oldest_age = s_patch_tall_cache_age[i];
            slot = i;
        }
    }
    if (!found_free && s_debug_patch_tall_cache_evictions != UINT16_MAX) {
        ++s_debug_patch_tall_cache_evictions;
    }

    uint8_t *dest = patch_tall_cache_pixels(slot);
    memcpy(dest, pixels, (size_t)height);
    s_patch_tall_cache_lump[slot] = lump;
    s_patch_tall_cache_col[slot] = col;
    s_patch_tall_cache_height[slot] = (uint16_t)height;
    s_patch_tall_cache_age[slot] = s_patch_column_cache_clock[core];
    s_patch_tall_cache_valid[slot] = 1u;
    s_flat_cache_picnum = -1;
    return dest;
}
#endif

static bool decode_patch_column(texturecolumn_t source,
                                const uint8_t **pixels_out, int *height_out)
{
    const unsigned core = patch_column_cache_core();
    const unsigned slot_begin = patch_column_cache_range_start(core);
    const unsigned slot_end = patch_column_cache_range_end(core);
    uint8_t *scratch = patch_column_cache_scratch(core);
    int lump;
    uint16_t col;
    // Multi-patch (composite) texture column: resolve_patch_source() rejects it
    // (patch_count != 0), so route to the composite decoder.  Use a distinct
    // negative cache key range so it never collides with real patch lumps.
    const bool composite =
        (source.real_id >= 0 && whd_textures != NULL &&
         whd_textures[source.real_id].patch_count != 0);
    if (composite) {
        lump = -(int)source.real_id - 2;
        col = source.col;
    } else if (!resolve_patch_source(source, &lump, &col)) {
        return false;
    }

    if (++s_patch_column_cache_clock[core] == 0u) {
        for (unsigned i = slot_begin; i < slot_end; ++i) {
            s_patch_column_cache_age[i] = 0u;
        }
        s_patch_column_cache_clock[core] = 1u;
    }

    const unsigned hash = patch_column_cache_hash(lump, col);
    const unsigned hinted_slot = s_patch_column_cache_hash[core][hash];
    if (hinted_slot > 0u) {
        const unsigned slot = slot_begin + hinted_slot - 1u;
        if (slot < slot_end &&
            patch_column_cache_slot_matches(slot, lump, col)) {
            s_patch_column_cache_age[slot] = s_patch_column_cache_clock[core];
            *pixels_out = s_patch_column_cache[slot];
            *height_out = s_patch_column_cache_height[slot];
            if (s_debug_patch_cache_hits != UINT16_MAX) {
                ++s_debug_patch_cache_hits;
            }
            return true;
        }
    }

    for (unsigned slot = slot_begin; slot < slot_end; ++slot) {
        if (patch_column_cache_slot_matches(slot, lump, col)) {
            s_patch_column_cache_age[slot] = s_patch_column_cache_clock[core];
            s_patch_column_cache_hash[core][hash] =
                (uint8_t)(slot - slot_begin + 1u);
            *pixels_out = s_patch_column_cache[slot];
            *height_out = s_patch_column_cache_height[slot];
            if (s_debug_patch_cache_hits != UINT16_MAX) {
                ++s_debug_patch_cache_hits;
            }
            return true;
        }
    }

    if (patch_tall_cache_lookup(core, lump, col, pixels_out, height_out)) {
        return true;
    }

    int decoded_height = 0;
    const bool ok = composite
        ? decode_composite_column_uncached(core, source.real_id, (uint8_t)col,
                                           scratch, &decoded_height)
        : decode_patch_column_uncached(core, source, scratch, &decoded_height);
    if (!ok) {
        return false;
    }

    if ((unsigned)decoded_height > HAL_PATCH_COLUMN_CACHE_HEIGHT) {
        *pixels_out = patch_tall_cache_insert(core, lump, col, scratch,
                                              decoded_height);
        *height_out = decoded_height;
        if (s_debug_patch_cache_misses != UINT16_MAX) {
            ++s_debug_patch_cache_misses;
        }
        if (s_debug_patch_cache_uncached_tall != UINT16_MAX) {
            ++s_debug_patch_cache_uncached_tall;
        }
        return true;
    }

    unsigned slot = 0;
    uint32_t oldest_age = UINT32_MAX;
    for (unsigned i = slot_begin; i < slot_end; ++i) {
        if (!s_patch_column_cache_valid[i]) {
            slot = i;
            break;
        }
        if (s_patch_column_cache_age[i] < oldest_age) {
            oldest_age = s_patch_column_cache_age[i];
            slot = i;
        }
    }

    memcpy(s_patch_column_cache[slot], scratch, (size_t)decoded_height);
    s_patch_column_cache_lump[slot] = lump;
    s_patch_column_cache_col[slot] = col;
    s_patch_column_cache_height[slot] = (uint16_t)decoded_height;
    s_patch_column_cache_age[slot] = s_patch_column_cache_clock[core];
    s_patch_column_cache_valid[slot] = 1u;
    s_patch_column_cache_hash[core][hash] =
        (uint8_t)(slot - slot_begin + 1u);
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
                                          int source_height,
                                          bool tile)
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

    // Solid wall tiers tile vertically when the wall is taller than the
    // texture; sprites/masked columns clamp.  The two only differ for
    // out-of-range source_y, which happens only on tall solid walls.
    const unsigned uheight = (unsigned)source_height;
    const bool pow2 = (uheight & (uheight - 1u)) == 0u;
    const int height_mask = source_height - 1;

    pixel_t *dest = I_VideoBuffer + yl * SCREENWIDTH + x;
    if (fracstep == 0) {
        fracstep = FRACUNIT;
    }
    fixed_t frac = texturemid + (yl - centery) * fracstep;

    if (tile && pow2) {
        for (int y = yl; y <= yh; ++y) {
            const int source_y = (frac >> FRACBITS) & height_mask;
            *dest = map[source_pixels[source_y]];
            dest += SCREENWIDTH;
            frac += fracstep;
        }
    } else if (tile) {
        for (int y = yl; y <= yh; ++y) {
            int source_y = frac >> FRACBITS;
            source_y %= source_height;
            if (source_y < 0) {
                source_y += source_height;
            }
            *dest = map[source_pixels[source_y]];
            dest += SCREENWIDTH;
            frac += fracstep;
        }
    } else {
        for (int y = yl; y <= yh; ++y) {
            int source_y = frac >> FRACBITS;
            if (source_y < 0) {
                source_y = 0;
            } else if (source_y >= source_height) {
                source_y = source_height - 1;
            }
            *dest = map[source_pixels[source_y]];
            dest += SCREENWIDTH;
            frac += fracstep;
        }
    }

    return true;
}

// Variant taking an explicit colormap, so a deferred (queued) column can be
// drawn later with the colormap captured at enqueue time instead of the global.
static bool draw_textured_vertical_map(int x, int yl, int yh, fixed_t texturemid,
                                       fixed_t fracstep, texturecolumn_t source,
                                       bool tile, const lighttable_t *map)
{
    const uint8_t *source_pixels = NULL;
    int source_height = 0;

    if (!decode_patch_column(source, &source_pixels, &source_height)) {
        return false;
    }

    return draw_textured_vertical_pixels(x, yl, yh, texturemid, fracstep, map,
                                         source_pixels, source_height, tile);
}

static bool draw_textured_vertical(int x, int yl, int yh, fixed_t texturemid,
                                   fixed_t fracstep, texturecolumn_t source,
                                   bool tile)
{
    return draw_textured_vertical_map(x, yl, yh, texturemid, fracstep, source,
                                      tile, current_column_colormap());
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
    DoomVideo_BeginRenderFrame();

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
        uint32_t hud_async_done = 0;
        uint32_t hud_async_waits = 0;
        uint32_t hud_async_wait_us = 0;
        uint16_t sprite_seg_overflow = 0;
        uint8_t sprite_seg_max = 0;
        uint32_t t_bsp = 0, t_planes = 0, t_masked = 0;
        uint32_t t_psprite = 0, t_hud = 0;
        uint32_t plane_async_done = 0, plane_async_waits = 0;
        uint32_t plane_async_wait_us = 0;
        uint32_t col_async_done = 0, col_async_waits = 0;
        uint32_t col_async_wait_us = 0;
        uint32_t dbuf_swaps = 0, dbuf_waits = 0, dbuf_wait_us = 0;
#if JASZCZURHAL_PORT
        DoomRenderSpriteDiag_Get(&sprite_seen, &sprite_projected,
                                 &sprite_queued, &sprite_drawn);
        DoomRenderSpriteSegDiag_Get(&sprite_seg_overflow, &sprite_seg_max);
        DoomRenderTimeDiag_Get(&t_bsp, &t_planes, &t_masked, &t_psprite,
                               &t_hud);
        col_render_async_stats(&col_async_done, &col_async_waits,
                               &col_async_wait_us);
        DoomHAL_RenderQueuedPlanesAsyncStats(&plane_async_done,
                                             &plane_async_waits,
                                             &plane_async_wait_us);
        DoomRenderOcclusionDiag_Get(&occluder_columns, &occluder_clipped);
        DoomVideo_GetAsyncFlushStats(&async_flushes, &async_waits,
                                     &async_wait_us);
        DoomVideo_GetDoubleBufferStats(&dbuf_swaps, &dbuf_waits,
                                       &dbuf_wait_us);
        DoomVideo_GetAsyncHudStats(&hud_async_done, &hud_async_waits,
                                   &hud_async_wait_us);
#endif
        // FPS x10 over the frames since the previous log line.
        const uint32_t now_ms = hal_millis();
        unsigned fps_x10 = 0;
        if (s_fps_last_ms != 0u && now_ms > s_fps_last_ms) {
            const uint32_t dframes =
                (uint32_t)s_render_frame - s_fps_last_frame;
            fps_x10 = (unsigned)((dframes * 10000u) / (now_ms - s_fps_last_ms));
        }
        s_fps_last_ms = now_ms;
        s_fps_last_frame = (uint32_t)s_render_frame;

        // Direct symptom measure: count genuinely UNDRAWN pixels (still holding
        // the sentinel clear value) in the just-completed frame, split into
        // Y-bands: top=ceiling/sky, mid=walls, bot=floor.  Using the sentinel
        // (not 0) avoids counting legitimate black texture pixels.
        unsigned black = 0, black_top = 0, black_mid = 0, black_bot = 0;
#if DOOM_RENDER_BLACK_DIAG
        {
            const int vh = active_view_height();
            const int third = vh / 3;
            for (int y = 0; y < vh; ++y) {
                const pixel_t *row = I_VideoBuffer + y * SCREENWIDTH;
                unsigned rowblack = 0;
                for (int x = 0; x < SCREENWIDTH; ++x) {
                    if (row[x] == DOOM_UNDRAWN_SENTINEL) {
                        ++rowblack;
                    }
                }
                black += rowblack;
                if (y < third) {
                    black_top += rowblack;
                } else if (y < 2 * third) {
                    black_mid += rowblack;
                } else {
                    black_bot += rowblack;
                }
            }
        }
#endif

        deb("[render] frame=%lu fps=%u.%u cols=%u planes=%u pdrop=%u "
                           "masked=%u texfail=%u flatfail=%u black=%u/%u/%u/%u "
                           "povr=%u sseg=%u/%u tus=%lu/%lu/%lu/%lu/%lu "
                           "pcache=%u/%u/%u/%u tall=%u/%u "
                           "tallh=%u/%u/%u/%u/%u "
                           "spr=%u/%u/%u/%u occ=%u/%u "
                           "ccol=%u/%u/%u/%u "
                           "casync=%lu/%lu/%lu "
                           "pasync=%lu/%lu/%lu "
                           "hasync=%lu/%lu/%lu "
                           "flush=%lu/%lu/%lu "
                           "dbuf=%lu/%lu/%lu free_heap=%lu\n",
                           (unsigned long)s_render_frame,
                           fps_x10 / 10u, fps_x10 % 10u, s_debug_columns,
                           s_debug_planes, s_debug_plane_drops,
                           s_debug_masked_columns,
                           s_debug_tex_fail,
                           s_debug_flat_fail,
                           black, black_top, black_mid, black_bot,
                           s_debug_plane_overwrite,
                           sprite_seg_overflow, (unsigned)sprite_seg_max,
                           (unsigned long)t_bsp, (unsigned long)t_planes,
                           (unsigned long)t_masked,
                           (unsigned long)t_psprite, (unsigned long)t_hud,
                           s_debug_patch_cache_hits,
                           s_debug_patch_cache_misses,
                           s_debug_patch_cache_uncached_tall,
                           s_debug_patch_cache_tall_hits,
                           s_debug_patch_tall_cache_resets,
                           s_debug_patch_tall_cache_evictions,
                           s_debug_patch_tall_height_le160,
                           s_debug_patch_tall_height_le192,
                           s_debug_patch_tall_height_le224,
                           s_debug_patch_tall_height_gt224,
                           s_debug_patch_tall_height_max,
                           sprite_seen, sprite_projected, sprite_queued,
                           sprite_drawn,
                           occluder_columns, occluder_clipped,
                           s_debug_col_queue_queued,
                           s_debug_col_queue_inline,
                           s_debug_col_queue_left,
                           s_debug_col_queue_right,
                           (unsigned long)col_async_done,
                           (unsigned long)col_async_waits,
                           (unsigned long)(col_async_wait_us / 1000u),
                           (unsigned long)plane_async_done,
                           (unsigned long)plane_async_waits,
                           (unsigned long)(plane_async_wait_us / 1000u),
                           (unsigned long)hud_async_done,
                           (unsigned long)hud_async_waits,
                           (unsigned long)(hud_async_wait_us / 1000u),
                           (unsigned long)async_flushes,
                           (unsigned long)async_waits,
                           (unsigned long)(async_wait_us / 1000u),
                           (unsigned long)dbuf_swaps,
                           (unsigned long)dbuf_waits,
                           (unsigned long)(dbuf_wait_us / 1000u),
                           (unsigned long)hal_get_free_heap());
    }

#if DOOM_RENDER_SENTINEL_CLEAR
    memset(I_VideoBuffer, DOOM_UNDRAWN_SENTINEL,
           SCREENWIDTH * active_view_height());
#else
    if (gamestate != GS_LEVEL || automapactive) {
        memset(I_VideoBuffer, DOOM_UNDRAWN_SENTINEL,
               SCREENWIDTH * active_view_height());
    }
#endif
    reset_framedrawables();
    s_debug_columns = 0;
    s_debug_planes = 0;
    s_debug_plane_drops = 0;
    s_debug_masked_columns = 0;
    s_debug_patch_cache_hits = 0;
    s_debug_patch_cache_misses = 0;
    s_debug_patch_cache_uncached_tall = 0;
    s_debug_patch_cache_tall_hits = 0;
    s_debug_patch_tall_cache_resets = 0;
    s_debug_patch_tall_cache_evictions = 0;
    s_debug_patch_tall_height_le160 = 0;
    s_debug_patch_tall_height_le192 = 0;
    s_debug_patch_tall_height_le224 = 0;
    s_debug_patch_tall_height_gt224 = 0;
    s_debug_patch_tall_height_max = 0;
    s_debug_col_queue_queued = 0;
    s_debug_col_queue_inline = 0;
    s_debug_col_queue_left = 0;
    s_debug_col_queue_right = 0;
    s_debug_tex_fail = 0;
    s_debug_flat_fail = 0;
    s_debug_plane_overwrite = 0;
    s_plane_queue_count = 0;
#if DOOM_DUAL_CORE_COLUMNS
    s_col_queue_count = 0u;
    s_col_render_busy = 0u;
    s_col_render_pending = 0u;
#endif
    patch_tall_cache_reset_counted(false);
    ++s_render_frame;
    s_render_diag.free_heap = hal_get_free_heap();
    render_diag_touch(1u);
}

// Decode+draw one wall/sky/midtex column with explicit params and colormap,
// plus the debug fallback and counters.  Shared by the inline path and the
// deferred queue drain.
static void draw_wall_column(int x, int yl, int yh, fixed_t texturemid,
                             fixed_t iscale, texturecolumn_t source, bool tile,
                             uint8_t type, const lighttable_t *map)
{
    const bool textured = draw_textured_vertical_map(x, yl, yh, texturemid,
                                                     iscale, source, tile, map);

    s_render_diag.last_type = type;
    s_render_diag.last_x = (int16_t)x;
    s_render_diag.last_yl = (int16_t)yl;
    s_render_diag.last_yh = (int16_t)yh;
    render_diag_touch(2u);
    if (!textured) {
        const pixel_t color =
            debug_color(column_base_color((pd_column_type)type), true);
        draw_debug_vertical(x, yl, yh, color);
        if (s_debug_tex_fail != UINT16_MAX) {
            ++s_debug_tex_fail;
        }
    }
    if (s_debug_columns != UINT16_MAX) {
        ++s_debug_columns;
    }
}

#if DOOM_DUAL_CORE_COLUMNS
static void draw_col_entry(const doom_col_entry_t *e)
{
    draw_wall_column(e->x, e->yl, e->yh, e->texturemid, e->iscale, e->source,
                     e->tile != 0u, e->type, e->map);
}

static void col_render_async_start(void)
{
    if (s_col_queue_count == 0u) {
        return;
    }

    for (;;) {
        hal_critical_section_enter();
        const bool idle =
            s_col_render_pending == 0u && s_col_render_busy == 0u;
        if (idle) {
            s_col_render_pending = 1u;
            s_col_render_busy = 1u;
        }
        hal_critical_section_exit();
        if (idle) {
            return;
        }
        hal_delay_us(50u);
    }
}

static void col_render_async_wait(void)
{
    bool waited = false;
    const uint32_t wait_start = hal_micros();
    for (;;) {
        hal_critical_section_enter();
        const bool busy =
            s_col_render_pending != 0u || s_col_render_busy != 0u;
        hal_critical_section_exit();
        if (!busy) {
            if (waited) {
                const uint32_t elapsed = hal_micros() - wait_start;
                hal_critical_section_enter();
                ++s_col_render_wait_count;
                s_col_render_wait_us += elapsed;
                hal_critical_section_exit();
            }
            return;
        }
        waited = true;
        hal_delay_us(50u);
    }
}

void pd_flush_columns(void)
{
    col_render_async_start();
    col_render_async_wait();
    s_col_queue_count = 0u;
}

static void col_render_async_poll(void)
{
    hal_critical_section_enter();
    const bool pending = s_col_render_pending != 0u;
    if (pending) {
        s_col_render_pending = 0u;
    }
    hal_critical_section_exit();

    if (!pending) {
        return;
    }

    for (unsigned i = 0; i < s_col_queue_count; ++i) {
        draw_col_entry(&s_col_queue[i]);
    }

    hal_critical_section_enter();
    s_col_render_busy = 0u;
    s_col_render_async_count += s_col_queue_count;
    hal_critical_section_exit();
}

static void col_render_async_stats(uint32_t *done, uint32_t *waits,
                                   uint32_t *wait_us)
{
    hal_critical_section_enter();
    *done = s_col_render_async_count;
    *waits = s_col_render_wait_count;
    *wait_us = s_col_render_wait_us;
    hal_critical_section_exit();
}
#else
static void col_render_async_poll(void)
{
}

static void col_render_async_stats(uint32_t *done, uint32_t *waits,
                                   uint32_t *wait_us)
{
    *done = 0u;
    *waits = 0u;
    *wait_us = 0u;
}
#endif

void pd_add_column(pd_column_type type)
{
    // Solid wall tiers (top/mid/bottom) tile vertically; masked columns do not.
    const bool tile = (type == PDCOL_TOP || type == PDCOL_MID ||
                       type == PDCOL_BOTTOM);
#if DOOM_DUAL_CORE_COLUMNS
    if (dc_x < DOOM_COL_SPLIT_X) {
        // The left half belongs to core0 anyway. Draw it inline so the bounded
        // queue is reserved for columns that core1 can actually offload.
        if (s_debug_col_queue_inline != UINT16_MAX) {
            ++s_debug_col_queue_inline;
        }
        draw_wall_column(dc_x, dc_yl, dc_yh, dc_texturemid, dc_iscale,
                         dc_source, tile, (uint8_t)type,
                         current_column_colormap());
        return;
    }

    if (s_col_queue_count >= DOOM_COL_QUEUE_MAX) {
        if (s_debug_col_queue_inline != UINT16_MAX) {
            ++s_debug_col_queue_inline;
        }
        draw_wall_column(dc_x, dc_yl, dc_yh, dc_texturemid, dc_iscale,
                         dc_source, tile, (uint8_t)type,
                         current_column_colormap());
        return;
    }

    doom_col_entry_t *e = &s_col_queue[s_col_queue_count++];
    e->texturemid = dc_texturemid;
    e->iscale = dc_iscale;
    e->source = dc_source;
    e->map = current_column_colormap();
    e->x = (int16_t)dc_x;
    e->yl = (int16_t)dc_yl;
    e->yh = (int16_t)dc_yh;
    e->tile = tile ? 1u : 0u;
    e->type = (uint8_t)type;

    if (s_debug_col_queue_queued != UINT16_MAX) {
        ++s_debug_col_queue_queued;
    }
    if (s_debug_col_queue_right != UINT16_MAX) {
        ++s_debug_col_queue_right;
    }
#else
    draw_wall_column(dc_x, dc_yl, dc_yh, dc_texturemid, dc_iscale, dc_source,
                     tile, (uint8_t)type, current_column_colormap());
#endif
}

void pd_add_masked_columns(const pd_masked_segment_t *segments, int seg_count)
{
    const pixel_t color = debug_color(column_base_color(PDCOL_MASKED), true);

    for (int i = 0; i < seg_count; ++i) {
        const pd_masked_segment_t *seg = &segments[i];
        const fixed_t texturemid =
            dc_texturemid - ((fixed_t)seg->source_y << FRACBITS);
        const bool textured =
            draw_textured_vertical(dc_x, seg->yl, seg->yh,
                                   texturemid, dc_iscale, dc_source, false);

        s_render_diag.last_type = (uint8_t)PDCOL_MASKED;
        s_render_diag.last_x = (int16_t)dc_x;
        s_render_diag.last_yl = (int16_t)seg->yl;
        s_render_diag.last_yh = (int16_t)seg->yh;
        render_diag_touch(3u);
        if (!textured) {
            draw_debug_vertical(dc_x, seg->yl, seg->yh, color);
            if (s_debug_tex_fail != UINT16_MAX) {
                ++s_debug_tex_fail;
            }
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
