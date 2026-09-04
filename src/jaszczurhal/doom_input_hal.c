/*
 * Minimal JaszczurHAL GPIO input backend.
 *
 * Buttons are active-low by default: each input pin uses the internal pull-up
 * and the physical button should short the pin to GND when pressed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/gpio/hal_gpio.h>
#include <hal/system/hal_system.h>

#include "d_event.h"
#include "doom_main_config.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "i_input.h"
#include "jaszczurhal/doom_gamepad_input.h"

typedef struct {
  uint8_t pin;
  int key;
  doom_input_action_mask_t action;
} doom_input_binding_t;

#define DOOM_INPUT_PIN_NONE UINT8_MAX

float mouse_acceleration = 2.0f;
int mouse_threshold = 10;

static bool s_text_input_enabled;
static bool s_input_initialized;
static bool s_pairing_chord_active;
static bool s_pairing_chord_consumed;
static uint32_t s_pairing_chord_started_ms;
static bool s_factory_reset_chord_active;
static bool s_factory_reset_chord_consumed;
static bool s_factory_reset_chord_guard_active;
static uint32_t s_factory_reset_chord_started_ms;
static doom_input_action_mask_t s_published_actions;

static const doom_input_binding_t s_bindings[] = {
    {DOOM_INPUT_PIN_UP, KEY_UPARROW, DOOM_INPUT_ACTION_UP},
    {DOOM_INPUT_PIN_DOWN, KEY_DOWNARROW, DOOM_INPUT_ACTION_DOWN},
    {DOOM_INPUT_PIN_LEFT, KEY_LEFTARROW, DOOM_INPUT_ACTION_LEFT},
    {DOOM_INPUT_PIN_RIGHT, KEY_RIGHTARROW, DOOM_INPUT_ACTION_RIGHT},
    {DOOM_INPUT_PIN_FIRE, KEY_RCTRL, DOOM_INPUT_ACTION_FIRE},
    {DOOM_INPUT_PIN_USE, ' ', DOOM_INPUT_ACTION_USE},
    {DOOM_INPUT_PIN_MENU, KEY_ESCAPE, DOOM_INPUT_ACTION_MENU},
    {DOOM_INPUT_PIN_ACCEPT, KEY_ENTER, DOOM_INPUT_ACTION_ACCEPT},
    {DOOM_INPUT_PIN_BACK, KEY_BACKSPACE, DOOM_INPUT_ACTION_BACK},
    {DOOM_INPUT_PIN_NONE, KEY_RALT, DOOM_INPUT_ACTION_STRAFE},
    {DOOM_INPUT_PIN_NONE, ',', DOOM_INPUT_ACTION_STRAFE_LEFT},
    {DOOM_INPUT_PIN_NONE, '.', DOOM_INPUT_ACTION_STRAFE_RIGHT},
};

static bool button_pressed(uint8_t pin) {
  const bool high = hal_gpio_read(pin);
#if DOOM_INPUT_ACTIVE_LOW
  return !high;
#else
  return high;
#endif
}

static void post_key_event(int type, int key) {
  event_t event = {0};

  event.type = type;
  event.data1 = key;
  event.data2 = type == ev_keydown ? key : 0;
  event.data3 = type == ev_keydown ? GetTypedChar(key, false) : 0;
  D_PostEvent(&event);
}

static doom_input_action_mask_t read_gpio_actions(void) {
  doom_input_action_mask_t actions = 0u;

  for (size_t i = 0; i < COUNTOF(s_bindings); ++i) {
    if (s_bindings[i].pin != DOOM_INPUT_PIN_NONE &&
        button_pressed(s_bindings[i].pin)) {
      actions |= s_bindings[i].action;
    }
  }
  return actions;
}

static doom_input_action_mask_t
handle_gamepad_chords(doom_input_action_mask_t actions) {
  const doom_input_action_mask_t pairing_chord =
      DOOM_INPUT_ACTION_MENU | DOOM_INPUT_ACTION_BACK;
  const doom_input_action_mask_t factory_reset_chord =
      DOOM_INPUT_ACTION_STRAFE_LEFT | DOOM_INPUT_ACTION_STRAFE_RIGHT |
      DOOM_INPUT_ACTION_BACK;
  const doom_input_action_mask_t factory_pair_strafe =
      DOOM_INPUT_ACTION_STRAFE_LEFT | DOOM_INPUT_ACTION_STRAFE_RIGHT;
  const doom_input_action_mask_t factory_pair_left_back =
      DOOM_INPUT_ACTION_STRAFE_LEFT | DOOM_INPUT_ACTION_BACK;
  const doom_input_action_mask_t factory_pair_right_back =
      DOOM_INPUT_ACTION_STRAFE_RIGHT | DOOM_INPUT_ACTION_BACK;
  const uint32_t now = hal_millis();

  if ((actions & factory_reset_chord) == factory_reset_chord) {
    s_factory_reset_chord_guard_active = true;
    if (!s_factory_reset_chord_active) {
      s_factory_reset_chord_active = true;
      s_factory_reset_chord_started_ms = now;
    }
    if (!s_factory_reset_chord_consumed &&
        hal_elapsed_u32(now, s_factory_reset_chord_started_ms,
                        DOOM_GAMEPAD_FACTORY_RESET_HOLD_MS)) {
      s_factory_reset_chord_consumed = true;
      DoomGamepadInput_RequestForget();
    }
    return actions & ~factory_reset_chord;
  }

  if (s_factory_reset_chord_guard_active) {
    const bool factory_buttons_released = (actions & factory_reset_chord) == 0u;
    actions &= ~factory_reset_chord;
    if (factory_buttons_released) {
      s_factory_reset_chord_guard_active = false;
      s_factory_reset_chord_active = false;
      s_factory_reset_chord_consumed = false;
    }
    return actions;
  }

  s_factory_reset_chord_active = false;
  s_factory_reset_chord_consumed = false;
  const bool both_pressed = (actions & pairing_chord) == pairing_chord;

  if (both_pressed) {
    if (!s_pairing_chord_active) {
      s_pairing_chord_active = true;
      s_pairing_chord_started_ms = now;
    }
    if (!s_pairing_chord_consumed &&
        hal_elapsed_u32(now, s_pairing_chord_started_ms,
                        DOOM_GAMEPAD_PAIRING_HOLD_MS)) {
      s_pairing_chord_consumed = true;
      DoomGamepadInput_RequestPairing();
    }
    return actions & ~pairing_chord;
  }

  if ((actions & factory_pair_strafe) == factory_pair_strafe ||
      (actions & factory_pair_left_back) == factory_pair_left_back ||
      (actions & factory_pair_right_back) == factory_pair_right_back) {
    s_factory_reset_chord_guard_active = true;
    return actions & ~factory_reset_chord;
  }

  if (s_pairing_chord_consumed) {
    const bool chord_released = (actions & pairing_chord) == 0u;
    actions &= ~pairing_chord;
    if (chord_released) {
      s_pairing_chord_active = false;
      s_pairing_chord_consumed = false;
    }
    return actions;
  }

  s_pairing_chord_active = false;
  return actions;
}

static void publish_action_changes(doom_input_action_mask_t actions) {
  const doom_input_action_mask_t changed = actions ^ s_published_actions;

  for (size_t i = 0; i < COUNTOF(s_bindings); ++i) {
    if ((changed & s_bindings[i].action) != 0u &&
        (s_published_actions & s_bindings[i].action) != 0u) {
      post_key_event(ev_keyup, s_bindings[i].key);
    }
  }
  for (size_t i = 0; i < COUNTOF(s_bindings); ++i) {
    if ((changed & s_bindings[i].action) != 0u &&
        (actions & s_bindings[i].action) != 0u) {
      post_key_event(ev_keydown, s_bindings[i].key);
    }
  }
  s_published_actions = actions;
}

void I_InputInit(void) {
  for (size_t i = 0; i < COUNTOF(s_bindings); ++i) {
    if (s_bindings[i].pin == DOOM_INPUT_PIN_NONE) {
      continue;
    }
#if DOOM_INPUT_ACTIVE_LOW
    hal_gpio_set_mode(s_bindings[i].pin, HAL_GPIO_INPUT_PULLUP);
#else
    hal_gpio_set_mode(s_bindings[i].pin, HAL_GPIO_INPUT_PULLDOWN);
#endif
  }

  DoomGamepadInput_Init();
  s_input_initialized = true;
}

void I_BindInputVariables(void) {}
void I_ReadMouse(void) {}

void I_StartTextInput(int x1, int y1, int x2, int y2) {
  (void)x1;
  (void)y1;
  (void)x2;
  (void)y2;
  s_text_input_enabled = true;
}

void I_StopTextInput(void) { s_text_input_enabled = false; }

int GetTypedChar(int scancode, boolean shiftdown) {
  (void)shiftdown;

  if (!s_text_input_enabled) {
    return 0;
  }

  if (scancode >= 32 && scancode < 127) {
    return scancode;
  }

  switch (scancode) {
  case KEY_ENTER:
  case KEY_BACKSPACE:
  case KEY_ESCAPE:
    return scancode;
  default:
    return 0;
  }
}

void I_GetEventTimeout(int timeout_ms) {
  (void)timeout_ms;

  if (!s_input_initialized) {
    I_InputInit();
  }

  const doom_input_action_mask_t gpio_actions = read_gpio_actions();
  const doom_input_action_mask_t gamepad_actions = DoomGamepadInput_Service();
  publish_action_changes(handle_gamepad_chords(gpio_actions | gamepad_actions));
}

void I_GetEvent(void) { I_GetEventTimeout(0); }
