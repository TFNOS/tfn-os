// INK KARTS: the karts (included by app_kart.c). The 6 racers (original characters: a helmet with ears, an antenna, a
// fin ...), their models (a fine one near the eye, a coarse one far), the physics of a kart (a fixed step of 1/60 s:
// grip, a powerslide, the surface, the walls, the bumps), THE INK RULE (your own old ink = BOOST, the ink of a rival =
// SLICK), the BLOT, and the computer drivers.

typedef struct {
    const char *name[2];
    uint32_t body, trim, suit, helm;
    int feature;  // the look of the helmet: 0 fox ears, 1 round ears, 2 antenna, 3 square, 4 long ears, 5 fin
    float line;   // the computer driver: its own line across the road (m)
    float skill;  // the computer driver: its speed (part of the top speed)
} racer_t;
static const racer_t RACERS[NKART] = {
        {{"ZIPPA", "זיפה"}, 0x16C4F2, 0xF4F8FF, 0x1C3A5C, 0x16C4F2, 0, -1.5f, 0.985f},
        {{"MOKO", "מוקו"}, 0xF03CB8, 0xFFE0F4, 0x4A1C48, 0xF060C8, 1, 0.9f, 0.975f},
        {{"TANGO", "טנגו"}, 0xF8CC1C, 0x2A2A34, 0x5A3A10, 0xFFD838, 2, -0.4f, 0.99f},
        {{"BRIX", "בריקס"}, 0x6ADE3C, 0x203018, 0x24481C, 0x7AE84C, 3, 1.6f, 0.97f},
        {{"LUMA", "לומה"}, 0xFF7E1E, 0xFFF0D8, 0x5C2A10, 0xFF9440, 4, 0.2f, 0.98f},
        {{"NOVA", "נובה"}, 0x9C5CFF, 0xE8DCFF, 0x2E1C5C, 0xAC74FF, 5, -1.0f, 0.98f},
};

// ---------------------------------------------------------------- the models
enum {
    M_WHEEL,
    M_WHEEL_LO,
    M_KART0
};  // then for each racer: the fine body, the coarse body
#define M_COUNT (M_KART0 + 2 * NKART)
static model_t s_mod[M_COUNT];

// the helmet and its feature (fine: more steps)
static void build_head(const racer_t *r, bool fine)
{
    int rg = fine ? 5 : 3, sg = fine ? 8 : 5;
    s_bmat = MT_PLASTIC;
    if (r->feature == 3) {
        prim_cbox(V3(0, 1.12f, -0.12f), V3(0.3f, 0.28f, 0.3f), fine ? 0.1f : 0.06f, r->helm);
    } else {
        prim_ell(V3(0, 1.12f, -0.12f), V3(0.31f, 0.3f, 0.33f), rg, sg, r->helm, r->body);
    }
    s_bmat = MT_GLASS;
    prim_ell(V3(0, 1.1f, 0.07f), V3(0.24f, 0.12f, 0.21f), fine ? 3 : 2, fine ? 6 : 4, 0x2A3C66, 0x101828);
    s_bmat = MT_PLASTIC;
    if (!fine && r->feature != 2) {
        return;
    }
    switch (r->feature) {
    case 0:
        for (int s = -1; s <= 1; s += 2) {
            prim_cyl(V3(s * 0.17f, 1.3f, -0.14f), 0, 0.34f, 0.12f, 0.01f, 5, 1, r->helm, r->trim, r->helm);
        }
        break;
    case 1:
        for (int s = -1; s <= 1; s += 2) {
            prim_ell(V3(s * 0.25f, 1.36f, -0.14f), V3(0.12f, 0.12f, 0.06f), 3, 6, r->helm, r->trim);
        }
        break;
    case 2:
        prim_cyl(V3(0.05f, 1.36f, -0.16f), 0, 0.42f, 0.025f, 0.02f, 4, 0, 0x606878, 0x808898, 0x606878);
        s_bmat = MT_GLOW;
        prim_ell(V3(0.05f, 1.82f, -0.16f), V3(0.09f, 0.09f, 0.09f), fine ? 3 : 2, fine ? 6 : 4, 0xFFFFC0, 0xFFC020);
        break;
    case 4:
        for (int s = -1; s <= 1; s += 2) {
            prim_ell(V3(s * 0.13f, 1.55f, -0.18f), V3(0.07f, 0.27f, 0.07f), 3, 6, r->helm, r->trim);
        }
        break;
    case 5:
        prim_cbox(V3(0, 1.42f, -0.18f), V3(0.035f, 0.13f, 0.27f), 0.03f, r->trim);
        break;
    default:
        break;
    }
    s_bmat = MT_MATTE;
}
// a kart: the chassis, the nose, the pods, the motor, the spoiler, the driver (x right, y up, z to the front). The
// rounded boxes (44 triangles) only where the eye sees them; plain boxes (12) for the rest (THE SPEED: the karts are
// near the camera, their triangles are the main cost of a frame)
static void build_kart(const racer_t *r, bool fine)
{
    if (!fine) {
        prim_box(V3(0, 0.3f, 0.1f), V3(0.62f, 0.16f, 0.95f), r->body);
        prim_box(V3(0, 0.66f, -0.98f), V3(0.62f, 0.04f, 0.14f), r->body);
        prim_ell(V3(0, 0.72f, -0.2f), V3(0.29f, 0.3f, 0.24f), 2, 4, r->suit, r->suit);
        build_head(r, false);
        // (the wheels in the far model: no model of its own for each wheel)
        static const float WQ4[4][3] = {
                {-0.66f, 0.28f, 0.7f}, {0.66f, 0.28f, 0.7f}, {-0.67f, 0.3f, -0.72f}, {0.67f, 0.3f, -0.72f}};
        for (int w = 0; w < 4; w++) {
            prim_box(V3(WQ4[w][0], WQ4[w][1], WQ4[w][2]), V3(0.12f, 0.28f, 0.28f), 0x1A1A20);
        }
        return;
    }
    prim_box(V3(0, 0.2f, 0), V3(0.62f, 0.07f, 1.0f), 0x2A2C36);
    s_bmat = MT_PLASTIC;
    if (fine) {
        prim_cbox(V3(0, 0.36f, 0.68f), V3(0.42f, 0.14f, 0.36f), 0.12f, r->body);
    } else {
        prim_box(V3(0, 0.36f, 0.68f), V3(0.42f, 0.14f, 0.36f), r->body);
    }
    prim_box(V3(0, 0.3f, 1.02f), V3(0.6f, 0.06f, 0.1f), r->trim);
    for (int s = -1; s <= 1; s += 2) {
        prim_box(V3(s * 0.55f, 0.34f, 0.0f), V3(0.15f, 0.13f, 0.5f), r->body);
    }
    prim_box(V3(0, 0.66f, -0.98f), V3(0.62f, 0.04f, 0.14f), r->body);
    if (fine) {
        for (int s = -1; s <= 1; s += 2) {
            prim_box(V3(s * 0.5f, 0.5f, -0.96f), V3(0.03f, 0.14f, 0.1f), r->trim);
        }
    }
    s_bmat = MT_METAL;
    prim_box(V3(0, 0.4f, -0.78f), V3(0.3f, 0.15f, 0.2f), 0x8C94A6);
    s_bmat = MT_MATTE;
    prim_box(V3(0, 0.56f, -0.44f), V3(0.3f, 0.24f, 0.07f), 0x22242C);
    // the driver: the suit, the arms to the wheel, the helmet
    prim_ell(V3(0, 0.7f, -0.2f), V3(0.29f, 0.3f, 0.24f), 3, fine ? 7 : 5, r->suit, r->suit);
    if (fine) {
        for (int s = -1; s <= 1; s += 2) {
            v3 a[3] = {V3(s * 0.26f, 0.86f, -0.18f), V3(s * 0.3f, 0.7f, 0.06f), V3(s * 0.15f, 0.66f, 0.28f)};
            float rr[3] = {0.08f, 0.075f, 0.07f};
            prim_tube(a, rr, 3, 4, r->suit, r->trim, false);
        }
    }
    build_head(r, fine);
}
static void build_wheel(bool fine)
{
    // (a cylinder along y, then turned so that its axis is x)
    xf_t x;
    xf_make(&x, V3(0, 0, 0), 0, 0, PI_F * 0.5f, V3(1, 1, 1));
    mb_frame(&x);
    prim_cyl(V3(0, 0, 0), -0.12f, 0.12f, 0.28f, 0.28f, fine ? 8 : 6, fine ? 3 : 2, 0x1A1A20, 0x24242C, 0xB8C0D0);
    mb_noframe();
}
static void karts_build(void)
{
    s_mnv = s_mnt = 0;
    mb_begin();
    build_wheel(true);
    model_add(&s_mod[M_WHEEL]);
    build_wheel(false);
    model_add(&s_mod[M_WHEEL_LO]);
    model_bake(&s_mod[M_WHEEL]);
    model_bake(&s_mod[M_WHEEL_LO]);
    for (int k = 0; k < NKART; k++) {
        build_kart(&RACERS[k], true);
        model_add(&s_mod[M_KART0 + 2 * k]);
        build_kart(&RACERS[k], false);
        model_add(&s_mod[M_KART0 + 2 * k + 1]);
        model_bake(&s_mod[M_KART0 + 2 * k + 1]);
    }
}

// ---------------------------------------------------------------- the state of a kart
typedef struct {
    int racer;  // the racer (the look, the colour of the ink)
    bool human;
    float x, z, yaw, vx, vz;     // the place, the heading (the front = (sin yaw, cos yaw)), the speed over the ground
    float x0, z0, yaw0;          // (the step before: the frame blends the two)
    float steer, steer_in;       // the steer now (-1..1) and the wish of the driver
    float vf;                    // the speed along the heading
    float slide;                 // the visual slide angle (rad)
    float roll, pitch;           // the lean of the body
    float s, d, prog;            // the place on the track: s (rows), d (across), the progress (laps x rows + s)
    int hint, ink_row;           // the nearest row, the last row with ink (in progress rows)
    int surf;                    // 0 the road, 1 a kerb, 2 off the road
    int inkst;                   // 0 none, 1 own ink (BOOST), 2 the ink of a rival (SLICK)
    float boost_k, slick_k;      // the looks of the two states (0..1, smooth)
    float meter;                 // the BLOT: 0..1, full = ready
    float blot_t, push_t;        // the wide brush (s), the push (s)
    float bump_t, spin;          // the last bump (s ago), the turn of the wheels
    bool done;                   // the finish line after the last lap
    float t_done;                // the time of the finish
    int place;                   // 1..NKART
    float ai_d, ai_ph, ai_blot;  // the computer: its line now, the phase of its weave, its wait to BLOT
    int hits;                    // (the test: the bumps)
} kart_t;
static kart_t K[NKART];
static int s_me = 0;  // the kart of the person (index in K)
#define LAPS 3
static int s_level = 1;  // 0 EASY, 1 NORMAL, 2 HARD
static bool s_demo;      // THE DEMO (a board test; the console: store kart_demo 1, then open): the kart of the
                         // person drives by itself, and the races go on from track to track
#define STEP_DT (1.0f / 60.0f)
#define VTOP    19.5f  // the top speed (m/s) on the road
#define TURN    2.0f   // the turn rate at full steer (rad/s)

static inline v3 fwd_of(float yaw)
{
    return V3(fsin(yaw), 0, fcos(yaw));
}

// the start: the grid behind the line (2 places in each row), the person in place 5 of 6
static void karts_reset(int player_racer)
{
    memset(K, 0, sizeof K);
    int slot_of[NKART];
    // (the person takes the 5th place; the computer racers fill the others in the order of the table)
    int order[NKART], n = 0;
    for (int r = 0; r < NKART; r++) {
        if (r != player_racer) {
            order[n++] = r;
        }
    }
    for (int g = 0, q = 0; g < NKART; g++) {
        slot_of[g] = g == 4 ? player_racer : order[q++];
    }
    for (int g = 0; g < NKART; g++) {
        kart_t *k = &K[g];
        k->racer = slot_of[g];
        k->human = slot_of[g] == player_racer;
        if (k->human) {
            s_me = g;
        }
        float s = -6.0f - (g / 2) * 6.0f, d = (g & 1) ? 2.6f : -2.6f;
        v3 p = tr_point(s, d);
        k->x = k->x0 = p.x;
        k->z = k->z0 = p.z;
        k->yaw = k->yaw0 = tr_yaw(s);
        k->hint = rwrap((int)ifloor(s));
        tr_coords(k->x, k->z, &k->hint, &k->s, &k->d);
        k->prog = s;
        k->ink_row = (int)ifloor(s);
        k->ai_d = d;
        k->ai_ph = g * 1.7f;
        k->ai_blot = 3 + g;
        k->place = g + 1;
    }
}
// the curve of the road at s (+ = it turns to the right): the change of the heading over 10 m
static float tr_curve(float s)
{
    const trow_t *a = &s_tr[rwrap((int)swrap(s))], *b = &s_tr[rwrap((int)swrap(s) + 10)];
    return (a->tx * b->tz - a->tz * b->tx) * -1.0f;
}

static void fx_bump(kart_t *k, float hard);
static void fx_blot(kart_t *k);
static void fx_wall(kart_t *k, float hard);

// a BLOT now (when the meter is full): the wide brush and a push
static bool kart_blot(kart_t *k)
{
    if (k->meter < 1 || k->done) {
        return false;
    }
    k->meter = 0;
    k->blot_t = 2.0f;
    k->push_t = 0.7f;
    fx_blot(k);
    return true;
}

// the computer driver of kart k: the line, the steer, the BLOT
static void ai_drive(kart_t *k, float dt)
{
    const racer_t *r = &RACERS[k->racer];
    float v = k->vf;
    // its own line, a slow weave, the inside of the next curve
    k->ai_ph += dt * 0.31f;
    float goal = r->line + 0.6f * fsin(k->ai_ph + k->s * 0.013f);
    float cv = tr_curve(k->s + 12);
    goal += clampf(cv * 6.0f, -2.2f, 2.2f);  // (the inside of a curve to the right is +d)
    // the ink ahead: keep off the ink of a rival, keep to its own ink
    float ahead = k->s + 7 + v * 0.25f, age;
    int own = k - K;
    int o = ink_at(ahead, goal, &age);
    if (o >= 0 && o != own && age > INK_RIVAL && age < INK_WET) {
        float best = goal, bs = -9;
        for (int q = -2; q <= 2; q++) {
            if (!q) {
                continue;
            }
            float g2 = clampf(goal + q * 1.5f, -ROAD_W + 1.2f, ROAD_W - 1.2f), a2;
            int o2 = ink_at(ahead, g2, &a2);
            float sc = o2 < 0 || (o2 != own && a2 >= INK_WET) ? 1 : o2 == own ? 2 : -1;
            sc -= absf(q) * 0.2f;
            if (sc > bs) {
                bs = sc;
                best = g2;
            }
        }
        goal = best;
    }
    // the karts ahead: go round them
    for (int j = 0; j < NKART; j++) {
        if (&K[j] == k) {
            continue;
        }
        float ds = K[j].prog - k->prog;
        if (ds < 0.5f || ds > 8.0f) {
            continue;
        }
        if (absf(K[j].d - goal) < 1.7f) {
            goal = K[j].d + (K[j].d > 0 ? -2.1f : 2.1f);
        }
    }
    goal = clampf(goal, -ROAD_W + 1.0f, ROAD_W - 1.0f);
    k->ai_d += (goal - k->ai_d) * clampf(dt * 1.6f, 0, 1);
    float look = 5.0f + v * 0.36f;
    v3 t = tr_point(k->s + look, k->ai_d);
    float want = fatan2(t.x - k->x, t.z - k->z);
    float da = wrap_pi(want - k->yaw);
    k->steer_in = clampf(da * 2.4f, -1, 1);
    // the BLOT on a straight, after a short random wait
    if (k->meter >= 1) {
        k->ai_blot -= dt;
        if (k->ai_blot <= 0 && absf(cv) < 0.12f) {
            kart_blot(k);
            k->ai_blot = 2 + (k->racer * 37 % 5);
        }
    }
}

// the walls: back onto the track side of the wall, the speed into the wall goes away
static void kart_wall(kart_t *k, float dt)
{
    float lim = WALL_D - 0.78f;
    if (absf(k->d) > lim) {
        float side = k->d > 0 ? 1.0f : -1.0f, over = absf(k->d) - lim;
        const trow_t *t = &s_tr[k->hint];
        v3 n = V3(t->tz * side, 0, -t->tx * side);  // out of the track
        k->x -= n.x * over;
        k->z -= n.z * over;
        float vn = k->vx * n.x + k->vz * n.z;
        if (vn > 0) {
            k->vx -= n.x * vn * 1.3f;
            k->vz -= n.z * vn * 1.3f;
            if (vn > 1.5f) {
                // (a hit: a part of the speed goes)
                k->vx *= 0.86f;
                k->vz *= 0.86f;
                fx_wall(k, vn);
            } else {
                // (a scrape: a small loss only. Before, each frame of a scrape took 14 % of the speed, so a kart that
                // touched the wall at a small angle stopped there)
                k->vx *= 0.995f;
                k->vz *= 0.995f;
            }
            // (a steep angle: the kart turns along the barrier, to the way of the race after a hit head-on. The steer
            // needs speed, so before, a kart that faced the wall stayed there)
            v3 hd = fwd_of(k->yaw);
            float into = hd.x * n.x + hd.z * n.z;
            if (into > 0.35f) {
                float want = hd.x * t->tx + hd.z * t->tz >= -0.25f ? fatan2(t->tx, t->tz) : fatan2(-t->tx, -t->tz);
                k->yaw = wrap_pi(k->yaw + wrap_pi(want - k->yaw) * clampf(dt * 5.0f * into, 0, 1));
            }
        }
        k->d = side * lim;
    }
}
// one physics step of kart k (the input of the person is in steer_in)
static void kart_step(kart_t *k, float dt, bool racing)
{
    k->x0 = k->x;
    k->z0 = k->z;
    k->yaw0 = k->yaw;
    if (k->bump_t < 9) {
        k->bump_t += dt;
    }
    if ((!k->human || k->done || s_demo) && racing) {
        ai_drive(k, dt);
    }
    // the steer follows the wish (fast, but not at once)
    k->steer += (k->steer_in - k->steer) * clampf(dt * (k->human ? 9.0f : 6.0f), 0, 1);
    v3 h = fwd_of(k->yaw);
    v3 rt = V3(h.z, 0, -h.x);
    float vf = k->vx * h.x + k->vz * h.z, vr = k->vx * rt.x + k->vz * rt.z;
    // the surface and the ink under the front of the kart
    float ad = absf(k->d);
    k->surf = ad <= ROAD_W ? 0 : ad <= ROAD_W + KERB_W ? 1 : 2;
    float age = 0;
    int o = racing ? ink_at(k->s + 0.9f, k->d, &age) : -1;
    int me = k - K;
    k->inkst = o < 0 ? 0 : o == me ? (age >= INK_OWN ? 1 : 0) : (age >= INK_RIVAL && age <= INK_WET ? 2 : 0);
    k->boost_k += ((k->inkst == 1 ? 1.0f : 0.0f) - k->boost_k) * clampf(dt * 5, 0, 1);
    k->slick_k += ((k->inkst == 2 ? 1.0f : 0.0f) - k->slick_k) * clampf(dt * 6, 0, 1);
    // the top speed: the level, the computer, the surface, the ink, the push of a BLOT
    float vmax = VTOP;
    if (!k->human) {
        float lv = s_level == 0 ? 0.92f : s_level == 1 ? 0.97f : 1.01f;
        // (the rubber band: a computer racer far ahead waits a little, one far behind comes on)
        float gap = (K[s_me].prog - k->prog) * (1.0f / 120.0f);
        float rb = s_level == 0 ? 0.08f : s_level == 1 ? 0.05f : 0.03f;
        vmax *= RACERS[k->racer].skill * lv * (1 + clampf(gap, -0.5f, 1) * rb);
    }
    float grip = 10.0f, acc = 9.0f;
    if (k->surf == 1) {
        vmax *= 0.97f;
    }
    if (k->surf == 2) {
        vmax *= 0.62f;
        grip = 7.0f;
    }
    if (k->inkst == 1) {
        vmax *= 1.22f;
        acc *= 1.5f;
    }
    if (k->inkst == 2) {
        vmax *= 0.9f;
        grip *= 0.45f;
    }
    if (k->push_t > 0) {
        vmax *= 1.28f;
        acc *= 2.2f;
        k->push_t -= dt;
    }
    if (k->blot_t > 0) {
        k->blot_t -= dt;
    }
    if (!racing || k->done) {
        vmax = k->done ? VTOP * 0.55f : 0;
    }
    // the meter: own ink fills it fast, the road slowly
    if (racing && !k->done) {
        k->meter = clampf(k->meter + dt * (k->inkst == 1 ? 1.0f / 3.5f : 1.0f / 22.0f), 0, 1);
    }
    // the motor
    if (vf < vmax) {
        vf += acc * dt * (1 - 0.6f * clampf(vf / vmax, 0, 1));
    } else {
        vf -= (vf - vmax) * 1.6f * dt;
    }
    if (vf < 0) {
        vf += dt * 6;
    }
    // the turn: weak at a low speed, a little weaker at the top speed
    float g = clampf(vf / 6.0f, 0, 1) * (1 - 0.22f * clampf((vf - 12) / 10, 0, 1));
    float yr = k->steer * TURN * g;
    // the powerslide: a hard steer at speed (less grip, the body turns out, sparks)
    bool slide = absf(k->steer) > 0.72f && vf > 13.0f && k->surf != 2;
    if (slide) {
        grip *= 0.55f;
        yr *= 1.12f;
        vf -= dt * 0.8f;
    }
    // the SLICK: a small wobble
    if (k->inkst == 2) {
        yr += fsin(s_rtime * 13 + me) * 0.25f;
    }
    k->yaw = wrap_pi(k->yaw + yr * dt);
    vr *= 1 - clampf(grip * dt, 0, 1);
    // (the slide of the turn: a part of the speed goes to the side)
    vr -= yr * vf * dt * (slide ? 0.55f : 0.25f);
    h = fwd_of(k->yaw);
    rt = V3(h.z, 0, -h.x);
    k->vx = h.x * vf + rt.x * vr;
    k->vz = h.z * vf + rt.z * vr;
    k->vf = vf;
    k->x += k->vx * dt;
    k->z += k->vz * dt;
    k->slide += ((slide ? k->steer * 0.32f : 0) + clampf(vr * -0.05f, -0.3f, 0.3f) - k->slide) * clampf(dt * 5, 0, 1);
    k->roll += (clampf(yr * vf * 0.012f, -0.16f, 0.16f) - k->roll) * clampf(dt * 6, 0, 1);
    k->spin += vf * dt / 0.28f;
    // the place on the track
    float s_old = k->s;
    tr_coords(k->x, k->z, &k->hint, &k->s, &k->d);
    float ds = k->s - s_old;
    if (ds > s_ntr * 0.5f) {
        ds -= s_ntr;
    }
    if (ds < -s_ntr * 0.5f) {
        ds += s_ntr;
    }
    k->prog += ds;
    kart_wall(k, dt);
    // the ink behind the kart (each row that the back passed)
    if (racing && !k->done) {
        int rear = (int)ifloor(k->prog - 0.9f);
        if (rear - k->ink_row > 6) {
            k->ink_row = rear - 1;
        }
        while (k->ink_row < rear) {
            k->ink_row++;
            ink_put(k->ink_row, me, k->d, k->blot_t > 0);
        }
        if (rear < k->ink_row - 2) {
            k->ink_row = rear;
        }
    }
}
// the karts push each other apart (circles of 0.8 m)
static void karts_collide(void)
{
    for (int a = 0; a < NKART; a++) {
        for (int b = a + 1; b < NKART; b++) {
            kart_t *p = &K[a], *q = &K[b];
            float dx = q->x - p->x, dz = q->z - p->z, d2 = dx * dx + dz * dz;
            const float R = 1.55f;
            if (d2 >= R * R || d2 < 1e-6f) {
                continue;
            }
            float d = fsqrt(d2), nx = dx / d, nz = dz / d, over = (R - d) * 0.5f;
            p->x -= nx * over;
            p->z -= nz * over;
            q->x += nx * over;
            q->z += nz * over;
            float vp = p->vx * nx + p->vz * nz, vq = q->vx * nx + q->vz * nz, rel = vp - vq;
            if (rel > 0) {
                // (the parts along the line go over to the other kart, with a loss)
                float m = rel * 0.6f;
                p->vx -= nx * m;
                p->vz -= nz * m;
                q->vx += nx * m;
                q->vz += nz * m;
                if (rel > 1.2f && p->bump_t > 0.25f && q->bump_t > 0.25f) {
                    fx_bump(p, rel);
                    fx_bump(q, rel);
                }
            }
        }
    }
}
// the places: the finished karts by their time, then the others by their progress
static void karts_places(void)
{
    int idx[NKART];
    for (int i = 0; i < NKART; i++) {
        idx[i] = i;
    }
    for (int i = 1; i < NKART; i++) {
        int v = idx[i], j = i;
        while (j > 0) {
            const kart_t *a = &K[idx[j - 1]], *b = &K[v];
            bool before = b->done && (!a->done || b->t_done < a->t_done);
            if (!b->done && !a->done) {
                before = b->prog > a->prog;
            }
            if (!before) {
                break;
            }
            idx[j] = idx[j - 1];
            j--;
        }
        idx[j] = v;
    }
    for (int i = 0; i < NKART; i++) {
        K[idx[i]].place = i + 1;
    }
}
