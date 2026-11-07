#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "gfx.h"

uint16_t *g_fb;

// ---------------------------------------------------------------- font

typedef struct {
    char ch;
    const char *rows[7];
} glyph_src_t;

static const glyph_src_t s_glyph_src[] = {
        {'0', {" ### ", "#   #", "#  ##", "# # #", "##  #", "#   #", " ### "}},
        {'1', {"  #  ", " ##  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
        {'2', {" ### ", "#   #", "    #", "   # ", "  #  ", " #   ", "#####"}},
        {'3', {"#####", "   # ", "  #  ", "   # ", "    #", "#   #", " ### "}},
        {'4', {"   # ", "  ## ", " # # ", "#  # ", "#####", "   # ", "   # "}},
        {'5', {"#####", "#    ", "#### ", "    #", "    #", "#   #", " ### "}},
        {'6', {"  ## ", " #   ", "#    ", "#### ", "#   #", "#   #", " ### "}},
        {'7', {"#####", "    #", "   # ", "  #  ", " #   ", " #   ", " #   "}},
        {'8', {" ### ", "#   #", "#   #", " ### ", "#   #", "#   #", " ### "}},
        {'9', {" ### ", "#   #", "#   #", " ####", "    #", "   # ", " ##  "}},
        {'A', {" ### ", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
        {'B', {"#### ", "#   #", "#   #", "#### ", "#   #", "#   #", "#### "}},
        {'C', {" ### ", "#   #", "#    ", "#    ", "#    ", "#   #", " ### "}},
        {'D', {"#### ", "#   #", "#   #", "#   #", "#   #", "#   #", "#### "}},
        {'E', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#####"}},
        {'F', {"#####", "#    ", "#    ", "#### ", "#    ", "#    ", "#    "}},
        {'G', {" ### ", "#   #", "#    ", "# ###", "#   #", "#   #", " ####"}},
        {'H', {"#   #", "#   #", "#   #", "#####", "#   #", "#   #", "#   #"}},
        {'I', {" ### ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
        {'J', {"  ###", "   # ", "   # ", "   # ", "   # ", "#  # ", " ##  "}},
        {'K', {"#   #", "#  # ", "# #  ", "##   ", "# #  ", "#  # ", "#   #"}},
        {'L', {"#    ", "#    ", "#    ", "#    ", "#    ", "#    ", "#####"}},
        {'M', {"#   #", "## ##", "# # #", "# # #", "#   #", "#   #", "#   #"}},
        {'N', {"#   #", "#   #", "##  #", "# # #", "#  ##", "#   #", "#   #"}},
        {'O', {" ### ", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
        {'P', {"#### ", "#   #", "#   #", "#### ", "#    ", "#    ", "#    "}},
        {'Q', {" ### ", "#   #", "#   #", "#   #", "# # #", "#  # ", " ## #"}},
        {'R', {"#### ", "#   #", "#   #", "#### ", "# #  ", "#  # ", "#   #"}},
        {'S', {" ####", "#    ", "#    ", " ### ", "    #", "    #", "#### "}},
        {'T', {"#####", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
        {'U', {"#   #", "#   #", "#   #", "#   #", "#   #", "#   #", " ### "}},
        {'V', {"#   #", "#   #", "#   #", "#   #", "#   #", " # # ", "  #  "}},
        {'W', {"#   #", "#   #", "#   #", "# # #", "# # #", "# # #", " # # "}},
        {'X', {"#   #", "#   #", " # # ", "  #  ", " # # ", "#   #", "#   #"}},
        {'Y', {"#   #", "#   #", "#   #", " # # ", "  #  ", "  #  ", "  #  "}},
        {'Z', {"#####", "    #", "   # ", "  #  ", " #   ", "#    ", "#####"}},
        {'-', {"     ", "     ", "     ", "#####", "     ", "     ", "     "}},
        {':', {"     ", " ##  ", " ##  ", "     ", " ##  ", " ##  ", "     "}},
        {'.', {"     ", "     ", "     ", "     ", "     ", " ##  ", " ##  "}},
        {'!', {"  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "     ", "  #  "}},
        {'/', {"    #", "    #", "   # ", "  #  ", " #   ", "#    ", "#    "}},
        {'>', {" #   ", "  #  ", "   # ", "    #", "   # ", "  #  ", " #   "}},
        {'<', {"   # ", "  #  ", " #   ", "#    ", " #   ", "  #  ", "   # "}},
        {'+', {"     ", "  #  ", "  #  ", "#####", "  #  ", "  #  ", "     "}},
        {'%', {"##   ", "##  #", "   # ", "  #  ", " #   ", "#  ##", "   ##"}},
        {'?', {" ### ", "#   #", "    #", "   # ", "  #  ", "     ", "  #  "}},
        {',', {"     ", "     ", "     ", "     ", " ##  ", "  #  ", " #   "}},
        {'*', {"     ", "# # #", " ### ", "#####", " ### ", "# # #", "     "}},
        {'#', {" # # ", " # # ", "#####", " # # ", "#####", " # # ", " # # "}},
        {'=', {"     ", "     ", "#####", "     ", "#####", "     ", "     "}},
        // lower-case letters
        {'i', {"  #  ", "     ", " ##  ", "  #  ", "  #  ", "  #  ", " ### "}},
        {'v', {"     ", "     ", "#   #", "#   #", "#   #", " # # ", "  #  "}},
        {'m', {"     ", "     ", "## # ", "# # #", "# # #", "# # #", "# # #"}},
        {'b', {"#    ", "#    ", "# ## ", "##  #", "#   #", "#   #", "#### "}},
        // the rest of the lower-case letters and the ASCII signs (WiFi passwords)
        {'a', {"     ", "     ", " ### ", "    #", " ####", "#   #", " ####"}},
        {'c', {"     ", "     ", " ### ", "#    ", "#    ", "#   #", " ### "}},
        {'d', {"    #", "    #", " ## #", "#  ##", "#   #", "#   #", " ####"}},
        {'e', {"     ", "     ", " ### ", "#   #", "#####", "#    ", " ### "}},
        {'f', {"  ## ", " #  #", " #   ", "###  ", " #   ", " #   ", " #   "}},
        {'g', {"     ", " ####", "#   #", "#   #", " ####", "    #", " ### "}},
        {'h', {"#    ", "#    ", "# ## ", "##  #", "#   #", "#   #", "#   #"}},
        {'j', {"   # ", "     ", "  ## ", "   # ", "   # ", "#  # ", " ##  "}},
        {'k', {"#    ", "#    ", "#  # ", "# #  ", "##   ", "# #  ", "#  # "}},
        {'l', {" ##  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", " ### "}},
        {'n', {"     ", "     ", "# ## ", "##  #", "#   #", "#   #", "#   #"}},
        {'o', {"     ", "     ", " ### ", "#   #", "#   #", "#   #", " ### "}},
        {'p', {"     ", "     ", "#### ", "#   #", "#### ", "#    ", "#    "}},
        {'q', {"     ", "     ", " ## #", "#  ##", " ####", "    #", "    #"}},
        {'r', {"     ", "     ", "# ## ", "##  #", "#    ", "#    ", "#    "}},
        {'s', {"     ", "     ", " ####", "#    ", " ### ", "    #", "#### "}},
        {'t', {" #   ", " #   ", "###  ", " #   ", " #   ", " #  #", "  ## "}},
        {'u', {"     ", "     ", "#   #", "#   #", "#   #", "#  ##", " ## #"}},
        {'w', {"     ", "     ", "#   #", "#   #", "# # #", "# # #", " # # "}},
        {'x', {"     ", "     ", "#   #", " # # ", "  #  ", " # # ", "#   #"}},
        {'y', {"     ", "     ", "#   #", "#   #", " ####", "    #", " ### "}},
        {'z', {"     ", "     ", "#####", "   # ", "  #  ", " #   ", "#####"}},
        {'"', {" # # ", " # # ", "     ", "     ", "     ", "     ", "     "}},
        {'$', {"  #  ", " ####", "# #  ", " ### ", "  # #", "#### ", "  #  "}},
        {'&', {" ##  ", "#  # ", "# #  ", " #   ", "# # #", "#  # ", " ## #"}},
        {'\'', {"  #  ", "  #  ", "     ", "     ", "     ", "     ", "     "}},
        {'(', {"   # ", "  #  ", " #   ", " #   ", " #   ", "  #  ", "   # "}},
        {')', {" #   ", "  #  ", "   # ", "   # ", "   # ", "  #  ", " #   "}},
        {';', {"     ", " ##  ", " ##  ", "     ", " ##  ", "  #  ", " #   "}},
        {'@', {" ### ", "#   #", "# ###", "# # #", "# ###", "#    ", " ### "}},
        {'[', {" ### ", " #   ", " #   ", " #   ", " #   ", " #   ", " ### "}},
        {'\\', {"#    ", "#    ", " #   ", "  #  ", "   # ", "    #", "    #"}},
        {']', {" ### ", "   # ", "   # ", "   # ", "   # ", "   # ", " ### "}},
        {'^', {"  #  ", " # # ", "#   #", "     ", "     ", "     ", "     "}},
        {'_', {"     ", "     ", "     ", "     ", "     ", "     ", "#####"}},
        {'`', {" #   ", "  #  ", "     ", "     ", "     ", "     ", "     "}},
        {'{', {"   # ", "  #  ", "  #  ", " #   ", "  #  ", "  #  ", "   # "}},
        {'|', {"  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  ", "  #  "}},
        {'}', {" #   ", "  #  ", "  #  ", "   # ", "  #  ", "  #  ", " #   "}},
        {'~', {"     ", "     ", " #   ", "# # #", "   # ", "     ", "     "}},
};

// glyph bits: row r, bit 4 = left column
static uint8_t s_font[128][7];
static bool s_has_glyph[128];

static void font_init(void)
{
    memset(s_font, 0, sizeof(s_font));
    for (size_t i = 0; i < sizeof(s_glyph_src) / sizeof(s_glyph_src[0]); i++) {
        const glyph_src_t *g = &s_glyph_src[i];
        for (int r = 0; r < 7; r++) {
            uint8_t bits = 0;
            for (int c = 0; c < 5; c++) {
                if (g->rows[r][c] == '#') {
                    bits |= (uint8_t)(0x10 >> c);
                }
            }
            s_font[(int)g->ch][r] = bits;
        }
        s_has_glyph[(int)g->ch] = true;
    }
}

// ---------------------------------------------------------------- orientation

int gfx_w = LCD_W, gfx_h = LCD_H;
static orient_t s_orient = ORIENT_PORTRAIT;
static bool s_native;
// the frame size in portrait (native or not)
static uint16_t *s_fbs[2];  // the two frame buffers (gfx_init)
#if BOARD_HI_FRAME
static bool s_hi_want, s_hi;  // the HI frame (gfx.h): the wish of the app, the state of this frame
static bool s_fb_hi[2];       // the picture in each frame buffer is HI
#define HI_K (s_hi ? 2 : 1)
#else
#define s_hi false
#define HI_K 1
#endif
static bool s_rhi;  // the round frame at the panel resolution (BOARD_ROUND_HI)
static int frame_w(void)
{
    return s_native ? (s_rhi ? BOARD_ROUND_HI : BOARD_NATIVE_W) : LCD_W;
}
static int frame_h(void)
{
    return s_native ? (s_rhi ? BOARD_ROUND_HI : BOARD_NATIVE_H) : LCD_H;
}
static int s_clip_y = 1 << 30;                                          // nothing is drawn at logical y >= s_clip_y
static int s_box_x0, s_box_y0, s_box_x1 = 1 << 30, s_box_y1 = 1 << 30;  // the clip box

void gfx_clip_bottom(int y)
{
    s_clip_y = y;
}

void gfx_clip_box(int x0, int y0, int x1, int y1)
{
    s_box_x0 = x0;
    s_box_y0 = y0;
    s_box_x1 = x1;
    s_box_y1 = y1;
}

void gfx_clip_box_off(void)
{
    s_box_x0 = s_box_y0 = 0;
    s_box_x1 = s_box_y1 = 1 << 30;
}

void gfx_clip_reset(void)
{
    s_clip_y = 1 << 30;
}

void gfx_set_orient(orient_t o)
{
    s_orient = o;
    gfx_clip_reset();
    gfx_clip_box_off();
    if (orient_is_landscape(o)) {
        gfx_w = frame_h();
        gfx_h = frame_w();
    } else {
        gfx_w = frame_w();
        gfx_h = frame_h();
    }
}

void gfx_set_native(int mode)
{
    s_native = mode && BOARD_NATIVE_W > 0;
    s_rhi = s_native && mode == 2 && BOARD_ROUND_HI != BOARD_NATIVE_W;
    gfx_set_orient(s_orient);
}

bool gfx_round_hi(void)
{
    return s_rhi;
}

bool gfx_native(void)
{
    return s_native;
}

void gfx_hi_want(bool on)
{
#if BOARD_HI_FRAME
    s_hi_want = on;
#else
    (void)on;
#endif
}

void gfx_hi_frame(bool allow)
{
#if BOARD_HI_FRAME
    s_hi = s_hi_want && allow;
    s_fb_hi[g_fb == s_fbs[1] ? 1 : 0] = s_hi;  // (the render that comes draws g_fb in this layout)
#else
    (void)allow;
#endif
}

bool gfx_hi(void)
{
    return s_hi;
}

int gfx_hi_stride(void)
{
    return gfx_w * HI_K;
}

int gfx_frame_bits(void)
{
    return (s_native ? ORIENT_NATIVE : 0) | (s_hi ? ORIENT_HI : 0) | (s_rhi ? ORIENT_RHI : 0);
}

void gfx_hi_flatten(void)
{
#if BOARD_HI_FRAME
    const int i = g_fb == s_fbs[1] ? 1 : 0;
    if (!s_fb_hi[i]) {
        return;
    }
    // (in place: each normal pixel comes from a buffer pixel at the same place or after it)
    const int s = gfx_w * 2;
    for (int y = 0; y < gfx_h; y++) {
        for (int x = 0; x < gfx_w; x++) {
            g_fb[y * gfx_w + x] = g_fb[2 * y * s + 2 * x];
        }
    }
    s_fb_hi[i] = false;
#endif
}

orient_t gfx_orient(void)
{
    return s_orient;
}

void gfx_touch_to_logical(int px, int py, int *x, int *y)
{
#if BOARD_TOUCH_LOGICAL
    // the board gives logical points of the current frame
    *x = px < 0 ? 0 : px >= gfx_w ? gfx_w - 1 : px;
    *y = py < 0 ? 0 : py >= gfx_h ? gfx_h - 1 : py;
    return;
#endif
    int W = frame_w(), H = frame_h();
#if BOARD_NATIVE_W > 0 && !BOARD_TOUCH_LOGICAL
    if (s_native) {
        // the touch frame (LCD_W x LCD_H) is the picture area of the panel, scaled (board_amoled.c)
        px = (px * BOARD_NATIVE_H + BOARD_NATIVE_H / 2) / LCD_H + (BOARD_NATIVE_W - LCD_W * BOARD_NATIVE_H / LCD_H) / 2;
        py = (py * BOARD_NATIVE_H + BOARD_NATIVE_H / 2) / LCD_H;
        if (px > W - 1) {
            px = W - 1;
        }
        if (py > H - 1) {
            py = H - 1;
        }
    }
#endif
    switch (s_orient) {
    case ORIENT_PORTRAIT:
        *x = px;
        *y = py;
        break;
    case ORIENT_PORTRAIT_FLIP:
        *x = W - 1 - px;
        *y = H - 1 - py;
        break;
    case ORIENT_LAND_A:
        *x = py;
        *y = W - 1 - px;
        break;
    case ORIENT_LAND_B:
        *x = H - 1 - py;
        *y = px;
        break;
    }
}

void gfx_logical_to_touch(int lx, int ly, int *px, int *py)
{
#if BOARD_TOUCH_LOGICAL
    *px = lx;
    *py = ly;
    return;
#endif
    int W = frame_w(), H = frame_h(), x, y;
    switch (s_orient) {
    case ORIENT_PORTRAIT:
        x = lx;
        y = ly;
        break;
    case ORIENT_PORTRAIT_FLIP:
        x = W - 1 - lx;
        y = H - 1 - ly;
        break;
    case ORIENT_LAND_A:
        x = W - 1 - ly;
        y = lx;
        break;
    default:
        x = ly;
        y = H - 1 - lx;
        break;
    }
#if BOARD_NATIVE_W > 0 && !BOARD_TOUCH_LOGICAL
    if (s_native) {
        x = (x - (BOARD_NATIVE_W - LCD_W * BOARD_NATIVE_H / LCD_H) / 2) * LCD_H / BOARD_NATIVE_H;
        y = y * LCD_H / BOARD_NATIVE_H;
        if (x < 0) {
            x = 0;
        }
        if (x > LCD_W - 1) {
            x = LCD_W - 1;
        }
        if (y > LCD_H - 1) {
            y = LCD_H - 1;
        }
    }
#endif
    *px = x;
    *py = y;
}

// Clip a logical rectangle. The buffer has the logical layout, so no turn is necessary.
static bool map_rect(int *x, int *y, int *w, int *h)
{
    int lx = *x, ly = *y, lw = *w, lh = *h;
    if (ly + lh > s_clip_y) {
        lh = s_clip_y - ly;
    }
    if (lx < s_box_x0) {
        lw -= s_box_x0 - lx;
        lx = s_box_x0;
    }
    if (ly < s_box_y0) {
        lh -= s_box_y0 - ly;
        ly = s_box_y0;
    }
    if (lx + lw > s_box_x1) {
        lw = s_box_x1 - lx;
    }
    if (ly + lh > s_box_y1) {
        lh = s_box_y1 - ly;
    }
    if (lx < 0) {
        lw += lx;
        lx = 0;
    }
    if (ly < 0) {
        lh += ly;
        ly = 0;
    }
    if (lx + lw > gfx_w) {
        lw = gfx_w - lx;
    }
    if (ly + lh > gfx_h) {
        lh = gfx_h - ly;
    }
    if (lw <= 0 || lh <= 0) {
        return false;
    }
    *x = lx;
    *y = ly;
    *w = lw;
    *h = lh;
    return true;
}

// ---------------------------------------------------------------- fast sine

#define SIN_N 512  // power of 2
static float s_sin[SIN_N + 1];

static void sin_init(void)
{
    for (int i = 0; i <= SIN_N; i++) {
        s_sin[i] = sinf(i * 6.2831853f / SIN_N);
    }
}

float fsin(float x)
{
    float f = x * (SIN_N / 6.2831853f);
    float fl = floorf(f);
    int i = (int)fl & (SIN_N - 1);
    float fr = f - fl;
    return s_sin[i] + (s_sin[i + 1] - s_sin[i]) * fr;
}

// ---------------------------------------------------------------- drawing

uint16_t hsv(int h, int s, int v)
{
    h %= 360;
    if (h < 0) {
        h += 360;
    }
    int region = h / 60;
    int rem = (h - region * 60) * 255 / 60;
    int p = v * (255 - s) / 255;
    int q = v * (255 - (s * rem) / 255) / 255;
    int t = v * (255 - (s * (255 - rem)) / 255) / 255;
    switch (region) {
    case 0:
        return rgb(v, t, p);
    case 1:
        return rgb(q, v, p);
    case 2:
        return rgb(p, v, t);
    case 3:
        return rgb(p, q, v);
    case 4:
        return rgb(t, p, v);
    default:
        return rgb(v, p, q);
    }
}

void gfx_init(void)
{
    // Two buffers of 220 KB do not fit in internal RAM. In PSRAM the drawing is
    // a little slower, but it runs at the same time as the LCD transfer.
    size_t bytes = LCD_W * LCD_H * sizeof(uint16_t);
#if BOARD_NATIVE_W > 0
    if (BOARD_NATIVE_W * BOARD_NATIVE_H * sizeof(uint16_t) > bytes) {
        bytes = BOARD_NATIVE_W * BOARD_NATIVE_H * sizeof(uint16_t);
    }
    if (BOARD_ROUND_HI * BOARD_ROUND_HI * sizeof(uint16_t) > bytes) {
        bytes = BOARD_ROUND_HI * BOARD_ROUND_HI * sizeof(uint16_t);
    }
#endif
#if BOARD_HI_FRAME
    bytes *= 4;  // (the HI frame: 2 x 2 buffer pixels for each logical pixel)
#endif
    for (int i = 0; i < 2; i++) {
        // cache-line aligned: the LCD task reads 64-byte parts
        s_fbs[i] = heap_caps_aligned_alloc(64, bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        assert(s_fbs[i]);
        memset(s_fbs[i], 0, bytes);
    }
    g_fb = s_fbs[0];
    font_init();
    sin_init();
}

uint16_t *gfx_swap(void)
{
    uint16_t *done = g_fb;
#if BOARD_HI_FRAME
    s_fb_hi[done == s_fbs[1] ? 1 : 0] = s_hi;
#endif
    g_fb = done == s_fbs[0] ? s_fbs[1] : s_fbs[0];
    return done;
}

void gfx_fill(uint16_t c)
{
    uint16_t *p = g_fb;
    const int fw = gfx_w * HI_K, fh = gfx_h * HI_K;
    for (int i = 0; i < fw; i++) {
        p[i] = c;
    }
    for (int j = 1; j < fh; j++) {
        memcpy(p + j * fw, p, fw * sizeof(uint16_t));
    }
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    if (!map_rect(&x, &y, &w, &h)) {
        return;
    }
    if (s_hi) {
        x *= 2;
        y *= 2;
        w *= 2;
        h *= 2;
    }
    const int stride = gfx_w * HI_K;
    uint16_t *row = g_fb + y * stride + x;
    if (w <= 8) {
        // narrow and tall: a plain loop is faster than memcpy
        for (int j = 0; j < h; j++, row += stride) {
            for (int i = 0; i < w; i++) {
                row[i] = c;
            }
        }
        return;
    }
    for (int i = 0; i < w; i++) {
        row[i] = c;
    }
    for (int j = 1; j < h; j++) {
        memcpy(row + j * stride, row, w * sizeof(uint16_t));
    }
}

void gfx_frame_rect(int x, int y, int w, int h, int t, uint16_t c)
{
    gfx_fill_rect(x, y, w, t, c);
    gfx_fill_rect(x, y + h - t, w, t, c);
    gfx_fill_rect(x, y + t, t, h - 2 * t, c);
    gfx_fill_rect(x + w - t, y + t, t, h - 2 * t, c);
}

void gfx_darken_rect(int x, int y, int w, int h, int shift)
{
    if (shift <= 0 || !map_rect(&x, &y, &w, &h)) {
        return;
    }
    // Mask keeps each channel from bleeding into its neighbour after the shift.
    const uint16_t mask = (shift == 1) ? 0x7BEF : 0x39E7;
    if (s_hi) {
        x *= 2;
        y *= 2;
        w *= 2;
        h *= 2;
    }
    for (int j = 0; j < h; j++) {
        uint16_t *p = g_fb + (y + j) * gfx_w * HI_K + x;
        for (int i = 0; i < w; i++) {
            uint16_t c = (uint16_t)((p[i] >> 8) | (p[i] << 8));
            c = (uint16_t)((c >> shift) & mask);
            p[i] = (uint16_t)((c >> 8) | (c << 8));
        }
    }
}

static int isqrt(int64_t v)
{
    if (v <= 0) {
        return 0;
    }
    int r = (int)sqrtf((float)v);
    while ((int64_t)r * r > v) {
        r--;
    }
    while ((int64_t)(r + 1) * (r + 1) <= v) {
        r++;
    }
    return r;
}

void gfx_fill_circle(int cx, int cy, int r, uint16_t c)
{
    gfx_fill_ellipse(cx, cy, r, r, c);
}

void gfx_fill_ellipse(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 0 || ry < 0) {
        return;
    }
    if (ry == 0) {
        gfx_fill_rect(cx - rx, cy, 2 * rx + 1, 1, c);
        return;
    }
    // only the rows on the screen (in 64 bits: a centre or a radius near INT_MIN / INT_MAX, from a value that is not
    // a number, give 2^31 rows, and the board stops in SPLAT)
    if ((int64_t)cy + ry < 0 || (int64_t)cy - ry > gfx_h - 1) {
        return;
    }
    if ((int64_t)cx + rx < 0 || (int64_t)cx - rx >= gfx_w) {
        return;
    }
    int y0 = (int)((int64_t)cy - ry < 0 ? -cy : -ry), y1 = (int)((int64_t)cy + ry > gfx_h - 1 ? gfx_h - 1 - cy : ry);
    float inv = 1.0f / ((float)ry * (float)ry);
    for (int dy = y0; dy <= y1; dy++) {
        float f = 1.0f - (float)dy * (float)dy * inv;
        int dx = f > 0 ? (int)(rx * sqrtf(f) + 0.5f) : 0;
        gfx_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}

void gfx_ring(int cx, int cy, int r_out, int r_in, uint16_t c)
{
    for (int dy = -r_out; dy <= r_out; dy++) {
        if (cy + dy < 0 || cy + dy >= gfx_h) {
            continue;
        }
        int xo = isqrt((int64_t)r_out * r_out - (int64_t)dy * dy);
        if (dy > -r_in && dy < r_in) {
            int xi = isqrt((int64_t)r_in * r_in - (int64_t)dy * dy);
            gfx_fill_rect(cx - xo, cy + dy, xo - xi, 1, c);
            gfx_fill_rect(cx + xi + 1, cy + dy, xo - xi, 1, c);
        } else {
            gfx_fill_rect(cx - xo, cy + dy, 2 * xo + 1, 1, c);
        }
    }
}

void gfx_spike(int x, int y, int w, int h, uint16_t c)
{
    for (int j = 0; j < h; j++) {
        int half = (w * (j + 1)) / (2 * h);
        gfx_fill_rect(x + w / 2 - half, y + j, 2 * half, 1, c);
    }
}

void gfx_text(int x, int y, const char *s, int scale, uint16_t c)
{
    for (; *s; s++, x += 6 * scale) {
        unsigned ch = (unsigned char)*s;
        if (ch >= 128) {
            continue;
        }
        if (ch >= 'a' && ch <= 'z' && !s_has_glyph[ch]) {
            ch -= 'a' - 'A';
        }
        for (int r = 0; r < 7; r++) {
            uint8_t bits = s_font[ch][r];
            // draw runs of set bits as one rectangle
            int col = 0;
            while (col < 5) {
                if (!(bits & (0x10 >> col))) {
                    col++;
                    continue;
                }
                int start = col;
                while (col < 5 && (bits & (0x10 >> col))) {
                    col++;
                }
                gfx_fill_rect(x + start * scale, y + r * scale, (col - start) * scale, scale, c);
            }
        }
    }
}

int gfx_text_width(const char *s, int scale)
{
    int n = (int)strlen(s);
    return n ? n * 6 * scale - scale : 0;
}

void gfx_text_center(int y, const char *s, int scale, uint16_t c)
{
    gfx_text((gfx_w - gfx_text_width(s, scale)) / 2, y, s, scale, c);
}

void gfx_text_center_in(int x, int w, int y, const char *s, int scale, uint16_t c)
{
    gfx_text(x + (w - gfx_text_width(s, scale)) / 2, y, s, scale, c);
}
