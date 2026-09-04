#ifndef DOOM_STORAGE_HAL_H
#define DOOM_STORAGE_HAL_H

#include <stdint.h>

#include "doom_main_config.h"

#define DOOM_STORAGE_WHD_BASE                                                  \
    ((const uint8_t *)(uintptr_t)(DOOM_WHD_FLASH_ADDR))
#if HAL_RP_FLASH_EEPROM_SIZE > DOOM_FLASH_SIZE_BYTES
#error "Persistent storage reservation exceeds physical flash."
#endif
#define DOOM_STORAGE_FLASH_END                                                 \
    ((const uint8_t *)(uintptr_t)(DOOM_FLASH_XIP_BASE + DOOM_FLASH_SIZE_BYTES  \
        - HAL_RP_FLASH_EEPROM_SIZE))

#ifdef __cplusplus
extern "C" {
#endif

const uint8_t *DoomStorage_WHDBase(void);
void DoomStorage_SetWHDBase(const uint8_t *base);
const uint8_t *DoomStorage_FlashEnd(void);

#ifdef __cplusplus
}
#endif

#endif
