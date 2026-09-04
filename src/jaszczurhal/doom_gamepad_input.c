#include "jaszczurhal/doom_gamepad_input.h"

#ifdef HAL_ENABLE_BLUETOOTH_GAMEPAD

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/bluetooth/hal_gamepad.h>
#include <hal/core/hal_status.h>
#include <hal/serial/hal_serial.h>
#include <hal/system/hal_system.h>
#ifdef HAL_ENABLE_KV
#include <hal/bluetooth/jh_gamepad_bond_kv_provider.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>
#endif

#include "doom_main_config.h"
#include "jaszczurhal/doom_sound_hal.h"

enum {
  ZERO2_BUTTON_A = 0x0001u,
  ZERO2_BUTTON_B = 0x0002u,
  ZERO2_BUTTON_X = 0x0008u,
  ZERO2_BUTTON_Y = 0x0010u,
  ZERO2_BUTTON_L = 0x0040u,
  ZERO2_BUTTON_R = 0x0080u,
  ZERO2_BUTTON_START = 0x0800u,
  ZERO2_BUTTON_SELECT = 0x0400u,
  GAMEPAD_AXIS_ACTIVE_THRESHOLD = 16384,
  GAMEPAD_PEER_RECONNECT_GRACE_MS = 10000,
  GAMEPAD_RECONNECT_RETRY_MS = 5000,
  GAMEPAD_INPUT_TRACE_LIMIT = 32,
};

static hal_gamepad_t s_gamepad;
static doom_input_action_mask_t s_actions;
static hal_gamepad_state_t s_previous_state = HAL_GAMEPAD_STATE_UNINITIALIZED;
static uint32_t s_generation;
static bool s_generation_valid;
static bool s_neutral_seen;
static bool s_pairing_requested;
static bool s_pairing_locally_approved;
static bool s_authorization_sent;
static bool s_reconnect_started;
static bool s_reconnect_retry_pending;
static uint32_t s_reconnect_retry_started_ms;
static uint32_t s_reconnect_retry_delay_ms;
static bool s_forget_requested;
static bool s_forget_waiting;
static bool s_reconnect_blocked;
static bool s_input_trace_valid;
static uint8_t s_input_trace_count;
static bool s_input_trace_suppressed;
static uint32_t s_input_trace_generation;
static uint32_t s_input_trace_buttons;
static uint16_t s_input_trace_axes_present;
static uint8_t s_input_trace_dpad;
static int16_t s_input_trace_x;
static int16_t s_input_trace_y;
static doom_input_action_mask_t s_input_trace_actions;
#if BT_AUTOMATIC_PAIRING
static bool s_automatic_pairing_pending;
#endif
#ifdef HAL_ENABLE_KV
static jh_gamepad_bond_kv_context_t s_bond_context;
static hal_gamepad_bond_provider_t s_kv_bond_provider;

static hal_status_t load_bond(void *context,
                              hal_gamepad_bond_blob_t *out_blob) {
  (void)context;
  return s_kv_bond_provider.load(s_kv_bond_provider.context, out_blob);
}

static hal_status_t store_bond(void *context,
                               const hal_gamepad_bond_blob_t *blob) {
  (void)context;
  const bool resume_audio = I_PicoSoundSuspendForFlash();
  const hal_status_t status =
      s_kv_bond_provider.store(s_kv_bond_provider.context, blob);
  I_PicoSoundResumeAfterFlash(resume_audio);
  return status;
}

static hal_status_t erase_bond(void *context) {
  (void)context;
  const bool resume_audio = I_PicoSoundSuspendForFlash();
  const hal_status_t status =
      s_kv_bond_provider.erase(s_kv_bond_provider.context);
  I_PicoSoundResumeAfterFlash(resume_audio);
  return status;
}
#endif

static void queue_pairing_request(const char *source);

static void report_error(const char *operation, hal_status_t status) {
  hal_derr_limited(operation, "[gamepad] failed: %s",
                   hal_status_to_string(status));
}

static void schedule_reconnect_retry(hal_status_t status) {
  s_reconnect_started = false;
  s_reconnect_retry_pending = true;
  s_reconnect_retry_started_ms = hal_millis();
  s_reconnect_retry_delay_ms = GAMEPAD_RECONNECT_RETRY_MS;
  hal_derr_limited("gamepad reconnect",
                   "[gamepad] reconnect attempt ended: %s; retry in %u ms",
                   hal_status_to_string(status),
                   (unsigned int)GAMEPAD_RECONNECT_RETRY_MS);
}

static doom_input_action_mask_t
map_snapshot(const hal_gamepad_snapshot_t *snapshot) {
  doom_input_action_mask_t actions = 0u;
  uint8_t directions = snapshot->dpad;

  if ((directions & (HAL_GAMEPAD_DPAD_LEFT | HAL_GAMEPAD_DPAD_RIGHT)) == 0u &&
      (snapshot->axes_present & (1u << HAL_GAMEPAD_AXIS_X)) != 0u) {
    const int16_t x = snapshot->axes[HAL_GAMEPAD_AXIS_X];
    if (x <= -GAMEPAD_AXIS_ACTIVE_THRESHOLD) {
      directions |= HAL_GAMEPAD_DPAD_LEFT;
    } else if (x >= GAMEPAD_AXIS_ACTIVE_THRESHOLD) {
      directions |= HAL_GAMEPAD_DPAD_RIGHT;
    }
  }
  if ((directions & (HAL_GAMEPAD_DPAD_UP | HAL_GAMEPAD_DPAD_DOWN)) == 0u &&
      (snapshot->axes_present & (1u << HAL_GAMEPAD_AXIS_Y)) != 0u) {
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
  if ((snapshot->buttons & ZERO2_BUTTON_Y) != 0u) {
    actions |= DOOM_INPUT_ACTION_STRAFE;
  }
  if ((snapshot->buttons & ZERO2_BUTTON_L) != 0u) {
    actions |= DOOM_INPUT_ACTION_STRAFE_LEFT;
  }
  if ((snapshot->buttons & ZERO2_BUTTON_R) != 0u) {
    actions |= DOOM_INPUT_ACTION_STRAFE_RIGHT;
  }
  return actions;
}

static void accept_snapshot(const hal_gamepad_snapshot_t *snapshot) {
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
    s_input_trace_count = 0u;
    s_input_trace_suppressed = false;
  }

  const doom_input_action_mask_t actions = map_snapshot(snapshot);
  if (!s_input_trace_valid ||
      snapshot->generation != s_input_trace_generation ||
      snapshot->buttons != s_input_trace_buttons ||
      snapshot->axes_present != s_input_trace_axes_present ||
      snapshot->dpad != s_input_trace_dpad ||
      snapshot->axes[HAL_GAMEPAD_AXIS_X] != s_input_trace_x ||
      snapshot->axes[HAL_GAMEPAD_AXIS_Y] != s_input_trace_y ||
      actions != s_input_trace_actions) {
    if (s_input_trace_count < GAMEPAD_INPUT_TRACE_LIMIT) {
      hal_deb("[gamepad] input generation=%lu buttons=0x%08lx "
              "dpad=0x%02x axes=0x%04x x=%d y=%d actions=0x%04x",
              (unsigned long)snapshot->generation,
              (unsigned long)snapshot->buttons, (unsigned int)snapshot->dpad,
              (unsigned int)snapshot->axes_present,
              (int)snapshot->axes[HAL_GAMEPAD_AXIS_X],
              (int)snapshot->axes[HAL_GAMEPAD_AXIS_Y], (unsigned int)actions);
      ++s_input_trace_count;
    } else if (!s_input_trace_suppressed) {
      hal_deb("[gamepad] parsed input trace limit reached");
      s_input_trace_suppressed = true;
    }
    s_input_trace_valid = true;
    s_input_trace_generation = snapshot->generation;
    s_input_trace_buttons = snapshot->buttons;
    s_input_trace_axes_present = snapshot->axes_present;
    s_input_trace_dpad = snapshot->dpad;
    s_input_trace_x = snapshot->axes[HAL_GAMEPAD_AXIS_X];
    s_input_trace_y = snapshot->axes[HAL_GAMEPAD_AXIS_Y];
    s_input_trace_actions = actions;
  }
  if (!s_neutral_seen) {
    if (actions == 0u) {
      s_neutral_seen = true;
    }
    s_actions = 0u;
    return;
  }
  s_actions = actions;
}

static void drain_snapshots(void) {
  for (;;) {
    hal_gamepad_snapshot_t snapshot = {0};
    const hal_status_t status = hal_gamepad_snapshot_next(s_gamepad, &snapshot);

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

static void service_profile_state(const hal_gamepad_info_t *info) {
  bool pairing_opened = false;
  const bool entered_ready =
      info->state == HAL_GAMEPAD_STATE_READY && info->state != s_previous_state;

  if (info->state != s_previous_state) {
    hal_deb("[gamepad] state=%d status=%s known=%u pairing=%u",
            (int)info->state, hal_status_to_string(info->last_status),
            info->known_device ? 1u : 0u, info->pairing_window_open ? 1u : 0u);
    s_previous_state = info->state;
  }

  if (entered_ready && info->known_device && !s_reconnect_started &&
      !s_reconnect_retry_pending) {
    s_reconnect_retry_pending = true;
    s_reconnect_retry_started_ms = hal_millis();
    s_reconnect_retry_delay_ms = GAMEPAD_PEER_RECONNECT_GRACE_MS;
    hal_deb("[gamepad] waiting %u ms for peer-initiated reconnect",
            (unsigned int)GAMEPAD_PEER_RECONNECT_GRACE_MS);
  }

#if BT_AUTOMATIC_PAIRING
  if (s_automatic_pairing_pending && info->state == HAL_GAMEPAD_STATE_READY) {
    s_automatic_pairing_pending = false;
    if (!info->known_device) {
      queue_pairing_request("automatic");
    }
  }

  if (info->known_device && info->pairing_pending && s_reconnect_started &&
      !s_pairing_locally_approved) {
    s_pairing_locally_approved = true;
    s_authorization_sent = false;
    hal_deb("[gamepad] known peer requested re-pairing during reconnect");
  }
#endif

  if (info->state == HAL_GAMEPAD_STATE_READY && s_reconnect_started) {
    schedule_reconnect_retry(info->last_status);
  }

  if (info->state != HAL_GAMEPAD_STATE_CONNECTED) {
    s_actions = 0u;
    s_neutral_seen = false;
  } else {
    s_reconnect_started = false;
    s_reconnect_retry_pending = false;
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

  if (info->pairing_pending && s_pairing_locally_approved &&
      !s_authorization_sent) {
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

  if (info->state == HAL_GAMEPAD_STATE_READY && info->known_device &&
      !s_pairing_requested && !s_reconnect_started && !s_reconnect_blocked &&
      (!s_reconnect_retry_pending ||
       hal_millis_deadline_expired(s_reconnect_retry_started_ms,
                                   s_reconnect_retry_delay_ms))) {
    const hal_status_t status = hal_gamepad_reconnect(s_gamepad);
    if (status == HAL_OK) {
      s_reconnect_started = true;
      s_reconnect_retry_pending = false;
      hal_deb("[gamepad] reconnect requested");
    } else {
      report_error("reconnect", status);
      schedule_reconnect_retry(status);
    }
  }

  if (!pairing_opened && s_pairing_locally_approved &&
      info->state == HAL_GAMEPAD_STATE_READY && !info->pairing_window_open &&
      !info->pairing_pending && !info->known_device) {
    s_pairing_locally_approved = false;
  }
}

static void queue_pairing_request(const char *source) {
  s_pairing_requested = true;
  s_pairing_locally_approved = false;
  s_authorization_sent = false;
  s_reconnect_blocked = false;
  s_reconnect_retry_pending = false;
  hal_deb("[gamepad] %s pairing request queued", source);
}

static hal_status_t open_gamepad(void) {
#ifdef HAL_ENABLE_KV
  uint16_t eeprom_size = 0u;
  hal_status_t status = hal_eeprom_init(HAL_EEPROM_FLASH, 0u, 0u);
  if (status == HAL_OK) {
    status = hal_eeprom_size_ex(&eeprom_size);
  }
  if (status == HAL_OK) {
    status = hal_kv_init_ex(0u, eeprom_size);
  }
  if (status == HAL_OK) {
    s_kv_bond_provider =
        jh_gamepad_bond_kv_provider(&s_bond_context, DOOM_GAMEPAD_BOND_KV_KEY);
    const hal_gamepad_bond_provider_t provider = {
        .context = &s_bond_context,
        .load = load_bond,
        .store = store_bond,
        .erase = erase_bond,
    };
    return hal_gamepad_open_ex(&s_gamepad, &provider);
  }

  report_error("persistent bond storage", status);
#endif
  return hal_gamepad_open(&s_gamepad);
}

void DoomGamepadInput_Init(void) {
  if (s_gamepad != NULL) {
    return;
  }

  const hal_status_t status = open_gamepad();
  if (status == HAL_OK) {
#if BT_AUTOMATIC_PAIRING
    s_automatic_pairing_pending = true;
    hal_deb(
        "[gamepad] profile opened; automatic pairing enabled for empty bond");
#else
    hal_deb("[gamepad] profile opened; hold GPIO Menu+Back for pairing");
#endif
  } else {
    s_gamepad = NULL;
    report_error("open", status);
  }
}

void DoomGamepadInput_RequestPairing(void) { queue_pairing_request("local"); }

void DoomGamepadInput_RequestForget(void) {
  s_forget_requested = true;
  s_forget_waiting = false;
  s_reconnect_blocked = true;
  s_pairing_requested = false;
  s_pairing_locally_approved = false;
  s_authorization_sent = false;
  s_reconnect_retry_pending = false;
}

doom_input_action_mask_t DoomGamepadInput_Service(void) {
  if (s_gamepad == NULL) {
    return 0u;
  }

  const hal_status_t poll_status = hal_gamepad_poll(s_gamepad);
  if (poll_status != HAL_OK && poll_status != HAL_EOVERFLOW &&
      poll_status != HAL_EBUSY && poll_status != HAL_EAGAIN) {
    s_actions = 0u;
    s_neutral_seen = false;
    report_error("poll", poll_status);
    return 0u;
  }
  if (poll_status == HAL_EOVERFLOW) {
    s_actions = 0u;
    s_neutral_seen = false;
  }

  if (s_forget_requested) {
    s_actions = 0u;
    s_neutral_seen = false;
    s_generation_valid = false;
    s_reconnect_started = false;
    s_reconnect_retry_pending = false;
    const hal_status_t forget_status = hal_gamepad_forget(s_gamepad);
    if (forget_status == HAL_OK) {
      s_forget_requested = false;
      s_forget_waiting = false;
      s_reconnect_blocked = false;
      hal_deb("[gamepad] persisted bond erased");
    } else if (forget_status == HAL_EBUSY) {
      if (!s_forget_waiting) {
        s_forget_waiting = true;
        hal_deb("[gamepad] waiting to erase persisted bond");
      }
    } else {
      s_forget_requested = false;
      s_forget_waiting = false;
      report_error("factory reset", forget_status);
    }
    return 0u;
  }

  hal_gamepad_info_t info = {0};
  const hal_status_t info_status = hal_gamepad_get_info(s_gamepad, &info);
  if (info_status != HAL_OK) {
    s_actions = 0u;
    s_neutral_seen = false;
    report_error("state", info_status);
    return 0u;
  }

  service_profile_state(&info);
  if (info.state == HAL_GAMEPAD_STATE_CONNECTED) {
    drain_snapshots();
  } else {
    s_actions = 0u;
    s_neutral_seen = false;
  }
  return s_actions;
}

#else

void DoomGamepadInput_Init(void) {}
void DoomGamepadInput_RequestPairing(void) {}
void DoomGamepadInput_RequestForget(void) {}

doom_input_action_mask_t DoomGamepadInput_Service(void) { return 0u; }

#endif
