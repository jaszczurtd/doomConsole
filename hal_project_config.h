#pragma once

/* HAL/project defaults */
// Default serial baud used by JaszczurHAL diagnostics and boot logs.
#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

// Enable the HAL DMA PWM audio backend for the real Doom firmware.
#if (!defined(DOOM_BOOT_PROBE_ONLY) || !DOOM_BOOT_PROBE_ONLY) &&              \
    !defined(HAL_ENABLE_DMA_PWM_AUDIO)
#define HAL_ENABLE_DMA_PWM_AUDIO
#endif

// Enable Arduino-Pico core1 loop support for async flush/render helper work.
#if (!defined(DOOM_BOOT_PROBE_ONLY) || !DOOM_BOOT_PROBE_ONLY) &&              \
    !defined(HAL_ENABLE_APP_TASK1)
#define HAL_ENABLE_APP_TASK1
#endif

// Default to ILI9341 support when no specific TFT driver was selected.
#if !defined(HAL_ENABLE_ILI9341) && !defined(HAL_ENABLE_ST7789) &&             \
    !defined(HAL_ENABLE_ST7735) && !defined(HAL_ENABLE_ST7796S)
#define HAL_ENABLE_ILI9341
#endif

// Pick the active HAL display family from the enabled driver.
#if !defined(HAL_DISPLAY_ILI9341) && !defined(HAL_DISPLAY_ST7789) &&           \
    !defined(HAL_DISPLAY_ST7735) && !defined(HAL_DISPLAY_ST7796S)
#if defined(HAL_ENABLE_ST7789)
#define HAL_DISPLAY_ST7789
#elif defined(HAL_ENABLE_ST7735)
#define HAL_DISPLAY_ST7735
#elif defined(HAL_ENABLE_ST7796S)
#define HAL_DISPLAY_ST7796S
#else
#define HAL_DISPLAY_ILI9341
#endif
#endif
