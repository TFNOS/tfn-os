// COIL RIDER: the game round the physics (included by app_coil.c): the states, the camera, the input (the tilt, the two
// halves of the screen, the double tap), the sound and the music, the HUD, the title, the level list, the cards, the
// panel and the app.

// ---------------------------------------------------------------- the state
enum {
    ST_TITLE,
    ST_SELECT,
    ST_INTRO,
    ST_PLAY,
    ST_CRASH,
    ST_RESULT
};
static int s_st;
static float s_st_t;  // the time in the state (real s)
static int64_t s_last;
static int s_lv;             // the level now
static bool s_demo;          // the title: the found track of level 1 plays behind the menu (quiet)
static int s_demo_f;         // the frame of the demo
static uint8_t s_stars[32];  // the best stars of each level (0 = not done)
static uint16_t s_best[32];  // the best time of each level (0.1 s; 0 = none)
static int s_res_stars;
static bool s_res_best;
static float s_tscale = 1, s_tscale_t = 1, s_slow_hold;
static bool s_sound = true, s_music = true;
static float s_gas_show;  // the gas for the gauge (-1..1)
static bool s_moved;
static int s_auto;       // (a test: the store key coil_auto = the found track of a level plays)
static int s_lean_show;  // -1, 0, +1: the side that the finger holds (the HUD)

// the action of one step of a track (the search of the PC, the demo): the tilt (5), the hands (4: the left half, none,
// the right half, both = the crouch), or the turn (20 and more)
#define TRK_FRAMES 6   // the frames of one action (0.2 s at 30 fps)
#define ACT_TURN   22  // (the tilt index 2 = no tilt)
static const float TRK_TILT[5] = {-0.30f, -0.13f, 0.0f, 0.13f, 0.30f};
static inline float act_tilt(int a)
{
    return TRK_TILT[a % 5];
}
static inline int act_lean(int a)
{
    return a >= 20 ? 0 : a / 5 - 1;
}  // -1, 0, +1, 2 = the crouch
static inline bool act_turn(int a)
{
    return a >= 20;
}

static sprite_t s_sp_title, s_sp_select, s_sp_card, s_sp_gauge, s_sp_glit[2];
static int s_corner_n = 6;
static int s_card_st = -1;
static bool s_sp_dirty = true;
static int s_sp_lang = -1;
static float s_sp_g[3];
static void sprites_free(void)
{
    sprite_free(&s_sp_title);
    sprite_free(&s_sp_select);
    sprite_free(&s_sp_card);
    sprite_free(&s_sp_gauge);
    sprite_free(&s_sp_glit[0]);
    sprite_free(&s_sp_glit[1]);
    s_card_st = -1;
}
static inline int stars_of(int lv, float t)
{
    const cr_level_t *L = &CR_LEV[lv];
    int ds = (int)(t * 10 + 0.5f);
    if (L->star[0] && ds <= L->star[0]) {
        return 3;
    }
    if (L->star[1] && ds <= L->star[1]) {
        return 2;
    }
    return 1;
}

// ---------------------------------------------------------------- the sound (the synth)
static void fx(uint16_t f0, uint16_t f1, uint16_t ms, wave_t w, int vol)
{
    if (s_sound && !s_demo) {
        sfx_tone(f0, f1, ms, w, (uint8_t)clampi(vol, 0, 100));
    }
}
static void engine_sound(bool on)
{
    if (!on || !s_sound || s_demo) {
        audio_drone(0, 60, 0, W_PULSE);
        return;
    }
    int ri = G.b.dir > 0 ? 0 : 1;
    float spin = fabsf(G.b.wh[ri].w) * WR;
    float hz = 42 + spin * 7 + G.b.eng * 30;
    audio_drone(0, hz, (int)(4 + 12 * G.b.eng + spin * 0.4f), W_PULSE);
}
static void snd_pick(int n)
{
    fx(1320, 1980, 90, W_SINE, 40);
    fx(1980, 2640, 120, W_TRI, 26 + n * 2);
}
static void snd_thud(float v)
{
    fx(140, 46, 110, W_SINE, (int)clampf(v * 7, 10, 60));
}
static void snd_crash(void)
{
    fx(520, 60, 420, W_NOISE, 50);
    fx(240, 60, 300, W_TRI, 40);
}
static void snd_tap(void)
{
    fx(660, 990, 60, W_TRI, 30);
}
static void snd_turn(void)
{
    fx(300, 700, 120, W_NOISE, 16);
}
static void snd_gate(void)
{
    fx(523, 1046, 300, W_TRI, 34);
}
// The short tunes. They are static: the audio task reads the notes while the tune plays, after the call is complete
// (an array on the stack of the caller is gone then, and the notes become random data).
static const tune_note_t CR_T_INTRO[] = {JN(0, 110, J_C5, W_TRI, 34), JN(120, 110, J_E5, W_TRI, 34),
                                         JN(240, 200, J_G5, W_TRI, 38)};
static const tune_note_t CR_T_WIN[] = {JN(0, 100, J_G4, W_TRI, 36), JN(110, 100, J_C5, W_TRI, 38),
                                       JN(220, 100, J_E5, W_TRI, 40), JN(330, 160, J_G5, W_TRI, 42),
                                       JSL(500, 420, J_C6, 0, W_SINE, 40)};
static const tune_note_t CR_T_OPEN[] = {JN(0, 100, J_C5, W_TRI, 36), JN(110, 100, J_G4, W_TRI, 34),
                                        JN(220, 100, J_C5, W_TRI, 36), JN(330, 100, J_E5, W_TRI, 38),
                                        JSL(450, 380, J_G5, 2, W_SINE, 40)};

// ---------------------------------------------------------------- the music (MIDI pieces, cr_music.c)
static int s_mus_track = -1;
static void mus_loop(int tr)
{
    if (!s_music || s_mus_track == tr) {
        return;
    }
    s_mus_track = tr;
    midi_bg_volume(CR_VOL[tr]);
    midi_bg_play_track(tr, 0, true);
}

// ---------------------------------------------------------------- the layout
typedef struct {
    int x, y, w, h;
} rect_t;
static rect_t s_b_play, s_b_levels, s_b_back, s_b_a, s_b_b;
static rect_t s_tile[32];
static void layout(void)
{
    frame_setup();
    s_vcx = SW * 0.5f;
    s_vcy = SH * 0.5f;
    s_z0 = s_round ? SW / 13.0f : fminf(SW / 12.0f, SH / 5.6f);
    if (BOARD_BAR_SCREEN) {
        s_b_play = (rect_t){SW - 250, 22, 190, 66};
        s_b_levels = (rect_t){SW - 250, 98, 190, 54};
        // 2 rows of 7 between x = 100 and 540 (the thumbs rest at the short edges)
        int tw = 56, th = 62, gx = 8, x0 = SW / 2 - (7 * tw + 6 * gx) / 2, y0 = 18;
        for (int i = 0; i < CR_NLEV; i++) {
            s_tile[i] = (rect_t){x0 + (i % 7) * (tw + gx), y0 + (i / 7) * (th + 10), tw, th};
        }
        s_b_back = (rect_t){18, 54, 64, 64};
        s_b_a = (rect_t){SW / 2 + 40, 40, 170, 60};
        s_b_b = (rect_t){SW / 2 + 40, 108, 170, 48};
    } else if (s_round) {
        int bw = (int)(SW * 0.50f), bh = (int)(SH * 0.13f);
        s_b_play = (rect_t){SW / 2 - bw / 2, (int)(SH * 0.58f), bw, bh};
        s_b_levels = (rect_t){SW / 2 - bw / 2, (int)(SH * 0.74f), bw, (int)(bh * 0.85f)};
        // rows of 4, 5, 5
        int t = (int)(SW * 0.13f), g = (int)(SW * 0.022f);
        static const int ROWN[3] = {4, 5, 5};
        int i = 0;
        for (int r = 0; r < 3; r++) {
            int n = ROWN[r];
            int x0 = SW / 2 - (n * t + (n - 1) * g) / 2;
            for (int c = 0; c < n && i < CR_NLEV; c++, i++) {
                s_tile[i] = (rect_t){x0 + c * (t + g), (int)(SH * 0.27f) + r * (t + g), t, t};
            }
        }
        s_b_back = (rect_t){SW / 2 - t / 2, (int)(SH * 0.27f) + 3 * (t + g) + g, t, (int)(t * 0.85f)};
        s_b_a = (rect_t){SW / 2 - bw / 2, (int)(SH * 0.57f), bw, bh};
        s_b_b = (rect_t){SW / 2 - bw / 2 + bw / 8, (int)(SH * 0.715f), bw * 3 / 4, (int)(bh * 0.8f)};
    } else {
        int bw = (int)(SW * 0.42f), bh = (int)(SH * 0.24f);
        if (bh > 80) {
            bh = 80;
        }
        s_b_play = (rect_t){SW / 2 - bw - 6, SH - bh - 14, bw, bh};
        s_b_levels = (rect_t){SW / 2 + 6, SH - bh - 14, bw, bh};
        // 3 rows of 5
        int g = 6, tw = (SW - 24 - 4 * g) / 5, th = (int)((SH * 0.70f - 2 * g) / 3);
        if (th > tw) {
            th = tw;
        }
        int y0 = (int)(SH * 0.24f);
        for (int i = 0; i < CR_NLEV; i++) {
            s_tile[i] = (rect_t){12 + (i % 5) * (tw + g), y0 + (i / 5) * (th + g), tw, th};
        }
        s_b_back = (rect_t){8, 6, 56, (int)(SH * 0.18f)};
        s_b_a = (rect_t){SW / 2 - bw - 6, SH - bh - 18, bw, bh};
        s_b_b = (rect_t){SW / 2 + 6, SH - bh - 18, bw, bh};
    }
}
static bool in_rect(rect_t b, int x, int y, int m)
{
    return x >= b.x - m && x < b.x + b.w + m && y >= b.y - m && y < b.y + b.h + m;
}

// ---------------------------------------------------------------- the camera
static float s_ctx, s_cty, s_ctz = 40, s_cvx, s_cvy, s_lax, s_lay;
static float s_shake;
static float s_vsm;      // the speed for the zoom (smooth)
#define ZOOM_STOP 0.80f  // the zoom at a stop (x the base zoom)
#define ZOOM_FAST 1.40f  // the zoom at 10 m/s and more
static float s_intro_x0, s_intro_y0;
static void cam_target(void)
{
    const bike_t *b = &G.b;
    float mx = (b->x * 2 + b->wh[0].x + b->wh[1].x) * 0.25f, my = (b->y * 2 + b->wh[0].y + b->wh[1].y) * 0.25f;
    float v = fsq(b->vx * b->vx + b->vy * b->vy);
    // the zoom follows the speed: near the driver at speed, a wide view when the bike stops
    s_vsm += (v - s_vsm) * 0.04f;
    float zk = clampf((s_vsm - 1.0f) * (1.0f / 9), 0, 1);
    zk = zk * zk * (3 - 2 * zk);
    float z = s_z0 * (ZOOM_STOP + (ZOOM_FAST - ZOOM_STOP) * zk);
    if (s_st == ST_RESULT) {
        z = s_z0 * 1.25f;
    }
    if (s_st == ST_CRASH) {
        z = s_z0 * 1.20f;
    }
    if (s_st == ST_TITLE || s_st == ST_SELECT) {
        z = s_z0 * 0.95f;
    }
    s_ctz = z;
    // the look ahead: in the direction of the motion (soft), else in the direction of the bike
    // (in screen parts: a close zoom keeps the bike in the frame)
    float axm = fminf(3.2f, 0.30f * SW / z), aym = fminf(1.6f, 0.22f * SH / z);
    float ax = clampf(b->vx * 0.40f, -axm, axm), ay = clampf(b->vy * 0.25f, -aym, aym);
    if (v < 1.5f) {
        ax = b->dir * 1.0f * (1 - v / 1.5f) + ax * (v / 1.5f);
    }
    s_lax += (ax - s_lax) * 0.06f;
    s_lay += (ay - s_lay) * 0.06f;
    s_ctx = mx + s_lax;
    s_cty = my + s_lay + 0.25f;
    if (s_st == ST_TITLE) {
        // (the bar: the bike at the left of the name; the other frames: over the buttons)
        s_ctx = mx + (BOARD_BAR_SCREEN ? 0.36f : 0.0f) * SW / cam_z;
        s_cty = my + (BOARD_BAR_SCREEN ? 0.10f : 0.05f) * SH / cam_z;
    }
}
static void cam_snap(void)
{
    cam_x = s_ctx;
    cam_y = s_cty;
    cam_z = s_ctz;
    s_cvx = s_cvy = 0;
}
static void cam_step(float dt)
{
    float k = 9.0f, d = 2 * 3.0f;
    s_cvx += ((s_ctx - cam_x) * k - s_cvx * d) * dt;
    s_cvy += ((s_cty - cam_y) * k * 1.3f - s_cvy * d * 1.1f) * dt;
    cam_x += s_cvx * dt;
    cam_y += s_cvy * dt;
    cam_z += (s_ctz - cam_z) * fminf(1, dt * 1.8f);
    s_roll = 0;
    // the bike stays in the frame: a hard limit on the view (a margin of 12 px; the bar: 100 px at the short edges)
    if (s_st == ST_PLAY || s_st == ST_CRASH || s_st == ST_RESULT) {
        float mx = BOARD_BAR_SCREEN ? 100 : 14, my = 10;
        for (int pass = 0; pass < 2; pass++) {
            const bike_t *b = &G.b;
            float hx, hy;
            head_pos(b, &hx, &hy);
            float px[4] = {b->wh[0].x, b->wh[1].x, hx, b->x}, py[4] = {b->wh[0].y, b->wh[1].y, hy, b->y};
            float lo_x = 1e9f, hi_x = -1e9f, lo_y = 1e9f, hi_y = -1e9f;
            for (int i = 0; i < 4; i++) {
                float sx = SX(px[i]), sy = SYW(px[i], py[i]), r = 0.36f * cam_z;
                lo_x = fminf(lo_x, sx - r);
                hi_x = fmaxf(hi_x, sx + r);
                lo_y = fminf(lo_y, sy - r);
                hi_y = fmaxf(hi_y, sy + r);
            }
            float w = hi_x - lo_x, h = hi_y - lo_y;
            if (w > SW - 2 * mx || h > SH - 2 * my) {
                cam_z *= fminf((SW - 2 * mx) / w, (SH - 2 * my) / h);
                continue;
            }
            if (lo_x < mx) {
                cam_x -= (mx - lo_x) / cam_z;
            }
            if (hi_x > SW - mx) {
                cam_x += (hi_x - (SW - mx)) / cam_z;
            }
            if (lo_y < my) {
                cam_y += (my - lo_y) / cam_z;
            }
            if (hi_y > SH - my) {
                cam_y -= (hi_y - (SH - my)) / cam_z;
            }
            break;
        }
    }
    s_shake *= 1 - fminf(1, dt * 7);
    s_shx = s_shake * fsin(s_t * 61);
    s_shy = s_shake * fcos(s_t * 47);
}
static void shake(float px)
{
    if (s_shake < px) {
        s_shake = px;
    }
}
static void slowmo(float k, float hold)
{
    s_tscale_t = k;
    if (s_slow_hold < hold) {
        s_slow_hold = hold;
    }
}

// ---------------------------------------------------------------- the input
static int64_t s_tap_ms;  // the time of the last tap (the double tap = the turn)
static int s_tap_x, s_tap_y;
// the touch now: the lean (TILT ON), or the four pads (TILT OFF: the right half = the gas (its top part = the gas and
// the lean to the right), the left half = the brake (its top part = the lean to the left))
static void touch_read(float *lean, float *gas, bool *crouch)
{
    int16_t px[INPUT_MAX_POINTS], py[INPUT_MAX_POINTS];
    int n = input_points(px, py, INPUT_MAX_POINTS);
    bool l = false, r = false, lt = false, lb = false, rt = false, rb = false;
    for (int i = 0; i < n; i++) {
        int x, y;
        gfx_touch_to_logical(px[i], py[i], &x, &y);
        bool top = y < SH / 2;
        if (x < SW / 2) {
            l = true;
            if (top) {
                lt = true;
            } else {
                lb = true;
            }
        } else {
            r = true;
            if (top) {
                rt = true;
            } else {
                rb = true;
            }
        }
    }
    *gas = 0;
    if (s_tilt_on) {
        *lean = (l && !r) ? -1.0f : (r && !l) ? 1.0f : 0.0f;
        *crouch = l && r;
        return;
    }
    *lean = (lt && !rt) ? -1.0f : (rt && !lt) ? 1.0f : 0.0f;
    *crouch = lt && rt;
    if (rt || rb) {
        *gas = 1;
    } else if (lb) {
        *gas = -1;
    }
}

// the controls of a frame: the tilt (or the pads) and the lean
static void read_controls(float dt, float *gas, float *lean, bool *crouch)
{
    float tx = 0, ty = 0;
    bool tilt = s_tilt_on && imu_ok();
    if (tilt) {
        imu_tilt_logical(&tx, &ty);
    }
    float tg = 0;
    touch_read(lean, &tg, crouch);
    float g = tilt_gas(tx, dt);
    *gas = tilt ? g : tg;
    s_lean_show = *crouch ? 2 : *lean < 0 ? -1 : *lean > 0 ? 1 : 0;
}

// ---------------------------------------------------------------- the flow
static void run_start(int lv)
{
    s_lv = lv;
    s_vsm = 0;
    game_start(lv);
    if (s_world != s_L->world) {
        s_world = s_L->world;
        s_look = &LOOKS[s_world];
        tex_make(s_world);
        sky_make();
        edge_colours();
    }
    s_npart = 0;
    s_tscale = s_tscale_t = 1;
    s_slow_hold = 0;
    s_shake = 0;
    s_moved = false;
    s_gate_k = 0;
    s_lax = G.b.dir * 1.0f;
    s_lay = 0;
    s_demo_f = 0;
}
static void title_go(void)
{
    s_st = ST_TITLE;
    s_st_t = 0;
    s_demo = true;
    run_start(0);
    cam_target();
    cam_snap();
    engine_sound(false);
    mus_loop(CRM_TITLE);
}
static void select_go(void)
{
    if (s_st != ST_TITLE) {
        s_demo = true;
        run_start(0);
        cam_target();
        cam_snap();
    }
    s_st = ST_SELECT;
    s_st_t = 0;
    s_demo = true;
    engine_sound(false);
    mus_loop(CRM_TITLE);
}
static void intro_go(int lv)
{
    s_demo = false;
    run_start(lv);
    s_st = ST_INTRO;
    s_st_t = 0;
    cam_target();
    cam_snap();
    s_intro_x0 = gate_x();
    s_intro_y0 = gate_y() + 1.0f;
    cam_x = s_intro_x0;
    cam_y = s_intro_y0;
    mus_loop(lv < 7 ? CRM_RIDE1 : CRM_RIDE2);
    JPLAY(CR_T_INTRO);
}
static void play_go(void)
{
    s_st = ST_PLAY;
    s_st_t = 0;
    s_tap_ms = 0;
    if (imu_ok()) {
        imu_set_level();
    }
    G.tilt_f = 0;
}
static void again_go(void)
{
    intro_go(s_lv);
    play_go();
    cam_target();
    cam_snap();
}
static void crash_go(void)
{
    s_card_st = -1;
    s_st = ST_CRASH;
    s_st_t = 0;
    slowmo(0.3f, 1.0f);
    shake(9);
    snd_crash();
    engine_sound(false);
    for (int k = 0; k < 10; k++) {
        part_add(PK_SPARK, G.b.hit_x, G.b.hit_y, frnd(-3, 3), frnd(0, 4), 0.6f, 0.04f);
    }
}
static void result_go(void)
{
    s_card_st = -1;
    s_st = ST_RESULT;
    s_st_t = 0;
    engine_sound(false);
    s_res_stars = stars_of(s_lv, G.t);
    s_res_best = false;
    if (s_res_stars > s_stars[s_lv]) {
        s_stars[s_lv] = (uint8_t)s_res_stars;
        char k[16];
        snprintf(k, sizeof k, CR_KEY "s%d", s_lv);
        store_set(k, s_stars[s_lv]);
        s_sp_dirty = true;
    }
    uint16_t t = (uint16_t)clampi((int)(G.t * 10 + 0.5f), 1, 65000);
    if (!s_best[s_lv] || t < s_best[s_lv]) {
        s_best[s_lv] = t;
        char k[16];
        snprintf(k, sizeof k, CR_KEY "t%d", s_lv);
        store_set(k, t);
        s_res_best = true;
        s_sp_dirty = true;
    }
    slowmo(0.5f, 0.6f);
    JPLAY(CR_T_WIN);
}
static int next_level(void)
{
    for (int i = 0; i < CR_NLEV; i++) {
        if (!s_stars[i]) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- the step of the game
static void events_after_step(void)
{
    if (G.max_land > 4.0f && s_st != ST_TITLE && s_st != ST_SELECT) {
        shake(clampf((G.max_land - 3.5f) * 1.8f, 0, 8));
        snd_thud(G.max_land);
        for (int i = 0; i < 2; i++) {
            if (G.b.wh[i].on) {
                for (int k = 0; k < 4; k++) {
                    part_add(PK_DUST, G.b.wh[i].x - G.b.wh[i].nx * WR, G.b.wh[i].y - G.b.wh[i].ny * WR,
                             frnd(-1.2f, 1.2f), frnd(0.2f, 1.2f), 0.5f, 0.06f);
                }
            }
        }
    }
    if (G.last_pick) {
        snd_pick(G.ngot);
        for (int k = 0; k < s_L->np; k++) {
            if (!(G.got & (1u << k))) {
                continue;
            }
            float px = s_L->pk[2 * k] * (1.0f / CR_VK), py = s_L->pk[2 * k + 1] * (1.0f / CR_VK);
            float dx = px - G.b.x, dy = py - G.b.y;
            if (dx * dx + dy * dy > 4) {
                continue;
            }
            for (int j = 0; j < 8; j++) {
                part_add(PK_SHARD, px, py, frnd(-2.5f, 2.5f), frnd(-1, 3), 0.55f, 0.05f);
            }
            part_add(PK_STAR, px, py, 0, 0, 0.4f, 0.25f);
        }
        if (gate_open()) {
            snd_gate();
        }
    }
    // the dust of a rear wheel that spins on the ground
    int ri = G.b.dir > 0 ? 0 : 1;
    const whl_t *R = &G.b.wh[ri];
    if (R->on && G.b.eng > 0.3f && (rnd32() & 3) == 0) {
        float tx = -R->ny, ty = R->nx;
        float slip = R->vx * tx + R->vy * ty - R->w * WR;
        if (fabsf(slip) > 1.2f) {
            part_add(PK_DUST, R->x - R->nx * WR, R->y - R->ny * WR, -G.b.dir * frnd(0.5f, 1.5f), frnd(0.2f, 0.8f),
                     0.45f, 0.05f);
        }
    }
}
static void step_run(float gdt, float gas, float lean, bool crouch)
{
    game_physics(gdt, gas, lean, crouch);
    s_gas_show = G.b.gas * G.b.dir;
}

// ---------------------------------------------------------------- the HUD
static void star_shape(float cx, float cy, float r, uint16_t c, int a)
{
    float pt[20];
    for (int i = 0; i < 10; i++) {
        float ang = -1.5708f + i * 0.6283f, rr = (i & 1) ? r * 0.45f : r;
        pt[2 * i] = cx + rr * fcos(ang);
        pt[2 * i + 1] = cy + rr * fsin(ang);
    }
    poly(pt, 10, c, a);
}
static void star_icon(float cx, float cy, float r, bool on)
{
    if (on) {
        star_shape(cx, cy + 1, r, rgb(150, 90, 20), 256);
        star_shape(cx, cy, r, rgb(255, 210, 60), 256);
        star_shape(cx - r * 0.12f, cy - r * 0.15f, r * 0.45f, rgb(255, 246, 190), 200);
    } else {
        star_shape(cx, cy, r, rgb(40, 40, 60), 150);
        star_shape(cx, cy, r * 0.7f, rgb(90, 90, 120), 120);
    }
}
static void crystal_icon(float cx, float cy, float r, bool on)
{
    float pt[8] = {cx, cy - r * 1.25f, cx + r * 0.75f, cy, cx, cy + r * 1.25f, cx - r * 0.75f, cy};
    poly(pt, 4, on ? c16(s_look->pick) : rgb(60, 60, 80), on ? 256 : 200);
    if (on) {
        disc(cx - r * 0.2f, cy - r * 0.4f, fmaxf(0.8f, r * 0.2f), rgb(255, 255, 255), 220);
    }
}
static void time_text(char *t, size_t n, float s)
{
    int ds = (int)(s * 10 + 0.5f);
    snprintf(t, n, "%d.%d", ds / 10, ds % 10);
}
static void gauge_ring(float cx, float cy, float r)
{
    int n = 24;
    for (int i = 0; i < n; i++) {
        float t0 = i / (float)n, t1 = (i + 1) / (float)n;
        float a0 = 3.1416f + t0 * 3.1416f, a1 = 3.1416f + t1 * 3.1416f;
        float tm = (t0 + t1) - 1;
        uint16_t c = tm < 0 ? rgb(255, 110, 100) : rgb(120, 230, 140);
        capsule(cx + r * fcos(a0), cy + r * fsin(a0), cx + r * fcos(a1), cy + r * fsin(a1), 2.4f, 2.4f, c,
                fabsf(tm) < 0.04f ? 60 : 110);
    }
}
static float s_gr_cx, s_gr_cy, s_gr_r;
static int s_glit_side;
static void gauge_ring_draw(void)
{
    gauge_ring(s_gr_cx, s_gr_cy, s_gr_r);
}
static void gauge_lit_draw(void)
{
    int n = 24;
    for (int i = 0; i < n; i++) {
        float t0 = i / (float)n, t1 = (i + 1) / (float)n, tm = (t0 + t1) - 1;
        if ((tm > 0) != (s_glit_side == 1)) {
            continue;
        }
        float a0 = 3.1416f + t0 * 3.1416f, a1 = 3.1416f + t1 * 3.1416f;
        capsule(s_gr_cx + s_gr_r * fcos(a0), s_gr_cy + s_gr_r * fsin(a0), s_gr_cx + s_gr_r * fcos(a1),
                s_gr_cy + s_gr_r * fsin(a1), 2.4f, 2.4f, tm < 0 ? rgb(255, 110, 100) : rgb(120, 230, 140), 240);
    }
}
// the gas: a half ring; the needle to the right = forward (green), to the left = brake and back (red)
static void gauge_hud(float cx, float cy, float r)
{
    if (!s_sp_gauge.ok || s_sp_g[0] != cx || s_sp_g[1] != cy || s_sp_g[2] != r) {
        s_gr_cx = cx;
        s_gr_cy = cy;
        s_gr_r = r;
        s_sp_g[0] = cx;
        s_sp_g[1] = cy;
        s_sp_g[2] = r;
        sprite_make(&s_sp_gauge, (int)(cx - r - 6), (int)(cy - r - 6), (int)(2 * r + 12), (int)(r + 12),
                    gauge_ring_draw);
        for (int sd = 0; sd < 2; sd++) {
            s_glit_side = sd;
            sprite_make(&s_sp_glit[sd], (int)(cx - r - 6), (int)(cy - r - 6), (int)(2 * r + 12), (int)(r + 12),
                        gauge_lit_draw);
        }
    }
    if (s_sp_gauge.ok) {
        sprite_put(&s_sp_gauge, 0, 0);
    } else {
        gauge_ring(cx, cy, r);
    }
    float g = clampf(s_gas_show, -1, 1);
    float ag = 3.1416f * 1.5f + g * 1.5708f, ex = cx + (r + 4) * fcos(ag);
    if (g > 0.02f && s_sp_glit[1].ok) {
        sprite_put_x(&s_sp_glit[1], 0, 0, (int)cx, (int)(ex + 1));
    }
    if (g < -0.02f && s_sp_glit[0].ok) {
        sprite_put_x(&s_sp_glit[0], 0, 0, (int)ex, (int)cx + 1);
    }
    capsule(cx, cy, cx + r * 0.85f * fcos(ag), cy + r * 0.85f * fsin(ag), 1.8f, 1.2f, rgb(255, 255, 255), 240);
    disc(cx, cy, 3.2f, rgb(255, 255, 255), 256);
}
static int64_t s_hud_us[3];  // (the log) the HUD parts: the icon, the texts, the gauge
static void hud_draw(void)
{
    char t[24];
    int64_t h0 = esp_timer_get_time();
    int cy = BOARD_BAR_SCREEN ? 16 : s_round ? (int)(SH * 0.10f) : 16;
    int cx = SW / 2;
    float r = BOARD_BAR_SCREEN ? 7 : s_round ? SW * 0.016f : 7;
    // the crystals: an icon and "2/5"; the time
    crystal_icon((float)(cx - 74), (float)cy, r, G.ngot > 0);
    snprintf(t, sizeof t, "%d/%d", G.ngot, s_L->np);
    int64_t h1 = esp_timer_get_time();
    // (a shadow, not an outline: the HUD draws each frame)
    mf_text_c(&MF_N20, cx - 40 + 1, cy + 10, t, rgb(20, 20, 40), 255, 1);
    mf_text_c(&MF_N20, cx - 40, cy + 8, t, gate_open() ? rgb(255, 230, 140) : rgb(255, 255, 255), 255, 1);
    time_text(t, sizeof t, G.t);
    mf_text_c(&MF_N20, cx + 40 + 1, cy + 10, t, rgb(20, 20, 40), 255, 1);
    mf_text_c(&MF_N20, cx + 40, cy + 8, t, rgb(255, 255, 255), 255, 1);
    int64_t h2 = esp_timer_get_time();
    float gr = BOARD_BAR_SCREEN ? 24 : s_round ? SW * 0.07f : SH * 0.10f;
    float gy = s_round ? SH * 0.90f : SH - 6.0f;
    if (s_tilt_on && imu_ok()) {
        gauge_hud(SW * 0.5f, gy, gr);
    }
    // the side that the finger holds (a soft bar at that edge of the middle)
    if (s_lean_show == 2) {
        for (int sd = -1; sd <= 1; sd += 2) {
            disc(SW * 0.5f + sd * gr * 1.9f, gy - 4, 4, rgb(255, 196, 60), 230);
        }
    } else if (s_lean_show) {
        float x = SW * 0.5f + s_lean_show * gr * 1.9f;
        float a = -s_lean_show * 0.7f;
        capsule(x - 10 * fcos(a), gy - 4 - 10 * fsin(a), x + 10 * fcos(a), gy - 4 + 10 * fsin(a), 2.5f, 2.5f,
                rgb(255, 220, 140), 220);
    }
    // TILT OFF: the four pads (faint)
    if (!s_tilt_on && s_st_t < 6) {
        int a = s_st_t < 4 ? 70 : (int)(70 * (6 - s_st_t) / 2);
        rrect_a(SW - SW / 4 - 40, SH / 2 + 8, 80, SH / 2 - 20, 14, rgb(120, 230, 140), a);
        rrect_a(SW / 4 - 40, SH / 2 + 8, 80, SH / 2 - 20, 14, rgb(255, 110, 100), a);
        rrect_a(SW - SW / 4 - 40, 12, 80, SH / 2 - 20, 14, rgb(255, 220, 140), a);
        rrect_a(SW / 4 - 40, 12, 80, SH / 2 - 20, 14, rgb(255, 220, 140), a);
    }
    int64_t h3 = esp_timer_get_time();
    s_hud_us[0] += h1 - h0;
    s_hud_us[1] += h2 - h1;
    s_hud_us[2] += h3 - h2;
}

// ---------------------------------------------------------------- the screens
static void button_body(rect_t b, uint16_t c, uint16_t c2, int sa)
{
    rrect_a(b.x, b.y + 3, b.w, b.h, b.h / 2, rgb(20, 10, 30), sa);
    rrect_a(b.x, b.y, b.w, b.h, b.h / 2, c, 256);
    hspan(b.x + b.h / 2, b.x + b.w - b.h / 2, b.y + 3, c2, 220);
}
static void button_text(rect_t b, const char *txt)
{
    const kfont_t *f = ui_text_w(&KF_BIG, txt) < b.w - 16 ? &KF_BIG : &KF_NAME;
    int base = b.y + b.h / 2 + (f == &KF_BIG ? 9 : 6);
    ui_text_c(f, b.x + b.w / 2 + 1, base + 2, txt, rgb(40, 20, 40));
    ui_text_c(f, b.x + b.w / 2, base, txt, rgb(255, 255, 255));
}
static void button(rect_t b, const char *txt, uint16_t c, uint16_t c2)
{
    button_body(b, c, c2, 200);
    button_text(b, txt);
}
static void back_button(rect_t b)
{
    rrect_a(b.x, b.y, b.w, b.h, (b.w < b.h ? b.w : b.h) / 2, rgb(50, 40, 80), 230);
    float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f, s = fminf(b.w, b.h) * 0.22f;
    float d = s_lang ? -1 : 1;
    capsule(cx - d * s, cy, cx + d * s, cy - s, 2.6f, 2.6f, rgb(255, 255, 255), 256);
    capsule(cx - d * s, cy, cx + d * s, cy + s, 2.6f, 2.6f, rgb(255, 255, 255), 256);
}
static const char *game_name(void)
{
    return TR("COIL RIDER", "רוכב הקפיץ");
}
static void title_name(int cx, int y)
{
    const char *nm = game_name();
    ui_text_c(&KF_TITLE, cx + 3, y + 4, nm, rgb(40, 16, 50));
    ui_text_c(&KF_TITLE, cx + 1, y + 2, nm, c16(s_look->edge_lo));
    ui_text_c(&KF_TITLE, cx, y, nm, c16(s_look->edge_hi));
}
static void title_text(void)
{
    if (BOARD_BAR_SCREEN) {
        int cx = 250;
        title_name(cx, 56);
        text_sh(&KF_NAME, cx, 86, TR("A TRIAL ON SPRINGS", "טריאל על קפיצים"), rgb(255, 226, 200));
    } else {
        int y = s_round ? (int)(SH * 0.26f) : (int)(SH * 0.22f);
        title_name(SW / 2, y);
        text_sh(&KF_NAME, SW / 2, y + 28, TR("A TRIAL ON SPRINGS", "טריאל על קפיצים"), rgb(255, 226, 200));
    }
    button_text(s_b_play, TR("PLAY", "שחק"));
    button_text(s_b_levels, TR("LEVELS", "שלבים"));
}
static void title_bodies(void)
{
    button_body(s_b_play, rgb(232, 110, 60), rgb(255, 180, 120), 256);
    button_body(s_b_levels, rgb(60, 130, 170), rgb(130, 200, 230), 256);
}
static void select_draw_live(void)
{
    {
        int x0 = SW, y0 = SH, x1 = 0, y1 = 0;
        for (int i = 0; i < CR_NLEV; i++) {
            if (s_tile[i].x < x0) {
                x0 = s_tile[i].x;
            }
            if (s_tile[i].y < y0) {
                y0 = s_tile[i].y;
            }
            if (s_tile[i].x + s_tile[i].w > x1) {
                x1 = s_tile[i].x + s_tile[i].w;
            }
            if (s_tile[i].y + s_tile[i].h > y1) {
                y1 = s_tile[i].y + s_tile[i].h;
            }
        }
        rect_t b = s_b_back;
        if (b.x < x0) {
            x0 = b.x;
        }
        if (b.y < y0) {
            y0 = b.y;
        }
        if (b.x + b.w > x1) {
            x1 = b.x + b.w;
        }
        if (b.y + b.h > y1) {
            y1 = b.y + b.h;
        }
        int m = BOARD_BAR_SCREEN ? 8 : 10;
        if (!BOARD_BAR_SCREEN) {
            y0 = s_round ? (int)(SH * 0.14f) : 0;
        }
        rrect_a(x0 - m, y0 - m, x1 - x0 + 2 * m, y1 - y0 + 2 * m, 18, rgb(18, 12, 34), 180);
    }
    if (!BOARD_BAR_SCREEN) {
        int ty = s_round ? (int)(SH * 0.21f) : (int)(SH * 0.15f);
        text_sh(&KF_BIG, SW / 2, ty, TR("LEVELS", "שלבים"), rgb(255, 236, 214));
    }
    static const col3 WCOL[WORLD_N] = {{200, 104, 70}, {50, 110, 170}, {170, 50, 40}};
    for (int i = 0; i < CR_NLEV; i++) {
        rect_t b = s_tile[i];
        int st = s_stars[i];
        col3 wc = WCOL[CR_LEV[i].world];
        uint16_t c = c16(st ? wc : mix3(wc, C3(40, 36, 60), 0.55f));
        int rr = (b.w < b.h ? b.w : b.h) / 4;
        rrect_a(b.x, b.y + 3, b.w, b.h, rr, rgb(10, 6, 20), 200);
        rrect_a(b.x, b.y, b.w, b.h, rr, c, 256);
        hspan(b.x + rr, b.x + b.w - rr, b.y + 2, rgb(255, 255, 255), 80);
        char t[8];
        snprintf(t, sizeof t, "%d", i + 1);
        mf_text_oc(&MF_N28, b.x + b.w / 2, b.y + (int)(b.h * 0.56f), t, rgb(255, 255, 255), rgb(30, 16, 40), 2, 1);
        float sr = fminf(b.w * 0.11f, 6.5f);
        for (int s = 0; s < 3; s++) {
            star_icon(b.x + b.w * 0.5f + (s - 1) * sr * 2.3f, b.y + b.h - sr - 4, sr, s < st);
        }
    }
    back_button(s_b_back);
}
static void overlay_prepare(void)
{
    if (s_sp_dirty || s_sp_lang != s_lang) {
        sprite_free(&s_sp_title);
        sprite_free(&s_sp_select);
        s_sp_dirty = false;
        s_sp_lang = s_lang;
    }
    if (s_st == ST_TITLE && !s_sp_title.ok) {
        sprite_make(&s_sp_title, 0, 0, SW, SH, title_text);
    }
    if (s_st == ST_SELECT && !s_sp_select.ok) {
        sprite_make(&s_sp_select, 0, 0, SW, SH, select_draw_live);
    }
}
static void intro_draw(void)
{
    float a = clampf(s_st_t / 0.3f, 0, 1) * clampf((2.6f - s_st_t) / 0.4f, 0, 1);
    if (a <= 0.05f) {
        return;
    }
    int y = BOARD_BAR_SCREEN ? 70 : (int)(SH * 0.36f);
    char t[16];
    snprintf(t, sizeof t, "%d", s_lv + 1);
    mf_text_oc(&MF_N34, SW / 2, y - 30, t, c16(s_look->edge_hi), rgb(20, 14, 34), 2, 1);
    const char *nm = s_L->name[s_lang];
    int room = s_round ? (int)(SW * 0.78f) : SW - 24;
    text_sh(ui_text_w(&KF_TITLE, nm) < room ? &KF_TITLE : &KF_BIG, SW / 2, y + 8, nm, rgb(255, 250, 236));
    float r = BOARD_BAR_SCREEN ? 7 : SH * 0.03f;
    int step = (int)(r * 2.8f);
    int x0 = (int)(SW / 2 - (s_L->np - 1) * step * 0.5f);
    for (int k = 0; k < s_L->np; k++) {
        crystal_icon((float)(x0 + k * step), y + (BOARD_BAR_SCREEN ? 30.0f : 36.0f), r, true);
    }
}
static void card(int *x, int *y, int *w, int *h)
{
    if (BOARD_BAR_SCREEN) {
        *w = 460;
        *h = 150;
    } else if (s_round) {
        *w = (int)(SW * 0.70f);
        *h = (int)(SH * 0.66f);
    } else {
        *w = SW - 30;
        *h = SH - 30;
    }
    *x = SW / 2 - *w / 2;
    *y = SH / 2 - *h / 2;
}
static void crash_draw_in(void)
{
    float a = clampf((s_st_t - 0.8f) / 0.3f, 0, 1);
    if (a <= 0) {
        return;
    }
    int x, y, w, h;
    card(&x, &y, &w, &h);
    rrect_a(x, y, w, h, 24, rgb(40, 18, 40), (int)(220 * a));
    if (a < 0.6f) {
        return;
    }
    const char *l1 = TR("HEAD FIRST!", "עם הראש!");
    char t[24];
    snprintf(t, sizeof t, "%d/%d", G.ngot, s_L->np);
    if (BOARD_BAR_SCREEN) {
        int cx = x + 120;
        text_sh(&KF_BIG, cx, y + 58, l1, rgb(255, 210, 200));
        crystal_icon((float)(cx - 30), (float)(y + 100), 7, true);
        mf_text_oc(&MF_N28, cx + 14, y + 110, t, rgb(255, 255, 255), rgb(20, 14, 34), 2, 1);
        s_b_a = (rect_t){x + w - 200, y + 22, 180, 58};
        s_b_b = (rect_t){x + w - 200, y + 90, 180, 46};
    } else {
        text_sh(ui_text_w(&KF_BIG, l1) < w - 30 ? &KF_BIG : &KF_NAME, SW / 2, y + (int)(h * 0.2f), l1,
                rgb(255, 210, 200));
        mf_text_oc(&MF_N28, SW / 2, y + (int)(h * 0.40f), t, rgb(255, 255, 255), rgb(20, 14, 34), 2, 1);
    }
    button(s_b_a, TR("AGAIN", "שוב"), rgb(232, 110, 60), rgb(255, 180, 120));
    button(s_b_b, TR("LEVELS", "שלבים"), rgb(60, 130, 170), rgb(130, 200, 230));
}
static void crash_draw(void)
{
    if (s_st_t > 1.2f && s_card_st != ST_CRASH) {
        s_card_st = ST_CRASH;
        sprite_make(&s_sp_card, 0, 0, SW, SH, crash_draw_in);
    }
    if (s_card_st == ST_CRASH && s_sp_card.ok) {
        sprite_put(&s_sp_card, 0, 0);
        return;
    }
    crash_draw_in();
}
static void result_draw_in(void)
{
    float a = clampf(s_st_t / 0.3f, 0, 1);
    int x, y, w, h;
    card(&x, &y, &w, &h);
    rrect_a(x, y, w, h, 24, rgb(26, 22, 56), (int)(215 * a));
    if (a < 0.6f) {
        return;
    }
    char t[24];
    int cx = BOARD_BAR_SCREEN ? x + 130 : SW / 2;
    int y0 = BOARD_BAR_SCREEN ? y + 30 : y + (int)(h * (s_round ? 0.11f : 0.13f));
    text_sh(&KF_NAME, cx, y0, s_L->name[s_lang], rgb(220, 236, 255));
    float sr = BOARD_BAR_SCREEN ? 17 : fminf(SW, SH) * (s_round ? 0.05f : 0.065f);
    for (int s = 0; s < 3; s++) {
        float pop = clampf((s_st_t - 0.4f - s * 0.25f) / 0.2f, 0, 1);
        bool on = s < s_res_stars && pop > 0;
        float rr = sr * (on ? 1 + 0.35f * (1 - pop) : 1);
        star_icon(cx + (s - 1) * sr * 2.5f, y0 + sr + 10, rr, on);
    }
    float ty = y0 + sr * 2 + 40;
    time_text(t, sizeof t, G.t);
    mf_text_oc(&MF_N28, cx, (int)ty, t, rgb(255, 255, 255), rgb(20, 14, 34), 2, 1);
    if (s_res_best) {
        text_sh(&KF_LABEL, cx, (int)ty + 20, TR("NEW BEST", "שיא חדש"), rgb(255, 214, 90));
    }
    if (BOARD_BAR_SCREEN) {
        s_b_a = (rect_t){x + w - 200, y + 22, 180, 58};
        s_b_b = (rect_t){x + w - 200, y + 90, 180, 46};
    }
    bool last = s_lv >= CR_NLEV - 1;
    button(s_b_a, last ? TR("LEVELS", "שלבים") : TR("NEXT", "הבא"), rgb(232, 110, 60), rgb(255, 180, 120));
    button(s_b_b, TR("AGAIN", "שוב"), rgb(60, 130, 170), rgb(130, 200, 230));
}
static void result_draw(void)
{
    if (s_st_t > 1.4f && s_card_st != ST_RESULT) {
        s_card_st = ST_RESULT;
        sprite_make(&s_sp_card, 0, 0, SW, SH, result_draw_in);
    }
    if (s_card_st == ST_RESULT && s_sp_card.ok) {
        sprite_put(&s_sp_card, 0, 0);
        return;
    }
    result_draw_in();
}

// ---------------------------------------------------------------- the panel items
enum {
    MI_LANG,
    MI_SND,
    MI_MUS,
    MI_TILT,
    MI_SENS,
    MI_LEVELS,
    MI_RESTART
};
static const char *const V_LANG[] = {"ENGLISH", "ENGLISH", "עברית", "עברית"};
static const char *const V_ONOFF[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const V_SENS[] = {"LOW", "נמוכה", "MID", "בינונית", "HIGH", "גבוהה"};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_TILT, 2, 0, {"TILT", "הטיה"}, V_ONOFF, CR_KEY "tilt"},
        {PI_PICK, MI_SENS, 3, 1, {"TILT SENSITIVITY", "רגישות הטיה"}, V_SENS, CR_KEY "sens"},
        {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, V_LANG, CR_KEY "lang"},
        {PI_PICK, MI_SND, 2, 0, {"SOUND", "צלילים"}, V_ONOFF, CR_KEY "snd"},
        {PI_PICK, MI_MUS, 2, 0, {"MUSIC", "מוזיקה"}, V_ONOFF, CR_KEY "mus"},
        {PI_ACT, MI_LEVELS, 0, 0, {"LEVELS", "שלבים"}, NULL, NULL},
        {PI_ACT2, MI_RESTART, 0, 0, {"RESTART", "מחדש"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == MI_RESTART) {
        return s_st == ST_PLAY || s_st == ST_CRASH || s_st == ST_INTRO;
    }
    if (id == MI_LEVELS) {
        return s_st != ST_SELECT;
    }
    if (id == MI_SENS) {
        return s_tilt_on;
    }
    return true;
}
static void item_set(int id, int v)
{
    if (id == MI_LANG) {
        s_lang = v & 1;
        s_sp_dirty = true;
    } else if (id == MI_SND) {
        s_sound = !(v & 1);
        engine_sound(false);
    } else if (id == MI_TILT) {
        s_tilt_on = !(v & 1);
    } else if (id == MI_SENS) {
        s_sens = clampi(v, 0, 2);
    } else if (id == MI_MUS) {
        s_music = !(v & 1);
        s_mus_track = -1;
        if (!s_music) {
            midi_bg_stop(300);
        } else {
            midi_bg_start();
            mus_loop(s_st == ST_TITLE || s_st == ST_SELECT ? CRM_TITLE : s_lv < 7 ? CRM_RIDE1 : CRM_RIDE2);
        }
    } else if (id == MI_LEVELS) {
        select_go();
    } else if (id == MI_RESTART) {
        again_go();
    }
}
static void item_opened(void)
{
    engine_sound(false);
}
static void item_resume(void)
{
    if (imu_ok()) {
        imu_set_level();
    }
    G.tilt_f = 0;
    s_corner_n = 6;
}
static const pitems_t PANEL = {ITEMS, 7, item_shown, NULL, item_set, item_opened, item_resume};

// ---------------------------------------------------------------- the app
static int64_t s_fps_t;
static int s_fps_n;
static int64_t s_draw_us;
static void cr_enter(int64_t now)
{
    fast_mem(true);
    layout();
    s_lang = store_get(CR_KEY "lang", 0) ? 1 : 0;
    s_sound = !store_get(CR_KEY "snd", 0);
    s_music = !store_get(CR_KEY "mus", 0);
    s_tilt_on = !store_get(CR_KEY "tilt", 0);
    s_sens = clampi((int)store_get(CR_KEY "sens", 1), 0, 2);
    for (int i = 0; i < CR_NLEV && i < 32; i++) {
        char k[16];
        snprintf(k, sizeof k, CR_KEY "s%d", i);
        s_stars[i] = (uint8_t)clampi((int)store_get(k, 0), 0, 3);
        snprintf(k, sizeof k, CR_KEY "t%d", i);
        s_best[i] = (uint16_t)store_get(k, 0);
    }
    s_world = -1;
    s_mus_track = -1;
    midi_bg_init(CR_TRACKS, CR_TRACK_COUNT, CR_MUSIC_VOICES);
    midi_bg_volume(CR_VOL[CRM_TITLE]);
    if (s_music) {
        midi_bg_start();
    }
    sysui_panel_items(&PANEL);
    s_rng ^= (uint32_t)now * 2654435761u;
    s_t = 0;
    s_last = now;
    s_fps_t = 0;
    s_fps_n = 0;
    s_draw_us = 0;
    title_go();
    s_auto = (int)store_get(CR_KEY "auto", 0);
    if (s_auto >= 1 && s_auto <= CR_NLEV) {
        s_lv = s_auto - 1;
        again_go();
    } else {
        s_auto = 0;
    }
    JPLAY(CR_T_OPEN);
}
static void cr_leave(void)
{
    sprites_free();
    tex_free();
    fast_mem(false);
    audio_drone(0, 0, 0, W_PULSE);
    midi_bg_end();
}
static void cr_input_ev(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
    switch (s_st) {
    case ST_TITLE:
        if (ev->kind != IN_TAP || s_st_t < 0.3f) {
            return;
        }
        if (in_rect(s_b_play, x, y, 6)) {
            snd_tap();
            int n = next_level();
            if (n < 0) {
                select_go();
            } else {
                intro_go(n);
            }
        } else if (in_rect(s_b_levels, x, y, 6)) {
            snd_tap();
            select_go();
        }
        return;
    case ST_SELECT:
        if (ev->kind != IN_TAP || s_st_t < 0.25f) {
            return;
        }
        for (int i = 0; i < CR_NLEV; i++) {
            if (in_rect(s_tile[i], x, y, 3)) {
                snd_tap();
                intro_go(i);
                return;
            }
        }
        if (in_rect(s_b_back, x, y, 6)) {
            snd_tap();
            title_go();
        }
        return;
    case ST_INTRO:
        if (ev->kind == IN_TAP && s_st_t > 0.4f) {
            play_go();
            cam_target();
        }
        return;
    case ST_PLAY:
        // the double tap (two short taps, 0.4 s or less, near each other) = the turn
        if (ev->kind == IN_TAP) {
            int64_t t = ev->t_ms;
            int dx = x - s_tap_x, dy = y - s_tap_y;
            if (s_tap_ms && t - s_tap_ms <= 400 && dx * dx + dy * dy < 160 * 160) {
                game_turn();
                snd_turn();
                s_tap_ms = 0;
            } else {
                s_tap_ms = t;
                s_tap_x = x;
                s_tap_y = y;
            }
        }
        return;
    case ST_CRASH:
        if (ev->kind != IN_TAP || s_st_t < 1.2f) {
            return;
        }
        if (in_rect(s_b_b, x, y, 4)) {
            snd_tap();
            select_go();
            return;
        }
        snd_tap();
        again_go();
        return;
    case ST_RESULT:
        if (ev->kind != IN_TAP || s_st_t < 1.0f) {
            return;
        }
        if (in_rect(s_b_a, x, y, 6)) {
            snd_tap();
            if (s_lv >= CR_NLEV - 1) {
                select_go();
            } else {
                intro_go(s_lv + 1);
            }
        } else if (in_rect(s_b_b, x, y, 6)) {
            snd_tap();
            again_go();
        }
        return;
    }
}
// a slow tap (a press of 350 - 900 ms) on the title, the levels, the intro and the cards acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void cr_input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    cr_input_ev(ev);
    if (slow && (s_st != ST_PLAY)) {
        cr_input_ev(&tap);
    }
}
static void cr_update_in(int64_t now);
static void cr_update(int64_t now)
{
    int64_t t0 = esp_timer_get_time();
    cr_update_in(now);
    s_prof[PR_UPD] += esp_timer_get_time() - t0;
}
// the action of the found track (the demo, the test key coil_auto)
// (the same input as the PC test gives: a turn = two short taps at the middle (the right half: a lean for one frame
// each), the turn with the second tap)
static void track_act(const cr_level_t *L, int f, float dt, float *gas, float *lean, bool *crouch)
{
    int i = f / TRK_FRAMES, ff = f % TRK_FRAMES;
    int a = L->track && i < L->ntrack ? L->track[i] : 7;
    int l = act_lean(a);
    *gas = tilt_gas(act_tilt(a), dt);
    *crouch = l == 2;
    *lean = l == 2 ? 0.0f : (float)l;
    if (act_turn(a)) {
        *lean = (ff == 0 || ff == 2) ? 1.0f : 0.0f;
        if (ff == 3) {
            game_turn();
        }
    }
}
static void cr_update_in(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    if (dt < 0) {
        dt = 0;
    }
    s_st_t += dt;
    s_t += dt;
    if (s_music) {
        midi_bg_service();
    }
    if (s_slow_hold > 0) {
        s_slow_hold -= dt;
    } else {
        s_tscale_t = 1;
    }
    s_tscale += (s_tscale_t - s_tscale) * fminf(1, dt * 8);
    float gdt = dt * s_tscale;
    s_gate_k += ((gate_open() ? 1.0f : 0.0f) - s_gate_k) * fminf(1, dt * 3);
    switch (s_st) {
    case ST_TITLE:
    case ST_SELECT: {
        float gas = 0, lean = 0;
        bool crouch = false;
        track_act(&CR_LEV[0], s_demo_f++, 1.0f / 30, &gas, &lean, &crouch);
        step_run(1.0f / 30 * (dt > 0 ? 1 : 0), gas, lean, crouch);
        if (G.done || G.b.crash || G.t > 90 || (s_demo_f / TRK_FRAMES) > CR_LEV[0].ntrack + 20) {
            run_start(0);
            cam_target();
            cam_snap();
        }
        break;
    }
    case ST_INTRO: {
        float k = smooth01((s_st_t - 0.3f) / 2.0f);
        cam_target();
        cam_x = lerpf(s_intro_x0, s_ctx, k);
        cam_y = lerpf(s_intro_y0, s_cty, k);
        cam_z = lerpf(s_z0 * 0.8f, s_ctz, k);
        if (s_st_t > 2.6f) {
            play_go();
        }
        break;
    }
    case ST_PLAY: {
        float gas, lean;
        bool crouch = false;
        read_controls(gdt, &gas, &lean, &crouch);
        if (s_auto) {
            track_act(s_L, s_demo_f++, gdt, &gas, &lean, &crouch);
        }
        step_run(gdt, gas, lean, crouch);
        if (fabsf(G.b.vx) > 0.8f) {
            s_moved = true;
        }
        if (G.b.crash) {
            crash_go();
            break;
        }
        if (G.done) {
            result_go();
        }
        break;
    }
    case ST_CRASH:
    case ST_RESULT:
        step_run(gdt, 0, 0, false);
        if (s_auto && s_st_t > 2.0f) {
            intro_go((s_lv + (s_st == ST_RESULT ? 1 : 0)) % CR_NLEV);
            play_go();
            cam_target();
            cam_snap();
            s_demo_f = 0;
        }
        break;
    }
    if (s_st == ST_PLAY) {
        engine_sound(true);
    } else if (s_st != ST_TITLE && s_st != ST_SELECT) {
        engine_sound(false);
    }
    events_after_step();
    parts_step(gdt);
    if (s_st != ST_INTRO) {
        cam_target();
        cam_step(dt);
    }
}
static void scene_draw(void)
{
    sky_setup();
    terrain_rows();
    prof_mark(PR_ROWS);
    terrain_lines();
    prof_mark(PR_LINES);
    for (int k = 0; k < s_L->np; k++) {
        if (G.got & (1u << k)) {
            continue;
        }
        float px = s_L->pk[2 * k] * (1.0f / CR_VK);
        if (px < s_view_x0 - 1 || px > s_view_x1 + 1) {
            continue;
        }
        pick_draw(px, s_L->pk[2 * k + 1] * (1.0f / CR_VK), k);
    }
    gate_draw();
    prof_mark(PR_THINGS);
    bike_draw();
    parts_draw();
    prof_mark(PR_BIKE);
}
static void cr_render(int64_t now)
{
    int64_t t0 = esp_timer_get_time();
    prof_start();
    terrain_setup();
    overlay_prepare();
    prof_mark(PR_EDGES);
    scene_draw();
    switch (s_st) {
    case ST_TITLE:
        title_bodies();
        if (s_sp_title.ok) {
            sprite_put(&s_sp_title, 0, 0);
        } else {
            title_text();
        }
        break;
    case ST_SELECT:
        if (s_sp_select.ok) {
            sprite_put(&s_sp_select, 0, 0);
        } else {
            select_draw_live();
        }
        break;
    case ST_INTRO:
        intro_draw();
        break;
    case ST_PLAY:
        hud_draw();
        break;
    case ST_CRASH:
        hud_draw();
        crash_draw();
        break;
    case ST_RESULT:
        result_draw();
        break;
    }
    if (s_corner_n > 0 || (s_fps_n & 63) == 0) {
        paint_corners();
    }
    if (s_corner_n > 0) {
        s_corner_n--;
    }
    prof_mark(PR_HUD);
    s_draw_us += esp_timer_get_time() - t0;
    s_fps_n++;
    if (!s_fps_t) {
        s_fps_t = now;
    }
    if (now - s_fps_t >= 5000) {
        int nf = s_fps_n ? s_fps_n : 1;
        int us[PR_N];
        for (int k = 0; k < PR_N; k++) {
            us[k] = (int)(s_prof[k] / nf);
        }
        ESP_LOGI(TAG,
                 "fps %.1f, draw %d us (edges %d rows %d lines %d things %d bike %d hud %d: %d %d %d) upd %d, edges "
                 "%d/%d, st %d lv %d x %d",
                 s_fps_n * 1000.0f / (float)(now - s_fps_t), (int)(s_draw_us / nf), us[PR_EDGES], us[PR_ROWS],
                 us[PR_LINES], us[PR_THINGS], us[PR_BIKE], us[PR_HUD], (int)(s_hud_us[0] / nf), (int)(s_hud_us[1] / nf),
                 (int)(s_hud_us[2] / nf), us[PR_UPD], s_nve, s_ne, s_st, s_lv + 1, (int)G.b.x);
        memset(s_hud_us, 0, sizeof s_hud_us);
        memset(s_prof, 0, sizeof s_prof);
        s_fps_t = now;
        s_fps_n = 0;
        s_draw_us = 0;
    }
}
static bool cr_awake(void)
{
    return s_st == ST_PLAY || s_st == ST_INTRO;
}

// the icon: a dusk sky, a dark hill with a bright edge, a small bike on two coil springs
static void cr_icon(int x, int y, int s)
{
    float t = icon_time();
    for (int j = 0; j < s; j++) {
        float k = j / (float)s;
        col3 c = k < 0.6f ? mix3(C3(38, 28, 84), C3(196, 92, 128), k / 0.6f)
                          : mix3(C3(196, 92, 128), C3(255, 176, 112), (k - 0.6f) / 0.4f);
        gfx_fill_rect(x, y + j, s, 1, c16(c));
    }
    gfx_fill_circle(x + s * 74 / 100, y + s * 30 / 100, s / 9, rgb(255, 236, 190));
    // the hill (a slope up to the right) with the bright edge
    for (int i = 0; i < s; i++) {
        float u = i / (float)s;
        int h = (int)(s * (0.30f + 0.22f * u * u));
        gfx_fill_rect(x + i, y + s - h, 1, h, rgb(44, 26, 44));
        gfx_fill_rect(x + i, y + s - h, 1, s / 22 + 1, rgb(255, 186, 76));
    }
    // the bike: two wheels, the springs that bounce, the body, the rider
    float b = 0.06f * fsin(t * 6.0f);
    int gy = y + s - (int)(s * 0.36f);
    int w1x = x + s * 34 / 100, w2x = x + s * 64 / 100, wr = s / 10;
    int w2y = gy - s * 4 / 100;
    gfx_fill_circle(w1x, gy - wr, wr, rgb(26, 24, 32));
    gfx_fill_circle(w2x, w2y - wr, wr, rgb(26, 24, 32));
    gfx_fill_circle(w1x, gy - wr, wr / 2, rgb(255, 170, 60));
    gfx_fill_circle(w2x, w2y - wr, wr / 2, rgb(200, 204, 220));
    int by = gy - wr - (int)(s * (0.16f + b));
    for (int k = 0; k < 4; k++) {
        int yy = by + (gy - wr - by) * k / 4;
        gfx_fill_rect(w1x - s / 30, yy, s / 15 + 1, 1, rgb(255, 196, 60));
        gfx_fill_rect(w2x - s / 30, yy - s * 2 / 100, s / 15 + 1, 1, rgb(255, 196, 60));
    }
    gfx_fill_rect(w1x - s / 20, by - s / 20, w2x - w1x + s / 10, s / 12, rgb(60, 214, 200));
    gfx_fill_rect(x + s * 40 / 100, by - s * 22 / 100, s / 12, s * 18 / 100, rgb(70, 76, 170));
    gfx_fill_circle(x + s * 45 / 100, by - s * 26 / 100, s / 14 + 1, rgb(255, 126, 52));
}

const app_t app_coil = {
        .name = "COIL RIDER",
        .landscape = true,
        .round = APP_ROUND_HI,
        .enter = cr_enter,
        .leave = cr_leave,
        .input = cr_input,
        .update = cr_update,
        .render = cr_render,
        .draw_icon = cr_icon,
        .awake = cr_awake,
};
