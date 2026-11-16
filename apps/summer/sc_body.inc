// SUMMER CUP: the rider (included by app_summer.c). One body for all events: a pose from a few values (crouch, tuck,
// lean, the arms), the gear (a scooter, a surfboard, a skimboard), the drawing with soft capsules, and a ragdoll
// (Verlet points with sticks) for each wipeout. The body frame: the feet on the board at (0, 0), +y up, the rider
// looks to +x (a mirror for the other way).

enum {
    J_HEAD,
    J_NECK,
    J_HIP,
    J_KNF,
    J_FTF,
    J_KNB,
    J_FTB,
    J_ELF,
    J_HDF,
    J_ELB,
    J_HDB,
    J_N
};
enum {
    GEAR_SCOOTER,
    GEAR_SURF,
    GEAR_SKIM
};
enum {
    ARMS_BAR,
    ARMS_OUT,
    ARMS_PADDLE,
    ARMS_RUN,
    ARMS_UP
};

typedef struct {
    float x, y;
} v2;
static inline v2 V2(float x, float y)
{
    v2 r = {x, y};
    return r;
}

typedef struct {
    float crouch, tuck, lean;  // 0..1, 0..1, -1..1
    int arms;                  // ARMS_*
    float phase;               // the run or the paddle cycle (rad)
    float wave;                // the arms swing (0..1)
    int gear;
    float whip;      // the deck or the board turn (rad, 0 = under the feet)
    bool prone;      // the surfer lies on the board (before the take-off)
    float body_rot;  // the body turns over the board round the feet (rad, + = to the left)
} pose_t;

static v2 s_j[J_N];  // the joints of the pose (body frame)

// a two-bone leg or arm: the middle joint from the ends (lengths a, b); bend = +1 or -1 (the side of the bend)
static v2 ik2(v2 p0, v2 p1, float a, float b, float bend)
{
    float dx = p1.x - p0.x, dy = p1.y - p0.y, d2 = dx * dx + dy * dy, d = fsq(d2);
    if (d > a + b - 0.001f) {
        float k = (a + b - 0.001f) / (d > 1e-5f ? d : 1);
        dx *= k;
        dy *= k;
        d = a + b - 0.001f;
        d2 = d * d;
    }
    if (d < 1e-4f) {
        return V2(p0.x + a, p0.y);
    }
    float x = (a * a - b * b + d2) / (2 * d), h2 = a * a - x * x, h = h2 > 0 ? fsq(h2) : 0;
    float ux = dx / d, uy = dy / d;
    return V2(p0.x + ux * x - uy * h * bend, p0.y + uy * x + ux * h * bend);
}

#define L_SHIN  0.46f
#define L_THIGH 0.46f
#define L_TORSO 0.56f
#define L_UARM  0.30f
#define L_FARM  0.29f
static void pose_make(const pose_t *P)
{
    float c = clampf(P->crouch, 0, 1), k = clampf(P->tuck, 0, 1), l = clampf(P->lean, -1, 1);
    v2 ff, fb;
    if (P->gear == GEAR_SCOOTER) {
        ff = V2(0.08f, 0.10f);
        fb = V2(-0.14f, 0.10f);
    } else if (P->gear == GEAR_SURF) {
        ff = V2(0.30f, 0.08f);
        fb = V2(-0.30f, 0.08f);
    } else {
        ff = V2(0.22f, 0.06f);
        fb = V2(-0.22f, 0.06f);
    }
    if (P->arms == ARMS_RUN) {  // (a run: the feet move in a cycle)
        float s = fsin(P->phase), q = fcos(P->phase);
        ff = V2(0.32f * s, 0.06f + 0.16f * fmaxf(0, q));
        fb = V2(-0.32f * s, 0.06f + 0.16f * fmaxf(0, -q));
    }
    float hy = 0.94f - 0.34f * c, hx = -0.03f - 0.10f * c + 0.06f * l;
    float tilt = 0.12f + 0.55f * c + 0.30f * l;  // the forward lean of the torso (rad)
    // the tuck: the hips go down to the feet, the knees come up to the chest
    hy = lerpf(hy, 0.42f, k);
    hx = lerpf(hx, -0.10f, k);
    tilt = lerpf(tilt, 0.85f, k);
    v2 hip = V2(hx, hy);
    v2 kf = ik2(ff, hip, L_SHIN, L_THIGH, -1), kb = ik2(fb, hip, L_SHIN, L_THIGH, -1);
    v2 neck = V2(hip.x + L_TORSO * fsin(tilt), hip.y + L_TORSO * fcos(tilt));
    v2 head = V2(neck.x + 0.19f * fsin(tilt * 0.5f + 0.1f), neck.y + 0.19f * fcos(tilt * 0.5f + 0.1f));
    v2 sh = V2(neck.x - 0.03f, neck.y - 0.05f);
    v2 hf, hb;
    if (P->arms == ARMS_BAR) {
        v2 bar = V2(0.40f, 0.92f);
        hf = bar;
        hb = V2(bar.x - 0.05f, bar.y + 0.01f);
    } else if (P->arms == ARMS_PADDLE) {
        float s = fsin(P->phase);
        hf = V2(sh.x + 0.45f * s, sh.y - 0.25f - 0.1f * fcos(P->phase));
        hb = V2(sh.x - 0.45f * s, sh.y - 0.25f + 0.1f * fcos(P->phase));
    } else if (P->arms == ARMS_RUN) {
        float s = fsin(P->phase);
        hf = V2(sh.x - 0.35f * s, sh.y - 0.38f);
        hb = V2(sh.x + 0.35f * s, sh.y - 0.38f);
    } else if (P->arms == ARMS_UP) {
        hf = V2(sh.x + 0.25f, sh.y + 0.45f);
        hb = V2(sh.x - 0.25f, sh.y + 0.45f);
    } else {
        float w = P->wave;
        hf = V2(sh.x + 0.50f, sh.y - 0.12f + 0.18f * w);
        hb = V2(sh.x - 0.48f, sh.y + 0.08f - 0.10f * w);
    }
    if (k > 0 && P->arms != ARMS_BAR) {  // (the tuck: the hands hold the shins)
        hf = V2(lerpf(hf.x, kf.x + 0.04f, k), lerpf(hf.y, kf.y - 0.12f, k));
        hb = V2(lerpf(hb.x, kb.x + 0.02f, k), lerpf(hb.y, kb.y - 0.10f, k));
    }
    v2 ef = ik2(sh, hf, L_UARM, L_FARM, 1), eb = ik2(sh, hb, L_UARM, L_FARM, 1);
    if (P->prone) {  // (lies on the board: a fixed pose)
        hip = V2(-0.35f, 0.16f);
        neck = V2(0.22f, 0.20f);
        head = V2(0.40f, 0.30f);
        kf = V2(-0.78f, 0.16f);
        kb = V2(-0.80f, 0.15f);
        ff = V2(-1.16f, 0.22f);
        fb = V2(-1.18f, 0.20f);
        float s = fsin(P->phase);
        ef = V2(0.30f + 0.10f * s, 0.05f);
        hf = V2(0.42f + 0.25f * s, -0.10f);
        eb = V2(0.28f - 0.10f * s, 0.06f);
        hb = V2(0.38f - 0.25f * s, -0.08f);
    }
    if (P->body_rot != 0 && !P->prone) {
        v2 m = V2((ff.x + fb.x) * 0.5f, (ff.y + fb.y) * 0.5f);
        float cs = fcos(P->body_rot), sn = fsin(P->body_rot);
        v2 *pp[9] = {&head, &neck, &hip, &kf, &kb, &ef, &hf, &eb, &hb};
        for (int i = 0; i < 9; i++) {
            float dx = pp[i]->x - m.x, dy = pp[i]->y - m.y;
            pp[i]->x = m.x + dx * cs - dy * sn;
            pp[i]->y = m.y + dx * sn + dy * cs;
        }
        kf = ik2(ff, hip, L_SHIN, L_THIGH, -1);
        kb = ik2(fb, hip, L_SHIN, L_THIGH, -1);
    }
    s_j[J_HEAD] = head;
    s_j[J_NECK] = neck;
    s_j[J_HIP] = hip;
    s_j[J_KNF] = kf;
    s_j[J_FTF] = ff;
    s_j[J_KNB] = kb;
    s_j[J_FTB] = fb;
    s_j[J_ELF] = ef;
    s_j[J_HDF] = hf;
    s_j[J_ELB] = eb;
    s_j[J_HDB] = hb;
}
// the centre of mass of the pose (body frame)
static v2 pose_com(void)
{
    v2 h = s_j[J_HIP], n = s_j[J_NECK];
    return V2(h.x * 0.6f + n.x * 0.4f, h.y * 0.6f + n.y * 0.4f - 0.05f);
}

// ---------------------------------------------------------------- the colours of the rider
typedef struct {
    uint16_t skin, skin_d, shirt, shirt_d, shorts, shorts_d, hair, helmet, shoe, board, board2, rail;
} look_t;
static look_t s_look;
static void look_set(int gear)
{
    s_look.skin = rgb(232, 168, 120);
    s_look.skin_d = rgb(196, 132, 92);
    s_look.hair = rgb(92, 56, 30);
    s_look.shoe = rgb(250, 250, 245);
    if (gear == GEAR_SCOOTER) {
        s_look.shirt = rgb(255, 214, 64);
        s_look.shirt_d = rgb(214, 168, 40);
        s_look.shorts = rgb(40, 96, 200);
        s_look.shorts_d = rgb(30, 70, 160);
        s_look.helmet = rgb(255, 90, 80);
        s_look.board = rgb(70, 220, 210);
        s_look.board2 = rgb(40, 40, 50);
        s_look.rail = rgb(210, 214, 222);
    } else if (gear == GEAR_SURF) {
        s_look.shirt = rgb(20, 40, 60);
        s_look.shirt_d = rgb(14, 28, 44);  // (a black rash guard)
        s_look.shorts = rgb(255, 108, 74);
        s_look.shorts_d = rgb(214, 80, 56);
        s_look.helmet = 0;
        s_look.board = rgb(252, 248, 236);
        s_look.board2 = rgb(255, 120, 80);
        s_look.rail = rgb(40, 180, 200);
    } else {
        s_look.shirt = rgb(250, 250, 250);
        s_look.shirt_d = rgb(210, 214, 220);
        s_look.shorts = rgb(30, 200, 150);
        s_look.shorts_d = rgb(20, 150, 110);
        s_look.helmet = 0;
        s_look.board = rgb(255, 196, 40);
        s_look.board2 = rgb(250, 90, 140);
        s_look.rail = rgb(160, 110, 30);
    }
}

// ---------------------------------------------------------------- the drawing of the rider
// the world place of the body frame: the feet point (fx, fy), the angle (rad, + = the body turns to the left), the
// side (+1: looks to +x, -1: to -x)
typedef struct {
    float x, y, ang, side;
} place_t;
static place_t s_pl;
static inline v2 bw(v2 b)  // body frame -> world
{
    float cs = fcos(s_pl.ang), sn = fsin(s_pl.ang);
    float bx = b.x * s_pl.side;
    return V2(s_pl.x + bx * cs - b.y * sn, s_pl.y + bx * sn + b.y * cs);
}
static void limb(v2 a, v2 b, float r0, float r1, uint16_t c)
{
    v2 A = bw(a), B = bw(b);
    wcap(A.x, A.y, B.x, B.y, r0, r1, c, 256);
}
static void gear_draw(const pose_t *P)
{
    if (P->gear == GEAR_SCOOTER) {
        float cw = fcos(P->whip), sw = fsin(P->whip);
        // the deck turns round the stem (x = 0.26): its length in the picture is cos(whip)
        float sx0 = 0.26f;
        v2 rw = V2(sx0 + (-0.22f - sx0) * cw, 0.055f), fw = V2(0.28f, 0.055f);
        v2 d0 = V2(sx0 + (-0.26f - sx0) * cw, 0.10f), d1 = V2(0.22f, 0.10f);
        uint16_t dc = cw > 0 ? s_look.board : rgb(30, 34, 40);
        limb(d0, d1, 0.028f, 0.028f, dc);
        limb(V2(d0.x, 0.12f), V2(d1.x, 0.12f), 0.012f, 0.012f, sw > 0.3f ? s_look.board2 : s_look.rail);
        v2 st0 = V2(0.28f, 0.06f), st1 = V2(0.40f, 0.96f);
        limb(st0, st1, 0.022f, 0.020f, s_look.rail);
        limb(V2(0.37f, 0.95f), V2(0.45f, 0.97f), 0.03f, 0.03f, rgb(40, 40, 46));
        v2 a = bw(rw), b = bw(fw);
        wdisc(a.x, a.y, 0.06f, rgb(36, 36, 40), 256);
        wdisc(a.x, a.y, 0.03f, rgb(230, 230, 236), 256);
        wdisc(b.x, b.y, 0.06f, rgb(36, 36, 40), 256);
        wdisc(b.x, b.y, 0.03f, rgb(230, 230, 236), 256);
    } else {
        bool surf = P->gear == GEAR_SURF;
        float L = surf ? 1.0f : 0.58f, th = surf ? 0.05f : 0.022f;
        float cw = fcos(P->whip);
        // the board as a few capsules: the nose turns up (the rocker)
        v2 pts[7];
        for (int i = 0; i < 7; i++) {
            float t = i / 6.0f, x = (-L + 2 * L * t) * cw;
            float rock = surf ? 0.10f * t * t * t * t + 0.03f * (1 - t) * (1 - t) : 0.03f * (t - 0.5f) * (t - 0.5f) * 4;
            pts[i] = V2(x, th + rock);
        }
        for (int i = 0; i < 6; i++) {
            float t0 = i / 6.0f, t1 = (i + 1) / 6.0f;
            float w0 = th * (0.6f + 0.6f * fsin((float)M_PI * t0)), w1 = th * (0.6f + 0.6f * fsin((float)M_PI * t1));
            limb(pts[i], pts[i + 1], w0, w1, s_look.board);
        }
        if (surf) {
            limb(V2(-L * 0.7f * cw, th * 0.6f), V2(L * 0.85f * cw, th + 0.04f), 0.012f, 0.012f, s_look.board2);
        }
        limb(V2(-L * 0.95f * cw, th * 0.2f), V2(L * 0.92f * cw, th * 0.4f), 0.01f, 0.01f, s_look.rail);
    }
}
static void rider_draw(const pose_t *P, place_t pl, bool with_gear)
{
    s_pl = pl;
    pose_make(P);
    const v2 *J = s_j;
    float sc = 1.0f;
    (void)sc;
    // the back arm and the back leg (darker)
    if (P->arms != ARMS_BAR) {
        limb(J[J_NECK], J[J_ELB], 0.05f, 0.045f, s_look.skin_d);
        limb(J[J_ELB], J[J_HDB], 0.045f, 0.04f, s_look.skin_d);
    }
    limb(J[J_HIP], J[J_KNB], 0.085f, 0.065f, s_look.shorts_d);
    limb(J[J_KNB], J[J_FTB], 0.06f, 0.048f, s_look.skin_d);
    limb(J[J_FTB], V2(J[J_FTB].x + 0.13f, J[J_FTB].y - 0.02f), 0.04f, 0.035f,
         P->gear == GEAR_SCOOTER ? s_look.shoe : s_look.skin_d);
    if (with_gear) {
        gear_draw(P);
    }
    if (P->arms == ARMS_BAR) {
        limb(J[J_NECK], J[J_ELB], 0.05f, 0.045f, s_look.skin_d);
        limb(J[J_ELB], J[J_HDB], 0.045f, 0.04f, s_look.skin_d);
    }
    // the torso, the front leg, the head, the front arm
    limb(J[J_HIP], J[J_NECK], 0.115f, 0.125f, s_look.shirt);
    limb(J[J_HIP], V2(J[J_HIP].x + (J[J_NECK].x - J[J_HIP].x) * 0.2f, J[J_HIP].y + (J[J_NECK].y - J[J_HIP].y) * 0.2f),
         0.12f, 0.12f, s_look.shorts);
    limb(J[J_HIP], J[J_KNF], 0.085f, 0.068f, s_look.shorts);
    limb(J[J_KNF], J[J_FTF], 0.062f, 0.05f, s_look.skin);
    limb(J[J_FTF], V2(J[J_FTF].x + 0.13f, J[J_FTF].y - 0.02f), 0.042f, 0.036f,
         P->gear == GEAR_SCOOTER ? s_look.shoe : s_look.skin);
    v2 h = bw(J[J_HEAD]);
    v2 nk = bw(J[J_NECK]);
    wcap(nk.x, nk.y, h.x, h.y, 0.05f, 0.05f, s_look.skin, 256);
    wdisc(h.x, h.y, 0.125f, s_look.skin, 256);
    v2 face = bw(V2(J[J_HEAD].x + 0.09f, J[J_HEAD].y + 0.01f));
    wdisc(face.x, face.y, 0.03f, s_look.skin_d, 200);  // (the nose and the cheek: the side of the face)
    if (s_look.helmet) {
        v2 hc = bw(V2(J[J_HEAD].x - 0.01f, J[J_HEAD].y + 0.035f));
        wdisc(hc.x, hc.y, 0.135f, s_look.helmet, 256);
        v2 vz = bw(V2(J[J_HEAD].x + 0.11f, J[J_HEAD].y + 0.07f));
        wcap(hc.x, hc.y, vz.x, vz.y, 0.03f, 0.025f, s_look.helmet, 256);
        v2 sh = bw(V2(J[J_HEAD].x - 0.06f, J[J_HEAD].y + 0.11f));
        wdisc(sh.x, sh.y, 0.035f, rgb(255, 255, 255), 120);
    } else {
        v2 hc = bw(V2(J[J_HEAD].x - 0.05f, J[J_HEAD].y + 0.05f));
        wdisc(hc.x, hc.y, 0.11f, s_look.hair, 256);
        v2 hb = bw(V2(J[J_HEAD].x - 0.13f, J[J_HEAD].y - 0.02f));
        wcap(hc.x, hc.y, hb.x, hb.y, 0.07f, 0.05f, s_look.hair, 256);
    }
    limb(J[J_NECK], J[J_ELF], 0.052f, 0.046f, s_look.skin);
    limb(J[J_ELF], J[J_HDF], 0.046f, 0.04f, s_look.skin);
    v2 hd = bw(J[J_HDF]);
    wdisc(hd.x, hd.y, 0.048f, s_look.skin, 256);
}

// ---------------------------------------------------------------- the ragdoll
#define RD_N (J_N + 2)  // the joints and the two ends of the gear
typedef struct {
    bool on;
    float t;  // the time since the fall
    float x[RD_N], y[RD_N], px[RD_N], py[RD_N];
    int gear;
    float side;
} rag_t;
static rag_t s_rag;
static const uint8_t RD_STICK[][2] = {
        {J_HEAD, J_NECK}, {J_NECK, J_HIP}, {J_HIP, J_KNF}, {J_KNF, J_FTF},  {J_HIP, J_KNB},
        {J_KNB, J_FTB},   {J_NECK, J_ELF}, {J_ELF, J_HDF}, {J_NECK, J_ELB}, {J_ELB, J_HDB},
        {J_HEAD, J_HIP},  {J_KNF, J_KNB},  {J_N, J_N + 1},
};
#define RD_NS ((int)(sizeof RD_STICK / sizeof RD_STICK[0]))
static float s_rd_len[RD_NS];
// the ground of the event for the ragdoll: it moves the point out of the ground and gives the friction
// (returns true on a contact); water: buoyancy and drag (the event function does it)
static bool (*s_rd_ground)(float *x, float *y, float *px, float *py, float dt);

// start the ragdoll from the pose now, with the speed (vx, vy) and the spin w (rad/s)
static void rag_start(const pose_t *P, place_t pl, float vx, float vy, float w, float dt)
{
    s_pl = pl;
    pose_make(P);
    rag_t *R = &s_rag;
    R->on = true;
    R->t = 0;
    R->gear = P->gear;
    R->side = pl.side;
    v2 com = bw(pose_com());
    for (int i = 0; i < J_N; i++) {
        v2 w2 = bw(s_j[i]);
        R->x[i] = w2.x;
        R->y[i] = w2.y;
        // the speed of the point: the speed of the body + the spin round the centre of mass
        float rx = w2.x - com.x, ry = w2.y - com.y;
        float pvx = vx - w * ry, pvy = vy + w * rx;
        R->px[i] = w2.x - pvx * dt;
        R->py[i] = w2.y - pvy * dt;
    }
    float L = P->gear == GEAR_SURF ? 1.0f : P->gear == GEAR_SKIM ? 0.55f : 0.30f;
    v2 g0 = bw(V2(-L, 0.06f)), g1 = bw(V2(L, 0.06f));
    R->x[J_N] = g0.x;
    R->y[J_N] = g0.y;
    R->x[J_N + 1] = g1.x;
    R->y[J_N + 1] = g1.y;
    for (int i = J_N; i < RD_N; i++) {
        R->px[i] = R->x[i] - (vx * 0.8f) * dt;
        R->py[i] = R->y[i] - (vy * 0.8f + 1.0f) * dt;
    }
    for (int s = 0; s < RD_NS; s++) {
        int a = RD_STICK[s][0], b = RD_STICK[s][1];
        float dx = R->x[b] - R->x[a], dy = R->y[b] - R->y[a];
        s_rd_len[s] = fsq(dx * dx + dy * dy);
    }
}
static void rag_step(float dt, float g)
{
    rag_t *R = &s_rag;
    if (!R->on) {
        return;
    }
    R->t += dt;
    for (int i = 0; i < RD_N; i++) {
        float vx = (R->x[i] - R->px[i]) * 0.995f, vy = (R->y[i] - R->py[i]) * 0.995f;
        R->px[i] = R->x[i];
        R->py[i] = R->y[i];
        R->x[i] += vx;
        R->y[i] += vy - g * dt * dt;
    }
    for (int it = 0; it < 4; it++) {
        for (int s = 0; s < RD_NS; s++) {
            int a = RD_STICK[s][0], b = RD_STICK[s][1];
            float dx = R->x[b] - R->x[a], dy = R->y[b] - R->y[a], d2 = dx * dx + dy * dy;
            if (d2 < 1e-8f) {
                continue;
            }
            float d = fsq(d2), k = (d - s_rd_len[s]) / d * 0.5f;
            if (s == 10) {
                k *= 0.3f;  // (the head-hip stick is soft)
            }
            R->x[a] += dx * k;
            R->y[a] += dy * k;
            R->x[b] -= dx * k;
            R->y[b] -= dy * k;
        }
        if (s_rd_ground) {
            for (int i = 0; i < RD_N; i++) {
                s_rd_ground(&R->x[i], &R->y[i], &R->px[i], &R->py[i], dt);
            }
        }
    }
}
static void rag_draw(void)
{
    rag_t *R = &s_rag;
    if (!R->on) {
        return;
    }
#define RP(i) R->x[i], R->y[i]
    // the gear first (it flies on its own)
    uint16_t gc = s_look.board;
    float gr = R->gear == GEAR_SURF ? 0.06f : 0.035f;
    wcap(RP(J_N), RP(J_N + 1), gr, gr, gc, 256);
    if (R->gear == GEAR_SCOOTER) {
        wdisc(RP(J_N), 0.06f, rgb(36, 36, 40), 256);
        wdisc(RP(J_N + 1), 0.06f, rgb(36, 36, 40), 256);
    }
    wcap(RP(J_NECK), RP(J_ELB), 0.05f, 0.045f, s_look.skin_d, 256);
    wcap(RP(J_ELB), RP(J_HDB), 0.045f, 0.04f, s_look.skin_d, 256);
    wcap(RP(J_HIP), RP(J_KNB), 0.085f, 0.065f, s_look.shorts_d, 256);
    wcap(RP(J_KNB), RP(J_FTB), 0.06f, 0.048f, s_look.skin_d, 256);
    wcap(RP(J_HIP), RP(J_NECK), 0.115f, 0.125f, s_look.shirt, 256);
    wcap(RP(J_HIP), RP(J_KNF), 0.085f, 0.068f, s_look.shorts, 256);
    wcap(RP(J_KNF), RP(J_FTF), 0.062f, 0.05f, s_look.skin, 256);
    wcap(RP(J_NECK), RP(J_HEAD), 0.05f, 0.05f, s_look.skin, 256);
    wdisc(RP(J_HEAD), 0.125f, s_look.skin, 256);
    if (s_look.helmet) {
        wdisc(R->x[J_HEAD], R->y[J_HEAD] + 0.03f, 0.13f, s_look.helmet, 256);
    } else {
        wdisc(R->x[J_HEAD] - 0.03f * R->side, R->y[J_HEAD] + 0.04f, 0.11f, s_look.hair, 256);
    }
    wcap(RP(J_NECK), RP(J_ELF), 0.052f, 0.046f, s_look.skin, 256);
    wcap(RP(J_ELF), RP(J_HDF), 0.046f, 0.04f, s_look.skin, 256);
#undef RP
}
static v2 rag_com(void)
{
    return V2(s_rag.x[J_HIP] * 0.6f + s_rag.x[J_NECK] * 0.4f, s_rag.y[J_HIP] * 0.6f + s_rag.y[J_NECK] * 0.4f);
}
