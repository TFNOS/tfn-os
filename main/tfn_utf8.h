// The UTF-8 decode of the text of the system screens (sysui.c, uikit.c). Static inline: no ABI name.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// Decodes s into at most max code points in cp; returns their number. It reads the 1, 2 and 3 byte forms (the BMP);
// a byte of a form that it does not read becomes '?'. A tab becomes a space. one_line: '\r' and '\n' become spaces;
// else '\r' goes away and '\n' stays (a text of more lines).
static inline int tfn_utf8_decode(const char *s, uint16_t *cp, int max, bool one_line)
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
        if (one_line && (c == '\r' || c == '\n')) {
            c = ' ';
        } else if (c == '\r') {
            continue;
        }
        cp[n++] = (uint16_t)(c == '\t' ? ' ' : c);
    }
    return n;
}
