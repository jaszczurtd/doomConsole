#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/bluetooth/hal_gamepad.h>

#include "doom_main_config.h"
#include "jaszczurhal/doom_gamepad_input.h"

enum {
    ZERO2_BUTTON_A = 0x0001u,
    ZERO2_BUTTON_B = 0x0002u,
    ZERO2_BUTTON_X = 0x0008u,
    ZERO2_BUTTON_Y = 0x0010u,
    ZERO2_BUTTON_L = 0x0040u,
    ZERO2_BUTTON_R = 0x0080u,
    ZERO2_BUTTON_SELECT = 0x0400u,
    ZERO2_BUTTON_START = 0x0800u,
    SNAPSHOT_CAPACITY = 16,
};

typedef struct {
    hal_status_t status;
    hal_gamepad_snapshot_t snapshot;
} queued_snapshot_t;

static hal_gamepad_info_t s_info;
static queued_snapshot_t s_snapshots[SNAPSHOT_CAPACITY];
static size_t s_snapshot_head;
static size_t s_snapshot_count;
static unsigned s_open_calls;
static unsigned s_pairing_open_calls;
static unsigned s_authorize_calls;
static unsigned s_reconnect_calls;

static void queue_result(hal_status_t status, hal_gamepad_snapshot_t snapshot)
{
    assert(s_snapshot_count < SNAPSHOT_CAPACITY);
    const size_t slot
        = (s_snapshot_head + s_snapshot_count) % SNAPSHOT_CAPACITY;
    s_snapshots[slot].status = status;
    s_snapshots[slot].snapshot = snapshot;
    ++s_snapshot_count;
}

static hal_gamepad_snapshot_t snapshot(
    uint32_t generation, uint32_t buttons, uint8_t dpad)
{
    hal_gamepad_snapshot_t result = { 0 };
    result.generation = generation;
    result.buttons = buttons;
    result.dpad = dpad;
    result.connected = true;
    return result;
}

static doom_input_action_mask_t service_connected(void)
{
    s_info.state = HAL_GAMEPAD_STATE_CONNECTED;
    s_info.known_device = true;
    return DoomGamepadInput_Service();
}

hal_status_t hal_gamepad_open(hal_gamepad_t* out_gamepad)
{
    ++s_open_calls;
    *out_gamepad = (hal_gamepad_t)(uintptr_t)1u;
    return HAL_OK;
}

hal_status_t hal_gamepad_poll(hal_gamepad_t gamepad)
{
    assert(gamepad != NULL);
    return HAL_OK;
}

hal_status_t hal_gamepad_get_info(
    hal_gamepad_t gamepad, hal_gamepad_info_t* out_info)
{
    assert(gamepad != NULL);
    *out_info = s_info;
    return HAL_OK;
}

hal_status_t hal_gamepad_snapshot_next(
    hal_gamepad_t gamepad, hal_gamepad_snapshot_t* out_snapshot)
{
    assert(gamepad != NULL);
    if (s_snapshot_count == 0u) {
        return HAL_EAGAIN;
    }

    const queued_snapshot_t queued = s_snapshots[s_snapshot_head];
    s_snapshot_head = (s_snapshot_head + 1u) % SNAPSHOT_CAPACITY;
    --s_snapshot_count;
    if (queued.status == HAL_OK) {
        *out_snapshot = queued.snapshot;
    }
    return queued.status;
}

hal_status_t hal_gamepad_pairing_open(hal_gamepad_t gamepad)
{
    assert(gamepad != NULL);
    ++s_pairing_open_calls;
    return HAL_OK;
}

hal_status_t hal_gamepad_pairing_authorize(hal_gamepad_t gamepad)
{
    assert(gamepad != NULL);
    ++s_authorize_calls;
    return HAL_OK;
}

hal_status_t hal_gamepad_reconnect(hal_gamepad_t gamepad)
{
    assert(gamepad != NULL);
    ++s_reconnect_calls;
    return HAL_OK;
}

void hal_deb(const char* format, ...) { (void)format; }

void hal_derr(const char* format, ...) { (void)format; }

int main(void)
{
    DoomGamepadInput_Init();
    DoomGamepadInput_Init();
    assert(s_open_calls == 1u);

    s_info.state = HAL_GAMEPAD_STATE_READY;
    assert(DoomGamepadInput_Service() == 0u);
#if BT_AUTOMATIC_PAIRING
    assert(s_pairing_open_calls == 1u);
#else
    assert(s_pairing_open_calls == 0u);
#endif
    assert(s_reconnect_calls == 0u);

    DoomGamepadInput_RequestPairing();
    assert(DoomGamepadInput_Service() == 0u);
#if BT_AUTOMATIC_PAIRING
    assert(s_pairing_open_calls == 2u);
#else
    assert(s_pairing_open_calls == 1u);
#endif

    s_info.state = HAL_GAMEPAD_STATE_DISCOVERING;
    s_info.pairing_window_open = true;
    s_info.pairing_pending = true;
    assert(DoomGamepadInput_Service() == 0u);
    assert(s_authorize_calls == 1u);
    assert(DoomGamepadInput_Service() == 0u);
    assert(s_authorize_calls == 1u);

    queue_result(HAL_OK, snapshot(1u, ZERO2_BUTTON_A, HAL_GAMEPAD_DPAD_UP));
    assert(service_connected() == 0u);

    queue_result(HAL_OK, snapshot(1u, 0u, HAL_GAMEPAD_DPAD_NONE));
    assert(service_connected() == 0u);

    hal_gamepad_snapshot_t active = snapshot(1u,
        ZERO2_BUTTON_A | ZERO2_BUTTON_B | ZERO2_BUTTON_X | ZERO2_BUTTON_Y
            | ZERO2_BUTTON_L | ZERO2_BUTTON_R | ZERO2_BUTTON_START
            | ZERO2_BUTTON_SELECT,
        HAL_GAMEPAD_DPAD_UP | HAL_GAMEPAD_DPAD_LEFT);
    queue_result(HAL_OK, active);
    assert(service_connected()
        == (DOOM_INPUT_ACTION_UP | DOOM_INPUT_ACTION_LEFT
            | DOOM_INPUT_ACTION_FIRE | DOOM_INPUT_ACTION_USE
            | DOOM_INPUT_ACTION_MENU | DOOM_INPUT_ACTION_ACCEPT
            | DOOM_INPUT_ACTION_BACK | DOOM_INPUT_ACTION_STRAFE
            | DOOM_INPUT_ACTION_STRAFE_LEFT
            | DOOM_INPUT_ACTION_STRAFE_RIGHT));

    hal_gamepad_snapshot_t axes = snapshot(1u, 0u, 0u);
    axes.axes_present = (1u << HAL_GAMEPAD_AXIS_X) | (1u << HAL_GAMEPAD_AXIS_Y);
    axes.axes[HAL_GAMEPAD_AXIS_X] = 20000;
    axes.axes[HAL_GAMEPAD_AXIS_Y] = 20000;
    queue_result(HAL_OK, axes);
    assert(service_connected()
        == (DOOM_INPUT_ACTION_RIGHT | DOOM_INPUT_ACTION_DOWN));

    s_info.state = HAL_GAMEPAD_STATE_READY;
    s_info.pairing_window_open = false;
    s_info.pairing_pending = false;
    s_info.known_device = true;
    queue_result(HAL_OK, active);
    assert(DoomGamepadInput_Service() == 0u);
    assert(s_reconnect_calls == 1u);
    assert(DoomGamepadInput_Service() == 0u);
    assert(s_reconnect_calls == 1u);

    queue_result(HAL_OK, snapshot(2u, ZERO2_BUTTON_A, 0u));
    assert(service_connected() == 0u);
    queue_result(HAL_OK, snapshot(2u, 0u, 0u));
    assert(service_connected() == 0u);
    queue_result(HAL_OK, snapshot(2u, ZERO2_BUTTON_A, 0u));
    assert(service_connected() == DOOM_INPUT_ACTION_FIRE);

    queue_result(HAL_EOVERFLOW, (hal_gamepad_snapshot_t) { 0 });
    queue_result(HAL_OK, snapshot(2u, ZERO2_BUTTON_A, 0u));
    assert(service_connected() == 0u);
    queue_result(HAL_OK, snapshot(2u, 0u, 0u));
    assert(service_connected() == 0u);
    queue_result(HAL_OK, snapshot(2u, ZERO2_BUTTON_A, 0u));
    assert(service_connected() == DOOM_INPUT_ACTION_FIRE);

    return 0;
}
