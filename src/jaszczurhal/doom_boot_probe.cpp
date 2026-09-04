#include <JaszczurHAL.h>
#include <hal/display/hal_display.h>
#include <hal/gpio/hal_gpio.h>
#include <hal/spi/hal_spi.h>
#include <hal/system/hal_system.h>

#include <stdio.h>
#include <stdint.h>

#include "doom_main_config.h"

static bool s_led_state = false;
static bool s_display_ready = false;
static uint32_t s_last_tick_ms = 0;
static uint32_t s_loop_count = 0;

static void draw_static_display(void) {
  const int w = hal_display_get_width();
  const int h = hal_display_get_height();

  hal_display_fill_screen(HAL_COLOR_BLACK);
  hal_display_fill_rect(0, 0, w, 34, HAL_COLOR_BLUE);
  hal_display_set_default_font_with_pos_and_color(10, 12, HAL_COLOR_WHITE);
  hal_display_print("rp2040-doom HAL probe");

  hal_display_draw_rect(8, 48, w - 16, 76, HAL_COLOR_CYAN);
  hal_display_fill_rect(16, 56, w - 32, 24, HAL_COLOR_GREEN);
  hal_display_fill_rect(16, 86, w - 32, 24, HAL_COLOR_ORANGE);

  hal_display_set_default_font_with_pos_and_color(12, h - 48, HAL_COLOR_WHITE);
  hal_display_print("USB + HAL + TFT");
}

static void draw_heartbeat(uint32_t loops, uint32_t ms, bool led_state) {
  if (!s_display_ready) {
    return;
  }

  const int h = hal_display_get_height();
  char line[48] = {};
  snprintf(line, sizeof(line), "loops=%lu t=%lums led=%u",
           (unsigned long)loops, (unsigned long)ms,
           (unsigned int)(led_state ? 1u : 0u));

  hal_display_fill_rect(12, h - 24, hal_display_get_width() - 24, 12,
                        HAL_COLOR_BLACK);
  hal_display_set_default_font_with_pos_and_color(12, h - 24,
                                                  HAL_COLOR_YELLOW);
  hal_display_print(line);
}

extern "C" void app_start(void) {
  hal_debug_init_default();
  hal_deb_set_prefix("HALPROBE");

  deb("");
  deb("app_start entered");
  deb("build %s %s", __DATE__, __TIME__);
#ifdef F_CPU
  deb("F_CPU=%lu", (unsigned long)F_CPU);
#endif
#ifdef ARDUINO_BOARD
  deb("board=%s", ARDUINO_BOARD);
#endif

  hal_gpio_set_mode((uint8_t)DOOM_BOOT_PROBE_LED_PIN, HAL_GPIO_OUTPUT_HIGH);
  deb("HAL GPIO OK, LED pin=%u", (unsigned int)DOOM_BOOT_PROBE_LED_PIN);

  deb("HAL SPI init: bus=0 miso=%u mosi=%u sck=%u",
      (unsigned int)DOOM_HAL_TFT_MISO_PIN,
      (unsigned int)DOOM_HAL_TFT_MOSI_PIN,
      (unsigned int)DOOM_HAL_TFT_SCK_PIN);
  hal_spi_init(0u, (uint8_t)DOOM_HAL_TFT_MISO_PIN,
               (uint8_t)DOOM_HAL_TFT_MOSI_PIN,
               (uint8_t)DOOM_HAL_TFT_SCK_PIN);

  deb("TFT init begin: cs=%u dc=%u rst=%u",
      (unsigned int)DOOM_HAL_TFT_CS_PIN,
      (unsigned int)DOOM_HAL_TFT_DC_PIN,
      (unsigned int)DOOM_HAL_TFT_RST_PIN);
  hal_display_init((uint8_t)DOOM_HAL_TFT_CS_PIN, (uint8_t)DOOM_HAL_TFT_DC_PIN,
                   (uint8_t)DOOM_HAL_TFT_RST_PIN);
  s_display_ready =
      hal_display_configure(DOOM_HAL_TFT_NATIVE_WIDTH,
                            DOOM_HAL_TFT_NATIVE_HEIGHT,
                            HAL_DISPLAY_ROTATION(DOOM_HAL_TFT_ROTATION_DEG),
                            DOOM_HAL_TFT_INVERT,
                            DOOM_HAL_TFT_COLOR_ORDER);

  deb("TFT configure=%s native=%dx%d rotation=%u actual=%dx%d",
      s_display_ready ? "OK" : "FAIL", DOOM_HAL_TFT_NATIVE_WIDTH,
      DOOM_HAL_TFT_NATIVE_HEIGHT, (unsigned int)DOOM_HAL_TFT_ROTATION_DEG,
      hal_display_get_width(), hal_display_get_height());

  if (s_display_ready) {
    draw_static_display();
    draw_heartbeat(0u, hal_millis(), true);
  }

  deb("setup complete; HAL heartbeat follows");
}

extern "C" void app_task0(void) {
  hal_debug_loop();

  const uint32_t now = hal_millis();

  if (now - s_last_tick_ms < 1000u) {
    hal_delay_ms(1u);
    return;
  }

  s_last_tick_ms = now;
  s_led_state = !s_led_state;
  ++s_loop_count;

  hal_gpio_write((uint8_t)DOOM_BOOT_PROBE_LED_PIN, s_led_state);
  draw_heartbeat(s_loop_count, now, s_led_state);

  deb("alive t=%lu loops=%lu led=%u display=%s", (unsigned long)now,
      (unsigned long)s_loop_count, (unsigned int)(s_led_state ? 1u : 0u),
      s_display_ready ? "OK" : "FAIL");
}
