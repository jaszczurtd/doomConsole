#pragma once

/* Variants: id, description and the definitions each one adds to this
 * configuration. */
#define JH_PROJECT_VARIANTS(X)                                                \
    X(BOOT_PROBE, "Minimal app_start/app_task0 smoke test",                   \
      DOOM_BOOT_PROBE_ONLY=1)                                                 \
    X(ST7796S, "ST7796S 480x320 panel instead of the ILI9341",                \
      DOOM_TFT_PANEL_ST7796S=1)

/* Doom build options. The recipe passes them, and the screen size they
 * select, to the Doom sources as well. */
// Smoke-test firmware instead of the game (the BOOT_PROBE variant).
#ifndef DOOM_BOOT_PROBE_ONLY
#define DOOM_BOOT_PROBE_ONLY 0
#endif

// Render the 3D scene at the full panel resolution: 320x240 on ILI9341,
// 480x320 on ST7796S.
#ifndef DOOM_HIGHRES_SCENE
#define DOOM_HIGHRES_SCENE 1
#endif

// Renderer and flush experiments; 0 keeps the conservative paths.
#ifndef DOOM_DUAL_CORE_COLUMNS
#define DOOM_DUAL_CORE_COLUMNS 0
#endif
#ifndef DOOM_RENDER_ASYNC_PLANES
#define DOOM_RENDER_ASYNC_PLANES 0
#endif
#ifndef DOOM_VIDEO_SYNC_FLUSH
#define DOOM_VIDEO_SYNC_FLUSH 0
#endif

/* HAL/project defaults */
// Default serial baud used by JaszczurHAL diagnostics and boot logs.
#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

// Two independently erasable 4 KiB banks for persistent application KV.
#ifndef HAL_RP_FLASH_EEPROM_SIZE
#define HAL_RP_FLASH_EEPROM_SIZE 8192
#endif

// The game plays PWM audio and renders on both cores; the smoke test does not.
#if !DOOM_BOOT_PROBE_ONLY
#define HAL_ENABLE_DMA_PWM_AUDIO
#define HAL_ENABLE_APP_TASK1
#endif

// Bluetooth gamepad with saved pairings on the Pico 2 W.
#if defined(HAL_TARGET_RP2350_ARM)
#define HAL_ENABLE_BLUETOOTH_GAMEPAD
#define HAL_ENABLE_KV
#endif

// TFT panel: ILI9341 unless the st7796s variant is built.
#if defined(DOOM_TFT_PANEL_ST7796S)
#if !defined(HAL_TARGET_RP2350_ARM) && !defined(HAL_TARGET_RP2350_RISCV)
#error "doomConsole ST7796S: the ST7796S panel is supported only on RP2350"
#endif
#define HAL_ENABLE_ST7796S
#define HAL_DISPLAY_ST7796S
#else
#define DOOM_TFT_PANEL_ILI9341 1
#define HAL_ENABLE_ILI9341
#define HAL_DISPLAY_ILI9341
#endif

// Requested TFT SPI clock (Hz) for both panel families; the actual rate is
// clk_peri divided by an even divisor.
#ifndef JH_ILI9341_SPI_DEFAULT_HZ
#define JH_ILI9341_SPI_DEFAULT_HZ 50000000
#endif
#ifndef JH_ST77XX_SPI_DEFAULT_HZ
#define JH_ST77XX_SPI_DEFAULT_HZ JH_ILI9341_SPI_DEFAULT_HZ
#endif
