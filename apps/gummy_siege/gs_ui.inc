// GUMMY SIEGE: the screens (included by app_gummy.c). The texts in English and Hebrew, the title, the level map of
// the three worlds, the HUD of a level, the name of a level at its start, the end card, the points that float up.
// Each layout has a 640 x 172 form (the 3.49) and a 320 x 240 form (the BOX).

// ================================================================ the texts
static int s_lang = 1;  // 0 English, 1 Hebrew (the default)
enum {
    T_PLAY,
    T_LEVEL,
    T_W0,
    T_W1,
    T_W2,
    T_CLEAR,
    T_FAIL,
    T_MAP,
    T_AGAIN,
    T_NEXT,
    T_BEST,
    T_NEWBEST,
    T_SLAM,
    T_SPLIT,
    T_DASH,
    T_HOP,
    T_YANK,
    T_NAME,
    T_N
};
static const char *const TXT[T_N][2] = {
        {"TAP TO PLAY", "געו כדי לשחק"},
        {"LEVEL", "שלב"},
        {"SUNSET PICNIC", "פיקניק בשקיעה"},
        {"MIDNIGHT KITCHEN", "מטבח בחצות"},
        {"BIRTHDAY BASH", "מסיבת יום הולדת"},
        {"LEVEL CLEAR!", "השלב נכבש"},
        {"OUT OF BEARS", "נגמרו הדובונים"},
        {"LEVELS", "שלבים"},
        {"AGAIN", "שוב"},
        {"NEXT", "הבא"},
        {"BEST", "שיא"},
        {"NEW BEST!", "שיא חדש"},
        {"SLAM!", "מכה"},
        {"SPLIT!", "פיצול"},
        {"DASH!", "זינוק"},
        {"HOP!", "קפיצה"},
        {"YANK!", "משיכה"},
        {"GUMMY SIEGE", "מצור הדובונים"},
};
#define TX(i) TXT[i][s_lang ? 1 : 0]
static bool rtl(void)
{
    return s_lang != 0;
}

static void fmt_num(uint32_t v, char *o)
{
    char t[16];
    int n = snprintf(t, sizeof t, "%lu", (unsigned long)v), k = 0;
    for (int i = 0; i < n; i++) {
        o[k++] = t[i];
        if ((n - i - 1) % 3 == 0 && i < n - 1) {
            o[k++] = ',';
        }
    }
    o[k] = 0;
}
// a text with a shadow: al 0 = the left end at x, 1 = the middle, 2 = the right end
static void txt(const kfont_t *f, int x, int base, const char *s, uint16_t c, int al, int w)
{
    uint16_t sh = tfd_rgb(30, 10, 30);
    for (int k = 0; k < 2; k++) {
        int dx = k ? 0 : 1, dy = k ? 0 : 1;
        uint16_t cc = k ? c : sh;
        if (al == 0) {
            tfd_text_l(f, x + dx, base + dy, s, cc, w);
        } else if (al == 1) {
            tfd_text_c(f, x + dx, base + dy, s, cc, w);
        } else {
            tfd_text_r(f, x + dx, base + dy, s, cc, w);
        }
    }
}
// ---------------------------------------------------------------- the text masks
// A text that stays (the name of the level in the HUD) is drawn ONE time, white on black, into a scratch frame; its
// light is the alpha of a mask. Each frame copies the mask (a shadow, then the colour): the font code does not run.
typedef struct mask_s {
    int w, h, asc;
    uint8_t *a;
} mask_t;
static uint16_t *s_scratch;  // a frame of scratch (PSRAM; enter() gets it)
static int s_freeze;         // the end card: 1 = the dim world is in s_scratch, 2 = the whole card (render)
static mask_t s_mk_lab, s_mk_name;
static mask_t s_mk_card[5];  // the end card: the result, the best score, the 3 buttons
static void mask_free(mask_t *m)
{
    free(m->a);
    memset(m, 0, sizeof *m);
}
static void mask_make(mask_t *m, const kfont_t *f, const char *s, int maxw)
{
    mask_free(m);
    if (!s_scratch) {
        return;
    }
    s_freeze = 0;  // (the scratch gets the text: a frozen picture in it is gone)
    int h = f->line + 6, w = ui_text_w(f, s) + 4;
    if (h > SH) {
        h = SH;
    }
    if (w > maxw + 4) {
        w = maxw + 4;
    }
    if (w > SW) {
        w = SW;
    }
    uint16_t *save = g_fb;
    g_fb = s_scratch;  // (the font code draws into g_fb: the scratch for a moment)
    tfd_screen();
    memset(s_scratch, 0, (size_t)SW * h * 2);
    tfd_text_l(f, 2, f->asc + 3, s, tfd_rgb(255, 255, 255), w - 4);
    g_fb = save;
    tfd_screen();
    m->a = (uint8_t *)gs_alloc((size_t)w * h);
    if (!m->a) {
        return;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r, g, b;
            tfd_unrgb(s_scratch[y * SW + x], &r, &g, &b);
            m->a[y * w + x] = (uint8_t)(g > 250 ? 255 : g);
        }
    }
    m->w = w;
    m->h = h;
    m->asc = f->asc + 3;
}
// the mask with its text base line at base: al 0 = the left end at x, 2 = the right end at x
static void mask_put(const mask_t *m, int x, int base, uint16_t c, uint16_t sh, int al)
{
    if (!m->a) {
        return;
    }
    int x0 = al == 2 ? x - m->w + 2 : al == 1 ? x - m->w / 2 : x - 2, y0 = base - m->asc;
    for (int k = 0; k < 2; k++) {
        uint16_t col = k ? c : sh;
        int ox = k ? 0 : 1;
        for (int y = 0; y < m->h; y++) {
            int yy = y0 + y + ox;
            if (yy < 0 || yy >= SH) {
                continue;
            }
            uint16_t *d = g_fb + yy * SW;
            const uint8_t *a = m->a + y * m->w;
            for (int x = 0; x < m->w; x++) {
                int xx = x0 + x + ox;
                if (!a[x] || xx < 0 || xx >= SW) {
                    continue;
                }
                d[xx] = a[x] == 255 ? col : tfd_mix(d[xx], col, a[x] + 1);
            }
        }
    }
}
static void hud_masks(void)
{
    char b[48];
    snprintf(b, sizeof b, "%s %d-%d", TX(T_LEVEL), L->world + 1, L->num);
    int wname = GS_BOX ? SW - 112 : SW / 2 - 70;
    if (GS_RND) {
        // (the round layout: the upper left part of the circle (the right part in Hebrew), bigger letters)
        wname = (int)(SW * 0.40f);
        mask_make(&s_mk_lab, &KF_TEXT, b, wname);
        mask_make(&s_mk_name, &KF_BIG, L->name[s_lang ? 1 : 0], wname);
        return;
    }
    mask_make(&s_mk_lab, &KF_LABEL, b, wname);
    mask_make(&s_mk_name, &KF_TEXT, L->name[s_lang ? 1 : 0], wname);
}

static const uint8_t WCOL[3][3] = {{236, 92, 70}, {70, 110, 190}, {214, 80, 160}};
static uint16_t wcol(int w, int k)
{
    return tfd_rgb(WCOL[w][0] * k / 256, WCOL[w][1] * k / 256, WCOL[w][2] * k / 256);
}

// a rounded box with a border in ONE write of each pixel (no box over a box: the frame is in PSRAM)
static void rrect_b(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border, int bw)
{
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy >= SH) {
            continue;
        }
        float dy = j < r ? r - (j + 0.5f) : j >= h - r ? (j + 0.5f) - (h - r) : 0;
        int in = dy > 0 ? (int)(r - sqrtf(maxf(0, r * r - dy * dy)) + 0.5f) : 0;
        int x0 = clampi(x + in, 0, SW), x1 = clampi(x + w - in, 0, SW);
        uint16_t *d = g_fb + yy * SW;
        if (j < bw || j >= h - bw) {
            fill16(d, x0, x1, border);
            continue;
        }
        int b0 = clampi(x0 + bw + (in > 0 ? 1 : 0), x0, x1), b1 = clampi(x1 - bw - (in > 0 ? 1 : 0), b0, x1);
        fill16(d, x0, b0, border);
        fill16(d, b0, b1, fill);
        fill16(d, b1, x1, border);
    }
}
// a button: a rounded box with a text; returns nothing (the hit test is hit())
typedef struct {
    int x, y, w, h;
} box_t;
static bool hit(box_t b, int x, int y)
{
    return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h;
}
static void button_m(box_t b, const char *s, const mask_t *m, uint16_t fill, bool pressed)
{
    int p = pressed ? 2 : 0;
    tfd_rrect(b.x, b.y + 3, b.w, b.h, 12, tfd_mix(fill, tfd_rgb(20, 4, 20), 170), 256);
    tfd_rrect(b.x, b.y + p, b.w, b.h, 12, fill, 256);
    tfd_rect(b.x + 10, b.y + 4 + p, b.w - 20, 3, tfd_mix(fill, tfd_rgb(255, 255, 255), 90), 256);
    if (m) {
        mask_put(m, b.x + b.w / 2, b.y + b.h / 2 + 7 + p, tfd_rgb(255, 255, 255), tfd_rgb(30, 10, 30), 1);
    } else {
        txt(&KF_TEXT, b.x + b.w / 2, b.y + b.h / 2 + 7 + p, s, tfd_rgb(255, 255, 255), 1, b.w - 8);
    }
}
static void button(box_t b, const char *s, uint16_t fill, bool pressed)
{
    button_m(b, s, NULL, fill, pressed);
}

// ================================================================ the HUD of a level
static void pops_draw(void)
{
    for (int i = 0; i < s_npop; i++) {
        const pop_t *p = &s_pop[i];
        float t = p->t / 1.4f;
        int x = (int)wsx(p->x), y = (int)(wsy(p->y) - t * 26);
        char b[16];
        fmt_num((uint32_t)p->pts, b);
        uint16_t c = p->kind == 1   ? tfd_rgb(255, 214, 60)
                     : p->kind == 2 ? tfd_rgb(255, 90, 80)
                                    : tfd_rgb(255, 255, 255);
        const mfont_t *f = p->kind ? &MF_N20 : &MF_N16;
        if (t < 0.85f || ((int)(p->t * 20) & 1)) {
            mf_text_oc(f, x, y, b, c, tfd_rgb(60, 16, 30), 1, 0);
        }
    }
}
// the HUD of the round layout: the name of the level in the upper left part of the circle, the score, the stars and the
// bears that wait in the upper right part (Hebrew: the sides change)
static void hud_draw_round(void)
{
    char sc[20];
    uint16_t sh = tfd_rgb(30, 10, 30);
    int xl = (int)(SW * 0.13f), xr = (int)(SW * 0.87f), sy = (int)(SH * 0.20f);
    mask_put(&s_mk_lab, rtl() ? xr : xl, sy, tfd_rgb(255, 236, 200), sh, rtl() ? 2 : 0);
    mask_put(&s_mk_name, rtl() ? xr : xl, sy + s_mk_name.asc + 4, tfd_rgb(255, 255, 255), sh, rtl() ? 2 : 0);
    fmt_num(s_score, sc);
    int w = mf_width(&MF_N24, sc, 0);
    mf_text_o(&MF_N24, rtl() ? xl : xr - w, sy, sc, tfd_rgb(255, 255, 255), tfd_rgb(60, 16, 40), 1, 0);
    float st = 10 * GS_RS / 1.35f, gap = 2.6f * st;
    for (int k = 0; k < 3; k++) {
        bool on = s_score >= (k == 0 ? 1u : k == 1 ? L->s2 : L->s3) && (k > 0 || s_enemies == 0);
        float sx = rtl() ? xl + st + k * gap : xr - st - (2 - k) * gap, yy = sy + 2.2f * st;
        star5(sx, yy, st, 0, tfd_rgb(60, 20, 40), 160);
        if (on) {
            star5(sx, yy, st * 0.85f, 0, tfd_rgb(255, 214, 60), 256);
        } else {
            star5(sx, yy, st * 0.7f, 0, tfd_rgb(140, 110, 120), 180);
        }
    }
    for (int i = 0; i < s_nqueue && i < 7; i++) {
        float bx = rtl() ? xl + st + i * 1.9f * st : xr - st - i * 1.9f * st;
        const pic_t *p = &s_pic[P_BEAR0 + s_queue[i]];
        spr(p, bx, sy + 5.0f * st, 0.24f * GS_RS / 1.35f, 0.24f * GS_RS / 1.35f, 0, 32, 41, 256, 0, 0);
    }
    tp_body_t *bb = s_nbody ? bear_main() : NULL;
    if (bb && s_phase == PH_FLY && !s_power_used && s_bear_kind >= 0 && s_tick < 66) {
        float X = wsx(bb->xf.p.x), Y = wsy(bb->xf.p.y) - bb->radius * cam_ppm() * 1.9f - 6;
        if (Y < SH * 0.32f) {
            Y = wsy(bb->xf.p.y) + bb->radius * cam_ppm() * 1.9f + 24;  // (under the bear: not on the HUD)
        }
        if (gfx_in_circle((int)X, (int)Y - 10, 30)) {
            txt(&KF_TEXT, (int)X, (int)Y, TX(T_SLAM + s_bear_kind), tfd_rgb(255, 255, 255), 1, 160);
        }
    }
}
static void hud_draw(void)
{
    char sc[20];
    int pad = 8;
    if (GS_RND) {
        hud_draw_round();
        return;
    }
    // (the name: a text only, so the pull zone of the panel can have it; the masks: hud_masks at the start of a level)
    uint16_t sh = tfd_rgb(30, 10, 30);
    mask_put(&s_mk_lab, rtl() ? SW - pad : pad, 15, tfd_rgb(255, 236, 200), sh, rtl() ? 2 : 0);
    mask_put(&s_mk_name, rtl() ? SW - pad : pad, 34, tfd_rgb(255, 255, 255), sh, rtl() ? 2 : 0);
    fmt_num(s_score, sc);
    int w = mf_width(&MF_N20, sc, 0);
    int x = rtl() ? pad : SW - pad - w;
    mf_text_o(&MF_N20, x, 24, sc, tfd_rgb(255, 255, 255), tfd_rgb(60, 16, 40), 1, 0);
    // the stars that this score gives now (the targets of the level)
    for (int k = 0; k < 3; k++) {
        bool on = s_score >= (k == 0 ? 1u : k == 1 ? L->s2 : L->s3) && (k > 0 || s_enemies == 0);
        float sx = rtl() ? pad + 8 + k * 16 : SW - pad - 8 - (2 - k) * 16;
        star5(sx, 38, 6.5f, 0, tfd_rgb(60, 20, 40), 160);
        if (on) {
            star5(sx, 38, 5.5f, 0, tfd_rgb(255, 214, 60), 256);
        } else {
            star5(sx, 38, 4.5f, 0, tfd_rgb(140, 110, 120), 180);
        }
    }
    // the bears that wait (the next one first), small, under the stars
    for (int i = 0; i < s_nqueue && i < 7; i++) {
        float bx = rtl() ? pad + 7 + i * 14 : SW - pad - 7 - i * 14;
        const pic_t *p = &s_pic[P_BEAR0 + s_queue[i]];
        spr(p, bx, 58, 0.17f, 0.17f, 0, 32, 41, 256, 0, 0);
    }
    // the power of the bear in the air: its name over the bear (once, until the tap)
    tp_body_t *bb = s_nbody ? bear_main() : NULL;
    if (bb && s_phase == PH_FLY && !s_power_used && s_bear_kind >= 0 && s_tick < 66) {
        float X = wsx(bb->xf.p.x), Y = wsy(bb->xf.p.y) - bb->radius * cam_ppm() * 1.9f - 4;
        if (Y > 14 && X > 30 && X < SW - 30) {
            txt(&KF_LABEL, (int)X, (int)Y, TX(T_SLAM + s_bear_kind), tfd_rgb(255, 255, 255), 1, 120);
        }
    }
}
// the name of the level at its start (it comes in and goes; a tap goes on at once)
static void intro_draw(void)
{
    if (s_phase != PH_INTRO) {
        return;
    }
    float t = s_tick / 150.0f;  // (the first 2.5 s of the camera tour)
    float a = smooth01(t * 5) * (1 - smooth01((t - 0.75f) * 4));
    if (a <= 0.01f) {
        return;
    }
    int cy = GS_BOX ? 86 : 74, h = GS_BOX ? 64 : 54;
    if (GS_RND) {
        // (the round layout: a band over the middle of the circle, bigger letters)
        cy = (int)(SH * 0.405f);  // (under the HUD)
        h = (int)(SH * 0.17f);
        tfd_rect(0, cy - h / 2, SW, h, wcol(L->world, 120), (int)(170 * a));
        tfd_rect(0, cy - h / 2, SW, 3, tfd_rgb(255, 230, 200), (int)(200 * a));
        tfd_rect(0, cy + h / 2 - 3, SW, 3, tfd_rgb(255, 230, 200), (int)(200 * a));
        if (a < 0.5f) {
            return;
        }
        char rb[48];
        snprintf(rb, sizeof rb, "%s %d-%d", TX(T_LEVEL), L->world + 1, L->num);
        txt(&KF_TEXT, SW / 2, cy - h / 2 + (int)(h * 0.36f), rb, tfd_rgb(255, 236, 200), 1, (int)(SW * 0.7f));
        txt(&KF_TITLE, SW / 2, cy + h / 2 - (int)(h * 0.16f), L->name[s_lang ? 1 : 0], tfd_rgb(255, 255, 255), 1,
            (int)(SW * 0.86f));
        return;
    }
    tfd_rect(0, cy - h / 2, SW, h, wcol(L->world, 120), (int)(170 * a));
    tfd_rect(0, cy - h / 2, SW, 2, tfd_rgb(255, 230, 200), (int)(200 * a));
    tfd_rect(0, cy + h / 2 - 2, SW, 2, tfd_rgb(255, 230, 200), (int)(200 * a));
    if (a < 0.5f) {
        return;
    }
    char b[48];
    snprintf(b, sizeof b, "%s %d-%d", TX(T_LEVEL), L->world + 1, L->num);
    txt(&KF_LABEL, SW / 2, cy - h / 2 + 18, b, tfd_rgb(255, 236, 200), 1, SW - 20);
    txt(&KF_BIG, SW / 2, cy + h / 2 - 9, L->name[s_lang ? 1 : 0], tfd_rgb(255, 255, 255), 1, SW - 20);
}

// ================================================================ the end card
static float s_card_t;  // the time of the card (s)
static bool s_newbest;
static int s_btn_dn = -1;  // the button under the finger (the look of a press)
static int card_buttons(box_t *bt)
{
    bool next = s_won && s_li + 1 < s_nlev;
    int n = next ? 3 : 2;
    if (GS_RND) {
        // (the round layout: one row under the middle of the round card; 49 px high on the 1.75C = 4.6 mm)
        int w = (int)(SW * (n == 3 ? 0.20f : 0.26f)), gap = (int)(SW * 0.025f), h = (int)(SH * 0.105f);
        int x0 = (SW - (n * w + (n - 1) * gap)) / 2;
        for (int i = 0; i < n; i++) {
            bt[i] = (box_t){x0 + i * (w + gap), (int)(SH * 0.645f), w, h};
        }
    } else if (!GS_BOX) {
        int x = rtl() ? 110 : SW - 110 - 120;
        int h = 44, gap = 6, y0 = 86 - (n * h + (n - 1) * gap) / 2;
        for (int i = 0; i < n; i++) {
            bt[i] = (box_t){x, y0 + i * (h + gap), 120, h};
        }
    } else {
        int w = n == 3 ? 92 : 128, gap = 8, x0 = (SW - (n * w + (n - 1) * gap)) / 2;
        if (GS_TAB5) {
            w = n == 3 ? 150 : 190;
            gap = 14;
            x0 = (SW - (n * w + (n - 1) * gap)) / 2;
        }
        for (int i = 0; i < n; i++) {
            bt[i] = GS_TAB5 ? (box_t){x0 + i * (w + gap), SH - 84, w, 58} : (box_t){x0 + i * (w + gap), 172, w, 50};
        }
    }
    return n;
}
// the order of the buttons: NEXT (when there is one), AGAIN, LEVELS
static int card_btn_id(int i)
{
    bool next = s_won && s_li + 1 < s_nlev;
    static const int A[3] = {T_NEXT, T_AGAIN, T_MAP}, B[2] = {T_AGAIN, T_MAP};
    return next ? A[i] : B[i];
}
static void card_masks(void)
{
    char sb[48];
    int lw = GS_RND ? (int)(SW * 0.62f) : GS_BOX ? SW - 36 : 280;
    const kfont_t *rf = &KF_BIG;
    if (GS_RND && ui_text_w(&KF_TITLE, TX(s_won ? T_CLEAR : T_FAIL)) <= lw) {
        rf = &KF_TITLE;  // (the round: big when it fits)
    }
    mask_make(&s_mk_card[0], rf, TX(s_won ? T_CLEAR : T_FAIL), lw);
    if (s_newbest && s_won) {
        snprintf(sb, sizeof sb, "%s", TX(T_NEWBEST));
    } else {
        char bb[20];
        fmt_num(s_best[s_li], bb);
        snprintf(sb, sizeof sb, "%s %s", TX(T_BEST), bb);
    }
    mask_make(&s_mk_card[1], GS_RND ? &KF_TEXT : &KF_LABEL, sb, lw);
    box_t bt[3];
    int n = card_buttons(bt);
    for (int i = 0; i < 3; i++) {
        if (i < n) {
            mask_make(&s_mk_card[2 + i], &KF_TEXT, TX(card_btn_id(i)), bt[i].w - 8);
        } else {
            mask_free(&s_mk_card[2 + i]);
        }
    }
}

static void card_btn_draw(int i, int dy)
{
    box_t bt[3];
    if (i < 0 || i >= card_buttons(bt)) {
        return;
    }
    bt[i].y += dy;
    int id = card_btn_id(i);
    uint16_t c = id == T_NEXT ? tfd_rgb(60, 180, 90) : id == T_AGAIN ? tfd_rgb(240, 140, 40) : tfd_rgb(90, 110, 200);
    button_m(bt[i], TX(id), &s_mk_card[2 + i], c, s_btn_dn == i);
}
// a button of a frozen card again (pressed): the fill of the card first, under the old button
static void card_btn_redraw(int i)
{
    box_t bt[3];
    if (i < 0 || i >= card_buttons(bt)) {
        return;
    }
    tfd_rect(bt[i].x, bt[i].y, bt[i].w, bt[i].h + 4, s_won ? wcol(L->world, 140) : tfd_rgb(60, 46, 70), 256);
    card_btn_draw(i, 0);
}
static void card_draw(void)
{
    if (s_phase != PH_END) {
        return;
    }
    float t = s_card_t, a = smooth01(t * 3);
    if (a <= 0.01f) {
        return;
    }
    int cx0, cy0, cw, ch;
    if (GS_RND) {
        cw = ch = (int)(SW * 0.88f);
        cx0 = cy0 = (SW - cw) / 2;
    }  // (the round layout: a round card)
    else if (GS_BOX) {
        cx0 = 8;
        cy0 = 8;
        cw = SW - 16;
        ch = SH - 16;
    } else {
        cx0 = 96;
        cy0 = 6;
        cw = SW - 192;
        ch = SH - 12;
    }
    int dy = (int)((1 - a) * 30), oy = GS_TAB5 ? 40 : 0;  // (oy: the Tab5: the result lower, in the middle)
    rrect_b(cx0, cy0 + dy, cw, ch, GS_RND ? cw / 2 : 18, s_won ? wcol(L->world, 140) : tfd_rgb(60, 46, 70),
            s_won ? wcol(L->world, 200) : tfd_rgb(90, 70, 100), 4);
    // the left part (the 3.49) or the top part (the BOX): the result, the stars, the score
    int mx = GS_BOX ? SW / 2 : (rtl() ? cx0 + cw - 160 : cx0 + 150);
    uint16_t shc = tfd_rgb(30, 10, 30);
    int ry = GS_RND ? (int)(SH * 0.265f) - cy0 : GS_BOX ? 36 : 32;  // (the round: the parts of SH)
    mask_put(&s_mk_card[0], mx, cy0 + dy + oy + ry, tfd_rgb(255, 255, 255), shc, 1);
    int sy = cy0 + dy + oy + (GS_RND ? (int)(SH * 0.38f) - cy0 : GS_BOX ? 80 : 68);
    for (int k = 0; k < 3; k++) {
        float st = t - 0.5f - k * 0.35f;
        float sx = mx + (k - 1) * (GS_RND ? 52 * GS_RS : GS_BOX ? 52 : 46), r = GS_RND ? 21 * GS_RS : GS_BOX ? 21 : 18;
        float yy = sy - (k == 1 ? 6 : 0);
        bool on = k < s_stars_now && st > 0;
        float pop = on && st < 0.25f ? 1.0f + 0.6f * (1 - st / 0.25f) : 1.0f;
        if (on && st < 0.6f) {
            tfd_glow(sx, yy, r * 1.5f, tfd_rgb(255, 200, 60), (int)(110 * (1 - st / 0.6f)));
        }
        star5(sx, yy, on ? r * pop : r - 2, 0, on ? tfd_rgb(255, 214, 60) : tfd_rgb(110, 80, 110), 256);
        if (on) {
            star5(sx - r * 0.15f, yy - r * 0.2f, r * 0.45f * pop, 0, tfd_rgb(255, 250, 200), 200);
        }
    }
    char b[32];
    float cnt = clampf((t - 0.4f) / 1.0f, 0, 1);
    fmt_num((uint32_t)(s_score * cnt), b);
    int base = cy0 + dy + oy + (GS_RND ? (int)(SH * 0.53f) - cy0 : GS_BOX ? 130 : 120);
    mf_text_oc(GS_RND ? &MF_N34 : &MF_N24, mx, base, b, tfd_rgb(255, 255, 255), tfd_rgb(60, 16, 40), 1, 0);
    if (t > 1.5f) {
        mask_put(&s_mk_card[1], mx,
                 base + (GS_RND   ? (int)(SH * 0.065f)
                         : GS_BOX ? 20
                                  : 22),
                 s_newbest && s_won ? tfd_rgb(255, 230, 120) : tfd_rgb(255, 230, 220), shc, 1);
    }
    if (t > 0.6f) {
        box_t bt[3];
        int n = card_buttons(bt);
        for (int i = 0; i < n; i++) {
            card_btn_draw(i, dy);
        }
    }
}

// ================================================================ the title
static float s_title_t;
static void title_draw(void)
{
    s_world = 0;
    s_cam.z = GS_RND ? 0.8f * GS_RS : GS_TAB5 ? 1.25f : GS_BOX ? 0.8f : 1.0f;
    s_lift = 0;
    s_cam.x = SL_X + SW * 0.5f / (PPM * s_cam.z) - 1.6f;
    s_cam.y = (s_tsy - SH * 0.5f) / (PPM * s_cam.z);
    s_shx = s_shy = 0;
    bg_draw();
    props_draw();
    tfd_screen();
    // the bears hop along the table, one after the other
    float t = s_title_t;
    for (int i = 0; i < 5; i++) {
        float ph = t * 1.3f + i * 0.9f, hop = fabsf(sinf(ph * 1.6f));
        float x = s_cam.x - SW * 0.5f / (PPM * s_cam.z) + 1.0f + i * 0.9f + fmodf(t * 0.25f, 0.9f) * 0;
        if (GS_RND) {
            x = s_cam.x + (i - 2) * 1.5f;  // (the round: in the middle of the circle)
        } else if (GS_BOX) {
            x = s_cam.x - 3.4f + i * 1.7f;
        } else {
            x = s_cam.x + (i - 2) * 1.6f + (i >= 2 ? 1.2f : -1.2f);
        }
        float r = BEAR[i].r * 1.25f, y = r + hop * 0.5f;
        bear_shadow(x, y, r);
        float sq = hop < 0.15f ? 0.25f * (1 - hop / 0.15f) : 0;
        bear_spr(i, x, y, r, sinf(ph) * 0.15f, 1 + sq, 1 - sq, 256, true);
    }
    // the name of the game
    int ty = GS_RND ? (int)(SH * 0.33f) : GS_BOX ? 70 : 52;
    if (s_lang) {
        txt(&KF_TITLE, SW / 2, ty + 6, TX(T_NAME), tfd_rgb(255, 214, 80), 1, SW - 20);
    } else {
        const char *nm = "GUMMY SIEGE";
        int w = mf_width(&MF_N34, nm, 2), x = SW / 2 - w / 2;
        static const uint8_t LC[5][3] = {
                {255, 60, 100}, {80, 220, 90}, {255, 214, 40}, {255, 150, 40}, {250, 246, 240}};
        for (int i = 0; nm[i]; i++) {
            char ch[2] = {nm[i], 0};
            float bob = sinf(t * 4 + i * 0.6f) * 2.5f;
            const uint8_t *c = LC[i % 5];
            mf_text_o(&MF_N34, x, ty + (int)bob, ch, tfd_rgb(c[0], c[1], c[2]), tfd_rgb(70, 16, 40), 2, 0);
            x += mf_width(&MF_N34, ch, 0) + 2;
        }
    }
    float pulse = 0.5f + 0.5f * sinf(t * 4);
    txt(GS_RND ? &KF_BIG : &KF_TEXT, SW / 2,
        ty + (GS_RND   ? (int)(SH * 0.10f)
              : GS_BOX ? 42
                       : 34),
        TX(T_PLAY), tfd_mix(tfd_rgb(255, 220, 200), tfd_rgb(255, 255, 255), (int)(pulse * 256)), 1, SW - 20);
}

// ================================================================ the level map
static int s_map_w;  // the world on the map
static int s_map_dn_x, s_map_dn_y;
static void map_layout(box_t *tile, box_t *prev, box_t *next)
{
    if (GS_RND) {
        // (the round layout: 5 x 2 tiles in the wide middle of the circle, the arrows low at the sides; a tile of 70 px =
        // 6.6 mm on the 1.75C)
        int tw = (int)(SW * 0.15f), th = tw, gx = (int)(SW * 0.02f), x0 = (SW - (5 * tw + 4 * gx)) / 2;
        int y0 = (int)(SH * 0.28f);
        for (int i = 0; i < 10; i++) {
            tile[i] = (box_t){x0 + (i % 5) * (tw + gx), y0 + (i / 5) * (th + gx), tw, th};
        }
        int aw = (int)(SW * 0.14f), ay = (int)(SH * 0.66f);
        *prev = (box_t){(int)(SW * 0.13f), ay, aw, aw};
        *next = (box_t){SW - (int)(SW * 0.13f) - aw, ay, aw, aw};
    } else if (!GS_BOX) {
        int tw = 64, th = 56, gx = 10, x0 = (SW - (5 * tw + 4 * gx)) / 2;
        for (int i = 0; i < 10; i++) {
            tile[i] = (box_t){x0 + (i % 5) * (tw + gx), 38 + (i / 5) * (th + 10), tw, th};
        }
        *prev = (box_t){10, 62, 56, 56};
        *next = (box_t){SW - 66, 62, 56, 56};
    } else if (GS_TAB5) {
        int tw = 80, th = 80, gx = 14, x0 = (SW - (5 * tw + 4 * gx)) / 2;
        for (int i = 0; i < 10; i++) {
            tile[i] = (box_t){x0 + (i % 5) * (tw + gx), 64 + (i / 5) * (th + 14), tw, th};
        }
        *prev = (box_t){10, 112, 64, 64};
        *next = (box_t){SW - 74, 112, 64, 64};
    } else {
        int tw = 54, th = 54, gx = 7, x0 = (SW - (5 * tw + 4 * gx)) / 2;
        for (int i = 0; i < 10; i++) {
            tile[i] = (box_t){x0 + (i % 5) * (tw + gx), 42 + (i / 5) * (th + 8), tw, th};
        }
        *prev = (box_t){8, 178, 56, 52};
        *next = (box_t){SW - 64, 178, 56, 52};
    }
    if (rtl()) {
        box_t t = *prev;
        *prev = *next;
        *next = t;
    }  // (Hebrew: the next world is at the left)
}
static void arrow(box_t b, bool right, bool on)
{
    tfd_rrect(b.x, b.y, b.w, b.h, 14, on ? tfd_rgb(255, 255, 255) : tfd_rgb(120, 100, 120), on ? 70 : 40);
    float cx = b.x + b.w * 0.5f, cy = b.y + b.h * 0.5f, d = right ? 1 : -1;
    float tri[6] = {cx + d * 9, cy, cx - d * 7, cy - 12, cx - d * 7, cy + 12};
    tfd_poly(tri, 3, on ? tfd_rgb(255, 255, 255) : tfd_rgb(150, 130, 150), 256);
}
static void map_draw(void)
{
    s_dim = true;
    s_world = s_map_w;
    s_cam.z = GS_RND ? GS_RS : 1.0f;
    s_lift = 0;
    s_cam.x = 6 + s_map_w * 3;
    s_cam.y = (s_tsy - SH * 0.5f) / (PPM * s_cam.z);
    s_shx = s_shy = 0;
    bg_draw();
    s_dim = false;
    tfd_screen();
    box_t tile[10], pv, nx;
    map_layout(tile, &pv, &nx);
    int first, n = levels_of_world(s_map_w, &first);
    char b[40];
    // the name of the world and its stars
    int hy = GS_RND ? (int)(SH * 0.22f) : GS_BOX ? 26 : 24;
    snprintf(b, sizeof b, "%d/%d", stars_of_world(s_map_w), n * 3);
    if (GS_RND) {
        // (the round layout: the stars over the tiles, the name of the world between the arrows)
        txt(&KF_BIG, SW / 2, (int)(SH * 0.66f) + (int)(SW * 0.07f) + 9, TX(T_W0 + s_map_w), tfd_rgb(255, 255, 255), 1,
            (int)(SW * 0.44f));
        int w = mf_width(&MF_N20, b, 0);
        mf_text_o(&MF_N20, SW / 2 - w / 2 + 10, hy, b, tfd_rgb(255, 230, 140), tfd_rgb(50, 10, 30), 1, 0);
        star5(SW / 2 - w / 2 - 8, hy - 8, 9, 0, tfd_rgb(255, 214, 60), 256);
    } else if (GS_BOX) {
        txt(&KF_TEXT, SW / 2, GS_TAB5 ? 284 : 214, TX(T_W0 + s_map_w), tfd_rgb(255, 255, 255), 1, SW - 140);
        int w = mf_width(&MF_SMALL, b, 0);
        mf_text_o(&MF_SMALL, SW / 2 - w / 2 + 8, hy, b, tfd_rgb(255, 230, 140), tfd_rgb(50, 10, 30), 1, 0);
        star5(SW / 2 - w / 2 - 6, hy - 5, 6, 0, tfd_rgb(255, 214, 60), 256);
    } else {
        int x = rtl() ? SW - 12 : 12;
        txt(&KF_TEXT, x, hy, TX(T_W0 + s_map_w), tfd_rgb(255, 255, 255), rtl() ? 2 : 0, 220);
        int w = mf_width(&MF_SMALL, b, 0), sx = rtl() ? 22 : SW - 12 - w;
        mf_text_o(&MF_SMALL, sx, hy, b, tfd_rgb(255, 230, 140), tfd_rgb(50, 10, 30), 1, 0);
        star5(sx - 10, hy - 5, 6, 0, tfd_rgb(255, 214, 60), 256);
    }
    for (int i = 0; i < 10 && i < n; i++) {
        int li = first + i;
        box_t t = tile[rtl() ? (i / 5) * 5 + 4 - i % 5 : i];
        bool open = level_open(li);
        uint16_t c = open ? wcol(s_map_w, 230) : tfd_rgb(70, 60, 80);
        tfd_rrect(t.x, t.y + 3, t.w, t.h, 12, tfd_rgb(20, 4, 20), 150);
        tfd_rrect(t.x, t.y, t.w, t.h, 12, c, 256);
        tfd_rrect(t.x + 4, t.y + 3, t.w - 8, t.h / 3, 8, tfd_rgb(255, 255, 255), open ? 50 : 20);
        if (open) {
            snprintf(b, sizeof b, "%d", i + 1);
            mf_text_oc(&MF_N20, t.x + t.w / 2, t.y + (GS_TAB5 ? 44 : 30), b, tfd_rgb(255, 255, 255),
                       tfd_rgb(60, 16, 40), 1, 0);
            for (int k = 0; k < 3; k++) {
                float sx = t.x + t.w / 2 + (k - 1) * 13, sy = t.y + t.h - 11;
                star5(sx, sy, 5.5f, 0, tfd_rgb(50, 16, 40), 200);
                if (k < s_stars[li]) {
                    star5(sx, sy, 5, 0, tfd_rgb(255, 214, 60), 256);
                }
            }
        } else {
            // a lock
            float cx = t.x + t.w * 0.5f, cy = t.y + t.h * 0.5f + 3;
            tfd_ring(cx, cy - 7, 6, 2.5f, tfd_rgb(170, 150, 170), 256);
            tfd_rrect((int)cx - 9, (int)cy - 5, 18, 14, 3, tfd_rgb(170, 150, 170), 256);
        }
    }
    arrow(pv, rtl(), s_map_w > 0);
    arrow(nx, !rtl(), s_map_w < 2);
}
