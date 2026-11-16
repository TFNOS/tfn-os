// SUMMER CUP: THE BOWL (scooter), included by app_summer.c. A round concrete bowl (radius BR) in a park on the cliff
// over the sea. The world: x right, y up (metres); the bowl centre at (0, BR), the bottom at (0, 0), the lips at
// (+-BR, BR), the deck at y = BR.
// THE CORE RULE (the pump): the wheels roll on the circle with the speed b_v. The rider stands up (a release) or
// crouches (a hold); the legs then do work against the push of the bowl N = g cos(phi) + v^2 / r: to stand up at the
// bottom (a big N) adds much speed; to crouch high on the wall (a small N) costs little (the legs take back only 30 %
// of it: a soft crouch). So a release through the bottom pumps, and each pump takes the rider higher, up over the
// coping into the air.

#define BR     4.0f
#define B_PUMP 1.0f
enum {
    BM_DROP,
    BM_RIDE,
    BM_AIR,
    BM_FALL
};
static float b_phi, b_v, b_crouch, b_h, b_side_vis = 1, b_x, b_mode_t, b_glow;
static int b_mode, b_airs, b_falls;
static float b_ai_off;  // the timing error of the AI for this pass (s)
static float b_ai_rel;  // the AI: the error of the release in the air (s)
static bool b_ai_flip, b_ai_whip;

static float b_hof(float c)
{
    return 0.95f - 0.40f * c;
}

static bool b_ground(float *x, float *y, float *px, float *py, float dt)
{
    (void)dt;
    float dx = *x, dy = *y - BR;
    // the deck (outside the bowl, at the height BR)
    if (fabsf(dx) > BR) {
        if (*y < BR) {
            *y = BR;
            *px = *x - (*x - *px) * 0.6f;
            *py = *y + (*py > *y ? 0 : 0);
            return true;
        }
        return false;
    }
    float d2 = dx * dx + dy * dy;
    if (*y > BR || d2 < (BR - 0.02f) * (BR - 0.02f)) {
        return false;
    }
    float d = fsq(d2), k = (BR - 0.02f) / d;
    *x = dx * k;
    *y = BR + dy * k;
    // the friction: the speed along the surface falls (the body slides on the concrete)
    float vx = *x - *px, vy = *y - *py, nx = -dx / d, ny = -dy / d;
    float vn = vx * nx + vy * ny, tx = vx - vn * nx, ty = vy - vn * ny;
    if (vn < 0) {
        vn = -vn * 0.15f;
    }
    *px = *x - (tx * 0.82f + vn * nx);
    *py = *y - (ty * 0.82f + vn * ny);
    return true;
}

static void bowl_start(void)
{
    b_mode = BM_DROP;
    b_mode_t = 0;
    b_x = -BR - 1.4f;
    b_v = 0;
    b_phi = -(float)M_PI / 2;
    b_crouch = 0;
    b_h = b_hof(0);
    b_side_vis = 1;
    b_airs = 0;
    b_falls = 0;
    b_glow = 0;
    s_ev_len = 50;
    s_pal = &PAL_BOWL;
    look_set(GEAR_SCOOTER);
    s_rd_ground = b_ground;
    s_ctx = 0;
    s_cty = BR * 0.85f;
    s_ctz = s_zb * 0.92f;
}

// the wheels on the circle
static inline v2 b_wheel(float phi)
{
    return V2(BR * fsin(phi), BR - BR * fcos(phi));
}

static void b_takeoff(void)
{
    float r = BR - b_h;
    float side = b_phi > 0 ? 1 : -1;
    float vx = 0, vy = fabsf(b_v) * r / BR;
    float x = side * r, y = BR;
    float back = side > 0 ? 1 : -1;
    air_start(x, y, vx, vy, b_phi, b_v / BR * 0.15f, back);
    b_mode = BM_AIR;
    b_mode_t = 0;
    b_airs++;
    s_ai_rand = frnd(0, 1);
    b_ai_flip = vy * vy / (2 * GRAV) > 1.0f && s_ai_rand < 0.85f;
    b_ai_whip = vy * vy / (2 * GRAV) > 1.6f && s_ai_rand > 0.35f;
    b_ai_rel = frnd(-1, 1) * s_ai_err;
    fx_rate(FX_GRIND, 40, 1.1f);
}

static void b_fall(float vx, float vy, float w)
{
    pose_t P = {0};
    P.crouch = b_crouch;
    P.tuck = A.tuck;
    P.arms = ARMS_BAR;
    P.gear = GEAR_SCOOTER;
    P.whip = A.whip;
    place_t pl;
    if (b_mode == BM_AIR) {
        s_pl.ang = A.ang;
        s_pl.side = b_side_vis >= 0 ? 1 : -1;
        s_pl.x = 0;
        s_pl.y = 0;
        pose_make(&P);
        v2 cb = pose_com();
        float cs = fcos(A.ang), sn = fsin(A.ang), bx = cb.x * s_pl.side;
        pl.x = A.x - (bx * cs - cb.y * sn);
        pl.y = A.y - (bx * sn + cb.y * cs);
        pl.ang = A.ang;
    } else {
        v2 wpt = b_wheel(b_phi);
        pl.x = wpt.x;
        pl.y = wpt.y;
        pl.ang = b_phi;
    }
    pl.side = b_side_vis >= 0 ? 1 : -1;
    rag_start(&P, pl, vx, vy, w, 1.0f / 240);
    b_mode = BM_FALL;
    b_mode_t = 0;
    b_falls++;
    A.on = false;
    shake(14);
    slowmo(0.5f, 0.5f);
    fx(FX_THUD, 85);
    fx_rate(FX_CLANK, 70, 0.8f);
    for (int i = 0; i < 14; i++) {
        part_add(PK_DUST, pl.x, pl.y + 0.2f, frnd(-2, 2), frnd(0.2f, 1.5f), frnd(0.4f, 0.9f), frnd(0.06f, 0.14f));
    }
}

static void b_land(void)
{
    float side = A.x > 0 ? 1 : -1;
    float lip = side * (float)M_PI / 2;
    float vdown = fabsf(A.vy);
    int res = air_land(lip);
    float height = A.ymax - BR;
    air_score(res, 120 + height * 380, "TAILWHIP", "טייל-וויפ");
#ifdef SC_DEBUG
    printf("bowl air: h %.2f m, rot %.0f deg, err %.0f deg, res %d, whips %d\n", height,
           (A.ang - A.ang0) * A.back * 57.3f, s_land_err * 57.3f, res, A.whips);
#endif
    if (res == LAND_FALL) {
        b_phi = lip;
        b_fall(0, A.vy, A.w);
        return;
    }
    b_mode = BM_RIDE;
    b_mode_t = 0;
    b_phi = lip - side * 0.01f;
    b_v = -side * vdown * (res == LAND_CLEAN ? 1.0f : 0.72f);
    b_v *= BR / (BR - b_h);
    fx(FX_CLANK, res == LAND_CLEAN ? 70 : 90);
    if (res == LAND_SKETCHY) {
        shake(6);
    } else {
        shake(2.5f);
    }
    v2 w = b_wheel(b_phi);
    for (int i = 0; i < 8; i++) {
        part_add(PK_SPARK, w.x, w.y, -side * frnd(0.5f, 2), frnd(-1, 1), frnd(0.15f, 0.35f), 0.03f);
    }
}

static void bowl_step(float dt)
{
    b_mode_t += dt;
    if (b_mode == BM_DROP) {
        // the roll on the deck to the lip, then the drop in (the speed comes from the height of the bowl)
        b_x += 1.7f * dt;
        if (b_x >= -BR) {
            b_mode = BM_RIDE;
            b_phi = -(float)M_PI / 2 + 0.02f;
            b_v = 1.2f;
            fx(FX_GRIND, 35);
        }
        b_crouch += ((hold_now() ? 1.0f : 0.3f) - b_crouch) * fminf(1, dt * 6);
        s_swipe = 0;
    } else if (b_mode == BM_RIDE) {
        // (the steps of 1/240 s for the pump and the circle)
        int n = (int)(dt * 240) + 1;
        float h = dt / n;
        for (int i = 0; i < n; i++) {
            float aim = hold_now() ? 1 : 0;
            float c0 = b_crouch;
            b_crouch += (aim - b_crouch) * fminf(1, h * 9);
            float h0 = b_hof(c0), h1 = b_hof(b_crouch), dh = h1 - h0;
            b_h = h1;
            float r = BR - b_h;
            float N = GRAV * fcos(b_phi) + b_v * b_v / r;
            if (N < 0) {
                N = 0;
            }
            float e = b_v * b_v * 0.5f;
            if (dh > 0) {
                e += N * dh * B_PUMP;
            } else {
                e += N * dh * 0.3f * B_PUMP;
            }
            if (e < 0) {
                e = 0;
            }
            float sp = fsq(2 * e);
            b_v = b_v >= 0 ? sp : -sp;
            b_v += -GRAV * fsin(b_phi) * h;
            float fr = (0.14f + 0.0065f * b_v * b_v) * h;
            if (fabsf(b_v) <= fr) {
                b_v = 0;
            } else {
                b_v -= b_v > 0 ? fr : -fr;
            }
            b_phi += b_v / BR * h;
            if (fabsf(b_phi) >= (float)M_PI / 2) {
                float side = b_phi > 0 ? 1 : -1;
                if (b_v * side > 1.0f) {
                    b_phi = side * (float)M_PI / 2;
                    b_takeoff();
                    break;
                }
                b_phi = side * ((float)M_PI / 2 - 0.001f);
                b_v = -side * 0.15f;  // (a stall at the coping: back down)
                fx_rate(FX_GRIND, 30, 0.8f);
            }
        }
        // the side of the rider follows the way of the wheels (a short turn at the top of a wall)
        if (fabsf(b_v) > 0.4f) {
            float aim = b_v > 0 ? 1 : -1;
            b_side_vis += (aim - b_side_vis) * fminf(1, dt * 7);
        }
        // the cue of the pump zone: it glows when the rider comes down to the bottom
        float near = 1 - clampf(fabsf(b_phi) / 0.9f, 0, 1);
        b_glow += (near - b_glow) * fminf(1, dt * 6);
        s_swipe = 0;
        // the dust of the wheels and the sound of the roll
        drone(0, 40 + fabsf(b_v) * 9, (int)clampf(fabsf(b_v) * 3.2f, 0, 26));
    } else if (b_mode == BM_AIR) {
        drone(0, 40, 0);
        air_step(dt, s_lean);
        A.vy -= GRAV * dt;
        A.y += A.vy * dt;
        A.x += A.vx * dt;
        if (A.vy < 0 && A.y <= BR) {
            b_land();
        } else {
            float hgt = A.ymax - BR;
            if (hgt > 1.7f && fabsf(A.vy) < 2.6f) {
                slowmo(0.42f, 0.25f);
            }
        }
    } else if (b_mode == BM_FALL) {
        drone(0, 40, 0);
        rag_step(dt, GRAV);
        s_swipe = 0;
        if (b_mode_t > 2.6f && s_ev_t < s_ev_len - 1) {
            s_rag.on = false;
            b_mode = BM_DROP;
            b_mode_t = 0;
            b_x = -BR - 1.4f;
            b_side_vis = 1;
            b_crouch = 0;
            memset(&A, 0, sizeof A);
        }
    }
    // the camera: a medium shot; in the air it goes near and up with the rider
    if (b_mode == BM_AIR) {
        float hgt = clampf((A.ymax - BR) / 3.0f, 0, 1);
        s_ctx = A.x * 0.62f;
        s_cty = lerpf(BR * 0.9f, A.y + 0.3f, 0.78f);
        s_ctz = s_zb * (1.18f + 0.22f * hgt);
    } else if (b_mode == BM_FALL) {
        v2 c = rag_com();
        s_ctx = c.x * 0.6f;
        s_cty = lerpf(BR * 0.7f, c.y + 0.6f, 0.6f);
        s_ctz = s_zb * (b_mode_t < 0.8f ? 1.35f : 1.05f);
    } else if (b_mode == BM_DROP) {
        s_ctx = b_x * 0.5f;
        s_cty = BR * 0.9f;
        s_ctz = s_zb * 1.0f;
    } else {
        v2 w = b_wheel(b_phi);
        s_ctx = w.x * 0.45f;
        s_cty = lerpf(BR * 0.78f, w.y + 0.9f, 0.4f);
        s_ctz = s_zb * (1.0f + 0.08f * clampf(fabsf(b_v) / 9, 0, 1));
    }
}

static bool bowl_over(void)
{
    return s_ev_t >= s_ev_len && b_mode != BM_AIR && !(b_mode == BM_FALL && b_mode_t < 1.6f);
}

// the AI: pump through the bottom; in the air a flip, a whip, and the release in time
static void bowl_ai(ai_t *o)
{
    if (b_mode == BM_RIDE) {
        float w = b_v / BR;
        float phi = b_phi + w * b_ai_off;                       // (a person acts a little early or late)
        bool up = phi * b_v > 0;                                // (it goes up a wall)
        o->hold = up ? fabsf(phi) > 0.5f : fabsf(phi) > 0.12f;  // (crouch high on the wall, stand at the bottom)
        if (fabsf(b_phi) < 0.05f) {
            b_ai_off = frnd(-1, 1) * s_ai_err;
        }
    } else if (b_mode == BM_AIR) {
        float tl = (A.vy + fsq(A.vy * A.vy + 2 * GRAV * fmaxf(0, A.y - BR))) / GRAV;  // the time to the landing
        float rot = (A.ang - A.ang0) * A.back;
        if (b_ai_flip && A.throws == 0 && A.t > 0.04f) {
            o->swipe = 1;
        }
        if (b_ai_whip && A.whips == 0 && fabsf(A.whip_v) < 0.1f && A.t > 0.12f && tl > 0.55f) {
            o->swipe = 3;
        }
        if (b_ai_flip) {
            float wo = fabsf(A.L);  // (the open spin)
            float aim = 2 * (float)M_PI;
            o->hold = rot + wo * (tl + b_ai_rel) < aim;
            if (A.throws == 0) {
                o->hold = true;
            }
        }
        // the small lean to fix the landing (only the careful AI)
        if (s_ai_err < 0.05f) {
            float side = A.x > 0 ? 1 : -1;
            float pred = A.ang + A.w * tl;
            float err = wrap_pi(pred - side * (float)M_PI / 2);
            o->lean = clampf(-err * 1.5f, -1, 1);
        }
    }
}

// ---------------------------------------------------------------- the drawing
static uint8_t s_grout[660];

static void bowl_render(void)
{
    float decky = SY(BR);
    int dk = clampi((int)ceilf(decky), 0, SH);
    for (int x = 0; x < SW; x++) {
        s_ntop[x] = (int16_t)dk;
    }
    prof_start();
    far_setup();
    far_raster();
    prof_mark(PR_FAR);
    far_props();
    // the far things on the deck: a rail, the palms (with the far clip: only above the deck line)
    s_cf = true;
    {
        uint16_t rc = rgb(120, 130, 140);
        float ry = BR + 0.9f;
        wcap(-12, ry, 12, ry, 0.03f, 0.03f, rc, 220);
        wcap(-12, ry - 0.45f, 12, ry - 0.45f, 0.02f, 0.02f, rc, 160);
        for (float x = -12; x <= 12; x += 1.5f) {
            wcap(x, BR, x, ry, 0.025f, 0.025f, rc, 200);
        }
        palm(-6.8f, BR, 5.2f, 0.12f, fsin(s_t * 0.9f));
        palm(7.3f, BR, 6.0f, -0.18f, fsin(s_t * 0.8f + 1));
        palm(-10.5f, BR, 4.4f, 0.25f, fsin(s_t * 1.1f + 2));
    }
    s_cf = false;
    prof_mark(PR_PROPS);

    // the near rows: the concrete cut, the far wall of the bowl (light at the left, shade at the right), the pool tiles
    for (int x = 0; x < SW; x++) {
        float u = (WXs(x + 0.5f) + 20) * 6.0f;
        s_grout[x] = (uint8_t)((u - (int)u) < 0.09f);
    }
    // the colour of the concrete in each row (light at the deck, darker down)
    static dc_t cut_row[MAXH];
    for (int y = dk; y < SH && y < MAXH; y++) {
        float wy = WYs(y + 0.5f);
        int dd = y - (int)decky;
        col3 c = dd < 3 ? C3(240, 236, 226)
                        : mix3(C3(212, 206, 194), C3(168, 160, 152), clampf((BR - wy) / (BR + 1.5f), 0, 1));
        cut_row[y] = dcol(c);
    }
    static dc_t seg[24];
    int seg_key = -1;
    for (int y = dk; y < SH; y++) {
        float wy = WYs(y + 0.5f);
        int xa = s_x0[y], xb = s_x1[y];
        uint16_t *p = &g_fb[y * SW];
        int pr = y & 1;
        int xl = SW, xr = -1;  // the chord of the inside
        if (wy > 0 && wy < BR) {
            float dyc = wy - BR, hw = fsq(BR * BR - dyc * dyc);
            xl = (int)ceilf(SX(-hw) - 0.5f);
            xr = (int)floorf(SX(hw) - 0.5f);
        }
        const uint16_t *cc = cut_row[y < MAXH ? y : MAXH - 1].c[pr];
        if (xl > xr) {
            fill2(p, xa, xb, cc);
            continue;
        }
        fill2(p, xa, clampi(xl, xa, xb), cc);
        fill2(p, clampi(xr + 1, xa, xb), xb, cc);
        // the far wall: 24 steps from the light side to the shade side (made again each third row or at a change
        // of the band)
        float vy = clampf(wy / BR, 0, 1);
        bool tile = wy > BR - 0.30f && wy < BR - 0.07f, lipsh = wy >= BR - 0.07f && wy < BR - 0.025f,
             far_cop = wy >= BR - 0.025f;
        int key = (y / 3) * 4 + (tile ? 1 : 0) + (lipsh ? 2 : 0) + (far_cop ? 3 : 0);
        for (int k = 0; k < 24 && key != seg_key; k++) {
            float t = k / 23.0f;
            col3 c;
            if (tile) {
                c = mix3(C3(70, 170, 226), C3(40, 120, 196), t * 0.7f + (BR - 0.07f - wy) * 1.2f);
            } else if (lipsh) {
                c = mix3(C3(120, 120, 128), C3(90, 92, 104), t);
            } else if (far_cop) {
                c = mix3(C3(236, 236, 240), C3(170, 172, 182), t);
            } else {
                col3 lit = mix3(C3(196, 186, 172), C3(238, 232, 220), vy),
                     shd = mix3(C3(142, 140, 146), C3(184, 182, 188), vy);
                c = mix3(lit, shd, smooth01(t));
            }
            seg[k] = dcol(c);
        }
        seg_key = key;
        int x0 = clampi(xl, xa, xb), x1 = clampi(xr + 1, xa, xb);
        if (tile) {
            uint16_t grout = pr ? rgb(226, 232, 236) : rgb(220, 226, 232);
            int span = xr - xl + 1;
            for (int x = x0; x < x1; x++) {
                int k = (x - xl) * 24 / span;
                p[x] = s_grout[x] ? grout : seg[k].c[pr][x & 1];
            }
        } else {
            int span = xr - xl + 1;
            for (int k = 0; k < 24; k++) {
                int s0 = xl + span * k / 24, s1 = xl + span * (k + 1) / 24;
                fill2(p, clampi(s0, x0, x1), clampi(s1, x0, x1), seg[k].c[pr]);
            }
        }
    }
    // the edge of the cut: a light line on the circle of the bowl
    for (int k = 0; k < 24; k++) {
        float a0 = -1.5708f + k * 3.1416f / 24, a1 = a0 + 3.1416f / 24;
        wcap(BR * fsin(a0), BR - BR * fcos(a0), BR * fsin(a1), BR - BR * fcos(a1), 0.025f, 0.025f, rgb(246, 242, 232),
             256);
    }
    // the painted sunset on the far wall: a sun with the stripes of the old beach posters, and two palms
    {
        float cx = SX(0.35f), cy = SY(2.05f), r = 1.05f * cam_z;
        int y0 = clampi((int)(cy - r), 0, SH), y1 = clampi((int)(cy + r), 0, SH);
        for (int y = y0; y < y1; y++) {
            float dy = y + 0.5f - cy, h2 = r * r - dy * dy;
            if (h2 <= 0) {
                continue;
            }
            float t = (dy + r) / (2 * r);
            if (t > 0.55f) {  // (the stripes in the lower half)
                float sp = (t - 0.55f) * 18;
                if (sp - (int)sp > 0.55f + (t - 0.55f)) {
                    continue;
                }
            }
            float hw = fsq(h2);
            col3 c = mix3(C3(255, 214, 70), C3(250, 80, 120), t);
            hspan((int)(cx - hw), (int)(cx + hw), y, c16(c), 150);
        }
        uint16_t pc = rgb(70, 50, 90);
        for (int k = 0; k < 2; k++) {
            float px = k ? 1.45f : -0.85f, ph = k ? 1.5f : 1.9f, lean = k ? -0.18f : 0.22f;
            float bx = px, by = 0.95f;
            wcap(bx, by, bx + lean * ph, by + ph, 0.05f, 0.035f, pc, 170);
            float tx = bx + lean * ph, ty = by + ph;
            for (int f = 0; f < 5; f++) {
                float a = -2.5f + f * 1.25f;
                wcap(tx, ty, tx + fcos(a) * 0.55f, ty + fsin(a) * 0.25f + 0.1f, 0.05f, 0.02f, pc, 170);
            }
        }
    }
    prof_mark(PR_NEAR);
    // the coping: a round steel pipe at each lip
    for (int s = -1; s <= 1; s += 2) {
        wdisc(s * BR, BR - 0.04f, 0.075f, rgb(150, 156, 166), 256);
        wdisc(s * BR - s * 0.015f, BR - 0.02f, 0.035f, rgb(246, 248, 252), 220);
    }
    // the cue of the pump zone: a soft warm band at the bottom (it glows when the rider comes down)
    if (b_mode == BM_RIDE || b_mode == BM_DROP) {
        int a = (int)(50 + 150 * b_glow);
        col3 gc = C3(255, 214, 120);
        for (int k = -4; k < 4; k++) {
            float p0 = k * 0.11f, p1 = (k + 1) * 0.11f;
            v2 q0 = V2((BR - 0.03f) * fsin(p0), BR - (BR - 0.03f) * fcos(p0)),
               q1 = V2((BR - 0.03f) * fsin(p1), BR - (BR - 0.03f) * fcos(p1));
            float edge = 1 - fabsf(k + 0.5f) / 4.5f;
            wcap(q0.x, q0.y, q1.x, q1.y, 0.025f, 0.025f, c16(gc), (int)(a * edge));
        }
        if (b_glow > 0.3f) {
            glow(SX(0), SY(0.05f), 0.9f * cam_z, gc, (int)(70 * b_glow));
        }
    }
    // the rider
    pose_t P = {0};
    P.gear = GEAR_SCOOTER;
    P.arms = ARMS_BAR;
    if (b_mode == BM_FALL) {
        rag_draw();
    } else if (b_mode == BM_AIR) {
        P.crouch = 0.25f;
        P.tuck = A.tuck;
        P.whip = A.whip;
        float side = b_side_vis >= 0 ? 1 : -1;
        s_pl.ang = A.ang;
        s_pl.side = side;
        s_pl.x = 0;
        s_pl.y = 0;
        pose_make(&P);
        v2 cb = pose_com();
        float cs = fcos(A.ang), sn = fsin(A.ang), bx = cb.x * side;
        place_t pl = {A.x - (bx * cs - cb.y * sn), A.y - (bx * sn + cb.y * cs), A.ang, side};
        rider_draw(&P, pl, true);
    } else {
        P.crouch = b_crouch;
        float side = b_side_vis >= 0 ? 1 : -1;
        place_t pl;
        if (b_mode == BM_DROP) {
            pl.x = b_x;
            pl.y = BR;
            pl.ang = 0;
        } else {
            v2 w = b_wheel(b_phi);
            pl.x = w.x;
            pl.y = w.y;
            pl.ang = b_phi;
        }
        pl.side = side;
        // (the turn at the top: the body gets narrow while the side changes)
        if (fabsf(b_side_vis) < 0.98f) {
            P.lean = 0;
        }
        // the shadow on the concrete
        float cs = fcos(pl.ang), sn = fsin(pl.ang);
        wcap(pl.x - cs * 0.42f, pl.y - sn * 0.42f + 0.01f, pl.x + cs * 0.42f, pl.y + sn * 0.42f + 0.01f, 0.05f, 0.05f,
             rgb(60, 56, 60), 90);
        rider_draw(&P, pl, true);
    }
    parts_draw();
    prof_mark(PR_FG);
}
