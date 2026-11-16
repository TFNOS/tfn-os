// CHESS: a game between two boards or against a computer, with a zoom on the 3.49. The rules and the computer: chess_engine.inc. Against the computer (EASY, MEDIUM, HARD; you play white or
// black) or a friend's board (duel.c; the board that invites plays white). A tap on your piece shows its moves (dots),
// a tap on a dot moves it; a pawn on the last rank: a choice of the piece. UNDO (against the computer: your last move
// and its answer), NEW, RESIGN. The end: checkmate, stalemate, 50 moves, the same position three times, too little
// material.
// The screens: the 3.49 board at the left (squares of 20 pixels; ZOOM: 40 pixels, a drag moves the board up and down;
// your move and the answer come into the view) and the panel at the right; the CoreS3 the board (28) and a narrow
// panel; the round boards (the 1.75C, the Watcher) the biggest board in the circle on a round table, the status over
// it, a round button at each side, the taken pieces under it (APP_ROUND_HI: all sizes from gfx_w).
// On the 3.49, physical orientation chooses the layout. Portrait shows all eight ranks at large size;
// sideways tilt reveals the files, as the view controls in KEEPER and CRUISE do.
// A game with a friend STANDS (the game waits for the friend, even if the friend goes to another app and returns,
// until one of them stops it, also remotely): each move goes into /data/chess.bin (the
// match, the friend, the side, the moves); the app opens into it; a friend that is in another app is AWAY (the game
// waits; your move goes when it is back); when the two boards meet again (duel_rejoin, also over the LAN / VPN) they
// compare the moves (SYNC) and the one that has more sends the rest. Only RESIGN (two taps) ends it before its end;
// a friend that resigned while you were away: you hear it when you come back.
// A SECOND FRIEND: INVITE and JOIN stay on the title next to THE GAME WITH <name>. A new match with a friend PARKS the
// standing game (/data/chess_p.bin; at most one parked game): the title shows THE GAME WITH <name> of each, and a tap on
// the parked one makes it the standing game again (the other one parks). The far board of a parked game sees this board
// AWAY. When the standing game ends, the parked game comes back by itself.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/duel.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "tfn/friends.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/imu.h"
#include "tfn/mfont.h"
#include "miniz.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "jingle/jingle.h"
// the opening of CHESS (its own): a slow and grand call in D minor, as two knights that salute, a low bell
static const tune_note_t CH_OPEN[] = {
        JN(0, 220, J_D4, W_TRI, 34),
        JN(240, 220, J_A4, W_TRI, 34),
        JN(480, 160, J_F4, W_TRI, 30),
        JN(660, 160, J_G4, W_TRI, 30),
        JN(840, 640, J_A4, JHOLD(W_PULSE), 22),
        JN(840, 640, J_D5, JHOLD(W_TRI), 30),
        JN(840, 640, J_F4, JHOLD(W_TRI), 18),
        JN(0, 460, J_D3, JHOLD(W_SINE), 44),
        JN(840, 640, J_D2, JHOLD(W_SINE), 50),
        JN(840, 900, J_A5, W_SINE, 12),
};
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#else
#define EXT_RAM_BSS_ATTR
#endif
static const char *TAG = "chess";
#include "chess_engine.inc"
static int64_t search_now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// ---------------------------------------------------------------- the layout
#if BOARD_UI_TABLET
#define CH_TAB5 1
#else
#define CH_TAB5 0
#endif
#if BOARD_BAR_SCREEN
#define BX 6
#define BY 6
#define SQ 20
#elif BOARD_ROUND
// THE ROUND LAYOUT (APP_ROUND_HI): the board is the biggest square in the circle (squares of 7/80 of the
// frame: 40 px of the 466 frame of the 1.75C, 36 px of the 412 frame of the Watcher), on a round table; the status in
// the top segment, the buttons in the side segments, the taken pieces in the bottom segment. All sizes come from gfx_w.
#define SQ (gfx_w * 7 / 80)
#define BX ((gfx_w - 8 * SQ) / 2)
#define BY ((gfx_h - 8 * SQ) / 2)
#elif BOARD_UI_TABLET
// the Tab5 (640 x 360): the board fills the height (squares of 43 px, about 7 mm), the panel is at the right
#define BX 8
#define BY 8
#define SQ 43
#else
#define BX 4
#define BY 8
#define SQ 28
#endif

// ---------------------------------------------------------------- the state
enum {
    G_TITLE,
    G_WAIT,
    G_PLAY,
    G_OVER
};
enum {
    LV_EASY,
    LV_MEDIUM,
    LV_HARD
};
#define MAXGAME 400
static struct {
    int g;
    float gt;
    cpos_t pos;
    bool vs_cpu;
    int my_side, level;
    cmove_t legal[MAXMV];
    int nlegal;
    int sel;  // the square of the selected piece (or NOSQ)
    cmove_t last;
    bool has_last;
    int nhist;  // the positions before each move (UNDO, the repetitions)
    bool promo_wait;
    cmove_t promo_mv;  // a pawn on the last rank: the choice
    char result[40];
    char why[40];
    int win;  // 1 you, 0 a draw, -1 the other
    float anim_t;
    cmove_t anim;  // the slide of the last move
    bool zoom;
    int pan;  // the 3.49: ZOOM and the pan (pixels)
    int64_t think_ms;
    int depth;
    uint32_t nodes;
} C;
static cpos_t s_hist[MAXGAME] EXT_RAM_BSS_ATTR;
static int64_t s_last;
static uint8_t s_inv[3][6];
static int s_ninv;
static float s_view_x, s_view_anchor;
static bool s_touch_active;
// THE THUMB (a move must not happen on one tap of a small target): on a board of small squares (the CoreS3
// 28 px, the Watcher 32 px: less than 5 mm) the first tap on a target only shows the move (a gold square); a second tap
// on the same square, 250 ms or more later, makes it. The 3.49 squares (57 and 68 px) move on one tap.
// The round layout (40 px of the 466 frame = 3.8 mm, 36 px of the 412 frame = 3.2 mm): always two taps.
#define PEND_SQ 36
static int s_pend = -1;  // the target that waits for the second tap (NOSQ: none)
static int64_t s_pend_ms;
static orient_t s_turn_target;
static int64_t s_turn_since;

// ---------------------------------------------------------------- the standing game with a friend (the file)
#ifdef ESP_PLATFORM
#define CHESS_FILE "/data/chess.bin"
#define PARK_FILE  "/data/chess_p.bin"
#elif !defined(CHESS_FILE)
#define CHESS_FILE "chess_save.bin"
#define PARK_FILE  "chess_park.bin"
#elif !defined(PARK_FILE)
#define PARK_FILE "chess_park.bin"
#endif
#define SG_MAGIC 0x43485331u
typedef struct {
    uint32_t magic, id;  // the match (the number of the duel)
    uint8_t peer[6];
    int8_t my_side;
    uint8_t state;           // 0 none, 1 on, 2 I resigned (the friend hears it when it comes back)
    uint8_t why, over;       // (over: it ended on the board; it stays until the friend has the last move)
    uint16_t n;              // the moves
    uint8_t mv[MAXGAME][3];  // from, to, the promotion
} sg_t;
static sg_t s_sg EXT_RAM_BSS_ATTR;
static sg_t s_park EXT_RAM_BSS_ATTR;  // the parked game (state 1: it waits for its friend; see the header)
static int s_send_from = -1;
static bool s_send_reset;  // (the moves that go to the friend, a few in each message)
static int64_t s_sync_at;
static char s_note[40];
static int64_t s_note_until;
static void sg_write(const char *path, const sg_t *g)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGW(TAG, "no %s", path);
        return;
    }
    fwrite(g, 1, (size_t)((const uint8_t *)g->mv - (const uint8_t *)g) + (size_t)g->n * 3, f);
    fclose(f);
}
static void sg_read(const char *path, sg_t *g)
{
    memset(g, 0, sizeof *g);
    FILE *f = fopen(path, "rb");
    if (f) {
        const size_t r = fread(g, 1, sizeof *g, f);
        (void)r;
        fclose(f);
    }
    if (g->magic != SG_MAGIC || g->n > MAXGAME) {
        memset(g, 0, sizeof *g);
    }
}
static void sg_save(void)
{
    sg_write(CHESS_FILE, &s_sg);
}
static void sg_load(void)
{
    sg_read(CHESS_FILE, &s_sg);
    sg_read(PARK_FILE, &s_park);
}
// the standing game and the parked game change places (both files), and the line goes to the new standing game
static void sg_swap(void)
{
    static sg_t tmp EXT_RAM_BSS_ATTR;
    tmp = s_sg;
    s_sg = s_park;
    s_park = tmp;
    sg_save();
    sg_write(PARK_FILE, &s_park);
    if (s_sg.state == 1) {
        duel_rejoin(s_sg.peer, s_sg.id);
    }
}
// a standing game that waits for its friend (not at its end): a new match parks it
static bool sg_waits(void)
{
    return s_sg.state == 1 && !s_sg.over;
}
// INVITE and JOIN on the title: no standing game that waits, or a free place to park it
static bool sg_can_new(void)
{
    return !sg_waits() || s_park.state != 1;
}
static uint16_t sg_crc(int k)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < k * 3; i++) {
        h = (h ^ (&s_sg.mv[0][0])[i]) * 16777619u;
    }
    return (uint16_t)(h ^ (h >> 16));
}
// the position of the standing game (a move that is not legal stops it)
static int sg_position(cpos_t *p)
{
    pos_start(p);
    cmove_t lm[MAXMV];
    for (int i = 0; i < s_sg.n; i++) {
        const int nl = legal_moves(p, lm);
        int k = 0;
        while (k < nl && !(lm[k].f == s_sg.mv[i][0] && lm[k].t == s_sg.mv[i][1] && lm[k].promo == s_sg.mv[i][2])) {
            k++;
        }
        if (k == nl) {
            return i;
        }
        cundo_t u;
        make_move(p, lm[k], &u);
    }
    return s_sg.n;
}
static void sg_send_sync(void)
{
    if (s_sg.state != 1 || duel_state() != DUEL_ON) {
        return;
    }
    const uint16_t c = sg_crc(s_sg.n);
    uint8_t b[10] = {2, 0, 0, 0, 0, (uint8_t)s_sg.n, (uint8_t)(s_sg.n >> 8), (uint8_t)c, (uint8_t)(c >> 8), s_sg.over};
    memcpy(b + 1, &s_sg.id, 4);
    duel_send(b, 10);
    s_sync_at = search_now_ms();
}
static void sg_view(void);
static const char *peer_name(void)
{
    const char *f = friends_name_of(s_sg.peer);
    return friends_short_name(f);
}
static const char *park_name(void)
{
    return friends_short_name(friends_name_of(s_park.peer));
}

// ---------------------------------------------------------------- the computer in its own task (the screen stays smooth)
static volatile int s_ai;  // 0 idle, 1 thinks, 2 the move is ready
static cmove_t s_ai_move;
static void ai_think_now(void)
{
    static const int DEPTH[3] = {1, 3, 6}, MS[3] = {300, 1200, 3500}, NOISE[3] = {120, 15, 0};
    int d;
    uint32_t n;
    const int64_t t0 = search_now_ms();
    if (!think(&C.pos, DEPTH[C.level], MS[C.level], NOISE[C.level], esp_random(), &s_ai_move, &d, &n)) {
        s_ai_move = (cmove_t){0};
    }
    C.think_ms = search_now_ms() - t0;
    C.depth = d;
    C.nodes = n;
    s_ai = 2;
}
#ifdef ESP_PLATFORM
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
            ai_think_now();
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    s_ai_quit = false;
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
static void ai_start(void)
{
    s_ai = 1;
    s_ai_go = true;
}
#else
static void ai_start(void)
{
    s_ai = 1;
    ai_think_now();
}
#endif

// ---------------------------------------------------------------- the pieces and the squares: 3D pictures
// Blender renders (ivory and ebony pieces, maple and walnut squares), made small by
// tools/mk_art.py into chess_art.c: P8 with 16 steps of alpha, deflated. art_build() inflates them one time (PSRAM);
// each size on the screen gets its own copy of the 12 pieces (a box filter), kept in a small cache.
extern const uint8_t CHESS_ART[];
extern const uint32_t CHESS_ART_LEN;
typedef struct {
    int w, h, ax, ay;
    const uint16_t *pal;
    uint8_t *idx, *a;
} cpic_t;  // a picture as it is in the data
typedef struct {
    int16_t w, h, ax, ay;
    uint16_t *c;
    uint8_t *a;
} cspr_t;                 // a picture at a size of the screen
static cpic_t s_src[14];  // 0-5 white P N B R Q K, 6-11 black, 12 maple, 13 walnut
static uint16_t *s_cpal;  // 4 palettes of 256 colours
static bool s_art_ok;
#define CACHE_N 6
static struct {
    int key;
    uint32_t used;
    cspr_t p[12];
} s_cache[CACHE_N];
static uint32_t s_cache_t;
static uint16_t *s_wood[2];  // the woods at 2 x 2 squares (for the size s_wood_s)
static int s_wood_s;
#define CKING 81.0f  // the height of the king over its anchor in the data (px)
#define CTALL 95.0f  // the height of the king in the data, with the part under the anchor (px)
static void *ch_alloc(size_t n)
{
#ifdef ESP_PLATFORM
    void *p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return p ? p : malloc(n);
#else
    return malloc(n);
#endif
}
static inline uint16_t c_mix(uint16_t d, uint16_t s, int a)  // a: 0 = d .. 256 = s (the byte order of the frame)
{
    if (a <= 0) {
        return d;
    }
    if (a >= 256) {
        return s;
    }
    uint32_t dd = (uint16_t)(d >> 8 | d << 8), ss = (uint16_t)(s >> 8 | s << 8), a5 = (uint32_t)(a + 4) >> 3;
    dd = (dd | (dd << 16)) & 0x07E0F81F;
    ss = (ss | (ss << 16)) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * a5) >> 5)) & 0x07E0F81F;
    r = (r | (r >> 16)) & 0xFFFF;
    return (uint16_t)(r >> 8 | r << 8);
}
static void art_build(void)
{
    if (s_art_ok) {
        return;
    }
    const uint8_t *b = CHESS_ART;
    size_t at = 5, n = CHESS_ART_LEN;
    if (n < 5 || memcmp(b, "CHA1", 4)) {
        return;
    }
    const int cnt = b[4];
    tinfl_decompressor *d = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
    uint8_t *pal8 = (uint8_t *)malloc(512);
    s_cpal = (uint16_t *)ch_alloc(4 * 256 * 2);
    bool ok = d && pal8 && s_cpal;
    for (int k = 0; ok && k < 4; k++) {
        size_t zl = b[at] | b[at + 1] << 8, in = zl, out = 512;
        at += 2;
        tinfl_init(d);
        ok = at + zl <= n &&
             tinfl_decompress(d, b + at, &in, pal8, pal8, &out,
                              TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) ==
                     TINFL_STATUS_DONE &&
             out == 512;
        for (int i = 0; ok && i < 256; i++) {
            s_cpal[k * 256 + i] = (uint16_t)(pal8[2 * i + 1] << 8 | pal8[2 * i]);  // (as the frame keeps it)
        }
        at += zl;
    }
    for (int k = 0; ok && k < cnt; k++) {
        if (at + 8 > n) {
            ok = false;
            break;
        }
        const int nm = b[at], pi = b[at + 1] & 3, w = b[at + 2], h = b[at + 3], ax = b[at + 4], ay = b[at + 5];
        size_t zl = b[at + 6] | b[at + 7] << 8;
        at += 8;
        int id = nm == 'm' ? 12 : nm == 'w' ? 13 : -1;
        const char *ord = "PNBRQK";
        for (int i = 0; i < 6; i++) {
            if (nm == ord[i]) {
                id = i;
            }
            if (nm == ord[i] + 32) {
                id = 6 + i;
            }
        }
        const bool alpha = id >= 0 && id < 12;
        size_t out = (size_t)w * h * (alpha ? 2 : 1), in = zl;
        uint8_t *dst = id >= 0 ? (uint8_t *)ch_alloc(out) : NULL;
        if (id >= 0 && dst && at + zl <= n) {
            tinfl_init(d);
            size_t got = out;
            if (tinfl_decompress(d, b + at, &in, dst, dst, &got,
                                 TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) ==
                        TINFL_STATUS_DONE &&
                got == out) {
                s_src[id] = (cpic_t){w, h, ax, ay, s_cpal + pi * 256, dst, alpha ? dst + w * h : NULL};
            } else {
                free(dst);
            }
        } else {
            free(dst);
        }
        at += zl;
    }
    free(d);
    free(pal8);
    s_art_ok = ok;
    for (int i = 0; i < 14; i++) {
        if (!s_src[i].idx) {
            s_art_ok = false;
        }
    }
    if (!s_art_ok) {
        ESP_LOGE(TAG, "the 3D pictures did not load");
    }
}
// a picture of the data at the scale k: each pixel is the mean of its box of the picture (the colours weighted by alpha)
static void spr_make(const cpic_t *s, float k, cspr_t *o)
{
    int W = (int)(s->w * k + 0.5f), H = (int)(s->h * k + 0.5f);
    if (W < 1) {
        W = 1;
    }
    if (H < 1) {
        H = 1;
    }
    o->c = (uint16_t *)ch_alloc((size_t)W * H * 3);
    if (!o->c) {
        o->w = o->h = 0;
        return;
    }
    o->a = (uint8_t *)(o->c + W * H);
    o->w = (int16_t)W;
    o->h = (int16_t)H;
    o->ax = (int16_t)((s->ax * W + s->w / 2) / s->w);
    o->ay = (int16_t)((s->ay * H + s->h / 2) / s->h);
    for (int y = 0; y < H; y++) {
        int y0 = y * s->h / H, y1 = (y + 1) * s->h / H;
        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        for (int x = 0; x < W; x++) {
            int x0 = x * s->w / W, x1 = (x + 1) * s->w / W;
            if (x1 <= x0) {
                x1 = x0 + 1;
            }
            int r = 0, g = 0, bl = 0, at = 0, cnt = 0;
            for (int sy = y0; sy < y1; sy++) {
                for (int sx = x0; sx < x1; sx++) {
                    const int i = sy * s->w + sx, a = s->a ? s->a[i] : 255;
                    const uint16_t c = (uint16_t)(s->pal[s->idx[i]] >> 8 | s->pal[s->idx[i]] << 8);
                    r += (c >> 11) * a;
                    g += ((c >> 5) & 63) * a;
                    bl += (c & 31) * a;
                    at += a;
                    cnt++;
                }
            }
            const int o_ = y * W + x;
            o->a[o_] = (uint8_t)(at / cnt);
            uint16_t c = at ? (uint16_t)((r / at) << 11 | (g / at) << 5 | (bl / at)) : 0;
            o->c[o_] = (uint16_t)(c >> 8 | c << 8);
        }
    }
}
// the 12 pieces at the scale k (from the cache, or made now in the slot that waited the longest)
static const cspr_t *pieces_at(float k)
{
    const int key = (int)(k * 4096);
    int slot = 0;
    for (int i = 0; i < CACHE_N; i++) {
        if (s_cache[i].key == key && s_cache[i].p[0].c) {
            s_cache[i].used = ++s_cache_t;
            return s_cache[i].p;
        }
        if (s_cache[i].used < s_cache[slot].used) {
            slot = i;
        }
    }
    for (int i = 0; i < 12; i++) {
        free(s_cache[slot].p[i].c);
        s_cache[slot].p[i].c = NULL;
    }
    for (int i = 0; i < 12; i++) {
        spr_make(&s_src[i], k, &s_cache[slot].p[i]);
    }
    s_cache[slot].key = key;
    s_cache[slot].used = ++s_cache_t;
    return s_cache[slot].p;
}
static int s_clip_y0, s_clip_y1;
static void spr_blit(const cspr_t *p, int x, int y)  // (x, y): the screen point of the anchor
{
    if (!p->c) {
        return;
    }
    const int x0 = x - p->ax, y0 = y - p->ay;
    for (int j = 0; j < p->h; j++) {
        const int py = y0 + j;
        if (py < s_clip_y0 || py >= s_clip_y1 || py < 0 || py >= gfx_h) {
            continue;
        }
        uint16_t *d = g_fb + py * gfx_w;
        const uint16_t *c = p->c + j * p->w;
        const uint8_t *a = p->a + j * p->w;
        for (int i = 0; i < p->w; i++) {
            const int px = x0 + i, al = a[i];
            if (!al || px < 0 || px >= gfx_w) {
                continue;
            }
            d[px] = al == 255 ? c[i] : c_mix(d[px], c[i], al + (al >> 7));
        }
    }
}
static int piece_id(int piece)
{
    const int k = piece > 0 ? piece : -piece;
    return k < 1 || k > 6 ? -1 : (piece > 0 ? 0 : 6) + k - 1;
}
// a piece that stands on a square of size s: the middle of its base at (cx, by) = 9/10 of the square down; the top of
// the king touches the top of the square (the far rank stays whole under the header)
static void piece_stand(int cx, int by, int s, int piece)
{
    const int id = piece_id(piece);
    if (id < 0 || !s_art_ok) {
        return;
    }
    spr_blit(&pieces_at(0.90f * s / CKING)[id], cx, by);
}
// a piece in a box of size s at (x, y) (the panels, the small board, the icon)
static void draw_piece(int x, int y, int s, int piece)
{
    const int id = piece_id(piece);
    if (id < 0 || !s_art_ok) {
        return;
    }
    const float k = s / CTALL;
    spr_blit(&pieces_at(k)[id], x + s / 2 - (int)(4 * k), y + (int)(81 * k + 0.5f));
}
// the 3D pictures out of the memory (the app closes; the icon or the next open loads them again)
static void art_free(void)
{
    for (int i = 0; i < CACHE_N; i++) {
        for (int k = 0; k < 12; k++) {
            free(s_cache[i].p[k].c);
            s_cache[i].p[k].c = NULL;
        }
    }
    memset(s_cache, 0, sizeof s_cache);
    for (int i = 0; i < 14; i++) {
        free(s_src[i].idx);
        s_src[i].idx = NULL;
    }
    for (int w = 0; w < 2; w++) {
        free(s_wood[w]);
        s_wood[w] = NULL;
    }
    s_wood_s = 0;
    free(s_cpal);
    s_cpal = NULL;
    s_art_ok = false;
}
// the woods of the squares at the size s (2 x 2 squares of each wood: a square takes one quarter)
static void woods_at(int s)
{
    if (s == s_wood_s && s_wood[0]) {
        return;
    }
    for (int w = 0; w < 2; w++) {
        free(s_wood[w]);
        s_wood[w] = (uint16_t *)ch_alloc((size_t)4 * s * s * 2);
        cspr_t t;
        if (!s_wood[w]) {
            continue;
        }
        spr_make(&s_src[12 + w], 2.0f * s / s_src[12 + w].w, &t);
        if (!t.c) {
            continue;
        }
        for (int y = 0; y < 2 * s; y++) {
            for (int x = 0; x < 2 * s; x++) {
                s_wood[w][y * 2 * s + x] = y < t.h && x < t.w ? t.c[y * t.w + x] : 0;
            }
        }
        free(t.c);
    }
    s_wood_s = s;
}
// a square: its wood (light: maple), and a tint over it (a = 0: none)
static void draw_square(int x, int y0, int y1, int ytop, int s, bool light, int sq, uint16_t tint, int ta)
{
    const uint16_t *w = s_wood[light ? 0 : 1];
    const int q = (sq * 7 + (sq >> 4) * 3) & 3, ox = (q & 1) * s, oy = (q >> 1) * s;
    for (int y = y0; y < y1; y++) {
        if (y < 0 || y >= gfx_h) {
            continue;
        }
        uint16_t *d = g_fb + y * gfx_w;
        const uint16_t *src = w ? w + (oy + y - ytop) * 2 * s + ox : NULL;
        for (int i = 0; i < s; i++) {
            const int px = x + i;
            if (px < 0 || px >= gfx_w) {
                continue;
            }
            uint16_t c = src ? src[i] : (light ? rgb(238, 214, 170) : rgb(176, 130, 92));
            d[px] = ta ? c_mix(c, tint, ta) : c;
        }
    }
}

// ---------------------------------------------------------------- the board on the screen
static bool portrait(void)
{
    return BOARD_BAR_SCREEN && gfx_h > gfx_w;
}
static bool portrait_board(void)
{
    return portrait() && C.g >= G_PLAY;
}
static int portrait_square(void)
{
    return (gfx_h - 96) / 8;
}
static float portrait_limit(void)
{
    return (float)(8 * portrait_square() - gfx_w);
}
static bool s_tilt;   // VIEW: false = SWIPE (a drag moves the view), true = TILT
static bool s_vdrag;  // this touch moves the view (no move of a piece)
static int s_vdx0, s_vdy0;
static float s_vd0;
static float s_view_y,
        s_view_yanchor;  // (the 3.49 in landscape: the top of the view in px of the big board; the view at no tilt)
static void view_center(void)
{
    s_view_y = s_view_yanchor = 8 * 57 - 172;  // (landscape: your own side, at the bottom)
    if (portrait()) {
        s_view_x = s_view_anchor = portrait_limit() * 0.5f;
    }
    if (BOARD_BAR_SCREEN && imu_ok()) {
        imu_set_level();
    }
}
// the 3.49 in landscape in a game: a big board of 3 ranks (57 px squares) on the left, the view moves up and down
#define LAND_SQ 57
#define MINI_SQ 16
#define MINI_X  484
#define MINI_Y  24
static bool land_board(void)
{
    return BOARD_BAR_SCREEN && !portrait() && C.g >= G_PLAY;
}
static float land_limit(void)
{
    return (float)(8 * LAND_SQ - 172);
}
static bool board_zoom(void)
{
    return false;
}
static int sq_size(void)
{
    return portrait_board() ? portrait_square() : land_board() ? LAND_SQ : SQ;
}
static int board_x(void)
{
    return portrait_board() ? -(int)lroundf(s_view_x) : land_board() ? 0 : BX;
}
static int board_y(void)
{
    return portrait_board() ? 40 : portrait() ? 88 : land_board() ? -(int)lroundf(s_view_y) : BY;
}
// the square of a cell (col, row from the top left of the view): the side of the player at the bottom
static int sq_of(int col, int row)
{
    return C.my_side == WHITE ? (7 - row) * 16 + col : row * 16 + (7 - col);
}
static void cell_of(int sq, int *col, int *row)
{
    const int r = sq >> 4, f = sq & 7;
    if (C.my_side == WHITE) {
        *col = f;
        *row = 7 - r;
    } else {
        *col = 7 - f;
        *row = r;
    }
}
static int sq_at(int x, int y)
{
    const int s = sq_size(), bx = board_x(), by = board_y();
    if (x < 0 || x >= gfx_w || y < 0 || y >= gfx_h) {
        return NOSQ;
    }
    if (x < bx || y < by || x >= bx + 8 * s || y >= by + 8 * s) {
        return NOSQ;
    }
    if (land_board() && (y < 0 || y >= 172)) {
        return NOSQ;
    }
    return sq_of((x - bx) / s, (y - by) / s);
}
static bool is_target(int sq)
{
    for (int i = 0; i < C.nlegal; i++) {
        if (C.legal[i].f == C.sel && C.legal[i].t == sq) {
            return true;
        }
    }
    return false;
}
static void draw_board(void)
{
    const int s = sq_size(), bx = board_x(), by = board_y();
    s_clip_y0 = land_board() ? 0 : by;
    s_clip_y1 = land_board() ? 172 : by + 8 * s;
    woods_at(s);
    const bool chk = C.g == G_PLAY && in_check(&C.pos, C.pos.side);
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            const int sq = sq_of(col, row), x = bx + col * s, y = by + row * s;
            if (y + s <= s_clip_y0 || y >= s_clip_y1) {
                continue;
            }
            const bool light = ((sq >> 4) + (sq & 7)) & 1;
            uint16_t tint = 0;
            int ta = 0;
            if (C.has_last && (sq == C.last.f || sq == C.last.t)) {
                tint = rgb(250, 226, 90);
                ta = 110;
            }
            if (sq == C.sel) {
                tint = rgb(110, 200, 110);
                ta = 150;
            }
            if (sq == s_pend && C.sel != NOSQ) {
                tint = rgb(250, 200, 60);
                ta = 170;
            }
            if (chk && sq == C.pos.ksq[kidx(C.pos.side)]) {
                tint = rgb(236, 70, 60);
                ta = 170;
            }
            const int y0 = y < s_clip_y0 ? s_clip_y0 : y, y1 = y + s > s_clip_y1 ? s_clip_y1 : y + s;
            draw_square(x, y0, y1, y, s, light, sq, tint, ta);
        }
    }
    // the pieces (the moving piece slides)
    const float k = C.anim_t < 0.18f ? C.anim_t / 0.18f : 1;
    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            const int sq = sq_of(col, row), pc = C.pos.b[sq];
            if (!pc) {
                continue;
            }
            int x = bx + col * s, y = by + row * s;
            if (k < 1 && C.has_last && sq == C.anim.t) {
                int c0, r0;
                cell_of(C.anim.f, &c0, &r0);
                x = (int)(bx + (c0 + (col - c0) * k) * s);
                y = (int)(by + (r0 + (row - r0) * k) * s);
            }
            piece_stand(x + s / 2, y + s * 9 / 10, s, pc);
        }
    }
    // the moves of the selected piece: dots (a ring on a capture)
    if (C.sel != NOSQ) {
        for (int i = 0; i < C.nlegal; i++) {
            if (C.legal[i].f != C.sel) {
                continue;
            }
            int col, row;
            cell_of(C.legal[i].t, &col, &row);
            const int cx = bx + col * s + s / 2, cy = by + row * s + s / 2;
            if (cy < s_clip_y0 || cy >= s_clip_y1) {
                continue;
            }
            if (C.pos.b[C.legal[i].t] || (C.legal[i].flags & 2)) {
                gfx_ring(cx, cy, s / 2 - 1, s / 2 - 3, rgb(60, 140, 60));
            } else {
                gfx_fill_circle(cx, cy, s / 7 + 1, rgb(60, 140, 60));
            }
        }
    }
    // the move that waits for the second tap: a gold frame on its target, and the piece there (a preview)
    if (C.sel != NOSQ && s_pend != NOSQ) {
        int col, row;
        cell_of(s_pend, &col, &row);
        const int x = bx + col * s, y = by + row * s;
        if (!C.pos.b[s_pend]) {
            piece_stand(x + s / 2, y + s * 9 / 10, s, C.pos.b[C.sel]);
        }
        gfx_frame_rect(x, y, s, s, 3, rgb(250, 210, 80));
    }
}

// ---------------------------------------------------------------- the game
static const tune_note_t TUNE_CH_WIN[] = {{0, 140, 67, 0, W_PULSE, 34},
                                          {150, 140, 72, 0, W_PULSE, 34},
                                          {300, 140, 76, 0, W_PULSE, 34},
                                          {450, 600, 79, 0, W_PULSE | TUNE_HOLD, 36}};
static const tune_note_t TUNE_CH_LOSE[] = {
        {0, 260, 64, 0, W_TRI, 40}, {270, 260, 60, 0, W_TRI, 40}, {540, 700, 55, 0, W_TRI | TUNE_HOLD, 40}};
#define TN(t) ((int)(sizeof(t) / sizeof((t)[0])))
static void sq_name(int sq, char *o)
{
    o[0] = (char)('a' + (sq & 7));
    o[1] = (char)('1' + (sq >> 4));
    o[2] = 0;
}
static void after_move(void);
static void refresh_legal(void)
{
    C.nlegal = legal_moves(&C.pos, C.legal);
    C.sel = NOSQ;
    s_pend = NOSQ;
}
static void play_move(cmove_t m, bool send)
{
    if (C.nhist < MAXGAME) {
        s_hist[C.nhist++] = C.pos;
    }
    const bool cap = C.pos.b[m.t] || (m.flags & 2);
    cundo_t u;
    make_move(&C.pos, m, &u);
    C.last = m;
    C.has_last = true;
    C.anim = m;
    C.anim_t = 0;
    if (in_check(&C.pos, C.pos.side)) {
        sfx_tone(1200, 1200, 60, W_SQUARE, 30);
        sfx_tone(1500, 1500, 80, W_SQUARE, 30);
    } else if (cap) {
        sfx_tone(420, 140, 90, W_NOISE, 44);
    } else {
        sfx_tone(900, 500, 35, W_NOISE, 34);
    }
    if (!C.vs_cpu && s_sg.state == 1 && s_sg.n < MAXGAME) {
        const int ply = s_sg.n;
        s_sg.mv[ply][0] = m.f;
        s_sg.mv[ply][1] = m.t;
        s_sg.mv[ply][2] = m.promo;
        s_sg.n++;
        sg_save();
        if (send) {
            const uint8_t b[6] = {1, (uint8_t)ply, (uint8_t)(ply >> 8), m.f, m.t, m.promo};
            duel_send(b, 6);
        }  // (away: it goes with the SYNC later)
    }
    // the 3.49 in landscape: a move of the other side that is out of the view comes into it (the view at no tilt goes there)
    if (land_board() && C.pos.side == C.my_side) {
        int col, row;
        cell_of(m.t, &col, &row);
        const float top = row * LAND_SQ, lim = land_limit();
        if (top < s_view_y || top + LAND_SQ > s_view_y + 172) {
            const float want = top + LAND_SQ / 2 - 86;
            s_view_yanchor = want < 0 ? 0 : want > lim ? lim : want;
        }
    }
    refresh_legal();
    after_move();
}
static int repetitions(void)
{
    int n = 0;
    for (int i = C.nhist - 2; i >= 0 && i >= C.nhist - 1 - C.pos.half; i -= 2) {
        const cpos_t *h = &s_hist[i];
        if (h->side == C.pos.side && h->castle == C.pos.castle && h->ep == C.pos.ep &&
            !memcmp(h->b, C.pos.b, sizeof h->b)) {
            n++;
        }
    }
    return n;
}
static bool too_little(void)
{
    int minors = 0;
    for (int sq = 0; sq < 128; sq++) {
        if (sq & 0x88) {
            sq += 7;
            continue;
        }
        const int a = abs(C.pos.b[sq]);
        if (a == PAWN || a == ROOK || a == QUEEN) {
            return false;
        }
        if (a == KNIGHT || a == BISHOP) {
            minors++;
        }
    }
    return minors <= 1;
}
static bool s_quiet_end;  // (a game shown again: no tune)
static void game_end(int win, const char *result, const char *why)
{
    C.g = G_OVER;
    C.gt = 0;
    C.win = win;
    snprintf(C.result, sizeof C.result, "%s", result);
    snprintf(C.why, sizeof C.why, "%s", why);
    if (!C.vs_cpu && s_sg.state == 1 && !s_sg.over) {
        s_sg.over = 1;
        sg_save();
        sg_send_sync();
    }
    if (s_quiet_end) {
        return;
    }
    if (win > 0) {
        audio_tune(TUNE_CH_WIN, TN(TUNE_CH_WIN));
    } else if (win < 0) {
        audio_tune(TUNE_CH_LOSE, TN(TUNE_CH_LOSE));
    }
}
static void after_move(void)
{
    if (C.nlegal == 0) {
        if (in_check(&C.pos, C.pos.side)) {
            game_end(C.pos.side == C.my_side ? -1 : 1, C.pos.side == WHITE ? "BLACK WINS" : "WHITE WINS", "CHECKMATE");
        } else {
            game_end(0, "A DRAW", "STALEMATE");
        }
    } else if (C.pos.half >= 100) {
        game_end(0, "A DRAW", "50 MOVES WITH NO CAPTURE");
    } else if (repetitions() >= 2) {
        game_end(0, "A DRAW", "THE SAME POSITION 3 TIMES");
    } else if (too_little()) {
        game_end(0, "A DRAW", "TOO LITTLE MATERIAL");
    } else if (C.vs_cpu && C.pos.side != C.my_side) {
        ai_start();
    }
}
static void game_new(bool cpu)
{
    C.vs_cpu = cpu;
    if (!cpu) {
        // a new game with a friend: it stands (the file) until a resign or its end; a standing game that waits parks
        C.my_side = duel_host() ? WHITE : BLACK;
        if (sg_waits() && s_park.state != 1) {
            s_park = s_sg;
            sg_write(PARK_FILE, &s_park);
        }
        memset(&s_sg, 0, sizeof s_sg);
        s_sg.magic = SG_MAGIC;
        s_sg.id = duel_seed();
        memcpy(s_sg.peer, duel_peer(), 6);
        s_sg.my_side = (int8_t)C.my_side;
        s_sg.state = 1;
        sg_save();
        duel_keep(true);
        s_note_until = 0;
    } else {
        C.my_side = store_get("ch_side", 0) ? BLACK : WHITE;
    }
    pos_start(&C.pos);
    C.nhist = 0;
    C.has_last = false;
    C.promo_wait = false;
    C.anim_t = 1;
    C.pan = C.my_side == WHITE ? 148 : 0;
    C.g = G_PLAY;
    C.gt = 0;
    s_ai = 0;
    view_center();
    refresh_legal();
    sfx_tone(660, 990, 120, W_SQUARE, 28);
    if (cpu && C.my_side == BLACK) {
        ai_start();
    }
}
static void undo(void)
{
    if (!C.vs_cpu || s_ai == 1) {
        return;
    }
    int k = C.pos.side == C.my_side ? 2 : 1;
    if (C.nhist < k) {
        return;
    }
    C.nhist -= k;
    C.pos = s_hist[C.nhist];
    C.has_last = false;
    s_ai = 0;
    refresh_legal();
    sfx_play(SFX_BACK);
}

// ---------------------------------------------------------------- the panel and the buttons
typedef struct {
    int x, y, w, h;
} rect_t;
enum {
    BT_CPU,
    BT_LEVEL,
    BT_SIDE,
    BT_INVITE,
    BT_JOIN,
    BT_CANCEL,
    BT_ZOOM,
    BT_UNDO,
    BT_NEW,
    BT_RESIGN,
    BT_Q,
    BT_R,
    BT_B,
    BT_N,
    BT_RESUME,
    BT_PARKED,
    BT_TITLE,
    BT_MENU
};
static int64_t s_resign_ask;  // (RESIGN: a second tap in 3 s)
static int64_t s_new_ask;     // (the round layout: NEW in a game, a second tap in 3 s)
static rect_t s_btn[8];
static int s_btn_id[8], s_nbtn;
static uint16_t C_TXT, C_DIM, C_YEL, C_BG;
static void button(int id, int x, int y, int w, int h, const char *s, bool main)
{
    if (s_nbtn == 8) {
        return;
    }
    s_btn[s_nbtn] = (rect_t){x, y, w, h};
    s_btn_id[s_nbtn++] = id;
    gfx_fill_rect(x, y, w, h, main ? rgb(70, 46, 22) : rgb(30, 26, 22));
    gfx_frame_rect(x, y, w, h, 2, main ? rgb(250, 210, 80) : rgb(176, 160, 130));
    const mfont_t *f = mf_width(&MF_LABEL, s, 1) <= w - 8 ? &MF_LABEL : &MF_TINY;
    mf_text_c(f, x + w / 2, y + (h + f->asc) / 2 - 1, s, main ? rgb(250, 216, 96) : C_TXT, 255, 1);
}
static int button_at(int x, int y)
{
    for (int i = 0; i < s_nbtn; i++) {
        if (x >= s_btn[i].x - 4 && x < s_btn[i].x + s_btn[i].w + 4 && y >= s_btn[i].y - 4 &&
            y < s_btn[i].y + s_btn[i].h + 4) {
            return s_btn_id[i];
        }
    }
    return -1;
}
#if BOARD_ROUND
// ---------------------------------------------------------------- the round layout (the parts)
// the half width of the circle at the row y (0: out of the circle)
static int rd_half(int y)
{
    const float r = gfx_w * 0.5f, d = y + 0.5f - gfx_h * 0.5f, q = r * r - d * d;
    return q > 0 ? (int)sqrtf(q) : 0;
}
// the round table: dark walnut, lighter at the top; a soft brass rim; the frame of the board
static void rd_table(void)
{
    const int R = gfx_w / 2;
    for (int y = 0; y < gfx_h; y++) {
        const int h = rd_half(y);
        if (!h) {
            continue;
        }
        const int t = y * 255 / gfx_h;
        gfx_fill_rect(R - h, y, 2 * h, 1, ui_mix(rgb(58, 40, 26), rgb(20, 14, 10), t));
    }
    ui_ring(R, gfx_h / 2, R - 3, 3, rgb(150, 112, 58), 200);
    const int b = 8 * SQ;
    ui_rrect(BX - 7, BY - 7, b + 14, b + 14, 7, rgb(84, 56, 30));
    gfx_frame_rect(BX - 2, BY - 2, b + 4, b + 4, 1, rgb(150, 112, 62));
}
// the largest font of the list whose text fits w (the last one if none)
static const mfont_t *rd_fit(const mfont_t *const *f, int n, const char *s, int w)
{
    for (int i = 0; i < n - 1; i++) {
        if (mf_width(f[i], s, 1) <= w) {
            return f[i];
        }
    }
    return f[n - 1];
}
// a round button (the side segments of the circle)
static void rd_cbutton(int id, int cx, int cy, int r, const char *s, bool main)
{
    if (s_nbtn == 8) {
        return;
    }
    s_btn[s_nbtn] = (rect_t){cx - r, cy - r, 2 * r, 2 * r};
    s_btn_id[s_nbtn++] = id;
    ui_circle(cx, cy, r, main ? rgb(92, 60, 26) : rgb(44, 32, 24), 255);
    ui_ring(cx, cy, r, 2, main ? rgb(250, 210, 80) : rgb(186, 160, 120), 255);
    static const mfont_t *const F[] = {&MF_LABEL, &MF_TINY};
    const mfont_t *f = rd_fit(F, 2, s, 2 * r - 10);
    mf_text_c(f, cx, cy + f->asc / 2, s, main ? rgb(250, 216, 96) : C_TXT, 255, 1);
}
// a pill button (the title)
static void rd_pill(int id, int x, int y, int w, int h, const char *s, bool main)
{
    if (s_nbtn == 8) {
        return;
    }
    s_btn[s_nbtn] = (rect_t){x, y, w, h};
    s_btn_id[s_nbtn++] = id;
    ui_rbox(x, y, w, h, h / 2, main ? rgb(92, 60, 26) : rgb(36, 27, 20), main ? rgb(250, 210, 80) : rgb(186, 160, 120),
            2);
    static const mfont_t *const F[] = {&MF_N20, &MF_LABEL, &MF_TINY};
    const mfont_t *f = rd_fit(F, 3, s, w - h / 2 - 8);
    mf_text_c(f, x + w / 2, y + (h + f->asc) / 2, s, main ? rgb(250, 216, 96) : C_TXT, 255, 1);
}
// the taken pieces of one colour (side: their colour) in a row from the middle of the circle to one side (dir -1:
// to the left, 1: to the right), from the top y; the step gets smaller when the row is long
static int rd_taken(int side, int dir, int y, int ps, int room)
{
    static const int START[7] = {0, 8, 2, 2, 2, 1, 0};
    int have[7] = {0}, n = 0, list[16];
    for (int sq = 0; sq < 128; sq++) {
        if (!(sq & 0x88)) {
            const int p = C.pos.b[sq] * side;
            if (p > 0 && p < 7) {
                have[p]++;
            }
        }
    }
    for (int k = 5; k >= 1; k--) {
        for (int i = have[k]; i < START[k] && n < 16; i++) {
            list[n++] = side * k;
        }
    }
    if (!n) {
        return 0;
    }
    int st = ps * 3 / 5;
    if (n > 1 && (n - 1) * st + ps > room) {
        st = (room - ps) / (n - 1);
    }
    s_clip_y0 = 0;
    s_clip_y1 = gfx_h;
    for (int i = 0; i < n; i++) {
        draw_piece(dir < 0 ? gfx_w / 2 - 8 - ps - i * st : gfx_w / 2 + 8 + i * st, y, ps, list[i]);
    }
    return n;
}
#endif
static const char *const LV_NAME[3] = {"EASY", "MEDIUM", "HARD"};
static void status_lines(char *a, char *b, int n)
{
    if (C.g == G_OVER) {
        snprintf(a, n, "%s", C.result);
        snprintf(b, n, "%s", C.why);
        return;
    }
    const bool mine = C.pos.side == C.my_side;
    snprintf(a, n, "%s",
             mine       ? (in_check(&C.pos, C.pos.side) ? "CHECK! YOUR MOVE" : "YOUR MOVE")
             : C.vs_cpu ? "THE COMPUTER THINKS"
                        : "YOUR FRIEND MOVES");
    if (!C.vs_cpu && duel_state() != DUEL_ON) {
        snprintf(b, n, "%s IS AWAY: THE GAME WAITS", peer_name());
        return;
    }
    if (C.has_last) {
        char f[3], t[3];
        sq_name(C.last.f, f);
        sq_name(C.last.t, t);
        snprintf(b, n, "MOVE %d   LAST %s-%s", C.pos.full, f, t);
    } else {
        snprintf(b, n, "MOVE %d", C.pos.full);
    }
}
static void portrait_text(const char *text, int y, uint16_t colour)
{
    // Short lines fit the narrow screen; status and friend names can need several lines.
    char words[80], line[80] = "";
    snprintf(words, sizeof words, "%s", text);
    for (char *p = strtok(words, " "); p; p = strtok(NULL, " ")) {
        char next[80];
        snprintf(next, sizeof next, "%s%s%s", line, line[0] ? " " : "", p);
        if (line[0] && mf_width(&MF_TINY, next, 1) > gfx_w - 16) {
            mf_text_c(&MF_TINY, gfx_w / 2, y, line, colour, 255, 1);
            y += 18;
            snprintf(line, sizeof line, "%s", p);
        } else {
            snprintf(line, sizeof line, "%s", next);
        }
    }
    if (line[0]) {
        mf_text_c(&MF_TINY, gfx_w / 2, y, line, colour, 255, 1);
    }
}
static void portrait_render(void)
{
    if (portrait_board()) {
        draw_board();
        char a[48], b[48];
        status_lines(a, b, sizeof a);
        portrait_text(a, 16, C.g == G_OVER ? C_YEL : C_TXT);
        portrait_text("TILT LEFT / RIGHT", 34, C_DIM);
        // The footer never hides a board square. Its bar shows the horizontal window.
        const int end = 40 + 8 * sq_size();
        const int bar_w = gfx_w * gfx_w / (8 * sq_size());
        const int bar_x = (int)(s_view_x * gfx_w / (8 * sq_size()));
        gfx_fill_rect(0, end + 1, gfx_w, 3, rgb(70, 58, 42));
        gfx_fill_rect(bar_x, end + 1, bar_w, 3, C_YEL);
        button(BT_MENU, 8, gfx_h - 52, gfx_w - 16, 48, "MENU", false);
        if (C.promo_wait) {
            static const int pieces[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
            gfx_fill_rect(8, 186, 156, 200, C_BG);
            portrait_text("PROMOTE TO", 206, C_YEL);
            for (int i = 0; i < 4; i++) {
                const int x = 16 + (i % 2) * 76, y = 222 + (i / 2) * 76;
                button(BT_Q + i, x, y, 64, 64, "", false);
                s_clip_y0 = 0;
                s_clip_y1 = gfx_h;
                draw_piece(x + 6, y + 6, 52, C.my_side * pieces[i]);
            }
        }
        return;
    }
    mf_text_c(&MF_N24, 86, 48, "CHESS", C_YEL, 255, 1);
    draw_board();
    if (C.g == G_TITLE || C.g == G_WAIT) {
        if (C.g == G_WAIT) {
            portrait_text("WAITING FOR A FRIEND", 292, C_TXT);
            portrait_text("YOUR FRIENDS SEE YOUR INVITE", 350, C_DIM);
            button(BT_CANCEL, 8, 426, 156, 48, "CANCEL", false);
        } else {
            char level[24], side[24];
            snprintf(level, sizeof level, "LEVEL: %s", LV_NAME[C.level]);
            snprintf(side, sizeof side, "YOU: %s", C.my_side == WHITE ? "WHITE" : "BLACK");
            button(BT_CPU, 8, 278, 156, 48, "COMPUTER", true);
            button(BT_LEVEL, 8, 334, 156, 48, level, false);
            button(BT_SIDE, 8, 390, 156, 48, side, false);
            if (s_sg.state == 1) {
                // the standing game (and the parked game: two halves with the names), then INVITE and JOIN
                if (s_park.state == 1) {
                    button(BT_RESUME, 8, 446, 76, 48, peer_name(), true);
                    button(BT_PARKED, 88, 446, 76, 48, park_name(), true);
                } else {
                    button(BT_RESUME, 8, 446, 156, 48, "RESUME FRIEND", true);
                }
                if (sg_can_new() && s_ninv) {
                    button(BT_INVITE, 8, 502, 76, 48, "INVITE", false);
                    button(BT_JOIN, 88, 502, 76, 48, "JOIN", true);
                } else if (sg_can_new()) {
                    button(BT_INVITE, 8, 502, 156, 48, "INVITE FRIEND", false);
                }
            } else {
                button(BT_INVITE, 8, 446, 156, 48, "INVITE FRIEND", false);
                if (s_ninv) {
                    button(BT_JOIN, 8, 502, 156, 48, "JOIN FRIEND", true);
                }
            }
        }
    }
    button(BT_MENU, 8, 566, 156, 48, "MENU", false);
}
#if BOARD_BAR_SCREEN
// the 3.49 title: the rows of the friends (the standing game, the parked game, JOIN and INVITE) and the note
static void bar_friend_rows(const char *jn, bool standing)
{
    const int iy = standing ? 130 : 88;
    if (standing && s_park.state == 1) {
        char pk[24];
        snprintf(pk, sizeof pk, "THE GAME WITH %s", park_name());
        button(BT_RESUME, 186, 87, 214, 36, jn, true);
        button(BT_PARKED, 410, 87, 222, 36, pk, true);
    } else if (standing) {
        button(BT_RESUME, 186, 87, 446, 36, jn, true);
    }
    if (standing && search_now_ms() < s_note_until) {
        mf_text_c(&MF_TINY, 410, 40, s_note, C_DIM, 255, 1);  // (under the title: the rows below are full)
    }
    if (sg_can_new()) {
        if (s_ninv) {
            char jf[24];
            const char *f = friends_name_of(s_inv[0]);
            snprintf(jf, sizeof jf, "JOIN %s", friends_short_name(f));
            button(BT_JOIN, 186, iy, 214, 36, jf, true);
            button(BT_INVITE, 410, iy, 222, 36, "INVITE A FRIEND", false);
        } else {
            button(BT_INVITE, 186, iy, 214, 36, "INVITE A FRIEND", false);
        }
    }
    if (!standing && search_now_ms() < s_note_until) {
        mf_text_c(&MF_TINY, 410, 140, s_note, C_DIM, 255, 1);
    }
}
#endif
static void ch_render(int64_t now)
{
    (void)now;
    gfx_fill(C_BG);
    s_nbtn = 0;
    if (portrait()) {
        portrait_render();
        return;
    }
    char a[48], b[48];
    if (C.g == G_TITLE || C.g == G_WAIT) {
        // a board at the start at the back (not on the round screen), the title and the choices
        char lv[24], sd[24], jn[24] = "";
        snprintf(lv, sizeof lv, "LEVEL: %s", LV_NAME[C.level]);
        snprintf(sd, sizeof sd, "YOU: %s", C.my_side == WHITE ? "WHITE" : "BLACK");
        if (s_ninv) {
            const char *f = friends_name_of(s_inv[0]);
            snprintf(jn, sizeof jn, "JOIN %s", friends_short_name(f));
        }
        const bool standing = s_sg.state == 1;
        if (standing) {
            snprintf(jn, sizeof jn, "THE GAME WITH %s", peer_name());
        }
#if BOARD_BAR_SCREEN
        draw_board();
        mf_text_c(&MF_N24, 410, 30, C.g == G_WAIT ? "WAITING FOR A FRIEND" : "CHESS", C_YEL, 255, 1);
        if (C.g == G_WAIT) {
            mf_text_c(&MF_LABEL, 410, 56, "YOUR FRIENDS SEE YOUR INVITE", C_TXT, 255, 1);
            button(BT_CANCEL, 340, 120, 140, 36, "CANCEL", false);
            return;
        }
        button(BT_CPU, 186, 44, 214, 36, "PLAY THE COMPUTER", true);
        button(BT_LEVEL, 410, 44, 108, 36, lv, false);
        button(BT_SIDE, 524, 44, 108, 36, sd, false);
        bar_friend_rows(jn, standing);
#elif BOARD_ROUND
        {
            // the round title: the table and the board at the start (dark) behind the title and the pills, all in the
            // middle of the circle (the block of rows has its centre at the centre of the circle)
            const int D = gfx_w, R = D / 2, bh = D / 10, st = bh + D / 45, bw = D * 29 / 50, x0 = R - bw / 2;
            rd_table();
            draw_board();
            gfx_darken_rect(BX, BY, 8 * SQ, 8 * SQ, 2);
            if (C.g == G_WAIT) {
                static const mfont_t *const F[] = {&MF_N24, &MF_N20, &MF_LABEL};
                mf_text_o(rd_fit(F, 3, "WAITING FOR A FRIEND", D - 80),
                          R - mf_width(rd_fit(F, 3, "WAITING FOR A FRIEND", D - 80), "WAITING FOR A FRIEND", 1) / 2,
                          R - st, "WAITING FOR A FRIEND", C_YEL, rgb(0, 0, 0), 2, 1);
                mf_text_o(&MF_LABEL, R - mf_width(&MF_LABEL, "YOUR FRIENDS SEE YOUR INVITE", 1) / 2, R - st + 30,
                          "YOUR FRIENDS SEE YOUR INVITE", C_TXT, rgb(0, 0, 0), 1, 1);
                rd_pill(BT_CANCEL, R - bw / 3, R + bh / 2, bw * 2 / 3, bh, "CANCEL", false);
                return;
            }
            const int rows = 2 + (standing ? 1 : 0) + (sg_can_new() ? 1 + (s_ninv ? 1 : 0) : 0);
            const int top = R - (MF_N34.asc + 22 + rows * st) / 2;
            mf_text_o(&MF_N34, R - mf_width(&MF_N34, "CHESS", 2) / 2, top + MF_N34.asc, "CHESS", C_YEL, rgb(0, 0, 0), 3,
                      2);
            int y = top + MF_N34.asc + 22;
            rd_pill(BT_CPU, x0, y, bw, bh, "PLAY THE COMPUTER", true);
            y += st;
            rd_pill(BT_LEVEL, x0, y, bw / 2 - 4, bh, lv, false);
            rd_pill(BT_SIDE, R + 4, y, bw / 2 - 4, bh, sd, false);
            y += st;
            if (standing && s_park.state == 1) {
                rd_pill(BT_RESUME, x0, y, bw / 2 - 4, bh, peer_name(), true);
                rd_pill(BT_PARKED, R + 4, y, bw / 2 - 4, bh, park_name(), true);
                y += st;
            } else if (standing) {
                rd_pill(BT_RESUME, x0, y, bw, bh, jn, true);
                y += st;
            }
            if (sg_can_new()) {
                if (s_ninv) {
                    char jf[24];
                    const char *f = friends_name_of(s_inv[0]);
                    snprintf(jf, sizeof jf, "JOIN %s", friends_short_name(f));
                    rd_pill(BT_JOIN, x0, y, bw, bh, jf, true);
                    y += st;
                }
                rd_pill(BT_INVITE, x0, y, bw, bh, "INVITE A FRIEND", false);
                y += st;
            }
            if (search_now_ms() < s_note_until) {
                mf_text_o(&MF_TINY, R - mf_width(&MF_TINY, s_note, 1) / 2, y + 8, s_note, C_TXT, rgb(0, 0, 0), 1, 1);
            }
        }
#else
#if BOARD_UI_TABLET
        // (the Tab5: the board at the start at the left, the title and the choices in the panel at the right)
        const int cx = 496, y0 = 44, bw = 264, x0 = 364, bh = 50, st = 58;
        draw_board();
#else
        const int cx = 160, y0 = 26, bw = 260, x0 = 30, bh = 34, st = 38;
#endif
        mf_text_c(&MF_N24, cx, y0, C.g == G_WAIT ? (CH_TAB5 ? "WAITING" : "WAITING FOR A FRIEND") : "CHESS", C_YEL, 255,
                  1);
        if (C.g == G_WAIT) {
            mf_text_c(&MF_LABEL, cx, y0 + 30, CH_TAB5 ? "FOR A FRIEND" : "YOUR FRIENDS SEE YOUR INVITE", C_TXT, 255, 1);
            button(BT_CANCEL, cx - 70, y0 + 60, 140, bh + 2, "CANCEL", false);
            return;
        }
        int y = y0 + 14 + (bh - 30);
        button(BT_CPU, x0, y, bw, bh, "PLAY THE COMPUTER", true);
        y += st;
        button(BT_LEVEL, x0, y, bw / 2 - 3, bh, lv, false);
        button(BT_SIDE, x0 + bw / 2 + 3, y, bw / 2 - 3, bh, sd, false);
        y += st;
        if (standing && s_park.state == 1) {
            button(BT_RESUME, x0, y, bw / 2 - 3, bh, peer_name(), true);
            button(BT_PARKED, x0 + bw / 2 + 3, y, bw / 2 - 3, bh, park_name(), true);
            y += st;
        } else if (standing) {
            button(BT_RESUME, x0, y, bw, bh, jn, true);
            y += st;
        }
        if (sg_can_new()) {
            if (s_ninv) {
                char jf[24];
                const char *f = friends_name_of(s_inv[0]);
                snprintf(jf, sizeof jf, "JOIN %s", friends_short_name(f));
                button(BT_JOIN, x0, y, bw, bh, jf, true);
                y += st;
            }
            button(BT_INVITE, x0, y, bw, bh, "INVITE A FRIEND", false);
            y += st;
        }
        if (search_now_ms() < s_note_until) {
            mf_text_c(&MF_TINY, cx, y + 10, s_note, C_DIM, 255, 1);
        }
#endif
        return;
    }
#if BOARD_ROUND
    rd_table();
#endif
    draw_board();
    status_lines(a, b, sizeof a);
#if BOARD_BAR_SCREEN
    {
        // the right panel: the status, the small full board and the frame of the big view, the last move or the end
        const int px = 8 * LAND_SQ, cx = px + (640 - px) / 2;
        gfx_fill_rect(px, 0, 640 - px, 172, C_BG);
        gfx_fill_rect(px, 0, 2, 172, rgb(70, 58, 42));
        const char *top = C.g == G_OVER             ? C.result
                          : C.pos.side == C.my_side ? (in_check(&C.pos, C.pos.side) ? "CHECK! YOUR MOVE" : "YOUR MOVE")
                          : C.vs_cpu                ? "THE COMPUTER THINKS"
                                                    : "YOUR FRIEND MOVES";
        const mfont_t *tf = mf_width(&MF_LABEL, top, 1) <= 640 - px - 8 ? &MF_LABEL : &MF_TINY;
        mf_text_c(tf, cx, 17, top, C.g == G_OVER || in_check(&C.pos, C.pos.side) ? C_YEL : C_TXT, 255, 1);
        const int ms = MINI_SQ;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                const int sq = sq_of(col, row), x = MINI_X + col * ms, y = MINI_Y + row * ms;
                const bool light = ((sq >> 4) + (sq & 7)) & 1;
                uint16_t c = light ? rgb(238, 214, 170) : rgb(176, 130, 92);
                if (C.has_last && (sq == C.last.f || sq == C.last.t)) {
                    c = light ? rgb(246, 234, 130) : rgb(206, 186, 70);
                }
                if (sq == C.sel) {
                    c = rgb(120, 190, 120);
                }
                gfx_fill_rect(x, y, ms, ms, c);
            }
        }
        s_clip_y0 = 0;
        s_clip_y1 = 172;
        for (int row = 0; row < 8; row++) {
            for (int col = 0; col < 8; col++) {
                const int pc = C.pos.b[sq_of(col, row)];
                if (pc) {
                    draw_piece(MINI_X + col * ms + 1, MINI_Y + row * ms + 1, ms - 2, pc);
                }
            }
        }
        const int fy = MINI_Y + (int)lroundf(s_view_y * ms / LAND_SQ), fh = 172 * ms / LAND_SQ;
        gfx_frame_rect(MINI_X - 2, fy - 1, 8 * ms + 4, fh + 2, 2, C_YEL);
        const char *low = C.g == G_OVER ? "TAP FOR THE TITLE" : C.gt < 8 ? "TILT: UP / DOWN" : b;
        mf_text_c(&MF_TINY, cx, 168, low, C.g == G_OVER ? C_TXT : C_DIM, 255, 1);
    }
#elif BOARD_ROUND
    {
        // the top segment: the status (big) and the move or the reason of the end (small); each line fits the width of
        // the circle at its top
        const int R = gfx_w / 2, b8 = 8 * SQ;
        const bool chk = C.g == G_PLAY && in_check(&C.pos, C.pos.side);
        static const mfont_t *const FA[] = {&MF_N20, &MF_LABEL, &MF_TINY};
        const int ya = BY * 11 / 20, yb = BY - 13;
        const mfont_t *fa = rd_fit(FA, 3, a, 2 * rd_half(ya - MF_N20.asc) - 16);
        mf_text_c(fa, R, ya, a, C.g == G_OVER ? C_YEL : chk ? rgb(255, 120, 96) : C_TXT, 255, 1);
        const bool pend = C.sel != NOSQ && s_pend != NOSQ;
        const char *lb = pend           ? "TAP IT AGAIN TO MOVE"
                         : s_new_ask    ? "TAP NEW AGAIN FOR A NEW GAME"
                         : s_resign_ask ? "TAP AGAIN TO RESIGN"
                                        : b;
        static const mfont_t *const FB[] = {&MF_LABEL, &MF_TINY};
        mf_text_c(rd_fit(FB, 2, lb, 2 * rd_half(yb - MF_LABEL.asc) - 12), R, yb, lb,
                  pend || s_new_ask || s_resign_ask ? C_YEL : C_DIM, 255, 1);
        // the side segments: a round button at each side of the board, at the height of its middle
        const int r = (BX - 14) / 2, cl = 6 + r, cr = gfx_w - 6 - r;
        if (C.vs_cpu) {
            rd_cbutton(BT_UNDO, cl, R, r, "UNDO", false);
            rd_cbutton(BT_NEW, cr, R, r, s_new_ask ? "AGAIN" : "NEW", C.g == G_OVER || s_new_ask);
        } else {
            rd_cbutton(BT_TITLE, cl, R, r, "TITLE", C.g == G_OVER);
            if (C.g != G_OVER) {
                rd_cbutton(BT_RESIGN, cr, R, r, s_resign_ask ? "AGAIN" : "RESIGN", s_resign_ask != 0);
            }
        }
        // the bottom segment: the pieces that you took (at the left), the pieces that you lost (at the right)
        const int ps = SQ * 4 / 5, yt = BY + b8 + 8, room = rd_half(yt + ps) - 14;
        const int nt = rd_taken(-C.my_side, -1, yt, ps, room) + rd_taken(C.my_side, 1, yt, ps, room);
        if (nt) {
            ui_circle(R, yt + ps / 2, 2, rgb(150, 112, 58), 255);
        }
    }
#elif CH_TAB5
    {
        // the Tab5: the panel at the right of the board (272 px): the status, the side, the end, big buttons
        const int px = BX + 8 * SQ + 8, cx = px + (gfx_w - px) / 2, pw = gfx_w - px - 8;
        const bool away = !C.vs_cpu && C.g != G_OVER && duel_state() != DUEL_ON;
        gfx_fill_rect(px - 4, 0, 2, gfx_h, rgb(70, 58, 42));
        const mfont_t *af = mf_width(&MF_N20, a, 1) <= pw ? &MF_N20 : &MF_LABEL;
        mf_text_c(af, cx, 40, a, C.g == G_OVER || (C.g == G_PLAY && in_check(&C.pos, C.pos.side)) ? C_YEL : C_TXT, 255,
                  1);
        if (C.g == G_OVER) {
            mf_text_c(&MF_N24, cx, 90, C.win > 0 ? "YOU WIN" : C.win < 0 ? "YOU LOSE" : "A DRAW", C_YEL, 255, 1);
        } else if (C.g == G_PLAY && in_check(&C.pos, C.pos.side)) {
            mf_text_c(&MF_N24, cx, 90, "CHECK!", rgb(255, 110, 90), 255, 1);
        }
        char w[64];
        snprintf(w, sizeof w, "%s", C.g == G_OVER ? C.why : away ? "THE FRIEND IS AWAY: THE GAME WAITS" : b);
        const mfont_t *wf = mf_width(&MF_LABEL, w, 1) <= pw ? &MF_LABEL : &MF_TINY;
        mf_text_c(wf, cx, 126, w, C.g == G_OVER ? C_TXT : C_DIM, 255, 1);
        char sd[48];
        if (C.vs_cpu) {
            snprintf(sd, sizeof sd, "YOU: %s   %s", C.my_side == WHITE ? "WHITE" : "BLACK", LV_NAME[C.level]);
        } else {
            snprintf(sd, sizeof sd, "YOU: %s   %s", C.my_side == WHITE ? "WHITE" : "BLACK", peer_name());
        }
        mf_text_c(mf_width(&MF_LABEL, sd, 1) <= pw ? &MF_LABEL : &MF_TINY, cx, 160, sd, C_DIM, 255, 1);
        if (C.vs_cpu) {
            button(BT_UNDO, px, 216, pw, 56, "UNDO", false);
            button(BT_NEW, px, 284, pw, 56, "NEW", C.g == G_OVER);
        } else {
            button(BT_TITLE, px, 216, pw, 56, "TITLE", C.g == G_OVER);
            if (C.g != G_OVER) {
                button(BT_RESIGN, px, 284, pw, 56, s_resign_ask ? "AGAIN" : "RESIGN", false);
            }
        }
    }
#else
    // the narrow panel: short lines
    const int px = 236, cx = 276;
    const bool away = !C.vs_cpu && C.g != G_OVER && duel_state() != DUEL_ON;
    {
        // (the status in two short lines: MF_LABEL when it fits the panel, else MF_TINY)
        const char *l1 = C.g == G_OVER             ? "THE END"
                         : C.pos.side == C.my_side ? "YOUR"
                         : C.vs_cpu                ? "COMPUTER"
                                                   : "FRIEND";
        const char *l2 = C.g == G_OVER ? "" : C.pos.side == C.my_side ? "MOVE" : C.vs_cpu ? "THINKS" : "MOVES";
        mf_text_c(mf_width(&MF_LABEL, l1, 1) <= 82 ? &MF_LABEL : &MF_TINY, cx, 22, l1, C.g == G_OVER ? C_YEL : C_TXT,
                  255, 1);
        mf_text_c(mf_width(&MF_LABEL, l2, 1) <= 82 ? &MF_LABEL : &MF_TINY, cx, 40, l2, C_TXT, 255, 1);
    }
    if (C.g == G_PLAY && in_check(&C.pos, C.pos.side)) {
        mf_text_c(&MF_LABEL, cx, 56, "CHECK!", rgb(255, 110, 90), 255, 1);
    }
    if (C.g == G_OVER) {
        mf_text_c(&MF_TINY, cx, 56, C.win > 0 ? "YOU WIN" : C.win < 0 ? "YOU LOSE" : "A DRAW", C_YEL, 255, 1);
    }
    char mv[16];
    snprintf(mv, sizeof mv, "MOVE %d", C.pos.full);
    mf_text_c(mf_width(&MF_LABEL, mv, 1) <= 82 ? &MF_LABEL : &MF_TINY, cx, 78, mv, C_DIM, 255, 1);
    if (C.sel != NOSQ && s_pend != NOSQ) {
        mf_text_c(&MF_LABEL, cx, 108, "TAP IT", C_YEL, 255, 1);
        mf_text_c(&MF_LABEL, cx, 126, "AGAIN", C_YEL, 255, 1);
    }
    {
        // the reason of the end, or the friend is away: words in lines of the panel
        char w[48];
        snprintf(w, sizeof w, "%s", C.g == G_OVER ? C.why : away ? "THE FRIEND IS AWAY: THE GAME WAITS" : "");
        char line[48] = "";
        int y = 100;
        for (char *p = strtok(w, " "); p && y < 146; p = strtok(NULL, " ")) {
            char t[48];
            snprintf(t, sizeof t, "%s%s%s", line, line[0] ? " " : "", p);
            if (line[0] && mf_width(&MF_TINY, t, 1) > 78) {
                mf_text_c(&MF_TINY, cx, y, line, C.g == G_OVER ? C_TXT : C_DIM, 255, 1);
                y += 14;
                snprintf(line, sizeof line, "%s", p);
            } else {
                snprintf(line, sizeof line, "%s", t);
            }
        }
        if (line[0] && y < 146) {
            mf_text_c(&MF_TINY, cx, y, line, C.g == G_OVER ? C_TXT : C_DIM, 255, 1);
        }
    }
    if (C.vs_cpu) {
        button(BT_UNDO, px, 148, 78, 40, "UNDO", false);
        button(BT_NEW, px, 196, 78, 40, "NEW", C.g == G_OVER);
    } else {
        button(BT_TITLE, px, 148, 78, 40, "TITLE", C.g == G_OVER);
        if (C.g != G_OVER) {
            button(BT_RESIGN, px, 196, 78, 40, s_resign_ask ? "AGAIN" : "RESIGN", false);
        }
    }
#endif
    // the choice of a promotion
    if (C.promo_wait) {
        const int s = CH_TAB5       ? 64
                      : BOARD_ROUND ? SQ * 13 / 10
                                    : 40,
                  x = CH_TAB5            ? BX + 4 * SQ - 2 * (s + 6) + 3
                      : BOARD_BAR_SCREEN ? 186
                      : BOARD_ROUND      ? gfx_w / 2 - (4 * s + 18) / 2
                                         : 30,
                  y = CH_TAB5            ? BY + 4 * SQ - s / 2
                      : BOARD_BAR_SCREEN ? 80
                      : BOARD_ROUND      ? gfx_h / 2 - s / 2
                                         : 96;
        gfx_fill_rect(x - 6, y - 6, 4 * (s + 6) + 6, s + 12, rgb(20, 18, 16));
        static const int PR[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
        for (int i = 0; i < 4; i++) {
            const int bx = x + i * (s + 6);
            gfx_fill_rect(bx, y, s, s, rgb(238, 214, 170));
            gfx_frame_rect(bx, y, s, s, 2, C_YEL);
            s_clip_y0 = 0;
            s_clip_y1 = gfx_h;
            draw_piece(bx + 3, y + 3, s - 6, C.my_side * PR[i]);
            if (s_nbtn < 8) {
                s_btn[s_nbtn] = (rect_t){bx, y, s, s};
                s_btn_id[s_nbtn++] = BT_Q + i;
            }
        }
    }
}

// ---------------------------------------------------------------- the app
// The shared panel comes down from the top edge. Keep the first chess UI on the screen;
// its game actions are also here so the current in-app menu remains available.
enum {
    CI_UNDO,
    CI_LEVEL,
    CI_NEW,
    CI_RESIGN,
    CI_TITLE,
    CI_CENTER,
    CI_VIEW
};
static const char *const CV_VIEW[] = {"SWIPE", "החלקה", "TILT", "הטיה"};
static const char *const CV_LEVEL[] = {"EASY", "קל", "MEDIUM", "בינוני", "HARD", "קשה"};
static const pitem_t CH_ITEMS[] = {
        {PI_ACT, CI_CENTER, 0, 0, {"CENTER", "למרכז"}, NULL, NULL},
        {PI_PICK, CI_VIEW, 2, 0, {"VIEW", "תצוגה"}, CV_VIEW, NULL},
        {PI_ACT2, CI_UNDO, 0, 0, {"UNDO", "ביטול מהלך"}, NULL, NULL},
        {PI_PICK, CI_LEVEL, 3, LV_MEDIUM, {"LEVEL", "רמה"}, CV_LEVEL, NULL},
        {PI_ACT2, CI_NEW, 0, 0, {"NEW GAME", "משחק חדש"}, NULL, NULL},
        {PI_ACT2, CI_RESIGN, 0, 0, {"RESIGN", "כניעה"}, NULL, NULL},
        {PI_ACT, CI_TITLE, 0, 0, {"THE TITLE", "מסך הפתיחה"}, NULL, NULL},
};
static bool ch_item_shown(int id);
static int ch_item_get(int id);
static void ch_item_set(int id, int v);
static void ch_item_resume(void);
static const pitems_t CH_PANEL = {
        CH_ITEMS, sizeof CH_ITEMS / sizeof CH_ITEMS[0], ch_item_shown, ch_item_get, ch_item_set, NULL, ch_item_resume};

static void ch_enter(int64_t now)
{
    if (BOARD_BAR_SCREEN) {
        if (g_settings.auto_rotate && imu_ok()) {
            gfx_set_orient(imu_upright_any(gfx_orient(), 0.5f));
        }
    }
    s_touch_active = false;
    s_turn_since = 0;
    s_turn_target = gfx_orient();
    view_center();
    C_TXT = rgb(240, 232, 214);
    C_DIM = rgb(180, 166, 140);
    C_YEL = rgb(250, 210, 80);
    C_BG = rgb(28, 22, 18);
    art_build();
    memset(&C, 0, sizeof C);
    s_tilt = store_get("ch_tilt", 0) & 1;  // (VIEW: SWIPE first)
    C.level = (int)store_get("ch_lvl", LV_MEDIUM) % 3;
    C.my_side = store_get("ch_side", 0) ? BLACK : WHITE;
    pos_start(&C.pos);
    C.sel = NOSQ;
    C.anim_t = 1;
    s_pend = NOSQ;
    C.g = G_TITLE;
    s_last = now;
    s_ai = 0;
    s_ninv = 0;
    s_send_from = -1;
    s_resign_ask = 0;
    s_note_until = 0;
    duel_open('C');
    sg_load();
    if (s_sg.state != 1 && s_park.state == 1) {
        sg_swap();  // (the standing game ended: the parked game stands again)
    }
    // a standing game: the link to the friend goes on, but the app opens at the title (THE GAME WITH <name> opens the game;
    // the user can exit the app, return and choose again what to do)
    if (s_sg.state == 1) {
        duel_rejoin(s_sg.peer, s_sg.id);
    }
    sysui_panel_items(&CH_PANEL);
    JPLAY(CH_OPEN);
#ifdef ESP_PLATFORM
    if (!s_ai_task) {
        s_ai_quit = false;
        xTaskCreatePinnedToCoreWithCaps(ai_task, "chess", 32768, NULL, 2, &s_ai_task, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
#endif
}
static void ch_leave(void)
{
    art_free();
#ifdef ESP_PLATFORM
    S.stop = true;
    for (int k = 0; k < 50 && s_ai == 1; k++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_ai_task) {
        s_ai_quit = true;
        vTaskDelay(pdMS_TO_TICKS(60));
        vTaskDeleteWithCaps(s_ai_task);
        s_ai_task = NULL;
    }
#endif
    if (s_sg.state == 1) {
        duel_leave();
    } else {
        duel_close();  // (a standing game: no BYE, the friend sees AWAY)
    }
    audio_tune_stop();
}
static void do_button(int id)
{
    switch (id) {
    case BT_MENU:
        sysui_panel_open();
        break;
    case BT_CPU:
        game_new(true);
        break;
    case BT_LEVEL:
        C.level = (C.level + 1) % 3;
        store_set("ch_lvl", (uint32_t)C.level);
        sfx_play(SFX_CLICK);
        break;
    case BT_SIDE:
        C.my_side = -C.my_side;
        store_set("ch_side", C.my_side == BLACK);
        sfx_play(SFX_CLICK);
        break;
    case BT_INVITE:
        duel_invite();
        C.g = G_WAIT;
        sfx_play(SFX_CLICK);
        break;
    case BT_JOIN:
        if (s_ninv) {
            duel_join(s_inv[0]);
            C.g = G_WAIT;
            sfx_play(SFX_CLICK);
        }
        break;
    case BT_CANCEL:
        duel_stop();
        C.g = G_TITLE;
        if (s_sg.state == 1) {
            duel_rejoin(s_sg.peer, s_sg.id);
        }
        sfx_play(SFX_BACK);
        break;
    case BT_ZOOM:
        C.zoom = !C.zoom;
        sfx_play(SFX_CLICK);
        break;
    case BT_UNDO:
        undo();
        break;
    case BT_NEW:
        // (the round layout: NEW is next to the board; in a game it needs a second tap in 3 s)
        if (BOARD_ROUND && C.g == G_PLAY && (!s_new_ask || search_now_ms() - s_new_ask > 3000)) {
            s_new_ask = search_now_ms();
            sfx_tone(500, 400, 60, W_SQUARE, 26);
            break;
        }
        s_new_ask = 0;
        S.stop = true;
        C.g = G_TITLE;
        s_ai = 0;
        pos_start(&C.pos);
        C.has_last = false;
        C.sel = NOSQ;
        sfx_play(SFX_BACK);
        break;
    case BT_RESIGN:
        if (!s_resign_ask || search_now_ms() - s_resign_ask > 3000) {
            s_resign_ask = search_now_ms();
            sfx_tone(500, 400, 60, W_SQUARE, 26);
            break;
        }
        s_resign_ask = 0;
        {
            const uint8_t m[2] = {4, 1};
            duel_send(m, 2);
        }  // (away: it hears it when it comes back)
        s_sg.state = 2;
        s_sg.why = 1;
        sg_save();
        duel_keep(false);
        game_end(-1, C.my_side == WHITE ? "BLACK WINS" : "WHITE WINS", "YOU RESIGNED");
        break;
    case BT_TITLE:
        C.g = G_TITLE;
        C.sel = NOSQ;
        sfx_play(SFX_BACK);
        break;
    case BT_RESUME:
        sg_view();
        sfx_play(SFX_CLICK);
        break;
    case BT_PARKED:
        if (s_park.state == 1) {
            sg_swap();
            sg_view();
            sfx_play(SFX_CLICK);
        }
        break;
    case BT_Q:
    case BT_R:
    case BT_B:
    case BT_N: {
        static const int PR[4] = {QUEEN, ROOK, BISHOP, KNIGHT};
        cmove_t m = C.promo_mv;
        m.promo = (uint8_t)PR[id - BT_Q];
        C.promo_wait = false;
        play_move(m, !C.vs_cpu);
        break;
    }
    }
}
static bool ch_item_shown(int id)
{
    if (id == CI_CENTER) {
        return portrait_board() || land_board();
    }
    if (id == CI_VIEW) {
        return BOARD_BAR_SCREEN && imu_ok();
    }
    if (C.g != G_PLAY) {
        return false;
    }
    if (id == CI_UNDO) {
        return C.vs_cpu && s_ai != 1 && C.nhist > 0;
    }
    if (id == CI_LEVEL || id == CI_NEW) {
        return C.vs_cpu;
    }
    return !C.vs_cpu;
}
static int ch_item_get(int id)
{
    return id == CI_LEVEL ? C.level : id == CI_VIEW ? s_tilt : 0;
}
static void ch_item_set(int id, int v)
{
    if (id == CI_CENTER) {
        view_center();
    } else if (id == CI_VIEW) {
        s_tilt = v & 1;
        store_set("ch_tilt", s_tilt);
        if (s_tilt && imu_ok()) {
            imu_set_level();
        }
    } else if (id == CI_UNDO) {
        do_button(BT_UNDO);
    } else if (id == CI_LEVEL) {
        C.level = v % 3;
        store_set("ch_lvl", (uint32_t)C.level);
    } else if (id == CI_NEW) {
        s_new_ask = search_now_ms();
        do_button(BT_NEW);
    }  // (a hold: no second tap)
    else if (id == CI_RESIGN) {
        // PI_ACT2 already asks for a deliberate hold, so perform the resign now.
        s_resign_ask = search_now_ms();
        do_button(BT_RESIGN);
    } else if (id == CI_TITLE) {
        do_button(BT_TITLE);
    }
}
static int s_drag_y0, s_pan0;
static bool s_drag;
static void ch_item_resume(void)
{
    // As in the tilt games, use the current hold as neutral after the panel closes.
    if (BOARD_BAR_SCREEN && imu_ok()) {
        imu_set_level();
    }
    s_view_anchor = s_view_x;
    s_view_yanchor = s_view_y;
    s_touch_active = false;
    s_turn_since = 0;
    s_drag = false;
}
// the standing game on the screen (its moves again from the start)
static void sg_view(void)
{
    C.vs_cpu = false;
    C.my_side = s_sg.my_side;
    pos_start(&C.pos);
    C.nhist = 0;
    C.has_last = false;
    C.promo_wait = false;
    C.anim_t = 1;
    s_ai = 0;
    C.g = G_PLAY;
    C.gt = 0;
    cmove_t lm[MAXMV];
    for (int i = 0; i < s_sg.n; i++) {
        const int nl = legal_moves(&C.pos, lm);
        int k = 0;
        while (k < nl && !(lm[k].f == s_sg.mv[i][0] && lm[k].t == s_sg.mv[i][1] && lm[k].promo == s_sg.mv[i][2])) {
            k++;
        }
        if (k == nl) {
            s_sg.n = (uint16_t)i;
            break;
        }
        if (C.nhist < MAXGAME) {
            s_hist[C.nhist++] = C.pos;
        }
        cundo_t u;
        make_move(&C.pos, lm[k], &u);
        C.last = lm[k];
        C.has_last = true;
    }
    C.pan = C.my_side == WHITE ? 148 : 0;
    view_center();
    if (C.has_last) {
        int col, row;
        cell_of(C.last.t, &col, &row);
        const int want = row * 40 + 20 - 86;
        C.pan = want < 0 ? 0 : want > 148 ? 148 : want;
    }
    refresh_legal();
    s_quiet_end = true;
    after_move();
    s_quiet_end = false;
}
// a move of the friend: on the screen when the game is there, else only into the file
static void sg_take(int f, int t, int promo)
{
    if (!C.vs_cpu && (C.g == G_PLAY || C.g == G_OVER) && C.my_side == s_sg.my_side) {
        for (int i = 0; i < C.nlegal; i++) {
            if (C.legal[i].f == f && C.legal[i].t == t && C.legal[i].promo == promo) {
                play_move(C.legal[i], false);
                return;
            }
        }
        return;
    }
    cpos_t p;
    if (sg_position(&p) != s_sg.n || s_sg.n >= MAXGAME) {
        return;
    }
    cmove_t lm[MAXMV];
    const int nl = legal_moves(&p, lm);
    for (int i = 0; i < nl; i++) {
        if (lm[i].f == f && lm[i].t == t && lm[i].promo == promo) {
            s_sg.mv[s_sg.n][0] = (uint8_t)f;
            s_sg.mv[s_sg.n][1] = (uint8_t)t;
            s_sg.mv[s_sg.n][2] = (uint8_t)promo;
            s_sg.n++;
            cundo_t u;
            make_move(&p, lm[i], &u);
            if (legal_moves(&p, lm)) {
                snprintf(s_note, sizeof s_note, "%s MOVED: YOUR MOVE", peer_name());
            } else {
                s_sg.over = 1;
                snprintf(s_note, sizeof s_note, "%s",
                         in_check(&p, p.side) ? "CHECKMATE: YOU LOSE" : "A DRAW: STALEMATE");
            }
            sg_save();
            s_note_until = search_now_ms() + 60000;
            sfx_tone(900, 1300, 80, W_SQUARE, 26);
            return;
        }
    }
}
// a question of a board about its match id: true when game g has it (a game that this board resigned: tell it)
static bool sg_answer(const sg_t *g, const uint8_t mac[6], uint32_t id)
{
    if (g->state == 2 && id == g->id && !memcmp(mac, g->peer, 6)) {
        duel_say_ended(mac, id, g->why);
        return true;
    }
    return g->state == 1 && id == g->id;
}
// the standing game: the line, the messages, the end on the other board, the questions of other boards
static void sg_step(void)
{
    {
        uint8_t mac[6];
        uint32_t id;
        if (duel_asked(mac, &id) && !sg_answer(&s_sg, mac, id) && !sg_answer(&s_park, mac, id)) {
            duel_say_ended(mac, id, 0);
        }
    }
    if (s_sg.state != 1 && s_park.state == 1 && C.g == G_TITLE) {
        sg_swap();  // (the standing game ended: the parked game stands again)
    }
    if (s_sg.state != 1) {
        return;
    }
    const int e = duel_ended();
    if (e) {
        const bool viewing = !C.vs_cpu && (C.g == G_PLAY || C.g == G_OVER);
        s_sg.state = 0;
        sg_save();
        if (viewing) {
            game_end(e == 2 ? 1 : 0, e == 2 ? (C.my_side == WHITE ? "WHITE WINS" : "BLACK WINS") : "THE GAME ENDED",
                     e == 2 ? "YOUR FRIEND RESIGNED" : "THE GAME IS NOT ON THE OTHER BOARD");
        } else {
            snprintf(s_note, sizeof s_note, "%s", e == 2 ? "YOUR FRIEND RESIGNED: YOU WIN" : "THE GAME IS GONE");
            s_note_until = search_now_ms() + 60000;
        }
        return;
    }
    if (duel_fresh()) {
        sg_send_sync();
    }
    uint8_t m[DUEL_MSG_MAX];
    int n;
    while (s_sg.state == 1 && duel_recv(m, &n)) {
        if (m[0] == 1 && n >= 6) {
            const int ply = m[1] | (m[2] << 8);
            if (ply == s_sg.n) {
                sg_take(m[3], m[4], m[5]);
            } else {
                sg_send_sync();
            }
        } else if (m[0] == 2 && n >= 9) {
            uint32_t id;
            memcpy(&id, m + 1, 4);
            const int np = m[5] | (m[6] << 8), cp = m[7] | (m[8] << 8), peer_over = n >= 10 && m[9];
            if (id != s_sg.id) {
                continue;
            }
            if (s_sg.n >= np) {
                if (sg_crc(np) == cp) {
                    if (s_sg.n > np) {
                        s_send_from = np;
                        s_send_reset = false;
                    } else if (s_sg.over || peer_over) {  // (both have all the moves and the end: the game is done)
                        s_sg.over = 1;
                        sg_send_sync();
                        s_sg.state = 0;
                        sg_save();
                        duel_keep(false);
                    }
                } else if (s_sg.my_side == WHITE) {
                    s_send_from = 0;
                    s_send_reset = true;
                }  // (they do not agree: the host's moves)
            } else if (search_now_ms() - s_sync_at > 1000) {
                sg_send_sync();
            }
        } else if (m[0] == 3 && n >= 5) {
            const int start = m[1] | (m[2] << 8), cnt = m[3], reset = m[4];
            if (reset && start == 0) {
                s_sg.n = 0;
                sg_save();
                if (!C.vs_cpu && C.g != G_TITLE) {
                    sg_view();
                }
            }
            if (start != s_sg.n) {
                continue;
            }
            for (int k = 0; k < cnt && 5 + 3 * k + 2 < n; k++) {
                const int before = s_sg.n;
                sg_take(m[5 + 3 * k], m[6 + 3 * k], m[7 + 3 * k]);
                if (s_sg.n == before) {
                    break;
                }
            }
            sg_send_sync();
        } else if (m[0] == 4 && n >= 2) {
            const bool viewing = !C.vs_cpu && (C.g == G_PLAY || C.g == G_OVER);
            s_sg.state = 0;
            sg_save();
            duel_keep(false);
            if (viewing) {
                game_end(1, C.my_side == WHITE ? "WHITE WINS" : "BLACK WINS", "YOUR FRIEND RESIGNED");
            } else {
                snprintf(s_note, sizeof s_note, "YOUR FRIEND RESIGNED: YOU WIN");
                s_note_until = search_now_ms() + 60000;
            }
        }
    }
    // the moves that the friend has not got: a few in each message
    while (s_send_from >= 0 && s_send_from < s_sg.n && duel_state() == DUEL_ON) {
        const int cnt = s_sg.n - s_send_from > 10 ? 10 : s_sg.n - s_send_from;
        uint8_t b[5 + 30] = {3, (uint8_t)s_send_from, (uint8_t)(s_send_from >> 8), (uint8_t)cnt,
                             (uint8_t)(s_send_reset && s_send_from == 0)};
        for (int k = 0; k < cnt; k++) {
            memcpy(b + 5 + 3 * k, s_sg.mv[s_send_from + k], 3);
        }
        if (!duel_send(b, 5 + 3 * cnt)) {
            break;
        }
        s_send_from += cnt;
    }
    if (s_send_from >= s_sg.n) {
        s_send_from = -1;
    }
}
static void ch_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        s_touch_active = true;
        s_turn_since = 0;
    }
    if (ev->kind == IN_UP || ev->kind == IN_TAP) {
        s_touch_active = false;
    }
    // SWIPE: a drag on the board of the 3.49 moves the view (landscape: up and down; upright: left and right)
    if (!s_tilt && (land_board() || portrait_board()) && !C.promo_wait) {
        const bool land = land_board();
        if (ev->kind == IN_DOWN) {
            s_vdx0 = ev->x;
            s_vdy0 = ev->y;
            s_vdrag = false;
            s_vd0 = land ? s_view_y : s_view_x;
        } else if (ev->kind == IN_MOVE && !(land && ev->x >= MINI_X - 12)) {
            const int d = land ? ev->y - s_vdy0 : ev->x - s_vdx0;
            if (!s_vdrag && abs(d) > 10) {
                s_vdrag = true;
                input_tap_cancel();
            }
            if (s_vdrag) {
                const float lim = land ? land_limit() : portrait_limit();
                float v = s_vd0 - d;
                v = v < 0 ? 0 : v > lim ? lim : v;
                if (land) {
                    s_view_y = s_view_yanchor = v;
                } else {
                    s_view_x = s_view_anchor = v;
                }
            }
            return;
        } else if (ev->kind == IN_TAP && s_vdrag) {
            s_vdrag = false;
            return;
        }
        if (ev->kind == IN_UP && s_vdrag) {
            s_vdrag = false;
            return;
        }
    }
    // the 3.49 with ZOOM: a drag on the board moves it up and down
    if (board_zoom() && C.g >= G_PLAY && ev->x < 320) {
        if (ev->kind == IN_DOWN) {
            s_drag_y0 = ev->y;
            s_pan0 = C.pan;
            s_drag = false;
        } else if (ev->kind == IN_MOVE) {
            const int d = ev->y - s_drag_y0;
            if (abs(d) > 8) {
                s_drag = true;
            }
            if (s_drag) {
                C.pan = s_pan0 - d;
                C.pan = C.pan < 0 ? 0 : C.pan > 148 ? 148 : C.pan;
            }
        }
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    // the small board of the landscape game: a tap moves the view to that rank (never a move)
    if (land_board() && ev->x >= MINI_X - 12 && ev->y >= MINI_Y - 8 && ev->y < MINI_Y + 8 * MINI_SQ + 8) {
        const float row = (ev->y - MINI_Y) / (float)MINI_SQ;
        float want = row * LAND_SQ - 86;
        s_view_yanchor = want < 0 ? 0 : want > land_limit() ? land_limit() : want;
        if (s_tilt && imu_ok()) {
            imu_set_level();
        }
        sfx_play(SFX_TICK);
        return;
    }
    if (C.g == G_OVER && C.gt > 1.0f && button_at(ev->x, ev->y) < 0) {
        C.g = G_TITLE;
        pos_start(&C.pos);
        C.has_last = false;
        return;
    }
    const int id = s_nbtn ? button_at(ev->x, ev->y) : -1;
    if (id >= 0) {
        do_button(id);
        return;
    }
    if (C.g != G_PLAY || C.promo_wait || C.pos.side != C.my_side || s_ai == 1) {
        return;
    }
    const int sq = sq_at(ev->x, ev->y);
    if (sq == NOSQ) {
        C.sel = NOSQ;
        s_pend = NOSQ;
        return;
    }
    if (C.sel != NOSQ && is_target(sq)) {
        if ((sq_size() < PEND_SQ || BOARD_ROUND) && !(sq == s_pend && search_now_ms() - s_pend_ms >= 250)) {
            // (the first tap on a small target, or a bounce of the second: no move yet)
            if (sq != s_pend) {
                s_pend = sq;
                s_pend_ms = search_now_ms();
                sfx_tone(700, 900, 25, W_SQUARE, 18);
            }
            return;
        }
        s_pend = NOSQ;
        for (int i = 0; i < C.nlegal; i++) {
            if (C.legal[i].f != C.sel || C.legal[i].t != sq) {
                continue;
            }
            if (C.legal[i].promo) {
                C.promo_wait = true;
                C.promo_mv = C.legal[i];
                return;
            }
            play_move(C.legal[i], !C.vs_cpu);
            return;
        }
    }
    s_pend = NOSQ;
    if (C.pos.b[sq] * C.my_side > 0) {
        C.sel = sq;
        sfx_tone(700, 700, 20, W_SQUARE, 18);
    } else {
        C.sel = NOSQ;
    }
}
static void ch_update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    if (BOARD_BAR_SCREEN && imu_ok() && !s_touch_active) {
        // The same upright selector as the menu, with the BOOKS 400 ms hold to avoid flicker.
        orient_t want = g_settings.auto_rotate ? imu_upright_any(gfx_orient(), 0.5f) : gfx_orient();
        if (want == gfx_orient()) {
            s_turn_since = 0;
        } else if (!s_turn_since || want != s_turn_target) {
            s_turn_target = want;
            s_turn_since = now;
        } else if (now - s_turn_since >= 400) {
            gfx_set_orient(want);
            C.zoom = false;
            s_nbtn = 0;
            s_drag = false;
            s_turn_since = 0;
            view_center();
        }
        if (land_board() && !C.promo_wait && s_tilt) {
            float tx, ty;
            imu_tilt_logical(&tx, &ty);
            (void)tx;
            // the curve of the portrait view; the top away (ty < 0) shows the ranks far from you
            float u = (fabsf(ty) - 0.026f) / (0.105f - 0.026f);
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            u = u * u * (3 - 2 * u);
            const float target = s_view_yanchor + (ty >= 0 ? land_limit() - s_view_yanchor : -s_view_yanchor) * u;
            s_view_y += (target - s_view_y) * (1 - expf(-dt / 0.12f));
            if (fabsf(target - s_view_y) < 0.1f) {
                s_view_y = target;
            }
        }
        if (portrait_board() && !C.promo_wait && s_tilt) {
            float tx, ty;
            imu_tilt_logical(&tx, &ty);
            (void)ty;
            // KEEPER's 1.5..6 degree smooth curve; CRUISE's positive tilt reveals the right side.
            float u = (fabsf(tx) - 0.026f) / (0.105f - 0.026f);
            u = u < 0 ? 0 : u > 1 ? 1 : u;
            u = u * u * (3 - 2 * u);
            const float target = s_view_anchor + (tx >= 0 ? portrait_limit() - s_view_anchor : -s_view_anchor) * u;
            s_view_x += (target - s_view_x) * (1 - expf(-dt / 0.12f));
            if (fabsf(target - s_view_x) < 0.1f) {
                s_view_x = target;
            }
        }
    }
    if (!s_tilt && !s_vdrag) {
        // SWIPE: the view glides to its anchor (CENTER, the small board, a move of the other side out of the view)
        s_view_y += (s_view_yanchor - s_view_y) * (1 - expf(-dt / 0.12f));
        s_view_x += (s_view_anchor - s_view_x) * (1 - expf(-dt / 0.12f));
    }
    C.gt += dt;
    C.anim_t += dt;
    duel_poll(now);
    s_ninv = duel_invites(s_inv, 3);
    const int ds = duel_state();
    if (C.g == G_WAIT) {
        if (ds == DUEL_ON) {
            game_new(false);
        } else if (ds == DUEL_GONE) {
            C.g = G_TITLE;
            if (s_sg.state == 1) {
                duel_rejoin(s_sg.peer, s_sg.id);
            }
        }
    }
    sg_step();
    if (s_resign_ask && search_now_ms() - s_resign_ask > 3000) {
        s_resign_ask = 0;
    }
    if (s_new_ask && search_now_ms() - s_new_ask > 3000) {
        s_new_ask = 0;
    }
    if (C.g == G_PLAY && C.vs_cpu && s_ai == 2 && C.pos.side != C.my_side) {
        s_ai = 0;
        ESP_LOGI(TAG, "the computer (%s): depth %d, %u nodes, %d ms", LV_NAME[C.level], C.depth, (unsigned)C.nodes,
                 (int)C.think_ms);
        for (int i = 0; i < C.nlegal; i++) {
            if (C.legal[i].f == s_ai_move.f && C.legal[i].t == s_ai_move.t && C.legal[i].promo == s_ai_move.promo) {
                play_move(C.legal[i], false);
                break;
            }  // (only a legal move: an old answer after NEW or UNDO is not played)
        }
    }
}
#include "tfdraw/tfdraw.h"
// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set is in each app; all are static, and an unused one gives no warning)
#define IC_FN static __attribute__((unused))
static struct {
    uint16_t *fb;
    int w, h, c[4];
} ic_keep __attribute__((unused));
// start an icon: tfdraw draws on the screen and only in the box; ic_end gives tfdraw its old target and clip back
IC_FN void ic_begin(int x, int y, int s)
{
    ic_keep.fb = tfd_fb;
    ic_keep.w = tfd_w;
    ic_keep.h = tfd_h;
    ic_keep.c[0] = tfd_cx0;
    ic_keep.c[1] = tfd_cy0;
    ic_keep.c[2] = tfd_cx1;
    ic_keep.c[3] = tfd_cy1;
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
}
IC_FN void ic_end(void)
{
    tfd_target(ic_keep.fb, ic_keep.w, ic_keep.h);
    tfd_clip(ic_keep.c[0], ic_keep.c[1], ic_keep.c[2], ic_keep.c[3]);
}
// the rows j0 .. j1 - 1 of the round tile (the corner radius of the menu frame), a gradient from c0 to c1
IC_FN void ic_band(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    int r = s / 8 + 2;
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        uint16_t c = tfd_mix(c0, c1, j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        if (dy <= 0) {
            tfd_hspan(x, x + s, y + j, c, 256);
            continue;
        }
        int in = tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy)));
        tfd_hspan(x + in + 1, x + s - in - 1, y + j, c, 256);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, y + j, c, a);
            tfd_px(x + s - 1 - k, y + j, c, a);
        }
    }
}
// the box inside the round corners (a shape in it does not cover a corner)
IC_FN void ic_inner(int x, int y, int s)
{
    int k = (s / 8 + 2) * 3 / 10 + 1;
    tfd_clip(x + k, y + k, x + s - k, y + s - k);
}
// a filled ellipse with a smooth edge
IC_FN void ic_oval(float cx, float cy, float rx, float ry, uint16_t c, int a)
{
    if (rx < 0.5f || ry < 0.5f) {
        return;
    }
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = fabsf(yy + 0.5f - cy), v = ry + 0.5f - d;
        if (v <= 0) {
            continue;
        }
        float q = d < ry ? d / ry : 1, hw = rx * sqrtf(1 - q * q), l = cx - hw, r = cx + hw;
        int aa = v >= 1 ? a : (int)(a * v);
        int il = tfd_ce(l), ir = tfd_fl(r);
        if (il > ir) {
            tfd_px(ir, yy, c, (int)(aa * (r - l)));
            continue;
        }
        tfd_hspan(il, ir, yy, c, aa);
        tfd_px(il - 1, yy, c, (int)(aa * (il - l)));
        tfd_px(ir, yy, c, (int)(aa * (r - ir)));
    }
}
// a ball in the light from the upper left: the dark rim, the body, the shine
IC_FN void ic_ball(float cx, float cy, float r, uint16_t dark, uint16_t body, uint16_t shine)
{
    tfd_disc(cx, cy, r, dark, 256);
    tfd_disc(cx - r * 0.1f, cy - r * 0.12f, r * 0.84f, body, 256);
    tfd_disc(cx - r * 0.34f, cy - r * 0.38f, r * 0.27f, shine, 200);
}
// a shape of n points (x, y pairs round 0, 0), turned by ang, k times bigger, at cx, cy
IC_FN void ic_shape(const float *p, int n, float cx, float cy, float k, float ang, uint16_t c, int a)
{
    float q[64], cs = fcos(ang) * k, sn = fsin(ang) * k;
    if (n > 32) {
        n = 32;
    }
    for (int i = 0; i < n; i++) {
        q[2 * i] = cx + p[2 * i] * cs - p[2 * i + 1] * sn;
        q[2 * i + 1] = cy + p[2 * i] * sn + p[2 * i + 1] * cs;
    }
    tfd_poly(q, n, c, a);
}
// A store app shows the frames of icon.bin, with at most 255 colours for all 16 frames: mk_app.py drops the low bits
// of each colour until they fit (most icons: 3 bits of red, 4 of green, 3 of blue). A colour on that grid stays as it
// is: ic_g gives the nearest one. ic_dband makes a gradient of grid colours with an ordered dither (few colours, no
// bands).
IC_FN uint16_t ic_g(int r, int g, int b)
{
    r = (r + 16) / 32 * 32;
    g = (g + 8) / 16 * 16;
    b = (b + 16) / 32 * 32;
    return rgb((uint8_t)(r > 224 ? 224 : r), (uint8_t)(g > 240 ? 240 : g), (uint8_t)(b > 224 ? 224 : b));
}
IC_FN void ic_dband(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    static const uint8_t IC_BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int r0, g0, b0, r1, g1, b1, r = s / 8 + 2;
    tfd_unrgb(c0, &r0, &g0, &b0);
    tfd_unrgb(c1, &r1, &g1, &b1);
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        int yy = y + j;
        if (yy < tfd_cy0 || yy >= tfd_cy1 || (unsigned)yy >= (unsigned)tfd_h) {
            continue;
        }
        // (the colour of the row in 1/16 steps of a colour value; a grid step is 32 = 512 for red and blue, 16 = 256
        // for green)
        int t = j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0;
        int fr = r0 * 16 + ((r1 - r0) * 16 * t >> 8), fg = g0 * 16 + ((g1 - g0) * 16 * t >> 8),
            fb = b0 * 16 + ((b1 - b0) * 16 * t >> 8);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        int in = dy > 0 ? tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy))) : -1;
        uint16_t *row = tfd_fb + yy * tfd_w;
        for (int i = in + 1; i < s - in - 1; i++) {
            int xx = x + i;
            if (xx < tfd_cx0 || xx >= tfd_cx1 || (unsigned)xx >= (unsigned)tfd_w) {
                continue;
            }
            int th = IC_BAYER[(j & 3) * 4 + (i & 3)];
            int lr = (fr >> 9) + (((fr >> 5) & 15) > th), lg = (fg >> 8) + (((fg >> 4) & 15) > th),
                lb = (fb >> 9) + (((fb >> 5) & 15) > th);
            row[xx] = rgb((uint8_t)(lr > 7 ? 224 : lr * 32), (uint8_t)(lg > 15 ? 240 : lg * 16),
                          (uint8_t)(lb > 7 ? 224 : lb * 32));
        }
        uint16_t c = ic_g(fr >> 4, fg >> 4, fb >> 4);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, yy, c, a);
            tfd_px(x + s - 1 - k, yy, c, a);
        }
    }
}
// a span of one row from l to r (in pixels, with soft ends)
IC_FN void ic_span(float l, float r, int yy, uint16_t c, int a)
{
    if (r <= l) {
        return;
    }
    int il = tfd_ce(l), ir = tfd_fl(r);
    if (il > ir) {
        tfd_px(ir, yy, c, (int)(a * (r - l)));
        return;
    }
    tfd_hspan(il, ir, yy, c, a);
    tfd_px(il - 1, yy, c, (int)(a * (il - l)));
    tfd_px(ir, yy, c, (int)(a * (r - ir)));
}
// the ring of an ellipse (rx, ry: the outer radii; w: its width), made of spans: cheap also when it is big
IC_FN void ic_ering(float cx, float cy, float rx, float ry, float w, uint16_t c, int a)
{
    float ix = rx - w, iy = ry - w;
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = yy + 0.5f - cy, qo = d / ry;
        if (qo * qo >= 1) {
            continue;
        }
        float ho = rx * sqrtf(1 - qo * qo), qi = iy > 0 ? d / iy : 2;
        if (qi * qi >= 1) {
            ic_span(cx - ho, cx + ho, yy, c, a);
            continue;
        }
        float hi = ix * sqrtf(1 - qi * qi);
        ic_span(cx - ho, cx - hi, yy, c, a);
        ic_span(cx + hi, cx + ho, yy, c, a);
    }
}
// the icon: a corner of the board (round corners) with the black king at the back; the white knight jumps over the
// squares and back, with its shadow on the board (a loop of 2 s). The pieces are the pictures of the game.
static void ch_icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    const int q = s / 4, r = s / 8 + 2;
    const uint16_t lt = rgb(238, 214, 170), dk = rgb(176, 130, 92);
    for (int j = 0; j < s; j++) {
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        int in = dy > 0 ? tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy))) : 0;
        for (int c = 0; c < 4; c++) {
            int a = c * q, b = c == 3 ? s : (c + 1) * q;
            if (a < in) {
                a = in;
            }
            if (b > s - in) {
                b = s - in;
            }
            tfd_hspan(x + a, x + b, y + j, ((j / q + c) & 1) ? dk : lt, 256);
        }
    }
    art_build();  // (it loads only when the pictures are not there)
    s_clip_y0 = 0;
    s_clip_y1 = gfx_h;
    float ph = fmodf(icon_time(), 2.0f) * 0.5f;
    // the jump: rest at A, jump to B, rest at B, jump back (an arc)
    float m = ph < 0.3f ? 0 : ph < 0.5f ? (ph - 0.3f) / 0.2f : ph < 0.8f ? 1 : 1 - (ph - 0.8f) / 0.2f;
    float e = m * m * (3 - 2 * m), arc = (ph > 0.3f && ph < 0.5f) || ph > 0.8f ? fsin(m * 3.14159f) : 0;
    float ks = s * 0.46f, ax = x + s * 0.04f, ay = y + s * 0.5f, bx = x + s * 0.18f, by = y + s * 0.14f;
    float kx = ax + (bx - ax) * e, ky = ay + (by - ay) * e - arc * s * 0.16f;
    float gy = ay + (by - ay) * e + ks * 0.88f;
    ic_oval(kx + ks * 0.5f, gy, ks * (0.32f - 0.1f * arc), ks * (0.08f - 0.03f * arc), rgb(90, 60, 40), 110);
    draw_piece(x + (int)(s * 0.5f), y + (int)(s * 0.0f), (int)(s * 0.48f), -KING);
    draw_piece((int)kx, (int)ky, (int)ks, KNIGHT);
    ic_end();
}
const app_t app_chess = {
        .name = "CHESS",
        .landscape = !BOARD_BAR_SCREEN,
        .enter = ch_enter,
        .leave = ch_leave,
        .input = ch_input,
        .update = ch_update,
        .render = ch_render,
        .draw_icon = ch_icon,
        .round = APP_ROUND_HI,
};
