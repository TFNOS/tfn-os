// SUMMER CUP: THE PEAK (surf), included by app_summer.c. The view is the side of the wave (a cut-away): the wave goes
// to the right (to the shore), the world frame moves with its crest (x = 0 at the crest, y = 0 the sea level).
// THE CORE RULE (the slice): the wave breaks along its length. The picture is the cut of the wave where the surfer is,
// and the speed u of the surfer along the wave chooses the stage p of the break in that cut:
//   dp/dt = (C_PEEL - u) * P_RATE.   p < 0.25: the shoulder (a soft bump, a weak push); 0.35 .. 0.85: the pocket (a
//   steep face, then the lip throws over: the barrel); p >= 0.86: the lip lands, then foam (a wipeout).
// The speed comes from the wave: the moving face lifts the board (free height); a drop down the face turns height into
// speed (u^2 grows by 2 g dh). The lean (tilt right) takes the board down the face; no lean: the wave lifts it.
// THE TUBE: in the barrel the surfer must crouch (hold); tall = the lip hits the head.

#define HMAX   2.4f
#define C_PEEL 6.4f
#define P_RATE 0.07f
#define W_LEN  24.0f
enum {
    PM_TAKEOFF,
    PM_RIDE,
    PM_AIR,
    PM_FALL,
    PM_WAIT
};
static float k_p, k_y, k_u, k_vf, k_crouch, k_t, k_x, k_tk_x;
static float k_ride_t, k_lip_t,
        k_sec_ph;  // (k_sec_ph: the phase of the sections of this wave)              // (k_lip_t: the time at the top under a lip that throws)                       // the time of the ride on this wave (the airs do not stop it)
static float k_H, k_Wf, k_k;  // the wave now: the height, the width of the face, the curve of the face
static float k_tube_t, k_tube_best, k_stand_t, k_carve_lo, k_carve_hi;
static int k_mode, k_wave, k_carve_dir;
static float k_mode_t;
static bool k_in_tube, k_closed;
static float k_ai_off, k_ai_tk;
static float k_fu[65];  // (1 - u)^k for u = 0..1 (the face curve, made each frame)

static void k_shape(float p)
{
    k_H = HMAX * (0.5f + 0.5f * smooth01(p / 0.45f));
    k_Wf = lerpf(6.0f, 2.2f, smooth01(p / 0.6f));
    k_k = lerpf(1.3f, 2.3f, smooth01(p / 0.6f));
    for (int i = 0; i <= 64; i++) {
        float u = i / 64.0f;
        k_fu[i] = u >= 1 ? 0 : powf(1 - u, k_k);
    }
}
static inline float k_face(float u)  // (1 - u)^k from the table
{
    if (u <= 0) {
        return 1;
    }
    if (u >= 1) {
        return 0;
    }
    float f = u * 64;
    int i = (int)f;
    return k_fu[i] + (k_fu[i + 1] - k_fu[i]) * (f - i);
}
// the surface of the water at x (the wave frame)
static float k_eta(float x)
{
    if (x < 0) {
        float b = x / 9.0f;
        if (b < -1) {
            return 0;
        }
        float q = 1 - b * b;
        return k_H * q * q;
    }
    float u = x / k_Wf;
    if (u >= 1) {
        return -0.12f * k_H * (1 - clampf((u - 1) * 0.5f, 0, 1));
    }
    return k_H * k_face(u) - 0.12f * k_H * smooth01(u * 1.4f - 0.4f);
}
// the place on the face for a height y: x, and the slope angle (rad, < 0: the face goes down to the right)
static float k_xof(float y)
{
    float t = clampf(y / k_H, 0.001f, 0.999f);
    // invert (1 - u)^k = t with a few steps of bisection on the table (no powf in the frame)
    float lo = 0, hi = 1;
    for (int i = 0; i < 16; i++) {
        float m = (lo + hi) * 0.5f;
        if (k_face(m) - 0.12f * smooth01(m * 1.4f - 0.4f) > t) {
            lo = m;
        } else {
            hi = m;
        }
    }
    return (lo + hi) * 0.5f * k_Wf;
}
static float k_slope(float x)
{
    float e0 = k_eta(x - 0.04f), e1 = k_eta(x + 0.04f);
    return fatan2(e1 - e0, 0.08f);
}
// the barrel: a circle in front of the crest, and the lip that throws over it
static float k_q(void)
{
    return clampf((k_p - 0.42f) / 0.42f, 0, 1);
}
static v2 k_tube_c(void)
{
    return V2(k_Wf * 0.40f, k_H * 0.52f);
}
static float k_tube_r(void)
{
    return k_H * 0.50f;
}

static bool k_ground(float *x, float *y, float *px, float *py, float dt)
{
    // the water: a soft push up and a drag (the body floats and tumbles in the foam)
    float s = k_eta(*x);
    if (*y >= s) {
        return false;
    }
    float d = s - *y;
    float vx = *x - *px, vy = *y - *py;
    vx *= 0.94f;
    vy *= 0.90f;
    vy += d * 6.0f * dt * dt * 60;
    vx += 1.6f * dt;  // (the foam pushes to the shore)
    *px = *x - vx;
    *py = *y - vy;
    if (d > 0.9f) {
        *y = s - 0.9f;
    }
    return true;
}

static void k_new_wave(void)
{
    k_mode = PM_TAKEOFF;
    k_mode_t = 0;
    k_tk_x = 9.0f;
    k_p = 0.22f;
    k_u = 0;
    k_vf = 0;
    k_crouch = 0;
    k_tube_t = 0;
    k_ride_t = 0;
    k_sec_ph = frnd(0, 6.28f);
    k_lip_t = 0;
    k_stand_t = 0;
    k_in_tube = false;
    k_closed = false;
    k_carve_dir = 0;
    k_carve_lo = 9;
    k_carve_hi = -9;
    k_ai_tk = 0.55f + frnd(-1, 1) * s_ai_err * 1.6f;
    s_rag.on = false;
    memset(&A, 0, sizeof A);
}
static void peak_start(void)
{
    k_wave = 0;
    k_tube_best = 0;
    s_ev_len = 80;
    s_pal = &PAL_SURF;
    look_set(GEAR_SURF);
    s_rd_ground = k_ground;
    k_new_wave();
    k_shape(k_p);
    s_ctx = 2;
    s_cty = 1.4f;
    s_ctz = s_zb * 0.8f;
}

static void k_fall(float vx, float vy, float w, const char *why_en, const char *why_he)
{
    pose_t P = {0};
    P.gear = GEAR_SURF;
    P.arms = ARMS_OUT;
    P.crouch = k_crouch;
    P.tuck = A.tuck;
    place_t pl;
    if (k_mode == PM_AIR) {
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
        pl.x = k_x;
        pl.y = k_y;
        pl.ang = k_slope(k_x);
    }
    pl.side = 1;
    rag_start(&P, pl, vx, vy, w, 1.0f / 240);
    k_mode = PM_FALL;
    k_mode_t = 0;
    A.on = false;
    shake(16);
    slowmo(0.45f, 0.6f);
    fx(FX_SPLASH, 95);
    if (why_en) {
        trick_say(TR(why_en, why_he), TR("WIPEOUT", "נפילה"), 0, rgb(255, 120, 110));
    }
    for (int i = 0; i < 40; i++) {
        part_add(PK_SPRAY, pl.x + frnd(-0.5f, 0.5f), pl.y + frnd(0, 0.6f), frnd(-3, 3), frnd(1, 6), frnd(0.5f, 1.2f),
                 frnd(0.05f, 0.14f));
    }
}

static void k_end_ride(void)
{
    k_mode = PM_WAIT;
    k_mode_t = 0;
    k_wave++;
}

static void peak_step(float dt)
{
    k_mode_t += dt;
    if (k_mode == PM_RIDE || k_mode == PM_AIR) {
        k_ride_t += dt;
    }
    if (k_mode == PM_TAKEOFF) {
        // the wave comes to the board: the crest gets near (x of the board in the wave frame falls)
        // (the wave pushes the paddling board: on the face the crest comes nearer more slowly)
        float hy0 = clampf(k_eta(k_tk_x) / k_H, 0, 1);
        k_tk_x -= 3.6f * (1 - 0.78f * smooth01(hy0 / 0.5f)) * dt;
        k_p = clampf(0.22f + (9 - k_tk_x) * 0.03f, 0.22f, 0.46f);
        k_shape(k_p);
        float y = k_eta(k_tk_x);
        float hy = y / k_H;
        k_x = k_tk_x;
        k_y = y;
        if (s_swipe == 1) {
            s_swipe = 0;
#ifdef SC_DEBUG
            printf("  take-off swipe: hy %.2f x %.2f p %.2f H %.2f\n", hy, k_tk_x, k_p, k_H);
#endif
            if (hy > 0.86f || k_tk_x < 0.3f) {
                k_y = y;
                k_fall(2, 1, 1.5f, "TOO LATE", "מאוחר מדי");
                fx(FX_SPLASH2, 70);
            } else if (hy >= 0.30f) {
                // the take-off: the surfer stands up, the drop gives the first speed
                k_mode = PM_RIDE;
                k_mode_t = 0;
                k_y = y;
                float good = 1 - fabsf(hy - 0.6f) / 0.3f;
                k_u = 5.6f + 1.4f * clampf(good, 0, 1);
                k_vf = -1.0f;
                k_p = 0.46f;
                trick_say(TR("UP!", "קם!"), good > 0.6f ? TR("GREAT TAKE-OFF", "המראה מצוינת") : "",
                          good > 0.6f ? 300 : 100, rgb(255, 240, 160));
                add_score(good > 0.6f ? 300 : 100);
                fx(FX_SLAP, 70);
            }
        }
        s_swipe = 0;
        if (k_tk_x < -1.0f) {
            trick_say(TR("MISSED THE WAVE", "פספסת את הגל"), "", 0, rgb(200, 230, 255));
            k_end_ride();
        }
        drone(1, 30 + k_H * 8, (int)(10 + 8 * k_H));
    } else if (k_mode == PM_RIDE) {
        float H0 = k_H;
        (void)H0;
        float aim = hold_now() ? 1 : 0;
        k_crouch += (aim - k_crouch) * fminf(1, dt * 9);
        // the stage of the break follows the speed along the wave
        // (when the lip throws, the break runs faster along the wave: a barrel lasts seconds, not the ride)
        // the sections of the point: the peel speed goes up and down along the wave (a fast section brings a
        // barrel, a slow one lets the surfer out); a barrel that lasts 5 s spits the surfer out
        float sec = 1.3f * fsin(k_ride_t * 0.75f + k_sec_ph);
        k_p += (C_PEEL + sec + 2.0f * k_q() - k_u) * P_RATE * dt;
        if (k_in_tube && k_tube_t > 5.0f) {
            k_p -= 0.25f * dt;
        }
        if (k_ride_t > W_LEN) {
            k_p += 0.6f * dt;  // (the end: the wave closes out)
        }
        if (k_ride_t > W_LEN + 3 && k_p < 0.86f) {
            k_p = 0.86f;
        }
        if (k_p < 0) {
            k_p = 0;
        }
        k_shape(k_p);
        float hy = clampf(k_y / k_H, 0, 1);
        // the lift of the wave (strong on the middle of the face, weak on the shoulder) and the lean of the surfer
        float lift = 1.6f * smooth01(k_p / 0.35f) * (0.35f + 0.65f * (1 - hy)) * (hy < 0.97f ? 1 : 0.2f);
        float lean = s_lean;
        float vl = -lean * 2.7f;
        float aimv = lift + vl;
        k_vf += (aimv - k_vf) * fminf(1, dt * 6);
        float dy = k_vf * dt;
        // the energy: the part of the move that the lean makes changes the speed (down = more, up = less)
        float lean_dy = (k_vf - lift) * dt;
        float u2 = k_u * k_u - 2 * GRAV * lean_dy * (lean_dy < 0 ? 0.92f : 1.0f);
        if (u2 < 0) {
            u2 = 0;
        }
        k_u = fsq(u2);
        float steep = clampf(-k_slope(k_x) / 1.2f, 0, 1) * (1 - smooth01((hy - 0.80f) / 0.15f));
        k_u += 0.95f * steep * smooth01((k_p - 0.15f) / 0.3f) * dt;
        // the top of the face under a lip that throws: the lip takes the board over the falls
        if (hy > 0.9f && k_q() > 0.12f) {
            k_lip_t += dt;
            if (k_lip_t > 1.3f) {
                k_fall(2.5f, -1, -4, "OVER THE FALLS", "נפלת מהשפה");
                return;
            }
        } else {
            k_lip_t = 0;
        }
        k_u -= (0.22f + 0.030f * k_u * k_u * (1 - 0.35f * k_crouch)) * dt;
        if (hy < 0.12f) {
            k_u -= 0.9f * dt;  // (the flat in front of the wave)
        }
        if (lean < 0) {
            k_u -= 1.6f * -lean * dt;  // (a lean back: the tail digs in, a brake)
        }
        if (k_u < 0) {
            k_u = 0;
        }
        k_y += dy;
        if (k_y < 0.04f * k_H) {
            k_y = 0.04f * k_H;
            if (k_vf < 0) {
                k_vf = 0;
            }
        }
        if (k_y > 0.98f * k_H) {
            k_y = 0.98f * k_H;
            if (k_vf > 0) {
                k_vf = 0;
            }
        }
        k_x = k_xof(k_y);
#ifdef SC_DEBUG
        if ((int)(k_ride_t * 2) != (int)((k_ride_t - dt) * 2) && getenv("SC_TRACE")) {
            printf("    t %.1f p %.2f hy %.2f u %.1f vf %.1f lean %.2f crouch %.1f q %.2f\n", k_ride_t, k_p, hy, k_u,
                   k_vf, s_lean, k_crouch, k_q());
        }
#endif
        // the carves: a full turn down and up on the face
        if (k_vf < -0.6f) {
            if (k_carve_dir > 0 && k_carve_hi - k_carve_lo > 0.45f * k_H) {
                add_score(150);
                trick_say(TR("CARVE", "חיתוך"), "", 150, rgb(180, 240, 255));
                fx(FX_SPLASH2, 40);
            }
            if (k_carve_dir > 0) {
                k_carve_hi = k_y;
            }
            k_carve_dir = -1;
            if (k_y < k_carve_lo) {
                k_carve_lo = k_y;
            }
        } else if (k_vf > 0.6f) {
            if (k_carve_dir < 0) {
                k_carve_lo = k_y;
            }
            k_carve_dir = 1;
            if (k_y > k_carve_hi) {
                k_carve_hi = k_y;
            }
        }
        if (k_carve_dir < 0 && k_y < k_carve_lo) {
            k_carve_lo = k_y;
        }
        if (k_carve_dir > 0 && k_y > k_carve_hi) {
            k_carve_hi = k_y;
        }
        // the points: each second in the pocket (more near the break and with speed)
        float pocket = smooth01((k_p - 0.25f) / 0.15f) * (1 - smooth01((k_p - 0.80f) / 0.06f));
        add_score(dt * 70 * pocket * (0.5f + 0.5f * clampf(k_u / 8, 0, 1)));
        // the tube: in the barrel (the lip over the board)
        float q = k_q();
        v2 tc = k_tube_c();
        float tr = k_tube_r();
        float dxx = k_x - tc.x, dyy = k_y - tc.y;
        // (the barrel: the face under a lip that is thrown over it)
        bool under = q > 0.32f && hy < 0.92f && k_x > 0.04f * k_Wf;
        (void)dxx;
        (void)dyy;
        (void)tr;
        if (under) {
            if (k_crouch > 0.55f) {
                k_stand_t = 0;
                if (!k_in_tube) {
                    k_in_tube = true;
                    k_tube_t = 0;
                    fx_rate(FX_WHOOSH, 50, 0.7f);
                }
                k_tube_t += dt;
                add_score(dt * 160);
            } else {
                k_stand_t += dt;
                if (k_stand_t > 0.35f) {
                    k_fall(1, -2, -2, "THE LIP HIT YOU", "השפה פגעה בך");
                    return;
                }
            }
        } else {
            k_stand_t = 0;
            if (k_in_tube) {
                k_in_tube = false;
                if (k_tube_t > 0.6f) {
                    int pts = (int)(450 * k_tube_t);
                    char t[32];
                    snprintf(t, sizeof t, "%s %.1f S", TR("TUBE", "צינור"), k_tube_t);
                    trick_say(t, TR("OUT CLEAN", "יצאת נקי"), pts, rgb(120, 255, 220));
                    add_score((float)pts);
                    if (k_tube_t > k_tube_best) {
                        k_tube_best = k_tube_t;
                    }
                    fx(FX_SPLASH2, 60);
                    slowmo(0.6f, 0.3f);
                }
            }
        }
        // the close: the lip lands; then foam
        if (k_p >= 0.86f) {
            if (k_ride_t > W_LEN) {
                // the end of the wave: the surfer kicks out over the back
                add_score(500);
                trick_say(TR("KICK OUT", "יציאה מהגל"), TR("RIDE COMPLETE", "גלישה שלמה"), 500, rgb(255, 240, 160));
                fx(FX_SPLASH2, 60);
                k_closed = true;
                k_end_ride();
                return;
            }
            k_fall(1.5f, -1, -3, "CAUGHT INSIDE", "הגל תפס אותך");
            k_closed = true;
            return;
        }
        // the air: a swipe up high on the face with speed
        if (s_swipe == 1 && hy > 0.62f && k_u > 5.0f && !k_in_tube) {
            float sl = k_slope(k_x);
            air_start(k_x, k_y + 0.05f, -0.4f, 3.2f + fmaxf(0, k_vf) + (k_u - 5) * 0.4f, sl, 0, -1);
            k_mode = PM_AIR;
            k_mode_t = 0;
            s_ai_rand = frnd(0, 1);
            for (int i = 0; i < 18; i++) {
                part_add(PK_SPRAY, k_x, k_y, frnd(-1, 2), frnd(1, 5), frnd(0.4f, 0.9f), frnd(0.04f, 0.1f));
            }
            fx(FX_SPLASH2, 60);
        }
        s_swipe = 0;
        // the spray of the rail in a fast turn
        if (fabsf(k_vf) > 1.2f && (rnd32() & 3) == 0) {
            part_add(PK_SPRAY, k_x - 0.4f, k_y + 0.05f, frnd(-2.5f, -0.5f), frnd(0.8f, 3.5f), frnd(0.3f, 0.7f),
                     frnd(0.03f, 0.08f));
        }
        drone(1, 30 + k_H * 8 + k_u * 2, (int)(14 + 6 * k_H + (k_in_tube ? 10 : 0)));
    } else if (k_mode == PM_AIR) {
        k_p += (C_PEEL - k_u * 0.9f) * P_RATE * dt;
        k_shape(k_p);
        air_step(dt, s_lean);
        A.vy -= GRAV * dt;
        A.y += A.vy * dt;
        A.x += A.vx * dt;
        float s = k_eta(A.x);
        if (A.vy < 0 && A.y <= s + 0.05f) {
            float target = k_slope(A.x);
            int res = air_land(target);
            float hgt = A.ymax - A.y0;
            air_score(res, 300 + hgt * 420, "BOARD SPIN", "סיבוב גלשן");
            if (res == LAND_FALL) {
                k_y = A.y;
                k_x = A.x;
                k_mode = PM_AIR;
                k_fall(0, A.vy, A.w, NULL, NULL);
                return;
            }
            k_mode = PM_RIDE;
            k_mode_t = 0;
            k_y = clampf(A.y, 0.05f * k_H, 0.95f * k_H);
            k_x = k_xof(k_y);
            k_vf = -1.5f;
            if (res == LAND_SKETCHY) {
                k_u *= 0.75f;
                shake(5);
            }
            fx(FX_SLAP, 75);
            for (int i = 0; i < 16; i++) {
                part_add(PK_SPRAY, k_x, k_y, frnd(-2, 2), frnd(1, 4), frnd(0.4f, 0.8f), frnd(0.04f, 0.1f));
            }
        } else if (A.ymax - A.y0 > 1.2f && fabsf(A.vy) < 2.2f) {
            slowmo(0.45f, 0.2f);
        }
    } else if (k_mode == PM_FALL) {
        if (k_p < 1.05f) {
            k_p += 0.25f * dt;
        }
        k_shape(fminf(k_p, 1.0f));
        rag_step(dt, GRAV);
        if ((rnd32() & 7) == 0) {
            v2 c = rag_com();
            part_add(PK_FOAM, c.x + frnd(-1, 1), c.y + frnd(-0.2f, 0.4f), frnd(0, 2), frnd(0, 1), frnd(0.6f, 1.2f),
                     frnd(0.05f, 0.13f));
        }
        if (k_mode_t > 2.6f) {
            k_end_ride();
        }
        drone(1, 34, 24);
    } else if (k_mode == PM_WAIT) {
        drone(1, 30, 10);
        if (k_mode_t > 1.6f && k_wave < 2) {
            k_new_wave();
        }
    }
    // the foam where the lip lands (the close)
    if (k_p > 0.84f && (rnd32() & 1)) {
        v2 tc = k_tube_c();
        float tr = k_tube_r();
        part_add(PK_FOAM, tc.x + frnd(-tr, tr * 1.2f), frnd(0, k_H * 0.6f), frnd(0.5f, 3), frnd(0.5f, 3),
                 frnd(0.6f, 1.4f), frnd(0.05f, 0.15f));
    }
    // the camera: the surfer near the middle; near in the barrel; wide at the take-off
    float rx = k_mode == PM_AIR ? A.x : k_mode == PM_FALL ? rag_com().x : k_x;
    float ry = k_mode == PM_AIR ? A.y : k_mode == PM_FALL ? rag_com().y : k_y;
    if (k_mode == PM_TAKEOFF || k_mode == PM_WAIT) {
        s_ctx = k_mode == PM_WAIT ? 2 : (k_tk_x + 0.0f) * 0.6f + 0.6f;
        s_cty = 1.3f;
        s_ctz = s_zb * 0.85f;
    } else if (k_in_tube) {
        v2 tc = k_tube_c();
        s_ctx = lerpf(tc.x, rx, 0.4f);
        s_cty = lerpf(tc.y, ry + 0.6f, 0.4f);
        s_ctz = s_zb * 1.35f;
    } else {
        s_ctx = rx * 0.8f + 0.4f;
        s_cty = lerpf(1.2f, ry + 0.7f, 0.65f);
        s_ctz = s_zb * (k_mode == PM_AIR ? 1.2f : 1.05f);
    }
}

static bool peak_over(void)
{
    return k_wave >= 2 || (s_ev_t >= s_ev_len && k_mode != PM_AIR && k_mode != PM_RIDE);
}

// the AI: the take-off at the middle of the face; then hold the pocket: drop when the break comes near (p high), let
// the wave lift when it is far; crouch under the lip
static void peak_ai(ai_t *o)
{
    if (k_mode == PM_TAKEOFF) {
        float hy = k_eta(k_tk_x) / k_H;
        o->swipe = hy > k_ai_tk ? 1 : 0;
    } else if (k_mode == PM_RIDE) {
        float hy = k_y / k_H;
        float pp = k_p + (C_PEEL - k_u) * P_RATE * (0.6f + k_ai_off);  // (the stage a little later)
        static float lean_dir = 1;
        if (hy > 0.72f) {
            lean_dir = 0.8f;  // (the pump: down from the top ...)
        }
        if (hy < 0.32f) {
            lean_dir = -0.1f;  // (... and let the wave lift from the bottom)
        }
        float e = pp - 0.62f;  // (the aim: the barrel stage)
        if (e > 0.07f) {
            o->lean = hy > 0.15f ? 1.0f : 0.3f;  // (the break comes: speed)
        } else if (e < -0.06f) {
            o->lean = -0.7f;  // (too fast: brake on the tail)
        } else {
            o->lean = lean_dir;
        }
        if (hy > 0.88f && e > -0.15f) {
            o->lean = 0.9f;  // (never stay under the lip)
        }
        v2 tc = k_tube_c();
        o->hold = (k_q() > 0.15f && k_x > tc.x - k_tube_r() * 1.3f) || pp > 0.62f;
        if ((rnd32() & 63) == 0) {
            k_ai_off = frnd(-1, 1) * s_ai_err * 3;
        }
        if (hy > 0.75f && k_u > 6.0f && k_p < 0.6f && s_ai_rand < 0.02f) {
            o->swipe = 1;
        }
        if ((rnd32() & 31) == 0) {
            s_ai_rand = frnd(0, 1);
        }
    } else if (k_mode == PM_AIR) {
        float tl = (A.vy + fsq(A.vy * A.vy + 2 * GRAV * fmaxf(0, A.y - k_eta(A.x)))) / GRAV;
        float rot = (A.ang - A.ang0) * A.back;
        if (A.ymax - A.y0 > 0.2f && A.throws == 0 && A.t < 0.1f && s_ai_rand < 0.5f) {
            o->swipe = 1;
        }
        o->hold = A.throws > 0 && rot + fabsf(A.L) * tl < 2 * (float)M_PI;
        if (s_ai_err < 0.05f) {
            float pred = A.ang + A.w * tl;
            float err = wrap_pi(pred - k_slope(A.x + A.vx * tl));
            o->lean = clampf(-err * 1.5f, -1, 1) * -A.back;
        }
    }
}

// ---------------------------------------------------------------- the drawing
static int16_t s_ksy[660];    // the screen row of the water surface in each column
static uint8_t s_kband[660];  // the height band of the surface in each column (0 flat .. 7 crest)
static dc_t s_kd[9][48];      // the water colours: [band][depth step]

static void k_lip_poly(float q)
{
    v2 tc = k_tube_c();
    float tr = k_tube_r();
    float a1 = (float)M_PI / 2 - q * 3.75f;  // (the tip angle: from the top round the front)
    float th0 = 0.30f * k_H * 0.5f + 0.12f, th1 = 0.06f;
    float pts[2 * 2 * 40 + 8];
    int n = 0;
    // the outer edge from the crest to the tip, then the inner edge back
    const int S = 18;
    pts[n++] = SX(-0.25f);
    pts[n++] = SY(k_H * 0.99f);
    for (int i = 0; i <= S; i++) {
        float t = i / (float)S, a = (float)M_PI / 2 + (a1 - (float)M_PI / 2) * t;
        float th = lerpf(th0, th1, t);
        float r = tr + th * 0.5f;
        pts[n++] = SX(tc.x + r * fcos(a) - (1 - t) * (1 - t) * 0.3f * 0);
        pts[n++] = SY(tc.y + r * fsin(a));
    }
    for (int i = S; i >= 0; i--) {
        float t = i / (float)S, a = (float)M_PI / 2 + (a1 - (float)M_PI / 2) * t;
        float th = lerpf(th0, th1, t);
        float r = tr - th * 0.5f;
        pts[n++] = SX(tc.x + r * fcos(a));
        pts[n++] = SY(tc.y + r * fsin(a));
    }
    pts[n++] = SX(-0.25f);
    pts[n++] = SY(k_H * 0.86f);
    poly(pts, n / 2, rgb(70, 196, 196), 250);
    // the light through the thin lip: a bright band at the outer edge
    n = 0;
    for (int i = 0; i <= S; i++) {
        float t = i / (float)S, a = (float)M_PI / 2 + (a1 - (float)M_PI / 2) * t;
        float th = lerpf(th0, th1, t);
        float r = tr + th * 0.5f;
        pts[n++] = SX(tc.x + r * fcos(a));
        pts[n++] = SY(tc.y + r * fsin(a));
    }
    for (int i = S; i >= 0; i--) {
        float t = i / (float)S, a = (float)M_PI / 2 + (a1 - (float)M_PI / 2) * t;
        float th = lerpf(th0, th1, t);
        float r = tr + th * 0.12f;
        pts[n++] = SX(tc.x + r * fcos(a));
        pts[n++] = SY(tc.y + r * fsin(a));
    }
    poly(pts, n / 2, rgb(150, 240, 220), 200);
    // the foam on the tip and the spray that it throws
    float tx = tc.x + tr * fcos(a1), ty = tc.y + tr * fsin(a1);
    wdisc(tx, ty, 0.07f + 0.05f * q, rgb(250, 254, 255), 220);
    if ((rnd32() & 3) == 0) {
        part_add(PK_SPRAY, tx, ty, frnd(-0.5f, 1.5f), frnd(-1, 1.5f), frnd(0.3f, 0.6f), frnd(0.03f, 0.07f));
    }
}

static void peak_render(void)
{
    float q = k_q();
    v2 tc = k_tube_c();
    float tr = k_tube_r();
    // the surface of the water in each column (the far rows stop there)
    int miny = SH;
    for (int x = 0; x < SW; x++) {
        float wx = WXs(x + 0.5f);
        float e = k_eta(wx);
        int sy = clampi((int)ceilf(SY(e)), -1, SH);
        if (sy < 0) {
            sy = 0;
        }
        s_ksy[x] = (int16_t)sy;
        s_ntop[x] = (int16_t)sy;
        if (sy < miny) {
            miny = sy;
        }
        float hb = e / HMAX;
        int band = clampi((int)(hb * 7.5f + 0.5f), 0, 7);
        if (k_p > 0.88f && wx > -0.6f && wx < tc.x + tr * 1.6f) {
            band = 8;  // (the foam of the close)
        }
        s_kband[x] = (uint8_t)band;
    }
    prof_start();
    far_setup();
    far_raster();
    prof_mark(PR_FAR);
    far_props();
    prof_mark(PR_PROPS);
    // the inside of the barrel (above the face, in the circle): a dark green that gets light near the lip
    if (q > 0.05f) {
        float cx = SX(tc.x), cy = SY(tc.y), r = tr * cam_z;
        int y0 = clampi((int)(cy - r), 0, SH), y1 = clampi((int)(cy + r), 0, SH);
        dc_t ring[16];
        for (int k = 0; k < 16; k++) {
            ring[k] = dcol(mix3(C3(14, 70, 84), C3(80, 180, 170), k / 15.0f * k / 15.0f));
        }
        float ir2 = 16.0f / (r * r + 1);
        for (int y = y0; y < y1; y++) {
            float dy = y + 0.5f - cy, h2 = r * r - dy * dy;
            if (h2 <= 0) {
                continue;
            }
            float hw = fsq(h2);
            int xa = clampi((int)(cx - hw), s_x0[y], s_x1[y]), xb = clampi((int)(cx + hw), s_x0[y], s_x1[y]);
            // (only the part under the lip: from the crest to the tip of the lip)
            uint16_t *p = &g_fb[y * SW];
            for (int x = xa; x < xb; x++) {
                if (y >= s_ksy[x]) {
                    continue;
                }
                float dx = x + 0.5f - cx;
                int k = (int)((dx * dx + dy * dy) * ir2);
                if (k > 15) {
                    k = 15;
                }
                // the open part (no lip over it yet) stays the sky: the barrel grows with q
                float ang = fatan2(-dy, dx);
                float a1 = (float)M_PI / 2 - q * 3.75f;
                if (ang < a1 && !(ang < -2.2f && q > 0.9f)) {
                    continue;
                }
                p[x] = ring[k].c[y & 1][x & 1];
            }
        }
    }
    // the near rows: the water under the surface (light green near the crest, deep blue below), the foam
    float iz = 1.0f / cam_z;
    for (int b = 0; b < 9; b++) {
        float hb = b / 7.0f;
        for (int d = 0; d < 48; d++) {
            float m = (d * d) * 0.02f * iz * 6;  // (the steps get longer with the depth)
            col3 c;
            if (b == 8) {
                c = mix3(C3(250, 252, 255), C3(120, 190, 210), clampf(m * 0.8f, 0, 1));
            } else {
                col3 top = mix3(C3(64, 168, 200), C3(110, 226, 210), hb);
                col3 mid = mix3(C3(30, 110, 160), C3(40, 150, 170), hb);
                col3 deep = C3(16, 60, 110);
                c = m < 0.25f ? mix3(top, mid, m / 0.25f) : mix3(mid, deep, clampf((m - 0.25f) / 1.6f, 0, 1));
                if (d == 0) {
                    c = mix3(c, C3(240, 252, 255), 0.55f);
                }
            }
            s_kd[b][d] = dcol(c);
        }
    }
    for (int y = miny; y < SH; y++) {
        int xa = s_x0[y], xb = s_x1[y], pr = y & 1;
        uint16_t *p = &g_fb[y * SW];
        for (int x = xa; x < xb; x++) {
            int d = y - s_ksy[x];
            if (d < 0) {
                continue;
            }
            int di = d < 48 ? d : 47;
            if (d > 2 && d < 48) {
                di = (int)(d * 0.5f + 1);  // (finer steps under the surface)
            }
            if (di > 47) {
                di = 47;
            }
            p[x] = s_kd[s_kband[x]][di].c[pr][x & 1];
        }
    }
    prof_mark(PR_NEAR);
    // the streaks of foam on the face (they run down with the wave)
    for (int k = 0; k < 7; k++) {
        float yy = fmodf(s_t * 0.6f + k * 0.33f, 1.0f) * k_H * 0.9f;
        float xx = k_xof(yy);
        float len = 0.25f + 0.1f * k;
        wcap(xx - len, k_eta(xx - len) - 0.03f, xx + len * 0.6f, k_eta(xx + len * 0.6f) - 0.03f, 0.018f, 0.018f,
             rgb(220, 248, 250), 90);
    }
    if (q > 0.02f && k_p < 0.95f) {
        k_lip_poly(q);
    }
    // the surfer
    pose_t P = {0};
    P.gear = GEAR_SURF;
    P.arms = ARMS_OUT;
    if (k_mode == PM_FALL) {
        rag_draw();
    } else if (k_mode == PM_AIR) {
        P.crouch = 0.3f;
        P.tuck = A.tuck;
        P.whip = A.whip;
        P.wave = 0.6f;
        s_pl.ang = A.ang;
        s_pl.side = 1;
        s_pl.x = 0;
        s_pl.y = 0;
        pose_make(&P);
        v2 cb = pose_com();
        float cs = fcos(A.ang), sn = fsin(A.ang);
        place_t pl = {A.x - (cb.x * cs - cb.y * sn), A.y - (cb.x * sn + cb.y * cs), A.ang, 1};
        rider_draw(&P, pl, true);
    } else if (k_mode == PM_TAKEOFF) {
        P.prone = true;
        P.phase = s_t * 7;
        float sl = k_slope(k_x);
        place_t pl = {k_x, k_eta(k_x) - 0.02f, sl, 1};
        rider_draw(&P, pl, true);
        // the cue of the take-off: a soft ring that glows while the face is right
        float hy = k_y / k_H;
        if (hy > 0.30f && hy < 0.86f) {
            float g = 1 - fabsf(hy - 0.6f) / 0.3f;
            glow(SX(k_x), SY(k_y + 0.3f), 1.2f * cam_z, C3(255, 240, 170), (int)(40 + 100 * clampf(g, 0, 1)));
        }
    } else if (k_mode == PM_RIDE) {
        P.crouch = k_crouch;
        P.lean = clampf(-k_vf * 0.25f, -0.6f, 0.6f);
        P.wave = 0.5f + 0.5f * fsin(s_t * 2);
        float sl = k_slope(k_x);
        P.body_rot = -sl * 0.55f;
        place_t pl = {k_x, k_eta(k_x) - 0.03f, sl, 1};
        rider_draw(&P, pl, true);
    }
    parts_draw();
    prof_mark(PR_FG);
}
