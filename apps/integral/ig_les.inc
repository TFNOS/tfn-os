// INTEGRALS: the lesson (the steps with the changed part in a colour, the graph, the number to change), the big
// graph of the box layout, and the dispatch of the screens (input, update, render).

typedef struct {
    rc_t head, main, prev, graph, plot, minus, plus, val, mini, endc;
    bool has_g, has_p, next_strip;
} lgeo_t;
static lgeo_t s_G;

typedef struct {
    tarena_t *ar;
    tform_t math, pv;  // the formula of the step; "a = 3"
    ttext_t text;
    int mx, my, tx, ty;  // the places in the main zone
} lstep_t;
static lstep_t s_L[2];
static int s_Lc;
static float s_lt = 1.0f;  // the change of the step 0..1
static int s_ldir;         // +1 next, -1 back, 0 a new value of the parameter
static int64_t s_hl_t0;    // the time of the change (the glow of the changed part)
static bool s_end;         // the end card of the lesson
static float s_end_t;
static bool s_pdrag;      // a drag on the graph changes the parameter
static int64_t s_open_t;  // the time when the lesson opened (the hint of the first tap)

// the lesson in the round frame: the head in the top cap, the steps in the wide middle, the value keys and the small
// graph in the bottom cap; the big graph (a tap on the small one) fills the middle
static void les_geom_round(lgeo_t *G, bool side)
{
    G->head = RC(RS(136), RS(44), RS(232), RS(52));
    G->prev = RC(RS(18), RS(150), RS(40), RS(166));
    G->main = RC(RS(62), RS(100), RS(342), side ? RS(212) : RS(276));
    G->minus = RC(RS(88), RS(318), RS(68), RS(56));
    G->val = RC(RS(156), RS(318), RS(154), RS(56));
    G->plus = RC(RS(310), RS(318), RS(68), RS(56));
    G->mini = G->has_p ? RC(RS(118), RS(380), RS(230), RS(50)) : RC(RS(96), RS(318), RS(274), RS(96));
    G->graph = RC(RS(52), RS(100), RS(362), RS(212));
}

static void les_geom(void)
{
    const igd_lesson_t *l = les_at(s_cc, s_li);
    const igd_var_t *v = var_at(s_cc, s_li, s_vi);
    lgeo_t *G = &s_G;
    memset(G, 0, sizeof *G);
    G->has_g = v->graph >= 0;
    G->has_p = l->nvals > 1;
    const bool side = G->has_g || G->has_p;
    const int L = lay();
    if (rnd()) {
        les_geom_round(G, side);
    } else if (L == LY_BAR) {
        G->head = RC(60, 0, side ? 386 : 520, 28);
        G->prev = RC(0, 30, 52, 142);
        G->main = RC(54, 30, side ? 392 : 532, 142);
        G->next_strip = !side;
        G->graph = RC(452, 6, 182, G->has_p ? 112 : 160);
        G->minus = RC(452, 124, 54, 46);
        G->val = RC(506, 124, 74, 46);
        G->plus = RC(580, 124, 54, 46);
    } else if (L == LY_TAB) {
        G->head = RC(80, 0, side ? 320 : 540, 50);
        G->prev = RC(0, 56, 60, 300);
        G->main = RC(62, 56, side ? 340 : 560, 300);
        G->next_strip = !side;
        G->graph = RC(412, 56, 220, G->has_p ? 212 : 296);
        G->minus = RC(412, 280, 70, 72);
        G->val = RC(482, 280, 80, 72);
        G->plus = RC(562, 280, 70, 72);
    } else {
        G->head = RC(56, 0, gfx_w - 60, 40);
        G->prev = RC(0, 42, 28, side ? 150 : 196);
        G->main = RC(30, 42, gfx_w - 36, side ? 148 : 196);
        G->minus = RC(4, 194, 50, 44);
        G->val = RC(54, 194, 76, 44);
        G->plus = RC(130, 194, 50, 44);
        G->mini = G->has_p ? RC(186, 194, gfx_w - 190, 44) : RC(4, 194, gfx_w - 8, 44);
        G->graph = RC(6, 44, gfx_w - 12, 146);
    }
    const rc_t g = G->graph;
    G->plot = RC(g.x + 10, g.y + 24, g.w - 20, g.h - 24 - 20);
    G->endc = RC(G->main.x + 8, G->main.y + G->main.h - (L == LY_TAB ? 84 : 58), G->main.w - 16, L == LY_TAB ? 76 : 52);
}

// the text of "a = 3" (the parameter and its value)
static void pv_text(char *b, int n)
{
    const igd_lesson_t *l = les_at(s_cc, s_li);
    const igd_var_t *v = var_at(s_cc, s_li, s_vi);
    snprintf(b, n, "%s = %s", S_(l->pname), S_(v->label));
}

// the math and the text of the step now in the slot k
static void step_layout(int k)
{
    lstep_t *S = &s_L[k];
    s_ar = S->ar;
    s_ar->n = 0;
    s_mbuf_n = 0;
    const igd_var_t *v = var_at(s_cc, s_li, s_vi);
    const igd_step_t *st = step_at(v, s_si);
    const rc_t m = s_G.main;
    const int L = lay();
    const int lv0 = L == LY_BOX ? 1 : 0, lv1 = 3;
    const int face0 = L == LY_TAB ? 0 : 1;
    const int gap = L == LY_TAB ? 14 : 8;
    static const int FR[3] = {66, 56, 46};  // the part of the height for the math (%)
    for (int i = 0; i < 3; i++) {
        s_ar->n = 0;
        s_mbuf_n = 0;
        t_layout(S_(st->math), lv0, lv1, TS_D, m.w - 8, m.h * FR[i] / 100, &S->math, true);
        const int rest = m.h - S->math.h - gap - 4;
        tt_layout(&S->text, L_(st->en, st->he), face0, m.w - 8, rest, s_he);
        if (S->text.fit && S->math.fit) {
            break;
        }
    }
    const int total = S->math.h + gap + S->text.h;
    int top = m.y + (m.h - total) / 2;
    if (top < m.y + 2) {
        top = m.y + 2;
    }
    S->mx = m.x + (m.w - S->math.w) / 2;
    S->my = top;
    S->tx = m.x + 4;
    if (S->text.lines == 1) {
        S->tx += s_he ? -(m.w - 8 - S->text.w) / 2 : (m.w - 8 - S->text.w) / 2;
    }
    S->ty = top + S->math.h + gap;
    // the value of the parameter and the labels of the graph (in this arena)
    char pv[64];
    pv_text(pv, sizeof pv);
    t_layout(pv, L == LY_TAB ? 0 : 1, 3, TS_T, s_G.val.w - 4, s_G.val.h - 6, &S->pv, false);
    g_labels(L == LY_TAB ? 2 : 3);
}

static void graph_place(bool morph)
{
    const igd_var_t *v = var_at(s_cc, s_li, s_vi);
    rc_t p;
    if (mini_graph() && s_st != ST_GRAPH && s_next != ST_GRAPH) {
        const rc_t m = mir(s_G.mini);
        p = RC(m.x + 4, m.y + 4, m.w - 8, m.h - 8);
    } else {
        p = mir(s_G.plot);
    }
    g_set(graph_of(v), p.x, p.y, p.w, p.h, morph);
}

static void lesson_enter(void)
{
    const igd_lesson_t *l = les_at(s_cc, s_li);
    if (s_vi >= l->nvals) {
        s_vi = l->def;
    }
    if (s_si >= var_at(s_cc, s_li, s_vi)->nsteps) {
        s_si = 0;
    }
    les_geom();
    s_Lc = 0;
    s_L[0].ar = s_arL[0];
    s_L[1].ar = s_arL[1];
    graph_place(false);
    step_layout(0);
    s_L[1].math.i0 = s_L[1].math.i1 = 0;
    s_L[1].text.ntok = 0;
    s_lt = 1.0f;
    s_end = false;
    s_hl_t0 = s_now;
}
static void lesson_open(int ch, int li, int vi, int si)
{
    s_cc = ch;
    s_li = li;
    s_vi = vi;
    s_si = si;
    s_open_t = s_now;
    go(ST_LES);
}

static void step_go(int d)
{
    const igd_var_t *v = var_at(s_cc, s_li, s_vi);
    if (s_end) {
        if (d < 0) {
            s_end = false;
            fx(FX_STEP);
        }
        return;
    }
    if (d > 0 && s_si >= v->nsteps - 1) {
        s_end = true;
        s_end_t = 0;
        mark_seen(s_cc, s_li);
        fx(FX_DONE);
        return;
    }
    if (d < 0 && s_si == 0) {
        return;
    }
    s_si += d;
    s_Lc ^= 1;
    step_layout(s_Lc);
    s_lt = 0;
    s_ldir = d;
    s_hl_t0 = s_now;
    fx(FX_STEP);
}

static void param_set(int vi)
{
    if ((s_st != ST_LES && s_st != ST_GRAPH) || !s_L[0].ar) {
        return;  // (only in a lesson: a late drag of another screen does nothing)
    }
    const igd_lesson_t *l = les_at(s_cc, s_li);
    if (vi < 0 || vi >= l->nvals || vi == s_vi) {
        return;
    }
    s_vi = vi;
    const igd_var_t *v = var_at(s_cc, s_li, s_vi);
    if (s_si >= v->nsteps) {
        s_si = v->nsteps - 1;
    }
    graph_place(true);
    s_Lc ^= 1;
    step_layout(s_Lc);
    s_lt = 0;
    s_ldir = 0;
    s_hl_t0 = s_now;
    fx(FX_STEP);
}

// ---------------------------------------------------------------- the drawing of a lesson
static void step_draw(const lstep_t *S, int dy, int alpha, int glow)
{
    const rc_t m = mir(s_G.main);
    const int dx = m.x - s_G.main.x;
    clip_set(m.x, m.y, m.x + m.w, m.y + m.h);
    s_tcol[TC_INK] = C_INK;
    t_draw(S->ar, &S->math, S->mx + dx, S->my + dy, alpha, glow);
    s_tcol[TC_INK] = ui_mix(C_TXT, C_INK, 90);
    tt_draw(S->ar, &S->text, S->tx + dx, S->ty + dy, C_TXT, alpha);
    s_tcol[TC_INK] = C_INK;
    clip_off();
}

static void les_head(int alpha)
{
    back_draw();
    const rc_t h = mir(s_G.head);
    const igd_lesson_t *l = les_at(s_cc, s_li);
    const igd_var_t *v = var_at(s_cc, s_li, s_vi);
    const int L = lay();
    const kfont_t *f = L == LY_TAB ? &KF_NAME : &KF_LABEL;
    char cnt[16];
    snprintf(cnt, sizeof cnt, "%d / %d", s_si + 1, v->nsteps);
    const int cw = kf_width(f, cnt) + 8;
    const int base = rnd() ? h.y + RS(30) : L == LY_TAB ? 32 : L == LY_BAR ? 19 : 26;
    char t[160];
    if (rnd()) {
        snprintf(t, sizeof t, "%s", L_(l->en, l->he));  // (the round head is short: the name of the lesson only)
    } else {
        snprintf(t, sizeof t, "%s  –  %s", L_(s_ch[s_cc].en, s_ch[s_cc].he), L_(l->en, l->he));
    }
    const rc_t tr = s_he ? RC(h.x + cw, 0, h.w - cw, 1) : RC(h.x, 0, h.w - cw, 1);
    line_start(t, tr.x, base, tr.w, f, C_SUB, alpha);
    const int cx = s_he ? h.x + cw / 2 : h.x + h.w - cw / 2;
    kf_line_c(f, cx, base, cnt, sec_col(sec_of(s_cc), 255), fa(alpha));
    // the progress: a thin line under the head
    const float p = (float)(s_si + 1) / v->nsteps;
    tfd_rect(h.x, h.y + h.h - 3, h.w, 2, C_LINE, fa(alpha));
    const int pw = (int)(h.w * p);
    tfd_rect(s_he ? h.x + h.w - pw : h.x, h.y + h.h - 3, pw, 2, sec_col(sec_of(s_cc), 255), fa(alpha));
}

static void param_row(int alpha)
{
    if (!s_G.has_p) {
        return;
    }
    const int sec = sec_of(s_cc);
    const igd_lesson_t *l = les_at(s_cc, s_li);
    const rc_t mi = mir(s_G.minus), pl = mir(s_G.plus), va = mir(s_G.val);
    const bool can_dn = s_vi > 0, can_up = s_vi < l->nvals - 1;
    // in Hebrew the - is at the right: the order of the values goes from the right
    tfd_rrect(mi.x + 3, mi.y + 3, mi.w - 6, mi.h - 6, 12, C_CARD, fa(alpha));
    tfd_rrect(pl.x + 3, pl.y + 3, pl.w - 6, pl.h - 6, 12, C_CARD, fa(alpha));
    const uint16_t cm = can_dn ? sec_col(sec, 255) : C_LINE, cp = can_up ? sec_col(sec, 255) : C_LINE;
    const float r = mi.h * 0.16f;
    tfd_line(mi.x + mi.w / 2 - r, mi.y + mi.h / 2.0f, mi.x + mi.w / 2 + r, mi.y + mi.h / 2.0f, 2.6f, cm, fa(alpha));
    tfd_line(pl.x + pl.w / 2 - r, pl.y + pl.h / 2.0f, pl.x + pl.w / 2 + r, pl.y + pl.h / 2.0f, 2.6f, cp, fa(alpha));
    tfd_line(pl.x + pl.w / 2.0f, pl.y + pl.h / 2 - r, pl.x + pl.w / 2.0f, pl.y + pl.h / 2 + r, 2.6f, cp, fa(alpha));
    const lstep_t *S = &s_L[s_Lc];
    const uint16_t keep = s_tcol[TC_INK];
    s_tcol[TC_INK] = C_INK;
    t_draw(S->ar, &S->pv, va.x + (va.w - S->pv.w) / 2, va.y + (va.h - S->pv.h) / 2 - 3, alpha, 0);
    s_tcol[TC_INK] = keep;
    // the values as dots under the value
    const int n = l->nvals;
    for (int i = 0; i < n; i++) {
        const int k = s_he ? n - 1 - i : i;
        const float dx = (i - (n - 1) / 2.0f) * 8;
        tfd_disc(va.x + va.w / 2 + dx, va.y + va.h - 5, k == s_vi ? 2.6f : 1.8f, k == s_vi ? sec_col(sec, 255) : C_LINE,
                 fa(alpha));
    }
}

static void end_card(int alpha)
{
    if (!s_end) {
        return;
    }
    const int sec = sec_of(s_cc);
    const rc_t c = mir(s_G.endc);
    const float t = smooth01(s_end_t);
    const int a = (int)(alpha * t);
    const int dy = (int)((1 - t) * 14);
    tfd_rrect(c.x - 4, c.y - 4 + dy, c.w + 8, c.h + 8, 16, C_BG0, fa(a * 235 / 255));
    const int bw = (c.w - 10) / 2;
    const rc_t again = mir(RC(s_G.endc.x, s_G.endc.y + dy, bw, s_G.endc.h));
    const rc_t next = mir(RC(s_G.endc.x + bw + 10, s_G.endc.y + dy, bw, s_G.endc.h));
    button(again, s_he ? "שוב מההתחלה" : "AGAIN", false, sec, a);
    const bool last_les = s_li >= s_ch[s_cc].n - 1;
    const bool last_ch = s_cc >= s_nch - 1;
    const char *nt = !last_les  ? (s_he ? "השיעור הבא" : "NEXT LESSON")
                     : !last_ch ? (s_he ? "הפרק הבא" : "NEXT CHAPTER")
                                : (s_he ? "תוכן העניינים" : "CONTENTS");
    button(next, nt, true, sec, a);
}

// the graph box as a picture: the curve, the area and the labels change only with a new value (the morph) or a
// fade; the other frames copy the picture (the glow lines of the curve cost the most)
static uint16_t *s_gc;
static rc_t s_gc_r;
static bool s_gc_ok;
static const void *s_gc_key;
static bool gcache_put(rc_t g, int alpha)
{
    const bool still = s_g_t >= 1.0f && fa(alpha) == 255 && s_gc_ok && s_gc_key == (const void *)s_g_data &&
                       s_gc_r.x == g.x && s_gc_r.y == g.y && s_gc_r.w == g.w && s_gc_r.h == g.h;
    if (!still || !s_gc) {
        return false;
    }
    for (int r = 0; r < g.h; r++) {
        memcpy(g_fb + (g.y + r) * gfx_w + g.x, s_gc + r * g.w, (size_t)g.w * 2);
    }
    return true;
}
static void gcache_take(rc_t g, int alpha)
{
    s_gc_ok = false;
    if (s_g_t < 1.0f || fa(alpha) != 255 || g.x < 0 || g.y < 0 || g.x + g.w > gfx_w || g.y + g.h > gfx_h) {
        return;
    }
    if (!s_gc || s_gc_r.w * s_gc_r.h < g.w * g.h) {
        heap_caps_free(s_gc);
        s_gc = heap_caps_malloc((size_t)g.w * g.h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_gc) {
        return;
    }
    for (int r = 0; r < g.h; r++) {
        memcpy(s_gc + r * g.w, g_fb + (g.y + r) * gfx_w + g.x, (size_t)g.w * 2);
    }
    s_gc_r = g;
    s_gc_key = s_g_data;
    s_gc_ok = true;
}

static void les_render(int alpha)
{
    const int sec = sec_of(s_cc);
    les_head(alpha);
    const float t = smooth01(s_lt);
    const int glow =
            40 +
            (int)(70 * (1.0f - (float)(s_now - s_hl_t0) / 1400.0f > 0 ? 1.0f - (float)(s_now - s_hl_t0) / 1400.0f : 0));
    if (t < 1.0f && (s_L[s_Lc ^ 1].math.i1 > s_L[s_Lc ^ 1].math.i0 || s_L[s_Lc ^ 1].text.ntok)) {
        step_draw(&s_L[s_Lc ^ 1], (int)(-s_ldir * 14 * t), (int)(alpha * (1 - t)), 40);
    }
    // the end card: the last step stays in its place, pale behind the card
    const int dim = s_end ? 255 - (int)(smooth01(s_end_t) * 165) : 255;
    step_draw(&s_L[s_Lc], (int)(s_ldir * 14 * (1 - t)), (int)(alpha * t) * dim / 255, glow);
    // the strips of the step before and of the next step
    if (s_si > 0 && !s_end) {
        const rc_t p = mir(s_G.prev);
        chevron(p.x + p.w / 2.0f, p.y + p.h / 2.0f, 9, s_he ? 1 : -1, C_SUB, alpha * 150 / 255);
    }
    if (s_si == 0 && !s_end && s_now - s_open_t < 6000) {
        const rc_t m = mir(s_G.main);
        const float pulse = 0.5f + 0.5f * fsin((float)(s_now - s_open_t) / 260.0f);
        const float x = s_he ? m.x + 10.0f : m.x + m.w - 10.0f;
        chevron(x, m.y + m.h / 2.0f, 9, s_he ? -1 : 1, sec_col(sec, 255), (int)(alpha * (0.3f + 0.6f * pulse)));
    }
    if (s_G.has_g) {
        if (mini_graph()) {
            const rc_t mm = mir(s_G.mini);
            g_draw(mm.x, mm.y, mm.w, mm.h, mm.x + 4, mm.y + 4, mm.w - 8, mm.h - 8, sec, alpha);
        } else {
            const rc_t g = mir(s_G.graph), p = mir(s_G.plot);
            if (!gcache_put(g, alpha)) {
                g_draw(g.x, g.y, g.w, g.h, p.x, p.y, p.w, p.h, sec, alpha);
                g_draw_labels(s_L[s_Lc].ar, p.x, p.y, p.w, p.h, alpha, sec_col(sec, 255));
                gcache_take(g, alpha);
            }
        }
    }
    param_row(alpha);
    end_card(alpha);
}

static void graph_render(int alpha)
{
    const int sec = sec_of(s_cc);
    les_head(alpha);
    const rc_t g = mir(s_G.graph), p = mir(s_G.plot);
    g_draw(g.x, g.y, g.w, g.h, p.x, p.y, p.w, p.h, sec, alpha);
    g_draw_labels(s_L[s_Lc].ar, p.x, p.y, p.w, p.h, alpha, sec_col(sec, 255));
    param_row(alpha);
}
