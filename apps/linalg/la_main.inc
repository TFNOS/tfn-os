// LINEAR ALGEBRA: the screens, the input, the panel, the saves and the icon.

// ---------------------------------------------------------------- the saves
#define SAVE_FILE  "/data/linalg.bin"
#define SAVE_MAGIC 0x4C414C31u
typedef struct {
    uint32_t magic;
    uint8_t ti, step, scr, nt;
    int8_t par[32][4];
    uint8_t stp[32];  // the last step of each chapter
    uint32_t seen;    // the chapters that were open
} save_t;
static save_t S;
static bool s_dirty;
static int64_t s_dirty_t;

static void save_now(void)
{
    if (storage_ok()) {
        storage_write(SAVE_FILE, &S, sizeof S);
    }
    s_dirty = false;
}
static void save_soon(void)
{
    s_dirty = true;
    s_dirty_t = s_now;
}
// a new save: no chapter seen, each number at its start
static void save_reset(void)
{
    memset(&S, 0, sizeof S);
    S.magic = SAVE_MAGIC;
    S.ti = 0xFF;
    for (int t = 0; t < NT; t++) {
        for (int i = 0; i < TOPICS[t]->np; i++) {
            S.par[t][i] = (int8_t)TOPICS[t]->p[i].def;
        }
    }
}
static void save_load(void)
{
    size_t n = 0;
    memset(&S, 0, sizeof S);
    if (!storage_ok() || !storage_read(SAVE_FILE, &S, sizeof S, &n) || n != sizeof S || S.magic != SAVE_MAGIC) {
        save_reset();
    }
    S.nt = (uint8_t)NT;
    // each number in its range (a save of an old version, or a change of a range)
    for (int t = 0; t < NT; t++) {
        const topic_t *tp = TOPICS[t];
        for (int i = 0; i < 4; i++) {
            int v = S.par[t][i];
            bool ok = (S.seen >> t) & 1;
            if (!ok || i >= tp->np || v < tp->p[i].lo || v > tp->p[i].hi) {
                S.par[t][i] = (int8_t)(i < tp->np ? tp->p[i].def : 0);
            }
        }
        if (S.stp[t] >= tp->ns) {
            S.stp[t] = 0;
        }
    }
    if (S.ti != 0xFF && S.ti >= NT) {
        S.ti = 0xFF;
    }
}

// ---------------------------------------------------------------- the effects
enum {
    FX_PAGE,
    FX_PAGEB,
    FX_TICK,
    FX_OPEN,
    FX_PICK,
    FX_N
};
static const char *const FXN[FX_N] = {"page", "pageb", "tick", "open", "pick"};
static void fx(int id, int vol)
{
    if (s_snd) {
        tfsfx_play_var(id, vol, 0.04f);
    }
}

static void calc_all(void)
{
    for (int t = 0; t < NT; t++) {
        for (int i = 0; i < 4; i++) {
            P[i] = S.par[t][i];
        }
        s_tp_now = TOPICS[t];
        TOPICS[t]->calc();
    }
    s_tp_now = NULL;
}
static void topic_open(int t, int step)
{
    s_ti = t;
    s_tp_now = TOPICS[t];
    for (int i = 0; i < 4; i++) {
        P[i] = S.par[t][i];
    }
    s_tp_now->calc();
    s_step = step < s_tp_now->ns ? step : 0;
    s_pg = 0;
    s_sel = 0;
    s_hot = -1;
    s_scr = SC_TOPIC;
    s_scr_t = s_step_t = s_now;
    s_dir = 1;
    s_ek = 1;
    s_snap = true;
    lay_make(s_tp_now->np);
    S.ti = (uint8_t)t;
    S.step = (uint8_t)s_step;
    S.seen |= 1u << t;
    save_soon();
}
static void home_open(void)
{
    if (s_tp_now) {
        s_tp_now = NULL;
    }
    s_scr = SC_HOME;
    s_scr_t = s_now;
    lay_make(0);
    float span = (float)Y.list.w;
    float total = NT * (float)(Y.cw + Y.gap) + Y.gap;
    if (Y.round) {
        total = (NT - 1) * (float)(Y.cw + Y.gap) + span;
    }
    float keep = ks_pos(&s_ks);
    ks_init(&s_ks, span, total - span);
    if (Y.round) {
        ks_set_snap(&s_ks, (float)(Y.cw + Y.gap));
    }
    // the card of the last chapter in the view
    if (S.ti != 0xFF) {
        float want = Y.round ? S.ti * (float)(Y.cw + Y.gap) : S.ti * (float)(Y.cw + Y.gap) - Y.cw;
        if (keep > 0) {
            want = keep;
        }
        ks_jump(&s_ks, want < 0 ? 0 : want > s_ks.max ? s_ks.max : want);
    }
}
static void step_go(int d)
{
    const topic_t *tp = s_tp_now;
    int n = s_step + d;
    if (n < 0) {
        return;
    }
    if (n >= tp->ns) {
        // after the last step: the next chapter
        if (s_ti + 1 < NT) {
            S.stp[s_ti] = 0;
            fx(FX_OPEN, 150);
            topic_open(s_ti + 1, S.stp[s_ti + 1]);
        } else {
            home_open();
        }
        return;
    }
    s_step = n;
    s_dir = d;
    s_step_t = s_now;
    S.step = (uint8_t)n;
    S.stp[s_ti] = (uint8_t)n;
    save_soon();
    fx(d > 0 ? FX_PAGE : FX_PAGEB, 150);
}
static void par_change(int i, int d)
{
    const par_t *p = &s_tp_now->p[i];
    int v = P[i] + d;
    if (v < p->lo || v > p->hi) {
        return;
    }
    P[i] = v;
    S.par[s_ti][i] = (int8_t)v;
    s_hot = i;
    s_hot_t = s_now;
    s_tp_now->calc();
    save_soon();
    fx(FX_TICK, 120);
}

// ---------------------------------------------------------------- the faces of the frame now
static void faces_set(void)
{
    if (gfx_round() && gfx_w > 440) {
        s_tsz = s_big ? 5 : 4;
        s_msz = 5;
    } else if (BOARD_UI_TABLET && gfx_h >= 300) {
        s_tsz = s_big ? 4 : 3;
        s_msz = s_big ? 5 : 4;
    } else {
        s_tsz = s_big ? 3 : 2;
        s_msz = s_big ? 4 : 3;
    }
    s_mbase = s_msz;
    s_rtl = s_he;
}
static const mfont_t *bface(int up)
{
    int i = s_tsz - 2 + up;
    return SZB[i < 0 ? 0 : i > 4 ? 4 : i];
}
// a one-line text (English or Hebrew) centred at cx, or from x (start side) in a width
static void say_c(const mfont_t *f, int cx, int base, const char *s, uint16_t col)
{
    bool kr = s_rtl;
    s_rtl = str_heb(s, (int)strlen(s));
    int n = tokenize(s, s + strlen(s), f, col);
    int sp4 = ty_adv(f, ' '), w = 0;
    for (int i = 0; i < n; i++) {
        w += s_it[i].w + (i && s_it[i].sp ? sp4 : 0);
    }
    line_draw(f, 0, n, cx - w / 8, cx + (w + 7) / 8, base, sp4);
    s_rtl = kr;
}
static int say_w(const mfont_t *f, const char *s)
{
    bool kr = s_rtl;
    s_rtl = str_heb(s, (int)strlen(s));
    int n = tokenize(s, s + strlen(s), f, COL_TEXT);
    int sp4 = ty_adv(f, ' '), w = 0;
    for (int i = 0; i < n; i++) {
        w += s_it[i].w + (i && s_it[i].sp ? sp4 : 0);
    }
    s_rtl = kr;
    return (w + 3) / 4;
}

static const char *const PART[4][2] = {
        {"BASICS", "יסודות"}, {"SPECTRAL", "ספקטרום"}, {"TRICKS", "טריקים"}, {"BEYOND", "ומעבר"}};
static uint16_t part_col(int p)
{
    static const uint8_t C[4][3] = {{70, 205, 190}, {170, 140, 255}, {255, 196, 92}, {255, 120, 170}};
    return rgb(C[p][0], C[p][1], C[p][2]);
}

static void bg_draw(void)
{
    tfd_noclip();
    tfd_vgrad(0, 0, gfx_w, gfx_h, rgb(14, 16, 28), rgb(8, 9, 18));
}
// the box of a button (a soft glow at the press)
static void key_box(rect_t r, bool hi, uint16_t col)
{
    tfd_noclip();
    tfd_rrect(r.x, r.y, r.w, r.h, r.h / 4 + 2, hi ? ui_mix(rgb(34, 38, 62), col, 70) : rgb(30, 34, 56), 256);
    if (hi) {
        tfd_rrect(r.x, r.y, r.w, 2, 1, col, 200);
    }
}
static void icon_chapters(rect_t r, uint16_t col)
{
    int s = r.h < r.w ? r.h : r.w;
    int u = s / 7, cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            tfd_rrect(cx - u * 3 / 2 - u / 2 + j * (u + u / 2), cy - u * 3 / 2 - u / 2 + i * (u + u / 2), u, u, 1, col,
                      230);
        }
    }
}

// ---------------------------------------------------------------- the chapters screen
static void card_rect(int k, rect_t *r)
{
    float pos = ks_pos(&s_ks);
    int x0 = Y.round ? Y.list.x + Y.list.w / 2 - Y.cw / 2 : Y.list.x + Y.gap;
    *r = R(x0 + (int)(k * (Y.cw + Y.gap) - pos), Y.list.y + (Y.list.h - Y.ch) / 2, Y.cw, Y.ch);
}
// a card: the small live picture at the top, the number and the part, the name
static void card_draw(int k, rect_t r, int64_t now)
{
    const topic_t *tp = TOPICS[k];
    uint16_t pc = part_col(tp->part);
    bool seen = (S.seen >> k) & 1;
    tfd_noclip();
    tfd_rrect(r.x, r.y, r.w, r.h, 12, rgb(26, 29, 48), 256);
    int m = r.w / 22 + 3;
    rect_t pr = R(r.x + m, r.y + m, r.w - 2 * m, r.h * (Y.round ? 56 : 47) / 100);
    if (pr.x + pr.w > 0 && pr.x < gfx_w) {
        tfd_rrect(pr.x, pr.y, pr.w, pr.h, 8, rgb(14, 16, 30), 256);
        int ka[4];
        for (int i = 0; i < 4; i++) {
            ka[i] = P[i];
            P[i] = S.par[k][i];
        }
        s_mini = true;
        const topic_t *keep = s_tp_now;
        s_tp_now = tp;
        rect_t pc2 = R(pr.x + 1, pr.y + 1, pr.w - 2, pr.h - 2);
        tfd_clip(pc2.x < 0 ? 0 : pc2.x, pc2.y, pc2.x + pc2.w, pc2.y + pc2.h);
        tp->pic(pc2, -1, (float)now / 1000.0f);
        s_tp_now = keep;
        s_mini = false;
        for (int i = 0; i < 4; i++) {
            P[i] = ka[i];
        }
        tfd_noclip();
    }
    // the number and the part on one line, the name under them
    char num[8];
    snprintf(num, sizeof num, "%d", k + 1);
    ty_clip(r.x < 0 ? 0 : r.x, r.y, r.x + r.w > gfx_w ? gfx_w : r.x + r.w, r.y + r.h);
    const mfont_t *nf = bface(1), *pf = SZ[s_tsz - 2 < 0 ? 0 : s_tsz - 2];
    int base = pr.y + pr.h + nf->asc + m + 2;
    int nw = (ty_adv(nf, num[0]) + (num[1] ? ty_adv(nf, num[1]) : 0)) / 4;
    int nx = s_he ? r.x + r.w - m - nw : r.x + m;
    ty_put(nf, num[0], nx * 4, base, seen ? pc : COL_DIM, 255);
    if (num[1]) {
        ty_put(nf, num[1], nx * 4 + ty_adv(nf, num[0]), base, seen ? pc : COL_DIM, 255);
    }
    int pw = say_w(pf, PART[tp->part][s_he]);
    say_c(pf, s_he ? r.x + m + pw / 2 : r.x + r.w - m - pw / 2, base, PART[tp->part][s_he], pc);
    bool kr = s_rtl;
    s_rtl = s_he;
    para(tp->name[s_he], tp->name[s_he] + strlen(tp->name[s_he]), bface(0), COL_TEXT, r.x + m, r.x + r.w - m, base + 4,
         true);
    s_rtl = kr;
    ty_noclip();
}
// the hero of the chapters screen: a grid that a symmetric map turns into an ellipse, its two axes
static void hero_pic(rect_t r, float t)
{
    if (r.w <= 0) {
        return;
    }
    view_set(r, 6.0f, 0, 0);
    float th = t * 0.35f, l1 = 1.55f + 0.35f * fsin(t * 0.7f), l2 = 0.55f + 0.2f * fcos(t * 0.5f);
    float c = fcos(th), s = fsin(th);
    float a = l1 * c * c + l2 * s * s, b = (l1 - l2) * c * s, d = l1 * s * s + l2 * c * c;
    grid_map(a, b, b, d, 6, rgb(70, 90, 150), 70);
    ellipse(a, b, b, d, 0, 0, 2.0f, COL_C4, 200);
    arrow(0, 0, l1 * c, l1 * s, COL_C1, 2.2f, 256);
    arrow(0, 0, -l2 * s, l2 * c, COL_C2, 2.2f, 256);
    tfd_noclip();
}
static void home_draw(int64_t now)
{
    bg_draw();
    float t = (float)now / 1000.0f;
    // the cards
    for (int k = 0; k < NT; k++) {
        rect_t r;
        card_rect(k, &r);
        if (r.x + r.w < Y.list.x - 4 || r.x > Y.list.x + Y.list.w + 4) {
            continue;
        }
        card_draw(k, r, now);
    }
    if (!Y.round) {
        // the title column (over the cards that go under it)
        tfd_noclip();
        if (Y.ttl.w < gfx_w) {
            tfd_vgrad(Y.ttl.x, Y.ttl.y, Y.ttl.w, Y.ttl.h, rgb(14, 16, 28), rgb(8, 9, 18));
        } else {
            tfd_vgrad(0, 0, gfx_w, Y.ttl.h, rgb(14, 16, 28), rgb(12, 14, 25));
        }
    }
    hero_pic(Y.hpic, t);
    const char *ttl = LG("LINEAR ALGEBRA", "אלגברה לינארית");
    const char *sub = LG("the beauty of it, step by step", "היופי שלה, צעד אחר צעד");
    const mfont_t *tf = bface(Y.round ? 3 : 2), *sf = SZ[s_tsz];
    if (Y.round || Y.ttl.w >= gfx_w) {
        int cx = Y.round ? gfx_w / 2 : (Y.cont.x - 8) / 2;
        // (12: the capitals stay under the grip of the panel, in the middle of the top edge)
        int base = Y.ttl.y + tf->asc + (Y.round ? 6 : 12);
        if (!Y.round && say_w(tf, ttl) > Y.cont.x - 16) {
            tf = bface(1);
        }
        // the line under the title: a smaller face when it is wider than its room (the CoreS3 cut it at the left edge)
        for (int k = s_tsz; !Y.round && k > 0 && say_w(sf, sub) > Y.cont.x - 16; k--) {
            sf = SZ[k - 1];
        }
        if (Y.round) {  // the title in the top cap: a smaller face when it is wider than the circle there
            int dy = gfx_h / 2 - (base - tf->asc), r = gfx_w / 2 - 12;
            int ch = dy < r ? 2 * (int)sqrtf((float)(r * r - dy * dy)) : 0;
            for (int up = 2; up >= 0 && say_w(tf, ttl) > ch; up--) {
                tf = bface(up);
            }
        }
        say_c(tf, cx, base, ttl, COL_TEXT);
        say_c(sf, cx, base + sf->line + 2, sub, COL_DIM);
    } else {
        int cx = Y.ttl.x + Y.ttl.w / 2;
        say_c(tf, cx, 12 + tf->asc, ttl, COL_TEXT);
        say_c(sf, cx, 16 + tf->asc + sf->line, sub, COL_DIM);
    }
    // CONTINUE
    if (S.ti != 0xFF) {
        rect_t c = Y.cont;
        key_box(c, true, COL_ACC);
        char line[96];
        snprintf(line, sizeof line, "%s %d", LG("CONTINUE", "המשך"), S.ti + 1);
        const mfont_t *cf = bface(0);
        say_c(cf, c.x + c.w / 2, c.y + c.h / 2 + cf->asc / 2, line, COL_ACC);
    }
    // the scroll bar
    int at, size, al;
    if (!Y.round && ks_bar(&s_ks, Y.list.w - 2 * Y.gap, &at, &size, &al)) {
        tfd_noclip();
        tfd_rrect(Y.list.x + Y.gap + at, gfx_h - 4, size, 3, 1, COL_DIM, al);
    }
    if (Y.round) {
        // the dots of the place in the list
        float pos = ks_pos(&s_ks) / (Y.cw + Y.gap);
        int y = Y.list.y + Y.list.h + 2;
        for (int k = 0; k < NT; k++) {
            float d = fabsf(k - pos);
            int a = d < 1 ? (int)(256 - d * 150) : 90;
            tfd_disc(gfx_w / 2 + (k - pos) * 9.0f, (float)y, d < 0.5f ? 3.0f : 2.0f, COL_DIM, a);
        }
    }
}
static void home_input(const input_event_t *ev)
{
    float ax = (float)ev->x;
    switch (ev->kind) {
    case IN_DOWN:
        ks_down(&s_ks, ax, ev->t_ms);
        break;
    case IN_MOVE:
        if (ks_move(&s_ks, ax, ev->t_ms)) {
            input_tap_cancel();
        }
        break;
    case IN_UP:
        ks_up(&s_ks, ev->t_ms);
        break;
    case IN_TAP:
        if (!ks_tap_ok(&s_ks)) {
            break;
        }
        if (S.ti != 0xFF && in_r(&Y.cont, ev->x, ev->y, 4)) {
            fx(FX_OPEN, 160);
            topic_open(S.ti, S.stp[S.ti]);
            return;
        }
        for (int k = 0; k < NT; k++) {
            rect_t r;
            card_rect(k, &r);
            if (in_r(&r, ev->x, ev->y, Y.gap / 2) && in_r(&Y.list, ev->x, ev->y, 0)) {
                if (Y.round && fabsf(ks_pos(&s_ks) - k * (float)(Y.cw + Y.gap)) > Y.cw / 2) {
                    ks_to(&s_ks, k * (float)(Y.cw + Y.gap));  // a side card comes to the middle first
                    return;
                }
                fx(FX_OPEN, 160);
                topic_open(k, S.stp[k]);
                return;
            }
        }
        break;
    default:
        break;
    }
}
