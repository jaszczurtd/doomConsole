#include "doom/doom_pre_wipe.h"

typedef enum {
    DOOM_PRE_WIPE_IDLE = 0,
    DOOM_PRE_WIPE_FRAME_NEEDED,
    DOOM_PRE_WIPE_FRAME_RENDERED,
} doom_pre_wipe_state_t;

static volatile doom_pre_wipe_state_t s_state;

bool DoomPreWipe_ShouldDeferAction(void)
{
    if (s_state == DOOM_PRE_WIPE_FRAME_RENDERED) {
        s_state = DOOM_PRE_WIPE_IDLE;
        return false;
    }

    s_state = DOOM_PRE_WIPE_FRAME_NEEDED;
    return true;
}

bool DoomPreWipe_IsPending(void)
{
    return s_state != DOOM_PRE_WIPE_IDLE;
}

void DoomPreWipe_MarkFrameRendered(void)
{
    if (s_state == DOOM_PRE_WIPE_FRAME_NEEDED) {
        s_state = DOOM_PRE_WIPE_FRAME_RENDERED;
    }
}
