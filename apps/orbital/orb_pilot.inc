// ORBITAL: the autopilot (included by app_orbital.c). The demo of the title, the AUTOPILOT help and the GUIDE help use
// it. It gives the same controls as a person (ctl_t): a turn, the engine, the tether. In the GUIDE help it does not
// fly: the game shows its aim (a marker on the ring of the nose), its wish to burn (the engine gate and the BURN hint),
// the time of its next burn, and its wish to fire or to let go (the TETHER button).
// The plan of a meeting: a search over the start time and the size of one burn along the way, with Kepler orbits (no
// air). The search is a job of many small steps (plan_work): each frame does a part, so a frame never stops.

typedef struct {
    float turn;
    uint8_t burn, fire, hold;
} ctl_t;

// ---- Kepler: a place on an orbit after t seconds (no air)
typedef struct {
    float a, e, w, M0, n, p, dir;
    int ok;
} kep_t;
static kep_t kep_from(float x, float y, float vx, float vy)
{
    kep_t k;
    elem_t E = elements(x, y, vx, vy);
    k.a = E.a;
    k.e = E.e;
    k.w = E.wp;
    k.dir = E.hz >= 0 ? 1.0f : -1.0f;
    k.ok = E.e < 0.95f && E.a > 0;
    if (!k.ok) {
        k.p = 1;
        k.n = 0;
        k.M0 = 0;
        return k;
    }
    k.p = E.a * (1 - E.e * E.e);
    k.n = fsqrt(MU_PL / (E.a * E.a * E.a));
    // (the eccentric anomaly from the true anomaly: atan2 of its sine and cosine)
    float cn = cosf(E.nu), sn = sinf(E.nu), d = 1 + E.e * cn;
    float ea = atan2f(fsqrt(1 - E.e * E.e) * sn / d, (E.e + cn) / d);
    k.M0 = ea - E.e * sinf(ea);
    return k;
}
static void kep_at(const kep_t *k, float t, float *x, float *y, float *vx, float *vy)
{
    float M = fmodf(k->M0 + k->n * t, TAU_F), e = k->e;
    float ea = M + e * sinf(M);
    for (int i = 0; i < 4; i++) {
        ea -= (ea - e * sinf(ea) - M) / (1 - e * cosf(ea));
    }
    float ce = cosf(ea), se = sinf(ea);
    float nu = atan2f(fsqrt(1 - e * e) * se, ce - e);
    float r = k->a * (1 - e * ce), ang = k->w + k->dir * nu, c = cosf(ang), s = sinf(ang);
    *x = r * c;
    *y = r * s;
    if (vx) {
        float q = fsqrt(MU_PL / k->p), vr = q * e * sinf(nu), vt = q * (1 + e * cosf(nu));
        *vx = vr * c - k->dir * vt * s;
        *vy = vr * s + k->dir * vt * c;
    }
}

// the least distance of a (from its time ta0) and b (from tb0) in span seconds: n samples, then a finer look
static float kep_min_dist(const kep_t *ka, float ta0, const kep_t *kb, float tb0, float span, int n, int refine,
                          float *tbest)
{
    float best = 1e9f, bt = 0;
    for (int i = 0; i <= n; i++) {
        float s = span * i / n, ax, ay, bx, by;
        kep_at(ka, ta0 + s, &ax, &ay, NULL, NULL);
        kep_at(kb, tb0 + s, &bx, &by, NULL, NULL);
        float d = sqf(ax - bx) + sqf(ay - by);
        if (d < best) {
            best = d;
            bt = s;
        }
    }
    float step = span / n;
    for (int it = 0; it < refine; it++) {
        step *= 0.5f;
        for (int sg = -1; sg <= 1; sg += 2) {
            float s = bt + sg * step, ax, ay, bx, by;
            if (s < 0) {
                continue;
            }
            kep_at(ka, ta0 + s, &ax, &ay, NULL, NULL);
            kep_at(kb, tb0 + s, &bx, &by, NULL, NULL);
            float d = sqf(ax - bx) + sqf(ay - by);
            if (d < best) {
                best = d;
                bt = s;
            }
        }
    }
    if (tbest) {
        *tbest = bt;
    }
    return fsqrt(best);
}
static int s_avoid = -1;  // a body that the plan must not meet (the debris of HEAD-ON)

// ---- Lambert: the orbit that goes from r1 to r2 in tof seconds (less than one turn, in the way dir of the orbit).
// The universal variable z (a bisection: the time grows with z); in float, with the series of the Stumpff functions
// near 0. A way of about 180 degrees has no stable answer (A goes to 0): no plan uses it.
static void stumpff(float z, float *C, float *S)
{
    if (z > 4) {
        float s = fsqrt(z);
        *C = (1 - cosf(s)) / z;
        *S = (s - sinf(s)) / (z * s);
    } else if (z < -4) {
        float s = fsqrt(-z), e = expf(s), ch = (e + 1 / e) * 0.5f, sh = (e - 1 / e) * 0.5f;
        *C = (ch - 1) / (-z);
        *S = (sh - s) / (-z * s);
    } else {
        float z2 = z * z, z3 = z2 * z, z4 = z2 * z2;
        *C = 0.5f - z / 24 + z2 / 720 - z3 / 40320 + z4 / 3628800;
        *S = 1.0f / 6 - z / 120 + z2 / 5040 - z3 / 362880 + z4 / 39916800;
    }
}
static bool lambert(float r1x, float r1y, float r2x, float r2y, float tof, float dir, float *v1x, float *v1y,
                    float *v2x, float *v2y)
{
    // (the other way round: mirror y, solve, mirror back)
    if (dir < 0) {
        r1y = -r1y;
        r2y = -r2y;
    }
    float r1 = fsqrt(r1x * r1x + r1y * r1y), r2 = fsqrt(r2x * r2x + r2y * r2y);
    float dth = atan2f(r1x * r2y - r1y * r2x, r1x * r2x + r1y * r2y);
    if (dth < 0) {
        dth += TAU_F;
    }
    float cdt = cosf(dth);
    if (1 - cdt < 1e-4f) {
        return false;
    }
    float A = sinf(dth) * fsqrt(r1 * r2 / (1 - cdt));
    if (absf(A) < 0.06f * fsqrt(r1 * r2)) {
        return false;
    }
    float sm = fsqrt(MU_PL), lo = -30, hi = 39.4f, z = 0, y = 0, C, S;
    for (int it = 0; it < 44; it++) {
        z = (lo + hi) * 0.5f;
        stumpff(z, &C, &S);
        y = r1 + r2 + A * (z * S - 1) / fsqrt(C);
        if (y <= 0) {
            lo = z;
            continue;
        }
        float q = y / C, F = q * fsqrt(q) * S + A * fsqrt(y) - sm * tof;
        if (F < 0) {
            lo = z;
        } else {
            hi = z;
        }
    }
    if (hi > 39.3f || lo < -29.9f || y <= 0) {
        return false;  // (no answer in one turn)
    }
    float f = 1 - y / r1, g = A * fsqrt(y / MU_PL), gd = 1 - y / r2;
    if (absf(g) < 1e-6f) {
        return false;
    }
    *v1x = (r2x - f * r1x) / g;
    *v1y = (r2y - f * r1y) / g;
    *v2x = (gd * r2x - r1x) / g;
    *v2y = (gd * r2y - r1y) / g;
    if (dir < 0) {
        *v1y = -*v1y;
        *v2y = -*v2y;
    }
    return true;
}

// ---- the plan of a meeting with body j: a wait t0, then one burn (a vector), then a coast of tof to the meeting.
// The search is a job of small steps over a grid of (t0, tof), then two finer grids round the best. The cost: the fuel of
// the burn and of the match at the meeting, and a little for the time. Each transfer stays out of the air and away from
// the debris; a target that dips into the air must be met before its low point (tlim).
typedef struct {
    float t0, tof, dvx, dvy, dv, dv2;
    int ok;
} rplan_t;
static rplan_t s_plan;
typedef struct {
    int on, done, j, stage;
    kep_t kt, kj, ka;
    float wait_max, tof_min, tof_max, tlim;
    int nw, nt, iw, it;
    float w_step, t_step, c_t0, c_tof;  // the grid of this stage: its steps and its centre
    float px, py, pvx, pvy;             // the tug at the wait (km, km/s), for the wait iw
    int wait_bad;                       // the debris comes near before this burn
    float best;
    double clock0;  // the mission time at the start of the search (its times count from here)
    rplan_t P;
} pjob_t;
static pjob_t s_pj;
static void plan_begin(int j, float wait_max, float tlim)
{
    memset(&s_pj, 0, sizeof s_pj);
    s_pj.on = 1;
    s_pj.j = j;
    s_pj.best = 1e9f;
    s_pj.clock0 = s_clock;
    s_pj.tlim = tlim;
    float x, y, vx, vy, m;
    com_state(0, &x, &y, &vx, &vy, &m);
    s_pj.kt = kep_from(x, y, vx, vy);
    s_pj.kj = kep_from((float)B[j].x, (float)B[j].y, B[j].vx, B[j].vy);
    if (s_avoid > 0) {
        s_pj.ka = kep_from((float)B[s_avoid].x, (float)B[s_avoid].y, B[s_avoid].vx, B[s_avoid].vy);
    }
    if (!s_pj.kt.ok || !s_pj.kj.ok) {
        s_pj.done = 1;
        return;
    }
    float T = TAU_F / s_pj.kt.n;
    s_pj.wait_max = wait_max;
    s_pj.tof_min = 150;
    s_pj.tof_max = T * 0.97f;
    s_pj.nw = wait_max > 1 ? 16 : 0;
    s_pj.nt = 18;
    s_pj.w_step = s_pj.nw ? wait_max / s_pj.nw : 0;
    s_pj.t_step = (s_pj.tof_max - s_pj.tof_min) / s_pj.nt;
    s_pj.stage = 1;
}
// the tug at the wait t0 (once for each wait)
static void plan_place(float t0)
{
    kep_at(&s_pj.kt, t0, &s_pj.px, &s_pj.py, &s_pj.pvx, &s_pj.pvy);
    // (the debris must not come near before the burn)
    s_pj.wait_bad = s_avoid > 0 && t0 > 1 && kep_min_dist(&s_pj.kt, 0, &s_pj.ka, 0, t0, 32, 4, NULL) < 3;
}
static void plan_eval(float t0, float tof)
{
    if (s_pj.wait_bad || t0 < 0 || tof < 60) {
        return;
    }
    if (t0 + tof > s_pj.tlim) {
        return;
    }
    float jx, jy, jvx, jvy, v1x, v1y, v2x, v2y;
    kep_at(&s_pj.kj, t0 + tof, &jx, &jy, &jvx, &jvy);
    if (!lambert(s_pj.px, s_pj.py, jx, jy, tof, s_pj.kt.dir, &v1x, &v1y, &v2x, &v2y)) {
        return;
    }
    float dvx = (v1x - s_pj.pvx) * 1000, dvy = (v1y - s_pj.pvy) * 1000, dv = fsqrt(dvx * dvx + dvy * dvy);
    float ex = (jvx - v2x) * 1000, ey = (jvy - v2y) * 1000, dv2 = fsqrt(ex * ex + ey * ey);
    if (dv > 450 || dv2 > 450) {
        return;
    }
    // (the transfer stays out of the air: its lowest point on the way)
    kep_t kn = kep_from(s_pj.px, s_pj.py, v1x, v1y);
    if (!kn.ok) {
        return;
    }
    if (kn.a * (1 - kn.e) < R_PL + 72) {
        for (int k = 1; k < 8; k++) {
            float x, y;
            kep_at(&kn, tof * k / 8, &x, &y, NULL, NULL);
            if (fsqrt(x * x + y * y) < R_PL + 70) {
                return;
            }
        }
    }
    if (s_avoid > 0 && kep_min_dist(&kn, 0, &s_pj.ka, t0, tof, 32, 4, NULL) < 3) {
        return;
    }
    float cost = dv + 0.8f * dv2 + 0.003f * (t0 + tof);
    if (cost < s_pj.best) {
        s_pj.best = cost;
        s_pj.P.t0 = t0;
        s_pj.P.tof = tof;
        s_pj.P.dvx = dvx;
        s_pj.P.dvy = dvy;
        s_pj.P.dv = dv;
        s_pj.P.dv2 = dv2;
        s_pj.P.ok = 1;
        s_pj.c_t0 = t0;
        s_pj.c_tof = tof;
    }
}
// n candidates of the search; true when the plan is ready
static bool plan_work(int n)
{
    if (!s_pj.on) {
        return true;
    }
    while (n-- > 0 && !s_pj.done) {
        float t0, tof;
        if (s_pj.stage == 1) {
            t0 = s_pj.iw * s_pj.w_step;
            tof = s_pj.tof_min + s_pj.it * s_pj.t_step;
        } else {
            // (stages 2 and 3: 5 x 5 round the best, at half and a quarter of the steps)
            float k = s_pj.stage == 2 ? 0.5f : 0.25f;
            t0 = s_pj.c_t0 + (s_pj.iw - 2) * s_pj.w_step * k;
            tof = s_pj.c_tof + (s_pj.it - 2) * s_pj.t_step * k;
        }
        if (s_pj.it == 0) {
            plan_place(t0 < 0 ? 0 : t0);
        }
        plan_eval(t0, tof);
        int nt = s_pj.stage == 1 ? s_pj.nt + 1 : 5, nw = s_pj.stage == 1 ? s_pj.nw + 1 : (s_pj.nw ? 5 : 1);
        if (++s_pj.it >= nt) {
            s_pj.it = 0;
            if (++s_pj.iw >= nw) {
                s_pj.iw = 0;
                if (++s_pj.stage > 3 || s_pj.best >= 1e9f) {
                    s_pj.done = 1;
                }
            }
        }
    }
    return s_pj.done;
}

// ---- the state of the pilot
enum {
    AP_START,
    AP_PLAN,
    AP_WAIT,
    AP_BURN,
    AP_COAST,
    AP_FIX,
    AP_CLOSE,
    AP_TOW,
    AP_LAND_WAIT,
    AP_LAND_BURN,
    AP_SAVE,
    AP_IDLE
};
static int s_ap, s_ap_tgt = 1;
static double s_ap_t0;            // the mission time of the planned burn
static float s_ap_dv, s_ap_done;  // the planned size of the burn (m/s), the size so far
static float s_ap_ux, s_ap_uy;    // the way of the burn (a world unit vector)
static float s_ap_v0x, s_ap_v0y;  // the engine speed (s_engx, s_engy) at the start of the burn (m/s)
static int s_ap_tries;
static double s_ap_meet;                                // the planned time of the meeting
static double s_ap_fix_t;                               // the last mid-course fix
static int s_ap_plans;                                  // the plans so far (a later plan has no limit of time)
static bool s_ap_aero;                                  // the tow uses the air brake
static double s_ap_land_t = -1;                         // the time of the last test fall (the landing missions)
static bool s_ap_letgo;                                 // the landing: the hold of the let go goes on
static int s_ap_trim = 1;                               // the landing: the way of the trim burn (1 back, -1 forward)
static float s_ap_land_da, s_ap_land_tf, s_ap_land_dv;  // its angle from the burn to the ground, its time, its burn
// the hints of the pilot for the GUIDE help and the HUD (each pilot_think sets them)
static float s_ap_aim;    // the world angle that the pilot wants for the nose
static bool s_ap_aim_on;  // the aim is a real wish (not only "keep the nose forward")
static bool s_ap_wb;      // the pilot wants the engine on now (the aim not counted)
static float s_ap_wait;   // mission seconds to the next planned burn (-1: none known)
static uint8_t s_ap_say;  // what the pilot does (the HUD): PS_*
enum {
    PS_NONE,
    PS_PLAN,
    PS_WAIT,
    PS_BURN,
    PS_COAST,
    PS_CLOSE,
    PS_FIRE,
    PS_TOW,
    PS_LETGO,
    PS_LAND,
    PS_SAVE,
    PS_DONE
};
static void pilot_reset(void)
{
    s_ap = AP_START;
    s_ap_tgt = 1;
    s_ap_tries = 0;
    s_ap_fix_t = 0;
    s_ap_plans = 0;
    s_ap_aero = false;
    s_ap_land_t = -1;
    s_ap_letgo = false;
    s_ap_wb = false;
    s_ap_aim_on = false;
    s_ap_wait = -1;
    s_ap_say = PS_NONE;
    s_pj.on = 0;
    s_avoid = -1;
    for (int i = 1; i < s_nb; i++) {
        if (B[i].kind == K_DEBRIS) {
            s_avoid = i;
        }
    }
}
// the turn to a world angle (the controller of the nose)
static float aim_turn(float world_ang, float *err)
{
    float th = atan2f((float)B[0].y, (float)B[0].x), want = wrap_pi(world_ang - th - PI_F / 2);
    float e = wrap_pi(want - s_nose);
    if (err) {
        *err = e;
    }
    s_ap_aim = world_ang;
    s_ap_aim_on = true;
    return clampf(e * 2.2f, -1, 1);
}
static void com_vel(float *vx, float *vy)
{
    float x, y, m;
    com_state(0, &x, &y, vx, vy, &m);
}
static float acc_now(void)
{
    return TUG_THRUST / (tug_mass() + (T.state == TH_ON ? B[T.to].mass : 0));
}
// a burn of the vector (dvx, dvy) m/s (of the tug with its tow)
static void burn_start(float dvx, float dvy)
{
    float d = fsqrt(dvx * dvx + dvy * dvy);
    s_ap = AP_BURN;
    s_ap_dv = d;
    s_ap_ux = d > 1e-6f ? dvx / d : 1;
    s_ap_uy = d > 1e-6f ? dvy / d : 0;
    s_ap_done = 0;
    s_ap_v0x = s_engx;
    s_ap_v0y = s_engy;
}
static bool goal_inside(void);
static float dv_left(void);
static void land_predict(float *impact_ang, float *impact_t, int *ok);
static float zone_angle(double t);
// mission seconds until the tug comes to the true anomaly nu1 (forward on its orbit)
static float time_to_nu(const elem_t *E, float nu1)
{
    if (E->period <= 0) {
        return -1;
    }
    float d = wrap_pi(nu1 - E->nu);
    if (d < 0) {
        d += TAU_F;
    }
    return d / TAU_F * E->period;
}
// the limit of the meeting: a target that dips into the air (its low point under 70 km) must be met before it
static float meet_limit(int j)
{
    elem_t F = elements((float)B[j].x, (float)B[j].y, B[j].vx, B[j].vy);
    if (F.rp > R_PL + 70 || F.period <= 0) {
        return 1e9f;
    }
    float t = time_to_nu(&F, 0) - 0.06f * F.period;
    return t > 200 ? t : 1e9f;
}
// the distance (m) at which the close guidance must take over: it must have the time to stop the drift
static float close_dist(float u)
{
    float a = acc_now();
    return fmaxf(8000, u * u / (2 * a) * 1.4f + u * 25);
}
// a test fall: the tow (the capsule) after a burn back, at once, to a low point at 30 km: the angle from here to the place
// where it comes down, the time of the fall (s), the size of the burn (m/s)
static void land_test(float *da, float *tf, float *dv)
{
    float x, y, vx, vy, m;
    com_state(0, &x, &y, &vx, &vy, &m);
    float r = fsqrt(x * x + y * y), v = fsqrt(vx * vx + vy * vy), rp = R_PL + 30;
    float vn = fsqrt(MU_PL * 2 * rp / (r * (r + rp)));
    *dv = (v - vn) * 1000;
    if (*dv < 0) {
        *dv = 0;
    }
    int i = T.state == TH_ON ? T.to : 1;
    pstate_t p;
    p.x = (double)x;
    p.y = (double)y;
    p.vx = vx * vn / v;
    p.vy = vy * vn / v;
    p.m = B[i].mass;
    p.cda = B[i].cda;
    p.heat = 0;
    p.heat_k = B[i].heat_k;
    p.peak = 0;
    float t = 0, a0 = atan2f(y, x);
    *da = 0;
    *tf = 0;
    for (int k = 0; k < 1500 && t < 6000; k++) {
        float alt = fsqrt(sqf((float)p.x) + sqf((float)p.y)) - R_PL, h = alt < 70 ? 2.0f : 8.0f;
        pstep(&p, h);
        t += h;
        if (alt < GONE_ALT) {
            float d = atan2f((float)p.y, (float)p.x) - a0;
            // (the way round of the tug: the angle grows for a prograde orbit)
            float hz = x * vy - y * vx;
            d = hz >= 0 ? d : -d;
            while (d < 0) {
                d += TAU_F;
            }
            *da = hz >= 0 ? d : -d;
            *tf = t;
            return;
        }
    }
}
// the speed (km/s) on an orbit of the half long axis a at the radius r
static float vis_viva(float r, float a)
{
    return fsqrt(MU_PL * fmaxf(2 / r - 1 / a, 0));
}
// the fuel (m/s) of the way from the orbit E down (or up) to a circle at mid: one burn at the high point, one at mid
static float band_need(const elem_t *E, float mid)
{
    float a0 = (E->rp + E->ra) / 2, a1 = (E->ra + mid) / 2;
    float d1 = absf(vis_viva(E->ra, a0) - vis_viva(E->ra, a1)), d2 = absf(vis_viva(mid, a1) - vis_viva(mid, mid));
    return (d1 + d2) * 1000;
}
// the world angle of a burn that raises the low point (the periapsis) of the tug with its tow the most: 16 ways, each
// with a small test burn of 1 m/s
static float lift_dir(void)
{
    float x, y, vx, vy, m;
    com_state(0, &x, &y, &vx, &vy, &m);
    float best = -1e9f, ba = atan2f(vy, vx);
    for (int k = 0; k < 16; k++) {
        float a = k * (TAU_F / 16);
        elem_t E = elements(x, y, vx + cosf(a) * 0.001f, vy + sinf(a) * 0.001f);
        if (E.rp > best) {
            best = E.rp;
            ba = a;
        }
    }
    return ba;
}
static ctl_t pilot_think(void)
{
    ctl_t c = {0, 0, 0, 0};
    const mis_t *M = &MIS[s_mis];
    float vx, vy;
    com_vel(&vx, &vy);
    float pro = atan2f(vy, vx);
    s_ap_wb = false;
    s_ap_aim_on = false;
    s_ap_aim = pro;
    s_ap_wait = -1;
    if (s_ap_tgt < 1 || s_ap_tgt >= s_nb) {
        s_ap_tgt = 1;
    }
    body_t *tg = &B[s_ap_tgt];
    if (s_ap == AP_START) {
        s_ap_say = PS_PLAN;
        if (T.state == TH_ON) {
            s_ap = AP_TOW;
            return c;
        }
        float dx = (float)((tg->x - B[0].x) * 1000.0), dy = (float)((tg->y - B[0].y) * 1000.0);
        float ux = (B[0].vx - tg->vx) * 1000, uy = (B[0].vy - tg->vy) * 1000;
        if (fsqrt(dx * dx + dy * dy) < fminf(close_dist(fsqrt(ux * ux + uy * uy)), 20000)) {
            s_ap = AP_CLOSE;
            return c;
        }
        elem_t E = body_elem(0);
        float lim = s_ap_plans == 0 ? meet_limit(s_ap_tgt) : 1e9f;
        plan_begin(s_ap_tgt, E.period * (s_ap_plans == 0 ? 1.1f : 2.5f), lim);
        s_ap_plans++;
        s_ap = AP_PLAN;
        return c;
    }
    if (s_ap == AP_PLAN || s_ap == AP_FIX) {
        s_ap_say = s_ap == AP_PLAN ? PS_PLAN : PS_COAST;
        if (s_ap == AP_FIX) {
            c.turn = aim_turn(pro, NULL);
            s_ap_aim_on = false;
        }
        // (the search is done in steps: a few ms of the board in each frame)
        if (!plan_work(s_ap == AP_PLAN ? 60 : 40)) {
            return c;
        }
        s_pj.on = 0;
        rplan_t p = s_pj.P;
        // (the search used the time of its start: the clock went on a little since then)
        if (s_ap == AP_FIX) {
            s_ap = AP_COAST;
            if (p.ok && p.dv > 0.6f && p.dv < 60) {
                s_ap_meet = s_pj.clock0 + (double)p.tof;
                burn_start(p.dvx, p.dvy);
            }
            return c;
        }
        if (!p.ok) {
            // (no plan: a longer search, or the close guidance from where it is)
            s_ap = s_ap_plans < 3 ? AP_START : AP_CLOSE;
            return c;
        }
        s_plan = p;
        s_ap_t0 = s_pj.clock0 + (double)p.t0;
        s_ap_dv = p.dv;
        s_ap_ux = p.dvx / fmaxf(p.dv, 1e-6f);
        s_ap_uy = p.dvy / fmaxf(p.dv, 1e-6f);
        s_ap_meet = s_pj.clock0 + (double)(p.t0 + p.tof);
        s_ap = AP_WAIT;
        return c;
    }
    if (s_ap == AP_WAIT) {
        // (point the nose; start the burn half its time early)
        float bt = s_ap_dv / acc_now();
        c.turn = aim_turn(atan2f(s_ap_uy, s_ap_ux), NULL);
        s_ap_wait = (float)(s_ap_t0 - s_clock) - bt / 2;
        s_ap_say = PS_WAIT;
        if (s_ap_wait <= 0) {
            burn_start(s_ap_ux * s_ap_dv, s_ap_uy * s_ap_dv);
        } else {
            return c;
        }
    }
    if (s_ap == AP_BURN) {
        float err;
        c.turn = aim_turn(atan2f(s_ap_uy, s_ap_ux), &err);
        // (the speed that the engine gave along the way of the burn)
        s_ap_done = (s_engx - s_ap_v0x) * s_ap_ux + (s_engy - s_ap_v0y) * s_ap_uy;
        s_ap_wb = true;
        s_ap_wait = 0;
        s_ap_say = PS_BURN;
        if (absf(err) < 0.15f) {
            c.burn = 1;
        }
        if (s_ap_done >= s_ap_dv - 0.05f) {
            c.burn = 0;
            s_ap_wb = false;
            s_ap = T.state == TH_ON ? AP_TOW : AP_COAST;
            s_ap_fix_t = s_clock;
        }
        return c;
    }
    if (s_ap == AP_COAST) {
        s_ap_say = PS_COAST;
        float dx = (float)((tg->x - B[0].x) * 1000.0), dy = (float)((tg->y - B[0].y) * 1000.0),
              d = fsqrt(dx * dx + dy * dy);
        float ux = (B[0].vx - tg->vx) * 1000, uy = (B[0].vy - tg->vy) * 1000;
        if (d < close_dist(fsqrt(ux * ux + uy * uy))) {
            s_ap = AP_CLOSE;
            return c;
        }
        // a mid-course fix (each 250 s, while the meeting is more than 250 s away): a new transfer to the same meeting
        float left = (float)(s_ap_meet - s_clock);
        if (s_clock - s_ap_fix_t > 250 && left > 250) {
            s_ap_fix_t = s_clock;
            float x, y, cvx, cvy, m, jx, jy, jvx, jvy, v1x, v1y, v2x, v2y;
            com_state(0, &x, &y, &cvx, &cvy, &m);
            kep_t kj = kep_from((float)tg->x, (float)tg->y, tg->vx, tg->vy);
            kep_t kt = kep_from(x, y, cvx, cvy);
            if (kj.ok && kt.ok) {
                kep_at(&kj, left, &jx, &jy, &jvx, &jvy);
                if (lambert(x, y, jx, jy, left, kt.dir, &v1x, &v1y, &v2x, &v2y)) {
                    float fx = (v1x - cvx) * 1000, fy = (v1y - cvy) * 1000, f = fsqrt(fx * fx + fy * fy);
                    if (f > 0.6f && f < 60) {
                        burn_start(fx, fy);
                        return c;
                    }
                }
            }
        }
        // (no meeting: plan again after the time of the meeting)
        if (s_clock > s_ap_meet + 200) {
            s_ap = AP_START;
        }
        c.turn = aim_turn(pro, NULL);
        s_ap_aim_on = false;
        return c;
    }
    if (s_ap == AP_CLOSE) {
        if (T.state == TH_ON) {
            s_ap = AP_TOW;
            return c;
        }
        s_ap_say = PS_CLOSE;
        // the tug relative to the target: r (m) to the target, u (m/s) the speed of the tug against it
        double hxw, hyw;
        hook_world(tg, &hxw, &hyw);
        float rx = (float)((hxw - B[0].x) * 1000.0), ry = (float)((hyw - B[0].y) * 1000.0),
              d = fsqrt(rx * rx + ry * ry);
        float ux = (B[0].vx - tg->vx) * 1000, uy = (B[0].vy - tg->vy) * 1000, u = fsqrt(ux * ux + uy * uy);
        if (d > 30000) {
            s_ap = AP_START;
            return c;
        }
        float vapp = d > 150 ? clampf(0.03f * d, 0.8f, 30) : 0.5f;
        float wx = rx / d * vapp - ux, wy = ry / d * vapp - uy, w = fsqrt(wx * wx + wy * wy), err;
        c.turn = aim_turn(atan2f(wy, wx), &err);
        if (w > 0.3f) {
            s_ap_wb = true;
        }
        if (w > 0.3f && absf(err) < 0.2f) {
            c.burn = 1;
        }
        // the tether: in range and slow; a hook that turns: fire when it will face the tug at the arrival
        if (T.state == TH_NONE && d < 215 && u < 2.5f) {
            float tf = d / TETHER_SPEED, keep = tg->ang;
            tg->ang += tg->w * tf;
            bool faces = hook_faces(tg, -rx, -ry);
            tg->ang = keep;
            if (faces) {
                c.fire = 1;
                s_ap_tries++;
                s_ap_say = PS_FIRE;
            }
        }
        if (T.state == TH_FLY) {
            s_ap_wb = false;
            c.burn = 0;
        }
        return c;
    }
    if (s_ap == AP_TOW) {
        if (T.state != TH_ON) {
            s_ap = AP_CLOSE;
            return c;
        }
        s_ap_say = PS_TOW;
        if (T.L > T.L_goal + 2) {
            s_ap_aim_on = false;
            return c;
        }  // (the winch first)
        elem_t E = body_elem(0);
        int g = M->goal_kind;
        if (g == G_DOCK || g == G_DODGE) {
            s_ap_say = PS_DONE;
            return c;
        }
        if (g == G_LIFT) {
            if (E.rp >= R_PL + M->lo + 2) {
                s_ap_say = PS_DONE;
                return c;
            }
            // (the way of the burn that raises the low point most: near the high point it is forward)
            float err;
            c.turn = aim_turn(lift_dir(), &err);
            s_ap_wb = true;
            s_ap_say = PS_BURN;
            if (absf(err) < 0.15f) {
                c.burn = 1;
            }
            return c;
        }
        if (g == G_LAND) {
            s_ap = AP_LAND_WAIT;
            return c;
        }
        // G_BAND, G_DELIVER2: the band
        float lo = R_PL + M->lo, hi = R_PL + M->hi, mid = (lo + hi) / 2;
        if (goal_inside()) {
            if (g == G_DELIVER2 && T.to == 1) {
                // (the first wreck: let go (a hold of the TETHER key); then the next wreck; the second one stays on the
                // rope until the end)
                c.hold = 1;
                c.turn = 0;
                s_ap_say = PS_LETGO;
                return c;
            }
            s_ap_say = PS_DONE;
            return c;
        }
        // the rules (in this order): the air brake when the fuel is short; the whole orbit above the band: lower the low
        // point at the high point; the whole orbit below: raise the high point at the low point; the far side too high:
        // lower it at the low point; the near side too low: raise it at the high point
        bool circ = E.e < 0.004f;
        bool at_pe = absf(E.nu) < 0.2f || circ, at_ap = absf(wrap_pi(E.nu - PI_F)) < 0.2f || circ;
        // (the air brake, once chosen, goes on until the high point is in the band)
        // (it is chosen for a big drop (the far side 100 km over the band: the air saves fuel), or when the fuel is short)
        if (!s_ap_aero && E.ra > hi + 20 && (E.ra > hi + 100 || dv_left() < band_need(&E, mid) * 1.1f)) {
            s_ap_aero = true;
        }
        // (it ends when the high point is less than 35 km over the band: the next pass could take it under the band)
        if (s_ap_aero && E.ra <= hi + 35) {
            s_ap_aero = false;
        }
        bool aero = s_ap_aero;
        int want = 0, where = -1;  // want: + forward, - back; where: 0 the low point, 1 the high point
        bool stop = false;
        if (aero) {
            if (E.rp > R_PL + 56) {
                want = -1;
                where = 1;
                stop = E.rp <= R_PL + 54;
            }
        } else if (E.rp > hi) {
            want = -1;
            where = 1;
            stop = E.rp <= mid;
        } else if (E.ra < lo) {
            want = 1;
            where = 0;
            stop = E.ra >= mid;
        } else if (E.rp < lo) {
            want = 1;
            where = 1;
            stop = E.rp >= mid;
        } else if (E.ra > hi) {
            want = -1;
            where = 0;
            stop = E.ra <= mid;
        }
        bool now = want != 0 && !stop && ((where == 0 && at_pe) || (where == 1 && at_ap));
        float err;
        // (the nose turns before the burn: the same way along the orbit)
        c.turn = aim_turn(want < 0 ? pro + PI_F : pro, &err);
        if (want == 0) {
            s_ap_aim_on = false;
        }
        if (now) {
            s_ap_wb = true;
            if (absf(err) < 0.15f) {
                c.burn = 1;
            }
            s_ap_say = PS_BURN;
        } else if (want != 0 && where >= 0 && E.period > 0) {
            float t = time_to_nu(&E, where == 0 ? 0 : PI_F) - 0.2f / TAU_F * E.period;
            if (t < 0) {
                t += E.period;
            }
            s_ap_wait = t;
            s_ap_say = PS_WAIT;
        } else {
            s_ap_say = aero ? PS_TOW : PS_WAIT;
        }
        return c;
    }
    if (s_ap == AP_LAND_WAIT || s_ap == AP_LAND_BURN) {
        if (T.state != TH_ON) {
            s_ap = AP_SAVE;
            return c;
        }
        s_ap_say = PS_LAND;
        float err;
        if (s_ap == AP_LAND_WAIT) {
            // the wait: the burn starts when a fall from here (the low point at 30 km) would end at the ship, a little
            // early for the length of the burn. The way of a fall (its angle and its time) comes from a test fall
            if (s_clock - s_ap_land_t > 120 || s_ap_land_t < 0) {
                land_test(&s_ap_land_da, &s_ap_land_tf, &s_ap_land_dv);
                s_ap_land_t = s_clock;
            }
            c.turn = aim_turn(pro + PI_F, &err);
            float th = atan2f((float)B[0].y, (float)B[0].x);
            float miss = wrap_pi(th + s_ap_land_da - zone_angle(s_clock + (double)s_ap_land_tf));
            // (the burn takes about dv / a seconds; the tug flies on in that time)
            float bt = s_ap_land_dv / acc_now();
            elem_t E = body_elem(0);
            float n = E.period > 0 ? TAU_F / E.period : 0.003f, lead = n * bt * 0.5f;
            float a2 = wrap_pi(-(miss + lead));  // (the angle that the tug must still fly)
            if (a2 < 0) {
                a2 += TAU_F;
            }
            float rel = n - PLANET_SPIN;
            if (a2 < 0.01f || a2 > TAU_F - 0.02f) {
                s_ap = AP_LAND_BURN;
            } else {
                s_ap_wait = rel > 0 ? a2 / rel : -1;
                s_ap_say = PS_WAIT;
                return c;
            }
        }
        // the burn: the fall of the capsule if it goes now; back (retro) while the fall ends past the ship, forward when
        // it ends before it; let go when it ends at the ship
        elem_t E = body_elem(0);
        // (first the low point down to 30 km: a fall from a higher low point can skip on the air and end far away)
        if (E.rp > R_PL + 32 && !s_ap_letgo) {
            c.turn = aim_turn(pro + PI_F, &err);
            s_ap_wb = true;
            if (absf(err) < 0.15f) {
                c.burn = 1;
            }
            s_ap_say = PS_BURN;
            return c;
        }
        float ia, it;
        int ok;
        land_predict(&ia, &it, &ok);
        float miss = ok ? wrap_pi(ia - zone_angle(s_clock + (double)it)) : 1;
        // (the let go starts when the fall ends within 2 degrees of the ship, and goes on to its end unless the fall moves
        // more than 3 degrees away: the hold of 1 s must not stop)
        if (ok && absf(miss) < 0.035f) {
            s_ap_letgo = true;
        }
        if (s_ap_letgo && (!ok || absf(miss) > 0.055f)) {
            s_ap_letgo = false;
        }
        if (s_ap_letgo) {
            c.turn = aim_turn(pro + PI_F, &err);
            c.hold = 1;
            s_ap_say = PS_LETGO;
            return c;
        }
        // (the trim: back while the fall ends past the ship, forward while it ends before it; a dead band keeps the way)
        if (ok && miss < -0.02f) {
            s_ap_trim = -1;
        }
        if (!ok || miss > 0.02f) {
            s_ap_trim = 1;
        }
        bool fwd = s_ap_trim < 0;
        if (!fwd && E.rp < R_PL + 18) {
            c.turn = aim_turn(pro + PI_F, &err);
            c.hold = 1;
            s_ap_say = PS_LETGO;
            return c;
        }
        c.turn = aim_turn(fwd ? pro : pro + PI_F, &err);
        s_ap_wb = true;
        if (absf(err) < 0.15f) {
            c.burn = 1;
        }
        s_ap_say = PS_BURN;
        return c;
    }
    if (s_ap == AP_SAVE) {
        // after the release: the tug lifts its own low side out of the air
        elem_t E = body_elem(0);
        if (E.rp >= R_PL + 74) {
            s_ap_say = PS_DONE;
            return c;
        }
        s_ap_say = PS_SAVE;
        float err;
        c.turn = aim_turn(lift_dir(), &err);
        s_ap_wb = true;
        if (absf(err) < 0.15f) {
            c.burn = 1;
        }
        return c;
    }
    return c;
}
