// GUMMY SIEGE: a slingshot game (a store app for all boards). Gummy bears against forts of tortilla-chip
// triangles, pretzel beams, crackers and salsa jars that the NACHO KNIGHTS hold. 30 levels in 3 worlds (a picnic
// table at sunset, a kitchen counter at night, a birthday party). The physics is libs/tfphys, the effects are MP3
// files (libs/tfsfx). The levels, the pictures and the effects are in the app file (gs_data.c): no SD card. The frame is
// 640 x 172 on the 3.49 and 320 x 240 on the other boards (the Watcher shows it in its circle).
// MANUAL.md "GUMMY SIEGE".
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "miniz.h"
#include "tfn/settings.h"
#include "tfn/storage.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "tfphys/tfphys.h"
#include "tfsfx/tfsfx.h"
#include "tfdraw/tfdraw.h"
#include "gs_music.h"
#include "gs_data.h"

#define TAG "gummy"

#include "gs_core.inc"
#include "gs_world.inc"
// The looks, the drawing, the camera and the menu icon use the table sine of gfx (fsin): sinf and cosf of the C library
// take about 12 us a call in an app file on the board. The rules of the game (gs_core.inc, gs_world.inc: the shot, the
// powers) keep the exact functions, so the paths of the shots do not change.
#undef sinf
#undef cosf
#define sinf(x) fsin(x)
#define cosf(x) fcos(x)
#include "gs_fx.inc"
#include "gs_draw.inc"
#include "gs_ui.inc"

// ================================================================ the state
enum {
    ST_TITLE,
    ST_MAP,
    ST_PLAY
};
static int s_state;
static bool s_ok;
static int64_t s_last;
static float s_acc;  // the game time that waits for a physics step
static bool s_music = true;
static int s_loop = -1;       // the music track that plays
static bool s_end_done;       // the end of a level is saved
static float s_dn_x, s_dn_y;  // the point where the aim began
static bool s_stretch_snd;
static bool s_aim_wait;  // a finger came down in the intro or the hop: it aims when the bear is in the sling
static bool s_map_down;  // a finger came down on the map (its release can be a tap)
static float s_ms_sum, s_ms_max, s_phys_sum, s_upd_sum, s_per_sum, s_music_sum;
static int64_t s_rend_last;
static int s_ms_n;

static void music_want(int track)
{
    if (!s_music) {
        return;
    }
    if (track != s_loop) {
        if (s_loop < 0) {
            midi_bg_start();
        }
        midi_bg_play_track(track, 0, true);
        s_loop = track;
    }
}
static void music_off(void)
{
    midi_bg_stop(300);
    s_loop = -1;
}

// ================================================================ the smooth motion
// The physics has steps of 1/60 s; a frame of 35-45 fps shows 1 step or 2. So each frame draws each body between
// its place before the last step and its place now (by the time that waits for the next step): the motion is even.
// The game rules use only the real places: the frame writes the drawn places into the bodies and puts the real
// ones back after the drawing.
typedef struct {
    tp_xf_t xf;
    float mass;
    uint8_t ok;
} prev_t;
static prev_t *s_prev;   // [W->body_cap]: the places before the last step
static tp_xf_t *s_real;  // [W->body_cap]: the real places while a frame draws
static void interp_keep(void)
{
    if (!s_prev || !W) {
        return;
    }
    for (int i = 0; i < W->body_hi && i < W->body_cap; i++) {
        const tp_body_t *b = &W->body[i];
        s_prev[i].ok = (b->flags & TP_ALIVE) && b->kind == TP_DYNAMIC;
        s_prev[i].xf = b->xf;
        s_prev[i].mass = b->mass;
    }
    for (int i = W->body_hi; i < W->body_cap; i++) {
        s_prev[i].ok = 0;
    }
}
static void interp_apply(float t)
{
    if (!s_prev || !s_real || !W) {
        return;
    }
    for (int i = 0; i < W->body_hi; i++) {
        tp_body_t *b = &W->body[i];
        s_real[i] = b->xf;
        const prev_t *P = &s_prev[i];
        // (a body that is new in this slot (a piece of a break) has no place before: it stays)
        if (!P->ok || !(b->flags & TP_ALIVE) || P->mass != b->mass) {
            continue;
        }
        float dx = b->xf.p.x - P->xf.p.x, dy = b->xf.p.y - P->xf.p.y;
        if (dx * dx + dy * dy > 1.0f) {
            continue;
        }
        float u = 1.0f - t;
        b->xf.p.x -= dx * u;
        b->xf.p.y -= dy * u;
        float c = P->xf.q.c + (b->xf.q.c - P->xf.q.c) * t, sn = P->xf.q.s + (b->xf.q.s - P->xf.q.s) * t;
        float l = sqrtf(c * c + sn * sn);
        if (l > 0.5f) {
            b->xf.q.c = c / l;
            b->xf.q.s = sn / l;
        }
    }
}
static void interp_restore(void)
{
    if (!s_prev || !s_real || !W) {
        return;
    }
    for (int i = 0; i < W->body_hi; i++) {
        W->body[i].xf = s_real[i];
    }
}
// ================================================================ the camera
// The intro shows the whole set, then the fort (all of it), then the bears, then the whole set
// again. In a flight the camera zooms in on the bear as it leaves the sling, stays near it through the hits, holds on
// the broken fort for a moment, then goes back to the whole set (about 1.3 s after the bear stops).
#define ZOOM_FLY   (GS_RND ? 2.00f * GS_RS : GS_BOX ? 2.00f : 2.40f)  // the zoom on a flying bear
#define WATCH_HOLD 80             // the ticks of the zoom on the broken fort after the bear stops
static float s_zx = 4, s_zy = 1;  // the point of the zoom: the bear in flight, then the place where it stopped
static float s_inx, s_iny;        // the place where the bear first came into the box of the fort
static bool s_in;                 // (in this shot)
static view_t view_fort(void)
{
    return view_fit(s_fort_x0 - 0.6f, s_fort_x1 + 0.6f, s_fort_top + 0.5f, GS_RND ? 2.0f * GS_RS : 2.0f);
}
// the whole set: the sling, the bears that wait, the fort
static view_t view_all(void)
{
    if (GS_RND) {
        view_t v = view_aim();  // (the round layout: the bears that wait are at the left of the sling in it too)
        if (v.z > 1.1f * GS_RS) {
            v.z = 1.1f * GS_RS;
            v.x = SL_X + SW * (0.5f - RND_SLX) / (PPM * v.z);
            v.y = (TABLE_SY - SH * 0.5f) / (PPM * v.z);
        }
        return v;
    }
    view_t v = view_fit(SL_X - SL_ROOM - 0.6f, s_fort_x1 + 1.0f, maxf(s_fort_top + 0.8f, 3.0f), 1.1f);
    if (v.z <= ZMIN + 0.001f) {
        v.x = SL_X - SL_ROOM - 0.6f + (float)SW * 0.5f / (PPM * v.z);
    }
    return v;
}
// the bears that wait at the sling
static view_t view_bears(void)
{
    view_t v;
    v.z = GS_RND ? 1.80f * GS_RS : GS_BOX ? 1.80f : 2.20f;
    v.x = SL_X - (GS_BOX ? 0.6f : 0.9f);
    v.y = 0;
    return v;
}
// a zoom on the point (x, y) of the world: the lift that keeps the point in the upper middle of the frame
static view_t view_zoom(float x, float y, float z, float *lift)
{
    view_t v;
    v.z = clampf(z, ZMIN, ZMAX);
    v.x = x + SW * 0.12f / (PPM * v.z);
    v.y = 0;
    *lift = clampf(SH * 0.42f - s_tsy + maxf(0, y) * PPM * v.z, 0, SH * 4.0f);
    return v;
}
static void camera_step(float dt)
{
    view_t to = view_aim();
    float kf = 2.2f, lift = 0;
    switch (s_phase) {
    case PH_INTRO:
        // the tour: the whole set, the fort, the bears, the whole set
        to = s_tick < 50 ? view_all() : s_tick < 150 ? view_fort() : s_tick < 250 ? view_bears() : view_all();
        kf = 2.6f;
        break;
    case PH_FLY: {
        tp_body_t *b = bear_main();
        if (b) {
            // (a point a little ahead of the bear: a fast bear (the DASH) stays in the frame)
            s_zx = b->xf.p.x + clampf(b->v.x, -30, 30) * 0.10f;
            s_zy = b->xf.p.y;
            if (!s_in && s_zx > s_fort_x0 - 0.4f && s_zx < s_fort_x1 + 0.4f && s_zy < s_fort_top + 0.4f) {
                s_in = true;
                s_inx = s_zx;
                s_iny = s_zy;
            }
        }
        to = view_zoom(s_zx, s_zy, ZOOM_FLY, &lift);
        kf = s_tick < 20 ? 3.5f : 6.0f;
        break;
    }
    case PH_WATCH:
        // the broken fort in the zoom (the place of the last break, else the place where the bear came into the fort,
        // else the bear), then all of it
        if (s_tick < WATCH_HOLD) {
            if (s_brk_n) {
                to = view_zoom(s_brk_x, s_brk_y, ZOOM_FLY * 0.85f, &lift);
            } else if (s_in) {
                to = view_zoom(s_inx, s_iny, ZOOM_FLY * 0.85f, &lift);
            } else {
                to = view_zoom(s_zx, s_zy, ZOOM_FLY * 0.9f, &lift);
            }
            kf = 3.0f;
        } else {
            to = view_all();
            kf = 2.2f;
        }
        break;
    case PH_END:
        to = view_all();
        kf = 1.6f;
        break;
    default:
        break;
    }
    if (s_focus_t > 0 && s_phase >= PH_FLY && s_phase < PH_END) {
        to.x = lerpf(to.x, s_focus_x, 0.5f);
        s_focus_t -= dt;
    }
    float f = 1.0f - expf(-kf * dt);
    s_cam.x += (to.x - s_cam.x) * f;
    s_cam.z = expf(logf(s_cam.z) + (logf(to.z) - logf(s_cam.z)) * f);
    s_lift += (lift - s_lift) * f;
    if (s_lift < 0.5f && lift == 0) {
        s_lift = 0;
    }
    s_cam.y = (s_tsy + s_lift - SH * 0.5f) / (PPM * s_cam.z);
    // the bear in the air stays in the frame (the soft follow can be slower than a fast bear)
    tp_body_t *fb = s_phase == PH_FLY ? bear_main() : NULL;
    if (fb) {
        float k = cam_ppm(), sx = cam_sx(fb->xf.p.x), lo = SW * 0.15f, hi = SW * 0.80f;
        if (sx > hi) {
            s_cam.x += (sx - hi) / k;
        } else if (sx < lo) {
            s_cam.x -= (lo - sx) / k;
        }
        float sy = cam_sy(fb->xf.p.y), top = SH * 0.16f;
        if (sy < top && s_lift < SH * 4.0f) {
            s_lift += top - sy;
            s_cam.y = (s_tsy + s_lift - SH * 0.5f) / (PPM * s_cam.z);
        }
    }
}
// a new shot: the camera forgets the breaks of the last one
static void camera_shot(void)
{
    s_brk_n = 0;
    s_in = false;
}

// ================================================================ the levels
static void level_go(int li)
{
    level_build(li);
    fort_box();
    s_world = L->world;
    s_nfxp = s_npop = s_ndecal = 0;
    s_ghost_t = -1;
    s_bear_sq = s_bear_sqv = 0;
    s_flash = 0;
    s_acc = 0;
    s_end_done = false;
    s_newbest = false;
    s_card_t = 0;
    s_btn_dn = -1;
    s_cam = view_all();
    s_lift = 0;
    s_cam.y = (s_tsy - SH * 0.5f) / (PPM * s_cam.z);
    camera_shot();
    s_aim_wait = false;
    s_freeze = 0;
    if (s_prev) {
        memset(s_prev, 0, sizeof(prev_t) * W->body_cap);  // (no place before: the new level)
    }
    s_state = ST_PLAY;
    hud_masks();
    music_want(GSM_PICNIC + L->world);
}
static void level_ended(void)
{
    s_end_done = true;
    s_newbest = s_won && s_score > s_best[s_li];
    if (s_won) {
        save_level(s_li, s_stars_now, s_score);
    }
    if (s_music) {
        music_off();
    }
    snd(s_won ? FX_WIN : FX_LOSE, 90, 0);
    s_card_t = 0;
    card_masks();
}
static void map_open(int w)
{
    s_state = ST_MAP;
    s_map_down = false;
    s_map_w = clampi(w, 0, 2);
    music_want(GSM_TITLE);
}

// ================================================================ the settings panel
enum {
    HI_SND,
    HI_MUS,
    HI_LANG,
    HI_RESTART,
    HI_MAP
};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const pitem_t ITEMS[] = {
        {PI_PICK, HI_SND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, "gummy_snd"},
        {PI_PICK, HI_MUS, 2, 0, {"MUSIC", "מוזיקה"}, ONOFF_V, "gummy_mus"},
        {PI_PICK, HI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "gummy_lang"},
        {PI_ACT2, HI_RESTART, 0, 0, {"RESTART", "מההתחלה"}, NULL, NULL},
        {PI_ACT, HI_MAP, 0, 0, {"LEVELS", "שלבים"}, NULL, NULL},
};
static bool item_shown(int id)
{
    return (id != HI_RESTART && id != HI_MAP) || s_state == ST_PLAY;
}
static void item_set(int id, int v)
{
    if (!s_ok) {
        return;  // (no game: no music player, no level)
    }
    if (id == HI_SND) {
        s_sound = v == 0;
    } else if (id == HI_MUS) {
        s_music = v == 0;
        if (!s_music) {
            music_off();
        } else {
            music_want(s_state == ST_PLAY && s_phase != PH_END ? GSM_PICNIC + L->world : GSM_TITLE);
        }
    } else if (id == HI_LANG) {
        s_lang = v;
        if (s_state == ST_PLAY) {
            hud_masks();
        }
        if (s_state == ST_PLAY && s_phase == PH_END && s_end_done) {
            card_masks();
        }
    } else if (id == HI_RESTART && s_state == ST_PLAY) {
        level_go(s_li);
    } else if (id == HI_MAP && s_state == ST_PLAY) {
        map_open(L->world);
    }
}
static const pitems_t PANEL = {ITEMS, 5, item_shown, NULL, item_set, NULL, NULL};

// ================================================================ the app
static void enter(int64_t now)
{
    s_last = now;
    s_ok = false;
    s_err[0] = 0;
    frame_geom();
    s_lang = store_get("gummy_lang", 1) ? 1 : 0;
    s_sound = store_get("gummy_snd", 0) == 0;
    s_music = store_get("gummy_mus", 0) == 0;
    sysui_panel_items(&PANEL);
    save_load();
    s_scratch = (uint16_t *)gs_alloc((size_t)SW * SH * 2);
    if (!levels_load() || !art_load()) {
        return;
    }
    if (!world_make()) {
        snprintf(s_err, sizeof s_err, "NO MEMORY (PHYSICS)");
        return;
    }
    s_prev = (prev_t *)gs_alloc(sizeof(prev_t) * W->body_cap);
    s_real = (tp_xf_t *)gs_alloc(sizeof(tp_xf_t) * W->body_cap);
    if (s_prev) {
        memset(s_prev, 0, sizeof(prev_t) * W->body_cap);
    }
    tfsfx_load_mem(GS_SFX_DATA, GS_SFX_LEN, FX_FILE, FX_N < GS_SFX_N ? FX_N : GS_SFX_N, 43);
    midi_bg_init(GS_TRACKS, GS_TRACK_COUNT, GS_MUSIC_VOICES);
    midi_bg_volume(70);
    s_loop = -1;
    s_state = ST_TITLE;
    s_title_t = 0;
    // the world of the first open level that has no 3 stars
    int w = 0;
    for (int i = 0; i < s_nlev; i++) {
        if (level_open(i) && s_stars[i] < 3) {
            w = s_lev[i].world;
            break;
        }
    }
    s_map_w = w;
    L = &s_lev[0];
    music_want(GSM_TITLE);
    s_ms_sum = s_ms_max = 0;
    s_ms_n = 0;
    s_ok = true;
}
static void leave(void)
{
    s_ok = false;
    midi_bg_end();
    tfsfx_free();
    free(s_prev);
    free(s_real);
    s_prev = NULL;
    s_real = NULL;
    world_free();
    art_free();
    mask_free(&s_mk_lab);
    mask_free(&s_mk_name);
    for (int i = 0; i < 5; i++) {
        mask_free(&s_mk_card[i]);
    }
    free(s_scratch);
    s_scratch = NULL;
    free(s_thing);
    s_thing = NULL;
    s_nthing = s_nlev = 0;
    L = NULL;
}
static void play_input(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
    if (s_phase == PH_END) {
        if (s_card_t < 0.6f) {
            return;
        }
        box_t bt[3];
        int n = card_buttons(bt), at = -1;
        for (int i = 0; i < n; i++) {
            if (hit(bt[i], x, y)) {
                at = i;
            }
        }
        if (ev->kind == IN_DOWN) {
            s_btn_dn = at;
        } else if (ev->kind == IN_UP) {
            // (the release in the button where the finger came down: also a slow press, which gives no IN_TAP)
            bool go = at >= 0 && at == s_btn_dn;
            s_btn_dn = -1;
            if (!go) {
                return;
            }
            snd(FX_CLICK, 70, 0);
            int id = card_btn_id(at);
            if (id == T_NEXT) {
                level_go(s_li + 1);
            } else if (id == T_AGAIN) {
                level_go(s_li);
            } else {
                map_open(L->world);
            }
        }
        return;
    }
    switch (ev->kind) {
    case IN_DOWN:
        if (s_phase == PH_INTRO) {
            s_phase = PH_HOP;
            s_tick = 0;
            s_aim_wait = true;
            return;
        }
        if (s_phase == PH_HOP) {
            s_aim_wait = true;
            return;
        }
        if (s_phase == PH_FLY) {
            power_use();
            return;
        }
        if (s_phase == PH_AIM) {
            s_aiming = true;
            s_aim_p = 0;
            s_dn_x = x;
            s_dn_y = y;
            s_stretch_snd = false;
            input_tap_cancel();
        }
        break;
    case IN_MOVE:
        if (s_aim_wait && !s_aiming && s_phase == PH_AIM) {
            // (the finger came down before the bear was in the sling: the aim starts here)
            s_aim_wait = false;
            s_aiming = true;
            s_aim_p = 0;
            s_dn_x = x;
            s_dn_y = y;
            s_stretch_snd = false;
            input_tap_cancel();
        }
        if (s_aiming && s_phase == PH_AIM) {
            float mx = x - s_dn_x, my = y - s_dn_y, d = sqrtf(mx * mx + my * my);
            float pull_px = GS_RND ? SW * 0.25f : GS_BOX ? 96.0f : 104.0f;  // (the round: 11 mm)
            s_aim_p = clampf(d / pull_px, 0, 1);
            if (d > 6 && mx < 0) {
                s_aim_a = clampf(atan2f(my, -mx), -0.55f, 1.45f);  // (a pull to the fort keeps the angle)
            }
            if (!s_stretch_snd && s_aim_p > 0.35f) {
                snd(FX_STRETCH, 55, 0.1f);
                s_stretch_snd = true;
            }
        }
        break;
    case IN_UP:
        s_aim_wait = false;
        if (s_aiming && s_phase == PH_AIM) {
            if (!shot_fire(s_aim_a, s_aim_p)) {
                s_aiming = false;
                s_aim_p = 0;
            } else {
                camera_shot();
            }
        }
        s_aiming = false;
        break;
    default:
        break;
    }
}
static void map_input(const input_event_t *ev)
{
    box_t tile[10], pv, nx;
    map_layout(tile, &pv, &nx);
    if (ev->kind == IN_DOWN) {
        s_map_dn_x = ev->x;
        s_map_dn_y = ev->y;
        s_map_down = true;
        return;
    }
    if (ev->kind != IN_UP || !s_map_down) {
        return;  // (the release of a touch that began on another screen)
    }
    s_map_down = false;
    int dx = ev->x - s_map_dn_x, dy = ev->y - s_map_dn_y;
    if (abs(dx) > 70 && abs(dx) > 2 * abs(dy)) {
        int d = (dx < 0) != rtl() ? 1 : -1;  // (a swipe to the left: the next world)
        if (s_map_w + d >= 0 && s_map_w + d <= 2) {
            s_map_w += d;
            snd(FX_CLICK, 60, 0);
        }
        return;
    }
    // a tap: the release near the point where the finger came down (also a slow press, which gives no IN_TAP)
    if (abs(dx) > 24 || abs(dy) > 24) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (hit(pv, x, y)) {
        if (s_map_w > 0) {
            s_map_w--;
            snd(FX_CLICK, 60, 0);
        }
        return;
    }
    if (hit(nx, x, y)) {
        if (s_map_w < 2) {
            s_map_w++;
            snd(FX_CLICK, 60, 0);
        }
        return;
    }
    int first, n = levels_of_world(s_map_w, &first);
    for (int i = 0; i < 10 && i < n; i++) {
        box_t t = tile[rtl() ? (i / 5) * 5 + 4 - i % 5 : i];
        // (a fat finger: the tile and 4 px round it)
        t.x -= 4;
        t.y -= 4;
        t.w += 8;
        t.h += 8;
        if (hit(t, x, y)) {
            if (level_open(first + i)) {
                snd(FX_CLICK, 70, 0);
                level_go(first + i);
            } else {
                snd(FX_CHIPHIT, 40, 0);
            }
            return;
        }
    }
}
static void input(const input_event_t *ev)
{
    if (!s_ok) {
        return;
    }
    if (s_state == ST_TITLE) {
        if ((ev->kind == IN_TAP || ev->kind == IN_LONG) && s_title_t > 0.4f) {
            snd(FX_CLICK, 70, 0);
            map_open(s_map_w);
        }
        return;
    }
    if (s_state == ST_MAP) {
        map_input(ev);
        return;
    }
    play_input(ev);
}
static void update_game(float dt);
static void update(int64_t now)
{
    float dt = clampf((now - s_last) / 1000.0f, 0, 0.066f);
    s_last = now;
    if (!s_ok) {
        return;
    }
    int64_t u0 = gs_us();
    midi_bg_service();
    s_music_sum += (gs_us() - u0) / 1000.0f;
    update_game(dt);
    s_upd_sum += (gs_us() - u0) / 1000.0f;
}
static void update_game(float dt)
{
    if (s_state == ST_TITLE) {
        s_title_t += dt;
        return;
    }
    if (s_state != ST_PLAY) {
        return;
    }
    // the game time: a slow motion at the big moments; at most 3 steps of 1/60 s in a frame
    float ts = s_slow_t > 0 ? 0.3f : 1.0f;
    s_slow_t = maxf(0, s_slow_t - dt);
    s_acc += dt * ts;
    int steps = 0;
    int64_t t0 = gs_us();
    while (s_acc >= STEP_DT && steps < 3) {
        s_fx_step = 0;
        interp_keep();
        game_tick();
        s_acc -= STEP_DT;
        steps++;
    }
    s_phys_sum += (gs_us() - t0) / 1000.0f;
    if (s_acc > STEP_DT * 2) {
        s_acc = 0;
    }
    fx_step(dt * ts);
    camera_step(dt);
    s_shake *= expf(-8.0f * dt);
    if (s_shake < 0.15f) {
        s_shake = 0;
    }
    if (s_phase == PH_END) {
        if (!s_end_done) {
            level_ended();
        }
        float t0 = s_card_t;
        s_card_t += dt;
        // a sound for each star of the card as it comes
        for (int k = 0; k < s_stars_now; k++) {
            float at = 0.5f + k * 0.35f;
            if (t0 < at && s_card_t >= at) {
                snd(FX_STAR, 70 + k * 10, 0);
            }
        }
    }
}
static void render(int64_t now)
{
    (void)now;
    tfd_screen();
    if (!s_ok) {
        gfx_fill(rgbc(30, 12, 34));
        tfd_text_c(&KF_LABEL, SW / 2, SH / 2, s_err[0] ? s_err : "...", rgbc(255, 200, 180), SW - 16);
        return;
    }
    int64_t a = gs_us();
    if (s_rend_last) {
        s_per_sum += (a - s_rend_last) / 1000.0f;
    }
    s_rend_last = a;
    if (s_state == ST_TITLE) {
        title_draw();
    } else if (s_state == ST_MAP) {
        map_draw();
    } else {
        // the world behind the card: half the light. It does not change much there, so it is drawn one time into the
        // scratch frame, and each frame copies it (the copy is about 9 ms, the world about 20 ms); when the card is
        // complete (its stars and its score), the whole card is frozen too, and only a pressed button is drawn again.
        bool dim = s_phase == PH_END && s_card_t > 0.25f;
        size_t fbn = (size_t)SW * SH * 2;
        s_prof_t = gs_us();
        if (dim && s_freeze && s_scratch) {
            memcpy(g_fb, s_scratch, fbn);
            prof(0);
        } else {
            s_dim = dim;
            interp_apply(clampf(s_acc / STEP_DT, 0, 1));
            world_draw();
            s_dim = false;
            pops_draw();
            hud_draw();
            intro_draw();
            interp_restore();
            if (dim && s_scratch) {
                memcpy(s_scratch, g_fb, fbn);
                s_freeze = 1;
            }
        }
        prof(6);
        if (s_freeze == 2) {
            card_btn_redraw(s_btn_dn);
        } else {
            card_draw();
            if (s_freeze == 1 && s_card_t > 2.2f && s_btn_dn < 0) {
                memcpy(s_scratch, g_fb, fbn);
                s_freeze = 2;
            }
        }
        prof(7);
    }
    float ms = (gs_us() - a) / 1000.0f;
    s_ms_sum += ms;
    if (ms > s_ms_max) {
        s_ms_max = ms;
    }
    if (++s_ms_n == 150) {
        ESP_LOGI(TAG, "draw %.1f ms (max %.1f), physics %.1f ms, bodies %d, parts %d, awake %d", s_ms_sum / s_ms_n,
                 s_ms_max, s_phys_sum / s_ms_n, W ? W->nbody : 0, W ? W->npart : 0, W ? W->stat.awake : 0);
        ESP_LOGI(TAG, "  frame period %.1f ms, update %.1f ms (music %.1f)", s_per_sum / s_ms_n, s_upd_sum / s_ms_n,
                 s_music_sum / s_ms_n);
        s_per_sum = s_upd_sum = s_music_sum = 0;
        ESP_LOGI(TAG, "  background %.1f, props %.1f, things %.1f, bears %.1f, parts %.1f, hud %.1f, card %.1f ms",
                 s_prof[0] / s_ms_n, s_prof[2] / s_ms_n, s_prof[3] / s_ms_n, s_prof[4] / s_ms_n, s_prof[5] / s_ms_n,
                 s_prof[6] / s_ms_n, s_prof[7] / s_ms_n);
        memset(s_prof, 0, sizeof s_prof);
        s_ms_sum = s_ms_max = s_phys_sum = 0;
        s_ms_n = 0;
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
// the icon: the sunset picnic: the band of the slingshot pulls a red gummy bear back, lets it go, the bear turns in its
// arc and hits the fort of nacho chips; crumbs fly and the nacho knight on top rocks (a loop of 2 s)
static void icon_bear(float cx, float cy, float k, float ang, uint16_t c, uint16_t lt)
{
    static const float P[8][3] = {{-3.1f, 6.6f, 2.5f}, {3.1f, 6.6f, 2.5f}, {0, 1.6f, 5.2f},      {-4.9f, 0.4f, 2.2f},
                                  {4.9f, 0.4f, 2.2f},  {0, -5.4f, 4.2f},   {-3.3f, -8.7f, 1.7f}, {3.3f, -8.7f, 1.7f}};
    float ca = fcos(ang), sa = fsin(ang);
    for (int i = 0; i < 8; i++) {
        float px = P[i][0] * k, py = P[i][1] * k;
        tfd_disc(cx + ca * px - sa * py + 0.8f * k, cy + sa * px + ca * py + 0.8f * k, (P[i][2] + 0.4f) * k,
                 ic_g(96, 0, 32), 256);
    }
    for (int i = 0; i < 8; i++) {
        float px = P[i][0] * k, py = P[i][1] * k;
        tfd_disc(cx + ca * px - sa * py, cy + sa * px + ca * py, P[i][2] * k, c, 256);
    }
    tfd_disc(cx + (-2 * ca - 1 * sa) * k, cy + (-2 * sa + 1 * ca) * k, 2.6f * k, lt, 150);
    tfd_disc(cx + (-1.6f * ca + 7 * sa) * k, cy + (-1.6f * sa - 7 * ca) * k, 1.3f * k, ic_g(224, 240, 240), 230);
    tfd_disc(cx + (-1.4f * ca + 5.6f * sa) * k, cy + (-1.4f * sa - 5.6f * ca) * k, 0.7f * k, ic_g(32, 0, 16), 256);
    tfd_disc(cx + (1.4f * ca + 5.6f * sa) * k, cy + (1.4f * sa - 5.6f * ca) * k, 0.7f * k, ic_g(32, 0, 16), 256);
}
static void icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f;
    float hz = 66;
    ic_dband(x, y, s, 0, (int)(hz * u), ic_g(96, 48, 128), ic_g(224, 160, 96));
    tfd_disc(x + 66 * u, y + (hz - 8) * u, 14 * u, ic_g(224, 192, 128), 90);
    tfd_disc(x + 66 * u, y + (hz - 8) * u, 9 * u, ic_g(224, 224, 160), 256);
    float hill[] = {x + 0.0f,          y + hz * u + 1, x + 0.0f,         y + (hz - 8) * u, x + 38 * u,
                    y + (hz - 13) * u, x + 100 * u,    y + (hz - 3) * u, x + 100 * u,      y + hz * u + 1};
    ic_inner(x, y, s);
    tfd_poly(hill, 5, ic_g(96, 48, 96), 256);
    tfd_clip(x, y, x + s, y + s);
    // the gingham cloth (rows of red and white checks)
    for (int r = 0; r < 4; r++) {
        int j0 = (int)((hz + r * 8.5f) * u), j1 = (int)((hz + (r + 1) * 8.5f) * u);
        for (int j = j0; j < j1 && j < s; j++) {
            int r0 = s / 8 + 2, in = 0;
            if (j >= s - r0) {
                float dy = (j + 0.5f) - (s - r0);
                in = tfd_ce(r0 - sqrtf(tfd_max(0, r0 * r0 - dy * dy)));
            }
            for (int i = 0; i < 8; i++) {
                int a = (i * s) / 8, b = ((i + 1) * s) / 8, odd = (i + r) & 1;
                if (a < in) {
                    a = in;
                }
                if (b > s - in) {
                    b = s - in;
                }
                uint16_t c = odd ? ic_g(192, 32, 48) : ((r & 1) ? ic_g(224, 96, 96) : ic_g(224, 224, 208));
                tfd_hspan(x + a, x + b, y + j, c, 256);
            }
        }
    }
    // the fort: two chips, a pretzel beam, a chip on top, the nacho knight
    float fx = x + 76 * u, base = y + 82 * u;
    float hit = ph > 0.62f ? (ph - 0.62f) / 0.38f : 0;
    float rock = hit > 0 ? fsin(hit * 18) * (1 - hit) * 0.35f : 0;
    for (int k = 0; k < 2; k++) {
        float cx = fx + (k ? 8 : -8) * u;
        float tri[] = {cx - 8 * u, base, cx + 8 * u, base, cx, base - 15 * u};
        tfd_poly(tri, 3, ic_g(160, 96, 32), 256);
        float tr2[] = {cx - 6 * u, base - 1.4f * u, cx + 6 * u, base - 1.4f * u, cx, base - 12.6f * u};
        tfd_poly(tr2, 3, ic_g(224, 176, 64), 256);
        tfd_disc(cx - 1.5f * u, base - 5 * u, 1.4f * u, ic_g(224, 128, 32), 256);
    }
    tfd_line(fx - 15 * u, base - 16 * u, fx + 15 * u, base - 16 * u, 3.4f * u, ic_g(128, 64, 32), 256);
    tfd_line(fx - 15 * u, base - 16.8f * u, fx + 15 * u, base - 16.8f * u, 1 * u, ic_g(192, 128, 64), 256);
    float kx = fx, ky = base - 18 * u, ca = fcos(rock), sa = fsin(rock);
    float kn[] = {kx - 8 * u * ca, ky - 8 * u * sa,  kx + 8 * u * ca,
                  ky + 8 * u * sa, kx + 15 * u * sa, ky - 15 * u * ca};
    tfd_poly(kn, 3, ic_g(224, 160, 48), 256);
    tfd_disc(kx + 10 * u * sa, ky - 10 * u * ca, 5.6f * u, ic_g(224, 192, 32), 256);
    tfd_disc(kx + 4 * u * sa - 2.6f * u * ca, ky - 4 * u * ca - 2.6f * u * sa, 1.6f * u, ic_g(224, 240, 240), 256);
    tfd_disc(kx + 4 * u * sa + 2.6f * u * ca, ky - 4 * u * ca + 2.6f * u * sa, 1.6f * u, ic_g(224, 240, 240), 256);
    tfd_disc(kx + 4 * u * sa - 2.2f * u * ca, ky - 4 * u * ca - 2.2f * u * sa, 0.8f * u, ic_g(32, 16, 16), 256);
    tfd_disc(kx + 4 * u * sa + 3 * u * ca, ky - 4 * u * ca + 3 * u * sa, 0.8f * u, ic_g(32, 16, 16), 256);
    // the slingshot (the back prong, the bear, the band, the front prong)
    float sx = x + 20 * u, sy = y + 58 * u;
    float pull = ph < 0.2f ? ph / 0.2f : 0, ft = ph < 0.2f ? -1 : (ph - 0.2f) / 0.42f;
    float bx = sx - pull * 8 * u, by = sy + pull * 6 * u, ang = 0;
    if (ft >= 0 && ft <= 1) {
        bx = sx + ft * 52 * u;
        by = sy - fsin(ft * 3.14159f) * 30 * u + ft * 6 * u;
        ang = ft * 9;
    }
    tfd_line(sx - 7 * u, sy, sx - 2 * u, sy + 14 * u, 3.2f * u, ic_g(96, 48, 16), 256);
    tfd_line(sx - 2 * u, sy + 14 * u, sx - 2 * u, y + 94 * u, 4 * u, ic_g(128, 64, 32), 256);
    if (ft < 0) {
        tfd_line(sx - 7 * u, sy, bx - 4 * u, by + 2 * u, 1.8f * u, ic_g(160, 16, 32), 256);
    }
    if (ft <= 1) {
        icon_bear(bx, by, 1.7f * u, ang, ic_g(224, 32, 64), ic_g(224, 160, 176));
    }
    if (ft < 0) {
        tfd_line(sx + 7 * u, sy, bx - 4 * u, by + 2 * u, 1.8f * u, ic_g(192, 32, 48), 256);
    }
    tfd_line(sx + 7 * u, sy, sx + 2 * u, sy + 14 * u, 3.2f * u, ic_g(160, 96, 48), 256);
    tfd_line(sx + 2 * u, sy + 14 * u, sx + 2 * u, y + 94 * u, 4 * u, ic_g(160, 96, 48), 256);
    // the hit: a ring and crumbs
    if (hit > 0) {
        float hx = fx - 6 * u, hy = base - 20 * u;
        tfd_ring(hx, hy, (4 + hit * 16) * u, 2 * u, ic_g(224, 240, 224), (int)(230 * (1 - hit)));
        for (int k = 0; k < 6; k++) {
            float a = k * 1.05f + 0.4f, d = (4 + hit * 16) * u;
            tfd_disc(hx + fcos(a) * d, hy + fsin(a) * d - hit * 5 * u + hit * hit * 12 * u, 1.6f * u,
                     ic_g(224, 176, 64), 256);
        }
    }
    ic_end();
}

const app_t app_gummy = {
        .name = "GUMMY SIEGE",
        .landscape = true,
        .round = APP_ROUND_HI,  // the round frame of the panel (466 x 466 on the 1.75C, 412 on the Watcher)
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
};
