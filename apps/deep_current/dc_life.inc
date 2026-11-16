// DEEP CURRENT: the life of the sea (included by app_deep.c). The prey (schools that part round the creature, drifters,
// shy swimmers, darters), the big jellyfish, the hunters (they stalk, coil and lunge; they flee when the creature is
// bigger than they are) and the rival (a creature of the same kind that eats, grows and hunts). Each prey carries the
// light of its trait: the creature that swallows it fills its gut with that trait.

enum {
    LS_IDLE,
    LS_FLEE,
    LS_STALK,
    LS_COIL,
    LS_LUNGE,
    LS_RECOIL
};
typedef struct {
    float x, y, vx, vy, a, ph, t, stun, hp, sz, a0, r0, z, cool, gulp;
    uint8_t sp, st, on;
    int8_t school;
} life_t;
#define NLIFE 220
static life_t s_life[NLIFE];
// the length of a hunter: each one has its own size, and the hunters of a deeper zone are bigger (life_new), so that
// the creature that grows does not outgrow all danger
static float hunter_len(const life_t *e)
{
    return SPEC[e->sp].len * e->sz / SPEC[e->sp].sz;
}
typedef struct {
    float x, y, vx, vy, tx, ty, alarm, turn_t;
    int sp, n;
    bool on;
} school_t;
#define NSCHOOL 2
static school_t s_school[NSCHOOL];
static uint32_t s_unl = UNL_BASE;  // the traits that live in the sea (the saved unlocks)
static float s_reveal;             // the PING / FLASH: all the life shows in the dark (0..1)
static float s_ink_blind;          // the INK: the hunters lost the creature (s)
static float s_hunt_t;             // the time to the next hunter
static int s_hunt_n;               // the hunters that came in this zone
static float s_respawn_t;

// ---- the gut: what the creature ate since its last growth (the traits and their food)
#define NGUT 48
static uint8_t s_gut_t[NGUT];
static float s_gut_f[NGUT];
static int s_ngut;
static float s_food, s_need;  // the food in the gut, the food for the next growth
static int s_eaten, s_steps;  // the prey eaten in this dive; the growths in this dive
static bool s_grow_ready;     // the gut is full: the growth choice comes
static int s_cause = -1;      // the death: the species (SP_N = the rival)
static int s_last_sfx_eat;
// the effects (the MP3 files: app_deep.c loads them)
enum {
    FX_EAT,
    FX_EAT2,
    FX_GULP,
    FX_PLANK,
    FX_DASH,
    FX_BITTEN,
    FX_BITE,
    FX_SHELL,
    FX_PRICK,
    FX_STING,
    FX_GROW,
    FX_PICK,
    FX_FLASH,
    FX_PING,
    FX_INK,
    FX_CURRENT,
    FX_LUNGE,
    FX_HATCH,
    FX_BUBBLE,
    FX_DEATH,
    FX_N
};
static void fx(int id, int vol, float var);
static float need_of(int step)
{
    return 6.0f + 1.0f * step;
}
static void eat_food(int trait, float food, float x, float y)
{
    s_food += food;
    if (s_ngut < NGUT) {
        s_gut_t[s_ngut] = (uint8_t)trait;
        s_gut_f[s_ngut] = food;
        s_ngut++;
    } else {
        s_gut_f[NGUT - 1] += food;
    }
    const uint8_t *c = TRAIT[trait].c;
    B.pulse = 0;
    B.pulse_c[0] = c[0];
    B.pulse_c[1] = c[1];
    B.pulse_c[2] = c[2];
    // the light of the trait flies into the creature
    float dx = wrapd(B.x[0] - x), dy = B.y[0] - y;
    part_new(PK_GENE, x, y, dx * 3, dy * 3, 0.35f, 1, c);
    if (s_food >= s_need) {
        s_grow_ready = true;
    }
}

// ================================================================ make and find
static life_t *life_new(int sp, float x, float y)
{
    for (int i = 0; i < NLIFE; i++) {
        life_t *e = &s_life[i];
        if (e->on) {
            continue;
        }
        memset(e, 0, sizeof *e);
        e->on = 1;
        e->sp = (uint8_t)sp;
        e->x = wrapx(x);
        e->y = clampf(y, 12, Z->h - 12);
        e->a = rndf() * 6.283f;
        e->ph = rndf() * 6.283f;
        e->a0 = rndf() * 6.283f;
        e->sz = SPEC[sp].sz * (0.9f + rndf() * 0.2f) * (is_hunter(sp) ? 1 + 0.3f * s_zone : 1);
        e->hp = SPEC[sp].hp;
        e->school = -1;
        e->z = rnds();
        return e;
    }
    return NULL;
}
static int life_count(int sp)
{
    int n = 0;
    for (int i = 0; i < NLIFE; i++) {
        n += s_life[i].on && s_life[i].sp == sp;
    }
    return n;
}
static bool sp_allowed(int sp)
{
    int t = SPEC[sp].trait;
    return is_hunter(sp) || sp == SP_BIGJELLY || (s_unl >> t & 1);
}
// a place out of the view of the camera (so that new life does not come from nothing)
static void far_place(float *x, float *y, float ymin, float ymax)
{
    for (int k = 0; k < 20; k++) {
        *x = rndf() * WW;
        *y = ymin + rndf() * (ymax - ymin);
        if (fabsf(wrapd(*x - C.x)) > s_fw * 0.6f / C.z + 60) {
            return;
        }
    }
}
static float zone_ymax(void)
{
    return (Z->far & FAR_VENTS) ? Z->h - 60 : Z->h - 20;
}
static void school_make(int sp, int n, float x, float y)
{
    for (int s = 0; s < NSCHOOL; s++) {
        school_t *sc = &s_school[s];
        if (sc->on) {
            continue;
        }
        memset(sc, 0, sizeof *sc);
        sc->on = true;
        sc->sp = sp;
        sc->x = x;
        sc->y = y;
        sc->tx = x;
        sc->ty = y;
        sc->n = n;
        for (int i = 0; i < n; i++) {
            life_t *e = life_new(sp, x, y);
            if (!e) {
                break;
            }
            e->school = (int8_t)s;
            e->r0 = powf(rndf(), 0.7f);
            e->z = rnds();
            float a = e->a0;
            e->x = wrapx(x + cosf(a) * e->r0 * 44);
            e->y = y + sinf(a) * e->r0 * 22;
            e->vx = -20;
        }
        return;
    }
}
static void zone_spawn(void)
{
    memset(s_life, 0, sizeof s_life);
    memset(s_school, 0, sizeof s_school);
    memset(s_plank, 0, sizeof s_plank);
    for (int k = 0; k < 10; k++) {
        const spawn_t *p = &Z->sp[k];
        if (p->n == 0) {
            break;
        }
        if (!sp_allowed(p->sp) || is_hunter(p->sp)) {
            continue;
        }
        if (SPEC[p->sp].beh == B_SCHOOL) {
            float x, y;
            far_place(&x, &y, 60, Z->h * 0.7f);
            x = wrapx(B.x[0] + 300 + rndf() * 200);
            school_make(p->sp, p->n, x, clampf(y, 50, Z->h - 60));
            continue;
        }
        for (int i = 0; i < p->n; i++) {
            float x = rndf() * WW, y = 20 + rndf() * (zone_ymax() - 20);
            if (fabsf(wrapd(x - B.x[0])) < 120) {
                x = wrapx(x + 300);
            }
            life_t *e = life_new(p->sp, x, y);
            if (e && p->sp == SP_ISOPOD) {
                e->y = floor_y(e->x) - 4;
            }
        }
    }
    for (int i = 0; i < 6; i++) {
        plank_cloud(rndf() * WW, 30 + rndf() * (zone_ymax() - 40), 28);
    }
    // the first cloud is near the creature: food at once
    plank_cloud(wrapx(B.x[0] + 90), B.y[0] + 10, 24);
    s_hunt_t = s_zone == 0 ? 28 : 16;
    s_hunt_n = 0;
    s_respawn_t = 2;
}
// new prey and plankton when the sea gets empty; the hunters come one by one, more of them as time goes on
static void zone_refill(float dt)
{
    s_respawn_t -= dt;
    if (s_respawn_t <= 0) {
        s_respawn_t = 1.6f;
        for (int k = 0; k < 10; k++) {
            const spawn_t *p = &Z->sp[k];
            if (p->n == 0) {
                break;
            }
            if (!sp_allowed(p->sp) || is_hunter(p->sp) || SPEC[p->sp].beh == B_SCHOOL) {
                continue;
            }
            if (life_count(p->sp) < p->n) {
                float x, y;
                far_place(&x, &y, 20, zone_ymax());
                life_t *e = life_new(p->sp, x, y);
                if (e && p->sp == SP_ISOPOD) {
                    e->y = floor_y(e->x) - 4;
                }
            }
        }
        // a school that lost fish gets new fish from the dark at its far side
        for (int s = 0; s < NSCHOOL; s++) {
            school_t *sc = &s_school[s];
            if (!sc->on) {
                continue;
            }
            int have = 0;
            for (int i = 0; i < NLIFE; i++) {
                have += s_life[i].on && s_life[i].school == s;
            }
            for (int i = have; i < sc->n && i < have + 6; i++) {
                if (fabsf(wrapd(sc->x - C.x)) < s_fw * 0.6f / C.z + 40) {
                    break;
                }
                life_t *e = life_new(sc->sp, sc->x, sc->y);
                if (!e) {
                    break;
                }
                e->school = (int8_t)s;
                e->r0 = powf(rndf(), 0.7f);
            }
        }
        if (plank_count() < 90) {
            float x, y;
            far_place(&x, &y, 30, zone_ymax() - 10);
            plank_cloud(x, y, 26);
        }
    }
    // the hunters
    s_hunt_t -= dt;
    if (s_hunt_t <= 0) {
        int want = 0;
        for (int k = 0; k < 10 && Z->sp[k].n; k++) {
            if (is_hunter(Z->sp[k].sp)) {
                want += Z->sp[k].n;
            }
        }
        int now = 0;
        for (int i = 0; i < NLIFE; i++) {
            now += s_life[i].on && is_hunter(s_life[i].sp);
        }
        int cap = want + s_hunt_n / 3;  // (the hunters gather: one more each third one)
        if (now < cap) {
            // the next hunter of the list (in turn)
            int pick = -1, idx = 0;
            for (int k = 0; k < 10 && Z->sp[k].n; k++) {
                if (is_hunter(Z->sp[k].sp)) {
                    if (idx == s_hunt_n % (want ? want : 1)) {
                        pick = Z->sp[k].sp;
                    }
                    idx += Z->sp[k].n;
                }
            }
            if (pick < 0) {
                for (int k = 0; k < 10 && Z->sp[k].n; k++) {
                    if (is_hunter(Z->sp[k].sp)) {
                        pick = Z->sp[k].sp;
                        break;
                    }
                }
            }
            if (pick >= 0) {
                float x, y;
                far_place(&x, &y, 40, zone_ymax() - 20);
                life_t *e = life_new(pick, x, y);
                if (e) {
                    e->st = LS_IDLE;
                    s_hunt_n++;
                }
            }
        }
        s_hunt_t = 40 - fminf(s_steps, 10) * 1.5f;
    }
}

// ================================================================ the threats that the prey see
typedef struct {
    float x, y, r;
} threat_t;
static threat_t s_thr[10];
static int s_nthr;
static void threats_make(void)
{
    s_nthr = 0;
    if (B.dead == 0) {
        float fr = (34 + 0.30f * B.len) * B.alert;
        s_thr[s_nthr++] = (threat_t){B.x[0], B.y[0], fr * (B.dash_t > 0 ? 1.35f : 1)};
        int m = B.n / 2;
        s_thr[s_nthr++] = (threat_t){B.x[m], B.y[m], fr * 0.75f};
    }
    for (int i = 0; i < NLIFE && s_nthr < 8; i++) {
        life_t *e = &s_life[i];
        if (e->on && is_hunter(e->sp) && e->st != LS_IDLE) {
            s_thr[s_nthr++] = (threat_t){e->x, e->y, hunter_len(e) * 0.9f};
        }
    }
    if (R.n > 0 && R.dead == 0 && s_nthr < 10) {
        s_thr[s_nthr++] = (threat_t){R.x[0], R.y[0], 30 + 0.25f * R.len};
    }
}
// the way away from the nearest threat (false: no threat near)
static bool threat_near(float x, float y, float scale, float *ax, float *ay, float *dist)
{
    float best = 1e9f;
    bool any = false;
    for (int i = 0; i < s_nthr; i++) {
        float dx = wrapd(x - s_thr[i].x), dy = y - s_thr[i].y, d = sqrtf(dx * dx + dy * dy), r = s_thr[i].r * scale;
        if (d < r && d < best) {
            best = d;
            *ax = dx / (d + 0.01f);
            *ay = dy / (d + 0.01f);
            any = true;
        }
    }
    *dist = best;
    return any;
}

// ================================================================ the steps of the life
static void school_step(float dt)
{
    for (int s = 0; s < NSCHOOL; s++) {
        school_t *sc = &s_school[s];
        if (!sc->on) {
            continue;
        }
        sc->turn_t -= dt;
        if (sc->turn_t <= 0) {
            sc->tx = wrapx(sc->x + rnds() * 380);
            sc->ty = clampf(sc->y + rnds() * 120, 50, zone_ymax() - 40);
            sc->turn_t = 4 + rndf() * 4;
        }
        float ax = wrapd(sc->tx - sc->x), ay = sc->ty - sc->y, d = sqrtf(ax * ax + ay * ay) + 0.01f;
        float wx = ax / d * 22, wy = ay / d * 14;
        // the school as a whole moves away from the creature
        float fx, fy, fd;
        if (threat_near(sc->x, sc->y, 2.2f, &fx, &fy, &fd)) {
            wx += fx * 46;
            wy += fy * 30;
            sc->alarm = fmaxf(sc->alarm, 0.4f);
        }
        sc->vx += (wx - sc->vx) * clampf(dt * 1.5f, 0, 1);
        sc->vy += (wy - sc->vy) * clampf(dt * 1.5f, 0, 1);
        sc->x = wrapx(sc->x + (sc->vx + Z->flow * 0.5f) * dt);
        sc->y = clampf(sc->y + sc->vy * dt, 40, zone_ymax() - 30);
        sc->alarm *= expf(-dt * 1.2f);
    }
}
static void hunter_step(life_t *e, float dt);
static void prey_step(life_t *e, float dt)
{
    const spec_t *S = &SPEC[e->sp];
    float fx, fy, fd;
    switch (S->beh) {
    case B_SCHOOL: {
        school_t *sc = &s_school[e->school >= 0 ? e->school : 0];
        float R = 46 * sqrtf(sc->n / 170.0f) + 14;
        float sa = e->a0 + s_time * (0.35f + e->r0 * 0.2f);
        float ox = sc->x + cosf(sa) * e->r0 * R, oy = sc->y + sinf(sa) * e->r0 * R * 0.5f * (0.6f + 0.4f * e->z);
        float ax = wrapd(ox - e->x) * 1.6f - e->vx * 1.2f, ay = (oy - e->y) * 1.6f - e->vy * 1.2f;
        for (int i = 0; i < s_nthr; i++) {
            float dx = wrapd(e->x - s_thr[i].x), dy = e->y - s_thr[i].y, d2 = dx * dx + dy * dy,
                  r = s_thr[i].r * (1 + sc->alarm * 0.5f);
            if (d2 < r * r) {
                float d = sqrtf(d2) + 0.01f, k = (r - d) * 30;
                ax += dx / d * k;
                ay += dy / d * k;
            }
        }
        e->vx += ax * dt;
        e->vy += ay * dt;
        float sp = sqrtf(e->vx * e->vx + e->vy * e->vy), mx = 140 + sc->alarm * 50;
        if (sp > mx) {
            e->vx *= mx / sp;
            e->vy *= mx / sp;
        }
        break;
    }
    case B_DRIFT: {
        float bob = sinf(s_time * 0.8f + e->a0) * S->speed * 0.6f;
        if (e->sp == SP_JELLY || e->sp == SP_COMB) {
            e->t += dt / 2.0f;
            float pulse = fmaxf(0, sinf(e->t * 6.283f));
            e->vy += (-pulse * 16 + 3.5f - e->vy * 0.8f) * dt;
            e->vx += (cosf(e->a0 + s_time * 0.1f) * 4 - e->vx) * dt;
        } else {
            e->vx += (cosf(e->a0 + s_time * 0.2f) * S->speed - e->vx) * dt;
            e->vy += (bob - e->vy) * dt;
        }
        e->a += dt * (e->sp == SP_URCHIN ? 0.8f : 0);
        break;
    }
    case B_FLEE:
    case B_DART:
    case B_SLOW: {
        bool scared = S->sense > 0 && threat_near(e->x, e->y, S->sense / 40.0f, &fx, &fy, &fd);
        if (scared) {
            e->st = LS_FLEE, e->t = 2.0f;
        }
        if (e->st == LS_FLEE) {
            e->t -= dt;
            if (e->t <= 0) {
                e->st = LS_IDLE;
            }
            float sp = S->speed;
            if (scared) {
                e->a = atan2f(fy, fx) + sinf(s_time * 3 + e->ph) * 0.4f;
            }
            if (S->beh == B_DART) {
                // a squid darts: a hard push, then a glide
                e->cool -= dt;
                if (scared && e->cool <= 0) {
                    e->vx = cosf(e->a) * sp * 2.6f;
                    e->vy = sinf(e->a) * sp * 2.6f;
                    e->cool = 1.3f + rndf() * 0.5f;
                    if (e->sp == SP_VAMP) {
                        static const uint8_t RC[3] = {255, 75, 110};
                        part_t *p = part_new(PK_INK, e->x, e->y, -e->vx * 0.2f, -e->vy * 0.2f, 2.2f, 0.7f, RC);
                        (void)p;
                    }
                }
                e->vx *= expf(-1.4f * dt);
                e->vy *= expf(-1.4f * dt);
            } else {
                float hop = e->sp == SP_ANGEL ? (0.6f + 0.8f * fmaxf(0, sinf(s_time * 9 + e->ph))) : 1.0f;
                e->vx += (cosf(e->a) * sp * hop - e->vx) * clampf(dt * 3, 0, 1);
                e->vy += (sinf(e->a) * sp * hop - e->vy) * clampf(dt * 3, 0, 1);
            }
        } else {
            // a slow wander
            e->a += sinf(s_time * 0.5f + e->a0 * 3) * 0.7f * dt;
            float sp = S->speed * (S->beh == B_SLOW ? 0.5f : 0.3f);
            e->vx += (cosf(e->a) * sp - e->vx) * clampf(dt * 1.5f, 0, 1);
            e->vy += (sinf(e->a) * sp * 0.6f - e->vy) * clampf(dt * 1.5f, 0, 1);
            if (e->sp == SP_ISOPOD) {
                e->vy = 0;
                e->y = floor_y(e->x) - 4;
            }
        }
        break;
    }
    case B_HAZARD: {
        e->t += dt / 2.6f;
        float pulse = fmaxf(0, sinf(e->t * 6.283f));
        e->vy += (-pulse * 14 + 3 - e->vy * 0.8f) * dt;
        e->vx += (sinf(s_time * 0.2f + e->a0) * 3 - e->vx) * dt;
        break;
    }
    default:
        break;
    }
}
static void life_step(float dt)
{
    threats_make();
    school_step(dt);
    for (int i = 0; i < NLIFE; i++) {
        life_t *e = &s_life[i];
        if (!e->on) {
            continue;
        }
        if (e->gulp > 0) {
            e->gulp -= dt;
            if (e->gulp <= 0) {
                e->on = 0;
            }
            continue;
        }
        if (e->stun > 0) {
            e->stun -= dt;
            e->vx *= expf(-2 * dt);
            e->vy *= expf(-2 * dt);
        } else if (is_hunter(e->sp)) {
            hunter_step(e, dt);
        } else {
            prey_step(e, dt);
        }
        e->x = wrapx(e->x + (e->vx + Z->flow * 0.6f) * dt);
        e->y += e->vy * dt;
        float ymax = (Z->far & FAR_VENTS) ? floor_y(e->x) - e->sz : Z->h - 8;
        if (e->y < 10) {
            e->y = 10;
            if (e->vy < 0) {
                e->vy *= -0.5f;
            }
        }
        if (e->y > ymax) {
            e->y = ymax;
            if (e->vy > 0) {
                e->vy *= -0.5f;
            }
        }
        e->ph += dt;
    }
}

// ================================================================ the hunters
static void hunter_mouth(const life_t *e, float *mx, float *my)
{
    float L = hunter_len(e);
    *mx = wrapx(e->x + cosf(e->a) * L * 0.5f);
    *my = e->y + sinf(e->a) * L * 0.5f;
}
static void hunter_turn(life_t *e, float want, float rate, float dt)
{
    e->a = angd(e->a + clampf(angd(want - e->a), -rate * dt, rate * dt));
}
static void player_bitten(life_t *e, int seg);
static void hunter_step(life_t *e, float dt)
{
    const spec_t *S = &SPEC[e->sp];
    float L = hunter_len(e);
    float dx = wrapd(B.x[0] - e->x), dy = B.y[0] - e->y, d = sqrtf(dx * dx + dy * dy);
    bool alive = B.dead == 0 && s_state_play();
    bool threat = alive && B.len < L * 0.92f;  // the creature is food for it
    bool afraid = alive && B.len > L * 1.15f;  // the creature is bigger: it flees
    bool lurk = S->beh == B_LURK;
    float sp = 0, want = e->a, rate = lurk ? 2.2f : 1.7f;
    e->t += dt;
    switch (e->st) {
    case LS_IDLE:
        if (lurk) {
            sp = 3;
            want = e->a + sinf(s_time * 0.3f + e->a0) * 0.4f;
        } else {
            if (e->t > 3 || e->cool <= 0) {
                e->cool = 3 + rndf() * 3;
                e->r0 = rnds() * 1.2f;
            }
            e->cool -= dt;
            want = e->a + e->r0 * dt * 4;
            // (it smells the creature from far: a slow drift toward it, so that the hunters find it)
            if (threat && s_ink_blind <= 0 && d < 420) {
                want = atan2f(dy, dx) + sinf(s_time * 0.7f + e->a0) * 0.6f;
            }
            // (keep away from the top and the bottom of the zone)
            if (e->y < 40) {
                want = 0.5f * (e->a > 0 ? 1 : -1) + 1.57f;
            }
            if (e->y > zone_ymax() - 30) {
                want = -1.57f;
            }
            sp = S->speed * 0.42f;
        }
        if (afraid && d < 150) {
            e->st = LS_FLEE;
            e->t = 0;
        } else if (threat && s_ink_blind <= 0 && d < S->sense * B.alert * (lurk ? 0.55f : 1)) {
            e->st = lurk ? LS_COIL : LS_STALK;
            e->t = 0;
        }
        break;
    case LS_STALK: {
        // (the place of the creature a moment later)
        float tx = dx + B.vx * 0.35f, ty = dy + B.vy * 0.35f;
        want = atan2f(ty, tx);
        sp = S->speed * 0.72f;
        if (!threat || s_ink_blind > 0 || d > S->sense * 1.6f) {
            e->st = LS_IDLE;
            e->t = 0;
        } else if (d < L * 0.9f + 34) {
            e->st = LS_COIL;
            e->t = 0;
        }
        break;
    }
    case LS_COIL:
        // the warning: it stops, its eye lights up, then it lunges
        want = atan2f(dy + B.vy * 0.25f, dx + B.vx * 0.25f);
        sp = 0;
        rate = 3.2f;
        if (!threat || s_ink_blind > 0) {
            e->st = LS_IDLE;
            e->t = 0;
            break;
        }
        if (e->t > (lurk ? 0.28f : 0.38f)) {
            e->st = LS_LUNGE;
            e->t = 0;
            float v = lurk ? 250 : S->speed * 2.7f;
            e->vx = cosf(e->a) * v;
            e->vy = sinf(e->a) * v;
            float sx, sy;
            if (on_screen(e->x, e->y, 40, &sx, &sy)) {
                fx(FX_LUNGE, 70, 0.08f);
            }
        }
        break;
    case LS_LUNGE: {
        sp = -1;  // (the lunge keeps its own speed)
        rate = 0.6f;
        float mx, my;
        hunter_mouth(e, &mx, &my);
        float sd;
        int seg = body_near(&B, mx, my, &sd);
        float bite_r = L * 0.16f + seg_r(&B, seg) + 2;
        if (alive && sd < bite_r) {
            player_bitten(e, seg);
            e->st = LS_RECOIL;
            e->t = 0;
        } else if (e->t > (lurk ? 0.32f : 0.55f)) {
            e->st = LS_RECOIL;
            e->t = 0.8f;
        }
        e->vx *= expf(-1.2f * dt);
        e->vy *= expf(-1.2f * dt);
        break;
    }
    case LS_RECOIL:
        want = atan2f(-dy, -dx);
        sp = S->speed * 0.8f;
        if (e->t > (lurk ? 1.2f : 1.7f)) {
            e->st = LS_IDLE;
            e->t = 0;
        }
        break;
    case LS_FLEE:
        want = atan2f(-dy, -dx);
        sp = S->speed;
        if (e->t > 2.4f && (!afraid || d > 220)) {
            e->st = LS_IDLE;
            e->t = 0;
        }
        break;
    }
    hunter_turn(e, want, rate, dt);
    if (sp >= 0) {
        e->vx += (cosf(e->a) * sp - e->vx) * clampf(dt * 2.2f, 0, 1);
        e->vy += (sinf(e->a) * sp - e->vy) * clampf(dt * 2.2f, 0, 1);
    }
    // the stingers of the creature hurt a hunter that touches them
    if (B.cnt[T_STING] && e->stun <= 0 && alive) {
        for (int i = 1; i <= B.n; i++) {
            if (B.kind[i] != T_STING) {
                continue;
            }
            float tx = wrapd(e->x - B.x[i]), ty = e->y - B.y[i];
            if (tx * tx + ty * ty < sqr(L * 0.35f + 14 * B.S)) {
                e->stun = 1.6f;
                e->st = LS_RECOIL;
                e->t = 0;
                static const uint8_t PK[3] = {255, 95, 200};
                burst(e->x, e->y, 10, 50, PK, PK_SPARK);
                fx(FX_STING, 60, 0.1f);
                break;
            }
        }
    }
    // a hunter eats the small fish that come into its mouth (a fish less in the school)
    if (e->st == LS_IDLE && !lurk && ((int)(s_time * 10 + e->a0 * 10) % 8) == 0) {
        float mx, my;
        hunter_mouth(e, &mx, &my);
        for (int i = 0; i < NLIFE; i++) {
            life_t *f = &s_life[i];
            if (!f->on || f->school < 0 || f->gulp > 0) {
                continue;
            }
            if (sqr(wrapd(f->x - mx)) + sqr(f->y - my) < sqr(L * 0.2f)) {
                f->on = 0;
                break;
            }
        }
    }
}

// ================================================================ the rival (the midnight and the abyss)
static float s_rival_t;  // the time to the rival
static int s_rival_tgt = -1;
static float s_rival_think, s_rival_food, s_rival_recoil;
static void rival_spawn(void)
{
    float x, y;
    far_place(&x, &y, 60, zone_ymax() - 40);
    int n = clampi(B.n + rndi(-1, 1), 3, MAXSEG - 2);
    body_make(&R, x, y, 0, 1);
    static const uint8_t RT[] = {T_FIN, T_TAIL, T_SPINE, T_SHELL, T_STING, T_FIN, T_TAIL};
    for (int i = 0; i < n; i++) {
        body_add(&R, RT[rndi(0, (int)sizeof RT - 1)]);
    }
    if (rndf() < 0.7f) {
        body_add(&R, T_JAWS);
    }
    for (int i = 0; i <= R.n; i++) {
        R.grow[i] = 1;
        R.x[i] = wrapx(x - i * 7 * R.S);
        R.y[i] = y;
    }
    R.head_grow = 1;
    body_derive(&R);
    s_rival_tgt = -1;
    s_rival_food = 0;
    s_rival_recoil = 0;
}
static void rival_step(float dt)
{
    if (R.n <= 0 && R.dead == 0) {
        return;
    }
    if (R.dead > 0) {
        R.dead += dt;
        return;
    }
    float dx = wrapd(B.x[0] - R.x[0]), dy = B.y[0] - R.y[0], d = sqrtf(dx * dx + dy * dy);
    bool alive = B.dead == 0 && s_state_play();
    bool hunt = alive && R.len > B.len * 1.08f && s_ink_blind <= 0;
    bool flee = alive && B.len > R.len * 1.12f && d < 200;
    float wx = cosf(R.a), wy = sinf(R.a), m = 0.45f;
    s_rival_think -= dt;
    if (s_rival_recoil > 0) {
        s_rival_recoil -= dt;
        wx = -dx / (d + 0.01f);
        wy = -dy / (d + 0.01f);
        m = 0.9f;
    } else if (flee) {
        wx = -dx / (d + 0.01f);
        wy = -dy / (d + 0.01f);
        m = 1;
        if (d < 70) {
            body_dash(&R);
        }
    } else if (hunt && d < 240) {
        wx = dx / d;
        wy = dy / d;
        m = 1;
        if (d < 60 + R.len * 0.2f) {
            body_dash(&R);
        }
    } else {
        // it hunts the prey that it can swallow
        if (s_rival_think <= 0) {
            s_rival_think = 0.5f;
            s_rival_tgt = -1;
            float bd = 260 * 260;
            for (int i = 0; i < NLIFE; i++) {
                life_t *e = &s_life[i];
                if (!e->on || e->gulp > 0 || is_hunter(e->sp) || e->sz > R.gape) {
                    continue;
                }
                float q = sqr(wrapd(e->x - R.x[0])) + sqr(e->y - R.y[0]);
                if (q < bd) {
                    bd = q;
                    s_rival_tgt = i;
                }
            }
        }
        if (s_rival_tgt >= 0 && s_life[s_rival_tgt].on) {
            life_t *e = &s_life[s_rival_tgt];
            float tx = wrapd(e->x - R.x[0]), ty = e->y - R.y[0], td = sqrtf(tx * tx + ty * ty) + 0.01f;
            wx = tx / td;
            wy = ty / td;
            m = 0.8f;
            if (td < 40) {
                body_dash(&R);
            }
        } else {
            wx = cosf(R.a + sinf(s_time * 0.4f) * 0.8f);
            wy = sinf(R.a + sinf(s_time * 0.4f) * 0.8f);
            m = 0.4f;
        }
    }
    body_move(&R, dt, wx, wy, m);
    // it eats what its mouth touches
    float mx, my, reach;
    body_mouth(&R, &mx, &my, &reach);
    for (int i = 0; i < NLIFE; i++) {
        life_t *e = &s_life[i];
        if (!e->on || e->gulp > 0 || is_hunter(e->sp) || e->sp == SP_BIGJELLY || e->sz > R.gape) {
            continue;
        }
        if (sqr(wrapd(e->x - mx)) + sqr(e->y - my) < sqr(reach + e->sz)) {
            e->gulp = 0.12f;
            s_rival_food += SPEC[e->sp].food;
            if (s_rival_food > 9 && R.n < MAXSEG - 1) {
                s_rival_food = 0;
                body_add(&R, SPEC[e->sp].trait == T_LURE ? T_FIN : SPEC[e->sp].trait);
            }
        }
    }
    // it bites the creature when it hunts it
    if (hunt && alive && R.dash_t > 0) {
        float sd;
        int seg = body_near(&B, mx, my, &sd);
        if (sd < reach + seg_r(&B, seg)) {
            life_t fake = {0};
            fake.sp = SP_N;
            player_bitten(&fake, seg);
            s_rival_recoil = 2.4f;
        }
    }
}

// ================================================================ the creature eats, bites and is bitten
static void player_bitten(life_t *e, int seg)
{
    int kind, r = body_bite(&B, seg, &kind);
    const uint8_t *c = TRAIT[kind].c;
    if (r == BITE_STOP) {
        if (kind == T_SHELL) {
            fx(FX_SHELL, 80, 0.06f);
            cam_kick(3);
            static const uint8_t AM[3] = {255, 196, 90};
            burst(B.x[seg > B.n ? B.n : seg], B.y[seg > B.n ? B.n : seg], 12, 60, AM, PK_SPARK);
        }
        if (e->sp < SP_N) {
            e->stun = 0.6f;
        }
        return;
    }
    if (r == BITE_SPINE) {
        fx(FX_PRICK, 80, 0.1f);
        cam_kick(3);
        if (e->sp < SP_N) {
            e->stun = 2.0f;
            e->hp -= 1;
        } else {
            s_rival_recoil = 3;
            R.stun = 1.2f;
        }
        static const uint8_t VI[3] = {183, 124, 255};
        burst(B.x[seg > B.n ? B.n : seg], B.y[seg > B.n ? B.n : seg], 14, 70, VI, PK_SPARK);
        return;
    }
    if (r == BITE_DEAD) {
        s_cause = e->sp;
        fx(FX_BITTEN, 100, 0);
        cam_kick(9);
        return;
    }
    // a segment is torn off: it sinks away as light
    fx(FX_BITTEN, 95, 0.08f);
    cam_kick(7);
    float bx = B.x[seg > B.n ? B.n : seg], by = B.y[seg > B.n ? B.n : seg];
    for (int k = 0; k < 16; k++) {
        float a = rndf() * 6.283f, v = 20 + rndf() * 50;
        part_new(PK_BLOOD, bx, by, cosf(a) * v, sinf(a) * v + 10, 1.0f + rndf() * 0.8f, 1,
                 kind == T_NONE ? (const uint8_t *)"\x63\xea\xff" : c);
    }
    B.kx += cosf(e->sp < SP_N ? e->a : R.a) * 70;
    B.ky += sinf(e->sp < SP_N ? e->a : R.a) * 70;
}
static void eat_life(life_t *e, float food)
{
    const spec_t *S = &SPEC[e->sp];
    eat_food(S->trait, food, e->x, e->y);
    s_eaten++;
    const uint8_t *c = TRAIT[S->trait].c;
    burst(e->x, e->y, food >= 2 ? 10 : 5, 40, c, PK_SPARK);
    if (e->sp == SP_OSTRA) {
        static const uint8_t BL[3] = {90, 170, 255};
        burst(e->x, e->y, 14, 70, BL, PK_TRAIL);
    }
    if (food >= 2.5f) {
        fx(FX_GULP, 80, 0.08f);
        C.punch = fmaxf(C.punch, 0.06f);
    } else {
        s_last_sfx_eat ^= 1;
        fx(s_last_sfx_eat ? FX_EAT : FX_EAT2, 62, 0.12f);
    }
}
static void player_feed(float dt)
{
    (void)dt;
    if (B.dead > 0) {
        return;
    }
    float mx, my, reach;
    body_mouth(&B, &mx, &my, &reach);
    for (int i = 0; i < NLIFE; i++) {
        life_t *e = &s_life[i];
        if (!e->on || e->gulp > 0) {
            continue;
        }
        float dx = wrapd(e->x - mx), dy = e->y - my;
        float bigr = is_hunter(e->sp) ? hunter_len(e) * 0.32f : e->sp == SP_BIGJELLY ? e->sz * 0.9f : e->sz;
        if (dx * dx + dy * dy > sqr(reach + bigr)) {
            continue;
        }
        const spec_t *S = &SPEC[e->sp];
        if (!is_hunter(e->sp) && e->sz <= B.gape) {
            // swallowed: it shrinks into the mouth
            e->gulp = 0.12f;
            e->vx = -dx * 6;
            e->vy = -dy * 6;
            eat_life(e, S->food);
            continue;
        }
        // too big to swallow: a dash with jaws (or a body as long as it) bites a piece of it
        bool can = B.dash_t > 0 && e->stun <= 0.3f &&
                   (B.jaws > 0 || B.len > (is_hunter(e->sp) ? hunter_len(e) : e->sz * 3) * 0.8f);
        if (can) {
            e->hp -= 1 + 0.5f * B.jaws;
            e->stun = 0.5f;
            if (is_hunter(e->sp)) {
                e->st = LS_FLEE;
                e->t = 0;
            }
            fx(FX_BITE, 85, 0.1f);
            cam_kick(2.5f);
            C.punch = fmaxf(C.punch, 0.05f);
            if (e->hp <= 0) {
                e->gulp = 0.2f;
                eat_life(e, S->food * 0.7f);
            } else {
                eat_food(S->trait, 1.2f, e->x, e->y);
            }
            B.dash_t = 0;
        } else if (e->sp == SP_BIGJELLY) {
            // the bell of the big jellyfish: it stings
        } else {
            // a bump: the creature is pushed back a little
            B.kx -= dx * 3;
            B.ky -= dy * 3;
        }
    }
    // the plankton: food that the mouth takes as it swims through it
    for (int i = 0; i < NPLANK; i++) {
        plank_t *p = &s_plank[i];
        if (!p->on) {
            continue;
        }
        float dx = wrapd(p->x - mx), dy = p->y - my;
        if (dx * dx + dy * dy < sqr(reach + 2.5f)) {
            p->on = 0;
            eat_food(T_NONE, 0.32f, p->x, p->y);
            fx(FX_PLANK, 26, 0.2f);
        }
    }
    // the rival: a dash tears off one of its segments (the creature then eats its trait)
    if (R.n >= 0 && R.dead == 0 && (R.n > 0 || R.len > 0) && B.dash_t > 0) {
        float sd;
        int seg = body_near(&R, mx, my, &sd);
        if (sd < reach + seg_r(&R, seg) && (B.len > R.len * 0.7f || B.jaws > 0)) {
            int kind, r = body_bite(&R, seg, &kind);
            if (r == BITE_TORN) {
                eat_food(kind, 2.5f, R.x[seg > R.n ? R.n : seg], R.y[seg > R.n ? R.n : seg]);
                s_eaten++;
                fx(FX_BITE, 90, 0.1f);
                cam_kick(3);
                s_rival_recoil = 2;
            } else if (r == BITE_DEAD) {
                eat_food(T_JAWS, 4, R.x[0], R.y[0]);
                s_eaten++;
                fx(FX_GULP, 90, 0);
                C.punch = 0.08f;
            } else if (r == BITE_SPINE) {
                B.stun = 0.6f;
                fx(FX_PRICK, 70, 0.1f);
            } else {
                fx(FX_SHELL, 70, 0.1f);
            }
            B.dash_t = 0;
        }
    }
}
// the stingers of the creature stun the prey that touch them; the big jellyfish stings the creature
static void stings_step(float dt)
{
    (void)dt;
    if (B.dead > 0) {
        return;
    }
    if (B.cnt[T_STING]) {
        for (int i = 1; i <= B.n; i++) {
            if (B.kind[i] != T_STING) {
                continue;
            }
            float tx = B.x[i] - cosf(B.a) * 8 * B.S, ty = B.y[i] - sinf(B.a) * 8 * B.S, r = 14 * B.S;
            for (int k = 0; k < NLIFE; k++) {
                life_t *e = &s_life[k];
                if (!e->on || e->gulp > 0 || is_hunter(e->sp) || e->stun > 0 || e->sp == SP_BIGJELLY) {
                    continue;
                }
                if (sqr(wrapd(e->x - tx)) + sqr(e->y - ty) < sqr(r + e->sz)) {
                    e->stun = 2.2f;
                    if (e->school >= 0) {
                        e->school = -1, e->sp = e->sp;  // (a stunned fish leaves its school)
                    }
                    static const uint8_t PK[3] = {255, 95, 200};
                    burst(e->x, e->y, 4, 30, PK, PK_SPARK);
                }
            }
        }
    }
    if (B.hurt > 0 || B.stun > 0) {
        return;
    }
    for (int k = 0; k < NLIFE; k++) {
        life_t *e = &s_life[k];
        if (!e->on || e->sp != SP_BIGJELLY || e->gulp > 0) {
            continue;
        }
        // the tentacles hang under the bell: a box under it
        float L = e->sz * 4.0f;
        for (int i = 0; i <= B.n; i += (B.n > 6 ? 2 : 1)) {
            float dx = wrapd(B.x[i] - e->x), dy = B.y[i] - e->y;
            if (fabsf(dx) < e->sz * 1.1f && dy > -e->sz * 0.3f && dy < L) {
                if (B.cnt[T_STING] || (B.kind[i] == T_SHELL && B.plate[i] > 0)) {
                    B.hurt = 0.5f;
                    break;
                }
                B.stun = 0.9f;
                B.hurt = 1.2f;
                B.kx += (dx > 0 ? 1 : -1) * 60;
                B.ky -= 30;
                fx(FX_STING, 90, 0.05f);
                cam_kick(4);
                static const uint8_t PK[3] = {255, 95, 200};
                burst(B.x[i], B.y[i], 12, 60, PK, PK_SPARK);
                break;
            }
        }
    }
}

// ================================================================ the life on the screen
// how well a thing shows in the dark: near the light of the creature, or in a PING
static float vis_at(float x, float y)
{
    if (Z->dark < 0.05f) {
        return 1;
    }
    float d = sqrtf(sqr(wrapd(x - B.x[0])) + sqr(y - B.y[0]));
    float v = clampf(1.35f - d / fmaxf(B.light, 20), 0, 1);
    v = fmaxf(v, s_reveal);
    return lerpf(1, v, Z->dark) * 0.85f + 0.15f;
}
// the light of the trait in a prey (it shows in the dark too)
static void gene_dot(float sx, float sy, int t, float r, float k)
{
    const uint8_t *c = TRAIT[t].c;
    float p = 0.7f + 0.3f * sinf(s_time * 4 + sx * 0.05f);
    glow(sx, sy, r * 2.2f, c[0], c[1], c[2], 0.55f * p * k);
    dot_add(sx, sy, (int)(255 * k), (int)(255 * k), (int)(255 * k));
}
static void draw_fish(const life_t *e, float sx, float sy, float vis)
{
    float a = atan2f(e->vy, e->vx), ca = cosf(a), sa = sinf(a);
    float s = 0.75f + (e->z + 1) * 0.3f, l = 2.6f * s * C.z * e->sz / 2.2f, dep = (0.45f + (e->z + 1) * 0.28f) * vis;
    float fl = fabsf(sinf(a * 3 + s_time * 0.8f + e->ph));
    fl *= fl;
    fl *= fl;
    fl *= fl;
    fl *= fl;
    if (e->stun > 0) {
        fl = 0.4f + 0.3f * sinf(s_time * 20);
    }
    int r, g, b;
    if (e->sp == SP_LANTERN) {
        r = (int)(40 + fl * 150);
        g = (int)(62 + fl * 160);
        b = (int)(86 + fl * 150);
    } else {
        r = (int)(60 + fl * 190);
        g = (int)(110 + fl * 140);
        b = (int)(150 + fl * 100);
    }
    if (l < 1.6f) {
        dot_mix(sx, sy, r, g, b, (int)(dep * 256));
        return;
    }
    ell_mix(sx, sy, ca, sa, l, l * 0.36f, r, g, b, dep);
    float t[8] = {sx - ca * l * 0.8f,
                  sy - sa * l * 0.8f,
                  sx - ca * l * 1.55f + sa * l * 0.42f,
                  sy - sa * l * 1.55f - ca * l * 0.42f,
                  sx - ca * l * 1.4f,
                  sy - sa * l * 1.4f,
                  sx - ca * l * 1.55f - sa * l * 0.42f,
                  sy - sa * l * 1.55f + ca * l * 0.42f};
    poly_mix(t, 4, r, g, b, (int)(dep * 256));
    if (l > 3) {
        line_mix(sx - ca * l * 0.6f - sa * l * 0.05f, sy - sa * l * 0.6f + ca * l * 0.05f,
                 sx + ca * l * 0.6f - sa * l * 0.05f, sy + sa * l * 0.6f + ca * l * 0.05f, 210, 240, 255,
                 (int)(dep * (0.35f + fl * 0.6f) * 256));
    }
    if (e->sp == SP_LANTERN) {
        float bl = 0.5f + 0.5f * sinf(s_time * 5 + e->ph * 3);
        dot_add(sx - sa * l * 0.25f, sy + ca * l * 0.25f, (int)(80 * bl), (int)(200 * bl), (int)(255 * bl));
    }
}
static void draw_jelly(const life_t *e, float sx, float sy, float scale, float vis, bool big)
{
    // the look page jellyfish: a clear bell, tentacles that trail, frilled arms, lights on the rim
    float pulse = fmaxf(0, sinf(e->t * 6.283f)), bw = 17 * (1 - 0.2f * pulse) * scale,
          bh = 13 * (1 + 0.12f * pulse) * scale;
    float jx = sx, jy = sy;
    if (big) {
        glow(jx, jy, 46 * scale, 255, 95, 200, 0.12f * vis);
    }
    int nt = big ? 11 : 5;
    for (int k = 0; k < nt; k++) {
        float rx = jx - bw + (k / (float)(nt - 1)) * bw * 2;
        float px0 = rx, py0 = jy + 1;
        int segs = big ? 14 : 6;
        for (int j = 1; j <= segs; j++) {
            float px = rx + sinf(s_time * 1.6f - j * 0.42f + k) * j * 0.5f * scale + (rx - jx) * j * 0.02f,
                  py = jy + 1 + j * 4.6f * scale;
            float m = k / (float)(nt - 1);
            line_add(px0, py0, px, py, (int)(lerpf(255, 183, m) * 0.32f * vis), (int)(lerpf(95, 124, m) * 0.32f * vis),
                     (int)(lerpf(200, 255, m) * 0.32f * vis));
            px0 = px;
            py0 = py;
        }
    }
    if (big) {
        for (int k = 0; k < 4; k++) {
            float ax = jx + (-5 + k * 3.4f) * scale, px0 = ax, py0 = jy;
            for (int j = 1; j <= 8; j++) {
                float px = ax + sinf(s_time * 2.2f - j * 0.7f + k * 1.3f) * 2.4f * scale, py = jy + j * 3.6f * scale;
                line_add(px0, py0, px, py, (int)(255 * 0.45f * vis), (int)(95 * 0.45f * vis), (int)(200 * 0.45f * vis));
                line_add(px0 + 0.8f, py0, px + 0.8f, py, (int)(255 * 0.25f * vis), (int)(95 * 0.25f * vis),
                         (int)(200 * 0.25f * vis));
                px0 = px;
                py0 = py;
            }
        }
    }
    // the bell: a half ellipse with a bright rim
    lut_t L[LUT_N];
    const float c0[3] = {255, 170, 230}, c1[3] = {255, 150, 230}, c2[3] = {255, 200, 250};
    lut_3(L, c0, 0.10f * vis, 0.7f, c1, 0.22f * vis, c2, 0.55f * vis, bh);
    clip_box(c_x0, c_y0, c_x1, (int)(jy + 2 * scale) + 1);
    ell_lut(jx, jy + 2 * scale, 1, 0, bw, bh * 1.25f, L, true);
    clip_full();
    for (int k = 0; k < 16; k++) {
        float px = jx - bw + (k / 15.0f) * bw * 2, f = 0.5f + 0.5f * sinf(s_time * 4 + k);
        dot_add(px, jy + 2.4f * scale, (int)(255 * f * vis), (int)(240 * f * vis), (int)(255 * f * vis));
    }
    if (big) {
        for (int k = 0; k < 4; k++) {
            float an = -1.5708f + (k - 1.5f) * 0.55f;
            glow(jx + cosf(an) * bw * 0.35f, jy - bh * 0.35f + sinf(an) * bh * 0.2f, 2.6f * scale, 255, 140, 120,
                 0.5f * vis);
        }
    }
}
static void draw_life_one(const life_t *e)
{
    const spec_t *S = &SPEC[e->sp];
    float m = is_hunter(e->sp) ? hunter_len(e) * C.z : e->sp == SP_BIGJELLY ? 70 * C.z : 30;
    float sx, sy;
    if (!on_screen(e->x, e->y, m, &sx, &sy)) {
        return;
    }
    float z = C.z, vis = vis_at(e->x, e->y);
    float gk = e->gulp > 0 ? e->gulp / 0.12f : 1;
    float sz = e->sz * z * gk;
    float a = e->a, ca = cosf(a), sa = sinf(a);
    switch (e->sp) {
    case SP_SPRAT:
    case SP_LANTERN:
        draw_fish(e, sx, sy, vis);
        return;
    case SP_EEL: {
        // a clear leaf: a long thin ribbon that waves
        float va = atan2f(e->vy, e->vx + 0.001f), cv = cosf(va), sv = sinf(va), L = S->len * 0.5f * z * gk;
        for (int k = -3; k <= 3; k++) {
            float f = k / 3.0f, w = sinf(s_time * 6 - k * 0.9f) * 1.6f * z;
            float px = sx + cv * f * L - sv * w, py = sy + sv * f * L + cv * w;
            ell_add(px, py, cv, sv, L * 0.22f, sz * 0.55f * (1 - fabsf(f) * 0.6f), 170, 230, 255, 0.22f * vis);
        }
        dot_mix(sx + cv * L, sy + sv * L, 10, 20, 30, (int)(200 * vis));
        gene_dot(sx, sy, S->trait, 1.0f * z, gk);
        return;
    }
    case SP_URCHIN: {
        for (int k = 0; k < 6; k++) {
            float an = e->a + k * 1.047f, L = sz * (1.6f + 0.3f * (k & 1));
            line_add(sx, sy, sx + cosf(an) * L, sy + sinf(an) * L, (int)(200 * 0.7f * vis), (int)(170 * 0.7f * vis),
                     (int)(255 * 0.7f * vis));
        }
        ell_add(sx, sy, 1, 0, sz * 0.6f, sz * 0.6f, 200, 170, 255, 0.4f * vis);
        gene_dot(sx, sy, S->trait, 0.8f * z, gk);
        return;
    }
    case SP_JELLY:
    case SP_COMB: {
        if (e->sp == SP_JELLY) {
            draw_jelly(e, sx, sy, sz / 17.0f * 2.2f, vis, false);
        } else {
            ell_add(sx, sy, 0, 1, sz * 1.3f, sz * 0.9f, 170, 220, 255, 0.22f * vis);
            for (int r = 0; r < 4; r++) {
                int hue = (int)(s_time * 140 + r * 60 + e->ph * 50) % 360;
                int cr, cg, cb;
                unpack8(hsv(hue, 255, 255), &cr, &cg, &cb);
                float bx = sx - sz * 0.75f + r * sz * 0.5f, by = sy - sz * 0.9f + fmodf(s_time * 8 + r, 3) * z * 0.5f;
                line_add(bx, by, bx, by + sz * 1.4f, cr * 3 / 4, cg * 3 / 4, cb * 3 / 4);
            }
        }
        gene_dot(sx, sy + (e->sp == SP_JELLY ? -sz * 0.2f : 0), S->trait, 1.0f * z, gk);
        return;
    }
    case SP_ANGEL: {
        float fl = sinf(s_time * 9 + e->ph);
        ell_mix(sx, sy, 0, 1, sz * 0.9f, sz * 0.45f, 255, 190, 140, 0.8f * vis);
        for (int sd = -1; sd <= 1; sd += 2) {
            float wa = -1.5708f + sd * (0.9f + 0.6f * fl);
            ell_add(sx + cosf(wa) * sz * 0.9f, sy + sinf(wa) * sz * 0.9f, cosf(wa), sinf(wa), sz * 0.95f, sz * 0.45f,
                    255, 170, 120, 0.45f * vis);
        }
        gene_dot(sx, sy, S->trait, 0.9f * z, gk);
        return;
    }
    case SP_OSTRA: {
        float bl = 0.5f + 0.5f * sinf(s_time * 2.5f + e->ph);
        ell_mix(sx, sy, ca, sa, sz * 1.1f, sz * 0.85f, 230, 190, 120, 0.85f * vis);
        line_mix(sx - ca * sz, sy - sa * sz, sx + ca * sz, sy + sa * sz, 140, 100, 50, (int)(180 * vis));
        glow(sx, sy, sz * 2.4f, 90, 170, 255, 0.35f * bl);
        gene_dot(sx, sy, S->trait, 0.8f * z, gk);
        return;
    }
    case SP_SQUID:
    case SP_VAMP:
    case SP_GLASSQ: {
        float va = atan2f(e->vy, e->vx + 0.001f);
        if (e->st != LS_FLEE) {
            va = e->a;
        }
        float cv = cosf(va), sv = sinf(va), L = S->len * 0.5f * z * gk;
        bool vamp = e->sp == SP_VAMP, glass = e->sp == SP_GLASSQ;
        int br = vamp ? 150 : glass ? 200 : 255, bg = vamp ? 40 : glass ? 224 : 170, bb = vamp ? 60 : glass ? 255 : 120;
        float al = glass ? 0.18f : 0.8f;
        // the mantle (the front is the way it swims: the arms trail)
        if (glass) {
            ell_add(sx, sy, cv, sv, L * 0.7f, sz * 0.9f, br, bg, bb, al * vis);
        } else {
            ell_mix(sx, sy, cv, sv, L * 0.7f, sz * 0.75f, br, bg, bb, al * vis);
        }
        for (int k = -2; k <= 2; k++) {
            float bx = sx - cv * L * 0.6f, by = sy - sv * L * 0.6f, px0 = bx, py0 = by;
            for (int j = 1; j <= 4; j++) {
                float w = sinf(s_time * 5 - j + k) * 1.2f * z + k * 0.9f * z * j * 0.5f;
                float px = bx - cv * L * 0.25f * j - sv * w, py = by - sv * L * 0.25f * j + cv * w;
                line_mix(px0, py0, px, py, br, bg, bb, (int)(al * 200 * vis));
                px0 = px;
                py0 = py;
            }
        }
        if (vamp) {
            glow(sx + cv * L * 0.5f, sy + sv * L * 0.5f, 3 * z, 90, 170, 255, 0.8f);
            glow(sx - cv * L * 0.5f, sy - sv * L * 0.5f, 3 * z, 90, 170, 255, 0.5f + 0.4f * sinf(s_time * 3));
        } else if (!glass) {
            for (int k = 0; k < 3; k++) {
                dot_add(sx + cv * (k - 1) * L * 0.3f, sy + sv * (k - 1) * L * 0.3f, 200, 90, 40);
            }
        } else {
            for (int k = 0; k < 3; k++) {
                dot_add(sx + cv * (k - 1) * L * 0.3f - sv * sz * 0.4f, sy + sv * (k - 1) * L * 0.3f + cv * sz * 0.4f,
                        160, 220, 255);
            }
        }
        gene_dot(sx, sy, S->trait, 1.0f * z, gk);
        return;
    }
    case SP_FANG:
    case SP_ALARVA: {
        float va = atan2f(e->vy, e->vx + 0.001f), cv = cosf(va), sv = sinf(va);
        if (e->sp == SP_FANG) {
            ell_mix(sx, sy, cv, sv, sz * 1.5f, sz * 0.9f, 120, 110, 130, 0.9f * vis);
            float tx = sx + cv * sz * 1.5f, ty = sy + sv * sz * 1.5f;
            line_mix(tx, ty, tx - sv * sz * 0.7f - cv * sz * 0.3f, ty + cv * sz * 0.7f - sv * sz * 0.3f, 240, 240, 255,
                     (int)(230 * vis));
            line_mix(tx - cv * sz * 0.5f, ty - sv * sz * 0.5f, tx - cv * sz * 0.5f + sv * sz * 0.6f,
                     ty - sv * sz * 0.5f - cv * sz * 0.6f, 240, 240, 255, (int)(230 * vis));
            float t[6] = {sx - cv * sz * 1.3f,
                          sy - sv * sz * 1.3f,
                          sx - cv * sz * 2.2f + sv * sz * 0.7f,
                          sy - sv * sz * 2.2f - cv * sz * 0.7f,
                          sx - cv * sz * 2.2f - sv * sz * 0.7f,
                          sy - sv * sz * 2.2f + cv * sz * 0.7f};
            poly_mix(t, 3, 100, 90, 110, (int)(220 * vis));
        } else {
            disc_mix(sx, sy, sz * 0.9f, 130, 120, 110, 0.85f * vis);
            float lx = sx + cv * sz * 1.2f - sv * sz * 1.4f, ly = sy + sv * sz * 1.2f + cv * sz * 1.4f;
            line_add(sx + cv * sz * 0.5f, sy + sv * sz * 0.5f, lx, ly, 100, 120, 90);
            glow(lx, ly, 4 * z, 157, 255, 106, 0.6f + 0.3f * sinf(s_time * 4 + e->ph));
        }
        gene_dot(sx, sy, S->trait, 0.9f * z, gk);
        return;
    }
    case SP_ISOPOD: {
        float va = e->vx >= 0 ? 0 : 3.14159f, cv = cosf(va), sv = sinf(va);
        ell_mix(sx, sy, cv, sv, sz * 1.4f, sz * 0.75f, 200, 180, 150, 0.95f * vis);
        for (int k = -2; k <= 2; k++) {
            line_mix(sx + cv * k * sz * 0.45f, sy - sz * 0.7f, sx + cv * k * sz * 0.45f, sy + sz * 0.2f, 120, 100, 80,
                     (int)(200 * vis));
        }
        for (int k = -3; k <= 3; k++) {
            float lg = sinf(s_time * 8 + k) * 1.5f * z;
            line_mix(sx + cv * k * sz * 0.35f, sy + sz * 0.5f, sx + cv * k * sz * 0.35f + lg, sy + sz * 1.0f, 160, 140,
                     110, (int)(200 * vis));
        }
        gene_dot(sx, sy - sz * 0.2f, S->trait, 1.0f * z, gk);
        return;
    }
    case SP_BIGJELLY: {
        draw_jelly(e, sx, sy, z * e->sz / 14.0f, vis, true);
        gene_dot(sx, sy - 4 * z, S->trait, 1.2f * z, gk);
        return;
    }
    default:
        break;
    }
    // ---------------- the hunters
    float L = hunter_len(e) * z * gk;
    bool coil = e->st == LS_COIL, lunge = e->st == LS_LUNGE;
    float eye = coil ? 1 : lunge ? 0.8f : 0.3f;
    float stun = e->stun > 0 ? 0.5f + 0.5f * sinf(s_time * 25) : 0;
    if (e->sp == SP_MACKEREL) {
        // a fast fish: a dark back with stripes, a silver belly, a forked tail
        float bend = sinf(e->ph * 9) * 0.12f;
        ell_mix(sx, sy, ca, sa, L * 0.5f, L * 0.13f, 170, 200, 220, 0.95f * vis);
        ell_mix(sx - sa * L * 0.035f * -1, sy + ca * L * 0.035f * -1, ca, sa, L * 0.46f, L * 0.07f, 40, 90, 110,
                0.9f * vis);
        for (int k = -3; k <= 3; k++) {
            float bx = sx + ca * k * L * 0.1f + sa * L * 0.09f, by = sy + sa * k * L * 0.1f - ca * L * 0.09f;
            line_mix(bx, by, bx + ca * L * 0.03f - sa * L * 0.05f, by + sa * L * 0.03f + ca * L * 0.05f, 20, 40, 60,
                     (int)(220 * vis));
        }
        float tx = sx - ca * L * 0.48f, ty = sy - sa * L * 0.48f, ta = a + 3.14159f + bend;
        for (int sd = -1; sd <= 1; sd += 2) {
            float t[6] = {tx,
                          ty,
                          tx + cosf(ta + sd * 0.5f) * L * 0.2f,
                          ty + sinf(ta + sd * 0.5f) * L * 0.2f,
                          tx + cosf(ta + sd * 0.2f) * L * 0.12f,
                          ty + sinf(ta + sd * 0.2f) * L * 0.12f};
            poly_mix(t, 3, 120, 150, 170, (int)(240 * vis));
        }
        float ex = sx + ca * L * 0.36f + sa * L * 0.03f, ey = sy + sa * L * 0.36f - ca * L * 0.03f;
        disc_mix(ex, ey, L * 0.035f + 0.8f, 250, 250, 250, vis);
        dot_mix(ex + ca * 0.5f, ey + sa * 0.5f, 10, 10, 20, 240);
        if (eye > 0.5f) {
            glow(ex, ey, 6 * z, 255, 90, 60, 0.6f * eye);
        }
    } else if (e->sp == SP_DRAGON || e->sp == SP_GULPER) {
        // a long dark body that waves; the dragonfish has a red light under its eye, the gulper a huge mouth
        bool gul = e->sp == SP_GULPER;
        int nseg = gul ? 12 : 8;
        float px0 = sx, py0 = sy;
        for (int k = 0; k <= nseg; k++) {
            float f = (float)k / nseg, w = sinf(e->ph * 6 - k * 0.7f) * L * 0.04f * f;
            float px = sx - ca * L * (f - 0.3f) - sa * w, py = sy - sa * L * (f - 0.3f) + ca * w;
            float th = (gul ? (k == 0 ? 0.22f : 0.06f * (1 - f) + 0.015f) : 0.08f * (1 - f * 0.7f)) * L;
            if (k > 0) {
                tline_mix(px0, py0, px, py, fmaxf(1.2f, th), 34, 30, 46, (int)(240 * vis));
            }
            if (!gul && (k % 2) == 1) {
                dot_add(px + sa * th * 0.4f, py - ca * th * 0.4f, 70, 140, 255);
            }
            px0 = px;
            py0 = py;
        }
        if (gul) {
            float open = lunge ? 1 : coil ? 0.7f : 0.25f + 0.1f * sinf(s_time * 2);
            float hx = sx + ca * L * 0.3f, hy = sy + sa * L * 0.3f;
            float t[8] = {hx - ca * L * 0.05f,
                          hy - sa * L * 0.05f,
                          hx + ca * L * 0.22f - sa * L * 0.16f * open,
                          hy + sa * L * 0.22f + ca * L * 0.16f * open,
                          hx + ca * L * 0.22f + sa * L * 0.16f * open,
                          hy + sa * L * 0.22f - ca * L * 0.16f * open,
                          hx - ca * L * 0.05f,
                          hy - sa * L * 0.05f};
            poly_mix(t, 3, 14, 10, 20, (int)(250 * vis));
            line_mix(t[2], t[3], t[0], t[1], 90, 60, 110, (int)(200 * vis));
            line_mix(t[4], t[5], t[0], t[1], 90, 60, 110, (int)(200 * vis));
            glow(px0, py0, 5 * z, 255, 120, 200, 0.7f + 0.3f * sinf(s_time * 3));
            glow(hx, hy, 3 * z, 255, 80, 80, 0.4f + 0.6f * eye);
        } else {
            float hx = sx + ca * L * 0.3f, hy = sy + sa * L * 0.3f;
            ell_mix(hx, hy, ca, sa, L * 0.1f, L * 0.07f, 30, 26, 40, 0.95f * vis);
            glow(hx - sa * L * 0.03f, hy + ca * L * 0.03f, 5 * z, 255, 60, 70, 0.55f + 0.45f * eye);
            float bx = hx + ca * L * 0.05f + sa * L * 0.1f, by = hy + sa * L * 0.05f - ca * L * 0.1f * -1;
            line_add(hx, hy + L * 0.04f, bx, by + L * 0.12f, 60, 80, 100);
            glow(bx, by + L * 0.12f, 3 * z, 120, 200, 255, 0.6f);
            if (lunge || coil) {
                for (int k = 0; k < 3; k++) {
                    float fx0 = hx + ca * L * (0.06f + k * 0.02f), fy0 = hy + sa * L * (0.06f + k * 0.02f);
                    line_mix(fx0, fy0, fx0 - sa * L * 0.04f, fy0 + ca * L * 0.04f, 240, 240, 255, 230);
                }
            }
        }
    } else if (e->sp == SP_ANGLER) {
        // a round dark body, a mouth full of needles, a lure with a green light
        float open = lunge ? 1 : coil ? 0.8f : 0.2f;
        ell_mix(sx, sy, ca, sa, L * 0.5f, L * 0.4f, 48, 40, 40, 0.97f * vis);
        float mx = sx + ca * L * 0.38f, my = sy + sa * L * 0.38f;
        float t[6] = {mx - ca * L * 0.15f,
                      my - sa * L * 0.15f,
                      mx + ca * L * 0.15f - sa * L * 0.18f * open,
                      my + sa * L * 0.15f + ca * L * 0.18f * open,
                      mx + ca * L * 0.15f + sa * L * 0.04f,
                      my + sa * L * 0.15f - ca * L * 0.04f};
        poly_mix(t, 3, 10, 6, 8, (int)(250 * vis));
        for (int k = 0; k < 5; k++) {
            float f = k / 4.0f, tx = lerpf(t[0], t[2], f), ty = lerpf(t[1], t[3], f);
            line_mix(tx, ty, tx - sa * L * 0.04f, ty + ca * L * 0.04f, 230, 230, 240, (int)(220 * vis));
        }
        float lx = sx + ca * L * 0.55f + sa * L * 0.45f, ly = sy + sa * L * 0.55f - ca * L * 0.45f,
              px0 = sx + sa * L * 0.3f, py0 = sy - ca * L * 0.3f;
        line_add(px0, py0, lx, ly, 80, 90, 70);
        float lp = 0.75f + 0.25f * sinf(s_time * 3 + e->ph);
        glow(lx, ly, 16 * z * lp, 157, 255, 106, 0.45f);
        glow(lx, ly, 4 * z, 230, 255, 220, 0.9f);
        float ex = sx + ca * L * 0.2f + sa * L * 0.15f, ey = sy + sa * L * 0.2f - ca * L * 0.15f;
        disc_mix(ex, ey, 1.2f * z, 200, 210, 200, vis);
        if (eye > 0.5f) {
            glow(ex, ey, 5 * z, 255, 80, 60, 0.6f * eye);
        }
    }
    if (stun > 0) {
        glow(sx, sy, L * 0.3f, 255, 255, 255, 0.25f * stun);
    }
    // (a hunter that the creature can now eat shows the light of its trait)
    if (B.len > hunter_len(e) * 1.15f) {
        gene_dot(sx, sy, S->trait, 1.4f * z, 1);
    }
}
static void draw_life(void)
{
    // the far ones first: the hunters and the big jellies behind the small prey
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NLIFE; i++) {
            life_t *e = &s_life[i];
            if (!e->on) {
                continue;
            }
            bool big = is_hunter(e->sp) || e->sp == SP_BIGJELLY;
            if (big != (pass == 0)) {
                continue;
            }
            draw_life_one(e);
        }
    }
}
