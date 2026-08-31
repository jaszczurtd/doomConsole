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
