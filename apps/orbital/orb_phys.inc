// ORBITAL: the physics (included by app_orbital.c). One planet with real gravity, a leapfrog integrator (kick, drift,
// kick: symplectic), the air (drag and heat), the tether (a rope: a spring that only pulls, with a winch), the hull of
// the wreck (no tug goes through it), and the prediction of the orbits with the closest approach.
// The positions are doubles (the ESP32-S3 has no double FPU, but only the drift adds to them: a float of 700 km keeps
// only 4 cm, and the rope needs better). Never compare two doubles (the ABI has no ==, != or <= of doubles).

#define R_PL    600.0f   // the radius of the planet (km)
#define MU_PL   3531.6f  // GM (km^3/s^2): a circular orbit at 100 km takes 1958 s at 2246 m/s
#define ATM_TOP 70.0f    // the top of the air (km)
#define ATM_H   5.6f     // the scale height of the air (km)
#ifndef RHO0
#define RHO0                                                                                                           \
    0.04f   // the density of the upper air extended to the ground (kg/m^3; a thin air: from an
            // apoapsis of 300 km a pass at 50 km takes 16 m/s from the tug, at 45 km 42 m/s)
#endif
#define DRAG_TOP 70.0f  // above this height there is no air (the density goes to 0 from 58 km to 70 km)
#define GONE_ALT 18.0f  // a body below this height is gone (it burns, or a capsule lands)
#define HEAT_TAU 3.0f   // s: the heat follows the heat rate in this time
#define HEAT_SCALE                                                                                                     \
    1.0f    // the heat rate: heat_k sqrt(rho / RHO0) v^3 (v in km/s); the tug has heat_k 3.3: a pass
            // at 2.4 km/s burns it at a periapsis of about 42 km

#define TUG_DRY    1000.0f  // kg
#define TUG_TANK   1000.0f  // kg of fuel in a full tank
#define TUG_THRUST 4000.0f  // N
#define TUG_VE                                                                                                         \
    1200.0f                    // the exhaust speed (m/s): a full tank gives 832 m/s to the tug alone, 345 m/s with a
                               // wreck of 2000 kg on the rope
#define TUG_RAD      9.0f      // m: the hull of the tug (a circle)
#define TETHER_RANGE 260.0f    // m: the harpoon flies this far
#define TETHER_SPEED 70.0f     // m/s: the harpoon, relative to the tug
#define ROPE_K       7000.0f   // N/m: the rope
#define ROPE_C       2600.0f   // N s/m: the damping of the rope (about 0.6 of the critical damping)
#define ROPE_MAX     32000.0f  // N: a stronger pull snaps the rope
#define ROPE_SHORT   55.0f     // m: the winch reels the rope to this length
#define ROPE_MIN     14.0f     // m: the winch takes in the slack down to this length
#define WINCH_SPEED  7.0f      // m/s

enum {
    K_TUG,
    K_WRECK,
    K_CAPSULE,
    K_SAT,
    K_STATION,
    K_DEBRIS
};
#define NB 4  // the tug and up to 3 other bodies
typedef struct {
    double x, y;      // the position (km; x to the right, y down: the angle grows in the way of a prograde orbit)
    float vx, vy;     // km/s
    float ax, ay;     // the acceleration of the last step (km/s^2): the first kick of the next step
    float mass;       // kg
    float cda;        // the drag area Cd * A (m^2)
    float ang, w;     // the attitude (rad, world) and the spin (rad/s); the tug: see s_nose
    float inertia;    // kg m^2
    float heat;       // 0 .. 1 (1 = it burns up)
    float heat_k;     // how fast it heats (a heat shield: small)
    float len, rad;   // the hull: a capsule of half length len and radius rad (m)
    float hx, hy;     // the hook in the frame of the body (m)
    float hnx, hny;   // the outward side of the hook (a unit vector in the frame of the body); 0, 0 = any side
    float peak_heat;  // the highest heat of the mission
    uint8_t kind, on, gone, in_air;
} body_t;
static body_t B[NB];
static int s_nb;             // the bodies in use (B[0] = the tug)
static double s_clock;       // the mission clock (s)
static float s_fuel;         // kg of fuel in the tug
static float s_throttle;     // 0..1 now (the main engine)
static float s_nose;         // the nose of the tug: the angle from the local horizontal (rad; 0 = the way of a
                             // prograde orbit, +pi/2 = down to the planet); the camera shows this angle on the screen
static float s_sun = -2.6f;  // the world angle of the direction to the sun

enum {
    TH_NONE,
    TH_FLY,
    TH_ON,
    TH_BACK
};
typedef struct {
    uint8_t state, to;        // TH_*, the body
    float L, L_goal;          // the length of the rope now and the goal of the winch (m)
    float px, py, pvx, pvy;   // the harpoon: its place and speed relative to the tug (m, m/s)
    float t, tension, flown;  // the time in this state, the pull now (N), the way of the harpoon (m)
    float jerk;               // the highest pull since the lock (N): a loud creak
    uint8_t snapped, missed;  // events for the game (it clears them)
} tether_t;
static tether_t T;

// ---- the events of a step (the game reads and clears them: sounds, shakes, sparks)
static uint8_t ev_bump, ev_hit, ev_snap, ev_lock, ev_miss;
static float ev_bump_v;  // the speed of the last bump (m/s)

static inline float body_alt(const body_t *b)
{
    float x = (float)b->x, y = (float)b->y;
    return sqrtf(x * x + y * y) - R_PL;
}
static inline float air_rho(float alt)
{
    if (alt > DRAG_TOP) {
        return 0;
    }
    // (below 30 km the air gets dense fast: a capsule slows down there; a steep entry gives a high g-load)
    if (alt < 30) {
        return RHO0 * expf(-30 / ATM_H) * expf((30 - alt) / 2.8f);
    }
    float r = RHO0 * expf(-alt / ATM_H);
    return alt > 58 ? r * (1 - smooth01((alt - 58) / 12)) : r;
}
static float tug_mass(void)
{
    return TUG_DRY + s_fuel;
}
// the place of the hook of body i in the world (km), from its frame
static void hook_world(const body_t *b, double *x, double *y)
{
    float c = cosf(b->ang), s = sinf(b->ang);
    *x = b->x + (double)((b->hx * c - b->hy * s) * 0.001f);
    *y = b->y + (double)((b->hx * s + b->hy * c) * 0.001f);
}
// the nose of the tug as a world angle: the local horizontal at the tug turns with its place on the orbit
static float nose_world(void)
{
    float th = atan2f((float)B[0].y, (float)B[0].x);
    return th + PI_F / 2 + s_nose;
}

// ---- the forces (gravity, air, engine, rope, hull) at the places now; the accelerations go to B[i].ax/ay
static float s_air_heat;  // the heat rate of the tug now (the plasma of the drawing)
static float s_engx,
        s_engy;  // the speed (m/s) that the engine gave to the tug and its tow since the start: the size of
                 // a burn (the speed itself also changes with the gravity)
static void forces(float h)
{
    for (int i = 0; i < s_nb; i++) {
        body_t *b = &B[i];
        if (!b->on) {
            continue;
        }
        float x = (float)b->x, y = (float)b->y, r2 = x * x + y * y, r = sqrtf(r2);
        float g = -MU_PL / (r2 * r);
        b->ax = g * x;
        b->ay = g * y;
        float alt = r - R_PL, rho = air_rho(alt);
        b->in_air = alt < ATM_TOP;
        if (rho > 0) {
            float v2 = b->vx * b->vx + b->vy * b->vy, v = sqrtf(v2);
            // (a [km/s^2] = 0.5 rho (1000 v)^2 CdA / m / 1000)
            float k = 500.0f * rho * v * b->cda / (i == 0 ? tug_mass() : b->mass);
            b->ax -= k * b->vx;
            b->ay -= k * b->vy;
            // the heat: the heat rate sqrt(rho) v^3 (the shield or the hull fails at a peak rate: a dive that is too
            // deep burns); the heat follows the rate in about HEAT_TAU seconds
            float q = b->heat_k * sqrtf(rho / RHO0) * v2 * v * HEAT_SCALE;
            b->heat += (q - b->heat) * (h / (HEAT_TAU + h));
            if (i == 0) {
                s_air_heat = q;
            }
        } else {
            b->heat -= b->heat * (h / (HEAT_TAU + h));
            if (i == 0) {
                s_air_heat = 0;
            }
        }
        if (b->heat < 0) {
            b->heat = 0;
        }
        if (b->heat > b->peak_heat) {
            b->peak_heat = b->heat;
        }
    }
    // the main engine of the tug
    if (s_throttle > 0 && s_fuel > 0 && B[0].on) {
        float m = tug_mass(), f = TUG_THRUST * s_throttle, a = f / m * 0.001f, nw = nose_world();
        B[0].ax += cosf(nw) * a;
        B[0].ay += sinf(nw) * a;
        float ac = f / (m + (T.state == TH_ON ? B[T.to].mass : 0)) * h;
        s_engx += cosf(nw) * ac;
        s_engy += sinf(nw) * ac;
        s_fuel -= f / TUG_VE * h;
        if (s_fuel < 0) {
            s_fuel = 0;
        }
    }
    // the rope (only a pull): the tug and the hook of the body; the pull at the hook also turns the body
    T.tension = 0;
    if (T.state == TH_ON && B[T.to].on) {
        body_t *w = &B[T.to];
        double hxw, hyw;
        hook_world(w, &hxw, &hyw);
        float dx = (float)((hxw - B[0].x) * 1000.0), dy = (float)((hyw - B[0].y) * 1000.0),
              d = sqrtf(dx * dx + dy * dy);
        // (the rope only pulls; but reeled in fully to its short length it is a tow bar: it also pushes a little, with
        // damping, so a burn against the side of the tow swings the tow round and does not jerk the rope)
        bool bar = T.L <= T.L_goal + 1.0f;
        if ((d > T.L || bar) && d > 0.01f) {
            float ux = dx / d, uy = dy / d;
            // (the speed of the hook: the body plus its spin)
            float c = cosf(w->ang), s = sinf(w->ang), rx = w->hx * c - w->hy * s, ry = w->hx * s + w->hy * c;
            float hvx = w->vx * 1000 - w->w * ry, hvy = w->vy * 1000 + w->w * rx;
            float vd = (hvx - B[0].vx * 1000) * ux + (hvy - B[0].vy * 1000) * uy;
            float f = ROPE_K * (d - T.L) + ROPE_C * vd;
            if (f < (bar ? -ROPE_MAX * 0.5f : 0)) {
                f = bar ? -ROPE_MAX * 0.5f : 0;
            }
            T.tension = f > 0 ? f : 0;
            float mt = tug_mass();
            B[0].ax += ux * f / mt * 0.001f;
            B[0].ay += uy * f / mt * 0.001f;
            w->ax -= ux * f / w->mass * 0.001f;
            w->ay -= uy * f / w->mass * 0.001f;
            // (the turn: r x F, with F = -f u at the hook)
            float tq = rx * (-uy * f) - ry * (-ux * f);
            w->w += tq / w->inertia * h;
            if (bar) {
                // the tow bar also stops the swing of the tow round the tug (the side jets of the tug hold it): a damping
                // of the side speed of the tow against the tug
                float svx = (w->vx - B[0].vx) * 1000, svy = (w->vy - B[0].vy) * 1000, sa = svx * ux + svy * uy;
                float px = svx - sa * ux, py = svy - sa * uy, cl = 1200;
                B[0].ax += px * cl / mt * 0.001f;
                B[0].ay += py * cl / mt * 0.001f;
                w->ax -= px * cl / w->mass * 0.001f;
                w->ay -= py * cl / w->mass * 0.001f;
            }
        }
    }
    // the hull: the tug may not go into another body (a stiff push with damping)
    for (int i = 1; i < s_nb; i++) {
        body_t *w = &B[i];
        if (!w->on) {
            continue;
        }
        float dx = (float)((B[0].x - w->x) * 1000.0), dy = (float)((B[0].y - w->y) * 1000.0);
        if (absf(dx) > w->len + w->rad + 40 || absf(dy) > w->len + w->rad + 40) {
            continue;
        }
        float c = cosf(w->ang), s = sinf(w->ang);
        float lx = dx * c + dy * s, ly = -dx * s + dy * c;  // (the tug in the frame of the body)
        float qx = clampf(lx, -w->len, w->len), ex = lx - qx, ey = ly, d = sqrtf(ex * ex + ey * ey);
        float over = w->rad + TUG_RAD - d;
        if (over <= 0 || d < 0.001f) {
            continue;
        }
        float nx = (ex * c - ey * s) / d, ny = (ex * s + ey * c) / d;  // (from the body to the tug, world)
        float vn = (B[0].vx - w->vx) * 1000 * nx + (B[0].vy - w->vy) * 1000 * ny;
        float mt = tug_mass(), mr = mt * w->mass / (mt + w->mass);
        float f = mr * (40.0f * over - (vn < 0 ? 6.0f * vn : 2.0f * vn));
        if (f < 0) {
            f = 0;
        }
        B[0].ax += nx * f / mt * 0.001f;
        B[0].ay += ny * f / mt * 0.001f;
        w->ax -= nx * f / w->mass * 0.001f;
        w->ay -= ny * f / w->mass * 0.001f;
        if (vn < -0.4f && -vn > ev_bump_v) {
            ev_bump = 1;
            ev_bump_v = -vn;
        }
    }
}

// the harpoon and the winch (the time h of the step)
static bool hook_faces(const body_t *w, float tx, float ty)
{
    // tx, ty: the tug relative to the hook (m, world). A hook with no side (hnx = hny = 0) takes the harpoon from all sides.
    if (w->hnx == 0 && w->hny == 0) {
        return true;
    }
    float c = cosf(w->ang), s = sinf(w->ang), nx = w->hnx * c - w->hny * s, ny = w->hnx * s + w->hny * c;
    float d = sqrtf(tx * tx + ty * ty);
    return d < 1 || (nx * tx + ny * ty) / d > 0.25f;
}
static void tether_step(float h)
{
    T.t += h;
    if (T.state == TH_FLY) {
        body_t *w = &B[T.to];
        double hxw, hyw;
        hook_world(w, &hxw, &hyw);
        // (the hook relative to the tug, and its speed: the harpoon homes on it)
        float gx = (float)((hxw - B[0].x) * 1000.0), gy = (float)((hyw - B[0].y) * 1000.0);
        float rvx = (w->vx - B[0].vx) * 1000, rvy = (w->vy - B[0].vy) * 1000;
        float dx = gx - T.px, dy = gy - T.py, d = sqrtf(dx * dx + dy * dy);
        if (d < 3.5f) {
            if (hook_faces(w, -gx, -gy)) {
                T.state = TH_ON;
                T.t = 0;
                T.L = sqrtf(gx * gx + gy * gy) + 2;
                T.L_goal = ROPE_SHORT;
                T.jerk = 0;
                ev_lock = 1;
            } else {
                T.state = TH_BACK;
                T.t = 0;
                T.missed = 1;
                ev_miss = 1;
            }
            ev_hit = 1;
            return;
        }
        // (the hull in the way when the hook looks away: the harpoon hits the hull)
        if (!hook_faces(w, -gx, -gy)) {
            float bx = (float)((w->x - B[0].x) * 1000.0) - T.px, by = (float)((w->y - B[0].y) * 1000.0) - T.py;
            if (sqrtf(bx * bx + by * by) < w->len * 0.8f + w->rad) {
                T.state = TH_BACK;
                T.t = 0;
                T.missed = 1;
                ev_miss = 1;
                ev_hit = 1;
                return;
            }
        }
        float sx = rvx + dx / d * TETHER_SPEED, sy = rvy + dy / d * TETHER_SPEED;
        T.pvx = sx;
        T.pvy = sy;
        T.px += sx * h;
        T.py += sy * h;
        T.flown += TETHER_SPEED * h;
        if (T.flown > TETHER_RANGE) {
            T.state = TH_BACK;
            T.t = 0;
            T.missed = 1;
            ev_miss = 1;
        }
    } else if (T.state == TH_BACK) {
        float d = sqrtf(T.px * T.px + T.py * T.py);
        if (d < 4) {
            T.state = TH_NONE;
            return;
        }
        float k = fminf(1, 90 * h / d);
        T.px -= T.px * k;
        T.py -= T.py * k;
    } else if (T.state == TH_ON) {
        // the winch: it reels in to the short length and takes in the slack (so the rope does not jerk after a slack
        // time); a hard pull makes its brake let out some rope
        body_t *w = &B[T.to];
        double hxw, hyw;
        hook_world(w, &hxw, &hyw);
        float dx = (float)((hxw - B[0].x) * 1000.0), dy = (float)((hyw - B[0].y) * 1000.0),
              d = sqrtf(dx * dx + dy * dy);
        if (T.L > T.L_goal) {
            T.L = fmaxf(T.L_goal, T.L - WINCH_SPEED * h);
        }
        if (d < T.L - 0.5f) {
            T.L = fmaxf(ROPE_MIN, fmaxf(d + 0.5f, T.L - 2 * WINCH_SPEED * h));
        } else if (T.L < T.L_goal && T.tension < 8000) {
            T.L = fminf(T.L_goal, T.L + 1.5f * h);
        }
        if (T.tension > ROPE_MAX * 0.55f && T.L < 140) {
            T.L += 4.0f * h;
        }
        if (T.tension > T.jerk) {
            T.jerk = T.tension;
        }
        if (T.tension > ROPE_MAX) {
            T.state = TH_BACK;
            T.t = 0;
            T.snapped = 1;
            ev_snap = 1;
            T.px = dx;
            T.py = dy;
        }
    }
}

// one step of h seconds: kick (h/2), drift, the new forces, kick (h/2)
static void phys_step(float h)
{
    for (int i = 0; i < s_nb; i++) {
        body_t *b = &B[i];
        if (!b->on) {
            continue;
        }
        b->vx += b->ax * h * 0.5f;
        b->vy += b->ay * h * 0.5f;
        b->x += (double)(b->vx * h);
        b->y += (double)(b->vy * h);
        b->ang += b->w * h;
        if (b->ang > PI_F) {
            b->ang -= TAU_F;
        }
        if (b->ang < -PI_F) {
            b->ang += TAU_F;
        }
    }
    forces(h);
    for (int i = 0; i < s_nb; i++) {
        body_t *b = &B[i];
        if (!b->on) {
            continue;
        }
        b->vx += b->ax * h * 0.5f;
        b->vy += b->ay * h * 0.5f;
    }
    tether_step(h);
    s_clock += (double)h;
}
// the largest safe step now: small near another body, with the rope, in the air; long in free flight
static float step_max(void)
{
    float hmax = 1.0f;
    if (T.state != TH_NONE) {
        hmax = 0.05f;
    }
    for (int i = 0; i < s_nb; i++) {
        if (!B[i].on) {
            continue;
        }
        if (body_alt(&B[i]) < DRAG_TOP) {
            hmax = fminf(hmax, 0.25f);
        }
        if (i > 0) {
            float dx = (float)((B[i].x - B[0].x) * 1000.0), dy = (float)((B[i].y - B[0].y) * 1000.0);
            float d = sqrtf(dx * dx + dy * dy);
            if (d < 400) {
                hmax = fminf(hmax, 0.05f);
            } else if (d < 3000) {
                hmax = fminf(hmax, 0.2f);
            }
        }
    }
    if (s_throttle > 0) {
        hmax = fminf(hmax, 0.1f);
    }
    return hmax;
}
// the mission time dt: the steps that it needs (at most 600: a slow frame loses time, not accuracy)
static int s_steps_last;
static void phys_advance(float dt)
{
    float hm = step_max();
    int n = (int)ceilf(dt / hm);
    if (n < 1) {
        n = 1;
    }
    if (n > 600) {
        n = 600;
        dt = hm * 600;
    }
    float h = dt / n;
    for (int k = 0; k < n; k++) {
        phys_step(h);
    }
    s_steps_last = n;
}

// ================================================================ the orbit elements
typedef struct {
    float a, e;    // the half long axis (km; < 0: escape) and the eccentricity
    float rp, ra;  // the radius of the periapsis and the apoapsis (km; ra < 0: escape)
    float wp;      // the world angle of the periapsis
    float nu;      // the true anomaly now (rad)
    float hz;      // the angular momentum (> 0: prograde)
    float period;  // s (0: escape)
} elem_t;
static elem_t elements(float x, float y, float vx, float vy)
{
    elem_t E;
    float r = sqrtf(x * x + y * y), v2 = vx * vx + vy * vy, rv = x * vx + y * vy;
    float en = v2 / 2 - MU_PL / r;
    E.hz = x * vy - y * vx;
    float ex = ((v2 - MU_PL / r) * x - rv * vx) / MU_PL, ey = ((v2 - MU_PL / r) * y - rv * vy) / MU_PL;
    E.e = sqrtf(ex * ex + ey * ey);
    E.a = absf(en) < 1e-9f ? 1e9f : -MU_PL / (2 * en);
    E.rp = E.a * (1 - E.e);
    if (E.e < 1 && E.a > 0) {
        E.ra = E.a * (1 + E.e);
        E.period = TAU_F * sqrtf(E.a * E.a * E.a / MU_PL);
    } else {
        E.ra = -1;
        E.period = 0;
        E.rp = E.hz * E.hz / MU_PL / (1 + E.e);
    }
    E.wp = E.e > 1e-6f ? atan2f(ey, ex) : atan2f(y, x);
    E.nu = wrap_pi(atan2f(y, x) - E.wp);
    if (E.hz < 0) {
        E.nu = -E.nu;
    }
    return E;
}
// the elements of a body, and of the tug with its tow (the centre of mass of the two)
static void com_state(int i, float *x, float *y, float *vx, float *vy, float *m)
{
    body_t *b = &B[i];
    *x = (float)b->x;
    *y = (float)b->y;
    *vx = b->vx;
    *vy = b->vy;
    *m = i == 0 ? tug_mass() : b->mass;
    if (i == 0 && T.state == TH_ON) {
        body_t *w = &B[T.to];
        float mt = *m, mw = w->mass, k = mw / (mt + mw);
        *x += (float)((w->x - b->x)) * k;
        *y += (float)((w->y - b->y)) * k;
        *vx += (w->vx - b->vx) * k;
        *vy += (w->vy - b->vy) * k;
        *m = mt + mw;
    }
}
static elem_t body_elem(int i)
{
    float x, y, vx, vy, m;
    com_state(i, &x, &y, &vx, &vy, &m);
    return elements(x, y, vx, vy);
}

// ================================================================ the prediction (the map: the coming orbits)
// The tug (with its tow: one body at the centre of mass) and each other body, the same steps of time; the air is in
// it (an orbit in the air decays). It stops where a body goes below GONE_ALT.
#define NP 200
typedef struct {
    float x[NP], y[NP];  // the places (km)
    int n;               // the samples (a body that goes down has fewer)
    int air_at;          // the first sample in the air (-1: none)
    float peak;          // the highest heat on the way (1 or more: it burns up there)
    int burn_at;         // the sample where it burns (-1: none)
} path_t;
static path_t s_path[NB];
static float s_pdt;           // the time between two samples (s)
static int s_ca_k;            // the closest approach: the sample (-1: none)
static float s_ca_d, s_ca_t;  // its distance (km) and its time from now (s)
static int s_ca_to;           // the body
typedef struct {
    double x, y;
    float vx, vy, m, cda, heat, heat_k, peak;
} pstate_t;
static void pstep(pstate_t *p, float h)
{
    // (one leapfrog step with the air: drift-kick-drift; the air with the speed of the middle)
    p->x += (double)(p->vx * h * 0.5f);
    p->y += (double)(p->vy * h * 0.5f);
    float x = (float)p->x, y = (float)p->y, r2 = x * x + y * y, r = sqrtf(r2), g = -MU_PL / (r2 * r);
    float ax = g * x, ay = g * y, rho = air_rho(r - R_PL);
    if (rho > 0) {
        float v2 = p->vx * p->vx + p->vy * p->vy, v = sqrtf(v2), k = 500.0f * rho * v * p->cda / p->m;
        // (an exact decay for the drag part: a big step in thick air must not turn the speed round)
        float f = expf(-k * h);
        p->vx *= f;
        p->vy *= f;
        float q = p->heat_k * sqrtf(rho / RHO0) * v2 * v * HEAT_SCALE;
        p->heat += (q - p->heat) * (h / (HEAT_TAU + h));
        if (p->heat > p->peak) {
            p->peak = p->heat;
        }
    } else {
        p->heat -= p->heat * (h / (HEAT_TAU + h));
    }
    p->vx += ax * h;
    p->vy += ay * h;
    p->x += (double)(p->vx * h * 0.5f);
    p->y += (double)(p->vy * h * 0.5f);
}
static void predict(float horizon)
{
    pstate_t P[NB];
    int on[NB] = {0};
    for (int i = 0; i < s_nb; i++) {
        if (!B[i].on || (T.state == TH_ON && i == T.to)) {
            s_path[i].n = 0;
            continue;
        }
        float x, y, vx, vy, m;
        com_state(i, &x, &y, &vx, &vy, &m);
        P[i].x = B[i].x;
        P[i].y = B[i].y;
        if (i == 0 && T.state == TH_ON) {
            P[i].x = (double)x;
            P[i].y = (double)y;
        }
        P[i].vx = vx;
        P[i].vy = vy;
        P[i].m = m;
        P[i].cda = B[i].cda + (i == 0 && T.state == TH_ON ? B[T.to].cda : 0);
        P[i].heat = B[i].heat;
        P[i].heat_k = B[i].heat_k;
        if (i == 0 && T.state == TH_ON) {
            P[i].heat = B[T.to].heat;
            P[i].heat_k = B[T.to].heat_k;
        }
        P[i].peak = P[i].heat;
        s_path[i].burn_at = -1;
        s_path[i].x[0] = x;
        s_path[i].y[0] = y;
        s_path[i].n = 1;
        s_path[i].air_at = -1;
        on[i] = 1;
    }
    s_pdt = horizon / (NP - 1);
    for (int k = 1; k < NP; k++) {
        for (int i = 0; i < s_nb; i++) {
            if (!on[i]) {
                continue;
            }
            pstate_t *p = &P[i];
            // (in the air: smaller steps)
            float alt = sqrtf((float)p->x * (float)p->x + (float)p->y * (float)p->y) - R_PL;
            int sub = alt < DRAG_TOP ? (int)ceilf(s_pdt / 4.0f) : (int)ceilf(s_pdt / 40.0f);
            if (sub < 1) {
                sub = 1;
            }
            if (sub > 24) {
                sub = 24;
            }
            for (int q = 0; q < sub; q++) {
                pstep(p, s_pdt / sub);
            }
            float fx = (float)p->x, fy = (float)p->y, a2 = sqrtf(fx * fx + fy * fy) - R_PL;
            s_path[i].x[k] = fx;
            s_path[i].y[k] = fy;
            s_path[i].n = k + 1;
            if (a2 < ATM_TOP && s_path[i].air_at < 0) {
                s_path[i].air_at = k;
            }
            if (P[i].peak >= 1 && s_path[i].burn_at < 0) {
                s_path[i].burn_at = k;
            }
            if (a2 < GONE_ALT) {
                on[i] = 0;
            }
        }
    }
    for (int i = 0; i < s_nb; i++) {
        s_path[i].peak = s_path[i].n > 0 ? P[i].peak : 0;
    }
}
// the closest approach of the tug and body j in the prediction (from the sample k0 on): a straight line between two
// samples
static void closest(int j, int k0)
{
    s_ca_k = -1;
    s_ca_d = 1e9f;
    s_ca_t = 0;
    s_ca_to = j;
    int n = s_path[0].n < s_path[j].n ? s_path[0].n : s_path[j].n;
    for (int k = k0; k + 1 < n; k++) {
        float ax = s_path[j].x[k] - s_path[0].x[k], ay = s_path[j].y[k] - s_path[0].y[k];
        float bx = s_path[j].x[k + 1] - s_path[0].x[k + 1], by = s_path[j].y[k + 1] - s_path[0].y[k + 1];
        float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
        float t = l2 > 1e-12f ? clampf(-(ax * dx + ay * dy) / l2, 0, 1) : 0;
        float qx = ax + dx * t, qy = ay + dy * t, d = sqrtf(qx * qx + qy * qy);
        if (d < s_ca_d) {
            s_ca_d = d;
            s_ca_k = k;
            s_ca_t = (k + t) * s_pdt;
        }
    }
}
