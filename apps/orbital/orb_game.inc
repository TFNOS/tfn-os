// ORBITAL: the game (included by app_orbital.c). The controls of a person (tilt or the turn keys, the BURN pad, the
// TETHER key, the BOOT key, the Watcher wheel) or of the autopilot; the help for a new player (the GUIDE: a marker of
// the right way for the nose, the engine that burns only at the right time, the time of the next burn; the nose snaps
// to a marker); the clock of the mission (it runs fast when nothing happens and slow at each big moment); the goals and
// the failures; the effects and the sounds.

enum {
    ST_TITLE,
    ST_BRIEF,
    ST_PLAY,
    ST_DONE,
    ST_FAIL
};
enum {
    HELP_GUIDE,
    HELP_OFF,
    HELP_AUTO
};
enum {
    CTL_TILT,
    CTL_TOUCH
};
static int s_state = ST_TITLE;
static float s_state_t;  // real seconds in this state
static int s_lang;       // 0 English, 1 Hebrew
#define TR(en, he) (s_lang ? (he) : (en))
static int s_ctl_mode;  // CTL_*
static int s_help;      // HELP_*
static bool s_music = true;
static bool s_demo;  // the title: the autopilot flies a mission behind the title (silent effects, no saves)
static int s_demo_mis;
static int s_tgt = 1;                      // the body that the mission works on now
static bool s_released[NB];                // a body that the tug let go in its goal (delivered or falling to the ship)
static int s_landed;                       // G_LAND: 0 not yet, 1 near the ship, 2 far from it
static float s_land_t;                     // real seconds since the capsule came down
static float s_inside_t;                   // real seconds inside the goal
static float s_rate = 5, s_rate_goal = 5;  // mission seconds for each real second
static float s_close;                      // the camera: 0 = the orbit view, 1 = the close view (eased)
static ctl_t s_ctl;                        // the controls of this frame
static bool s_auto;              // the autopilot flies (the demo of the title, the AUTOPILOT help, the test bot)
static float s_ramp;             // the engine: 0..1 (it grows in 0.3 s)
static float s_hold_t;           // the TETHER key (or BOOT) held while towing (s): 1 s lets the tow go
static float s_turn_v;           // the turn of the nose now (rad/s, eased)
static int s_snap = -1;          // the marker that holds the nose (-1: none)
static float s_nofuel_t;         // real seconds with no fuel and no way to the goal
static float s_debris_d = 1e9f;  // HEAD-ON: the least distance to the debris so far (km)
static int s_fail_why;
enum {
    F_NONE,
    F_TGT_BURN,
    F_TUG_BURN,
    F_FUEL,
    F_DEBRIS,
    F_FAR_LAND,
    F_TGT_LOST
};
static float s_result_fuel;  // the results of a mission: fuel left (kg), the time (s), the stars
static double s_result_time;
static int s_result_stars;
static bool s_new_best;
static float s_pred_t;       // real seconds since the last prediction
static float s_burn_wait_t;  // real seconds that the GUIDE wants a burn and the engine is off
static float s_free_dv;      // m/s of the burns that the guide did not ask for (then it plans again)
static bool s_gated;         // the GUIDE stops the engine now (the finger is down, the time is wrong)
static bool s_finger_burn;   // a finger is on the BURN pad (or the autopilot burns)
static bool s_teth_down;     // a finger is on the TETHER key
static float s_engine;       // the engine sound and the flame: 0..1 (eased)

// ================================================================ the layout of the keys of the play
typedef struct {
    int x, y, w, h;
} box_t;
static inline bool in_box(box_t b, int x, int y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}
static inline bool longl(void)
{
    return SW >= 600;
}  // the 3.49 bar (640 x 172)
// (the 3.49: the right thumb has BURN (below) and TETHER (above) at the right end, the left thumb the turn keys; the 320
// x 240 frame: one row of keys at the bottom: the turn keys at the left, TETHER and BURN at the right. All 62 px or more:
// 7.9 mm)
// The round frame (gfx_round(), 466 x 466 on the 1.75C, 412 on the Watcher): the keys are on the bottom arc of the
// circle (the turn keys at the left, TETHER and BURN at the right), the HUD text in the top cap.
static inline bool rndl(void)
{
    return gfx_round();
}
// a key of the bottom arc: k = 0 .. 3 from the left (each a bit higher at the ends)
static box_t box_arc(int k)
{
    int w = SW * 15 / 100, g = SW / 100, x0 = SW / 2 - 2 * w - g - g / 2;
    int x = x0 + k * (w + g) + (k >= 2 ? 2 * g : 0);
    int y = SH * 73 / 100 - (k == 0 || k == 3 ? SH * 5 / 100 : 0);
    box_t b = {x, y, w, w};
    return b;
}
static box_t box_burn(void)
{
    if (longl()) {
        box_t b = {SW - 120, 70, 116, SH - 74};
        return b;
    }
    if (rndl()) {
        return box_arc(s_ctl_mode == CTL_TOUCH ? 3 : 2);
    }
    box_t b = {SW - 80, SH - 72, 76, 68};
    return b;
}
static box_t box_tether(void)
{
    if (longl()) {
        box_t b = {SW - 120, 4, 116, 62};
        return b;
    }
    if (rndl()) {
        return box_arc(s_ctl_mode == CTL_TOUCH ? 2 : 1);
    }
    box_t b = {SW - 166, SH - 72, 82, 68};
    return b;
}
static box_t box_turn(int k)  // k 0: left, 1: right
{
    if (longl()) {
        box_t b = {4 + k * 68, SH - 68, 64, 64};
        return b;
    }
    if (rndl()) {
        return box_arc(k);
    }
    box_t b = {4 + k * 66, SH - 72, 62, 68};
    return b;
}

// ================================================================ the sound
// the MP3 effects in the app file (orb_sfx.c; libs/tfsfx)
enum {
    FX_IGNITE,
    FX_PUFF,
    FX_HARPOON,
    FX_CLANK,
    FX_LOCK,
    FX_WINCH,
    FX_CREAK,
    FX_SNAP,
    FX_BUMP,
    FX_ALARM,
    FX_BEEP,
    FX_WHOOSH,
    FX_BOOM,
    FX_SPLASH,
    FX_DEBRIS,
    FX_CLICK,
    FX_CONFIRM,
    FX_ERROR,
    FX_N
};
static const char *const FX_NAMES[FX_N] = {"ignite", "puff",   "harpoon", "clank", "lock",    "winch",
                                           "creak",  "snap",   "bump",    "alarm", "beep",    "whoosh",
                                           "boom",   "splash", "debris",  "click", "confirm", "error"};
static void fx(int id, int vol, float rate)
{
    if (s_demo) {
        return;
    }
    tfsfx_play(id, clampi(vol, 1, 100), rate);
}
static void fxv(int id, int vol, float var)
{
    if (s_demo) {
        return;
    }
    tfsfx_play_var(id, clampi(vol, 1, 100), var);
}
// the engine (a deep noise) and the air (a high roar): two drones
static void drones(float engine, float air)
{
    if (s_demo) {
        engine = 0;
        air = 0;
    }
    audio_drone(0, 70 + engine * 50, (int)(engine * 20), W_NOISE);
    audio_drone(1, 300 + air * 500, (int)(clampf(air, 0, 1) * 24), W_NOISE);
}
static void drones_off(void)
{
    audio_drone(0, 0, 0, W_NOISE);
    audio_drone(1, 0, 0, W_NOISE);
}
// the music: a loop for each part of the play (a soft change on a phrase line), the jingles
static int s_mus_loop = -1;
static float s_jingle_t;
static int s_mus_after = -1;
static int music_want(void)
{
    if (s_state == ST_TITLE) {
        return ORM_TITLE;
    }
    if (s_state == ST_DONE || s_state == ST_FAIL) {
        return ORM_COAST;
    }
    if (B[0].in_air || (T.state == TH_ON && B[T.to].in_air)) {
        return ORM_AIR;
    }
    return s_close > 0.5f ? ORM_CLOSE : ORM_COAST;
}
static void jingle(int track, int ms)
{
    if (!s_music) {
        return;
    }
    midi_bg_play_track(track, 0, false);
    s_jingle_t = ms / 1000.0f - 0.03f;
    s_mus_loop = -1;
}
static void music_step(float dt)
{
    if (!s_music) {
        return;
    }
    midi_bg_service();
    if (s_jingle_t > 0) {
        s_jingle_t -= dt;
        if (s_jingle_t > 0) {
            return;
        }
    }
    int want = music_want();
    if (want == s_mus_loop) {
        return;
    }
    if (s_mus_loop < 0 || s_state == ST_TITLE) {
        midi_bg_play_track(want, 0, true);
    } else {
        // (a loop to a loop: the new one comes in on the next phrase line)
        static const int PH[ORM_COUNT] = {OR_PHASE_TITLE, OR_PHASE_COAST, OR_PHASE_CLOSE, OR_PHASE_AIR};
        midi_bg_hold(false);
        midi_bg_set_phase(PH[want]);
    }
    s_mus_loop = want;
}

// ================================================================ the particles (in metres relative to the tug)
typedef struct {
    float x, y, vx, vy, life, max, size;
    uint8_t kind;
} part_t;
enum {
    P_PUFF,
    P_SPARK,
    P_VENT,
    P_SMOKE,
    P_EMBER
};
#define NPART 200
static part_t s_part[NPART];
static int s_npart;
static void part_add(int kind, float x, float y, float vx, float vy, float max, float size)
{
    if (s_npart >= NPART) {
        return;
    }
    part_t *p = &s_part[s_npart++];
    p->x = x;
    p->y = y;
    p->vx = vx;
    p->vy = vy;
    p->life = 0;
    p->max = max;
    p->size = size;
    p->kind = (uint8_t)kind;
}
// a body relative to the tug (m)
static void rel_m(int i, float *x, float *y)
{
    *x = (float)((B[i].x - B[0].x) * 1000.0);
    *y = (float)((B[i].y - B[0].y) * 1000.0);
}
static float s_shake;  // the screen shake (px)
static void shake(float px)
{
    if (px > s_shake) {
        s_shake = px;
    }
}
static void sparks_at(float x, float y, int n, float speed)
{
    for (int i = 0; i < n; i++) {
        float a = rndf() * TAU_F, s = speed * (0.3f + rndf());
        part_add(P_SPARK, x, y, cosf(a) * s, sinf(a) * s, 0.25f + rndf() * 0.5f, 1);
    }
}
// the speed of the particles on the screen: the time of the mission, but at most 4 times the real time
static float s_rate_vis(void)
{
    return clampf(s_rate, 1, 4);
}
static void parts_step(float dt, float dvx, float dvy)
{
    // dt: real seconds (the particles live in real time); dvx, dvy: the change of the speed of the tug this frame (m/s),
    // so the puffs stay in the space and do not go with the tug
    float vs = s_rate_vis();
    for (int i = s_npart - 1; i >= 0; i--) {
        part_t *p = &s_part[i];
        p->life += dt;
        if (p->life >= p->max) {
            s_part[i] = s_part[--s_npart];
            continue;
        }
        p->vx -= dvx;
        p->vy -= dvy;
        float k = p->kind == P_SPARK ? 1.6f : p->kind == P_VENT ? 0.4f : 1.0f;
        float f = 1 - clampf(k * dt, 0, 0.5f);
        p->vx *= f;
        p->vy *= f;
        p->x += p->vx * dt * vs;
        p->y += p->vy * dt * vs;
    }
}

static void cam_reset(void);
static void cam_punch(float k);
static float s_tow_rt;   // (orb_view.inc) real seconds since the tether locked
static float s_burn_rt;  // (orb_view.inc) real seconds since the engine started

// ================================================================ the controls
static float s_tx0, s_ty0;  // the zero of the tilt (the position at the start)
static void tilt_zero(void)
{
    imu_set_level();
    imu_tilt_logical(&s_tx0, &s_ty0);
}
static int s_fire_req;      // a tap on the TETHER key (or BOOT, or the Watcher wheel button) since the last frame
static float s_wheel_turn;  // the Watcher wheel: a turn that it asked for (rad of the nose, used up over time)
static ctl_t human_ctl(void)
{
    ctl_t c = {0, 0, 0, 0};
    int16_t xs[INPUT_MAX_POINTS], ys[INPUT_MAX_POINTS];
    int n = input_points(xs, ys, INPUT_MAX_POINTS);
    bool burn = false, teth = false;
    int tl = 0, tr = 0;
    for (int i = 0; i < n; i++) {
        // (the points are physical: change them to the frame of the app)
        int lx, ly;
        gfx_touch_to_logical(xs[i], ys[i], &lx, &ly);
        if (in_box(box_tether(), lx, ly)) {
            teth = true;
        } else if (in_box(box_burn(), lx, ly)) {
            burn = true;
        } else if (s_ctl_mode == CTL_TOUCH && in_box(box_turn(0), lx, ly)) {
            tl = 1;
        } else if (s_ctl_mode == CTL_TOUCH && in_box(box_turn(1), lx, ly)) {
            tr = 1;
        }
    }
    c.burn = burn;
    s_teth_down = teth;
    if (s_ctl_mode == CTL_TOUCH) {
        c.turn = (float)(tr - tl);
    } else if (imu_ok()) {
        float tx, ty;
        imu_tilt_logical(&tx, &ty);
        float dx = tx - s_tx0, a = absf(dx);
        // (1 degree does nothing; full at about 8 degrees; a soft curve near the middle for fine turns)
        float k = clampf((a - 0.017f) / 0.12f, 0, 1);
        c.turn = (dx > 0 ? 1 : -1) * k * (0.35f + 0.65f * k);
    }
    // the Watcher wheel: each step turns the nose 8 degrees
    if (input_has_wheel()) {
        int w = input_wheel();
        if (w) {
            s_wheel_turn = clampf(s_wheel_turn + w * 0.14f, -1.2f, 1.2f);
        }
        if (absf(s_wheel_turn) > 0.01f && absf(c.turn) < 0.01f) {
            c.turn = clampf(s_wheel_turn * 4, -1, 1);
        }
        if (input_wheel_presses() > 0) {
            s_fire_req++;
        }
    }
    if (app_key_presses() > 0) {
        s_fire_req++;
    }
    c.fire = s_fire_req > 0;
    s_fire_req = 0;
    c.hold = s_teth_down || app_key_down();
    return c;
}
#define TURN_MAX 2.2f  // rad/s: the turn at full tilt (or a held turn key)

// the markers that the nose snaps to (world angles): forward, back; near a target: to it, and against the speed
// relative to it (a burn there stops the drift); the GUIDE marker (the way that the pilot wants)
enum {
    MK_PRO,
    MK_RETRO,
    MK_TO,
    MK_KILL,
    MK_GUIDE,
    MK_N
};
static float s_mk[MK_N];
static bool s_mk_on[MK_N];
static float s_u_rel;         // the speed of the tug relative to the target (m/s)
static float s_d_rel = 1e9f;  // the distance to the target hook (m)
static void markers(void)
{
    float vx, vy;
    com_vel(&vx, &vy);
    s_mk[MK_PRO] = atan2f(vy, vx);
    s_mk[MK_RETRO] = s_mk[MK_PRO] + PI_F;
    s_mk_on[MK_PRO] = s_mk_on[MK_RETRO] = true;
    s_mk_on[MK_TO] = s_mk_on[MK_KILL] = false;
    s_u_rel = 0;
    s_d_rel = 1e9f;
    if (s_tgt > 0 && s_tgt < s_nb && B[s_tgt].on && T.state != TH_ON) {
        double hx, hy;
        hook_world(&B[s_tgt], &hx, &hy);
        float rx = (float)((hx - B[0].x) * 1000.0), ry = (float)((hy - B[0].y) * 1000.0);
        float ux = (B[0].vx - B[s_tgt].vx) * 1000, uy = (B[0].vy - B[s_tgt].vy) * 1000;
        s_d_rel = fsqrt(rx * rx + ry * ry);
        s_u_rel = fsqrt(ux * ux + uy * uy);
        if (s_d_rel < 20000) {
            s_mk[MK_TO] = atan2f(ry, rx);
            s_mk_on[MK_TO] = true;
            if (s_u_rel > 0.15f) {
                s_mk[MK_KILL] = atan2f(-uy, -ux);
                s_mk_on[MK_KILL] = true;
            }
        }
    }
    s_mk_on[MK_GUIDE] = s_help != HELP_OFF && s_ap_aim_on && !s_auto;
    s_mk[MK_GUIDE] = s_ap_aim;
}
static float nose_err(float world)  // the nose against a world angle (rad, -pi..pi)
{
    float th = atan2f((float)B[0].y, (float)B[0].x);
    return wrap_pi(world - (th + PI_F / 2 + s_nose));
}
static void fire_tether(void);
static void release_tow(void);
// the GUIDE gates the engine in these states of the pilot: it burns only when the pilot wants
static bool guide_gates(void)
{
    if (s_help != HELP_GUIDE || s_auto) {
        return false;
    }
    return s_ap == AP_BURN || s_ap == AP_CLOSE || s_ap == AP_TOW || s_ap == AP_LAND_WAIT || s_ap == AP_LAND_BURN ||
           s_ap == AP_SAVE;
}
// the controls act: the turn (in real time), the snap, the engine (with its ramp), the tether
static void apply_ctl(float dt)
{
    ctl_t c = s_ctl;
    markers();
    // the turn: eased, so a tilt gives a smooth turn
    float want = c.turn * TURN_MAX;
    s_turn_v += (want - s_turn_v) * follow_k(dt, 0.08f);
    if (absf(c.turn) > 0.01f && s_wheel_turn != 0) {
        float used = s_turn_v * dt;
        if ((s_wheel_turn > 0) == (used > 0)) {
            s_wheel_turn = absf(used) >= absf(s_wheel_turn) ? 0 : s_wheel_turn - used;
        }
    }
    float th = atan2f((float)B[0].y, (float)B[0].x), nw = th + PI_F / 2 + s_nose;
    // the snap: a small turn and a marker near the nose: the nose goes to it and stays (a person turns it away). The
    // GUIDE marker catches from further away (about 14 degrees)
    if (absf(c.turn) < 0.12f) {
        int best = -1;
        float bd = s_snap >= 0 ? 0.30f : 0.16f;  // (about 9 degrees to catch, 17 to lose)
        for (int k = 0; k < MK_N; k++) {
            if (!s_mk_on[k]) {
                continue;
            }
            float d = absf(wrap_pi(s_mk[k] - nw)) * (k == MK_GUIDE ? 0.65f : 1);
            if (d < bd) {
                bd = d;
                best = k;
            }
        }
        s_snap = best;
        if (best >= 0) {
            float e = wrap_pi(s_mk[best] - nw);
            s_nose += clampf(e, -2.5f * dt, 2.5f * dt);
        }
    } else {
        s_snap = -1;
    }
    s_nose = wrap_pi(s_nose + s_turn_v * dt);
    // the engine: a finger on the BURN pad; the GUIDE lets it burn only at the right time (the auto stop and start)
    s_finger_burn = c.burn;
    s_gated = false;
    bool burn = c.burn && s_fuel > 0 && T.state != TH_FLY;
    if (burn && guide_gates() && !s_ap_wb) {
        burn = false;
        s_gated = true;
    }
    if (burn && s_ramp <= 0) {
        fx(FX_IGNITE, 60, 1);
    }
    s_ramp = burn ? fminf(1, s_ramp + dt / 0.3f) : 0;
    s_throttle = s_ramp;
    // the tether: a tap fires it; while towing, a hold of 1 s lets the tow go
    if (c.fire && T.state == TH_NONE) {
        fire_tether();
    }
    if (c.hold && T.state == TH_ON) {
        s_hold_t += dt;
        if (s_hold_t >= 1.0f) {
            release_tow();
            s_hold_t = 0;
        }
    } else {
        s_hold_t = 0;
    }
}

// ================================================================ the tether
static float s_say_t;  // a short message of the game (TOO FAR, MISSED ...)
static int s_say;
enum {
    SAY_NONE,
    SAY_FAR,
    SAY_MISS,
    SAY_SNAP,
    SAY_FAST,
    SAY_LOCK,
    SAY_FREE,
    SAY_NOFUEL,
    SAY_HOT,
    SAY_LETGO,
    SAY_NOTGT
};
static void say(int what)
{
    s_say = what;
    s_say_t = 2.2f;
}
static void fire_tether(void)
{
    if (T.state != TH_NONE) {
        return;
    }
    if (s_tgt <= 0 || s_tgt >= s_nb || !B[s_tgt].on || s_released[s_tgt]) {
        say(SAY_NOTGT);
        fx(FX_ERROR, 45, 1);
        return;
    }
    if (s_d_rel > TETHER_RANGE) {
        say(SAY_FAR);
        fx(FX_ERROR, 50, 1);
        return;
    }
    T.state = TH_FLY;
    T.to = (uint8_t)s_tgt;
    T.t = 0;
    T.px = 0;
    T.py = 0;
    T.flown = 0;
    T.missed = 0;
    T.snapped = 0;
    fx(FX_HARPOON, 70, 1);
    shake(1.5f);
}
static void release_tow(void)
{
    if (T.state != TH_ON) {
        return;
    }
    int i = T.to;
    T.state = TH_NONE;
    say(SAY_FREE);
    fx(FX_CLANK, 55, 0.8f);
    const mis_t *M = &MIS[s_mis];
    if (M->goal_kind == G_DELIVER2 || M->goal_kind == G_LAND) {
        s_released[i] = true;
    }
}

// ================================================================ the clock of the mission
// It runs fast when nothing happens (a coast far from the target), slow at the big moments (a burn near the target,
// the harpoon, the air, a burn that the GUIDE asks for).
static float next_event_t(void)
{
    // the mission seconds to the next thing that a person must see
    float t = 1e9f;
    if (s_ca_k >= 0 && T.state != TH_ON) {
        t = fminf(t, s_ca_t);
    }
    for (int i = 0; i < s_nb; i++) {
        if (s_path[i].air_at > 0) {
            t = fminf(t, s_path[i].air_at * s_pdt);
        }
    }
    // (the high and the low point of an orbit that is not round: the places of the burns; with the help of the pilot
    // only its own planned burns count)
    elem_t E = body_elem(0);
    if (E.e > 0.01f && E.period > 0 && s_help == HELP_OFF && !s_auto) {
        float n = TAU_F / E.period, nu = E.nu, to_ap = wrap_pi(PI_F - nu), to_pe = wrap_pi(-nu);
        if (to_ap < 0) {
            to_ap += TAU_F;
        }
        if (to_pe < 0) {
            to_pe += TAU_F;
        }
        // (a rough time: the mean motion)
        t = fminf(t, fminf(to_ap, to_pe) / n);
    }
    if (s_avoid > 0 && B[s_avoid].on) {
        // (the debris: its next pass)
        float dx = (float)(B[s_avoid].x - B[0].x), dy = (float)(B[s_avoid].y - B[0].y), d = fsqrt(dx * dx + dy * dy);
        t = fminf(t, d / 4.6f);
    }
    return t;
}
static void rate_step(float dt)
{
    float g;
    bool near = s_d_rel < 3000;
    if (s_throttle > 0) {
        g = s_close > 0.5f ? 6 : 14;
    } else if (T.state == TH_FLY || (T.state == TH_ON && T.t < 3)) {
        g = 3;
    } else if (B[0].in_air || (T.state == TH_ON && B[T.to].in_air)) {
        // (a pass through the air: slow when it is hot)
        float h = fmaxf(B[0].heat, T.state == TH_ON ? B[T.to].heat : 0);
        g = h > 0.3f ? 10 : 30;
    } else if (s_landed || (MIS[s_mis].goal_kind == G_LAND && s_released[1] && B[1].on)) {
        g = 25;
    } else {
        float base = s_close > 0.5f ? (near ? 4 : 8) : 30;
        float t = next_event_t();
        // (the next thing comes in about 4 real seconds; at most 240 times the real time)
        g = clampf((t - 60) / 4, base, 240);
        if (near) {
            // (a slow drift to the target: faster, but a person still sees it come)
            float tm = s_u_rel > 0.05f ? s_d_rel / s_u_rel : 1e9f;
            g = clampf(tm / 7, base, 40);
        }
    }
    if (s_avoid > 0 && B[s_avoid].on) {
        // HEAD-ON: the pass of the debris is a big moment: slow motion
        float dx = (float)(B[s_avoid].x - B[0].x), dy = (float)(B[s_avoid].y - B[0].y);
        if (dx * dx + dy * dy < 900) {
            g = fminf(g, 1.0f);
        }
    }
    if ((s_help == HELP_GUIDE || s_auto) && (s_state == ST_PLAY || s_demo)) {
        // the pilot plans (a short time): the clock almost stops, so the plan is still true at its end
        if (s_ap == AP_PLAN || s_ap == AP_FIX) {
            g = fminf(g, 1.0f);
        }
        // the next planned burn comes in about 3 real seconds
        if (s_ap_wait >= 0) {
            g = fminf(g, fmaxf(1.5f, s_ap_wait / 3));
        }
        // the pilot wants a burn and the engine is off: slow, so a person (or the autopilot) has the time to turn and to
        // hold BURN
        if (s_ap_wb && s_throttle <= 0) {
            g = fminf(g, 1.5f);
        }
    }
    if (s_state == ST_BRIEF || s_state == ST_DONE || s_state == ST_FAIL) {
        g = fminf(g, 6);
    }
    s_rate_goal = g;
    // (down at once, up slowly)
    if (g < s_rate) {
        s_rate += (g - s_rate) * follow_k(dt, 0.08f);
    } else {
        s_rate += (g - s_rate) * follow_k(dt, 0.9f);
    }
}

// ================================================================ the goals
static float zone_angle(double t)
{
    // the recovery ship: on the planet, it turns with it
    return MIS[s_mis].zone * (PI_F / 180) + PLANET_SPIN * (float)t;
}
static float dv_left(void)
{
    float m = tug_mass() + (T.state == TH_ON ? B[T.to].mass : 0);
    if (m - s_fuel <= 1) {
        return 0;
    }
    return TUG_VE * logf(m / (m - s_fuel));
}
static bool goal_inside(void)
{
    const mis_t *M = &MIS[s_mis];
    if (T.state != TH_ON) {
        return false;
    }
    elem_t E = body_elem(0);
    return E.rp >= R_PL + M->lo && E.ra > 0 && E.ra <= R_PL + M->hi;
}
// G_LAND: where the capsule comes down if it goes now (its own path, from the place of the tow)
static void land_predict(float *ia, float *it, int *ok)
{
    *ok = 0;
    *ia = 0;
    *it = 0;
    // (the capsule itself: after the let go it flies on with its own place and speed)
    int i = T.state == TH_ON ? T.to : 1;
    pstate_t p;
    p.x = B[i].x;
    p.y = B[i].y;
    p.vx = B[i].vx;
    p.vy = B[i].vy;
    p.m = B[i].mass;
    p.cda = B[i].cda;
    p.heat = 0;
    p.heat_k = B[i].heat_k;
    p.peak = 0;
    elem_t E = elements((float)p.x, (float)p.y, p.vx, p.vy);
    if (E.rp > R_PL + 64) {
        return;
    }
    float t = 0;
    for (int k = 0; k < 900 && t < 5000; k++) {
        float alt = fsqrt(sqf((float)p.x) + sqf((float)p.y)) - R_PL, h = alt < 70 ? 2.0f : 8.0f;
        pstep(&p, h);
        t += h;
        if (alt < GONE_ALT) {
            *ok = 1;
            *ia = atan2f((float)p.y, (float)p.x);
            *it = t;
            return;
        }
    }
}
static uint8_t s_stars[NMIS];
static int s_unlock;
static void stars_save(void);
static void fail(int why)
{
    if (s_state != ST_PLAY) {
        return;
    }
    s_state = ST_FAIL;
    s_state_t = 0;
    s_fail_why = why;
    s_throttle = 0;
    s_ramp = 0;
    jingle(ORM_J_FAIL, OR_J_FAIL_MS);
    if (why == F_TGT_BURN || why == F_TUG_BURN || why == F_DEBRIS) {
        fx(FX_BOOM, 85, 1);
        shake(6);
    }
}
static void done(void)
{
    if (s_state != ST_PLAY) {
        return;
    }
    const mis_t *M = &MIS[s_mis];
    s_state = ST_DONE;
    s_state_t = 0;
    s_throttle = 0;
    s_ramp = 0;
    s_result_fuel = s_fuel;
    s_result_time = s_clock;
    int st = 1;
    if (s_fuel >= M->star2) {
        st = 2;
    }
    if (s_fuel >= M->star3 && (float)s_clock <= M->time3) {
        st = 3;
    }
    if (s_help == HELP_AUTO) {
        st = 1;  // (the autopilot flew it)
    }
    s_result_stars = st;
    s_new_best = false;
    if (!s_demo) {
        s_new_best = st > s_stars[s_mis];
        if (s_new_best) {
            s_stars[s_mis] = (uint8_t)st;
        }
        if (s_mis + 1 > s_unlock && s_mis + 1 < NMIS) {
            s_unlock = s_mis + 1;
        }
        stars_save();
    }
    jingle(s_mis == NMIS - 1 ? ORM_J_ALL : ORM_J_DONE, s_mis == NMIS - 1 ? OR_J_ALL_MS : OR_J_DONE_MS);
    fx(FX_CONFIRM, 60, 1);
}
static void pilot_next(int tgt)
{
    s_ap_tgt = tgt;
    s_ap = AP_START;
    s_ap_plans = 0;
}
static void goal_step(float dt)
{
    const mis_t *M = &MIS[s_mis];
    // ---- the failures: a body of the mission or the tug burns up; the debris; no fuel and no way
    if (B[0].heat >= 1 || body_alt(&B[0]) < GONE_ALT) {
        fail(F_TUG_BURN);
        return;
    }
    for (int i = 1; i < s_nb; i++) {
        body_t *b = &B[i];
        if (!b->on || b->kind == K_DEBRIS) {
            continue;
        }
        bool down = body_alt(b) < GONE_ALT;
        if (M->goal_kind == G_LAND && b->kind == K_CAPSULE && b->heat_k < 1 && down) {
            // the capsule came down: near the ship or far from it
            if (!s_landed) {
                float miss = absf(wrap_pi(atan2f((float)b->y, (float)b->x) - zone_angle(s_clock)));
                s_landed = miss < 0.06f ? 1 : 2;  // (about 3.4 degrees: 36 km)
                s_land_t = 0;
                fx(FX_SPLASH, 70, 1);
            }
            if (T.state == TH_ON && T.to == i) {
                T.state = TH_NONE;
            }
            b->on = 0;
            continue;
        }
        if (b->heat >= 1 || down) {
            b->on = 0;
            if (T.state == TH_ON && T.to == i) {
                T.state = TH_NONE;
            }
            fail(F_TGT_BURN);
            return;
        }
    }
    if (s_avoid > 0 && B[s_avoid].on) {
        float dx = (float)(B[s_avoid].x - B[0].x), dy = (float)(B[s_avoid].y - B[0].y), d = fsqrt(dx * dx + dy * dy);
        if (d < s_debris_d) {
            s_debris_d = d;
        }
        if (d < 1.2f) {
            fail(F_DEBRIS);
            return;
        }
    }
    // ---- the goal
    switch (M->goal_kind) {
    case G_DOCK:
    case G_DODGE:
        if (T.state == TH_ON && T.to == s_tgt && T.t > 1.5f) {
            done();
        }
        break;
    case G_BAND:
        if (goal_inside()) {
            s_inside_t += dt;
            if (s_inside_t > 2.0f) {
                done();
            }
        } else {
            s_inside_t = 0;
        }
        break;
    case G_LIFT:
        if (T.state == TH_ON && T.L <= T.L_goal + 3) {
            elem_t E = body_elem(0);
            if (E.rp >= R_PL + M->lo) {
                s_inside_t += dt;
                if (s_inside_t > 1.5f) {
                    done();
                }
            } else {
                s_inside_t = 0;
            }
        }
        break;
    case G_DELIVER2:
        if (s_tgt == 1 && s_released[1]) {
            // (the first wreck let go: in the band = delivered, else it waits for the tug again)
            elem_t E = elements((float)B[1].x, (float)B[1].y, B[1].vx, B[1].vy);
            if (E.rp >= R_PL + M->lo && E.ra > 0 && E.ra <= R_PL + M->hi) {
                s_tgt = 2;
                pilot_next(2);
                say(SAY_LETGO);
                fx(FX_CONFIRM, 60, 1);
            } else {
                s_released[1] = false;
            }
        } else if (s_tgt == 2 && goal_inside()) {
            s_inside_t += dt;
            if (s_inside_t > 2.0f) {
                done();
            }
        } else {
            s_inside_t = 0;
        }
        break;
    case G_LAND:
        if (s_landed == 1) {
            // (the crew is down near the ship; the tug must not fall too)
            elem_t E = body_elem(0);
            s_land_t += dt;
            if (E.rp >= R_PL + 70 && s_land_t > 3) {
                done();
            }
        } else if (s_landed == 2) {
            s_land_t += dt;
            if (s_land_t > 3) {
                fail(F_FAR_LAND);
            }
        }
        if (s_released[1] && B[1].on && !s_landed) {
            // (a capsule that was let go and stays up: catch it again)
            elem_t E = elements((float)B[1].x, (float)B[1].y, B[1].vx, B[1].vy);
            if (E.rp > R_PL + 66) {
                s_released[1] = false;
            }
        }
        break;
    }
    // ---- no fuel: the end when nothing can change (no tow on a way through the air, no meeting ahead)
    if (s_fuel <= 0 && s_state == ST_PLAY) {
        bool hope = false;
        if (T.state != TH_ON && s_ca_k >= 0 && s_ca_d < 0.25f) {
            hope = true;
        }
        if (T.state == TH_ON && (s_path[0].air_at >= 0 || M->goal_kind == G_DOCK)) {
            hope = true;
        }
        if (s_landed || (M->goal_kind == G_LAND && s_released[1])) {
            hope = true;
        }
        if (T.state == TH_FLY) {
            hope = true;
        }
        s_nofuel_t = hope ? 0 : s_nofuel_t + dt;
        if (s_nofuel_t > 4) {
            fail(F_FUEL);
        }
    }
}

// ================================================================ the events of the physics: sound, shake, sparks
static float s_creak_t, s_winch_t, s_alarm_t;
static void events_step(float dt)
{
    if (ev_hit) {
        ev_hit = 0;
        sparks_at(T.px, T.py, 22, 50);
        fx(FX_CLANK, 75, 1);
        shake(3);
    }
    if (ev_lock) {
        ev_lock = 0;
        s_tow_rt = 0;
        cam_punch(0.6f);
        say(SAY_LOCK);
        fx(FX_LOCK, 80, 1);
        jingle(ORM_J_LOCK, OR_J_LOCK_MS);
        shake(4);
    }
    if (ev_miss) {
        ev_miss = 0;
        say(SAY_MISS);
        fx(FX_ERROR, 50, 1);
    }
    if (ev_snap) {
        ev_snap = 0;
        say(SAY_SNAP);
        fx(FX_SNAP, 85, 1);
        shake(5);
    }
    if (ev_bump) {
        ev_bump = 0;
        fxv(FX_BUMP, (int)clampf(30 + ev_bump_v * 12, 30, 90), 0.1f);
        shake(clampf(ev_bump_v * 1.2f, 1, 6));
        ev_bump_v = 0;
    }
    // the rope: a creak under a hard pull, the winch while it reels in
    s_creak_t -= dt;
    s_winch_t -= dt;
    s_alarm_t -= dt;
    if (T.state == TH_ON && T.tension > 9000 && s_creak_t <= 0) {
        fxv(FX_CREAK, (int)clampf(T.tension / 400, 25, 80), 0.15f);
        s_creak_t = 0.6f;
    }
    if (T.state == TH_ON && T.L > T.L_goal + 1 && s_winch_t <= 0) {
        fx(FX_WINCH, 40, 1);
        s_winch_t = 0.9f;
    }
    // the heat alarm
    float hot = B[0].heat;
    if (T.state == TH_ON) {
        hot = fmaxf(hot, B[T.to].heat);
    }
    if (hot > 0.7f && s_alarm_t <= 0) {
        fx(FX_ALARM, 55, 1);
        s_alarm_t = 1.4f;
        say(SAY_HOT);
    }
    if (s_say_t > 0) {
        s_say_t -= dt;
    }
}

// ================================================================ the start of a mission and the step of the play
static void mission_start(int m, bool demo)
{
    s_mis = clampi(m, 0, NMIS - 1);
    s_demo = demo;
    mission_bodies(s_mis);
    pilot_reset();
    s_tgt = 1;
    memset(s_released, 0, sizeof s_released);
    s_landed = 0;
    s_land_t = 0;
    s_inside_t = 0;
    s_rate = 4;
    s_rate_goal = 4;
    s_ramp = 0;
    s_hold_t = 0;
    s_turn_v = 0;
    s_snap = -1;
    s_nofuel_t = 0;
    s_debris_d = 1e9f;
    s_fail_why = F_NONE;
    s_npart = 0;
    s_shake = 0;
    s_say = SAY_NONE;
    s_say_t = 0;
    s_pred_t = 1;
    s_burn_wait_t = 0;
    s_free_dv = 0;
    s_fire_req = 0;
    s_wheel_turn = 0;
    // (the presses of the BOOT key and of the wheel button on the title must not fire the tether)
    (void)app_key_presses();
    if (input_has_wheel()) {
        (void)input_wheel_presses();
        (void)input_wheel();
    }
    s_ca_k = -1;
    s_auto = demo || s_help == HELP_AUTO;
    s_d_rel = 1e9f;
    s_u_rel = 0;
    s_state = demo ? ST_PLAY : ST_BRIEF;
    s_state_t = 0;
    markers();
    cam_reset();
    if (!demo) {
        tilt_zero();
        jingle(ORM_J_START, OR_J_START_MS);
    }
}
// the prediction for the view: the coming orbits (the far views) or the next minutes near the target (the close views)
static void predict_now(void)
{
    float hz;
    elem_t E = body_elem(0);
    if (s_close > 0.5f && T.state != TH_ON) {
        hz = 240;
    } else {
        hz = E.period > 0 ? clampf(E.period * 1.15f, 600, 6000) : 3000;
    }
    if (T.state == TH_ON && B[T.to].in_air) {
        hz = fminf(hz, 1500);
    }
    predict(hz);
    if (s_tgt > 0 && s_tgt < s_nb && B[s_tgt].on && T.state != TH_ON) {
        closest(s_tgt, 0);
    } else {
        s_ca_k = -1;
    }
}
// one frame of the play (dt: real seconds)
static void play_step(float dt)
{
    // the controls: the autopilot, or a person (with the hints of the pilot in the GUIDE help)
    ctl_t pc = {0, 0, 0, 0};
    if (s_auto || s_help == HELP_GUIDE) {
        pc = pilot_think();
    }
    if (s_auto) {
        s_ctl = pc;
        // (the hold of the pilot: the same 1 s hold as a person)
        s_teth_down = pc.hold;
    } else {
        s_ctl = human_ctl();
    }
    if (s_state == ST_BRIEF || s_state == ST_DONE || s_state == ST_FAIL) {
        s_ctl.burn = 0;
        s_ctl.fire = 0;
        s_ctl.hold = 0;
        s_ctl.turn = 0;
    }
    float vx0 = B[0].vx, vy0 = B[0].vy;
    apply_ctl(dt);
    // the GUIDE: a burn that it did not ask for (a person who tries a thing) makes it plan again; a burn that it asks for
    // and nobody does for a long time too
    if (s_help == HELP_GUIDE && !s_auto) {
        bool planned = s_ap == AP_BURN || s_ap == AP_CLOSE || s_ap == AP_TOW || s_ap == AP_LAND_WAIT ||
                       s_ap == AP_LAND_BURN || s_ap == AP_SAVE;
        if (s_throttle > 0 && !planned) {
            s_free_dv += TUG_THRUST * s_throttle / (tug_mass() + (T.state == TH_ON ? B[T.to].mass : 0)) * dt * s_rate;
        }
        if (s_throttle <= 0 && s_free_dv > 1.0f) {
            s_free_dv = 0;
            s_ap = AP_START;
            s_ap_plans = 1;
        }
        if (s_ap == AP_BURN && s_throttle <= 0) {
            s_burn_wait_t += dt;
            if (s_burn_wait_t > 14) {
                s_burn_wait_t = 0;
                s_ap = AP_START;
            }
        } else {
            s_burn_wait_t = 0;
        }
    }
    rate_step(dt);
    // the physics: the mission time of this frame
    float mdt = dt * s_rate;
    if (mdt > 30) {
        mdt = 30;
    }
    phys_advance(mdt);
    // the particles keep their place in space: the change of the speed of the tug
    parts_step(dt, (B[0].vx - vx0) * 1000, (B[0].vy - vy0) * 1000);
    // the effects of the engine: puffs at the nozzle; the side jets of a turn
    float nw = nose_world();
    if (s_throttle > 0 && rndf() < 0.4f) {
        float bx = -cosf(nw), by = -sinf(nw);
        part_add(P_SMOKE, bx * 14, by * 14, bx * (30 + rndf() * 20) + rnds() * 6, by * (30 + rndf() * 20) + rnds() * 6,
                 0.6f + rndf() * 0.5f, 1);
    }
    if (absf(s_turn_v) > 0.3f && rndf() < 0.35f) {
        float s = s_turn_v > 0 ? 1 : -1, px = -sinf(nw) * s, py = cosf(nw) * s;
        float fxp = cosf(nw) * 8, fyp = sinf(nw) * 8;
        part_add(P_PUFF, fxp - px * 5, fyp - py * 5, -px * 25 + rnds() * 5, -py * 25 + rnds() * 5, 0.35f, 1);
    }
    // the heat: embers behind a body in the air
    for (int i = 0; i < s_nb; i++) {
        if (!B[i].on || B[i].heat < 0.25f || rndf() > B[i].heat) {
            continue;
        }
        float x, y;
        rel_m(i, &x, &y);
        float vx = B[i].vx * 1000, vy = B[i].vy * 1000, v = fsqrt(vx * vx + vy * vy) + 1;
        part_add(P_EMBER, x + rnds() * 6, y + rnds() * 6, -vx / v * 40 + rnds() * 10, -vy / v * 40 + rnds() * 10,
                 0.4f + rndf() * 0.4f, 1);
    }
    events_step(dt);
    if (T.state == TH_ON) {
        s_tow_rt += dt;
    } else {
        s_tow_rt = 0;
    }
    s_burn_rt = s_throttle > 0 ? s_burn_rt + dt : 0;
    if (s_state == ST_PLAY) {
        goal_step(dt);
    }
    // the prediction (a few times a second; at once after a burn)
    s_pred_t += dt;
    if (s_pred_t > (s_throttle > 0 ? 0.15f : 0.3f)) {
        s_pred_t = 0;
        predict_now();
    }
    // the sound of the engine and of the air
    s_engine += (s_throttle - s_engine) * follow_k(dt, 0.12f);
    float air = 0;
    if (B[0].in_air) {
        air = clampf(B[0].heat * 1.5f + 0.15f, 0, 1);
    }
    drones(s_engine, air);
    if (s_shake > 0) {
        s_shake = fmaxf(0, s_shake - dt * 12);
    }
}
