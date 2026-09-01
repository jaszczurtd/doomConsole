#pragma once

static inline int DoomMenu_TranslatePromptKey(int key, int forward_key,
                                               int back_key, int confirm_key,
                                               int abort_key)
{
    if (key == forward_key) {
        return confirm_key;
    }
    if (key == back_key) {
        return abort_key;
    }
    return key;
}
