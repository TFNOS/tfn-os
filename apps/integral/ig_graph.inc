// INTEGRALS: the graph of the integrand: the curve, the area between the limits (above the axis in the colour of the
// section, under it in rose), the labels of the limits and the value. A change of the parameter morphs the old
// curve into the new one (all in screen px: the sample i of both curves is the same column).

typedef struct {
    float py[2][IG_NS];  // the screen y of each sample of the curves (NAN = no value)
    float lo, hi;        // the screen x of the limits of the area
    float axis;          // the screen y of y = 0
    uint8_t kind, ncurve;
    bool ok;
} gshape_t;

static gshape_t s_g_old, s_g_new;
static float s_g_t = 1.0f;  // the morph 0..1
static const igd_graph_t *s_g_data;
static tform_t s_g_lbl[3];  // the label of lo, of hi, the value
static bool s_g_lbl_ok;

static void g_shape(const igd_graph_t *g, int x, int y, int w, int h, gshape_t *o)
{
    memset(o, 0, sizeof *o);
    if (!g) {
        return;
    }
    o->ok = true;
    o->kind = g->kind;
    o->ncurve = g->ncurve;
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < IG_NS; i++) {
            const int16_t s = g->s[c][i];
            o->py[c][i] = s == -32768 ? NAN : (float)(y + h) - (float)s * (float)h / 30000.0f;
        }
    }
    const float sx = (float)w / (g->x1 - g->x0);
    o->lo = x + (g->lo - g->x0) * sx;
    o->hi = x + (g->hi - g->x0) * sx;
    float ay = (float)(y + h) - (0.0f - g->y0) / (g->y1 - g->y0) * (float)h;
    o->axis = ay < y ? (float)y : ay > y + h ? (float)(y + h) : ay;
}

// a new graph: the old shape is the shape now (it can be in a morph)
static void g_set(const igd_graph_t *g, int x, int y, int w, int h, bool morph)
{
    gshape_t now;
    if (morph && s_g_new.ok) {
        now = s_g_new;
        const float t = s_g_t;
        for (int c = 0; c < 2; c++) {
            for (int i = 0; i < IG_NS; i++) {
                const float a = s_g_old.py[c][i], b = s_g_new.py[c][i];
                now.py[c][i] = isnan(a) || isnan(b) ? b : a + (b - a) * t;
            }
        }
        now.lo = s_g_old.lo + (s_g_new.lo - s_g_old.lo) * t;
        now.hi = s_g_old.hi + (s_g_new.hi - s_g_old.hi) * t;
        now.axis = s_g_old.axis + (s_g_new.axis - s_g_old.axis) * t;
        s_g_old = now;
        s_g_t = 0.0f;
    } else {
        s_g_t = 1.0f;
    }
    g_shape(g, x, y, w, h, &s_g_new);
    if (!morph || !s_g_old.ok) {
        s_g_old = s_g_new;
        s_g_t = 1.0f;
    }
    s_g_data = g;
    s_g_lbl_ok = false;
}

// the labels of the graph (in the arena of the scene)
static void g_labels(int level)
{
    if (!s_g_data) {
        return;
    }
    static char val[48];
    snprintf(val, sizeof val, "%s", S_(s_g_data->value));
    t_layout_at(S_(s_g_data->lo_lbl), level, TS_T, 400, &s_g_lbl[0]);
    t_layout_at(S_(s_g_data->hi_lbl), level, TS_T, 400, &s_g_lbl[1]);
    t_layout_at(val, level > 0 ? level - 1 : 0, TS_T, 400, &s_g_lbl[2]);
    s_g_lbl_ok = true;
}

static inline float smooth01(float t)
{
    if (t <= 0) {
        return 0;
    }
    if (t >= 1) {
        return 1;
    }
    return t * t * (3 - 2 * t);
}

// the graph in the box (x, y, w, h); sec = the colour of the section; inside = the plot box (the caller's numbers)
static void g_draw(int x, int y, int w, int h, int px, int py, int pw, int ph, int sec, int alpha)
{
    const int a = fa(alpha);
    tfd_rrect(x, y, w, h, 12, C_CARD, a * 200 / 255);
    if (!s_g_new.ok) {
        return;
    }
    const float t = smooth01(s_g_t);
    float yv[2][IG_NS];
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < IG_NS; i++) {
            const float o = s_g_old.py[c][i], nw = s_g_new.py[c][i];
            yv[c][i] = isnan(o) || isnan(nw) ? nw : o + (nw - o) * t;
        }
    }
    const float lo = s_g_old.lo + (s_g_new.lo - s_g_old.lo) * t;
    const float hi = s_g_old.hi + (s_g_new.hi - s_g_old.hi) * t;
    const float ax = s_g_old.axis + (s_g_new.axis - s_g_old.axis) * t;
    tfd_clip(px - 2, py - 2, px + pw + 2, py + ph + 2);
    const uint16_t hue = sec_col(sec, 255), neg = rgb(255, 110, 140);
    // the area: one column for each px, a soft gradient to the axis
    if (s_g_new.kind & IGK_AREA) {
        const int c0 = (int)(lo < px ? px : lo), c1 = (int)(hi > px + pw ? px + pw : hi);
        for (int cx = c0; cx < c1; cx++) {
            const float f = (float)(cx - px) / (float)pw * (IG_NS - 1);
            const int i = (int)f;
            if (i < 0 || i >= IG_NS - 1) {
                continue;
            }
            const float y0 = yv[0][i], y1 = yv[0][i + 1];
            if (isnan(y0) || isnan(y1)) {
                continue;
            }
            const float cy = y0 + (y1 - y0) * (f - i);
            int fade = 255;
            if ((s_g_new.kind & IGK_INF_HI) && cx > px + pw * 3 / 4) {
                fade = 255 - (cx - (px + pw * 3 / 4)) * 255 / (pw / 4 + 1);
            }
            const bool up = cy < ax;
            const int ya = (int)(up ? cy : ax), yb = (int)(up ? ax : cy);
            const int span = yb - ya < 1 ? 1 : yb - ya;
            for (int yy = ya; yy <= yb; yy++) {
                const int k = up ? (yb - yy) * 255 / span : (yy - ya) * 255 / span;
                tfd_px(cx, yy, up ? hue : neg, a * fade / 255 * (60 + k * 70 / 255) / 255);
            }
        }
        // the edges of the area
        if (lo >= px && lo <= px + pw) {
            tfd_line(lo, py, lo, py + ph, 1.0f, C_SUB, a / 3);
        }
        if (hi >= px && hi <= px + pw && !(s_g_new.kind & IGK_INF_HI)) {
            tfd_line(hi, py, hi, py + ph, 1.0f, C_SUB, a / 3);
        }
    }
    // the axis
    tfd_line(px, ax, px + pw, ax, 1.0f, C_SUB, a * 2 / 3);
    // the curves: a soft glow, then the line
    for (int c = s_g_new.ncurve - 1; c >= 0; c--) {
        const uint16_t col = c ? rgb(150, 170, 210) : ui_mix(hue, rgb(255, 255, 255), 70);
        for (int pass = 0; pass < 2; pass++) {
            for (int i = 0; i + 1 < IG_NS; i++) {
                const float ya = yv[c][i], yb = yv[c][i + 1];
                if (isnan(ya) || isnan(yb) || fabsf(ya - yb) > ph * 0.8f) {
                    continue;
                }
                const float xa = px + (float)i * pw / (IG_NS - 1), xb = px + (float)(i + 1) * pw / (IG_NS - 1);
                if (pass == 0 && !c) {
                    tfd_line(xa, ya, xb, yb, 5.0f, hue, a / 6);
                } else if (pass == 1) {
                    tfd_line(xa, ya, xb, yb, c ? 1.4f : 2.0f, col, a);
                }
            }
        }
    }
    tfd_noclip();
    clip_off();
    (void)h;
}

// the labels under the limits and the value at the top
static void g_draw_labels(const tarena_t *ar, int px, int py, int pw, int ph, int alpha, uint16_t vcol)
{
    if (!s_g_new.ok || !s_g_lbl_ok || !(s_g_new.kind & IGK_AREA)) {
        return;
    }
    const float t = smooth01(s_g_t);
    const float lo = s_g_old.lo + (s_g_new.lo - s_g_old.lo) * t;
    const float hi = s_g_old.hi + (s_g_new.hi - s_g_old.hi) * t;
    const uint16_t keep = s_tcol[TC_INK];
    s_tcol[TC_INK] = C_TXT;
    const tform_t *L = &s_g_lbl[0], *H = &s_g_lbl[1], *V = &s_g_lbl[2];
    int lx = (int)lo - L->w / 2, hx = (int)hi - H->w / 2;
    if (lx < px - 2) {
        lx = px - 2;
    }
    if (hx + H->w > px + pw + 2) {
        hx = px + pw + 2 - H->w;
    }
    if (hx < lx + L->w + 4) {
        hx = lx + L->w + 4;
    }
    const int ly = py + ph + 3;
    t_draw(ar, L, lx, ly, alpha, 0);
    if (!(s_g_new.kind & IGK_INF_HI)) {
        t_draw(ar, H, hx, ly, alpha, 0);
    } else {
        t_draw(ar, H, px + pw - H->w, ly, alpha, 0);
    }
    s_tcol[TC_INK] = vcol;
    t_draw(ar, V, px + 4, py + 2, alpha, 0);
    s_tcol[TC_INK] = keep;
}

static void g_update(float dt)
{
    if (s_g_t < 1.0f) {
        s_g_t += dt / 0.35f;
        if (s_g_t > 1.0f) {
            s_g_t = 1.0f;
        }
    }
}
