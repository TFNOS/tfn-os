// INTEGRALS: the screens: the title, the contents (the chapters), a chapter (its lessons); ig_les.inc has the lesson
// and the dispatch. Three layouts by the size of the frame: the bar (640 x 172), the box (320 x 240: the CoreS3, the
// 1.75C and the Watcher in the FIT frame) and the tablet (640 x 360). A Hebrew screen is the mirror picture.

enum {
    ST_TITLE,
    ST_TOC,
    ST_CHAP,
    ST_LES,
    ST_GRAPH,
    ST_FAIL
};
enum {
    LY_BAR,
    LY_BOX,
    LY_TAB
};

typedef struct {
    int x, y, w, h;
} rc_t;

static int s_st = ST_TITLE, s_next = -1;
static float s_tr = 1.0f;  // the fade between two screens: 0..0.5 the old one goes, 0.5..1 the new one comes
static int64_t s_now, s_last, s_touch_t;
static int s_cc, s_li, s_si, s_vi;  // the place: the chapter, the lesson, the step, the value of the parameter
static bool s_has_save;
static int s_sv_cc, s_sv_li, s_sv_si, s_sv_vi;  // the saved place (CONTINUE)
static uint32_t s_seen[2];
static tarena_t *s_arL[2], *s_arT, *s_arC, *s_arH;
static int s_total_les, s_total_steps;
static int s_dn_x, s_dn_y;  // the touch down
static bool s_drag;         // a scroll or a drag of the parameter is on

// The round frame (the 1.75C: 466 x 466, the Watcher: 412 x 412) uses the faces of the tablet layout, its own places
// (RS: a size of the 466 layout, scaled to the frame) and the small graph of the box layout.
#define RS(v) ((v) * gfx_w / 466)
static inline bool rnd(void)
{
    return gfx_round();
}
static inline int lay(void)
{
    if (gfx_round()) {
        return LY_TAB;
    }
    if (gfx_h <= 200) {
        return LY_BAR;
    }
    if (gfx_w >= 560 && gfx_h >= 300) {
        return LY_TAB;
    }
    return LY_BOX;
}
// the lesson has a small graph (a tap opens the big graph): the box layout and the round frame
static inline bool mini_graph(void)
{
    return lay() == LY_BOX || rnd();
}
// the width of the circle at the row y, less the margin m on each side (the round frame)
static inline int chord_w(int y, int m)
{
    const float r = gfx_w * 0.5f, dy = y - gfx_h * 0.5f;
    const float h = r * r - dy * dy;
    const int w = h > 0 ? (int)(2.0f * sqrtf(h)) - 2 * m : 0;
    return w > 0 ? w : 0;
}
static inline bool in_rc(const rc_t *r, int x, int y)
{
    return x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h;
}
static inline rc_t mir(rc_t r)
{
    if (s_he) {
        r.x = gfx_w - r.x - r.w;
    }
    return r;
}
static inline rc_t RC(int x, int y, int w, int h)
{
    return (rc_t){x, y, w, h};
}
static inline int sec_of(int ch)
{
    return s_ch[ch].section & 3;
}

// ---------------------------------------------------------------- the saves
static inline int les_global(int ch, int li)
{
    return s_ch[ch].first + li;
}
static bool seen(int ch, int li)
{
    const int g = les_global(ch, li);
    return g < 64 && ((s_seen[g >> 5] >> (g & 31)) & 1);
}
static void mark_seen(int ch, int li)
{
    const int g = les_global(ch, li);
    if (g < 64 && !seen(ch, li)) {
        s_seen[g >> 5] |= 1u << (g & 31);
        store_set(g < 32 ? "integral_seen0" : "integral_seen1", s_seen[g >> 5]);
    }
}
static void ui_save(void)
{
    if (s_db && (s_st == ST_LES || s_st == ST_GRAPH || s_st == ST_CHAP)) {
        const uint32_t v =
                1u << 24 | (uint32_t)s_cc | (uint32_t)s_li << 5 | (uint32_t)s_si << 10 | (uint32_t)s_vi << 16;
        store_set("integral_pos", v);
        s_has_save = true;
        s_sv_cc = s_cc;
        s_sv_li = s_li;
        s_sv_si = s_si;
        s_sv_vi = s_vi;
    }
}
static void load_save(void)
{
    const uint32_t v = store_get("integral_pos", 0);
    s_seen[0] = store_get("integral_seen0", 0);
    s_seen[1] = store_get("integral_seen1", 0);
    s_has_save = false;
    if (!((v >> 24) & 1)) {
        return;
    }
    const int ch = v & 31, li = (v >> 5) & 31, si = (v >> 10) & 63, vi = (v >> 16) & 31;
    if (ch < s_nch && li < s_ch[ch].n) {
        s_cc = ch;
        s_li = li;
        const igd_lesson_t *l = les_at(ch, li);
        s_vi = vi < l->nvals ? vi : l->def;
        s_si = si < var_at(ch, li, s_vi)->nsteps ? si : 0;
        s_has_save = true;
        s_sv_cc = s_cc;
        s_sv_li = s_li;
        s_sv_si = s_si;
        s_sv_vi = s_vi;
    }
}

// ---------------------------------------------------------------- small parts
// a text in one line that fits w: the biggest of the faces; a long one is cut at a word with "..."
static const kfont_t *fit_face(const char *s, int w, const kfont_t *const *faces, int n)
{
    for (int i = 0; i < n; i++) {
        if (kf_width(faces[i], s) <= w) {
            return faces[i];
        }
    }
    return faces[n - 1];
}
static void line_fit(const char *s, int x, int base, int w, const kfont_t *f, uint16_t col, int alpha, int align)
{
    static char buf[160];
    snprintf(buf, sizeof buf, "%s", s);
    if (kf_width(f, buf) > w) {
        // cut the last words until the text and "..." fit
        char *sp;
        while ((sp = strrchr(buf, ' ')) != NULL && sp != buf) {
            *sp = 0;
            const size_t n = strlen(buf);
            if (n + 4 < sizeof buf) {
                strcpy(buf + n, "...");
                if (kf_width(f, buf) <= w) {
                    break;
                }
                buf[n] = 0;
            }
        }
    }
    const int tw = kf_width(f, buf);
    const int cx = align < 0 ? x + tw / 2 : align > 0 ? x + w - tw / 2 : x + w / 2;
    kf_line_c(f, cx, base, buf, col, fa(alpha));
}
// a line at the start side (the left in English, the right in Hebrew) of the box x..x+w
static inline void line_start(const char *s, int x, int base, int w, const kfont_t *f, uint16_t col, int alpha)
{
    line_fit(s, x, base, w, f, col, alpha, s_he ? 1 : -1);
}

// a chevron (< or >) of thin lines; dir -1 = it points to the left
static void chevron(float cx, float cy, float r, int dir, uint16_t c, int alpha)
{
    tfd_line(cx - dir * r * 0.5f, cy - r, cx + dir * r * 0.5f, cy, 2.2f, c, fa(alpha));
    tfd_line(cx + dir * r * 0.5f, cy, cx - dir * r * 0.5f, cy + r, 2.2f, c, fa(alpha));
}
// the back key: a chevron in a soft circle at the start corner
static rc_t back_rc(void)
{
    const int L = lay();
    if (rnd()) {
        return mir(RC(RS(62), RS(36), RS(72), RS(66)));  // the start side of the top cap
    }
    return mir(L == LY_BAR ? RC(0, 0, 60, 40) : L == LY_TAB ? RC(0, 0, 76, 56) : RC(0, 0, 54, 44));
}
static void back_draw(void)
{
    const rc_t r = back_rc();
    const int L = lay();
    const int rad = L == LY_TAB ? 17 : 13;
    float cx = s_he ? r.x + r.w - 6 - rad : r.x + 6 + rad;
    float cy = L == LY_TAB ? 26 : L == LY_BAR ? 17 : 21;
    if (rnd()) {
        cx = r.x + r.w / 2.0f;
        cy = r.y + r.h / 2.0f;
    }
    tfd_disc(cx, cy, rad, C_CARD, fa(230));
    chevron(cx, cy, rad * 0.42f, s_he ? 1 : -1, C_INK, 230);
}

// a soft button with a text in the middle; primary = the colour of the section
static void button(rc_t r, const char *txt, bool primary, int sec, int alpha)
{
    const uint16_t fill = primary ? sec_col(sec, 255) : C_CARD;
    tfd_rrect(r.x, r.y, r.w, r.h, r.h / 2 > 18 ? 18 : r.h / 2, fill, fa(alpha * (primary ? 235 : 255) / 255));
    if (!primary) {
        tfd_rrect(r.x + 1, r.y + 1, r.w - 2, r.h - 2, 16, C_LINE, fa(alpha * 60 / 255));
    }
    const kfont_t *const faces[] = {&KF_BIG, &KF_TEXT, &KF_NAME, &KF_LABEL};
    const kfont_t *f = fit_face(txt, r.w - 20, r.h >= 50 ? faces : faces + 1, r.h >= 50 ? 4 : 3);
    kf_line_c(f, r.x + r.w / 2, r.y + r.h / 2 + f->asc / 2, txt, primary ? rgb(20, 16, 10) : C_INK, fa(alpha));
}

// ---------------------------------------------------------------- the title
static tform_t s_tt_sign;
static rc_t title_btn(int i)
{
    const int L = lay();
    if (rnd()) {
        // the bottom cap: one wide key, or two keys side by side
        if (!s_has_save) {
            return RC(gfx_w / 2 - RS(120), RS(330), RS(240), RS(64));
        }
        return mir(RC(gfx_w / 2 - RS(166) + i * RS(170), RS(326), RS(162), RS(64)));
    }
    if (L == LY_BAR) {
        return mir(RC(430, 30 + i * 62, 190, 52));
    }
    if (L == LY_TAB) {
        return mir(RC(400, 120 + i * 84, 220, 68));
    }
    return RC(20 + i * 146, 178, 134, 50);
}
static void title_layout(void)
{
    s_ar = s_arH;
    s_ar->n = 0;
    t_layout_at("\\int", 0, TS_D, 400, &s_tt_sign);
}
// the live curve of the title: an area that sweeps under a moving wave
static void title_wave(int x0, int y0, int w, int h, float t, int alpha)
{
    const uint16_t hue = sec_col(1, 255);
    const float a0 = 0.18f + 0.12f * fsin(t * 0.7f), a1 = a0 + 0.42f + 0.1f * fsin(t * 0.43f);
    float prev = 0;
    for (int i = 0; i < w; i++) {
        const float u = (float)i / w;
        const float v = 0.5f + 0.24f * fsin(u * 7.0f + t) + 0.13f * fsin(u * 15.0f - t * 1.7f) * fcos(t * 0.31f);
        const float y = y0 + h * (1.0f - v);
        if (u >= a0 && u <= a1) {
            const int ya = (int)y, yb = y0 + h;
            for (int yy = ya; yy < yb; yy += 1) {
                tfd_px(x0 + i, yy, hue, fa(alpha * (40 + 70 * (yb - yy) / (yb - ya + 1)) / 255));
            }
        }
        if (i) {
            tfd_line(x0 + i - 1, prev, x0 + i, y, 2.0f, ui_mix(hue, C_INK, 120), fa(alpha));
        }
        prev = y;
    }
    tfd_line(x0, y0 + h, x0 + w, y0 + h, 1.0f, C_SUB, fa(alpha / 2));
}
static void title_keys(int alpha)
{
    int i = 0;
    if (s_has_save) {
        button(title_btn(i++), s_he ? "המשך" : "CONTINUE", true, 1, alpha);
    }
    button(title_btn(i), s_he ? "תוכן העניינים" : "CONTENTS", !s_has_save, 1, alpha);
}
// the title in the round frame: the sign in the top cap, the wave across the wide middle, the keys in the bottom cap
static void title_round(float t, int alpha)
{
    const uint16_t keep = s_tcol[TC_INK];
    title_wave(0, RS(150), gfx_w, RS(150), t, alpha * 70 / 255);
    s_tcol[TC_INK] = sec_col(1, 255);
    const int sx = (gfx_w - s_tt_sign.w) / 2, sy = RS(96) - s_tt_sign.h / 2;
    tfd_glow(gfx_w / 2.0f, RS(96), RS(70), sec_col(1, 255), fa(alpha / 5));
    t_draw(s_arH, &s_tt_sign, sx, sy, alpha, 0);
    s_tcol[TC_INK] = keep;
    char info[96];
    snprintf(info, sizeof info,
             s_he ? "%d פרקים, %d שיעורים, כל צעד נבדק" : "%d chapters, %d lessons, every step checked", s_nch,
             s_total_les);
    const int y0 = RS(204), y1 = RS(244), y2 = RS(278);
    line_fit(s_he ? "אינטגרלים" : "INTEGRALS", gfx_w / 2 - chord_w(y0, RS(30)) / 2, y0, chord_w(y0, RS(30)), &KF_TITLE,
             C_INK, alpha, 0);
    line_fit(s_he ? "שיטות האינטגרציה, צעד אחר צעד" : "The techniques, step by step",
             gfx_w / 2 - chord_w(y1, RS(30)) / 2, y1, chord_w(y1, RS(30)), &KF_TEXT, C_TXT, alpha, 0);
    line_fit(info, gfx_w / 2 - chord_w(y2, RS(40)) / 2, y2, chord_w(y2, RS(40)), &KF_LABEL, C_SUB, alpha, 0);
    title_keys(alpha);
}
static void title_render(int alpha)
{
    const int L = lay();
    const float t = (float)(s_now % 600000) / 1000.0f;
    const uint16_t keep = s_tcol[TC_INK];
    if (rnd()) {
        title_round(t, alpha);
        return;
    }
    // the wave behind, the sign, the name, the line under it
    if (L == LY_BAR) {
        title_wave(s_he ? 300 : 0, 18, 340, 140, t, alpha * 70 / 255);
    } else if (L == LY_TAB) {
        title_wave(s_he ? 280 : 0, 70, 360, 240, t, alpha * 70 / 255);
    } else {
        title_wave(0, 60, gfx_w, 100, t, alpha * 70 / 255);
    }
    const int sx = L == LY_BAR ? 30 : L == LY_TAB ? 40 : 20;
    const int sy = L == LY_BAR ? 24 : L == LY_TAB ? 70 : 18;
    s_tcol[TC_INK] = sec_col(1, 255);
    tfd_glow(s_he ? gfx_w - sx - 20.0f : sx + 20.0f, sy + 40.0f, 60, sec_col(1, 255), fa(alpha / 5));
    t_draw(s_arH, &s_tt_sign, s_he ? gfx_w - sx - s_tt_sign.w : sx, sy, alpha, 0);
    s_tcol[TC_INK] = keep;
    const char *name = s_he ? "אינטגרלים" : "INTEGRALS";
    const char *sub = s_he ? "שיטות האינטגרציה, צעד אחר צעד" : "The techniques, step by step";
    char info[96];
    snprintf(info, sizeof info,
             s_he ? "%d פרקים, %d שיעורים, כל צעד נבדק" : "%d chapters, %d lessons, every step checked", s_nch,
             s_total_les);
    const int tx = L == LY_BOX ? 72 : L == LY_TAB ? 110 : 96;
    const int tw = L == LY_BOX ? gfx_w - tx - 8 : L == LY_TAB ? 290 : 320;
    const int ty = L == LY_BOX ? 52 : L == LY_TAB ? 112 : 66;
    const rc_t tr = mir(RC(tx, 0, tw, 1));
    line_start(name, tr.x, ty, tr.w, &KF_TITLE, C_INK, alpha);
    if (L == LY_BOX) {
        line_fit(sub, 8, 98, gfx_w - 16, &KF_TEXT, C_TXT, alpha, 0);
        line_fit(info, 8, 124, gfx_w - 16, &KF_LABEL, C_SUB, alpha, 0);
    } else {
        line_start(sub, tr.x, ty + (L == LY_TAB ? 40 : 30), tr.w, &KF_TEXT, C_TXT, alpha);
        line_start(info, tr.x, ty + (L == LY_TAB ? 70 : 54), tr.w, &KF_LABEL, C_SUB, alpha);
    }
    title_keys(alpha);
}
static void go(int st);
static void lesson_open(int ch, int li, int vi, int si);
static void title_tap(int x, int y)
{
    int i = 0;
    if (s_has_save) {
        if (in_rc((rc_t[]){title_btn(0)}, x, y)) {
            fx(FX_OPEN);
            lesson_open(s_sv_cc, s_sv_li, s_sv_vi, s_sv_si);
            return;
        }
        i = 1;
    }
    const rc_t r = title_btn(i);
    if (in_rc(&r, x, y)) {
        fx(FX_OPEN);
        go(ST_TOC);
    }
}

// ---------------------------------------------------------------- the lists (the contents, the lessons of a chapter)
// One grid of cards that scrolls: the bar scrolls in x (one row), the box and the tablet in y.
typedef struct {
    rc_t view;      // the part of the screen of the list
    int cw, ch;     // a card
    int gap, cols;  // cols 0 = one row that scrolls in x
    int n;
    kscroll_t ks;
} grid_t;
static grid_t s_gT, s_gC;

static void grid_init(grid_t *G, rc_t view, int cw, int chh, int gap, int cols, int n)
{
    view = mir(view);
    G->view = view;
    G->cw = cw;
    G->ch = chh;
    G->gap = gap;
    G->cols = cols;
    G->n = n;
    float len, vlen;
    if (!cols) {
        len = n * (cw + gap) - gap;
        vlen = view.w;
    } else {
        const int rows = (n + cols - 1) / cols;
        len = rows * (chh + gap) - gap;
        vlen = view.h;
    }
    ks_init(&G->ks, vlen, len > vlen ? len - vlen : 0);
}
// the box of the card i on the screen now
static rc_t grid_card(const grid_t *G, int i)
{
    const int p = ks_ipos(&G->ks);
    rc_t r;
    if (!G->cols) {
        r = RC(G->view.x + i * (G->cw + G->gap) - p, G->view.y, G->cw, G->ch);
    } else {
        const int c = i % G->cols, row = i / G->cols;
        const int left = G->view.x + (G->view.w - (G->cols * G->cw + (G->cols - 1) * G->gap)) / 2;
        r = RC(left + c * (G->cw + G->gap), G->view.y + row * (G->ch + G->gap) - p, G->cw, G->ch);
    }
    if (rnd() && G->cols == 1) {
        // a round list: the card is as wide as the circle at its visible edge that is far from the middle
        const int y0 = r.y > G->view.y ? r.y : G->view.y;
        const int y1 = r.y + r.h < G->view.y + G->view.h ? r.y + r.h : G->view.y + G->view.h;
        const int far = abs(y0 - gfx_h / 2) > abs(y1 - gfx_h / 2) ? y0 : y1;
        int w = chord_w(far, RS(14));
        w = w > G->cw ? G->cw : w < RS(300) ? RS(300) : w;
        r.x = gfx_w / 2 - w / 2;
        r.w = w;
        return r;
    }
    if (s_he) {
        r.x = G->view.x + G->view.x + G->view.w - r.x - r.w;
    }
    return r;
}
static inline float grid_axis(const grid_t *G, int x, int y)
{
    return G->cols ? (float)y : (float)(s_he ? -x : x);
}
static int grid_hit(const grid_t *G, int x, int y)
{
    if (!in_rc(&G->view, x, y)) {
        return -1;
    }
    for (int i = 0; i < G->n; i++) {
        const rc_t r = grid_card(G, i);
        if (in_rc(&r, x, y)) {
            return i;
        }
    }
    return -1;
}
// the clip of the view for the cards
static void grid_clip(const grid_t *G)
{
    clip_set(G->view.x, G->view.y, G->view.x + G->view.w, G->view.y + G->view.h);
}
static void grid_bar(const grid_t *G, int alpha)
{
    int at, size, a;
    const bool row = !G->cols;
    if (rnd()) {
        return;  // (the round list: the cards that grow and shrink show the scroll)
    }
    if (!ks_bar(&G->ks, row ? G->view.w : G->view.h, &at, &size, &a)) {
        return;
    }
    if (row) {
        const int x = s_he ? G->view.x + G->view.w - at - size : G->view.x + at;
        tfd_rrect(x, G->view.y + G->view.h + 1, size, 3, 1, C_SUB, fa(a * alpha / 255));
    } else {
        tfd_rrect(G->view.x + G->view.w - 4, G->view.y + at, 3, size, 1, C_SUB, fa(a * alpha / 255));
    }
}
