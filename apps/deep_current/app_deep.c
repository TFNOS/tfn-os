// DEEP CURRENT (a store app for the 3.49): you are a clear sea creature that glows from inside. It eats,
// and it becomes what it eats: each prey carries the light of one trait (a tail, fins, jaws, spines, a shell,
// stingers, a lure, ink, a jet, a glass skin); when the gut is full, three cards from the gut offer the next body
// part. A bite of a hunter tears off the segment that it bites: the body is the life. When the creature grew enough,
// a deep current of light opens and carries it down: the shallows, the twilight, the midnight, the abyss.
//   Controls: tilt = swim (the board is a window: the creature goes "down the hill"; the position at the dive start is
//   the zero); a touch = a dash (the jaws bite in a dash); the BOOT key, or a tap on the ring at the bottom right =
//   the organ: PING (all the life shows), FLASH (the lure: the hunters are blind and flee), INK (a cloud: the hunters
//   lose the creature). The panel: CONTROL (TILT / TOUCH: the creature swims to the finger, a quick tap dashes),
//   SOUND, MUSIC, LANGUAGE, THE TITLE.
//   Files: dc_core.inc (helpers, traits, species, zones, texts), dc_gfx.inc (the drawing), dc_world.inc (camera, water,
//   far layers, snow, particles, plankton, the current), dc_body.inc (the creature), dc_life.inc (the sea life, the
//   hunters, the rival, eating and bites), dc_ui.inc (icons, HUD, cards, title, summary, menu icon), dc_music.c/.h.
//   Saves: deep_best (m), deep_dives, deep_unl (the traits in the sea), deep_seen, deep_lang, deep_snd, deep_mus,
//   deep_ctl. The sound effects: /sdcard/deep/sfx/*.mp3 (the data pack).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "midi_bg/midi_bg.h"
#if __has_include("dc_music.h")
#include "dc_music.h"
#include "dc_sfx.h"
#define DC_HAVE_MUSIC 1
#else
#define DC_HAVE_MUSIC 0
#endif
#include "tfsfx/tfsfx.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#define DC_DIR BOARD_SD_ROOT "/deep"
#else
#define DC_DIR "deep_card"
#endif
// The round panels (app_t.round = APP_ROUND_HI; the AMOLED-1.75C 466 x 466, the Watcher 412 x 412): the sea and the
// HUD in a square frame of 310 x 310 (about the pixels of the CoreS3 frame: the same speed), scaled out to the circle
// by libs/rndout (3 px from 2 on the 1.75C). The code of the narrow frame lays out the HUD from s_fw x s_fh.
#if BOARD_ROUND
#include "rndout/rndout.h"
static rndo_t s_ro;
static bool s_rnd;  // (the round frame: gfx_round() at the enter)
#define RND s_rnd
#else
#define RND 0
#endif

// the states of the app (dc_ui.inc and dc_life.inc read them)
enum {
    ST_TITLE,
    ST_HATCH,
    ST_PLAY,
    ST_CHOICE,
    ST_DESCENT,
    ST_DEATH,
    ST_SUMMARY
};
static int s_state;
static float s_state_t;
static bool s_state_play(void)
{
    return s_state == ST_PLAY;
}

#include "dc_core.inc"
#include "dc_gfx.inc"
#include "dc_world.inc"
#include "dc_body.inc"
#include "dc_life.inc"
#include "dc_ui.inc"

// ================================================================ the sound and the music
static bool s_snd = true, s_music = true;
static const char *const FXN[FX_N] = {"eat",   "eat2",    "gulp",  "plank", "dash",   "bitten", "bite",
                                      "shell", "prick",   "sting", "grow",  "pick",   "flash",  "ping",
                                      "ink",   "current", "lunge", "hatch", "bubble", "death"};
static void fx(int id, int vol, float var)
{
    if (s_snd && id >= 0 && id < FX_N) {
        tfsfx_play_var(id, vol, var);
    }
}
static float s_jingle_left;  // the time to the end of a jingle (s); then the loop s_after
static int s_after = -1;
static float s_after_at;  // the beat where the loop s_after goes on (a growth: the place of the zone)
#if DC_HAVE_MUSIC
static const int ZONE_TRACK[NZONE] = {DCM_Z1, DCM_Z2, DCM_Z3, DCM_Z4};
static void mus_loop_at(int tr, float at)
{
    s_jingle_left = 0;
    s_after = -1;
    if (s_music) {
        midi_bg_play_track(tr, at, true);
    }
}
static void mus_loop(int tr)
{
    mus_loop_at(tr, 0);
}
static void mus_jingle(int j, int ms, int after)
{
    if (!s_music) {
        return;
    }
    midi_bg_play_track(j, 0, false);
    s_jingle_left = ms / 1000.0f - 0.03f;
    s_after = after;
    s_after_at = 0;
}
// a growth: the zone loop goes on from the bar where it was (the beat before the jingle, on a bar line)
static void mus_grow(void)
{
    if (!s_music) {
        return;
    }
    midi_bg_state_t st;
    midi_bg_state(&st);
    float at = st.track == ZONE_TRACK[s_zone] ? 4.0f * (int)(st.beat / 4.0f) : 0;
    mus_jingle(DCM_J_GROW, DC_J_GROW_MS, ZONE_TRACK[s_zone]);
    s_after_at = at;
}
#define MUS_TITLE()        mus_loop(DCM_TITLE)
#define MUS_ZONE()         mus_loop(ZONE_TRACK[s_zone])
#define MUS_J(name, after) mus_jingle(DCM_J_##name, DC_J_##name##_MS, after)
#define ZONE_TR(z)         ZONE_TRACK[z]
#define TITLE_TR           DCM_TITLE
#else
#define MUS_TITLE()                                                                                                    \
    do {                                                                                                               \
    } while (0)
#define MUS_ZONE()                                                                                                     \
    do {                                                                                                               \
    } while (0)
#define MUS_J(name, after)                                                                                             \
    do {                                                                                                               \
        (void)(after);                                                                                                 \
    } while (0)
#define ZONE_TR(z) (z)
#define TITLE_TR   0
#endif
static void music_step(float dt)
{
#if DC_HAVE_MUSIC
    if (s_jingle_left > 0) {
        s_jingle_left -= dt;
        if (s_jingle_left <= 0) {
            s_jingle_left = 0;
            if (s_after >= 0) {
                mus_loop_at(s_after, s_after_at);
            } else {
                midi_bg_stop(400);
            }
        }
    }
#else
    (void)dt;
#endif
}

// ================================================================ the input
static int s_ctl;  // 0 = TILT, 1 = TOUCH
static float s_tx0, s_ty0;
static void tilt_zero(void)
{
    if (imu_ok()) {
        imu_tilt_logical(&s_tx0, &s_ty0);
    }
}
static bool s_fdown;  // a finger on the glass (TOUCH: the creature swims to it)
static float s_fing_x, s_fing_y;
static int64_t s_fdown_ms;
static float s_ping_t = 9;  // the ring of a PING (s since it)
// (the ring of the organ on each frame: dc_ui.inc organ_cx/organ_cy; a fixed point missed the CoreS3 and the Tab5)
static bool at_organ(int x, int y)
{
    int dx = x - organ_cx(), dy = y - organ_cy();
    return dx * dx + dy * dy < 30 * 30;
}
// the wanted way of the swim (a unit vector) and the thrust 0..1
static void control_read(float *dx, float *dy, float *m)
{
    *dx = cosf(B.a);
    *dy = sinf(B.a);
    *m = 0;
    if (s_ctl == 1) {
        if (!s_fdown) {
            return;
        }
        float hx = sx_of(B.x[0]), hy = sy_of(B.y[0]);
        float vx = s_fing_x - hx, vy = s_fing_y - hy, d = sqrtf(vx * vx + vy * vy);
        if (d < 6) {
            return;
        }
        *dx = vx / d;
        *dy = vy / d;
        *m = clampf((d - 6) / 70.0f, 0, 1);
        return;
    }
    if (!imu_ok()) {
        return;
    }
    float tx, ty;
    imu_tilt_logical(&tx, &ty);
    float vx = tx - s_tx0, vy = ty - s_ty0, mag = sqrtf(vx * vx + vy * vy);
    // (a dead zone of about 1.5 degrees, the full thrust at about 9 degrees)
    if (mag < 0.026f) {
        return;
    }
    *dx = vx / mag;
    *dy = vy / mag;
    *m = clampf((mag - 0.026f) / 0.13f, 0, 1);
}

// ================================================================ the saves
static void save_meta(void)
{
    store_set("deep_best", (uint32_t)s_best_m);
    store_set("deep_dives", (uint32_t)s_dives);
    store_set("deep_unl", s_unl);
    store_set("deep_seen", s_seen);
}

// ================================================================ the dive
static float s_zbase = 1.6f;  // the zoom of the play (smaller as the creature grows)
static float s_trail_t;
static int s_max_m;       // the deepest point of this dive (m)
static float s_fade;      // a dark cover (the ride down, the death)
static bool s_zone_done;  // the ride down: the next zone is made
static float zbase_of(const body_t *b)
{
    return clampf(2.0f - b->len / 140.0f, 1.0f, 1.9f);
}
static int depth_m(void)
{
    return (int)(Z->m0 + clampf(B.y[0] / Z->h, 0, 1) * (Z->m1 - Z->m0));
}
static void zone_set(int z)
{
    s_zone = z;
    Z = &ZONE[z];
    far_make();
}
static void cam_jump(float zoom)
{
    C.x = B.x[0];
    C.y = B.y[0];
    C.z = zoom;
    C.sh = 0;
    C.punch = 0;
    C.wide = 0;
    snow_make();
}
static void dive_start(void)
{
    zone_set(0);
    body_make(&B, 300, 120, 2, 0);
    B.a = 0.3f;
    memset(&R, 0, sizeof R);
    s_rival_t = 30;
    s_food = 0;
    s_ngut = 0;
    s_need = need_of(0);
    s_steps = 0;
    s_eaten = 0;
    s_grow_ready = false;
    s_cause = -1;
    s_cur_on = false;
    s_cur_open = 0;
    s_npart = 0;
    s_reveal = 0;
    s_flash = 0;
    s_ink_blind = 0;
    s_msg_t = 0;
    s_banner_t = 0;
    s_max_m = 0;
    s_run_t = 0;
    s_sum_unl = 0;
    s_sum_won = false;
    s_sum_best = false;
    s_fade = 0;
    s_ping_t = 9;
    zone_spawn();
    s_zbase = zbase_of(&B);
    cam_jump(s_zbase * 1.7f);
    tilt_zero();
    static const uint8_t HC[3] = {232, 251, 255};
    burst(B.x[0], B.y[0], 24, 60, HC, PK_SPARK);
    for (int i = 0; i < 6; i++) {
        part_new(PK_BUBBLE, B.x[0] + rnds() * 6, B.y[0] + rnds() * 4, rnds() * 10, -20 - rndf() * 20, 1.4f, 1, HC);
    }
    fx(FX_HATCH, 80, 0);
    MUS_J(HATCH, ZONE_TR(0));
    s_state = ST_HATCH;
    s_state_t = 0;
}
static void unlock(int t)
{
    if (s_unl >> t & 1) {
        return;
    }
    s_unl |= 1u << t;
    s_sum_unl |= 1u << t;
    store_set("deep_unl", s_unl);  // (at once: a dive that the player leaves keeps its unlock)
    char b[48];
    snprintf(b, sizeof b, "%s: %s", TX(X_UNLOCK), TNAME(t));
    msg_show(b, TRAIT[t].c, 2.6f);
}
static void end_dive(void)
{
    s_sum_m = s_sum_won ? END_M : s_max_m;
    s_sum_best = s_sum_m > s_best_m;
    if (s_sum_best) {
        s_best_m = s_sum_m;
    }
    if (s_eaten >= 40) {
        unlock(T_GLASS);
    }
    s_dives++;
    save_meta();
    // the portrait: the creature straight, lit
    s_portrait = B;
    body_t *P = &s_portrait;
    P->dead = 0;
    P->hurt = 0;
    P->stun = 0;
    P->dash_t = 0;
    P->a = 0;
    P->pulse = -10;
    P->head_grow = 1;
    float x = 1000;
    for (int i = 0; i <= P->n; i++) {
        P->grow[i] = 1;
        if (i > 0) {
            x -= seg_space(P, i);
        }
        P->x[i] = x;
        P->y[i] = 200;
    }
    body_derive(P);
    s_state = ST_SUMMARY;
    s_state_t = 0;
}
static void title_go(void);
// the current opens: the growth goal of the zone is reached
static void current_open(void)
{
    if (s_cur_on) {
        return;
    }
    s_cur_on = true;
    s_cur_x = wrapx(B.x[0] + (rndf() < 0.5f ? 520 : -520));
    static const uint8_t CC[3] = {120, 230, 255};
    msg_show(TX(X_OPEN), CC, 2.4f);
    fx(FX_CURRENT, 80, 0);
}
static void next_zone(void)
{
    zone_set(s_zone + 1);
    // (the creature comes in at the top of the new zone, at the same x)
    float dy = 34 - B.y[0];
    for (int i = 0; i <= B.n; i++) {
        B.y[i] += dy;
    }
    B.ky = 0;
    B.kx = 0;
    memset(&R, 0, sizeof R);
    s_rival_t = 25;
    s_cur_on = false;
    s_cur_open = 0;
    s_npart = 0;
    zone_spawn();
    cam_jump(s_zbase);
    s_banner_t = 3.4f;
    if (s_zone == 1) {
        unlock(T_JET);
    }
    if (s_zone == 2) {
        unlock(T_LURE);
    }
    if (s_zone == 3) {
        unlock(T_INK);
    }
}

// ================================================================ the growth: three cards from the gut
static int trait_room(int t)
{
    // (false: the body has the most of this trait, or no room for a new segment)
    if (!(s_unl >> t & 1)) {
        return 0;
    }
    if (t == T_JAWS) {
        return B.jaws < TRAIT[t].maxn;
    }
    if (t == T_LURE) {
        return B.lure < 1;
    }
    if (B.n >= MAXSEG) {
        return 0;
    }
    if (TRAIT[t].maxn && B.cnt[t] >= TRAIT[t].maxn) {
        return 0;
    }
    return 1;
}
static bool choice_open(void)
{
    float w[T_N] = {0};
    for (int i = 0; i < s_ngut; i++) {
        if (s_gut_t[i] != T_NONE) {
            w[s_gut_t[i]] += s_gut_f[i];
        }
    }
    int n = 0;
    for (int k = 0; k < 3; k++) {
        float sum = 0;
        for (int t = 1; t < T_N; t++) {
            if (w[t] > 0 && trait_room(t)) {
                sum += w[t];
            }
        }
        if (sum <= 0) {
            break;
        }
        float r = rndf() * sum;
        int pick = -1;
        for (int t = 1; t < T_N; t++) {
            if (w[t] <= 0 || !trait_room(t)) {
                continue;
            }
            pick = t;
            if ((r -= w[t]) <= 0) {
                break;
            }
        }
        if (pick < 0) {
            break;
        }
        s_card[n] = (uint8_t)pick;
        s_card_wild[n] = false;
        n++;
        w[pick] = 0;
    }
    // the rest: WILD cards (a random trait of the sea that the gut did not hold)
    for (int guard = 0; n < 3 && guard < 60; guard++) {
        int t = rndi(1, T_N - 1);
        bool dup = false;
        for (int k = 0; k < n; k++) {
            dup |= s_card[k] == t;
        }
        if (dup || !trait_room(t)) {
            continue;
        }
        s_card[n] = (uint8_t)t;
        s_card_wild[n] = true;
        n++;
    }
    if (n < 3) {
        return false;
    }
    s_pick = -1;
    s_pick_t = 0;
    s_state = ST_CHOICE;
    s_state_t = 0;
    fx(FX_GROW, 80, 0);
    return true;
}
static void growth_done(void)
{
    s_steps++;
    s_food -= s_need;
    if (s_food < 0) {
        s_food = 0;
    }
    s_ngut = 0;
    if (s_food > 0) {
        s_gut_t[0] = T_NONE;
        s_gut_f[0] = s_food;
        s_ngut = 1;
    }
    s_need = need_of(s_steps);
    s_grow_ready = s_food >= s_need;
    if (s_steps >= Z->goal) {
        current_open();
    }
}
static void card_pick(int k)
{
    if (s_pick >= 0) {
        return;
    }
    int t = s_card[k];
    s_pick = k;
    s_pick_t = 0;
    body_add(&B, t);
    s_seen |= 1u << t;
    msg_show(TNAME(t), TRAIT[t].c, 2.2f);
    fx(FX_PICK, 85, 0);
#if DC_HAVE_MUSIC
    mus_grow();
#endif
    growth_done();
}

// ================================================================ the organ (the BOOT key)
static void organ(void)
{
    if (B.boot_cd > 0 || B.dead > 0) {
        return;
    }
    if (B.cnt[T_INK]) {
        int m = B.n / 2;
        static const uint8_t RC[3] = {255, 75, 110};
        for (int k = 0; k < 6; k++) {
            part_new(PK_INK, B.x[m] + rnds() * 8, B.y[m] + rnds() * 6, -cosf(B.a) * (20 + k * 6) + rnds() * 10,
                     -sinf(B.a) * 20 + rnds() * 10, 2.6f, 1.1f + k * 0.08f, RC);
        }
        s_ink_blind = 3.5f;
        for (int i = 0; i < NLIFE; i++) {
            life_t *e = &s_life[i];
            if (e->on && is_hunter(e->sp) && (e->st == LS_STALK || e->st == LS_COIL)) {
                e->st = LS_IDLE;
                e->t = 0;
            }
        }
        if (R.n > 0) {
            s_rival_recoil = 3;
        }
        B.boot_cd = 7;
        fx(FX_INK, 85, 0.05f);
    } else if (B.lure) {
        s_flash = 1;
        s_reveal = 1;
        for (int i = 0; i < NLIFE; i++) {
            life_t *e = &s_life[i];
            if (!e->on || !is_hunter(e->sp)) {
                continue;
            }
            if (sqr(wrapd(e->x - B.x[0])) + sqr(e->y - B.y[0]) < 190 * 190) {
                e->stun = 2.2f;
                e->st = LS_FLEE;
                e->t = 0;
            }
        }
        if (R.n > 0 && sqr(wrapd(R.x[0] - B.x[0])) + sqr(R.y[0] - B.y[0]) < 190 * 190) {
            R.stun = 1.6f;
            s_rival_recoil = 3;
        }
        cam_kick(2);
        B.boot_cd = 6;
        fx(FX_FLASH, 85, 0.03f);
    } else {
        s_reveal = 1;
        s_ping_t = 0;
        B.boot_cd = 5;
        fx(FX_PING, 75, 0.03f);
    }
}
static void dash(void)
{
    if (!body_dash(&B)) {
        return;
    }
    fx(FX_DASH, 70, 0.1f);
    static const uint8_t BC[3] = {200, 230, 240};
    int t = B.n;
    for (int i = 0; i < 3; i++) {
        part_new(PK_BUBBLE, B.x[t] + rnds() * 4, B.y[t] + rnds() * 4, -cosf(B.a) * 30 + rnds() * 10, -10 - rndf() * 15,
                 1.0f, 0.5f, BC);
    }
    C.punch = fmaxf(C.punch, 0.04f);
}

// ================================================================ the steps of the world
static void world_step(float dt, bool play)
{
    s_time += dt;
    life_step(dt);
    if (play) {
        player_feed(dt);
        stings_step(dt);
        zone_refill(dt);
        // the rival: the midnight and the abyss
        if (s_zone >= 2) {
            if (R.n <= 0 && R.dead == 0 && R.len == 0) {
                if ((s_rival_t -= dt) <= 0) {
                    rival_spawn();
                }
            }
            if (R.dead > 3) {
                memset(&R, 0, sizeof R);
            }
        }
    }
    rival_step(dt);
    plank_step(dt);
    parts_step(dt);
    // the lure draws the prey that do not flee
    if (B.lure && B.dead == 0) {
        float lx = B.x[0], ly = B.y[0] - 14 * B.S, r = fmaxf(B.light, 40) * 1.4f;
        for (int i = 0; i < NLIFE; i++) {
            life_t *e = &s_life[i];
            if (!e->on || is_hunter(e->sp) || e->school >= 0 || e->st == LS_FLEE || e->sp == SP_BIGJELLY) {
                continue;
            }
            float dx = wrapd(lx - e->x), dy = ly - e->y, d2 = dx * dx + dy * dy;
            if (d2 < r * r && d2 > 1) {
                float d = sqrtf(d2);
                e->vx += dx / d * 16 * dt;
                e->vy += dy / d * 16 * dt;
            }
        }
    }
    // the trail of the creature
    s_trail_t -= dt;
    if (s_trail_t <= 0 && B.dead == 0 && B.sp > 15) {
        s_trail_t = 0.05f;
        static const uint8_t TC[3] = {99, 234, 255};
        part_new(PK_TRAIL, B.x[B.n], B.y[B.n], 0, 0, 0.9f, 1, TC);
    }
    // the deep current opens slowly
    s_cur_open = clampf(s_cur_open + (s_cur_on ? dt / 1.5f : -dt), 0, 1);
    if (s_reveal > 0) {
        s_reveal = fmaxf(0, s_reveal - dt / 2.5f);
    }
    if (s_flash > 0) {
        s_flash = fmaxf(0, s_flash - dt / 0.45f);
    }
    if (s_ink_blind > 0) {
        s_ink_blind -= dt;
    }
    if (s_msg_t > 0) {
        s_msg_t -= dt;
    }
    if (s_banner_t > 0) {
        s_banner_t -= dt;
    }
    s_ping_t += dt;
    // the camera and the pool of light of the creature (in the dark)
    s_zbase += (zbase_of(&B) - s_zbase) * clampf(dt * 0.5f, 0, 1);
    cam_step(dt, B.x[0], B.y[0], B.vx, B.vy, s_zbase);
    snow_step(dt);
    s_pool_k = Z->dark * (B.dead > 0 ? clampf(1 - B.dead, 0, 1) : 1);
    s_pool_x = sx_of(B.x[0]);
    s_pool_y = sy_of(B.y[0]);
    s_pool_r = fmaxf(B.light, 30) * C.z * 1.3f;
}

// ================================================================ the title: a creature swims by itself
static float s_demo_a;
static void title_go(void)
{
    zone_set(0);
    body_make(&B, 300, 150, 0, 0);
    static const uint8_t DT[] = {T_TAIL, T_FIN, T_SPINE, T_STING, T_SHELL, T_FIN, T_TAIL};
    for (int i = 0; i < (int)sizeof DT; i++) {
        body_add(&B, DT[i]);
    }
    body_add(&B, T_JAWS);
    for (int i = 0; i <= B.n; i++) {
        B.grow[i] = 1;
        B.x[i] = wrapx(300 - i * 8 * B.S);
        B.y[i] = 150;
    }
    B.head_grow = 1;
    body_derive(&B);
    memset(&R, 0, sizeof R);
    s_npart = 0;
    s_cur_on = false;
    s_cur_open = 0;
    s_msg_t = 0;
    s_banner_t = 0;
    s_fade = 0;
    zone_spawn();
    s_zbase = 1.15f;
    cam_jump(s_zbase);
    s_demo_a = 0;
    MUS_TITLE();
    s_state = ST_TITLE;
    s_state_t = 0;
}
static void demo_step(float dt)
{
    // a slow wander in a band of the zone, a dash now and then
    s_demo_a += dt * 0.35f;
    float want = sinf(s_demo_a) * 0.5f + sinf(s_demo_a * 0.37f) * 0.4f;
    float y = B.y[0];
    if (y < 90) {
        want = 0.5f;
    }
    if (y > 230) {
        want = -0.5f;
    }
    body_move(&B, dt, cosf(want), sinf(want), 0.55f);
    if (fmodf(s_time, 6.0f) < dt) {
        body_dash(&B);
    }
    player_feed(dt);
    s_grow_ready = false;
    s_food = 0;
    s_ngut = 0;
}

// ================================================================ the app
static bool s_ok;
static int64_t s_last;
static char s_err[48];
enum {
    DI_CTL,
    DI_SND,
    DI_MUS,
    DI_LANG,
    DI_TITLE
};
static const char *const CTL_V[] = {"TILT", "הטיה", "TOUCH", "מגע"};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const pitem_t ITEMS[] = {
        {PI_PICK, DI_CTL, 2, 0, {"CONTROL", "שליטה"}, CTL_V, "deep_ctl"},
        {PI_PICK, DI_SND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, "deep_snd"},
        {PI_PICK, DI_MUS, 2, 0, {"MUSIC", "מוזיקה"}, ONOFF_V, "deep_mus"},
        {PI_PICK, DI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "deep_lang"},
        {PI_ACT, DI_TITLE, 0, 0, {"THE TITLE", "המסך הראשי"}, NULL, NULL},
};
static bool item_shown(int id)
{
    return id != DI_TITLE || s_state != ST_TITLE;
}
static void item_set(int id, int v)
{
    if (id == DI_CTL) {
        s_ctl = v & 1;
        tilt_zero();
    } else if (id == DI_SND) {
        s_snd = v == 0;
    } else if (id == DI_MUS) {
        s_music = v == 0;
        if (!s_music) {
            midi_bg_stop(300);
        } else {
            midi_bg_start();
#if DC_HAVE_MUSIC
            if (s_state == ST_TITLE) {
                MUS_TITLE();
            } else {
                MUS_ZONE();
            }
#endif
        }
    } else if (id == DI_LANG) {
        s_lang = v & 1;
    } else if (id == DI_TITLE) {
        title_go();
    }
}
static void item_resume(void)
{
    tilt_zero();
}
static const pitems_t PANEL = {ITEMS, 5, item_shown, NULL, item_set, NULL, item_resume};

static void enter_main(int64_t now);
static void enter(int64_t now)
{
#if BOARD_ROUND
    s_rnd = gfx_round() && rndo_open(&s_ro, 310);
    if (s_rnd) {
        rndo_enter(&s_ro, false);  // (the code sees the square frame)
        enter_main(now);
        rndo_leave(&s_ro);
        return;
    }
#endif
    enter_main(now);
}
static void enter_main(int64_t now)
{
    s_last = now;
    s_ok = false;
    s_err[0] = 0;
    s_lang = (int)store_get("deep_lang", 1) ? 1 : 0;
    s_ctl = (int)store_get("deep_ctl", imu_ok() ? 0 : 1) & 1;  // (no motion sensor, as the Watcher: TOUCH)
    s_snd = store_get("deep_snd", 0) == 0;
    s_music = store_get("deep_mus", 0) == 0;
    s_best_m = (int)store_get("deep_best", 0);
    s_dives = (int)store_get("deep_dives", 0);
    s_unl = store_get("deep_unl", UNL_BASE) | UNL_BASE;
    s_seen = store_get("deep_seen", 0);
    s_rng = (uint32_t)(now * 2654435761u) | 1;
    clip_full();
    sysui_panel_items(&PANEL);
    tfsfx_load_mem(DC_SFX_DATA, DC_SFX_LEN, FXN, FX_N < DC_SFX_N ? FX_N : DC_SFX_N, 41);  // (in the app file: no card)
#if DC_HAVE_MUSIC
    midi_bg_init(DC_TRACKS, DC_TRACK_COUNT, DC_MUSIC_VOICES);
    midi_bg_volume(70);
    if (s_music) {
        midi_bg_start();
    }
#endif
    s_fdown = false;
    title_go();
    s_ok = true;
}
static void leave(void)
{
    if (s_ok) {
        save_meta();  // (the traits that the creature swallowed in a dive that did not end)
    }
#if DC_HAVE_MUSIC
    midi_bg_end();
#endif
    tfsfx_free();
    s_ok = false;
#if BOARD_ROUND
    rndo_close(&s_ro);
    s_rnd = false;
#endif
}
static void input_main(const input_event_t *ev);
static void input(const input_event_t *ev)
{
#if BOARD_ROUND
    if (s_rnd) {  // (a point of the panel to a point of the square frame)
        input_event_t e = *ev;
        int x = e.x, y = e.y;
        rndo_point(&s_ro, &x, &y);
        e.x = (int16_t)x;
        e.y = (int16_t)y;
        rndo_enter(&s_ro, false);
        input_main(&e);
        rndo_leave(&s_ro);
        return;
    }
#endif
    input_main(ev);
}
static void input_main_ev(const input_event_t *ev)
{
    if (!s_ok) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (ev->kind == IN_DOWN) {
        s_fdown = true;
        s_fing_x = (float)x;
        s_fing_y = (float)y;
        s_fdown_ms = s_last;
    } else if (ev->kind == IN_MOVE) {
        s_fing_x = (float)x;
        s_fing_y = (float)y;
    } else if (ev->kind == IN_UP) {
        s_fdown = false;
    }
    switch (s_state) {
    case ST_TITLE:
        if (ev->kind == IN_TAP) {
            fx(FX_PICK, 60, 0);
            dive_start();
        }
        break;
    case ST_PLAY:
        if (ev->kind == IN_DOWN && at_organ(x, y)) {
            organ();
            break;
        }
        // TILT: a touch dashes at once; TOUCH: the finger steers, a quick tap dashes
        if (s_ctl == 0 && ev->kind == IN_DOWN) {
            dash();
        } else if (s_ctl == 1 && ev->kind == IN_TAP && !at_organ(x, y) && s_last - s_fdown_ms < 250) {
            dash();
        }
        break;
    case ST_CHOICE:
        // (a tap: the finger must come off the glass on the card; the first 0.4 s takes no tap)
        if (ev->kind == IN_TAP && s_state_t > 0.4f && s_pick < 0) {
            int k = card_at(x, y);
            if (k >= 0) {
                card_pick(k);
            }
        }
        break;
    case ST_SUMMARY:
        if (ev->kind == IN_TAP && s_state_t > 1.5f) {
            dive_start();
        }
        break;
    default:
        break;
    }
}
// a slow tap (a press of 350 - 900 ms) on the title, the cards of the growth and the summary acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void input_main(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    input_main_ev(ev);
    if (slow && (s_state == ST_TITLE || s_state == ST_CHOICE || s_state == ST_SUMMARY)) {
        input_main_ev(&tap);
    }
}
static void update_main(int64_t now);
static void update(int64_t now)
{
#if BOARD_ROUND
    if (s_rnd) {
        rndo_enter(&s_ro, false);
        update_main(now);
        rndo_leave(&s_ro);
        return;
    }
#endif
    update_main(now);
}
static void update_main(int64_t now)
{
    float dt = clampf((now - s_last) / 1000.0f, 0, 0.05f);
    s_last = now;
    if (!s_ok) {
        return;
    }
#if DC_HAVE_MUSIC
    midi_bg_service();
#endif
    music_step(dt);
    s_state_t += dt;
    int keys = app_key_presses();
    switch (s_state) {
    case ST_TITLE:
        demo_step(dt);
        world_step(dt, false);
        if (keys > 0) {
            dive_start();
        }
        break;
    case ST_HATCH: {
        float dx, dy, m;
        control_read(&dx, &dy, &m);
        body_move(&B, dt, dx, dy, m * clampf(s_state_t / 1.2f, 0, 1));
        world_step(dt, false);
        if (s_state_t > 1.6f) {
            s_state = ST_PLAY;
            s_state_t = 0;
        }
        break;
    }
    case ST_PLAY: {
        s_run_t += dt;
        float dx, dy, m;
        control_read(&dx, &dy, &m);
        body_move(&B, dt, dx, dy, m);
        if (keys > 0) {
            organ();
        }
        world_step(dt, true);
        int dm = depth_m();
        if (dm > s_max_m) {
            s_max_m = dm;
        }
        // the deep current: a soft pull toward its column, the ride down in it
        if (s_cur_on && s_cur_open > 0.5f) {
            float cx = wrapd(s_cur_x - B.x[0]);
            if (fabsf(cx) < 90 && B.y[0] > Z->h * 0.35f) {
                B.kx += (cx > 0 ? 1 : -1) * 30 * dt;
            }
            if (fabsf(cx) < 26 && B.y[0] > Z->h * 0.5f) {
                s_state = ST_DESCENT;
                s_state_t = 0;
                s_zone_done = false;
                fx(FX_CURRENT, 90, 0);
                MUS_J(DESCENT, s_zone + 1 < NZONE ? ZONE_TR(s_zone + 1) : TITLE_TR);
                break;
            }
        }
        if (B.dead > 0) {
            s_state = ST_DEATH;
            s_state_t = 0;
            fx(FX_DEATH, 90, 0);
            MUS_J(DEATH, TITLE_TR);
            break;
        }
        if (s_grow_ready && B.dead == 0) {
            if (!choice_open()) {
                s_grow_ready = false;
                growth_done();
            }
        }
        break;
    }
    case ST_CHOICE:
        // (the sea goes on in slow motion behind the cards)
        world_step(dt * 0.12f, false);
        if (s_pick >= 0) {
            s_pick_t += dt;
            if (s_pick_t > 0.55f) {
                s_state = ST_PLAY;
                s_state_t = 0;
                tilt_zero();
            }
        }
        break;
    case ST_DESCENT: {
        // the current takes the creature down; the screen goes dark; the next zone
        float t = s_state_t;
        float cx = wrapd(s_cur_x - B.x[0]);
        B.kx = cx * 3;
        B.ky = 120;
        body_move(&B, dt, 0, 1, 1);
        world_step(dt, false);
        C.wide = fminf(0.25f, t * 0.3f);
        s_fade = t < 1.4f ? clampf(t / 1.4f, 0, 1) : clampf(1 - (t - 1.4f) / 1.0f, 0, 1);
        if (t >= 1.4f && !s_zone_done) {
            s_zone_done = true;
            if (s_zone + 1 >= NZONE) {
                s_sum_won = true;
                MUS_J(DEEP, TITLE_TR);
                end_dive();
                break;
            }
            next_zone();
        }
        if (t > 2.4f) {
            s_state = ST_PLAY;
            s_state_t = 0;
            C.wide = 0;
            s_fade = 0;
        }
        break;
    }
    case ST_DEATH:
        body_move(&B, dt, 0, 0, 0);
        world_step(dt, false);
        s_fade = clampf((s_state_t - 1.2f) / 1.6f, 0, 0.7f);
        if (s_state_t > 2.8f) {
            end_dive();
        }
        break;
    case ST_SUMMARY:
        world_step(dt * 0.4f, false);
        if (keys > 0 && s_state_t > 1.5f) {
            dive_start();
        }
        break;
    }
}
static int s_nlife_on(void)
{
    int n = 0;
    for (int i = 0; i < NLIFE; i++) {
        n += s_life[i].on;
    }
    return n;
}
static void ping_draw(void)
{
    if (s_ping_t > 1.3f) {
        return;
    }
    float hx = sx_of(B.x[0]), hy = sy_of(B.y[0]), r = s_ping_t * 320, a = 1 - s_ping_t / 1.3f;
    int n = 80;
    for (int i = 0; i < n; i++) {
        float an = i * 6.2832f / n;
        dot_add(hx + cosf(an) * r, hy + sinf(an) * r * 0.6f, (int)(99 * a), (int)(234 * a), (int)(255 * a));
    }
}
static void render_main(int64_t now);
static void render(int64_t now)
{
#if BOARD_ROUND
    if (s_rnd) {
        rndo_enter(&s_ro, true);
        render_main(now);
        rndo_leave(&s_ro);
        rndo_out(&s_ro);
        return;
    }
#endif
    render_main(now);
}
static void render_main(int64_t now)
{
    (void)now;
    clip_full();
    if (!s_ok) {
        gfx_fill(rgbc(4, 12, 24));
        ui_text_c(&KF_LABEL, s_fw / 2, 90, s_err[0] ? s_err : "...", rgbc(200, 230, 255));
        return;
    }
#ifdef ESP_PLATFORM
    int64_t t0 = esp_timer_get_time();
#endif
#if defined(DC_PROF) && defined(ESP_PLATFORM)
    static int64_t pf[8], pt;
    static int pn;
#define PF(i)                                                                                                          \
    do {                                                                                                               \
        int64_t _t = esp_timer_get_time();                                                                             \
        pf[i] += _t - pt;                                                                                              \
        pt = _t;                                                                                                       \
    } while (0)
    pt = esp_timer_get_time();
#else
#define PF(i)                                                                                                          \
    do {                                                                                                               \
    } while (0)
#endif
    draw_water();
    PF(0);
#if defined(DC_PROF) && defined(ESP_PLATFORM)
    draw_water();
    PF(7);
#endif
    draw_whale();
    draw_far_floor();
    draw_sparks();
    draw_combs();
    PF(1);
    draw_snow(0);
    draw_abyss_floor();
    draw_current();
    draw_plank();
    draw_parts(false);
    PF(2);
    draw_life();
    PF(3);
    if (R.n > 0 || (R.dead > 0 && R.dead < 3)) {
        body_draw(&R, 1, 1);
    }
    if (s_state != ST_SUMMARY) {
        body_draw(&B, 1, s_state == ST_HATCH ? clampf(s_state_t / 1.0f, 0.15f, 1) : 1);
    }
    if (s_state == ST_HATCH && s_state_t < 0.8f) {
        glow(sx_of(B.x[0]), sy_of(B.y[0]), 40 * (1 - s_state_t), 232, 251, 255, 0.9f * (1 - s_state_t / 0.8f));
    }
    PF(4);
    draw_parts(true);
    draw_snow(2);
    draw_surface();
    ping_draw();
    PF(5);
    if (s_fade > 0.01f) {
        box_mix(0, 0, s_fw, s_fh, 0, 4, 10, (int)(s_fade * 256));
    }
    switch (s_state) {
    case ST_TITLE:
        title_draw();
        break;
    case ST_HATCH:
    case ST_PLAY:
    case ST_DESCENT:
    case ST_DEATH:
        hud_draw(1);
        break;
    case ST_CHOICE:
        cards_draw(s_state_t);
        break;
    case ST_SUMMARY:
        summary_draw(s_state_t);
        break;
    }
    PF(6);
#if defined(DC_PROF) && defined(ESP_PLATFORM)
    if (++pn == 90) {
        ESP_LOGI("deep", "prof us: water %d (again %d) far %d mid %d life %d body %d front %d ui %d", (int)(pf[0] / pn),
                 (int)(pf[7] / pn), (int)(pf[1] / pn), (int)(pf[2] / pn), (int)(pf[3] / pn), (int)(pf[4] / pn),
                 (int)(pf[5] / pn), (int)(pf[6] / pn));
        memset(pf, 0, sizeof pf);
        pn = 0;
    }
#endif
#ifdef ESP_PLATFORM
    static int64_t s_sum_us;
    static int s_n;
    s_sum_us += esp_timer_get_time() - t0;
    if (++s_n == 150) {
        ESP_LOGI("deep", "frame %d us (draw), zone %d, life %d, parts %d", (int)(s_sum_us / s_n), s_zone, s_nlife_on(),
                 s_npart);
        s_sum_us = 0;
        s_n = 0;
    }
#endif
}

const app_t app_deep = {
        .name = "DEEP CURRENT",
        .landscape = true,
        .round = APP_ROUND_HI,  // (the round panels: a square frame scaled to the circle)
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .key = true,
};
