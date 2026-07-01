#include <JaszczurHAL.h>
#include <hal/hal_app.h>
#include <hal/hal_display.h>
#include <hal/hal_gpio.h>
#include <hal/hal_serial.h>
#include <hal/hal_spi.h>
#include <hal/hal_system.h>
#include <utils/tools_api.h>

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

/* Raise clk_peri so the TFT SPI bus can run at its proper speed.  Arduino-Pico
 * leaves clk_peri on PLL_USB (48 MHz), which caps SPI at 24 MHz regardless of
 * the requested clock or the CPU clock.  USB CDC (console) is on PLL_USB and
 * unaffected; PWM audio is on clk_sys and unaffected. */
#if __has_include(<hardware/clocks.h>)
#include <hardware/clocks.h>
#define DOOM_HAVE_PICO_CLOCKS 1
#endif
#if __has_include(<hardware/vreg.h>)
#include <hardware/vreg.h>
#define DOOM_HAVE_PICO_VREG 1
#endif

#include "i_main.h"
#include "doom_main_config.h"
#include "jaszczurhal/doom_storage_hal.h"
#include "picodoom.h"

extern "C" void DoomRenderDiag_ReportRetained(void);
extern "C" void DoomRenderDiag_StartRun(void);
extern "C" void DoomHAL_LogRenderConfig(void);

bool core1_separate_stack = true;

static bool s_doom_started = false;
static bool s_boot_blocked = false;
static bool s_display_available = false;
static bool s_led_state = false;
static uint32_t s_last_blink_ms = 0;
static uint32_t s_boot_ready_ms = 0;
static uint32_t s_last_diag_ms = 0;
static uint32_t s_last_flash_scan_ms = 0;

static bool whd_payload_magic_ok(const uint8_t *base) {
#if WHD_SUPER_TINY
  const char expected_kind = 'X';
#else
  const char expected_kind = 'D';
#endif

  return base != nullptr && base[0] == 'I' && base[1] == 'W' &&
         base[2] == 'H' && base[3] == expected_kind;
}

static uint32_t boot_read_le32(const uint8_t *base) {
  return (uint32_t)base[0] | ((uint32_t)base[1] << 8u) |
         ((uint32_t)base[2] << 16u) | ((uint32_t)base[3] << 24u);
}

static bool whd_payload_header_ok(const uint8_t *base) {
  if (!whd_payload_magic_ok(base)) {
    return false;
  }

  const uint32_t num_lumps = boot_read_le32(base + 4u);
  const uint32_t info_table_offset = boot_read_le32(base + 8u);
  const uint32_t payload_size = boot_read_le32(base + 12u);
  const uintptr_t payload_start = (uintptr_t)base;
  const uintptr_t flash_end = (uintptr_t)DoomStorage_FlashEnd();

  if (num_lumps == 0u || num_lumps > 65535u) {
    return false;
  }

  if (info_table_offset < 36u || info_table_offset >= payload_size) {
    return false;
  }

  if (payload_size < 64u || payload_size > DOOM_FLASH_SIZE_BYTES) {
    return false;
  }

  return payload_start <= flash_end && payload_size <= flash_end - payload_start;
}

static const char *expected_payload_magic(void) {
#if WHD_SUPER_TINY
  return "IWHX";
#else
  return "IWHD";
#endif
}

static char printable_byte(uint8_t value) {
  return (value >= 32u && value <= 126u) ? (char)value : '.';
}

static void boot_format_first4(char *out, size_t out_size, const uint8_t *base) {
  if (out == nullptr || out_size == 0u) {
    return;
  }

  snprintf(out, out_size, "%02x %02x %02x %02x", (unsigned int)base[0],
           (unsigned int)base[1], (unsigned int)base[2],
           (unsigned int)base[3]);
}

static void boot_log_payload_probe(uintptr_t address, const char *label) {
  const uint8_t *base = (const uint8_t *)address;
  char hex[16u * 3u + 1u];
  char *out = hex;

  for (unsigned int i = 0; i < 16u; ++i) {
    const int written = snprintf(out, 4u, "%s%02x", i == 0u ? "" : " ",
                                 (unsigned int)base[i]);
    out += written > 0 ? written : 0;
  }
  *out = '\0';

  deb("[flash] %s 0x%08lx: %s  magic='%c%c%c%c'", label,
      (unsigned long)address, hex, printable_byte(base[0]),
      printable_byte(base[1]), printable_byte(base[2]), printable_byte(base[3]));
}

static void boot_log_payload_header(const uint8_t *base, const char *label) {
  deb("[boot] %s at 0x%08lx: lumps=%lu table=0x%08lx size=%lu",
      label, (unsigned long)(uintptr_t)base,
      (unsigned long)boot_read_le32(base + 4u),
      (unsigned long)boot_read_le32(base + 8u),
      (unsigned long)boot_read_le32(base + 12u));
}

static const uint8_t *boot_find_payload_base(void) {
  const uint8_t *const preferred = DOOM_STORAGE_WHD_BASE;

  if (whd_payload_header_ok(preferred)) {
    boot_log_payload_header(preferred, "WHX configured");
    return preferred;
  }

  const uintptr_t scan_start = DOOM_FLASH_XIP_BASE;
  const uintptr_t scan_end = (uintptr_t)DoomStorage_FlashEnd();
  deb("[boot] scanning flash for %s: 0x%08lx..0x%08lx step=%u",
      expected_payload_magic(), (unsigned long)scan_start,
      (unsigned long)scan_end, (unsigned int)DOOM_WHD_SCAN_STEP_BYTES);

  for (uintptr_t address = scan_start; address + 64u < scan_end;
       address += DOOM_WHD_SCAN_STEP_BYTES) {
    const uint8_t *const candidate = (const uint8_t *)address;
    if (whd_payload_header_ok(candidate)) {
      boot_log_payload_header(candidate, "WHX found");
      return candidate;
    }
  }

  deb("[boot] scan complete: %s not found", expected_payload_magic());
  return preferred;
}

static void boot_log_flash_scan(void) {
  static const uintptr_t scan_addresses[] = {
      DOOM_FLASH_XIP_BASE,
      DOOM_FLASH_XIP_BASE + 0x00040000u,
      DOOM_FLASH_XIP_BASE + 0x00100000u,
      DOOM_FLASH_XIP_BASE + 0x001ff000u,
      DOOM_WHD_FLASH_ADDR,
      DOOM_WHD_FLASH_ADDR + 0x00000100u,
      DOOM_WHD_FLASH_ADDR + 0x00100000u,
  };

  for (unsigned int i = 0; i < sizeof(scan_addresses) / sizeof(scan_addresses[0]);
       ++i) {
    boot_log_payload_probe(scan_addresses[i],
                           scan_addresses[i] == DOOM_WHD_FLASH_ADDR ? "whx" : "xip");
  }

  const uintptr_t active = (uintptr_t)DoomStorage_WHDBase();
  if (active != DOOM_WHD_FLASH_ADDR) {
    boot_log_payload_probe(active, "active");
  }
}

static void boot_display_message(uint16_t background, uint16_t foreground,
                                 const char *line1, const char *line2,
                                 const char *line3) {
  if (!s_display_available) {
    return;
  }

  hal_display_fill_screen(background);
  hal_display_set_text_color(foreground);
  hal_display_set_text_size(2u);
  hal_display_print_at(12, 36, line1);
  hal_display_print_at(12, 72, line2);
  hal_display_set_text_size(1u);
  hal_display_print_at(12, 116, line3);
  hal_display_flush();
}

static void boot_blink(void) {
  const uint32_t now = hal_millis();
  if (now - s_last_blink_ms < 250u) {
    return;
  }

  s_last_blink_ms = now;
  s_led_state = !s_led_state;
  hal_gpio_write((uint8_t)DOOM_BOOT_LED_PIN, s_led_state);
}

static void boot_log_blocked_status(uint32_t now) {
  const uint8_t *const whd_base = DoomStorage_WHDBase();
  char first4[16] = {};
  boot_format_first4(first4, sizeof(first4), whd_base);

  hal_derr("BLOCKED Missing WHX: t=%lu addr=0x%08lx got=%s expected=%s "
           "display=%s",
           (unsigned long)now, (unsigned long)(uintptr_t)whd_base, first4,
           expected_payload_magic(), s_display_available ? "OK" : "FAIL");

  boot_log_payload_probe(DOOM_WHD_FLASH_ADDR, "whx");
}

void app_start(void) {
  hal_fault_subsystem_init();
  const bool stack_guard_ready = hal_stack_guard_init();
  debugInit();
  hal_deb_set_prefix("DOOM");

  hal_delay_ms(4000u);

  deb("");
  deb("[boot] rp2040-doom boot");
  deb("[boot] build: %s %s", __DATE__, __TIME__);

  const hal_reset_reason_t reset_reason = hal_get_reset_reason();
  hal_fault_info_t last_fault = {};
  deb("[boot] reset reason=%s watchdog=%d brownout=%d free_heap=%lu",
      hal_reset_reason_str(reset_reason), hal_watchdog_caused_reboot() ? 1 : 0,
      hal_last_boot_was_brownout() ? 1 : 0,
      (unsigned long)hal_get_free_heap());
  deb("DOOM [boot] reset reason=%s watchdog=%d brownout=%d "
                     "free_heap=%lu\n",
                     hal_reset_reason_str(reset_reason),
                     hal_watchdog_caused_reboot() ? 1 : 0,
                     hal_last_boot_was_brownout() ? 1 : 0,
                     (unsigned long)hal_get_free_heap());
  deb("[boot] core1 separate stack=%d", core1_separate_stack ? 1 : 0);
  deb("DOOM [boot] core1 separate stack=%d\n",
                     core1_separate_stack ? 1 : 0);
  deb("[boot] stack guard=%d", stack_guard_ready ? 1 : 0);
  deb("DOOM [boot] stack guard=%d\n", stack_guard_ready ? 1 : 0);
  if (hal_get_last_fault(&last_fault)) {
    derr("[boot] last fault: pc=0x%08lx lr=0x%08lx psr=0x%08lx",
             (unsigned long)last_fault.pc, (unsigned long)last_fault.lr,
             (unsigned long)last_fault.psr);
    deb("DOOM [boot] last fault: pc=0x%08lx lr=0x%08lx "
                       "psr=0x%08lx\n",
                       (unsigned long)last_fault.pc,
                       (unsigned long)last_fault.lr,
                       (unsigned long)last_fault.psr);
  }
  DoomRenderDiag_ReportRetained();
  DoomRenderDiag_StartRun();
  DoomHAL_LogRenderConfig();

#ifdef F_CPU
  deb("[boot] F_CPU=%lu", (unsigned long)F_CPU);
#endif

#if DOOM_SYS_OVERCLOCK && defined(DOOM_HAVE_PICO_CLOCKS)
  // Native rp2040-doom raises the core to 270 MHz @ 1.30V; the Arduino-Pico core
  // boots at 125 MHz and the port's own overclock (src/i_main.c) is compiled out
  // for JASZCZURHAL_PORT.  Re-issue it here: bump voltage first, then clock,
  // before any peripheral (SPI/TFT) is configured.  clk_peri is retied to
  // clk_sys just below, so the TFT SPI bus scales with the new system clock.
  {
    const uint32_t before_hz = clock_get_hz(clk_sys);
#if defined(DOOM_HAVE_PICO_VREG)
    // DOOM_SYS_VREG_VOLTAGE defaults to VREG_VOLTAGE_1_30 (valid on RP2040 and
    // RP2350); enough headroom for 250 MHz (RP2040) and 300 MHz (RP2350).
    vreg_set_voltage(DOOM_SYS_VREG_VOLTAGE);
    hal_delay_us(1000u);
#endif
    const bool clk_ok = set_sys_clock_khz(DOOM_SYS_CLOCK_KHZ, true);
    deb("[boot] overclock %s: clk_sys %lu -> %lu Hz (requested %u kHz)",
        clk_ok ? "OK" : "FAILED", (unsigned long)before_hz,
        (unsigned long)clock_get_hz(clk_sys), (unsigned int)DOOM_SYS_CLOCK_KHZ);
  }
#endif

#ifdef DOOM_HAVE_PICO_CLOCKS
  // Move clk_peri from PLL_USB (48 MHz) to PLL_SYS so the TFT SPI bus is no
  // longer capped at 24 MHz.  Done before any SPI/UART/I2C peripheral is
  // configured (display init happens later in I_DoomMain).  With clk_sys at
  // 250 MHz this frees the TFT SPI request (62.5 MHz) to be met exactly.
  {
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    clock_configure(clk_peri, 0,
                    CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS,
                    sys_hz, sys_hz);
    deb("[boot] clk_peri -> %lu Hz (was 48 MHz); TFT SPI request=%lu Hz -> "
        "actual<=clk_peri/even_div",
        (unsigned long)clock_get_hz(clk_peri),
        (unsigned long)JH_ILI9341_SPI_DEFAULT_HZ);
  }
#endif
  deb("[boot] flash: xip=0x%08lx size=0x%08lx end=0x%08lx",
      (unsigned long)DOOM_FLASH_XIP_BASE, (unsigned long)DOOM_FLASH_SIZE_BYTES,
      (unsigned long)(uintptr_t)DoomStorage_FlashEnd());
  deb("[boot] payload: addr=0x%08lx expected=%s WHD_SUPER_TINY=%d",
      (unsigned long)DOOM_WHD_FLASH_ADDR, expected_payload_magic(),
      (int)WHD_SUPER_TINY);

  hal_gpio_set_mode((uint8_t)DOOM_BOOT_LED_PIN, HAL_GPIO_OUTPUT_LOW);
  deb("[boot] LED pin=%u initialized", (unsigned int)DOOM_BOOT_LED_PIN);

  deb("[boot] TFT pins: sck=%u mosi=%u miso=%u cs=%u dc=%u rst=%u",
      (unsigned int)DOOM_HAL_TFT_SCK_PIN,
      (unsigned int)DOOM_HAL_TFT_MOSI_PIN,
      (unsigned int)DOOM_HAL_TFT_MISO_PIN, (unsigned int)DOOM_HAL_TFT_CS_PIN,
      (unsigned int)DOOM_HAL_TFT_DC_PIN, (unsigned int)DOOM_HAL_TFT_RST_PIN);
  deb("[boot] SPI init begin: bus=0 miso=%u mosi=%u sck=%u",
      (unsigned int)DOOM_HAL_TFT_MISO_PIN,
      (unsigned int)DOOM_HAL_TFT_MOSI_PIN,
      (unsigned int)DOOM_HAL_TFT_SCK_PIN);
  hal_spi_init(0u, (uint8_t)DOOM_HAL_TFT_MISO_PIN,
               (uint8_t)DOOM_HAL_TFT_MOSI_PIN,
               (uint8_t)DOOM_HAL_TFT_SCK_PIN);
  deb("[boot] TFT init begin");
  hal_display_init((uint8_t)DOOM_HAL_TFT_CS_PIN, (uint8_t)DOOM_HAL_TFT_DC_PIN,
                   (uint8_t)DOOM_HAL_TFT_RST_PIN);
  s_display_available =
      hal_display_configure(DOOM_HAL_TFT_NATIVE_WIDTH,
                            DOOM_HAL_TFT_NATIVE_HEIGHT,
                            HAL_DISPLAY_ROTATION(DOOM_HAL_TFT_ROTATION_DEG),
                            DOOM_HAL_TFT_INVERT,
                            DOOM_HAL_TFT_COLOR_ORDER);
  deb("[boot] TFT configure=%s native=%ux%u rotation=%u",
      s_display_available ? "OK" : "FAIL",
      (unsigned int)DOOM_HAL_TFT_NATIVE_WIDTH,
      (unsigned int)DOOM_HAL_TFT_NATIVE_HEIGHT,
      (unsigned int)DOOM_HAL_TFT_ROTATION_DEG);
  if (s_display_available) {
    deb("[boot] TFT actual=%dx%d", hal_display_get_width(),
        hal_display_get_height());
  }
  deb("DOOM [boot] reset summary post-TFT: reason=%s "
                     "watchdog=%d brownout=%d free_heap=%lu stack_guard=%d\n",
                     hal_reset_reason_str(reset_reason),
                     hal_watchdog_caused_reboot() ? 1 : 0,
                     hal_last_boot_was_brownout() ? 1 : 0,
                     (unsigned long)hal_get_free_heap(),
                     stack_guard_ready ? 1 : 0);

  const uint8_t *const whd_base = boot_find_payload_base();
  DoomStorage_SetWHDBase(whd_base);
  boot_log_flash_scan();
  if (!whd_payload_header_ok(whd_base)) {
    s_boot_blocked = true;
    char first4[16] = {};
    boot_format_first4(first4, sizeof(first4), whd_base);
    hal_derr("missing WHD/WHX payload at 0x%08lx; got %s expected %s",
             (unsigned long)(uintptr_t)whd_base, first4,
             expected_payload_magic());
    boot_display_message(HAL_COLOR_RED, HAL_COLOR_WHITE, "RP2040 Doom",
                         "Missing WHX", first4);
    return;
  }

  hal_deb("WHD/WHX payload OK at 0x%08lx",
          (unsigned long)(uintptr_t)whd_base);
  boot_display_message(HAL_COLOR_BLACK, HAL_COLOR_GREEN, "RP2040 Doom",
                       "WHX OK", "Starting...");
  s_boot_ready_ms = hal_millis();
  deb("[boot] diagnostics hold: %u ms before I_DoomMain",
      (unsigned int)DOOM_BOOT_DIAG_HOLD_MS);
}

void app_task0(void) {
  hal_alive_mark();
  hal_debug_loop();

  const uint32_t now = hal_millis();

  if (s_boot_blocked) {
    boot_blink();

    if (now - s_last_diag_ms >= DOOM_BOOT_BLOCKED_LOG_MS) {
      s_last_diag_ms = now;
      boot_log_blocked_status(now);
    }

    if (now - s_last_flash_scan_ms >= DOOM_BOOT_BLOCKED_SCAN_MS) {
      s_last_flash_scan_ms = now;
      boot_log_flash_scan();
    }

    hal_delay_ms(10u);
    return;
  }

  if (!s_doom_started) {
    if (s_boot_ready_ms == 0u) {
      s_boot_ready_ms = now;
    }

    boot_blink();
    if (now - s_last_diag_ms >= 1000u) {
      s_last_diag_ms = now;
      deb("[boot] alive: t=%lu ms display=%s doom=%s", (unsigned long)now,
          s_display_available ? "OK" : "FAIL",
          s_doom_started ? "started" : "pending");
    }

    if (now - s_boot_ready_ms < DOOM_BOOT_DIAG_HOLD_MS) {
      hal_delay_ms(10u);
      return;
    }

    s_doom_started = true;
    deb("[boot] entering I_DoomMain");
    hal_delay_ms(100u);
    (void)I_DoomMain(0, nullptr);
  }

  hal_delay_ms(1000u);
}

void app_task1(void) {
  if (s_boot_blocked || !s_doom_started) {
    hal_delay_ms(10u);
    return;
  }

  pd_core1_loop();
  hal_delay_us(10u);
}
