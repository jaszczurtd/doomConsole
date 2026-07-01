/*
 * JaszczurHAL TFT display backend.
 *
 * Doom renders into its logical indexed framebuffer.  This file
 * converts one display line at a time to big-endian RGB565 and streams it
 * through the HAL TFT write-window API, centered on the configured TFT.
 */

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <JaszczurHAL.h>
#include <utils/tools_api.h>

#include <hal/hal_display.h>
#include <hal/hal_sync.h>
#include <hal/hal_system.h>

/* Diagnostic only: confirm the real system/peripheral clocks so we can tell
 * whether the TFT SPI is actually running at the requested rate. */
#if __has_include(<hardware/clocks.h>)
#include <hardware/clocks.h>
#define DOOM_HAVE_PICO_CLOCKS 1
#endif

#include "doomtype.h"
#include "i_video.h"
#include "picodoom.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"
#include "doom/am_map.h"
#include "doom/d_main.h"
#include "doom/doomstat.h"
#include "doom/f_finale.h"
#include "doom/f_wipe.h"
#include "doom/hu_stuff.h"
#include "doom/m_menu.h"
#include "doom/st_stuff.h"
#include "doom/wi_stuff.h"
#include "doom_main_config.h"

void DoomRenderDiag_MarkPhase(uint8_t phase);
extern uint32_t doom_render_us_hud;

should_be_const constcharstar video_driver = "jaszczurhal-tft";
boolean screenvisible = true;
boolean screensaver_mode = false;
isb_int8_t usegamma = 0;
int screen_width = SCREENWIDTH;
int screen_height = SCREENHEIGHT;
int fullscreen = 0;
int aspect_ratio_correct = 0;
int integer_scaling = 0;
int vga_porch_flash = 0;
int force_software_renderer = 0;
should_be_const constcharstar window_position = "";

#if DOOM_VIDEO_DOUBLE_BUFFER && !PICO_RP2350
#error DOOM_VIDEO_DOUBLE_BUFFER is only supported on RP2350 by this port.
#endif

#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
int vanilla_keyboard_mapping = true;
#endif

#if DOOM_VIDEO_DOUBLE_BUFFER
static pixel_t s_video_buffers[2][SCREENWIDTH * SCREENHEIGHT];
static uint8_t s_render_buffer_index = 0;
#define ACTIVE_VIDEO_BUFFER s_video_buffers[s_render_buffer_index]
pixel_t *I_VideoBuffer = s_video_buffers[0];
#else
static pixel_t s_video_buffer[SCREENWIDTH * SCREENHEIGHT];
#define ACTIVE_VIDEO_BUFFER s_video_buffer
pixel_t *I_VideoBuffer = ACTIVE_VIDEO_BUFFER;
#endif

#if DOOM_TINY
uint8_t next_video_type = VIDEO_TYPE_NONE;
uint8_t next_frame_index = 0;
uint8_t next_overlay_index = 0;
#if !DEMO1_ONLY
uint8_t *next_video_scroll = NULL;
#endif
int16_t *wipe_yoffsets_raw = NULL;
uint8_t *wipe_yoffsets = NULL;
uint32_t *wipe_linelookup = NULL;
wipestate_t wipestate = WIPESTATE_NONE;
volatile uint8_t wipe_min = 0;
#endif
pre_wipe_state_t pre_wipe_state = PRE_WIPE_NONE;

volatile uint8_t interp_in_use = 0;
int pd_flag = 0;
fixed_t pd_scale = FRACUNIT;

static uint16_t s_palette_rgb565_be[256];
static uint8_t s_palette_rgb888[256][3];
// Convert/transfer several lines per DMA call to amortise the per-call DMA
// setup + blocking-wait overhead (200 single-line DMAs per frame was costly).
static uint16_t
    s_line_rgb565_be[DOOM_FLUSH_PIPELINE_BUFFERS]
                     [SCREENWIDTH * DOOM_FLUSH_LINES];
static bool s_palette_ready = false;
static bool s_display_ready = false;
static int s_active_palette_num = 0;
static int s_next_palette_num = 0;
static int s_palette_gamma = -1;
static int s_display_x = 0;
static int s_display_y = 0;
static volatile uint8_t s_flush_pending = 0;
static volatile uint8_t s_flush_busy = 0;
static const pixel_t * volatile s_flush_buffer = NULL;
static volatile uint32_t s_async_flush_count = 0;
static volatile uint32_t s_async_wait_count = 0;
static volatile uint32_t s_async_wait_us = 0;

#if DOOM_RENDER_ASYNC_HUD
static volatile uint8_t s_hud_pending = 0;
static volatile uint8_t s_hud_busy = 0;
static const pixel_t * volatile s_hud_finalize_buffer = NULL;
static volatile uint32_t s_hud_async_count = 0;
static volatile uint32_t s_hud_wait_count = 0;
static volatile uint32_t s_hud_wait_us = 0;
#endif

#if DOOM_VIDEO_DOUBLE_BUFFER
static volatile uint32_t s_double_buffer_swap_count = 0;
static volatile uint32_t s_double_buffer_wait_count = 0;
static volatile uint32_t s_double_buffer_wait_us = 0;

static void select_render_buffer(uint8_t index)
{
    if (index == s_render_buffer_index) {
        return;
    }

    s_render_buffer_index = index;
    I_VideoBuffer = s_video_buffers[s_render_buffer_index];
    V_RestoreBuffer();
}
#endif

static void DoomVideo_DrawFrameOverlay(void);
static void DoomVideo_DrawGameplayOverlayTo(pixel_t *buffer);

#if USE_WHD
static vpatchlist_t s_hal_patch_list[HAL_PATCH_LIST_MAX_ENTRIES + 1];

static void begin_hal_patch_list(void)
{
    s_hal_patch_list[0].header.max = HAL_PATCH_LIST_MAX_ENTRIES;
    V_BeginPatchList(s_hal_patch_list);
}

static void flush_hal_patch_list(void)
{
    V_DrawPatchList(s_hal_patch_list);
    V_EndPatchList();
}
#endif

static int current_gamma(void)
{
    int gamma = usegamma;

    if (gamma < 0) {
        return 0;
    }
    if (gamma > 4) {
        return 4;
    }
    return gamma;
}

static uint16_t rgb_to_rgb565(int r, int g, int b)
{
    return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static uint16_t rgb565_to_be_store_word(uint16_t color)
{
    return (uint16_t)((color << 8) | (color >> 8));
}

static void set_palette_entry(int index, int r, int g, int b, int gamma)
{
    if (gamma > 0) {
        r = gammatable[gamma - 1][r];
        g = gammatable[gamma - 1][g];
        b = gammatable[gamma - 1][b];
    }

    s_palette_rgb888[index][0] = (uint8_t)r;
    s_palette_rgb888[index][1] = (uint8_t)g;
    s_palette_rgb888[index][2] = (uint8_t)b;
    s_palette_rgb565_be[index] = rgb565_to_be_store_word(rgb_to_rgb565(r, g, b));
}

static void build_grayscale_palette(int gamma)
{
    for (int i = 0; i < 256; ++i) {
        set_palette_entry(i, i, i, i, gamma);
    }
}

static void build_palette_from_playpal(int palette_num)
{
    const int gamma = current_gamma();

    if (numlumps == 0) {
        build_grayscale_palette(gamma);
        s_palette_gamma = gamma;
        return;
    }

    const lumpindex_t lump = W_CheckNumForName("PLAYPAL");

    if (lump < 0) {
        build_grayscale_palette(gamma);
        s_active_palette_num = 0;
        s_palette_gamma = gamma;
        s_palette_ready = true;
        return;
    }

    const uint8_t *playpal = W_CacheLumpNum(lump, PU_STATIC);
    const int length = W_LumpLength(lump);
    const bool can_use_direct =
        length >= (palette_num + 1) * 768 && palette_num >= 0;
    const bool can_synthesize = length == 768 && palette_num > 0;

    if (length < 768 || playpal == NULL) {
        build_grayscale_palette(gamma);
        s_active_palette_num = 0;
        s_palette_gamma = gamma;
        s_palette_ready = true;
        return;
    }

    if (can_use_direct || !can_synthesize) {
        const int direct_palette = can_use_direct ? palette_num : 0;
        const uint8_t *doompalette = playpal + direct_palette * 768;

        for (int i = 0; i < 256; ++i) {
            const int r = *doompalette++;
            const int g = *doompalette++;
            const int b = *doompalette++;
            set_palette_entry(i, r, g, b, gamma);
        }
        s_active_palette_num = direct_palette;
    } else {
        int mul;
        int r0;
        int g0;
        int b0;

        if (palette_num < 9) {
            mul = palette_num * 65536 / 9;
            r0 = 255;
            g0 = 0;
            b0 = 0;
        } else if (palette_num < 13) {
            mul = (palette_num - 8) * 65536 / 8;
            r0 = 215;
            g0 = 186;
            b0 = 69;
        } else {
            mul = 65536 / 8;
            r0 = 0;
            g0 = 256;
            b0 = 0;
        }

        const uint8_t *doompalette = playpal;
        for (int i = 0; i < 256; ++i) {
            int r = *doompalette++;
            int g = *doompalette++;
            int b = *doompalette++;

            r += ((r0 - r) * mul) >> 16;
            g += ((g0 - g) * mul) >> 16;
            b += ((b0 - b) * mul) >> 16;
            if (r < 0) r = 0;
            if (g < 0) g = 0;
            if (b < 0) b = 0;
            if (r > 255) r = 255;
            if (g > 255) g = 255;
            if (b > 255) b = 255;
            set_palette_entry(i, r, g, b, gamma);
        }
        s_active_palette_num = palette_num;
    }

    s_palette_gamma = gamma;
    s_palette_ready = true;
}

static void update_palette_if_needed(void)
{
    const int gamma = current_gamma();
    const bool needs_update =
        !s_palette_ready || s_next_palette_num >= 0 || gamma != s_palette_gamma;

    if (!needs_update) {
        return;
    }

    if (s_next_palette_num >= 0) {
        build_palette_from_playpal(s_next_palette_num);
        s_next_palette_num = -1;
    } else {
        build_palette_from_playpal(s_active_palette_num);
    }
}

static void configure_display_window(void)
{
    const int display_w = hal_display_get_width();
    const int display_h = hal_display_get_height();

    s_display_ready = false;
    if (display_w < SCREENWIDTH || display_h < SCREENHEIGHT) {
        return;
    }

    s_display_x = (display_w - SCREENWIDTH) / 2;
    s_display_y = (display_h - SCREENHEIGHT) / 2;
    hal_display_fill_screen(HAL_COLOR_BLACK);
    s_display_ready = true;
}

void I_InitGraphics(void)
{
    deb("[video] I_InitGraphics begin\n");

#if DOOM_VIDEO_DOUBLE_BUFFER
    memset(s_video_buffers, 0, sizeof(s_video_buffers));
    s_render_buffer_index = 0;
#else
    memset(s_video_buffer, 0, sizeof(s_video_buffer));
#endif
    I_VideoBuffer = ACTIVE_VIDEO_BUFFER;
    V_RestoreBuffer();
    s_next_palette_num = 0;

    hal_display_init((uint8_t)DOOM_HAL_TFT_CS_PIN, (uint8_t)DOOM_HAL_TFT_DC_PIN,
                     (uint8_t)DOOM_HAL_TFT_RST_PIN);
    if (!hal_display_configure(DOOM_HAL_TFT_NATIVE_WIDTH,
                               DOOM_HAL_TFT_NATIVE_HEIGHT,
                               HAL_DISPLAY_ROTATION(DOOM_HAL_TFT_ROTATION_DEG),
                               DOOM_HAL_TFT_INVERT,
                               DOOM_HAL_TFT_COLOR_ORDER)) {
        s_display_ready = false;
        deb("[video] I_InitGraphics display configure FAIL\n");
        return;
    }

    configure_display_window();
    deb("[video] I_InitGraphics display=%dx%d window=(%d,%d) "
                       "ready=%s\n",
                       hal_display_get_width(), hal_display_get_height(),
                       s_display_x, s_display_y,
                       s_display_ready ? "OK" : "FAIL");
#ifdef DOOM_HAVE_PICO_CLOCKS
    const uint32_t peri_hz = clock_get_hz(clk_peri);
    deb("[video] clk_sys=%lu Hz clk_peri=%lu Hz "
                       "(TFT SPI requested %lu Hz estimated_actual=%lu Hz)\n",
                       (unsigned long)clock_get_hz(clk_sys),
                       (unsigned long)peri_hz,
                       (unsigned long)DOOM_TFT_SPI_REQUEST_HZ,
                       (unsigned long)DoomEstimateRp2040SpiActualHz(
                           peri_hz, DOOM_TFT_SPI_REQUEST_HZ));
#endif
}

void I_GraphicsCheckCommandLine(void) {}

void I_ShutdownGraphics(void)
{
    s_display_ready = false;
}

void I_SetPaletteNum(int num)
{
    const int requested = num < 0 ? 0 : num;

    if (s_palette_ready && s_next_palette_num < 0 &&
        requested == s_active_palette_num &&
        current_gamma() == s_palette_gamma) {
        return;
    }

    s_next_palette_num = requested;
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0;
    int best_distance = INT_MAX;

    update_palette_if_needed();

    for (int i = 0; i < 256; ++i) {
        const int dr = r - s_palette_rgb888[i][0];
        const int dg = g - s_palette_rgb888[i][1];
        const int db = b - s_palette_rgb888[i][2];
        const int distance = dr * dr + dg * dg + db * db;

        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }

    return best;
}

void I_UpdateNoBlit(void) {}

static void finish_update_buffer(const pixel_t *buffer)
{
    if (!s_display_ready || buffer == NULL) {
        return;
    }

    if (!hal_display_begin_write(s_display_x, s_display_y, SCREENWIDTH,
                                 SCREENHEIGHT)) {
        return;
    }

    unsigned slot = 0u;
    bool dma_active = false;
    for (int y0 = 0; y0 < SCREENHEIGHT; y0 += DOOM_FLUSH_LINES) {
        int lines = SCREENHEIGHT - y0;
        if (lines > DOOM_FLUSH_LINES) {
            lines = DOOM_FLUSH_LINES;
        }

        uint16_t *dst = s_line_rgb565_be[slot];
        for (int ly = 0; ly < lines; ++ly) {
            const pixel_t *src = buffer + (y0 + ly) * SCREENWIDTH;
            for (int x = 0; x < SCREENWIDTH; ++x) {
                *dst++ = s_palette_rgb565_be[src[x]];
            }
        }

#if DOOM_FLUSH_PIPELINE_BUFFERS > 1
        if (dma_active && !hal_display_write_pixels_dma_async_wait()) {
            break;
        }
        if (!hal_display_write_pixels_dma_async_start(
                (const uint8_t *)s_line_rgb565_be[slot],
                (size_t)lines * SCREENWIDTH * 2)) {
            break;
        }
        dma_active = true;
        slot = (slot + 1u) % DOOM_FLUSH_PIPELINE_BUFFERS;
#else
        if (!hal_display_write_pixels_dma(
                (const uint8_t *)s_line_rgb565_be[0],
                (size_t)lines * SCREENWIDTH * 2)) {
            break;
        }
#endif
    }

#if DOOM_FLUSH_PIPELINE_BUFFERS > 1
    if (dma_active) {
        (void)hal_display_write_pixels_dma_async_wait();
    }
#endif
    (void)hal_display_end_write();
}

static bool palette_update_pending(void)
{
    return !s_palette_ready || s_next_palette_num >= 0 ||
           current_gamma() != s_palette_gamma;
}

void DoomVideo_WaitForAsyncFlush(void)
{
    bool waited = false;
    const uint32_t wait_start = hal_micros();

    for (;;) {
        hal_critical_section_enter();
        const bool busy = s_flush_busy != 0u || s_flush_pending != 0u;
        hal_critical_section_exit();

        if (!busy) {
            if (waited) {
                const uint32_t elapsed = hal_micros() - wait_start;

                hal_critical_section_enter();
                ++s_async_wait_count;
                s_async_wait_us += elapsed;
                hal_critical_section_exit();
            }
            return;
        }

        waited = true;
        hal_delay_us(50u);
    }
}

void DoomVideo_BeginRenderFrame(void)
{
#if DOOM_VIDEO_DOUBLE_BUFFER
    const uint32_t wait_start = hal_micros();
    bool waited = false;

    for (;;) {
        const pixel_t *flushing = NULL;
        const pixel_t *finalizing = NULL;
        bool busy = false;

        hal_critical_section_enter();
        busy = s_flush_busy != 0u || s_flush_pending != 0u;
        flushing = s_flush_buffer;
#if DOOM_RENDER_ASYNC_HUD
        if (s_hud_pending != 0u || s_hud_busy != 0u) {
            busy = true;
            finalizing = s_hud_finalize_buffer;
        }
#endif
        hal_critical_section_exit();

        if (!busy ||
            (flushing != I_VideoBuffer && finalizing != I_VideoBuffer)) {
            if (waited) {
                const uint32_t elapsed = hal_micros() - wait_start;

                hal_critical_section_enter();
                ++s_double_buffer_wait_count;
                s_double_buffer_wait_us += elapsed;
                hal_critical_section_exit();
            }
            return;
        }

        const uint8_t other = (uint8_t)(s_render_buffer_index ^ 1u);
        if (flushing != s_video_buffers[other] &&
            finalizing != s_video_buffers[other]) {
            select_render_buffer(other);

            hal_critical_section_enter();
            ++s_double_buffer_swap_count;
            hal_critical_section_exit();
            return;
        }

        waited = true;
        hal_delay_us(50u);
    }
#else
    DoomVideo_WaitForAsyncFlush();
#endif
}

void DoomVideo_Core1Poll(void)
{
    const pixel_t *buffer = NULL;

    hal_critical_section_enter();
    if (s_flush_pending != 0u) {
        buffer = s_flush_buffer;
        s_flush_pending = 0u;
    }
    hal_critical_section_exit();

    if (buffer != NULL) {
        finish_update_buffer(buffer);

        hal_critical_section_enter();
        s_flush_buffer = NULL;
#if DOOM_RENDER_ASYNC_HUD
        if (s_hud_finalize_buffer == NULL) {
            s_flush_busy = 0u;
        }
#else
        s_flush_busy = 0u;
#endif
        ++s_async_flush_count;
        hal_critical_section_exit();
    }

#if DOOM_RENDER_ASYNC_HUD
    const pixel_t *finalize_buffer = NULL;

    hal_critical_section_enter();
    if (s_hud_pending != 0u) {
        finalize_buffer = s_hud_finalize_buffer;
        s_hud_pending = 0u;
        s_hud_busy = 1u;
    }
    hal_critical_section_exit();

    if (finalize_buffer == NULL && s_hud_busy != 0u) {
        DoomVideo_DrawFrameOverlay();

        hal_critical_section_enter();
        s_hud_busy = 0u;
        ++s_hud_async_count;
        hal_critical_section_exit();
    } else if (finalize_buffer != NULL) {
        DoomVideo_DrawGameplayOverlayTo((pixel_t *)finalize_buffer);

        hal_critical_section_enter();
        s_flush_buffer = finalize_buffer;
        hal_critical_section_exit();

        finish_update_buffer(finalize_buffer);

        hal_critical_section_enter();
        s_flush_buffer = NULL;
        s_hud_finalize_buffer = NULL;
        s_hud_busy = 0u;
        s_flush_busy = 0u;
        ++s_hud_async_count;
        ++s_async_flush_count;
        hal_critical_section_exit();
    }
#endif
}

#if DOOM_RENDER_ASYNC_HUD
static void DoomVideo_WaitForAsyncHud(void)
{
    const uint32_t wait_start = hal_micros();
    bool waited = false;

    for (;;) {
        uint8_t pending = 0;
        uint8_t busy = 0;

        hal_critical_section_enter();
        pending = s_hud_pending;
        busy = s_hud_busy;
        hal_critical_section_exit();

        if (pending == 0u && busy == 0u) {
            if (waited) {
                const uint32_t elapsed = hal_micros() - wait_start;

                hal_critical_section_enter();
                ++s_hud_wait_count;
                s_hud_wait_us += elapsed;
                hal_critical_section_exit();
            }
            return;
        }

        waited = true;
        hal_delay_us(20u);
    }
}

static bool DoomVideo_StartAsyncHud(void)
{
    bool started = false;

    hal_critical_section_enter();
    if (s_hud_pending == 0u && s_hud_busy == 0u &&
        s_hud_finalize_buffer == NULL) {
        s_hud_pending = 1u;
        started = true;
    }
    hal_critical_section_exit();

    return started;
}

static bool DoomVideo_StartAsyncFrameFinalize(const pixel_t *buffer)
{
    bool started = false;

    hal_critical_section_enter();
    if (s_hud_pending == 0u && s_hud_busy == 0u &&
        s_hud_finalize_buffer == NULL) {
        s_hud_finalize_buffer = buffer;
        s_hud_pending = 1u;
        s_flush_busy = 1u;
        started = true;
    }
    hal_critical_section_exit();

    return started;
}

void DoomVideo_GetAsyncHudStats(uint32_t *done, uint32_t *waits,
                                uint32_t *wait_us)
{
    hal_critical_section_enter();
    *done = s_hud_async_count;
    *waits = s_hud_wait_count;
    *wait_us = s_hud_wait_us;
    hal_critical_section_exit();
}
#else
void DoomVideo_GetAsyncHudStats(uint32_t *done, uint32_t *waits,
                                uint32_t *wait_us)
{
    *done = 0;
    *waits = 0;
    *wait_us = 0;
}
#endif

void DoomVideo_GetAsyncFlushStats(uint32_t *flushes, uint32_t *waits,
                                  uint32_t *wait_us)
{
    hal_critical_section_enter();
    *flushes = s_async_flush_count;
    *waits = s_async_wait_count;
    *wait_us = s_async_wait_us;
    hal_critical_section_exit();
}

void DoomVideo_GetDoubleBufferStats(uint32_t *swaps, uint32_t *waits,
                                    uint32_t *wait_us)
{
#if DOOM_VIDEO_DOUBLE_BUFFER
    hal_critical_section_enter();
    *swaps = s_double_buffer_swap_count;
    *waits = s_double_buffer_wait_count;
    *wait_us = s_double_buffer_wait_us;
    hal_critical_section_exit();
#else
    *swaps = 0;
    *waits = 0;
    *wait_us = 0;
#endif
}

void I_FinishUpdate(void)
{
    if (!s_display_ready) {
        return;
    }

#if !DOOM_VIDEO_SYNC_FLUSH && DOOM_VIDEO_DOUBLE_BUFFER
    if (palette_update_pending()) {
        DoomVideo_WaitForAsyncFlush();
    }
#endif
    update_palette_if_needed();
#if DOOM_VIDEO_SYNC_FLUSH
    DoomVideo_WaitForAsyncFlush();
    finish_update_buffer(I_VideoBuffer);

    hal_critical_section_enter();
    ++s_async_flush_count;
    hal_critical_section_exit();
#else
    DoomVideo_WaitForAsyncFlush();

    hal_critical_section_enter();
    s_flush_buffer = I_VideoBuffer;
    s_flush_busy = 1u;
    s_flush_pending = 1u;
    hal_critical_section_exit();
#endif
}

void I_BeginRead(void) {}
void I_SetWindowTitle(const char *title) { (void)title; }
void I_CheckIsScreensaver(void) {}
void I_SetGrabMouseCallback(grabmouse_callback_t func) { (void)func; }
void I_DisplayFPSDots(boolean dots_on) { (void)dots_on; }
void I_BindVideoVariables(void) {}
void I_InitWindowTitle(void) {}
void I_InitWindowIcon(void) {}
void I_StartFrame(void) {}
void I_StartTic(void) {}
void I_EnableLoadingDisk(int xoffs, int yoffs) { (void)xoffs; (void)yoffs; }

void I_GetWindowPosition(int *x, int *y, int w, int h)
{
    (void)w;
    (void)h;
    if (x != NULL) *x = 0;
    if (y != NULL) *y = 0;
}

void I_ReadScreen(pixel_t *scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT * sizeof(*scr));
}

static void DoomVideo_DrawFrameOverlay(void)
{
    const uint32_t hud_t0 = hal_micros();

#if USE_WHD
    begin_hal_patch_list();
#endif

    switch (gamestate) {
    case GS_LEVEL:
        if (automapactive) {
            AM_Drawer();
        }
        ST_Drawer(false, true);
        if (gametic) {
            HU_Drawer();
        }
        break;
    case GS_INTERMISSION:
#if !NO_USE_WI
        WI_Drawer();
#endif
        break;
    case GS_FINALE:
        F_Drawer();
        break;
    case GS_DEMOSCREEN:
        D_PageDrawer();
        break;
    default:
        break;
    }

    M_Drawer();

#if USE_WHD
    flush_hal_patch_list();
#endif

    doom_render_us_hud = hal_micros() - hud_t0;
}

static void DoomVideo_DrawGameplayOverlayTo(pixel_t *buffer)
{
    const uint32_t hud_t0 = hal_micros();

    V_SetRestoreBufferOverride(buffer);
    V_RestoreBuffer();

#if USE_WHD
    begin_hal_patch_list();
#endif

    ST_Drawer(false, true);
    if (gametic) {
        HU_Drawer();
    }

#if USE_WHD
    flush_hal_patch_list();
#endif

    V_SetRestoreBufferOverride(NULL);
    V_RestoreBuffer();

    doom_render_us_hud = hal_micros() - hud_t0;
}

static bool DoomVideo_CanFinalizeGameplayAsync(void)
{
#if DOOM_RENDER_ASYNC_HUD && DOOM_VIDEO_DOUBLE_BUFFER && !DOOM_VIDEO_SYNC_FLUSH
    return gamestate == GS_LEVEL && gametic && !automapactive && !menuactive;
#else
    return false;
#endif
}

void pd_end_frame(int wipe_start)
{
    (void)wipe_start;

    DoomRenderDiag_MarkPhase(5u);

#if DOOM_RENDER_ASYNC_HUD
    if (DoomVideo_CanFinalizeGameplayAsync()) {
        if (palette_update_pending()) {
            DoomVideo_WaitForAsyncFlush();
            update_palette_if_needed();
        }
        if (DoomVideo_StartAsyncFrameFinalize(I_VideoBuffer)) {
            DoomRenderDiag_MarkPhase(7u);
            return;
        }
    }
#endif

#if DOOM_RENDER_ASYNC_HUD
    if (DoomVideo_StartAsyncHud()) {
        DoomVideo_WaitForAsyncHud();
    } else
#endif
    {
        DoomVideo_DrawFrameOverlay();
    }

    DoomRenderDiag_MarkPhase(6u);
    I_FinishUpdate();
    DoomRenderDiag_MarkPhase(7u);
}
