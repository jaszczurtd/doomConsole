#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/gpio/hal_gpio.h>

#include "d_event.h"
#include "doom_main_config.h"
#include "doomkeys.h"
#include "i_input.h"
#include "jaszczurhal/doom_gamepad_input.h"

enum {
    PIN_CAPACITY = 32,
    EVENT_CAPACITY = 32,
};

static bool s_pin_high[PIN_CAPACITY];
static hal_gpio_mode_t s_pin_modes[PIN_CAPACITY];
static event_t s_events[EVENT_CAPACITY];
static size_t s_event_count;
static uint32_t s_now_ms;
static doom_input_action_mask_t s_gamepad_actions;
static unsigned s_gamepad_init_calls;
static unsigned s_gamepad_service_calls;
static unsigned s_pairing_requests;

static void clear_events(void) { s_event_count = 0u; }

static void expect_event(size_t index, evtype_t type, int key)
{
    assert(index < s_event_count);
    assert(s_events[index].type == type);
    assert(s_events[index].data1 == key);
}

void hal_gpio_set_mode(uint8_t pin, hal_gpio_mode_t mode)
{
    assert(pin < PIN_CAPACITY);
    s_pin_modes[pin] = mode;
}

bool hal_gpio_read(uint8_t pin)
{
    assert(pin < PIN_CAPACITY);
    return s_pin_high[pin];
}

uint32_t hal_millis(void) { return s_now_ms; }

void D_PostEvent(event_t* event)
{
    assert(s_event_count < EVENT_CAPACITY);
    s_events[s_event_count++] = *event;
}

void DoomGamepadInput_Init(void) { ++s_gamepad_init_calls; }

void DoomGamepadInput_RequestPairing(void) { ++s_pairing_requests; }

doom_input_action_mask_t DoomGamepadInput_Service(void)
{
    ++s_gamepad_service_calls;
    return s_gamepad_actions;
}

int main(void)
{
    for (size_t pin = 0; pin < PIN_CAPACITY; ++pin) {
        s_pin_high[pin] = true;
    }

    I_GetEvent();
    assert(s_gamepad_init_calls == 1u);
    assert(s_gamepad_service_calls == 1u);
    assert(s_event_count == 0u);

    const uint8_t input_pins[] = {
        DOOM_INPUT_PIN_UP,
        DOOM_INPUT_PIN_DOWN,
        DOOM_INPUT_PIN_LEFT,
        DOOM_INPUT_PIN_RIGHT,
        DOOM_INPUT_PIN_FIRE,
        DOOM_INPUT_PIN_USE,
        DOOM_INPUT_PIN_MENU,
        DOOM_INPUT_PIN_ACCEPT,
        DOOM_INPUT_PIN_BACK,
    };
    for (size_t i = 0; i < sizeof(input_pins); ++i) {
        assert(s_pin_modes[input_pins[i]] == HAL_GPIO_INPUT_PULLUP);
    }

    s_pin_high[DOOM_INPUT_PIN_FIRE] = false;
    I_GetEvent();
    assert(s_event_count == 1u);
    expect_event(0u, ev_keydown, KEY_RCTRL);

    clear_events();
    s_gamepad_actions = DOOM_INPUT_ACTION_FIRE;
    s_pin_high[DOOM_INPUT_PIN_FIRE] = true;
    I_GetEvent();
    assert(s_event_count == 0u);

    s_gamepad_actions = 0u;
    I_GetEvent();
    assert(s_event_count == 1u);
    expect_event(0u, ev_keyup, KEY_RCTRL);

    clear_events();
    s_gamepad_actions = DOOM_INPUT_ACTION_UP | DOOM_INPUT_ACTION_DOWN
        | DOOM_INPUT_ACTION_LEFT | DOOM_INPUT_ACTION_RIGHT
        | DOOM_INPUT_ACTION_FIRE | DOOM_INPUT_ACTION_USE
        | DOOM_INPUT_ACTION_MENU | DOOM_INPUT_ACTION_ACCEPT
        | DOOM_INPUT_ACTION_BACK | DOOM_INPUT_ACTION_STRAFE
        | DOOM_INPUT_ACTION_STRAFE_LEFT | DOOM_INPUT_ACTION_STRAFE_RIGHT;
    I_GetEvent();
    assert(s_event_count == 12u);
    expect_event(0u, ev_keydown, KEY_UPARROW);
    expect_event(1u, ev_keydown, KEY_DOWNARROW);
    expect_event(2u, ev_keydown, KEY_LEFTARROW);
    expect_event(3u, ev_keydown, KEY_RIGHTARROW);
    expect_event(4u, ev_keydown, KEY_RCTRL);
    expect_event(5u, ev_keydown, ' ');
    expect_event(6u, ev_keydown, KEY_ESCAPE);
    expect_event(7u, ev_keydown, KEY_ENTER);
    expect_event(8u, ev_keydown, KEY_BACKSPACE);
    expect_event(9u, ev_keydown, KEY_RALT);
    expect_event(10u, ev_keydown, ',');
    expect_event(11u, ev_keydown, '.');

    clear_events();
    s_gamepad_actions = 0u;
    I_GetEvent();
    assert(s_event_count == 12u);
    expect_event(0u, ev_keyup, KEY_UPARROW);
    expect_event(1u, ev_keyup, KEY_DOWNARROW);
    expect_event(2u, ev_keyup, KEY_LEFTARROW);
    expect_event(3u, ev_keyup, KEY_RIGHTARROW);
    expect_event(4u, ev_keyup, KEY_RCTRL);
    expect_event(5u, ev_keyup, ' ');
    expect_event(6u, ev_keyup, KEY_ESCAPE);
    expect_event(7u, ev_keyup, KEY_ENTER);
    expect_event(8u, ev_keyup, KEY_BACKSPACE);
    expect_event(9u, ev_keyup, KEY_RALT);
    expect_event(10u, ev_keyup, ',');
    expect_event(11u, ev_keyup, '.');

    clear_events();
    s_now_ms = 100u;
    s_pin_high[DOOM_INPUT_PIN_MENU] = false;
    s_pin_high[DOOM_INPUT_PIN_BACK] = false;
    I_GetEvent();
    assert(s_event_count == 0u);
    assert(s_pairing_requests == 0u);

    s_now_ms = 3099u;
    I_GetEvent();
    assert(s_pairing_requests == 0u);
    s_now_ms = 3100u;
    I_GetEvent();
    assert(s_pairing_requests == 1u);
    assert(s_event_count == 0u);

    s_now_ms = 7000u;
    I_GetEvent();
    assert(s_pairing_requests == 1u);
    s_pin_high[DOOM_INPUT_PIN_MENU] = true;
    I_GetEvent();
    assert(s_event_count == 0u);
    s_pin_high[DOOM_INPUT_PIN_BACK] = true;
    I_GetEvent();
    assert(s_event_count == 0u);

    s_now_ms = 8000u;
    s_pin_high[DOOM_INPUT_PIN_MENU] = false;
    s_pin_high[DOOM_INPUT_PIN_BACK] = false;
    I_GetEvent();
    s_now_ms = 11000u;
    I_GetEvent();
    assert(s_pairing_requests == 2u);

    return 0;
}
