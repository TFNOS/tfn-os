// MURANO: the workshop. It is painted one time at enter() into a picture of the world (WW x WH px, RGB565 in PSRAM):
// the furnace with the glory hole, the wall with the window to the canal, the tools, the shelf of finished work, the
// marver, the bench, the floor. Each frame copies a part of it to the screen; the fire in the glory hole and the water
// of the canal are painted again in each frame.
// Included by app_murano.c.

// ---------------------------------------------------------------- the colours (ramps from dark to light)
typedef struct {
    float r, g, b;
} c3;
#define RAMP(name) static const uint8_t name[][3]
RAMP(BRICK) = {{0x12, 0x07, 0x05}, {0x1f, 0x0d, 0x08}, {0x2e, 0x14, 0x0b}, {0x41, 0x19, 0x0d},
               {0x58, 0x21, 0x0f}, {0x72, 0x2b, 0x12}, {0x8f, 0x38, 0x15}, {0xae, 0x48, 0x18},
               {0xcc, 0x5e, 0x1e}, {0xe5, 0x7d, 0x29}, {0xf6, 0xa2, 0x3c}};
RAMP(MORTAR) = {{0x0b, 0x05, 0x04}, {0x16, 0x0b, 0x07}, {0x24, 0x12, 0x0b},
                {0x34, 0x1b, 0x10}, {0x4a, 0x27, 0x17}, {0x64, 0x36, 0x1f}};
RAMP(PLASTER) = {{0x0c, 0x09, 0x09}, {0x14, 0x10, 0x10}, {0x1c, 0x16, 0x15}, {0x25, 0x1d, 0x1a}, {0x2f, 0x25, 0x20},
                 {0x3b, 0x2e, 0x27}, {0x4a, 0x39, 0x2f}, {0x5c, 0x46, 0x37}, {0x71, 0x55, 0x41}};
RAMP(WOOD) = {{0x0f, 0x08, 0x05}, {0x1b, 0x0f, 0x08}, {0x28, 0x17, 0x0c}, {0x38, 0x20, 0x12}, {0x4a, 0x2b, 0x17},
              {0x5f, 0x38, 0x1d}, {0x77, 0x48, 0x26}, {0x92, 0x5a, 0x30}, {0xad, 0x6e, 0x3c}};
RAMP(STEEL) = {{0x07, 0x08, 0x0a}, {0x10, 0x12, 0x17}, {0x1a, 0x1d, 0x24}, {0x26, 0x2a, 0x33}, {0x35, 0x3a, 0x44},
               {0x48, 0x4e, 0x58}, {0x61, 0x67, 0x6f}, {0x80, 0x86, 0x8c}, {0xa6, 0xab, 0xb0}, {0xd2, 0xd5, 0xd7}};
RAMP(STONE) = {{0x1c, 0x1a, 0x18}, {0x2c, 0x29, 0x25}, {0x3e, 0x3a, 0x34}, {0x53, 0x4d, 0x45}, {0x6b, 0x64, 0x59},
               {0x85, 0x7c, 0x6f}, {0xa1, 0x97, 0x87}, {0xbd, 0xb3, 0xa2}, {0xd8, 0xcf, 0xbd}, {0xec, 0xe5, 0xd6}};
RAMP(HOT) = {{0x1e, 0x05, 0x02}, {0x3d, 0x0a, 0x03}, {0x65, 0x13, 0x04}, {0x8f, 0x1f, 0x06},
             {0xb8, 0x32, 0x0a}, {0xd9, 0x50, 0x0f}, {0xf0, 0x74, 0x17}, {0xfd, 0x9a, 0x27},
             {0xff, 0xbd, 0x45}, {0xff, 0xda, 0x76}, {0xff, 0xef, 0xb0}, {0xff, 0xfb, 0xe8}};
RAMP(NIGHT) = {{0x03, 0x05, 0x0b}, {0x06, 0x0a, 0x16}, {0x0a, 0x10, 0x22},
               {0x0f, 0x18, 0x30}, {0x15, 0x21, 0x40}, {0x1d, 0x2c, 0x52}};
RAMP(WATER) = {{0x02, 0x04, 0x0a}, {0x04, 0x08, 0x12}, {0x07, 0x10, 0x1f},
               {0x0b, 0x18, 0x2d}, {0x10, 0x22, 0x3d}, {0x17, 0x2d, 0x4e}};
RAMP(FACADE) = {{0x0c, 0x08, 0x0a}, {0x14, 0x0d, 0x10}, {0x1d, 0x13, 0x16},
                {0x27, 0x1a, 0x1c}, {0x32, 0x22, 0x24}, {0x3e, 0x2a, 0x2b}};
RAMP(LAMP) = {{0x5a, 0x2a, 0x0c}, {0x9a, 0x4f, 0x16}, {0xd9, 0x8a, 0x2b}, {0xff, 0xc5, 0x60}, {0xff, 0xe7, 0xa6}};
RAMP(BRASS) = {{0x1c, 0x12, 0x06}, {0x3a, 0x26, 0x0c}, {0x5e, 0x3f, 0x14}, {0x86, 0x60, 0x1f},
               {0xb0, 0x85, 0x2e}, {0xd6, 0xad, 0x4a}, {0xf2, 0xd3, 0x7a}};
RAMP(SKIN) = {{0x1a, 0x0b, 0x06}, {0x36, 0x19, 0x0e}, {0x55, 0x29, 0x16}, {0x75, 0x3c, 0x22}, {0x94, 0x51, 0x2f},
              {0xb1, 0x68, 0x40}, {0xcb, 0x83, 0x52}, {0xe2, 0xa2, 0x72}, {0xf2, 0xc1, 0x94}};
RAMP(LINEN) = {{0x1a, 0x17, 0x15}, {0x2e, 0x29, 0x25}, {0x47, 0x40, 0x3a}, {0x62, 0x59, 0x51},
               {0x81, 0x77, 0x6c}, {0xa2, 0x97, 0x88}, {0xc4, 0xb9, 0xa8}, {0xe0, 0xd6, 0xc4}};
RAMP(PARCH) = {{0x2a, 0x1d, 0x11}, {0x4a, 0x36, 0x20}, {0x6e, 0x55, 0x32}, {0x97, 0x7a, 0x4c},
               {0xbf, 0xa2, 0x6c}, {0xdc, 0xc3, 0x8e}, {0xef, 0xdc, 0xae}};
RAMP(TERRA) = {{0x11, 0x07, 0x06}, {0x1b, 0x0d, 0x0b}, {0x27, 0x14, 0x10}, {0x35, 0x1c, 0x16}, {0x45, 0x25, 0x1c}};
RAMP(ROUND) = {{0x0b, 0x16, 0x15}, {0x12, 0x22, 0x21}, {0x1b, 0x30, 0x2e},
               {0x28, 0x44, 0x41}, {0x3c, 0x5f, 0x5a}, {0x6f, 0x8f, 0x8a}};
RAMP(SKYD) = {{0x5f, 0x5d, 0x8e}, {0x8a, 0x77, 0xa2}, {0xb8, 0x8a, 0xa0},
              {0xdf, 0x9e, 0x95}, {0xf2, 0xb9, 0x8c}, {0xfb, 0xd5, 0x9b}};
RAMP(FACD) = {{0x4a, 0x2e, 0x26}, {0x6b, 0x42, 0x32}, {0x8d, 0x5a, 0x3e},
              {0xb0, 0x78, 0x50}, {0xcf, 0x9a, 0x68}, {0xe6, 0xbb, 0x88}};
RAMP(WATD) = {{0x4e, 0x6a, 0x86}, {0x6b, 0x88, 0xa2}, {0x8a, 0xa6, 0xbb},
              {0xb4, 0xbf, 0xc6}, {0xe2, 0xc9, 0xa6}, {0xfd, 0xe0, 0xa8}};
RAMP(ROUD) = {{0x5d, 0x6a, 0x52}, {0x7d, 0x8a, 0x68}, {0x9f, 0xab, 0x82},
              {0xc3, 0xcb, 0x9f}, {0xe4, 0xe8, 0xc4}, {0xfb, 0xf6, 0xdc}};
#define NR(R) ((int)(sizeof(R) / sizeof(R[0])))

static inline c3 C3(const uint8_t *p)
{
    c3 c = {p[0], p[1], p[2]};
    return c;
}
static inline c3 cmix(c3 a, c3 b, float t)
{
    c3 c = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    return c;
}
static inline c3 crgb(float r, float g, float b)
{
    c3 c = {r, g, b};
    return c;
}
static inline uint16_t pk(c3 c)
{
    int r = (int)c.r, g = (int)c.g, b = (int)c.b;
    r = r < 0 ? 0 : r > 255 ? 255 : r;
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    b = b < 0 ? 0 : b > 255 ? 255 : b;
    return rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
}
static inline uint16_t sw16(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}  // (the frame has the bytes swapped)
static inline c3 unpk(uint16_t s)
{
    uint16_t v = (uint16_t)((s >> 8) | (s << 8));
    c3 c = {(float)(((v >> 11) & 31) * 255 / 31), (float)(((v >> 5) & 63) * 255 / 63), (float)((v & 31) * 255 / 31)};
    return c;
}

// ---------------------------------------------------------------- small helpers
static const uint8_t B4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
static inline float bay(int x, int y)
{
    return (B4[(y & 3) * 4 + (x & 3)] + 0.5f) / 16.0f;
}
static inline float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float len2(float x, float y)
{
    return sqrtf(x * x + y * y);
}  // (hypotf is slow on the board)
// (sinf and cosf of the C library take about 12 us a call in an app file on the board: the code uses fsin and fcos of
// gfx.h, a table with an error of about 1e-4)
static inline int clampi(int v, int a, int b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float sstep(float a, float b, float v)
{
    float t = clampf((v - a) / (b - a), 0, 1);
    return t * t * (3 - 2 * t);
}
static inline float h2(int x, int y)
{
    uint32_t h = (uint32_t)x * 374761393u ^ (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (float)((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}
static float n2(float x, float y)
{
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float xf = x - xi, yf = y - yi;
    float u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf);
    float a = h2(xi, yi), b = h2(xi + 1, yi), c = h2(xi, yi + 1), d = h2(xi + 1, yi + 1);
    return a + (b - a) * u + (c - a) * v + (a - b - c + d) * u * v;
}
// a colour of a ramp: flat, or with the ordered dither between two steps (only for glows)
static inline c3 flat_(const uint8_t (*R)[3], int n, float v)
{
    int i = (int)(clampf(v, 0, 1) * (n - 1) + 0.5f);
    return C3(R[i]);
}
static inline c3 dith_(const uint8_t (*R)[3], int n, float v, int x, int y)
{
    float f = clampf(v, 0, 1) * (n - 1);
    int i = (int)f;
    if (f - i > bay(x, y)) {
        i++;
    }
    return C3(R[i < n ? i : n - 1]);
}
#define FLAT(R, v)       flat_(R, NR(R), v)
#define DITH(R, v, x, y) dith_(R, NR(R), v, x, y)
#define CR(R, i)         C3(R[i])

// ---------------------------------------------------------------- the size of the world and its places
#define WW       900
#define WH0      280  // the rows of the room; a taller screen (the Tab5) gets rafters above (s_top rows)
#define HOLE_X   150
#define HOLE_Y   150
#define HOLE_R   46
#define PIPE_Y   150
#define PIVOT_X  820  // the right hand of the gaffer holds the pipe here
#define TIP_X    480  // the end of the pipe: the glass starts here
#define SLIDE_PX 268  // the pipe goes this far to the left into the glory hole
#define WCX      580  // the window: the middle, the start of the arch, the point, the sill
#define WSPRING  80
#define WPEAK    26
#define WSILL    168
#define FLOOR_Y  262
#define LAMP_X   498
#define LAMP_Y   52
#define DAWN_X0  440  // the part of the world that the morning changes (the window and the wall round it)
#define DAWN_X1  720
#define DAWN_Y1  262

static uint16_t *s_world;  // WW x s_wh (row 0 = the world row -s_top)
static uint16_t *s_dawn;   // the morning: (DAWN_X1 - DAWN_X0) x DAWN_Y1
static int s_top, s_wh;

static inline void wset(int x, int y, c3 c)
{
    y += s_top;
    if (x < 0 || x >= WW || y < 0 || y >= s_wh) {
        return;
    }
    s_world[y * WW + x] = pk(c);
}
static inline c3 wget(int x, int y)
{
    x = clampi(x, 0, WW - 1);
    y = clampi(y + s_top, 0, s_wh - 1);
    return unpk(s_world[y * WW + x]);
}
static void wrect(int x, int y, int w, int h, c3 c)
{
    for (int j = y; j < y + h; j++) {
        for (int i = x; i < x + w; i++) {
            wset(i, j, c);
        }
    }
}
static void wline(int x0, int y0, int x1, int y1, c3 c)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        wset(x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * e;
        if (e2 >= dy) {
            e += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            e += dx;
            y0 += sy;
        }
    }
}

static inline float dome_top(int x)
{
    float k = (x - 150) / 158.0f;
    return 38 + 66 * k * k;
}
static inline bool is_furnace(int x, int y)
{
    return x < 306 && y >= dome_top(x) && y < FLOOR_Y;
}
static inline float furn_light(int x, int y)
{
    float d = len2((float)(x - HOLE_X), (float)(y - HOLE_Y)) - HOLE_R;
    return d <= 0 ? 1 : 1 / (1 + (d / 95) * (d / 95));
}
static inline float lamp_light(int x, int y)
{
    float d = len2((x - LAMP_X) * 0.9f, (float)(y - LAMP_Y - 4));
    return 1 / (1 + (d / 46) * (d / 46));
}
static inline float lit(int x, int y)
{
    return furn_light(x, y) * 0.6f + lamp_light(x, y) * 0.45f;
}
static float win_half(int y, int g)
{
    int pk_ = WPEAK - g;
    float hw = 50.0f + g;
    if (y >= WSPRING) {
        return hw;
    }
    if (y < pk_) {
        return -1;
    }
    float t = (float)(WSPRING - y) / (WSPRING - pk_);
    return hw * (1 - sstep(0, 1, t));
}
static inline bool in_win(int x, int y, int g)
{
    if (y > WSILL + g) {
        return false;
    }
    float h = win_half(y, g);
    return h >= 0 && fabsf((float)(x - WCX)) <= h;
}
static inline bool in_canal(int x, int y)
{
    return x < WCX - 2 && in_win(x, y, 0) && y >= WPEAK;
}
static float soot_at(int x, int y)
{
    int top = HOLE_Y - 34;
    if (y > top) {
        return 0;
    }
    float spread = 26 + (top - y) * 0.55f;
    return sstep(spread, spread * 0.35f, fabsf((float)(x - HOLE_X))) * (0.55f + 0.45f * n2(x / 5.0f, y / 26.0f)) *
           sstep((float)top + 4, (float)top - 30, (float)y);
}

// ---------------------------------------------------------------- the painting of the room
static void paint_wall(void)
{
    // (the noise and the light once for each 2 x 2 block: the painting at enter is 4 times faster)
    for (int y = 0; y < WH0; y += 2) {
        for (int x = 0; x < WW; x += 2) {
            float v = 0.3f + 0.16f * (n2(x / 46.0f, y / 38.0f) - 0.5f) + 0.07f * (n2(x / 11.0f + 50, y / 9.0f) - 0.5f);
            v = v * 0.6f + furn_light(x, y) * 0.62f + lamp_light(x, y) * 0.42f - soot_at(x, y) * 0.3f;
            c3 c = FLAT(PLASTER, v);
            wset(x, y, c);
            wset(x + 1, y, c);
            wset(x, y + 1, c);
            wset(x + 1, y + 1, c);
        }
    }
    // two places where the plaster fell off: old bricks behind it
    static const int16_t holes[][3] = {{372, 200, 17}, {874, 150, 14}};
    for (int k = 0; k < 2; k++) {
        int cx = holes[k][0], cy = holes[k][1], r = holes[k][2];
        for (int y = cy - r - 3; y <= cy + r + 3; y++) {
            for (int x = cx - r - 8; x <= cx + r + 8; x++) {
                float e = len2((x - cx) / 1.6f, (float)(y - cy)) + (n2(x / 3.5f, y / 3.5f) - 0.5f) * 17 +
                          (n2(x / 9.0f, y / 9.0f) - 0.5f) * 10;
                if (e > r + 1) {
                    continue;
                }
                float L = furn_light(x, y);
                if (e > r) {
                    wset(x, y, FLAT(PLASTER, 0.12f + L * 0.3f));
                    wset(x, y - 1, FLAT(PLASTER, 0.5f + L * 0.5f));
                    continue;
                }
                int row = (int)floorf(y / 6.0f), off = (row & 1) * 6, col = (int)floorf((x + off) / 12.0f);
                bool mort = y % 6 == 5 || (x + off) % 12 == 11;
                wset(x, y,
                     mort ? FLAT(MORTAR, 0.2f + L * 0.5f) : FLAT(BRICK, 0.12f + h2(col, row) * 0.18f + L * 0.45f));
            }
        }
    }
    // a crack under the window
    int px = 612, py = 176;
    while (py < 252) {
        int nx = px + (int)lroundf((h2(px, py) - 0.35f) * 3), ny = py + 2 + (int)(h2(py, px) * 2);
        wline(px, py, nx, ny, CR(PLASTER, 1));
        wline(px + 1, py + 1, nx + 1, ny + 1, CR(PLASTER, 5));
        px = nx;
        py = ny;
    }
}
static void paint_beam(void)
{
    for (int y = 0; y < 15; y++) {
        for (int x = 0; x < WW; x++) {
            float v = 0.2f + 0.18f * n2(x / 34.0f, y / 1.6f) + furn_light(x, 40) * 0.35f;
            if (y == 13) {
                v += 0.18f;
            }
            if (y == 14) {
                v = 0.02f;
            }
            if (x % 151 == 0 && y < 13) {
                v = 0.05f;
            }
            wset(x, y, FLAT(WOOD, v));
        }
    }
    // a tall screen: the rafters and the dark roof above the beam
    for (int y = -s_top; y < 0; y++) {
        for (int x = 0; x < WW; x++) {
            float v = 0.04f + 0.05f * n2(x / 20.0f, y / 6.0f) + furn_light(x, 0) * 0.12f * (1 + y / (float)(s_top + 1));
            int rx = (x + 40) % 120;
            if (rx < 12) {
                v = 0.12f + (rx == 0 ? 0.12f : 0) + furn_light(x, 0) * 0.2f;
            }
            if ((y + s_top) % 46 < 5) {
                v = 0.1f + furn_light(x, 0) * 0.15f;
            }
            wset(x, y, FLAT(WOOD, v));
        }
    }
}
static void paint_posts(void)
{
    static const int16_t posts[][2] = {{504, 10}, {852, 11}};
    for (int k = 0; k < 2; k++) {
        int x0 = posts[k][0], w = posts[k][1];
        for (int y = 14; y < FLOOR_Y; y++) {
            for (int x = x0; x < x0 + w; x++) {
                float v = 0.22f + 0.16f * n2(x / 2.0f, y / 14.0f) + lit(x, y) * 0.35f;
                if (x == x0) {
                    v += 0.22f;
                }
                if (x == x0 + w - 1) {
                    v = 0.06f;
                }
                if ((y - 30) % 61 == 0) {
                    v = 0.1f;
                }
                wset(x, y, FLAT(WOOD, v));
            }
        }
        for (int q = 0; q < 30; q++) {
            for (int t = 0; t < 5; t++) {
                int x = x0 + w + q, y = 44 - q + t;
                if (y < 15) {
                    continue;
                }
                wset(x, y, FLAT(WOOD, t == 0 ? 0.5f + lit(x, y) * 0.3f : t == 4 ? 0.06f : 0.28f + lit(x, y) * 0.3f));
            }
        }
    }
}
static void paint_canal_px(int x, int y)
{
    if (y < 52) {
        wset(x, y, DITH(NIGHT, 0.12f + (y - 26) / 26.0f * 0.55f, x, y));
        if (h2(x * 7, y * 13) > 0.985f) {
            wset(x, y, CR(STONE, 7));
        }
        return;
    }
    if (y < 118) {
        float v = 0.35f + 0.25f * n2(x / 7.0f, y / 9.0f);
        if (y < 56) {
            v = 0.75f;
        }
        wset(x, y, FLAT(FACADE, v));
        return;
    }
    wset(x, y, FLAT(WATER, 0.35f + (y - 118) / 50.0f * 0.35f));
}
static void paint_roundel_px(int x, int y, float L, bool day)
{
    // the round panes (rulli) in lead
    int row = (int)lroundf((y - 30) / 10.0f), oy = 30 + row * 10;
    float ox = 588 + ((row & 1) ? 5.5f : 0);
    float cx = ox + lroundf((x - ox) / 11) * 11.0f;
    float dx = x - cx, dy = (float)(y - oy), d = len2(dx, dy);
    const uint8_t (*R)[3] = day ? ROUD : ROUND;
    c3 c;
    if (d > 4.6f) {
        c = day ? CR(STEEL, 3) : CR(STEEL, 1);
    } else {
        c = d < 1.3f ? C3(R[day ? 4 : 3]) : (d > 2.4f && d < 3.4f) ? C3(R[day ? 3 : 2]) : C3(R[day ? 2 : 1]);
        if (lroundf(dx) == -2 && lroundf(dy) == -2) {
            c = C3(R[5]);
        }
        if (!day && lroundf(dx) == -2 && lroundf(dy) == 2 && L > 0.22f) {
            c = CR(BRICK, 8);
        }
        if (!day && lroundf(dx) == 3 && lroundf(dy) == 1) {
            c = CR(ROUND, 0);
        }
    }
    wset(x, y, c);
}
static void paint_window(void)
{
    for (int y = 10; y < 180; y++) {
        for (int x = WCX - 64; x < WCX + 64; x++) {
            float L = furn_light(x, y);
            if (in_win(x, y, 7) && !in_win(x, y, 0)) {  // the frame of white Istrian stone
                float v = 0.42f + 0.14f * h2(y / 22, x > WCX ? 1 : 0) + L * 0.3f;
                if (x < WCX) {
                    v += 0.1f;
                }
                if (!in_win(x, y, 6)) {
                    v -= 0.12f;
                }
                if (in_win(x, y, 1)) {
                    v -= 0.2f;
                }
                if (y % 22 == 0 && y > WSPRING) {
                    v = 0.12f;
                }
                wset(x, y, FLAT(STONE, v));
                continue;
            }
            if (!in_win(x, y, 0)) {
                continue;
            }
            if (x >= WCX - 2 && x <= WCX + 2) {
                wset(x, y, FLAT(STONE, (x == WCX - 2 ? 0.75f : 0.45f) + L * 0.15f));
                continue;
            }
            if (x < WCX) {
                paint_canal_px(x, y);
            } else {
                paint_roundel_px(x, y, L, false);
            }
        }
    }
    wrect(WCX - 1, WPEAK - 12, 3, 4, CR(STONE, 6));
    wset(WCX, WPEAK - 13, CR(STONE, 8));
    for (int y = WSILL + 1; y < WSILL + 8; y++) {
        for (int x = WCX - 66; x < WCX + 67; x++) {
            float v = 0.62f + furn_light(x, y) * 0.2f;
            if (y == WSILL + 1) {
                v = 0.85f;
            }
            if (y >= WSILL + 6) {
                v = 0.2f;
            }
            wset(x, y, FLAT(STONE, v));
        }
    }
    for (int x = WCX - 66; x < WCX + 67; x++) {
        wset(x, WSILL + 8, cmix(wget(x, WSILL + 9), crgb(0, 0, 0), 0.5f));
    }
}
static void paint_canal_detail(void)
{
    // a funnel chimney (camino a imbuto), two lit windows and a balcony, the striped mooring pole (palina)
    for (int y = 36; y < 54; y++) {
        int w = y < 42 ? 4 : (int)fmaxf(1, lroundf(4 - (y - 42) * 0.3f));
        for (int x = 550 - w; x <= 550 + w; x++) {
            if (in_canal(x, y)) {
                wset(x, y, FLAT(FACADE, y < 38 ? 1 : 0.6f + (x < 550 ? 0.2f : 0)));
            }
        }
    }
    static const int16_t lw[][2] = {{538, 66}, {560, 72}};
    for (int k = 0; k < 2; k++) {
        for (int y = lw[k][1]; y < lw[k][1] + 12; y++) {
            for (int x = lw[k][0]; x < lw[k][0] + 6; x++) {
                if (!in_canal(x, y)) {
                    continue;
                }
                if (y - lw[k][1] < 2 && (x == lw[k][0] || x == lw[k][0] + 5)) {
                    continue;
                }
                wset(x, y, y > lw[k][1] + 8 ? CR(LAMP, 2) : CR(LAMP, 3 + (h2(x, y) > 0.7f ? 1 : 0)));
            }
        }
    }
    for (int x = 534; x < 552; x++) {
        if (in_canal(x, 86)) {
            wset(x, 86, CR(FACADE, 5));
            if (x % 3 == 0) {
                wset(x, 87, CR(FACADE, 4));
            }
            wset(x, 89, CR(FACADE, 5));
        }
    }
    for (int y = 94; y < 150; y++) {
        for (int x = 569; x < 572; x++) {
            if (!in_canal(x, y)) {
                continue;
            }
            if (y >= 118 && (h2(y, 3) > 0.55f || y > 140)) {
                continue;
            }
            c3 c = ((y - 94) / 5) & 1 ? crgb(0x7c, 0x1c, 0x16) : CR(LINEN, 6);
            if (x == 571) {
                c = cmix(c, crgb(0, 0, 0), 0.45f);
            }
            if (y >= 118) {
                c = cmix(c, CR(WATER, 3), 0.55f);
            }
            wset(x, y, c);
        }
    }
    wrect(568, 92, 5, 2, CR(LINEN, 7));
}

// the tools on the wall (a shadow first, then the tool); ty = the rows down
typedef void (*put_fn)(int x, int y, c3 c);
static int s_ty;
static void put_shadow(int x, int y, c3 c)
{
    (void)c;
    wset(x + 2, y + 2 + s_ty, cmix(wget(x + 2, y + 2 + s_ty), crgb(0, 0, 0), 0.55f));
}
static void put_real(int x, int y, c3 c)
{
    wset(x, y + s_ty, c);
}
static void thick(put_fn put, int x0, int y0, int x1, int y1, int w, c3 litc)
{
    int n = abs(x1 - x0) > abs(y1 - y0) ? abs(x1 - x0) : abs(y1 - y0);
    for (int i = 0; i <= n; i++) {
        int x = (int)lroundf(x0 + (x1 - x0) * (float)i / n), y = (int)lroundf(y0 + (y1 - y0) * (float)i / n);
        for (int k = 0; k < w; k++) {
            put(x + k, y, k == 0 ? litc : CR(STEEL, 3));
        }
    }
}
static void tool_jacks(put_fn put)
{
    for (int a = 0; a <= 20; a++) {
        float t = (float)M_PI * a / 20;
        put((int)lroundf(345 - fcos(t) * 6), (int)lroundf(31 - fsin(t) * 5), a < 8 ? CR(STEEL, 7) : CR(STEEL, 4));
        put((int)lroundf(345 - fcos(t) * 6), (int)lroundf(32 - fsin(t) * 5), CR(STEEL, 3));
    }
    thick(put, 339, 32, 335, 90, 2, CR(STEEL, 7));
    thick(put, 350, 32, 354, 90, 2, CR(STEEL, 6));
    thick(put, 335, 90, 338, 98, 2, CR(STEEL, 7));
    thick(put, 354, 90, 351, 98, 2, CR(STEEL, 6));
}
static void tool_paddle(put_fn put)
{
    for (int y = 26; y < 96; y++) {
        for (int x = 377; x < 393; x++) {
            bool blade = y >= 56, handle = x >= 383 && x <= 386;
            if (!blade && !handle) {
                continue;
            }
            if (blade && y == 95 && (x == 377 || x == 392)) {
                continue;
            }
            float v = 0.45f + 0.2f * n2(x / 1.5f, y / 9.0f);
            if (x == (blade ? 377 : 383)) {
                v += 0.3f;
            }
            if (blade && (x > 389 || y > 92)) {
                v = 0.08f;
            }
            put(x, y, FLAT(WOOD, v));
        }
    }
}
static void tool_shears(put_fn put)
{
    thick(put, 418, 42, 431, 92, 2, CR(STEEL, 7));
    thick(put, 431, 42, 418, 92, 2, CR(STEEL, 6));
    for (int a = 0; a < 24; a++) {
        float t = a / 24.0f * 2 * (float)M_PI;
        put((int)lroundf(416 + fcos(t) * 4), (int)lroundf(35 + fsin(t) * 4), CR(STEEL, a < 12 ? 6 : 4));
        put((int)lroundf(433 + fcos(t) * 4), (int)lroundf(35 + fsin(t) * 4), CR(STEEL, a < 12 ? 6 : 4));
    }
    put(424, 66, CR(BRASS, 6));
    put(425, 66, CR(BRASS, 4));
    put(424, 67, CR(BRASS, 4));
}
static void tool_block(put_fn put)
{
    for (int y = 26; y < 82; y++) {
        for (int x = 460; x < 481; x++) {
            float qx = (x - 470) / 10.0f, qy = (y - 60) / 20.0f;
            bool cup = y >= 60 && qx * qx + qy * qy < 1;
            bool handle = y < 60 && x >= 468 && x <= 471;
            if (!cup && !handle) {
                continue;
            }
            float v = 0.5f + 0.18f * n2(x / 2.0f, y / 7.0f);
            if (x == 468 || (cup && x < 463)) {
                v += 0.25f;
            }
            if (cup && y < 64) {
                v = 0.03f;
            }
            put(x, y, FLAT(WOOD, v));
        }
    }
}
static void paint_tools(void)
{
    s_ty = 31;
    for (int x = 316; x < 510; x++) {
        for (int y = 21; y < 26; y++) {
            put_real(x, y, FLAT(WOOD, y == 21 ? 0.85f : y == 25 ? 0.15f : 0.5f + furn_light(x, y) * 0.2f));
        }
    }
    void (*tools[4])(put_fn) = {tool_jacks, tool_paddle, tool_shears, tool_block};
    for (int k = 0; k < 4; k++) {
        tools[k](put_shadow);
        tools[k](put_real);
    }
    static const int16_t pegs[] = {345, 385, 425, 470};
    for (int k = 0; k < 4; k++) {
        wrect(pegs[k] - 1, 26 + s_ty, 3, 3, CR(WOOD, 7));
    }
}
static void paint_lamp(void)
{
    for (int y = 15; y < 44; y++) {
        wset(LAMP_X + ((y >> 1) & 1), y, y & 1 ? CR(BRASS, 2) : CR(BRASS, 4));
    }
    for (int y = 44; y < 60; y++) {
        for (int x = LAMP_X - 9; x <= LAMP_X + 9; x++) {
            int dy = y - 44;
            float half = dy < 3 ? 2 + dy : dy < 9 ? 6 + (dy - 3) * 0.5f : 9 - (dy - 9) * 1.2f;
            if (fabsf((float)(x - LAMP_X)) > half) {
                continue;
            }
            float v = 0.45f + (LAMP_X - x) / 18.0f + (dy == 8 ? 0.25f : 0);
            if (fabsf((float)(x - LAMP_X)) > half - 1) {
                v -= 0.25f;
            }
            wset(x, y, FLAT(BRASS, v));
        }
    }
    for (int k = 0; k < 5; k++) {
        wset(LAMP_X + 9 + k, 51 - (k >> 1), CR(BRASS, 3));
    }
    static const int8_t fl[][3] = {{15, 48, 11}, {15, 47, 10}, {14, 46, 9}, {15, 46, 10},
                                   {16, 47, 9},  {15, 45, 8},  {15, 44, 7}};
    for (int k = 0; k < 7; k++) {
        wset(LAMP_X + fl[k][0], fl[k][1], CR(HOT, fl[k][2]));
    }
}
static void paint_furnace(void)
{
    for (int y = 0; y < FLOOR_Y; y++) {
        for (int x = 0; x < 306; x++) {
            if (!is_furnace(x, y)) {
                continue;
            }
            float d = len2((float)(x - HOLE_X), (float)(y - HOLE_Y));
            if (d < HOLE_R) {
                continue;  // the hole: painted in each frame
            }
            if (d < HOLE_R + 5) {  // the iron collar
                float ang = atan2f((float)(y - HOLE_Y), (float)(x - HOLE_X));
                c3 c = FLAT(STEEL, 0.35f + 0.25f * fcos(ang - 2.4f));
                if (d < HOLE_R + 1.3f) {
                    c = CR(HOT, 6);
                }
                float k = fmodf((ang + (float)M_PI) / ((float)M_PI / 6), 1);
                if (d > HOLE_R + 2 && d < HOLE_R + 4 && (k < 0.09f || k > 0.91f)) {
                    c = CR(STEEL, 8);
                }
                wset(x, y, c);
                continue;
            }
            if (d < HOLE_R + 24) {  // the arch bricks round the hole
                float ang = atan2f((float)(y - HOLE_Y), (float)(x - HOLE_X)) + (float)M_PI;
                float segf = ang / (2 * (float)M_PI) * 28;
                int seg = (int)segf;
                float edge = (segf - seg) * (2 * (float)M_PI * d / 28);
                float q = (d - HOLE_R - 5) / 19;
                if (edge < 1.1f || q < 0.05f || q > 0.95f) {
                    wset(x, y, FLAT(MORTAR, 0.55f + 0.4f * (1 - q)));
                    continue;
                }
                float v = 0.4f + 0.4f * (1 - q) + 0.16f * h2(seg, 7) - soot_at(x, y) * 0.3f;
                if (q < 0.15f) {
                    v += 0.08f;
                }
                wset(x, y, FLAT(BRICK, v));
                continue;
            }
            int row = y / 9, off = (row & 1) * 11, col = (x + off) / 22;
            int bx = col * 22 - off + 10, by = row * 9 + 4;
            float L = furn_light(bx, by);
            if (y % 9 == 8 || (x + off) % 22 == 21) {
                wset(x, y, FLAT(MORTAR, 0.12f + L * 0.75f));
                continue;
            }
            float v = 0.14f + 0.2f * h2(col, row) + L * 0.6f - soot_at(bx, by) * 0.5f;
            if (y % 9 == 0) {
                v += 0.06f;
            }
            if ((x + off) % 22 == 0) {
                v += 0.04f;
            }
            if (h2(col * 3, row * 5) > 0.86f && (x + off) % 22 > 18 && y % 9 < 3) {
                v = 0.04f;
            }
            wset(x, y, FLAT(BRICK, v));
        }
    }
    for (int x = 0; x < 306; x++) {
        int t = (int)ceilf(dome_top(x));
        wset(x, t, CR(BRICK, 2));
        wset(x, t + 1, CR(BRICK, 3));
    }
    for (int y = 0; y < FLOOR_Y; y++) {
        if (is_furnace(305, y)) {
            wset(305, y, CR(BRICK, 1));
            wset(304, y, CR(BRICK, 2));
        }
    }
    // the fire box under the hole
    for (int y = 236; y < FLOOR_Y; y++) {
        for (int x = 120; x < 181; x++) {
            float e = len2((x - 150) / 1.3f, (float)(y - FLOOR_Y));
            if (e > 25) {
                continue;
            }
            if (e > 22) {
                wset(x, y, CR(BRICK, 1));
                continue;
            }
            float v = 0.18f + 0.3f * n2(x / 3.0f, y / 2.0f) + 0.25f * (y - 236) / 26.0f;
            wset(x, y, DITH(HOT, v * 0.55f, x, y));
        }
    }
    // the yoke: the iron rest of the pipe in front of the glory hole
    for (int y = 152; y < FLOOR_Y; y++) {
        for (int x = 248; x < 262; x++) {
            bool post = y > 160 && x >= 252 && x <= 256;
            bool vee = y <= 160 && fabsf(fabsf(x - 254.5f) - (y - 150) * 0.7f) < 1.3f;
            if (!post && !vee) {
                continue;
            }
            wset(x, y, FLAT(STEEL, (x == 252 || (vee && x < 254)) ? 0.75f : 0.32f));
        }
    }
}
static void paint_floor_things(void)
{
    // the marver: the steel table under the glass
    for (int y = 218; y < FLOOR_Y; y++) {
        for (int x = 274; x < 524; x++) {
            float v;
            if (y < 228) {
                v = 0.5f + (y - 218) * 0.04f + 0.08f * fsin(x * 0.07f);
            } else if (y == 228) {
                v = 0.92f;
            } else if (y < 254) {
                v = 0.22f + furn_light(x, y) * 0.25f + ((x - 274) % 34 == 6 && (y == 233 || y == 249) ? 0.45f : 0);
            } else if ((x > 282 && x < 290) || (x > 506 && x < 514)) {
                v = 0.2f;
            } else {
                continue;
            }
            wset(x, y, FLAT(STEEL, v));
        }
    }
    // the end of the bench arm under the hand (the growth rings of the wood)
    for (int y = 158; y < FLOOR_Y; y++) {
        for (int x = 792; x < 834; x++) {
            int ring = (int)(len2((x - 812) * 1.1f, (float)(y - 214)) / 3.2f);
            float v = (ring & 1) ? 0.42f : 0.55f;
            v += 0.1f * n2(x / 2.0f, y / 2.0f);
            if (y < 161) {
                v = 0.8f;
            }
            if (x < 795) {
                v = 0.12f;
            }
            if (fabsf(x - 812 - (y - 214) * 0.25f) < 0.7f && y > 190) {
                v = 0.08f;
            }
            wset(x, y, FLAT(WOOD, v));
        }
    }
    // the floor: terrazzo (stone bits in red lime)
    for (int y = FLOOR_Y; y < WH0; y++) {
        for (int x = 0; x < WW; x++) {
            c3 c = FLAT(TERRA, 0.25f + 0.4f * n2(x / 9.0f, y / 3.0f) + furn_light(x, y) * 0.5f);
            float k = h2(x * 3, y * 7);
            if (k > 0.94f) {
                c = k > 0.985f ? CR(STONE, 6) : k > 0.965f ? crgb(0x6a, 0x3a, 0x22) : crgb(0x33, 0x49, 0x4a);
            }
            if (y == FLOOR_Y) {
                c = CR(TERRA, 0);
            }
            wset(x, y, c);
        }
    }
    // split beech logs under the shelf: the fuel of the furnace
    for (int row = 0; row < 5; row++) {
        int n = 9 - row, cy0 = FLOOR_Y - 7 - row * 11;
        for (int k = 0; k < n; k++) {
            float cx = 662 + row * 7 + k * 15 + (int)(h2(row, k) * 4), cy = cy0 + (int)(h2(k, row) * 2);
            float r = 6.2f + h2(k * 3, row) * 1.6f, q = h2(row * 7, k);
            for (int y = (int)floorf(cy - r - 1); y <= cy + r + 1; y++) {
                for (int x = (int)floorf(cx - r - 1); x <= cx + r + 1; x++) {
                    float d = len2(x - cx, (y - cy) * 1.08f);
                    bool split = q > 0.55f && x - cx > (y - cy) * 0.6f + r * 0.25f;
                    if (d > r || split) {
                        continue;
                    }
                    float L = lit(x, y);
                    c3 c;
                    if (d > r - 1.3f) {
                        c = FLAT(WOOD, 0.1f + L * 0.2f);
                    } else {
                        int ring = ((int)(d / 1.7f)) & 1;
                        c = FLAT(WOOD, 0.5f + ring * 0.12f + L * 0.35f - (d < 1.2f ? 0.25f : 0));
                        if (fabsf(atan2f(y - cy, x - cx) - q * 6) < 0.08f) {
                            c = CR(WOOD, 2);
                        }
                    }
                    wset(x, y, c);
                }
            }
        }
    }
    // the water bucket (the wooden tools go in it so that they do not burn)
    for (int y = 236; y < FLOOR_Y; y++) {
        for (int x = 530; x < 556; x++) {
            float half = 13 - (y - 236) * 0.08f;
            if (fabsf((float)(x - 543)) > half) {
                continue;
            }
            int stave = (x - 530) / 4;
            float v = 0.32f + 0.12f * h2(stave, 2) + lit(x, y) * 0.4f + (543 - x) / 60.0f;
            if ((x - 530) % 4 == 0) {
                v = 0.12f;
            }
            c3 c = FLAT(WOOD, v);
            if (y == 240 || y == 241 || y == 255 || y == 256) {
                c = FLAT(STEEL, y & 1 ? 0.25f : 0.55f);
            }
            wset(x, y, c);
        }
    }
    for (int x = 531; x < 556; x++) {
        wset(x, 236, CR(WATER, 2));
        wset(x, 235, CR(WOOD, 6));
    }
    wset(536, 236, CR(NIGHT, 5));
    wset(537, 236, CR(NIGHT, 4));
    for (int k = 0; k < 22; k++) {
        wset(550 + (k >> 2), 236 - k, CR(WOOD, 5));
    }
    // a box of broken glass (cullet: it goes back into the pot)
    for (int y = 242; y < FLOOR_Y; y++) {
        for (int x = 598; x < 642; x++) {
            float v = 0.3f + 0.1f * n2(x / 3.0f, y / 8.0f) + lit(x, y) * 0.35f;
            if (y == 242 || y == 251) {
                v = 0.55f;
            }
            if (x == 598 || x == 641) {
                v = 0.12f;
            }
            wset(x, y, FLAT(WOOD, v));
        }
    }
    static const uint8_t cul[5][3] = {{52, 82, 196}, {60, 160, 90}, {190, 40, 66}, {214, 140, 52}, {200, 220, 214}};
    for (int k = 0; k < 46; k++) {
        int x = 600 + (int)(h2(k, 1) * 40), y = 237 + (int)(h2(k, 2) * 5);
        if (y > 241 - fabsf((float)(x - 620)) * 0.12f) {
            continue;
        }
        c3 c = C3(cul[k % 5]);
        wset(x, y, c);
        if (h2(k, 3) > 0.5f) {
            wset(x + 1, y, cmix(c, crgb(255, 255, 255), 0.5f));
        }
    }
}

// the morning: the window shows the dawn; the wall near it gets the day light (into s_dawn)
static void paint_dawn(void)
{
    // (paint into the world, copy the part to s_dawn, then put the night back from a copy)
    int dw = DAWN_X1 - DAWN_X0;
    uint16_t *keep = heap_caps_malloc((size_t)dw * DAWN_Y1 * 2, MALLOC_CAP_SPIRAM);
    if (!keep) {
        return;
    }
    for (int y = 0; y < DAWN_Y1; y++) {
        memcpy(keep + y * dw, s_world + (y + s_top) * WW + DAWN_X0, (size_t)dw * 2);
    }
    for (int y = 0; y < DAWN_Y1; y++) {
        for (int x = DAWN_X0; x < DAWN_X1; x++) {
            if (in_win(x, y, 7) || is_furnace(x, y)) {
                continue;
            }
            float d = len2((x - WCX) / 1.3f, (float)(y - 120));
            float k = sstep(150, 40, d) * 0.55f;
            if (k < 0.02f) {
                continue;
            }
            c3 c = wget(x, y);
            wset(x, y, cmix(c, crgb(c.r * 1.9f + 30, c.g * 1.8f + 26, c.b * 1.7f + 22), k));
        }
    }
    for (int y = WPEAK; y <= WSILL; y++) {
        for (int x = WCX - 50; x <= WCX + 50; x++) {
            if (!in_win(x, y, 0) || abs(x - WCX) <= 2) {
                continue;
            }
            if (x < WCX) {
                c3 c;
                if (y < 52) {
                    c = DITH(SKYD, (y - 26) / 26.0f * 0.8f + 0.1f, x, y);
                } else if (y < 118) {
                    c = FLAT(FACD, 0.45f + 0.3f * n2(x / 7.0f, y / 9.0f) + (x < 550 ? 0.15f : 0));
                    if (y < 56) {
                        c = CR(FACD, 1);
                    }
                } else {
                    c = FLAT(WATD,
                             0.3f + (y - 118) / 50.0f * 0.3f + (fsin(y * 1.7f + fsin(x * 0.4f)) > 0.7f ? 0.35f : 0));
                }
                wset(x, y, c);
            } else {
                paint_roundel_px(x, y, 0, true);
            }
        }
    }
    for (int y = 36; y < 54; y++) {
        int w = y < 42 ? 4 : (int)fmaxf(1, lroundf(4 - (y - 42) * 0.3f));
        for (int x = 550 - w; x <= 550 + w; x++) {
            if (in_canal(x, y)) {
                wset(x, y, CR(FACD, x < 550 ? 2 : 1));
            }
        }
    }
    static const int16_t lw[][2] = {{538, 66}, {560, 72}};
    for (int k = 0; k < 2; k++) {
        for (int y = lw[k][1]; y < lw[k][1] + 12; y++) {
            for (int x = lw[k][0]; x < lw[k][0] + 6; x++) {
                if (in_canal(x, y) && !(y - lw[k][1] < 2 && (x == lw[k][0] || x == lw[k][0] + 5))) {
                    wset(x, y, y < lw[k][1] + 3 ? CR(FACD, 0) : crgb(0x2e, 0x24, 0x27));
                }
            }
        }
    }
    for (int y = 94; y < 150; y++) {
        for (int x = 569; x < 572; x++) {
            if (!in_canal(x, y) || (y >= 118 && (h2(y, 3) > 0.55f || y > 140))) {
                continue;
            }
            c3 c = ((y - 94) / 5) & 1 ? crgb(0xb2, 0x30, 0x2a) : crgb(0xf4, 0xec, 0xe0);
            if (x == 571) {
                c = cmix(c, crgb(60, 40, 50), 0.4f);
            }
            if (y >= 118) {
                c = cmix(c, CR(WATD, 2), 0.5f);
            }
            wset(x, y, c);
        }
    }
    for (int x = WCX - 66; x < WCX + 67; x++) {
        wset(x, WSILL + 1, crgb(0xff, 0xf2, 0xd8));
        wset(x, WSILL + 2, cmix(wget(x, WSILL + 2), crgb(255, 230, 190), 0.5f));
    }
    for (int y = 0; y < DAWN_Y1; y++) {
        memcpy(s_dawn + y * dw, s_world + (y + s_top) * WW + DAWN_X0, (size_t)dw * 2);
        memcpy(s_world + (y + s_top) * WW + DAWN_X0, keep + y * dw, (size_t)dw * 2);
    }
    heap_caps_free(keep);
}

// the parts of the world that move: the fire in the glory hole and the water of the canal (each frame)
#define HD (2 * HOLE_R + 1)
static float *s_hole_rn, *s_hole_ang, *s_hole_base;  // for each pixel of the hole: the radius, the angle, the light
static int s_anim_n;
static void hole_tables(void)
{
    if (!s_hole_rn) {
        s_hole_rn = heap_caps_malloc(HD * HD * sizeof(float) * 3, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_hole_rn) {
        return;
    }
    s_hole_ang = s_hole_rn + HD * HD;
    s_hole_base = s_hole_ang + HD * HD;
    for (int j = 0; j < HD; j++) {
        for (int i = 0; i < HD; i++) {
            int x = HOLE_X - HOLE_R + i, y = HOLE_Y - HOLE_R + j, k = j * HD + i;
            float dx = (float)(x - HOLE_X), dy = (float)(y - HOLE_Y);
            float rn = sqrtf(dx * dx + dy * dy) / HOLE_R;
            s_hole_rn[k] = rn;
            s_hole_ang[k] = atan2f(dy, dx);
            float px = (x - 164) / 26.0f, py = (y - 176) / 14.0f, pot = px * px + py * py;
            float b = 1 - rn * rn * 0.5f;
            if (pot < 1) {
                b -= 0.2f;  // the pot inside
            }
            if (fabsf(pot - 1) < 0.12f && y < 176) {
                b += 0.12f;
            }
            s_hole_base[k] = b;
        }
    }
}
static void hole_free(void)
{
    heap_caps_free(s_hole_rn);
    s_hole_rn = NULL;
}
static float s_colsin[48];  // fsin(x * 0.4) of each column of the canal (the shape of the waves)
static void animate_world(float t)
{
    // (the fire and the water change on alternate frames: each one at half the frame rate, half the work)
    if ((s_anim_n++ & 1) == 0) {
        if (!s_hole_rn) {
            return;
        }
        // (half of the rows on each of these frames: each pixel of the fire changes on each 4th frame)
        for (int j = (s_anim_n >> 1) & 1; j < HD; j += 2) {
            for (int i = 0; i < HD; i++) {
                int k = j * HD + i;
                float rn = s_hole_rn[k];
                if (rn >= 1) {
                    continue;
                }
                int x = HOLE_X - HOLE_R + i, y = HOLE_Y - HOLE_R + j;
                float v = s_hole_base[k] + (n2(s_hole_ang[k] * 3 + 10 + t * 1.3f, rn * 5 - t * 2.6f) - 0.5f) * 0.4f;
                wset(x, y, DITH(HOT, 0.5f + v * 0.5f, x, y));
            }
        }
        return;
    }
    if (s_colsin[1] == 0) {
        for (int i = 0; i < 48; i++) {
            s_colsin[i] = fsin((WCX - 50 + i) * 0.4f);
        }
    }
    // the water of the canal (under the arch the left light is the full width: x = WCX - 50 .. WCX - 3)
    static const int16_t lw[2] = {538, 560};
    for (int y = 118; y <= WSILL; y++) {
        float depth = (y - 118) / 50.0f;
        uint16_t c0 = pk(FLAT(WATER, 0.5f - depth * 0.25f)), c1 = pk(FLAT(WATER, 0.75f - depth * 0.25f));
        uint16_t cl = pk(depth < 0.35f ? CR(LAMP, 2) : CR(LAMP, 1));
        uint16_t *row = s_world + (y + s_top) * WW;
        float ph = y * 1.7f + t * 1.8f;
        for (int i = 0; i < 48; i++) {
            row[WCX - 50 + i] = fsin(ph + s_colsin[i]) > 0.75f ? c1 : c0;
        }
        if (y >= 152) {
            continue;
        }
        int wob = (int)(fsin(y * 0.9f + t * 2.2f) * 1.5f + 2) - 2;
        for (int k = 0; k < 2; k++) {
            for (int x = lw[k] + 1 + wob; x < lw[k] + 5 + wob; x++) {
                if (fsin(y * 1.9f + t * 2.4f + x * 0.3f) > 0.45f + depth * 0.9f) {
                    row[x] = cl;
                }
            }
        }
    }
    // the gondola: it goes past the window from time to time
    float gp = fmodf(t, 30) / 30, gx = 486 + gp * 170;
    if (gx > 500 && gx < 610) {
#define GPUT(x, y, c)                                                                                                  \
    do {                                                                                                               \
        int x_ = (x), y_ = (y);                                                                                        \
        if (in_canal(x_, y_))                                                                                          \
            wset(x_, y_, (c));                                                                                         \
    } while (0)
        for (int i = -18; i <= 18; i++) {
            int h = 3 - (int)lroundf(abs(i) / 9.0f);
            float e = i / 18.0f;
            for (int k = 0; k < h; k++) {
                GPUT((int)lroundf(gx + i), 133 - k + (int)lroundf(e * e * e * e * -4), CR(FACADE, 0));
            }
        }
        for (int k = 0; k < 6; k++) {
            GPUT((int)lroundf(gx + 18), 126 + k, CR(FACADE, 1));
        }
        for (int k = 0; k < 4; k++) {
            GPUT((int)lroundf(gx + 19), 127 + k + (k & 1), CR(FACADE, 1));
        }
        for (int k = 0; k < 11; k++) {
            GPUT((int)lroundf(gx - 13), 122 + k, CR(FACADE, 0));
        }
        GPUT((int)lroundf(gx - 13), 121, CR(FACADE, 2));
        for (int k = 0; k <= 10; k++) {
            GPUT((int)lroundf(gx - 14 + k), 125 + (int)lroundf(k * 1.2f), CR(FACADE, 0));
        }
        GPUT((int)lroundf(gx + 13), 127, CR(LAMP, 4));
        GPUT((int)lroundf(gx + 13), 128, CR(LAMP, 3));
        for (int k = 0; k < 9; k += 2) {
            GPUT((int)lroundf(gx + 13 + fsin(t * 3 + k)), 136 + k, CR(LAMP, 1));
        }
#undef GPUT
    }
}
