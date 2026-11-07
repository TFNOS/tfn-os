// TFN OS SDK example 1: TAPPER, a small tap game.
//
// Targets come and go. Tap each target before its ring closes. A target that you miss costs one life. The game ends
// after 3 lost lives. The best score stays in the saves.
//
// What this example shows:
//   - the frame: one layout for the 3.49 bar (640 x 172) and one for the 320 x 240 frame, and a render() that paints
//     the WHOLE frame each time (visual.md, sections 1 and 3)
//   - the anti-aliased fonts MF_* of mfont.h (visual.md, section 4)
//   - touch targets of 44 px or more (visual.md, section 5): a target is 48 px wide and its hit radius is 30 px
//   - the sounds: sfx_play(), sfx_tone() and a start tune with audio_tune() (behave.md, section 5)
//   - the saves: store_get() and store_set() with the key prefix of the app (behave.md, section 4)
//   - the menu icon with icon_time() (visual.md, section 8)
//
// Build (from tfn-os/):
//   python sdk/tools/mk_app.py sdk/examples/tapper/app_tapper.c --sym app_tapper --name TAPPER --kinds lcd349,cores3 --nosign
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_random.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/uikit.h"

// ---------------------------------------------------------------- the layout of each board kind
// All numbers are logical px of the frame. The frame is 640 x 172 on the 3.49 and 320 x 240 on the other boards (the
// AMOLED and the Watcher show the same 320 x 240 frame).
#if BOARD_BAR_SCREEN
#define HUD_H       40                      // the top strip with the score and the lives
#define FIELD_X0    80                      // the area where a target can appear (its centre)
#define FIELD_Y0    (HUD_H + 36)
#define FIELD_X1    (640 - 80)
#define FIELD_Y1    (172 - 36)
#define BTN_W       220
#define BTN_H       56
#define TB_X        ((640 - BTN_W) / 2)     // the START button on the title
#define TB_Y        104
#define OB_X        390                     // the AGAIN button on the end screen
#define OB_Y        58
#define TITLE_Y     62                      // the baselines of the texts
#define BEST_Y      90
#define OV_CX       190
#define OV_Y1       58
#define OV_Y2       100
#define OV_Y3       134
#else
#define HUD_H       44
#define FIELD_X0    44
#define FIELD_Y0    (HUD_H + 36)
#define FIELD_X1    (320 - 44)
#define FIELD_Y1    (240 - 36)
#define BTN_W       220
#define BTN_H       56
#define TB_X        ((320 - BTN_W) / 2)
#define TB_Y        150
#define OB_X        ((320 - BTN_W) / 2)
#define OB_Y        164
#define TITLE_Y     84
#define BEST_Y      120
#define OV_CX       160
#define OV_Y1       68
#define OV_Y2       110
#define OV_Y3       144
#endif

#define R_DISC      24                      // the disc of a target: 48 px wide
#define R_HIT       30                      // a touch inside this radius hits (60 px wide, more than 44 px)
#define MIN_DIST    84                      // two targets are at least this far apart
#define MAX_T       3                       // targets on the screen at the same time
#define LIVES       3
#define POP_MS      300                     // the length of the burst after a hit
#define OVER_GUARD  700                     // ms after the end: a fast tap must not start a new game by mistake

#define SAVE_BEST   "tapper_best"           // the key starts with the save prefix of the app (<id>_)

// ---------------------------------------------------------------- the state
enum { ST_TITLE, ST_PLAY, ST_OVER };

typedef struct {
    bool on;
    int x, y;
    int64_t born;
    int life;                               // ms from the birth to the miss
} target_t;

typedef struct {
    int64_t born;                           // 0: free
    int x, y, pts;
} pop_t;

static int s_state;
static target_t s_t[MAX_T];
static pop_t s_pop[4];
static int s_score, s_lives, s_streak;
static uint32_t s_best;                     // the saved best score
static bool s_newbest;
static int64_t s_next_spawn, s_over_ms, s_hurt_ms;
static bool s_press_start;                  // a finger is down on the button (the button draws darker)
static uint32_t s_rng;

// the start tune: four notes (t and dur in ms, the MIDI note, the bend, the wave, the volume 0..100)
static const tune_note_t START_TUNE[] = {
    {0,   110, 72, 0, W_PULSE, 30},
    {120, 110, 76, 0, W_PULSE, 30},
    {240, 110, 79, 0, W_PULSE, 30},
    {360, 420, 84, 0, W_PULSE | TUNE_HOLD, 32},
};

// ---------------------------------------------------------------- helpers
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

static int rnd_range(int a, int b)          // a number from a to b, both ends included
{
    return a + (int)(rnd() % (uint32_t)(b - a + 1));
}

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static bool in_box(int x, int y, int bx, int by, int bw, int bh)
{
    const int m = 8;                        // a margin for a fat finger
    return x >= bx - m && x < bx + bw + m && y >= by - m && y < by + bh + m;
}

// How long a target lives, and the gap between two spawns. Both get shorter with the score.
static int target_life(void) { int v = 2300 - s_score * 30; return v < 1100 ? 1100 : v; }
static int spawn_gap(void)   { int v = 950 - s_score * 14;  return v < 420 ? 420 : v; }

static void spawn(int64_t now)
{
    int n = 0;
    for (int i = 0; i < MAX_T; i++) n += s_t[i].on;
    if (n >= MAX_T) return;
    for (int tries = 0; tries < 16; tries++) {
        int x = rnd_range(FIELD_X0, FIELD_X1), y = rnd_range(FIELD_Y0, FIELD_Y1);
        bool ok = true;
        for (int i = 0; i < MAX_T; i++) {
            int dx = s_t[i].x - x, dy = s_t[i].y - y;
            if (s_t[i].on && dx * dx + dy * dy < MIN_DIST * MIN_DIST) ok = false;
        }
        if (!ok) continue;
        for (int i = 0; i < MAX_T; i++) {
            if (!s_t[i].on) {
                s_t[i] = (target_t){true, x, y, now, target_life()};
                return;
            }
        }
    }
}

static void add_pop(int x, int y, int pts, int64_t now)
{
    int k = 0;
    for (int i = 0; i < 4; i++) {           // take the oldest slot
        if (s_pop[i].born == 0) { k = i; break; }
        if (s_pop[i].born < s_pop[k].born) k = i;
    }
    s_pop[k] = (pop_t){now, x, y, pts};
}

static void start_game(int64_t now)
{
    s_state = ST_PLAY;
    s_score = 0;
    s_lives = LIVES;
    s_streak = 0;
    s_newbest = false;
    for (int i = 0; i < MAX_T; i++) s_t[i].on = false;
    for (int i = 0; i < 4; i++) s_pop[i].born = 0;
    s_next_spawn = now + 700;               // a short wait: the player sees the tune
    audio_tune(START_TUNE, (int)(sizeof START_TUNE / sizeof START_TUNE[0]));
}

static void end_game(int64_t now)
{
    s_state = ST_OVER;
    s_over_ms = now;
    for (int i = 0; i < MAX_T; i++) s_t[i].on = false;
    if ((uint32_t)s_score > s_best) {       // save only when the best score changes, and only here
        s_best = (uint32_t)s_score;
        store_set(SAVE_BEST, s_best);
        s_newbest = true;
        sfx_play(SFX_WIN);
    } else {
        sfx_play(SFX_GAME_OVER);
    }
}

// ---------------------------------------------------------------- the app functions
static void enter(int64_t now)
{
    (void)now;
    s_rng = esp_random() | 1u;
    s_best = store_get(SAVE_BEST, 0);       // the saves are the only memory between two opens
    s_state = ST_TITLE;
    s_press_start = false;
    s_hurt_ms = -10000;
    for (int i = 0; i < MAX_T; i++) s_t[i].on = false;
    for (int i = 0; i < 4; i++) s_pop[i].born = 0;
}

static void input(const input_event_t *ev)
{
    switch (s_state) {
    case ST_TITLE:
        if (ev->kind == IN_DOWN) s_press_start = in_box(ev->x, ev->y, TB_X, TB_Y, BTN_W, BTN_H);
        if (ev->kind == IN_UP) s_press_start = false;
        // a button is a precise target: it acts when the finger comes off the glass (IN_TAP)
        if (ev->kind == IN_TAP && in_box(ev->x, ev->y, TB_X, TB_Y, BTN_W, BTN_H)) {
            sfx_play(SFX_CLICK);
            start_game(ev->t_ms);
        }
        break;
    case ST_PLAY:
        if (ev->kind != IN_DOWN) break;
        {   // a game action that needs speed acts at once, on IN_DOWN. The nearest target in reach wins.
            int best = -1, bd = R_HIT * R_HIT + 1;
            for (int i = 0; i < MAX_T; i++) {
                if (!s_t[i].on) continue;
                int dx = s_t[i].x - ev->x, dy = s_t[i].y - ev->y, d = dx * dx + dy * dy;
                if (d < bd) { bd = d; best = i; }
            }
            if (best < 0) break;            // a touch on an empty place costs nothing
            target_t *t = &s_t[best];
            bool quick = (ev->t_ms - t->born) * 10 < (int64_t)t->life * 4;     // the first 40% of the life
            int pts = quick ? 2 : 1;
            s_score += pts;
            s_streak++;
            add_pop(t->x, t->y, pts, ev->t_ms);
            t->on = false;
            // a tone that goes up with the streak
            int f = 520 + 30 * (s_streak < 12 ? s_streak : 12);
            sfx_tone((uint16_t)f, (uint16_t)(f + 180), 70, W_PULSE, 35);
        }
        break;
    case ST_OVER:
        if (ev->t_ms - s_over_ms < OVER_GUARD) break;
        if (ev->kind == IN_DOWN) s_press_start = in_box(ev->x, ev->y, OB_X, OB_Y, BTN_W, BTN_H);
        if (ev->kind == IN_UP) s_press_start = false;
        if (ev->kind == IN_TAP && in_box(ev->x, ev->y, OB_X, OB_Y, BTN_W, BTN_H)) {
            sfx_play(SFX_CLICK);
            start_game(ev->t_ms);
        }
        break;
    }
}

static void update(int64_t now)
{
    if (s_state != ST_PLAY) return;
    if (now >= s_next_spawn) {
        spawn(now);
        s_next_spawn = now + spawn_gap();
    }
    for (int i = 0; i < MAX_T; i++) {
        target_t *t = &s_t[i];
        if (!t->on || now - t->born < t->life) continue;
        t->on = false;                      // the ring closed: a miss
        s_streak = 0;
        s_lives--;
        s_hurt_ms = now;
        sfx_play(SFX_BUMP);
        if (s_lives <= 0) { end_game(now); return; }
    }
}

// ---------------------------------------------------------------- drawing
// The sky: eight bands from dark blue to violet. It paints the whole frame, so no old picture stays.
static void draw_bg(void)
{
    const uint16_t top = rgb(20, 16, 52), bot = rgb(58, 26, 92);
    for (int i = 0; i < 8; i++) {
        int y0 = gfx_h * i / 8, y1 = gfx_h * (i + 1) / 8;
        gfx_fill_rect(0, y0, gfx_w, y1 - y0, ui_mix(top, bot, i * 255 / 7));
    }
}

static void draw_button(int x, int y, const char *label, bool pressed)
{
    uint16_t c = pressed ? rgb(40, 150, 90) : rgb(70, 220, 130);
    ui_rrect(x, y + 4, BTN_W, BTN_H, 16, rgb(20, 70, 50));              // a hard shadow under the key
    ui_rrect(x, y + (pressed ? 3 : 0), BTN_W, BTN_H, 16, c);
    mf_text_c(&MF_N24, x + BTN_W / 2, y + (pressed ? 3 : 0) + BTN_H / 2 + 12, label, rgb(12, 40, 28), 255, 1);
}

static void draw_target(const target_t *t, int64_t now)
{
    float age = (float)(now - t->born);
    float a = clampf(age / (float)t->life, 0, 1);                       // 0 = born, 1 = the miss
    float grow = clampf(age / 120.0f, 0, 1);                            // the target grows in 120 ms
    int r = (int)(R_DISC * (0.4f + 0.6f * grow));
    uint16_t col = ui_mix(rgb(80, 230, 140), rgb(255, 90, 80), (int)(a * 255));    // green to red
    ui_circle(t->x, t->y + 3, r, rgb(10, 8, 30), 120);                  // a soft shadow
    ui_circle(t->x, t->y, r, col, 255);
    ui_circle(t->x - r / 3, t->y - r / 3, r / 3, rgb(255, 255, 255), 90);   // a shine
    ui_ring(t->x, t->y, R_DISC + 3 + (int)((1.0f - a) * 12), 3, col, 220);  // the ring closes on the target
}

static void draw_pops(int64_t now)
{
    for (int i = 0; i < 4; i++) {
        pop_t *p = &s_pop[i];
        if (!p->born || now - p->born >= POP_MS) continue;
        float f = (float)(now - p->born) / POP_MS;                      // 0..1
        ui_ring(p->x, p->y, R_DISC + (int)(f * 22), 3, rgb(255, 230, 140), (int)(255 * (1 - f)));
        char b[8];
        snprintf(b, sizeof b, "+%d", p->pts);
        mf_text_c(&MF_N20, p->x, p->y + 8 - (int)(f * 18), b, rgb(255, 240, 180), (int)(255 * (1 - f)), 1);
    }
}

static void draw_hud(void)
{
    gfx_fill_rect(0, 0, gfx_w, HUD_H, rgb(12, 10, 32));
    char b[24];
    snprintf(b, sizeof b, "SCORE %d", s_score);
    mf_text(&MF_N20, 14, HUD_H / 2 + 11, b, rgb(255, 255, 255), 255, 1);
    snprintf(b, sizeof b, "BEST %u", (unsigned)s_best);
    mf_text(&MF_LABEL, 14 + mf_width(&MF_N20, "SCORE 000", 1) + 10, HUD_H / 2 + 7, b, rgb(190, 180, 230), 255, 1);
    for (int i = 0; i < LIVES; i++)         // the lives: full circles and empty circles
        gfx_fill_circle(gfx_w - 24 - i * 28, HUD_H / 2, 9, i < s_lives ? rgb(255, 90, 100) : rgb(60, 50, 90));
}

static void render(int64_t now)
{
    draw_bg();
    float t = (float)now / 1000.0f;

    if (s_state == ST_TITLE) {
        // two discs float at the sides of the title
        for (int i = 0; i < 2; i++) {
            int x = gfx_w * (i ? 4 : 1) / 5 + (int)(14 * fsin(t * 0.9f + i * 2.1f));
            int y = gfx_h * 40 / 100 + (int)(10 * fsin(t * 1.3f + i * 1.7f));
            ui_circle(x, y, R_DISC, hsv((int)(t * 30 + i * 150) % 360, 140, 220), 90);
        }
        mf_text_oc(&MF_N34, gfx_w / 2, TITLE_Y, "TAPPER", rgb(255, 240, 200), rgb(30, 16, 40), 3, 2);
        char b[24];
        snprintf(b, sizeof b, "BEST %u", (unsigned)s_best);
        mf_text_c(&MF_N16, gfx_w / 2, BEST_Y, b, rgb(200, 190, 240), 255, 1);
        draw_button(TB_X, TB_Y, "START", s_press_start);
        return;
    }

    if (s_state == ST_PLAY) {
        for (int i = 0; i < MAX_T; i++) if (s_t[i].on) draw_target(&s_t[i], now);
        draw_pops(now);
        draw_hud();
        // a red frame for 250 ms after a lost life
        if (now - s_hurt_ms < 250) {
            int k = (int)(255 - (now - s_hurt_ms) * 255 / 250);             // 255 .. 0
            gfx_frame_rect(0, HUD_H, gfx_w, gfx_h - HUD_H, 6, ui_mix(rgb(20, 16, 52), rgb(255, 60, 60), k));
        }
        return;
    }

    // ST_OVER
    char b[32];
    mf_text_oc(&MF_N34, OV_CX, OV_Y1, "GAME OVER", rgb(255, 240, 200), rgb(30, 16, 40), 3, 1);
    snprintf(b, sizeof b, "SCORE %d", s_score);
    mf_text_c(&MF_N24, OV_CX, OV_Y2, b, rgb(255, 255, 255), 255, 1);
    if (s_newbest) {
        // blinking, so that the player sees it
        if ((now / 300) % 2) mf_text_oc(&MF_N20, OV_CX, OV_Y3, "NEW BEST!", rgb(255, 214, 90), rgb(60, 30, 10), 2, 1);
    } else {
        snprintf(b, sizeof b, "BEST %u", (unsigned)s_best);
        mf_text_c(&MF_N16, OV_CX, OV_Y3, b, rgb(200, 190, 240), 255, 1);
    }
    draw_button(OB_X, OB_Y, "AGAIN", s_press_start && now - s_over_ms >= OVER_GUARD);
}

// The menu icon. Landscape logical coordinates; a box of s x s px from (x, y). Use gfx_* shapes only (they obey the
// clip box that the tool sets) and paint the whole box. The ring closes on the disc each second: a loop with no jump.
static void icon(int x, int y, int s)
{
    float t = icon_time();
    const uint16_t bg = rgb(44, 28, 96);
    gfx_fill_rect(x, y, s, s, bg);
    for (int i = 0; i < 2; i++) {
        float p = t + 0.5f * i;
        float f = p - floorf(p);                                        // 0..1 each second
        int cx = x + (i ? s * 7 / 10 : s * 3 / 10);
        int cy = y + (i ? s * 68 / 100 : s * 36 / 100);
        int r = s * 13 / 100;
        uint16_t col = f < 0.5f ? rgb(80, 230, 140) : (f < 0.8f ? rgb(240, 210, 90) : rgb(255, 90, 80));
        int ro = r + s / 40 + (int)((1.0f - f) * s * 12 / 100);
        int w = s / 32 > 2 ? s / 32 : 2;
        gfx_fill_circle(cx, cy, ro, col);                               // the ring: a big circle, then the
        gfx_fill_circle(cx, cy, ro - w, bg);                            // colour of the box inside it
        gfx_fill_circle(cx, cy, r, col);
    }
}

const app_t app_tapper = {
    .name = "TAPPER",
    .landscape = true,                      // 640 x 172 on the 3.49, 320 x 240 on the other boards
    .enter = enter,
    .input = input,                         // never NULL: the system calls it for each touch event
    .update = update,
    .render = render,
    .draw_icon = icon,
    .round_fit = true,                      // the Watcher: the whole 320 x 240 frame, 1:1 in the circle
};
