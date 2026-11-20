// CUBBY: the small creatures of the island (included by app_cubby.c; round 3: small creatures that move). Only for the look (no part of the play, not sent to a friend board):
// - CRABS on the beach: they walk to the side, stop, walk again; near the robot they run away fast.
// - BUTTERFLIES over the meadows and the orchard: a loop round a flower bed; near the robot they go up and away.
// - FISH: from time to time a fish jumps out of the sea near the robot (an arc and two splashes).
// Each one is drawn only when it is near the eye (LIFE_D).

#define LIFE_D 20.0f
#define NCRAB  7
#define NFLY   9
#define NFISH  2
typedef struct {
    float x, z, yaw;  // the place, the facing (a crab walks to its side)
    float v;          // the speed to the side (+ right, - left)
    float t;          // the time to the next change
    float leg;        // the phase of the legs
} crab_t;
static crab_t s_crab[NCRAB];
static const float CRAB_AT[NCRAB][2] = {{14, 5}, {25, 4}, {30, 9}, {39, 5}, {47, 8}, {53, 6}, {9, 10}};
typedef struct {
    float cx, cy, cz;  // the middle of its loop
    float ph, sp;      // the phase and the speed of the loop
    float r;           // the size of the loop
    float up;          // the flight up and away from the robot (0..1)
    v3 p;              // the place now
    float yaw;
    int col;
} fly_t;
static fly_t s_fly[NFLY];
static const float FLY_AT[NFLY][3] = {{20, 3.2f, 17}, {28, 3.0f, 20}, {44, 3.2f, 15}, {14, 3.4f, 37}, {18, 3.1f, 34},
                                      {33, 3.4f, 31}, {50, 3.0f, 32}, {29, 7.2f, 42}, {52, 7.0f, 51}};
typedef struct {
    bool on;
    v3 a, b;  // out of the sea, back into the sea
    float t, dur;
} fish_t;
static fish_t s_fish[NFISH];
static float s_fish_next;

static void life_reset(void)
{
    for (int k = 0; k < NCRAB; k++) {
        s_crab[k] = (crab_t){CRAB_AT[k][0] + 0.5f, CRAB_AT[k][1] + 0.5f, k * 1.3f, 0, 0.5f + k * 0.37f, 0};
    }
    for (int k = 0; k < NFLY; k++) {
        s_fly[k] = (fly_t){FLY_AT[k][0],
                           FLY_AT[k][1],
                           FLY_AT[k][2],
                           k * 2.1f,
                           0.8f + 0.15f * (k % 3),
                           1.2f + 0.3f * (k % 4),
                           0,
                           V3(FLY_AT[k][0], FLY_AT[k][1], FLY_AT[k][2]),
                           0,
                           k % 3};
    }
    memset(s_fish, 0, sizeof s_fish);
    s_fish_next = 3;
}
// a crab stays on the sand (a cell with the top 0 that is not the sea)
static bool crab_ok(float x, float z)
{
    int i = ifloor(x), j = ifloor(z);
    return cell_in(i, j) && !cell_water(i, j) && s_H[i][j] == 0 && !s_nstk[i][j];
}
static void life_step(float dt)
{
    for (int k = 0; k < NCRAB; k++) {
        crab_t *c = &s_crab[k];
        float dx = c->x - P.p.x, dz = c->z - P.p.z, d2 = dx * dx + dz * dz;
        if (d2 > 30 * 30) {
            continue;  // (far: no work)
        }
        c->t -= dt;
        v3 side = V3(fcos(c->yaw), 0, -fsin(c->yaw));  // (the right of the crab)
        if (d2 < 2.6f * 2.6f && absf(P.p.y) < 1.5f) {
            // the robot is near: run away to the side that goes from it
            float s = side.x * dx + side.z * dz;
            c->v = s >= 0 ? 3.2f : -3.2f;
            c->t = 0.6f;
        } else if (c->t <= 0) {
            float r = hash2(c->x * 3.1f + s_t, c->z * 1.7f);
            c->v = r < 0.35f ? 0 : (r < 0.68f ? 0.9f : -0.9f);
            c->t = 0.7f + 1.6f * hash2(c->z + s_t, k * 1.3f);
            if (c->v == 0 && r < 0.12f) {
                c->yaw += (r - 0.06f) * 12;  // (a turn)
            }
        }
        float nx = c->x + side.x * c->v * dt, nz = c->z + side.z * c->v * dt;
        if (crab_ok(nx, nz)) {
            c->x = nx;
            c->z = nz;
        } else {
            c->v = -c->v;
        }
        c->leg += absf(c->v) * dt * 14;
    }
    for (int k = 0; k < NFLY; k++) {
        fly_t *f = &s_fly[k];
        f->ph += dt * f->sp;
        float dx = f->p.x - P.p.x, dz = f->p.z - P.p.z;
        bool near = dx * dx + dz * dz < 2.2f * 2.2f && absf(f->p.y - P.p.y - 1) < 2;
        f->up = approach(f->up, near ? 1.0f : 0.0f, dt * (near ? 1.5f : 0.3f));
        v3 p = V3(f->cx + fcos(f->ph) * f->r + fsin(f->ph * 2.3f) * 0.3f,
                  f->cy + fsin(f->ph * 3.1f) * 0.25f + f->up * 2.5f, f->cz + fsin(f->ph * 1.3f) * f->r * 0.8f);
        float vx = p.x - f->p.x, vz = p.z - f->p.z;
        if (vx * vx + vz * vz > 1e-6f) {
            f->yaw = fatan2(vx, vz);
        }
        f->p = p;
    }
    // the fish: one jump each few seconds in the sea near the robot (a cell of water, the sea in front of the camera)
    s_fish_next -= dt;
    if (s_fish_next <= 0) {
        s_fish_next = 2.5f + 4.0f * hash2(s_t, 7.7f);
        for (int k = 0; k < NFISH; k++) {
            if (s_fish[k].on) {
                continue;
            }
            float a = s_cam_yaw + (hash2(s_t, 1.1f) - 0.5f) * 1.6f, r = 6 + 8 * hash2(s_t, 2.2f);
            float x = P.p.x + fsin(a) * r, z = P.p.z + fcos(a) * r;
            float h = hash2(s_t, 3.3f) * TAU_F, len = 1.6f + 1.2f * hash2(s_t, 4.4f);
            float x2 = x + fsin(h) * len, z2 = z + fcos(h) * len;
            if (cell_water(ifloor(x), ifloor(z)) && cell_water(ifloor(x2), ifloor(z2)) &&
                cell_in(ifloor(x), ifloor(z))) {
                s_fish[k] = (fish_t){true, V3(x, SEA_Y, z), V3(x2, SEA_Y, z2), 0, 0.55f + 0.2f * len / 2.8f};
                for (int n = 0; n < 6; n++) {
                    part_add(V3(x, SEA_Y + 0.05f, z), V3(frs() * 0.8f, 1.5f + frand() * 1.5f, frs() * 0.8f), 0.5f,
                             0.05f, PT_SPLASH, 0xE8FFFF);
                }
            }
            break;
        }
    }
    for (int k = 0; k < NFISH; k++) {
        fish_t *q = &s_fish[k];
        if (!q->on) {
            continue;
        }
        q->t += dt;
        if (q->t >= q->dur) {
            q->on = false;
            for (int n = 0; n < 7; n++) {
                part_add(V3(q->b.x, SEA_Y + 0.05f, q->b.z), V3(frs() * 0.9f, 1.4f + frand() * 1.6f, frs() * 0.9f), 0.5f,
                         0.05f, PT_SPLASH, 0xE8FFFF);
            }
            float dx = q->b.x - P.p.x, dz = q->b.z - P.p.z;
            fx(FX_SPLASH, (int)clampf(40 - fsqrt(dx * dx + dz * dz) * 2, 0, 30), 1.8f);
        }
    }
}
// the creatures into the list of the frame (near the eye only)
static void life_draw(void)
{
    xf_t x;
    for (int k = 0; k < NCRAB; k++) {
        const crab_t *c = &s_crab[k];
        v3 at = V3(c->x, 0.0f, c->z);
        if (xf_pt(&C.w2c, at).z > LIFE_D) {
            continue;
        }
        float bob = absf(fsin(c->leg)) * 0.025f;
        xf_make(&x, V3(at.x, bob, at.z), c->yaw, 0, fsin(c->leg) * 0.12f, V3(1.6f, 1.6f, 1.6f));
        dl_model(&s_mod[M_CRAB], &x, 1, 0);
        dl_shadow(V3(at.x, 0.015f, at.z), 0.34f, 0.28f, c->yaw, 0.3f);
    }
    for (int k = 0; k < NFLY; k++) {
        const fly_t *f = &s_fly[k];
        if (xf_pt(&C.w2c, f->p).z > LIFE_D) {
            continue;
        }
        float flap = fsin(s_rt * 22 + k) * 1.0f;
        for (int s = 0; s < 2; s++) {
            xf_make(&x, f->p, f->yaw, 0, s ? flap : -flap, V3(s ? -1.5f : 1.5f, 1.5f, 1.5f));
            dl_model(&s_mod[M_WING_A + f->col], &x, 1, 0);
        }
    }
    for (int k = 0; k < NFISH; k++) {
        const fish_t *q = &s_fish[k];
        if (!q->on) {
            continue;
        }
        float u = q->t / q->dur;
        v3 c = V3(q->a.x + (q->b.x - q->a.x) * u, SEA_Y + 4 * 0.9f * u * (1 - u), q->a.z + (q->b.z - q->a.z) * u);
        if (xf_pt(&C.w2c, c).z > LIFE_D) {
            continue;
        }
        float yaw = fatan2(q->b.x - q->a.x, q->b.z - q->a.z);
        xf_make(&x, c, yaw, (u - 0.5f) * 2.2f, 0, V3(1.3f, 1.3f, 1.3f));
        dl_model(&s_mod[M_FISH], &x, 1, 0);
    }
}
