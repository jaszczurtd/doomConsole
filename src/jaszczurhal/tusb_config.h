#pragma once

/*
 * The active JaszczurHAL port uses GPIO buttons, not the legacy TinyUSB host
 * keyboard backend. Keep TinyUSB on the HAL-owned device configuration so the
 * firmware and JaszczurHAL compile with one consistent USB mode.
 */
#include <hal/impl/rp2040/drivers/usb/tusb_config.h>
