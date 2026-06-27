/*
 * Minimal JaszczurHAL GPIO input backend.
 *
 * Buttons are active-low by default: each input pin uses the internal pull-up
 * and the physical button should short the pin to GND when pressed.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/hal_gpio.h>

#include "d_event.h"
#include "doomkeys.h"
#include "doomtype.h"
#include "i_input.h"

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

typedef struct {
    uint8_t pin;
    int key;
    bool pressed;
} doom_gpio_button_t;

float mouse_acceleration = 2.0f;
int mouse_threshold = 10;

static bool s_text_input_enabled;
static bool s_input_initialized;

static doom_gpio_button_t s_buttons[] = {
    { DOOM_INPUT_PIN_UP, KEY_UPARROW, false },
    { DOOM_INPUT_PIN_DOWN, KEY_DOWNARROW, false },
    { DOOM_INPUT_PIN_LEFT, KEY_LEFTARROW, false },
    { DOOM_INPUT_PIN_RIGHT, KEY_RIGHTARROW, false },
    { DOOM_INPUT_PIN_FIRE, KEY_RCTRL, false },
    { DOOM_INPUT_PIN_USE, ' ', false },
    { DOOM_INPUT_PIN_MENU, KEY_ESCAPE, false },
    { DOOM_INPUT_PIN_ACCEPT, KEY_ENTER, false },
    { DOOM_INPUT_PIN_BACK, KEY_BACKSPACE, false },
};

static bool button_pressed(uint8_t pin)
{
    const bool high = hal_gpio_read(pin);
#if DOOM_INPUT_ACTIVE_LOW
    return !high;
#else
    return high;
#endif
}

static void post_key_event(int type, int key)
{
    event_t event;

    event.type = type;
    event.data1 = key;
    event.data2 = type == ev_keydown ? key : 0;
    event.data3 = type == ev_keydown ? GetTypedChar(key, false) : 0;
    D_PostEvent(&event);
}

void I_InputInit(void)
{
    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
#if DOOM_INPUT_ACTIVE_LOW
        hal_gpio_set_mode(s_buttons[i].pin, HAL_GPIO_INPUT_PULLUP);
#else
        hal_gpio_set_mode(s_buttons[i].pin, HAL_GPIO_INPUT_PULLDOWN);
#endif
        s_buttons[i].pressed = button_pressed(s_buttons[i].pin);
    }

    s_input_initialized = true;
}

void I_BindInputVariables(void) {}
void I_ReadMouse(void) {}

void I_StartTextInput(int x1, int y1, int x2, int y2)
{
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    s_text_input_enabled = true;
}

void I_StopTextInput(void)
{
    s_text_input_enabled = false;
}

int GetTypedChar(int scancode, boolean shiftdown)
{
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

void I_GetEventTimeout(int timeout_ms)
{
    (void)timeout_ms;

    if (!s_input_initialized) {
        I_InputInit();
    }

    for (size_t i = 0; i < sizeof(s_buttons) / sizeof(s_buttons[0]); ++i) {
        const bool pressed = button_pressed(s_buttons[i].pin);

        if (pressed == s_buttons[i].pressed) {
            continue;
        }

        s_buttons[i].pressed = pressed;
        post_key_event(pressed ? ev_keydown : ev_keyup, s_buttons[i].key);
    }
}

void I_GetEvent(void)
{
    I_GetEventTimeout(0);
}
