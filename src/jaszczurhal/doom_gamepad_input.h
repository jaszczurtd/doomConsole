#pragma once

#include <stdint.h>

typedef uint16_t doom_input_action_mask_t;

enum {
    DOOM_INPUT_ACTION_UP = (1u << 0),
    DOOM_INPUT_ACTION_DOWN = (1u << 1),
    DOOM_INPUT_ACTION_LEFT = (1u << 2),
    DOOM_INPUT_ACTION_RIGHT = (1u << 3),
    DOOM_INPUT_ACTION_FIRE = (1u << 4),
    DOOM_INPUT_ACTION_USE = (1u << 5),
    DOOM_INPUT_ACTION_MENU = (1u << 6),
    DOOM_INPUT_ACTION_ACCEPT = (1u << 7),
    DOOM_INPUT_ACTION_BACK = (1u << 8),
};

void DoomGamepadInput_Init(void);
void DoomGamepadInput_RequestPairing(void);
doom_input_action_mask_t DoomGamepadInput_Service(void);
