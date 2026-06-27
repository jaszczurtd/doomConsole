#ifndef DOOM_STORAGE_HAL_H
#define DOOM_STORAGE_HAL_H

#include <stdint.h>

#ifndef DOOM_FLASH_XIP_BASE
#define DOOM_FLASH_XIP_BASE 0x10000000u
#endif

#ifndef DOOM_FLASH_SIZE_BYTES
#if defined(PICO_FLASH_SIZE_BYTES)
#define DOOM_FLASH_SIZE_BYTES PICO_FLASH_SIZE_BYTES
#else
#define DOOM_FLASH_SIZE_BYTES 0x400000u
#endif
#endif

#ifndef DOOM_WHD_FLASH_ADDR
#if defined(TINY_WAD_ADDR)
#define DOOM_WHD_FLASH_ADDR TINY_WAD_ADDR
#else
#define DOOM_WHD_FLASH_ADDR 0x10200000u
#endif
#endif

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
