#pragma once

#ifndef HAL_DEBUG_DEFAULT_BAUD
#define HAL_DEBUG_DEFAULT_BAUD 115200u
#endif

#if (!defined(DOOM_BOOT_PROBE_ONLY) || !DOOM_BOOT_PROBE_ONLY) &&              \
    !defined(HAL_ENABLE_DMA_PWM_AUDIO)
#define HAL_ENABLE_DMA_PWM_AUDIO
#endif

#if (!defined(DOOM_BOOT_PROBE_ONLY) || !DOOM_BOOT_PROBE_ONLY) &&              \
    !defined(HAL_ENABLE_APP_TASK1)
#define HAL_ENABLE_APP_TASK1
#endif

#if !defined(HAL_ENABLE_ILI9341) && !defined(HAL_ENABLE_ST7789) &&             \
    !defined(HAL_ENABLE_ST7735) && !defined(HAL_ENABLE_ST7796S)
#define HAL_ENABLE_ILI9341
#endif

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

/*
 * Etap 1 uses the RP2040 Arduino-Pico backend through JaszczurHAL.
 * The CMake-generated .ino supplies setup()/loop()/loop1() and calls
 * app_start(), app_task0(), and app_task1().
 */
