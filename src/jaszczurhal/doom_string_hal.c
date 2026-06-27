/*
 * Small C library compatibility shims for the JaszczurHAL Doom port.
 */

#include <stddef.h>
#include <string.h>

static char doom_ascii_tolower(char c)
{
    if (c >= 'A' && c <= 'Z') {
        c = (char)(c + ('a' - 'A'));
    }
    return c;
}

int strnicmp(const char *a, const char *b, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        const char ca = doom_ascii_tolower(a[i]);
        const char cb = doom_ascii_tolower(b[i]);
        const int diff = (int)(unsigned char)ca - (int)(unsigned char)cb;

        if (diff != 0 || ca == '\0') {
            return diff;
        }
    }

    return 0;
}

int stricmp(const char *a, const char *b)
{
    const size_t len_a = strlen(a);
    const size_t len_b = strlen(b);
    const size_t len = len_a > len_b ? len_a : len_b;

    return strnicmp(a, b, len + 1u);
}
