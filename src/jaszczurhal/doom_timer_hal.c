/*
 * JaszczurHAL timer backend for Doom.
 */

#include <stdint.h>

#include <hal/system/hal_system.h>

#include "i_timer.h"

int I_GetTime(void)
{
    const uint32_t ms = hal_millis();
    return (int)(((uint64_t)ms * TICRATE) / 1000u);
}

int I_GetTimeMS(void)
{
    return (int)hal_millis();
}

void I_Sleep(int ms)
{
    if (ms <= 0) {
        return;
    }

    hal_delay_ms((uint32_t)ms);
}

void I_WaitVBL(int count)
{
    if (count <= 0) {
        return;
    }

    I_Sleep((count * 1000) / 70);
}

void I_InitTimer(void)
{
}
