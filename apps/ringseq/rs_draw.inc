// RING SEQUENCER: the still layer of each screen and the moving parts of each frame. (Part of app_ringseq.c.)

#define WHITE   rgb(255, 255, 255)
#define C_DIM   rgb(120, 128, 160)
#define C_TEXT  rgb(226, 230, 246)
#define C_PANEL rgb(20, 22, 36)
#define C_EDGE  rgb(58, 64, 104)
#define C_GOLD  rgb(255, 206, 96)

static int s_act = -1;         // the zone that the finger holds now (it shows brighter)
static int s_act_arg;          //  (and its arg)
static int64_t s_now;          // the time of the frame (ms)
static float s_ph;             // the place of the arm: 16th notes from the start
static int64_t s_flash_t[NR];  // the last step that sounded on each ring: when
static uint8_t s_flash_k[NR];  //  and which step
static uint32_t s_seen[NR];
static char s_toast[2][48];  // two lines
static int64_t s_toast_until;
static int64_t s_zoom_t0;  // the start of the zoom in (V_RING)
static float s_zoom_from;
static int64_t s_hold_t0;  // the hold on a slot (V_SLOTS)
static int s_hold_slot = -1;
static int s_pick = -1;  // V_MAIN: the ring under the finger (-1: none)
static bool s_pick_low;  //  (the finger is in the lower half: the name shows in the upper half)

static const char *tx(const char *en, const char *he)
{
    return s_he ? he : en;
}
static void toast(const char *a, const char *b)
{
    snprintf(s_toast[0], sizeof s_toast[0], "%s", a ? a : "");
    snprintf(s_toast[1], sizeof s_toast[1], "%s", b ? b : "");
    s_toast_until = s_now + 2200;
}
static float pt_x(float r, float a)
{
    return L.cx + r * fsin(a);
}
static float pt_y(float r, float a)
{
    return L.cy - r * fcos(a);
}
static float step_a(int k, int n)
{
    return 6.2831853f * k / n;
}

// ---------------------------------------------------------------- icons

static void icon(int id, float x, float y, float sz, uint16_t c)
{
    float k = sz / 20;
    if (id == B_PLAY && s_run) {
        p_rrect(x - 7 * k, y - 7 * k, 14 * k, 14 * k, 2 * k, c, 256);
    } else if (id == B_PLAY) {
        p_tri(x - 6 * k, y - 9 * k, x - 6 * k, y + 9 * k, x + 9 * k, y, c, 256);
    } else if (id == B_UNDO) {
        for (int i = 0; i < 7; i++) {
            float a0 = rad(-60 + 34 * i), a1 = rad(-60 + 34 * (i + 1));
            p_line(x + 8 * k * fsin(a0), y - 8 * k * fcos(a0) + 2 * k, x + 8 * k * fsin(a1),
                   y - 8 * k * fcos(a1) + 2 * k, 2.4f * k, c, 256, false);
        }
        float ax = x + 8 * k * fsin(rad(-60)), ay = y - 8 * k * fcos(rad(-60)) + 2 * k;
        p_tri(ax - 6 * k, ay - 1 * k, ax + 3 * k, ay - 6 * k, ax + 2 * k, ay + 4 * k, c, 256);
    } else if (id == B_MODE) {
        p_ring(x, y, 8.5f * k, 1.8f * k, c, 256, false);
        if (!P->poly) {
            p_line(x, y, x + 6 * k, y - 6 * k, 2.2f * k, c, 256, false);
        } else {
            for (int i = 0; i < 3; i++) {
                float a = rad(30 + 110 * i), r0 = (2 + 2 * i) * k;
                p_line(x + r0 * fsin(a), y - r0 * fcos(a), x + (r0 + 3.5f * k) * fsin(a), y - (r0 + 3.5f * k) * fcos(a),
                       2.2f * k, c, 256, false);
            }
        }
    } else if (id == B_VARY) {
        p_rrect(x - 9 * k, y - 9 * k, 18 * k, 18 * k, 4 * k, c, 256);
        p_rrect(x - 7 * k, y - 7 * k, 14 * k, 14 * k, 3 * k, C_PANEL, 256);
        p_disc(x - 3.5f * k, y - 3.5f * k, 1.7f * k, c, 256, false);
        p_disc(x, y, 1.7f * k, c, 256, false);
        p_disc(x + 3.5f * k, y + 3.5f * k, 1.7f * k, c, 256, false);
    } else if (id == B_EXPORT) {
        p_line(x, y - 9 * k, x, y + 2 * k, 2.4f * k, c, 256, false);
        p_tri(x - 5.5f * k, y, x + 5.5f * k, y, x, y + 6 * k, c, 256);
        p_line(x - 8 * k, y + 8.5f * k, x + 8 * k, y + 8.5f * k, 2.4f * k, c, 256, false);
    } else if (id == B_SLOTS) {
        for (int i = 0; i < 4; i++) {
            p_rrect(x + (i % 2 ? 1.5f : -8.5f) * k, y + (i / 2 ? 1.5f : -8.5f) * k, 7 * k, 7 * k, 1.5f * k, c, 256);
        }
    } else if (id == B_BACK) {
        p_ring(x, y, 8 * k, 1.6f * k, c, 256, false);
        p_ring(x, y, 4 * k, 1.6f * k, c, 256, false);
    }
}

// ---------------------------------------------------------------- the texts of the controls

static void key_text(char *b, int len)
{
    snprintf(b, len, "%s %s", KEY_NAME[P->key % 12], MODE_NAME[P->mode % NMODE][s_he]);
}
static void param_text(int i, char *b, int len)
{
    const ring_t *g = &P->r[s_sel];
    if (i == 0) {
        snprintf(b, len, "%d", ring_hits(g));
    } else if (i == 1) {
        snprintf(b, len, "%d", g->n);
    } else if (i == 2) {
        snprintf(b, len, "%s", snd_name(s_sel, g->snd));
    } else {
        snprintf(b, len, "%s", g->mute ? tx("MUTE", "מושתק") : g->solo ? tx("SOLO", "סולו") : "");
        if (!g->mute && !g->solo) {
            snprintf(b, len, "%d", g->lvl);
        }
    }
}
static const char *param_name(int i)
{
    static const char *const N[4][2] = {{"HITS", "מכות"}, {"STEPS", "צעדים"}, {"SOUND", "צליל"}, {"LEVEL", "עוצמה"}};
    return N[i & 3][s_he];
}
// the label (lbl) and the value (val) of a control; an icon id in *ic (0: none)
static void elem_text(int id, int arg, char *lbl, char *val, int *ic)
{
    *lbl = *val = 0;
    *ic = 0;
    switch (id) {
    case B_KEY:
        strcpy(lbl, tx("KEY", "סולם"));
        key_text(val, 32);
        break;
    case B_SWING:
        strcpy(lbl, tx("SWING", "סווינג"));
        snprintf(val, 32, "%d%%", P->swing);
        break;
    case B_MODE:
        strcpy(lbl, P->poly ? tx("METER", "משקלים") : tx("RHYTHM", "מקצבים"));
        *ic = B_MODE;
        break;
    case B_UNDO:
        strcpy(lbl, tx("UNDO", "בטל"));
        *ic = B_UNDO;
        break;
    case B_SLOTS:
        snprintf(lbl, 32, "%s %d", tx("SLOT", "תבנית"), S->cur + 1);
        *ic = B_SLOTS;
        break;
    case B_VARY:
        strcpy(lbl, tx("VARY", "שינוי"));
        *ic = B_VARY;
        break;
    case B_EXPORT:
        strcpy(lbl, "MIDI");
        *ic = B_EXPORT;
        break;
    case B_PLAY:
        snprintf(lbl, 32, "%d BPM", P->bpm);
        *ic = B_PLAY;
        break;
    case B_RING:
        strcpy(lbl, RNAME[arg][s_he]);
        snprintf(val, 32, "%d/%d", ring_hits(&P->r[arg]), P->r[arg].n);
        break;
    case P_HITS:
    case P_STEPS:
    case P_SND:
    case P_LVL:
        strcpy(lbl, param_name(id - P_HITS));
        param_text(id - P_HITS, val, 32);
        break;
    case B_MINUS:
        strcpy(val, arg == 2 ? "<" : "-");
        break;
    case B_PLUS:
        strcpy(val, arg == 2 ? ">" : "+");
        break;
    case B_BACK:
        strcpy(lbl, tx("BACK", "חזרה"));
        *ic = B_BACK;
        break;
    case B_PREV:
        strcpy(lbl, tx("RING", "טבעת"));
        strcpy(val, "<");
        break;
    case B_NEXT:
        strcpy(lbl, tx("RING", "טבעת"));
        strcpy(val, ">");
        break;
    case K_KEY:
        strcpy(val, KEY_NAME[arg % 12]);
        break;
    case K_MODE:
        strcpy(val, MODE_NAME[arg % NMODE][s_he]);
        break;
    case K_DONE:
        strcpy(val, tx("DONE", "סיום"));
        break;
    case S_SLOT:
        snprintf(val, 32, "%d", arg + 1);
        break;
    case B_MUTE:
        strcpy(val, tx("MUTE", "השתק"));
        break;
    case B_SOLO:
        strcpy(val, tx("SOLO", "סולו"));
        break;
    }
}
static bool elem_on(int id, int arg)
{
    return (id == K_KEY && P->key % 12 == arg) || (id == K_MODE && P->mode % NMODE == arg) ||
           (id == S_SLOT && S->cur == arg) || (id == B_RING && P->r[arg].mute) || (id == B_MUTE && P->r[s_sel].mute) ||
           (id == B_SOLO && P->r[s_sel].solo);
}

// a rect control: a panel, the label and the value (or an icon)
static void draw_button(const hit_t *h)
{
    char lbl[40], val[40];
    int ic;
    elem_text(h->id, h->arg, lbl, val, &ic);
    bool on = elem_on(h->id, h->arg), act = s_act == h->id && s_act_arg == h->arg;
    uint16_t fill = act ? rgb(46, 52, 86) : C_PANEL, tc = C_TEXT;
    if (h->id == B_RING) {
        fill = on ? rgb(24, 24, 30) : rcol(h->arg, act ? 30 : 16);
        tc = on ? C_DIM : rcol(h->arg, 100);
    } else if (on) {
        fill = h->id == S_SLOT ? rgb(60, 52, 24) : rgb(70, 58, 20);
        tc = C_GOLD;
    } else if (h->id >= P_HITS && h->id <= P_LVL) {
        fill = rcol(s_sel, act ? 26 : 13);
    }
    float rr = h->d < h->c ? h->d * 0.22f : h->c * 0.22f;
    p_rrect(h->a, h->b, h->c, h->d, rr, C_EDGE, 256);
    p_rrect(h->a + 1.5f, h->b + 1.5f, h->c - 3, h->d - 3, rr - 1, fill, 256);
    float cx = h->a + h->c / 2, cy = h->b + h->d / 2;
    if (L.kind == L_SIDE && h->id >= P_HITS && h->id <= P_LVL) {
        // the halves of a value row: less at the left, more at the right
        float ch = h->d * 0.16f;
        p_tri(h->a + 6, cy, h->a + 6 + ch, cy - ch, h->a + 6 + ch, cy + ch, rcol(s_sel, 80), 256);
        p_tri(h->a + h->c - 6, cy, h->a + h->c - 6 - ch, cy - ch, h->a + h->c - 6 - ch, cy + ch, rcol(s_sel, 80), 256);
    }
    if (ic) {
        float isz = h->d * 0.42f > 26 ? 26 : h->d * 0.42f;
        bool two = *lbl && h->d > 2.2f * isz + L.fl->line;
        icon(ic, cx, two ? cy - L.fl->line * 0.45f : cy, isz, tc);
        if (two) {
            p_text(fit(lbl, h->c - 6, L.fl, MF(&MF_SMALL), NULL), cx, (int)(cy + isz * 0.5f + L.fl->asc + 2), lbl,
                   C_DIM, 1);
        }
        return;
    }
    if (*lbl && *val) {
        const font_t *fv = fit(val, h->c - 8, L.fv, L.fl, MF(&MF_SMALL));
        int tot = L.fl->asc + fv->asc + 6;
        p_text(fit(lbl, h->c - 6, L.fl, MF(&MF_SMALL), NULL), cx, (int)(cy - tot / 2 + L.fl->asc), lbl, C_DIM, 1);
        p_text(fv, cx, (int)(cy + tot / 2), val, tc, 1);
    } else {
        static const char *const SHORT[NMODE][2] = {{"MAJOR", "מז'ור"}, {"MINOR", "מינור"}, {"DORIAN", "דורי"},
                                                    {"PHRYG", "פריגי"}, {"LYDIAN", "לידי"}, {"MIXO", "מיקסו"},
                                                    {"HARM", "הרמוני"}, {"PENTA", "פנטה"}};
        const char *t = *val ? val : lbl;
        if (h->id == K_MODE && p_textw(L.fv, t) > h->c - 8) {
            t = SHORT[h->arg % NMODE][s_he];
        }
        bool big = h->id == K_KEY || h->id == S_SLOT || h->id == B_MINUS || h->id == B_PLUS;
        const font_t *f = big ? fit(t, h->c - 8, L.fb, L.fv, L.fl) : fit(t, h->c - 8, L.fv, L.fl, MF(&MF_SMALL));
        p_text(f, cx, (int)(cy + f->asc / 2), t, tc, 1);
    }
}

// ---------------------------------------------------------------- the circle of all rings (V_MAIN)

static void centre_static(void)
{
    float rc = L.rc, k = rc / 50;
    p_disc(L.cx, L.cy, rc, rgb(14, 16, 28), 256, false);
    p_ring(L.cx, L.cy, rc - 0.8f * k, 1.6f * k, s_act == E_CENTRE ? rgb(160, 170, 255) : C_EDGE, 256, false);
    char b[16];
    snprintf(b, sizeof b, "%d", P->bpm);
    const font_t *f = L.kind == L_BAR ? L.fv : MF(rc > 40 ? &MF_N28 : &MF_N20);
    icon(B_PLAY, L.cx, L.cy - rc * 0.46f, rc * 0.42f, s_run ? rgb(255, 120, 120) : rgb(140, 255, 170));
    p_text(f, L.cx, (int)(L.cy + f->asc * 0.55f + rc * 0.08f), b, C_TEXT, 1);
    if (rc > 30) {
        p_text(MF(&MF_TINY), L.cx, (int)(L.cy + rc * 0.72f), "BPM", C_DIM, 1);
    }
}
// the dots of ring i on the circle of all rings
static void ring_dots(int i)
{
    const ring_t *g = &P->r[i];
    bool on = audible(P, i);
    int f = on ? 100 : 28;
    float r = ring_r(i), k = L.gd / 17;
    p_ring(L.cx, L.cy, r, 1.3f * k, rcol(i, on ? 46 : 16), 256, false);
    p_ring(L.cx, L.cy, r, 6 * k, rcol(i, f), 26, true);
    for (int j = 0; j < g->n; j++) {
        float a = step_a(j, g->n), pr = r + pitch_f(i, g->note[j]) * 0.5f * L.gd;
        float x = pt_x(pr, a), y = pt_y(pr, a);
        if (g->vel[j]) {
            float dr = (0.16f + 0.05f * g->vel[j]) * L.gd;
            p_glow(x, y, dr * 2.6f, rcol(i, f), 80);
            p_disc(x, y, dr, rcol(i, f), 256, false);
            p_disc(x, y, dr * 0.42f, WHITE, on ? 150 : 40, false);
        } else {
            p_disc(x, y, 0.1f * L.gd + 0.5f, rcol(i, f * 45 / 100), 256, false);
        }
    }
}
static void rim_static(void)
{
    float rm = (L.rim0 + L.R) / 2, k = L.s;
    for (int i = 0; i < s_nhit; i++) {
        const hit_t *h = &s_hit[i];
        if (h->sh != SH_SECT) {
            continue;
        }
        bool act = s_act == h->id;
        float a0 = h->e + 0.02f, a1 = h->f - 0.02f, am = (h->e + h->f) / 2;
        p_arc(L.cx, L.cy, rm, L.R - L.rim0 - 2 * k, a0, a1, act ? rgb(40, 44, 76) : rgb(17, 19, 32), 256, false);
        p_arc(L.cx, L.cy, L.rim0 + 2 * k, 2 * k, a0, a1, act ? rgb(170, 180, 255) : C_EDGE, 256, false);
        char lbl[40], val[40];
        int ic;
        elem_text(h->id, h->arg, lbl, val, &ic);
        float x = pt_x(rm, am), y = pt_y(rm, am);
        if (h->id == B_KEY || h->id == B_SWING) {
            p_text(L.fl, x, (int)(y - 3 * k), lbl, C_DIM, 1);
            p_text(fit(val, 82 * k, L.fv, L.fl, NULL), x, (int)(y + 17 * k), val, C_TEXT, 1);
        } else if (h->id == B_MODE || h->id == B_UNDO) {
            bool dim = h->id == B_UNDO && s_undo_n == 0;
            icon(ic, x, y, 26 * k, dim ? rgb(60, 64, 90) : C_TEXT);
        } else {
            icon(ic, x, y - 9 * k, 22 * k, C_TEXT);
            p_text(fit(lbl, 74 * k, L.fl, MF(&MF_SMALL), NULL), x, (int)(y + 17 * k), lbl, C_DIM, 1);
        }
    }
}
static void main_static(void)
{
    for (int i = 0; i < NR; i++) {
        ring_dots(i);
    }
    centre_static();
    if (L.kind == L_ROUND) {
        rim_static();
    }
}

// ---------------------------------------------------------------- one ring big (V_RING)

static void zoom_dot_pos(int k, float *x, float *y, float *pr)
{
    const ring_t *g = &P->r[s_sel];
    float dr = zoom_dot(g->n), a = step_a(k, g->n);
    *pr = L.zr + pitch_f(s_sel, g->note[k]) * (L.zo - L.zi - 2 * dr) * 0.9f;
    *x = pt_x(*pr, a);
    *y = pt_y(*pr, a);
}
static void zoom_step(int k)
{
    const ring_t *g = &P->r[s_sel];
    float dr = zoom_dot(g->n), x, y, pr, kk = L.s;
    zoom_dot_pos(k, &x, &y, &pr);
    bool on = audible(P, s_sel);
    int f = on ? 100 : 35;
    if (g->vel[k]) {
        float d = dr * (0.62f + 0.13f * g->vel[k]);
        p_glow(x, y, d * 2.4f, rcol(s_sel, f), 90);
        p_disc(x, y, d, rcol(s_sel, f), 256, false);
        p_disc(x, y, d * 0.4f, WHITE, 140, false);
        if (g->vel[k] == 3) {
            p_ring(x, y, d + 3.5f * kk, 1.6f * kk, WHITE, 170, false);
        }
        if (melodic(s_sel) && L.kind != L_BAR) {
            char b[12];
            note_label(P, s_sel, g->note[k], b, sizeof b);
            float a = step_a(k, g->n), lr = pr < L.zr ? pr + dr + 12 * kk : pr - dr - 12 * kk;
            p_text(L.fl, pt_x(lr, a), (int)(pt_y(lr, a) + L.fl->asc / 2), b, C_TEXT, 1);
        }
    } else {
        p_ring(x, y, dr * 0.5f, 1.6f * kk, rcol(s_sel, f * 40 / 100), 256, false);
    }
}
static void pad_static(const hit_t *h)
{
    bool act = s_act == h->id;
    float rm = (L.pin + L.pout) / 2, am = (h->e + h->f) / 2, k = L.s;
    p_arc(L.cx, L.cy, rm, L.pout - L.pin, h->e + 0.03f, h->f - 0.03f, rcol(s_sel, act ? 28 : 12), 256, false);
    p_arc(L.cx, L.cy, L.pout - 1.5f * k, 2 * k, h->e + 0.03f, h->f - 0.03f, rcol(s_sel, act ? 90 : 40), 256, false);
    char lbl[40], val[40];
    int ic;
    elem_text(h->id, h->arg, lbl, val, &ic);
    float x = pt_x(rm + 4 * k, am), y = pt_y(rm + 4 * k, am);
    bool side = h->id == P_STEPS || h->id == P_LVL;
    float w = side ? 70 * k : 120 * k;
    const font_t *fv = fit(val, w, L.fb, L.fv, L.fl);
    p_text(L.fl, x, (int)(y - 6 * k), lbl, rcol(s_sel, 70), 1);
    p_text(fv, x, (int)(y + 4 * k + fv->asc), val, C_TEXT, 1);
}
static void zoom_static(void)
{
    const ring_t *g = &P->r[s_sel];
    float k = L.R / 233;
    p_ring(L.cx, L.cy, L.zr, 1.6f * k, rcol(s_sel, 40), 256, false);
    p_ring(L.cx, L.cy, L.zr, 10 * k, rcol(s_sel, 100), 18, true);
    for (int j = 0; j < g->n; j++) {
        float a = step_a(j, g->n);
        p_line(pt_x(L.zi + 4 * k, a), pt_y(L.zi + 4 * k, a), pt_x(L.zo - 2 * k, a), pt_y(L.zo - 2 * k, a), 1.4f * k,
               rcol(s_sel, j == 0 ? 50 : 22), 256, false);
    }
    for (int j = 0; j < g->n; j++) {
        zoom_step(j);
    }
    p_disc(L.cx, L.cy, L.zc, rgb(14, 16, 28), 256, false);
    p_ring(L.cx, L.cy, L.zc - k, 2 * k, rcol(s_sel, s_act == E_CENTRE ? 100 : 60), 256, false);
    const font_t *f = fit(RNAME[s_sel][s_he], L.zc * 1.6f, L.fv, L.fl, MF(&MF_SMALL));
    p_text(f, L.cx, (int)(L.cy + f->asc / 2 + 6 * k), RNAME[s_sel][s_he], rcol(s_sel, 100), 1);
    icon(B_BACK, L.cx, L.cy - 22 * k, 16 * k, C_DIM);
    if (L.kind == L_ROUND) {
        // the arrows of the swipe to the next ring
        float y = L.cy + 6 * k, dx = L.zc - 9 * k;
        p_tri(L.cx - dx, y, L.cx - dx + 6 * k, y - 6 * k, L.cx - dx + 6 * k, y + 6 * k, C_DIM, 200);
        p_tri(L.cx + dx, y, L.cx + dx - 6 * k, y - 6 * k, L.cx + dx - 6 * k, y + 6 * k, C_DIM, 200);
        for (int i = 0; i < s_nhit; i++) {
            if (s_hit[i].id >= P_HITS && s_hit[i].id <= P_LVL) {
                pad_static(&s_hit[i]);
            }
        }
    }
}
// the lane of one ring (BAR V_RING)
static void lane_static(void)
{
    const ring_t *g = &P->r[s_sel];
    float cw = L.lw / g->n;
    p_rrect(L.lx, L.ly, L.lw, L.lh, 10, rcol(s_sel, 9), 256);
    for (int j = 0; j < g->n; j++) {
        float x = L.lx + j * cw;
        p_rrect(x + 2, L.ly + 3, cw - 4, L.lh - 6, 6, rcol(s_sel, j % 4 == 0 ? 17 : 12), 256);
        int v = g->vel[j];
        if (!v) {
            continue;
        }
        float cx = x + cw / 2;
        if (melodic(s_sel)) {
            float y = L.ly + L.lh / 2 - pitch_f(s_sel, g->note[j]) * (L.lh - 44);
            float d = cw * 0.18f + 3 * v;
            d = d > 14 ? 14 : d;
            p_glow(cx, y, d * 2.2f, rcol(s_sel, 100), 80);
            p_disc(cx, y, d, rcol(s_sel, 100), 256, false);
            char b[12];
            note_label(P, s_sel, g->note[j], b, sizeof b);
            const font_t *f = fit(b, cw - 2, L.fl, MF(&MF_SMALL), MF(&MF_TINY));
            p_text(f, cx, (int)(y < L.ly + L.lh / 2 ? y + d + f->asc + 3 : y - d - 4), b, C_TEXT, 1);
        } else {
            float h = (L.lh - 12) * (0.35f + 0.21f * v);
            p_rrect(x + 5, L.ly + L.lh - 6 - h, cw - 10, h, 5, rcol(s_sel, 100), 256);
        }
    }
    const font_t *f = L.fl;
    p_rrect(L.lx + 3, L.ly + 3, p_textw(f, RNAME[s_sel][s_he]) + 12, f->line + 4, 6, rgb(10, 10, 18), 200);
    p_text(f, L.lx + 9, (int)(L.ly + f->asc + 5), RNAME[s_sel][s_he], rcol(s_sel, 100), 0);
}

// ---------------------------------------------------------------- the keys and the slots

static void key_static(void)
{
    float k = L.R / 233;
    for (int i = 0; i < s_nhit; i++) {
        const hit_t *h = &s_hit[i];
        if (h->id == K_KEY && h->sh == SH_SECT) {
            float am = (h->e + h->f) / 2, r = L.kind == L_ROUND ? 190 * k : 0.76f * L.R,
                  d = L.kind == L_ROUND ? 28 * k : 0.17f * L.R;
            bool on = elem_on(K_KEY, h->arg);
            float x = pt_x(r, am), y = pt_y(r, am);
            p_disc(x, y, d, on ? C_GOLD : C_EDGE, 256, false);
            if (!on) {
                p_disc(x, y, d - 1.8f * k, C_PANEL, 256, false);
            }
            const font_t *f = fit(KEY_NAME[h->arg], d * 1.7f, L.fv, L.fl, NULL);
            p_text(f, x, (int)(y + f->asc / 2), KEY_NAME[h->arg], on ? rgb(30, 24, 8) : C_TEXT, 1);
        } else if (h->id == K_DONE && h->sh == SH_DISC) {
            p_disc(L.cx, L.cy, h->c - 2, C_EDGE, 256, false);
            p_disc(L.cx, L.cy, h->c - 4, C_PANEL, 256, false);
            char b[32];
            key_text(b, sizeof b);
            const font_t *f = fit(b, h->c * 1.8f, L.fv, L.fl, MF(&MF_SMALL));
            p_text(f, L.cx, (int)(L.cy - 4), b, C_GOLD, 1);
            p_text(L.fl, L.cx, (int)(L.cy + L.fl->asc + 8), tx("DONE", "סיום"), C_TEXT, 1);
        }
    }
    if (L.kind == L_BAR) {
        char b[32];
        key_text(b, sizeof b);
        p_text(L.fb, 16, 158, b, C_GOLD, 0);
    }
}
// a small picture of a pattern: its rings round (x, y)
static void slot_mini(const pat_t *p, float x, float y, float r)
{
    for (int i = 0; i < NR; i++) {
        const ring_t *g = &p->r[i];
        float rr = r * (0.3f + 0.7f * i / (NR - 1));
        p_ring(x, y, rr, 1, rcol(i, 22), 256, false);
        for (int j = 0; j < g->n; j++) {
            if (g->vel[j]) {
                float a = step_a(j, g->n);
                p_disc(x + rr * fsin(a), y - rr * fcos(a), r * 0.07f + 0.6f, rcol(i, 100), 256, false);
            }
        }
    }
}
static void slots_static(void)
{
    float k = L.R / 233;
    for (int i = 0; i < s_nhit; i++) {
        const hit_t *h = &s_hit[i];
        if (h->id != S_SLOT) {
            continue;
        }
        bool on = S->cur == h->arg, used = S->used[h->arg];
        if (h->sh == SH_SECT) {
            float rm = (h->c + L.R) / 2, am = (h->e + h->f) / 2, w = L.R - h->c - 4 * k;
            p_arc(L.cx, L.cy, rm, w, h->e + 0.02f, h->f - 0.02f, on ? rgb(52, 44, 18) : C_PANEL, 256, false);
            p_arc(L.cx, L.cy, h->c + 1.5f * k, 2 * k, h->e + 0.02f, h->f - 0.02f, on ? C_GOLD : C_EDGE, 256, false);
            float x = pt_x(rm + 6 * k, am), y = pt_y(rm + 6 * k, am);
            if (used) {
                slot_mini(&S->slot[h->arg], x, y, w * 0.33f);
            }
            float nx = pt_x(h->c + 14 * k, am), ny = pt_y(h->c + 14 * k, am);
            char b[4];
            snprintf(b, sizeof b, "%d", h->arg + 1);
            p_text(L.fv, nx, (int)(ny + L.fv->asc / 2), b, on ? C_GOLD : used ? C_TEXT : C_DIM, 1);
        } else {
            draw_button(h);
            if (used) {
                slot_mini(&S->slot[h->arg], h->a + h->c / 2, h->b + h->d * 0.62f, h->c * 0.36f);
            }
        }
    }
    if (L.kind != L_BAR) {
        const hit_t *c = hit_find(E_CENTRE, 0);
        float r = c ? c->c : 60;
        p_disc(L.cx, L.cy, r - 2, C_PANEL, 256, false);
        p_ring(L.cx, L.cy, r - 2, 2 * k, C_EDGE, 256, false);
        icon(B_BACK, L.cx, L.cy - r * 0.32f, r * 0.3f, C_DIM);
        p_text(fit(tx("SLOTS", "תבניות"), r * 1.6f, L.fv, L.fl, NULL), L.cx, (int)(L.cy + 8 * k), tx("SLOTS", "תבניות"),
               C_TEXT, 1);
        if (L.kind == L_ROUND) {
            p_text(L.fl, L.cx, (int)(L.cy + 38 * k), tx("HOLD = COPY HERE", "החזקה = העתק לכאן"), C_DIM, 1);
        }
    }
}

// ---------------------------------------------------------------- the still layer of the screen now

static void draw_static(void)
{
    p_fill_rect(s_cx0, s_cy0, s_cx1 - s_cx0, s_cy1 - s_cy0, 0);
    if (s_view == V_MAIN) {
        main_static();
    } else if (s_view == V_RING) {
        if (L.kind == L_BAR) {
            lane_static();
        } else {
            zoom_static();
        }
    } else if (s_view == V_KEY) {
        key_static();
    } else {
        slots_static();
    }
    for (int i = 0; i < s_nhit; i++) {
        const hit_t *h = &s_hit[i];
        if (h->sh == SH_RECT && h->id != E_LANE && !(s_view == V_SLOTS && h->id == S_SLOT)) {
            draw_button(h);
        }
    }
}
static bool s_bg_all;   // the layer must be drawn again (all of it)
static int s_bq[8][4];  // boxes of the layer to draw again
static int s_bqn;
static void bg_all(void)
{
    s_bg_all = true;
}
static void bg_box(float x0, float y0, float x1, float y1)
{
    if (s_bqn >= 8) {
        s_bg_all = true;
        return;
    }
    int *b = s_bq[s_bqn++];
    b[0] = ifl(x0) - 2;
    b[1] = ifl(y0) - 2;
    b[2] = ice(x1) + 2;
    b[3] = ice(y1) + 2;
}
static void bg_hit(const hit_t *h)
{
    if (!h) {
        return;
    }
    if (h->sh == SH_RECT) {
        bg_box(h->a, h->b, h->a + h->c, h->b + h->d);
    } else if (h->sh == SH_DISC) {
        bg_box(h->a - h->c, h->b - h->c, h->a + h->c, h->b + h->c);
    } else {
        // the box of a sector: its corners and the points of the circle in it
        float r1 = h->d > L.R ? L.R : h->d, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int i = 0; i <= 8; i++) {
            float a = h->e + (h->f - h->e) * i / 8;
            for (int k = 0; k < 2; k++) {
                float r = k ? r1 : h->c, x = pt_x(r, a), y = pt_y(r, a);
                x0 = x < x0 ? x : x0;
                y0 = y < y0 ? y : y0;
                x1 = x > x1 ? x : x1;
                y1 = y > y1 ? y : y1;
            }
        }
        bg_box(x0, y0, x1, y1);
    }
}
static void bg_flush(void)
{
    if (s_bg_all) {
        target_bg(0, 0, s_W, s_H);
        draw_static();
        full_both();
        s_bg_all = false;
        s_bqn = 0;
    }
    for (int i = 0; i < s_bqn; i++) {
        int *b = s_bq[i];
        target_bg(b[0], b[1], b[2], b[3]);
        draw_static();
        mark_both(s_cx0, s_cy0, s_cx1, s_cy1);
    }
    s_bqn = 0;
}

// ---------------------------------------------------------------- the moving parts

// the angle of the arm of ring i now
static float arm_a(int i)
{
    float n = P->poly ? P->r[i].n : 16;
    float q = s_ph / n;
    return 6.2831853f * (q - (int)q);
}
static void flash_dot(int i, float x, float y, float dr)
{
    int64_t dt = s_now - s_flash_t[i];
    if (dt < 0 || dt > 260 || !s_flash_t[i]) {
        return;
    }
    float f = 1.0f - dt / 260.0f;
    p_glow(x, y, dr * (2.0f + 1.8f * (1 - f)), rcol(i, 100), (int)(230 * f));
    p_disc(x, y, dr * (0.9f + 0.4f * (1 - f)), WHITE, (int)(190 * f), true);
}
static void main_moving(void)
{
    float k = L.gd / 17;
    for (int i = 0; i < NR; i++) {
        const ring_t *g = &P->r[i];
        float r = ring_r(i);
        if (s_run) {
            float a = arm_a(i);
            for (int t = 0; t < 4; t++) {
                float at = a - t * 0.08f;
                p_glow(pt_x(r, at), pt_y(r, at), 0.58f * L.gd, rcol(i, 100), 130 * (4 - t) / 4);
            }
            if (P->poly) {
                p_line(pt_x(r - 0.42f * L.gd, a), pt_y(r - 0.42f * L.gd, a), pt_x(r + 0.42f * L.gd, a),
                       pt_y(r + 0.42f * L.gd, a), 1.6f * k, WHITE, 200, true);
            }
        }
        int kk = s_flash_k[i];
        if (kk < g->n && g->vel[kk]) {
            float pr = r + pitch_f(i, g->note[kk]) * 0.5f * L.gd, a = step_a(kk, g->n);
            flash_dot(i, pt_x(pr, a), pt_y(pr, a), (0.16f + 0.05f * g->vel[kk]) * L.gd);
        }
    }
    if (s_pick >= 0 && s_pick < NR) {
        // the ring under the finger: a bright ring and its name on the other side of the circle
        float r = ring_r(s_pick);
        p_ring(L.cx, L.cy, r, 6 * k, rcol(s_pick, 100), 150, true);
        p_ring(L.cx, L.cy, r, 2 * k, WHITE, 200, true);
        const char *nm = RNAME[s_pick][s_he];
        const font_t *f = L.fv;
        float w = p_textw(f, nm) + 24, h = f->line + 10;
        float y = s_pick_low ? L.cy - 0.62f * L.R : L.cy + 0.62f * L.R - h;
        p_rrect(L.cx - w / 2, y, w, h, h / 2, rcol(s_pick, 100), 256);
        p_rrect(L.cx - w / 2 + 2, y + 2, w - 4, h - 4, h / 2 - 2, rgb(10, 10, 20), 256);
        p_text(f, L.cx, (int)(y + 5 + f->asc), nm, rcol(s_pick, 100), 1);
    }
    if (s_run && !P->poly) {
        float a = arm_a(0), r0 = L.rc + 2 * k, r1 = ring_r(NR - 1) + 0.6f * L.gd;
        float x0 = pt_x(r0, a), y0 = pt_y(r0, a), x1 = pt_x(r1, a), y1 = pt_y(r1, a);
        p_line(x0, y0, x1, y1, 7 * k, rgb(90, 110, 255), 60, true);
        p_line(x0, y0, x1, y1, 1.7f * k, rgb(235, 238, 255), 220, true);
    }
}
static void ring_moving(void)
{
    const ring_t *g = &P->r[s_sel];
    float k = L.R / 233;
    if (L.kind == L_BAR) {
        float q = s_ph / (P->poly ? g->n : 16);
        float x = L.lx + L.lw * (q - (int)q);
        if (s_run) {
            p_line(x, L.ly + 2, x, L.ly + L.lh - 2, 7, rcol(s_sel, 100), 60, true);
            p_line(x, L.ly + 2, x, L.ly + L.lh - 2, 2, WHITE, 200, true);
        }
        int kk = s_flash_k[s_sel];
        if (kk < g->n && g->vel[kk]) {
            float cw = L.lw / g->n;
            flash_dot(s_sel, L.lx + (kk + 0.5f) * cw, L.ly + L.lh / 2, cw * 0.3f);
        }
        return;
    }
    if (s_run) {
        float a = arm_a(s_sel);
        for (int t = 0; t < 9; t++) {
            float at = a - t * 0.04f;
            p_glow(pt_x(L.zr, at), pt_y(L.zr, at), 15 * k, rcol(s_sel, 100), 120 * (9 - t) / 9);
        }
        p_line(pt_x(L.zi, a), pt_y(L.zi, a), pt_x(L.zo, a), pt_y(L.zo, a), 7 * k, rcol(s_sel, 100), 70, true);
        p_line(pt_x(L.zi, a), pt_y(L.zi, a), pt_x(L.zo, a), pt_y(L.zo, a), 1.8f * k, WHITE, 220, true);
    }
    int kk = s_flash_k[s_sel];
    if (kk < g->n && g->vel[kk]) {
        float x, y, pr;
        zoom_dot_pos(kk, &x, &y, &pr);
        flash_dot(s_sel, x, y, zoom_dot(g->n) * 0.9f);
    }
    // the zoom in: the ring grows from its place on the circle of all rings
    int64_t dt = s_now - s_zoom_t0;
    if (dt >= 0 && dt < 260) {
        float f = dt / 260.0f, e = 1 - (1 - f) * (1 - f), r = s_zoom_from + (L.zr - s_zoom_from) * e;
        p_ring(L.cx, L.cy, r, 3 * k, rcol(s_sel, 100), (int)(220 * (1 - f)), true);
    }
}
static void toast_moving(void)
{
    if (s_now >= s_toast_until) {
        return;
    }
    float f = (s_toast_until - s_now) / 300.0f;
    int a = f >= 1 ? 256 : (int)(256 * f);
    const font_t *fa = L.fv, *fb = L.fl;
    float w = p_textw(fa, s_toast[0]), w2 = p_textw(fb, s_toast[1]);
    w = (w > w2 ? w : w2) + 30;
    float h = *s_toast[1] ? fa->line + fb->line + 16 : fa->line + 14;
    float x = L.kind == L_BAR ? gfx_w - w - 10 : L.cx - w / 2;
    float y = L.kind == L_BAR ? gfx_h - h - 8
                              : L.cy +
                                        (s_view == V_MAIN    ? 0.5f
                                         : s_view == V_SLOTS ? 0.62f
                                                             : 0.28f) *
                                                L.R -
                                        h / 2;
    if (L.kind == L_SIDE) {
        x = L.cx - w / 2;
        y = L.cy + 0.5f * L.R - h / 2;
    }
    p_rrect(x, y, w, h, h / 3, rgb(150, 160, 255), a * 3 / 4);
    p_rrect(x + 2, y + 2, w - 4, h - 4, h / 3 - 2, rgb(12, 14, 28), a);
    p_text(fa, x + w / 2, (int)(y + 7 + fa->asc), s_toast[0], C_TEXT, 1);
    if (*s_toast[1]) {
        p_text(fb, x + w / 2, (int)(y + 9 + fa->line + fb->asc), s_toast[1], C_DIM, 1);
    }
}
static void hold_moving(void)
{
    if (s_hold_slot < 0) {
        return;
    }
    const hit_t *h = hit_find(S_SLOT, s_hold_slot);
    float f = (s_now - s_hold_t0) / 1000.0f;
    if (!h || f < 0.12f) {
        return;
    }
    f = f > 1 ? 1 : f;
    if (h->sh == SH_SECT) {
        int n = (int)(12 * f);
        for (int i = 0; i <= n; i++) {
            float a = h->e + (h->f - h->e) * i / 12;
            p_glow(pt_x(L.R - 8 * L.s, a), pt_y(L.R - 8 * L.s, a), 10 * L.s, C_GOLD, 200);
        }
    } else {
        p_rrect(h->a + 4, h->b + h->d - 8, (h->c - 8) * f, 5, 2, C_GOLD, 256);
    }
}
static void draw_moving(void)
{
    if (s_view == V_MAIN) {
        main_moving();
    } else if (s_view == V_RING) {
        ring_moving();
    } else if (s_view == V_SLOTS) {
        hold_moving();
    }
    toast_moving();
}
