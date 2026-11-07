#pragma once

#include <stdint.h>

typedef struct {
    uint16_t cp;
    uint8_t adv4;
    int8_t ox, oy;
    uint8_t w, h;
    uint32_t off;
} mglyph_t;
typedef struct {
    uint8_t asc, line;
    uint16_t n;
    const mglyph_t *g;
    const uint8_t *a4;
} mfont_t;

// asc: the height of the capitals over the baseline (about); line: the line height
extern const mfont_t MF_N34, MF_N28, MF_N24, MF_N20, MF_N16, MF_SMALL, MF_LABEL, MF_TINY;

// The simple drawer for the games. col is a gfx colour (rgb()), alpha 0..255. x is the left end, base the
// baseline. It writes g_fb directly: it obeys the screen edges, NOT the gfx clip box.
const mglyph_t *mf_glyph(const mfont_t *f, int cp);
int mf_width(const mfont_t *f, const char *s, int spacing);
void mf_text(const mfont_t *f, int x, int base, const char *s, uint16_t col, int alpha, int spacing);
void mf_text_c(const mfont_t *f, int cx, int base, const char *s, uint16_t col, int alpha, int spacing);
// The look of a game word: a shadow (+sh, +sh; 0 = none), a 1 px outline (4 offsets), then the colour.
void mf_text_o(const mfont_t *f, int x, int base, const char *s, uint16_t col, uint16_t outline, int sh, int spacing);
void mf_text_oc(const mfont_t *f, int cx, int base, const char *s, uint16_t col, uint16_t outline, int sh, int spacing);
