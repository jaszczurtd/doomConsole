#include <assert.h>

#include "doom/doom_menu_action.h"

int main(void)
{
    enum {
        FORWARD_KEY = 13,
        BACK_KEY = 8,
        CONFIRM_KEY = 'y',
        ABORT_KEY = 'n',
    };

    assert(DoomMenu_TranslatePromptKey(FORWARD_KEY, FORWARD_KEY, BACK_KEY,
               CONFIRM_KEY, ABORT_KEY)
        == CONFIRM_KEY);
    assert(DoomMenu_TranslatePromptKey(BACK_KEY, FORWARD_KEY, BACK_KEY,
               CONFIRM_KEY, ABORT_KEY)
        == ABORT_KEY);
    assert(DoomMenu_TranslatePromptKey(' ', FORWARD_KEY, BACK_KEY,
               CONFIRM_KEY, ABORT_KEY)
        == ' ');

    return 0;
}
