// INK KARTS: the race (included by app_kart.c): the states (the title, the countdown, the race, the finish, the
// results), the input of the person (the tilt, the two halves of the screen, the BLOT button), the camera, the
// particles and the sounds, the frame list of the karts and of the ink, the HUD.

enum {
    GS_TITLE,
    GS_INTRO,
    GS_RACE,
    GS_FINISH,
    GS_RESULTS
};
static int s_gs;
static float s_gt;      // the time in the state now (s)
static float s_t;       // the time of the world (s; the slow motion makes it slower)
static float s_rt;      // the real time since the open (s)
static float s_acc;     // the time of the physics that is not done yet (s)
static float s_slowmo;  // the slow motion now (1 = none)
static bool s_he;       // Hebrew
static bool s_sound = true, s_tilt_on = true;
static int s_sel;         // the racer of the person (the title)
static int s_tsel;        // the track (the title)
static float s_tsteer;    // the steer of the touch (-1..1, it grows)
static float s_shake;     // the shake of the camera (0..1)
static float s_flash;     // the flash of the screen at a BLOT (0..1)
static int s_lap_shown;   // the lap that the banner shows (0: none)
static float s_banner_t;  // the time of the banner of a lap
static float s_word_t;    // the word of the ink (BOOST / SLICK) and its time
static int s_word, s_nword[3];
static int s_best_place, s_new_best;
static float s_wrong_t;  // the person drives the wrong way (s)
static int s_last_place;

// ---------------------------------------------------------------- the layout (output px)
#if KT_LAY == 1 && OH > 200
#define BTN_X  574
#define BTN_Y  290
#define BTN_R  48
#define F_BASE 360.0f
#elif KT_LAY == 1
#define BTN_X  574
#define BTN_Y  114
#define BTN_R  40
#define F_BASE 290.0f
#elif KT_LAY == 2
// (the round frame: the numbers are for the 412 frame; RS() scales them to the frame now)
#define RS(v)  ((int)((v) * OW / 412))
#define BTN_X  RS(302)
#define BTN_Y  RS(312)
#define BTN_R  RS(40)
#define F_BASE (310.0f * OW / 412.0f)
#else
#define BTN_X  274
#define BTN_Y  194
#define BTN_R  36
#define F_BASE 215.0f
#endif

#if KT_LAY == 2
#define CAM_H     2.0f  // the chase camera: its height, the look ahead, the height of the look point
#define CAM_AHEAD 3.4f
#define CAM_ATY   0.6f
#define CAM_TD    4.2f  // the turntable of the title: its radius and its height
#define CAM_TH    1.5f
#else
#define CAM_H     1.7f
#define CAM_AHEAD 4.2f
#define CAM_ATY   0.75f
#define CAM_TD    4.7f
#define CAM_TH    1.55f
#endif

// ---------------------------------------------------------------- the sounds
enum {
    FX_BLOT,
    FX_BUMP,
    FX_WALL,
    FX_BOOST,
    FX_SLICK,
    FX_LAP,
    FX_BEEP,
    FX_GO,
    FX_PICK,
    FX_WIN,
    FX_LOSE,
    FX_N
};
static const char *const FX_NAMES[FX_N] = {"blot", "bump", "wall", "boost", "slick", "lap",
                                           "beep", "go",   "pick", "win",   "lose"};
static void fx(int id, int vol, float rate)
{
    if (!s_sound) {
        return;
    }
    tfsfx_play(id, vol, rate);
}

// ---------------------------------------------------------------- the particles
enum {
    PT_GLOW,
    PT_DUST,
    PT_SPARK
};
typedef struct {
    v3 p, v;
    float life, max, size;
    uint8_t col, kind;
} part_t;
#define NPART 72
static part_t s_part[NPART];
static int s_part_next;
static void part_add(v3 p, v3 v, float life, float size, int col, int kind)
{
    part_t *q = &s_part[s_part_next];
    s_part_next = (s_part_next + 1) % NPART;
    q->p = p;
    q->v = v;
    q->life = q->max = life;
    q->size = size;
    q->col = (uint8_t)col;
    q->kind = (uint8_t)kind;
}
static float hrand(uint32_t *h)
{
    *h ^= *h << 13;
    *h ^= *h >> 17;
    *h ^= *h << 5;
    return (*h & 0xFFFF) / 65535.0f;
}
static uint32_t s_ph = 12345;
static void parts_step(float dt)
{
    for (int k = 0; k < NPART; k++) {
        part_t *q = &s_part[k];
        if (q->life <= 0) {
            continue;
        }
        q->life -= dt;
        q->p = vadd(q->p, vmul(q->v, dt));
        if (q->kind != PT_DUST) {
            q->v.y -= 9 * dt;
        }
        if (q->p.y < 0.05f) {
            q->p.y = 0.05f;
            q->v.y = 0;
            q->v = vmul(q->v, 0.8f);
        }
    }
}

// ---------------------------------------------------------------- the effects of the karts (kt_kart.inc calls them)
static inline bool near_me(const kart_t *k)
{
    float dx = k->x - K[s_me].x, dz = k->z - K[s_me].z;
    return dx * dx + dz * dz < 30 * 30;
}
static void fx_bump(kart_t *k, float hard)
{
    k->bump_t = 0;
    k->hits++;
    if (k == &K[s_me]) {
        s_shake = FMAX(s_shake, clampf(hard * 0.12f, 0.25f, 0.8f));
        fx(FX_BUMP, (int)clampf(50 + hard * 8, 50, 95), 0.9f + hrand(&s_ph) * 0.2f);
    }
    for (int i = 0; i < 4; i++) {
        part_add(V3(k->x, 0.5f, k->z), V3(hrand(&s_ph) * 4 - 2, 2 + hrand(&s_ph) * 2, hrand(&s_ph) * 4 - 2), 0.4f,
                 0.22f, AC_GOLD, PT_SPARK);
    }
}
static void fx_wall(kart_t *k, float hard)
{
    k->bump_t = 0;
    if (k == &K[s_me]) {
        s_shake = FMAX(s_shake, clampf(hard * 0.1f, 0.2f, 0.7f));
        fx(FX_WALL, (int)clampf(40 + hard * 6, 40, 85), 1.0f);
    }
    for (int i = 0; i < 3; i++) {
        part_add(V3(k->x, 0.4f, k->z), V3(hrand(&s_ph) * 3 - 1.5f, 1.5f + hrand(&s_ph) * 2, hrand(&s_ph) * 3 - 1.5f),
                 0.35f, 0.2f, AC_WHITE, PT_SPARK);
    }
}
static void fx_blot(kart_t *k)
{
    int col = AC_INK0 + k->racer;
    for (int i = 0; i < 14; i++) {
        float a = hrand(&s_ph) * TAU_F, sp = 2 + hrand(&s_ph) * 5;
        part_add(V3(k->x, 0.6f, k->z),
                 V3(fcos(a) * sp + k->vx * 0.4f, 3 + hrand(&s_ph) * 4, fsin(a) * sp + k->vz * 0.4f), 0.7f, 0.5f, col,
                 PT_GLOW);
    }
    if (k == &K[s_me]) {
        s_shake = FMAX(s_shake, 0.55f);
        s_flash = 1;
        fx(FX_BLOT, 95, 1.0f);
    } else if (near_me(k)) {
        fx(FX_BLOT, 55, 1.1f);
    }
}

// ---------------------------------------------------------------- the input of the person
static bool in_btn(int x, int y)
{
    int dx = x - BTN_X, dy = y - BTN_Y;
    return dx * dx + dy * dy <= (BTN_R + 8) * (BTN_R + 8);
}
// the steer of the person: the tilt (zero = the board at the start) and the touch (each half of the screen)
static float steer_of_person(float dt)
{
    float st = 0;
    if (s_tilt_on && imu_ok()) {
        float tx = 0, ty = 0;
        imu_tilt_logical(&tx, &ty);
        float a = absf(tx);
        // (1.5 degrees of dead zone, the full steer at about 12 degrees, a soft curve: small angles do something)
        float v = clampf((a - 0.026f) / (0.21f - 0.026f), 0, 1);
        v = v * (0.55f + 0.45f * v);
        st = tx < 0 ? -v : v;
    }
#if BOARD_WHEEL
    else {
        float tx = 0, ty = 0;
        imu_tilt_logical(&tx, &ty);  // (the Watcher: the wheel is a tilt to the side)
        st = clampf(tx * 4, -1, 1);
    }
#endif
    // the touch: a finger on the left half steers left, on the right half right (not on the BLOT button)
    int16_t px[INPUT_MAX_POINTS], py[INPUT_MAX_POINTS];
    int n = input_points(px, py, INPUT_MAX_POINTS);
    int want = 0;
    for (int q = 0; q < n; q++) {
        int x, y;
        gfx_touch_to_logical(px[q], py[q], &x, &y);
        if (in_btn(x, y)) {
            continue;
        }
        if (x < 0 || x >= OW || y < 0 || y >= OH) {
            continue;
        }
        want += x < OW / 2 ? -1 : 1;
    }
    float tw = (float)clampi(want, -1, 1);
    if (tw == 0) {
        s_tsteer += (0 - s_tsteer) * clampf(dt * 14, 0, 1);
    } else {
        s_tsteer = clampf(s_tsteer + tw * dt * 4.0f, -1, 1);
    }
    if (tw != 0 && s_tsteer * tw < 0) {
        s_tsteer = 0;
    }
    return clampf(st + s_tsteer, -1, 1);
}

// ---------------------------------------------------------------- the camera
static v3 s_ceye, s_cat;
static float s_cyaw, s_cdist, s_cf;  // (s_cf: the first camera_step sets it)
static int s_cam_hint;
static float s_cam_s;
static bool s_cam_init;
static void camera_step(float rdt)
{
    const kart_t *k = &K[s_me];
    float alpha = clampf(s_acc / STEP_DT, 0, 1);
    float kx = k->x0 + (k->x - k->x0) * alpha, kz = k->z0 + (k->z - k->z0) * alpha;
    float kyaw = k->yaw0 + wrap_pi(k->yaw - k->yaw0) * alpha;
    v3 kp = V3(kx, 0, kz);
    v3 eye, at;
    float f = F_BASE;
    if (s_gs == GS_TITLE) {
        // the turntable: a slow orbit close round the kart of the person
        float a = kyaw + PI_F * 0.75f + s_rt * 0.35f;
        eye = vadd(kp, V3(fsin(a) * CAM_TD, CAM_TH, fcos(a) * CAM_TD));
        at = vadd(kp, V3(0, 0.5f, 0));
        f = F_BASE * 1.05f;
        s_cyaw = kyaw;
    } else if (s_gs == GS_INTRO && s_gt < 3.2f) {
        // the fly-in: from high in front of the grid round to the place behind the kart
        float t = smooth01(s_gt / 3.2f);
        float a = kyaw + PI_F * (1 - t) * 0.9f + 0.35f * (1 - t);
        float r = 4.6f + (1 - t) * 9.0f, hgt = 1.8f + (1 - t) * 6.0f;
        eye = vadd(kp, V3(-fsin(a) * r, hgt, -fcos(a) * r));
        at = vadd(kp, V3(fsin(kyaw) * 4.5f * t, 0.8f, fcos(kyaw) * 4.5f * t));
        s_cyaw = kyaw;
        s_cdist = 4.6f;
    } else if (s_gs == GS_FINISH || s_gs == GS_RESULTS) {
        // the finish: a slow orbit round the kart of the person
        float a = kyaw + PI_F * 0.55f + s_gt * 0.45f;
        float r = s_gs == GS_FINISH ? 4.4f + s_gt * 0.4f : 6.2f;
        eye = vadd(kp, V3(fsin(a) * r, 1.5f + (s_gs == GS_RESULTS ? 0.9f : 0), fcos(a) * r));
        at = vadd(kp, V3(0, 0.7f, 0));
    } else {
        // the chase: a spring on the heading, back and up with the speed; nearer and wider on a BOOST
        float want = kyaw + k->slide * 0.35f;
        s_cyaw += wrap_pi(want - s_cyaw) * clampf(rdt * 4.5f, 0, 1);
        float v = absf(k->vf), bk = FMAX(k->boost_k, clampf(k->push_t * 2, 0, 1));
        float dist = 4.3f + v * 0.045f - bk * 0.5f;
        s_cdist += (dist - s_cdist) * clampf(rdt * 3, 0, 1);
        v3 fw = fwd_of(s_cyaw);
        eye = vadd(kp, V3(-fw.x * s_cdist, CAM_H + v * 0.018f - bk * 0.15f, -fw.z * s_cdist));
        at = vadd(kp, V3(fw.x * CAM_AHEAD, CAM_ATY, fw.z * CAM_AHEAD));
        f = F_BASE * (1 - 0.13f * bk);
        // (the SLICK: the view rocks a little)
        if (k->slick_k > 0.05f) {
            at = vadd(at, V3(fcos(s_rt * 9) * 0.25f * k->slick_k, 0, fsin(s_rt * 7) * 0.25f * k->slick_k));
        }
    }
    if (!s_cam_init) {
        s_ceye = eye;
        s_cat = at;
        s_cf = f;
        s_cam_init = true;
    }
    float kk = clampf(rdt * (s_gs == GS_RACE ? 14.0f : 6.0f), 0, 1);
    s_ceye = vlerp(s_ceye, eye, kk);
    s_cat = vlerp(s_cat, at, kk);
    s_cf += (f - s_cf) * clampf(rdt * 5, 0, 1);
    // the shake
    v3 sh = V3(0, 0, 0);
    if (s_shake > 0.01f) {
        sh = V3((hrand(&s_ph) - 0.5f) * s_shake * 0.35f, (hrand(&s_ph) - 0.5f) * s_shake * 0.25f,
                (hrand(&s_ph) - 0.5f) * s_shake * 0.35f);
        s_shake *= 1 - clampf(rdt * 6, 0, 1);
    }
    v3 e = vadd(s_ceye, sh);
    if (e.y < 0.4f) {
        e.y = 0.4f;
    }
    cam_set(e, vadd(s_cat, vmul(sh, 0.5f)), s_cf);
    float sd;
    tr_coords(e.x, e.z, &s_cam_hint, &s_cam_s, &sd);
}

// ---------------------------------------------------------------- the frame list: the karts, the ink, the effects
// (the start lights: the frame of the gantry)
static xf_t s_gantry;
static void kart_draw(int i, float alpha)
{
    const kart_t *k = &K[i];
    float x = k->x0 + (k->x - k->x0) * alpha, z = k->z0 + (k->z - k->z0) * alpha;
    float yaw = k->yaw0 + wrap_pi(k->yaw - k->yaw0) * alpha;
    v3 cc = xf_pt(&C.w2c, V3(x, 0.6f, z));
    if (!cam_sees(cc, 2.0f)) {
        return;
    }
    bool fine = i == s_me || cc.z < 5.0f;  // (the fine model: the kart of the person, and a rival very near)
    float bounce = k->bump_t < 0.3f ? fsin(k->bump_t / 0.3f * PI_F) * 0.12f : 0;
    float rumble = k->surf ? fsin(s_t * 40 + i) * 0.015f : 0;
    xf_t X;
    xf_make(&X, V3(x, bounce + rumble, z), yaw + k->slide, -0.02f * k->push_t, k->roll, V3(1, 1, 1));
    float flash = k->push_t > 0 ? 0.25f * k->push_t : 0;
    dl_model_s(&s_mod[M_KART0 + 2 * k->racer + (fine ? 0 : 1)], &X, 1, flash, fine ? i : -1);
    // the wheels: the front ones turn with the steer, all spin
    static const float WP[4][3] = {
            {-0.66f, 0.28f, 0.7f}, {0.66f, 0.28f, 0.7f}, {-0.67f, 0.3f, -0.72f}, {0.67f, 0.3f, -0.72f}};
    for (int w = 0; w < 4 && fine; w++) {
        xf_t L, W;
        float sc = w < 2 ? 1.0f : 1.08f;
        xf_make(&L, V3(WP[w][0], WP[w][1], WP[w][2]), w < 2 ? k->steer * 0.38f : 0, fine ? k->spin : 0, 0,
                V3(sc, sc, sc));
        xf_mul(&W, &X, &L);
        dl_model(&s_mod[M_WHEEL], &W, 1, 0);
    }
    dl_shadow(V3(x, 0.03f, z), 0.95f, 1.3f, yaw, 0.5f);
    // the glow of a BOOST at the motor, the flame of a push
    float bk = FMAX(k->boost_k, clampf(k->push_t * 1.5f, 0, 1));
    if (bk > 0.05f) {
        v3 f = fwd_of(yaw);
        v3 ex = V3(x - f.x * 1.15f, 0.35f, z - f.z * 1.15f);
        dl_glow(ex, 0.45f + 0.25f * bk + 0.08f * fsin(s_t * 30 + i), AC_INK0 + k->racer, (int)(200 * bk));
        dl_glow(ex, 0.18f, AC_WHITE, (int)(220 * bk));
    }
}
// the ink ribbons near the camera: each kart, each row from the camera to the far cut (1 m near, then 2 and 4 m), a
// quad that adds the light of its colour
static void ink_draw(void)
{
    if (!s_ink || s_gs == GS_TITLE) {
        return;
    }
    int r0 = (int)s_cam_s;  // (the rows behind the camera are out of the view)
    for (int k = 0; k < NKART; k++) {
        int col = AC_INK0 + K[k].racer;
        int pid[4] = {-1, -1, -1, -1}, pt = 0;
        bool p4 = false;
        float pd = 0;
        int r = r0;
        while (r < r0 + 104) {
            int step = r - r0 < 20 ? 1 : r - r0 < 44 ? 2 : 4;
            const ink_t *e = &s_ink[rwrap(r) * NKART + k];
            float age = e->t ? s_rtime - (e->t - 1) * 0.1f : 999;
            if (!e->t || age > INK_LIFE || age < 0) {
                pid[1] = -1;
                r += step;
                continue;
            }
            float d = e->d * 0.1f, hw = ink_hw(e);
            // (the strength: it fades in its last 10 s and with the distance; a new ribbon shines more. The ribbon of
            // the person stays bright (its lane); a ribbon of a rival is bright while it is wet, dim when it is dry)
            float base = k == s_me || age < INK_WET ? 175.0f : 60.0f;
            if (k != s_me && age >= INK_WET - 1.5f && age < INK_WET) {
                base = 60 + 90 * (INK_WET - age) / 1.5f;
            }
            float st = clampf((INK_LIFE - age) * 0.1f, 0, 1) * (base + (age < 1.2f ? (1.2f - age) * 70 : 0));
            st *= clampf(1 - (r - r0) * (1.0f / 112), 0, 1);
            // (near the camera a ribbon has soft edges: 4 corners across it, the outer ones faint)
            bool near = r - r0 < 18;
            float ew = near ? 0.16f : 0;
            int id[4];
            const float DD[4] = {-hw, -hw + ew, hw - ew, hw};
            for (int q = near ? 0 : 1; q < (near ? 4 : 3); q++) {
                v3 p = tr_point((float)r, d + DD[q]);
                p.y = 0.04f;
                id[q] = pv_add_cam(xf_pt(&C.w2c, p), (q == 0 || q == 3) ? st * 0.12f : st, 0, 0);
            }
            if (!near) {
                id[0] = id[1];
                id[3] = id[2];
            }
            // (a ribbon joins the one of the row before when both are from one pass of the kart)
            if (pid[1] >= 0 && absf(d - pd) < 2.5f && abs((int)e->t - pt) < 40) {
                dl_tri(pid[1], id[1], id[2], DK_ADD, col);
                dl_tri(pid[1], id[2], pid[2], DK_ADD, col);
                if (near && p4) {
                    dl_tri(pid[0], id[0], id[1], DK_ADD, col);
                    dl_tri(pid[0], id[1], pid[1], DK_ADD, col);
                    dl_tri(pid[2], id[2], id[3], DK_ADD, col);
                    dl_tri(pid[2], id[3], pid[3], DK_ADD, col);
                }
            }
            for (int q = 0; q < 4; q++) {
                pid[q] = id[q];
            }
            p4 = near;
            pd = d;
            pt = e->t;
            r += step;
        }
    }
}
static void effects_draw(void)
{
    // the particles
    for (int k = 0; k < NPART; k++) {
        const part_t *q = &s_part[k];
        if (q->life <= 0) {
            continue;
        }
        float a = q->life / q->max;
        dl_glow(q->p, q->size * (q->kind == PT_DUST ? 1 + (1 - a) * 1.5f : 1), q->col,
                (int)(a * (q->kind == PT_DUST ? 90 : 230)));
    }
    // the start lights: red one by one, then all green at GO
    if (s_gs == GS_INTRO || (s_gs == GS_RACE && s_gt < 1.2f)) {
        for (int q = 0; q < 3; q++) {
            v3 p = xf_pt(&s_gantry, V3(-1.6f + q * 1.6f, 5.6f, -0.45f));
            bool go = s_gs == GS_RACE;
            bool on = go || s_gt > 1.0f + q * 1.0f;
            if (!on) {
                continue;
            }
            dl_glow(p, 0.9f, go ? 5 /* lime */ : 3 /* magenta-red */, 230);
            dl_glow(p, 0.35f, AC_WHITE, 240);
        }
    }
    // the lamps of the bay
    for (int i = 0; i < s_nlamp; i++) {
        v3 c = xf_pt(&C.w2c, s_lamp[i]);
        if (c.z < 1 || c.z > 80) {
            continue;
        }
        dl_glow(s_lamp[i], 1.4f, i & 1 ? AC_INK0 + 1 : AC_INK0, 140);
        dl_glow(s_lamp[i], 0.45f, AC_WHITE, 200);
    }
}

// ---------------------------------------------------------------- the race
static float s_go_beep;
static void race_setup(void)
{
    if (s_track != s_tsel) {
        track_build(s_tsel);
    }
    karts_reset(s_sel);
    ink_clear();
    memset(s_part, 0, sizeof s_part);
    s_rtime = 0;
    s_acc = 0;
    s_slowmo = 1;
    s_lap_shown = 0;
    s_banner_t = 99;
    s_word_t = 99;
    memset(s_nword, 0, sizeof s_nword);
    s_cam_init = false;
    s_cam_hint = 0;
    s_wrong_t = 0;
    s_new_best = 0;
    xf_make(&s_gantry, tr_point(0.6f, 0), tr_yaw(0.6f), 0, 0, V3(1, 1, 1));
}
static void music_to(int track);
static void race_start(void)
{
    race_setup();
    s_gs = GS_INTRO;
    s_gt = 0;
    s_go_beep = 0;
    imu_set_level();
    music_to(-1);
}
static void race_results(void)
{
    // the karts that did not finish: their time from their distance to the line
    for (int i = 0; i < NKART; i++) {
        kart_t *k = &K[i];
        if (k->done) {
            continue;
        }
        float left = LAPS * s_ntr - k->prog;
        k->done = true;
        k->t_done = s_rtime + left / 18.0f;
    }
    karts_places();
    char key[16];
    snprintf(key, sizeof key, KT_KEY "b%d", s_tsel);
    int old = (int)store_get(key, 0);
    int pl = K[s_me].place;
    if ((!old || pl < old) && !s_demo) {
        store_set(key, (uint32_t)pl);
        s_new_best = old ? 1 : 0;
    }
    s_best_place = old && old < pl ? old : pl;
}
static void race_update(float rdt)
{
    float dt = rdt * s_slowmo;
    s_gt += rdt;
    s_t += dt;
    s_wtime = s_t;
    bool racing = s_gs == GS_RACE || s_gs == GS_FINISH || s_gs == GS_RESULTS;
    kart_t *me = &K[s_me];
    if (s_gs == GS_INTRO) {
        // the countdown: three beeps, then GO
        int beat = (int)(s_gt - 1.0f);
        if (s_gt >= 1.0f && beat >= (int)s_go_beep && beat < 3) {
            fx(FX_BEEP, 80, 1.0f);
            s_go_beep = (float)(beat + 1);
        }
        if (s_gt >= 4.0f) {
            s_gs = GS_RACE;
            s_gt = 0;
            fx(FX_GO, 90, 1.0f);
            music_to(KTM_RACE);
            imu_set_level();
        }
    }
    me->steer_in = (s_gs == GS_RACE) ? steer_of_person(rdt) : 0;
    s_acc += dt;
    if (s_acc > 0.1f) {
        s_acc = 0.1f;
    }
    while (s_acc >= STEP_DT) {
        s_acc -= STEP_DT;
        if (racing) {
            s_rtime += STEP_DT;
        }
        for (int i = 0; i < NKART; i++) {
            kart_step(&K[i], STEP_DT, racing);
        }
        if (racing) {
            karts_collide();
        }
        // the laps and the finish
        for (int i = 0; i < NKART; i++) {
            kart_t *k = &K[i];
            if (k->done || !racing) {
                continue;
            }
            int lap = (int)ifloor(k->prog / s_ntr) + 1;
            if (k->prog >= LAPS * s_ntr) {
                k->done = true;
                k->t_done = s_rtime;
                if (i == s_me) {
                    karts_places();
                    s_gs = GS_FINISH;
                    s_gt = 0;
                    s_slowmo = 0.35f;
                    music_to(-1);
                    fx(me->place <= 3 ? FX_WIN : FX_LOSE, 90, 1.0f);
                }
            } else if (i == s_me && lap > 1 && lap > s_lap_shown) {
                s_lap_shown = lap;
                s_banner_t = 0;
                fx(FX_LAP, 85, lap == LAPS ? 1.25f : 1.0f);
            }
        }
    }
    if (s_lap_shown == 0 && s_gs == GS_RACE) {
        s_lap_shown = 1;
    }
    if (racing) {
        karts_places();
    }
    // the words of the ink (the first times only): BOOST, SLICK
    if (s_gs == GS_RACE) {
        int w = me->inkst;
        if (w && w != s_word && s_word_t > 1.6f && s_nword[w] < 3) {
            s_word = w;
            s_word_t = 0;
            s_nword[w]++;
            fx(w == 1 ? FX_BOOST : FX_SLICK, 70, 1.0f);
        } else if (w && w != s_word && s_word_t > 1.6f) {
            s_word = w;
            fx(w == 1 ? FX_BOOST : FX_SLICK, 45, 1.0f);
        }
        if (!w && s_word_t > 0.6f) {
            s_word = 0;
        }
        // the wrong way: the kart looks back along the road
        float da = absf(wrap_pi(me->yaw - tr_yaw(me->s)));
        s_wrong_t = da > 1.9f ? s_wrong_t + rdt : 0;
    }
    s_word_t += rdt;
    s_banner_t += rdt;
    // the finish: the slow motion ends, then the results
    if (s_gs == GS_FINISH) {
        if (s_gt > 1.4f) {
            s_slowmo += (1 - s_slowmo) * clampf(rdt * 3, 0, 1);
        }
        if (s_gt > 4.6f) {
            race_results();
            s_gs = GS_RESULTS;
            s_gt = 0;
            s_slowmo = 1;
            music_to(KTM_TITLE);
        }
    }
    // the dust and the sparks of the person and of the karts near it
    for (int i = 0; i < NKART; i++) {
        kart_t *k = &K[i];
        if (!racing || !near_me(k)) {
            continue;
        }
        if (k->surf == 2 && k->vf > 5 && hrand(&s_ph) < rdt * 14) {
            part_add(V3(k->x, 0.3f, k->z), V3(hrand(&s_ph) - 0.5f, 0.8f, hrand(&s_ph) - 0.5f), 0.7f, 0.35f, AC_WHITE,
                     PT_DUST);
        }
        if (absf(k->slide) > 0.12f && hrand(&s_ph) < rdt * 22) {
            v3 f = fwd_of(k->yaw), rt = V3(f.z, 0, -f.x);
            float sd = hrand(&s_ph) < 0.5f ? -0.66f : 0.66f;
            part_add(V3(k->x - f.x * 0.8f + rt.x * sd, 0.15f, k->z - f.z * 0.8f + rt.z * sd),
                     V3(-f.x * 2 + hrand(&s_ph) - 0.5f, 1.5f, -f.z * 2 + hrand(&s_ph) - 0.5f), 0.3f, 0.14f, AC_GOLD,
                     PT_SPARK);
        }
        if (k->blot_t > 0 && hrand(&s_ph) < rdt * 20) {
            part_add(V3(k->x, 0.5f, k->z), V3(hrand(&s_ph) * 2 - 1, 1.2f, hrand(&s_ph) * 2 - 1), 0.5f, 0.32f,
                     AC_INK0 + k->racer, PT_GLOW);
        }
    }
    parts_step(dt);
    if (s_flash > 0) {
        s_flash -= rdt * 3;
    }
    // the motor of the person (a drone: its pitch follows the speed)
    if (s_sound && (s_gs == GS_RACE || s_gs == GS_INTRO)) {
        float v = absf(me->vf);
        audio_drone(0, 48 + v * 4.2f + me->boost_k * 18, 16 + (int)(v * 0.7f), W_SAW);
        audio_drone(1, 96 + v * 8.0f + me->boost_k * 40, 6 + (int)(v * 0.3f), W_TRI);
    } else {
        audio_drone(0, 0, 0, W_SAW);
        audio_drone(1, 0, 0, W_TRI);
    }
}

// ---------------------------------------------------------------- the HUD (shapes into the tiles, text onto the frame)
// a string of the layout: English or Hebrew
#define TR(en, he) (s_he ? (he) : (en))
static uint32_t ink_hex(int racer)
{
    const uint8_t *c = ADD_COL[AC_INK0 + racer];
    return (uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | c[2];
}
// the rects of the title (output px): the arrows, the track, START
#if KT_LAY == 1 && OH > 200
#define T_AL_X 190
#define T_AR_X 450
#define T_A_Y  200
#define T_TR_X 540
#define T_TR_Y 70
#define T_ST_X 540
#define T_ST_Y 290
#define T_W    170
#elif KT_LAY == 1
#define T_AL_X 200
#define T_AR_X 440
#define T_A_Y  98
#define T_TR_X 552
#define T_TR_Y 46
#define T_ST_X 552
#define T_ST_Y 124
#define T_W    156
#elif KT_LAY == 2
#define T_AL_X RS(70)
#define T_AR_X RS(342)
#define T_A_Y  RS(214)
#define T_TR_X RS(206)
#define T_TR_Y RS(306)
#define T_ST_X RS(206)
#define T_ST_Y RS(364)
#define T_W    RS(168)
#else
#define T_AL_X 36
#define T_AR_X 284
#define T_A_Y  120
#define T_TR_X 82
#define T_TR_Y 214
#define T_ST_X 236
#define T_ST_Y 214
#define T_W    140
#endif
#if KT_LAY == 2
#define T_AR_R RS(32)  // (the round frame: 64 px of 412 = 5.5 mm on the 1.75C)
#define T_H    RS(52)
#else
#define T_AR_R 26
#define T_H    44
#endif
static bool in_rect(int x, int y, int cx, int cy, int w, int h)
{
    return x >= cx - w / 2 - 6 && x < cx + w / 2 + 6 && y >= cy - h / 2 - 6 && y < cy + h / 2 + 6;
}
static bool in_disc(int x, int y, int cx, int cy, int r)
{
    return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= (r + 8) * (r + 8);
}
// a pill: one line with round ends (one shape: no darker overlap)
static void hud_pill(float cx, float cy, float w, float h, uint32_t hex, int a)
{
    float r = h * 0.5f;
    hud_line(cx - w / 2 + r, cy, cx + w / 2 - r, cy, h, hex, a);
}
static void arrow_shape(float cx, float cy, int dir)
{
    hud_disc(cx, cy, T_AR_R, 0x101830, 150);
    hud_ring(cx, cy, T_AR_R, 3, 0xFFFFFF, 220);
    hud_line(cx - dir * 5, cy - 10, cx + dir * 6, cy, 5, 0xFFFFFF, 255);
    hud_line(cx + dir * 6, cy, cx - dir * 5, cy + 10, 5, 0xFFFFFF, 255);
}
static void title_shapes(void)
{
    arrow_shape(T_AL_X, T_A_Y, -1);
    arrow_shape(T_AR_X, T_A_Y, 1);
    hud_pill(T_TR_X, T_TR_Y, T_W, T_H, 0x101830, 170);
    float pa = 0.75f + 0.25f * fsin(s_rt * 4);
    hud_pill(T_ST_X, T_ST_Y, T_W, T_H + 4, ink_hex(s_sel), (int)(255 * pa));
}
// a text with a dark edge (it stays clear over the 3D)
static void txt_c(const kfont_t *f, int cx, int base, const char *s, uint16_t col)
{
    uint16_t dk = rgb(10, 12, 30);
    ui_text_c(f, cx + 1, base + 2, s, dk);
    ui_text_c(f, cx, base, s, col);
}
static void title_text(void)
{
    const racer_t *r = &RACERS[s_sel];
#if KT_LAY == 2
    int ty = RS(92), ny = RS(268);
#elif KT_LAY == 1 && OH > 200
    int ty = 60, ny = 330;
#elif KT_LAY == 1
    int ty = 36, ny = 164;
#else
    int ty = 36, ny = 170;
#endif
    if (s_he) {
        txt_c(&KF_TITLE, KT_LAY == 1 ? 120 : OW / 2, ty, "קארטי דיו", rgb(255, 255, 255));
    } else {
        mf_text_oc(&MF_N34, KT_LAY == 1 ? 120 : OW / 2, ty, "INK KARTS", rgb(255, 255, 255), rgb(20, 24, 60), 2, 3);
    }
    // (the line under the name of the game: the six inks)
    txt_c(&KF_BIG, OW / 2, ny, r->name[s_he], rgb(255, 255, 255));
    const theme_t *th = &THEMES[s_tsel];
    const kfont_t *fs[3] = {&KF_BIG, &KF_NAME, &KF_LABEL};
    const kfont_t *tf = fs[2];
    for (int q = 0; q < 3; q++) {
        if (ui_text_w(fs[q], th->name[s_he]) <= T_W - 24) {
            tf = fs[q];
            break;
        }
    }
    txt_c(tf, T_TR_X, T_TR_Y + 7, th->name[s_he], rgb(255, 240, 200));
    txt_c(&KF_BIG, T_ST_X, T_ST_Y + 8, TR("RACE", "סע!"), rgb(255, 255, 255));
    // the best place on this track
    char key[16];
    snprintf(key, sizeof key, KT_KEY "b%d", s_tsel);
    int best = (int)store_get(key, 0);
    if (best) {
        char b[24];
        snprintf(b, sizeof b, "%s %d/6", TR("BEST", "שיא"), best);
#if KT_LAY == 2
        txt_c(&KF_LABEL, OW / 2, ty + 28, b, rgb(255, 214, 120));  // (the round frame: under the name of the game)
#else
        txt_c(&KF_LABEL, T_TR_X, T_TR_Y - T_H / 2 - 6, b, rgb(255, 214, 120));
#endif
    }
}
// the HUD of the race
static void race_shapes(void)
{
    const kart_t *me = &K[s_me];
    uint32_t ih = ink_hex(me->racer);
    // the BLOT button: the meter as an arc of the ink; full = a pulse of the ink and a drop
    bool full = me->meter >= 1;
    float pu = 0.5f + 0.5f * fsin(s_rt * 7);
    hud_disc(BTN_X, BTN_Y, BTN_R, full ? ih : 0x101830, full ? (int)(170 + 85 * pu) : 130);
    hud_ring(BTN_X, BTN_Y, BTN_R + 1.5f, 2.5f, 0x101830, 140);
    hud_ring(BTN_X, BTN_Y, BTN_R, 3, 0xFFFFFF, full ? 255 : 160);
    if (!full) {
        float k = clampf(me->meter, 0, 1), a0 = -PI_F * 0.5f, a1 = a0 + TAU_F * k, rr = BTN_R - 8;
        int n = (int)(14 * k) + 1;
        for (int i = 0; i < n && k > 0.01f; i++) {
            float u0 = a0 + (a1 - a0) * i / n, u1 = a0 + (a1 - a0) * (i + 1) / n;
            hud_line(BTN_X + fcos(u0) * rr, BTN_Y + fsin(u0) * rr, BTN_X + fcos(u1) * rr, BTN_Y + fsin(u1) * rr, 6, ih,
                     255);
        }
    }
    // (the drop)
    float dr = BTN_R * 0.3f;
    uint32_t dc = full ? 0xFFFFFF : 0x8890B0;
    hud_disc(BTN_X, BTN_Y + dr * 0.35f, dr, dc, 255);
    hud_line(BTN_X - dr * 0.72f, BTN_Y - dr * 0.05f, BTN_X, BTN_Y - dr * 1.6f, dr * 0.55f, dc, 255);
    hud_line(BTN_X + dr * 0.72f, BTN_Y - dr * 0.05f, BTN_X, BTN_Y - dr * 1.6f, dr * 0.55f, dc, 255);
    // the BOOST: lines of speed from the edges; the SLICK: a band of the rival colour at the sides
    float bk = FMAX(me->boost_k, clampf(me->push_t * 2, 0, 1));
    if (bk > 0.1f && s_gs == GS_RACE) {
        uint32_t h = (uint32_t)(s_rt * 60) * 2654435761u;
        for (int q = 0; q < 7; q++) {
            h = h * 1103515245u + 12345u;
            float a = (h >> 8 & 1023) / 1024.0f * TAU_F;
            float cx = OW * 0.5f, cy = OH * 0.52f, r0 = OW * (KT_LAY == 1 ? 0.30f : 0.36f),
                  r1 = OW * (KT_LAY == 1 ? 0.48f : 0.52f);
            float sy = KT_LAY == 1 ? 0.3f : 1.0f;
            hud_line(cx + fcos(a) * r0, cy + fsin(a) * r0 * sy, cx + fcos(a) * r1, cy + fsin(a) * r1 * sy, 2.5f,
                     0xFFFFFF, (int)(150 * bk));
        }
    }
    if (me->slick_k > 0.1f) {
        float age;
        int o = ink_at(me->s + 0.9f, me->d, &age);
        uint32_t rh = o >= 0 ? ink_hex(K[o].racer) : 0xFFFFFF;
        // (a soft band: three steps that fade toward the middle)
        for (int q = 0; q < 3; q++) {
            int a = (int)((70 - q * 20) * me->slick_k);
            float w = OW * (0.025f + q * 0.02f);
            hud_rect(0, 0, w, OH, rh, a / 2);
            hud_rect(OW - w, 0, w, OH, rh, a / 2);
        }
    }
    if (s_flash > 0) {
        hud_rect(0, 0, OW, OH, ih, (int)(90 * s_flash));
    }
    // (the place: a soft dark disc behind the number; not in the countdown)
    if (s_gs == GS_INTRO) {
        return;
    }
#if KT_LAY == 2
    hud_disc(OW / 2, RS(70), RS(34), 0x0A0C20, 120);
#elif KT_LAY == 1
    hud_disc(44, 40, 34, 0x0A0C20, 120);
#else
    hud_disc(36, 36, 30, 0x0A0C20, 120);
#endif
    // the wrong way: a big turn arrow
    if (s_wrong_t > 1.2f) {
        float cx = OW / 2, cy = OH * 0.4f, r = 22;
        hud_ring(cx, cy, r, 6, 0xFFD040, 230);
        hud_line(cx + r, cy, cx + r + 9, cy - 10, 6, 0xFFD040, 230);
        hud_line(cx + r, cy, cx + r - 11, cy - 7, 6, 0xFFD040, 230);
    }
}
static const char *place_sfx(int p)
{
    return p == 1 ? "st" : p == 2 ? "nd" : p == 3 ? "rd" : "th";
}
static void race_text(void)
{
    const kart_t *me = &K[s_me];
    char b[32];
#if KT_LAY == 2
    int px = OW / 2, py = RS(84), lx = OW / 2, ly = RS(128);
#elif KT_LAY == 1
    int px = 44, py = 54, lx = 44, ly = 96;
#else
    int px = 36, py = 50, lx = 36, ly = 88;
#endif
    if (s_gs == GS_RACE || s_gs == GS_FINISH) {
        snprintf(b, sizeof b, "%d", me->place);
        bool lap_on = s_gs == GS_RACE;
        // (plain text: the dark disc of the HUD is behind it; an outline costs many passes over the frame)
        mf_text_c(&MF_N34, px, py, b, me->place == 1 ? rgb(255, 214, 80) : rgb(255, 255, 255), 255, 1);
        if (!s_he) {
            mf_text(&MF_SMALL, px + (me->place == 1 ? 9 : 11), py - 20, place_sfx(me->place), rgb(255, 255, 255), 255,
                    1);
        }
        int lap = clampi((int)ifloor(me->prog / s_ntr) + 1, 1, LAPS);
        snprintf(b, sizeof b, "%s %d/%d", TR("LAP", "הקפה"), lap, LAPS);
        if (lap_on) {
            txt_c(&KF_LABEL, lx, ly, b, rgb(255, 255, 255));
        }
    }
    // the countdown
    if (s_gs == GS_INTRO && s_gt >= 1.0f) {
        int n = 3 - (int)(s_gt - 1.0f);
        if (n >= 1 && n <= 3) {
            snprintf(b, sizeof b, "%d", n);
            mf_text_oc(&MF_N34, OW / 2, OH / 2 + 4, b, rgb(255, 255, 255), rgb(10, 12, 30), 3, 1);
        }
    }
    if (s_gs == GS_RACE && s_gt < 0.9f) {
        if (s_he) {
            txt_c(&KF_TITLE, OW / 2, OH / 2 + 6, "סע!", rgb(140, 255, 90));
        } else {
            mf_text_oc(&MF_N34, OW / 2, OH / 2 + 6, "GO!", rgb(140, 255, 90), rgb(10, 30, 10), 3, 2);
        }
    }
    // the banner of a lap
    if (s_gs == GS_RACE && s_banner_t < 2.0f && s_lap_shown > 1) {
        if (s_lap_shown == LAPS) {
            snprintf(b, sizeof b, "%s", TR("FINAL LAP", "הקפה אחרונה"));
        } else {
            snprintf(b, sizeof b, "%s %d", TR("LAP", "הקפה"), s_lap_shown);
        }
        txt_c(&KF_BIG, OW / 2, OH * 0.34f, b, rgb(255, 230, 140));
    }
    // the word of the ink (the first times): BOOST in the own colour, SLICK in the colour of the rival
    if (s_gs == GS_RACE && s_word_t < 1.1f && s_word) {
        const uint8_t *c = ADD_COL[AC_INK0 + me->racer];
        uint16_t col = s_word == 1 ? rgb(c[0], c[1], c[2]) : rgb(255, 255, 255);
        txt_c(&KF_BIG, OW / 2, (int)(OH * (KT_LAY == 1 ? 0.3f : 0.36f)),
              s_word == 1 ? TR("BOOST!", "טורבו!") : TR("SLICK!", "מחליק!"), col);
    }
    if (s_gs == GS_FINISH && s_gt > 0.3f) {
        txt_c(&KF_TITLE, OW / 2, OH * 0.3f, TR("FINISH!", "סיום!"), rgb(255, 255, 255));
    }
}
// the results: the places, the names, the times
// a text from its left end (only names of the ABI of the app files: ui_text_c and ui_text_w)
static void txt_l(const kfont_t *f, int x, int base, const char *s, uint16_t col)
{
    ui_text_c(f, x + ui_text_w(f, s) / 2, base, s, col);
}
static void results_shapes(void)
{
#if KT_LAY == 1 && OH < 200
    hud_dark(150, 8, 340, 156, 150);
#elif KT_LAY == 2
    hud_dark(RS(76), RS(96), RS(260), RS(220), 150);
#else
    hud_dark(OW / 2 - 140, 30, 280, OH - 60, 150);
#endif
    hud_pill(T_ST_X, T_ST_Y, T_W, T_H, 0x101830, 190);
}
static void results_text(void)
{
    int order[NKART];
    for (int i = 0; i < NKART; i++) {
        order[K[i].place - 1] = i;
    }
#if KT_LAY == 1 && OH < 200
    int x0 = 162, y0 = 64, dy = 36, cols = 2, cw = 166, rows = 3;
#elif KT_LAY == 2
    int x0 = RS(92), y0 = RS(128), dy = RS(30), cols = 1, cw = RS(230), rows = 6;
#else
    int x0 = OW / 2 - 124, y0 = 62, dy = (OH - 100) / 6, cols = 1, cw = 248, rows = 6;
#endif
    (void)cols;
    for (int p = 0; p < NKART; p++) {
        const kart_t *k = &K[order[p]];
        int cx = x0 + (p / rows) * cw, cy = y0 + (p % rows) * dy;
        char b[32];
        uint16_t col = order[p] == s_me ? rgb(255, 230, 120) : rgb(235, 240, 255);
        const uint8_t *ic = ADD_COL[AC_INK0 + k->racer];
        snprintf(b, sizeof b, "%d", p + 1);
        txt_l(&KF_LABEL, cx, cy, b, rgb(ic[0], ic[1], ic[2]));
        txt_l(&KF_LABEL, cx + 22, cy, RACERS[k->racer].name[s_he], col);
        int ts = (int)(k->t_done * 10);
        snprintf(b, sizeof b, "%d:%02d.%d", ts / 600, (ts / 10) % 60, ts % 10);
        txt_l(&KF_LABEL, cx + cw - 76, cy, b, col);
    }
#if KT_LAY == 1 && OH < 200
    txt_c(&KF_NAME, OW / 2, 26, s_new_best ? TR("NEW BEST!", "שיא חדש!") : THEMES[s_tsel].name[s_he],
          rgb(255, 230, 140));
#elif KT_LAY == 2
    txt_c(&KF_NAME, OW / 2, RS(118), s_new_best ? TR("NEW BEST!", "שיא חדש!") : THEMES[s_tsel].name[s_he],
          rgb(255, 230, 140));
#else
    txt_c(&KF_NAME, OW / 2, 50, s_new_best ? TR("NEW BEST!", "שיא חדש!") : THEMES[s_tsel].name[s_he],
          rgb(255, 230, 140));
#endif
    txt_c(&KF_BIG, T_ST_X, T_ST_Y + 8, TR("OK", "אישור"), rgb(255, 255, 255));
}
