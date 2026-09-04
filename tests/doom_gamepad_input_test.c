#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <hal/bluetooth/hal_gamepad.h>
#include <hal/bluetooth/jh_gamepad_bond_kv_provider.h>
#include <hal/storage/hal_eeprom.h>
#include <hal/storage/hal_kv.h>

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
static unsigned s_forget_calls;
static unsigned s_eeprom_init_calls;
static unsigned s_kv_init_calls;
static bool s_open_received_provider;
static hal_gamepad_bond_provider_t s_open_provider;
static unsigned s_audio_suspend_calls;
static unsigned s_audio_resume_calls;
static uint32_t s_now_ms;

static void queue_result(hal_status_t status, hal_gamepad_snapshot_t snapshot) {
  assert(s_snapshot_count < SNAPSHOT_CAPACITY);
  const size_t slot = (s_snapshot_head + s_snapshot_count) % SNAPSHOT_CAPACITY;
  s_snapshots[slot].status = status;
  s_snapshots[slot].snapshot = snapshot;
  ++s_snapshot_count;
}

static hal_gamepad_snapshot_t snapshot(uint32_t generation, uint32_t buttons,
                                       uint8_t dpad) {
  hal_gamepad_snapshot_t result = {0};
  result.generation = generation;
  result.buttons = buttons;
  result.dpad = dpad;
  result.connected = true;
  return result;
}

static doom_input_action_mask_t service_connected(void) {
  s_info.state = HAL_GAMEPAD_STATE_CONNECTED;
  s_info.known_device = true;
  return DoomGamepadInput_Service();
}

hal_status_t hal_gamepad_open(hal_gamepad_t *out_gamepad) {
  ++s_open_calls;
  *out_gamepad = (hal_gamepad_t)(uintptr_t)1u;
  return HAL_OK;
}

hal_status_t hal_gamepad_open_ex(hal_gamepad_t *out_gamepad,
                                 const hal_gamepad_bond_provider_t *provider) {
  ++s_open_calls;
  s_open_received_provider = provider != NULL && provider->context != NULL &&
                             provider->load != NULL &&
                             provider->store != NULL && provider->erase != NULL;
  if (s_open_received_provider) {
    s_open_provider = *provider;
  }
  *out_gamepad = (hal_gamepad_t)(uintptr_t)1u;
  return HAL_OK;
}

static hal_status_t bond_load(void *context,
                              hal_gamepad_bond_blob_t *out_blob) {
  (void)context;
  (void)out_blob;
  return HAL_ENOENT;
}

static hal_status_t bond_store(void *context,
                               const hal_gamepad_bond_blob_t *blob) {
  (void)context;
  (void)blob;
  return HAL_OK;
}

static hal_status_t bond_erase(void *context) {
  (void)context;
  return HAL_OK;
}

hal_gamepad_bond_provider_t
jh_gamepad_bond_kv_provider(jh_gamepad_bond_kv_context_t *context,
                            uint16_t key) {
  assert(context != NULL);
  assert(key == DOOM_GAMEPAD_BOND_KV_KEY);
  hal_gamepad_bond_provider_t provider = {0};
  provider.context = context;
  provider.load = bond_load;
  provider.store = bond_store;
  provider.erase = bond_erase;
  return provider;
}

hal_status_t hal_eeprom_init(hal_eeprom_type_t type, uint16_t size,
                             uint8_t i2c_addr) {
  ++s_eeprom_init_calls;
  assert(type == HAL_EEPROM_FLASH);
  assert(size == 0u);
  assert(i2c_addr == 0u);
#ifdef DOOM_TEST_BOND_STORAGE_FAIL
  return HAL_EIO;
#else
  return HAL_OK;
#endif
}

hal_status_t hal_eeprom_size_ex(uint16_t *out_size) {
  assert(out_size != NULL);
  *out_size = HAL_RP_FLASH_EEPROM_SIZE;
  return HAL_OK;
}

hal_status_t hal_kv_init_ex(uint16_t base_addr, uint16_t size_bytes) {
  ++s_kv_init_calls;
  assert(base_addr == 0u);
  assert(size_bytes == HAL_RP_FLASH_EEPROM_SIZE);
  return HAL_OK;
}

hal_status_t hal_gamepad_poll(hal_gamepad_t gamepad) {
  assert(gamepad != NULL);
  return HAL_OK;
}

hal_status_t hal_gamepad_get_info(hal_gamepad_t gamepad,
                                  hal_gamepad_info_t *out_info) {
  assert(gamepad != NULL);
  *out_info = s_info;
  return HAL_OK;
}

hal_status_t hal_gamepad_snapshot_next(hal_gamepad_t gamepad,
                                       hal_gamepad_snapshot_t *out_snapshot) {
  assert(gamepad != NULL);
  if (s_info.state != HAL_GAMEPAD_STATE_CONNECTED) {
    return HAL_EUNINIT;
  }
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

hal_status_t hal_gamepad_pairing_open(hal_gamepad_t gamepad) {
  assert(gamepad != NULL);
  ++s_pairing_open_calls;
  return HAL_OK;
}

hal_status_t hal_gamepad_pairing_authorize(hal_gamepad_t gamepad) {
  assert(gamepad != NULL);
  ++s_authorize_calls;
  return HAL_OK;
}

hal_status_t hal_gamepad_reconnect(hal_gamepad_t gamepad) {
  assert(gamepad != NULL);
  ++s_reconnect_calls;
  return HAL_OK;
}

hal_status_t hal_gamepad_forget(hal_gamepad_t gamepad) {
  assert(gamepad != NULL);
  ++s_forget_calls;
#ifdef DOOM_TEST_FORGET_FAIL
  return HAL_EIO;
#elif defined(DOOM_TEST_FORGET_BUSY_ONCE)
  return s_forget_calls == 1u ? HAL_EBUSY : HAL_OK;
#else
  return HAL_OK;
#endif
}

void hal_deb(const char *format, ...) { (void)format; }

void hal_derr(const char *format, ...) { (void)format; }

void hal_derr_limited(const char *source, const char *format, ...) {
  (void)source;
  (void)format;
}

uint32_t hal_millis(void) { return s_now_ms; }

bool I_PicoSoundSuspendForFlash(void) {
  ++s_audio_suspend_calls;
  return true;
}

void I_PicoSoundResumeAfterFlash(bool suspended) {
  assert(suspended);
  ++s_audio_resume_calls;
}

int main(void) {
  DoomGamepadInput_Init();
  DoomGamepadInput_Init();
  assert(s_open_calls == 1u);
  assert(s_eeprom_init_calls == 1u);
#ifdef DOOM_TEST_BOND_STORAGE_FAIL
  assert(s_kv_init_calls == 0u);
  assert(!s_open_received_provider);
#else
  assert(s_kv_init_calls == 1u);
  assert(s_open_received_provider);
  hal_gamepad_bond_blob_t blob = {0};
  assert(s_open_provider.store(s_open_provider.context, &blob) == HAL_OK);
  assert(s_open_provider.erase(s_open_provider.context) == HAL_OK);
  assert(s_audio_suspend_calls == 2u);
  assert(s_audio_resume_calls == 2u);
#endif

#ifdef DOOM_TEST_START_KNOWN
  s_info.state = HAL_GAMEPAD_STATE_READY;
  s_info.known_device = true;
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_pairing_open_calls == 0u);
  assert(s_reconnect_calls == 0u);
  s_now_ms += 9999u;
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 0u);
  ++s_now_ms;
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 1u);
  return 0;
#endif

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

  hal_gamepad_snapshot_t active =
      snapshot(1u,
               ZERO2_BUTTON_A | ZERO2_BUTTON_B | ZERO2_BUTTON_X |
                   ZERO2_BUTTON_Y | ZERO2_BUTTON_L | ZERO2_BUTTON_R |
                   ZERO2_BUTTON_START | ZERO2_BUTTON_SELECT,
               HAL_GAMEPAD_DPAD_UP | HAL_GAMEPAD_DPAD_LEFT);
  queue_result(HAL_OK, active);
  assert(service_connected() ==
         (DOOM_INPUT_ACTION_UP | DOOM_INPUT_ACTION_LEFT |
          DOOM_INPUT_ACTION_FIRE | DOOM_INPUT_ACTION_USE |
          DOOM_INPUT_ACTION_MENU | DOOM_INPUT_ACTION_ACCEPT |
          DOOM_INPUT_ACTION_BACK | DOOM_INPUT_ACTION_STRAFE |
          DOOM_INPUT_ACTION_STRAFE_LEFT | DOOM_INPUT_ACTION_STRAFE_RIGHT));

  hal_gamepad_snapshot_t axes = snapshot(1u, 0u, 0u);
  axes.axes_present = (1u << HAL_GAMEPAD_AXIS_X) | (1u << HAL_GAMEPAD_AXIS_Y);
  axes.axes[HAL_GAMEPAD_AXIS_X] = 20000;
  axes.axes[HAL_GAMEPAD_AXIS_Y] = 20000;
  queue_result(HAL_OK, axes);
  assert(service_connected() ==
         (DOOM_INPUT_ACTION_RIGHT | DOOM_INPUT_ACTION_DOWN));

  s_info.state = HAL_GAMEPAD_STATE_READY;
  s_info.pairing_window_open = false;
  s_info.pairing_pending = false;
  s_info.known_device = true;
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 0u);
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 0u);
  s_now_ms += 9999u;
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 0u);
  ++s_now_ms;
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 1u);
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 1u);
  s_now_ms += 4999u;
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 1u);
  ++s_now_ms;
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 2u);

  s_info.state = HAL_GAMEPAD_STATE_CONNECTING;
  s_info.pairing_pending = true;
  assert(DoomGamepadInput_Service() == 0u);
#if BT_AUTOMATIC_PAIRING
  assert(s_authorize_calls == 2u);
#else
  assert(s_authorize_calls == 1u);
#endif
  s_info.pairing_pending = false;

  queue_result(HAL_OK, snapshot(2u, ZERO2_BUTTON_A, 0u));
  assert(service_connected() == 0u);
  queue_result(HAL_OK, snapshot(2u, 0u, 0u));
  assert(service_connected() == 0u);
  queue_result(HAL_OK, snapshot(2u, ZERO2_BUTTON_A, 0u));
  assert(service_connected() == DOOM_INPUT_ACTION_FIRE);

  queue_result(HAL_EOVERFLOW, (hal_gamepad_snapshot_t){0});
  queue_result(HAL_OK, snapshot(2u, ZERO2_BUTTON_A, 0u));
  assert(service_connected() == 0u);
  queue_result(HAL_OK, snapshot(2u, 0u, 0u));
  assert(service_connected() == 0u);
  queue_result(HAL_OK, snapshot(2u, ZERO2_BUTTON_A, 0u));
  assert(service_connected() == DOOM_INPUT_ACTION_FIRE);

  DoomGamepadInput_RequestForget();
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_forget_calls == 1u);
  assert(DoomGamepadInput_Service() == 0u);
#ifdef DOOM_TEST_FORGET_BUSY_ONCE
  assert(s_forget_calls == 2u);
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_forget_calls == 2u);
#else
  assert(s_forget_calls == 1u);
#endif

#ifdef DOOM_TEST_FORGET_FAIL
  s_info.state = HAL_GAMEPAD_STATE_READY;
  s_info.known_device = true;
  assert(DoomGamepadInput_Service() == 0u);
  assert(s_reconnect_calls == 2u);

  DoomGamepadInput_RequestPairing();
  assert(DoomGamepadInput_Service() == 0u);
#if BT_AUTOMATIC_PAIRING
  assert(s_pairing_open_calls == 3u);
#else
  assert(s_pairing_open_calls == 2u);
#endif
#endif

  return 0;
}
