// SUMMER CUP: the screens around the play (included by app_summer.c): the title with the event list, the intro of an
// event, the HUD, the result with the medal, the end of THE CUP, the panel, the app.

static const char *const EV_NAME[EV_N][2] = {{"THE BOWL", "הקערה"}, {"THE PEAK", "שיא הגל"}, {"SKIM", "סקימבורד"}};
static const char *const EV_SUB[EV_N][2] = {
        {"CLIFF PARK", "פארק הצוק"}, {"NORTH POINT", "הכף הצפוני"}, {"SUNSET SHORE", "חוף השקיעה"}};

// the layout of the frame
typedef struct {
    int x, y, w, h;
} rect_t;
static rect_t s_btn[4];  // THE CUP, THE BOWL, THE PEAK, SKIM
static int s_title_y;
static void layout(void)
{
    frame_setup();
    s_vcx = SW * 0.5f;
    s_vcy = SH * 0.5f;
    float m = (float)(SW < SH ? SW : SH);
    s_zb = SH < 200 ? SH / 5.6f : (SW > SH * 1.5f ? m / 8.0f : m / (s_round ? 9.4f : 8.4f));
    s_swipe_px = 5.0f / (BOARD_ROUND ? BOARD_NATIVE_MM_PX : BOARD_FRAME_MM_PX);
    if (BOARD_BAR_SCREEN) {
        s_title_y = 74;
        int bw = 186, bh = 64, x0 = SW - 2 * bw - 22, y0 = 16;
        for (int i = 0; i < 4; i++) {
            s_btn[i].x = x0 + (i & 1) * (bw + 10);
            s_btn[i].y = y0 + (i >> 1) * (bh + 12);
            s_btn[i].w = bw;
            s_btn[i].h = bh;
        }
    } else if (s_round) {
        s_title_y = (int)(SH * 0.25f);
        int bw = (int)(SW * 0.56f), bh = (int)(SH * 0.115f), gap = (int)(SH * 0.022f);
        int y0 = (int)(SH * 0.31f);
        for (int i = 0; i < 4; i++) {
            s_btn[i].w = bw;
            s_btn[i].h = bh;
            s_btn[i].x = SW / 2 - bw / 2;
            s_btn[i].y = y0 + i * (bh + gap);
        }
    } else {
        s_title_y = (int)(SH * 0.2f);
        int bw = (SW - 30) / 2, bh = (int)(SH * 0.25f), y0 = (int)(SH * 0.33f);
        if (bh > 70) {
            bh = 70;
        }
        for (int i = 0; i < 4; i++) {
            s_btn[i].x = 10 + (i & 1) * (bw + 10);
            s_btn[i].y = y0 + (i >> 1) * (bh + 10);
            s_btn[i].w = bw;
            s_btn[i].h = bh;
        }
    }
}

// ---------------------------------------------------------------- the medal
static const col3 MED_C[4] = {{90, 100, 120}, {214, 140, 82}, {214, 220, 232}, {255, 204, 60}};
static void medal(float cx, float cy, float r, int m)
{
    if (m <= 0) {
        disc(cx, cy, r, rgb(40, 50, 70), 140);
        disc(cx, cy, r * 0.7f, rgb(70, 84, 104), 160);
        return;
    }
    col3 c = MED_C[m];
    capsule(cx - r * 0.5f, cy - r * 2.0f, cx - r * 0.15f, cy - r * 0.6f, r * 0.28f, r * 0.24f, rgb(40, 120, 220), 256);
    capsule(cx + r * 0.5f, cy - r * 2.0f, cx + r * 0.15f, cy - r * 0.6f, r * 0.28f, r * 0.24f, rgb(240, 80, 90), 256);
    disc(cx, cy, r, c16(mix3(c, C3(60, 40, 20), 0.35f)), 256);
    disc(cx - r * 0.06f, cy - r * 0.06f, r * 0.82f, c16(c), 256);
    disc(cx - r * 0.3f, cy - r * 0.3f, r * 0.28f, rgb(255, 255, 240), 150);
}
static const char *medal_name(int m)
{
    static const char *const N[4][2] = {
            {"NO MEDAL", "בלי מדליה"}, {"BRONZE", "ארד"}, {"SILVER", "כסף"}, {"GOLD", "זהב"}};
    return N[clampi(m, 0, 3)][s_lang];
}

// ---------------------------------------------------------------- the HUD of the play
static void hud_time_arc(float f)
{
    // the round frame: an arc on the edge of the glass (from the top, with the clock); the other frames: a bar
    if (s_round) {
        float cx = SW * 0.5f, cy = SH * 0.5f, r = SW * 0.5f - 7;
        int n = 64, k = (int)(n * clampf(f, 0, 1) + 0.5f);
        for (int i = 0; i < n; i++) {
            float a0 = -1.5708f + i * 6.2832f / n, a1 = a0 + 6.2832f / n;
            uint16_t c = i < k ? rgb(255, 214, 90) : rgb(255, 255, 255);
            int al = i < k ? 230 : 60;
            capsule(cx + r * fcos(a0), cy + r * fsin(a0), cx + r * fcos(a1), cy + r * fsin(a1), 2.2f, 2.2f, c, al);
        }
    } else {
        int w = SW < 400 ? 90 : 120, x = 12, y = 10;
        rrect_a(x, y, w, 7, 3, rgb(20, 30, 50), 140);
        int fw = (int)(w * clampf(f, 0, 1));
        if (fw > 3) {
            rrect_a(x, y, fw, 7, 3, rgb(255, 214, 90), 256);
        }
    }
}
static void hud_pocket(void)
{
    // THE PEAK: the stage of the break (the gold zone = the pocket; red at the top = the lip comes; blue = too fast)
    if (s_ev != EV_PEAK || k_mode != PM_RIDE) {
        return;
    }
    float p = clampf(k_p / 0.9f, 0, 1);
    if (s_round) {
        float cx = SW * 0.5f, cy = SH * 0.5f, r = SW * 0.5f - 22;
        int n = 24;
        for (int i = 0; i < n; i++) {
            float t0 = i / (float)n, t1 = (i + 1) / (float)n;
            float a0 = 2.35f + t0 * 1.6f, a1 = 2.35f + t1 * 1.6f;  // (the left side of the glass, from the bottom up)
            float tm = (t0 + t1) * 0.5f;
            col3 c = tm < 0.36f ? C3(90, 170, 255) : tm < 0.86f ? C3(255, 214, 90) : C3(255, 90, 80);
            capsule(cx + r * fcos(a0), cy - r * fsin(a0 - 3.1416f) * -1, cx + r * fcos(a1),
                    cy - r * fsin(a1 - 3.1416f) * -1, 3, 3, c16(c), 150);
        }
        float a = 2.35f + p * 1.6f;
        disc(cx + r * fcos(a), cy + r * fsin(a), 7, rgb(255, 255, 255), 256);
        disc(cx + r * fcos(a), cy + r * fsin(a), 4, rgb(20, 40, 60), 256);
    } else {
        int h = SH < 200 ? 110 : 140, x = 14, y0 = SH / 2 + h / 2;
        for (int i = 0; i < h; i += 2) {
            float t = i / (float)h;
            col3 c = t < 0.36f ? C3(90, 170, 255) : t < 0.86f ? C3(255, 214, 90) : C3(255, 90, 80);
            hspan(x - 3, x + 4, y0 - i, c16(c), 170);
        }
        disc((float)x, (float)(y0 - p * h), 6, rgb(255, 255, 255), 256);
        disc((float)x, (float)(y0 - p * h), 3.5f, rgb(20, 40, 60), 256);
    }
}
static void hud_draw(void)
{
    char t[24];
    snprintf(t, sizeof t, "%d", (int)s_score);
    if (BOARD_BAR_SCREEN) {
        mf_text_o(&MF_N28, SW - 14 - mf_width(&MF_N28, t, 1), 36, t, rgb(255, 255, 255), rgb(20, 30, 60), 2, 1);
    } else {
        num_c(&MF_N28, SW / 2, s_round ? 64 : 36, t, rgb(255, 255, 255));
    }
    hud_time_arc(s_ev_len > 0 ? s_ev_t / s_ev_len : 0);
    hud_pocket();
    // the wave or the run of now
    char c[24] = "";
    if (s_ev == EV_PEAK) {
        snprintf(c, sizeof c, "%s %d/2", TR("WAVE", "גל"), k_wave < 2 ? k_wave + 1 : 2);
    } else if (s_ev == EV_SKIM) {
        snprintf(c, sizeof c, "%s %d/3", TR("RUN", "ריצה"), m_run < 3 ? m_run + 1 : 3);
    }
    if (c[0]) {
        if (BOARD_BAR_SCREEN) {
            text_sh(&KF_LABEL, SW - 14 - ui_text_w(&KF_LABEL, c) / 2, 58, c, rgb(230, 240, 255));
        } else {
            text_sh(&KF_LABEL, SW / 2, s_round ? 84 : 54, c, rgb(230, 240, 255));
        }
    }
    // the trick text
    if (s_tk_t > 0) {
        float a = clampf(s_tk_t / 0.4f, 0, 1);
        float pop = 1 + 0.15f * clampf((s_tk_t - 1.9f) / 0.3f, 0, 1);
        (void)pop;
        int y = BOARD_BAR_SCREEN ? 40 : s_round ? (int)(SH * 0.74f) : (int)(SH * 0.78f);
        int cx = BOARD_BAR_SCREEN ? SW / 2 : SW / 2;
        if (a > 0.05f) {
            const kfont_t *f1 = ui_text_w(&KF_BIG, s_tk_txt[0]) < SW * (s_round ? 0.62f : 0.8f) ? &KF_BIG : &KF_NAME;
            text_sh(f1, cx, y, s_tk_txt[0], s_tk_col);
            char l2[56];
            if (s_tk_pts > 0) {
                snprintf(l2, sizeof l2, "%s  +%d", s_tk_txt[1], s_tk_pts);
            } else {
                snprintf(l2, sizeof l2, "%s", s_tk_txt[1]);
            }
            if (l2[0]) {
                text_sh(&KF_NAME, cx, y + (BOARD_BAR_SCREEN ? 22 : 26), l2, rgb(255, 255, 255));
            }
        }
    }
}

// ---------------------------------------------------------------- the hints (pictures, no text), until the player
// does it one time in one play: THE PEAK: tilt the board (the surfer sits under the lip with no lean); SKIM: a
// touch (the film under the runner is right for the drop)
static bool s_did_tilt;
static void hint_tilt(int cx, int cy)
{
    float a = 0.30f * fsin(s_t * 4);
    float w = 26, h = 15, cs = fcos(a), sn = fsin(a);
    float pts[8];
    float cx2[4] = {-w, w, w, -w}, cy2[4] = {-h, -h, h, h};
    for (int i = 0; i < 4; i++) {
        pts[2 * i] = cx + cx2[i] * cs - cy2[i] * sn;
        pts[2 * i + 1] = cy + cx2[i] * sn + cy2[i] * cs;
    }
    poly(pts, 4, rgb(20, 40, 70), 200);
    capsule(pts[0], pts[1], pts[2], pts[3], 2, 2, rgb(255, 255, 255), 230);
    capsule(pts[2], pts[3], pts[4], pts[5], 2, 2, rgb(255, 255, 255), 230);
    capsule(pts[4], pts[5], pts[6], pts[7], 2, 2, rgb(255, 255, 255), 230);
    capsule(pts[6], pts[7], pts[0], pts[1], 2, 2, rgb(255, 255, 255), 230);
    // the arrow down to the right (the way down the face)
    capsule(cx + 34, cy - 10, cx + 46, cy + 8, 2.5f, 2.5f, rgb(255, 214, 90), 240);
    capsule(cx + 46, cy + 8, cx + 36, cy + 6, 2.5f, 2.5f, rgb(255, 214, 90), 240);
    capsule(cx + 46, cy + 8, cx + 47, cy - 2, 2.5f, 2.5f, rgb(255, 214, 90), 240);
}
static void hint_tap(int cx, int cy)
{
    float k = s_t * 2.2f - (int)(s_t * 2.2f);
    disc((float)cx, (float)cy, 9, rgb(255, 255, 255), 220);
    for (int i = 0; i < 24; i++) {
        float a0 = i * 0.2618f, a1 = a0 + 0.2618f, r = 12 + 16 * k;
        capsule(cx + r * fcos(a0), cy + r * fsin(a0), cx + r * fcos(a1), cy + r * fsin(a1), 1.6f, 1.6f,
                rgb(255, 255, 255), (int)(220 * (1 - k)));
    }
}
static void hints_draw(void)
{
    int cx = SW / 2, cy = BOARD_BAR_SCREEN ? SH - 40 : (int)(SH * (s_round ? 0.86f : 0.84f));
    if (s_ev == EV_PEAK && k_mode == PM_RIDE && !s_did_tilt && s_tilt_on && imu_ok() && k_y / k_H > 0.84f) {
        hint_tilt(cx - 20, cy);
    }
    if (s_ev == EV_SKIM && m_mode == SM_RUN && !s_did_drop && m_backwash() && m_film(m_x + 0.6f) > 0.002f) {
        hint_tap(cx, cy);
    }
}

// ---------------------------------------------------------------- the screens
// The short tunes. They are static: the audio task reads the notes while the tune plays, after the call is complete
// (an array on the stack of the caller is gone then, and the notes become random data: a note that never stops).
static const tune_note_t SC_T_INTRO[] = {JN(0, 120, J_G4, W_TRI, 40), JN(130, 120, J_B4, W_TRI, 40),
                                         JN(260, 260, J_D5, W_TRI, 45)};
static const tune_note_t SC_T_OPEN[] = {JN(0, 110, J_E4, W_TRI, 40), JN(120, 110, J_G4, W_TRI, 40),
                                        JN(240, 110, J_B4, W_TRI, 42), JN(360, 110, J_E5, W_TRI, 44),
                                        JSL(480, 420, J_D5, 2, W_TRI, 46)};
static void title_go(void)
{
    s_st = ST_TITLE;
    s_st_t = 0;
    s_cup = false;
    s_demo = true;
    s_demo_ev = (s_demo_ev + 1) % EV_N;
    s_ai_err = 0.02f;
    ev_start(s_demo_ev);
    mus_loop(SCM_TITLE);
}
static void intro_go(int ev)
{
    s_demo = false;
    s_ai_hold = false;
    s_st = ST_INTRO;
    s_st_t = 0;
    ev_start(ev);
    mus_loop(SCM_PLAY);
    JPLAY(SC_T_INTRO);
}
static void play_go(void)
{
    s_st = ST_PLAY;
    s_st_t = 0;
    s_down = false;
    s_press = false;
    s_swipe = 0;
    if (imu_ok()) {
        imu_set_level();
    }
}
static void result_go(void)
{
    s_st = ST_RESULT;
    s_st_t = 0;
    int sc = (int)s_score;
    s_new_med = medal_of(s_ev, (float)sc);
    s_new_best = (uint32_t)sc > s_best[s_ev];
    if (s_new_best) {
        s_best[s_ev] = (uint32_t)sc;
        char k[20];
        snprintf(k, sizeof k, SC_KEY "best%d", s_ev);
        store_set(k, s_best[s_ev]);
    }
    if ((uint32_t)s_new_med > s_med[s_ev]) {
        s_med[s_ev] = (uint32_t)s_new_med;
        char k[20];
        snprintf(k, sizeof k, SC_KEY "med%d", s_ev);
        store_set(k, s_med[s_ev]);
    }
    if (s_cup) {
        s_cup_sc[s_ev] = sc;
        s_cup_med[s_ev] = s_new_med;
    }
    if (s_new_med > 0) {
        if (SC_HAVE_MUSIC && s_music) {
            mus_jingle(SCM_J_MEDAL, SC_J_MEDAL_MS, SCM_PLAY);
        } else {
            fx(FX_MEDAL, 70);
        }
    }
    audio_drone(0, 40, 0, W_NOISE);
    audio_drone(1, 30, 0, W_NOISE);
}
static int cup_points(void)
{
    int p = 0;
    for (int i = 0; i < EV_N; i++) {
        p += s_cup_med[i];
    }
    return p;
}
static int cup_medal(void)
{
    int p = cup_points();
    return p >= 8 ? 3 : p >= 5 ? 2 : p >= 2 ? 1 : 0;
}
static void cupend_go(void)
{
    s_st = ST_CUPEND;
    s_st_t = 0;
    int tot = s_cup_sc[0] + s_cup_sc[1] + s_cup_sc[2];
    if ((uint32_t)tot > s_best_cup) {
        s_best_cup = (uint32_t)tot;
        store_set(SC_KEY "cupb", s_best_cup);
    }
    int m = cup_medal();
    if ((uint32_t)m > s_med_cup) {
        s_med_cup = (uint32_t)m;
        store_set(SC_KEY "cupm", s_med_cup);
    }
    if (m > 0) {
        mus_jingle(SCM_J_MEDAL, SC_J_MEDAL_MS, SCM_TITLE);
    }
}

static void btn_draw(int i, const char *name, int med, uint32_t best)
{
    rect_t b = s_btn[i];
    bool cup = i == 0;
    rrect_a(b.x, b.y + 3, b.w, b.h, b.h / 2, rgb(10, 24, 44), 256);
    rrect_a(b.x, b.y, b.w, b.h, b.h / 2, cup ? rgb(255, 120, 70) : rgb(22, 74, 112), 256);
    hspan(b.x + b.h / 2, b.x + b.w - b.h / 2, b.y + 3, cup ? rgb(255, 168, 120) : rgb(60, 112, 150), 256);
    float mr = b.h * 0.22f;
    float mx = s_lang ? b.x + b.w - b.h * 0.5f : b.x + b.h * 0.5f;
    medal(mx, b.y + b.h * 0.56f, mr, med);
    int tx = b.x + b.w / 2 + (s_lang ? -(int)(b.h * 0.22f) : (int)(b.h * 0.22f));
    const kfont_t *f = ui_text_w(&KF_BIG, name) < b.w - b.h ? &KF_BIG : &KF_NAME;
    int base = b.y + b.h / 2 + (best ? 4 : 9);
    ui_text_c(f, tx, base, name, rgb(255, 255, 255));
    if (best) {
        char t[16];
        snprintf(t, sizeof t, "%u", (unsigned)best);
        mf_text_c(&MF_SMALL, tx, b.y + b.h - 7, t, rgb(255, 236, 170), 230, 1);
    }
}
static void title_draw(void)
{
    // the name of the game: big, with a warm shadow
    const char *nm = TR("SUMMER CUP", "גביע הקיץ");
    int tx = BOARD_BAR_SCREEN ? (s_btn[0].x) / 2 : SW / 2;
    ui_text_c(&KF_TITLE, tx + 2, s_title_y + 3, nm, rgb(150, 50, 40));
    ui_text_c(&KF_TITLE, tx, s_title_y, nm, rgb(255, 246, 214));
    if (BOARD_BAR_SCREEN) {
        text_sh(&KF_NAME, tx, s_title_y + 30, TR("THE GOLD COAST", "חוף הזהב"), rgb(255, 230, 170));
    }
    btn_draw(0, TR("THE CUP", "הגביע"), (int)s_med_cup, s_best_cup);
    for (int e = 0; e < EV_N; e++) {
        btn_draw(e + 1, EV_NAME[e][s_lang], (int)s_med[e], s_best[e]);
    }
}
static void intro_draw(void)
{
    float a = clampf(s_st_t / 0.3f, 0, 1) * clampf((2.2f - s_st_t) / 0.4f, 0, 1);
    if (a <= 0.02f) {
        return;
    }
    int y = BOARD_BAR_SCREEN ? 76 : (int)(SH * 0.38f);
    if (s_cup) {
        char t[24];
        snprintf(t, sizeof t, "%s %d/3", TR("EVENT", "מקצה"), s_ev + 1);
        text_sh(&KF_NAME, SW / 2, y - 44, t, rgb(255, 230, 160));
    }
    text_sh(&KF_TITLE, SW / 2, y, EV_NAME[s_ev][s_lang], rgb(255, 250, 230));
    text_sh(&KF_NAME, SW / 2, y + 28, EV_SUB[s_ev][s_lang], rgb(200, 240, 255));
}
static void card_box(int *x, int *y, int *w, int *h)
{
    if (BOARD_BAR_SCREEN) {
        *w = 380;
        *h = 150;
    } else if (s_round) {
        *w = (int)(SW * 0.66f);
        *h = (int)(SH * 0.5f);
    } else {
        *w = SW - 40;
        *h = SH - 70;
    }
    *x = SW / 2 - *w / 2;
    *y = SH / 2 - *h / 2;
}
static void result_draw(void)
{
    int x, y, w, h;
    card_box(&x, &y, &w, &h);
    float a = clampf(s_st_t / 0.35f, 0, 1);
    rrect_a(x, y, w, h, 22, rgb(12, 30, 56), (int)(215 * a));
    if (a < 0.6f) {
        return;
    }
    int cx = SW / 2;
    char t[24];
    snprintf(t, sizeof t, "%d", (int)s_score);
    const char *mn = medal_name(s_new_med);
    float pop = 1 + 0.18f * fmaxf(0, 1 - (s_st_t - 0.3f) * 3);
    if (BOARD_BAR_SCREEN) {
        // the 3.49: the medal at the left, the name of the event and the score in the middle
        text_sh(&KF_NAME, cx + 30, y + 30, EV_NAME[s_ev][s_lang], rgb(200, 236, 255));
        num_c(&MF_N34, cx + 30, y + 82, t, rgb(255, 255, 255));
        if (s_new_best) {
            text_sh(&KF_LABEL, cx + 30, y + 108, TR("NEW BEST", "שיא חדש"), rgb(255, 214, 90));
        }
        medal(x + 62, y + h / 2 + 6, 22 * pop, s_new_med);
        text_sh(&KF_LABEL, x + 62, y + h - 14, mn, c16(MED_C[s_new_med]));
        return;
    }
    text_sh(&KF_NAME, cx, y + 28, EV_NAME[s_ev][s_lang], rgb(200, 236, 255));
    int sy = y + (int)(h * 0.44f);
    num_c(&MF_N34, cx, sy, t, rgb(255, 255, 255));
    if (s_new_best) {
        text_sh(&KF_LABEL, cx, sy + 22, TR("NEW BEST", "שיא חדש"), rgb(255, 214, 90));
    }
    float mr = 17;
    int my = y + h - (int)mr - 16;
    int tw = ui_text_w(&KF_NAME, mn);
    float mx = cx - (tw + 2 * mr + 10) * 0.5f + mr;
    medal(mx, (float)my, mr * pop, s_new_med);
    text_sh(&KF_NAME, (int)(mx + mr + 10 + tw / 2), my + 6, mn, c16(MED_C[s_new_med]));
}
static void cupend_draw(void)
{
    int x, y, w, h;
    card_box(&x, &y, &w, &h);
    float a = clampf(s_st_t / 0.35f, 0, 1);
    rrect_a(x, y, w, h, 22, rgb(12, 30, 56), (int)(225 * a));
    if (a < 0.6f) {
        return;
    }
    int cx = SW / 2;
    text_sh(&KF_NAME, cx, y + 26, TR("THE CUP", "הגביע"), rgb(255, 214, 120));
    float mr = BOARD_BAR_SCREEN ? 12 : 13;
    for (int e = 0; e < EV_N; e++) {
        float mx = cx + (e - 1) * (BOARD_BAR_SCREEN ? 80 : w * 0.28f);
        medal(mx, y + (BOARD_BAR_SCREEN ? 70 : h * 0.38f), mr, s_cup_med[e]);
    }
    char t[24];
    snprintf(t, sizeof t, "%d", s_cup_sc[0] + s_cup_sc[1] + s_cup_sc[2]);
    num_c(&MF_N34, cx, y + (int)(h * (BOARD_BAR_SCREEN ? 0.78f : 0.68f)), t, rgb(255, 255, 255));
    int m = cup_medal();
    text_sh(&KF_BIG, cx, y + h - (BOARD_BAR_SCREEN ? 8 : 16), medal_name(m), c16(MED_C[m]));
}

// ---------------------------------------------------------------- the panel items
enum {
    MI_LANG,
    MI_TILT,
    MI_MUS,
    MI_TITLE
};
static const char *const V_LANG[] = {"ENGLISH", "ENGLISH", "עברית", "עברית"};
static const char *const V_ONOFF[] = {"ON", "פועל", "OFF", "כבוי"};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, V_LANG, SC_KEY "lang"},
        {PI_PICK, MI_TILT, 2, 0, {"TILT", "הטיה"}, V_ONOFF, SC_KEY "tilt"},
        {PI_PICK, MI_MUS, 2, 0, {"MUSIC", "מוזיקה"}, V_ONOFF, SC_KEY "mus"},
        {PI_ACT, MI_TITLE, 0, 0, {"THE TITLE", "למסך הפתיחה"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == MI_TITLE) {
        return s_st != ST_TITLE;
    }
    if (id == MI_TILT) {
        return imu_ok();
    }
    return true;
}
static void item_set(int id, int v)
{
    if (id == MI_LANG) {
        s_lang = v & 1;
    } else if (id == MI_TILT) {
        s_tilt_on = !(v & 1);
    } else if (id == MI_MUS) {
        s_music = !(v & 1);
        s_mus_track = -1;
        s_mus_back_t = 0;
        if (!SC_HAVE_MUSIC) {
            return;
        }
        if (!s_music) {
            midi_bg_stop(300);
        } else {
            midi_bg_start();
            mus_loop(s_st == ST_TITLE ? SCM_TITLE : SCM_PLAY);
        }
    } else if (id == MI_TITLE) {
        title_go();
    }
}
static void item_opened(void)
{
    audio_drone(0, 0, 0, W_NOISE);
    audio_drone(1, 0, 0, W_NOISE);
}
static void item_resume(void)
{
    if (imu_ok()) {
        imu_set_level();
    }
}
static const pitems_t PANEL = {ITEMS, 4, item_shown, NULL, item_set, item_opened, item_resume};

// ---------------------------------------------------------------- the app
static int64_t s_fps_t;
static int s_fps_n;
static int64_t s_draw_us;
static void sc_enter(int64_t now)
{
    layout();
    s_lang = store_get(SC_KEY "lang", 0) ? 1 : 0;
    s_tilt_on = !store_get(SC_KEY "tilt", 0);
    s_music = !store_get(SC_KEY "mus", 0);
    for (int e = 0; e < EV_N; e++) {
        char k[20];
        snprintf(k, sizeof k, SC_KEY "best%d", e);
        s_best[e] = store_get(k, 0);
        snprintf(k, sizeof k, SC_KEY "med%d", e);
        s_med[e] = store_get(k, 0);
    }
    s_best_cup = store_get(SC_KEY "cupb", 0);
    s_med_cup = store_get(SC_KEY "cupm", 0);
    s_mus_track = -1;
    s_mus_back_t = 0;
    if (SC_HAVE_MUSIC) {
        midi_bg_init(SC_TRACKS, SC_TRACK_COUNT, SC_MUSIC_VOICES);
        midi_bg_volume(55);
        if (s_music) {
            midi_bg_start();
        }
    }
    tfsfx_load_mem(SC_SFX_DATA, SC_SFX_LEN, FXN, FX_N < SC_SFX_N ? FX_N : SC_SFX_N, 46);
    sysui_panel_items(&PANEL);
    s_rng ^= (uint32_t)now * 2654435761u;
    s_demo_ev = 2;
    s_t = 0;
    s_last = now;
    s_fps_t = 0;
    s_fps_n = 0;
    s_draw_us = 0;
    title_go();
    cam_snap();
    JPLAY(SC_T_OPEN);
}
static void sc_leave(void)
{
    audio_drone(0, 0, 0, W_NOISE);
    audio_drone(1, 0, 0, W_NOISE);
    if (SC_HAVE_MUSIC) {
        midi_bg_end();
    }
    tfsfx_free();
}
static bool in_btn(int i, int x, int y)
{
    rect_t b = s_btn[i];
    int m = 6;  // (a fat finger: a little more than the box)
    return x >= b.x - m && x < b.x + b.w + m && y >= b.y - m && y < b.y + b.h + m;
}
static void sc_input_ev(const input_event_t *ev)
{
    if (s_st == ST_TITLE) {
        if (ev->kind != IN_TAP || s_st_t < 0.3f) {
            return;
        }
        for (int i = 0; i < 4; i++) {
            if (!in_btn(i, ev->x, ev->y)) {
                continue;
            }
            fx(FX_POP, 60);
            s_cup = i == 0;
            memset(s_cup_sc, 0, sizeof s_cup_sc);
            memset(s_cup_med, 0, sizeof s_cup_med);
            intro_go(i == 0 ? EV_BOWL : i - 1);
            s_cup = i == 0;
            return;
        }
        return;
    }
    if (s_st == ST_INTRO) {
        if (ev->kind == IN_TAP && s_st_t > 0.4f) {
            play_go();
        }
        return;
    }
    if (s_st == ST_PLAY) {
        if (ev->kind == IN_DOWN) {
            in_down(ev->x, ev->y, ev->t_ms);
        } else if (ev->kind == IN_MOVE) {
            in_move(ev->x, ev->y);
        } else if (ev->kind == IN_UP) {
            in_up();
        }
        return;
    }
    if (ev->kind != IN_TAP || s_st_t < 0.9f) {
        return;
    }
    if (s_st == ST_RESULT) {
        if (s_cup && s_ev < EV_N - 1) {
            intro_go(s_ev + 1);
        } else if (s_cup) {
            cupend_go();
        } else {
            title_go();
        }
    } else if (s_st == ST_CUPEND) {
        title_go();
    }
}
// a slow tap (a press of 350 - 900 ms) on the title, the intro and the cards acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void sc_input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    sc_input_ev(ev);
    if (slow && (s_st != ST_PLAY)) {
        sc_input_ev(&tap);
    }
}
static void sc_update(int64_t now)
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
    mus_step(dt);
    if (s_tk_t > 0) {
        s_tk_t -= dt;
    }
    // the slow motion: the aim stays for its time, then goes back to 1 (soft)
    if (s_slow_hold > 0) {
        s_slow_hold -= dt;
    } else {
        s_tscale_t = 1;
    }
    s_tscale += (s_tscale_t - s_tscale) * fminf(1, dt * 7);
    float gdt = dt * s_tscale;
    // the input of this frame: the tilt (the lean) or the AI (the demo, and the auto lean with no tilt)
    float tx = 0, ty = 0;
    bool tilt = s_tilt_on && imu_ok() && !s_demo;
    if (tilt) {
        imu_tilt_logical(&tx, &ty);
        float a = fabsf(tx) < 0.026f ? 0 : (tx - (tx > 0 ? 0.026f : -0.026f)) / 0.11f;
        s_lean = clampf(a, -1, 1);
        if (s_st == ST_PLAY && s_ev == EV_PEAK && fabsf(s_lean) > 0.35f) {
            s_did_tilt = true;
        }
    }
    if (s_demo || (s_st == ST_PLAY && !tilt)) {
        ev_ai();
        if (s_demo) {
            s_ai_hold = s_ai.hold;
            if (s_ai.swipe) {
                s_swipe = s_ai.swipe;
            }
            if (s_ai.press) {
                s_press = true;
            }
            s_lean = s_ai.lean;
        } else {
            // (no tilt: the game leans by itself in THE PEAK; the air and the bowl need no lean)
            s_lean = s_ev == EV_PEAK && k_mode == PM_RIDE ? s_ai.lean : 0;
        }
    }
    if (s_st == ST_PLAY || s_st == ST_TITLE || s_st == ST_RESULT || s_st == ST_CUPEND) {
        bool run = s_st == ST_PLAY || s_st == ST_TITLE || s_st == ST_RESULT;
        if (run) {
            if (s_st == ST_PLAY || s_st == ST_TITLE) {
                s_ev_t += gdt;
            }
            ev_step(gdt);
            parts_step(gdt);
        }
        if (s_st == ST_PLAY && ev_over()) {
            result_go();
        }
        if (s_st == ST_TITLE && ev_over()) {
            s_demo_ev = (s_demo_ev + 1) % EV_N;
            s_ai_err = 0.02f;
            ev_start(s_demo_ev);
        }
        if (s_st == ST_PLAY && !s_demo) { /* (no auto end) */
        }
    } else if (s_st == ST_INTRO) {
        // the fly-in: a wide shot that comes near to the start place
        ev_step(0);
        s_swipe = 0;
        s_press = false;
        if (s_st_t > 2.2f) {
            play_go();
        }
    }
    if (s_st == ST_TITLE && s_ev_t > 26) {
        s_demo_ev = (s_demo_ev + 1) % EV_N;
        ev_start(s_demo_ev);
    }
    if (s_st != ST_INTRO || s_st_t > 0.15f) {
        cam_step(dt);
    }
    if (s_st == ST_INTRO) {
        float k = clampf(1 - s_st_t / 2.0f, 0, 1);
        cam_z = lerpf(cam_z, s_ctz * (1 - 0.35f * k), 0.2f);
    }
}
static void sc_render(int64_t now)
{
    int64_t t0 = esp_timer_get_time();
    ev_render();
    if (s_st == ST_TITLE) {
        title_draw();
    } else if (s_st == ST_INTRO) {
        intro_draw();
    } else if (s_st == ST_PLAY) {
        hud_draw();
        hints_draw();
    } else if (s_st == ST_RESULT) {
        result_draw();
    } else if (s_st == ST_CUPEND) {
        cupend_draw();
    }
    paint_corners();
    prof_mark(PR_HUD);
    s_draw_us += esp_timer_get_time() - t0;
    s_fps_n++;
    if (!s_fps_t) {
        s_fps_t = now;
    }
    if (now - s_fps_t >= 5000) {
        int nf = s_fps_n ? s_fps_n : 1;
        ESP_LOGI(TAG, "fps %.1f, draw %d ms (far %d props %d near %d fg %d hud %d), ev %d st %d, parts %d",
                 s_fps_n * 1000.0f / (float)(now - s_fps_t), (int)(s_draw_us / 1000 / nf),
                 (int)(s_prof[PR_FAR] / 1000 / nf), (int)(s_prof[PR_PROPS] / 1000 / nf),
                 (int)(s_prof[PR_NEAR] / 1000 / nf), (int)(s_prof[PR_FG] / 1000 / nf),
                 (int)(s_prof[PR_HUD] / 1000 / nf), s_ev, s_st, s_npart);
        memset(s_prof, 0, sizeof s_prof);
        s_fps_t = now;
        s_fps_n = 0;
        s_draw_us = 0;
    }
}
static bool sc_awake(void)
{
    return s_st == ST_PLAY || s_st == ST_INTRO;
}

// the icon: a wave with its lip over a sun, a small surfer
static void sc_icon(int x, int y, int s)
{
    float t = icon_time();
    for (int j = 0; j < s; j++) {
        float k = j / (float)s;
        col3 c = k < 0.55f ? mix3(C3(70, 150, 236), C3(255, 196, 140), k / 0.55f)
                           : mix3(C3(40, 150, 196), C3(16, 70, 120), (k - 0.55f) / 0.45f);
        gfx_fill_rect(x, y + j, s, 1, c16(c));
    }
    gfx_fill_circle(x + s * 70 / 100, y + s * 36 / 100, s / 7, rgb(255, 240, 180));
    // the wave: a curl of water from the left
    float cx = x + s * 0.40f, cy = y + s * 0.58f, r = s * 0.30f;
    for (int a = 0; a < 26; a++) {
        float an = 3.1416f * 0.5f - a * 0.16f;
        float rr = r * (1 - a * 0.012f);
        int px = (int)(cx + rr * fcos(an)), py = (int)(cy - rr * fsin(an));
        int w = s / 14 + 1;
        gfx_fill_circle(px, py, w, a < 18 ? rgb(90, 210, 210) : rgb(240, 252, 255));
    }
    gfx_fill_rect(x, y + (int)(s * 0.82f), s, s - (int)(s * 0.82f), rgb(20, 90, 140));
    // the surfer on the face
    int bx = x + (int)(s * 0.30f + s * 0.03f * fsin(t * 2)), by = y + (int)(s * 0.72f);
    gfx_fill_rect(bx - s / 8, by, s / 4, s / 26 + 1, rgb(255, 120, 80));
    gfx_fill_rect(bx - 1, by - s / 6, s / 22 + 1, s / 6, rgb(30, 40, 60));
    gfx_fill_circle(bx, by - s / 6 - s / 24, s / 24 + 1, rgb(232, 168, 120));
}

const app_t app_summer = {
        .name = "SUMMER CUP",
        .landscape = true,
        .round = APP_ROUND_HI,  // (the AMOLED-1.75C: the round frame of the panel, 466 x 466, no scale)
        .enter = sc_enter,
        .leave = sc_leave,
        .input = sc_input,
        .update = sc_update,
        .render = sc_render,
        .draw_icon = sc_icon,
        .awake = sc_awake,
};
