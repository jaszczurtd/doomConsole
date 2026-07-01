#ifndef DOOM_MAIN_CONFIG_H
#define DOOM_MAIN_CONFIG_H

/*
 * Central configuration for the JaszczurHAL Doom port.
 *
 * Keep port-level knobs here instead of scattering fallback #defines across
 * backends.  Values may still be overridden by compiler definitions before this
 * header is included.
 */

/* Boot/probe */
#ifndef DOOM_BOOT_LED_PIN
#define DOOM_BOOT_LED_PIN 25u
#endif

#ifndef DOOM_BOOT_PROBE_LED_PIN
#define DOOM_BOOT_PROBE_LED_PIN DOOM_BOOT_LED_PIN
#endif

// Keep the boot diagnostics screen visible before entering Doom.
#ifndef DOOM_BOOT_DIAG_HOLD_MS
#define DOOM_BOOT_DIAG_HOLD_MS 4000u
#endif

// Interval for repeated boot-blocked status messages.
#ifndef DOOM_BOOT_BLOCKED_LOG_MS
#define DOOM_BOOT_BLOCKED_LOG_MS 1000u
#endif

// Interval for rescanning flash while waiting for a WHD/WAD payload.
#ifndef DOOM_BOOT_BLOCKED_SCAN_MS
#define DOOM_BOOT_BLOCKED_SCAN_MS 5000u
#endif

/* System clock / overclock.
 *
 * Native rp2040-doom runs the core at 270 MHz @ 1.30V; the Arduino-Pico core
 * boots at only 125 MHz and the port's own set_sys_clock_khz() is compiled out
 * (see src/i_main.c: gated by !JASZCZURHAL_PORT).  We re-issue it in app_start().
 *
 * 250 MHz is chosen deliberately: clk_peri is retied to clk_sys, and the TFT
 * SPI baud is clk_peri / (even divisor).  250 MHz divides cleanly to 62.5 MHz
 * (250/4) which is the intended TFT SPI rate; 276 MHz would only reach 46 MHz
 * (276/6, since 276/4 = 69 MHz exceeds the 62.5 MHz request). */
#ifndef DOOM_SYS_OVERCLOCK
#define DOOM_SYS_OVERCLOCK 1
#endif

#ifndef DOOM_SYS_CLOCK_KHZ
#define DOOM_SYS_CLOCK_KHZ 250000u
#endif

/* TFT/display */
#ifndef DOOM_HAL_TFT_SCK_PIN
#define DOOM_HAL_TFT_SCK_PIN 18u
#endif

#ifndef DOOM_HAL_TFT_MOSI_PIN
#define DOOM_HAL_TFT_MOSI_PIN 19u
#endif

#ifndef DOOM_HAL_TFT_MISO_PIN
#define DOOM_HAL_TFT_MISO_PIN 16u
#endif

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

// Logical display rotation passed to the HAL display driver.
#ifndef DOOM_HAL_TFT_ROTATION_DEG
#define DOOM_HAL_TFT_ROTATION_DEG 90
#endif

// Panel inversion mode; useful when the panel colors are visibly inverted.
#ifndef DOOM_HAL_TFT_INVERT
#define DOOM_HAL_TFT_INVERT HAL_DISPLAY_INVERT_OFF
#endif

// RGB/BGR order expected by the physical panel.
#ifndef DOOM_HAL_TFT_COLOR_ORDER
#define DOOM_HAL_TFT_COLOR_ORDER HAL_DISPLAY_COLOR_ORDER_RGB
#endif

// Diagnostic mode: 1 forces synchronous TFT flush on core0 instead of async core1.
#ifndef DOOM_VIDEO_SYNC_FLUSH
#define DOOM_VIDEO_SYNC_FLUSH 0
#endif

// Number of framebuffer lines converted and flushed per TFT transfer chunk.
#ifndef DOOM_FLUSH_LINES
#define DOOM_FLUSH_LINES 8
#endif

/* GPIO input */
// Selects active-low button wiring with internal pull-ups.
#ifndef DOOM_INPUT_ACTIVE_LOW
#define DOOM_INPUT_ACTIVE_LOW 1
#endif

#ifndef DOOM_INPUT_PIN_UP
#define DOOM_INPUT_PIN_UP 2
#endif

#ifndef DOOM_INPUT_PIN_DOWN
#define DOOM_INPUT_PIN_DOWN 3
#endif

#ifndef DOOM_INPUT_PIN_LEFT
#define DOOM_INPUT_PIN_LEFT 4
#endif

#ifndef DOOM_INPUT_PIN_RIGHT
#define DOOM_INPUT_PIN_RIGHT 5
#endif

#ifndef DOOM_INPUT_PIN_FIRE
#define DOOM_INPUT_PIN_FIRE 7
#endif

#ifndef DOOM_INPUT_PIN_USE
#define DOOM_INPUT_PIN_USE 8
#endif

#ifndef DOOM_INPUT_PIN_MENU
#define DOOM_INPUT_PIN_MENU 9
#endif

#ifndef DOOM_INPUT_PIN_ACCEPT
#define DOOM_INPUT_PIN_ACCEPT 10
#endif

#ifndef DOOM_INPUT_PIN_BACK
#define DOOM_INPUT_PIN_BACK 11
#endif

/* Audio */
#ifndef DOOM_HAL_AUDIO_PWM_PIN
#define DOOM_HAL_AUDIO_PWM_PIN 6u
#endif

// PWM resolution used by the DAC-less audio output.
#ifndef DOOM_HAL_AUDIO_PWM_BITS
#define DOOM_HAL_AUDIO_PWM_BITS 12u
#endif

// PWM period derived from the selected bit depth.
#ifndef DOOM_HAL_AUDIO_PWM_PERIOD_TICKS
#define DOOM_HAL_AUDIO_PWM_PERIOD_TICKS (1u << DOOM_HAL_AUDIO_PWM_BITS)
#endif

// Idle PWM duty level used as audio zero.
#ifndef DOOM_HAL_AUDIO_PWM_IDLE
#define DOOM_HAL_AUDIO_PWM_IDLE (DOOM_HAL_AUDIO_PWM_PERIOD_TICKS / 2u)
#endif

// Number of samples per audio transfer block.
#ifndef DOOM_HAL_AUDIO_BLOCK_SIZE
#define DOOM_HAL_AUDIO_BLOCK_SIZE 1024u
#endif

// Audio sample rate; derived from F_CPU when available to match PWM timing.
#ifndef PICO_SOUND_SAMPLE_FREQ
#ifdef F_CPU
#define PICO_SOUND_SAMPLE_FREQ                                             \
    ((uint32_t)(F_CPU / DOOM_HAL_AUDIO_PWM_PERIOD_TICKS))
#else
#define PICO_SOUND_SAMPLE_FREQ 48828u
#endif
#endif

// In-game sound mixer channel count.
#ifndef NUM_SOUND_CHANNELS
#define NUM_SOUND_CHANNELS 8
#endif

/* Flash/WHD storage */
// RP2040 XIP base address used to convert flash pointers to offsets.
#ifndef DOOM_FLASH_XIP_BASE
#define DOOM_FLASH_XIP_BASE 0x10000000u
#endif

// Total flash size used to bound WHD/save scanning.
#ifndef DOOM_FLASH_SIZE_BYTES
#if defined(PICO_FLASH_SIZE_BYTES)
#define DOOM_FLASH_SIZE_BYTES PICO_FLASH_SIZE_BYTES
#else
#define DOOM_FLASH_SIZE_BYTES 0x400000u
#endif
#endif

// Absolute XIP address where the WHD/WAD payload is expected by default.
#ifndef DOOM_WHD_FLASH_ADDR
#if defined(TINY_WAD_ADDR)
#define DOOM_WHD_FLASH_ADDR TINY_WAD_ADDR
#else
#define DOOM_WHD_FLASH_ADDR 0x10200000u
#endif
#endif

// Flash scan granularity while looking for the WHD payload magic.
#ifndef DOOM_WHD_SCAN_STEP_BYTES
#define DOOM_WHD_SCAN_STEP_BYTES 4u
#endif

/* Zone/heap budget */
// Preferred Doom zone memory size for the JaszczurHAL build.
#ifndef JASZCZURHAL_ZONE_BYTES
#define JASZCZURHAL_ZONE_BYTES (96 * 1024)
#endif

// Minimum acceptable Doom zone size before startup fails.
#ifndef JASZCZURHAL_ZONE_MIN_BYTES
#define JASZCZURHAL_ZONE_MIN_BYTES (72 * 1024)
#endif

// Heap kept outside the Doom zone for HAL, stacks, buffers, and runtime services.
#ifndef JASZCZURHAL_HEAP_RESERVE_BYTES
#define JASZCZURHAL_HEAP_RESERVE_BYTES (24 * 1024)
#endif

/* Render diagnostics and caches */
// Magic for retained render diagnostics stored in .noinit across fatal aborts.
#ifndef DOOM_RENDER_DIAG_MAGIC
#define DOOM_RENDER_DIAG_MAGIC 0x44524447u /* DRDG */
#endif

// Version tag for retained render diagnostics layout.
#ifndef DOOM_RENDER_DIAG_VERSION
#define DOOM_RENDER_DIAG_VERSION 1u
#endif

// Huffman decoder table capacity for patch/texture columns.
#ifndef HAL_PATCH_DECODER_HWORDS
#define HAL_PATCH_DECODER_HWORDS 2048u
#endif

// Temporary scratch buffer used while building patch decoders.
#ifndef HAL_PATCH_DECODER_TMP_BYTES
#define HAL_PATCH_DECODER_TMP_BYTES 1024u
#endif

// Maximum supported decoded patch column height.
#ifndef HAL_PATCH_COLUMN_MAX_HEIGHT
#define HAL_PATCH_COLUMN_MAX_HEIGHT 256u
#endif

// Hash table size for quick decoded-column cache lookup.
#ifndef HAL_PATCH_COLUMN_CACHE_HASH_SIZE
#define HAL_PATCH_COLUMN_CACHE_HASH_SIZE 128u
#endif

// Maximum height kept in the transient tall-column cache.
#ifndef HAL_PATCH_TALL_CACHE_HEIGHT
#define HAL_PATCH_TALL_CACHE_HEIGHT 160u
#endif

// Number of transient tall-column cache slots.
#ifndef HAL_PATCH_TALL_CACHE_SLOTS
#define HAL_PATCH_TALL_CACHE_SLOTS 18u
#endif

// Huffman decoder table capacity for flat/plane textures.
#ifndef HAL_FLAT_DECODER_HWORDS
#define HAL_FLAT_DECODER_HWORDS 512u
#endif

// Temporary scratch buffer used while building flat decoders.
#ifndef HAL_FLAT_DECODER_TMP_BYTES
#define HAL_FLAT_DECODER_TMP_BYTES 512u
#endif

// Enables queued wall-column rendering and the staged core1 column helper.
#ifndef DOOM_DUAL_CORE_COLUMNS
#define DOOM_DUAL_CORE_COLUMNS 0
#endif

#if DOOM_DUAL_CORE_COLUMNS
// Decoded compact-column cache slots in the dual-core memory budget.
// NOTE: the cache is partitioned per core (SLOTS / CACHE_CORES), so with 2 cores
// only SLOTS/2 slots serve each core.  Growing this is tempting (85% miss rate)
// but each slot is HAL_PATCH_COLUMN_CACHE_HEIGHT bytes and the Doom zone is
// malloc'd from the SAME remaining SRAM: 256 slots (+29 KB) starved the zone
// ("Unable to allocate ... for zone") and crashed.  There is simply not enough
// RAM to brute-force this cache -- the real fix is a faster decode path (RAM
// placement + interpolators), copied from the original pd_render.cpp.
#ifndef HAL_PATCH_COLUMN_CACHE_SLOTS
#define HAL_PATCH_COLUMN_CACHE_SLOTS 32u
#endif
// Deferred plane queue capacity in the dual-core memory budget.
#ifndef HAL_PLANE_QUEUE_MAX
#define HAL_PLANE_QUEUE_MAX 384u
#endif
// Number of independent column-cache ownership domains.
#ifndef HAL_PATCH_COLUMN_CACHE_CORES
#define HAL_PATCH_COLUMN_CACHE_CORES 2u
#endif
#else
// Decoded compact-column cache slots in the default single-core budget.
#ifndef HAL_PATCH_COLUMN_CACHE_SLOTS
#define HAL_PATCH_COLUMN_CACHE_SLOTS 84u
#endif
// Deferred plane queue capacity in the default single-core budget.
#ifndef HAL_PLANE_QUEUE_MAX
#define HAL_PLANE_QUEUE_MAX 768u
#endif
// Number of independent column-cache ownership domains.
#ifndef HAL_PATCH_COLUMN_CACHE_CORES
#define HAL_PATCH_COLUMN_CACHE_CORES 1u
#endif
#endif

// Maximum height stored in the compact decoded-column cache.
#ifndef HAL_PATCH_COLUMN_CACHE_HEIGHT
#define HAL_PATCH_COLUMN_CACHE_HEIGHT 129u
#endif

// Experimental async plane rendering path; currently not the default win.
#ifndef DOOM_RENDER_ASYNC_PLANES
#define DOOM_RENDER_ASYNC_PLANES 0
#endif

// Framebuffer clear value used to detect undrawn pixels in `black=`.
#ifndef DOOM_UNDRAWN_SENTINEL
#define DOOM_UNDRAWN_SENTINEL 0u
#endif

// Bounded queue size for deferred wall columns in the dual-core path.
#ifndef DOOM_COL_QUEUE_MAX
#define DOOM_COL_QUEUE_MAX 144u
#endif

// X split between core0 and core1 for deferred wall-column work.
#ifndef DOOM_COL_SPLIT_X
#define DOOM_COL_SPLIT_X (SCREENWIDTH / 2)
#endif

// Patch-list capacity used for WHD-backed overlay drawing.
#ifndef HAL_PATCH_LIST_MAX_ENTRIES
#define HAL_PATCH_LIST_MAX_ENTRIES VPATCHLIST_COUNT_OVERLAY
#endif

#endif
