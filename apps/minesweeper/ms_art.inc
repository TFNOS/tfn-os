// MINESWEEPER: the art (included by app_mines.c). All pictures come from code: art pixels of 2 x 2 screen px (3 x 3
// for the mole and the icons of the buttons). The sprites of a garden are made when the garden starts (the tiles, the
// earth, the numbers, the flags, the bombs, the surprises); the landscape is one picture of 640 x 172 in PSRAM.

#define CELL 24        // a cell on the screen
static uint16_t KEYC;  // the clear colour of a sprite (pure magenta: no art uses it)

// ================================================================ the canvas (art pixels)
#define CVMAX 40
static uint16_t s_cv[CVMAX * CVMAX], s_cv2[CVMAX * CVMAX];
static int s_cw, s_ch;
static void cv_new(int w, int h)
{
    s_cw = w;
    s_ch = h;
    for (int i = 0; i < w * h; i++) {
        s_cv[i] = KEYC;
    }
}
static inline void cv_px(int x, int y, uint16_t c)
{
    if (x >= 0 && y >= 0 && x < s_cw && y < s_ch) {
        s_cv[y * s_cw + x] = c;
    }
}
static inline uint16_t cv_get(int x, int y)
{
    return (x >= 0 && y >= 0 && x < s_cw && y < s_ch) ? s_cv[y * s_cw + x] : KEYC;
}
static void cv_rect(int x, int y, int w, int h, uint16_t c)
{
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            cv_px(x + i, y + j, c);
        }
    }
}
static void cv_ell(float cx, float cy, float rx, float ry, uint16_t c)
{
    for (int y = (int)floorf(cy - ry); y <= (int)ceilf(cy + ry); y++) {
        for (int x = (int)floorf(cx - rx); x <= (int)ceilf(cx + rx); x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) {
                cv_px(x, y, c);
            }
        }
    }
}
static void cv_line(int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        cv_px(x0, y0, c);
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
// a dark line round the shape (the clear pixels next to a pixel of the shape)
static void cv_outline(uint16_t c)
{
    memcpy(s_cv2, s_cv, sizeof(uint16_t) * s_cw * s_ch);
    for (int y = 0; y < s_ch; y++) {
        for (int x = 0; x < s_cw; x++) {
            if (s_cv2[y * s_cw + x] != KEYC) {
                continue;
            }
            bool n = (x > 0 && s_cv2[y * s_cw + x - 1] != KEYC) || (x < s_cw - 1 && s_cv2[y * s_cw + x + 1] != KEYC) ||
                     (y > 0 && s_cv2[(y - 1) * s_cw + x] != KEYC) ||
                     (y < s_ch - 1 && s_cv2[(y + 1) * s_cw + x] != KEYC);
            if (n) {
                s_cv[y * s_cw + x] = c;
            }
        }
    }
}

// ================================================================ the sprites
typedef struct {
    uint16_t *px;
    int16_t w, h;
    bool key;
} spr_t;
static uint8_t *s_pool;
static size_t s_pool_at;
#define POOL_N (180 * 1024)
static uint16_t *pool_take(size_t n)
{
    n = (n + 3) & ~(size_t)3;
    if (!s_pool || s_pool_at + n > POOL_N) {
        return NULL;
    }
    uint16_t *p = (uint16_t *)(s_pool + s_pool_at);
    s_pool_at += n;
    return p;
}
static void spr_make(spr_t *s, int scale)
{
    s->w = (int16_t)(s_cw * scale);
    s->h = (int16_t)(s_ch * scale);
    s->px = pool_take((size_t)s->w * s->h * 2);
    s->key = false;
    if (!s->px) {
        s->w = s->h = 0;
        return;
    }
    for (int y = 0; y < s->h; y++) {
        for (int x = 0; x < s->w; x++) {
            uint16_t c = s_cv[(y / scale) * s_cw + x / scale];
            s->px[y * s->w + x] = c;
            if (c == KEYC) {
                s->key = true;
            }
        }
    }
}
static void spr_draw(const spr_t *s, int x, int y)
{
    if (!s->px) {
        return;
    }
    int x0 = x < 0 ? -x : 0, x1 = x + s->w > SW ? SW - x : s->w;
    if (x1 <= x0) {
        return;
    }
    for (int j = 0; j < s->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) {
            continue;
        }
        const uint16_t *a = s->px + j * s->w;
        uint16_t *d = g_fb + yy * SW + x;
        if (!s->key) {
            memcpy(d + x0, a + x0, (size_t)(x1 - x0) * 2);
        } else {
            for (int i = x0; i < x1; i++) {
                if (a[i] != KEYC) {
                    d[i] = a[i];
                }
            }
        }
    }
}
// a sprite at 2 x its size (the loupe), or mixed with what is there (k of 32), or with a colour over it
static void spr_draw2(const spr_t *s, int x, int y)
{
    if (!s->px) {
        return;
    }
    for (int j = 0; j < s->h * 2; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) {
            continue;
        }
        const uint16_t *a = s->px + (j / 2) * s->w;
        uint16_t *d = g_fb + yy * SW;
        for (int i = 0; i < s->w * 2; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= SW) {
                continue;
            }
            uint16_t c = a[i / 2];
            if (c != KEYC) {
                d[xx] = c;
            }
        }
    }
}
static void spr_draw_k(const spr_t *s, int x, int y, int k, uint16_t tint, int tk)
{
    if (!s->px || k <= 0) {
        return;
    }
    for (int j = 0; j < s->h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) {
            continue;
        }
        const uint16_t *a = s->px + j * s->w;
        uint16_t *d = g_fb + yy * SW;
        for (int i = 0; i < s->w; i++) {
            int xx = x + i;
            if (xx < 0 || xx >= SW || a[i] == KEYC) {
                continue;
            }
            uint16_t c = tk ? mix565(a[i], tint, tk) : a[i];
            d[xx] = k >= 32 ? c : mix565(d[xx], c, k);
        }
    }
}
// a box of the screen mixed with a colour (a plate, a shadow)
static void shade_box(int x, int y, int w, int h, uint16_t c, int k)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) {
            continue;
        }
        uint16_t *d = g_fb + yy * SW;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx >= 0 && xx < SW) {
                d[xx] = mix565(d[xx], c, k);
            }
        }
    }
}
// a round plate (a mix with a colour), the corners cut by a circle of radius r
static void plate(int x, int y, int w, int h, int r, uint16_t c, int k)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) {
            continue;
        }
        int cut = 0;
        if (j < r) {
            float d = r - j - 0.5f;
            cut = (int)(r - sqrtf(fmaxf(0, r * r - d * d)) + 0.5f);
        } else if (j >= h - r) {
            float d = j - (h - r) + 0.5f;
            cut = (int)(r - sqrtf(fmaxf(0, r * r - d * d)) + 0.5f);
        }
        uint16_t *d = g_fb + yy * SW;
        for (int i = cut; i < w - cut; i++) {
            int xx = x + i;
            if (xx >= 0 && xx < SW) {
                d[xx] = mix565(d[xx], c, k);
            }
        }
    }
}

// the sprites of a garden
#define NCOV 4
static spr_t S_COV[NCOV], S_OPEN[3], S_NUM[9], S_FLAG[3], S_BOMB[2], S_CRATER, S_COIN[4], S_CLOVER, S_CHEST[2],
        S_MOLE[3], S_CAT[4], S_SPROUT, S_BLOOM[3], S_X;

static const char *const DIGITS[8][7] = {
        {"..#..", ".##..", "#.#..", "..#..", "..#..", "..#..", "#####"},
        {".###.", "#...#", "....#", "..##.", ".#...", "#....", "#####"},
        {"####.", "....#", "....#", ".###.", "....#", "....#", "####."},
        {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."},
        {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."},
        {".###.", "#....", "#....", "####.", "#...#", "#...#", ".###."},
        {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."},
        {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."},
};
static const uint8_t NUMC[2][8][3] = {
        {{30, 80, 210},
         {30, 130, 40},
         {210, 40, 40},
         {96, 36, 168},
         {156, 44, 30},
         {20, 130, 140},
         {40, 40, 40},
         {100, 100, 100}},
        {{120, 190, 255},
         {120, 240, 120},
         {255, 110, 110},
         {210, 150, 255},
         {255, 170, 90},
         {110, 240, 230},
         {240, 240, 240},
         {186, 186, 186}},
};

// the small things on a tile, by the garden
static void deco(const garden_t *G, int v, uint32_t h)
{
    uint16_t d0 = rgb3(G->deco0), d1 = rgb3(G->deco1), td = rgb3(G->topd), tl = rgb3(G->topl);
    int x = 2 + (int)(h % 7), y = 2 + (int)((h >> 4) % 5);
    switch (G->amb) {
    case AMB_PETAL:  // a flower (petals round a centre), or two small white ones
        if (v == 0 || v == 2) {
            cv_px(x, y - 1, d0);
            cv_px(x - 1, y, d0);
            cv_px(x + 1, y, d0);
            cv_px(x, y + 1, d0);
            cv_px(x, y, d1);
            cv_px(x, y + 2, td);
        } else if (v == 3) {
            cv_px(x, y, rgbc(250, 250, 250));
            cv_px(x + 3, y + 2, rgbc(250, 250, 250));
            cv_px(x + 3, y + 3, td);
            cv_px(x, y + 1, td);
        }
        break;
    case AMB_SUN:  // a small sunflower
        if (v != 1) {
            cv_px(x, y - 1, d0);
            cv_px(x - 1, y, d0);
            cv_px(x + 1, y, d0);
            cv_px(x, y + 1, d0);
            cv_px(x - 1, y - 1, d0);
            cv_px(x + 1, y + 1, d0);
            cv_px(x + 1, y - 1, d0);
            cv_px(x - 1, y + 1, d0);
            cv_px(x, y, d1);
            cv_px(x, y + 2, td);
            cv_px(x, y + 3, td);
        }
        break;
    case AMB_SEA:  // a shell or a starfish
        if (v == 0) {
            cv_px(x, y, d0);
            cv_px(x + 1, y, d0);
            cv_px(x + 2, y, d0);
            cv_px(x + 1, y - 1, d0);
            cv_px(x, y + 1, rgb3k(G->deco0, 80));
            cv_px(x + 2, y + 1, rgb3k(G->deco0, 80));
        } else if (v == 2) {
            cv_px(x, y, d1);
            cv_px(x - 1, y, d1);
            cv_px(x + 1, y, d1);
            cv_px(x, y - 1, d1);
            cv_px(x - 1, y + 1, d1);
            cv_px(x + 1, y + 1, d1);
        } else {
            cv_px(x, y, td);
            cv_px(x + 4, y + 3, td);
        }
        break;
    case AMB_LEAF:  // a fallen leaf on half of the tiles
        if (v == 0) {
            cv_px(x, y, d0);
            cv_px(x + 1, y, d0);
            cv_px(x, y + 1, d0);
            cv_px(x + 1, y + 1, rgb3k(G->deco0, 75));
        }
        if (v == 2) {
            cv_px(x, y + 2, d1);
            cv_px(x + 1, y + 2, d1);
            cv_px(x + 1, y + 3, rgb3k(G->deco1, 75));
        }
        break;
    case AMB_SNOW:  // the glints of the ice; a small snowflake
        cv_px(x, y, d0);
        cv_px(x + 5 > 10 ? x - 4 : x + 5, y + 3, d0);
        if (v == 2) {
            cv_px(x + 2, y + 1, d1);
            cv_px(x + 1, y + 2, d1);
            cv_px(x + 3, y + 2, d1);
            cv_px(x + 2, y + 3, d1);
            cv_px(x + 2, y + 2, rgbc(240, 250, 255));
        }
        break;
    case AMB_SAND:  // sandstone blocks: the lines of the stones
        for (int i = 1; i < 11; i++) {
            cv_px(i, 5, td);
        }
        cv_px(5, 1, td);
        cv_px(5, 2, td);
        cv_px(5, 3, td);
        cv_px(5, 4, td);
        cv_px(8, 6, td);
        cv_px(8, 7, td);
        cv_px(8, 8, td);
        cv_px(8, 9, td);
        cv_px(3, 6, td);
        cv_px(3, 7, td);
        cv_px(3, 8, td);
        cv_px(3, 9, td);
        if (v == 1) {
            cv_px(x, y, rgb3k(G->top, 75));
            cv_px(x + 1, y + 1, rgb3k(G->top, 75));
        }
        break;
    case AMB_FIREFLY:  // a glowing mushroom, or dew
        if (v == 1 || v == 3) {
            uint16_t cap = v == 1 ? d0 : d1;
            cv_px(x - 1, y, cap);
            cv_px(x, y, cap);
            cv_px(x + 1, y, cap);
            cv_px(x, y - 1, cap);
            cv_px(x, y + 1, rgbc(220, 230, 220));
            cv_px(x, y + 2, rgbc(200, 210, 200));
        } else {
            cv_px(x, y, tl);
            cv_px(x + 3, y + 2, tl);
        }
        break;
    case AMB_EMBER:  // glowing cracks
        if (v != 0) {
            int a = x, b = y;
            for (int k = 0; k < 5; k++) {
                cv_px(a, b, k & 1 ? d1 : d0);
                a += (int)((h >> (k + 8)) & 1) ? 1 : 0;
                b++;
                if (b > 9) {
                    break;
                }
            }
        }
        break;
    }
}
static void art_cov(const garden_t *G, int v)
{
    cv_new(12, 12);
    uint16_t top = rgb3(G->top), tl = rgb3(G->topl), td = rgb3(G->topd);
    cv_rect(0, 0, 12, 10, top);
    for (int i = 0; i < 12; i++) {
        cv_px(i, 0, tl);
    }
    for (int i = 0; i < 10; i++) {
        cv_px(0, i, tl);
        cv_px(11, i, td);
    }
    for (int i = 1; i < 11; i++) {
        cv_px(i, 9, td);
    }
    // the front face of the tile
    cv_rect(0, 10, 12, 2, rgb3(G->face));
    for (int i = 0; i < 12; i++) {
        cv_px(i, 11, rgb3k(G->face, 70));
    }
    // tufts of the grass (or the grain of the sand, the snow)
    uint32_t h = hash2(v * 7 + 3, G->amb * 13 + 1);
    for (int k = 0; k < (G->amb == AMB_LEAF ? 2 : 4); k++) {
        int x = 2 + (int)((h >> (k * 5)) % 8), y = 2 + (int)((h >> (k * 5 + 3)) % 6);
        if (G->amb == AMB_SAND || G->amb == AMB_SNOW || G->amb == AMB_SEA) {
            cv_px(x, y, k & 1 ? td : tl);
            continue;
        }
        cv_px(x, y, td);
        cv_px(x + 1, y - 1, td);
        cv_px(x + 2, y, td);
    }
    deco(G, v, hash2(v, 99 + G->amb));
}
static void art_open(const garden_t *G, int v)
{
    cv_new(12, 12);
    uint16_t dm = rgb3(G->dirt), dd = rgb3(G->dirtd), dl = rgb3(G->dirtl);
    cv_rect(0, 0, 12, 12, dm);
    // a sunken cell: dark at the top and the left, light at the bottom and the right
    for (int i = 0; i < 12; i++) {
        cv_px(i, 0, dd);
        cv_px(0, i, dd);
        cv_px(i, 11, dl);
        cv_px(11, i, dl);
    }
    cv_px(0, 11, dm);
    cv_px(11, 0, dm);
    uint32_t h = hash2(v * 31 + 5, 17);
    for (int k = 0; k < 3; k++) {
        int x = 2 + (int)((h >> (k * 6)) % 8), y = 2 + (int)((h >> (k * 6 + 3)) % 8);
        cv_px(x, y, k == 1 ? dl : dd);
        if (k == 0) {
            cv_px(x + 1, y, dl);
        }
    }
}
static void art_num(const garden_t *G, int n)
{
    cv_new(8, 9);
    const uint8_t *c = NUMC[G->bright][n - 1];
    uint16_t main = rgb3(c), low = rgb3k(c, 78), hi = rgbc(c[0] + 60, c[1] + 60, c[2] + 60);
    for (int y = 0; y < 7; y++) {
        for (int x = 0; x < 5; x++) {
            if (DIGITS[n - 1][y][x] == '#') {
                uint16_t k = y >= 5 ? low : main;
                cv_px(1 + x, 1 + y, k);
                cv_px(2 + x, 1 + y, k);
            }
        }
    }
    // (a light pixel at the top of each stroke)
    for (int x = 1; x < 7; x++) {
        for (int y = 1; y < 8; y++) {
            if (cv_get(x, y) != KEYC) {
                cv_px(x, y, hi);
                break;
            }
        }
    }
    cv_outline(G->bright ? rgbc(10, 8, 16) : rgb3k(G->dirt, 45));
}
static void art_flag(int f)
{
    cv_new(12, 12);
    uint16_t pole = rgbc(90, 60, 40), red = rgbc(226, 44, 44), redl = rgbc(255, 110, 100), redd = rgbc(160, 24, 30);
    cv_line(4, 1, 4, 9, pole);
    // the cloth: 6 columns that wave (the tip goes up and down)
    for (int i = 0; i < 6; i++) {
        int dy = (int)roundf(sinf(i * 0.9f + f * 2.1f) * (i * 0.25f));
        int hh = 5 - i * 4 / 6;
        for (int j = 0; j < hh; j++) {
            cv_px(5 + i, 1 + j + dy + (5 - hh) / 2, j == 0 ? redl : j == hh - 1 ? redd : red);
        }
    }
    cv_px(3, 10, rgbc(110, 80, 50));
    cv_px(4, 10, rgbc(110, 80, 50));
    cv_px(5, 10, rgbc(110, 80, 50));
    cv_outline(rgbc(40, 20, 20));
}
static void art_bomb(int f)
{
    cv_new(12, 12);
    cv_ell(5.5f, 7.0f, 3.8f, 3.8f, rgbc(36, 36, 48));
    cv_px(4, 5, rgbc(200, 200, 220));
    cv_px(3, 6, rgbc(120, 120, 140));
    cv_px(4, 6, rgbc(120, 120, 140));
    cv_rect(7, 3, 2, 2, rgbc(130, 130, 140));
    cv_line(8, 2, 10, 1, rgbc(150, 110, 70));
    if (f == 0) {
        cv_px(10, 0, rgbc(255, 250, 200));
        cv_px(11, 1, rgbc(255, 200, 60));
        cv_px(9, 0, rgbc(255, 160, 40));
        cv_px(11, 0, rgbc(255, 120, 30));
    } else {
        cv_px(10, 0, rgbc(255, 210, 80));
        cv_px(11, 0, rgbc(255, 250, 210));
    }
    cv_outline(rgbc(14, 12, 20));
}
static void art_crater(const garden_t *G)
{
    cv_new(12, 12);
    cv_ell(6, 6.5f, 5.2f, 4.2f, rgb3k(G->dirtd, 60));
    cv_ell(6, 6.5f, 3.6f, 2.8f, rgbc(30, 24, 22));
    cv_px(4, 5, rgbc(80, 70, 60));
    cv_px(8, 8, rgbc(60, 50, 44));
}
static void art_coin(int f)
{
    static const float W[4] = {3.4f, 2.4f, 1.0f, 2.4f};
    cv_new(12, 12);
    cv_ell(6, 6, W[f], 3.6f, rgbc(255, 196, 40));
    if (f != 2) {
        cv_ell(6, 6, W[f] * 0.55f, 2.2f, rgbc(236, 160, 30));
        cv_px(6 - (int)(W[f] * 0.5f), 4, rgbc(255, 250, 200));
    }
    cv_outline(rgbc(140, 90, 20));
}
static void art_clover(void)
{
    cv_new(12, 12);
    uint16_t g = rgbc(60, 190, 70), gl = rgbc(120, 230, 110);
    cv_ell(4.5f, 4.5f, 2.0f, 2.0f, g);
    cv_ell(7.5f, 4.5f, 2.0f, 2.0f, g);
    cv_ell(4.5f, 7.5f, 2.0f, 2.0f, g);
    cv_ell(7.5f, 7.5f, 2.0f, 2.0f, g);
    cv_px(4, 4, gl);
    cv_px(7, 4, gl);
    cv_px(4, 7, gl);
    cv_line(7, 8, 9, 11, rgbc(40, 130, 50));
    cv_outline(rgbc(20, 60, 24));
}
static void art_chest(int open)
{
    cv_new(12, 12);
    uint16_t w = rgbc(150, 92, 46), wd = rgbc(104, 60, 30), gold = rgbc(255, 204, 60);
    cv_rect(1, 6, 10, 5, w);
    for (int i = 1; i < 11; i++) {
        cv_px(i, 10, wd);
    }
    cv_rect(1, 8, 10, 1, gold);
    if (!open) {
        cv_rect(1, 3, 10, 3, w);
        cv_rect(1, 3, 10, 1, rgbc(186, 120, 64));
        cv_rect(5, 5, 2, 2, gold);
    } else {
        cv_rect(1, 1, 10, 2, wd);
        cv_rect(2, 4, 8, 2, gold);
        cv_px(3, 4, rgbc(255, 250, 220));
        cv_px(7, 4, rgbc(255, 250, 220));
    }
    cv_outline(rgbc(50, 28, 14));
}
// the mole of a hint: f = 0 the top of the head, 1 the head, 2 the head and the paws
static void art_mole(int f)
{
    cv_new(12, 12);
    int top = 8 - f * 3;
    cv_ell(6, 10.5f, 4.5f, 1.4f, rgbc(40, 26, 18));
    uint16_t fur = rgbc(124, 88, 62);
    cv_ell(6, top + 3.5f, 3.6f, 3.5f, fur);
    cv_rect(2, top + 3, 9, 11 - top - 3 < 0 ? 0 : 11 - (top + 3), fur);
    cv_ell(6, top + 1.5f, 3.8f, 1.8f, rgbc(250, 200, 40));
    cv_px(6, top, rgbc(255, 252, 220));
    if (top + 3 < 11) {
        cv_px(4, top + 3, rgbc(20, 14, 10));
        cv_px(8, top + 3, rgbc(20, 14, 10));
    }
    if (top + 5 < 11) {
        cv_px(5, top + 5, rgbc(255, 160, 170));
        cv_px(6, top + 5, rgbc(255, 160, 170));
        cv_px(7, top + 5, rgbc(255, 160, 170));
    }
    if (f == 2) {
        cv_px(1, 6, rgbc(255, 170, 180));
        cv_px(10, 6, rgbc(255, 170, 180));
        cv_px(10, 5, rgbc(255, 170, 180));
    }
    for (int x = 0; x < 12; x++) {
        for (int y = 11; y < 12; y++) {
            cv_px(x, y, KEYC);
        }
    }
    cv_outline(rgbc(36, 22, 14));
}
// the cat: 0, 1 the walk, 2 sits, 3 sleeps
static void art_cat(int f)
{
    cv_new(14, 12);
    uint16_t o = rgbc(240, 150, 60), od = rgbc(196, 104, 40), w = rgbc(255, 236, 210), k = rgbc(30, 20, 14);
    if (f <= 1) {
        cv_ell(6, 7, 4.5f, 2.6f, o);
        for (int i = 0; i < 4; i++) {
            cv_px(2 + i * 2, 7, od);
        }
        cv_ell(11, 5, 2.4f, 2.2f, o);
        cv_px(9, 2, o);
        cv_px(10, 3, o);
        cv_px(12, 2, o);
        cv_px(12, 3, o);
        cv_px(10, 5, k);
        cv_px(12, 5, k);
        cv_px(11, 6, rgbc(255, 140, 150));
        int a = f ? 1 : 0;
        cv_px(3 - a, 9, od);
        cv_px(3 - a, 10, od);
        cv_px(5 + a, 9, od);
        cv_px(5 + a, 10, od);
        cv_px(8 - a, 9, o);
        cv_px(8 - a, 10, o);
        cv_px(10 + a, 9, o);
        cv_px(10 + a, 10, o);
        cv_line(1, 6, 0, 3, od);
        cv_px(1, 2, od);
        cv_px(8, 8, w);
        cv_px(9, 7, w);
    } else if (f == 2) {
        cv_ell(6, 8, 3.2f, 3.0f, o);
        cv_ell(6, 4, 2.6f, 2.3f, o);
        cv_px(4, 1, o);
        cv_px(4, 2, o);
        cv_px(8, 1, o);
        cv_px(8, 2, o);
        cv_px(5, 4, k);
        cv_px(7, 4, k);
        cv_px(6, 5, rgbc(255, 140, 150));
        cv_ell(6, 8.5f, 1.4f, 1.8f, w);
        cv_line(9, 10, 12, 9, od);
        cv_px(12, 8, od);
        cv_px(4, 10, od);
        cv_px(8, 10, od);
    } else {
        cv_ell(7, 8, 5.2f, 2.8f, o);
        cv_ell(3.5f, 7.5f, 2.4f, 2.0f, o);
        cv_px(2, 5, o);
        cv_px(4, 5, o);
        cv_px(2, 7, k);
        cv_px(3, 7, k);
        cv_line(4, 10, 11, 10, od);
        cv_px(12, 9, od);
        for (int i = 6; i < 11; i += 2) {
            cv_px(i, 6, od);
        }
    }
    cv_outline(rgbc(60, 30, 16));
}
static void art_sprout(void)
{
    cv_new(12, 12);
    uint16_t g = rgbc(80, 210, 90), gd = rgbc(40, 140, 50);
    cv_line(6, 10, 6, 5, gd);
    cv_ell(4.0f, 5.0f, 2.0f, 1.2f, g);
    cv_ell(8.0f, 4.0f, 2.0f, 1.2f, g);
    cv_px(3, 4, rgbc(170, 250, 160));
    cv_px(8, 3, rgbc(170, 250, 160));
    cv_outline(rgbc(20, 70, 26));
    cv_px(10, 1, rgbc(255, 255, 220));
    cv_px(1, 2, rgbc(255, 255, 220));
}
static void art_bloom(const garden_t *G, int f)
{
    cv_new(12, 12);
    uint16_t st = rgbc(60, 150, 60), p = rgb3(G->deco0), c = rgb3(G->deco1);
    if (G->amb == AMB_SNOW || G->amb == AMB_SAND || G->amb == AMB_EMBER || G->amb == AMB_SEA) {
        p = rgbc(255, 120, 170);
        c = rgbc(255, 230, 90);
    }
    cv_line(6, 11, 6, 6, st);
    cv_px(5, 9, st);
    cv_px(7, 8, st);
    if (f == 0) {
        cv_ell(6, 5, 1.4f, 1.6f, rgbc(120, 200, 90));
    } else {
        float r = f == 1 ? 1.6f : 2.4f;
        for (int i = 0; i < 5; i++) {
            float a = i * 1.2566f - 1.57f;
            cv_ell(6 + cosf(a) * r, 5 + sinf(a) * r, 1.4f, 1.4f, p);
        }
        cv_ell(6, 5, 1.1f, 1.1f, c);
    }
    cv_outline(rgbc(30, 40, 24));
}
static void art_x(void)
{
    cv_new(12, 12);
    uint16_t r = rgbc(240, 40, 40);
    for (int i = 2; i < 10; i++) {
        cv_px(i, i, r);
        cv_px(i + 1, i, r);
        cv_px(11 - i, i, r);
        cv_px(10 - i, i, r);
    }
    cv_outline(rgbc(60, 10, 10));
}
static void sprites_make(const garden_t *G)
{
    s_pool_at = 0;
    for (int v = 0; v < NCOV; v++) {
        art_cov(G, v);
        spr_make(&S_COV[v], 2);
    }
    for (int v = 0; v < 3; v++) {
        art_open(G, v);
        spr_make(&S_OPEN[v], 2);
    }
    for (int n = 1; n <= 8; n++) {
        art_num(G, n);
        spr_make(&S_NUM[n], 2);
    }
    for (int f = 0; f < 3; f++) {
        art_flag(f);
        spr_make(&S_FLAG[f], 2);
    }
    for (int f = 0; f < 2; f++) {
        art_bomb(f);
        spr_make(&S_BOMB[f], 2);
    }
    art_crater(G);
    spr_make(&S_CRATER, 2);
    for (int f = 0; f < 4; f++) {
        art_coin(f);
        spr_make(&S_COIN[f], 2);
    }
    art_clover();
    spr_make(&S_CLOVER, 2);
    for (int f = 0; f < 2; f++) {
        art_chest(f);
        spr_make(&S_CHEST[f], 2);
    }
    for (int f = 0; f < 3; f++) {
        art_mole(f);
        spr_make(&S_MOLE[f], 2);
    }
    for (int f = 0; f < 4; f++) {
        art_cat(f);
        spr_make(&S_CAT[f], 2);
    }
    art_sprout();
    spr_make(&S_SPROUT, 2);
    for (int f = 0; f < 3; f++) {
        art_bloom(G, f);
        spr_make(&S_BLOOM[f], 2);
    }
    art_x();
    spr_make(&S_X, 2);
}

// ================================================================ the mole of the HUD (DIGBY), 3 x 3 art pixels
enum {
    MOOD_IDLE,
    MOOD_HAPPY,
    MOOD_CHEER,
    MOOD_SOOT,
    MOOD_OOPS,
    MOOD_SALUTE,
    MOOD_LOOK
};
static int s_mk = 3;  // the screen px of an art pixel of DIGBY (2 in the narrow HUD)
static void mole_draw(int x, int y, int mood, float t)
{
    cv_new(22, 22);
    bool soot = mood == MOOD_SOOT;
    uint16_t fur = soot ? rgbc(60, 54, 56) : rgbc(124, 88, 62), belly = soot ? rgbc(90, 84, 84) : rgbc(196, 156, 122);
    uint16_t pink = soot ? rgbc(120, 100, 100) : rgbc(255, 160, 172), k = rgbc(20, 14, 10), wht = rgbc(255, 255, 255);
    int bob = (mood == MOOD_CHEER) ? (int)(fabsf(sinf(t * 9)) * 2) : (int)(sinf(t * 2.2f) > 0.6f);
    int oy = -bob;
    // the feet and the body
    cv_ell(7.5f, 20.5f + oy, 2.2f, 1.2f, rgbc(90, 60, 42));
    cv_ell(14.5f, 20.5f + oy, 2.2f, 1.2f, rgbc(90, 60, 42));
    cv_ell(11, 15.5f + oy, 7.2f, 5.6f, fur);
    cv_ell(11, 16.5f + oy, 4.2f, 3.8f, belly);
    // the head
    cv_ell(11, 8.5f + oy, 6.4f, 5.6f, fur);
    // the helmet with its lamp (it shines)
    for (int yy = 0; yy < 22; yy++) {
        for (int xx = 0; xx < 22; xx++) {
            float dx = (xx + 0.5f - 11) / 6.9f, dy = (yy + 0.5f - (6.2f + oy)) / 3.9f;
            if (dx * dx + dy * dy <= 1 && yy <= 6 + oy) {
                cv_px(xx, yy, soot ? rgbc(110, 100, 70) : rgbc(250, 200, 40));
            }
        }
    }
    for (int xx = 4; xx < 18; xx++) {
        cv_px(xx, 7 + oy, soot ? rgbc(80, 70, 50) : rgbc(206, 150, 28));
    }
    cv_ell(11, 3.2f + oy, 1.7f, 1.5f, soot ? rgbc(140, 140, 120) : rgbc(255, 252, 214));
    if (!soot) {
        cv_px(11, 3 + oy, wht);
    }
    // the eyes
    bool blink = mood == MOOD_IDLE && fmodf(t, 3.7f) < 0.12f;
    int ex0 = 8, ex1 = 14, ey = 10 + oy;
    if (mood == MOOD_LOOK) {
        ex0++;
        ex1++;
    }
    if (soot) {
        cv_px(ex0 - 1, ey - 1, wht);
        cv_px(ex0 + 1, ey + 1, wht);
        cv_px(ex0 + 1, ey - 1, wht);
        cv_px(ex0 - 1, ey + 1, wht);
        cv_px(ex0, ey, wht);
        cv_px(ex1 - 1, ey - 1, wht);
        cv_px(ex1 + 1, ey + 1, wht);
        cv_px(ex1 + 1, ey - 1, wht);
        cv_px(ex1 - 1, ey + 1, wht);
        cv_px(ex1, ey, wht);
    } else if (blink || mood == MOOD_HAPPY || mood == MOOD_CHEER) {
        // (closed or happy: a small arc)
        cv_px(ex0 - 1, ey, k);
        cv_px(ex0, ey - 1, k);
        cv_px(ex0 + 1, ey, k);
        cv_px(ex1 - 1, ey, k);
        cv_px(ex1, ey - 1, k);
        cv_px(ex1 + 1, ey, k);
    } else {
        cv_px(ex0, ey - 1, k);
        cv_px(ex0, ey, k);
        cv_px(ex1, ey - 1, k);
        cv_px(ex1, ey, k);
        cv_px(ex0, ey - 1, wht);
        cv_px(ex1, ey - 1, wht);
    }
    // the snout and the mouth
    cv_ell(11, 12.4f + oy, 2.4f, 1.4f, pink);
    cv_px(11, 12 + oy, k);
    if (mood == MOOD_CHEER || mood == MOOD_SOOT) {
        cv_rect(10, 14 + oy, 3, 2, rgbc(120, 30, 40));
        cv_px(11, 15 + oy, rgbc(255, 120, 130));
    } else if (mood == MOOD_OOPS) {
        cv_px(10, 14 + oy, k);
        cv_px(11, 14 + oy, k);
        cv_px(12, 14 + oy, k);
        cv_px(17, 7 + oy, rgbc(140, 200, 255));
        cv_px(17, 8 + oy, rgbc(140, 200, 255));
    } else {
        cv_px(10, 14 + oy, k);
        cv_px(11, 15 + oy, k);
        cv_px(12, 14 + oy, k);
    }
    // the paws: up for a cheer, one at the helmet for a salute
    if (mood == MOOD_CHEER) {
        cv_ell(3, 9 + oy, 1.6f, 1.6f, pink);
        cv_ell(19, 9 + oy, 1.6f, 1.6f, pink);
        cv_line(4, 11 + oy, 5, 13 + oy, fur);
        cv_line(18, 11 + oy, 17, 13 + oy, fur);
    } else if (mood == MOOD_SALUTE) {
        cv_ell(4, 15 + oy, 1.6f, 1.4f, pink);
        cv_ell(17, 6 + oy, 1.6f, 1.4f, pink);
    } else {
        cv_ell(4, 15 + oy, 1.6f, 1.4f, pink);
        cv_ell(18, 15 + oy, 1.6f, 1.4f, pink);
    }
    cv_outline(rgbc(40, 24, 16));
    // (to the screen at 3 x 3, or s_mk x s_mk)
    for (int j = 0; j < 22 * s_mk; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) {
            continue;
        }
        uint16_t *d = g_fb + yy * SW;
        for (int i = 0; i < 22 * s_mk; i++) {
            uint16_t c = s_cv[(j / s_mk) * 22 + i / s_mk];
            int xx = x + i;
            if (c != KEYC && xx >= 0 && xx < SW) {
                d[xx] = c;
            }
        }
    }
    if (soot) {
        for (int i = 0; i < 3; i++) {
            float a = t * 3 + i * 2.1f;
            ui_circle(x + 10 * s_mk + (int)(cosf(a) * 10), y + 2 - (int)(fmodf(t * 20 + i * 7, 20)), 3,
                      rgbc(120, 120, 120), 140);
        }
    }
}
// the icons of the buttons (3 x 3 art pixels): a shovel, a flag, the clear of the neighbours
static void icon_draw(int which, int x, int y, bool dim)
{
    cv_new(14, 14);
    uint16_t wood = rgbc(170, 110, 60), metal = rgbc(200, 210, 224), metald = rgbc(130, 140, 160);
    if (which == 0 || which == 2) {
        cv_line(2, 2, 7, 7, wood);
        cv_line(3, 2, 8, 7, wood);
        cv_rect(1, 1, 3, 2, rgbc(120, 80, 40));
        cv_ell(9.5f, 9.5f, 3.4f, 3.4f, metal);
        cv_px(11, 11, metald);
        cv_px(12, 10, metald);
        cv_px(10, 12, metald);
        cv_px(8, 8, rgbc(240, 244, 255));
        if (which == 2) {
            cv_px(1, 10, rgbc(255, 240, 120));
            cv_px(2, 12, rgbc(255, 240, 120));
            cv_px(4, 11, rgbc(255, 255, 200));
            cv_px(12, 3, rgbc(255, 240, 120));
        }
    } else {
        cv_line(4, 1, 4, 12, rgbc(110, 74, 44));
        cv_line(5, 1, 5, 12, rgbc(90, 60, 36));
        for (int i = 0; i < 7; i++) {
            for (int j = 0; j < 6 - i * 5 / 7; j++) {
                cv_px(6 + i, 2 + j + i / 3, j == 0 ? rgbc(255, 110, 100) : rgbc(226, 44, 44));
            }
        }
        cv_rect(2, 12, 6, 1, rgbc(110, 80, 50));
    }
    cv_outline(rgbc(30, 20, 14));
    for (int j = 0; j < 42; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) {
            continue;
        }
        uint16_t *d = g_fb + yy * SW;
        for (int i = 0; i < 42; i++) {
            uint16_t c = s_cv[(j / 3) * 14 + i / 3];
            int xx = x + i;
            if (c == KEYC || xx < 0 || xx >= SW) {
                continue;
            }
            d[xx] = dim ? mix565(c, rgbc(60, 60, 70), 20) : c;
        }
    }
}

// ================================================================ the landscape (320 x 86 art pixels into s_bg)
static uint16_t *s_bg;
static int s_bg_g = -1;
static inline void bgp(int x, int y, uint16_t c)
{
    if (x < 0 || y < 0 || x >= SW / 2 || y >= SH / 2) {
        return;
    }
    uint16_t *d = s_bg + (y * 2) * SW + x * 2;
    d[0] = d[1] = d[SW] = d[SW + 1] = c;
}
static inline uint16_t bgg(int x, int y)
{
    return (x < 0 || y < 0 || x >= SW / 2 || y >= SH / 2) ? 0 : s_bg[(y * 2) * SW + x * 2];
}
static void bg_disc(float cx, float cy, float r, uint16_t c)
{
    for (int y = (int)(cy - r); y <= (int)(cy + r); y++) {
        for (int x = (int)(cx - r); x <= (int)(cx + r); x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            if (dx * dx + dy * dy <= r * r) {
                bgp(x, y, c);
            }
        }
    }
}
static void bg_cloud(float x, float y, float w, uint16_t c, uint16_t cs)
{
    for (int k = 0; k < 5; k++) {
        float px = x + (k - 2) * w * 0.2f, r = w * (0.14f + 0.08f * (k == 2) + 0.04f * (k == 1 || k == 3));
        bg_disc(px, y - r * 0.5f, r, c);
    }
    for (int i = (int)(x - w * 0.5f); i < (int)(x + w * 0.5f); i++) {
        bgp(i, (int)y, cs);
        bgp(i, (int)y + 1, cs);
    }
}
static void bg_tree(int x, int base, int s, uint16_t leaf, uint16_t leafd, uint16_t trunk)
{
    for (int y = base - s; y <= base; y++) {
        bgp(x, y, trunk);
        bgp(x + 1, y, trunk);
    }
    bg_disc(x + 0.5f, base - s - s * 0.5f, s * 0.75f, leafd);
    bg_disc(x - s * 0.35f, base - s - s * 0.3f, s * 0.5f, leafd);
    bg_disc(x + s * 0.5f, base - s - s * 0.35f, s * 0.5f, leafd);
    bg_disc(x, base - s - s * 0.65f, s * 0.6f, leaf);
    bgp(x - 1, base - s - s, add565(leaf, 40, 40, 30));
}
static void bg_pine(int x, int base, int h, uint16_t c, uint16_t snow)
{
    for (int y = 0; y < h; y++) {
        int w = (y * 5 / h) + (y % 4 == 3);
        for (int i = -w; i <= w; i++) {
            bgp(x + i, base - h + y, (snow && (y % 4 == 0 || i == -w)) ? snow : c);
        }
    }
    bgp(x, base + 1, rgbc(80, 50, 30));
    bgp(x, base + 2, rgbc(80, 50, 30));
}
static void bg_house(int x, int base, int w, int h, uint16_t wall, uint16_t roof, bool lit)
{
    for (int y = base - h; y <= base; y++) {
        for (int i = 0; i < w; i++) {
            bgp(x + i, y, wall);
        }
    }
    for (int k = 0; k <= w / 2 + 1; k++) {
        for (int i = -1 + k; i <= w - k; i++) {
            bgp(x + i, base - h - k, roof);
        }
    }
    uint16_t win = lit ? rgbc(255, 220, 120) : rgbc(60, 80, 110);
    bgp(x + 2, base - h + 3, win);
    bgp(x + 3, base - h + 3, win);
    bgp(x + w - 4, base - h + 3, win);
    bgp(x + w - 3, base - h + 3, win);
    for (int y = base - 3; y <= base; y++) {
        bgp(x + w / 2, y, rgbc(90, 60, 40));
        bgp(x + w / 2 - 1, y, rgbc(90, 60, 40));
    }
}
static void bg_sunflower(int x, int base, int h)
{
    for (int y = base - h; y <= base; y++) {
        bgp(x, y, rgbc(70, 130, 40));
    }
    bgp(x + 1, base - h / 2, rgbc(90, 160, 50));
    bgp(x - 1, base - h / 2 + 2, rgbc(90, 160, 50));
    bg_disc(x + 0.5f, base - h, 2.2f, rgbc(255, 206, 40));
    bgp(x, base - h, rgbc(110, 70, 30));
}
static void bg_palm(int x, int base, int h)
{
    for (int y = 0; y < h; y++) {
        int dx = (int)(sinf(y * 0.08f) * 3);
        bgp(x + dx, base - y, rgbc(150, 100, 60));
        bgp(x + dx + 1, base - y, rgbc(120, 80, 50));
    }
    int tx = x + (int)(sinf(h * 0.08f) * 3), ty = base - h;
    for (int k = 0; k < 6; k++) {
        float a = k * 1.05f + 0.3f;
        for (int i = 0; i < 9; i++) {
            bgp(tx + (int)(cosf(a) * i), ty + (int)(sinf(a) * i * 0.5f + i * i * 0.05f),
                k & 1 ? rgbc(40, 140, 60) : rgbc(60, 170, 70));
        }
    }
}
static void bg_cactus(int x, int base, int h)
{
    uint16_t g = rgbc(80, 150, 80), gd = rgbc(50, 110, 60);
    for (int y = base - h; y <= base; y++) {
        bgp(x, y, g);
        bgp(x + 1, y, gd);
    }
    for (int y = base - h * 2 / 3; y <= base - h / 3; y++) {
        bgp(x - 3, y, g);
    }
    for (int i = -3; i <= 0; i++) {
        bgp(x + i, base - h / 3, g);
    }
    for (int y = base - h * 3 / 4; y <= base - h / 2; y++) {
        bgp(x + 4, y, gd);
    }
    for (int i = 1; i <= 4; i++) {
        bgp(x + i, base - h / 2, gd);
    }
}
static void bg_paint(int g)
{
    if (s_bg_g == g) {
        return;
    }
    const garden_t *G = &GARD[g];
    uint32_t keep = s_rng;
    s_rng = 0xB0A7 + g * 131;
    const int W = SW / 2, H = SH / 2, HZ = H * 58 / 86;
#define BX(v) ((v) * W / 320)  // (an x of the 3.49 landscape on this width)
    // the sky: bands with a dither between them
    for (int y = 0; y < H; y++) {
        float t = clampf((float)y / HZ, 0, 1) * 9;
        int b = (int)t;
        float f = t - b;
        uint16_t c0 = rgbc((int)lerpf(G->sky0[0], G->sky1[0], b / 9.0f), (int)lerpf(G->sky0[1], G->sky1[1], b / 9.0f),
                           (int)lerpf(G->sky0[2], G->sky1[2], b / 9.0f));
        uint16_t c1 = rgbc((int)lerpf(G->sky0[0], G->sky1[0], (b + 1) / 9.0f),
                           (int)lerpf(G->sky0[1], G->sky1[1], (b + 1) / 9.0f),
                           (int)lerpf(G->sky0[2], G->sky1[2], (b + 1) / 9.0f));
        for (int x = 0; x < W; x++) {
            bgp(x, y, (f > 0.66f && ((x + y) & 1)) || (f > 0.85f) ? c1 : c0);
        }
    }
    bool night = G->amb == AMB_FIREFLY || G->amb == AMB_EMBER;
    // the stars, the moon or the sun
    if (night) {
        for (int i = 0; i < 160; i++) {
            int x = rndi(0, W - 1), y = rndi(0, HZ - 8);
            bgp(x, y, rndf() < 0.2f ? rgbc(255, 255, 230) : rgbc(150, 150, 190));
        }
        if (G->amb == AMB_FIREFLY) {
            bg_disc(BX(250), 14, 11, rgbc(40, 44, 96));
            bg_disc(BX(250), 14, 8, rgbc(240, 240, 214));
            bg_disc(BX(250) - 4, 11, 1.6f, rgbc(214, 214, 190));
            bg_disc(BX(250) + 3, 17, 1.2f, rgbc(214, 214, 190));
            bg_disc(BX(250) + 4, 10, 0.9f, rgbc(214, 214, 190));
        }
    } else {
        float sx = BX(40 + g * 33 % 240), sy = 14 + g * 5 % 12;
        bg_disc(sx, sy, 10, add565(rgb3(G->sky1), 20, 20, 0));
        bg_disc(sx, sy, 7, rgbc(255, 244, 196));
        bg_disc(sx, sy, 5.5f, rgbc(255, 252, 230));
        for (int i = 0; i < 4; i++) {
            bg_cloud(rndi(0, W), rndi(8, 30), rndi(28, 50), rgbc(255, 255, 255), rgbc(216, 230, 246));
        }
    }
    // the far hills (or the sea, or the dunes, or the mountain of fire)
    for (int x = 0; x < W; x++) {
        int h = HZ - 6 + (int)(sinf(x * 0.031f + g) * 5 + sinf(x * 0.077f + g * 2) * 3);
        if (G->amb == AMB_SEA) {
            h = HZ - 2;
        }
        for (int y = h; y < H; y++) {
            bgp(x, y, y == h ? add565(rgb3(G->hill0), 24, 24, 24) : rgb3(G->hill0));
        }
    }
    if (G->amb == AMB_SEA) {
        for (int i = 0; i < 90; i++) {
            int x = rndi(0, W - 4), y = rndi(HZ - 1, HZ + 8);
            bgp(x, y, rgbc(200, 240, 255));
            bgp(x + 1, y, rgbc(200, 240, 255));
        }
    }
    if (G->amb == AMB_EMBER) {
        // the mountain with fire at the top and a river of fire
        const int mx = BX(225);
        for (int x = mx - 75; x < mx + 75; x++) {
            int h = 14 + abs(x - mx) * 3 / 4;
            for (int y = h; y < H; y++) {
                bgp(x, y, y < h + 3 && abs(x - mx) < 14 ? rgbc(255, 120, 30) : rgbc(56, 28, 30));
            }
        }
        for (int y = 16; y < HZ + 4; y++) {
            int x = mx + (int)(sinf(y * 0.3f) * 3) + (y - 16) / 3;
            bgp(x, y, rgbc(255, 150, 40));
            bgp(x + 1, y, rgbc(255, 90, 20));
        }
        for (int i = 0; i < 6; i++) {
            bg_disc(mx - 3 + i * 4, 10 - i * 4, 4 + i, rgbc(90, 70, 70));
        }
    }
    if (G->amb == AMB_SAND) {
        for (int k = 0; k < 18; k++) {
            for (int i = -k; i <= k; i++) {
                bgp(BX(230) + i, HZ - 22 + k, i < 0 ? rgbc(230, 190, 120) : rgbc(196, 150, 90));
            }
        }
    }
    // the near ground with its things
    for (int x = 0; x < W; x++) {
        int h = HZ + 6 + (int)(sinf(x * 0.05f + g * 3) * 3 + sinf(x * 0.13f) * 1.5f);
        for (int y = h; y < H; y++) {
            bgp(x, y,
                y == h ? add565(rgb3(G->hill1), 30, 30, 20)
                       : ((x * 7 + y * 13) % 23 == 0 ? rgb3k(G->hill1, 85) : rgb3(G->hill1)));
        }
    }
    for (int i = 0; i < 12; i++) {
        int x = rndi(4, W - 8), base = HZ + 6 + (int)(sinf(x * 0.05f + g * 3) * 3 + sinf(x * 0.13f) * 1.5f);
        switch (G->amb) {
        case AMB_PETAL:
            if (i < 5) {
                bg_tree(x, base, rndi(4, 7), rgbc(110, 200, 90), rgbc(70, 160, 60), rgbc(110, 74, 44));
            } else {
                bgp(x, base + 2, i & 1 ? rgb3(G->deco0) : rgb3(G->deco1));
            }
            break;
        case AMB_SUN:
            bg_sunflower(x, base + 2, rndi(5, 9));
            if (i == 0) {
                bg_house(x, base + 1, 12, 8, rgbc(196, 60, 50), rgbc(120, 50, 40), false);
            }
            break;
        case AMB_SEA:
            if (i < 3) {
                bg_palm(x, base + 2, rndi(16, 24));
            } else {
                bgp(x, base + 3, rgb3(G->deco0));
            }
            break;
        case AMB_LEAF:
            if (i < 7) {
                bg_tree(x, base, rndi(4, 7), i & 1 ? rgbc(230, 120, 40) : rgbc(210, 70, 40), rgbc(160, 70, 30),
                        rgbc(100, 64, 40));
            }
            break;
        case AMB_SNOW:
            if (i < 6) {
                bg_pine(x, base + 1, rndi(9, 14), rgbc(40, 90, 70), rgbc(240, 246, 255));
            } else if (i == 6) {
                bg_house(x, base + 1, 12, 7, rgbc(150, 100, 70), rgbc(240, 246, 255), true);
            }
            break;
        case AMB_SAND:
            if (i < 5) {
                bg_cactus(x, base + 1, rndi(6, 10));
            }
            break;
        case AMB_FIREFLY:
            if (i < 6) {
                bg_tree(x, base, rndi(5, 8), rgbc(30, 70, 60), rgbc(20, 50, 44), rgbc(40, 30, 30));
            } else {
                bgp(x, base - rndi(4, 16), rgbc(220, 255, 140));
            }
            break;
        case AMB_EMBER:
            bgp(x, base + 1, rgbc(255, 120, 30));
            bgp(x + 1, base + 1, rgbc(255, 180, 60));
            break;
        }
    }
    s_rng = keep;
    s_bg_g = g;
#undef BX
}
