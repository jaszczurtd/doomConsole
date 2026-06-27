#ifndef DOOM_BOOT_LOG_H
#define DOOM_BOOT_LOG_H

#include <stdbool.h>
#include <stdint.h>

#if defined(__GNUC__)
#define DOOM_BOOT_PRINTF_ATTR(fmt_index, first_arg)                            \
    __attribute__((format(printf, fmt_index, first_arg)))
#define DOOM_BOOT_NORETURN __attribute__((noreturn))
#else
#define DOOM_BOOT_PRINTF_ATTR(fmt_index, first_arg)
#define DOOM_BOOT_NORETURN
#endif

#ifdef __cplusplus
extern "C" {
#endif

void DoomBootLog_Init(void);
void DoomBootLog_Flush(void);
bool DoomBootLog_SerialConnected(void);
void DoomBootLog_WaitForSerial(uint32_t timeout_ms);
void DoomBootLog_WriteRaw(const char *data, unsigned int length);
void DoomBootLog_Printf(const char *format, ...)
    DOOM_BOOT_PRINTF_ATTR(1, 2);
void DoomFatalError(const char *format, ...)
    DOOM_BOOT_NORETURN DOOM_BOOT_PRINTF_ATTR(1, 2);

#ifdef __cplusplus
}
#endif

#endif
