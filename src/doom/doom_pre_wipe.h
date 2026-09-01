#pragma once

#include <stdbool.h>

bool DoomPreWipe_ShouldDeferAction(void);
bool DoomPreWipe_IsPending(void);
void DoomPreWipe_MarkFrameRendered(void);
