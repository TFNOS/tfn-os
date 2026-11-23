// HOP (GAMES): a vertical platform game on the portrait screen (172 x 640, or 240 x 320 on the CoreS3: there
// all heights are smaller, see HS()).
// The task: the app task of TFN OS calls the hooks of app_hop.
// The memory: the file statics (the platforms, the monsters, the gems, the bullets); no heap.
//
// The blob bounces by itself. Tilt the board (or hold the left / right side of
// the screen) to move. Touch the middle to shoot up at monsters. The sides of
// the screen wrap around. Platforms: green (normal), blue (moving), white
// (one bounce), brown (breaks, no bounce). Springs throw high, a jetpack flies.
// The sky changes with the height: meadow, clouds, sunset, space.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/tunes.h"
#include "tfn/gfx.h"
#include "tfn/mfont.h"
#include "tfn/imu.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/board.h"

#if BOARD_UI_TABLET
// The Tab5 (frame 360 x 640): the heights of the 3.49 (the same height of the screen), and
// a wider world. The platforms are a little wider, so the jumps are as easy as on the 3.49.
#define SCR_W    360
#define SCR_H    640
#define HS(h)    (h)
#define CAM_KEEP 250
#define FALL_CAM 200
#elif BOARD_BAR_SCREEN
#define SCR_W    172
#define SCR_H    640
#define HS(h)    (h)  // heights in the world: as they are
#define CAM_KEEP 250  // the blob stays at least this far below the top
#define FALL_CAM 200  // after a fall: the blob this far above the bottom
#elif BOARD_ROUND
// THE ROUND PANELS (APP_ROUND_HI): the game draws a square view of RV_S x RV_S (hop_rv.inc) at the heights of the
// CoreS3, and the view is scaled out into the whole circle. The platforms, the monsters and the gems come in the
// middle columns (PX_LO .. PX_HI: there the circle is high); the blob still goes round the sides.
#define K_RND    1
#define RV_S     311
#define SCR_W    RV_S
#define SCR_H    RV_S
#define HS(h)    ((h) * 0.63f)
#define CAM_KEEP 150
#define FALL_CAM 100
#define PX_LO    60
#define PX_HI    (SCR_W - 60)
#else
// CoreS3: a screen of half the height. All heights and vertical speeds are 0.65 of
// the 3.49 values (the time of a bounce stays the same), so the screen shows about
// 2.7 bounce heights (the 3.49 shows 3.4).
#define SCR_W    240
#define SCR_H    320
#define HS(h)    ((h) * 0.65f)
#define CAM_KEEP 150
#define FALL_CAM 100
#endif
#ifndef K_RND
#define K_RND 0
#define PX_LO (PLAT_W / 2)
#define PX_HI (SCR_W - PLAT_W / 2)
#endif
#include "esp_heap_caps.h"
#include "hop_rv.inc"
#define GRAV     HS(1300.0f)
#define BOUNCE_V HS(700.0f)  // bounce height 188 px (CoreS3: 122 px)
#define SPRING_V HS(1150.0f)
#define JET_V    HS(720.0f)
#define JET_TIME 2.4f
#define MAX_VX   330.0f
#define STEP     (1.0f / 240.0f)
#if BOARD_UI_TABLET
#define PLAT_W 56
#else
#define PLAT_W 40
#endif
#define PLAT_H 8
#define PR     9  // blob radius

#define NPLAT 56
#define NMON  8
#define NGEM  20
#define NBUL  10
#define NPART 64

enum {
    P_NORMAL,
    P_MOVING,
    P_BREAK,
    P_VANISH
};
typedef struct {
    float x, h, vx, t, spring_t;
    int8_t spring_dx;
    uint8_t kind;
    bool spring, jet, broken, gone;
} plat_t;
typedef struct {
    float x, h, vx, t, dead_vy;
    bool dead;
} mon_t;
typedef struct {
    float x, h;
} gem_t;
typedef struct {
    float x, h;
} bullet_t;
typedef struct {
    float x, h, vx, vy, life;
    uint16_t c;
} part_t;
typedef enum {
    ST_TITLE,
    ST_PLAY,
    ST_FALL,
    ST_OVER
} hop_state_t;

static plat_t s_plat[NPLAT];
static int s_nplat;
static mon_t s_mon[NMON];
static int s_nmon;
static gem_t s_gem[NGEM];
static int s_ngem;
static bullet_t s_bul[NBUL];
static int s_nbul;
static part_t s_part[NPART];

static hop_state_t s_state;
static float s_x, s_h, s_vx, s_vy;
static float s_cam, s_max_h;
static float s_jet_t, s_squash, s_face;
static bool s_hit;  // hit by a monster: no more landing
static int s_touch_dir;
static int s_gems, s_kills;
static uint32_t s_score, s_best;
static bool s_new_best;
static float s_gen_h, s_last_mon_h;
static float s_acc;
static int64_t s_last, s_start_at, s_over_at;
static uint16_t s_text, s_accent;

// ---------------------------------------------------------------- helpers

static float frand(float a, float b)
{
    return a + (b - a) * (float)(esp_random() & 0xFFFF) / 65535.0f;
}

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static float wrap_dx(float dx)
{
    if (dx > SCR_W / 2) {
        dx -= SCR_W;
    }
    if (dx < -SCR_W / 2) {
        dx += SCR_W;
    }
    return dx;
}

static inline int sy(float h)
{
    return SCR_H - (int)(h - s_cam);
}

static float difficulty(void)
{
    float d = s_max_h / HS(20000.0f);
    return d > 1 ? 1 : d;
}

static void particles(float x, float h, int n, uint16_t c, float spread, float up)
{
    for (int i = 0; i < NPART && n > 0; i++) {
        if (s_part[i].life <= 0) {
            s_part[i] = (part_t){x, h, frand(-spread, spread), frand(up * 0.2f, up), frand(0.3f, 0.7f), c};
            n--;
        }
    }
}

// ---------------------------------------------------------------- world

static plat_t *add_plat(float x, float h, uint8_t kind)
{
    if (s_nplat >= NPLAT) {
        return NULL;
    }
    plat_t *p = &s_plat[s_nplat++];
    memset(p, 0, sizeof(*p));
    p->x = x;
    p->h = h;
    p->kind = kind;
    return p;
}

// The chain of platforms that can always be reached: normal, moving or one-bounce.
// Brown platforms and monsters come in between as extras.
static void generate(float hmax)
{
    while (s_gen_h < hmax && s_nplat < NPLAT - 3) {
        float d = difficulty();
        float gap = frand(HS(35), HS(55 + 100 * d));  // never more than the bounce height
        float h0 = s_gen_h;
        s_gen_h += gap;
        float r = frand(0, 1);
        uint8_t kind = P_NORMAL;
        if (r < 0.15f + 0.25f * d) {
            kind = P_MOVING;
        } else if (r < 0.23f + 0.33f * d) {
            kind = P_VANISH;
        }
        plat_t *p = add_plat(frand(PX_LO, PX_HI), s_gen_h, kind);
        if (!p) {
            return;
        }
        if (kind == P_MOVING) {
            p->vx = (frand(0, 1) < 0.5f ? -1 : 1) * (40 + 70 * d);
        }
        if (kind == P_NORMAL) {
            float q = frand(0, 1);
            if (q < 0.07f) {
                p->spring = true;
                p->spring_dx = (int8_t)frand(-12, 12);
            } else if (q < 0.085f && s_gen_h > HS(1500)) {
                p->jet = true;
            }
        }
        if (frand(0, 1) < 0.25f && s_ngem < NGEM) {
            s_gem[s_ngem++] = (gem_t){p->x, p->h + 30};
        }
        // a brown platform in the gap: it looks like a step, but breaks
        if (frand(0, 1) < 0.15f + 0.25f * d) {
            add_plat(frand(PX_LO, PX_HI), h0 + gap * 0.5f, P_BREAK);
        }
        // a monster now and then (not too close to the one before)
        if (s_gen_h > HS(2500) && s_gen_h - s_last_mon_h > HS(500) && frand(0, 1) < 0.03f + 0.07f * d &&
            s_nmon < NMON) {
            mon_t *m = &s_mon[s_nmon++];
            memset(m, 0, sizeof(*m));
            m->x = K_RND ? frand(PX_LO, PX_HI) : frand(20, SCR_W - 20);
            m->h = s_gen_h + gap * 0.5f + 20;
            m->vx = frand(0, 1) < 0.5f ? 30 + 40 * d : -(30 + 40 * d);
            s_last_mon_h = m->h;
        }
    }
}

static void cull(void)
{
    float low = s_cam - 80;
    int j = 0;
    for (int i = 0; i < s_nplat; i++) {
        if (s_plat[i].h > low && !(s_plat[i].gone && s_plat[i].t > 0.4f)) {
            s_plat[j++] = s_plat[i];
        }
    }
    s_nplat = j;
    j = 0;
    for (int i = 0; i < s_nmon; i++) {
        if (s_mon[i].h > low) {
            s_mon[j++] = s_mon[i];
        }
    }
    s_nmon = j;
    j = 0;
    for (int i = 0; i < s_ngem; i++) {
        if (s_gem[i].h > low) {
            s_gem[j++] = s_gem[i];
        }
    }
    s_ngem = j;
    j = 0;
    for (int i = 0; i < s_nbul; i++) {
        if (s_bul[i].h < s_cam + SCR_H + 40) {
            s_bul[j++] = s_bul[i];
        }
    }
    s_nbul = j;
}

static void new_game(int64_t now)
{
    s_nplat = s_nmon = s_ngem = s_nbul = 0;
    memset(s_part, 0, sizeof(s_part));
    s_cam = 0;
    s_max_h = 0;
    s_gen_h = 20;
    s_last_mon_h = 0;
    // a wide start: three platforms at the bottom
    add_plat(SCR_W / 2, 20, P_NORMAL);
    add_plat(30, HS(90), P_NORMAL);
    add_plat(SCR_W - 30, HS(150), P_NORMAL);
    s_gen_h = HS(150);
    generate(SCR_H + 200);
    s_x = SCR_W / 2;
    s_h = 20;
    s_vx = 0;
    s_vy = BOUNCE_V;
    s_jet_t = 0;
    s_hit = false;
    s_touch_dir = 0;
    s_gems = s_kills = 0;
    s_score = 0;
    s_acc = 0;
    s_start_at = now;
    s_state = ST_PLAY;
    imu_set_level();
}

static void game_over(int64_t now)
{
    s_state = ST_OVER;
    s_over_at = now;
    audio_drone(0, 0, 0, W_NOISE);
    audio_drone(1, 0, 0, W_SAW);
    s_new_best = s_score > s_best;
    if (s_new_best) {
        s_best = s_score;
        store_set("hop_best", s_best);
    }
}

// ---------------------------------------------------------------- physics

static float target_vx(void)
{
    float v = 0;
    if (imu_ok()) {
        float tx, ty;
        imu_tilt_logical(&tx, &ty);
        if (fabsf(tx) < 0.03f) {
            tx = 0;
        }
        v = tx * 1000.0f;
    }
    v += s_touch_dir * MAX_VX;
    if (v > MAX_VX) {
        v = MAX_VX;
    }
    if (v < -MAX_VX) {
        v = -MAX_VX;
    }
    return v;
}

static void kill_monster(mon_t *m, int pts)
{
    m->dead = true;
    m->dead_vy = 0;
    s_kills++;
    s_score += (uint32_t)pts;
    particles(m->x, m->h, 14, rgb(200, 90, 240), 120, 200);
    sfx_play(SFX_STOMP);
}

// the moving platforms, the timers of the broken and gone platforms and of the springs
static void plat_move(float dt)
{
    for (int i = 0; i < s_nplat; i++) {
        plat_t *p = &s_plat[i];
        if (p->kind == P_MOVING) {
            p->x += p->vx * dt;
            if (p->x < PLAT_W / 2) {
                p->x = PLAT_W / 2;
                p->vx = fabsf(p->vx);
            }
            if (p->x > SCR_W - PLAT_W / 2) {
                p->x = SCR_W - PLAT_W / 2;
                p->vx = -fabsf(p->vx);
            }
        }
        if (p->broken || p->gone) {
            p->t += dt;
        }
        if (p->spring_t > 0) {
            p->spring_t -= dt;
        }
    }
}

// the landing on a platform (prev: the height before this step) and on the grass of the start
static void plat_land(float prev)
{
    if (!s_hit && s_vy < 0 && s_jet_t <= 0) {
        for (int i = 0; i < s_nplat; i++) {
            plat_t *p = &s_plat[i];
            if (p->gone || p->broken) {
                continue;
            }
            float dx = wrap_dx(s_x - p->x);
            if (fabsf(dx) < PLAT_W / 2 + 5 && prev >= p->h && s_h <= p->h) {
                if (p->kind == P_BREAK) {
                    p->broken = true;
                    sfx_play(SFX_CRUMBLE);
                    continue;
                }
                s_h = p->h;
                if (p->spring && fabsf(dx - p->spring_dx) < 10) {
                    s_vy = SPRING_V;
                    p->spring_t = 0.3f;
                    sfx_play(SFX_SPRING);
                } else {
                    s_vy = BOUNCE_V;
                    sfx_tone(220 + (uint16_t)(esp_random() % 60), 640, 110, W_TRI, 70);
                }
                if (p->kind == P_VANISH) {
                    p->gone = true;
                    particles(p->x, p->h, 10, rgb(240, 245, 255), 60, 80);
                }
                s_squash = 0.12f;
                break;
            }
        }
    }
    // the grass of the start is a floor while it is on the screen (a blob that misses the first
    // platform must not fall through the grass that the player sees, or the camera shows a screen of green)
    if (!s_hit && s_vy < 0 && s_jet_t <= 0 && s_cam < 10 && s_h <= 10) {
        s_h = 10;
        s_vy = BOUNCE_V;
        s_squash = 0.12f;
        sfx_tone(220 + (uint16_t)(esp_random() % 60), 640, 110, W_TRI, 70);
    }
}

// the jetpacks on the platforms and the gems
static void pick_up(void)
{
    for (int i = 0; i < s_nplat; i++) {
        plat_t *p = &s_plat[i];
        if (p->jet && fabsf(wrap_dx(s_x - p->x)) < 14 && fabsf(s_h - (p->h + 12)) < 18) {
            p->jet = false;
            s_jet_t = JET_TIME;
            sfx_play(SFX_POWERUP);
        }
    }
    // gems
    for (int i = 0; i < s_ngem; i++) {
        if (fabsf(wrap_dx(s_x - s_gem[i].x)) < 13 && fabsf(s_h + PR - s_gem[i].h) < 16) {
            particles(s_gem[i].x, s_gem[i].h, 8, rgb(80, 230, 255), 70, 90);
            s_gems++;
            s_score += 25;
            sfx_play(SFX_COIN);
            s_gem[i] = s_gem[--s_ngem];
            i--;
        }
    }
}

// the monsters: the move, a hit of the blob (a stomp, the jetpack or a hit), the bullets
static void mon_step(float dt)
{
    for (int i = 0; i < s_nmon; i++) {
        mon_t *m = &s_mon[i];
        m->t += dt;
        if (m->dead) {
            m->dead_vy -= GRAV * dt;
            m->h += m->dead_vy * dt;
            continue;
        }
        m->x += m->vx * dt;
        if (m->x < 16) {
            m->x = 16;
            m->vx = fabsf(m->vx);
        }
        if (m->x > SCR_W - 16) {
            m->x = SCR_W - 16;
            m->vx = -fabsf(m->vx);
        }
        float mh = m->h + 6 * fsin(m->t * 3);
        float dx = wrap_dx(s_x - m->x), dh = (s_h + PR) - mh;
        if (!s_hit && dx * dx + dh * dh < 19 * 19) {
            if (s_jet_t > 0) {
                kill_monster(m, 100);
            } else if (s_vy < 0 && dh > 6) {
                kill_monster(m, 100);
                s_vy = BOUNCE_V * 1.1f;
            } else {
                s_hit = true;
                s_vy = s_vy > 0 ? 0 : s_vy;
                particles(s_x, s_h + PR, 10, rgb(255, 255, 255), 90, 90);
                sfx_play(SFX_SHIELD_HIT);
            }
        }
        // bullets
        for (int b = 0; b < s_nbul; b++) {
            float bdx = wrap_dx(s_bul[b].x - m->x), bdh = s_bul[b].h - mh;
            if (!m->dead && bdx * bdx + bdh * bdh < 16 * 16) {
                kill_monster(m, 100);
                s_bul[b] = s_bul[--s_nbul];
                b--;
            }
        }
    }
}

static void step(float dt)
{
    float k = 10.0f * dt;
    s_vx += (target_vx() - s_vx) * (k > 1 ? 1 : k);
    s_x += s_vx * dt;
    if (s_x < 0) {
        s_x += SCR_W;
    }
    if (s_x >= SCR_W) {
        s_x -= SCR_W;
    }
    if (fabsf(s_vx) > 20) {
        s_face = s_vx > 0 ? 1 : -1;
    }

    float prev = s_h;
    if (s_jet_t > 0) {
        s_jet_t -= dt;
        s_vy = JET_V;
    } else {
        s_vy -= GRAV * dt;
    }
    s_h += s_vy * dt;

    plat_move(dt);
    plat_land(prev);
    pick_up();
    mon_step(dt);
    for (int b = 0; b < s_nbul; b++) {
        s_bul[b].h += 900 * dt;
    }

    // camera up only
    if (s_h - s_cam > SCR_H - CAM_KEEP) {
        s_cam = s_h - (SCR_H - CAM_KEEP);
    }
    if (s_h > s_max_h) {
        s_max_h = s_h;
    }
    s_score = (uint32_t)(s_max_h / HS(10)) + 25u * (uint32_t)s_gems + 100u * (uint32_t)s_kills;
    if (s_h < s_cam - 40) {
        s_state = ST_FALL;
        s_over_at = 0;
        sfx_play(SFX_FALL);
    }
}

// ---------------------------------------------------------------- app

static void hop_init(void)
{
    s_best = store_get("hop_best", 0);
    s_text = rgb(255, 255, 255);
    s_accent = rgb(255, 225, 60);
    s_state = ST_TITLE;
    s_nplat = 0;
    s_gen_h = 20;
    add_plat(SCR_W / 2, 20, P_NORMAL);
    generate(SCR_H + 100);
    s_x = SCR_W / 2;
    s_h = 20;
    s_vy = BOUNCE_V;
    s_face = 1;
}

// the settings panel: RESTART (a hold of 1 s, only in a game: the climb is lost)
static const pitem_t HOP_ITEMS[] = {
        {PI_ACT2, 0, 0, 0, {"RESTART", "מחדש"}, NULL, NULL},
};
static bool hop_item_shown(int id)
{
    (void)id;
    return s_state == ST_PLAY;
}
static void hop_item_set(int id, int v)
{
    (void)id;
    (void)v;
    new_game(s_last);
}
// the panel went away: the board can be in another position now
static void hop_panel_resume(void)
{
    imu_set_level();
}
static const pitems_t HOP_PANEL = {HOP_ITEMS, 1, hop_item_shown, NULL, hop_item_set, NULL, hop_panel_resume};

static void hop_enter(int64_t now)
{
    s_last = now;
    sysui_panel_items(&HOP_PANEL);
    s_touch_dir = 0;
    if (s_state == ST_PLAY || s_state == ST_FALL) {
        // no pause: back to the title
        s_state = ST_TITLE;
    }
}

static void hop_leave(void)
{
#if K_RND
    rv_free();
#endif
    audio_drone(0, 0, 0, W_NOISE);
    audio_drone(1, 0, 0, W_SAW);
}

static void hop_input_v(const input_event_t *ev);
static void hop_input(const input_event_t *ev)
{
#if K_RND
    input_event_t e = *ev;  // (the round panels: the point in the view)
    int ex = e.x, ey = e.y;
    rv_pt(&ex, &ey);
    e.x = (int16_t)ex;
    e.y = (int16_t)ey;
    hop_input_v(&e);
#else
    hop_input_v(ev);
#endif
}
static void hop_input_v(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        if (s_state == ST_PLAY) {
            if (ev->x < SCR_W * 3 / 10) {
                s_touch_dir = -1;
            } else if (ev->x > SCR_W * 7 / 10) {
                s_touch_dir = 1;
            } else if (s_nbul < NBUL && !s_hit) {
                s_bul[s_nbul++] = (bullet_t){s_x, s_h + 2 * PR};
                sfx_play(SFX_SHOOT);
            }
        }
        break;
    case IN_UP:
        s_touch_dir = 0;
        break;
    case IN_TAP:
        if (s_state == ST_TITLE || (s_state == ST_OVER && ev->t_ms - s_over_at > 700)) {
            new_game(ev->t_ms);
            tune_play(TUNE_HOP);
        }
        break;
    default:
        break;
    }
}

static void hop_update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.05f) {
        dt = 0.05f;
    }

    for (int i = 0; i < NPART; i++) {
        part_t *p = &s_part[i];
        if (p->life > 0) {
            p->life -= dt;
            p->vy -= 500 * dt;
            p->x += p->vx * dt;
            p->h += p->vy * dt;
        }
    }
    if (s_squash > 0) {
        s_squash -= dt;
    }

    if (s_state == ST_PLAY) {
        if (now - s_start_at < 400) {
            imu_set_level();  // the hand still moves after the start tap
        }
        s_acc += dt;
        while (s_acc >= STEP && s_state == ST_PLAY) {
            step(STEP);
            s_acc -= STEP;
        }
        generate(s_cam + SCR_H + 200);
        cull();
        // jetpack sound
        if (s_jet_t > 0) {
            audio_drone(0, 2200, 26, W_NOISE);
            audio_drone(1, 95, 22, W_SAW);
            particles(s_x - s_face * 6, s_h + 2, 2, rgb(255, 160, 40), 30, -250);
        } else {
            audio_drone(0, 0, 0, W_NOISE);
            audio_drone(1, 0, 0, W_SAW);
        }
    } else if (s_state == ST_FALL) {
        // the camera follows the fall for a moment
        s_vy -= GRAV * dt;
        s_h += s_vy * dt;
        s_cam += (s_h - FALL_CAM - s_cam) * (dt * 5 > 1 ? 1 : dt * 5);
        if (s_cam < 0) {
            s_cam = 0;  // (the camera stops at the ground: no screen of green)
        }
        if (s_over_at == 0) {
            s_over_at = now;
        }
        if (now - s_over_at > 900) {
            game_over(now);
        }
    } else if (s_state == ST_TITLE) {
        // the blob bounces on the title screen
        s_vy -= GRAV * dt;
        s_h += s_vy * dt;
        if (s_h < 20) {
            s_h = 20;
            s_vy = BOUNCE_V;
            s_squash = 0.12f;
        }
    }
}

// ---------------------------------------------------------------- drawing

static void mix3(const uint8_t *a, const uint8_t *b, float t, uint8_t *out)
{
    for (int i = 0; i < 3; i++) {
        out[i] = (uint8_t)(a[i] + (b[i] - a[i]) * t);
    }
}

// sky colours by height: meadow, clouds, sunset, space
static void sky(float h, uint8_t top[3], uint8_t bot[3])
{
    static const float H[5] = {0, 3000, 8000, 15000, 22000};
    static const uint8_t T[5][3] = {{90, 170, 250}, {120, 150, 240}, {120, 70, 160}, {20, 16, 60}, {4, 4, 16}};
    static const uint8_t B[5][3] = {{190, 230, 255}, {220, 210, 250}, {255, 150, 90}, {60, 30, 90}, {12, 10, 30}};
    int i = 0;
    while (i < 3 && h > H[i + 1]) {
        i++;
    }
    float t = (h - H[i]) / (H[i + 1] - H[i]);
    if (t < 0) {
        t = 0;
    }
    if (t > 1) {
        t = 1;
    }
    mix3(T[i], T[i + 1], t, top);
    mix3(B[i], B[i + 1], t, bot);
}

static void draw_background(void)
{
    uint8_t top[3], bot[3], c[3];
#if BOARD_BAR_SCREEN || BOARD_UI_TABLET
    sky(s_cam + SCR_H / 2, top, bot);
#else
    sky((s_cam + SCR_H / 2) / HS(1.0f), top, bot);  // the colours change at the same "metres"
#endif
    for (int y = 0; y < SCR_H; y += 16) {
        mix3(top, bot, (float)y / SCR_H, c);
        gfx_fill_rect(0, y, SCR_W, 16, rgb(c[0], c[1], c[2]));
    }
    // stars high up
    float star = (s_cam - HS(10000)) / HS(5000);
    if (star > 0) {
        if (star > 1) {
            star = 1;
        }
        uint8_t b = (uint8_t)(230 * star);
        for (int i = 0; i < 50; i++) {
            uint32_t hsh = hash32((uint32_t)i * 977u);
            int x = (int)(hsh % SCR_W);
            int y = (int)(((hsh >> 10) % 1200) + (int)(s_cam * 0.1f)) % SCR_H;
            gfx_fill_rect(x, y, 1 + (int)((hsh >> 30) & 1), 1, rgb(b, b, b));
        }
    }
    // clouds, slower than the world
    if (s_cam < HS(16000)) {
        float par = s_cam * 0.5f;
        int first = (int)(par / 160) - 1;
        for (int k = first; k < first + 6; k++) {
            uint32_t hsh = hash32((uint32_t)k * 2654435761u);
            if ((hsh & 3) == 0) {
                continue;
            }
            int x = (int)(hsh % (SCR_W + 60)) - 30;
            int y = SCR_H - (int)(k * 160 - par);
            uint8_t w = (uint8_t)(s_cam < HS(8000) ? 250 : 230);
            uint16_t cc = rgb(w, w, (uint8_t)(w > 5 ? w : 255));
            gfx_fill_ellipse(x, y, 22, 8, cc);
            gfx_fill_ellipse(x - 14, y + 3, 14, 6, cc);
            gfx_fill_ellipse(x + 15, y + 2, 15, 6, cc);
        }
    }
    // grass at the start
    int g = sy(10);
    if (g < SCR_H) {
        gfx_fill_rect(0, g, SCR_W, SCR_H - g, rgb(80, 180, 70));
        gfx_fill_rect(0, g, SCR_W, 4, rgb(120, 220, 90));
    }
}

static void draw_platform(const plat_t *p)
{
    int x = (int)p->x - PLAT_W / 2, y = sy(p->h);
    if (y < -20 || y > SCR_H + 20) {
        return;
    }
    if (p->kind == P_BREAK && p->broken) {
        // two halves fall apart
        int drop = (int)(p->t * p->t * 600);
        int spread = (int)(p->t * 40);
        gfx_fill_rect(x - spread, y + drop, PLAT_W / 2 - 1, PLAT_H, rgb(150, 100, 60));
        gfx_fill_rect(x + PLAT_W / 2 + spread, y + drop, PLAT_W / 2, PLAT_H, rgb(150, 100, 60));
        return;
    }
    if (p->gone) {
        return;
    }
    uint16_t c, hi, lo;
    switch (p->kind) {
    case P_MOVING:
        c = rgb(60, 140, 235);
        hi = rgb(150, 200, 255);
        lo = rgb(30, 80, 160);
        break;
    case P_BREAK:
        c = rgb(160, 110, 65);
        hi = rgb(200, 150, 100);
        lo = rgb(100, 65, 35);
        break;
    case P_VANISH:
        c = rgb(235, 240, 250);
        hi = rgb(255, 255, 255);
        lo = rgb(170, 180, 200);
        break;
    default:
        c = rgb(70, 190, 80);
        hi = rgb(150, 235, 120);
        lo = rgb(40, 120, 50);
        break;
    }
    gfx_fill_rect(x + 1, y, PLAT_W - 2, PLAT_H, c);
    gfx_fill_rect(x, y + 1, PLAT_W, PLAT_H - 2, c);
    gfx_fill_rect(x + 2, y + 1, PLAT_W - 4, 2, hi);
    gfx_fill_rect(x + 2, y + PLAT_H - 2, PLAT_W - 4, 1, lo);
    if (p->kind == P_BREAK) {
        gfx_fill_rect(x + 12, y + 2, 1, 5, lo);
        gfx_fill_rect(x + 13, y + 4, 1, 3, lo);
        gfx_fill_rect(x + 27, y + 1, 1, 4, lo);
    }
    if (p->spring) {
        int sx = (int)p->x + p->spring_dx;
        int hgt = p->spring_t > 0.15f ? 12 : 7;
        for (int k = 0; k < hgt; k += 2) {
            gfx_fill_rect(sx - 4 + ((k / 2) & 1) * 2, y - 2 - k, 6, 1, rgb(200, 200, 210));
        }
        gfx_fill_rect(sx - 6, y - hgt - 3, 12, 3, rgb(235, 70, 60));
    }
    if (p->jet) {
        int jx = (int)p->x, jy = y - 14;
        gfx_fill_rect(jx - 6, jy, 5, 12, rgb(170, 175, 190));
        gfx_fill_rect(jx + 1, jy, 5, 12, rgb(170, 175, 190));
        gfx_fill_rect(jx - 6, jy, 12, 3, rgb(230, 60, 60));
    }
}

static void draw_blob(int cx, int cy, int64_t now)
{
    int rx = PR, ry = PR;
    if (s_squash > 0) {
        rx = PR + 3;
        ry = PR - 3;
        cy += 3;
    } else if (s_vy > 500) {
        rx = PR - 1;
        ry = PR + 2;
    }
    uint16_t body = rgb(130, 220, 70);
    if (s_hit) {
        body = rgb(160, 160, 160);
    }
    // feet
    gfx_fill_rect(cx - 7, cy + ry - 2, 5, 3, rgb(255, 150, 40));
    gfx_fill_rect(cx + 2, cy + ry - 2, 5, 3, rgb(255, 150, 40));
    // a white rim and a dark outline: the green blob stands out from the green platforms and the grass
    gfx_fill_ellipse(cx, cy, rx + 3, ry + 3, rgb(255, 255, 255));
    gfx_fill_ellipse(cx, cy, rx + 2, ry + 2, rgb(20, 40, 20));
    gfx_fill_ellipse(cx, cy, rx, ry, body);
    gfx_fill_ellipse(cx, cy + 3, rx - 4, ry - 5 > 0 ? ry - 5 : 1, rgb(200, 250, 150));
    int f = (int)s_face;
    if (s_hit) {
        // x x eyes
        gfx_text(cx - 7, cy - 6, "X", 1, rgb(0, 0, 0));
        gfx_text(cx + 2, cy - 6, "X", 1, rgb(0, 0, 0));
    } else {
        gfx_fill_circle(cx - 3 + f * 2, cy - 3, 3, rgb(255, 255, 255));
        gfx_fill_circle(cx + 4 + f * 2, cy - 3, 3, rgb(255, 255, 255));
        gfx_fill_rect(cx - 2 + f * 3, cy - 3, 2, 2, rgb(0, 0, 0));
        gfx_fill_rect(cx + 5 + f * 3, cy - 3, 2, 2, rgb(0, 0, 0));
    }
    if (s_jet_t > 0) {
        gfx_fill_rect(cx - f * 10 - 3, cy - 4, 6, 11, rgb(170, 175, 190));
        int fl = 4 + (int)((now / 40) % 5);
        gfx_fill_rect(cx - f * 10 - 2, cy + 7, 4, fl, rgb(255, 170, 40));
    }
}

static void draw_monster(const mon_t *m)
{
    float mh = m->h + 6 * fsin(m->t * 3);
    int x = (int)m->x, y = sy(mh);
    if (y < -30 || y > SCR_H + 30) {
        return;
    }
    uint16_t body = m->dead ? rgb(120, 100, 130) : rgb(165, 60, 205);
    // horns
    gfx_spike(x - 12, y - 17, 7, 7, rgb(240, 230, 200));
    gfx_spike(x + 5, y - 17, 7, 7, rgb(240, 230, 200));
    // wings flap
    bool up = ((int)(m->t * 8)) & 1;
    gfx_fill_ellipse(x - 17, y + (up ? -5 : 2), 6, 3, rgb(110, 40, 150));
    gfx_fill_ellipse(x + 17, y + (up ? -5 : 2), 6, 3, rgb(110, 40, 150));
    gfx_fill_ellipse(x, y, 14, 11, body);
    gfx_fill_circle(x - 5, y - 3, 4, rgb(255, 255, 255));
    gfx_fill_circle(x + 5, y - 3, 4, rgb(255, 255, 255));
    float dx = wrap_dx(s_x - m->x);
    int look = dx > 0 ? 1 : -1;
    gfx_fill_rect(x - 5 + look, y - 3, 2, 2, rgb(0, 0, 0));
    gfx_fill_rect(x + 5 + look, y - 3, 2, 2, rgb(0, 0, 0));
    gfx_fill_rect(x - 6, y + 4, 12, 3, rgb(60, 10, 40));
    gfx_fill_rect(x - 4, y + 4, 2, 2, rgb(255, 255, 255));
    gfx_fill_rect(x + 2, y + 4, 2, 2, rgb(255, 255, 255));
}

static void draw_gem(int x, int y, int r, uint16_t c, uint16_t hi)
{
    gfx_spike(x - r, y - r, 2 * r, r, c);
    for (int j = 0; j < r; j++) {
        int half = r - j;
        gfx_fill_rect(x - half, y + j, 2 * half, 1, c);
    }
    gfx_fill_rect(x - 1, y - r / 2, 2, r / 2, hi);
}

#define HY      (K_RND ? 22 : 0)  // (the round frame: the text rows of the CoreS3 a little lower, in the circle)
#define HOP_INK rgb(14, 22, 44)   // the outline of the words

// a word in a Bungee face in the middle (big text and numbers look rough in the dot font); top = the top of
// the capitals
static void hop_word(const mfont_t *f, int top, const char *t, uint16_t c)
{
    mf_text_oc(f, SCR_W / 2, top + f->asc, t, c, HOP_INK, 2, 1);
}

// a line of small text (Rubik) in the middle
static void hop_line(int top, const char *t, uint16_t c)
{
    mf_text_oc(&MF_LABEL, SCR_W / 2, top + MF_LABEL.asc, t, c, HOP_INK, 1, 0);
}

static void hop_draw(int64_t now);
// the frame (the round panels: the game draws the square view, then it is scaled out into the circle)
static void hop_render(int64_t now)
{
#if K_RND
    if (!rv_begin()) {
        return;
    }
    hop_draw(now);
    rv_end();
#else
    hop_draw(now);
#endif
}
static void hop_draw(int64_t now)
{
    draw_background();
    for (int i = 0; i < s_nplat; i++) {
        draw_platform(&s_plat[i]);
    }
    for (int i = 0; i < s_ngem; i++) {
        int y = sy(s_gem[i].h) + (int)(2 * fsin(now * 0.006f + s_gem[i].x));
        draw_gem((int)s_gem[i].x, y, 6, rgb(60, 220, 255), rgb(220, 255, 255));
    }
    for (int i = 0; i < s_nmon; i++) {
        draw_monster(&s_mon[i]);
    }
    for (int i = 0; i < s_nbul; i++) {
        gfx_fill_circle((int)s_bul[i].x, sy(s_bul[i].h), 3, rgb(255, 230, 60));
    }
    for (int i = 0; i < NPART; i++) {
        const part_t *p = &s_part[i];
        if (p->life > 0) {
            gfx_fill_rect((int)p->x, sy(p->h), 2, 2, p->c);
        }
    }
    if (s_state != ST_OVER) {
        int bx = (int)s_x, by = sy(s_h) - PR;
        draw_blob(bx, by, now);
        if (bx < 14) {
            draw_blob(bx + SCR_W, by, now);
        }
        if (bx > SCR_W - 14) {
            draw_blob(bx - SCR_W, by, now);
        }
    }

    char buf[24];
#if K_RND
    if (s_state != ST_TITLE) {
        // (the round frame: the score and the best in the top cap of the circle)
        gfx_darken_rect(0, 0, SCR_W, 54, 1);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_score);
        mf_text_o(&MF_N20, SCR_W / 2 - 10 - mf_width(&MF_N20, buf, 1), 48, buf, s_text, HOP_INK, 1, 1);
        snprintf(buf, sizeof(buf), "HI %lu", (unsigned long)s_best);
        mf_text_o(&MF_N16, SCR_W / 2 + 10, 48, buf, s_accent, HOP_INK, 1, 1);
        if (s_jet_t > 0) {
            gfx_fill_rect(SCR_W / 2 - 30, 48, (int)(s_jet_t / JET_TIME * 60), 3, rgb(255, 170, 40));
        }
    }
#else
    if (s_state != ST_TITLE) {
        gfx_darken_rect(0, 0, SCR_W, 26, 1);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_score);
        mf_text_o(&MF_N20, 6, 23, buf, s_text, HOP_INK, 1, 1);
        snprintf(buf, sizeof(buf), "HI %lu", (unsigned long)s_best);
        mf_text_o(&MF_N16, SCR_W - 6 - mf_width(&MF_N16, buf, 1), 21, buf, s_accent, HOP_INK, 1, 1);
        if (s_jet_t > 0) {
            gfx_fill_rect(6, 28, (int)(s_jet_t / JET_TIME * 60), 3, rgb(255, 170, 40));
        }
    }
#endif
#if BOARD_BAR_SCREEN || BOARD_UI_TABLET
    if (s_state == ST_TITLE) {
        gfx_darken_rect(0, 90, SCR_W, 300, 2);
        hop_word(&MF_N34, 108, "HOP", s_accent);
        hop_line(176, "TILT TO MOVE", s_text);
        hop_line(196, "OR HOLD A SIDE", s_text);
        hop_line(222, "TAP MIDDLE:", s_text);
        hop_line(240, "SHOOT UP", s_text);
        hop_line(264, "STOMP MONSTERS", s_text);
        hop_line(282, "BROWN BREAKS!", rgb(255, 180, 120));
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
        hop_word(&MF_N20, 306, buf, s_accent);
        if (((now / 450) & 1) == 0) {
            hop_word(&MF_N16, 348, "TAP TO START", s_text);
        }
    } else if (s_state == ST_OVER) {
        gfx_darken_rect(0, 180, SCR_W, 200, 1);
        hop_word(&MF_N34, 196, "FELL!", rgb(255, 120, 120));
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_score);
        hop_word(&MF_N28, 244, buf, s_text);
        snprintf(buf, sizeof(buf), "%d M %d GEMS", (int)(s_max_h / 10), s_gems);
        hop_line(284, buf, s_text);
        if (s_new_best) {
            hop_word(&MF_N20, 304, "NEW BEST!", s_accent);
        } else {
            snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
            hop_word(&MF_N20, 304, buf, s_text);
        }
        if (now - s_over_at > 700 && ((now / 450) & 1) == 0) {
            hop_word(&MF_N16, 345, "TAP TO PLAY", s_accent);
        }
    }
#else
    if (s_state == ST_TITLE) {
        gfx_darken_rect(0, 36 + HY, SCR_W, 240, 2);
        hop_word(&MF_N34, 44 + HY, "HOP", s_accent);
        hop_line(92 + HY, "TILT TO MOVE", s_text);
        hop_line(109 + HY, "OR HOLD A SIDE", s_text);
        hop_line(130 + HY, "TAP MIDDLE:", s_text);
        hop_line(147 + HY, "SHOOT UP", s_text);
        hop_line(168 + HY, "STOMP MONSTERS", s_text);
        hop_line(185 + HY, "BROWN BREAKS!", rgb(255, 180, 120));
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
        hop_word(&MF_N20, 206 + HY, buf, s_accent);
        if (((now / 450) & 1) == 0) {
            hop_word(&MF_N16, 240 + HY, "TAP TO START", s_text);
        }
    } else if (s_state == ST_OVER) {
        gfx_darken_rect(0, 70 + HY, SCR_W, 170, 1);
        hop_word(&MF_N34, 80 + HY, "FELL!", rgb(255, 120, 120));
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)s_score);
        hop_word(&MF_N28, 124 + HY, buf, s_text);
        snprintf(buf, sizeof(buf), "%d M  %d GEMS", (int)(s_max_h / HS(10)), s_gems);
        hop_line(162 + HY, buf, s_text);
        if (s_new_best) {
            hop_word(&MF_N20, 182 + HY, "NEW BEST!", s_accent);
        } else {
            snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)s_best);
            hop_word(&MF_N20, 182 + HY, buf, s_text);
        }
        if (now - s_over_at > 700 && ((now / 450) & 1) == 0) {
            hop_word(&MF_N16, 214 + HY, "TAP TO PLAY", s_accent);
        }
    }
#endif
}

// ---------------------------------------------------------------- the menu icon (smooth shapes of tfdraw)
#include "tfdraw/tfdraw.h"

// one pixel row of a smooth shape: the shape covers l[k] .. r[k] in each of 4 rows of samples (l > r: none there)
TFD_FN void hop_row(int y, const float *l, const float *r, uint16_t c, int al)
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
// an oval with a smooth edge
TFD_FN void hop_oval(float cx, float cy, float rx, float ry, uint16_t c, int al)
{
    float l[4], r[4];
    for (int y = tfd_fl(cy - ry); y < tfd_ce(cy + ry); y++) {
        for (int k = 0; k < 4; k++) {
            float d = (y + 0.125f + k * 0.25f - cy) / ry, h2 = 1 - d * d, hw = h2 > 0 ? rx * sqrtf(h2) : -1;
            l[k] = cx - hw;
            r[k] = cx + hw;
        }
        hop_row(y, l, r, c, al);
    }
}
// a convex polygon (n points as x, y pairs, at most 16) with a smooth edge
TFD_FN void hop_cvx(const float *p, int n, uint16_t c, int al)
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
        hop_row(y, l, r, c, al);
    }
}
// a straight bar of width w from (x0, y0) to (x1, y1), with square ends (cheaper than a line with round ends)
TFD_FN void hop_bar(float x0, float y0, float x1, float y1, float w, uint16_t c, int al)
{
    float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
    if (l < 0.01f) {
        return;
    }
    float nx = -dy / l * w / 2, ny = dx / l * w / 2;
    float p[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    hop_cvx(p, 4, c, al);
}
// THE ICON: the blob bounces up from platform to platform, and the world goes down under it (one bounce each second,
// two kinds of platform: the loop of 2 s ends where it starts). It squashes when it lands; a gem sparkles.
static void hop_icon(int x, int y, int s)
{
    float t = icon_time(), u = s / 100.0f, q = fmodf(t, 1.0f);
    int bn = (int)fmodf(t, 2.0f);  // (the bounce of the loop: 0 or 1)
    float scroll = 33 * q;         // (the world goes down 33 units in each bounce)
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
    tfd_vgrad(x, y, s, s, rgb(84, 168, 248), rgb(206, 236, 255));
    // the clouds and the platforms of the world (a pattern of 66 units: green on the left, blue on the right)
    for (int i = -1; i < 4; i++) {
        int k = i + bn;  // (the number of the platform in the world)
        float wy = y + (78 - 33 * i + scroll) * u, px = x + ((k & 1) ? 68 : 32) * u;
        if (k & 1) {
            for (int c = 0; c < 3; c++) {
                tfd_disc(x + (14 + c * 9) * u, wy - (17 + (c == 1) * 4) * u, (6 + (c == 1) * 2) * u, rgb(255, 255, 255),
                         200);
            }
        }
        uint16_t pc = (k & 1) ? rgb(70, 140, 240) : rgb(80, 200, 80),
                 pd = (k & 1) ? rgb(36, 74, 160) : rgb(36, 110, 40);
        tfd_rrect((int)(px - 14 * u), (int)(wy + 1 * u), (int)(28 * u), (int)(7 * u), (int)(3.5f * u), pd, 256);
        tfd_rrect((int)(px - 14 * u), (int)wy, (int)(28 * u), (int)(5.6f * u), (int)(2.8f * u), pc, 256);
        tfd_rect((int)(px - 10 * u), (int)(wy + 1 * u), (int)(20 * u), (int)(1.2f * u + 0.5f), rgb(255, 255, 255), 110);
    }
    // the gem (in the world): it turns and sparkles
    float gy = y + (30 + scroll + (bn ? 33 : 0)) * u, gx = x + 84 * u, gw = fabsf(fcos(t * 3.14159f)) * 5 * u + u;
    if (gy > y + 101 * u) {
        gy -= 66 * u;
    }
    float gp[8] = {gx, gy - 7 * u, gx + gw, gy, gx, gy + 7 * u, gx - gw, gy};
    hop_cvx(gp, 4, rgb(40, 200, 250), 256);
    float gq[6] = {gx, gy - 7 * u, gx + gw * 0.5f, gy - 1 * u, gx - gw * 0.5f, gy - 1 * u};
    hop_cvx(gq, 3, rgb(210, 250, 255), 230);
    float tw = fsin(t * 6.2832f * 2) * 0.5f + 0.5f;
    hop_bar(gx + 6 * u, gy - 8 * u - 3 * u * tw, gx + 6 * u, gy - 8 * u + 3 * u * tw, 1.2f * u, rgb(255, 255, 255),
            230);
    hop_bar(gx + 3 * u * (1 - tw) + 3 * u, gy - 8 * u, gx + 9 * u - 3 * u * (1 - tw), gy - 8 * u, 1.2f * u,
            rgb(255, 255, 255), 230);
    // the blob: from the platform under it to the next one, high in the middle; squashed at the ends
    float h = 4 * q * (1 - q), sq = fmaxf(0, 1 - q / 0.12f) + fmaxf(0, (q - 0.9f) / 0.1f);
    float e = q * q * (3 - 2 * q),
          bx = x + (((bn & 1) ? 68 : 32) + (((bn & 1) ? 32 : 68) - ((bn & 1) ? 68 : 32)) * e) * u;
    float rx = 11 * u * (1 + 0.22f * sq - 0.06f * h), ry = 11 * u * (1 - 0.22f * sq + 0.08f * h);
    float by = y + (78 - 34 * h) * u - ry;
    hop_oval(bx + 1.5f * u, y + 79 * u, rx * 0.8f * (1 - 0.4f * h), 1.8f * u, rgb(20, 60, 30), 70 - (int)(50 * h));
    hop_oval(bx, by, rx + 1.2f * u, ry + 1.2f * u, rgb(30, 92, 28), 256);
    hop_oval(bx, by, rx, ry, rgb(120, 214, 64), 256);
    hop_oval(bx + 2 * u, by + ry * 0.35f, rx * 0.75f, ry * 0.5f, rgb(80, 170, 46), 200);
    hop_oval(bx - rx * 0.38f, by - ry * 0.45f, rx * 0.32f, ry * 0.22f, rgb(230, 255, 200), 220);
    for (int sd = -1; sd <= 1; sd += 2) {
        hop_oval(bx + sd * 4.2f * u, by - 2.5f * u, 3.4f * u, 4.2f * u * (1 - 0.3f * sq), rgb(255, 255, 255), 256);
        tfd_disc(bx + sd * 4.2f * u + 0.6f * u, by - 3.6f * u, 1.8f * u, rgb(20, 24, 30), 256);
    }
    tfd_noclip();
}

const app_t app_hop = {
        .name = "HOP",
        .landscape = false,
        .needs = 0,             // the touch sides also move the blob
        .round = APP_ROUND_HI,  // (a round panel: the view in the whole circle, hop_rv.inc)
        .init = hop_init,
        .enter = hop_enter,
        .leave = hop_leave,
        .input = hop_input,
        .update = hop_update,
        .render = hop_render,
        .draw_icon = hop_icon,
};
