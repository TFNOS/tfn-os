// The unit test of tfn_utf8_decode() (main/tfn_utf8.h) against the two decoders that it replaced (R4d): the text of
// sysui.c (more lines) and of uikit.c (one line), on edge cases and on random bytes.
// Build and run: python tests/unit/run.py
#include <stdio.h>
#include <string.h>
#include "tfn_utf8.h"

static int old_sysui(const char *s, uint16_t *cp, int max)
{
    int n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p && n < max) {
        uint32_t c = *p++;
        if (c >= 0xC0 && c < 0xE0 && (*p & 0xC0) == 0x80) {
            c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        } else if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
            c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if (c >= 0x80) {
            c = '?';
        }
        if (c == '\r') {
            continue;
        }
        cp[n++] = (uint16_t)(c == '\t' ? ' ' : c);
    }
    return n;
}

static int old_uikit(const char *s, uint16_t *cp, int max)
{
    int n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p && n < max) {
        uint32_t c = *p++;
        if (c >= 0xC0 && c < 0xE0 && (*p & 0xC0) == 0x80) {
            c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        } else if (c >= 0xE0 && c < 0xF0 && (p[0] & 0xC0) == 0x80 && (p[1] & 0xC0) == 0x80) {
            c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F);
            p += 2;
        } else if (c >= 0x80) {
            c = '?';
        }
        if (c == '\r' || c == '\n') {
            c = ' ';
        }
        cp[n++] = (uint16_t)(c == '\t' ? ' ' : c);
    }
    return n;
}

static int s_fails;

static void check(const char *s)
{
    // (a small max too: the limit of the output must act the same)
    static const int maxes[] = {0, 1, 3, 64};
    for (int m = 0; m < 4; m++) {
        uint16_t a[64], b[64];
        int na = old_sysui(s, a, maxes[m]);
        int nb = tfn_utf8_decode(s, b, maxes[m], false);
        if (na != nb || memcmp(a, b, sizeof a[0] * (size_t)na)) {
            s_fails++;
        }
        na = old_uikit(s, a, maxes[m]);
        nb = tfn_utf8_decode(s, b, maxes[m], true);
        if (na != nb || memcmp(a, b, sizeof a[0] * (size_t)na)) {
            s_fails++;
        }
    }
}

int main(void)
{
    static const char *fixed[] = {
            "",     "abc",      "a\r\nb\tc", "\xd7\x90\xd7\x91", "\xe2\x82\xac", "\xf0\x9f\x98\x80",
            "\xc3", "\xe2\x82", "\x80\xbf",  "\xc0\x80",         "\xff\xfe"};
    for (size_t k = 0; k < sizeof fixed / sizeof fixed[0]; k++) {
        check(fixed[k]);
    }
    unsigned seed = 7;
    char t[80];
    for (int round = 0; round < 200000; round++) {
        int n = (int)(seed % 70);
        for (int k = 0; k < n; k++) {
            seed = seed * 1103515245u + 12345u;
            unsigned r = (seed >> 16) & 0xff;
            // (more bytes of the UTF-8 forms and of the line ends than of plain text)
            t[k] = (char)(r < 40    ? 0x80 + (r & 0x3f)
                          : r < 60  ? 0xc0 + (r & 0x1f)
                          : r < 80  ? 0xe0 + (r & 0x0f)
                          : r < 90  ? '\r'
                          : r < 100 ? '\n'
                          : r < 105 ? '\t'
                                    : r | 1);
        }
        t[n] = 0;
        check(t);
    }
    printf("UTF8 TESTS %s\n", s_fails ? "FAILED" : "OK");
    return s_fails ? 1 : 0;
}
