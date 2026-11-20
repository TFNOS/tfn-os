#pragma once

// THE TEXT OF THE SYSTEM PAGES (SETUP, its pages and the system keyboard), on all boards: the 5 x 7 dot font of
// gfx_text is coarse and hard to read. After this header, each gfx_text* call of the file draws an anti-aliased face
// of about the same size (uikit.h, ui_dot_text: scale 1 = KF_LABEL, 2 = KF_TEXT, 3 = KF_BIG, 4 and more = KF_TITLE).
// y stays the top of the dot letters (7 x scale rows); the letters of the face sit in the middle of that band. Only
// the firmware pages include this file.
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "gfx.h"
#include "uikit.h"

#define gfx_text           ui_dot_text
#define gfx_text_width     ui_dot_text_w
#define gfx_text_center    ui_dot_text_c
#define gfx_text_center_in ui_dot_text_in

// A text of scale sc in lines of at most w px, from (x, y): lh px from a line to the next; a line breaks at a space
// (a word that is too long for a line stays whole). centre: each line in the middle of x .. x + w. The result: the
// count of lines.
static inline int set_wrap(int x, int y, int w, int lh, const char *s, int sc, uint16_t c, bool centre)
{
    char a[128];
    int n = 0;
    while (*s) {
        while (*s == ' ') {
            s++;
        }
        if (!*s) {
            break;
        }
        int len = (int)strlen(s), cut = len;
        if (len >= (int)sizeof(a) || ui_dot_text_w(s, sc) > w) {
            cut = -1;
            for (int i = 1; i < len && i < (int)sizeof(a) - 1; i++) {
                if (s[i] != ' ') {
                    continue;
                }
                memcpy(a, s, (size_t)i);
                a[i] = 0;
                if (cut > 0 && ui_dot_text_w(a, sc) > w) {
                    break;
                }
                cut = i;
            }
            if (cut < 0) {
                cut = len < (int)sizeof(a) - 1 ? len : (int)sizeof(a) - 1;
            }
        }
        memcpy(a, s, (size_t)cut);
        a[cut] = 0;
        if (centre) {
            ui_dot_text_in(x, w, y + n * lh, a, sc, c);
        } else {
            ui_dot_text(x, y + n * lh, a, sc, c);
        }
        n++;
        s += cut;
    }
    return n;
}

// The name of a row in at most w px, from x, y (the top of a line of scale 2): scale 2 when it fits, else scale 1 in
// one line, or in two lines (12 px apart, in the band of the scale 2 line).
static inline void set_name_fit(int x, int y, int w, const char *s, uint16_t c)
{
    if (ui_dot_text_w(s, 2) <= w) {
        ui_dot_text(x, y, s, 2, c);
    } else if (ui_dot_text_w(s, 1) <= w) {
        ui_dot_text(x, y + 3, s, 1, c);
    } else {
        set_wrap(x, y - 3, w, 12, s, 1, c, false);
    }
}
