#pragma once

// Big smooth letters and digits drawn with thick strokes (strokefont.c): the note names of TUNER and
// the tempo of METRO. Any size, no font data in the flash.

#include <stdint.h>

// A line of round dots (radius r) from (x0, y0) to (x1, y1)
void stroke_line(float x0, float y0, float x1, float y1, float r, uint16_t c);
// One character in a box of h px height (the width is about 0.6 h): 'A'..'G', '#', '0'..'9'. Others: nothing.
void stroke_char(char ch, float x, float y, float h, float r, uint16_t c);
// A string of those characters; the advance is 0.78 h. Returns the width.
float stroke_text(const char *s, float x, float y, float h, float r, uint16_t c);
float stroke_width(const char *s, float h);
