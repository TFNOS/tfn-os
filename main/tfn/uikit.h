#pragma once

// A small kit for app screens (the system layer, WALKIE, CHAT): boxes with soft round corners,
// soft circles and rings, colour mixes, and text in the quest fonts (UTF-8: English and Hebrew, a Hebrew line from
// the right). It writes g_fb directly: it obeys the screen edges, NOT the gfx clip box.
#include <stdbool.h>
#include <stdint.h>
#include "kfont.h"

// a mix of two gfx colours (rgb()): t = 0 gives a, t = 255 gives b
uint16_t ui_mix(uint16_t a, uint16_t b, int t);
// a filled box with round corners of radius r (soft edges)
void ui_rrect(int x, int y, int w, int h, int r, uint16_t c);
// a box with a border of t px: the border colour, then the fill inside it
void ui_rbox(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border, int t);
// a filled circle (soft edge) at alpha a
void ui_circle(int cx, int cy, int r, uint16_t c, int a);
// a ring of radius r and width t (soft edges) at alpha a
void ui_ring(int cx, int cy, int r, int t, uint16_t c, int a);

// Text: one line, x = the centre (ui_text_c) or the left end (ui_text); base = the baseline. A line with Hebrew is laid
// out from the right. ui_text_w: the width in px. ui_text_fit: the largest of the fonts (big to small) whose width of
// the text is w or less (the last one if none).
void ui_text_c(const kfont_t *f, int cx, int base, const char *utf8, uint16_t col);
int ui_text_w(const kfont_t *f, const char *utf8);
