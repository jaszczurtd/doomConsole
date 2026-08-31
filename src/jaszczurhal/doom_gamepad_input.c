#include "jaszczurhal/doom_gamepad_input.h"

#ifdef HAL_ENABLE_BLUETOOTH_GAMEPAD

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/bluetooth/hal_gamepad.h>
#include <hal/core/hal_status.h>
#include <utils/tools_api.h>

#include "doom_main_config.h"

enum {
    ZERO2_BUTTON_A = 0x0001u,
    ZERO2_BUTTON_B = 0x0002u,
    ZERO2_BUTTON_X = 0x0008u,
    ZERO2_BUTTON_START = 0x0800u,
    ZERO2_BUTTON_SELECT = 0x0400u,
    GAMEPAD_AXIS_ACTIVE_THRESHOLD = 16384,
};

static hal_gamepad_t s_gamepad;
static doom_input_action_mask_t s_actions;
static hal_gamepad_state_t s_previous_state = HAL_GAMEPAD_STATE_UNINITIALIZED;
static hal_status_t s_previous_error = HAL_NONE;
static uint32_t s_generation;
static bool s_generation_valid;
static bool s_neutral_seen;
static bool s_pairing_requested;
static bool s_pairing_locally_approved;
static bool s_authorization_sent;
static bool s_reconnect_started;

static void report_error(const char* operation, hal_status_t status)
{
    if (status == s_previous_error) {
        return;
    }

    s_previous_error = status;
    hal_derr(
        "[gamepad] %s failed: %s", operation, hal_status_to_string(status));
}

static doom_input_action_mask_t map_snapshot(
    const hal_gamepad_snapshot_t* snapshot)
{
    doom_input_action_mask_t actions = 0u;
    uint8_t directions = snapshot->dpad;

    if ((directions & (HAL_GAMEPAD_DPAD_LEFT | HAL_GAMEPAD_DPAD_RIGHT)) == 0u
        && (snapshot->axes_present & (1u << HAL_GAMEPAD_AXIS_X)) != 0u) {
        const int16_t x = snapshot->axes[HAL_GAMEPAD_AXIS_X];
        if (x <= -GAMEPAD_AXIS_ACTIVE_THRESHOLD) {
            directions |= HAL_GAMEPAD_DPAD_LEFT;
        } else if (x >= GAMEPAD_AXIS_ACTIVE_THRESHOLD) {
            directions |= HAL_GAMEPAD_DPAD_RIGHT;
        }
    }
    if ((directions & (HAL_GAMEPAD_DPAD_UP | HAL_GAMEPAD_DPAD_DOWN)) == 0u
        && (snapshot->axes_present & (1u << HAL_GAMEPAD_AXIS_Y)) != 0u) {
        const int16_t y = snapshot->axes[HAL_GAMEPAD_AXIS_Y];
        if (y <= -GAMEPAD_AXIS_ACTIVE_THRESHOLD) {
            directions |= HAL_GAMEPAD_DPAD_UP;
        } else if (y >= GAMEPAD_AXIS_ACTIVE_THRESHOLD) {
            directions |= HAL_GAMEPAD_DPAD_DOWN;
        }
    }

    if ((directions & HAL_GAMEPAD_DPAD_UP) != 0u) {
        actions |= DOOM_INPUT_ACTION_UP;
    }
    if ((directions & HAL_GAMEPAD_DPAD_DOWN) != 0u) {
        actions |= DOOM_INPUT_ACTION_DOWN;
    }
    if ((directions & HAL_GAMEPAD_DPAD_LEFT) != 0u) {
        actions |= DOOM_INPUT_ACTION_LEFT;
    }
    if ((directions & HAL_GAMEPAD_DPAD_RIGHT) != 0u) {
        actions |= DOOM_INPUT_ACTION_RIGHT;
    }
    if ((snapshot->buttons & ZERO2_BUTTON_A) != 0u) {
        actions |= DOOM_INPUT_ACTION_FIRE;
    }
    if ((snapshot->buttons & ZERO2_BUTTON_B) != 0u) {
        actions |= DOOM_INPUT_ACTION_USE;
    }
    if ((snapshot->buttons & ZERO2_BUTTON_START) != 0u) {
        actions |= DOOM_INPUT_ACTION_MENU;
    }
    if ((snapshot->buttons & ZERO2_BUTTON_X) != 0u) {
        actions |= DOOM_INPUT_ACTION_ACCEPT;
    }
    if ((snapshot->buttons & ZERO2_BUTTON_SELECT) != 0u) {
        actions |= DOOM_INPUT_ACTION_BACK;
    }
    return actions;
}

static void accept_snapshot(const hal_gamepad_snapshot_t* snapshot)
{
    if (!snapshot->connected) {
        s_actions = 0u;
        s_neutral_seen = false;
        return;
    }

    if (!s_generation_valid || snapshot->generation != s_generation) {
        s_generation = snapshot->generation;
        s_generation_valid = true;
        s_neutral_seen = false;
        s_actions = 0u;
    }

    const doom_input_action_mask_t actions = map_snapshot(snapshot);
    if (!s_neutral_seen) {
        if (actions == 0u) {
            s_neutral_seen = true;
        }
        s_actions = 0u;
        return;
    }
    s_actions = actions;
}

static void drain_snapshots(void)
{
    for (;;) {
        hal_gamepad_snapshot_t snapshot = { 0 };
        const hal_status_t status
            = hal_gamepad_snapshot_next(s_gamepad, &snapshot);

        if (status == HAL_OK) {
            accept_snapshot(&snapshot);
            continue;
        }
        if (status == HAL_EOVERFLOW) {
            s_actions = 0u;
            s_neutral_seen = false;
            continue;
        }
        if (status != HAL_EAGAIN) {
            s_actions = 0u;
            s_neutral_seen = false;
            report_error("snapshot", status);
        }
        return;
    }
}

static void service_profile_state(const hal_gamepad_info_t* info)
{
    bool pairing_opened = false;

    if (info->state != s_previous_state) {
        hal_deb("[gamepad] state=%d status=%s known=%u pairing=%u",
            (int)info->state, hal_status_to_string(info->last_status),
            info->known_device ? 1u : 0u, info->pairing_window_open ? 1u : 0u);
        s_previous_state = info->state;
    }

    if (info->state != HAL_GAMEPAD_STATE_CONNECTED) {
        s_actions = 0u;
        s_neutral_seen = false;
    } else {
        s_reconnect_started = false;
        s_pairing_requested = false;
        s_pairing_locally_approved = false;
    }

    if (s_pairing_requested && info->state == HAL_GAMEPAD_STATE_READY) {
        const hal_status_t status = hal_gamepad_pairing_open(s_gamepad);
        if (status == HAL_OK) {
            s_pairing_requested = false;
            s_pairing_locally_approved = true;
            s_authorization_sent = false;
            s_reconnect_started = false;
            pairing_opened = true;
            hal_deb("[gamepad] 120-second pairing window opened");
        } else {
            report_error("pairing window", status);
        }
    }

    if (info->pairing_pending && s_pairing_locally_approved
        && !s_authorization_sent) {
        const hal_status_t status = hal_gamepad_pairing_authorize(s_gamepad);
        if (status == HAL_OK) {
            s_authorization_sent = true;
            hal_deb("[gamepad] local pairing authorization accepted");
        } else {
            report_error("pairing authorization", status);
        }
    } else if (!info->pairing_pending) {
        s_authorization_sent = false;
    }

    if (info->state == HAL_GAMEPAD_STATE_READY && info->known_device
        && !s_pairing_requested && !s_reconnect_started) {
        const hal_status_t status = hal_gamepad_reconnect(s_gamepad);
        if (status == HAL_OK) {
            s_reconnect_started = true;
            hal_deb("[gamepad] reconnect requested");
        } else {
            report_error("reconnect", status);
        }
    }

    if (!pairing_opened && s_pairing_locally_approved
        && info->state == HAL_GAMEPAD_STATE_READY && !info->pairing_window_open
        && !info->pairing_pending && !info->known_device) {
        s_pairing_locally_approved = false;
    }
}

static void queue_pairing_request(const char* source)
{
    s_pairing_requested = true;
    s_pairing_locally_approved = false;
    s_authorization_sent = false;
    hal_deb("[gamepad] %s pairing request queued", source);
}

void DoomGamepadInput_Init(void)
{
    if (s_gamepad != NULL) {
        return;
    }

    const hal_status_t status = hal_gamepad_open(&s_gamepad);
    if (status == HAL_OK) {
        s_previous_error = HAL_NONE;
#if BT_AUTOMATIC_PAIRING
        queue_pairing_request("automatic");
        hal_deb("[gamepad] profile opened; automatic pairing enabled");
#else
        hal_deb("[gamepad] profile opened; hold GPIO Menu+Back for pairing");
#endif
    } else {
        s_gamepad = NULL;
        report_error("open", status);
    }
}

void DoomGamepadInput_RequestPairing(void)
{
    queue_pairing_request("local");
}

doom_input_action_mask_t DoomGamepadInput_Service(void)
{
    if (s_gamepad == NULL) {
        return 0u;
    }

    const hal_status_t poll_status = hal_gamepad_poll(s_gamepad);
    if (poll_status != HAL_OK && poll_status != HAL_EOVERFLOW) {
        s_actions = 0u;
        s_neutral_seen = false;
        report_error("poll", poll_status);
        return 0u;
    }
    if (poll_status == HAL_EOVERFLOW) {
        s_actions = 0u;
        s_neutral_seen = false;
    }

    hal_gamepad_info_t info = { 0 };
    const hal_status_t info_status = hal_gamepad_get_info(s_gamepad, &info);
    if (info_status != HAL_OK) {
        s_actions = 0u;
        s_neutral_seen = false;
        report_error("state", info_status);
        return 0u;
    }

    s_previous_error = HAL_NONE;
    service_profile_state(&info);
    drain_snapshots();
    if (info.state != HAL_GAMEPAD_STATE_CONNECTED) {
        s_actions = 0u;
        s_neutral_seen = false;
    }
    return s_actions;
}

#else

void DoomGamepadInput_Init(void) { }
void DoomGamepadInput_RequestPairing(void) { }

doom_input_action_mask_t DoomGamepadInput_Service(void) { return 0u; }

#endif
