// ORBITAL (GAMES): a rescue tug in a low orbit round a small planet. Real orbits (one planet, true
// gravity, a thin air that brakes and burns): catch drifting ships with a harpoon tether and tow them into safe orbits,
// lift falling wrecks out of the air, and bring the crew capsules down near the recovery ship. 17 missions; each one
// teaches one idea of the orbits (a burn forward raises the far side; to catch up, slow down; the air brakes ...).
//
// - THE CONTROLS: TURN the nose = a tilt (the zero is the start position) or the turn keys (TOUCH; the Watcher wheel);
//   BURN = hold the BURN pad (the right end); TETHER = tap the TETHER key (or the BOOT key, or the wheel button): the
//   harpoon flies 260 m; while it tows, a hold of 1 s on the same key lets go.
// - THE HELP for a small touch screen: the nose snaps to a marker near it (forward, back, to the target, against the
//   drift); the GUIDE (the default) shows the right way for the nose (a green mark), lets the engine burn only at the
//   right time (the auto stop and start: hold BURN, the GUIDE does the timing), shows the time of the next burn and
//   slows the clock for each burn; the AUTOPILOT flies the mission (1 star). The clock of the mission runs fast when
//   nothing happens (up to 240 times) and slow at each big moment.
// - THE FILES: orb_core.inc (math, colours, noise), orb_phys.inc (the physics), orb_mis.inc (the missions),
//   orb_pilot.inc (the autopilot and its plan of a meeting), orb_game.inc (the controls, the clock, the goals, the
//   effects), orb_view.inc (the camera, the planet, the ships), orb_hud.inc (the HUD and the screens), orb_music.c (the
//   music, made by tools/music), orb_sfx.c (the CC0 effects, made by tools/mk_sfx.py).
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "midi_bg/midi_bg.h"
#include "orb_music.h"
#include "tfsfx/tfsfx.h"
#include "orb_sfx.h"
#include "tfdraw/tfdraw.h"

static const char *ORB_TAG = "orbital";
#define ORB_KEY "orbital_"  // (the manifest: saves=orbital_)

#include "orb_core.inc"
#include "orb_phys.inc"
#include "orb_mis.inc"
#include "orb_pilot.inc"
#include "orb_game.inc"
#include "orb_view.inc"
#include "orb_hud.inc"

// ================================================================ the saves
static void stars_save(void)
{
    uint32_t a = 0, b = 0;
    for (int i = 0; i < NMIS; i++) {
        if (i < 16) {
            a |= (uint32_t)(s_stars[i] & 3) << (2 * i);
        } else {
            b |= (uint32_t)(s_stars[i] & 3) << (2 * (i - 16));
        }
    }
    store_set(ORB_KEY "st1", a);
    store_set(ORB_KEY "st2", b);
    store_set(ORB_KEY "open", (uint32_t)s_unlock);
}
static void stars_load(void)
{
    uint32_t a = store_get(ORB_KEY "st1", 0), b = store_get(ORB_KEY "st2", 0);
    for (int i = 0; i < NMIS; i++) {
        s_stars[i] = (uint8_t)(i < 16 ? (a >> (2 * i)) & 3 : (b >> (2 * (i - 16))) & 3);
    }
    s_unlock = clampi((int)store_get(ORB_KEY "open", 0), 0, NMIS - 1);
}

// ================================================================ the title and its demo
static const int DEMO_MIS[] = {0, 7, 1, 5, 14};
static float s_demo_t;
static void demo_next(void)
{
    s_demo_mis = (s_demo_mis + 1) % (int)(sizeof DEMO_MIS / sizeof DEMO_MIS[0]);
    mission_start(DEMO_MIS[s_demo_mis], true);
    s_state = ST_TITLE;
    s_demo_t = 0;
}
static void title_go(void)
{
    s_state = ST_TITLE;
    s_state_t = 0;
    s_sel = clampi(s_sel, 0, s_unlock);
    s_mus_loop = -1;
    s_jingle_t = 0;
    demo_next();
}

// ================================================================ the panel items
enum {
    MI_LANG,
    MI_CTL,
    MI_HELP,
    MI_MUS,
    MI_TITLE,
    MI_AGAIN
};
static const char *const V_LANG[] = {"ENGLISH", "ENGLISH", "עברית", "עברית"};
static const char *const V_CTL[] = {"TILT", "הטיה", "TOUCH", "מגע"};
static const char *const V_HELP[] = {"GUIDE", "מדריך", "OFF", "כבוי", "AUTOPILOT", "טייס אוטו׳"};
static const char *const V_ONOFF[] = {"ON", "פועל", "OFF", "כבוי"};
static pitem_t ITEMS[] = {
        {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, V_LANG, ORB_KEY "lang"},
        {PI_PICK, MI_CTL, 2, 0, {"CONTROLS", "שליטה"}, V_CTL, ORB_KEY "ctl"},
        {PI_PICK, MI_HELP, 3, 0, {"HELP", "עזרה"}, V_HELP, ORB_KEY "help"},
        {PI_PICK, MI_MUS, 2, 0, {"MUSIC", "מוזיקה"}, V_ONOFF, ORB_KEY "mus"},
        {PI_ACT, MI_TITLE, 0, 0, {"THE TITLE", "למסך הפתיחה"}, NULL, NULL},
        {PI_ACT2, MI_AGAIN, 0, 0, {"RESTART", "מההתחלה"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == MI_TITLE || id == MI_AGAIN) {
        return s_state != ST_TITLE;
    }
    return true;
}
static void item_set(int id, int v)
{
    if (id == MI_LANG) {
        s_lang = v & 1;
    } else if (id == MI_CTL) {
        s_ctl_mode = v == 1 ? CTL_TOUCH : CTL_TILT;
        tilt_zero();
    } else if (id == MI_HELP) {
        s_help = clampi(v, 0, 2);
        if (s_state != ST_TITLE) {
            s_auto = s_help == HELP_AUTO;
        }
    } else if (id == MI_MUS) {
        s_music = !(v & 1);
        s_mus_loop = -1;
        s_jingle_t = 0;
        if (!s_music) {
            midi_bg_stop(300);
        } else {
            midi_bg_start();
        }
    } else if (id == MI_TITLE) {
        title_go();
    } else if (id == MI_AGAIN) {
        mission_start(s_mis, false);
    }
}
static void item_opened(void)
{
    drones_off();
}
static void item_resume(void)
{
    tilt_zero();
}
static const pitems_t PANEL = {ITEMS, 6, item_shown, NULL, item_set, item_opened, item_resume};

// ================================================================ the app
static bool s_loaded;
static int s_load_frames;
static int64_t s_last;
static int64_t s_fps_t, s_draw_us;
static int s_fps_n;
static void orb_enter(int64_t now)
{
    s_lang = store_get(ORB_KEY "lang", 0) ? 1 : 0;
    // (the default controls: TILT where the board has a motion sensor, else the turn keys)
    ITEMS[1].def = imu_ok() ? 0 : 1;
    s_ctl_mode = (int)store_get(ORB_KEY "ctl", ITEMS[1].def) == 1 ? CTL_TOUCH : CTL_TILT;
    s_help = clampi((int)store_get(ORB_KEY "help", 0), 0, 2);
    s_music = !store_get(ORB_KEY "mus", 0);
    stars_load();
    s_sel = s_unlock;
    s_loaded = false;
    s_load_frames = 0;
    s_last = now;
    s_fps_t = 0;
    s_fps_n = 0;
    s_draw_us = 0;
    s_cam_t = 0;
    s_demo_mis = -1;
    midi_bg_init(OR_TRACKS, OR_TRACK_COUNT, OR_MUSIC_VOICES);
    midi_bg_volume(55);
    if (s_music) {
        midi_bg_start();
    }
    s_mus_loop = -1;
    s_jingle_t = 0;
    sysui_panel_items(&PANEL);
}
static void orb_leave(void)
{
    drones_off();
    midi_bg_end();
    tfsfx_free();
    planet_free();
    tc_free();
    kc_free();
    orb_free(s_sunspr);
    s_sunspr = NULL;
    s_loaded = false;
}
static void orb_input_ev(const input_event_t *ev)
{
    if (!s_loaded) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (s_state == ST_TITLE) {
        if (ev->kind != IN_TAP) {
            return;
        }
        if (in_box(box_prev(), x, y)) {
            if (s_sel > 0) {
                s_sel--;
                fx(FX_CLICK, 50, 1);
            }
            return;
        }
        if (in_box(box_next(), x, y)) {
            if (s_sel < s_unlock) {
                s_sel++;
                fx(FX_CLICK, 50, 1);
            } else {
                sfx_play(SFX_BACK);
            }
            return;
        }
        if (in_box(box_fly(), x, y) || in_box(box_card(), x, y)) {
            s_demo = false;
            mission_start(s_sel, false);
        }
        return;
    }
    if (s_state == ST_BRIEF) {
        if (ev->kind == IN_TAP && s_state_t > 0.4f) {
            s_state = ST_PLAY;
            s_state_t = 0;
        }
        return;
    }
    if (s_state == ST_PLAY) {
        // a new finger on the TETHER key fires it (the hold to let go is read from the points each frame)
        if (ev->kind == IN_DOWN && !s_auto && in_box(box_tether(), x, y)) {
            s_fire_req++;
        }
        return;
    }
    if ((s_state == ST_DONE || s_state == ST_FAIL) && ev->kind == IN_TAP && s_state_t > 1.6f) {
        for (int k = 0; k < res_keys(); k++) {
            if (!in_box(box_res(k), x, y)) {
                continue;
            }
            int a = res_action(k);
            fx(FX_CLICK, 50, 1);
            if (a == 0) {
                s_sel = s_mis + 1;
                mission_start(s_mis + 1, false);
            } else if (a == 1) {
                mission_start(s_mis, false);
            } else {
                s_sel = clampi(s_mis + (s_state == ST_DONE ? 1 : 0), 0, s_unlock);
                title_go();
            }
            return;
        }
    }
}
// a slow tap (a press of 350 - 900 ms) on the title, the brief and the cards acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void orb_input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    orb_input_ev(ev);
    if (slow && (s_state != ST_PLAY)) {
        orb_input_ev(&tap);
    }
}
static void orb_update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    if (!s_loaded) {
        // (the first frame shows the loading screen; the next one makes the planet and the sky)
        if (s_load_frames++ == 0) {
            return;
        }
        if (s_load_frames > 2) {
            return;  // (one try: no memory = NO MEMORY on the screen)
        }
        int64_t t0 = esp_timer_get_time();
        planet_make();
        sky_make();
        sun_make();
        tfsfx_load_mem(ORB_SFX_DATA, ORB_SFX_LEN, FX_NAMES, FX_N < ORB_SFX_N ? FX_N : ORB_SFX_N, 44);
        s_loaded = s_tex != NULL;
        ESP_LOGI(ORB_TAG, "the planet is made in %d ms", (int)((esp_timer_get_time() - t0) / 1000));
        (void)t0;
        if (s_loaded) {
            title_go();
        }
        return;
    }
    if (dt <= 0) {
        return;
    }
    int64_t tu = esp_timer_get_time();
    s_state_t += dt;
    music_step(dt);
    if (s_state == ST_TITLE) {
        s_demo_t += dt;
        play_step(dt);
        // (the demo flies 70 s of a mission, or until its tow is done; then the next one)
        if (s_demo_t > 70 || !B[0].on || (s_tgt > 0 && !B[s_tgt].on) || B[0].heat > 0.9f) {
            demo_next();
        }
    } else {
        if (s_state == ST_BRIEF && s_state_t > 4.2f) {
            s_state = ST_PLAY;
            s_state_t = 0;
        }
        play_step(dt);
    }
    cam_step(dt);
    // the landing marks: the end of the fall of the capsule (a few times a second)
    static float land_t;
    land_t += dt;
    if (land_t > 0.25f) {
        land_t = 0;
        s_land_ok_show = 0;
        if (MIS[s_mis].goal_kind == G_LAND && T.state == TH_ON) {
            land_predict(&s_land_ia_show, &s_land_it_show, &s_land_ok_show);
        }
    }
    s_prof[5] += esp_timer_get_time() - tu;
}
static void draw_loading(void)
{
    gfx_fill(rgb(2, 3, 10));
    ui_text_c(&KF_TITLE, SW / 2, SH / 2 - 4, "ORBITAL", C_AMB);
    ui_text_c(&KF_LABEL, SW / 2, SH / 2 + 18, TR("THE PLANET TURNS...", "הכוכב מסתובב..."), rgb(255, 228, 196));
}
static void orb_render(int64_t now)
{
    if (!s_loaded) {
        if (s_load_frames > 1 && !s_tex) {
            gfx_fill(rgb(0, 0, 0));
            ui_text_c(&KF_NAME, SW / 2, SH / 2, "NO MEMORY", rgb(255, 200, 160));
            return;
        }
        draw_loading();
        return;
    }
    int64_t t0 = esp_timer_get_time();
    s_tc_frame++;
    tfd_screen();
    tfd_noclip();
    // (the result card covers the frame: a dark fill in place of the world under it)
    bool card = (s_state == ST_DONE || s_state == ST_FAIL) && s_state_t >= 1.0f;
    holes_make(card);
    if (card) {
        gfx_fill(rgb(6, 9, 20));
    } else {
        draw_world();
    }
    int64_t th = esp_timer_get_time();
    if (s_state == ST_TITLE) {
        draw_title();
    } else {
        if (!card) {
            draw_hud();
        }
        if (s_state == ST_BRIEF) {
            draw_brief();
        }
        if (s_state == ST_DONE || s_state == ST_FAIL) {
            draw_result();
        }
    }
    s_prof[4] += esp_timer_get_time() - th;
    // the frames a second and the time of the drawing (the log, each 5 s)
    s_draw_us += esp_timer_get_time() - t0;
    s_fps_n++;
    if (!s_fps_t) {
        s_fps_t = now;
    }
    if (now - s_fps_t >= 5000) {
        int fps10 = (int)(s_fps_n * 10000 / (now - s_fps_t)), draw = (int)(s_draw_us / 1000 / s_fps_n);
        ESP_LOGI(ORB_TAG, "%d.%d fps, the drawing %d ms a frame, planet %d px, view %d, x%d, %d steps", fps10 / 10,
                 fps10 % 10, draw, s_planet_px, s_view, (int)s_rate, s_steps_last);
        int n = s_fps_n;
        ESP_LOGI(ORB_TAG,
                 "parts ms: sky %d (fill %d), planet %d, far %d, ships %d, hud %d (marks %d, keys %d), update %d",
                 (int)(s_prof[0] / 1000 / n), (int)(s_prof[11] / 1000 / n), (int)(s_prof[1] / 1000 / n),
                 (int)(s_prof[2] / 1000 / n), (int)(s_prof[3] / 1000 / n), (int)(s_prof[4] / 1000 / n),
                 (int)(s_prof[6] / 1000 / n), (int)(s_prof[7] / 1000 / n), (int)(s_prof[5] / 1000 / n));
        (void)n;
        for (int k = 0; k < 12; k++) {
            s_prof[k] = 0;
        }
        (void)fps10;
        (void)draw;
        s_fps_t = now;
        s_fps_n = 0;
        s_draw_us = 0;
    }
}
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
// the icon: the rescue tug flies one orbit round the small planet in 2 s (behind it, then in front of it) and tows a
// wreck on its tether; the flame of the tug flickers, the beacon of the wreck blinks, the stars shine
static void orb_icon_obj(float cx, float cy, float u, float k, float ang, int wreck, float an)
{
    static const float HULL[] = {-7, -3.5f, 4, -3.5f, 7, -1.5f, 7, 1.5f, 4, 3.5f, -7, 3.5f};
    static const float BAND[] = {-1.5f, -3.7f, 1.5f, -3.7f, 1.5f, 3.7f, -1.5f, 3.7f};
    static const float FIRE[] = {-6.5f, -2.2f, -6.5f, 2.2f, -13, 0};
    static const float WREK[] = {-6, -2.6f, 5, -2.6f, 6.5f, 0, 5, 2.6f, -6, 2.6f};
    static const float BOX[] = {-4, -2.6f, 0, -2.6f, 0, -5.5f, -4, -5.5f};
    float cs = fcos(ang), sn = fsin(ang), q = k * u * 1.35f;
    if (wreck) {
        ic_shape(WREK, 5, cx + 0.8f * u, cy + 1.2f * u, q * 1.15f, ang, ic_g(10, 14, 30), 200);
        ic_shape(WREK, 5, cx, cy, q, ang, ic_g(170, 178, 192), 256);
        ic_shape(BOX, 4, cx, cy, q, ang, ic_g(63, 193, 201), 256);
        float bx = cx + (3 * cs + 2.6f * sn) * q, by = cy + (3 * sn - 2.6f * cs) * q;
        if (fsin(an * 4) > 0) {
            tfd_disc(bx, by, 2.6f * q, ic_g(255, 60, 40), 110);
            tfd_disc(bx, by, 1.2f * q, ic_g(255, 210, 190), 256);
        } else {
            tfd_disc(bx, by, 1.0f * q, ic_g(150, 30, 26), 256);
        }
        return;
    }
    float fl = 1 + 0.18f * fsin(an * 16);
    float f2[6];
    for (int i = 0; i < 6; i++) {
        f2[i] = FIRE[i] * (i == 4 ? fl : 1);
    }
    ic_shape(f2, 3, cx, cy, q, ang, ic_g(90, 170, 255), 210);
    ic_shape(f2, 3, cx - cs * 1.5f * q, cy - sn * 1.5f * q, q * 0.6f, ang, ic_g(230, 245, 255), 256);
    ic_shape(HULL, 6, cx + 0.8f * u, cy + 1.2f * u, q * 1.15f, ang, ic_g(10, 14, 30), 200);
    ic_shape(HULL, 6, cx, cy, q, ang, ic_g(232, 236, 244), 256);
    ic_shape(BAND, 4, cx, cy, q, ang, ic_g(255, 138, 61), 256);
    tfd_disc(cx + (4.2f * cs + 0.6f * sn) * q, cy + (4.2f * sn - 0.6f * cs) * q, 1.4f * q, ic_g(70, 200, 255), 256);
}
static void orb_icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f, an = ph * 6.2831853f;
    ic_dband(x, y, s, 0, s, ic_g(32, 32, 112), ic_g(0, 0, 16));
    static const uint8_t ST[][2] = {{14, 18}, {30, 10}, {72, 14}, {88, 32}, {10, 50},
                                    {88, 68}, {20, 86}, {64, 90}, {50, 8},  {40, 94}};
    for (int k = 0; k < 10; k++) {
        float tw = 0.5f + 0.5f * fsin(an * 2 + k * 1.7f);
        tfd_disc(x + ST[k][0] * u, y + ST[k][1] * u, (0.6f + 0.7f * tw) * u + 0.3f, ic_g(224, 224, 224), 256);
    }
    float pcx = x + 50 * u, pcy = y + 52 * u, pr = 21 * u, tl = -0.32f, ct = fcos(tl), st = fsin(tl);
    float rx = 41 * u, ry = 13 * u;
    // the orbit: a dotted ellipse (the far half before the planet, the near half after it)
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 36; i++) {
            float th = i * 6.2831853f / 36 + an * 0.25f, sy = fsin(th);
            if ((sy < 0) != (pass == 0)) {
                continue;
            }
            float ex = rx * fcos(th), ey = ry * sy;
            tfd_disc(pcx + ex * ct - ey * st, pcy + ex * st + ey * ct, 0.75f * u + 0.2f,
                     pass ? ic_g(96, 192, 224) : ic_g(64, 96, 160), 256);
        }
        // the tug and the wreck in this half
        for (int o = 1; o >= 0; o--) {
            float th = an - (o ? 0.62f : 0), sy = fsin(th);
            if ((sy < 0) != (pass == 0)) {
                continue;
            }
            float ex = rx * fcos(th), ey = ry * sy, k = 1 + 0.16f * sy;
            float ox = pcx + ex * ct - ey * st, oy = pcy + ex * st + ey * ct;
            if (o) {
                // the tether from the tug to the wreck
                float t2 = an, ex2 = rx * fcos(t2), ey2 = ry * fsin(t2);
                float tx2 = pcx + ex2 * ct - ey2 * st, ty2 = pcy + ex2 * st + ey2 * ct;
                tfd_line(ox, oy, tx2, ty2, 0.9f * u + 0.3f, ic_g(120, 255, 230), 200);
            }
            orb_icon_obj(ox, oy, u, k, th + 1.5707963f + tl * 0.6f, o, an);
        }
        if (pass == 0) {
            // the planet: the air, the night side (the dark rim), the sea, the land, the clouds, the shine
            tfd_disc(pcx, pcy, pr + 3 * u, ic_g(60, 130, 255), 70);
            tfd_disc(pcx, pcy, pr + 1.4f * u, ic_g(140, 210, 255), 150);
            tfd_disc(pcx, pcy, pr, ic_g(14, 40, 100), 256);
            tfd_disc(pcx - 2.2f * u, pcy - 2.6f * u, pr * 0.86f, ic_g(34, 110, 196), 256);
            ic_oval(pcx - 7 * u, pcy - 4 * u, 7 * u, 4.5f * u, ic_g(96, 176, 86), 256);
            ic_oval(pcx + 5 * u, pcy + 5 * u, 6 * u, 3.4f * u, ic_g(122, 168, 78), 256);
            ic_oval(pcx + 3 * u, pcy - 11 * u, 4.5f * u, 2.2f * u, ic_g(214, 182, 118), 256);
            ic_oval(pcx - 3 * u, pcy + 9 * u, 8 * u, 1.6f * u, ic_g(240, 246, 255), 200);
            ic_oval(pcx + 7 * u, pcy - 4 * u, 5 * u, 1.3f * u, ic_g(240, 246, 255), 170);
            tfd_disc(pcx - 8 * u, pcy - 9 * u, 4.5f * u, ic_g(255, 255, 255), 90);
        }
    }
    ic_end();
}

static bool orb_awake(void)
{
    return s_state == ST_PLAY || s_state == ST_BRIEF;
}

const app_t app_orbital = {
        .name = "ORBITAL",
        .landscape = true,
        .key = true,
        .wheel = true,
        .round = APP_ROUND_HI,
        .enter = orb_enter,
        .leave = orb_leave,
        .input = orb_input,
        .update = orb_update,
        .render = orb_render,
        .draw_icon = orb_icon,
        .awake = orb_awake,
};
