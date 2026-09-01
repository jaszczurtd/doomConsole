#include <assert.h>

#include "doom/doom_pre_wipe.h"

int main(void)
{
    DoomPreWipe_MarkFrameRendered();
    assert(!DoomPreWipe_IsPending());

    assert(DoomPreWipe_ShouldDeferAction());
    assert(DoomPreWipe_IsPending());
    assert(DoomPreWipe_ShouldDeferAction());

    DoomPreWipe_MarkFrameRendered();
    assert(DoomPreWipe_IsPending());
    assert(!DoomPreWipe_ShouldDeferAction());
    assert(!DoomPreWipe_IsPending());

    DoomPreWipe_MarkFrameRendered();
    assert(!DoomPreWipe_IsPending());

    assert(DoomPreWipe_ShouldDeferAction());
    DoomPreWipe_MarkFrameRendered();
    DoomPreWipe_MarkFrameRendered();
    assert(!DoomPreWipe_ShouldDeferAction());

    return 0;
}
