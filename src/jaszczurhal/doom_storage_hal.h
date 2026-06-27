#ifndef DOOM_STORAGE_HAL_H
#define DOOM_STORAGE_HAL_H

#include <stdint.h>

#include "doom_main_config.h"

#define DOOM_STORAGE_WHD_BASE                                                  \
    ((const uint8_t *)(uintptr_t)(DOOM_WHD_FLASH_ADDR))
#define DOOM_STORAGE_FLASH_END                                                 \
    ((const uint8_t *)(uintptr_t)(DOOM_FLASH_XIP_BASE + DOOM_FLASH_SIZE_BYTES))

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
