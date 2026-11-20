// STACK: build a tall tower on the portrait screen (172 x 640), or on the landscape screen of the CoreS3 (320 x 240).
// The task: the app task of TFN OS calls the hooks of app_stack.
// The memory: the file statics only (no heap).
//
// A block slides from side to side above the tower. Touch the screen to drop it.
// The part that hangs over the edge falls off. A drop that is almost exact
// counts as perfect: after 3 perfect drops in a row, the block grows again.
// The feel: the tower drops a little at each landing, the score jumps, a perfect drop gives sparks, the cut part
// falls away from the tower, a miss shakes the view. The big words and numbers use the Bungee faces (mfont.h).

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/tunes.h"
#include "tfn/gfx.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"

#if BOARD_BAR_SCREEN
#define SCR_W      172
#define SCR_H      640
#define SLAB_H     18
#define BASE_W     110
#define BASE_Y     600  // screen y of the bottom of the tower at the start
#define TOP_Y      210  // the moving block stays near this height on a tall tower
#define PERFECT_PX 3.0f
#define SPEED0     75.0f  // px/s of the moving block: at the start, more for each block, the top
#define SPEED_N    5.0f
#define SPEED_MAX  250.0f
#define SCORE_Y    40
#define COMBO_Y    96
#elif BOARD_UI_TABLET
// The Tab5 (landscape, 640 x 360): the CoreS3 game at 2 times the width. The block goes
// 640 px, so the speed is 2 times the CoreS3 speed (a pass takes the same time).
#define SCR_W      640
#define SCR_H      360
#define SLAB_H     24
#define BASE_W     260
#define BASE_Y     340
#define TOP_Y      168
#define PERFECT_PX 7.0f
#define SPEED0     210.0f
#define SPEED_N    14.0f
#define SPEED_MAX  700.0f
#define SCORE_Y    10
#define COMBO_Y    70
#define COMBO_S    3  // the scale of the PERFECT text
#elif BOARD_ROUND
// THE ROUND PANELS (APP_ROUND_HI): the game draws a square view of RV_S x RV_S (stack_rv.inc) at the sizes of the
// CoreS3, and the view is scaled out into the whole circle. The block slides through the wide middle rows of the
// circle; the tower stands on the ground in the bottom cap; the score is in the top cap.
#define K_RND      1
#define RV_S       311
#define SCR_W      RV_S
#define SCR_H      RV_S
#define SLAB_H     16
#define BASE_W     130
#define BASE_Y     268
#define TOP_Y      140
#define PERFECT_PX 4.0f
#define SPEED0     105.0f
#define SPEED_N    7.0f
#define SPEED_MAX  350.0f
#define SCORE_Y    34
#define COMBO_Y    82
#else
// The CoreS3 (landscape): the block goes 320 px, so it is faster (a pass takes about the
// same time), and the base and the perfect window are a little wider.
#define SCR_W      320
#define SCR_H      240
#define SLAB_H     16
#define BASE_W     130
#define BASE_Y     226
#define TOP_Y      112
#define PERFECT_PX 4.0f
#define SPEED0     105.0f
#define SPEED_N    7.0f
#define SPEED_MAX  350.0f
#define SCORE_Y    8
#define COMBO_Y    48
#endif
#ifndef COMBO_S
#define COMBO_S 2
#endif
#ifndef K_RND
#define K_RND 0
#endif
#include "esp_heap_caps.h"
#include "stack_rv.inc"
#define TY        (K_RND ? 44 : 0)  // (the round frame: the text rows of the CoreS3 lower, in the circle)
#define MAX_SLABS 600
#define N_DEBRIS  8
#define N_SPARK   32
#define SPARK_PX  (BOARD_UI_TABLET ? 4 : 3)  // the size of a spark

typedef struct {
    float x, w;
} slab_t;
// a part that falls: y is in the units of the tower (the screen y with no scroll), so it does not jump when the
// view scrolls
typedef struct {
    float x, y, w, vx, vy;
    int idx;
    bool on;
} debris_t;
typedef struct {
    float x, y, vx, vy, life;  // y: the units of the tower; life: s left
    uint16_t c;
} spark_t;
typedef enum {
    ST_TITLE,
    ST_PLAY,
    ST_OVER
} stack_state_t;

static slab_t s_slab[MAX_SLABS];
static int s_n;                 // blocks in the tower (block 0 = base)
static float s_mx, s_mw, s_mv;  // moving block: x, width, speed (+ = right)
static debris_t s_debris[N_DEBRIS];
static stack_state_t s_state;
static int s_combo;
static int s_hue0;
static float s_scroll, s_scroll_target;
static uint32_t s_best;
static bool s_new_best;
static int64_t s_last, s_over_at, s_perfect_at, s_land_at;
static uint16_t s_c_text, s_c_accent;
static spark_t s_spark[N_SPARK];
static float s_shake;  // the shake of the view after a miss (px, it decays)

// ---------------------------------------------------------------- helpers

static uint16_t slab_color(int i, int dv)
{
    int v = 225 + dv;
    if (v > 255) {
        v = 255;
    }
    if (v < 0) {
        v = 0;
    }
    return hsv(s_hue0 + i * 7, 150, v);
}

// screen y of the top edge of block i
static int slab_y(int i)
{
    return (int)(BASE_Y - (i + 1) * SLAB_H + s_scroll);
}

static float speed_for(int n)
{
    float v = SPEED0 + SPEED_N * n;
    return v > SPEED_MAX ? SPEED_MAX : v;
}

static void next_block(void)
{
    s_mw = s_slab[s_n - 1].w;
    if (s_n & 1) {
        s_mx = -s_mw * 0.4f;
        s_mv = speed_for(s_n);
    } else {
        s_mx = SCR_W - s_mw * 0.6f;
        s_mv = -speed_for(s_n);
    }
    float tower_top = (float)(s_n + 1) * SLAB_H;
    s_scroll_target = tower_top > (BASE_Y - TOP_Y) ? tower_top - (BASE_Y - TOP_Y) : 0;
}

// the y of the top edge of block i in the units of the tower (no scroll)
static float slab_wy(int i)
{
    return (float)(BASE_Y - (i + 1) * SLAB_H);
}

static float frnd(float lo, float hi)
{
    return lo + (hi - lo) * (float)(esp_random() % 1000) / 1000.0f;
}

// a part that falls; dir: -1 = to the left of the tower, 1 = to the right (it falls away from the tower)
static void add_debris(float x, float w, int idx, float dir)
{
    for (int i = 0; i < N_DEBRIS; i++) {
        if (!s_debris[i].on) {
            s_debris[i] = (debris_t){.x = x,
                                     .y = slab_wy(idx),
                                     .w = w,
                                     .vx = dir * frnd(20, 45),
                                     .vy = -frnd(40, 90),
                                     .idx = idx,
                                     .on = true};
            return;
        }
    }
}

// n sparks at the point x, y (the units of the tower), up and to the side dir (-1, 1; 0 = both sides)
static void add_sparks(float x, float y, int n, float dir, uint16_t c)
{
    for (int i = 0; i < N_SPARK && n > 0; i++) {
        spark_t *p = &s_spark[i];
        if (p->life > 0) {
            continue;
        }
        float d = dir != 0 ? dir : (n & 1 ? 1.0f : -1.0f);
        *p = (spark_t){
                .x = x, .y = y, .vx = d * frnd(30, 140), .vy = -frnd(80, 260), .life = frnd(0.35f, 0.7f), .c = c};
        n--;
    }
}

static void start_game(int64_t now)
{
    memset(s_debris, 0, sizeof(s_debris));
    memset(s_spark, 0, sizeof(s_spark));
    s_shake = 0;
    s_land_at = 0;
    s_hue0 = (int)(esp_random() % 360);
    s_slab[0] = (slab_t){(SCR_W - BASE_W) / 2.0f, BASE_W};
    s_n = 1;
    s_combo = 0;
    s_scroll = s_scroll_target = 0;
    s_new_best = false;
    s_state = ST_PLAY;
    next_block();
}

static void game_over(int64_t now)
{
    s_state = ST_OVER;
    s_over_at = now;
    const slab_t *top = &s_slab[s_n - 1];
    add_debris(s_mx, s_mw, s_n, s_mx + s_mw / 2 < top->x + top->w / 2 ? -1.0f : 1.0f);
    s_shake = BOARD_BAR_SCREEN ? 7.0f : 6.0f;
    sfx_play(SFX_GAME_OVER);
    uint32_t score = (uint32_t)(s_n - 1);
    s_new_best = score > s_best;
    if (s_new_best) {
        s_best = score;
        store_set("stack_best", s_best);
    }
    s_scroll_target = 0;  // pan down along the whole tower
}

static void drop(int64_t now)
{
#if !BOARD_BAR_SCREEN
    // The CoreS3 draws about 30 frames a second: move the block to the time of the
    // touch, so that the frame step does not change the result.
    float de = (now - s_last) / 1000.0f;
    if (de > 0.05f) {
        de = 0.05f;
    }
    if (de > 0) {
        s_mx += s_mv * de;
        float lo = -s_mw * 0.5f, hi = SCR_W - s_mw * 0.5f;
        if (s_mx < lo) {
            s_mx = lo;
            s_mv = fabsf(s_mv);
        }
        if (s_mx > hi) {
            s_mx = hi;
            s_mv = -fabsf(s_mv);
        }
        s_last = now;
    }
#endif
    const slab_t *top = &s_slab[s_n - 1];
    float l = s_mx > top->x ? s_mx : top->x;
    float r = (s_mx + s_mw) < (top->x + top->w) ? (s_mx + s_mw) : (top->x + top->w);
    if (r - l <= 0.5f) {
        game_over(now);
        return;
    }
    slab_t nb;
    if (fabsf(s_mx - top->x) <= PERFECT_PX) {
        s_combo++;
        nb.x = top->x;
        nb.w = top->w;
        if (s_combo >= 3 && nb.w < BASE_W) {
            nb.w += 8;
            nb.x -= 4;
            if (nb.w > BASE_W) {
                nb.w = BASE_W;
            }
        }
        s_perfect_at = now;
        // a higher note for each perfect drop in a row
        int semis = s_combo > 12 ? 12 : s_combo;
        uint16_t f = (uint16_t)(523.0f * powf(2.0f, semis / 12.0f));
        sfx_tone(f, f, 160, W_PULSE, 22);
        sfx_tone(f / 2, f / 2, 160, W_TRI, 60);
        // sparks from the two top corners (more for a longer row of perfect drops)
        int n = 5 + (s_combo > 5 ? 5 : s_combo);
        float wy = slab_wy(s_n);
        add_sparks(nb.x, wy, n, -1, rgb(255, 250, 220));
        add_sparks(nb.x + nb.w, wy, n, 1, slab_color(s_n, 60));
    } else {
        s_combo = 0;
        nb.x = l;
        nb.w = r - l;
        // the part that hangs over: it falls away from the tower; crumbs at the cut
        if (s_mx < l) {
            add_debris(s_mx, l - s_mx, s_n, -1);
            add_sparks(l, slab_wy(s_n) + SLAB_H / 2, 4, -1, slab_color(s_n, 30));
        } else {
            add_debris(r, s_mx + s_mw - r, s_n, 1);
            add_sparks(r, slab_wy(s_n) + SLAB_H / 2, 4, 1, slab_color(s_n, 30));
        }
        sfx_play(SFX_CUT);
        sfx_play(SFX_PLACE);
    }
    if (s_n < MAX_SLABS) {
        s_slab[s_n++] = nb;
    }
    s_land_at = now;
    next_block();
}

// ---------------------------------------------------------------- app

static void stack_init(void)
{
    s_best = store_get("stack_best", 0);
    s_c_text = rgb(250, 250, 255);
    s_c_accent = rgb(255, 225, 90);
    s_hue0 = 200;
    s_slab[0] = (slab_t){(SCR_W - BASE_W) / 2.0f, BASE_W};
    s_n = 1;
    s_state = ST_TITLE;
}

// the settings panel: RESTART (a hold of 1 s, only in a game: the tower is lost)
static const pitem_t STK_ITEMS[] = {
        {PI_ACT2, 0, 0, 0, {"RESTART", "מחדש"}, NULL, NULL},
};
static bool stk_item_shown(int id)
{
    (void)id;
    return s_state == ST_PLAY;
}
static void stk_item_set(int id, int v)
{
    (void)id;
    (void)v;
    start_game(s_last);
    tune_play(TUNE_STACK);
}
static const pitems_t STK_PANEL = {STK_ITEMS, 1, stk_item_shown, NULL, stk_item_set, NULL, NULL};

static void stack_enter(int64_t now)
{
    s_last = now;
    sysui_panel_items(&STK_PANEL);
}

static void stack_input_v(const input_event_t *ev);
static void stack_input(const input_event_t *ev)
{
#if K_RND
    input_event_t e = *ev;  // (the round panels: the point in the view)
    int ex = e.x, ey = e.y;
    rv_pt(&ex, &ey);
    e.x = (int16_t)ex;
    e.y = (int16_t)ey;
    stack_input_v(&e);
#else
    stack_input_v(ev);
#endif
}
static void stack_input_v(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        // react on the touch itself: this game needs exact timing
        if (s_state == ST_PLAY) {
            drop(ev->t_ms);
        }
        break;
    case IN_TAP:
        if (s_state == ST_TITLE || (s_state == ST_OVER && ev->t_ms - s_over_at > 900)) {
            start_game(ev->t_ms);
            tune_play(TUNE_STACK);
        }
        break;
    default:
        break;
    }
}

static void stack_update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.05f) {
        dt = 0.05f;
    }

    if (s_state == ST_PLAY) {
        s_mx += s_mv * dt;
        float lo = -s_mw * 0.5f, hi = SCR_W - s_mw * 0.5f;
        if (s_mx < lo) {
            s_mx = lo;
            s_mv = fabsf(s_mv);
        }
        if (s_mx > hi) {
            s_mx = hi;
            s_mv = -fabsf(s_mv);
        }
    }
    float k = (s_state == ST_OVER) ? 1.5f : 8.0f;  // slow pan after game over
    s_scroll += (s_scroll_target - s_scroll) * (k * dt > 1 ? 1 : k * dt);

    for (int i = 0; i < N_DEBRIS; i++) {
        debris_t *d = &s_debris[i];
        if (d->on) {
            d->vy += 1400 * dt;
            d->x += d->vx * dt;
            d->y += d->vy * dt;
            if (d->y + s_scroll > SCR_H + 40) {
                d->on = false;
            }
        }
    }
    for (int i = 0; i < N_SPARK; i++) {
        spark_t *p = &s_spark[i];
        if (p->life > 0) {
            p->life -= dt;
            p->vy += 900 * dt;
            p->x += p->vx * dt;
            p->y += p->vy * dt;
        }
    }
    s_shake -= s_shake * (dt * 9 > 1 ? 1 : dt * 9);
    if (s_shake < 0.3f) {
        s_shake = 0;
    }
}

static int s_ox, s_oy;  // the offset of the world in this frame (the shake, the landing)

static void draw_slab(float x, int y, float w, int idx)
{
    int ix = (int)x + s_ox, iw = (int)(w + 0.5f);
    y += s_oy;
    gfx_fill_rect(ix, y, iw, SLAB_H, slab_color(idx, 0));
    gfx_fill_rect(ix, y, iw, 3, slab_color(idx, 40));
    gfx_fill_rect(ix, y + SLAB_H - 3, iw, 3, slab_color(idx, -70));
    gfx_fill_rect(ix, y, 2, SLAB_H, slab_color(idx, 20));
    gfx_fill_rect(ix + iw - 2, y, 2, SLAB_H, slab_color(idx, -40));
}

static void stack_draw(int64_t now);
// the frame (the round panels: the game draws the square view, then it is scaled out into the circle)
static void stack_render(int64_t now)
{
#if K_RND
    if (!rv_begin()) {
        return;
    }
    stack_draw(now);
    rv_end();
#else
    stack_draw(now);
#endif
}
// a big word of the game: a Bungee face with a dark outline and a shadow, in the middle; top = the top of the
// capitals (big text and numbers look rough in the dot font)
static void stk_word(const mfont_t *f, int top, const char *s, uint16_t col)
{
    mf_text_oc(f, SCR_W / 2, top + f->asc, s, col, rgb(16, 12, 28), 2, 1);
}

// a line of small text (Rubik) in the middle
static void stk_line(int top, const char *s, uint16_t col)
{
    mf_text_oc(&MF_LABEL, SCR_W / 2, top + MF_LABEL.asc, s, col, rgb(16, 12, 28), 1, 0);
}

static void stack_draw(int64_t now)
{
    // background: its colour moves with the height of the tower
    int h = s_hue0 + s_n * 7 + 180;
    for (int y = 0; y < SCR_H; y += 8) {
        gfx_fill_rect(0, y, SCR_W, 8, hsv(h + y / 40, 110, 70 - y * 40 / SCR_H));
    }

    // the offset of the world: the shake after a miss, and the tower goes down a little at each landing
    s_ox = (int)(s_shake * fsin((float)now * 0.07f));
    s_oy = (int)(s_shake * 0.5f * fcos((float)now * 0.11f));
    int land = s_land_at ? (int)(now - s_land_at) : 1000;
    int thud = land < 140 ? (BOARD_BAR_SCREEN ? 4 : 3) * (140 - land) / 140 : 0;
    s_oy += thud;

    // tower (only the visible blocks)
    for (int i = 0; i < s_n; i++) {
        int y = slab_y(i);
        if (y > SCR_H) {
            continue;
        }
        if (y + SLAB_H < 0) {
            break;
        }
        draw_slab(s_slab[i].x, y, s_slab[i].w, i);
    }
    // under the base block: the ground
    int gy = slab_y(0) + SLAB_H + s_oy;
    if (gy < SCR_H) {
        gfx_fill_rect(0, gy, SCR_W, SCR_H - gy, rgb(30, 30, 44));
    }
    s_oy -= thud;  // (the moving block and the parts that fall do not take the landing)

    if (s_state == ST_PLAY) {
        draw_slab(s_mx, slab_y(s_n), s_mw, s_n);
        if (now - s_perfect_at < 350 && s_n > 1) {
            int grow = (int)((now - s_perfect_at) / 25);
            const slab_t *t = &s_slab[s_n - 1];
            gfx_frame_rect((int)t->x - grow + s_ox, slab_y(s_n - 1) - grow + s_oy + thud, (int)t->w + 2 * grow,
                           SLAB_H + 2 * grow, 1, s_c_text);
            if (now - s_perfect_at < 110) {
                // a short white flash on the block that landed exactly
                gfx_fill_rect((int)t->x + s_ox, slab_y(s_n - 1) + s_oy + thud, (int)(t->w + 0.5f), SLAB_H,
                              rgb(255, 255, 240));
            }
        }
    }
    for (int i = 0; i < N_DEBRIS; i++) {
        const debris_t *d = &s_debris[i];
        if (d->on) {
            draw_slab(d->x, (int)(d->y + s_scroll), d->w, d->idx);
        }
    }
    for (int i = 0; i < N_SPARK; i++) {
        const spark_t *p = &s_spark[i];
        if (p->life > 0) {
            int sz = p->life > 0.2f ? SPARK_PX : SPARK_PX - 1;
            gfx_fill_rect((int)p->x + s_ox - sz / 2, (int)(p->y + s_scroll) + s_oy - sz / 2, sz, sz, p->c);
        }
    }

    char buf[24];
    if (s_state != ST_TITLE) {
        snprintf(buf, sizeof(buf), "%d", s_n - 1);
        // the score jumps up at each landing, and it is gold for a moment after a perfect drop
        int pop = land < 160 ? 7 * (160 - land) / 160 : 0;
        uint16_t sc = (now - s_perfect_at < 300 && s_combo > 0) ? s_c_accent : s_c_text;
        stk_word(&MF_N34, SCORE_Y - pop, buf, sc);
        if (s_combo >= 2 && s_state == ST_PLAY) {
            snprintf(buf, sizeof(buf), "PERFECT X%d", s_combo);
            stk_word(BOARD_UI_TABLET ? &MF_N20 : &MF_N16, COMBO_Y, buf, s_c_accent);
        }
    }
#if BOARD_UI_TABLET
    // the Tab5: the CoreS3 texts, 1.5 times lower and bigger
    if (s_state == ST_TITLE) {
        stk_word(&MF_N34, 50, "STACK", s_c_text);
        stk_word(&MF_N20, 130, "TOUCH TO DROP THE BLOCK", s_c_text);
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
        stk_word(&MF_N20, 172, buf, s_c_accent);
        if (((now / 450) & 1) == 0) {
            stk_word(&MF_N20, 222, "TAP TO START", s_c_text);
        }
    } else if (s_state == ST_OVER) {
        gfx_darken_rect(0, 72, SCR_W, 204, 1);
        stk_word(&MF_N34, 90, "GAME OVER", rgb(255, 110, 110));
        if (s_new_best) {
            stk_word(&MF_N20, 160, "NEW BEST!", s_c_accent);
        } else {
            snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
            stk_word(&MF_N20, 160, buf, s_c_text);
        }
        if (now - s_over_at > 900 && ((now / 450) & 1) == 0) {
            stk_word(&MF_N20, 219, "TAP TO PLAY", s_c_text);
        }
    }
#elif !BOARD_BAR_SCREEN
    if (s_state == ST_TITLE) {
        stk_word(&MF_N34, 36 + TY, "STACK", s_c_text);
        stk_line(88 + TY, "TOUCH TO DROP THE BLOCK", s_c_text);
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
        stk_word(&MF_N20, 114 + TY, buf, s_c_accent);
        if (((now / 450) & 1) == 0) {
            stk_word(&MF_N16, 150 + TY, "TAP TO START", s_c_text);
        }
    } else if (s_state == ST_OVER) {
        gfx_darken_rect(0, 48 + TY, SCR_W, 136, 1);
        stk_word(&MF_N28, 60 + TY, "GAME OVER", rgb(255, 110, 110));
        if (s_new_best) {
            stk_word(&MF_N20, 104 + TY, "NEW BEST!", s_c_accent);
        } else {
            snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
            stk_word(&MF_N20, 104 + TY, buf, s_c_text);
        }
        if (now - s_over_at > 900 && ((now / 450) & 1) == 0) {
            stk_word(&MF_N16, 146 + TY, "TAP TO PLAY", s_c_text);
        }
    }
#else
    if (s_state == ST_TITLE) {
        stk_word(&MF_N34, 120, "STACK", s_c_text);
        stk_line(190, "TOUCH TO DROP", s_c_text);
        stk_line(210, "THE BLOCK", s_c_text);
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
        stk_word(&MF_N20, 258, buf, s_c_accent);
        if (((now / 450) & 1) == 0) {
            stk_word(&MF_N16, 330, "TAP TO START", s_c_text);
        }
    } else if (s_state == ST_OVER) {
        gfx_darken_rect(0, 120, SCR_W, 178, 1);
        stk_word(&MF_N28, 134, "GAME", rgb(255, 110, 110));
        stk_word(&MF_N28, 172, "OVER", rgb(255, 110, 110));
        if (s_new_best) {
            stk_word(&MF_N20, 222, "NEW BEST!", s_c_accent);
        } else {
            snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
            stk_word(&MF_N20, 222, buf, s_c_text);
        }
        if (now - s_over_at > 900 && ((now / 450) & 1) == 0) {
            stk_word(&MF_N16, 262, "TAP TO PLAY", s_c_text);
        }
    }
#endif
}

// ---------------------------------------------------------------- the menu icon (smooth shapes of tfdraw)
#include "tfdraw/tfdraw.h"

// one pixel row of a smooth shape: the shape covers l[k] .. r[k] in each of 4 rows of samples (l > r: none there)
TFD_FN void stk_row(int y, const float *l, const float *r, uint16_t c, int al)
{
    if (y < tfd_cy0 || y >= tfd_cy1) {
        return;
    }
    float lo = 1e9f, hi = -1e9f, li = -1e9f, ri = 1e9f;
    for (int k = 0; k < 4; k++) {
        if (l[k] > r[k]) {
            li = 1e9f;
            ri = -1e9f;
            continue;
        }
        lo = tfd_min(lo, l[k]);
        hi = tfd_max(hi, r[k]);
        li = tfd_max(li, l[k]);
        ri = tfd_min(ri, r[k]);
    }
    if (lo > hi) {
        return;
    }
    int sa = tfd_ce(li), sb = tfd_fl(ri);
    if (sa < sb) {
        tfd_hspan(sa, sb, y, c, al);
    } else {
        sa = sb = tfd_ce(hi) + 1;
    }
    for (int x = tfd_fl(lo); x < tfd_ce(hi); x++) {
        if (x >= sa && x < sb) {
            x = sb - 1;
            continue;
        }
        float cv = 0;
        for (int k = 0; k < 4; k++) {
            float o = tfd_min(r[k], x + 1.0f) - tfd_max(l[k], (float)x);
            if (o > 0) {
                cv += o;
            }
        }
        tfd_px(x, y, c, (int)(al * cv * 0.25f));
    }
}
// a convex polygon (n points as x, y pairs, at most 16) with a smooth edge
TFD_FN void stk_cvx(const float *p, int n, uint16_t c, int al)
{
    float y0 = p[1], y1 = p[1], l[4], r[4], ey0[16], ey1[16], ex0[16], ek[16];
    int m = 0;
    for (int i = 0, j = n - 1; i < n && i < 16; j = i++) {
        y0 = tfd_min(y0, p[2 * i + 1]);
        y1 = tfd_max(y1, p[2 * i + 1]);
        float ay = p[2 * j + 1], by = p[2 * i + 1];
        if (ay == by) {
            continue;
        }
        int lo = ay < by ? j : i, hi = ay < by ? i : j;
        ey0[m] = p[2 * lo + 1];
        ey1[m] = p[2 * hi + 1];
        ex0[m] = p[2 * lo];
        ek[m++] = (p[2 * hi] - p[2 * lo]) / (p[2 * hi + 1] - p[2 * lo + 1]);
    }
    for (int y = tfd_fl(y0); y < tfd_ce(y1); y++) {
        if (y < tfd_cy0 || y >= tfd_cy1) {
            continue;
        }
        for (int k = 0; k < 4; k++) {
            float sy = y + 0.125f + k * 0.25f;
            l[k] = 1e9f;
            r[k] = -1e9f;
            for (int e = 0; e < m; e++) {
                if (sy < ey0[e] || sy >= ey1[e]) {
                    continue;
                }
                float xx = ex0[e] + (sy - ey0[e]) * ek[e];
                l[k] = tfd_min(l[k], xx);
                r[k] = tfd_max(r[k], xx);
            }
        }
        stk_row(y, l, r, c, al);
    }
}
// a box of w x h with its middle at cx, cy, turned by the angle a (radians)
TFD_FN void stk_box(float cx, float cy, float w, float h, float a, uint16_t c, int al)
{
    float ca = fcos(a) / 2, sa = fsin(a) / 2;
    float p[8] = {cx - w * ca + h * sa, cy - w * sa - h * ca, cx + w * ca + h * sa, cy + w * sa - h * ca,
                  cx + w * ca - h * sa, cy + w * sa + h * ca, cx - w * ca - h * sa, cy - w * sa + h * ca};
    stk_cvx(p, 4, c, al);
}
// one slab of the icon: the colour comes from its height on the screen (orange low, green high), so the tower looks
// the same after it goes down one slab (the loop has no jump)
static void stack_icon_slab(float x0, float y0, float w, float h, float yc_u)
{
    int hue = 20 + (int)((100 - yc_u) * 2.0f);
    if (hue < 20) {
        hue = 20;
    }
    if (hue > 160) {
        hue = 160;
    }
    tfd_rrect((int)x0, (int)y0, (int)w, (int)h, (int)(h * 0.22f), hsv(hue, 170, 190), 256);
    tfd_rrect((int)x0, (int)y0, (int)w, (int)(h * 0.78f), (int)(h * 0.22f), hsv(hue, 150, 240), 256);
    tfd_rect((int)(x0 + h * 0.3f), (int)(y0 + h * 0.12f), (int)(w - h * 0.6f), (int)(h * 0.18f + 0.6f),
             hsv(hue, 70, 255), 256);
}
// THE ICON: a wide slab slides in over the tower, drops, and the part that hangs over the edge breaks off and falls;
// then the tower goes down one slab. A loop of 2 s.
static void stack_icon(int x, int y, int s)
{
    float t = icon_time(), u = s / 100.0f, p = fmodf(t, 2.0f);
    const float W = 56, H = 13, L = 22;  // (the width and the height of a slab, the left edge of the tower)
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
    tfd_vgrad(x, y, s, s, rgb(34, 22, 66), rgb(82, 44, 46));
    for (int k = 0; k < 7; k++) {
        float tw = fsin(t * 3.14159f + k * 1.7f) * 0.5f + 0.5f;
        tfd_disc(x + (8 + k * 13.5f) * u, y + (6 + (k * 7) % 22) * u, (0.5f + 0.5f * tw) * u + 0.3f, rgb(255, 240, 220),
                 120 + (int)(120 * tw));
    }
    // the camera goes down one slab after the landing
    float sc = p < 1.2f ? 0 : p > 1.8f ? 1 : (p - 1.2f) / 0.6f;
    sc = sc * sc * (3 - 2 * sc) * H;
    for (int j = -1; j < 6; j++) {
        if (j < 0 && p < 1.0f) {
            continue;  // (the new slab is the top one only after it lands)
        }
        float ty = 60 + j * H + sc;
        stack_icon_slab(x + L * u, y + ty * u, W * u, H * u, ty + H / 2);
    }
    // the new slab: wider than the tower; it slides in from the left, then drops
    if (p < 1.0f) {
        float q = fminf(p / 0.75f, 1.0f), e = 1 - (1 - q) * (1 - q);
        float sx = -70 + (L - (-70)) * e, sy = 60 - H - 7;
        if (p > 0.8f) {
            float d = (p - 0.8f) / 0.2f;
            sy += 7 * d * d;
        }
        tfd_rect(x + (int)((sx + 4) * u), y + (int)(60 * u), (int)((W + 10) * u), (int)(2 * u + 1), rgb(0, 0, 0), 60);
        stack_icon_slab(x + sx * u, y + sy * u, (W + 12) * u, H * u, sy + H / 2);
    }
    // the cut part: it falls and turns off to the right; a flash on the edge where it broke
    if (p >= 1.0f) {
        float q = p - 1.0f, fx = L + W + 6 + q * 14, fy = 60 - H + H / 2 + q * q * 120;
        int hue = 20 + (int)((100 - (60 - H / 2)) * 2.0f);
        stk_box(x + fx * u, y + fy * u, 12 * u, H * u, q * 3.0f, hsv(hue, 150, 240), 256);
        if (q < 0.25f) {
            float f = 1 - q / 0.25f;
            tfd_rect(x + (int)((L + W - 1) * u), y + (int)((60 - H - 2) * u), (int)(2.5f * u + 1), (int)((H + 4) * u),
                     rgb(255, 255, 230), (int)(255 * f));
            tfd_ring(x + (L + W) * u, y + (60 - H / 2) * u, (3 + q * 40) * u, 1.6f * u, rgb(255, 250, 210),
                     (int)(200 * f));
        }
    }
    tfd_noclip();
}

// the user leaves: the start tune stops (it played on in the menu before)
static void stack_leave(void)
{
    audio_tune_stop();
#if K_RND
    rv_free();
#endif
}

const app_t app_stack = {
        .name = "STACK",
        .landscape = !BOARD_BAR_SCREEN,  // the CoreS3: landscape, the way it stands
        .needs = 0,
        .init = stack_init,
        .enter = stack_enter,
        .leave = stack_leave,
#if K_RND
        .round = APP_ROUND_HI,  // (a round panel: the view in the whole circle, stack_rv.inc)
#endif
        .input = stack_input,
        .update = stack_update,
        .render = stack_render,
        .draw_icon = stack_icon,
};
