// DEEP CURRENT: the screens over the sea (included by app_deep.c): the icons of the traits, the HUD (the depth, the
// gut and the BOOT organ), the three cards of a growth, the title with the codex of the traits, the summary of a dive
// and the menu icon.

// ================================================================ the icons of the traits
// a small picture of each trait in its colour, in a box of s px round (cx, cy); k = the light (a locked trait: dim)
static void trait_icon(int t, float cx, float cy, float s, float k)
{
    const uint8_t *c = TRAIT[t].c;
    float u = s / 40.0f, an = s_time;
    int r = (int)(c[0] * k), g = (int)(c[1] * k), b = (int)(c[2] * k);
    switch (t) {
    case T_TAIL:
        for (int i = 0; i < 4; i++) {
            float x = cx - 12 * u + i * 7.5f * u, y = cy + sinf(an * 4 - i * 0.8f) * 2 * u, rr = (7 - i * 1.3f) * u;
            ell_add(x, y, 1, 0, rr * 1.3f, rr, r, g, b, 0.35f);
            glow(x, y, rr, r, g, b, 0.25f);
        }
        {
            float tx = cx + 14 * u, ty = cy + sinf(an * 4 - 3.2f) * 2 * u, f = sinf(an * 4) * 3 * u;
            float p[8] = {tx - 2 * u, ty, tx + 9 * u, ty - 8 * u + f, tx + 5 * u, ty, tx + 9 * u, ty + 8 * u + f};
            poly_add(p, 4, r, g, b, 120);
        }
        break;
    case T_FIN: {
        ell_add(cx - 4 * u, cy, 1, 0, 10 * u, 7 * u, r, g, b, 0.3f);
        float fl = sinf(an * 5) * 2 * u;
        for (int sd = -1; sd <= 1; sd += 2) {
            float p[2 * 10];
            p[0] = cx - 2 * u;
            p[1] = cy + sd * 5 * u;
            int m = quad_pts(p, 1, p[0], p[1], cx + 6 * u, cy + sd * (16 * u + fl), cx - 14 * u,
                             cy + sd * (14 * u + fl), 6);
            p[2 * m] = cx - 8 * u;
            p[2 * m + 1] = cy + sd * 4 * u;
            m++;
            poly_add(p, m, r, g, b, 90);
            poly_line_add(p, m, r * 3 / 4, g * 3 / 4, b * 3 / 4);
        }
        break;
    }
    case T_JAWS: {
        float o = (0.4f + 0.3f * fmaxf(0, sinf(an * 3))) * 7 * u;
        for (int sd = -1; sd <= 1; sd += 2) {
            float p[2 * 12];
            p[0] = cx - 10 * u;
            p[1] = cy + sd * 2 * u;
            int m = quad_pts(p, 1, p[0], p[1], cx + 2 * u, cy + sd * (9 * u + o), cx + 12 * u,
                             cy + sd * (2 * u + o * 0.4f), 5);
            m = quad_pts(p, m, cx + 12 * u, cy + sd * (2 * u + o * 0.4f), cx, cy + sd * (4 * u + o * 0.5f), cx - 8 * u,
                         cy, 4);
            poly_add(p, m, r, g, b, 200);
            glow(cx + 12 * u, cy + sd * (2 * u + o * 0.4f), 4 * u, r, g, b, 0.6f);
        }
        break;
    }
    case T_SPINE:
        ell_add(cx, cy + 5 * u, 1, 0, 13 * u, 7 * u, r, g, b, 0.3f);
        for (int i = -1; i <= 1; i++) {
            float bx = cx + i * 8 * u, by = cy + 1 * u, tx = bx - 4 * u, ty = by - 13 * u;
            line_add(bx, by, tx, ty, r, g, b);
            glow(tx, ty, 4 * u, r, g, b, 0.8f);
        }
        break;
    case T_SHELL:
        ell_add(cx, cy + 4 * u, 1, 0, 13 * u, 7 * u, 99, 234, 255, 0.25f * k);
        for (int i = 0; i <= 12; i++) {
            float a = 3.14159f * (0.1f + 0.8f * i / 12), x = cx + cosf(a) * 15 * u, y = cy + 4 * u - sinf(a) * 12 * u;
            glow(x, y, 2.6f * u, r, g, b, 0.7f);
        }
        break;
    case T_STING: {
        float p[2 * 12];
        p[0] = cx - 10 * u;
        p[1] = cy - 2 * u;
        int m = quad_pts(p, 1, p[0], p[1], cx, cy - 18 * u, cx + 10 * u, cy - 2 * u, 8);
        poly_add(p, m, r, g, b, 90);
        for (int i = 0; i < 4; i++) {
            float x0 = cx - 7 * u + i * 4.6f * u, y0 = cy - 2 * u, px0 = x0, py0 = y0;
            for (int j = 1; j <= 5; j++) {
                float px = x0 + sinf(an * 3 - j * 0.9f + i) * 1.6f * u, py = y0 + j * 3.4f * u;
                line_add(px0, py0, px, py, r * 3 / 4, g * 3 / 4, b * 3 / 4);
                px0 = px;
                py0 = py;
            }
        }
        break;
    }
    case T_LURE: {
        float bob = sinf(an * 2.4f) * 2 * u, px0 = cx - 10 * u, py0 = cy + 12 * u;
        for (int q = 1; q <= 8; q++) {
            float tt = q / 8.0f, w = 1 - tt;
            float px = w * w * (cx - 10 * u) + 2 * w * tt * (cx - 6 * u) + tt * tt * (cx + 8 * u);
            float py = w * w * (cy + 12 * u) + 2 * w * tt * (cy - 16 * u) + tt * tt * (cy - 6 * u + bob);
            line_add(px0, py0, px, py, 150 * k, 190 * k, 170 * k);
            px0 = px;
            py0 = py;
        }
        float lp = 0.8f + 0.2f * sinf(an * 5);
        glow(cx + 8 * u, cy - 6 * u + bob, 13 * u * lp, r, g, b, 0.6f);
        glow(cx + 8 * u, cy - 6 * u + bob, 4 * u, (int)(240 * k), (int)(255 * k), (int)(230 * k), 0.95f);
        break;
    }
    case T_INK:
        for (int i = 0; i < 3; i++) {
            float x = cx + (i - 1) * 8 * u, y = cy + (i == 1 ? -4 : 3) * u + sinf(an * 2 + i) * u,
                  rr = (9 - (i == 1) * 0) * u;
            disc_mix(x, y, rr, 40, 6, 18, 0.7f * k);
            glow(x, y, rr * 1.1f, r, g, b, 0.35f);
        }
        break;
    case T_JET: {
        ell_add(cx + 6 * u, cy, 1, 0, 9 * u, 7 * u, 99, 234, 255, 0.3f * k);
        float f = 0.8f + 0.2f * sinf(an * 12);
        float p[6] = {cx - 2 * u, cy - 4 * u, cx - 2 * u, cy + 4 * u, cx - 18 * u * f, cy};
        poly_add(p, 3, r, g, b, 170);
        glow(cx - 4 * u, cy, 7 * u, r, g, b, 0.7f);
        break;
    }
    case T_GLASS:
        ell_add(cx, cy, 1, 0, 14 * u, 10 * u, r, g, b, 0.18f);
        for (int i = 0; i <= 30; i++) {
            float a = i / 30.0f * 6.2832f;
            dot_add(cx + cosf(a) * 14 * u, cy + sinf(a) * 10 * u, r * 3 / 4, g * 3 / 4, b * 3 / 4);
        }
        for (int i = 0; i < 3; i++) {
            float tw = 0.5f + 0.5f * sinf(an * 3 + i * 2.1f);
            glow(cx + (i - 1) * 7 * u, cy - (i & 1) * 4 * u, 3 * u, 255, 255, 255, 0.7f * tw * k);
        }
        break;
    default:
        for (int i = 0; i < 9; i++) {
            dot_add(cx + sinf(i * 2.3f) * 10 * u, cy + cosf(i * 1.7f) * 8 * u, r, g, b);
        }
        break;
    }
}
// a small lock (a locked trait)
static void lock_mark(float cx, float cy, float u)
{
    box_mix((int)(cx - 4 * u), (int)(cy - 1 * u), (int)(8 * u), (int)(6 * u), 150, 170, 190, 200);
    for (int i = 0; i <= 10; i++) {
        float a = 3.14159f * i / 10;
        dot_mix(cx + cosf(a) * 2.6f * u, cy - 1 * u - sinf(a) * 3.2f * u, 150, 170, 190, 220);
    }
}

// ================================================================ messages over the play
static char s_msg[48];
static float s_msg_t, s_msg_len;  // the time left of the message, its whole time
static uint8_t s_msg_c[3];
static void msg_show(const char *t, const uint8_t *c, float secs)
{
    snprintf(s_msg, sizeof s_msg, "%s", t);
    s_msg_t = secs;
    s_msg_len = secs;
    s_msg_c[0] = c[0];
    s_msg_c[1] = c[1];
    s_msg_c[2] = c[2];
}
static float s_banner_t;  // the name of a new zone (big, in the middle)

// ================================================================ the HUD of the play
static const uint16_t RING = 0x0000;
// the panel of the growth: the bottom right of a wide frame (432 x 120 on the 3.49; the Tab5 640 x 360: 432 x 308);
// the bottom middle of a 320 x 240 frame (the old fixed x of 432 put it out of that frame: no gut, no organ)
#define GROW_PW 166
#define GROW_PH 46
static int grow_px(void)
{
    return s_fw >= 600 ? s_fw - 208 : (s_fw - GROW_PW) / 2;
}
static int grow_py(void)
{
    return s_fh - (RND ? 72 : 52);  // (the round frame: above the bottom of the circle)
}
// the middle of the ring of the BOOT organ (the tap target)
static int organ_cx(void)
{
    return grow_px() + GROW_PW - 22;
}
static int organ_cy(void)
{
    return grow_py() + 23;
}
static void hud_draw(float ui_a)
{
    char b[64], n[24];
    // the zone and the depth (the look page: the top left)
    int m = (int)(Z->m0 + clampf(B.y[0] / Z->h, 0, 1) * (Z->m1 - Z->m0));
    fmt_k(n, sizeof n, m);
    snprintf(b, sizeof b, "%s %s %s", TX(X_DEPTH), n, TX(X_M));
    // (one dark plate under the two lines: a ring of 4 copies of each text cost 5 text draws)
    int tw = ui_text_w(&KF_LABEL, Z->name[s_lang]), tw2 = ui_text_w(&KF_LABEL, b), zw = (tw > tw2 ? tw : tw2) + 12;
    const int zx = RND ? s_fw / 2 - zw / 2 : 6, zy = RND ? 24 : 5;  // (the round frame: the middle of the top cap)
    rbox_mix(zx, zy, zw, 36, 6, 2, 8, 18, 150);
    text_l(&KF_LABEL, zx + 6, zy + 13, Z->name[s_lang], rgbc(200, 245, 255));
    text_l(&KF_LABEL, zx + 6, zy + 30, b, rgbc(99, 234, 255));
    // the panel of the growth (the look page: the bottom right): the count, the gut lights, the BOOT organ
    const int px = grow_px(), py = grow_py(), pw = GROW_PW, ph = GROW_PH;
    rbox_mix(px, py, pw, ph, 6, 2, 10, 22, (int)(140 * ui_a));
    rbox_line_add(px, py, pw, ph, 6, 24, 58, 64);
    snprintf(b, sizeof b, "%s %d", TX(X_GROWTH), s_steps);
    text_l(&KF_LABEL, px + 10, py + 17, b, rgbc(200, 245, 255));
    // the gut: 8 lights; each takes the colour of the food that filled it
    float per = s_need / 8;
    for (int k = 0; k < 8; k++) {
        int x = px + 10 + k * 15, y = py + 26;
        float f0 = k * per, fill = clampf((s_food - f0) / per, 0, 1);
        // (the colour: the item of the gut that holds the middle of this light)
        float acc = 0, mid = f0 + per * 0.5f;
        int t = T_NONE;
        for (int i = 0; i < s_ngut; i++) {
            acc += s_gut_f[i];
            if (acc >= mid) {
                t = s_gut_t[i];
                break;
            }
            if (i == s_ngut - 1) {
                t = s_gut_t[i];
            }
        }
        const uint8_t *c = TRAIT[t].c;
        rbox_mix(x, y, 12, 12, 3, 20, 40, 56, 200);
        if (fill > 0) {
            int h = (int)(12 * fill + 0.5f);
            rbox_mix(x, y + 12 - h, 12, h, 3, c[0], c[1], c[2], (int)(220 * fminf(1, fill + 0.3f)));
            if (fill >= 1) {
                glow(x + 6, y + 6, 8, c[0], c[1], c[2], 0.25f);
            }
        }
    }
    // the BOOT organ: PING, FLASH (the lure) or INK, with the ring of its charge
    {
        float cx = organ_cx(), cy = organ_cy();
        int t = B.cnt[T_INK] ? T_INK : B.lure ? T_LURE : T_NONE;
        float cd = B.boot_cd, full = B.cnt[T_INK] ? 7.0f : B.lure ? 6.0f : 5.0f, f = clampf(1 - cd / full, 0, 1);
        const uint8_t *c = t == T_NONE ? (const uint8_t *)"\x63\xea\xff" : TRAIT[t].c;
        for (int i = 0; i <= 40; i++) {
            float a = -1.5708f + i / 40.0f * 6.2832f;
            bool on = i / 40.0f <= f;
            dot_add(cx + cosf(a) * 15, cy + sinf(a) * 15, on ? c[0] * 3 / 4 : 30, on ? c[1] * 3 / 4 : 40,
                    on ? c[2] * 3 / 4 : 50);
        }
        if (t == T_NONE) {
            // the PING: a ring
            float pr = 4 + 5 * fmodf(s_time, 1.2f) / 1.2f;
            for (int i = 0; i <= 24; i++) {
                float a = i / 24.0f * 6.2832f;
                dot_add(cx + cosf(a) * pr, cy + sinf(a) * pr, (int)(99 * f), (int)(234 * f), (int)(255 * f));
            }
            glow(cx, cy, 3, 232, 251, 255, 0.8f * f + 0.1f);
        } else {
            trait_icon(t, cx, cy, 22, 0.35f + 0.65f * f);
        }
    }
    // a message (a new part, the current is open)
    if (s_msg_t > 0) {
        float a = fminf(1, s_msg_t * 3) * fminf(1, (s_msg_len - s_msg_t) * 6 + 0.3f);
        a = clampf(a, 0, 1);
        text_o(&KF_TEXT, s_fw / 2, RND ? 82 : 46, s_msg,
               rgbc((int)(s_msg_c[0] * a), (int)(s_msg_c[1] * a), (int)(s_msg_c[2] * a)), rgbc(2, 8, 18));
    }
    // the name of a new zone
    if (s_banner_t > 0) {
        float a = clampf(fminf(s_banner_t, 3.4f - s_banner_t) * 1.6f, 0, 1);
        uint16_t c = rgbc((int)(232 * a), (int)(251 * a), (int)(255 * a)),
                 c2 = rgbc((int)(99 * a), (int)(234 * a), (int)(255 * a));
        text_o(&KF_TITLE, s_fw / 2, 80, Z->name[s_lang], c, rgbc(2, 8, 18));
        char a0[16], a1[16];
        fmt_k(a0, sizeof a0, Z->m0);
        fmt_k(a1, sizeof a1, Z->m1);
        snprintf(b, sizeof b, "%s - %s %s", a0, a1, TX(X_M));
        text_o(&KF_LABEL, s_fw / 2, 100, b, c2, rgbc(2, 8, 18));
    }
    // the deep current: a mark at the edge when it is out of the view
    if (s_cur_on && s_state == ST_PLAY) {
        float sx = sx_of(s_cur_x), sy = sy_of(Z->h * 0.62f);
        float p = 0.6f + 0.4f * sinf(s_time * 4);
        if (sx < 30 || sx > s_fw - 30) {
            bool right = sx > s_fw / 2;
            float ax = right ? s_fw - 40 : 40, ay = clampf(sy, 70, s_fh - 70);
            glow(ax, ay, 18, 60, 170, 255, 0.4f * p);
            float d = right ? 1 : -1;
            float tri[6] = {ax + d * 12, ay, ax - d * 2, ay - 9, ax - d * 2, ay + 9};
            poly_add(tri, 3, 120, 230, 255, (int)(230 * p));
            text_o(&KF_LABEL, (int)(ax - d * 6), (int)ay - 14, TX(X_CURRENT), rgbc(120, 230, 255), rgbc(2, 8, 18));
        } else {
            float y0 = clampf(sy_of(Z->h * 0.5f) - 18, 68, s_fh - 30);
            text_o(&KF_LABEL, (int)sx, (int)y0, TX(X_CURRENT), rgbc((int)(120 * p), (int)(230 * p), (int)(255 * p)),
                   rgbc(2, 8, 18));
            float tri[6] = {sx, y0 + 16, sx - 7, y0 + 6, sx + 7, y0 + 6};
            poly_add(tri, 3, 120, 230, 255, (int)(230 * p));
        }
    }
}

// ================================================================ the cards of a growth
// the cards: 176 x 126 on the 3.49; 98 x 150 on a narrow frame (320 px)
#define NARROW (s_fw < 400)
// the Tab5 (640 x 360): the pages of the 3.49 (172 rows) go in the middle rows (0 on the other boards)
#define TALL_OY (s_fw >= 600 && s_fh >= 300 ? (s_fh - SH) / 2 : 0)
#define CARD_W  (RND ? 90 : NARROW ? 98 : 176)  // (the round frame: the three cards inside the circle)
#define CARD_H  (NARROW ? 150 : 126)
#define CARD_Y  (NARROW ? (s_fh - CARD_H) / 2 : 30 + TALL_OY)
#define CARD_G  (RND ? 4 : NARROW ? 6 : 20)
static int card_x(int k)
{
    return (s_fw - (3 * CARD_W + 2 * CARD_G)) / 2 + k * (CARD_W + CARD_G);
}
static uint8_t s_card[3];
static bool s_card_wild[3];
static int s_pick = -1;
static float s_pick_t;
static int card_at(int x, int y)
{
    for (int k = 0; k < 3; k++) {
        int cx = card_x(k);
        if (x >= cx - 6 && x < cx + CARD_W + 6 && y >= CARD_Y - 8 && y < CARD_Y + CARD_H + 10) {
            return k;
        }
    }
    return -1;
}
static void cards_draw(float t)
{
    float in = ease_out(t / 0.35f);
    for (int k = 0; k < 3; k++) {
        int tr = s_card[k];
        const uint8_t *c = TRAIT[tr].c;
        int x = card_x(k), y = CARD_Y + (int)((1 - in) * 30);
        float a = in;
        if (s_pick >= 0) {
            a = k == s_pick ? 1 : clampf(1 - s_pick_t * 4, 0, 1);
        }
        if (a <= 0.01f) {
            continue;
        }
        rbox_mix(x, y, CARD_W, CARD_H, 10, 3, 12, 26, (int)(205 * a));
        rbox_line_add(x, y, CARD_W, CARD_H, 10, (int)(c[0] * 0.5f * a), (int)(c[1] * 0.5f * a), (int)(c[2] * 0.5f * a));
        if (s_pick == k) {
            float f = clampf(1 - s_pick_t * 2.5f, 0, 1);
            rbox_line_add(x + 1, y + 1, CARD_W - 2, CARD_H - 2, 9, (int)(c[0] * f), (int)(c[1] * f), (int)(c[2] * f));
            glow(x + CARD_W / 2, y + 40, 60, c[0], c[1], c[2], 0.25f * f);
        }
        glow(x + CARD_W / 2, y + 40, 34, c[0], c[1], c[2], 0.16f * a);
        trait_icon(tr, x + CARD_W / 2, y + 40, 46, a);
        uint16_t tc = rgbc((int)(232 * a), (int)(251 * a), (int)(255 * a)),
                 dc = rgbc((int)(c[0] * a), (int)(c[1] * a), (int)(c[2] * a));
        const char *nm = TNAME(tr);
        const kfont_t *f = ui_text_w(&KF_BIG, nm) <= CARD_W - 16   ? &KF_BIG
                           : ui_text_w(&KF_TEXT, nm) <= CARD_W - 8 ? &KF_TEXT
                                                                   : &KF_LABEL;
        ui_text_c(f, x + CARD_W / 2, y + 88, nm, tc);
        const char *d = TRAIT[tr].does[s_lang];
        if (ui_text_w(&KF_LABEL, d) <= CARD_W - 8) {
            ui_text_c(&KF_LABEL, x + CARD_W / 2, y + 110, d, dc);
        } else {
            // (a narrow card: two lines, cut at the space nearest to the middle)
            char l1[64], l2[64];
            int n = (int)strlen(d), cut = -1;
            for (int i = 1; i < n; i++) {
                if (d[i] == ' ' && (cut < 0 || abs(i - n / 2) < abs(cut - n / 2))) {
                    cut = i;
                }
            }
            if (cut < 0) {
                cut = n / 2;
            }
            snprintf(l1, sizeof l1, "%.*s", cut, d);
            snprintf(l2, sizeof l2, "%s", d + cut + (d[cut] == ' '));
            ui_text_c(&KF_LABEL, x + CARD_W / 2, y + 112, l1, dc);
            ui_text_c(&KF_LABEL, x + CARD_W / 2, y + 128, l2, dc);
        }
        if (s_card_wild[k]) {
            int w = ui_text_w(&KF_LABEL, TX(X_WILD)) + 12;
            rbox_mix(x + 8, y + 8, w, 18, 6, 80, 40, 90, (int)(200 * a));
            ui_text_c(&KF_LABEL, x + 8 + w / 2, y + 22, TX(X_WILD),
                      rgbc((int)(230 * a), (int)(190 * a), (int)(255 * a)));
        }
    }
}

// ================================================================ the title: the name, the best dive and the codex
static uint32_t s_seen;  // the traits that the creature ever swallowed (saved)
static int s_best_m, s_dives;
static const struct {
    uint8_t t, hint;
} UNLOCKS[4] = {{T_JET, X_H_TWI}, {T_LURE, X_H_MID}, {T_GLASS, X_H_EAT}, {T_INK, X_H_ABY}};
static void title_draw(void)
{
    float p = 0.8f + 0.2f * sinf(s_time * 3);        // (a soft pulse: a deep pulse made dark blue text on the glow)
    const int oy = RND ? 30 : NARROW ? 6 : TALL_OY;  // (a narrow frame: a little lower; the codex in two rows)
    glow(s_fw / 2, 52 + oy, 120, 30, 120, 160, 0.20f);
    text_o(RND ? &KF_BIG : &KF_TITLE, s_fw / 2, 60 + oy, TX(X_TITLE), rgbc(232, 251, 255), rgbc(2, 10, 24));
    text_o(&KF_TEXT, s_fw / 2, 88 + oy, TX(X_TAP), rgbc((int)(99 * p + 60), (int)(234 * p + 10), 255), rgbc(2, 10, 24));
    char b[64], n[24];
    if (s_best_m > 0) {
        fmt_k(n, sizeof n, s_best_m);
        snprintf(b, sizeof b, "%s %s %s", TX(X_BEST), n, TX(X_M));
        text_o(&KF_LABEL, s_fw / 2, 108 + oy, b, rgbc(200, 245, 255), rgbc(2, 10, 24));
    }
    // the codex: the ten traits (lit: in the sea; dim with a lock: not yet)
    const int pitch = 44, x0 = s_fw / 2 - (T_N - 1) * pitch / 2 + pitch / 2 - pitch / 2;
    for (int t = 1; t < T_N; t++) {
        float cx = x0 + (t - 1) * pitch + pitch / 2 - (T_N - 1) * 0, cy = 134 + TALL_OY;
        cx = s_fw / 2 + (t - 5.5f) * pitch;
        if (NARROW) {
            int k = (t - 1) % 5;
            cx = s_fw / 2 + (k - 2) * pitch;
            cy = (t <= 5 ? 140 : 178) + oy;
        }
        bool on = s_unl >> t & 1;
        rbox_mix((int)cx - 19, (int)cy - 16, 38, 32, 7, 2, 10, 22, 150);
        trait_icon(t, cx, cy, 26, on ? 1.0f : 0.22f);
        if (!on) {
            lock_mark(cx + 11, cy + 9, 1.1f);
        } else if (s_seen >> t & 1) {
            dot_add(cx + 14, cy - 11, 232, 251, 255);
        }
    }
    // the next trait to unlock: how
    for (int i = 0; i < 4; i++) {
        if (s_unl >> UNLOCKS[i].t & 1) {
            continue;
        }
        snprintf(b, sizeof b, "%s: %s", TXT[UNLOCKS[i].hint][s_lang], TNAME(UNLOCKS[i].t));
        text_o(&KF_LABEL, s_fw / 2, NARROW ? 220 + oy : 166 + oy, b, rgbc(150, 190, 210), rgbc(2, 10, 24));
        break;
    }
    (void)x0;
}

// ================================================================ the summary of a dive
static int s_sum_m;  // the depth of the dive (m)
static bool s_sum_won, s_sum_best;
static uint32_t s_sum_unl;  // the traits that this dive unlocked
static float s_run_t;       // the time of the dive (s)
static body_t s_portrait;   // the creature at its end (straight, for the picture)
static void summary_draw(float t)
{
    float a = clampf(t * 3, 0, 1);
    rbox_mix(24, 10, s_fw - 48, s_fh - 20, 14, 2, 8, 20, (int)(200 * a));
    rbox_line_add(24, 10, s_fw - 48, s_fh - 20, 14, 30, 80, 100);
    // the creature (it turns slowly in the left part)
    {
        cam_t keep = C;
        float kx = s_shx, ky = s_shy;
        body_t *P = &s_portrait;
        float L = fmaxf(P->len, 20);
        // (the whole creature in the box of 250 px: the jaws and the tail fluke too; the middle of head and tail)
        C.z = clampf(190.0f / (L + 40), 0.5f, 3.0f);
        C.x = (P->x[0] + P->x[P->n]) * 0.5f;
        C.y = P->y[P->n / 2];
        s_shx = (150 - s_fw * 0.5f);
        s_shy = (86 + TALL_OY - s_fh * 0.5f) + 4;
        if (NARROW) {
            C.z = clampf(150.0f / (L + 40), 0.4f, 2.4f);
            s_shx = 0;
            s_shy = 64 - s_fh * 0.5f;
        }  // (over the texts)
        if (NARROW) {
            clip_box(30, 14, s_fw - 30, 112);
        } else {
            clip_box(30, 14, 280, s_fh - 14);
        }
        body_draw(P, 0.8f, a);
        clip_full();
        C = keep;
        s_shx = kx;
        s_shy = ky;
    }
    char b[80], n[24];
    int xr = NARROW ? s_fw / 2 : 440, oy = NARROW ? 86 : TALL_OY;  // (a narrow frame: the texts under the creature)
    uint16_t ring = rgbc(2, 8, 20);
    if (s_sum_won) {
        text_o(ui_text_w(&KF_BIG, TX(X_DEEP)) <= s_fw - 60 ? &KF_BIG : &KF_TEXT, xr, 44 + oy, TX(X_DEEP),
               rgbc(157, 255, 200), ring);
    } else {
        const char *who = s_cause == SP_N ? TX(X_RIVAL) : s_cause >= 0 ? SPEC[s_cause].name[s_lang] : "";
        snprintf(b, sizeof b, "%s %s", TX(X_CAUGHT), who);
        const kfont_t *f = ui_text_w(&KF_BIG, b) <= (NARROW ? s_fw - 60 : 300) ? &KF_BIG : &KF_TEXT;
        text_o(f, xr, 44 + oy, b, rgbc(255, 160, 170), ring);
    }
    fmt_k(n, sizeof n, s_sum_m);
    snprintf(b, sizeof b, "%s %s %s", TX(X_DEPTH), n, TX(X_M));
    text_o(&KF_BIG, xr, 76 + oy - (NARROW ? 6 : 0), b, rgbc(99, 234, 255), ring);
    int mm = (int)s_run_t / 60, ss = (int)s_run_t % 60;
    snprintf(b, sizeof b, "%s %d   %s %d   %s %d:%02d", TX(X_SIZE), s_portrait.n + 1, TX(X_EATEN), s_eaten, TX(X_TIME),
             mm, ss);
    text_o(&KF_LABEL, xr, 100 + oy - (NARROW ? 12 : 0), b, rgbc(200, 245, 255), ring);
    int y = 120 + oy - (NARROW ? 16 : 0);
    const int ls = NARROW ? 18 : 20;  // (the step of the lines)
    if (s_sum_best) {
        text_o(&KF_TEXT, xr, y, TX(X_NEWBEST), rgbc(255, 220, 120), ring);
        y += ls;
    }
    for (int i = 0; i < 4; i++) {
        int tr = UNLOCKS[i].t;
        if (!(s_sum_unl >> tr & 1)) {
            continue;
        }
        snprintf(b, sizeof b, "%s: %s", TX(X_UNLOCK), TNAME(tr));
        int w = ui_text_w(&KF_TEXT, b);
        trait_icon(tr, xr - w / 2 - 18, y - 6, 24, 1);
        text_o(&KF_TEXT, xr + 6, y, b, rgbc(TRAIT[tr].c[0], TRAIT[tr].c[1], TRAIT[tr].c[2]), ring);
        y += ls;
        break;
    }
    if (t > 1.5f && y < 150 + oy) {
        float p = 0.8f + 0.2f * sinf(s_time * 3);
        text_o(&KF_LABEL, xr, NARROW ? s_fh - (RND ? 42 : 13) : 154 + oy, TX(X_AGAIN),
               rgbc((int)(200 * p), (int)(245 * p), 255), ring);
    } else if (t > 1.5f) {
        float p = 0.8f + 0.2f * sinf(s_time * 3);
        text_o(&KF_LABEL, xr, NARROW ? s_fh - (RND ? 42 : 13) : 158 + oy, TX(X_AGAIN),
               rgbc((int)(200 * p), (int)(245 * p), 255), ring);
    }
}

#include "tfdraw/tfdraw.h"
// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set is in each app; all are static and unused, so they give no warning)
#define IC_FN static __attribute__((unused))
static struct {
    uint16_t *fb;
    int w, h, c[4];
} ic_keep __attribute__((unused));
// start an icon: tfdraw draws on the screen and only in the box; ic_end gives tfdraw its old target and clip back
IC_FN void ic_begin(int x, int y, int s)
{
    ic_keep.fb = tfd_fb;
    ic_keep.w = tfd_w;
    ic_keep.h = tfd_h;
    ic_keep.c[0] = tfd_cx0;
    ic_keep.c[1] = tfd_cy0;
    ic_keep.c[2] = tfd_cx1;
    ic_keep.c[3] = tfd_cy1;
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
}
IC_FN void ic_end(void)
{
    tfd_target(ic_keep.fb, ic_keep.w, ic_keep.h);
    tfd_clip(ic_keep.c[0], ic_keep.c[1], ic_keep.c[2], ic_keep.c[3]);
}
// the rows j0 .. j1 - 1 of the round tile (the corner radius of the menu frame), a gradient from c0 to c1
IC_FN void ic_band(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    int r = s / 8 + 2;
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        uint16_t c = tfd_mix(c0, c1, j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        if (dy <= 0) {
            tfd_hspan(x, x + s, y + j, c, 256);
            continue;
        }
        int in = tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy)));
        tfd_hspan(x + in + 1, x + s - in - 1, y + j, c, 256);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, y + j, c, a);
            tfd_px(x + s - 1 - k, y + j, c, a);
        }
    }
}
// the box inside the round corners (a shape in it does not cover a corner)
IC_FN void ic_inner(int x, int y, int s)
{
    int k = (s / 8 + 2) * 3 / 10 + 1;
    tfd_clip(x + k, y + k, x + s - k, y + s - k);
}
// a filled ellipse with a smooth edge
IC_FN void ic_oval(float cx, float cy, float rx, float ry, uint16_t c, int a)
{
    if (rx < 0.5f || ry < 0.5f) {
        return;
    }
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = fabsf(yy + 0.5f - cy), v = ry + 0.5f - d;
        if (v <= 0) {
            continue;
        }
        float q = d < ry ? d / ry : 1, hw = rx * sqrtf(1 - q * q), l = cx - hw, r = cx + hw;
        int aa = v >= 1 ? a : (int)(a * v);
        int il = tfd_ce(l), ir = tfd_fl(r);
        if (il > ir) {
            tfd_px(ir, yy, c, (int)(aa * (r - l)));
            continue;
        }
        tfd_hspan(il, ir, yy, c, aa);
        tfd_px(il - 1, yy, c, (int)(aa * (il - l)));
        tfd_px(ir, yy, c, (int)(aa * (r - ir)));
    }
}
// a ball in the light from the upper left: the dark rim, the body, the shine
IC_FN void ic_ball(float cx, float cy, float r, uint16_t dark, uint16_t body, uint16_t shine)
{
    tfd_disc(cx, cy, r, dark, 256);
    tfd_disc(cx - r * 0.1f, cy - r * 0.12f, r * 0.84f, body, 256);
    tfd_disc(cx - r * 0.34f, cy - r * 0.38f, r * 0.27f, shine, 200);
}
// a shape of n points (x, y pairs round 0, 0), turned by ang, k times bigger, at cx, cy
IC_FN void ic_shape(const float *p, int n, float cx, float cy, float k, float ang, uint16_t c, int a)
{
    float q[64], cs = fcos(ang) * k, sn = fsin(ang) * k;
    if (n > 32) {
        n = 32;
    }
    for (int i = 0; i < n; i++) {
        q[2 * i] = cx + p[2 * i] * cs - p[2 * i + 1] * sn;
        q[2 * i + 1] = cy + p[2 * i] * sn + p[2 * i + 1] * cs;
    }
    tfd_poly(q, n, c, a);
}
// A store app shows the frames of icon.bin, with at most 255 colours for all 16 frames: mk_app.py drops the low bits
// of each colour until they fit (most icons: 3 bits of red, 4 of green, 3 of blue). A colour on that grid stays as it
// is: ic_g gives the nearest one. ic_dband makes a gradient of grid colours with an ordered dither (few colours, no
// bands).
IC_FN uint16_t ic_g(int r, int g, int b)
{
    r = (r + 16) / 32 * 32;
    g = (g + 8) / 16 * 16;
    b = (b + 16) / 32 * 32;
    return rgb((uint8_t)(r > 224 ? 224 : r), (uint8_t)(g > 240 ? 240 : g), (uint8_t)(b > 224 ? 224 : b));
}
IC_FN void ic_dband(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    static const uint8_t IC_BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int r0, g0, b0, r1, g1, b1, r = s / 8 + 2;
    tfd_unrgb(c0, &r0, &g0, &b0);
    tfd_unrgb(c1, &r1, &g1, &b1);
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        int yy = y + j;
        if (yy < tfd_cy0 || yy >= tfd_cy1 || (unsigned)yy >= (unsigned)tfd_h) {
            continue;
        }
        // (the colour of the row in 1/16 steps of a colour value; a grid step is 32 = 512 for red and blue, 16 = 256
        // for green)
        int t = j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0;
        int fr = r0 * 16 + ((r1 - r0) * 16 * t >> 8), fg = g0 * 16 + ((g1 - g0) * 16 * t >> 8),
            fb = b0 * 16 + ((b1 - b0) * 16 * t >> 8);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        int in = dy > 0 ? tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy))) : -1;
        uint16_t *row = tfd_fb + yy * tfd_w;
        for (int i = in + 1; i < s - in - 1; i++) {
            int xx = x + i;
            if (xx < tfd_cx0 || xx >= tfd_cx1 || (unsigned)xx >= (unsigned)tfd_w) {
                continue;
            }
            int th = IC_BAYER[(j & 3) * 4 + (i & 3)];
            int lr = (fr >> 9) + (((fr >> 5) & 15) > th), lg = (fg >> 8) + (((fg >> 4) & 15) > th),
                lb = (fb >> 9) + (((fb >> 5) & 15) > th);
            row[xx] = rgb((uint8_t)(lr > 7 ? 224 : lr * 32), (uint8_t)(lg > 15 ? 240 : lg * 16),
                          (uint8_t)(lb > 7 ? 224 : lb * 32));
        }
        uint16_t c = ic_g(fr >> 4, fg >> 4, fb >> 4);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, yy, c, a);
            tfd_px(x + s - 1 - k, yy, c, a);
        }
    }
}
// a span of one row from l to r (in pixels, with soft ends)
IC_FN void ic_span(float l, float r, int yy, uint16_t c, int a)
{
    if (r <= l) {
        return;
    }
    int il = tfd_ce(l), ir = tfd_fl(r);
    if (il > ir) {
        tfd_px(ir, yy, c, (int)(a * (r - l)));
        return;
    }
    tfd_hspan(il, ir, yy, c, a);
    tfd_px(il - 1, yy, c, (int)(a * (il - l)));
    tfd_px(ir, yy, c, (int)(a * (r - ir)));
}
// the ring of an ellipse (rx, ry: the outer radii; w: its width), made of spans: cheap also when it is big
IC_FN void ic_ering(float cx, float cy, float rx, float ry, float w, uint16_t c, int a)
{
    float ix = rx - w, iy = ry - w;
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = yy + 0.5f - cy, qo = d / ry;
        if (qo * qo >= 1) {
            continue;
        }
        float ho = rx * sqrtf(1 - qo * qo), qi = iy > 0 ? d / iy : 2;
        if (qi * qi >= 1) {
            ic_span(cx - ho, cx + ho, yy, c, a);
            continue;
        }
        float hi = ix * sqrtf(1 - qi * qi);
        ic_span(cx - ho, cx - hi, yy, c, a);
        ic_span(cx + hi, cx + ho, yy, c, a);
    }
}
// ================================================================ the menu icon: the glowing creature swims in the deep
// Its body waves, its fins beat, the light of its lure pulses, small prey drift to its
// mouth and bubbles rise (a loop of 2 s)
static void icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f, an = ph * 6.2831853f;
    ic_dband(x, y, s, 0, s, ic_g(0, 80, 128), ic_g(0, 0, 32));
    // the light from the top: two pale rays
    ic_inner(x, y, s);
    float r1[] = {x + 18 * u, y + 0.0f, x + 30 * u, y + 0.0f, x + 12 * u, y + 70 * u, x + 2 * u, y + 70 * u};
    float r2[] = {x + 52 * u, y + 0.0f, x + 60 * u, y + 0.0f, x + 44 * u, y + 60 * u, x + 37 * u, y + 60 * u};
    tfd_poly(r1, 4, ic_g(64, 160, 192), 60);
    tfd_poly(r2, 4, ic_g(64, 160, 192), 45);
    tfd_clip(x, y, x + s, y + s);
    // the bubbles and the prey (small lights that drift to the mouth)
    for (int k = 0; k < 3; k++) {
        float t = fmodf(ph + k * 0.33f, 1.0f);
        tfd_ring(x + (20 + k * 27) * u + 2 * u * fsin(an + k), y + (92 - t * 80) * u, 1.6f * u, 0.8f * u,
                 ic_g(128, 224, 224), 256);
    }
    float hx = x + 70 * u, hy = y + 50 * u;
    for (int k = 0; k < 3; k++) {
        float t = fmodf(ph * 2 + k * 0.33f, 1.0f), px = hx + (34 - t * 26) * u,
              py = hy + (k - 1) * 9 * u * (1 - t) + 4 * u;
        if (t < 0.9f) {
            tfd_disc(px, py, 1.7f * u, ic_g(224, 224, 128), 256);
        }
    }
    // the body: segments along a wave that runs to the tail (the head at the right)
    float sx[7], sy[7], sr[7];
    for (int i = 0; i < 7; i++) {
        sx[i] = hx - i * 8.5f * u;
        sy[i] = hy + fsin(an * 2 - i * 0.7f) * (1.2f + i * 0.9f) * u;
        sr[i] = (i == 0 ? 10 : 12.5f - i * 1.5f) * u;
    }
    // (the tail fin, the fins on the back and under the belly, they beat)
    float tx = sx[6], ty = sy[6], tw = fsin(an * 2 - 4.6f) * 4 * u;
    float tf[] = {tx, ty, tx - 14 * u, ty - 9 * u + tw, tx - 10 * u, ty + tw * 0.5f, tx - 14 * u, ty + 9 * u + tw};
    tfd_poly(tf, 4, ic_g(224, 64, 192), 256);
    float fb = fsin(an * 2) * 3 * u;
    float f1[] = {sx[2] + 6 * u, sy[2] - 8 * u, sx[3] - 4 * u, sy[3] - 20 * u - fb, sx[4] - 2 * u, sy[4] - 6 * u};
    float f2[] = {sx[2] + 4 * u, sy[2] + 8 * u, sx[3] - 4 * u, sy[3] + 18 * u + fb, sx[4], sy[4] + 6 * u};
    tfd_poly(f1, 3, ic_g(224, 64, 192), 256);
    tfd_poly(f2, 3, ic_g(160, 32, 160), 256);
    for (int i = 6; i >= 0; i--) {
        tfd_disc(sx[i], sy[i], sr[i] + 3 * u, ic_g(0, 128, 160), 256);
    }
    for (int i = 6; i >= 0; i--) {
        tfd_disc(sx[i], sy[i], sr[i], ic_g(64, 192, 224), 256);
    }
    for (int i = 6; i >= 0; i--) {
        tfd_disc(sx[i] - 1 * u, sy[i] - sr[i] * 0.35f, sr[i] * 0.55f, ic_g(160, 240, 240), 256);
    }
    // (spots of light along the side)
    for (int i = 1; i < 6; i++) {
        tfd_disc(sx[i], sy[i] + sr[i] * 0.45f, 1.5f * u, ic_g(224, 240, 160), 256);
    }
    // (the head: the jaw with teeth, the eye; the lure on its stalk, its light pulses)
    float jaw[] = {hx + 6 * u, hy + 1 * u, hx + 13 * u, hy + 3 * u, hx + 6 * u, hy + 8 * u};
    tfd_poly(jaw, 3, ic_g(0, 32, 64), 256);
    for (int k = 0; k < 3; k++) {
        float t1[] = {hx + (7 + k * 2) * u, hy + 2 * u,           hx + (8 + k * 2) * u,
                      hy + 4.4f * u,        hx + (9 + k * 2) * u, hy + 2.2f * u};
        tfd_poly(t1, 3, ic_g(224, 240, 240), 256);
    }
    tfd_disc(hx + 1 * u, hy - 4 * u, 3.2f * u, ic_g(224, 240, 240), 256);
    tfd_disc(hx + 1.8f * u, hy - 3.6f * u, 1.8f * u, ic_g(0, 0, 32), 256);
    float lx = hx + 18 * u + 2 * u * fsin(an), ly = hy - 17 * u + 2 * u * fcos(an);
    tfd_line(hx + 2 * u, hy - 9 * u, hx + 10 * u, hy - 20 * u, 1.4f * u, ic_g(64, 192, 224), 256);
    tfd_line(hx + 10 * u, hy - 20 * u, lx, ly, 1.4f * u, ic_g(64, 192, 224), 256);
    float pl = 0.5f + 0.5f * fsin(an * 2);
    tfd_disc(lx, ly, (5 + 2 * pl) * u, ic_g(224, 224, 128), 80);
    tfd_disc(lx, ly, 3 * u, ic_g(224, 240, 160), 256);
    ic_end();
}
