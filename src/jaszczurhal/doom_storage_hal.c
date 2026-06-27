/*
 * JaszczurHAL storage/layout adapter for the Doom port.
 *
 * WHD/WHDX data is currently treated as a raw XIP-readable flash payload
 * programmed separately from the firmware. Raw flash writes for save slots are
 * intentionally unavailable until JaszczurHAL exposes a public raw flash API.
 */

#include <stddef.h>
#include <stdint.h>

#include "doomtype.h"
#include "doom/p_saveg.h"
#include "jaszczurhal/doom_storage_hal.h"

static const uint8_t *s_whd_base = DOOM_STORAGE_WHD_BASE;

const uint8_t *DoomStorage_WHDBase(void)
{
    return s_whd_base;
}

void DoomStorage_SetWHDBase(const uint8_t *base)
{
    s_whd_base = base != NULL ? base : DOOM_STORAGE_WHD_BASE;
}

const uint8_t *DoomStorage_FlashEnd(void)
{
    return DOOM_STORAGE_FLASH_END;
}

#if PICO_ON_DEVICE
const uint8_t *get_end_of_flash(void)
{
    return DoomStorage_FlashEnd();
}

void P_SaveGameGetExistingFlashSlotAddresses(flash_slot_info_t *slots, int count)
{
    if (slots == NULL) {
        return;
    }

    for (int i = 0; i < count; ++i) {
        slots[i].data = NULL;
        slots[i].size = 0;
    }
}

boolean P_SaveGameWriteFlashSlot(int slot, const uint8_t *buffer,
                                 unsigned int size, uint8_t *buffer4k)
{
    (void)slot;
    (void)buffer;
    (void)size;
    (void)buffer4k;
    return false;
}
#endif
