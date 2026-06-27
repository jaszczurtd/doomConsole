/*
 * JaszczurHAL TFT display backend.
 *
 * Doom still renders into its logical 320x200 indexed framebuffer.  This file
 * converts one display line at a time to big-endian RGB565 and streams it
 * through the HAL TFT write-window API, centered on a landscape 320x240 TFT.
 */

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <hal/hal_display.h>
#include <hal/hal_sync.h>
#include <hal/hal_system.h>

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
#include "jaszczurhal/doom_boot_log.h"

void DoomRenderDiag_MarkPhase(uint8_t phase);

#ifndef DOOM_HAL_TFT_CS_PIN
#define DOOM_HAL_TFT_CS_PIN 17u
#endif

#ifndef DOOM_HAL_TFT_DC_PIN
#define DOOM_HAL_TFT_DC_PIN 20u
#endif

#ifndef DOOM_HAL_TFT_RST_PIN
#define DOOM_HAL_TFT_RST_PIN 21u
#endif

#ifndef DOOM_HAL_TFT_NATIVE_WIDTH
#define DOOM_HAL_TFT_NATIVE_WIDTH 240
#endif

#ifndef DOOM_HAL_TFT_NATIVE_HEIGHT
#define DOOM_HAL_TFT_NATIVE_HEIGHT 320
#endif

#ifndef DOOM_HAL_TFT_ROTATION_DEG
#define DOOM_HAL_TFT_ROTATION_DEG 90
#endif

#ifndef DOOM_HAL_TFT_INVERT
#define DOOM_HAL_TFT_INVERT HAL_DISPLAY_INVERT_OFF
#endif

#ifndef DOOM_HAL_TFT_COLOR_ORDER
#define DOOM_HAL_TFT_COLOR_ORDER HAL_DISPLAY_COLOR_ORDER_RGB
#endif

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

#if !USE_VANILLA_KEYBOARD_MAPPING_ONLY
int vanilla_keyboard_mapping = true;
#endif

static pixel_t s_video_buffer[SCREENWIDTH * SCREENHEIGHT];
pixel_t *I_VideoBuffer = s_video_buffer;

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

static uint16_t s_palette_rgb565[256];
static uint8_t s_palette_rgb888[256][3];
static uint8_t s_line_rgb565_be[SCREENWIDTH * 2];
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

#if USE_WHD
#define HAL_PATCH_LIST_MAX_ENTRIES VPATCHLIST_COUNT_OVERLAY
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
    s_palette_rgb565[index] = rgb_to_rgb565(r, g, b);
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
    DoomBootLog_Printf("[video] I_InitGraphics begin\n");

    memset(s_video_buffer, 0, sizeof(s_video_buffer));
    I_VideoBuffer = s_video_buffer;
    s_next_palette_num = 0;

    hal_display_init((uint8_t)DOOM_HAL_TFT_CS_PIN, (uint8_t)DOOM_HAL_TFT_DC_PIN,
                     (uint8_t)DOOM_HAL_TFT_RST_PIN);
    if (!hal_display_configure(DOOM_HAL_TFT_NATIVE_WIDTH,
                               DOOM_HAL_TFT_NATIVE_HEIGHT,
                               HAL_DISPLAY_ROTATION(DOOM_HAL_TFT_ROTATION_DEG),
                               DOOM_HAL_TFT_INVERT,
                               DOOM_HAL_TFT_COLOR_ORDER)) {
        s_display_ready = false;
        DoomBootLog_Printf("[video] I_InitGraphics display configure FAIL\n");
        return;
    }

    configure_display_window();
    DoomBootLog_Printf("[video] I_InitGraphics display=%dx%d window=(%d,%d) "
                       "ready=%s\n",
                       hal_display_get_width(), hal_display_get_height(),
                       s_display_x, s_display_y,
                       s_display_ready ? "OK" : "FAIL");
}

void I_GraphicsCheckCommandLine(void) {}

void I_ShutdownGraphics(void)
{
    s_display_ready = false;
}

void I_SetPaletteNum(int num)
{
    s_next_palette_num = num < 0 ? 0 : num;
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

    for (int y = 0; y < SCREENHEIGHT; ++y) {
        const pixel_t *src = buffer + y * SCREENWIDTH;

        for (int x = 0; x < SCREENWIDTH; ++x) {
            const uint16_t color = s_palette_rgb565[src[x]];
            s_line_rgb565_be[x * 2] = (uint8_t)(color >> 8);
            s_line_rgb565_be[x * 2 + 1] = (uint8_t)color;
        }

        if (!hal_display_write_pixels_dma(s_line_rgb565_be,
                                          sizeof(s_line_rgb565_be))) {
            break;
        }
    }

    (void)hal_display_end_write();
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

void DoomVideo_Core1Poll(void)
{
    const pixel_t *buffer = NULL;

    hal_critical_section_enter();
    if (s_flush_pending != 0u) {
        buffer = s_flush_buffer;
        s_flush_pending = 0u;
    }
    hal_critical_section_exit();

    if (buffer == NULL) {
        return;
    }

    finish_update_buffer(buffer);

    hal_critical_section_enter();
    s_flush_buffer = NULL;
    s_flush_busy = 0u;
    ++s_async_flush_count;
    hal_critical_section_exit();
}

void DoomVideo_GetAsyncFlushStats(uint32_t *flushes, uint32_t *waits,
                                  uint32_t *wait_us)
{
    hal_critical_section_enter();
    *flushes = s_async_flush_count;
    *waits = s_async_wait_count;
    *wait_us = s_async_wait_us;
    hal_critical_section_exit();
}

void I_FinishUpdate(void)
{
    if (!s_display_ready) {
        return;
    }

    update_palette_if_needed();
    DoomVideo_WaitForAsyncFlush();

    hal_critical_section_enter();
    s_flush_buffer = I_VideoBuffer;
    s_flush_busy = 1u;
    s_flush_pending = 1u;
    hal_critical_section_exit();
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

void pd_end_frame(int wipe_start)
{
    (void)wipe_start;

    DoomRenderDiag_MarkPhase(5u);

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

    DoomRenderDiag_MarkPhase(6u);
    I_FinishUpdate();
    DoomRenderDiag_MarkPhase(7u);
}
