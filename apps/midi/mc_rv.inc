// THE ROUND VIEW (the round panels: app_t.round = APP_ROUND_HI; the AMOLED-1.75C 466 x 466, the Watcher 412 x 412).
// The app draws its 320 x 240 layout as before; these functions put the frame of the layout in a logical square of
// RVM_S x RVM_S at the row RVM_OY (the frame y 0 = the square y RVM_OY), and the square on the panel at
// k = gfx_w / RVM_S, the same on the two axes (sharp: no scale of pixels). The rows of the frame then follow the
// circle: the layout of the round build keeps each row in its chord (rvm_ch). The text has a whole scale (1 -> 2,
// others: the scale x k): rvm_text_width gives its width in the frame, rvm_text keeps the left end and the middle row.
// After this file the gfx names of the app go to these functions (macros). rvm_pt() changes a touch point.
#if BOARD_ROUND
#ifndef RVM_S
#define RVM_S 320
#endif
#ifndef RVM_OY
#define RVM_OY 40
#endif
static float rvm_k(void)
{
    return (float)gfx_w / RVM_S;
}
static int rvm_x(int v)
{
    return (int)floorf((float)v * rvm_k());
}
static int rvm_y(int v)
{
    return (int)floorf((float)(v + RVM_OY) * rvm_k());
}
static int rvm_ps(int scale)
{
    return scale <= 1 ? 2 : (int)((float)scale * rvm_k() + 0.5f);
}
// the half chord of the circle of the square at the row y of the frame; the narrowest of the rows y .. y + h
static int rvm_ch(int y)
{
    int dy = y + RVM_OY - RVM_S / 2, r = RVM_S / 2;
    return dy * dy >= r * r ? 0 : (int)sqrtf((float)(r * r - dy * dy));
}
static int rvm_ch2(int y, int h)
{
    int a = rvm_ch(y), b = rvm_ch(y + h);
    return a < b ? a : b;
}
// a touch point of the panel to a point of the frame
static void rvm_pt(int *x, int *y)
{
    float k = rvm_k();
    *x = (int)((float)*x / k);
    *y = (int)((float)*y / k) - RVM_OY;
}
static void rvm_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    int x0 = rvm_x(x), y0 = rvm_y(y);
    gfx_fill_rect(x0, y0, rvm_x(x + w) - x0, rvm_y(y + h) - y0, c);
}
static void rvm_frame_rect(int x, int y, int w, int h, int t, uint16_t c)
{
    int x0 = rvm_x(x), y0 = rvm_y(y), tt = (int)((float)t * rvm_k() + 0.5f);
    gfx_frame_rect(x0, y0, rvm_x(x + w) - x0, rvm_y(y + h) - y0, tt < 1 ? 1 : tt, c);
}
static void rvm_fill_circle(int cx, int cy, int r, uint16_t c)
{
    float k = rvm_k();
    gfx_fill_circle((int)(((float)cx + 0.5f) * k), (int)(((float)(cy + RVM_OY) + 0.5f) * k), (int)((float)r * k + 0.5f),
                    c);
}
static int rvm_text_width(const char *s, int scale)
{
    return (int)ceilf((float)gfx_text_width(s, rvm_ps(scale)) / rvm_k());
}
// the panel row of the top of a text of the scale at the frame row y (its middle row stays)
static int rvm_ty(int y, int scale)
{
    return (int)(((float)(y + RVM_OY) + 3.5f * (float)scale) * rvm_k()) - 7 * rvm_ps(scale) / 2;
}
static void rvm_text(int x, int y, const char *s, int scale, uint16_t c)
{
    gfx_text(rvm_x(x), rvm_ty(y, scale), s, rvm_ps(scale), c);
}
static void rvm_text_center_in(int x, int w, int y, const char *s, int scale, uint16_t c)
{
    int ps = rvm_ps(scale), x0 = rvm_x(x), x1 = rvm_x(x + w);
    gfx_text(x0 + (x1 - x0 - gfx_text_width(s, ps)) / 2, rvm_ty(y, scale), s, ps, c);
}
static void rvm_text_center(int y, const char *s, int scale, uint16_t c)
{
    gfx_text_center(rvm_ty(y, scale), s, rvm_ps(scale), c);
}
static void rvm_clip_bottom(int y)
{
    gfx_clip_bottom(rvm_y(y));
}
#undef gfx_text_width  // (the app maps these names to the anti-aliased text first: the functions above use it)
#undef gfx_text
#undef gfx_text_center_in
#undef gfx_text_center
#undef gfx_clip_bottom
#define gfx_fill_rect      rvm_fill_rect
#define gfx_frame_rect     rvm_frame_rect
#define gfx_fill_circle    rvm_fill_circle
#define gfx_text_width     rvm_text_width
#define gfx_text           rvm_text
#define gfx_text_center_in rvm_text_center_in
#define gfx_text_center    rvm_text_center
#define gfx_clip_bottom    rvm_clip_bottom
#endif
