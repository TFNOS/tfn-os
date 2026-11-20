// LINEAR ALGEBRA: the screens. The chapters (a list of cards that scrolls) and a chapter: the live picture, the
// numbers of the example (the chips, + and -), the steps of the text (a tap = the next step). The boxes of each
// screen come from lay_make() for the frame now: the 3.49 bar, the 320 x 240 box, the Tab5 tablet, the round 466.

// ---------------------------------------------------------------- the boxes
typedef struct {
    rect_t pic, txt, head, body, chap, minus, plus;
    rect_t chip[4];
    int npar;  // the chips that the layout made (the chips of the chapter now)
    // the chapters screen
    rect_t ttl, list, cont, hpic;
    int cw, ch, rows, gap;
    bool round;
} lay_t;
static lay_t Y;

static inline bool in_r(const rect_t *r, int x, int y, int pad)
{
    return x >= r->x - pad && x < r->x + r->w + pad && y >= r->y - pad && y < r->y + r->h + pad;
}
static inline rect_t R(int x, int y, int w, int h)
{
    rect_t r = {x, y, w, h};
    return r;
}
// n chips in a grid of cols columns in the box p (gap g)
static void chips_grid(rect_t p, int n, int cols, int g)
{
    int rows = (n + cols - 1) / cols;
    if (rows < 1) {
        rows = 1;
    }
    int cw = (p.w - g * (cols - 1)) / cols, ch = (p.h - g * (rows - 1)) / rows;
    for (int i = 0; i < 4; i++) {
        Y.chip[i] = R(p.x + (i % cols) * (cw + g), p.y + (i / cols) * (ch + g), cw, ch);
    }
    if (n % cols && n > cols) {
        // the last row has fewer chips: they get the whole width
        int last = n - n % cols;
        int k = n - last, w = (p.w - g * (k - 1)) / k;
        for (int i = last; i < n; i++) {
            Y.chip[i] = R(p.x + (i - last) * (w + g), Y.chip[i].y, w, ch);
        }
    }
}

static void lay_make(int npar)
{
    memset(&Y, 0, sizeof Y);
    Y.npar = npar;
    int W = gfx_w, H = gfx_h;
    if (gfx_round()) {
        // the round 466 (or 412): the picture at the top, the chips at its sides, the text in the middle band,
        // - CHAPTERS + at the bottom
        float k = W / 466.0f;
#define K(v) ((int)((v) * k))
        Y.round = true;
        Y.pic = R(K(140), K(26), K(186), K(178));
        Y.txt = R(K(52), K(208), K(362), K(170));
        Y.head = R(K(150), K(206), K(166), K(0));
        Y.body = R(K(64), K(212), K(338), K(162));
        // the chips as a 2 x 2 matrix: a b at the top, c d under them (left and right of the picture)
        Y.chip[0] = R(K(52), K(86), K(92), K(54));
        Y.chip[1] = R(K(322), K(86), K(92), K(54));
        Y.chip[2] = R(K(52), K(146), K(92), K(54));
        Y.chip[3] = R(K(322), K(146), K(92), K(54));
        if (npar <= 2) {
            Y.chip[0] = R(K(52), K(116), K(92), K(54));
            Y.chip[1] = R(K(322), K(116), K(92), K(54));
        }
        Y.minus = R(K(96), K(382), K(86), K(54));
        Y.chap = R(K(190), K(388), K(86), K(58));
        Y.plus = R(K(284), K(382), K(86), K(54));
        // the chapters: the title in the top cap, one big card in the wide middle band (the cards at its sides go
        // out of the circle), the dots under it, CONTINUE in the bottom cap
        Y.ttl = R(0, K(44), W, K(70));
        Y.list = R(0, K(114), W, K(256));
        Y.cw = K(282);
        Y.ch = K(248);
        Y.rows = 1;
        Y.gap = K(18);
        Y.cont = R(K(138), K(384), K(190), K(56));
#undef K
        return;
    }
#if BOARD_UI_TABLET
    if (W >= 600 && H >= 300) {
        // the Tab5 (640 x 360, x2 on the panel)
        Y.pic = R(8, 8, 290, 262);
        rect_t p = R(8, 276, 290, 40);
        chips_grid(p, npar ? npar : 1, npar ? npar : 1, 6);
        Y.minus = R(8, 320, 142, 36);
        Y.plus = R(156, 320, 142, 36);
        Y.txt = R(306, 0, W - 306, H);
        Y.head = R(318, 6, W - 318 - 64, 40);
        Y.chap = R(W - 60, 4, 56, 48);
        Y.body = R(318, 54, W - 330, H - 60);
        Y.ttl = R(0, 0, W, 96);
        Y.hpic = R(10, 102, 232, 248);
        Y.list = R(250, 100, W - 250, H - 100);
        Y.rows = 1;
        Y.gap = 10;
        Y.ch = Y.list.h - 12;
        Y.cw = 176;
        Y.cont = R(W - 236, 22, 220, 54);
        return;
    }
#endif
    if (W >= 2 * H) {
        // the 3.49 bar: the picture at the left, the numbers, the text at the right
        Y.pic = R(4, 4, 168, H - 8);
        rect_t p = R(178, 28, 112, H - 28 - 48);
        chips_grid(p, npar ? npar : 1, npar >= 3 ? 2 : 1, 4);
        Y.minus = R(178, H - 44, 54, 42);
        Y.plus = R(236, H - 44, 54, 42);
        Y.txt = R(296, 0, W - 296, H);
        Y.head = R(306, 2, W - 306 - 54, 26);
        Y.chap = R(W - 50, 0, 50, 40);
        Y.body = R(306, 32, W - 316, H - 36);
        Y.ttl = R(0, 0, 210, H);
        Y.hpic = R(8, 58, 196, 58);
        Y.list = R(212, 0, W - 212, H);
        Y.rows = 1;
        Y.gap = 6;
        Y.ch = H - 12;
        Y.cw = 136;
        Y.cont = R(12, 122, 188, 44);
        return;
    }
    // the 320 x 240 box (the CoreS3; the AMOLED x1.4): the picture at the top left, the chips under it, the text
    // in a column at the right (the - + keys under the text when there are more than 2 numbers)
    int pw = 124;
    Y.pic = R(4, 4, pw, pw);
    int big = npar >= 3;
    rect_t p = R(4, pw + 8, pw, big ? H - pw - 10 : 40);
    chips_grid(p, npar ? npar : 1, 2, 4);
    if (big) {
        Y.minus = R(pw + 10, H - 42, (W - pw - 14) / 2 - 2, 40);
        Y.plus = R(Y.minus.x + Y.minus.w + 4, H - 42, W - Y.minus.x - Y.minus.w - 8, 40);
    } else {
        Y.minus = R(4, H - 42, pw / 2 - 2, 40);
        Y.plus = R(4 + pw / 2 + 2, H - 42, pw / 2 - 2, 40);
    }
    Y.txt = R(pw + 6, 0, W - pw - 6, H);
    Y.head = R(pw + 14, 2, W - pw - 14 - 50, 30);
    Y.chap = R(W - 48, 0, 48, 36);
    Y.body = R(pw + 14, 36, W - pw - 20, (big ? H - 46 : H) - 38);
    Y.ttl = R(0, 0, W, 62);
    Y.list = R(0, 62, W, H - 62);
    Y.rows = 1;
    Y.gap = 6;
    Y.ch = Y.list.h - 8;
    Y.cw = 136;
    Y.cont = R(W - 140, 10, 132, 44);
}
