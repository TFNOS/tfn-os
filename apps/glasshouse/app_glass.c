// GLASSHOUSE (GAMES, a store app for all boards): a destruction puzzle in a diorama of glass, iron and
// timber. Each level is a small structure on a walnut stand: glass cells in iron frames, timber, ropes, a water tank,
// and one fragile thing (an orchid in a bell jar, an egg, a china vase). A few marked moves (CUT a rope, PULL a pin,
// CRACK a pane, PUSH a load, OPEN a valve, START a winch) start a chain of events in the real physics (libs/tfphys):
// bring down the marked part of the structure, but the fragile thing must stay whole. The director zooms to the action, slows the
// big moment and shows an instant replay. English and Hebrew. MANUAL.md "GLASSHOUSE".
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/storage.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfphys/tfphys.h"
#include "tfsfx/tfsfx.h"
#include "tfdraw/tfdraw.h"
#include "gl_music.h"
#include "gl_sfx.h"

#define TAG "glass"

#include "gl_canvas.inc"
#include "gl_level.inc"
#include "gl_world.inc"
// The pictures, the camera and the icon use the table sine of gfx (fsin): sinf and cosf of the C library take about
// 12 us a call in an app file on the board. The physics (gl_world.inc) keeps the exact functions.
#undef sinf
#undef cosf
#define sinf(x) fsin(x)
#define cosf(x) fcos(x)
#include "gl_view.inc"
#include "gl_art.inc"
#include "gl_scene.inc"
#include "gl_ui.inc"
#include "gl_levels.inc"

#define STEP_DT  DT_STEP
#define TITLE_LI 6  // the level of the picture of the title (THE ORCHID HOUSE)
#define NLEV     GL_NLEVELS

// ================================================================ the state
enum {
    ST_LOAD,
    ST_TITLE,
    ST_SHELF,
    ST_PLAY
};
enum {
    PH_INTRO,
    PH_PLAY,
    PH_END,
    PH_REPLAY
};
static int s_state, s_phase;
static int s_next_state;  // ST_LOAD: the state after the build
static bool s_ok;
static char s_err[128];
static int64_t s_last;
static float s_t;      // the time in the state or the phase (s)
static float s_acc;    // the game time that waits for a physics step
static int s_li = -1;  // the level now
static int s_load_li;  // the level to build
static bool s_built;   // the world of s_li is built
static uint8_t s_stars[NLEV];
static bool s_sound = true, s_music = true;
static int s_sel = -1;    // the selected handle
static int s_press = -1;  // the button under the finger (the title, the shelf, the card)
static bool s_down, s_drag;
static float s_dn_x, s_dn_y, s_dn_cx, s_dn_cy;
static float s_panx, s_pany;  // the drag of the player (world px)
static float s_tilt_x, s_tilt_y;
// the end of a level
enum {
    RS_WON,
    RS_LOST,
    RS_STILL
};
static int s_result, s_end_stars;
static float s_end_t;
static bool s_has_replay;
// the director
typedef struct {
    float x, y, z;
} view_t;
static view_t s_aimv, s_to;
static float s_kf = 2.5f;
static float s_zmin = 1, s_zmax = 3;
static float s_shake;
static float s_slow;                      // the real seconds of slow motion left
static float s_fx_x, s_fx_y, s_fx_t = 9;  // the last big event (the camera looks at it)
static float s_obj_risk_t = -9;
static bool s_first_shatter, s_goal_seen;
// the replay
static uint32_t s_rp_step[MAXH];
static int8_t s_rp_h[MAXH];
static int s_rp_n, s_rp_i;
static uint32_t s_rp_first, s_rp_big;
static float s_rp_bx, s_rp_by;
// the music
static int s_track = -1;
static float s_jingle_left;
static int s_after_jingle = -1;
// the speed log
static float s_ms_sum, s_ms_max, s_phys_sum;
static int s_ms_n;
static int64_t s_fps_t0;
static int64_t gl_us(void)
{
    return esp_timer_get_time();
}

// ================================================================ the sound
enum {
    SF_CRACK,
    SF_SHATTER,
    SF_BURST,
    SF_TINKLE,
    SF_TING,
    SF_OBJBREAK,
    SF_WOOD,
    SF_SNAP,
    SF_METAL,
    SF_CLANG,
    SF_THUD,
    SF_SPLASH,
    SF_POUR,
    SF_CUT,
    SF_CREAK,
    SF_CHAIN,
    SF_WINCH,
    SF_CLICK,
    SF_SELECT,
    SF_STAR,
    SF_N
};
static const char *const SF_NAME[SF_N] = {"crack", "shatter", "burst", "tinkle", "ting",   "objbreak", "wood",
                                          "snap",  "metal",   "clang", "thud",   "splash", "pour",     "cut",
                                          "creak", "chain",   "winch", "click",  "select", "star"};
static float s_snd_t[SF_N];  // the last start of each effect (s): no flood of one effect
static void snd(int id, int vol, float var)
{
    if (!s_sound || id < 0 || id >= SF_N) {
        return;
    }
    if (s_clock - s_snd_t[id] < 0.05f) {
        return;
    }
    s_snd_t[id] = s_clock;
    tfsfx_play_var(id, clampi(vol, 1, 100), var);
}
static bool s_mus_on;  // the player of the music is started (midi_bg_start once, again after a stop)
static void music_want(int track)
{
    if (!s_music || !s_ok) {
        return;
    }
    if (track == s_track) {
        return;
    }
    if (!s_mus_on) {
        midi_bg_start();
        s_mus_on = true;
    }
    midi_bg_play_track(track, 0, true);
    s_track = track;
    s_after_jingle = -1;
}
static void music_jingle(int track, int ms, int next)
{
    if (!s_music || !s_ok) {
        return;
    }
    if (!s_mus_on) {
        midi_bg_start();
        s_mus_on = true;
    }
    midi_bg_play_track(track, 0, false);
    s_track = track;
    s_jingle_left = ms / 1000.0f - 0.03f;
    s_after_jingle = next;
}
static void music_off(void)
{
    midi_bg_stop(300);
    s_mus_on = false;
    s_track = -1;
    s_after_jingle = -1;
}

// ================================================================ the saves
static void save_load(void)
{
    uint32_t v = store_get("glass_stars", 0);
    for (int i = 0; i < NLEV; i++) {
        s_stars[i] = (uint8_t)((v >> (2 * (i % 16))) & 3);
    }
}
static void save_stars(void)
{
    uint32_t v = 0;
    for (int i = 0; i < NLEV && i < 16; i++) {
        v |= (uint32_t)(s_stars[i] & 3) << (2 * i);
    }
    store_set("glass_stars", v);
}
// the last level that the shelf opens: the first level with no star, and one more
static int open_to(void)
{
    int first = NLEV - 1;
    for (int i = 0; i < NLEV; i++) {
        if (!s_stars[i]) {
            first = i;
            break;
        }
    }
    return clampi(first + 1, 0, NLEV - 1);
}
static int first_unsolved(void)
{
    for (int i = 0; i < NLEV; i++) {
        if (!s_stars[i]) {
            return i;
        }
    }
    return NLEV - 1;
}

// ================================================================ the camera
static float view_h(void)
{
    return rlay() ? SH * 0.52f : box_frame() ? (float)SH - 74 : (float)SH - 28;
}  // (the HUD bands)
static view_t view_fit(float x0, float y0, float x1, float y1)
{
    float vw = rlay() ? SW * 0.8f : (float)SW - 24, vh = view_h();
    float z = fminf(vw / fmaxf(20, x1 - x0), vh / fmaxf(20, y1 - y0));
    z = clampf(z, s_zmin, s_zmax);
    return (view_t){(x0 + x1) / 2, (y0 + y1) / 2, z};
}
static void cam_clamp(float *x, float *y, float z)
{
    float hw = SW / 2.0f / z;
    *x = hw >= 320 ? 320 : clampf(*x, hw, 640 - hw);
    float extra = rlay() ? SH * 0.24f / z : box_frame() ? 36 / z : 0;
    float bot = *y + ((float)SH - s_cy) / z, top = *y - s_cy / z;
    if (bot > 172 + extra) {
        *y -= bot - (172 + extra);
    } else if (top < -extra * 2 && bot < 172) {
        *y += fminf(-extra * 2 - top, 172 - bot);
    }
}
// the aim view: the parts that can move, the handles and the object, in the frame
static void aim_view(void)
{
    // (the core of the puzzle: the parts of the goal, the handles, the object; the bar screen also shows each part
    // that can move, the 320 x 240 frame only the core: its zoom stays big enough)
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < LV->np; i++) {
        const pdef_t *P = &LV->p[i];
        bool goal = (P->tags & (1u << LV->goal_tag)) != 0;
        if (!goal && (box_frame() || (P->flags & PF_STATIC))) {
            continue;
        }
        x0 = fminf(x0, P->x - P->w / 2);
        x1 = fmaxf(x1, P->x + P->w / 2);
        y0 = fminf(y0, P->y - P->h / 2);
        y1 = fmaxf(y1, P->y + P->h / 2);
    }
    for (int h = 0; h < LV->nh; h++) {
        float x, y;
        move_point(h, &x, &y);
        x0 = fminf(x0, x - 8);
        x1 = fmaxf(x1, x + 8);
        y0 = fminf(y0, y - 10);
        y1 = fmaxf(y1, y + 8);
    }
    // (the fixed points of the ropes: a crane, a hook)
    for (int k = 0; k < LV->nj; k++) {
        const jdef_t *J = &LV->j[k];
        if (J->kind != J_ROPE && J->kind != J_ROD) {
            continue;
        }
        if (J->a < 0) {
            x0 = fminf(x0, J->x1 - 6);
            x1 = fmaxf(x1, J->x1 + 6);
            y0 = fminf(y0, J->y1 - 6);
        }
        if (J->b < 0) {
            x0 = fminf(x0, J->x2 - 6);
            x1 = fmaxf(x1, J->x2 + 6);
            y0 = fminf(y0, J->y2 - 6);
        }
    }
    const objk_t *K = &OBK[LV->obj];
    x0 = fminf(x0, LV->obj_x - K->w);
    x1 = fmaxf(x1, LV->obj_x + K->w);
    y0 = fminf(y0, LV->obj_y - K->sh - K->h - 6);
    y1 = fmaxf(y1, FLOOR + 8);
    s_aimv = view_fit(x0 - 14, y0 - 10, x1 + 14, y1);
}
static void cam_set(view_t v, float kf)
{
    s_to = v;
    s_kf = kf;
}
static void cam_snap(void)
{
    s_cam.x = s_to.x;
    s_cam.y = s_to.y;
    s_cam.z = s_to.z;
    cam_clamp(&s_cam.x, &s_cam.y, s_cam.z);
}
static void cam_step(float dt)
{
    float f = 1.0f - expf(-s_kf * dt);
    s_cam.x += (s_to.x - s_cam.x) * f;
    s_cam.y += (s_to.y - s_cam.y) * f;
    s_cam.z = expf(logf(s_cam.z) + (logf(clampf(s_to.z, s_zmin, s_zmax)) - logf(s_cam.z)) * f);
    cam_clamp(&s_cam.x, &s_cam.y, s_cam.z);
    s_shake *= expf(-7.0f * dt);
    if (s_shake < 0.2f) {
        s_shake = 0;
    }
    s_cam.sx = s_shake * (vrnd() - 0.5f) * 2;
    s_cam.sy = s_shake * (vrnd() - 0.5f) * 2;
}
// the action: the box of the bodies that move (and of the last big event, and of the object when it is at risk)
static view_t action_view(void)
{
    // (the bodies that move fast; each part of the goal that moves, all of it (a tower that starts to fall); the last
    // big event; the object when it is at risk; and the floor under them)
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    int n = 0;
    for (int i = 0; i < W->body_hi; i++) {
        const tp_body_t *b = &W->body[i];
        if (!(b->flags & TP_ALIVE) || !(b->flags & TP_AWAKE) || b->kind != TP_DYNAMIC || b->gen) {
            continue;
        }
        bool goal = b->user >= 0 && b->user < LV->np && (LV->p[b->user].tags & (1u << LV->goal_tag));
        if (!goal && b->v.x * b->v.x + b->v.y * b->v.y < 0.04f) {
            continue;
        }
        float x = px_x(b->xf.p.x), y = px_y(b->xf.p.y), r = b->extent * PXM;
        if (x < -20 || x > 660) {
            continue;
        }
        x0 = fminf(x0, x - r);
        x1 = fmaxf(x1, x + r);
        y0 = fminf(y0, y - r);
        y1 = fmaxf(y1, y + r);
        n++;
    }
    bool fresh = s_clock - s_fx_t < 0.5f;
    if (fresh) {
        x0 = fminf(x0, s_fx_x - 24);
        x1 = fmaxf(x1, s_fx_x + 24);
        y0 = fminf(y0, s_fx_y - 20);
        y1 = fmaxf(y1, s_fx_y + 20);
        n++;
    }
    if (s_clock - s_obj_risk_t < 1.0f) {
        x0 = fminf(x0, G.o.x - 20);
        x1 = fmaxf(x1, G.o.x + 20);
        y0 = fminf(y0, G.o.y - 24);
        y1 = fmaxf(y1, G.o.y + 16);
        n++;
    }
    if (!n) {
        return s_aimv;
    }
    y1 = fmaxf(y1, FLOOR + 4);
    view_t v = view_fit(x0 - 20, y0 - 16, x1 + 20, y1 + 6);
    v.z = clampf(v.z, s_aimv.z * 0.95f, s_aimv.z * 1.7f);
    if (fresh) {
        v.x = lerpf(v.x, s_fx_x, 0.25f);
        v.y = lerpf(v.y, s_fx_y, 0.25f);
    }
    return v;
}
static void focus(float x, float y)
{
    s_fx_x = x;
    s_fx_y = y;
    s_fx_t = s_clock;
}
static void slow(float s)
{
    if (s > s_slow) {
        s_slow = s;
    }
}

// ================================================================ the events of the world: the sound and the director
static bool s_creak;  // the creak of the structure that starts to fall (once in a run)
static void fx_events(void)
{
    // (many panes that break in one step: one big crash)
    int nsh = 0;
    for (int i = 0; i < s_nfx; i++) {
        nsh += s_fx[i].kind == FX_SHATTER;
    }
    for (int i = 0; i < s_nfx; i++) {
        const fx_t *F = &s_fx[i];
        switch (F->kind) {
        case FX_HIT: {
            float v = F->v;
            if (v < 0.15f) {
                break;
            }
            int vol = clampi((int)(30 + v * 30), 20, 95);
            int id = F->mat == TP_SND_GLASS   ? SF_TING
                     : F->mat == TP_SND_METAL ? (v > 1.5f ? SF_CLANG : SF_METAL)
                     : F->mat == TP_SND_WOOD  ? SF_WOOD
                                              : SF_THUD;
            snd(id, vol, 0.12f);
            if (v > 1.2f) {
                s_shake = fmaxf(s_shake, clampf(v * 1.2f, 0, 5));
                focus(F->x, F->y);
            }
            break;
        }
        case FX_SHATTER:
            snd(nsh >= 3 ? SF_BURST : SF_SHATTER, clampi((int)(60 + F->v * 0.1f), 55, 100), 0.15f);
            s_shake = fmaxf(s_shake, 2.2f);
            focus(F->x, F->y);
            if (!s_first_shatter) {
                s_first_shatter = true;
                slow(0.55f);
            }
            break;
        case FX_CRACK:
            snd(SF_CRACK, 70, 0.15f);
            break;
        case FX_BREAK:
            snd(F->mat == TP_SND_GLASS   ? SF_BURST
                : F->mat == TP_SND_WOOD  ? SF_SNAP
                : F->mat == TP_SND_METAL ? SF_CLANG
                                         : SF_THUD,
                85, 0.1f);
            s_shake = fmaxf(s_shake, 3);
            focus(F->x, F->y);
            break;
        case FX_SNAP:
            snd(F->mat == MB_WOOD ? SF_SNAP : SF_CHAIN, 80, 0.1f);
            s_shake = fmaxf(s_shake, 1.5f);
            focus(F->x, F->y);
            break;
        case FX_SPLASH:
            snd(SF_SPLASH, clampi((int)(30 + F->v * 4), 30, 80), 0.2f);
            break;
        case FX_TINKLE:
            snd(SF_TINKLE, 45, 0.2f);
            break;
        case FX_MOVE: {
            static const int MS[MV_N] = {SF_CUT, SF_CHAIN, SF_CRACK, SF_WOOD, SF_POUR, SF_WINCH};
            snd(MS[F->mat % MV_N], 90, 0.05f);
            s_shake = fmaxf(s_shake, 1);
            focus(F->x, F->y);
            break;
        }
        case FX_OBJ_HIT:
            snd(SF_TING, 90, 0.05f);
            s_obj_risk_t = s_clock;
            if (F->v > 0.5f) {
                slow(0.9f);
            }
            break;
        case FX_OBJ_BREAK:
            snd(SF_OBJBREAK, 100, 0);
            s_obj_risk_t = s_clock;
            s_shake = fmaxf(s_shake, 4);
            slow(1.4f);
            focus(F->x, F->y);
            break;
        default:
            break;
        }
    }
    s_nfx = 0;
    if (s_tinkle) {
        snd(SF_TINKLE, clampi(30 + s_tinkle * 6, 30, 70), 0.25f);
        s_tinkle = 0;
        s_tinkle_v = 0;
    }
    if (!s_goal_seen && G.goal_met) {
        s_goal_seen = true;
        slow(0.7f);
    }
    // the creak: a part of the goal starts to turn
    for (int i = 0; i < LV->np && !s_creak; i++) {
        if (!(LV->p[i].tags & (1u << LV->goal_tag))) {
            continue;
        }
        tp_body_t *b = part_body(i);
        if (b && b->kind == TP_DYNAMIC && (b->flags & TP_AWAKE) && fabsf(b->w) > 0.6f) {
            s_creak = true;
            snd(SF_CREAK, 75, 0.05f);
        }
    }
}

// ================================================================ the levels
static uint32_t level_seed(int li)
{
    return 1000u + (uint32_t)li * 77u;
}
static bool level_build(int li)
{
    int64_t t0 = gl_us();
    (void)t0;  // (the host has no log)
    s_built = false;
    s_err[0] = 0;
    if (li < 0 || li >= NLEV) {
        return false;
    }
    if (!lv_parse(LV, GL_LEVELS[li])) {
        snprintf(s_err, sizeof s_err, "LEVEL %d: %s", li + 1, s_lv_err);
        return false;
    }
    if (!w_build(LV, level_seed(li))) {
        snprintf(s_err, sizeof s_err, "NO MEMORY (PHYSICS)");
        return false;
    }
    ESP_LOGI(TAG, "  the world and its settle: %d ms", (int)((gl_us() - t0) / 1000));
    if (s_prev) {
        memset(s_prev, 0, sizeof(prev_t) * (size_t)W->body_cap);
    }
    scene_paint();
    s_li = li;
    s_built = true;
    s_has_replay = false;
    aim_view();
    ESP_LOGI(TAG, "build level %d in %d ms, PSRAM free %u", li + 1, (int)((gl_us() - t0) / 1000),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return true;
}
// a new try or a level that starts: the director at rest
static void play_reset(void)
{
    s_sel = -1;
    s_acc = 0;
    s_slow = 0;
    s_shake = 0;
    s_fx_t = -9;
    s_obj_risk_t = -9;
    s_first_shatter = false;
    s_goal_seen = false;
    s_creak = false;
    s_panx = s_pany = 0;
    s_interp = 1;
    if (s_prev) {
        memset(s_prev, 0, sizeof(prev_t) * (size_t)W->body_cap);
    }
    imu_set_level();
}
static void level_start(void)
{
    play_reset();
    s_state = ST_PLAY;
    s_phase = PH_INTRO;
    s_t = 0;
    ESP_LOGI(TAG, "start level %d", s_li + 1);
    view_t wide = view_fit(STAND_X0 - 20, 20, STAND_X1 + 20, FLOOR + 8);
    cam_set(wide, 3);
    cam_snap();
    cam_set(s_aimv, 1.4f);
    music_want(GLM_AIM);
}
// the world of the level as it was before the first move: a new build (the pictures and the layers stay)
static bool level_rebuild(void)
{
    if (!w_build(LV, level_seed(s_li))) {
        return false;
    }
    if (s_prev) {
        memset(s_prev, 0, sizeof(prev_t) * (size_t)W->body_cap);
    }
    scene_rest();
    return true;
}
// AGAIN: a new try of the level
static void level_again(void)
{
    if (!level_rebuild()) {
        level_build(s_li);
    }
    play_reset();
    s_state = ST_PLAY;
    s_phase = PH_PLAY;
    s_t = 0;
    cam_set(s_aimv, 2.2f);
    music_want(GLM_AIM);
}
static void load_go(int li, int next_state)
{
    s_load_li = li;
    s_next_state = next_state;
    s_state = ST_LOAD;
    s_t = 0;
}
// play level li: the world that is built when it is that level (a new build when moves were done in it), else a load
static void play_go(int li)
{
    if (li != s_li || !s_built) {
        load_go(li, ST_PLAY);
        return;
    }
    if (G.nrec && !level_rebuild()) {
        load_go(li, ST_PLAY);
        return;
    }
    level_start();
}

// ================================================================ the moves of the player
static void do_move(int h)
{
    if (!hand_live(h)) {
        return;
    }
    w_move(h);
    s_sel = -1;
    s_panx = s_pany = 0;
    music_want(GLM_ACTION);
}
// the end of the action: the result, the stars, the save, the jingle
static void level_end(int result)
{
    s_result = result;
    s_phase = PH_END;
    s_end_t = 0;
    s_press = -1;
    s_end_stars = 0;
    s_has_replay = G.nrec > 0;
    if (result == RS_WON) {
        s_end_stars = 1 + (G.moves_done <= LV->par) + (G.o.hurt < 0.35f);
        if (s_end_stars > s_stars[s_li]) {
            s_stars[s_li] = (uint8_t)s_end_stars;
            save_stars();
        }
        bool all = true;
        for (int i = 0; i < NLEV; i++) {
            all &= s_stars[i] > 0;
        }
        if (all && s_li == NLEV - 1) {
            music_jingle(GLM_J_ALL, GL_J_ALL_MS, GLM_TITLE);
        } else {
            music_jingle(GLM_J_WIN, GL_J_WIN_MS, GLM_AIM);
        }
    } else if (result == RS_LOST) {
        music_jingle(GLM_J_LOST, GL_J_LOST_MS, GLM_AIM);
    } else {
        music_jingle(GLM_J_STAND, GL_J_STAND_MS, GLM_AIM);
    }
    // the record of the moves (G goes back to the snapshot in a replay)
    s_rp_n = G.nrec;
    for (int i = 0; i < s_rp_n; i++) {
        s_rp_step[i] = G.rec_step[i];
        s_rp_h[i] = G.rec_h[i];
    }
    s_rp_first = G.first_break;
    s_rp_big = G.big_step;
    s_rp_bx = G.big_x;
    s_rp_by = G.big_y;
    ESP_LOGI(TAG, "end level %d %s moves %d stars %d hurt %.2f", s_li + 1,
             result == RS_WON    ? "WON"
             : result == RS_LOST ? "LOST"
                                 : "STILL",
             G.moves_done, s_end_stars, G.o.hurt);
    end_layout(result == RS_WON, result == RS_LOST, s_li + 1 < NLEV, s_has_replay);
}
static void replay_start(void)
{
    if (!s_has_replay || !level_rebuild()) {
        return;
    }
    play_reset();
    s_phase = PH_REPLAY;
    s_t = 0;
    s_rp_i = 0;
    cam_set(s_aimv, 3);
    cam_snap();
    music_want(GLM_ACTION);
}
static void replay_end(void)
{
    // (the end state of the run comes back: the world plays to its end with the same moves)
    while (G.state == LV_ACT || (G.state == LV_AIM && s_rp_i < s_rp_n)) {
        if (s_rp_i < s_rp_n && G.step >= s_rp_step[s_rp_i]) {
            w_move(s_rp_h[s_rp_i]);
            s_rp_i++;
            continue;
        }
        if (G.state != LV_ACT) {
            break;
        }
        w_step();
        if (G.step > 60 * 60) {
            break;
        }
    }
    s_nfx = 0;
    s_nsh = 0;
    scene_sync();
    s_phase = PH_END;
    s_end_t = 2.5f;
    s_press = -1;
    cam_set(s_aimv, 2);
    music_want(GLM_AIM);
}

// ================================================================ the update
static void physics(float dt)
{
    float ts = s_slow > 0 ? 0.28f : 1.0f;
    s_slow = fmaxf(0, s_slow - dt);
    s_acc += dt * ts;
    int steps = 0;
    int64_t t0 = gl_us();
    bool replay = s_phase == PH_REPLAY;
    while (s_acc >= STEP_DT && steps < 3) {
        if (replay) {
            // the moves of the record at their steps; a move that waits for the rest of the world: at once
            while (s_rp_i < s_rp_n && (G.step >= s_rp_step[s_rp_i] || G.state == LV_AIM)) {
                w_move(s_rp_h[s_rp_i]);
                s_rp_i++;
            }
            // the director knows the future: slow motion at the first break and at the biggest hit
            if (s_rp_first && G.step + 6 == s_rp_first) {
                slow(0.8f);
            }
            if (s_rp_big && G.step + 8 == s_rp_big) {
                slow(1.0f);
                focus(s_rp_bx, s_rp_by);
            }
        }
        if (G.state != LV_ACT) {
            s_acc = 0;
            break;
        }
        interp_keep();
        w_step();
        s_acc -= STEP_DT;
        steps++;
        fx_events();
    }
    if (s_acc > STEP_DT * 2) {
        s_acc = 0;
    }
    s_interp = clampf(s_acc / STEP_DT, 0, 1);
    if (G.state != LV_ACT) {
        s_interp = 1;
    }
    s_phys_sum += (gl_us() - t0) / 1000.0f;
}
static void update_play(float dt)
{
    // the tilt: a small look round the stand (at the aim)
    float tx = 0, ty = 0;
    imu_tilt_logical(&tx, &ty);
    s_tilt_x += (clampf(tx, -0.3f, 0.3f) - s_tilt_x) * fminf(1, dt * 4);
    s_tilt_y += (clampf(ty, -0.3f, 0.3f) - s_tilt_y) * fminf(1, dt * 4);
    if (s_phase == PH_INTRO) {
        if (s_t > 2.9f) {
            s_phase = PH_PLAY;
            s_t = 0;
        }
    }
    if (s_phase == PH_PLAY || s_phase == PH_INTRO) {
        // the BOOT key does the selected move
        if (app_key_presses() > 0 && s_sel >= 0) {
            do_move(s_sel);
        }
        if (s_sel >= 0 && !hand_live(s_sel)) {
            s_sel = -1;
        }
        if (G.state == LV_ACT) {
            physics(dt);
            if (G.state == LV_AIM) {
                ESP_LOGI(TAG, "rest level %d moves left %d", s_li + 1, G.moves_left);
            }
        } else {
            s_acc = 0;
        }
        if (G.state == LV_WON) {
            level_end(RS_WON);
        } else if (G.state == LV_LOST) {
            level_end(RS_LOST);
        } else if (G.state == LV_STILL) {
            level_end(RS_STILL);
        }
    } else if (s_phase == PH_REPLAY) {
        physics(dt);
        bool done = G.state == LV_WON || G.state == LV_LOST || G.state == LV_STILL ||
                    (G.state == LV_AIM && s_rp_i >= s_rp_n);
        if (done && s_t > 1) {
            replay_end();
        }
        if (s_t > 40) {
            replay_end();
        }
    } else if (s_phase == PH_END) {
        float t0 = s_end_t;
        s_end_t += dt;
        float at = s_result == RS_WON ? 1.5f : s_result == RS_LOST ? 1.3f : 0.6f;
        for (int k = 0; k < s_end_stars && s_result == RS_WON; k++) {
            float st = at + 0.5f + k * 0.35f;
            if (t0 < st && s_end_t >= st) {
                snd(SF_STAR, 70 + k * 10, 0);
            }
        }
    }
    // the director
    if (s_phase == PH_INTRO) {
        view_t v = s_t < 0.9f ? view_fit(STAND_X0 - 20, 20, STAND_X1 + 20, FLOOR + 8) : s_aimv;
        cam_set(v, s_t < 0.9f ? 3 : 1.5f);
    } else if (s_phase == PH_PLAY || s_phase == PH_REPLAY) {
        if (G.state == LV_ACT) {
            cam_set(action_view(), s_slow > 0 ? 3.0f : 2.2f);
        } else if (s_sel >= 0) {
            float x, y;
            move_point(s_sel, &x, &y);
            cam_set((view_t){x, y + 6 / s_aimv.z,
                             fminf(s_zmax, fmaxf(s_aimv.z * 1.7f, rlay()        ? 1.5f * RK
                                                                  : box_frame() ? 1.5f
                                                                                : 2.0f))},
                    3.2f);
        } else {
            view_t v = s_aimv;
            v.x += s_panx + s_tilt_x * 70 / v.z + fsin(s_clock * 0.21f) * 4 / v.z;
            v.y += s_pany + s_tilt_y * 30 / v.z;
            v.z *= 1 + 0.015f * fsin(s_clock * 0.17f);
            cam_set(v, s_down && s_drag ? 12 : 2.2f);
        }
    } else if (s_phase == PH_END) {
        float at = s_result == RS_WON ? 1.5f : s_result == RS_LOST ? 1.3f : 0.6f;
        if (s_end_t < at && s_result != RS_STILL) {
            view_t v = {G.o.x, G.o.y - 4, fminf(s_zmax, s_aimv.z * 2.2f)};
            cam_set(v, 2.6f);
        } else {
            cam_set(s_aimv, 1.6f);
        }
    }
    // the shards, the puddles and the time of the picture
    if (s_phase != PH_END || s_end_t < 4) {
        shards_step(dt * (s_slow > 0 ? 0.28f : 1.0f));
    }
    if (G.state == LV_ACT) {
        puddles();
    }
    int64_t ts = gl_us();
    scene_sync();
    int sync_ms = (int)((gl_us() - ts) / 1000);
    if (sync_ms > 25) {
        ESP_LOGI(TAG, "  slow sync %d ms", sync_ms);
    }
}
static void update(int64_t now)
{
    float dt = clampf((now - s_last) / 1000.0f, 0, 0.066f);
    s_last = now;
    if (!s_ok) {
        return;
    }
    s_clock += dt;
    s_t += dt;
    midi_bg_service();
    if (s_after_jingle >= 0) {
        s_jingle_left -= dt;
        if (s_jingle_left <= 0) {
            int t = s_after_jingle;
            s_after_jingle = -1;
            music_want(t);
        }
    }
    switch (s_state) {
    case ST_LOAD:
        // (the first frame shows the dark screen; then the build)
        if (s_t > 0.05f) {
            if (!level_build(s_load_li)) {
                s_ok = false;
                return;
            }
            if (s_next_state == ST_PLAY) {
                level_start();
            } else {
                s_state = ST_TITLE;
                s_t = 0;
                play_reset();
                music_want(GLM_TITLE);
            }
        }
        break;
    case ST_TITLE: {
        view_t v = s_aimv;
        float k = box_frame() ? 0 : (s_he ? -1 : 1);
        v.x -= k * 120 / v.z;
        v.x += fsin(s_t * 0.13f) * 40 / v.z;
        v.z *= 0.92f + 0.05f * fsin(s_t * 0.09f);
        cam_set(v, 1.2f);
        if (s_t < 0.1f) {
            cam_snap();
        }
        scene_sync();
        break;
    }
    case ST_SHELF:
        break;
    case ST_PLAY:
        update_play(dt);
        break;
    }
    cam_step(dt);
}

// ================================================================ the render
static void draw_handles(void)
{
    if (s_phase != PH_PLAY && s_phase != PH_INTRO) {
        return;
    }
    if (G.moves_left == 0 || G.o.st != OS_WHOLE) {
        return;
    }
    for (int h = 0; h < LV->nh; h++) {
        if (!hand_live(h)) {
            continue;
        }
        float x, y;
        hand_point(h, &x, &y);
        bool sel = h == s_sel;
        float p = 0.5f + 0.5f * fsin(s_clock * 6 + h);
        float r = (sel ? 11 : 8) + p * 1.5f;
        uint16_t c = sel ? C3(255, 230, 160) : C_HUD;
        tfd_disc(x, y, r + 2, C3(4, 12, 14), 110);
        tfd_ring(x, y, r, sel ? 2 : 1.5f, c, 230);
        for (int k = 0; k < 4; k++) {
            float dx = k == 0 ? 1.0f : k == 1 ? -1.0f : 0, dy = k == 2 ? 1.0f : k == 3 ? -1.0f : 0;
            tfd_line(x + dx * (r + 2), y + dy * (r + 2), x + dx * (r + 5), y + dy * (r + 5), 1.4f, c, 200);
        }
        const hdef_t *H = &LV->h[h];
        hand_icon(H->kind, x, y, r * 0.9f, c, 240, H->dx, H->dy);
        if (sel) {
            box_t b = pill_box(x, y);
            glass_button(b, move_word(H->kind), true, s_press == 50);
        }
    }
}
// a hand that shows the first move on the first levels (no text): it taps the handle, then the pill
static void hint_hand(float x, float y, float t)
{
    float k = fmodf(t, 1.2f) / 1.2f, press = k < 0.35f ? k / 0.35f : k < 0.55f ? 1 : 1 - (k - 0.55f) / 0.45f;
    float tx = x + 2, ty = y + 2 + 3 * press, z = box_frame() ? 0.9f : 1.0f;
    if (press > 0.9f) {
        tfd_ring(x, y, 6 + 10 * (k - 0.35f), 1.5f, C3(255, 255, 255), (int)(200 * (1 - k)));
    }
    // the finger (from its tip down and to the right), the palm, the thumb
    float fx = tx + 9 * z, fy = ty + 20 * z;
    tfd_line(tx + 1, ty + 1, fx + 1, fy + 1, 7.5f * z, C3(10, 16, 18), 120);
    tfd_disc(fx + 7 * z + 1, fy + 8 * z + 1, 10 * z, C3(10, 16, 18), 120);
    tfd_line(tx, ty, fx, fy, 6.5f * z, C3(250, 236, 222), 245);
    tfd_disc(tx, ty, 3.25f * z, C3(250, 236, 222), 245);
    tfd_disc(fx + 7 * z, fy + 8 * z, 10 * z, C3(250, 236, 222), 245);
    tfd_line(fx - 2 * z, fy + 6 * z, fx - 7 * z, fy + 1 * z, 5 * z, C3(240, 222, 206), 245);
    tfd_disc(tx, ty - 1.2f * z, 1.6f * z, C3(255, 214, 214), 200);
}
static void draw_hint(void)
{
    if (s_li > 1 || s_phase != PH_PLAY || G.state != LV_AIM || G.nrec || s_t < 2.5f || LV->nsol < 1 || s_down) {
        return;
    }
    int h = LV->sol[0].h;
    if (!hand_live(h)) {
        return;
    }
    float x, y;
    hand_point(h, &x, &y);
    if (s_sel == h) {
        box_t b = pill_box(x, y);
        x = b.x + b.w * 0.5f;
        y = b.y + b.h * 0.55f;
    } else if (s_sel >= 0) {
        return;
    }
    hint_hand(x, y, s_t);
}
// the bands of a film in the slow motion
static float s_band;
static void slow_bands(float dt)
{
    s_band += ((s_slow > 0 ? 1.0f : 0.0f) - s_band) * fminf(1, dt * 8);
    int bh = (int)(s_band * (box_frame() ? 14 : 10) + 0.5f);
    for (int y = 0; y < bh; y++) {
        tfd_hspan(0, SW, y, C3(0, 0, 0), 235);
        tfd_hspan(0, SW, SH - 1 - y, C3(0, 0, 0), 235);
    }
}
// the soft studio vignette: dark bands at the top and the bottom of the frame
static void vignette(void)
{
    int n = box_frame() ? 6 : 8;
    for (int y = 0; y < n; y++) {
        int a = (int)(110 * (1 - (float)y / n) * (1 - (float)y / n));
        tfd_hspan(0, SW, y, C3(0, 6, 8), a);
        tfd_hspan(0, SW, SH - 1 - y, C3(0, 6, 8), a);
    }
}
// the time of each part of a frame (the speed log): the world layer, the parts that move, the rest of the world, the
// overlays
static float s_prof[4];
static int64_t s_prof_t;
static void prof(int k)
{
    int64_t t = gl_us();
    s_prof[k] += (t - s_prof_t) / 1000.0f;
    s_prof_t = t;
}
static float s_zsum;
static int s_n2x;
static void draw_world(void)
{
    s_zsum += s_cam.z;
    s_n2x += s_cam.z > 1.45f;
    s_prof_t = gl_us();
    blit_world();
    prof(0);
    cv_screen();
    tfd_screen();
    tfd_noclip();
    draw_glows();
    draw_live_parts();
    prof(1);
    draw_growing_cracks();
    draw_pieces();
    draw_joints();
    draw_emitters();
    draw_particles();
    vignette();
    prof(2);
}
static int obj_state(void)
{
    if (G.o.st != OS_WHOLE) {
        return 2;
    }
    return s_clock - s_obj_risk_t < 1.5f || (G.state == LV_ACT && G.o.hurt >= 0.35f) ? 1 : 0;
}
static void render(int64_t now)
{
    (void)now;
    tfd_screen();
    tfd_noclip();
    if (!s_ok) {
        gfx_fill(C3(20, 30, 34));
        tfd_text_c(&KF_LABEL, SW / 2, SH / 2, s_err[0] ? s_err : "...", C3(255, 200, 180), SW - 16);
        return;
    }
    int64_t a = gl_us();
    if (s_state == ST_LOAD || !s_built) {
        gfx_fill(C3(8, 14, 16));
        const char *nm = s_load_li >= 0 && s_load_li < NLEV ? GL_LEVEL_NAME[s_load_li][s_he] : "";
        tfd_text_c(&KF_BIG, SW / 2, SH / 2 - 4, s_next_state == ST_PLAY ? nm : TX("GLASSHOUSE", "חממה"),
                   C3(242, 213, 139), SW - 16);
        tfd_text_c(&KF_LABEL, SW / 2, SH / 2 + 20, "...", C3(200, 220, 214), SW - 16);
        return;
    }
    if (s_state == ST_SHELF) {
        const char *names[NLEV];
        for (int i = 0; i < NLEV; i++) {
            names[i] = GL_LEVEL_NAME[i][s_he];
        }
        shelf_draw(NLEV, s_stars, open_to(), s_press, names);
        return;
    }
    draw_world();
    if (s_state == ST_TITLE) {
        int pl = first_unsolved();
        title_draw(s_press, pl, GL_LEVEL_NAME[pl][s_he]);
    } else {
        // the name on the brass plate of the stand
        float py = scr_y(159.5f), px = scr_x(320);
        if (py > 0 && py < SH + 10) {
            const kfont_t *f = s_cam.z > 1.7f ? &KF_NAME : &KF_LABEL;
            tfd_text_c(f, (int)px, (int)(py + (f == &KF_NAME ? 5 : 4)), LV->name[s_he], C3(58, 38, 10),
                       (int)((s_plate_x1 - s_plate_x0) * s_cam.z) - 6);
        }
        draw_handles();
        draw_hint();
        slow_bands(s_phase == PH_REPLAY ? 0 : 0.033f);
        if (s_phase == PH_REPLAY) {
            replay_band(s_t);
        } else {
            bool gb = LV->goal == GOAL_BREAK;
            int panes = gb ? (int)(G.goal_now * LV->goal_v + 0.01f) : 0;
            hud_draw(LV->name[s_he], s_li, G.moves_left, LV->moves, G.goal_now, gb, panes, (int)LV->goal_v, obj_state(),
                     OBK[LV->obj].name[s_he], OBK[LV->obj].safe[1], OBK[LV->obj].broke[1]);
        }
        if (s_phase == PH_INTRO) {
            float al = clampf(s_t < 0.3f ? (s_t - 0.1f) / 0.2f : s_t > 2.5f ? (2.9f - s_t) / 0.4f : 1, 0, 1);
            intro_card(LV->name[s_he], LV->idea[s_he], al);
        }
        if (s_phase == PH_END) {
            float at = s_result == RS_WON ? 1.5f : s_result == RS_LOST ? 1.3f : 0.6f;
            if (s_end_t >= at) {
                char title[80], line[96];
                if (s_result == RS_WON) {
                    snprintf(title, sizeof title, "%s", OBK[LV->obj].safe[s_he]);
                } else if (s_result == RS_LOST) {
                    snprintf(title, sizeof title, "%s", OBK[LV->obj].broke[s_he]);
                } else {
                    snprintf(title, sizeof title, "%s", TX("IT STILL STANDS", "זה עדיין עומד"));
                }
                if (s_result == RS_WON) {
                    char mvs[24];
                    if (G.moves_done == 1) {
                        snprintf(mvs, sizeof mvs, "%s", TX("1 MOVE", "מהלך אחד"));
                    } else {
                        snprintf(mvs, sizeof mvs, "%d %s", G.moves_done, TX("MOVES", "מהלכים"));
                    }
                    snprintf(line, sizeof line, "%s  -  %s", mvs,
                             G.o.hurt < 0.35f ? TX("NOT A SCRATCH", "בלי שריטה") : TX("A SMALL CRACK", "סדק קטן"));
                } else if (s_result == RS_LOST) {
                    snprintf(line, sizeof line, "%s", TX("TRY ANOTHER WAY", "נסו דרך אחרת"));
                } else {
                    snprintf(line, sizeof line, "%s", TX("NO MOVE IS LEFT", "לא נשארו מהלכים"));
                }
                end_card(s_result, s_end_stars, title, line, s_end_t - at, s_press);
            }
        }
    }
    prof(3);
    float ms = (gl_us() - a) / 1000.0f;
    s_ms_sum += ms;
    if (ms > s_ms_max) {
        s_ms_max = ms;
    }
    if (s_ms_n == 0) {
        s_fps_t0 = a;
    }
    if (++s_ms_n == 150) {
        float fps = a > s_fps_t0 ? 149.0f * 1e6f / (float)(a - s_fps_t0) : 0;
        (void)fps;
        ESP_LOGI(TAG, "fps %.1f, draw %.1f ms (max %.1f), physics %.1f ms, bodies %d, parts %d, awake %d, state %d/%d",
                 fps, s_ms_sum / s_ms_n, s_ms_max, s_phys_sum / s_ms_n, W ? W->nbody : 0, W ? W->npart : 0,
                 W ? W->stat.awake : 0, s_state, s_phase);
        ESP_LOGI(TAG, "  world %.1f, moving parts %.1f, the rest %.1f, overlays %.1f ms; zoom %.2f, 2x frames %d",
                 s_prof[0] / s_ms_n, s_prof[1] / s_ms_n, s_prof[2] / s_ms_n, s_prof[3] / s_ms_n, s_zsum / s_ms_n,
                 s_n2x);
        s_zsum = 0;
        s_n2x = 0;
        memset(s_prof, 0, sizeof s_prof);
        s_ms_sum = s_ms_max = s_phys_sum = 0;
        s_ms_n = 0;
    }
}

// ================================================================ the input
static void play_tap(int x, int y)
{
    if (s_phase == PH_INTRO) {
        s_phase = PH_PLAY;
        s_t = 0;
        return;
    }
    if (s_phase != PH_PLAY) {
        return;
    }
    if (G.moves_left == 0 || G.o.st != OS_WHOLE) {
        return;
    }
    if (s_sel >= 0) {
        float hx, hy;
        hand_point(s_sel, &hx, &hy);
        box_t b = pill_box(hx, hy);
        // the pill, or the selected handle again: the move
        if (in_box_m(b, x, y, 4) || (hx - x) * (hx - x) + (hy - y) * (hy - y) < 30 * 30) {
            do_move(s_sel);
            return;
        }
    }
    int h = hand_at((float)x, (float)y, rlay() ? 56 : box_frame() ? 44 : 50);
    if (h >= 0 && h != s_sel) {
        s_sel = h;
        snd(SF_SELECT, 70, 0);
        return;
    }
    s_sel = -1;
}
static int end_btn_at(int x, int y)
{
    for (int i = 0; i < s_end_nb; i++) {
        if (in_box_m(s_end_btn[i], x, y, 4)) {
            return i;
        }
    }
    return -1;
}
static void end_press(int i)
{
    if (i < 0 || i >= s_end_nb) {
        return;
    }
    snd(SF_CLICK, 70, 0);
    int id = s_end_id[i];
    if (id == EB_NEXT && s_li + 1 < NLEV) {
        load_go(s_li + 1, ST_PLAY);
    } else if (id == EB_REPLAY) {
        replay_start();
    } else {
        level_again();
    }
}
static bool card_ready(void)
{
    float at = s_result == RS_WON ? 1.5f : s_result == RS_LOST ? 1.3f : 0.6f;
    return s_end_t >= at + 0.4f;
}
static void input(const input_event_t *ev)
{
    if (!s_ok || s_state == ST_LOAD) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (ev->kind == IN_DOWN) {
        s_down = true;
        s_drag = false;
        s_dn_x = x;
        s_dn_y = y;
        s_dn_cx = s_panx;
        s_dn_cy = s_pany;
        s_press = -1;
        if (s_state == ST_TITLE) {
            s_press = in_box_m(title_play(), x, y, 4) ? 0 : in_box_m(title_levels(), x, y, 4) ? 1 : -1;
        } else if (s_state == ST_SHELF) {
            if (in_box_m(shelf_back(), x, y, 4)) {
                s_press = 99;
            } else {
                for (int i = 0; i < NLEV; i++) {
                    if (in_box_m(shelf_tile(i, NLEV), x, y, 3)) {
                        s_press = i;
                    }
                }
            }
        } else if (s_phase == PH_END && card_ready()) {
            s_press = end_btn_at(x, y);
        } else if (s_phase == PH_PLAY && s_sel >= 0) {
            float hx, hy;
            hand_point(s_sel, &hx, &hy);
            if (in_box_m(pill_box(hx, hy), x, y, 4)) {
                s_press = 50;
            }
        }
        return;
    }
    if (ev->kind == IN_MOVE && s_down) {
        float dx = x - s_dn_x, dy = y - s_dn_y;
        if (!s_drag && dx * dx + dy * dy > 14 * 14) {
            s_drag = true;
            if (s_state == ST_PLAY) {
                input_tap_cancel();
            }
        }
        // a drag at the aim moves the view (a look round the stand)
        if (s_drag && s_state == ST_PLAY && s_phase == PH_PLAY && G.state != LV_ACT && s_press < 0) {
            float z = s_cam.z;
            s_panx = clampf(s_dn_cx - dx / z, -200 / z, 200 / z);
            s_pany = clampf(s_dn_cy - dy / z, -60 / z, 60 / z);
            s_sel = -1;
        }
        return;
    }
    if (ev->kind != IN_UP || !s_down) {
        return;
    }
    s_down = false;
    int pr = s_press;
    s_press = -1;
    bool tap = !s_drag;
    if (s_state == ST_TITLE) {
        if (pr == 0 && in_box_m(title_play(), x, y, 6)) {
            snd(SF_CLICK, 70, 0);
            play_go(first_unsolved());
        } else if (pr == 1 && in_box_m(title_levels(), x, y, 6)) {
            snd(SF_CLICK, 70, 0);
            s_state = ST_SHELF;
            s_t = 0;
        }
        return;
    }
    if (s_state == ST_SHELF) {
        if (pr == 99 && in_box_m(shelf_back(), x, y, 6)) {
            snd(SF_CLICK, 70, 0);
            s_state = ST_TITLE;
            s_t = 0.2f;
            return;
        }
        if (pr >= 0 && pr < NLEV && in_box_m(shelf_tile(pr, NLEV), x, y, 6)) {
            if (pr <= open_to()) {
                snd(SF_CLICK, 70, 0);
                play_go(pr);
            } else {
                snd(SF_THUD, 40, 0);
            }
        }
        return;
    }
    if (s_phase == PH_REPLAY) {
        if (tap) {
            replay_end();
        }
        return;
    }
    if (s_phase == PH_END) {
        if (pr >= 0 && end_btn_at(x, y) == pr) {
            end_press(pr);
        }
        return;
    }
    if (tap) {
        play_tap(x, y);
    }
}

// ================================================================ the settings panel
enum {
    HI_SND,
    HI_MUS,
    HI_LANG,
    HI_RESTART,
    HI_LEVELS
};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const pitem_t ITEMS[] = {
        {PI_PICK, HI_SND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, "glass_snd"},
        {PI_PICK, HI_MUS, 2, 0, {"MUSIC", "מוזיקה"}, ONOFF_V, "glass_mus"},
        {PI_PICK, HI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "glass_lang"},
        {PI_ACT2, HI_RESTART, 0, 0, {"RESTART", "מההתחלה"}, NULL, NULL},
        {PI_ACT, HI_LEVELS, 0, 0, {"LEVELS", "שלבים"}, NULL, NULL},
};
static bool item_shown(int id)
{
    return (id != HI_RESTART && id != HI_LEVELS) || s_state == ST_PLAY;
}
static void item_set(int id, int v)
{
    if (!s_ok) {
        return;
    }
    if (id == HI_SND) {
        s_sound = v == 0;
    } else if (id == HI_MUS) {
        s_music = v == 0;
        if (!s_music) {
            music_off();
        } else {
            music_want(s_state == ST_PLAY ? GLM_AIM : GLM_TITLE);
        }
    } else if (id == HI_LANG) {
        s_he = v == 1;
    } else if (id == HI_RESTART && s_state == ST_PLAY && s_built) {
        level_again();
    } else if (id == HI_LEVELS && s_state == ST_PLAY) {
        s_state = ST_SHELF;
        s_t = 0;
        music_want(GLM_TITLE);
    }
}
static const pitems_t PANEL = {ITEMS, 5, item_shown, NULL, item_set, NULL, NULL};

// ================================================================ the app
static void leave(void);
static void enter(int64_t now)
{
    s_last = now;
    s_ok = false;
    s_err[0] = 0;
    s_built = false;
    s_li = -1;
    s_he = store_get("glass_lang", 1) == 1;  // (no key: the APP LANGUAGE of SETUP; else Hebrew, as most games)
    s_sound = store_get("glass_snd", 0) == 0;
    s_music = store_get("glass_mus", 0) == 0;
    sysui_panel_items(&PANEL);
    save_load();
    s_cx = SW / 2.0f;
    s_cy = box_frame() ? SH / 2.0f - 6 : SH / 2.0f;
    s_zmin = box_frame() ? 0.62f : 1.0f;
    s_zmax = box_frame() ? 2.6f : 3.2f;
    if (rlay()) {
        // (the round frame: the zoom of the 320 x 240 frame, RK times bigger; the middle of the circle)
        s_cy = SH / 2.0f;
        s_zmin = 0.75f * RK;
        s_zmax = 2.6f * RK;
    }
    for (int i = 0; i < 3; i++) {
        s_glass_tint[i] = HX(GLASS_TINT[i]);
    }
    LV = (level_t *)gl_alloc(sizeof(level_t));
    if (!LV || !view_alloc() || !w_alloc()) {
        leave();
        snprintf(s_err, sizeof s_err, "NO MEMORY");
        return;
    }
    {
        tp_world_def_t d = w_def();
        s_prev = (prev_t *)gl_alloc(sizeof(prev_t) * (size_t)d.max_bodies);
    }
    tfsfx_load_mem(GL_SFX_DATA, GL_SFX_LEN, SF_NAME, SF_N < GL_SFX_N ? SF_N : GL_SFX_N, 47);
    midi_bg_init(GL_TRACKS, GL_TRACK_COUNT, GL_MUSIC_VOICES);
    midi_bg_volume(70);
    s_track = -1;
    s_mus_on = false;
    s_after_jingle = -1;
    memset(s_snd_t, 0, sizeof s_snd_t);
    s_clock = 0;
    s_ok = true;
    s_ms_sum = s_ms_max = s_phys_sum = 0;
    s_ms_n = 0;
    // (the title shows THE ORCHID HOUSE, the look of the game; PLAY then opens the first level with no star)
    load_go(NLEV > TITLE_LI ? TITLE_LI : 0, ST_TITLE);
    ESP_LOGI(TAG, "open: world %u B, PSRAM free %u", (unsigned)s_wsize,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
static void leave(void)
{
    if (s_ok) {
        midi_bg_end();
        tfsfx_free();
    }
    s_ok = false;
    w_free();
    view_free();
    heap_caps_free(s_prev);
    s_prev = NULL;
    heap_caps_free(LV);
    LV = NULL;
    s_built = false;
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
// the icon: a glass dome on a timber base at dusk, with a red flower inside (the fragile thing); an iron ball on a rope
// swings from the beam in 2 s, touches the glass, and the glass cracks and throws shards
static void icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f, an = ph * 6.2831853f;
    ic_dband(x, y, s, 0, s * 72 / 100, ic_g(64, 96, 192), ic_g(224, 160, 128));
    ic_dband(x, y, s, s * 72 / 100, s, ic_g(64, 112, 48), ic_g(16, 48, 32));
    // the dome: the glass, the flower in it, the panes and the shine
    float dx = x + 66 * u, dy = y + 80 * u, dr = 27 * u;
    tfd_clip(x, y, x + s, (int)dy);
    tfd_disc(dx, dy, dr + 1.6f * u, ic_g(224, 224, 208), 256);
    tfd_disc(dx, dy, dr, ic_g(64, 144, 144), 256);
    tfd_disc(dx - 6 * u, dy - 8 * u, dr * 0.7f, ic_g(96, 176, 160), 256);
    tfd_line(dx - 2 * u, dy, dx - 2 * u, dy - 15 * u, 1.6f * u, ic_g(32, 128, 32), 256);
    ic_oval(dx - 6 * u, dy - 9 * u, 4 * u, 2 * u, ic_g(64, 160, 64), 256);
    tfd_disc(dx - 2 * u, dy - 17 * u, 4.6f * u, ic_g(160, 0, 32), 256);
    tfd_disc(dx - 3 * u, dy - 18 * u, 2.8f * u, ic_g(224, 64, 64), 256);
    for (int k = 1; k < 6; k++) {
        float a = 3.14159f + k * 3.14159f / 6;
        tfd_line(dx, dy, dx + fcos(a) * dr, dy + fsin(a) * dr, 1.4f * u, ic_g(224, 224, 208), 256);
    }
    tfd_ring(dx, dy, dr * 0.55f, 1.3f * u, ic_g(224, 224, 208), 256);
    tfd_line(dx - 19 * u, dy - 9 * u, dx - 12 * u, dy - 19 * u, 2.2f * u, ic_g(224, 240, 224), 190);
    tfd_line(dx - 20 * u, dy - 3 * u, dx - 18 * u, dy - 6 * u, 2.2f * u, ic_g(224, 240, 224), 190);
    tfd_disc(dx, dy - dr - 2 * u, 2.4f * u, ic_g(224, 224, 208), 256);
    tfd_clip(x, y, x + s, y + s);
    // the timber base
    tfd_rrect((int)(dx - dr - 6 * u), (int)dy, (int)(2 * dr + 12 * u), (int)(9 * u), (int)(1.5f * u), ic_g(96, 48, 16),
              256);
    tfd_rect((int)(dx - dr - 6 * u), (int)dy, (int)(2 * dr + 12 * u), (int)(2.4f * u), ic_g(192, 128, 64), 256);
    // the beam and the rope with the ball (the angle from the vertical; the far right end touches the glass)
    float bx = x + 28 * u, by = y + 15 * u, th = 0.24f + 0.5f * fsin(an), L = 42 * u;
    tfd_rrect((int)(x + 8 * u), (int)(by - 3 * u), (int)(44 * u), (int)(6 * u), (int)(1.5f * u), ic_g(128, 64, 32),
              256);
    tfd_rect((int)(x + 8 * u), (int)(by - 3 * u), (int)(44 * u), (int)(1.6f * u), ic_g(192, 128, 64), 256);
    float kx = bx + L * fsin(th), ky = by + L * fcos(th);
    tfd_line(bx, by, kx, ky, 1.3f * u + 0.3f, ic_g(224, 192, 128), 256);
    ic_oval(kx + 3 * u, y + 93 * u, 7 * u, 1.6f * u, ic_g(0, 32, 16), 120);
    ic_ball(kx, ky + 2 * u, 8.5f * u, ic_g(16, 16, 32), ic_g(64, 64, 96), ic_g(192, 192, 224));
    // the hit: a crack star where the ball touches, and shards that fly
    float hit = (th - 0.6f) / 0.14f;
    if (hit > 0) {
        float vx = kx - dx, vy = ky + 2 * u - dy, vl = sqrtf(vx * vx + vy * vy);
        float hx = dx + vx * dr / vl, hy = dy + vy * dr / vl;
        for (int k = 0; k < 6; k++) {
            float a = k * 1.047f + 0.3f;
            tfd_line(hx, hy, hx + fcos(a) * 7 * u, hy + fsin(a) * 7 * u, 1.1f * u, ic_g(224, 240, 224), 256);
        }
        static const float SH[] = {-2, -1.5f, 2.5f, -1, 0, 2};
        for (int k = 0; k < 5; k++) {
            float a = -1.9f + k * 0.45f, d = (6 + 14 * hit) * u;
            ic_shape(SH, 3, hx + fcos(a) * d, hy + fsin(a) * d, 1.3f * u, a * 3, ic_g(192, 240, 240), 256);
        }
        tfd_disc(hx, hy, 3 * u, ic_g(224, 240, 224), 256);
    }
    ic_end();
}

const app_t app_glass = {
        .name = "GLASSHOUSE",
        .landscape = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .round = APP_ROUND_HI,
        .key = true,
};
