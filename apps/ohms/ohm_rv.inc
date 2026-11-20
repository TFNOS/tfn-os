// THE ROUND VIEW (the round panels: app_t.round = APP_ROUND_HI; the AMOLED-1.75C 466 x 466, the Watcher 412 x 412).
// The app draws its 320 x 240 layout as before. These functions put the frame in a logical square of RVW_S x RVW_S
// (the frame from the row RVW_OY, in the middle) and the square on the panel at k = gfx_w / RVW_S (sharp: no scale of
// pixels). Each row of the frame follows the circle: the rows near the top and the bottom are narrower (x is
// pressed to the chord of the row, at most 1:1). A rectangle becomes a shape with round sides; a text stays in the
// middle of its box at its row (a word too wide for the box gets closer letters). rvw_pt() changes a touch point
// back to the frame. After this file the gfx names of the app go to these functions (macros). (A copy of
// apps/studio/st_rv.inc, with the ellipse, the darker box, the spike and the panel text of OHMS.)
#if BOARD_ROUND
#define RVW_S   340
#define RVW_OX  10  // the frame in the square: x 10 .. 330, y RVW_OY .. RVW_OY + 240
#define RVW_OY  50
#define RVW_MAX 480              // the rows of the panel (the table)
static float rvw_sx_t[RVW_MAX];  // the press of x at each row of the panel (1 = none)
static int rvw_d;                // the size of the table (0 = none yet)

static float rvw_k(void)
{
    return (float)gfx_w / RVW_S;
}
// the press of x at the frame row y (float)
static float rvw_sx_f(float yf)
{
    float r = RVW_S * 0.5f, dy = yf + RVW_OY - r, h2 = r * r - dy * dy;
    float h = h2 > 0 ? sqrtf(h2) - 8 : 0;  // (8: a margin to the edge of the glass)
    float s = h / 160;
    return s > 1 ? 1 : s < 0.2f ? 0.2f : s;
}
static float rvw_sx(int py)
{
    if (rvw_d != gfx_h) {
        for (int i = 0; i < RVW_MAX; i++) {
            rvw_sx_t[i] = rvw_sx_f(((float)i + 0.5f) / rvw_k() - RVW_OY);
        }
        rvw_d = gfx_h;
    }
    return rvw_sx_t[py < 0 ? 0 : py >= RVW_MAX ? RVW_MAX - 1 : py];
}
static int rvw_y(int v)
{
    return (int)floorf((float)(v + RVW_OY) * rvw_k());
}
// the panel x of the frame x at the panel row py
static int rvw_xp(float x, int py)
{
    return (int)floorf((RVW_S * 0.5f + (x - 160) * rvw_sx(py)) * rvw_k());
}
static int rvw_ps(int scale)
{
    return scale <= 1 ? 2 : (int)((float)scale * rvw_k() + 0.5f);
}
// a touch point of the panel to a point of the frame
static void rvw_pt(int *x, int *y)
{
    float k = rvw_k(), s = rvw_sx(*y);
    *x = (int)(160 + ((float)*x / k - RVW_S * 0.5f) / s);
    *y = (int)((float)*y / k) - RVW_OY;
}
static void rvw_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    int y0 = rvw_y(y), y1 = rvw_y(y + h);
    if (y1 <= y0) {
        y1 = y0 + 1;
    }
    for (int py = y0; py < y1; py++) {
        int a = rvw_xp((float)x, py), b = rvw_xp((float)(x + w), py);
        gfx_fill_rect(a, py, b > a ? b - a : 1, 1, c);
    }
}
static void rvw_frame_rect(int x, int y, int w, int h, int t, uint16_t c)
{
    rvw_fill_rect(x, y, w, t, c);
    rvw_fill_rect(x, y + h - t, w, t, c);
    rvw_fill_rect(x, y + t, t, h - 2 * t, c);
    rvw_fill_rect(x + w - t, y + t, t, h - 2 * t, c);
}
static void rvw_fill_circle(int cx, int cy, int r, uint16_t c)
{
    int py = rvw_y(cy);
    gfx_fill_circle(rvw_xp((float)cx + 0.5f, py), py, (int)((float)r * rvw_k() + 0.5f), c);
}
static int rvw_text_width(const char *s, int scale)
{
    return (int)ceilf((float)ohm_aa_w(s, rvw_ps(scale)) / rvw_k());  // (the AA faces of app_ohm.c)
}
// the panel row of the top of a text of the scale at the frame row y (its middle row stays)
static int rvw_ty(int y, int scale)
{
    return (int)(((float)(y + RVW_OY) + 3.5f * (float)scale) * rvw_k()) - 7 * rvw_ps(scale) / 2;
}
// a text in the panel box x0 .. x1 at the panel row py: in the middle; a smaller face when it is too wide (its middle row
// stays)
static void rvw_text_box(int x0, int x1, int py, const char *s, int ps, uint16_t c)
{
    int k = ps, bw = x1 - x0;
    while (k > 1 && ohm_aa_w(s, k) > bw - 2) {
        k--;
    }
    ohm_aa_text(x0 + (bw - ohm_aa_w(s, k)) / 2, py + 7 * (ps - k) / 2, s, k, c);
}
static void rvw_text(int x, int y, const char *s, int scale, uint16_t c)
{
    int ps = rvw_ps(scale), py = rvw_ty(y, scale), mid = py + 7 * ps / 2;
    ohm_aa_text(rvw_xp((float)x, mid), py, s, ps, c);
}
static void rvw_text_center_in(int x, int w, int y, const char *s, int scale, uint16_t c)
{
    int ps = rvw_ps(scale), py = rvw_ty(y, scale), mid = py + 7 * ps / 2;
    rvw_text_box(rvw_xp((float)x, mid), rvw_xp((float)(x + w), mid), py, s, ps, c);
}
static void rvw_text_center(int y, const char *s, int scale, uint16_t c)
{
    rvw_text_center_in(0, 320, y, s, scale, c);
}
static void rvw_clip_bottom(int y)
{
    gfx_clip_bottom(rvw_y(y));
}
static void rvw_fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c)
{
    int py = rvw_y(cy);
    float k = rvw_k();
    gfx_fill_ellipse(rvw_xp((float)cx + 0.5f, py), py, (int)((float)rx * rvw_sx(py) * k + 0.5f),
                     (int)((float)ry * k + 0.5f), c);
}
static void rvw_darken_rect(int x, int y, int w, int h, int shift)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    for (int py = rvw_y(y); py < rvw_y(y + h); py++) {
        int a = rvw_xp((float)x, py), b = rvw_xp((float)(x + w), py);
        gfx_darken_rect(a, py, b - a, 1, shift);
    }
}
static void rvw_spike(int x, int y, int w, int h, uint16_t c)
{
    int py = rvw_y(y);
    float k = rvw_k();
    gfx_spike(rvw_xp((float)x, py), py, (int)((float)w * k + 0.5f), (int)((float)h * k + 0.5f), c);
}
#define gfx_fill_rect      rvw_fill_rect
#define gfx_frame_rect     rvw_frame_rect
#define gfx_fill_circle    rvw_fill_circle
#define gfx_text_width     rvw_text_width
#define gfx_text           rvw_text
#define gfx_text_center_in rvw_text_center_in
#define gfx_text_center    rvw_text_center
#define gfx_clip_bottom    rvw_clip_bottom
#define gfx_fill_ellipse   rvw_fill_ellipse
#define gfx_darken_rect    rvw_darken_rect
#define gfx_spike          rvw_spike
#endif
