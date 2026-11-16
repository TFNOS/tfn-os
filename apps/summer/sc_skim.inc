// SUMMER CUP: SKIM (skimboard), included by app_summer.c. The wet sand of the shore: the sand goes down to the right
// (y = -SLOPE x; x = 0 at the still water line). The wash of each wave runs up the sand as a thin film, then drains
// back. THE CORE RULE (thin water is fast): the board glides only on a film of water. Dry sand stops it at once; a
// film of 1 - 3 cm (and the drain of the backwash) makes it fly; deep water makes it plow and sink. The rider runs to
// the sea by itself; a touch drops the board and the rider jumps on. Then the next small wave is a ramp: the speed of
// the board and of the wave throws the rider up (and back to the shore).

#define SLOPE  0.07f
#define SW_T   5.2f      // the time of one wash (s)
#define X_UP   (-11.0f)  // the highest point of the wash on the sand
#define WAVE_C 3.0f      // the speed of the shore-break wave
#define WAVE_H 1.15f
enum {
    SM_READY,
    SM_RUN,
    SM_GLIDE,
    SM_RAMP,
    SM_AIR,
    SM_OUT,
    SM_FALL,
    SM_WAIT
};
static int m_mode, m_run;
static bool s_did_drop;  // (the hint of the drop: set when the player makes a drop)
static float m_mode_t, m_x, m_u, m_t0, m_glide, m_wx, m_launch_h, m_ai_drop;
static float m_run_pts;
#define WET_N 360
static float m_wet[WET_N];  // the wet sand: cells of 0.1 m from x = -24 (1 = just wet; it dries)
static inline float m_wet_at(float x)
{
    int i = (int)((x + 24) * 10);
    return i < 0 || i >= WET_N ? 0 : m_wet[i];
}
static bool m_sticky_said;

static float m_tau(void)
{
    float t = fmodf(s_ev_t + m_t0, SW_T);
    return t < 0 ? t + SW_T : t;
}
// the front of the wash on the sand now
static float m_front(void)
{
    float t = m_tau();
    if (t < 2.0f) {
        return 2 - 13 * fsin((float)M_PI * 0.5f * t / 2.0f);
    }
    return X_UP + 13 * smooth01((t - 2.0f) / (SW_T - 2.0f));
}
static bool m_backwash(void)
{
    return m_tau() >= 2.0f;
}
static float m_sand(float x)
{
    return -SLOPE * x;
}
// the depth of the film of the wash (m) at x (0 = dry sand), and its speed (+ = to the sea)
static float m_film(float x)
{
    float xf = m_front();
    if (x < xf) {
        return 0;
    }
    float t = m_tau();
    float dm = t < 2.0f ? 0.065f : lerpf(0.040f, 0.010f, clampf((t - 2.0f) / 2.6f, 0, 1));
    float k = clampf((x - xf) / 2.2f, 0, 1);
    float f = dm * fsq(k);
    if (x > 0) {
        f += SLOPE * x;  // (the sea)
    }
    return f;
}
static float m_flow(void)
{
    float t = m_tau();
    if (t < 2.0f) {
        return -2.2f * (1 - t / 2.0f);
    }
    return 2.4f * fsin((float)M_PI * (t - 2.0f) / (SW_T - 2.0f));
}
// the shore-break wave: its crest comes from the sea and breaks at the shore at the start of each wash
static float m_wave_x(void)
{
    float t = m_tau();
    float to_break = SW_T - t;  // (it breaks at x = 2 when the next wash starts)
    return 2 + WAVE_C * to_break;
}
static float m_wave_h(void)
{
    float wx = m_wave_x();
    return WAVE_H * clampf((16 - wx) / 9.0f, 0.15f, 1);
}
// the water surface (the film or the sea with the wave); returns -100 for dry sand
static float m_water(float x)
{
    float f = m_film(x);
    float wx = m_wave_x(), h = m_wave_h();
    float dx = x - wx, e = 0;
    if (dx > -1.6f && dx < 5) {
        e = dx < 0 ? h * (1 - smooth01(-dx / 1.6f)) : h * (1 - smooth01(dx / 5.0f));
    }
    if (x < wx - 1.6f && f <= 0) {
        return -100;
    }
    return m_sand(x) + fmaxf(f, 0) + (x > 0 || f > 0 ? e : 0);
}
static float m_surface(float x)
{
    float w = m_water(x), s = m_sand(x);
    return w > s ? w : s;
}

static bool m_ground(float *x, float *y, float *px, float *py, float dt)
{
    (void)dt;
    float s = m_sand(*x), w = m_water(*x);
    if (w > s && *y < w) {  // (water: soft)
        float vx = (*x - *px) * 0.92f, vy = (*y - *py) * 0.85f;
        vy += (w - *y) * 4 * dt * dt * 60;
        *px = *x - vx;
        *py = *y - vy;
    }
    if (*y < s) {
        *y = s;
        float vx = (*x - *px) * 0.55f;
        *px = *x - vx;
        *py = *y;
        return true;
    }
    return false;
}

static void m_new_run(void)
{
    m_mode = SM_READY;  // (the rider waits for the wash, then runs as it drains)
    m_mode_t = 0;
    m_x = -17;
    m_u = 0;
    m_glide = 0;
    m_run_pts = 0;
    m_launch_h = 0;
    m_sticky_said = false;
    // the wash: a new phase for each run (the rider must read the water)
    m_t0 = frnd(0, SW_T) - s_ev_t;
    m_ai_drop = frnd(-1, 1) * s_ai_err;
    s_rag.on = false;
    memset(&A, 0, sizeof A);
}
static void skim_start(void)
{
    m_run = 0;
    s_ev_len = 70;
    s_pal = &PAL_SKIM;
    look_set(GEAR_SKIM);
    s_rd_ground = m_ground;
    for (int i = 0; i < WET_N; i++) {
        m_wet[i] = 0;
    }
    m_new_run();
    s_ctx = -12;
    s_cty = 1.5f;
    s_ctz = s_zb * 0.9f;
}

static void m_end(const char *en, const char *he)
{
    char t[40];
    int pts = (int)m_run_pts;
    snprintf(t, sizeof t, "%s %.1f M", TR("GLIDE", "גלישה"), m_glide);
    trick_say(t, en ? TR(en, he) : "", pts, rgb(255, 230, 150));
    m_mode = SM_WAIT;
    m_mode_t = 0;
    m_run++;
}
static void m_fall(float vx, float vy, float w)
{
    pose_t P = {0};
    P.gear = GEAR_SKIM;
    P.arms = ARMS_OUT;
    P.crouch = 0.4f;
    P.tuck = A.tuck;
    place_t pl;
    if (m_mode == SM_AIR) {
        s_pl.ang = A.ang;
        s_pl.side = 1;
        s_pl.x = 0;
        s_pl.y = 0;
        pose_make(&P);
        v2 cb = pose_com();
        float cs = fcos(A.ang), sn = fsin(A.ang);
        pl.x = A.x - (cb.x * cs - cb.y * sn);
        pl.y = A.y - (cb.x * sn + cb.y * cs);
        pl.ang = A.ang;
    } else {
        pl.x = m_x;
        pl.y = m_surface(m_x);
        pl.ang = 0;
    }
    pl.side = 1;
    rag_start(&P, pl, vx, vy, w, 1.0f / 240);
    m_mode = SM_FALL;
    m_mode_t = 0;
    A.on = false;
    shake(12);
    slowmo(0.5f, 0.5f);
    fx(FX_SPLASH, 85);
    for (int i = 0; i < 26; i++) {
        part_add(PK_SPRAY, pl.x, pl.y + 0.2f, frnd(-2, 2), frnd(1, 4), frnd(0.4f, 0.9f), frnd(0.04f, 0.1f));
    }
}

static void skim_step(float dt)
{
    m_mode_t += dt;
    float xf = m_front();
    // the wet sand dries; the wash makes it wet again
    for (int i = 0; i < WET_N; i++) {
        float wx = -24 + i * 0.1f;
        if (wx >= xf) {
            m_wet[i] = 1;
        } else {
            m_wet[i] *= 1 - dt * 0.08f;
        }
    }
    if (m_mode == SM_READY) {
        // the rider bounces on the dry sand and starts to run when the wash is at its top (it drains after it)
        float t = m_tau();
        if (t > 1.15f && t < 1.6f && m_mode_t > 0.6f) {
            m_mode = SM_RUN;
            m_mode_t = 0;
        }
        s_press = false;
        s_swipe = 0;
    } else if (m_mode == SM_RUN) {
        m_u += (7.6f - m_u) * fminf(1, dt * 1.8f);
        m_x += m_u * dt;
        bool drop = s_press;
        s_press = false;
        if (m_x > 1.0f) {
            drop = true;  // (the latest point: the water is deep there)
        }
        if (drop && m_x > -15.5f) {
            if (!s_demo && m_x < 1.0f) {
                s_did_drop = true;
            }
            m_mode = SM_GLIDE;
            m_mode_t = 0;
            m_u += 0.5f;
            fx(FX_SLAP, 75);
            for (int i = 0; i < 10; i++) {
                part_add(m_film(m_x) > 0.002f ? PK_SPRAY : PK_SAND, m_x, m_sand(m_x), frnd(1, 4), frnd(0.3f, 1.8f),
                         frnd(0.3f, 0.7f), frnd(0.03f, 0.07f));
            }
        }
        s_swipe = 0;
    } else if (m_mode == SM_GLIDE) {
        // THE CORE RULE: the board planes on a thin film (little friction); on dry sand it stops at once; in deep
        // water it plows when it is slow (a fast board planes over the water)
        int n = (int)(dt * 240) + 1;
        float h = dt / n;
        for (int i = 0; i < n; i++) {
            float dep = m_film(m_x);
            float a = GRAV * SLOPE;
            if (dep < 0.003f) {
                a -= 0.48f * GRAV;  // (dry sand)
            } else {
                a -= 0.03f * GRAV;
                a -= 0.12f * (m_u - m_flow());  // (the water under the board)
                if (dep > 0.03f) {
                    a -= 2.2f * clampf((dep - 0.03f) / 0.05f, 0, 3) * clampf(1 - m_u / 6.5f, 0.12f, 1);
                }
            }
            m_u += a * h;
            if (m_u < 0) {
                m_u = 0;
            }
            m_x += m_u * h;
            m_glide += m_u * h;
        }
        float dep = m_film(m_x);
        if ((rnd32() & 1) && m_u > 1) {
            part_add(dep > 0.002f ? PK_SPRAY : PK_SAND, m_x - 0.4f, m_surface(m_x), -m_u * 0.2f + frnd(-0.5f, 0.5f),
                     frnd(0.3f, 1.4f), frnd(0.2f, 0.5f), frnd(0.025f, 0.05f));
        }
        drone(0, 50 + m_u * 6, (int)clampf(m_u * 2.4f, 0, 18));
        // the wave: the board meets its face
        float wx = m_wave_x();
        if (m_x > wx - 1.4f && m_x < wx + 0.5f && m_u > 1.0f && m_wave_h() > 0.3f) {
            m_mode = SM_RAMP;
            m_mode_t = 0;
        } else if (m_u < 0.25f) {
            m_run_pts += m_glide * 60;
            add_score(m_glide * 60);
            m_end(dep < 0.003f ? "STUCK IN THE SAND" : "SANK", dep < 0.003f ? "נתקע בחול" : "שקע במים");
            fx(FX_THUD, 40);
        } else if (dep > 0.12f && m_u < 2.0f) {
            m_run_pts += m_glide * 60;
            add_score(m_glide * 60);
            m_fall(m_u * 0.5f, 0, 1);
        }
        s_swipe = 0;
    } else if (m_mode == SM_RAMP) {
        // up the face of the wave: the speed of the board and of the wave turns into height
        float h = m_wave_h();
        float vrel = m_u + WAVE_C;
        float vy = vrel * 0.62f, vx = m_u * 0.25f - WAVE_C * 0.55f;
        m_launch_h = vy * vy / (2 * GRAV);
        m_run_pts += m_glide * 60;
        add_score(m_glide * 60);
        air_start(m_x, m_sand(m_x) + h, vx, vy, -0.35f, 0.6f, 1);
        m_mode = SM_AIR;
        m_mode_t = 0;
        s_ai_rand = frnd(0, 1);
        fx(FX_SPLASH2, 80);
        shake(4);
        for (int i = 0; i < 30; i++) {
            part_add(PK_SPRAY, m_x + 0.3f, m_sand(m_x) + h * 0.8f, frnd(-2, 2), frnd(2, 6), frnd(0.5f, 1.1f),
                     frnd(0.04f, 0.11f));
        }
    } else if (m_mode == SM_AIR) {
        drone(0, 40, 0);
        air_step(dt, s_lean);
        A.vy -= GRAV * dt;
        A.x += A.vx * dt;
        A.y += A.vy * dt;
        float s = m_surface(A.x);
        if (A.vy < 0 && A.y <= s) {
            float e0 = m_surface(A.x - 0.1f), e1 = m_surface(A.x + 0.1f);
            float target = fatan2(e1 - e0, 0.2f);
            int res = air_land(target);
            float base = 200 + (A.ymax - A.y0 + 0.6f) * 520;
            int pts = air_score(res, base, "SHUV-IT", "שוב-איט");
            m_run_pts += pts;
            if (res == LAND_FALL) {
                m_fall(A.vx, A.vy, A.w);
                return;
            }
            m_mode = SM_OUT;
            m_mode_t = 0;
            m_x = A.x;
            m_u = fabsf(A.vx) + 1;
            fx(FX_SLAP, 80);
            for (int i = 0; i < 16; i++) {
                part_add(PK_SPRAY, A.x, s, frnd(-2, 2), frnd(1, 3), frnd(0.4f, 0.8f), frnd(0.04f, 0.09f));
            }
        } else if (A.ymax - A.y0 > 1.4f && fabsf(A.vy) < 2.2f) {
            slowmo(0.45f, 0.2f);
        }
    } else if (m_mode == SM_OUT) {
        // the ride to the sand after the landing (the board slows down)
        m_u -= 2.5f * dt;
        if (m_u < 0) {
            m_u = 0;
        }
        m_x -= m_u * dt;
        if (m_mode_t > 1.4f) {
            m_end(NULL, NULL);
        }
        s_swipe = 0;
    } else if (m_mode == SM_FALL) {
        rag_step(dt, GRAV);
        s_swipe = 0;
        if (m_mode_t > 2.2f) {
            m_end(NULL, NULL);
        }
    } else if (m_mode == SM_WAIT) {
        drone(0, 40, 0);
        s_swipe = 0;
        s_press = false;
        if (m_mode_t > 2.2f && m_run < 3) {
            m_new_run();
        }
    }
    drone(1, 28, 12);
    // the camera: the rider near the middle; the wave in the view when it comes; near in the air
    float rx = m_mode == SM_AIR ? A.x : m_mode == SM_FALL ? rag_com().x : m_x;
    float ry = m_mode == SM_AIR ? A.y : m_mode == SM_FALL ? rag_com().y : m_surface(m_x);
    if (m_mode == SM_AIR) {
        s_ctx = rx;
        s_cty = ry + 0.4f;
        s_ctz = s_zb * 1.3f;
    } else if (m_mode == SM_WAIT || m_mode == SM_READY) {
        s_ctx = -12;
        s_cty = 1.5f;
        s_ctz = s_zb * 0.9f;
    } else {
        float wx = m_wave_x();
        float look = m_mode == SM_GLIDE && wx - rx < 9 ? (wx - rx) * 0.35f : 1.5f;
        s_ctx = rx + look;
        s_cty = ry + 1.0f;
        s_ctz = s_zb * (m_mode == SM_GLIDE ? 1.0f : 0.95f);
    }
}

static bool skim_over(void)
{
    return m_run >= 3 && m_mode == SM_WAIT && m_mode_t > 1.4f;
}

// the AI: drop the board on the thin film of the backwash, near its front; flips in the air
static void skim_ai(ai_t *o)
{
    if (m_mode == SM_RUN) {
        float xf = m_front();
        float ahead = m_x + m_u * (0.08f + m_ai_drop);  // (where the board lands)
        float f = m_film(ahead);
        o->press = m_backwash() && f > 0.002f && f < 0.03f && ahead > xf + 1.2f;
    } else if (m_mode == SM_AIR) {
        float tl = (A.vy + fsq(A.vy * A.vy + 2 * GRAV * fmaxf(0, A.y - m_surface(A.x)))) / GRAV;
        float rot = (A.ang - A.ang0) * A.back;
        bool flip = m_launch_h > 1.0f && s_ai_rand < 0.7f;
        if (flip && A.throws == 0 && A.t > 0.03f) {
            o->swipe = 1;
        } else if (!flip && s_ai_rand > 0.3f && A.whips == 0 && fabsf(A.whip_v) < 0.1f && A.t > 0.1f && tl > 0.5f) {
            o->swipe = 2;
        }
        if (flip) {
            o->hold = A.throws == 0 || rot + fabsf(A.L) * tl < 2 * (float)M_PI;
        }
        if (s_ai_err < 0.05f) {
            float pred = A.ang + A.w * tl;
            float err = wrap_pi(pred);
            o->lean = clampf(-err * 1.5f, -1, 1);
        }
    }
}

// ---------------------------------------------------------------- the drawing
static int16_t s_msand[660], s_mwat[660];
static uint8_t s_mwetc[660];
static dc_t s_md_sand[4][24], s_md_wat[24], s_md_film[2];

static void tower(float x)
{
    // a lifeguard tower on the sand: legs, a hut, a ramp, a flag
    float base = m_sand(x);
    uint16_t wood = rgb(236, 236, 230), leg = rgb(150, 140, 130), roof = rgb(240, 80, 70);
    wcap(x - 0.9f, base, x - 0.7f, base + 2.0f, 0.05f, 0.05f, leg, 256);
    wcap(x + 0.9f, base, x + 0.7f, base + 2.0f, 0.05f, 0.05f, leg, 256);
    wcap(x - 0.8f, base + 1.0f, x + 0.8f, base + 1.0f, 0.03f, 0.03f, leg, 256);
    float hy = base + 2.0f;
    for (int k = 0; k < 6; k++) {
        wcap(x - 1.0f, hy + 0.15f + k * 0.17f, x + 1.0f, hy + 0.15f + k * 0.17f, 0.09f, 0.09f,
             k == 3 ? rgb(90, 170, 220) : wood, 256);
    }
    wcap(x - 1.3f, hy + 1.2f, x + 1.3f, hy + 1.2f, 0.12f, 0.12f, roof, 256);
    wcap(x + 1.0f, hy + 0.1f, x + 2.4f, base + 0.02f, 0.05f, 0.05f, leg, 256);
    wcap(x, hy + 1.2f, x, hy + 2.2f, 0.02f, 0.02f, leg, 256);
    float fl = fsin(s_t * 4) * 0.08f;
    wcap(x, hy + 2.15f, x + 0.6f, hy + 2.0f + fl, 0.09f, 0.06f, roof, 240);
}
static void umbrella(float x, uint16_t c1, uint16_t c2)
{
    float b = m_sand(x);
    wcap(x, b, x + 0.2f, b + 2.0f, 0.03f, 0.03f, rgb(230, 230, 230), 256);
    float pts[24];
    int n = 0;
    float cx = SX(x + 0.2f), cy = SY(b + 2.0f), r = 1.2f * cam_z;
    for (int i = 0; i <= 10; i++) {
        float a = (float)M_PI * (0.05f + 0.9f * i / 10.0f);
        pts[n++] = cx - r * fcos(a);
        pts[n++] = cy - r * 0.45f * fsin(a);
    }
    pts[n++] = cx;
    pts[n++] = cy + 1;
    poly(pts, n / 2, c1, 256);
    for (int k = 0; k < 3; k++) {
        float a = (float)M_PI * (0.2f + 0.3f * k);
        capsule(cx, cy - r * 0.45f, cx - r * fcos(a), cy - r * 0.45f * fsin(a) + 1, 1.2f, 1.2f, c2, 220);
    }
}

static void skim_render(void)
{
    // the sand and the water in each column
    int miny = SH;
    float xf = m_front();
    for (int x = 0; x < SW; x++) {
        float wx = WXs(x + 0.5f);
        float s = m_sand(wx), w = m_water(wx);
        int ss = clampi((int)ceilf(SY(s)), 0, SH), ws = w > s ? clampi((int)ceilf(SY(w)), 0, SH) : ss;
        s_msand[x] = (int16_t)ss;
        s_mwetc[x] = (uint8_t)clampi((int)(m_wet_at(wx) * 3.99f), 0, 3);
        s_mwat[x] = (int16_t)ws;
        s_ntop[x] = (int16_t)ws;
        if (ws < miny) {
            miny = ws;
        }
    }
    prof_start();
    far_setup();
    far_raster();
    prof_mark(PR_FAR);
    far_props();
    s_cf = true;
    tower(-8.5f);
    umbrella(-13.5f, rgb(255, 210, 70), rgb(240, 90, 80));
    umbrella(-4.5f, rgb(70, 170, 230), rgb(250, 250, 250));
    s_cf = false;
    prof_mark(PR_PROPS);
    // the colours: the sand (dry, wet) by the depth; the water (a glossy film, the sea)
    float iz = 1.0f / cam_z;
    for (int d = 0; d < 24; d++) {
        float m = d * iz;
        float k = clampf(m / 0.6f, 0, 1);
        col3 dry = d == 0 ? C3(252, 236, 200) : mix3(C3(240, 214, 168), C3(206, 172, 128), k);
        col3 wet = d == 0 ? C3(255, 220, 180) : mix3(C3(176, 136, 102), dry, clampf(m / 0.22f, 0, 1));
        for (int w = 0; w < 4; w++) {
            s_md_sand[w][d] = dcol(mix3(dry, wet, w / 3.0f));
        }
        col3 wa = d == 0 ? C3(255, 246, 226) : mix3(C3(110, 170, 196), C3(46, 100, 140), clampf(m / 0.9f, 0, 1));
        s_md_wat[d] = dcol(wa);
    }
    s_md_film[0] = dcol(C3(255, 196, 150));  // (the thin film: the sky of the evening in it)
    s_md_film[1] = dcol(C3(255, 236, 196));  // (near the sun: a bright path)
    float sunx = far_sx(s_pal->sun_fx);
    for (int y = miny; y < SH; y++) {
        int xa = s_x0[y], xb = s_x1[y], pr = y & 1;
        uint16_t *p = &g_fb[y * SW];
        for (int x = xa; x < xb; x++) {
            int ws = s_mwat[x], ss = s_msand[x];
            if (y < ws) {
                continue;
            }
            if (y < ss) {
                int d = y - ws;
                bool thin = ss - ws <= 3;
                p[x] = thin ? s_md_film[fabsf(x - sunx) < 40 + 18 * fsin(x * 0.3f + s_t * 3)].c[pr][x & 1]
                            : s_md_wat[d < 24 ? d : 23].c[pr][x & 1];
                continue;
            }
            int d = y - ss;
            int wet = s_mwetc[x];
            p[x] = s_md_sand[wet][d < 24 ? d : 23].c[pr][x & 1];
        }
    }
    prof_mark(PR_NEAR);
    // the foam at the front of the wash and on the wave
    {
        float fy = m_sand(xf);
        // the lace of foam at the front of the wash
        for (int k = 0; k < 14; k++) {
            float u = k * 0.17f + 0.06f * fsin(s_t * 4 + k * 1.7f);
            wdisc(xf + u, m_sand(xf + u) + 0.012f, 0.03f + 0.02f * ((k * 7) % 3) / 2.0f, rgb(255, 255, 255),
                  200 - k * 9);
        }
        (void)fy;
        float wx = m_wave_x(), h = m_wave_h();
        if (h > 0.3f) {
            float top = m_sand(wx) + h;
            for (int k = 0; k < 5; k++) {
                wdisc(wx - 0.2f + k * 0.12f, top - k * 0.05f, 0.07f, rgb(250, 254, 255), 210);
            }
            if (m_tau() > SW_T - 0.5f) {
                // the break: the crest falls
                float t = (m_tau() - (SW_T - 0.5f)) / 0.5f;
                wcap(wx, top, wx - 0.9f * t, top - h * t, 0.12f, 0.08f, rgb(200, 240, 245), 230);
            }
        }
    }
    // the rider
    pose_t P = {0};
    P.gear = GEAR_SKIM;
    if (m_mode == SM_FALL) {
        rag_draw();
    } else if (m_mode == SM_RUN || m_mode == SM_WAIT || m_mode == SM_READY) {
        if (m_mode == SM_READY) {
            P.arms = ARMS_OUT;
            P.crouch = 0.15f + 0.15f * (0.5f + 0.5f * fsin(s_t * 6));
            P.wave = 0.2f;
            place_t pl = {m_x, m_sand(m_x), 0, 1};
            v2 h = V2(m_x - 0.35f, m_sand(m_x) + 0.55f);
            wcap(h.x, h.y - 0.5f, h.x - 0.1f, h.y + 0.5f, 0.05f, 0.05f, s_look.board, 256);
            rider_draw(&P, pl, false);
        } else if (m_mode == SM_RUN) {
            P.arms = ARMS_RUN;
            P.phase = m_x * 2.1f;
            P.lean = 0.5f;
            place_t pl = {m_x, m_sand(m_x), 0, 1};
            // the board under the arm
            v2 h = V2(m_x + 0.15f, m_sand(m_x) + 1.05f);
            wcap(h.x - 0.55f, h.y - 0.05f, h.x + 0.55f, h.y + 0.05f, 0.05f, 0.05f, s_look.board, 256);
            rider_draw(&P, pl, false);
        }
    } else if (m_mode == SM_AIR) {
        P.arms = ARMS_OUT;
        P.crouch = 0.3f;
        P.tuck = A.tuck;
        P.whip = A.whip;
        P.wave = 0.7f;
        s_pl.ang = A.ang;
        s_pl.side = 1;
        s_pl.x = 0;
        s_pl.y = 0;
        pose_make(&P);
        v2 cb = pose_com();
        float cs = fcos(A.ang), sn = fsin(A.ang);
        place_t pl = {A.x - (cb.x * cs - cb.y * sn), A.y - (cb.x * sn + cb.y * cs), A.ang, 1};
        rider_draw(&P, pl, true);
    } else {
        P.arms = ARMS_OUT;
        P.crouch = 0.35f + 0.25f * hold_now();
        P.wave = 0.5f + 0.5f * fsin(s_t * 3);
        float side = m_mode == SM_OUT ? -1 : 1;
        float e0 = m_surface(m_x - 0.2f), e1 = m_surface(m_x + 0.2f);
        place_t pl = {m_x, m_surface(m_x), fatan2(e1 - e0, 0.4f), side};
        rider_draw(&P, pl, true);
    }
    parts_draw();
    prof_mark(PR_FG);
}
