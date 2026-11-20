// ORBITAL: the HUD and the screens (included by app_orbital.c). The amber HUD of the look page: the goal and the hint
// of the GUIDE, the bracket of the target, the ring of the nose with its markers, the fuel, the height, the clock; the
// keys of the play (BURN, TETHER, the turn keys); the title with the mission card; the brief; the result card.
// The text: the kfont faces (English and Hebrew; a Hebrew line starts at the right).

// ================================================================ the text helpers
// A plate under a text: a solid dark box (only writes: a blend or a darken of each pixel reads the frame in PSRAM too,
// and took 3-20 ms a frame for the plates of the HUD on the 3.49)
static int64_t s_prof[12];
static void plate(int x, int y, int w, int h, int a)
{
    (void)a;
    // (the rows between the rounded ends: a fill that writes only the pixels that change; the rounded ends: tfdraw, a
    // box of height 2r cut in two by a clip: the same pixels as one tfd_rrect)
    const int r = 6;
    uint16_t c = rgb(6, 9, 20);
    if (h <= 2 * r + 1 || x < 0 || x + w > SW) {
        tfd_rrect(x, y, w, h, r, c, 256);
    } else {
        for (int j = r; j < h - r; j++) {
            int yy = y + j;
            if ((unsigned)yy < (unsigned)SH) {
                fill16(g_fb + yy * SW + x, w, c);
            }
        }
        tfd_clip(0, y, SW, y + r);
        tfd_rrect(x, y, w, 2 * r, r, c, 256);
        tfd_clip(0, y + h - r, SW, y + h);
        tfd_rrect(x, y + h - 2 * r, w, 2 * r, r, c, 256);
        tfd_noclip();
    }
}

// The text cache. The board took 10-40 ms a frame for the HUD text: each call decodes the glyphs, finds the Hebrew order
// and measures the width again. A line now goes once into an alpha mask (the font draws it in white into a small frame
// of its own: g_fb points there for that time), and each frame only blends the mask. Up to TC_N lines stay (the least
// used goes first); a line that changes (a number) makes a new mask.
#define TC_N 40
typedef struct {
    uint32_t h;         // the hash of the font, the text and the width limit (0: a free place)
    int16_t w, bw, bh;  // the width of the text, the size of the mask
    uint8_t *m;         // bw x bh alpha (0..255), the baseline at row TC_PAD + asc, the text from column TC_PAD
    int16_t *rg;        // the ink of each row: the first column (rg[2j]) and the column after the last (rg[2j+1])
    uint32_t used;      // the frame of the last use
} tce_t;
#define TC_PAD 3
static tce_t s_tc[TC_N];
static uint32_t s_tc_frame;
static uint16_t *s_tc_buf;  // the frame of the font (the biggest line: 660 x 60 px)
#define TC_BUF_W 660
#define TC_BUF_H 60
static uint32_t tc_hash(const kfont_t *f, const char *s, int w)
{
    uint32_t h = 2166136261u ^ (uint32_t)(uintptr_t)f ^ ((uint32_t)w * 2654435761u);
    for (const char *p = s; *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    return h ? h : 1;
}
static void tc_free(void)
{
    for (int i = 0; i < TC_N; i++) {
        orb_free(s_tc[i].m);
        orb_free(s_tc[i].rg);
        s_tc[i].m = NULL;
        s_tc[i].rg = NULL;
        s_tc[i].h = 0;
    }
    orb_free(s_tc_buf);
    s_tc_buf = NULL;
}
static const tce_t *tc_get(const kfont_t *f, const char *s, int wmax)
{
    uint32_t h = tc_hash(f, s, wmax);
    int old = 0;
    for (int i = 0; i < TC_N; i++) {
        if (s_tc[i].h == h) {
            s_tc[i].used = s_tc_frame;
            return &s_tc[i];
        }
        if (s_tc[i].used < s_tc[old].used) {
            old = i;
        }
    }
    if (!s_tc_buf) {
        s_tc_buf = orb_alloc((size_t)TC_BUF_W * TC_BUF_H * 2);
    }
    if (!s_tc_buf) {
        return NULL;
    }
    char b[160];
    const char *t = wmax > 0 ? tfd_fit(f, s, wmax, b, sizeof b) : s;
    int w = ui_text_w(f, t), bw = w + 2 * TC_PAD, bh = f->line + 2 * TC_PAD;
    if (bw > TC_BUF_W) {
        bw = TC_BUF_W;
    }
    if (bh > TC_BUF_H) {
        bh = TC_BUF_H;
    }
    tce_t *e = &s_tc[old];
    orb_free(e->m);
    orb_free(e->rg);
    e->m = orb_alloc((size_t)bw * bh);
    e->rg = orb_alloc((size_t)bh * 4);
    e->h = 0;
    if (!e->m || !e->rg) {
        return NULL;
    }
    memset(s_tc_buf, 0, (size_t)bw * bh * 2);
    // (the font draws into the small frame)
    uint16_t *fb = g_fb;
    int gw = gfx_w, gh = gfx_h;
    g_fb = s_tc_buf;
    gfx_w = bw;
    gfx_h = bh;
    ui_text_c(f, TC_PAD + w / 2, TC_PAD + f->asc, t, rgb(255, 255, 255));
    g_fb = fb;
    gfx_w = gw;
    gfx_h = gh;
    for (int j = 0; j < bh; j++) {
        int lo = bw, hi = 0;
        for (int i = 0; i < bw; i++) {
            uint16_t c = (uint16_t)((s_tc_buf[j * bw + i] >> 8) | (s_tc_buf[j * bw + i] << 8));
            int g = (c >> 5) & 63;
            e->m[j * bw + i] = (uint8_t)((g << 2) | (g >> 4));
            if (g) {
                if (i < lo) {
                    lo = i;
                }
                hi = i + 1;
            }
        }
        e->rg[2 * j] = (int16_t)lo;
        e->rg[2 * j + 1] = (int16_t)hi;
    }
    e->h = h;
    e->w = (int16_t)w;
    e->bw = (int16_t)bw;
    e->bh = (int16_t)bh;
    e->used = s_tc_frame;
    return e;
}
// the mask on the screen: the text from x (its left end), the baseline at base. One pass over the rows: a dark shadow
// 1 px down and right (the HUD has no plates: the text stays readable on the planet), then the text on it. Only the
// columns with ink: each read of the frame in PSRAM is slow.
static void tc_draw(const tce_t *e, const kfont_t *f, int x, int base, uint16_t col)
{
    if (!e) {
        return;
    }
    int X0 = x - TC_PAD, Y0 = base - f->asc - TC_PAD, bw = e->bw;
    uint16_t blk = rgb(0, 0, 0);
    for (int j = 0; j <= e->bh; j++) {
        int y = Y0 + j;
        if ((unsigned)y >= (unsigned)SH) {
            continue;
        }
        // (the text row j and the shadow row j - 1, 1 px to the right; the columns i of the screen row are X0 + i)
        const uint8_t *mt = j < e->bh ? e->m + j * bw : NULL, *ms = j > 0 ? e->m + (j - 1) * bw : NULL;
        int lo = 1 << 14, hi = -1;
        if (mt && e->rg[2 * j] < e->rg[2 * j + 1]) {
            lo = e->rg[2 * j];
            hi = e->rg[2 * j + 1];
        }
        if (ms && e->rg[2 * j - 2] < e->rg[2 * j - 1]) {
            if (e->rg[2 * j - 2] + 1 < lo) {
                lo = e->rg[2 * j - 2] + 1;
            }
            if (e->rg[2 * j - 1] + 1 > hi) {
                hi = e->rg[2 * j - 1] + 1;
            }
        }
        if (lo >= hi) {
            continue;
        }
        if (X0 + lo < 0) {
            lo = -X0;
        }
        if (X0 + hi > SW) {
            hi = SW - X0;
        }
        uint16_t *row = g_fb + y * SW + X0;
        for (int i = lo; i < hi; i++) {
            int a = mt && i < bw ? mt[i] : 0;
            if (a >= 250) {
                if (row[i] != col) {
                    row[i] = col;
                }
                continue;
            }
            int sa = ms && i > 0 ? ms[i - 1] * 3 >> 2 : 0;
            if (!a && !sa) {
                continue;
            }
            uint16_t d0 = row[i], d = d0;
            if (sa) {
                d = tfd_mix(d, blk, sa);
            }
            if (a) {
                d = tfd_mix(d, col, a);
            }
            if (d != d0) {
                row[i] = d;
            }
        }
    }
}
// the cached forms of the text calls: in the middle of cx; at the start side or the end side of a box; cut to a width
static void ctext_c(const kfont_t *f, int cx, int base, const char *s, uint16_t col)
{
    const tce_t *e = tc_get(f, s, 0);
    if (e) {
        tc_draw(e, f, cx - e->w / 2, base, col);
    }
}
static int ctext_w(const kfont_t *f, const char *s)
{
    const tce_t *e = tc_get(f, s, 0);
    return e ? e->w : 0;
}
// a text at the start side of the box x0..x1 (the left in English, the right in Hebrew)
// the round frame: the width of the circle at the row y (px)
static int rchord(int y)
{
    float dy = y - SH * 0.5f, h2 = SW * SW * 0.25f - dy * dy;
    return h2 > 0 ? (int)(2 * fsqrt(h2)) : 0;
}
static void text_s(const kfont_t *f, int x0, int x1, int base, const char *s, uint16_t c)
{
    const tce_t *e = tc_get(f, s, x1 - x0);
    if (e) {
        tc_draw(e, f, (s_lang == 1 || tfd_heb(s)) ? x1 - e->w : x0, base, c);
    }
}
// a text at the end side (the right in English, the left in Hebrew)
static void text_e(const kfont_t *f, int x0, int x1, int base, const char *s, uint16_t c)
{
    const tce_t *e = tc_get(f, s, x1 - x0);
    if (e) {
        tc_draw(e, f, (s_lang == 1 || tfd_heb(s)) ? x0 : x1 - e->w, base, c);
    }
}
static void text_c(const kfont_t *f, int cx, int base, const char *s, uint16_t c, int w)
{
    const tce_t *e = tc_get(f, s, w);
    if (e) {
        tc_draw(e, f, cx - e->w / 2, base, c);
    }
}
// the face of the small labels: the round frame (466 px on the 1.75C: 0.095 mm a px) gets the next bigger face
static const kfont_t *lbl_f(void)
{
    return rndl() ? &KF_NAME : &KF_LABEL;
}
// a text with a dark plate under it (in the middle of the box)
static void text_plate_c(const kfont_t *f, int cx, int base, const char *s, uint16_t c, int wmax)
{
    const tce_t *e = tc_get(f, s, wmax);
    if (!e) {
        return;
    }
    int w = e->w + 18;
    plate(cx - w / 2, base - f->asc - 6, w, f->line + 6, 200);
    tc_draw(e, f, cx - e->w / 2, base, c);
}
// a line in two lines at a space near the middle (when it does not fit w)
static int wrap2(const kfont_t *f, const char *s, int w, char *a, char *b, int max)
{
    a[0] = 0;
    b[0] = 0;
    if (ui_text_w(f, s) <= w) {
        snprintf(a, (size_t)max, "%s", s);
        return 1;
    }
    int n = (int)strlen(s), best = -1, bd = 1 << 30;
    for (int i = 1; i < n - 1; i++) {
        if (s[i] != ' ') {
            continue;
        }
        int d = absf((float)(i - n / 2)) > 0 ? (int)absf((float)(i - n / 2)) : 0;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best < 0 || best >= max) {
        snprintf(a, (size_t)max, "%s", s);
        return 1;
    }
    memcpy(a, s, (size_t)best);
    a[best] = 0;
    snprintf(b, (size_t)max, "%s", s + best + 1);
    return 2;
}
static void fmt_time(char *o, int n, float secs)
{
    int s = (int)secs;
    if (s < 0) {
        s = 0;
    }
    if (s >= 3600) {
        snprintf(o, (size_t)n, "%d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
    } else {
        snprintf(o, (size_t)n, "%d:%02d", s / 60, s % 60);
    }
}
static void star_shape(float cx, float cy, float r, uint16_t c, int a)
{
    float p[20];
    for (int i = 0; i < 10; i++) {
        float ang = -PI_F / 2 + i * PI_F / 5, rr = i % 2 ? r * 0.45f : r;
        p[2 * i] = cx + cosf(ang) * rr;
        p[2 * i + 1] = cy + sinf(ang) * rr;
    }
    tfd_poly(p, 10, c, a);
}
static const char *body_name(int i)
{
    if (i <= 0 || i >= s_nb) {
        return "";
    }
    return MIS[s_mis].t[i - 1].name;
}

// ================================================================ the HUD of the play
static const char *say_text(int s)
{
    switch (s) {
    case SAY_FAR:
        return TR("TOO FAR: COME WITHIN 260 M", "רחוק מדי: התקרבו ל-260 מ׳");
    case SAY_MISS:
        return TR("THE HARPOON MISSED", "הצלצל החטיא");
    case SAY_SNAP:
        return TR("THE TETHER SNAPPED", "הכבל נקרע");
    case SAY_LOCK:
        return TR("TETHER LOCKED", "הכבל ננעל");
    case SAY_FREE:
        return TR("LET GO", "שוחרר");
    case SAY_NOFUEL:
        return TR("NO FUEL", "אין דלק");
    case SAY_HOT:
        return TR("HEAT!", "חום!");
    case SAY_LETGO:
        return TR("DELIVERED - NOW THE NEXT ONE", "נמסר - עכשיו הבא");
    case SAY_NOTGT:
        return TR("NOTHING TO CATCH", "אין מה לתפוס");
    }
    return "";
}
// the hint of the GUIDE (or of the autopilot): what to do now; ok = the hint is a call to act (green)
static const char *hint_text(char *buf, int n, bool *act)
{
    *act = false;
    if (s_help == HELP_OFF && !s_auto) {
        return "";
    }
    char t[24];
    float err = nose_err(s_ap_aim);
    bool aimed = absf(err) < 0.2f;
    switch (s_ap_say) {
    case PS_PLAN:
        return TR("PLANNING THE MEETING...", "מתכננים את המפגש...");
    case PS_WAIT:
        if (s_ap_wait < 0) {
            return "";
        }
        fmt_time(t, sizeof t, s_ap_wait);
        if (s_ap_wait < 6 && !s_auto) {
            *act = true;
            snprintf(buf, (size_t)n, TR("TURN TO THE GREEN MARK - BURN IN %s", "פנו אל הסימן הירוק - בעירה בעוד %s"),
                     t);
        } else {
            snprintf(buf, (size_t)n, TR("NEXT BURN IN %s", "הבעירה הבאה בעוד %s"), t);
        }
        return buf;
    case PS_BURN:
    case PS_CLOSE:
    case PS_SAVE:
        if (s_ap_wb) {
            *act = true;
            if (s_auto) {
                return TR("BURN", "בעירה");
            }
            if (!aimed) {
                return TR("TURN TO THE GREEN MARK", "פנו אל הסימן הירוק");
            }
            return TR("HOLD BURN", "החזיקו מנוע");
        }
        if (s_ap_say == PS_CLOSE) {
            if (s_d_rel < 1000) {
                snprintf(buf, (size_t)n, TR("CLOSING IN: %d M", "מתקרבים: %d מ׳"), (int)s_d_rel);
            } else {
                snprintf(buf, (size_t)n, TR("CLOSING IN: %.1f KM", "מתקרבים: %.1f ק״מ"), (double)(s_d_rel / 1000));
            }
            return buf;
        }
        if (s_ap_say == PS_SAVE) {
            return TR("NOW LIFT YOURSELF OUT OF THE AIR", "עכשיו הרימו את עצמכם מהאוויר");
        }
        return "";
    case PS_COAST: {
        float tm = (float)(s_ap_meet - s_clock);
        if (tm < 0) {
            return TR("COAST", "שיוט");
        }
        fmt_time(t, sizeof t, tm);
        snprintf(buf, (size_t)n, TR("COAST TO THE MEETING: %s", "שיוט אל המפגש: %s"), t);
        return buf;
    }
    case PS_FIRE:
        *act = true;
        return TR("FIRE THE TETHER!", "ירו את הכבל!");
    case PS_TOW:
        return T.L > T.L_goal + 2 ? TR("THE WINCH REELS IN", "הכננת מושכת") : TR("TOWING", "גוררים");
    case PS_LETGO:
        *act = true;
        return TR("HOLD TETHER TO LET GO", "החזיקו כבל כדי לשחרר");
    case PS_LAND:
        return TR("LOWER THE FALL TO THE SHIP", "הנמיכו את הנפילה אל הספינה");
    }
    return "";
}
static bool s_hud_tick;  // the numbers of the HUD change in this frame (4 times a second)
static float s_hud_last = -1;
// the bracket round the target, its name and the distance
static void draw_bracket(void)
{
    int j;
    cam_target_body(&j);
    if (j <= 0 || !B[j].on) {
        return;
    }
    float sx, sy;
    body_screen(j, &sx, &sy);
    float k = body_k(j), half = body_half(s_shape[j]) * k + 6;
    if (half < 12) {
        half = 12;
    }
    if (sx < -half || sx > SW + half || sy < -half || sy > SH + half) {
        // (out of the frame: an arrow at the edge to it)
        float cx = vcx(), cy = vcy(), dx = sx - cx, dy = sy - cy, d = fsqrt(dx * dx + dy * dy);
        if (d < 1) {
            return;
        }
        float ux = dx / d, uy = dy / d,
              t = fminf((vhw() - 24) / fmaxf(absf(ux), 1e-3f), (SH * 0.5f - 24) / fmaxf(absf(uy), 1e-3f));
        float ax = cx + ux * t, ay = cy + uy * t;
        float p[6] = {ax + ux * 10, ay + uy * 10, ax - uy * 7, ay + ux * 7, ax + uy * 7, ay - ux * 7};
        tfd_poly(p, 3, C_CYA, 220);
        return;
    }
    bool on = T.state == TH_ON;
    bool inr = s_d_rel <= TETHER_RANGE && T.state == TH_NONE;
    uint16_t c = on || inr ? C_CYA : C_AMB;
    float x0 = sx - half, x1 = sx + half, y0 = sy - half, y1 = sy + half;
    float L = 7;
    tfd_line(x0, y0, x0 + L, y0, 1.4f, c, 230);
    tfd_line(x0, y0, x0, y0 + L, 1.4f, c, 230);
    tfd_line(x1, y0, x1 - L, y0, 1.4f, c, 230);
    tfd_line(x1, y0, x1, y0 + L, 1.4f, c, 230);
    tfd_line(x0, y1, x0 + L, y1, 1.4f, c, 230);
    tfd_line(x0, y1, x0, y1 - L, 1.4f, c, 230);
    tfd_line(x1, y1, x1 - L, y1, 1.4f, c, 230);
    tfd_line(x1, y1, x1, y1 - L, 1.4f, c, 230);
    char t[48];
    static char u[48];
    snprintf(t, sizeof t, "%s", body_name(j));
    if (s_hud_tick) {
        u[0] = 0;
        if (!on && s_d_rel < 1e8f) {
            if (s_d_rel < 2000) {
                snprintf(u, sizeof u, TR("%d M  %.1f M/S", "%d מ׳  %.1f מ/ש"), (int)s_d_rel, (double)s_u_rel);
            } else {
                snprintf(u, sizeof u, TR("%.1f KM", "%.1f ק״מ"), (double)(s_d_rel / 1000));
            }
        }
    }
    if (on) {
        u[0] = 0;
    }
    // (the name and the distance at the side of the bracket: the right side, or the left side when there is no room)
    const kfont_t *lf = lbl_f();
    int w = ctext_w(lf, t), wu = ctext_w(lf, u);
    if (wu > w) {
        w = wu;
    }
    int right = (int)(vcx() + vhw()) - 4;
    bool at_r = x1 + 8 + w <= right || x0 - 8 - w < 4;
    int lx = at_r ? (int)x1 + 8 + w / 2 : (int)x0 - 8 - w / 2;
    int ty = (int)clampf(sy - 2, (longl() ? 60 : rndl() ? SH * 22 / 100 : 56), rndl() ? SH * 66 / 100 : SH - 34);
    ctext_c(lf, lx, ty, t, c);
    if (u[0]) {
        ctext_c(lf, lx, ty + lf->line - 2, u, inr ? C_CYA : rgb(255, 240, 220));
    }
}
// the ring of the nose: the markers that the nose snaps to, and the nose itself
static void draw_nose_ring(void)
{
    if (s_state != ST_PLAY && s_state != ST_BRIEF) {
        return;
    }
    float R = fmaxf(26, 16 * s_tug_k + 14);
    float cx = s_tug_sx, cy = s_tug_sy;
    float off = -PI_F / 2 - s_croll;
    fcircle(cx, cy, R, rgb(255, 179, 102), 60);
    for (int m = 0; m < MK_N; m++) {
        if (!s_mk_on[m]) {
            continue;
        }
        float a = s_mk[m] + off, x = cx + cosf(a) * R, y = cy + sinf(a) * R;
        switch (m) {
        case MK_PRO:
            tfd_ring(x, y, 4, 1.4f, C_AMB, 230);
            tfd_disc(x, y, 1.2f, C_AMB, 256);
            break;
        case MK_RETRO:
            tfd_line(x - 3.5f, y - 3.5f, x + 3.5f, y + 3.5f, 1.4f, C_AMB, 230);
            tfd_line(x - 3.5f, y + 3.5f, x + 3.5f, y - 3.5f, 1.4f, C_AMB, 230);
            break;
        case MK_TO: {
            float ca = cosf(a), sa = sinf(a);
            float p[6] = {x + ca * 5,          y + sa * 5,          x - ca * 3 - sa * 4,
                          y - sa * 3 + ca * 4, x - ca * 3 + sa * 4, y - sa * 3 - ca * 4};
            tfd_poly(p, 3, C_CYA, 230);
            break;
        }
        case MK_KILL:
            tfd_ring(x, y, 3.5f, 1.2f, C_RED, 220);
            break;
        case MK_GUIDE: {
            float pu = 0.75f + 0.25f * fsin(s_cam_t * 8);
            float r = 6 * pu + 1;
            float p[8] = {x, y - r, x + r, y, x, y + r, x - r, y};
            tfd_glow(x, y, 12, C_GRN, 120);
            tfd_poly(p, 4, C_GRN, 250);
            break;
        }
        }
    }
    // the nose: a short white line out of the ring
    float a = nose_world() + off, x0 = cx + cosf(a) * (R - 6), y0 = cy + sinf(a) * (R - 6), x1 = cx + cosf(a) * (R + 7),
          y1 = cy + sinf(a) * (R + 7);
    tfd_line(x0, y0, x1, y1, 2.2f, s_snap >= 0 ? C_GRN : C_WHT, 240);
    // the tether range (near the target)
    if (T.state == TH_NONE && s_d_rel < 700 && s_cs > 200) {
        float rr = TETHER_RANGE * s_cs * 0.001f;
        if (rr < SW) {
            ring_line(cx, cy, rr, s_d_rel <= TETHER_RANGE ? C_CYA : C_AMB, 110, 4);
        }
    }
}
// a rounded frame (t px) with no fill: 4 lines and 4 corner arcs
static void rframe(int x, int y, int w, int h, int r, uint16_t c, int a, float t)
{
    int ti = (int)(t + 0.5f);
    tfd_rect(x + r, y, w - 2 * r, ti, c, a);
    tfd_rect(x + r, y + h - ti, w - 2 * r, ti, c, a);
    tfd_rect(x, y + r, ti, h - 2 * r, c, a);
    tfd_rect(x + w - ti, y + r, ti, h - 2 * r, c, a);
    // (the corners: each pixel of the corner box, by its distance from the centre of the corner)
    float ro = (float)r, ri = r - t;
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            float dx = r - (i + 0.5f), dy = r - (j + 0.5f), d = fsqrt(dx * dx + dy * dy);
            float cov = tfd_min(ro + 0.5f - d, d - ri + 0.5f);
            if (cov <= 0) {
                continue;
            }
            int aa = cov >= 1 ? a : (int)(a * cov);
            tfd_px(x + i, y + j, c, aa);
            tfd_px(x + w - 1 - i, y + j, c, aa);
            tfd_px(x + i, y + h - 1 - j, c, aa);
            tfd_px(x + w - 1 - i, y + h - 1 - j, c, aa);
        }
    }
}
// The key cache: each look of a play key is drawn once into a picture of its own (g_fb points there for that time); each
// frame copies it row by row (the rounded corners blend with a table). The blend of the old translucent keys took
// about 26000 pixels a frame.
#define KC_R     12
#define KC_SLOTS 8
enum {
    KEY_BURN,
    KEY_TETHER,
    KEY_TL,
    KEY_TR,
    KEY_PREV,
    KEY_NEXT,
    KEY_CARD,
    KEY_FLY,
    KEY_N
};
typedef struct {
    uint32_t id, used;
    int16_t w, h;
    uint16_t *pix;
} kslot_t;
static kslot_t s_kc[KEY_N][KC_SLOTS];
static uint8_t s_kcorner[KC_R][KC_R];  // the cover of a corner pixel (0..255): (i, j) from the corner
static bool s_kcorner_ok;
static void kc_free(void)
{
    for (int k = 0; k < KEY_N; k++) {
        for (int i = 0; i < KC_SLOTS; i++) {
            orb_free(s_kc[k][i].pix);
            s_kc[k][i].pix = NULL;
            s_kc[k][i].id = 0;
        }
    }
}
// the face of a key at (0, 0): its look id has all that changes it
static void key_paint(int key, uint32_t id, int w, int h)
{
    bool lang = id & 1;
    (void)lang;
    if (key == KEY_BURN) {
        bool want = id & 2, pulse = id & 4, burning = id & 8, gated = id & 16;
        uint16_t fill = burning ? rgb(120, 70, 30) : rgb(14, 19, 32);
        uint16_t edge = want ? (pulse ? C_GRN : rgb(60, 140, 80)) : burning ? C_AMB : rgb(120, 96, 70);
        tfd_rect(0, 0, w, h, fill, 256);
        rframe(0, 0, w, h, KC_R, edge, 240, want ? 3.0f : 2.0f);
        float fx = w * 0.5f, fy = h * 0.5f - 8, fl = burning ? 1 : 0.6f;
        float p[8] = {fx, fy - 16 * fl, fx + 8, fy + 2, fx, fy + 10, fx - 8, fy + 2};
        tfd_poly(p, 4, burning ? rgb(255, 200, 120) : rgb(150, 120, 90), 240);
        float q[8] = {fx, fy - 7 * fl, fx + 4, fy + 3, fx, fy + 8, fx - 4, fy + 3};
        tfd_poly(q, 4, burning ? C_WHT : rgb(90, 74, 60), 240);
        const char *bl = gated ? TR("WAIT", "המתינו") : TR("BURN", "מנוע");
        ctext_c(&KF_NAME, w / 2, h - 10, bl, gated ? rgb(160, 150, 140) : C_WHT);
    } else if (key == KEY_TETHER) {
        int st = (id >> 1) & 3;
        bool inr = id & 8, fire = id & 16, pulse = id & 32;
        uint16_t tf = st == TH_ON ? rgb(20, 60, 70) : fire ? rgb(20, 70, 84) : rgb(14, 19, 32);
        uint16_t te = st == TH_ON ? C_CYA : fire ? (pulse || !inr ? C_CYA : C_WHT) : rgb(80, 110, 120);
        tfd_rect(0, 0, w, h, tf, 256);
        rframe(0, 0, w, h, KC_R, te, 240, 2.0f);
        const char *tl = st == TH_ON ? TR("LET GO", "שחררו") : st == TH_FLY ? "..." : TR("TETHER", "כבל");
        // (a word that does not fit with 4 px at each side takes the small face: TETHER is 65 px in KF_NAME, the key
        // of the round frame 74 px on the 1.75C and 65 px on the Watcher)
        const kfont_t *kf = longl() ? &KF_BIG : &KF_NAME;
        if (!longl() && ui_text_w(kf, tl) > w - 8) {
            kf = &KF_LABEL;
        }
        ctext_c(kf, w / 2,
                h / 2 + (longl()          ? 9
                         : kf == &KF_NAME ? 6
                                          : 5),
                tl, st == TH_NONE && !inr ? rgb(150, 170, 176) : C_WHT);
        if (st == TH_ON && longl()) {
            ctext_c(&KF_LABEL, w / 2, h - 4, TR("HOLD 1 S", "החזיקו שנייה"), rgb(170, 220, 230));
        }
    } else if (key == KEY_PREV || key == KEY_NEXT) {
        bool on = id & 2;
        tfd_rect(0, 0, w, h, rgb(14, 20, 36), 256);
        rframe(0, 0, w, h, KC_R, on ? C_AMB : rgb(80, 66, 54), 240, 2.0f);
        float cx = w * 0.5f, cy = h * 0.5f, sg = key == KEY_PREV ? -1.0f : 1.0f;
        float p[6] = {cx + sg * 11, cy, cx - sg * 8, cy - 14, cx - sg * 8, cy + 14};
        tfd_poly(p, 3, on ? C_AMB : rgb(90, 76, 62), 240);
    } else if (key == KEY_CARD) {
        int sel = (id >> 1) & 31, st = (id >> 6) & 3;
        char t[48];
        tfd_rect(0, 0, w, h, rgb(14, 20, 36), 256);
        rframe(0, 0, w, h, KC_R, C_AMB, 240, 2.0f);
        snprintf(t, sizeof t, TR("MISSION %d OF %d", "משימה %d מתוך %d"), sel + 1, NMIS);
        text_c(&KF_LABEL, w / 2, 17, t, rgb(255, 214, 140), w - 12);
        text_c(&KF_NAME, w / 2, 39, MIS[sel].title[s_lang], C_WHT, w - 12);
        for (int k = 0; k < 3; k++) {
            star_shape(w / 2 - 20 + k * 20, h - 13, 7, k < st ? rgb(255, 210, 90) : rgb(60, 56, 52), 256);
        }
    } else if (key == KEY_FLY) {
        bool pu = id & 2;
        tfd_rect(0, 0, w, h, rgb(160, 90, 30), 256);
        rframe(0, 0, w, h, KC_R, pu ? C_AMB : rgb(255, 214, 140), 240, 2.0f);
        ctext_c(&KF_BIG, w / 2, h / 2 + 9, TR("FLY", "המריאו"), C_WHT);
    } else {
        bool on = id & 2;
        tfd_rect(0, 0, w, h, on ? rgb(70, 50, 30) : rgb(14, 19, 32), 256);
        rframe(0, 0, w, h, KC_R, on ? C_AMB : rgb(120, 96, 70), 240, 2.0f);
        float cx = w * 0.5f, cy = h * 0.5f, sg = key == KEY_TL ? -1.0f : 1.0f;
        float p[6] = {cx + sg * 13, cy, cx - sg * 9, cy - 15, cx - sg * 9, cy + 15};
        tfd_poly(p, 3, on ? C_WHT : C_AMB, 240);
    }
}
static void key_draw(int key, box_t b, uint32_t id)
{
    if (!s_kcorner_ok) {
        for (int j = 0; j < KC_R; j++) {
            for (int i = 0; i < KC_R; i++) {
                float dx = KC_R - (i + 0.5f), dy = KC_R - (j + 0.5f), d = fsqrt(dx * dx + dy * dy);
                s_kcorner[j][i] = (uint8_t)clampi((int)((KC_R + 0.5f - d) * 255), 0, 255);
            }
        }
        s_kcorner_ok = true;
    }
    kslot_t *sl = NULL, *old = &s_kc[key][0];
    for (int i = 0; i < KC_SLOTS; i++) {
        kslot_t *q = &s_kc[key][i];
        if (q->pix && q->id == id && q->w == b.w && q->h == b.h) {
            sl = q;
            break;
        }
        if (q->used < old->used) {
            old = q;
        }
    }
    if (!sl) {
        sl = old;
        if (!sl->pix || sl->w != b.w || sl->h != b.h) {
            orb_free(sl->pix);
            sl->pix = orb_alloc((size_t)b.w * b.h * 2);
            if (!sl->pix) {
                // (no memory: a solid box, because the world under a key is not drawn: a hole)
                tfd_rect(b.x, b.y, b.w, b.h, rgb(14, 19, 32), 256);
                return;
            }
        }
        sl->id = id;
        sl->w = (int16_t)b.w;
        sl->h = (int16_t)b.h;
        uint16_t *fb = g_fb;
        int gw = gfx_w, gh = gfx_h;
        g_fb = sl->pix;
        gfx_w = b.w;
        gfx_h = b.h;
        tfd_screen();
        key_paint(key, id, b.w, b.h);
        g_fb = fb;
        gfx_w = gw;
        gfx_h = gh;
        tfd_screen();
    }
    sl->used = s_tc_frame;
    for (int j = 0; j < b.h; j++) {
        int y = b.y + j;
        if ((unsigned)y >= (unsigned)SH) {
            continue;
        }
        uint16_t *row = g_fb + y * SW + b.x;
        const uint16_t *src = sl->pix + j * b.w;
        int cj = j < KC_R ? j : (j >= b.h - KC_R ? b.h - 1 - j : -1);
        if (cj < 0) {
            copy16(row, src, b.w);
            continue;
        }
        for (int i = 0; i < KC_R; i++) {
            int a = s_kcorner[cj][i];
            if (a) {
                row[i] = a >= 250 ? src[i] : tfd_mix(row[i], src[i], a);
                int k = b.w - 1 - i;
                row[k] = a >= 250 ? src[k] : tfd_mix(row[k], src[k], a);
            }
        }
        copy16(row + KC_R, src + KC_R, b.w - 2 * KC_R);
    }
}
static void draw_keys(void)
{
    uint32_t lg = s_lang ? 1 : 0;
    bool pulse = fsin(s_cam_t * 8) > 0;
    // BURN: a flame; it glows while the engine burns; WAIT while the GUIDE keeps it off; a green edge when it asks
    bool want = !s_auto && s_help == HELP_GUIDE && s_ap_wb;
    key_draw(KEY_BURN, box_burn(),
             lg | (want ? 2u : 0) | (want && pulse ? 4u : 0) | (s_throttle > 0 ? 8u : 0) | (s_gated ? 16u : 0));
    // TETHER: bright in range; LET GO with a hold fill while towing
    box_t t = box_tether();
    bool inr = T.state == TH_NONE && s_d_rel <= TETHER_RANGE && s_tgt > 0 && !s_released[s_tgt];
    bool fire = (s_ap_say == PS_FIRE || inr) && T.state == TH_NONE;
    key_draw(KEY_TETHER, t,
             lg | ((uint32_t)(T.state & 3) << 1) | (inr ? 8u : 0) | (fire ? 16u : 0) | (fire && pulse ? 32u : 0));
    if (T.state == TH_ON && s_hold_t > 0) {
        tfd_rrect(t.x + 3, t.y + 3, (int)((t.w - 6) * clampf(s_hold_t, 0, 1)), t.h - 6, 10, C_CYA, 120);
    }
    // the turn keys (TOUCH)
    if (s_ctl_mode == CTL_TOUCH) {
        key_draw(KEY_TL, box_turn(0), lg | (s_ctl.turn < -0.1f ? 2u : 0));
        key_draw(KEY_TR, box_turn(1), lg | (s_ctl.turn > 0.1f ? 2u : 0));
    }
}
static void draw_hud(void)
{
    const mis_t *M = &MIS[s_mis];
    char buf[96];
    // (the numbers and the hint change at most 4 times a second: each change is a new line of the text cache)
    s_hud_tick = s_cam_t - s_hud_last >= 0.25f || s_cam_t < s_hud_last;
    if (s_hud_tick) {
        s_hud_last = s_cam_t;
    }
    int64_t tq = esp_timer_get_time();
    draw_bracket();
    draw_nose_ring();
    s_prof[6] += esp_timer_get_time() - tq;
    int L = longl(), R = rndl();
    int hx0 = R ? SW * 15 / 100 : 8, hx1 = L ? SW - 128 : R ? SW - SW * 15 / 100 : SW - 8;
    // the goal and the hint (the GUIDE)
    snprintf(buf, sizeof buf, "%d  %s", s_mis + 1, M->goal[s_lang]);
    int b1 = L ? 18 : R ? SH * 18 / 100 : 16;
    if (R) {
        int cw = rchord(b1 - 8) - 24;
        text_c(ctext_w(&KF_NAME, buf) > cw ? &KF_LABEL : &KF_NAME, SW / 2, b1, buf, C_AMB, cw);
    } else {
        text_s(L ? &KF_NAME : &KF_LABEL, hx0, hx1, b1, buf, C_AMB);
    }
    static bool act;
    static char hb2[128];
    if (s_hud_tick) {
        char hb[96];
        const char *h = hint_text(hb, sizeof hb, &act);
        if (s_say_t > 0 && s_say != SAY_NONE) {
            h = say_text(s_say);
            act = s_say == SAY_LOCK || s_say == SAY_LETGO;
        }
        if (s_auto && !s_demo) {
            snprintf(hb2, sizeof hb2, "%s%s%s", TR("AUTOPILOT", "טייס אוטומטי"), h[0] ? " - " : "", h);
        } else {
            snprintf(hb2, sizeof hb2, "%s", h);
        }
    }
    // (a long hint, as "AUTOPILOT - COAST TO THE MEETING" on the 320 frame: the smaller font, not a cut line)
    if (hb2[0]) {
        const kfont_t *hf = ctext_w(&KF_NAME, hb2) > hx1 - hx0 ? &KF_LABEL : &KF_NAME;
        uint16_t hc = s_auto && !s_demo ? C_CYA : act ? C_GRN : C_WHT;
        if (R) {
            int hw = rchord(b1 + 16) - 24;
            text_c(ctext_w(&KF_NAME, hb2) > hw ? &KF_LABEL : &KF_NAME, SW / 2, b1 + 24, hb2, hc, hw);
        } else {
            text_s(hf, hx0, hx1, b1 + (L ? 22 : 21), hb2, hc);
        }
    }
    // the fuel (the bar and the speed that it can still give), the height, the speed of the clock: three places in a row
    int touch = s_ctl_mode == CTL_TOUCH;
    int iy = L ? SH - 8 : R ? box_arc(0).y - 10 : SH - 80;
    int ix0 = L ? (touch ? 146 : 10) : R ? SW * 12 / 100 : 8, ix1 = L ? SW - 128 : R ? SW - SW * 12 / 100 : SW - 8;
    float ff = clampf(s_fuel / fmaxf(M->fuel, 1), 0, 1);
    int bw = L ? 54 : 38, rtl = s_lang == 1;
    int bx = rtl ? ix1 - bw : ix0;
    tfd_rrect(bx, iy - 9, bw, 5, 2, rgb(80, 56, 32), 256);
    tfd_rrect(rtl ? bx + bw - (int)(bw * ff) : bx, iy - 9, (int)(bw * ff), 5, 2, ff < 0.15f ? C_RED : C_AMB, 256);
    static char s1[32], s2[32], s3[16];
    if (s_hud_tick) {
        snprintf(s1, sizeof s1, TR("%d M/S", "%d מ/ש"), (int)dv_left());
        snprintf(s3, sizeof s3, "x%d", (int)(s_rate + 0.5f));
        snprintf(s2, sizeof s2, TR("%d KM", "%d ק״מ"), (int)body_alt(&B[0]));
    }
    const kfont_t *lf = lbl_f();
    int w1 = ctext_w(lf, s1);
    ctext_c(lf, rtl ? bx - 6 - w1 / 2 : bx + bw + 6 + w1 / 2, iy, s1, C_AMB);
    int w3 = ctext_w(lf, s3);
    ctext_c(lf, rtl ? ix0 + w3 / 2 : ix1 - w3 / 2, iy, s3, rgb(255, 228, 196));
    int m0 = rtl ? ix0 + w3 + 8 : bx + bw + 12 + w1, m1 = rtl ? bx - 12 - w1 : ix1 - w3 - 8;
    ctext_c(lf, (m0 + m1) / 2, iy, s2, rgb(255, 228, 196));
    tq = esp_timer_get_time();
    draw_keys();
    s_prof[7] += esp_timer_get_time() - tq;
}

// ================================================================ the title
static int s_sel;  // the mission on the card of the title
static box_t box_prev(void)
{
    if (longl()) {
        box_t b = {SW - 326, 10, 60, 70};
        return b;
    }
    if (rndl()) {
        box_t b = {SW * 9 / 100, SH * 42 / 100, SW * 12 / 100, SH * 15 / 100};
        return b;
    }
    box_t b = {4, 90, 56, 66};
    return b;
}
static box_t box_card(void)
{
    if (longl()) {
        box_t b = {SW - 262, 10, 198, 70};
        return b;
    }
    if (rndl()) {
        box_t b = {SW * 23 / 100, SH * 42 / 100, SW * 54 / 100, SH * 15 / 100};
        return b;
    }
    box_t b = {64, 90, 192, 66};
    return b;
}
static box_t box_next(void)
{
    if (longl()) {
        box_t b = {SW - 60, 10, 56, 70};
        return b;
    }
    if (rndl()) {
        box_t b = {SW * 79 / 100, SH * 42 / 100, SW * 12 / 100, SH * 15 / 100};
        return b;
    }
    box_t b = {260, 90, 56, 66};
    return b;
}
static box_t box_fly(void)
{
    if (longl()) {
        box_t b = {SW - 262, 90, 198, 70};
        return b;
    }
    if (rndl()) {
        box_t b = {SW * 27 / 100, SH * 61 / 100, SW * 46 / 100, SH * 15 / 100};
        return b;
    }
    box_t b = {64, 164, 192, 66};
    return b;
}
static int stars_total(void)
{
    int n = 0;
    for (int i = 0; i < NMIS; i++) {
        n += s_stars[i];
    }
    return n;
}
// the plate of the name on the title (the text from tx0 to tx1)
static box_t box_title_plate(int *tx0, int *tx1)
{
    int L = longl();
    if (rndl()) {
        *tx0 = SW * 20 / 100;
        *tx1 = SW - *tx0;
        box_t b = {*tx0 - 8, SH * 12 / 100, (*tx1 - *tx0) + 16, SH * 27 / 100};
        return b;
    }
    *tx0 = L ? 14 : 10;
    *tx1 = L ? SW - 340 : SW - 10;
    box_t b = {*tx0 - 8, 4, (*tx1 - *tx0) + 16, L ? SH - 8 : 78};
    return b;
}
// The holes of this frame (orb_view.inc): the boxes that the HUD paints opaque after the world. They must be the boxes
// of draw_title() and draw_keys() in this frame.
static void holes_make(bool card)
{
    s_nholes = 0;
    if (card) {
        return;
    }
    if (s_state == ST_TITLE) {
        int tx0, tx1;
        box_t p = box_title_plate(&tx0, &tx1), k[4] = {box_prev(), box_next(), box_card(), box_fly()};
        hole_add(p.x, p.y, p.w, p.h, 7);
        for (int i = 0; i < 4; i++) {
            hole_add(k[i].x, k[i].y, k[i].w, k[i].h, KC_R);
        }
        return;
    }
    box_t b = box_burn(), t = box_tether();
    hole_add(b.x, b.y, b.w, b.h, KC_R);
    hole_add(t.x, t.y, t.w, t.h, KC_R);
    if (s_ctl_mode == CTL_TOUCH) {
        for (int i = 0; i < 2; i++) {
            box_t u = box_turn(i);
            hole_add(u.x, u.y, u.w, u.h, KC_R);
        }
    }
}
static void draw_title(void)
{
    int L = longl();
    // the name: a dark plate at the left (the demo flies behind it)
    int tx0, tx1;
    box_t pb = box_title_plate(&tx0, &tx1);
    plate(pb.x, pb.y, pb.w, pb.h, 150);
    char t[64];
    if (rndl()) {
        int ry = pb.y + 4, tw = tx1 - tx0;
        text_c(&KF_TITLE, SW / 2, ry + 42, "ORBITAL", C_AMB, tw);
        text_c(&KF_NAME, SW / 2, ry + 68, TR("A RESCUE TUG IN LOW ORBIT", "גוררת חילוץ במסלול נמוך"), C_WHT, tw);
        snprintf(t, sizeof t, TR("%d OF %d STARS", "%d מתוך %d כוכבים"), stars_total(), NMIS * 3);
        text_c(&KF_NAME, SW / 2, ry + 96, t, rgb(255, 214, 140), tw);
        // (over the planet: on a dark plate, so that the land under it does not hide the letters)
        text_plate_c(&KF_LABEL, SW / 2, SH * 83 / 100, TR("MORE IN THE PANEL: PULL DOWN", "עוד בלוח: משכו למטה"),
                     rgb(190, 180, 170), SW * 60 / 100);
    } else {
        text_s(&KF_TITLE, tx0, tx1, L ? 48 : 42, "ORBITAL", C_AMB);
        text_s(&KF_NAME, tx0, tx1, L ? 74 : 66, TR("A RESCUE TUG IN LOW ORBIT", "גוררת חילוץ במסלול נמוך"), C_WHT);
    }
    if (L) {
        snprintf(t, sizeof t, TR("%d OF %d STARS", "%d מתוך %d כוכבים"), stars_total(), NMIS * 3);
        text_s(&KF_LABEL, tx0, tx1, 100, t, rgb(255, 214, 140));
        const char *hm = s_help == HELP_GUIDE  ? TR("GUIDE: FOLLOW THE GREEN MARK", "מדריך: לכו אחרי הסימן הירוק")
                         : s_help == HELP_AUTO ? TR("AUTOPILOT: WATCH IT FLY", "טייס אוטומטי: צפו בו טס")
                                               : TR("NO HELP: YOU FLY ALONE", "בלי עזרה: אתם טסים לבד");
        text_s(&KF_LABEL, tx0, tx1, 124, hm, C_CYA);
        text_s(&KF_LABEL, tx0, tx1, 148, TR("MORE IN THE PANEL: PULL DOWN", "עוד בלוח: משכו למטה"), rgb(170, 160, 150));
    }
    // the mission card: the number, the name, the stars
    // (the keys of the title: the key cache)
    uint32_t lg = s_lang ? 1 : 0;
    key_draw(KEY_PREV, box_prev(), lg | (s_sel > 0 ? 2u : 0));
    key_draw(KEY_NEXT, box_next(), lg | (s_sel < s_unlock ? 2u : 0));
    key_draw(KEY_CARD, box_card(), lg | ((uint32_t)s_sel << 1) | ((uint32_t)(s_stars[s_sel] & 3) << 6));
    // FLY
    key_draw(KEY_FLY, box_fly(), lg | (fsin(s_cam_t * 3) > 0 ? 2u : 0));
}

// ================================================================ the brief (the start of a mission)
static void draw_brief(void)
{
    const mis_t *M = &MIS[s_mis];
    int L = longl();
    int w = L ? SW - 200 : SW - 16, x = L ? 24 : 8, y = L ? 50 : 60, h = L ? 112 : 130;
    if (rndl()) {
        w = SW * 80 / 100;
        x = SW * 10 / 100;
        y = SH * 33 / 100;
    }
    float a = clampf(s_state_t / 0.3f, 0, 1) * clampf((4.2f - s_state_t) / 0.4f, 0, 1);
    plate(x, y, w, h, (int)(215 * a));
    if (a < 0.3f) {
        return;
    }
    char t[48], l1[96], l2[96];
    snprintf(t, sizeof t, TR("MISSION %d", "משימה %d"), s_mis + 1);
    text_s(&KF_LABEL, x + 12, x + w - 12, y + 18, t, rgb(255, 214, 140));
    text_s(&KF_BIG, x + 12, x + w - 12, y + 44, M->title[s_lang], C_WHT);
    text_s(&KF_NAME, x + 12, x + w - 12, y + 66, M->goal[s_lang], C_CYA);
    int nl = wrap2(&KF_LABEL, M->idea[s_lang], w - 24, l1, l2, sizeof l1);
    text_s(&KF_LABEL, x + 12, x + w - 12, y + 88, l1, rgb(255, 228, 196));
    if (nl > 1) {
        text_s(&KF_LABEL, x + 12, x + w - 12, y + 106, l2, rgb(255, 228, 196));
    }
}

// ================================================================ the result card
static int res_keys(void);
static box_t box_res(int k)  // 0 NEXT (or AGAIN after a failure), 1 AGAIN, 2 MISSIONS
{
    if (longl()) {
        box_t b = {24 + k * 158, SH - 58, 150, 52};
        return b;
    }
    if (rndl()) {
        int w = SW * 22 / 100, g = SW * 2 / 100, n = res_keys();
        box_t b = {SW / 2 - (n * w + (n - 1) * g) / 2 + k * (w + g), SH * 59 / 100, w, SH * 13 / 100};
        return b;
    }
    box_t b = {4 + k * 105, SH - 62, 101, 56};
    return b;
}
static int res_keys(void)
{
    return s_state == ST_DONE && s_mis + 1 < NMIS ? 3 : 2;
}
static int res_action(int k)  // the key k: 0 next, 1 again, 2 missions
{
    if (res_keys() == 3) {
        return k;
    }
    return k == 0 ? 1 : 2;
}
static const char *fail_text(void)
{
    static char b[96];
    switch (s_fail_why) {
    case F_TGT_BURN:
        snprintf(b, sizeof b, TR("%s BURNED UP IN THE AIR", "%s נשרף באוויר"), body_name(s_tgt));
        return b;
    case F_TUG_BURN:
        return TR("YOUR TUG BURNED UP", "הגוררת נשרפה");
    case F_FUEL:
        return TR("OUT OF FUEL", "נגמר הדלק");
    case F_DEBRIS:
        return TR("HIT BY THE DEBRIS", "פגיעה מהפסולת");
    case F_FAR_LAND:
        return TR("THE CREW CAME DOWN FAR FROM THE SHIP", "הצוות נחת רחוק מהספינה");
    }
    return TR("THE MISSION IS LOST", "המשימה אבדה");
}
static void draw_result(void)
{
    if (s_state_t < 1.0f) {
        return;
    }
    int L = longl();
    bool ok = s_state == ST_DONE;
    int x = L ? 16 : 6, w = L ? SW - 32 : SW - 12, y = L ? 6 : 30, h = L ? SH - 12 : SH - 36;
    if (rndl()) {
        x = SW * 14 / 100;
        w = SW - 2 * x;
        y = SH * 22 / 100;
        h = SH * 56 / 100;
    }
    // (the card: the frame is a dark fill when it shows (orb_render): only its edge here)
    rframe(x, y, w, h, 10, rgb(120, 96, 70), 230, 1.5f);
    char t[96], tm[24];
    const char *head = ok ? (s_mis == NMIS - 1 ? TR("ALL CREWS ARE HOME", "כל הצוותים בבית")
                                               : TR("MISSION COMPLETE", "המשימה הושלמה"))
                          : TR("MISSION LOST", "המשימה נכשלה");
    text_c(&KF_BIG, SW / 2, y + 30, head, ok ? C_AMB : C_RED, w - 20);
    if (ok) {
        for (int k = 0; k < 3; k++) {
            float pop = clampf((s_state_t - 1.2f - k * 0.3f) / 0.25f, 0, 1);
            star_shape(SW / 2 - 40 + k * 40, y + (L ? 52 : 58), 13 * (0.6f + 0.4f * pop),
                       k < s_result_stars && pop > 0 ? rgb(255, 210, 90) : rgb(60, 56, 52), 256);
        }
        fmt_time(tm, sizeof tm, (float)s_result_time);
        snprintf(t, sizeof t, TR("FUEL LEFT %d KG   TIME %s", "נשאר דלק %d ק״ג   זמן %s"), (int)s_result_fuel, tm);
        text_c(&KF_LABEL, SW / 2, y + (L ? 82 : 92), t, rgb(255, 228, 196), w - 20);
        if (s_new_best && !L && !rndl()) {
            text_c(&KF_LABEL, SW / 2, y + 112, TR("A NEW BEST", "שיא חדש"), C_GRN, w - 20);
        }
    } else {
        text_c(&KF_NAME, SW / 2, y + (L ? 60 : 70), fail_text(), C_WHT, w - 20);
    }
    if (s_state_t < 1.6f) {
        return;
    }
    static const char *const EN[3] = {"NEXT", "AGAIN", "MISSIONS"};
    static const char *const HE[3] = {"הבאה", "שוב", "משימות"};
    for (int k = 0; k < res_keys(); k++) {
        box_t b = box_res(k);
        int a = res_action(k);
        uint16_t fill = a == 0 || (!ok && a == 1) ? rgb(160, 90, 30) : rgb(20, 28, 48);
        tfd_rrect(b.x, b.y, b.w, b.h, 14, fill, 235);
        ui_rbox(b.x, b.y, b.w, b.h, 14, fill, C_AMB, 2);
        ctext_c(&KF_NAME, b.x + b.w / 2, b.y + b.h / 2 + 6, s_lang ? HE[a] : EN[a], C_WHT);
    }
}
