#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/uikit.h.
#include "tfn/uikit.h"

#include <stdbool.h>
#include <stdint.h>
#include "kfont.h"

// true when (x, y) is in the box rx, ry, rw x rh, or less than m px out of it (a margin for a thumb)
static inline bool ui_hit(int x, int y, int rx, int ry, int rw, int rh, int m)
{
    return x >= rx - m && x < rx + rw + m && y >= ry - m && y < ry + rh + m;
}

// one pixel at alpha a (0..255)
void ui_px(int x, int y, uint16_t c, int a);
// the same at alpha a (0..255): a soft shape over the frame (the grip of the settings panel)
void ui_rrect_a(int x, int y, int w, int h, int r, uint16_t c, int a);
// ui_rrect_a with no middle: the box inset by t px on each side is not drawn (an opaque thing covers it: a glow or a
// ring round a tile costs only its band)
void ui_rrect_band(int x, int y, int w, int h, int r, uint16_t c, int a, int t);

void ui_text(const kfont_t *f, int x, int base, const char *utf8, uint16_t col);
const kfont_t *ui_text_fit(const kfont_t *const *fonts, int n, const char *utf8, int w);
// THE DOT TEXT IN A REAL FACE: the same arguments as gfx_text, gfx_text_width, gfx_text_center and
// gfx_text_center_in (y is the top of the dot line of scale sc), drawn in the Rubik face of about the same size
// (1 = KF_LABEL, 2 = KF_TEXT, 3 = KF_BIG, 4 and more = KF_TITLE). A system page that drew the dot font maps the
// gfx_text names to these. They write g_fb directly: they obey only the screen edges, not the gfx clip box.
void ui_dot_text(int x, int y, const char *s, int sc, uint16_t c);
int ui_dot_text_w(const char *s, int sc);
void ui_dot_text_c(int y, const char *s, int sc, uint16_t c);
void ui_dot_text_in(int x, int w, int y, const char *s, int sc, uint16_t c);
// a friendly colour for a name (the same name: the same colour), for an avatar
uint16_t ui_name_colour(const char *utf8);
// the first letter of a name (UTF-8) as a short string (for an avatar)
void ui_initial(const char *utf8, char out[4]);
