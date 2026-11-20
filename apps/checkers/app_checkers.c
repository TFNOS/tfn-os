// CHECKERS (GAMES): checkers ("damka") on a wood board
// with men and kings made in Blender (tools/mk_art.py -> ck_art.c).
// THE RULES (ck_engine.inc): ISRAELI (the default: men capture back too, the kings fly) or ENGLISH (the panel). Three
// ways to play: VS COMPUTER (3 levels; the computer thinks in its own task), 2 PLAYERS on this board, 2 BOARDS (two
// boards over the radio: libs/duel, the host plays light). On the title the computer plays itself (the demo).
// THE 3.49 VIEW: a big board of 54 px squares
// (7.2 mm) that rolls under a camera: 8 columns and about 3 rows in the view. The camera goes to the action (the
// pieces that can move at the start of a turn, the selected piece and its squares, the move of the other side). A drag
// on the board (not on a piece) moves the view; the small board at the right shows the whole game and moves the view
// to a row (a tap or a drag). The 320 x 240 frame and the Tab5 show the whole board (28 px and 42 px squares).
// THE TOUCH (all frames): a DRAG of a piece that can move lifts it; it snaps to the nearest legal square (a ghost shows
// where it lands), and the release moves it there (else it goes back). A TAP selects the nearest piece that can move;
// a tap on one of its squares moves it AT ONCE only when the tap is clearly on that square (each other piece and
// square is 9 mm further away): else the move shows as a ghost, and a second tap on it makes it ("TAP AGAIN"). A
// chain of captures with more than one way goes one jump at a time. A thumb probe showed 0 wrong moves at
// sd 2.5 mm.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "jingle/jingle.h"
// the opening of CHECKERS: a light march in C, staccato, a tuba under it
static const tune_note_t CK_OPEN[] = {
        JN(0, 90, J_G4, W_PULSE, 20),
        JN(150, 90, J_C5, W_PULSE, 20),
        JN(300, 90, J_E5, W_PULSE, 20),
        JN(450, 180, J_G5, W_PULSE, 22),
        JN(700, 90, J_F5, W_PULSE, 20),
        JN(850, 90, J_D5, W_PULSE, 20),
        JN(1000, 450, J_C5, JHOLD(W_PULSE), 20),
        JN(1000, 450, J_E5, JHOLD(W_SQUARE), 9),
        JN(0, 140, J_C3, W_TRI, 55),
        JN(300, 140, J_G2, W_TRI, 55),
        JN(700, 140, J_F2, W_TRI, 55),
        JN(1000, 450, J_C3, JHOLD(W_TRI), 55),
        JSNARE(450),
        JSNARE(1000),
};
#include "tfn/board.h"
#include "tfn/duel.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "miniz.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "tfsfx/tfsfx.h"
#include "ck_sfx.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
static const char *TAG = "checkers";
static int64_t ck_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}
#include "ck_engine.inc"

static int s_lang = 1;  // 0 English, 1 Hebrew
#define T(en, he) (s_lang ? (he) : (en))

// ---------------------------------------------------------------- the layout (enter: the frame of this board kind)
static int SQ = 28, PS = 32, BX = 90, BY = 8;  // the square, the piece picture, the top left of the board (BY moves
                                               // with the camera on the 3.49)
static bool NARROW;                            // 320 x 240: the board at the right, the info at the left
static bool BIG;              // the Tab5 640 x 360: the board (42 px) at the left, the info at the right
static bool ROLL;             // the 3.49 640 x 172: the big rolling board, the panel at the right
static bool RND;              // a round panel (APP_ROUND_HI): the board in the middle of
                              // the circle, the state on the top arc, the counts on the side arcs,
                              // UNDO on the bottom arc; the table round it (s_table)
static uint16_t *s_table;     // (RND) the table, the frame and the shadow: gfx_w x gfx_h
static int INFO_X0, INFO_X1;  // the info column (320 x 240, Tab5)
static float MMPX = 0.128f;   // mm in one px of the frame
// the 3.49 (ROLL): the board x 4..435; the panel x 442..638 with the small board (17 px squares) and the counts
#define PX0   442
#define PX1   638
#define MM_X  446
#define MM_Y  32
#define MM_S  17
#define CNT_X 612
static bool s_lp_on;  // the loupe of the 320 x 240 frame shows (below)
static void draw_loupe(void);
static float VR = 3;     // the rows in the view (ROLL)
#define MARGIN_MM  9.0f  // a tap moves at once: every other piece and square this much further
#define CONFIRM_MM 3.0f  // the second tap on the ghost: this much nearer to it than to the rest
#define DRAG_PX    10    // a finger that moves more than this drags (the tap slop of input.c)

// ---------------------------------------------------------------- the art (ck_art.c)
extern const uint8_t CK_ART[];
extern const uint32_t CK_ART_LEN;
static uint16_t *s_board_img;  // 8 SQ x 8 SQ
static uint16_t *s_pc[4];      // man_l, king_l, man_d, king_d: PS x PS colours
static uint8_t *s_pa[4];       // their alpha (0..15)
static uint8_t *s_art_mem;
static void *ck_alloc(size_t n)
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
#else
    return malloc(n);
#endif
}
static bool art_load(int sq)
{
    const uint8_t *b = CK_ART;
    if (CK_ART_LEN < 5 || memcmp(b, "CKA1", 4)) {
        return false;
    }
    size_t at = 5;
    for (int k = 0; k < b[4]; k++) {
        const int s = b[at], p = b[at + 1];
        const uint32_t zl = b[at + 2] | b[at + 3] << 8 | b[at + 4] << 16 | (uint32_t)b[at + 5] << 24;
        at += 6;
        if (s == sq) {
            const size_t out = (size_t)64 * s * s * 2 + 4 * ((size_t)p * p * 3);
            s_art_mem = ck_alloc(out);
            tinfl_decompressor *d = malloc(sizeof(tinfl_decompressor));
            if (!s_art_mem || !d) {
                free(d);
                return false;
            }
            tinfl_init(d);
            size_t in = zl, got = out;
            const int st = tinfl_decompress(d, b + at, &in, s_art_mem, s_art_mem, &got,
                                            TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
            free(d);
            if (st != TINFL_STATUS_DONE || got != out) {
                return false;
            }
            uint8_t *q = s_art_mem;
            s_board_img = (uint16_t *)q;
            q += (size_t)64 * s * s * 2;
            for (int i = 0; i < 4; i++) {
                s_pc[i] = (uint16_t *)q;
                q += (size_t)p * p * 2;
                s_pa[i] = q;
                q += (size_t)p * p;
            }
            PS = p;
            return true;
        }
        at += zl;
    }
    return false;
}
#if BOARD_UI_TABLET
// the Tab5: the art of the 28 px squares made 1.5 times bigger (42 px squares, about 7 mm), with a bilinear filter
// (the colours weighted by the alpha). One time at the start of the app; the new memory replaces s_art_mem.
static uint16_t ck_sw(uint16_t c)
{
    return (uint16_t)(c >> 8 | c << 8);
}
static void grow(const uint16_t *sc, const uint8_t *sa, int sw, uint16_t *dc, uint8_t *da, int dw)
{
    for (int y = 0; y < dw; y++) {
        float fy = (y + 0.5f) * sw / dw - 0.5f;
        if (fy < 0) {
            fy = 0;
        }
        int y0 = (int)fy;
        if (y0 > sw - 2) {
            y0 = sw - 2;
        }
        const float wy = fy - y0 > 1 ? 1 : fy - y0;
        for (int x = 0; x < dw; x++) {
            float fx = (x + 0.5f) * sw / dw - 0.5f;
            if (fx < 0) {
                fx = 0;
            }
            int x0 = (int)fx;
            if (x0 > sw - 2) {
                x0 = sw - 2;
            }
            const float wx = fx - x0 > 1 ? 1 : fx - x0;
            float r = 0, g = 0, b = 0, a = 0, wsum = 0;
            for (int k = 0; k < 4; k++) {
                const int xx = x0 + (k & 1), yy = y0 + (k >> 1);
                const float w = ((k & 1) ? wx : 1 - wx) * ((k >> 1) ? wy : 1 - wy);
                const float al = sa ? sa[yy * sw + xx] : 15;
                const uint16_t c = ck_sw(sc[yy * sw + xx]);
                r += (c >> 11) * w * al;
                g += ((c >> 5) & 63) * w * al;
                b += (c & 31) * w * al;
                a += al * w;
                wsum += w * al;
            }
            if (da) {
                da[y * dw + x] = (uint8_t)(a + 0.5f);
            }
            const uint16_t c = wsum > 0 ? (uint16_t)((int)(r / wsum + 0.5f) << 11 | (int)(g / wsum + 0.5f) << 5 |
                                                     (int)(b / wsum + 0.5f))
                                        : 0;
            dc[y * dw + x] = ck_sw(c);
        }
    }
}
static bool art_grow(int s1)
{
    const int s0 = SQ, p0 = PS, p1 = p0 * s1 / s0;
    uint8_t *m = ck_alloc((size_t)64 * s1 * s1 * 2 + 4 * ((size_t)p1 * p1 * 3));
    if (!m) {
        return false;
    }
    uint8_t *q = m;
    uint16_t *board = (uint16_t *)q;
    grow(s_board_img, NULL, 8 * s0, board, NULL, 8 * s1);
    q += (size_t)64 * s1 * s1 * 2;
    for (int i = 0; i < 4; i++) {
        uint16_t *c = (uint16_t *)q;
        q += (size_t)p1 * p1 * 2;
        uint8_t *a = q;
        q += (size_t)p1 * p1;
        grow(s_pc[i], s_pa[i], p0, c, a, p1);
        s_pc[i] = c;
        s_pa[i] = a;
    }
    free(s_art_mem);
    s_art_mem = m;
    s_board_img = board;
    SQ = s1;
    PS = p1;
    return true;
}
#endif

// ---------------------------------------------------------------- the game
enum {
    G_TITLE,
    G_WAIT,
    G_PLAY,
    G_OVER
};
enum {
    MODE_CPU,
    MODE_TWO,
    MODE_NET,
    MODE_DEMO
};
static struct {
    int g, mode, level, my_side;
    ckpos_t pos, prev;  // the position; the one before the move that slides now
    ckmove_t legal[CK_MAXM];
    int nlegal, sel;  // the moves now; the selected piece (its square; -1: none)
    ckmove_t last;
    bool has_last;
    float gt;  // the time in the state
    char result[40], why[48];
    int win;  // 1 = I won, -1 = I lost, 0 = a draw (G_OVER)
} C;
#define HIST 200
static ckpos_t s_hist[HIST];
static int s_nhist;
static char s_note[64];
static int64_t s_note_until;
static float s_demo_wait;  // the demo game ended: a new one starts after this time

// THE SELECTION: the selected piece, the jumps that the person made already (a chain one jump at a time), the squares
// where it can go now (its next landings), the square that waits for the second tap
static uint8_t s_pre[CK_PATH];
static int s_npre;
typedef struct {
    uint8_t sq, end, cap;
} tgt_t;  // a landing; a move ends there; the square of the piece that the jump takes (0xff)
static tgt_t s_tg[CK_MAXM];
static int s_ntg;
static int s_pend = -1;  // the landing that waits for the second tap (-1: none)
static int64_t s_pend_ms;
static float s_hint_t = 9;  // the pieces that can move flash (a tap on a piece that cannot move)

// THE ANIMATION of a move: legs from point to point (board px: from the top left of the board picture)
typedef struct {
    float x, y;
} pt_t;
static struct {
    bool on, partial;  // partial: one jump of a chain (the move is not made yet)
    ckmove_t m;
    int leg0, nlegs, popped, landed;
    pt_t p[CK_PATH + 2];
    float t0[CK_PATH + 2], t;
} A;
// the move of the other side (the computer, the friend): first the camera goes there and the piece rises
static struct {
    bool on;
    ckmove_t m;
    float t;
} PRE;
// the drag of a finger
enum {
    DK_NONE,
    DK_PIECE,
    DK_VIEW,
    DK_MAP
};
static struct {
    bool on, moved, lift;
    int kind, sq, tgt;  // the piece; the index in s_tg of the square under the piece now (-1)
    int x0, y0, x, y;
    float cam0, snd_t;
} D;
// a piece that goes back to its square after a drop on no legal square
static struct {
    bool on;
    int sq;
    float x, y, t;
} RB;

static bool view_flip(void)
{
    return C.mode == MODE_TWO || C.mode == MODE_DEMO ? false : C.my_side < 0;
}
// a square on the screen: its top left
static void sq_xy(int sq, int *x, int *y)
{
    int r = sq >> 3, c = sq & 7;
    if (view_flip()) {
        r = 7 - r;
        c = 7 - c;
    }
    *x = BX + c * SQ;
    *y = BY + r * SQ;
}
// the middle of a square on the board picture (board px)
static void sq_bxy(int sq, float *x, float *y)
{
    int r = sq >> 3, c = sq & 7;
    if (view_flip()) {
        r = 7 - r;
        c = 7 - c;
    }
    *x = c * SQ + SQ / 2.0f;
    *y = r * SQ + SQ / 2.0f;
}
static int scr_row(int sq)
{
    const int r = sq >> 3;
    return view_flip() ? 7 - r : r;
}
// the side that this person moves now (0: not this board's turn)
static int turn_side(void)
{
    if (C.g != G_PLAY || C.mode == MODE_DEMO) {
        return 0;
    }
    if (C.mode == MODE_TWO) {
        return C.pos.side;
    }
    return C.pos.side == C.my_side ? C.my_side : 0;
}
static int mine_now(void)
{
    return A.on || PRE.on || RB.on ? 0 : turn_side();
}
static int cur_sq(void)
{
    return s_npre ? s_pre[s_npre - 1] : C.sel;
}
// a move of the selected piece that goes through the jumps of the person
static bool cand(int i)
{
    const ckmove_t *m = &C.legal[i];
    if (m->from != C.sel) {
        return false;
    }
    if (s_npre >= (m->ncap ? m->ncap : 1)) {
        return false;
    }
    for (int k = 0; k < s_npre; k++) {
        if (m->path[k] != s_pre[k]) {
            return false;
        }
    }
    return true;
}
static void targets(void)
{
    s_ntg = 0;
    if (C.sel < 0) {
        return;
    }
    for (int i = 0; i < C.nlegal; i++) {
        if (!cand(i)) {
            continue;
        }
        const ckmove_t *m = &C.legal[i];
        const int nl = m->ncap ? m->ncap : 1, x = m->path[s_npre];
        int k = 0;
        while (k < s_ntg && s_tg[k].sq != x) {
            k++;
        }
        if (k == s_ntg) {
            s_tg[k].sq = (uint8_t)x;
            s_tg[k].end = 0;
            s_tg[k].cap = m->ncap ? m->caps[s_npre] : 0xff;
            s_ntg++;
        }
        if (nl == s_npre + 1) {
            s_tg[k].end = 1;
        }
    }
}
static bool movable(int sq)
{
    for (int i = 0; i < C.nlegal; i++) {
        if (C.legal[i].from == sq) {
            return true;
        }
    }
    return false;
}
static void refresh_legal(void)
{
    C.nlegal = ck_gen(&C.pos, C.legal, CK_MAXM);
    C.sel = -1;
    s_npre = 0;
    s_pend = -1;
    s_ntg = 0;
    // (one piece only can move, as often in a capture: it is selected at once)
    if (C.nlegal && turn_side()) {
        bool one = true;
        for (int i = 1; i < C.nlegal; i++) {
            if (C.legal[i].from != C.legal[0].from) {
                one = false;
            }
        }
        if (one) {
            C.sel = C.legal[0].from;
            targets();
        }
    }
}

// ---------------------------------------------------------------- the camera (the 3.49: the rows in the view)
static float s_cam, s_cam_to;  // the top of the view (screen rows); its target
static void cam_clamp(float *t)
{
    if (*t > 8 - VR) {
        *t = 8 - VR;
    }
    if (*t < 0) {
        *t = 0;
    }
}  // (the whole board in the view: 0)
static void cam_to(float t)
{
    cam_clamp(&t);
    s_cam_to = t;
}
static bool row_in(int r, float t)
{
    return r >= t - 0.15f && r + 1 <= t + VR + 0.15f;
}
// the selected piece and its squares; nothing when all of them are in the view
static void cam_sel(void)
{
    if (!ROLL) {
        return;
    }
    const int c = cur_sq();
    if (c < 0) {
        return;
    }
    int r0 = scr_row(c), r1 = r0;
    for (int k = 0; k < s_ntg; k++) {
        const int r = scr_row(s_tg[k].sq);
        if (r < r0) {
            r0 = r;
        }
        if (r > r1) {
            r1 = r;
        }
    }
    if (r1 + 1 - r0 <= VR) {
        const float t = (r0 + r1 + 1) / 2.0f - VR / 2;
        if (!(row_in(r0, s_cam_to) && row_in(r1, s_cam_to)) || fabsf(t - s_cam_to) > 0.25f) {
            cam_to(t);
        }
    } else if (!row_in(scr_row(c), s_cam_to)) {
        cam_to(scr_row(c) + 0.5f - VR / 2);  // (a king with far squares)
    }
}
// the start of a turn of this board: the view that shows the most moves (the piece and its first square; a capture
// counts 3), with the least travel
static void cam_turn(void)
{
    if (!ROLL || !turn_side()) {
        return;
    }
    if (C.sel >= 0) {
        cam_sel();
        return;
    }
    float best_t = s_cam_to, best = -1e9f;
    for (int s = 0; s <= 40; s++) {
        const float t = (8 - VR) * s / 40;
        float v = -fabsf(t - s_cam_to) * 0.2f;
        for (int i = 0; i < C.nlegal; i++) {
            if (row_in(scr_row(C.legal[i].from), t) && row_in(scr_row(C.legal[i].path[0]), t)) {
                v += C.legal[i].ncap ? 3 : 1;
            }
        }
        if (v > best) {
            best = v;
            best_t = t;
        }
    }
    // (the rows of the moves in that view: in the middle)
    int r0 = 8, r1 = -1;
    for (int i = 0; i < C.nlegal; i++) {
        const int a = scr_row(C.legal[i].from), b = scr_row(C.legal[i].path[0]);
        if (!row_in(a, best_t) || !row_in(b, best_t)) {
            continue;
        }
        const int lo = a < b ? a : b, hi = a < b ? b : a;
        if (lo < r0) {
            r0 = lo;
        }
        if (hi > r1) {
            r1 = hi;
        }
    }
    if (r1 >= r0 && r1 + 1 - r0 <= VR) {
        best_t = (r0 + r1 + 1) / 2.0f - VR / 2;
    }
    cam_to(best_t);
}
// a move of the other side: its piece and its first square
static void cam_move(const ckmove_t *m)
{
    if (!ROLL) {
        return;
    }
    const int a = scr_row(m->from), b = scr_row(m->path[0]), r0 = a < b ? a : b, r1 = a < b ? b : a;
    if (row_in(r0, s_cam_to) && row_in(r1, s_cam_to)) {
        return;
    }
    cam_to((r0 + r1 + 1) / 2.0f - VR / 2);
}

// ---------------------------------------------------------------- the effects: pops, rings, sparks, the crown
static struct {
    int8_t sq, pc;
    float t;
} s_pop[CK_PATH];
static int s_npop;
#define NRING 8
static struct {
    float x, y, t, len, r0, r1;
    uint16_t c;
} s_ring[NRING];  // (board px)
#define NPART 72
static struct {
    float x, y, vx, vy, life, max, g;
    uint16_t c;
    uint8_t r, scr;
} s_part[NPART];
static struct {
    bool on;
    int sq;
    float t;
} s_crown;
static struct {
    int sq;
    float t;
} s_squash = {-1, 9};
static float s_shake;
static uint32_t s_fxr = 1;
static float frnd(void)
{
    s_fxr = s_fxr * 1664525u + 1013904223u;
    return (s_fxr >> 8) / 16777216.0f;
}
static void ring_add(float x, float y, float r0, float r1, float len, uint16_t c)
{
    for (int i = 0; i < NRING; i++) {
        if (s_ring[i].t < s_ring[i].len) {
            continue;
        }
        s_ring[i].x = x;
        s_ring[i].y = y;
        s_ring[i].t = 0;
        s_ring[i].len = len;
        s_ring[i].r0 = r0;
        s_ring[i].r1 = r1;
        s_ring[i].c = c;
        return;
    }
}
static void part_add(float x, float y, float vx, float vy, float life, float g, uint16_t c, int r, bool scr)
{
    for (int i = 0; i < NPART; i++) {
        if (s_part[i].life > 0) {
            continue;
        }
        s_part[i].x = x;
        s_part[i].y = y;
        s_part[i].vx = vx;
        s_part[i].vy = vy;
        s_part[i].life = life;
        s_part[i].max = life;
        s_part[i].g = g;
        s_part[i].c = c;
        s_part[i].r = (uint8_t)r;
        s_part[i].scr = scr;
        return;
    }
}
static void fx_clear(void)
{
    s_npop = 0;
    for (int i = 0; i < NRING; i++) {
        s_ring[i].t = s_ring[i].len = 0;
    }
    for (int i = 0; i < NPART; i++) {
        s_part[i].life = 0;
    }
    s_crown.on = false;
    s_squash.sq = -1;
    s_shake = 0;
}

// ---------------------------------------------------------------- the sounds (CC0 MP3 in the app file, libs/tfsfx)
static bool s_snd = true;
enum {
    FX_PICK,
    FX_PLACE,
    FX_HOP,
    FX_TAKE,
    FX_SLIDE,
    FX_CROWN,
    FX_WIN,
    FX_LOSE,
    FX_BAD,
    FX_N
};
static const char *const FXN[FX_N] = {"pick", "place", "hop", "take", "slide", "crown", "win", "lose", "bad"};
static bool loud(void)
{
    return s_snd && C.mode != MODE_DEMO;
}
static void fx(int id, int vol, float var)
{
    if (loud()) {
        tfsfx_play_var(id, vol, var);
    }
}
static void voice(float d_ms, float len_ms, float f0, float f1, int wave, int vol)
{
    if (!loud()) {
        return;
    }
    audio_voice_t v = {0};
    v.delay = (uint32_t)(d_ms * AUDIO_SAMPLE_RATE / 1000);
    v.dur = (uint32_t)(len_ms * AUDIO_SAMPLE_RATE / 1000);
    v.f0 = f0;
    v.f1 = f1;
    v.wave = (uint8_t)wave;
    v.vol = (uint8_t)vol;
    v.shape = AUDIO_PERC;
    audio_play(&v);
}
static void snd_crown(void)
{
    fx(FX_CROWN, 85, 0.02f);
    for (int i = 0; i < 3; i++) {
        voice(60 + i * 70, 220, 784 * (1 + i * 0.26f), 784 * (1 + i * 0.26f), W_TRI, 14);
    }
}

// ---------------------------------------------------------------- the computer (its own task: the frames stay smooth)
static volatile int s_ai;                     // 0 idle, 1 asked, 2 the answer is ready
static volatile bool s_ai_busy;               // the task thinks now
static volatile uint32_t s_ai_ask, s_ai_got;  // the generation of the question, of the answer
static uint32_t s_gen;  // +1 at each move, new game, undo, title: an answer for an older position is dropped
static int s_ai_level;
static ckmove_t s_ai_move;
static int64_t s_ai_t0;
static void ai_think_now(void)
{
    static const int DEPTH[3] = {2, 5, 12}, MS[3] = {200, 900, 2500}, NOISE[3] = {90, 18, 0};
    const uint32_t g = s_ai_ask;
    const int lv = s_ai_level;
    ckpos_t p = C.pos;
    ckmove_t m;
    int d;
    if (!ck_think(&p, DEPTH[lv], MS[lv], NOISE[lv], esp_random(), &m, &d)) {
        memset(&m, 0, sizeof m);
    }
    s_ai_move = m;
    s_ai_got = g;
    s_ai = 2;
}
#ifdef ESP_PLATFORM
// (the stack: each ply of ck_search keeps its 64 moves, about 2 KB; HARD goes to 12 plies + 6 of the capture chase.
// 16 KB overflows on the Tab5: its faster CPU goes deeper in the same time. The stack is in PSRAM.)
static TaskHandle_t s_ai_task;
static volatile bool s_ai_go, s_ai_quit;
static void ai_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (s_ai_quit) {
            break;
        }
        if (s_ai_go) {
            s_ai_go = false;
            s_ai_busy = true;
            ai_think_now();
            s_ai_busy = false;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_ai_quit = false;
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
static void ai_ask(void)
{
    s_ai_go = true;
}
#else
static void ai_ask(void)
{
    ai_think_now();
}
#endif
static void ai_start(int level)
{
    if (s_ai || s_ai_busy) {
        return;
    }
    s_ai_level = level < 0 ? 0 : level > 2 ? 2 : level;
    s_ai_ask = s_gen;
    s_ai_t0 = ck_now_ms();
    s_ai = 1;
    ai_ask();
}
// the computer moves now: VS COMPUTER on its turn, or the demo of the title
static bool ai_turn(void)
{
    if (C.mode == MODE_DEMO) {
        return (C.g == G_TITLE || C.g == G_WAIT) && s_demo_wait <= 0;
    }
    return C.g == G_PLAY && C.mode == MODE_CPU && C.pos.side != C.my_side;
}

// ---------------------------------------------------------------- the moves
static void game_end(int win, const char *res, const char *why)
{
    C.g = G_OVER;
    C.gt = 0;
    C.win = win;
    snprintf(C.result, sizeof C.result, "%s", res);
    snprintf(C.why, sizeof C.why, "%s", why);
    fx(win >= 0 ? FX_WIN : FX_LOSE, 80, 0);
    if (win > 0) {
        // (the confetti: screen points)
        const uint16_t CC[6] = {rgb(255, 214, 60), rgb(255, 80, 80),   rgb(90, 220, 120),
                                rgb(80, 170, 255), rgb(255, 140, 220), rgb(255, 255, 255)};
        for (int i = 0; i < 46; i++) {
            part_add(frnd() * (ROLL ? 8 * SQ : gfx_w), -10 - frnd() * 60, (frnd() - 0.5f) * 40, 30 + frnd() * 60,
                     2.2f + frnd(), 40, CC[i % 6], 2 + (int)(frnd() * 2), true);
        }
    }
}
static void check_end(void)
{
    const int e = ck_over(&C.pos);
    if (!e) {
        return;
    }
    if (C.mode == MODE_DEMO) {
        s_demo_wait = 2.5f;
        return;
    }
    if (e == 2) {
        game_end(0, T("A DRAW", "תיקו"), T("50 MOVES WITH NO CAPTURE", "50 מהלכים בלי לכידה"));
        return;
    }
    const int loser = C.pos.side, winner = -loser;
    const char *res = winner > 0 ? T("LIGHT WINS", "הלבנים ניצחו") : T("RED WINS", "האדומים ניצחו");
    const char *why = T("NO MOVE LEFT", "לא נשאר מהלך");
    if (C.mode == MODE_TWO) {
        game_end(1, res, why);
    } else {
        game_end(winner == C.my_side ? 1 : -1, winner == C.my_side ? T("YOU WIN!", "ניצחתם!") : T("YOU LOSE", "הפסדתם"),
                 why);
    }
}
static float leg_time(float d)
{
    const float t = 0.11f + 0.065f * d;
    return t > 0.5f ? 0.5f : t;
}  // (d: squares)
// a slide from the start point (board px) along the landings of the move, from the landing leg0 on
static void anim_start(const ckmove_t *m, int leg0, float sx, float sy, bool partial)
{
    A.on = true;
    A.partial = partial;
    A.m = *m;
    A.t = 0;
    A.leg0 = leg0;
    A.nlegs = partial ? leg0 + 1 : (m->ncap ? m->ncap : 1);
    A.p[leg0].x = sx;
    A.p[leg0].y = sy;
    float t = 0;
    for (int k = leg0; k < A.nlegs; k++) {
        sq_bxy(m->path[k], &A.p[k + 1].x, &A.p[k + 1].y);
        const float dx = A.p[k + 1].x - A.p[k].x, dy = A.p[k + 1].y - A.p[k].y, d = sqrtf(dx * dx + dy * dy) / SQ;
        A.t0[k] = t;
        t += d < 0.6f ? 0.09f : leg_time(d);
    }
    A.t0[A.nlegs] = t;
    A.popped = partial ? 0 : leg0;
    A.landed = leg0;
}
static void pop_piece(int sq, int pc, float delay)
{
    if (s_npop >= CK_PATH || !pc) {
        return;
    }
    s_pop[s_npop].sq = (int8_t)sq;
    s_pop[s_npop].pc = (int8_t)pc;
    s_pop[s_npop].t = -delay;
    s_npop++;
    float x, y;
    sq_bxy(sq, &x, &y);
    const uint16_t c = pc > 0 ? rgb(250, 238, 205) : rgb(235, 50, 50),
                   c2 = pc > 0 ? rgb(200, 180, 140) : rgb(150, 20, 28);
    for (int i = 0; i < 14; i++) {
        const float a = i * 0.4488f + frnd() * 0.3f, v = SQ * (1.6f + frnd() * 1.8f);
        part_add(x, y, fcos(a) * v, fsin(a) * v - SQ * 0.8f, 0.45f + frnd() * 0.3f, SQ * 6.0f, i & 1 ? c : c2,
                 2 + (SQ > 40) + (int)(frnd() * 2), false);
    }
    ring_add(x, y, SQ * 0.2f, SQ * 0.75f, 0.3f, rgb(255, 230, 160));
    s_shake = SQ > 40 ? 3.5f : 2.0f;
    fx(FX_TAKE, 85, 0.06f);
}
static void squash(int sq)
{
    s_squash.sq = sq;
    s_squash.t = 0;
}
static void net_send_move(const ckmove_t *m);
static void play_move(const ckmove_t *m, bool send, int leg0, float sx, float sy)
{
    if (s_nhist < HIST) {
        s_hist[s_nhist++] = C.pos;
    }
    C.prev = C.pos;
    ck_make(&C.pos, m);
    s_gen++;
    C.last = *m;
    C.has_last = true;
    C.sel = -1;
    C.nlegal = 0;
    s_npre = 0;
    s_pend = -1;
    s_ntg = 0;
    anim_start(m, leg0, sx, sy, false);
    // (the captures of the jumps that the person made one at a time pop at once)
    for (int k = 0; k < leg0 && k < m->ncap; k++) {
        pop_piece(m->caps[k], C.prev.b[m->caps[k]], k * 0.06f);
    }
    if (send) {
        net_send_move(m);
    }
}
// the end of the slide: the landing, the crown, the next turn
static void anim_done(void)
{
    const bool partial = A.partial;
    A.on = false;
    const int to = partial ? A.m.path[A.nlegs - 1] : A.m.to;
    float x, y;
    sq_bxy(to, &x, &y);
    ring_add(x, y + SQ * 0.1f, SQ * 0.3f, SQ * 0.62f, 0.28f, rgb(255, 236, 200));
    squash(to);
    if (partial) {
        fx(FX_HOP, 80, 0.05f);
        cam_sel();
        return;
    }
    fx(FX_PLACE, 85, 0.05f);
    if (A.m.promo) {
        s_crown.on = true;
        s_crown.sq = to;
        s_crown.t = 0;
    }
    refresh_legal();
    check_end();
    cam_turn();
}
static void anim_update(float dt)
{
    A.t += dt;
    if (!A.partial) {
        // (a taken piece pops when the man passes over it: the middle of its leg)
        while (A.popped < A.m.ncap && A.popped < A.nlegs && A.t >= (A.t0[A.popped] + A.t0[A.popped + 1]) / 2) {
            pop_piece(A.m.caps[A.popped], C.prev.b[A.m.caps[A.popped]], 0);
            A.popped++;
        }
        while (A.landed < A.nlegs - 1 && A.t >= A.t0[A.landed + 1]) {
            fx(FX_HOP, 70, 0.08f);
            A.landed++;
        }
    }
    if (A.t >= A.t0[A.nlegs]) {
        anim_done();
    }
}
// the place of the moving piece now (board px), its height (px) and its size
static void anim_place(float *x, float *y, float *lift, float *scale)
{
    int k = A.leg0;
    while (k < A.nlegs - 1 && A.t >= A.t0[k + 1]) {
        k++;
    }
    const float len = A.t0[k + 1] - A.t0[k];
    float f = len > 0 ? (A.t - A.t0[k]) / len : 1;
    if (f > 1) {
        f = 1;
    }
    if (f < 0) {
        f = 0;
    }
    const float e = f * f * (3 - 2 * f);
    const float dx = A.p[k + 1].x - A.p[k].x, dy = A.p[k + 1].y - A.p[k].y;
    *x = A.p[k].x + dx * e;
    *y = A.p[k].y + dy * e;
    const float d = sqrtf(dx * dx + dy * dy) / SQ, s = fsin(f * 3.14159f);
    const bool jump = A.m.ncap > 0 && d > 1.2f;
    *lift = s * (d < 0.6f ? 0 : jump ? SQ * 0.55f : SQ * 0.16f);
    *scale = 1 + s * (jump ? 0.16f : d < 0.6f ? 0 : 0.06f);
}
// one landing of the selected piece (from a tap or a drop at the board point bx, by): the move when the landing ends
// it or when only one way goes on from it; else one jump of a chain
static void choose(int X, bool drop, float bx, float by)
{
    int n = 0, last = -1, end = -1;
    for (int i = 0; i < C.nlegal; i++) {
        if (!cand(i) || C.legal[i].path[s_npre] != X) {
            continue;
        }
        n++;
        last = i;
        if ((C.legal[i].ncap ? C.legal[i].ncap : 1) == s_npre + 1) {
            end = i;
        }
    }
    if (!n) {
        return;
    }
    float sx, sy;
    if (drop) {
        sx = bx;
        sy = by;
    } else {
        sq_bxy(cur_sq(), &sx, &sy);
    }
    if (end >= 0 || n == 1) {
        const ckmove_t m = C.legal[end >= 0 ? end : last];
        play_move(&m, C.mode == MODE_NET, s_npre, sx, sy);
        return;
    }
    // (more than one way on from X: one jump; the piece waits at X for the next)
    anim_start(&C.legal[last], s_npre, sx, sy, true);
    s_pre[s_npre++] = (uint8_t)X;
    s_pend = -1;
    targets();
}

// ---------------------------------------------------------------- 2 BOARDS (libs/duel; the host plays light)
enum {
    NM_MOVE = 1,
    NM_BYE = 2
};
static uint8_t s_net_b[4 + CK_PATH];  // the last move of the friend that this board did not play yet
static int s_net_n;
static void net_send_move(const ckmove_t *m)
{
    if (C.mode != MODE_NET) {
        return;
    }
    uint8_t b[4 + CK_PATH];
    b[0] = NM_MOVE;
    b[1] = m->from;
    b[2] = m->to;
    b[3] = m->ncap;
    memcpy(b + 4, m->caps, m->ncap);
    duel_send(b, 4 + m->ncap);
}
static void game_new(int mode)
{
    C.mode = mode;
    ck_start(&C.pos);
    C.has_last = false;
    C.gt = 0;
    s_nhist = 0;
    A.on = PRE.on = RB.on = D.on = false;
    s_gen++;
    s_demo_wait = 0;
    s_net_n = 0;
    fx_clear();
    if (mode == MODE_NET) {
        C.my_side = duel_host() ? 1 : -1;
    } else if (mode == MODE_TWO || mode == MODE_DEMO) {
        C.my_side = 1;
    } else {
        C.my_side = store_get("ck_side", 0) ? -1 : 1;
    }
    C.g = mode == MODE_DEMO ? C.g : G_PLAY;
    refresh_legal();
    // (the view at the side of this person: the bottom rows)
    s_cam = s_cam_to = 8;
    cam_clamp(&s_cam);
    cam_clamp(&s_cam_to);
    cam_turn();
}
static void to_title(void)
{
    if (C.mode == MODE_NET) {
        duel_stop();
    }
    C.g = G_TITLE;
    game_new(MODE_DEMO);
    s_cam = s_cam_to = (8 - VR) / 2;
    cam_clamp(&s_cam);
    cam_clamp(&s_cam_to);
}
static void net_poll(int64_t now)
{
    duel_poll(now);
    const int ds = duel_state();
    if (C.g == G_WAIT && ds == DUEL_ON) {
        ck_israeli = true;
        game_new(MODE_NET);
        return;
    }  // (2 BOARDS: the Israeli rules on both)
    if (C.mode == MODE_NET && (C.g == G_PLAY || C.g == G_WAIT) && ds == DUEL_GONE) {
        to_title();
        snprintf(s_note, sizeof s_note, "%s", T("YOUR FRIEND LEFT", "החבר יצא"));
        s_note_until = now + 4000;
        return;
    }
    uint8_t b[64];
    int n = sizeof b;
    while (duel_recv(b, &n)) {
        if (b[0] == NM_MOVE && n >= 4 && n <= (int)sizeof s_net_b && C.g == G_PLAY && C.mode == MODE_NET) {
            memcpy(s_net_b, b, n);
            s_net_n = n;
        }
        n = sizeof b;
    }
    // (the move of the friend waits until this board is ready for it: it can come while a move slides)
    if (s_net_n && C.g == G_PLAY && C.mode == MODE_NET && !A.on && !PRE.on && !s_crown.on && C.pos.side != C.my_side) {
        for (int i = 0; i < C.nlegal; i++) {
            const ckmove_t *m = &C.legal[i];
            if (m->from == s_net_b[1] && m->to == s_net_b[2] && m->ncap == s_net_b[3] &&
                !memcmp(m->caps, s_net_b + 4, m->ncap)) {
                PRE.on = true;
                PRE.m = *m;
                PRE.t = 0;
                cam_move(m);
                fx(FX_PICK, 60, 0.05f);
                break;
            }
        }
        s_net_n = 0;
    }
}

// ---------------------------------------------------------------- the drawing
static inline uint16_t mix16(uint16_t d, uint16_t s, int a)  // a 0..16 (the byte order of the frame)
{
    uint32_t dd = (uint16_t)(d >> 8 | d << 8), ss = (uint16_t)(s >> 8 | s << 8);
    dd = (dd | dd << 16) & 0x07E0F81F;
    ss = (ss | ss << 16) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * (uint32_t)a * 2) >> 5)) & 0x07E0F81F;
    r = (r | (r >> 16)) & 0xFFFF;
    return (uint16_t)(r >> 8 | r << 8);
}
static void tint(int x, int y, int w, int h, uint16_t c, int a)
{
    for (int j = 0; j < h; j++) {
        const int yy = y + j;
        if (yy < 0 || yy >= gfx_h) {
            continue;
        }
        uint16_t *d = g_fb + yy * gfx_w;
        for (int i = 0; i < w; i++) {
            const int xx = x + i;
            if (xx >= 0 && xx < gfx_w) {
                d[xx] = mix16(d[xx], c, a);
            }
        }
    }
}
// a soft glow: discs that grow fainter to the edge
static void glow(int cx, int cy, int r, uint16_t c, int a)
{
    for (int k = 0; k < 3; k++) {
        ui_circle(cx, cy, r - k * r / 4, c, a / 3);
    }
}
static int pulse255(float hz, int lo, int hi)
{
    const float s = fsin((float)(ck_now_ms() % 100000) / 1000.0f * hz * 6.2832f);
    return lo + (int)((hi - lo) * (0.5f + 0.5f * s));
}
// a piece picture at the screen point (cx, cy): its shadow, then the picture (sx, sy: the size; a16 0..16; lift: px up)
static void piece_draw(int pc, float cx, float cy, float sx, float sy, int a16, float lift)
{
    const int k = (pc > 0 ? 0 : 2) + (pc == 2 || pc == -2 ? 1 : 0);
    if (!s_pc[k] || a16 <= 0) {
        return;
    }
    const float sc = (sx + sy) / 2;
    if (lift < 1) {
        ui_circle((int)(cx + 1), (int)(cy + 2), (int)(PS * 0.45f * sc), rgb(0, 0, 0), 70 * a16 / 16);
    } else {
        // (a piece in the air: a soft shadow that grows fainter and wider with the height)
        int sa = 56 - (int)(lift * 0.9f);
        if (sa < 22) {
            sa = 22;
        }
        const int r = (int)(PS * (0.42f + lift * 0.004f) * sc), x = (int)(cx + 1 + lift * 0.25f),
                  y = (int)(cy + 2 + lift * 0.7f);
        for (int k = 0; k < 3; k++) {
            ui_circle(x, y, r - k * r / 6, rgb(0, 0, 0), sa * a16 / 48);
        }
    }
    cy -= lift;
    if (fabsf(sx - 1) < 0.02f && fabsf(sy - 1) < 0.02f) {
        const int x0 = (int)lroundf(cx) - PS / 2, y0 = (int)lroundf(cy) - PS / 2 - 1;
        for (int j = 0; j < PS; j++) {
            const int yy = y0 + j;
            if (yy < 0 || yy >= gfx_h) {
                continue;
            }
            uint16_t *d = g_fb + yy * gfx_w;
            const uint16_t *c = s_pc[k] + j * PS;
            const uint8_t *a = s_pa[k] + j * PS;
            for (int i = 0; i < PS; i++) {
                const int xx = x0 + i;
                if (xx < 0 || xx >= gfx_w || !a[i]) {
                    continue;
                }
                const int aa = a[i] * a16 / 16;
                d[xx] = aa >= 15 ? c[i] : mix16(d[xx], c[i], aa + (aa > 7));
            }
        }
        return;
    }
    // (another size: a bilinear filter, the colours weighted by the alpha)
    const int w = (int)(PS * sx + 0.5f), h = (int)(PS * sy + 0.5f);
    if (w < 2 || h < 2) {
        return;
    }
    const int x0 = (int)lroundf(cx - w / 2.0f), y0 = (int)lroundf(cy - h / 2.0f) - 1;
    const int fxs = (int)(65536.0f / sx), fys = (int)(65536.0f / sy);
    const uint8_t *AL = s_pa[k];
    const uint16_t *CO = s_pc[k];
    for (int j = 0; j < h; j++) {
        const int yy = y0 + j;
        if (yy < 0 || yy >= gfx_h) {
            continue;
        }
        int v = fys / 2 + j * fys - 32768;
        if (v < 0) {
            v = 0;
        }
        int vy = v >> 16, wy = (v >> 8) & 255;
        if (vy >= PS - 1) {
            vy = PS - 2;
            wy = 255;
        }
        uint16_t *d = g_fb + yy * gfx_w;
        int u = fxs / 2 - 32768;
        for (int i = 0; i < w; i++, u += fxs) {
            const int xx = x0 + i;
            if (xx < 0 || xx >= gfx_w) {
                continue;
            }
            const int uu = u < 0 ? 0 : u;
            int ux = uu >> 16, wx = (uu >> 8) & 255;
            if (ux >= PS - 1) {
                ux = PS - 2;
                wx = 255;
            }
            const int o = vy * PS + ux;
            const int w00 = (256 - wx) * (256 - wy), w01 = wx * (256 - wy), w10 = (256 - wx) * wy, w11 = wx * wy;
            const int q00 = AL[o] * w00, q01 = AL[o + 1] * w01, q10 = AL[o + PS] * w10, q11 = AL[o + PS + 1] * w11;
            const int qs = q00 + q01 + q10 + q11;  // (alpha 0..15 x 65536)
            if (qs < 32768) {
                continue;
            }
            const uint16_t c0 = CO[o], c1 = CO[o + 1], c2 = CO[o + PS], c3 = CO[o + PS + 1];
            const uint32_t s0 = (uint16_t)(c0 >> 8 | c0 << 8), s1 = (uint16_t)(c1 >> 8 | c1 << 8),
                           s2 = (uint16_t)(c2 >> 8 | c2 << 8), s3 = (uint16_t)(c3 >> 8 | c3 << 8);
            // (32-bit sums: 63 x 15 x 65536 is less than 2^31)
            const int r =
                    ((int)(s0 >> 11) * q00 + (int)(s1 >> 11) * q01 + (int)(s2 >> 11) * q10 + (int)(s3 >> 11) * q11) /
                    qs;
            const int g = ((int)((s0 >> 5) & 63) * q00 + (int)((s1 >> 5) & 63) * q01 + (int)((s2 >> 5) & 63) * q10 +
                           (int)((s3 >> 5) & 63) * q11) /
                          qs;
            const int b =
                    ((int)(s0 & 31) * q00 + (int)(s1 & 31) * q01 + (int)(s2 & 31) * q10 + (int)(s3 & 31) * q11) / qs;
            const uint16_t cc = (uint16_t)(r << 11 | g << 5 | b), cs = (uint16_t)(cc >> 8 | cc << 8);
            const int aa = (qs >> 16) * a16 / 16;  // (0..15)
            d[xx] = aa >= 15 ? cs : mix16(d[xx], cs, aa + (aa > 7));
        }
    }
}
// (RND) the table under the board: a dark green baize with a soft light in the middle, a wood frame round the board and
// its soft shadow; made one time at enter (gfx_w x gfx_h in PSRAM)
static void table_make(void)
{
    const int D = gfx_w, W = 8 * SQ, F = SQ / 4 + 2;  // F: the width of the frame
    s_table = ck_alloc((size_t)D * D * 2);
    if (!s_table) {
        return;
    }
    const float R = D / 2.0f;
    uint32_t h = 0x9E3779B9u;
    for (int y = 0; y < D; y++) {
        for (int x = 0; x < D; x++) {
            const float dx = x + 0.5f - R, dy = y + 0.5f - R, r2 = (dx * dx + dy * dy) / (R * R);
            if (r2 > 1.0f) {
                s_table[y * D + x] = 0;
                continue;
            }
            h = h * 1664525u + 1013904223u;
            const float n = ((h >> 24) & 15) / 15.0f - 0.5f;  // (the grain of the cloth)
            float k = 1.06f - 0.55f * r2;
            // (the box of the board with its frame: in it the wood; out of it the shadow, down and to the right)
            const int bx0 = BX - F, by0 = BY - F, bx1 = BX + W + F, by1 = BY + W + F;
            float cr = 26, cg = 64, cb = 48;
            if (x >= bx0 && x < bx1 && y >= by0 && y < by1) {
                // the frame: walnut, lighter at the top and the left edges
                const int e = x - bx0 < y - by0 ? x - bx0 : y - by0,
                          e2 = bx1 - 1 - x < by1 - 1 - y ? bx1 - 1 - x : by1 - 1 - y;
                const float g = 0.5f + 0.5f * fsin(y * 0.9f + fsin(x * 0.05f) * 4.0f);
                cr = 92 - 26 * g;
                cg = 56 - 16 * g;
                cb = 32 - 10 * g;
                if (e < 2) {
                    cr *= 1.35f;
                    cg *= 1.35f;
                    cb *= 1.35f;
                }
                if (e2 < 2) {
                    cr *= 0.6f;
                    cg *= 0.6f;
                    cb *= 0.6f;
                }
                k = 1.0f;
            } else {
                const float sx = x - 4.0f < bx0 ? bx0 - (x - 4.0f) : x - 4.0f > bx1 ? x - 4.0f - bx1 : 0;
                const float sy = y - 7.0f < by0 ? by0 - (y - 7.0f) : y - 7.0f > by1 ? y - 7.0f - by1 : 0;
                const float d = sqrtf(sx * sx + sy * sy);
                if (d < 14) {
                    k *= 0.45f + 0.55f * d / 14;
                }
                cr += n * 6;
                cg += n * 8;
                cb += n * 6;
            }
            const float a = r2 > 0.985f ? (1.0f - r2) / 0.015f : 1.0f;  // (a soft edge of the circle)
            int R8 = (int)(cr * k * a), G8 = (int)(cg * k * a), B8 = (int)(cb * k * a);
            R8 = R8 < 0 ? 0 : R8 > 255 ? 255 : R8;
            G8 = G8 < 0 ? 0 : G8 > 255 ? 255 : G8;
            B8 = B8 < 0 ? 0 : B8 > 255 ? 255 : B8;
            s_table[y * D + x] = rgb((uint8_t)R8, (uint8_t)G8, (uint8_t)B8);
        }
    }
}
static void draw_board(void)
{
    const int W = 8 * SQ;
    if (RND && s_table) {
        // (the table: only the part of each row in the circle)
        const int r = gfx_w / 2;
        for (int j = 0; j < gfx_h; j++) {
            const int dy = j - gfx_h / 2;
            int hw = r * r - dy * dy;
            if (hw <= 0) {
                continue;
            }
            hw = (int)sqrtf((float)hw) + 2;
            const int x0 = r - hw < 0 ? 0 : r - hw, x1 = r + hw > gfx_w ? gfx_w : r + hw;
            memcpy(g_fb + j * gfx_w + x0, s_table + j * gfx_w + x0, (size_t)(x1 - x0) * 2);
        }
    }
    for (int yy = 0; yy < gfx_h; yy++) {
        const int j = yy - BY;
        if (j < 0 || j >= W) {
            continue;
        }
        memcpy(g_fb + yy * gfx_w + BX, s_board_img + j * W, (size_t)W * 2);
    }
    // a thin dark frame round the board
    const uint16_t fr = RND ? rgb(22, 12, 8) : rgb(30, 18, 12);
    gfx_fill_rect(BX - 2, BY - 2, W + 4, 2, fr);
    gfx_fill_rect(BX - 2, BY + W, W + 4, 2, fr);
    gfx_fill_rect(BX - 2, BY, 2, W, fr);
    gfx_fill_rect(BX + W, BY, 2, W, fr);
    if (ROLL) {
        // (the rows out of the view: a soft shade at the edge)
        for (int k = 0; k < 9; k++) {
            if (s_cam > 0.05f) {
                tint(BX, k, W, 1, rgb(0, 0, 0), 9 - k);
            }
            if (s_cam < 8 - VR - 0.05f) {
                tint(BX, gfx_h - 1 - k, W, 1, rgb(0, 0, 0), 9 - k);
            }
        }
    }
}
// a small triangle (the edge signs of the 3.49: a piece or a square out of the view)
static void tri(int cx, int y, int w, int h, bool up, uint16_t c, int a16)
{
    for (int j = 0; j < h; j++) {
        const int half = up ? w * j / (2 * h) : w * (h - 1 - j) / (2 * h);
        tint(cx - half, y + j, 2 * half + 1, 1, c, a16);
    }
}
// the pieces that the jumps of a chain took already (they leave at the end of the move)
static bool taken_now(int sq)
{
    if (!s_npre || C.sel < 0) {
        return false;
    }
    for (int i = 0; i < C.nlegal; i++) {
        if (!cand(i)) {
            continue;
        }
        for (int k = 0; k < s_npre; k++) {
            if (C.legal[i].caps[k] == sq) {
                return true;
            }
        }
        return false;
    }
    return false;
}
static void draw_marks(void)
{
    const int me = mine_now();
    // the last move (no selection): its squares a little lighter
    if (C.has_last && !A.on && C.sel < 0) {
        int x, y;
        sq_xy(C.last.from, &x, &y);
        tint(x, y, SQ, SQ, rgb(255, 220, 120), 4);
        sq_xy(C.last.to, &x, &y);
        tint(x, y, SQ, SQ, rgb(255, 220, 120), 5);
    }
    if (!me) {
        return;
    }
    const bool must = C.nlegal && C.legal[0].ncap;
    if (C.sel < 0 || s_hint_t < 0.8f) {
        // the pieces that can move (a capture is a must: only those): a soft ring that pulses; brighter after a "no"
        const bool hint = s_hint_t < 0.8f;
        for (int i = 0; i < C.nlegal; i++) {
            bool first = true;
            for (int k = 0; k < i; k++) {
                if (C.legal[k].from == C.legal[i].from) {
                    first = false;
                }
            }
            if (!first) {
                continue;
            }
            int x, y;
            sq_xy(C.legal[i].from, &x, &y);
            const uint16_t c = must ? rgb(255, 150, 50) : rgb(110, 240, 130);
            const int a = hint ? pulse255(3, 160, 255) : must ? pulse255(1.2f, 120, 230) : pulse255(0.8f, 60, 150);
            if (must || hint) {
                glow(x + SQ / 2, y + SQ / 2, SQ / 2 + 2, c, a / 3);
            }
            ui_ring(x + SQ / 2, y + SQ / 2, SQ / 2 - 1, SQ > 40 ? 3 : 2, c, a);
            // (a capture: the piece that it takes gets a red ring)
            if (must) {
                for (int k = 0; k < C.nlegal; k++) {
                    if (C.legal[k].from != C.legal[i].from) {
                        continue;
                    }
                    sq_xy(C.legal[k].caps[0], &x, &y);
                    ui_ring(x + SQ / 2, y + SQ / 2, SQ / 2 - 2, 2, rgb(240, 60, 50), 150);
                }
            }
        }
        if (C.sel < 0) {
            return;
        }
    }
    int x, y;
    if (!s_npre && s_hint_t >= 0.8f) {
        for (int i = 0; i < C.nlegal; i++) {
            if (C.legal[i].from == C.sel) {
                continue;
            }
            sq_xy(C.legal[i].from, &x, &y);
            ui_ring(x + SQ / 2, y + SQ / 2, SQ / 2 - 1, 2, must ? rgb(255, 150, 50) : rgb(110, 240, 130), 70);
        }
    }
    sq_xy(cur_sq(), &x, &y);
    if (!D.lift && !RB.on) {
        glow(x + SQ / 2, y + SQ / 2, SQ / 2 + 3, rgb(255, 220, 90), 90);
    }
    ui_ring(x + SQ / 2, y + SQ / 2, SQ / 2, SQ > 40 ? 3 : 2, rgb(255, 225, 90), 255);
    const int pr = SQ > 40 ? 1 + pulse255(1.5f, 0, 2) : 0;
    for (int k = 0; k < s_ntg; k++) {
        sq_xy(s_tg[k].sq, &x, &y);
        const bool cap = s_tg[k].cap != 0xff, hot = s_tg[k].sq == s_pend || (D.lift && D.tgt == k);
        const uint16_t c = cap ? rgb(255, 170, 60) : rgb(110, 240, 130);
        if (hot) {
            continue;
        }
        glow(x + SQ / 2, y + SQ / 2, SQ / 3 + 2, c, 120);
        ui_circle(x + SQ / 2, y + SQ / 2, SQ / 6 + 1, c, 230);
        ui_ring(x + SQ / 2, y + SQ / 2, SQ * 3 / 8 + pr, 2, c, 200);
        if (cap) {
            sq_xy(s_tg[k].cap, &x, &y);
            tint(x, y, SQ, SQ, rgb(255, 40, 30), 3);
            ui_ring(x + SQ / 2, y + SQ / 2, SQ / 2 - 1, SQ > 40 ? 3 : 2, rgb(240, 50, 40), pulse255(1.5f, 150, 255));
        }
    }
}
// the landing that waits (the second tap) or that is under the dragged piece: the piece there as a ghost, a gold ring
static void draw_ghost(int sq)
{
    int x, y;
    sq_xy(sq, &x, &y);
    const int pc = C.pos.b[C.sel];
    piece_draw(pc, x + SQ / 2.0f, y + SQ / 2.0f, 1, 1, 8, 0);
    const int p = pulse255(2.5f, 0, 3);
    ui_ring(x + SQ / 2, y + SQ / 2, SQ / 2 + p - 1, SQ > 40 ? 4 : 3, rgb(255, 210, 60), 255);
    for (int k = 0; k < s_ntg; k++) {
        if (s_tg[k].sq != sq || s_tg[k].cap == 0xff) {
            continue;
        }
        sq_xy(s_tg[k].cap, &x, &y);
        ui_ring(x + SQ / 2, y + SQ / 2, SQ / 2 - 1, SQ > 40 ? 4 : 3, rgb(255, 40, 30), 255);
    }
}
static void draw_pieces(void)
{
    const ckpos_t *p = A.on && !A.partial ? &C.prev : &C.pos;
    const int hide = A.on ? A.m.from : (C.sel >= 0 && (s_npre || D.lift || RB.on)) ? C.sel : -1;
    const int chain = !A.on && s_npre && !D.lift && !RB.on ? cur_sq() : -1;
    for (int sq = 0; sq < 64; sq++) {
        int pc = p->b[sq];
        if (!pc || sq == hide) {
            continue;
        }
        bool gone = false;
        if (A.on && !A.partial) {
            for (int i = 0; i < A.popped; i++) {
                if (A.m.caps[i] == sq) {
                    gone = true;
                }
            }
        }
        for (int i = 0; i < s_npop; i++) {
            if (s_pop[i].sq == sq && s_pop[i].t >= 0) {
                gone = true;
            }
        }
        if (gone) {
            continue;
        }
        // (the crown: the man becomes a king when the crown lands on it)
        if (s_crown.on && s_crown.sq == sq && s_crown.t < 0.3f) {
            pc = pc > 0 ? 1 : -1;
        }
        int a16 = 16;
        if (taken_now(sq)) {
            a16 = 6;
        }
        int x, y;
        sq_xy(sq, &x, &y);
        float sx = 1, sy = 1, lift = 0;
        if (s_squash.sq == sq && s_squash.t < 0.22f) {
            const float e = (1 - s_squash.t / 0.22f) * fsin(s_squash.t / 0.22f * 6.2832f * 1.0f + 1.5708f);
            sx = 1 + 0.09f * e;
            sy = 1 - 0.09f * e;
        }
        if (sq == C.sel && !s_npre && mine_now()) {
            lift = 3;
            sx *= 1.04f;
            sy *= 1.04f;
        }
        piece_draw(pc, x + SQ / 2.0f, y + SQ / 2.0f, sx, sy, a16, lift);
        if (a16 < 16) {
            // (taken in this chain: a red cross)
            const int cx = x + SQ / 2, cy = y + SQ / 2, h = SQ / 4;
            for (int t = -1; t <= 1; t++) {
                for (int i = -h; i <= h; i++) {
                    ui_circle(cx + i, cy + i + t, 1, rgb(240, 40, 30), 255);
                    ui_circle(cx + i, cy - i + t, 1, rgb(240, 40, 30), 255);
                }
            }
        }
    }
    // the piece of a chain at its last landing
    if (chain >= 0) {
        int x, y;
        sq_xy(chain, &x, &y);
        float sx = 1, sy = 1;
        if (s_squash.sq == chain && s_squash.t < 0.22f) {
            const float e = (1 - s_squash.t / 0.22f) * fsin(s_squash.t / 0.22f * 6.2832f + 1.5708f);
            sx = 1 + 0.09f * e;
            sy = 1 - 0.09f * e;
        }
        piece_draw(C.pos.b[C.sel], x + SQ / 2.0f, y + SQ / 2.0f, sx * 1.04f, sy * 1.04f, 16, 3);
    }
    // the pieces that pop
    for (int i = 0; i < s_npop; i++) {
        const float t = s_pop[i].t;
        if (t < 0 || t > 0.22f) {
            continue;
        }
        int x, y;
        sq_xy(s_pop[i].sq, &x, &y);
        const float s = 1 + t * 1.6f;
        piece_draw(s_pop[i].pc, x + SQ / 2.0f, y + SQ / 2.0f, s, s, (int)(16 * (1 - t / 0.22f)), 0);
    }
}
static void drag_piece_xy(float *px, float *py);
// the moving pieces: the slide, the dragged piece, a piece that goes back, the move of the other side before it starts
static void draw_moving(void)
{
    if (A.on) {
        float x, y, lift, sc;
        anim_place(&x, &y, &lift, &sc);
        const int pc = A.partial ? C.pos.b[A.m.from] : C.prev.b[A.m.from];
        piece_draw(pc, BX + x, BY + y, sc, sc, 16, lift);
    }
    if (PRE.on) {
        int x, y;
        sq_xy(PRE.m.from, &x, &y);
        const float k = PRE.t < 0.25f ? PRE.t / 0.25f : 1;
        glow(x + SQ / 2, y + SQ / 2, SQ / 2 + 4, rgb(255, 200, 90), (int)(150 * k));
        ui_ring(x + SQ / 2, y + SQ / 2, SQ / 2, SQ > 40 ? 3 : 2, rgb(255, 225, 120), (int)(255 * k));
    }
    if (RB.on) {
        float tx, ty;
        sq_bxy(RB.sq, &tx, &ty);
        const float f = RB.t / 0.18f > 1 ? 1 : RB.t / 0.18f, e = f * f * (3 - 2 * f);
        piece_draw(C.pos.b[RB.sq], BX + RB.x + (tx - RB.x) * e, BY + RB.y + (ty - RB.y) * e, 1.08f - 0.08f * e,
                   1.08f - 0.08f * e, 16, 6 * (1 - e));
    }
    if (D.lift && C.sel >= 0) {
        if (D.tgt >= 0 && D.tgt < s_ntg) {
            draw_ghost(s_tg[D.tgt].sq);
        }
        float px, py;
        drag_piece_xy(&px, &py);
        piece_draw(C.pos.b[C.sel], px, (float)D.y, 1.14f, 1.14f, 16, D.y - py);
    }
}
static void draw_fx(float dt_unused)
{
    (void)dt_unused;
    for (int i = 0; i < NRING; i++) {
        if (s_ring[i].t >= s_ring[i].len) {
            continue;
        }
        const float f = s_ring[i].t / s_ring[i].len;
        ui_ring(BX + (int)s_ring[i].x, BY + (int)s_ring[i].y, (int)(s_ring[i].r0 + (s_ring[i].r1 - s_ring[i].r0) * f),
                SQ > 40 ? 3 : 2, s_ring[i].c, (int)(200 * (1 - f)));
    }
    for (int i = 0; i < NPART; i++) {
        if (s_part[i].life <= 0) {
            continue;
        }
        const float f = s_part[i].life / s_part[i].max;
        const int x = s_part[i].scr ? (int)s_part[i].x : BX + (int)s_part[i].x,
                  y = s_part[i].scr ? (int)s_part[i].y : BY + (int)s_part[i].y;
        if (s_part[i].scr) {
            gfx_fill_rect(x, y, s_part[i].r + 1, s_part[i].r * 2, s_part[i].c);
        } else {
            ui_circle(x, y, s_part[i].r, s_part[i].c, (int)(255 * (f > 0.5f ? 1 : f * 2)));
        }
    }
    // the crown: it falls on the man, a flash, sparks; the man is a king from then on
    if (s_crown.on) {
        int x, y;
        sq_xy(s_crown.sq, &x, &y);
        const float t = s_crown.t, cx = x + SQ / 2.0f;
        float cy = y + SQ / 2.0f - SQ * 0.15f;
        if (t < 0.3f) {
            cy -= (1 - t / 0.3f) * (1 - t / 0.3f) * SQ * 1.3f;
        }
        const float a = t < 0.3f ? 1 : t > 0.9f ? 0 : 1 - (t - 0.3f) / 0.6f;
        if (a > 0) {
            const int w = (int)(SQ * (t < 0.3f ? 0.62f : 0.62f - 0.3f * (t - 0.3f))), h = w * 6 / 10;
            const int x0 = (int)cx - w / 2, y0 = (int)cy - h / 2;
            glow((int)cx, (int)cy, w * 3 / 4, rgb(255, 210, 80), (int)(160 * a));
            const uint16_t g1 = rgb(255, 205, 60), g2 = rgb(200, 140, 30);
            if (a > 0.3f) {
                gfx_fill_rect(x0, y0 + h * 6 / 10, w, h * 4 / 10 + 1, g2);
                gfx_fill_rect(x0 + 1, y0 + h * 6 / 10, w - 2, h * 3 / 10, g1);
                gfx_spike(x0, y0 + h / 10, w * 4 / 10, h * 6 / 10, g1);
                gfx_spike(x0 + w * 3 / 10, y0, w * 4 / 10, h * 7 / 10, g1);
                gfx_spike(x0 + w * 6 / 10, y0 + h / 10, w * 4 / 10, h * 6 / 10, g1);
                ui_circle(x0 + w / 5, y0 + h / 10, w / 12 + 1, rgb(255, 245, 200), 255);
                ui_circle(x0 + w / 2, y0, w / 12 + 1, rgb(255, 245, 200), 255);
                ui_circle(x0 + w * 4 / 5, y0 + h / 10, w / 12 + 1, rgb(255, 245, 200), 255);
            }
        }
    }
}
// THE SMALL BOARD of the 3.49 (the whole game; the frame of the rows in the view)
static void mm_xy(int sq, int *x, int *y)
{
    int r = sq >> 3, c = sq & 7;
    if (view_flip()) {
        r = 7 - r;
        c = 7 - c;
    }
    *x = MM_X + c * MM_S;
    *y = MM_Y + r * MM_S;
}
static void mm_piece(int pc, int cx, int cy, int r)
{
    const bool l = pc > 0;
    ui_circle(cx, cy + 1, r, rgb(0, 0, 0), 90);
    ui_circle(cx, cy, r, l ? rgb(200, 182, 150) : rgb(140, 20, 28), 255);
    ui_circle(cx, cy - 1, r - 2, l ? rgb(252, 242, 220) : rgb(232, 52, 52), 255);
    if (pc == 2 || pc == -2) {
        ui_circle(cx, cy - 1, r / 2, rgb(255, 205, 60), 255);
    }
}
static void draw_minimap(void)
{
    const uint16_t lt = rgb(226, 196, 150), dk = rgb(118, 74, 44);
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            gfx_fill_rect(MM_X + c * MM_S, MM_Y + r * MM_S, MM_S, MM_S, (r + c) & 1 ? dk : lt);
        }
    }
    const ckpos_t *p = A.on && !A.partial ? &C.prev : &C.pos;
    int x, y;
    if (C.has_last && !A.on) {
        mm_xy(C.last.from, &x, &y);
        tint(x, y, MM_S, MM_S, rgb(255, 220, 120), 6);
        mm_xy(C.last.to, &x, &y);
        tint(x, y, MM_S, MM_S, rgb(255, 220, 120), 7);
    }
    const int chain = !A.on && s_npre && C.sel >= 0 ? C.sel : -1;
    if (chain >= 0) {
        mm_xy(cur_sq(), &x, &y);
        mm_piece(C.pos.b[chain], x + MM_S / 2, y + MM_S / 2, MM_S / 2 - 1);
    }
    for (int sq = 0; sq < 64; sq++) {
        if (!p->b[sq] || (A.on && sq == A.m.from) || sq == chain) {
            continue;
        }
        bool gone = false;
        if (A.on && !A.partial) {
            for (int i = 0; i < A.popped; i++) {
                if (A.m.caps[i] == sq) {
                    gone = true;
                }
            }
        }
        if (gone) {
            continue;
        }
        mm_xy(sq, &x, &y);
        mm_piece(p->b[sq], x + MM_S / 2, y + MM_S / 2, MM_S / 2 - 1);
    }
    if (A.on) {
        float bx, by, l, s;
        anim_place(&bx, &by, &l, &s);
        const int pc = A.partial ? C.pos.b[A.m.from] : C.prev.b[A.m.from];
        int ax = MM_X + (int)(bx * MM_S / SQ), ay = MM_Y + (int)(by * MM_S / SQ);
        mm_piece(pc, ax, ay - (int)(l * MM_S / SQ), MM_S / 2);
    }
    // the pieces that can move (a ring), the selected one (gold), the move of the other side (gold)
    if (mine_now()) {
        const bool must = C.nlegal && C.legal[0].ncap;
        if (!s_npre) {
            for (int i = 0; i < C.nlegal; i++) {
                mm_xy(C.legal[i].from, &x, &y);
                ui_ring(x + MM_S / 2, y + MM_S / 2, MM_S / 2, 2, must ? rgb(255, 150, 50) : rgb(110, 240, 130),
                        must         ? 255
                        : C.sel >= 0 ? 90
                                     : 170);
            }
        }
        if (C.sel >= 0) {
            mm_xy(cur_sq(), &x, &y);
            ui_ring(x + MM_S / 2, y + MM_S / 2, MM_S / 2 + 1, 2, rgb(255, 225, 90), 255);
            for (int k = 0; k < s_ntg; k++) {
                mm_xy(s_tg[k].sq, &x, &y);
                ui_circle(x + MM_S / 2, y + MM_S / 2, MM_S / 4 + 1,
                          s_tg[k].cap != 0xff ? rgb(255, 170, 60) : rgb(110, 240, 130), 255);
            }
        }
    }
    if (PRE.on) {
        mm_xy(PRE.m.from, &x, &y);
        ui_ring(x + MM_S / 2, y + MM_S / 2, MM_S / 2 + 1, 2, rgb(255, 225, 90), pulse255(3, 120, 255));
    }
    // the frame of the view
    const int fy = MM_Y + (int)lroundf(s_cam * MM_S), fh = (int)lroundf(VR * MM_S);
    gfx_frame_rect(MM_X - 2, fy - 2, 8 * MM_S + 4, fh + 4, 2, rgb(255, 255, 255));
    gfx_frame_rect(MM_X - 3, fy - 3, 8 * MM_S + 6, fh + 6, 1, rgb(40, 40, 50));
}
static int count(int s)
{
    int n = 0;
    for (int i = 0; i < 64; i++) {
        n += C.pos.b[i] * s > 0;
    }
    return n;
}
// the buttons of this screen: their boxes, ids
enum {
    BT_CPU,
    BT_TWO,
    BT_INVITE,
    BT_JOIN,
    BT_CANCEL,
    BT_UNDO
};
typedef struct {
    int16_t x, y, w, h;
} rect_t;
static rect_t s_btn[6];
static int s_btn_id[6], s_nbtn;
static void button(int id, int x, int y, int w, int h, const char *label, uint16_t col)
{
    ui_rrect(x, y + 2, w, h, 10, rgb(12, 10, 14));
    ui_rrect(x, y, w, h, 10, col);
    ui_rrect(x + 3, y + 3, w - 6, h / 2 - 3, 7, ui_mix(col, rgb(255, 255, 255), 50));
    ui_rrect(x + 3, y + h / 2, w - 6, h / 2 - 3, 7, col);
    const kfont_t *f = ui_text_w(&KF_TEXT, label) <= w - 10 ? &KF_TEXT : &KF_LABEL;
    const int base = y + h / 2 + (f == &KF_TEXT ? 7 : 5);
    ui_text_c(f, x + w / 2 + 1, base + 1, label, rgb(20, 16, 24));
    ui_text_c(f, x + w / 2, base, label, rgb(255, 255, 255));
    if (s_nbtn < 6) {
        s_btn[s_nbtn] = (rect_t){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
        s_btn_id[s_nbtn++] = id;
    }
}
static int button_at(int x, int y)
{
    for (int i = 0; i < s_nbtn; i++) {
        const rect_t r = s_btn[i];
        if (x >= r.x - 6 && x < r.x + r.w + 6 && y >= r.y - 6 && y < r.y + r.h + 6) {
            return s_btn_id[i];
        }
    }
    return -1;
}
static uint8_t s_inv[3][6];
static int s_ninv;
// a text in a box: one line, or two (cut at the space nearest to the middle) when it is too wide
static void info_text(const kfont_t *f, int cx, int y, int w, const char *s, uint16_t col)
{
    if (ui_text_w(f, s) <= w) {
        ui_text_c(f, cx, y, s, col);
        return;
    }
    int n = (int)strlen(s), cut = -1;
    for (int i = 1; i < n; i++) {
        if (s[i] == ' ' && (cut < 0 || abs(i - n / 2) < abs(cut - n / 2))) {
            cut = i;
        }
    }
    char a[48], b[48];
    if (cut < 0) {
        cut = n / 2;
    }
    snprintf(a, sizeof a, "%.*s", cut, s);
    snprintf(b, sizeof b, "%s", s + cut + (s[cut] == ' '));
    ui_text_c(&KF_LABEL, cx, y - 6, a, col);
    ui_text_c(&KF_LABEL, cx, y + 9, b, col);
}
// the line of the state: whose move, a capture is a must, tap again, the result
static const char *state_text(uint16_t *col)
{
    *col = rgb(255, 236, 190);
    if (C.g == G_OVER) {
        *col = rgb(255, 214, 90);
        return C.result;
    }
    const int side = A.on && !A.partial ? C.prev.side : C.pos.side;
    if (mine_now() && s_pend >= 0) {
        *col = rgb(255, 214, 90);
        return T("TAP AGAIN", "הקישו שוב");
    }
    if (C.mode == MODE_TWO) {
        return side > 0 ? T("LIGHT MOVES", "תור הלבנים") : T("RED MOVES", "תור האדומים");
    }
    if (side == C.my_side) {
        if (C.nlegal && C.legal[0].ncap && mine_now()) {
            *col = rgb(255, 160, 80);
            return T("CAPTURE!", "חובה ללכוד!");
        }
        return T("YOUR MOVE", "התור שלכם");
    }
    *col = rgb(190, 205, 235);
    return C.mode == MODE_CPU ? T("COMPUTER", "המחשב") : T("YOUR FRIEND", "החבר");
}
// the 3.49 panel: the state, the small board, the counts (a glow on the side that moves)
static void draw_panel(void)
{
    ui_rrect(PX0, 2, PX1 - PX0, gfx_h - 4, 12, rgb(34, 28, 34));
    uint16_t col;
    const char *t = state_text(&col);
    const int cx = (PX0 + PX1) / 2;
    char b[48];
    if ((C.g == G_PLAY && !mine_now() && !A.on && C.mode != MODE_TWO) && (s_ai || PRE.on || C.mode == MODE_NET)) {
        // (the other side thinks: dots that run)
        const int n = (int)(ck_now_ms() / 300 % 4);
        snprintf(b, sizeof b, "%s%.*s", t, n, "...");
        t = b;
    }
    const kfont_t *f = ui_text_w(&KF_TEXT, t) <= PX1 - PX0 - 10 ? &KF_TEXT : &KF_NAME;
    ui_text_c(f, cx + 1, 24, t, rgb(10, 8, 12));
    ui_text_c(f, cx, 23, t, col);
    draw_minimap();
    // the counts
    for (int s = 0; s < 2; s++) {
        const int side = s ? -1 : 1, yy = MM_Y + 22 + s * 64;
        const bool now = C.g == G_PLAY && C.pos.side == side;
        if (now) {
            glow(CNT_X, yy, 22, side > 0 ? rgb(255, 240, 200) : rgb(255, 80, 70), pulse255(1.2f, 90, 200));
        }
        mm_piece(side, CNT_X, yy, 13);
        snprintf(b, sizeof b, "%d", count(side));
        ui_text_c(&KF_BIG, CNT_X, yy + 40, b, now ? rgb(255, 255, 255) : rgb(170, 170, 185));
    }
}
// the edge signs of the 3.49: a piece that can move or a square of the selected piece out of the view
static void draw_edges(void)
{
    if (!ROLL || !mine_now()) {
        return;
    }
    bool up[8] = {0}, dn[8] = {0};
    const bool must = C.nlegal && C.legal[0].ncap;
    if (C.sel < 0) {
        for (int i = 0; i < C.nlegal; i++) {
            const int r = scr_row(C.legal[i].from), c = view_flip() ? 7 - (C.legal[i].from & 7) : C.legal[i].from & 7;
            if (r + 0.6f < s_cam) {
                up[c] = true;
            } else if (r + 0.4f > s_cam + VR) {
                dn[c] = true;
            }
        }
    } else {
        for (int k = 0; k < s_ntg; k++) {
            const int r = scr_row(s_tg[k].sq), c = view_flip() ? 7 - (s_tg[k].sq & 7) : s_tg[k].sq & 7;
            if (r + 0.6f < s_cam) {
                up[c] = true;
            } else if (r + 0.4f > s_cam + VR) {
                dn[c] = true;
            }
        }
    }
    const uint16_t c = must || C.sel >= 0 ? rgb(255, 170, 60) : rgb(120, 240, 140);
    const int a = pulse255(1.5f, 8, 16), bob = pulse255(1.5f, 0, 3);
    for (int i = 0; i < 8; i++) {
        const int x = BX + i * SQ + SQ / 2;
        if (up[i]) {
            tri(x, 2 + bob, 26, 12, true, rgb(0, 0, 0), 8);
            tri(x, 1 + bob, 22, 10, true, c, a);
        }
        if (dn[i]) {
            tri(x, gfx_h - 14 - bob, 26, 12, false, rgb(0, 0, 0), 8);
            tri(x, gfx_h - 12 - bob, 22, 10, false, c, a);
        }
    }
}
static void draw_info(void)
{
    const int cx = (INFO_X0 + INFO_X1) / 2, w = INFO_X1 - INFO_X0;
    uint16_t col;
    const char *t = state_text(&col);
    char tb[48];
    if (C.g == G_PLAY && !mine_now() && !A.on && C.mode == MODE_CPU && (s_ai || PRE.on)) {
        snprintf(tb, sizeof tb, "%s%.*s", t, (int)(ck_now_ms() / 300 % 4), "...");
        t = tb;
    }
    const kfont_t *f = ui_text_w(&KF_TEXT, t) <= w ? &KF_TEXT : &KF_LABEL;
    int y = NARROW ? 26 : BIG ? 44 : 30;
    if (ui_text_w(f, t) > w) {
        // (a narrow column: two lines)
        char a[48], b[48];
        const char *sp = strchr(t, ' ');
        if (sp) {
            snprintf(a, sizeof a, "%.*s", (int)(sp - t), t);
            snprintf(b, sizeof b, "%s", sp + 1);
        } else {
            snprintf(a, sizeof a, "%s", t);
            b[0] = 0;
        }
        ui_text_c(&KF_LABEL, cx, y, a, col);
        ui_text_c(&KF_LABEL, cx, y + 16, b, col);
        y += 16;
    } else {
        ui_text_c(f, cx, y, t, col);
    }
    if (C.g == G_OVER) {
        info_text(&KF_LABEL, cx, y + 22, w, C.why, rgb(200, 205, 215));
    }
    // the pieces left: an icon and a count for each side (a glow on the side that moves)
    const int iy = NARROW ? 104 : BIG ? 136 : 96, io = BIG ? 28 : 14;
    for (int s = 0; s < 2; s++) {
        const int side = s ? -1 : 1, ix = NARROW ? cx : INFO_X0 + w / 4 + s * w / 2;
        const int yy = NARROW ? iy + s * 40 : iy;
        if (C.g == G_PLAY && C.pos.side == side) {
            glow(ix - io, yy, BIG ? 30 : 17, side > 0 ? rgb(255, 240, 200) : rgb(255, 80, 70), pulse255(1.2f, 70, 170));
        }
        piece_draw(side, (float)(ix - io), (float)yy, 1, 1, 16, 0);
        char b[8];
        snprintf(b, sizeof b, "%d", count(side));
        ui_text_c(BIG ? &KF_BIG : &KF_TEXT, ix + io, yy + (BIG ? 11 : 7), b, rgb(240, 240, 245));
    }
    if (C.mode == MODE_CPU && C.g == G_PLAY && s_nhist >= 2 && !A.on && !PRE.on && turn_side()) {
        if (NARROW) {
            button(BT_UNDO, INFO_X0 + 4, gfx_h - 46, w - 8, 40, T("UNDO", "ביטול"), rgb(90, 100, 120));
        } else if (BIG) {
            button(BT_UNDO, INFO_X0 + 20, 236, w - 40, 60, T("UNDO", "ביטול"), rgb(90, 100, 120));
        }
    }
    if (BIG && C.g == G_PLAY) {
        static const char *const LV[3][2] = {{"EASY", "קל"}, {"MEDIUM", "בינוני"}, {"HARD", "קשה"}};
        char b[64];
        if (C.mode == MODE_CPU) {
            snprintf(b, sizeof b, "%s  -  %s", LV[C.level][s_lang],
                     ck_israeli ? T("ISRAELI RULES", "חוקי דמקה") : T("ENGLISH RULES", "חוקים אנגליים"));
        } else {
            snprintf(b, sizeof b, "%s",
                     ck_israeli ? T("ISRAELI RULES", "חוקי דמקה") : T("ENGLISH RULES", "חוקים אנגליים"));
        }
        ui_text_c(&KF_LABEL, cx, 200, b, rgb(170, 180, 200));
    }
}
// (RND) the round info: the state on the top arc, the pieces left on the side arcs (light at the left, red at the right),
// UNDO on the bottom arc
static void draw_info_round(void)
{
    const int cx = gfx_w / 2, u = gfx_w, F = SQ / 4 + 2;
    uint16_t col;
    const char *t = state_text(&col);
    char tb[48];
    if (C.g == G_PLAY && !mine_now() && !A.on && C.mode == MODE_CPU && (s_ai || PRE.on)) {
        snprintf(tb, sizeof tb, "%s%.*s", t, (int)(ck_now_ms() / 300 % 4), "...");
        t = tb;
    }
    static const kfont_t *const FT[] = {&KF_BIG, &KF_TEXT, &KF_LABEL};
    const int ty = BY - F - u * 16 / 466;
    // (the biggest font that fits; ui_text_w only: the ABI of the app files has no ui_text_fit)
    const kfont_t *f = FT[2];
    for (int k = 0; k < 3; k++) {
        if (ui_text_w(FT[k], t) <= u * 220 / 466) {
            f = FT[k];
            break;
        }
    }
    ui_text_c(f, cx + 1, ty + 1, t, rgb(8, 10, 8));
    ui_text_c(f, cx, ty, t, col);
    for (int s = 0; s < 2; s++) {
        const int side = s ? -1 : 1, ix = s ? BX + 8 * SQ + F + (BX - F) / 2 : (BX - F) / 2,
                  iy = gfx_h / 2 - u * 22 / 466;
        if (C.g == G_PLAY && C.pos.side == side) {
            glow(ix, iy, SQ / 2, side > 0 ? rgb(255, 240, 200) : rgb(255, 80, 70), pulse255(1.2f, 70, 170));
        }
        piece_draw(side, (float)ix, (float)iy, 1, 1, 16, 0);
        char b[8];
        snprintf(b, sizeof b, "%d", count(side));
        ui_text_c(&KF_BIG, ix, iy + SQ / 2 + u * 30 / 466, b, rgb(240, 240, 245));
    }
    if (C.mode == MODE_CPU && C.g == G_PLAY && s_nhist >= 2 && !A.on && !PRE.on && turn_side()) {
        const int bw = u * 160 / 466, bh = u * 46 / 466;
        button(BT_UNDO, cx - bw / 2, BY + 8 * SQ + F + u * 4 / 466, bw, bh, T("UNDO", "ביטול"), rgb(90, 100, 120));
    }
}
static void draw_title(void)
{
    const int cx = gfx_w / 2;
    if (ROLL) {
        // the demo game on the board at the left; the name on it; the buttons in the panel
        tint(BX, 0, 8 * SQ, gfx_h, rgb(0, 0, 0), 4);
        const char *name = T("CHECKERS", "דמקה");
        const int nw = ui_text_w(&KF_TITLE, name) + 44, nx = BX + 10;
        tint(nx, 10, nw, 52, rgb(14, 8, 6), 12);
        ui_text_c(&KF_TITLE, nx + nw / 2 + 2, 52, name, rgb(40, 20, 10));
        ui_text_c(&KF_TITLE, nx + nw / 2, 50, name, rgb(255, 214, 120));
        ui_rrect(PX0, 2, PX1 - PX0, gfx_h - 4, 12, rgb(34, 28, 34));
        const int bw = PX1 - PX0 - 8, bh = 50, x = PX0 + 4;
        button(BT_CPU, x, 6, bw, bh, T("VS COMPUTER", "נגד המחשב"), rgb(46, 150, 80));
        button(BT_TWO, x, 61, bw, bh, T("2 PLAYERS", "2 שחקנים"), rgb(196, 112, 36));
        if (s_ninv) {
            button(BT_JOIN, x, 116, bw, bh, T("JOIN YOUR FRIEND", "הצטרפו לחבר"), rgb(48, 112, 210));
        } else {
            button(BT_INVITE, x, 116, bw, bh, T("2 BOARDS", "2 לוחות"), rgb(48, 112, 210));
        }
        if (s_note[0] && ck_now_ms() < s_note_until) {
            const int w = ui_text_w(&KF_TEXT, s_note) + 30, bx = BX + 4 * SQ - w / 2;
            ui_rrect(bx, gfx_h - 50, w, 36, 10, rgb(120, 40, 36));
            ui_text_c(&KF_TEXT, BX + 4 * SQ, gfx_h - 26, s_note, rgb(255, 230, 220));
        }
        return;
    }
    // the board (the demo game), dim, and a plate with the name and the buttons
    tint(0, 0, gfx_w, gfx_h, rgb(0, 0, 0), 9);
    int pw = NARROW ? gfx_w - 40 : BIG ? 360 : 300, ph = NARROW ? 200 : BIG ? 240 : 164;
    if (RND) {
        pw = gfx_w * 300 / 466;
        ph = gfx_w * 252 / 466;
    }  // (the round: a plate in the middle of the circle)
    const int px = (gfx_w - pw) / 2, py = (gfx_h - ph) / 2;
    ui_rrect(px, py, pw, ph, 14, rgb(28, 22, 18));
    ui_rrect(px + 3, py + 3, pw - 6, ph - 6, 12, rgb(46, 34, 26));
    // (a note, as YOUR FRIEND LEFT: in the place of the name for a few seconds)
    if (s_note[0] && ck_now_ms() < s_note_until) {
        ui_text_c(&KF_TEXT, cx, py + (RND ? gfx_w * 42 / 466 : 30), s_note, rgb(255, 180, 150));
    } else if (RND) {
        ui_text_c(&KF_TITLE, cx, py + gfx_w * 48 / 466, T("CHECKERS", "דמקה"), rgb(255, 228, 170));
    } else {
        ui_text_c(&KF_BIG, cx, py + 32, T("CHECKERS", "דמקה"), rgb(255, 228, 170));
    }
    const int bw = pw - 40,
              bh = RND      ? gfx_w * 50 / 466
                   : NARROW ? 38
                   : BIG    ? 50
                            : 34,
              gap = RND      ? gfx_w * 10 / 466
                    : NARROW ? 8
                    : BIG    ? 10
                             : 6;
    int by = py + (RND ? gfx_w * 66 / 466 : BIG ? 52 : 44);
    button(BT_CPU, px + 20, by, bw, bh, T("VS COMPUTER", "נגד המחשב"), rgb(70, 140, 80));
    by += bh + gap;
    button(BT_TWO, px + 20, by, bw, bh, T("2 PLAYERS", "2 שחקנים"), rgb(150, 100, 50));
    by += bh + gap;
    if (s_ninv) {
        button(BT_JOIN, px + 20, by, bw, bh, T("JOIN YOUR FRIEND", "הצטרפו לחבר"), rgb(60, 110, 190));
    } else {
        button(BT_INVITE, px + 20, by, bw, bh, T("2 BOARDS", "2 לוחות"), rgb(60, 110, 190));
    }
}
static void draw_wait(void)
{
    if (ROLL) {
        tint(BX, 0, 8 * SQ, gfx_h, rgb(0, 0, 0), 8);
        const int bc = BX + 4 * SQ;
        ui_rrect(bc - 190, 40, 380, 92, 14, rgb(28, 22, 30));
        ui_text_c(&KF_BIG, bc, 76, T("WAITING FOR A FRIEND", "מחכים לחבר"), rgb(255, 236, 190));
        ui_text_c(&KF_TEXT, bc, 112, T("THE OTHER BOARD: CHECKERS > JOIN", "בלוח השני: דמקה > הצטרפו"),
                  rgb(200, 205, 215));
        ui_rrect(PX0, 2, PX1 - PX0, gfx_h - 4, 12, rgb(34, 28, 34));
        button(BT_CANCEL, PX0 + 4, 56, PX1 - PX0 - 8, 60, T("CANCEL", "ביטול"), rgb(170, 60, 50));
        return;
    }
    tint(0, 0, gfx_w, gfx_h, rgb(0, 0, 0), 10);
    const int cx = gfx_w / 2, cy = gfx_h / 2;
    ui_text_c(&KF_TEXT, cx, cy - 30, T("WAITING FOR A FRIEND", "מחכים לחבר"), rgb(255, 236, 190));
    ui_text_c(&KF_LABEL, cx, cy - 10, T("ON THE OTHER BOARD: CHECKERS > JOIN", "בלוח השני: דמקה > הצטרפו"),
              rgb(200, 205, 215));
    button(BT_CANCEL, cx - 70, cy + 8, 140, 38, T("CANCEL", "ביטול"), rgb(150, 60, 50));
}
// the end of a game on the 3.49: a plate over the board
static void draw_over(void)
{
    const int bc = BX + 4 * SQ, a = C.gt < 0.4f ? (int)(C.gt / 0.4f * 16) : 16;
    if (a < 4) {
        return;
    }
    tint(BX, 0, 8 * SQ, gfx_h, rgb(0, 0, 0), a / 3);
    const int w = 330, h = 116, x = bc - w / 2, y = (gfx_h - h) / 2;
    ui_rrect(x, y + 3, w, h, 16, rgb(8, 6, 10));
    ui_rrect(x, y, w, h, 16, C.win > 0 ? rgb(60, 44, 20) : C.win < 0 ? rgb(44, 30, 40) : rgb(36, 36, 48));
    ui_rrect(x + 3, y + 3, w - 6, h - 6, 14,
             C.win > 0   ? rgb(84, 62, 26)
             : C.win < 0 ? rgb(60, 40, 54)
                         : rgb(50, 50, 66));
    ui_text_c(&KF_TITLE, bc + 2, y + 48, C.result, rgb(20, 12, 8));
    ui_text_c(&KF_TITLE, bc, y + 46, C.result, C.win >= 0 ? rgb(255, 214, 90) : rgb(235, 200, 220));
    ui_text_c(&KF_TEXT, bc, y + 74, C.why, rgb(225, 225, 235));
    if (C.gt > 1.0f) {
        ui_text_c(&KF_TEXT, bc, y + 102, T("TAP FOR THE TITLE", "הקישו למסך הראשי"),
                  ui_mix(rgb(160, 165, 180), rgb(255, 255, 255), pulse255(1, 0, 255)));
    }
}
static void render(int64_t now)
{
    (void)now;
    s_nbtn = 0;
    gfx_fill(rgb(24, 22, 26));
    if (!s_board_img) {
        ui_text_c(&KF_LABEL, gfx_w / 2, gfx_h / 2, "NO MEMORY", rgb(255, 200, 180));
        return;
    }
    const int bx0 = BX, by0 = BY;
    if (ROLL) {
        BY = -(int)lroundf(s_cam * SQ);
    }
    if (s_shake > 0.3f) {
        BX += (int)lroundf(s_shake * fsin((float)(ck_now_ms() % 10000) * 0.09f));
    }
    draw_board();
    draw_marks();
    draw_pieces();
    if (mine_now() && s_pend >= 0 && C.sel >= 0) {
        draw_ghost(s_pend);
    }
    draw_moving();
    draw_fx(0);
    draw_edges();
    BX = bx0;
    if (!ROLL) {
        BY = by0;
    }
    if (C.g == G_TITLE) {
        draw_title();
        return;
    }
    if (C.g == G_WAIT) {
        draw_wait();
        return;
    }
    if (ROLL || RND) {
        if (ROLL) {
            draw_panel();
        } else {
            draw_info_round();
        }
        if (C.g == G_OVER) {
            draw_over();
        }
        if (RND && C.g == G_PLAY && mine_now()) {
            draw_loupe();
        } else if (RND) {
            s_lp_on = false;
        }
        return;
    }
    draw_info();
    if (C.g == G_OVER && C.gt > 1.0f) {
        info_text(&KF_LABEL, (INFO_X0 + INFO_X1) / 2, gfx_h - 16, INFO_X1 - INFO_X0,
                  T("TAP FOR THE TITLE", "הקישו למסך הראשי"), rgb(200, 205, 215));
    }
    if (C.g == G_PLAY && mine_now()) {
        draw_loupe();
    } else {
        s_lp_on = false;
    }
}

// ---------------------------------------------------------------- the touch
// the distance (px) from a frame point to the middle of a square
static float dpx(int sq, float x, float y)
{
    int sx, sy;
    sq_xy(sq, &sx, &sy);
    const float dx = x - (sx + SQ / 2.0f), dy = y - (sy + SQ / 2.0f);
    return sqrtf(dx * dx + dy * dy);
}
// a piece that the person sees on the square (the chain piece shows at its last landing)
static bool shown_piece(int sq)
{
    if (s_npre && C.sel >= 0) {
        if (sq == C.sel) {
            return false;
        }
        if (sq == cur_sq()) {
            return true;
        }
    }
    return C.pos.b[sq] != 0;
}
// THE LOUPE (the 320 x 240 frame only: 3.6 mm squares, so a loupe helps): 3 x 3 squares from (s_lp_sx, s_lp_sy)
// at LP_Z times, at (s_lp_x, s_lp_y) on the side of the frame away from the tap; the board pixels come from the frame
#define LP_N 3
#define LP_Z 2
static int s_lp_sx, s_lp_sy, s_lp_x, s_lp_y;
static uint16_t *s_lp_buf;
static void loupe_open(int x, int y)
{
    const int n = LP_N * SQ, w = n * LP_Z;
    int sx = x - n / 2, sy = y - n / 2;
    if (sx < BX) {
        sx = BX;
    }
    if (sy < BY) {
        sy = BY;
    }
    if (sx > BX + 8 * SQ - n) {
        sx = BX + 8 * SQ - n;
    }
    if (sy > BY + 8 * SQ - n) {
        sy = BY + 8 * SQ - n;
    }
    s_lp_sx = sx;
    s_lp_sy = sy;
    s_lp_x = x < gfx_w / 2 ? gfx_w - w - 4 : 4;
    s_lp_y = (gfx_h - w) / 2;
    if (RND) {
        // (the round: a round lens in the half of the circle away from the tap)
        const int rr = w / 2 + 8, off = gfx_w / 2 - rr - 6;
        s_lp_x = (x < gfx_w / 2 ? gfx_w / 2 + off : gfx_w / 2 - off) - w / 2;
    }
    s_lp_on = true;
    s_pend = -1;
    fx(FX_PICK, 55, 0.05f);
}
static bool in_loupe(int x, int y)
{
    const int w = LP_N * SQ * LP_Z;
    if (RND) {
        const int rr = w / 2 + 8, dx = x - (s_lp_x + w / 2), dy = y - (s_lp_y + w / 2);
        return dx * dx + dy * dy <= rr * rr;
    }
    return x >= s_lp_x && x < s_lp_x + w && y >= s_lp_y && y < s_lp_y + w;
}
static void draw_loupe(void)
{
    if (!s_lp_on) {
        return;
    }
    const int n = LP_N * SQ, w = n * LP_Z;
    if (!s_lp_buf) {
        s_lp_buf = heap_caps_malloc((size_t)n * n * 2, MALLOC_CAP_SPIRAM);
    }
    if (!s_lp_buf) {
        return;
    }
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            const int fx0 = s_lp_sx + c, fy0 = s_lp_sy + r;
            s_lp_buf[r * n + c] = fx0 >= 0 && fx0 < gfx_w && fy0 >= 0 && fy0 < gfx_h ? g_fb[fy0 * gfx_w + fx0] : 0;
        }
    }
    if (RND) {
        // (the round lens: the zoom in a circle, a gold ring round it)
        const int rr = w / 2 + 8, lcx = s_lp_x + w / 2, lcy = s_lp_y + w / 2;
        ui_circle(lcx + 2, lcy + 4, rr + 4, rgb(0, 0, 0), 120);
        for (int dy = -rr; dy <= rr; dy++) {
            const int yy = lcy + dy, hw = (int)sqrtf((float)(rr * rr - dy * dy));
            if (yy < 0 || yy >= gfx_h) {
                continue;
            }
            uint16_t *d = g_fb + yy * gfx_w;
            for (int dx = -hw; dx <= hw; dx++) {
                const int xx = lcx + dx, c = xx - s_lp_x, r = yy - s_lp_y;
                if (xx < 0 || xx >= gfx_w) {
                    continue;
                }
                d[xx] = c >= 0 && c < w && r >= 0 && r < w ? s_lp_buf[(r / LP_Z) * n + c / LP_Z] : rgb(30, 30, 38);
            }
        }
        ui_ring(lcx, lcy, rr, 3, rgb(255, 214, 90), 255);
        return;
    }
    ui_rbox(s_lp_x - 4, s_lp_y - 4, w + 8, w + 8, 8, rgb(30, 30, 38), rgb(255, 214, 90), 2);
    for (int r = 0; r < w; r++) {
        uint16_t *d = g_fb + (s_lp_y + r) * gfx_w + s_lp_x;
        const uint16_t *src = s_lp_buf + (r / LP_Z) * n;
        for (int c = 0; c < w; c++) {
            d[c] = src[c / LP_Z];
        }
    }
}
// a tap in the loupe: the board point under it, then the landing or the piece of that square
static void loupe_tap(int x, int y);
static void tap_board(int x, int y)
{
    if (!mine_now()) {
        return;
    }
    const float RH = 0.9f * SQ, MARGIN = MARGIN_MM / MMPX;
    // the nearest landing of the selected piece, and the nearest other thing (a landing, a piece of any side)
    int L = -1;
    float dL = 1e9f, dO = 1e9f;
    int O = -1;
    for (int k = 0; k < s_ntg; k++) {
        const float d = dpx(s_tg[k].sq, x, y);
        if (d < dL) {
            dL = d;
            L = k;
        }
    }
    for (int k = 0; k < s_ntg; k++) {
        if (k != L) {
            const float d = dpx(s_tg[k].sq, x, y);
            if (d < dO) {
                dO = d;
            }
        }
    }
    for (int sq = 0; sq < 64; sq++) {
        if (!shown_piece(sq)) {
            continue;
        }
        const float d = dpx(sq, x, y);
        if (d < dO) {
            dO = d;
            O = sq;
        }
    }
    if (L >= 0 && dL <= RH && dL < dO) {
        const int X = s_tg[L].sq;
        // (clearly on it: the move at once; else a ghost, and a second tap on it, 250 ms or more later)
        // (the second tap: on the ghost; on big squares also 3 mm nearer to it than to each other piece and square. The
        // squares of the 320 x 240 frame (3.6 mm) are too small for that: there the nearest is enough, as before)
        const bool big_sq = SQ * MMPX >= 6.0f;
        if (dO - dL >= MARGIN ||
            (s_pend == X && ck_now_ms() - s_pend_ms >= 250 && (!big_sq || dO - dL >= CONFIRM_MM / MMPX))) {
            choose(X, false, 0, 0);
            return;
        }
        if ((NARROW || RND) && !big_sq) {
            loupe_open(x, y);
            return;
        }  // (the small squares: the loupe)
        if (s_pend != X) {
            s_pend = X;
            s_pend_ms = ck_now_ms();
            fx(FX_PICK, 55, 0.05f);
        }
        return;
    }
    if (s_npre > 0) {
        // (a chain: a tap on the square where the piece started takes its jumps back)
        if (dpx(C.sel, x, y) <= RH && dpx(C.sel, x, y) <= dO + 1) {
            s_npre = 0;
            s_pend = -1;
            targets();
            cam_sel();
            fx(FX_PICK, 60, 0.05f);
        }
        return;
    }
    // a piece of this side that the person aims at
    const bool on_piece = O >= 0 && dO <= 0.5f * SQ;
    if (on_piece && C.pos.b[O] * mine_now() > 0 && !movable(O)) {
        // (a piece that cannot move: "no", and the pieces that can move flash)
        fx(FX_BAD, 60, 0);
        s_hint_t = 0;
        s_pend = -1;
        cam_turn();
        return;
    }
    if (on_piece && C.pos.b[O] * mine_now() < 0) {
        // (a tap on a piece of the other side that a landing takes: that landing waits for a second tap)
        int best = -1;
        float bd = 1e9f;
        for (int k = 0; k < s_ntg; k++) {
            if (s_tg[k].cap == O) {
                const float d = dpx(s_tg[k].sq, x, y);
                if (d < bd) {
                    bd = d;
                    best = k;
                }
            }
        }
        if (best >= 0) {
            if (s_pend != s_tg[best].sq) {
                s_pend = s_tg[best].sq;
                s_pend_ms = ck_now_ms();
                fx(FX_PICK, 55, 0.05f);
            }
            return;
        }
    }
    // the nearest piece that can move (up to 1 square away: the snap)
    int P = -1;
    float dP = 1.0f * SQ;
    for (int i = 0; i < C.nlegal; i++) {
        const float d = dpx(C.legal[i].from, x, y);
        if (d < dP) {
            dP = d;
            P = C.legal[i].from;
        }
    }
    if (P >= 0) {
        if (P != C.sel) {
            C.sel = P;
            s_npre = 0;
            targets();
            fx(FX_PICK, 70, 0.05f);
            cam_sel();
        }
        s_pend = -1;
        return;
    }
    // nothing near: the ghost goes; then the selection (not when it is the only piece that can move)
    if (s_pend >= 0) {
        s_pend = -1;
        return;
    }
    bool one = true;
    for (int i = 1; i < C.nlegal; i++) {
        if (C.legal[i].from != C.legal[0].from) {
            one = false;
        }
    }
    if (!one) {
        C.sel = -1;
        s_ntg = 0;
    }
}
static void undo(void)
{
    // back to the last position where it was my move (one move of the computer and one of mine)
    if (A.on) {
        return;
    }
    PRE.on = false;
    while (s_nhist > 0) {
        C.pos = s_hist[--s_nhist];
        if (C.pos.side == C.my_side) {
            break;
        }
    }
    s_gen++;
    C.has_last = false;
    fx_clear();
    refresh_legal();
    cam_turn();
    fx(FX_PICK, 60, 0);
}
static void do_button(int id)
{
    switch (id) {
    case BT_CPU:
        ck_israeli = store_get("ck_rules", 0) == 0;
        game_new(MODE_CPU);
        fx(FX_PICK, 70, 0);
        break;
    case BT_TWO:
        ck_israeli = store_get("ck_rules", 0) == 0;
        game_new(MODE_TWO);
        fx(FX_PICK, 70, 0);
        break;
    case BT_INVITE:
        duel_invite();
        C.g = G_WAIT;
        voice(0, 18, 1800, 1500, W_TRI, 14);
        break;
    case BT_JOIN:
        if (s_ninv) {
            duel_join(s_inv[0]);
            C.g = G_WAIT;
            voice(0, 18, 1800, 1500, W_TRI, 14);
        }
        break;
    case BT_CANCEL:
        duel_stop();
        C.g = G_TITLE;
        voice(0, 18, 1800, 1500, W_TRI, 14);
        break;
    case BT_UNDO:
        undo();
        break;
    }
}
static bool on_board(int x, int y)
{
    return x >= BX - 4 && x < BX + 8 * SQ + 4 && y >= (ROLL ? 0 : BY - 4) && y < (ROLL ? gfx_h : BY + 8 * SQ + 4);
}
static bool in_map(int x, int y)
{
    return ROLL && x >= MM_X - 6 && x < MM_X + 8 * MM_S + 4 && y >= MM_Y - 8 && y < gfx_h;
}
static void map_to(int y)
{
    cam_to((y - MM_Y) / (float)MM_S - VR / 2);
}
// a piece that can move under the finger (the piece of a chain only)
static int pick_at(int x, int y)
{
    const float R = (ROLL ? 0.62f : 0.9f) * SQ;  // (the small squares: the nearest piece that can move, as a tap)
    if (s_npre) {
        return dpx(cur_sq(), x, y) <= R ? cur_sq() : -1;
    }
    int best = -1;
    float bd = R;
    for (int i = 0; i < C.nlegal; i++) {
        const float d = dpx(C.legal[i].from, x, y);
        if (d < bd) {
            bd = d;
            best = C.legal[i].from;
        }
    }
    return best;
}
// the dragged piece rides above the finger (the thumb does not hide it): the height grows with the drag, so that the
// small move of a tap does not carry the piece; its middle now
static float drag_travel(void)
{
    const float dx = (float)(D.x - D.x0), dy = (float)(D.y - D.y0);
    return sqrtf(dx * dx + dy * dy);
}
static void drag_piece_xy(float *px, float *py)
{
    const float full = SQ * (ROLL ? 0.42f : 0.55f), t = drag_travel() * 0.8f;
    *px = (float)D.x;
    *py = D.y - (t < full ? t : full);
}
// the landing under the dragged piece: the nearest one, up to 0.85 square; the one now stays until another is 0.15
// square nearer. No landing before the finger moved half a square (a tap that slid a little).
static void drag_snap(void)
{
    float px, py;
    drag_piece_xy(&px, &py);
    if (drag_travel() < 0.5f * SQ) {
        D.tgt = -1;
        return;
    }
    int best = -1;
    float bd = 0.85f * SQ;
    for (int k = 0; k < s_ntg; k++) {
        const float d = dpx(s_tg[k].sq, px, py);
        if (d < bd) {
            bd = d;
            best = k;
        }
    }
    if (D.tgt >= 0 && D.tgt < s_ntg && best != D.tgt) {
        const float dc = dpx(s_tg[D.tgt].sq, px, py);
        if (dc <= 0.95f * SQ && (best < 0 || dc - bd < 0.15f * SQ)) {
            best = D.tgt;
        }
    }
    if (best != D.tgt && best >= 0) {
        fx(FX_SLIDE, 35, 0.1f);
    }
    D.tgt = best;
}
static void drag_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        memset(&D, 0, sizeof D);
        D.on = true;
        D.tgt = -1;
        D.x = D.x0 = ev->x;
        D.y = D.y0 = ev->y;
        if (in_map(ev->x, ev->y) && (C.g == G_PLAY || C.g == G_OVER)) {
            D.kind = DK_MAP;
            map_to(ev->y);
            return;
        }
        if (!on_board(ev->x, ev->y)) {
            return;
        }
        if (mine_now()) {
            const int sq = pick_at(ev->x, ev->y);
            if (sq >= 0) {
                D.kind = DK_PIECE;
                D.sq = sq;
                return;
            }
        }
        if (ROLL && (C.g == G_PLAY || C.g == G_OVER)) {
            D.kind = DK_VIEW;
            D.cam0 = s_cam;
        }
        return;
    }
    if (!D.on) {
        return;
    }
    if (ev->kind == IN_MOVE || !D.lift) {
        D.x = ev->x;
        D.y = ev->y;
    }
    if (ev->kind == IN_MOVE) {
        if (!D.moved && abs(D.x - D.x0) + abs(D.y - D.y0) > DRAG_PX) {
            D.moved = true;
            if (D.kind != DK_NONE) {
                input_tap_cancel();
            }
            if (D.kind == DK_PIECE) {
                if (!mine_now()) {
                    D.kind = DK_NONE;
                    return;
                }
                if (D.sq != cur_sq()) {
                    C.sel = D.sq;
                    s_npre = 0;
                    targets();
                }
                s_pend = -1;
                D.lift = true;
                fx(FX_PICK, 70, 0.05f);
            }
        }
        if (D.kind == DK_VIEW && D.moved) {
            float t = D.cam0 - (D.y - D.y0) / (float)SQ;
            cam_clamp(&t);
            s_cam = s_cam_to = t;
        }
        if (D.kind == DK_MAP) {
            map_to(D.y);
        }
        if (D.lift) {
            drag_snap();
        }
        return;
    }
    if (ev->kind == IN_UP) {
        if (D.lift) {
            // (the drop goes where the ghost showed at the last move of the finger: what the person saw)
            float px, py;
            drag_piece_xy(&px, &py);
            D.lift = false;
            if (D.tgt >= 0 && D.tgt < s_ntg && mine_now()) {
                choose(s_tg[D.tgt].sq, true, px - BX, py - BY);
            } else if (drag_travel() < 0.5f * SQ) {
                RB.on = true;
                RB.sq = cur_sq();
                RB.x = px - BX;
                RB.y = py - BY;
                RB.t = 0.1f;
            }  // (a tap that slid: the piece stays selected)
            else {
                // (no legal square: the piece goes back)
                RB.on = true;
                RB.sq = cur_sq();
                RB.x = px - BX;
                RB.y = py - BY;
                RB.t = 0;
                fx(FX_BAD, 55, 0);
            }
        }
        D.on = false;
        D.kind = DK_NONE;
    }
}
static void loupe_tap(int x, int y)
{
    s_lp_on = false;
    if (!in_loupe(x, y) || !mine_now()) {
        return;
    }
    const float bx = s_lp_sx + (x - s_lp_x) / (float)LP_Z, by = s_lp_sy + (y - s_lp_y) / (float)LP_Z;
    // (in the loupe a square is 7.2 mm: the square under the finger is the choice)
    for (int k = 0; k < s_ntg; k++) {
        if (dpx(s_tg[k].sq, bx, by) <= 0.5f * SQ) {
            choose(s_tg[k].sq, false, 0, 0);
            return;
        }
    }
    for (int i = 0; i < C.nlegal; i++) {
        if (dpx(C.legal[i].from, bx, by) <= 0.5f * SQ) {
            if (C.legal[i].from != C.sel) {
                C.sel = C.legal[i].from;
                s_npre = 0;
                targets();
                fx(FX_PICK, 70, 0.05f);
                cam_sel();
            }
            return;
        }
    }
}
static void input_ev(const input_event_t *ev)
{
    if (s_lp_on) {
        // (the loupe: no drag; a tap in it chooses, a tap out of it closes it)
        if (ev->kind == IN_TAP) {
            loupe_tap(ev->x, ev->y);
        }
        return;
    }
    if (ev->kind == IN_DOWN || ev->kind == IN_MOVE || ev->kind == IN_UP) {
        drag_input(ev);
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    const int id = button_at(ev->x, ev->y);
    if (id >= 0) {
        do_button(id);
        return;
    }
    if (C.g == G_OVER && C.gt > 1.0f) {
        to_title();
        return;
    }
    if (in_map(ev->x, ev->y)) {
        return;  // (the touch moved the view)
    }
    if (C.g == G_PLAY) {
        tap_board(ev->x, ev->y);
    }
}
// a slow tap (a press of 350 - 900 ms) on the title and the end acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    input_ev(ev);
    if (slow && (C.g == G_TITLE || C.g == G_OVER)) {
        input_ev(&tap);
    }
}

// ---------------------------------------------------------------- the panel
enum {
    PI_LEVEL,
    PI_RULES,
    PI_SIDE,
    PI_LANG,
    PI_SND,
    PI_NEW,
    PI_TITLE,
    PI_UNDO
};
static const char *const LV_V[] = {"EASY", "קל", "MEDIUM", "בינוני", "HARD", "קשה"};
static const char *const RULES_V[] = {"ISRAELI", "ישראליים", "ENGLISH", "אנגליים"};
static const char *const SIDE_V[] = {"LIGHT", "לבנים", "RED", "אדומים"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const pitem_t ITEMS[] = {
        {PI_ACT, PI_UNDO, 0, 0, {"UNDO", "ביטול מהלך"}, NULL, NULL},
        {PI_PICK, PI_LEVEL, 3, 1, {"LEVEL", "רמה"}, LV_V, "ck_lvl"},
        {PI_PICK, PI_RULES, 2, 0, {"RULES", "חוקים"}, RULES_V, "ck_rules"},
        {PI_PICK, PI_SIDE, 2, 0, {"YOU PLAY", "אתם משחקים"}, SIDE_V, "ck_side"},
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "ck_lang"},
        {PI_PICK, PI_SND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, "ck_snd"},
        {PI_ACT2, PI_NEW, 0, 0, {"NEW GAME", "משחק חדש"}, NULL, NULL},
        {PI_ACT, PI_TITLE, 0, 0, {"THE TITLE", "המסך הראשי"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == PI_UNDO) {
        return ROLL && C.g == G_PLAY && C.mode == MODE_CPU && s_nhist >= 2;
    }
    if (id == PI_NEW) {
        return C.g == G_PLAY && C.mode != MODE_NET;
    }
    if (id == PI_TITLE) {
        return C.g != G_TITLE;
    }
    if (id == PI_RULES) {
        return !(C.g == G_PLAY && C.mode == MODE_NET);
    }
    return true;
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_UNDO:
        if (C.g == G_PLAY && C.mode == MODE_CPU) {
            undo();
        }
        break;
    case PI_LEVEL:
        C.level = v % 3;
        break;
    case PI_RULES:
        // (a new rule: a new game when one is on, so that the moves stay legal)
        ck_israeli = v == 0;
        if (C.g == G_PLAY) {
            game_new(C.mode);
        } else if (C.g == G_TITLE || C.g == G_WAIT) {
            const int g = C.g;
            game_new(MODE_DEMO);
            C.g = g;
        }
        break;
    case PI_SIDE:
        if (C.g == G_PLAY && C.mode == MODE_CPU) {
            game_new(MODE_CPU);
        }
        break;
    case PI_LANG:
        s_lang = v & 1;
        break;
    case PI_SND:
        s_snd = v == 0;
        break;
    case PI_NEW:
        game_new(C.mode);
        break;
    case PI_TITLE:
        to_title();
        break;
    }
}
static const pitems_t PANEL = {ITEMS, sizeof ITEMS / sizeof ITEMS[0], item_shown, NULL, item_set, NULL, NULL};

// ---------------------------------------------------------------- the app
static int64_t s_last;
static void enter(int64_t now)
{
    s_last = now;
    NARROW = gfx_w < 400;
    ROLL = !NARROW && gfx_h < 240;
    BIG = false;
    RND = false;
#if BOARD_ROUND
    RND = gfx_round();
    if (RND) {
        NARROW = ROLL = false;
    }
#endif
    if (RND) {
        SQ = gfx_w >= 440 ? 39 : 34;
        BX = BY = (gfx_w - 8 * SQ) / 2;
        INFO_X0 = 0;
        INFO_X1 = gfx_w;
    } else if (NARROW) {
        SQ = 28;
        BX = gfx_w - 8 * SQ - 6;
        BY = (gfx_h - 8 * SQ) / 2;
        INFO_X0 = 2;
        INFO_X1 = BX - 6;
    } else if (ROLL) {
        SQ = 54;
        BX = 4;
        BY = 0;
        INFO_X0 = PX0;
        INFO_X1 = PX1;
    } else {
        SQ = 28;
        BX = 12;
        BY = 12;
        INFO_X0 = 8 * 42 + 28;
        INFO_X1 = gfx_w - 8;
    }
#if BOARD_UI_TABLET
    BIG = !NARROW && !ROLL;
    MMPX = 0.173f;
#elif BOARD_NATIVE_W && !BOARD_ROUND_ZOOM  // (an AMOLED panel that scales the frame of the apps)
    MMPX = BOARD_FRAME_MM_PX;
#else
    MMPX = ROLL ? 0.133f : 0.128f;
#endif
    if (RND) {
        MMPX = gfx_w >= 440 ? 0.095f : 0.089f;  // (the round frame: the 1.75C 0.095 mm, the Watcher 0.089 mm)
    }
    VR = gfx_h / (float)SQ;
    s_lang = (int)store_get("ck_lang", 1) & 1;
    s_snd = store_get("ck_snd", 0) == 0;
    if (s_snd) {
        JPLAY(CK_OPEN);  // (the opening obeys SOUND OFF)
    }
    C.level = (int)(store_get("ck_lvl", 1) % 3);
    ck_israeli = store_get("ck_rules", 0) == 0;
    if (!art_load(SQ)) {
        free(s_art_mem);
        s_art_mem = NULL;
        s_board_img = NULL;
        ESP_LOGE(TAG, "the art did not load");
    }
#if BOARD_UI_TABLET
    else if (BIG && !art_grow(42)) {
        free(s_art_mem);
        s_art_mem = NULL;
        s_board_img = NULL;
        ESP_LOGE(TAG, "no memory for the big art");
    }
#endif
    if (ROLL) {
        VR = gfx_h / (float)SQ;
    }
    if (RND && !s_table) {
        table_make();
    }
    tfsfx_load_mem(CK_SFX_DATA, CK_SFX_LEN, FXN, FX_N < CK_SFX_N ? FX_N : CK_SFX_N, 46);  // (in the app file)
    s_ai = 0;
    s_ai_busy = false;
    s_nhist = 0;
    s_note[0] = 0;
    s_hint_t = 9;
    memset(&D, 0, sizeof D);
    C.g = G_TITLE;
    game_new(MODE_DEMO);
    s_cam = s_cam_to = (8 - VR) / 2;
    cam_clamp(&s_cam);
    cam_clamp(&s_cam_to);
    duel_open('K');
    sysui_panel_items(&PANEL);
#ifdef ESP_PLATFORM
    if (!s_ai_task) {
        s_ai_quit = false;
        xTaskCreatePinnedToCoreWithCaps(ai_task, "checkers", 65536, NULL, 2, &s_ai_task, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
#endif
}
static void leave(void)
{
    s_lp_on = false;
    if (s_lp_buf) {
        heap_caps_free(s_lp_buf);
        s_lp_buf = NULL;
    }
#ifdef ESP_PLATFORM
    for (int k = 0; k < 300 && (s_ai == 1 || s_ai_busy); k++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_ai_task) {
        s_ai_quit = true;
        vTaskDelay(pdMS_TO_TICKS(60));
        vTaskDeleteWithCaps(s_ai_task);
        s_ai_task = NULL;
    }
#endif
    duel_close();
    tfsfx_free();
    free(s_art_mem);
    s_art_mem = NULL;
    s_board_img = NULL;
    free(s_table);
    s_table = NULL;
    for (int i = 0; i < 4; i++) {
        s_pc[i] = NULL;
        s_pa[i] = NULL;
    }
}
static void fx_update(float dt)
{
    for (int i = 0; i < s_npop; i++) {
        s_pop[i].t += dt;
    }
    while (s_npop > 0 && s_pop[0].t > 0.3f && !A.on) {
        s_npop--;
        memmove(s_pop, s_pop + 1, s_npop * sizeof s_pop[0]);
    }
    for (int i = 0; i < NRING; i++) {
        if (s_ring[i].t < s_ring[i].len) {
            s_ring[i].t += dt;
        }
    }
    for (int i = 0; i < NPART; i++) {
        if (s_part[i].life <= 0) {
            continue;
        }
        s_part[i].life -= dt;
        s_part[i].x += s_part[i].vx * dt;
        s_part[i].y += s_part[i].vy * dt;
        s_part[i].vy += s_part[i].g * dt;
        if (!s_part[i].scr) {
            s_part[i].vx *= 1 - dt * 2.5f;
        } else if (s_part[i].y > gfx_h + 4) {
            s_part[i].life = 0;
        }
    }
    if (s_crown.on) {
        const float t0 = s_crown.t;
        s_crown.t += dt;
        if (t0 < 0.3f && s_crown.t >= 0.3f) {
            // (the crown lands: a flash, gold sparks, the sound)
            float x, y;
            sq_bxy(s_crown.sq, &x, &y);
            ring_add(x, y, SQ * 0.3f, SQ * 0.95f, 0.4f, rgb(255, 215, 90));
            for (int i = 0; i < 16; i++) {
                const float a = i * 0.3927f, v = SQ * (1.5f + frnd() * 1.5f);
                part_add(x, y - SQ * 0.1f, fcos(a) * v, fsin(a) * v - SQ, 0.5f + frnd() * 0.35f, SQ * 4,
                         i & 1 ? rgb(255, 240, 160) : rgb(255, 196, 50), 2 + (SQ > 40), false);
            }
            squash(s_crown.sq);
            snd_crown();
        }
        if (s_crown.t > 0.95f) {
            s_crown.on = false;
        }
    }
    if (s_squash.sq >= 0) {
        s_squash.t += dt;
    }
    s_shake *= 1 - dt * 9 > 0 ? 1 - dt * 9 : 0;
    if (s_hint_t < 9) {
        s_hint_t += dt;
    }
}
static void update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    C.gt += dt;
    if (A.on) {
        anim_update(dt);
    }
    if (RB.on) {
        RB.t += dt;
        if (RB.t >= 0.18f) {
            RB.on = false;
            squash(RB.sq);
            fx(FX_PLACE, 50, 0.05f);
        }
    }
    // the move of the other side: the camera goes there, the piece glows, then it moves
    if (PRE.on) {
        PRE.t += dt;
        const bool there = !ROLL || fabsf(s_cam - s_cam_to) < 0.08f;
        if (PRE.t >= (C.mode == MODE_DEMO ? 0.4f : 0.32f) && (there || PRE.t > 0.9f)) {
            PRE.on = false;
            float x, y;
            sq_bxy(PRE.m.from, &x, &y);
            const ckmove_t m = PRE.m;
            play_move(&m, false, 0, x, y);
        }
    }
    // the computer
    if (s_ai == 2) {
        const int min_ms = C.mode == MODE_DEMO ? 900 : 450;
        if (s_ai_got != s_gen || !ai_turn()) {
            s_ai = 0;  // (an answer for an older position)
        } else if (ck_now_ms() - s_ai_t0 >= min_ms && !A.on && !PRE.on && !s_crown.on) {
            s_ai = 0;
            if (s_ai_move.from != s_ai_move.to) {
                for (int i = 0; i < C.nlegal; i++) {
                    if (ck_same(&C.legal[i], &s_ai_move)) {
                        PRE.on = true;
                        PRE.m = C.legal[i];
                        PRE.t = 0;
                        cam_move(&PRE.m);
                        fx(FX_PICK, 60, 0.05f);
                        break;
                    }
                }
            }
        }
    } else if (s_ai == 0 && ai_turn() && !A.on && !PRE.on) {
        ai_start(C.mode == MODE_DEMO ? 0 : C.level);
    }
    // the demo of the title: a new game after the end of one
    if (C.mode == MODE_DEMO && s_demo_wait > 0) {
        s_demo_wait -= dt;
        if (s_demo_wait <= 0) {
            const int g = C.g;
            game_new(MODE_DEMO);
            C.g = g;
            cam_to((8 - VR) / 2);
        }
    }
    // the camera (the 3.49)
    if (ROLL) {
        if (A.on) {
            float x, y, l, s;
            anim_place(&x, &y, &l, &s);
            const float r = y / SQ;
            if (r - 0.85f < s_cam_to) {
                cam_to(r - 0.85f);
            } else if (r + 0.85f > s_cam_to + VR) {
                cam_to(r + 0.85f - VR);
            }
        }
        if (D.lift) {
            // (a dragged piece at the top or the bottom edge: the view goes that way)
            float t = s_cam_to;
            if (D.y < 30) {
                t -= dt * 3.2f * (30 - D.y) / 30.0f;
            } else if (D.y > gfx_h - 30) {
                t += dt * 3.2f * (D.y - (gfx_h - 30)) / 30.0f;
            }
            cam_clamp(&t);
            if (t != s_cam_to) {
                s_cam_to = t;
                s_cam = t;
                drag_snap();
            }
        }
        const float k = dt * 9 > 1 ? 1 : dt * 9;
        s_cam += (s_cam_to - s_cam) * k;
        if (fabsf(s_cam - s_cam_to) < 0.002f) {
            s_cam = s_cam_to;
        }
    }
    fx_update(dt);
    net_poll(now);
    s_ninv = duel_invites(s_inv, 3);
}
// THE ICON: a corner of the board (3 x 3 squares). A red man jumps over a light man (the light man pops), a crown
// falls on it and it is a king; then it slides back, and the light man grows again (a loop of 2 s with no jump).
static void icon_piece(int cx, int cy, int r, bool red, bool king)
{
    gfx_fill_circle(cx + 1, cy + r / 4 + 1, r, rgb(40, 22, 12));
    gfx_fill_circle(cx, cy + r / 5, r, red ? rgb(140, 18, 26) : rgb(190, 172, 140));
    gfx_fill_circle(cx, cy, r, red ? rgb(226, 44, 46) : rgb(250, 240, 214));
    gfx_ring(cx, cy, r * 7 / 10, r * 7 / 10 - (r > 12 ? 2 : 1), red ? rgb(180, 28, 34) : rgb(214, 198, 166));
    if (r > 6) {
        gfx_fill_circle(cx - r / 3, cy - r / 3, r / 5 + 1, red ? rgb(255, 120, 110) : rgb(255, 255, 250));
    }
    if (king) {
        gfx_fill_circle(cx, cy, r * 4 / 10, rgb(255, 200, 60));
    }
    if (king && r > 8) {
        gfx_fill_circle(cx, cy, r / 5, rgb(255, 240, 170));
    }
}
static void icon(int x, int y, int s)
{
    const float t = icon_time();
    const float u = t - 2.0f * (float)(int)(t / 2.0f);
    const int q = s / 3, pr = q * 38 / 100;
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 3; c++) {
            const int w = c == 2 ? s - 2 * q : q, h = r == 2 ? s - 2 * q : q;
            const bool dark = ((r + c) & 1) == 0;
            gfx_fill_rect(x + c * q, y + r * q, w, h, dark ? rgb(112, 66, 38) : rgb(236, 206, 150));
            gfx_fill_rect(x + c * q, y + r * q + h / 3, w, 1 + q / 40, dark ? rgb(98, 56, 32) : rgb(222, 190, 134));
        }
    }
    // the places: the red starts at (2, 0), jumps over (1, 1) to (0, 2)
    const float ax = x + q * 0.5f, ay = y + q * 2.5f, bx = x + q * 2.5f, by = y + q * 0.5f, mx = x + q * 1.5f,
                my = y + q * 1.5f;
    // the light man: normal, pops at the middle of the jump, grows again at the end of the loop
    if (u < 0.36f) {
        icon_piece((int)mx, (int)my, pr, false, false);
    } else if (u < 0.5f) {
        const float f = (u - 0.36f) / 0.14f;
        icon_piece((int)mx, (int)my, (int)(pr * (1 + 0.35f * f)), false, false);
    } else if (u < 0.75f) {
        // (the sparks of the pop)
        const float f = (u - 0.5f) / 0.25f;
        for (int i = 0; i < 6; i++) {
            const float a = i * 1.0472f;
            gfx_fill_circle((int)(mx + fcos(a) * q * (0.3f + 0.5f * f)), (int)(my + fsin(a) * q * (0.3f + 0.5f * f)),
                            (int)(q / 14 * (1 - f)) + 1, i & 1 ? rgb(255, 250, 230) : rgb(255, 210, 90));
        }
    } else if (u > 1.75f) {
        icon_piece((int)mx, (int)my, (int)(pr * (u - 1.75f) / 0.25f), false, false);
    }
    // the red: the jump (0 .. 0.7 s), the crown (0.7 .. 1.0), a rest, the slide back (1.4 .. 1.75)
    float rx, ry, rs = 1;
    bool king = false;
    if (u < 0.7f) {
        const float f = u / 0.7f, e = f * f * (3 - 2 * f), h = fsin(f * 3.14159f);
        rx = ax + (bx - ax) * e;
        ry = ay + (by - ay) * e - h * q * 0.45f;
        rs = 1 + 0.18f * h;
    } else if (u < 1.4f) {
        rx = bx;
        ry = by;
        king = u >= 0.95f;
    } else if (u < 1.75f) {
        const float f = (u - 1.4f) / 0.35f, e = f * f * (3 - 2 * f);
        rx = bx + (ax - bx) * e;
        ry = by + (ay - by) * e;
        king = f < 0.5f;
    } else {
        rx = ax;
        ry = ay;
    }
    icon_piece((int)rx, (int)ry, (int)(pr * rs), true, king);
    // the crown that falls on it, then a ring of light
    if (u >= 0.7f && u < 0.95f) {
        const float f = (u - 0.7f) / 0.25f;
        const int w = q * 6 / 10, h = w * 6 / 10, cx = (int)bx, cy = (int)(by - q * 0.45f * (1 - f) * (1 - f)) - h / 3;
        const int x0 = cx - w / 2, y0 = cy - h / 2;
        gfx_fill_rect(x0, y0 + h * 6 / 10, w, h * 4 / 10 + 1, rgb(214, 150, 30));
        gfx_spike(x0, y0 + h / 10, w * 4 / 10, h * 6 / 10, rgb(255, 205, 60));
        gfx_spike(x0 + w * 3 / 10, y0, w * 4 / 10, h * 7 / 10, rgb(255, 205, 60));
        gfx_spike(x0 + w * 6 / 10, y0 + h / 10, w * 4 / 10, h * 6 / 10, rgb(255, 205, 60));
    }
    if (u >= 0.95f && u < 1.3f) {
        const float f = (u - 0.95f) / 0.35f;
        const int r0 = (int)(pr * (1.05f + 0.25f * f));
        gfx_ring((int)bx, (int)by, r0, r0 - 2, f < 0.5f ? rgb(255, 236, 140) : rgb(255, 200, 60));
    }
}
const app_t app_checkers = {
        .name = "CHECKERS",
        .landscape = true,
        .round = APP_ROUND_HI,  // (a round panel: the board in the middle of the circle at the panel resolution)
        .round_fit = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
};
