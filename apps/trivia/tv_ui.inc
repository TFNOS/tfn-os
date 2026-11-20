// TRIVIA: the layouts and the screens (included by app_trivia.c). One layout for each shape of the frame: the long bar
// of the 3.49 (640 x 172), the 320 x 240 frame, the tablet frame (640 x 360) and the round panels (466 or 412).

enum {
    LK_BAR,
    LK_BOX,
    LK_TAB,
    LK_ROUND
};
typedef struct {
    int x, y, w, h;
} rect_t;
static struct {
    int kind;
    rect_t q;                // the question (and the headline of the other screens)
    rect_t bar;              // the timer bar (LK_ROUND: none; the arc on the edge of the panel)
    rect_t grid;             // the boxes of the answers and of the buttons
    rect_t tgrid;            // the buttons of the title
    int gap, r;              // the gap between the boxes, the corner radius
    int bcx, bcy, br;        // the badge of the subject (the timer ring around it on the bar)
    int isz_badge, isz_btn;  // the sizes of the icons (TV_ART_PX index)
    int ilogo, logo_y;       // the logo and its centre
    const kfont_t *fq[4], *fa[4], *fh[3];
    int nfq, nfa;
} L;
static const kfont_t *const F_SMALL[] = {&KF_TEXT, &KF_NAME, &KF_LABEL};
static const kfont_t *const F_ONE_BIG[] = {&KF_TITLE, &KF_BIG, &KF_TEXT, &KF_NAME};

static void lay_make(void)
{
    const int W = gfx_w, H = gfx_h;
    memset(&L, 0, sizeof L);
    L.gap = 8;
    L.r = 12;
    const kfont_t *const big[] = {&KF_BIG, &KF_TEXT, &KF_NAME, &KF_LABEL};
    memcpy(L.fq, big, sizeof big);
    memcpy(L.fa, big, sizeof big);
    L.nfq = L.nfa = 4;
    L.fh[0] = &KF_BIG, L.fh[1] = &KF_TEXT, L.fh[2] = &KF_NAME;
    if (gfx_round()) {
        L.kind = LK_ROUND;
        const float s = W / 466.0f;
        L.q = (rect_t){(int)(64 * s), (int)(118 * s), W - (int)(128 * s), (int)(110 * s)};
        L.grid = (rect_t){(int)(24 * s), (int)(240 * s), W - (int)(48 * s), (int)(150 * s)};
        L.tgrid = (rect_t){(int)(24 * s), (int)(226 * s), W - (int)(48 * s), (int)(164 * s)};
        L.bcx = W / 2, L.bcy = (int)(52 * s), L.br = (int)(30 * s);
        L.isz_badge = 2, L.isz_btn = 1, L.ilogo = 1, L.logo_y = (int)(150 * s);
        L.gap = 10, L.r = 16;
    } else if (W >= 3 * H) {
        L.kind = LK_BAR;
        L.q = (rect_t){76, 4, 488, 82};
        L.bar = (rect_t){76, 89, 488, 6};
        L.grid = (rect_t){76, 101, 488, 66};
        L.tgrid = (rect_t){84, 96, 472, 70};
        L.bcx = 606, L.bcy = 42, L.br = 27;
        L.isz_badge = 1, L.isz_btn = 1, L.ilogo = 0, L.logo_y = 36;
    } else if (W >= 600) {
        L.kind = LK_TAB;
        L.q = (rect_t){24, 52, W - 48, 118};
        L.bar = (rect_t){24, 176, W - 48, 9};
        L.grid = (rect_t){24, 194, W - 48, H - 202};
        L.tgrid = (rect_t){40, 176, W - 80, H - 186};
        L.bcx = W - 44, L.bcy = 26, L.br = 20;
        L.isz_badge = 1, L.isz_btn = 2, L.ilogo = 2, L.logo_y = 82;
        L.gap = 10, L.r = 18;
        const kfont_t *const tq[] = {&KF_TITLE, &KF_BIG, &KF_TEXT, &KF_NAME};
        memcpy(L.fq, tq, sizeof tq);
        memcpy(L.fa, tq, sizeof tq);
        L.fh[0] = &KF_TITLE, L.fh[1] = &KF_BIG, L.fh[2] = &KF_TEXT;
    } else {
        L.kind = LK_BOX;
        L.q = (rect_t){8, 30, W - 16, H * 74 / 240};
        L.bar = (rect_t){8, L.q.y + L.q.h + 3, W - 16, 5};
        L.grid = (rect_t){8, L.bar.y + 11, W - 16, H - (L.bar.y + 11) - 5};
        L.tgrid = (rect_t){8, H * 100 / 240, W - 16, H - H * 100 / 240 - 6};
        L.bcx = W - 20, L.bcy = 14, L.br = 12;
        L.isz_badge = 0, L.isz_btn = 1, L.ilogo = 0, L.logo_y = 42;
        L.gap = 6;
    }
}
// the half width of the round panel at the height y (with a margin)
static int round_half(int y, int margin)
{
    const int r = gfx_w / 2 - margin, dy = y - gfx_h / 2;
    const int q = r * r - dy * dy;
    if (q <= 0) {
        return 0;
    }
    int z = r;
    for (int i = 0; i < 12; i++) {
        z = (z + q / (z ? z : 1)) / 2;
    }
    return z;
}
// box i of n in the area g: the bar = one row (the first box at the right: Hebrew reads from the right); the others =
// two columns (a last row with one box is the full width); the round panel keeps each row in the circle
static rect_t grid_box(const rect_t *g, int n, int i)
{
    rect_t b;
    const int gap = L.gap;
    if (L.kind == LK_BAR) {
        const int w = (g->w - (n - 1) * gap) / n;
        const int tot = n * w + (n - 1) * gap;
        const int x0 = g->x + (g->w - tot) / 2;
        b = (rect_t){x0 + tot - (i + 1) * w - i * gap, g->y, w, g->h};
        return b;
    }
    const int rows = (n + 1) / 2, row = i / 2, col = i % 2;
    const int h = (g->h - (rows - 1) * gap) / rows;
    const int y = g->y + row * (h + gap);
    const bool single = n % 2 == 1 && i == n - 1;
    int x0 = g->x, x1 = g->x + g->w;
    if (L.kind == LK_ROUND) {
        const int far = (y + h / 2) < gfx_h / 2 ? y : y + h;
        const int hw = round_half(far, 14);
        if (gfx_w / 2 - hw > x0) {
            x0 = gfx_w / 2 - hw;
        }
        if (gfx_w / 2 + hw < x1) {
            x1 = gfx_w / 2 + hw;
        }
    }
    if (single) {
        const int w = n == 1 ? (x1 - x0) * 2 / 3 : x1 - x0;
        return (rect_t){(x0 + x1 - w) / 2, y, w, h};
    }
    const int w = (x1 - x0 - gap) / 2;
    b = (rect_t){col == 0 ? x1 - w : x0, y, w, h};
    return b;
}
static void box_center(int p, int *cx, int *cy)
{
    const rect_t b = grid_box(&L.grid, 4, p);
    *cx = b.x + b.w / 2;
    *cy = b.y + b.h / 2;
}
// the box of a tap (a fat finger: the boxes grow by half the gap, then the nearest centre wins); -1 = none
static int grid_hit(const rect_t *g, int n, int x, int y)
{
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < n; i++) {
        const rect_t b = grid_box(g, n, i);
        const int m = L.gap / 2 + 3;
        if (x < b.x - m || x >= b.x + b.w + m || y < b.y - m || y >= b.y + b.h + m) {
            continue;
        }
        const int dx = x - (b.x + b.w / 2), dy = y - (b.y + b.h / 2), d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d, best = i;
        }
    }
    return best;
}

static float ease_out(float t)
{
    if (t <= 0) {
        return 0;
    }
    if (t >= 1) {
        return 1;
    }
    const float u = 1 - t;
    return 1 - u * u * u;
}
// a pop: 0 -> a small overshoot -> 1
static float ease_back(float t)
{
    if (t <= 0) {
        return 0;
    }
    if (t >= 1) {
        return 1;
    }
    const float c = 1.7f, u = t - 1;
    return 1 + (c + 1) * u * u * u + c * u * u;
}

// ---------------------------------------------------------------- the players
static const uint8_t PL_RGB[2][3] = {{70, 190, 255}, {255, 150, 60}};
static const char *pl_name(int p)
{
    if (G.mode == MODE_NET) {
        return p == s_me ? "אני" : "החבר";
    }
    return p == 0 ? "שחקן 1" : "שחקן 2";
}
static const char *const LVL_HE[3] = {"קל", "בינוני", "קשה"};
static const uint8_t LVL_RGB[3][3] = {{46, 170, 96}, {226, 150, 30}, {214, 64, 70}};

// a button card: the colour, an icon over the label, a small second line (NULL: none), the alpha
static void button(const rect_t *b, const uint8_t *c, int ic, const char *label, const char *sub, int a, bool hot)
{
    card(b->x, b->y, b->w, b->h, L.r, rgb3(c, hot ? 300 : 230), rgb3(c, hot ? 420 : 330), a);
    const int isz = TV_ART_PX[L.isz_btn];
    const kfont_t *lf = L.kind == LK_TAB ? &KF_BIG : &KF_NAME;  // the label under an icon
    const int sh = sub ? KF_LABEL.line : 0;
    const bool room = b->h >= isz + lf->line + sh + 6;
    int ty = b->y + (b->h - sh) / 2;
    if (ic >= 0 && room) {
        const int tot = isz + 2 + lf->line + sh;
        const int top = b->y + (b->h - tot) / 2;
        icon(L.isz_btn, ic, b->x + b->w / 2, top + isz / 2, a);
        ty = top + isz + 2 + lf->line / 2;
    } else if (ic >= 0 && b->w > 3 * isz) {
        icon(L.isz_btn, ic, b->x + b->w - isz / 2 - 8, b->y + b->h / 2, a);
    }
    static const kfont_t *const fl[] = {&KF_TITLE, &KF_BIG, &KF_TEXT, &KF_NAME, &KF_LABEL};
    const kfont_t *const *f = fl + 1;
    int nf = 4;
    if (ic >= 0 && room) {
        f = L.kind == LK_TAB ? fl + 1 : fl + 3;
        nf = 2;
    } else if (L.kind == LK_TAB) {
        f = fl;
        nf = 5;
    }
    const int lw = b->w - 10 - (ic >= 0 && !room && b->w > 3 * isz ? isz + 8 : 0);
    tx_line(label, f, nf, b->x + 5 + lw / 2, ty + lf->asc / 2, lw, C_WHITE, a);
    if (sub) {
        tx_line(sub, fl + 4, 1, b->x + b->w / 2, ty + lf->asc / 2 + KF_LABEL.line, b->w - 8, rgb(240, 236, 255),
                a * 9 / 10);
    }
}

// ---------------------------------------------------------------- the frame of a question: the badge, the scores
static void num_text(int v, char *b, int n)
{
    snprintf(b, (size_t)n, "%d", v);
}
// the badge of the subject (a light disc, the icon) and the timer ring round it; f = the part of the time left (< 0: none)
static void badge(int subj, int cx, int cy, int r, float f, int a)
{
    ui_circle(cx, cy, r, rgb(250, 246, 255), a * 230 / 255);
    if (f >= 0) {
        ui_ring(cx, cy, r + 4, 4, rgb(30, 20, 60), a / 2);
        arc(cx, cy, r + 2, 2, f, time_col(f), a);
    }
    icon(L.isz_badge, subj, cx, cy, a);
}
// the scores: a big number for each player (the colour of the player when there are two)
static void scores_col(int x, int w, int y0, int a)
{
    char b[16];
    if (G.players == 1) {
        tx_line("ניקוד", F_SMALL + 2, 1, x + w / 2, y0 + 12, w, C_DIM, a);
        num_text(G.score[0], b, sizeof b);
        mf_text_oc(&MF_N24, x + w / 2, y0 + 42, b, C_WHITE, C_INK, 2, 1);
        return;
    }
    for (int p = 0; p < 2; p++) {
        const int y = y0 + p * 46;
        const bool on = G.mode == MODE_PASS ? p == active_player() : p == s_me;
        tx_line(pl_name(p), F_SMALL + 2, 1, x + w / 2, y + 12, w, rgb3(PL_RGB[p], on ? 340 : 256), a);
        num_text(G.score[p], b, sizeof b);
        mf_text_oc(&MF_N20, x + w / 2, y + 36, b, on ? C_WHITE : C_DIM, C_INK, 2, 1);
    }
}
// the streak of the player now (the fire and the count), from 2 right answers in a row
static void streak_mark(int cx, int cy, int a)
{
    const int p = active_player(), n = G.streak[p];
    if (n < 2) {
        return;
    }
    char b[8];
    num_text(n, b, sizeof b);
    icon(0, TI_FIRE, cx + 10, cy, a);
    mf_text_oc(&MF_N16, cx - 10, cy + 7, b, C_GOLD, C_INK, 1, 1);
}
static void q_frame_bar(float f, int a)
{
    char b[24];
    // the right side: the badge with the timer ring, the subject, the level
    badge(s_q.subj, L.bcx, L.bcy, L.br, f, a);
    tx_line(SUBJ_HE[s_q.subj], F_SMALL + 1, 2, L.bcx, 98, 66, C_WHITE, a);
    tx_line(LVL_HE[G.lvl], F_SMALL + 2, 1, L.bcx, 118, 66, rgb3(LVL_RGB[G.lvl], 360), a);
    snprintf(b, sizeof b, "%d/%d", G.cur + 1, G.n);
    tx_line(b, F_SMALL, 1, L.bcx, 146, 66, C_DIM, a);
    if (G.mode == MODE_PASS) {
        const int p = active_player();
        box_a(L.bcx - 30, 154, 60, 16, 8, rgb3(PL_RGB[p], 200), a);
        tx_line(pl_name(p), F_SMALL + 2, 1, L.bcx, 167, 56, C_WHITE, a);
    }
    // the left side: the scores and the streak
    scores_col(2, 68, 6, a);
    streak_mark(36, G.players == 1 ? 82 : 116, a);
}
static void q_frame_top(float f, int a)
{
    char b[24];
    const bool tab = L.kind == LK_TAB;
    const int H = tab ? 44 : 26;
    const kfont_t *const *fs = tab ? F_ONE_BIG + 1 : F_SMALL;
    // the right end: the badge and the subject (the text ends at the badge)
    badge(s_q.subj, L.bcx, L.bcy, L.br, -1, a);
    static tx_t t;
    const int sw = gfx_w / 3 - 20;
    tx_fit(&t, SUBJ_HE[s_q.subj], fs, 2, sw, H);
    t.nl = 1;
    const int xr = L.bcx - L.br - 6;
    tx_draw_sh(&t, xr - (int)t.lw[0] / 2, (H - t.f->line) / 2 + 1, C_WHITE, a);
    // the middle: the question number (and the level on the tablet)
    if (tab) {
        snprintf(b, sizeof b, "%d/%d | %s", G.cur + 1, G.n, LVL_HE[G.lvl]);
    } else {
        snprintf(b, sizeof b, "%d/%d", G.cur + 1, G.n);
    }
    tx_line(b, fs, 2, gfx_w / 2, H / 2 + (tab ? 9 : 6), sw, C_DIM, a);
    // the left end: the scores (two players: a dot of the colour of each), the streak after them
    int x = 8;
    const mfont_t *nf = tab ? &MF_N28 : G.players == 2 ? &MF_N16 : &MF_N20;
    for (int p = 0; p < G.players; p++) {
        num_text(G.score[p], b, sizeof b);
        if (G.players == 2) {
            const bool on = G.mode == MODE_PASS ? p == active_player() : p == s_me;
            ui_circle(x + 5, H / 2, on ? 5 : 4, rgb3(PL_RGB[p], on ? 330 : 200), a);
            x += 13;
        }
        mf_text_o(nf, x, H / 2 + nf->asc / 2, b, C_WHITE, C_INK, 2, 1);
        x += mf_width(nf, b, 1) + 12;
    }
    if (tab || G.players == 1) {
        streak_mark(x + 20, H / 2, a);
    }
    (void)f;
}
static void q_frame_round(float f, int a)
{
    char b[40];
    // the timer: an arc on the edge of the panel
    ui_ring(gfx_w / 2, gfx_h / 2, gfx_w / 2 - 6, 5, rgb(20, 12, 44), 160);
    if (f >= 0) {
        arc(gfx_w / 2, gfx_h / 2, gfx_w / 2 - 6, 3, f, time_col(f), a);
    }
    badge(s_q.subj, L.bcx, L.bcy, L.br, -1, a);
    if (G.players == 1) {
        snprintf(b, sizeof b, "%s | %d/%d | %d", SUBJ_HE[s_q.subj], G.cur + 1, G.n, G.score[0]);
    } else {
        snprintf(b, sizeof b, "%d/%d | %d : %d", G.cur + 1, G.n, G.score[0], G.score[1]);
    }
    tx_line(b, F_SMALL, 2, gfx_w / 2, L.bcy + L.br + 22, gfx_w / 2, C_WHITE, a);
    streak_mark(gfx_w / 2 + L.br + 30, L.bcy, a);
}

// ---------------------------------------------------------------- the question screen
// the state of answer box p now: 0 normal, 1 chosen (it waits), 2 right, 3 chosen and wrong, 4 dim
static int ans_state(int p)
{
    const int me = active_player();
    if (s_qp == QP_SHOW) {
        if (p == s_q.right) {
            return 2;
        }
        for (int k = 0; k < G.players; k++) {
            if (G.pick[k] == p && (G.mode != MODE_PASS || k == me)) {
                return 3;
            }
        }
        return 4;
    }
    return G.pick[me] == p ? 1 : 0;
}
static void answer_box(int p, int64_t now, int a)
{
    rect_t b = grid_box(&L.grid, 4, p);
    const float tin = (float)(now - s_qp_t0) / 1000.0f;
    if (s_qp == QP_IN) {
        const float e = ease_back((tin - 0.30f - 0.09f * p) / 0.35f);
        if (e <= 0) {
            return;
        }
        a = a * (e > 1 ? 255 : (int)(e * 255)) / 255;
        b.y += (int)((1 - e) * 14);
    }
    const int st = ans_state(p);
    const uint8_t *c = ANS_RGB[p];
    uint16_t fill = rgb3(c, 230), edge = rgb3(c, 330);
    if (st == 1) {
        const int k = 300 + (int)(30 * fsin(now / 90.0f));
        fill = rgb3(c, k);
        edge = C_WHITE;
    } else if (st == 2) {
        const int k = (int)(40 * fsin(now / 140.0f));
        fill = rgb((uint8_t)(40 + k / 2), (uint8_t)(170 + k), 90);
        edge = rgb(200, 255, 210);
    } else if (st == 3) {
        fill = rgb(180, 44, 52);
        edge = rgb(255, 150, 150);
        const float ts = (float)(now - s_qp_t0) / 1000.0f;
        if (ts < 0.45f) {
            b.x += (int)(6 * fsin(ts * 60) * (0.45f - ts) / 0.45f);
        }
    } else if (st == 4) {
        // dim: solid colours mixed toward the dark (a blend of the whole box costs too much on the board)
        fill = mix_fb(fill, rgb(24, 16, 44), 150);
        edge = mix_fb(edge, rgb(24, 16, 44), 150);
    }
    if (st == 1 || st == 2) {
        box_a(b.x - 3, b.y - 3, b.w + 6, b.h + 6, L.r + 3, edge, a / 3);
    }
    card(b.x, b.y, b.w, b.h, L.r, fill, edge, a);
    const int ic = st == 2 ? TI_OK : st == 3 ? TI_NO : -1;
    int tw = b.w - 10;
    if (ic >= 0) {
        icon(0, ic, b.x + b.w - 14, b.y + 14, a);
    }
    static tx_t t;
    tx_fit(&t, s_q.a[p], L.fa, L.nfa, tw, b.h - 6);
    tx_draw_sh(&t, b.x + b.w / 2, b.y + (b.h - tx_h(&t)) / 2, st == 4 ? rgb(150, 144, 172) : C_WHITE, a);
    // the marks of the players (two players): a dot of the colour of each one who chose this box
    if (s_qp == QP_SHOW && G.players == 2) {
        for (int k = 0; k < 2; k++) {
            if (G.pick[k] == p && (G.mode == MODE_NET || k == active_player())) {
                ui_circle(b.x + 10 + k * 14, b.y + b.h - 10, 6, rgb3(PL_RGB[k], 300), a);
                ui_ring(b.x + 10 + k * 14, b.y + b.h - 10, 6, 1, C_WHITE, a);
            }
        }
    }
}
// the result line of the player of this board (or of the turn): right, wrong, the time ended
static void result_head(char *b, int n, uint16_t *col)
{
    const int p = G.mode == MODE_PASS ? active_player() : s_me;
    if (G.pick[p] == s_q.right) {
        if (G.mode == MODE_NET && G.first == p) {
            snprintf(b, (size_t)n, "ראשון! %d נקודות", G.gain[p]);
        } else {
            snprintf(b, (size_t)n, "נכון! %d נקודות", G.gain[p]);
        }
        *col = rgb(120, 255, 160);
    } else if (G.pick[p] < 0) {
        snprintf(b, (size_t)n, "נגמר הזמן");
        *col = rgb(255, 210, 110);
    } else {
        snprintf(b, (size_t)n, "לא נכון");
        *col = rgb(255, 130, 130);
    }
}
static void q_text(int64_t now, int a)
{
    const rect_t *q = &L.q;
    panel(q->x - 2, q->y, q->w + 4, q->h, L.r, 80);
    if (s_qp != QP_SHOW) {
        const float e = s_qp == QP_IN ? ease_out((float)(now - s_qp_t0) / 450.0f) : 1;
        const int dx = (int)((1 - e) * -60);
        tx_box(s_q.q, L.fq, L.nfq, q->x + 6 + dx, q->y + 2, q->w - 12, q->h - 4, C_WHITE, a * (int)(e * 255) / 255);
        return;
    }
    // the result: the headline, then the fact
    char b[48];
    uint16_t col;
    result_head(b, sizeof b, &col);
    const float e = ease_out((float)(now - s_qp_t0) / 350.0f);
    static tx_t t;
    const kfont_t *hf = L.fh[0];
    const int hh = hf->line;
    // (the room of the points at the left end of the line: q_pop)
    const int pw = G.gain[G.mode == MODE_PASS ? active_player() : s_me] > 0 ? 70 : 0;
    tx_line(b, L.fh, 3, q->x + pw + (q->w - pw) / 2, q->y + 4 + hf->asc + (hh - hf->asc) / 2, q->w - 12 - pw, col,
            a * (int)(e * 255) / 255);
    tx_fit(&t, s_q.fact, L.fq + 1, L.nfq - 1, q->w - 16, q->h - hh - 6);
    tx_draw_sh(&t, q->x + q->w / 2, q->y + hh + 4 + (q->h - hh - 6 - tx_h(&t)) / 2, rgb(236, 232, 255),
               a * (int)(e * 255) / 255);
}
// the timer bar (and in the result: the hint to go on, or the wait for the other board)
static void q_bar(int64_t now, float f, int a)
{
    const rect_t *r = &L.bar;
    if (!r->w) {
        return;
    }
    if (s_qp == QP_SHOW) {
        if (G.mode == MODE_NET) {
            const float k = (float)(now - s_qp_t0) / NET_SHOW_MS;
            box_a(r->x, r->y, r->w, r->h, r->h / 2, rgb(20, 12, 44), a / 2);
            box_a(r->x, r->y, (int)(r->w * (k > 1 ? 1 : k)), r->h, r->h / 2, C_DIM, a / 2);
        } else if (now - s_qp_t0 > NEXT_MS) {
            const int pa = 140 + (int)(100 * fsin(now / 260.0f));
            tx_line("הקישו כדי להמשיך", F_SMALL + 2, 1, r->x + r->w / 2, r->y + r->h / 2 + 5, r->w, C_WHITE,
                    pa * a / 255);
        }
        return;
    }
    panel(r->x, r->y, r->w, r->h, r->h / 2, 190);
    const int w = (int)(r->w * f);
    const bool pulse = f < 0.25f && s_qp == QP_ASK;
    const int pa = pulse ? 180 + (int)(75 * fsin(now / 80.0f)) : 255;
    if (w > 0) {
        box_a(r->x + r->w - w, r->y, w, r->h, r->h / 2, time_col(f), a * pa / 255);  // it shrinks to the left
    }
    if (G.mode == MODE_NET && s_qp == QP_ASK) {
        const int other = 1 - s_me;
        const char *m = G.pick[s_me] >= 0 ? (G.pick[other] >= 0 ? NULL : "ממתינים לחבר...")
                                          : (G.pick[other] >= 0 ? "החבר כבר ענה!" : NULL);
        if (m) {
            tx_line(m, F_SMALL + 2, 1, r->x + r->w / 2, r->y - 2, r->w, C_GOLD, a);
        }
    }
}
// where the score of player p shows (the points of a right answer fly there)
static void score_pos(int p, int *x, int *y)
{
    if (L.kind == LK_BAR) {
        *x = 36;
        *y = G.players == 1 ? 40 : 6 + p * 46 + 30;
    } else if (L.kind == LK_ROUND) {
        *x = gfx_w / 2;
        *y = L.bcy + L.br + 14;
    } else {
        *x = 10 + p * (L.kind == LK_TAB ? 110 : 64) + 24;
        *y = (L.kind == LK_TAB ? 44 : 26) / 2;
    }
}
// the points of the result: they rise from the right box, then fly to the score and get smaller
static void q_pop(int64_t now)
{
    const int p = G.mode == MODE_PASS ? active_player() : s_me;
    if (s_qp != QP_SHOW || G.gain[p] <= 0) {
        return;
    }
    const float t = (float)(now - s_qp_t0) / 1000.0f;
    if (t > 1.25f) {
        return;
    }
    int sx, sy;
    score_pos(p, &sx, &sy);
    char b[16];
    snprintf(b, sizeof b, "+%d", G.gain[p]);
    // the points pop at the left end of the headline (the answers and the fact stay readable), then fly to the score
    const mfont_t *f = L.kind == LK_BOX ? &MF_N20 : &MF_N24;
    const int x0 = L.q.x + 8 + mf_width(f, b, 1) / 2, y0 = L.q.y + 4 + L.fh[0]->asc;
    if (t < 0.5f) {
        const float e = ease_back(t / 0.3f);
        mf_text_oc(f, x0, y0 + (int)((1 - (e > 1 ? 1 : e)) * 8), b, C_GOLD, C_INK, 2, 1);
        return;
    }
    const float k = ease_out((t - 0.5f) / 0.75f);
    const int x = (int)(x0 + (sx - x0) * k), y = (int)(y0 + (sy + 6 - y0) * k);
    mf_text_oc(k < 0.5f ? f : &MF_N16, x, y, b, C_GOLD, C_INK, 2, 1);
}
static void render_q(int64_t now)
{
    if (G.mode == MODE_NET && s_me == 1 && !s_cl_have) {
        tx_box("מתחילים...", F_ONE_BIG, 4, 0, 0, gfx_w, gfx_h, C_WHITE, 255);
        return;
    }
    const int left = s_qp == QP_ASK ? q_left(now) : (s_qp == QP_IN ? s_t_total : 0);
    const float f = s_t_total ? (float)left / s_t_total : 0;
    const float fr = s_qp == QP_SHOW ? -1 : f;
    if (L.kind == LK_BAR) {
        q_frame_bar(fr, 255);
    } else if (L.kind == LK_ROUND) {
        q_frame_round(fr, 255);
    } else {
        q_frame_top(fr, 255);
    }
    q_text(now, 255);
    q_bar(now, f, 255);
    for (int p = 0; p < 4; p++) {
        answer_box(p, now, 255);
    }
    part_draw();
    q_pop(now);
}

// ---------------------------------------------------------------- the title
enum {
    TB_CONT,
    TB_SOLO,
    TB_PASS,
    TB_NET
};
static int title_buttons(int *ids)
{
    int n = 0;
    if (S.r_on) {
        ids[n++] = TB_CONT;
    }
    ids[n++] = TB_SOLO;
    ids[n++] = TB_PASS;
    ids[n++] = TB_NET;
    return n;
}
static const uint8_t TB_RGB[4][3] = {{214, 150, 30}, {50, 128, 236}, {226, 70, 110}, {136, 86, 222}};
static const int TB_ICON[4] = {TI_WATCH, TI_BRAIN, TI_TWO, TI_PHONE};
static const char *const TB_HE[4] = {"המשך", "משחק", "שניים בלוח", "עם חבר"};

// the icons of the subjects go round the logo (the bar: a slow row behind it)
static void title_icons(int64_t now)
{
    const float t = now / 1000.0f;
    const int sz = L.kind == LK_TAB ? 2 : 1;
    if (L.kind == LK_BAR) {
        const float span = TV_NSUBJ * 58.0f;
        const float off = t * 18 - (int)(t * 18 / span) * span;
        for (int i = 0; i < TV_NSUBJ; i++) {
            float x = i * 58.0f - off;
            if (x < -40) {
                x += span;
            }
            const int y = 30 + (int)(8 * fsin(t * 1.3f + i));
            const pic_t *lg = s_art[TV_ART_SIZES] ? &s_logo[L.ilogo] : NULL;
            if (lg && x > gfx_w / 2 - lg->w / 2 + 14 && x < gfx_w / 2 + lg->w / 2 - 14) {
                continue;  // (the logo covers it: no blend of a hidden icon)
            }
            icon(sz, i, (int)x, y, 70);
        }
        return;
    }
    const int rx = L.kind == LK_BOX ? gfx_w / 2 - 22 : (L.kind == LK_TAB ? 290 : gfx_w * 34 / 100);
    const int ry = L.kind == LK_BOX ? 30 : (L.kind == LK_TAB ? 64 : gfx_h * 16 / 100);
    for (int i = 0; i < TV_NSUBJ; i++) {
        const float ang = t * 0.35f + i * 6.2832f / TV_NSUBJ;
        const float z = fsin(ang);  // the back of the ring is faint
        icon(sz, i, gfx_w / 2 + (int)(rx * fcos(ang)), L.logo_y + (int)(ry * z), 90 + (int)(z * 60));
    }
}
static void render_title(int64_t now)
{
    char b[64];
    title_icons(now);
    const float e = ease_back((float)(now - s_st_t0) / 600.0f);
    const pic_t *lg = s_art[TV_ART_SIZES] ? &s_logo[L.ilogo] : NULL;
    const int bob = (int)(3 * fsin(now / 500.0f));
    if (lg) {
        pic_draw(lg, gfx_w / 2, L.logo_y + bob - (int)((1 - e) * 20), (int)(255 * (e > 1 ? 1 : e)));
    }
    snprintf(b, sizeof b, "%d שאלות | 12 נושאים | 3 רמות", TV_BANK_N);
    const int sy = L.logo_y + (lg ? lg->h / 2 : 20) + (L.kind == LK_BAR ? 12 : 14);
    tx_line(b, F_SMALL + 1, 2, gfx_w / 2, sy, gfx_w * 3 / 4, C_DIM, 255);
    int ids[4];
    const int n = title_buttons(ids);
    for (int i = 0; i < n; i++) {
        rect_t r = grid_box(&L.tgrid, n, i);
        const float k = ease_back((float)(now - s_st_t0 - 120 - 70 * i) / 400.0f);
        if (k <= 0) {
            continue;
        }
        r.y += (int)((1 - k) * 16);
        const char *sub = NULL;
        if (ids[i] == TB_CONT) {
            snprintf(b, sizeof b, "%s %d/10", LVL_HE[S.r_lvl], S.r_cur + 1);
            sub = b;
        }
        button(&r, TB_RGB[ids[i]], TB_ICON[ids[i]], TB_HE[ids[i]], sub, (int)(255 * (k > 1 ? 1 : k)), false);
    }
}

// ---------------------------------------------------------------- the level, the turn, the end
static void render_level(int64_t now)
{
    char sub[3][40];
    static const char *const MODE_HE[3] = {"משחק יחיד", "שניים בלוח אחד", "עם חבר בלוח שני"};
    tx_box(MODE_HE[s_mode_pick], L.fh, 3, L.q.x, L.q.y, L.q.w, L.q.h / 2, C_DIM, 255);
    tx_box("בחרו רמה", F_ONE_BIG, 4, L.q.x, L.q.y + L.q.h / 3, L.q.w, L.q.h * 2 / 3, C_WHITE, 255);
    static const uint8_t BACK[3] = {90, 84, 120};
    for (int i = 0; i < 4; i++) {
        rect_t r = grid_box(&L.grid, 4, i);
        const float k = ease_back((float)(now - s_st_t0 - 60 * i) / 380.0f);
        if (k <= 0) {
            continue;
        }
        r.y += (int)((1 - k) * 14);
        const int a = (int)(255 * (k > 1 ? 1 : k));
        if (i == 3) {
            button(&r, BACK, -1, "חזרה", NULL, a, false);
            continue;
        }
        if (s_mode_pick == MODE_SOLO && S.best[i]) {
            snprintf(sub[i], sizeof sub[i], "שיא %u", (unsigned)S.best[i]);
        } else {
            snprintf(sub[i], sizeof sub[i], "%d חדשות", fresh_count(i));
        }
        button(&r, LVL_RGB[i], -1, LVL_HE[i], sub[i], a, false);
    }
}
static void render_pass(int64_t now)
{
    const int p = active_player();
    char b[48];
    snprintf(b, sizeof b, "התור של %s", pl_name(p));
    const float e = ease_back((float)(now - s_st_t0) / 500.0f);
    tx_box(b, F_ONE_BIG, 4, L.q.x, L.q.y, L.q.w, L.q.h * 3 / 5, rgb3(PL_RGB[p], 330), (int)(255 * (e > 1 ? 1 : e)));
    snprintf(b, sizeof b, "שאלה %d מתוך %d | %d : %d", G.cur + 1, G.n, G.score[0], G.score[1]);
    tx_box(b, F_SMALL, 3, L.q.x, L.q.y + L.q.h * 3 / 5, L.q.w, L.q.h * 2 / 5, C_DIM, 255);
    rect_t r = grid_box(&L.grid, 1, 0);
    if (L.kind == LK_BAR) {
        r.x = gfx_w / 2 - 130, r.w = 260;
    }
    button(&r, PL_RGB[p], TI_HAND, "מוכנים? הקישו כאן", NULL, 255, (now / 600) % 2 == 0);
}
// the end of a round: who won, the points, the right answers; AGAIN and the title
static void end_head(char *b, int n, int *icon_id)
{
    *icon_id = TI_TROPHY;
    if (G.mode == MODE_SOLO) {
        const char *t = s_new_best        ? "שיא חדש!"
                        : G.right[0] >= 8 ? "מצוין!"
                        : G.right[0] >= 5 ? "יפה מאוד!"
                                          : "סיימתם!";
        snprintf(b, (size_t)n, "%s", t);
        if (!s_new_best && G.right[0] < 5) {
            *icon_id = TI_THINK;
        }
        return;
    }
    if (G.score[0] == G.score[1]) {
        snprintf(b, (size_t)n, "תיקו!");
        *icon_id = TI_HAND;
    } else if (G.mode == MODE_NET) {
        const bool won = G.score[s_me] > G.score[1 - s_me];
        snprintf(b, (size_t)n, "%s", won ? "ניצחת!" : "החבר ניצח");
        *icon_id = won ? TI_TROPHY : TI_MEDAL;
    } else {
        snprintf(b, (size_t)n, "%s ניצח!", pl_name(G.score[1] > G.score[0]));
    }
}
// the two buttons of the end (0 = again, 1 = the title)
static rect_t end_button(int i)
{
    rect_t r = grid_box(&L.grid, 2, i);
    if (L.kind == LK_BAR) {
        r.x = i == 0 ? gfx_w / 2 + 8 : gfx_w / 2 - 188, r.w = 180;
    }
    return r;
}
static void render_end(int64_t now)
{
    char b[64];
    int ic;
    end_head(b, sizeof b, &ic);
    const float t = (float)(now - s_st_t0) / 1000.0f, e = ease_back(t / 0.6f);
    const int a = (int)(255 * (e > 1 ? 1 : e));
    const rect_t *q = &L.q;
    const int hh = L.fh[0]->line;
    tx_line(b, L.fh, 3, q->x + q->w / 2, q->y + 2 + hh * 3 / 4, q->w, C_GOLD, a);
    const float c = ease_out(t / 1.2f);  // the numbers count up
    for (int p = 0; p < G.players; p++) {
        const int cx = G.players == 1 ? q->x + q->w / 2 : q->x + q->w * (p == 0 ? 3 : 1) / 4;
        snprintf(b, sizeof b, "%d", (int)(G.score[p] * c));
        mf_text_oc(L.kind == LK_TAB ? &MF_N34 : &MF_N28, cx, q->y + hh + 30, b,
                   G.players == 1 ? C_WHITE : rgb3(PL_RGB[p], 340), C_INK, 2, 1);
        if (G.players == 1) {
            snprintf(b, sizeof b, "%d מתוך 10 נכונות | רצף %d", G.right[p], G.best_streak[p]);
        } else {
            snprintf(b, sizeof b, "%s: %d נכונות", pl_name(p), G.right[p]);
        }
        tx_line(b, F_SMALL, 3, cx, q->y + hh + 50, G.players == 1 ? q->w : q->w / 2 - 8, C_DIM, a);
    }
    if (L.kind == LK_BAR) {
        icon(3, ic, 606, 70 + (int)(4 * fsin(now / 300.0f)), a);
        icon(3, G.players == 1 ? TI_STAR : TI_CROWN, 34, 70 + (int)(4 * fsin(now / 330.0f + 1)), a);
    } else {
        icon(L.isz_badge + 1, ic, L.bcx - 6, L.bcy + 8, a);
    }
    static const uint8_t AG[3] = {46, 170, 96}, MN[3] = {90, 84, 120};
    for (int i = 0; i < 2; i++) {
        const rect_t r = end_button(i);
        const float k = ease_back((t - 0.8f - 0.1f * i) / 0.4f);
        if (k > 0) {
            button(&r, i == 0 ? AG : MN, -1, i == 0 ? "עוד סבב" : "לתפריט", NULL, (int)(255 * (k > 1 ? 1 : k)), false);
        }
    }
    part_draw();
}
static void render_msg(const char *l1, const char *l2)
{
    tx_box(l1, F_ONE_BIG, 4, 16, gfx_h / 2 - 70, gfx_w - 32, 76, C_WHITE, 255);
    tx_box(l2, F_SMALL, 3, 10, gfx_h / 2 + 8, gfx_w - 20, 40, C_DIM, 255);
}
// the lobby of two boards: the own Hebrew screen; while the user must choose a friend (or add a new board), the lobby
// of net2 (its buttons are the targets of net2_lobby_tap)
static void render_lobby(int64_t now)
{
    if (net2_lobby_choosing()) {
        tx_line("בחרו חבר", F_ONE_BIG, 4, gfx_w / 2, L.kind == LK_ROUND ? gfx_h / 5 : 24, gfx_w * 3 / 4, C_WHITE, 255);
        net2_draw_lobby("TRIVIA");
        return;
    }
    const bool bar = L.kind == LK_BAR;
    const int cx = bar ? gfx_w - 90 : gfx_w / 2;
    const int cy = bar ? gfx_h / 2 : L.kind == LK_BOX ? gfx_h / 2 + 4 : L.q.y + L.q.h / 2 + 10;
    // rings that go out from the phone: the board looks for the other one
    for (int i = 0; i < 3; i++) {
        const float k = (float)((now + i * 500) % 1500) / 1500.0f;
        const int rr = 18 + (int)(k * (bar ? 62 : L.kind == LK_BOX ? 40 : 70));
        ui_ring(cx, cy, rr, 3, rgb3(TB_RGB[TB_NET], 380), (int)(200 * (1 - k)));
    }
    ui_circle(cx, cy, 26, rgb(250, 246, 255), 230);
    icon(L.isz_btn, TI_PHONE, cx, cy, 255);
    char b[64];
    snprintf(b, sizeof b, "מחפשים חבר%.*s", (int)(now / 400) % 4, "...");
    if (bar) {
        tx_line("עם חבר בלוח שני", F_ONE_BIG, 4, 290, 34, 440, C_WHITE, 255);
        tx_line("בלוח של החבר: פתחו טריוויה ובחרו עם חבר", F_SMALL, 3, 290, 72, 460, C_DIM, 255);
        tx_line(b, F_ONE_BIG + 1, 3, 290, 112, 440, C_GOLD, 255);
        snprintf(b, sizeof b, "רמה: %s | הקישו כדי לבטל", LVL_HE[s_lvl]);
        tx_line(b, F_SMALL + 1, 2, 290, 150, 440, C_DIM, 255);
        return;
    }
    const int top = L.kind == LK_ROUND ? gfx_h / 5 : (L.kind == LK_TAB ? 40 : 24);
    tx_line("עם חבר בלוח שני", F_ONE_BIG, 4, gfx_w / 2, top, gfx_w * 3 / 4, C_WHITE, 255);
    tx_box("בלוח של החבר: פתחו טריוויה ובחרו עם חבר", F_SMALL, 3, gfx_w / 8, top + 8, gfx_w * 3 / 4, 44, C_DIM, 255);
    const int y2 = L.grid.y + L.grid.h / 3;
    tx_line(b, F_ONE_BIG + 1, 3, gfx_w / 2, y2 + 20, gfx_w * 3 / 4, C_GOLD, 255);
    snprintf(b, sizeof b, "רמה: %s | הקישו כדי לבטל", LVL_HE[s_lvl]);
    tx_line(b, F_SMALL + 1, 2, gfx_w / 2, y2 + 52, gfx_w * 3 / 4, C_DIM, 255);
}
// the link to the other board ended: a Hebrew banner for 3 s (in place of the English line of net2)
static void render_lost(int64_t now)
{
    if (!s_lost_t || now - s_lost_t > 3000) {
        return;
    }
    const int h = 40, y = gfx_h / 2 - h / 2;
    box_a(gfx_w / 8, y, gfx_w * 3 / 4, h, 14, rgb(150, 30, 50), 235);
    tx_line("החיבור לחבר נותק", F_ONE_BIG + 1, 3, gfx_w / 2, y + 28, gfx_w * 3 / 4 - 16, C_WHITE, 255);
}

// ---------------------------------------------------------------- the touch
static void input_title(int x, int y, int64_t now)
{
    int ids[4];
    const int n = title_buttons(ids), k = grid_hit(&L.tgrid, n, x, y);
    if (k < 0) {
        return;
    }
    sfx(FX_TAP, 55);
    if (ids[k] == TB_CONT) {
        round_continue(now);
        return;
    }
    s_mode_pick = ids[k] == TB_SOLO ? MODE_SOLO : ids[k] == TB_PASS ? MODE_PASS : MODE_NET;
    go(ST_LEVEL, now);
}
static void input_level(int x, int y, int64_t now)
{
    const int k = grid_hit(&L.grid, 4, x, y);
    if (k < 0) {
        return;
    }
    sfx(FX_TAP, 55);
    if (k == 3) {
        go(ST_TITLE, now);
        return;
    }
    s_lvl = k;
    if (s_mode_pick == MODE_NET) {
        G.lvl = k;
        net_lobby(now);
    } else {
        round_new(s_mode_pick, k, now);
    }
}
static void input_q(int x, int y, int64_t now)
{
    if (G.mode == MODE_NET && s_me == 1) {
        const int k = grid_hit(&L.grid, 4, x, y);
        if (k >= 0) {
            client_answer(k, now);
        }
        return;
    }
    if (s_qp == QP_ASK) {
        const int k = grid_hit(&L.grid, 4, x, y), p = active_player();
        if (k >= 0 && G.pick[p] < 0) {
            q_answer(p, k, q_left(now), now);
        }
    } else if (s_qp == QP_SHOW && G.mode != MODE_NET && now - s_qp_t0 > NEXT_MS) {
        q_next(now);
    }
}
static bool in_rect(const rect_t *r, int x, int y, int m)
{
    return x >= r->x - m && x < r->x + r->w + m && y >= r->y - m && y < r->y + r->h + m;
}
static void input_end(int x, int y, int64_t now)
{
    if (now - s_st_t0 < 900) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        const rect_t r = end_button(i);
        if (!in_rect(&r, x, y, L.gap / 2 + 3)) {
            continue;
        }
        sfx(FX_TAP, 55);
        part_clear();
        if (i == 1) {
            net_off();
            go(ST_TITLE, now);
            theme_to(-1);
        } else if (G.mode == MODE_NET) {
            if (s_net == N2_HOST) {
                s_seq++;
                round_new(MODE_NET, G.lvl, now);
            } else if (s_net == N2_CLIENT) {
                s_again_t = now;
            }
        } else {
            round_new(G.mode, G.lvl, now);
        }
        return;
    }
}

// the solid boxes of this frame: the background leaves them out (tv_draw.inc hole_add)
static void holes(int64_t now)
{
    s_nhole = 0;
    if (s_st == ST_Q && !(G.mode == MODE_NET && s_me == 1 && !s_cl_have)) {
        hole_add(L.q.x - 2, L.q.y, L.q.w + 4, L.q.h, L.r);
        const bool shake = s_qp == QP_SHOW && now - s_qp_t0 < 500;
        for (int p = 0; p < 4 && s_qp != QP_IN; p++) {
            if (shake && ans_state(p) == 3) {
                continue;
            }
            const rect_t b = grid_box(&L.grid, 4, p);
            hole_add(b.x, b.y, b.w, b.h, L.r);
        }
    } else if (s_st == ST_TITLE && now - s_st_t0 > 1200) {
        int ids[4];
        const int n = title_buttons(ids);
        for (int i = 0; i < n; i++) {
            const rect_t b = grid_box(&L.tgrid, n, i);
            hole_add(b.x, b.y, b.w, b.h, L.r);
        }
    } else if (s_st == ST_LEVEL && now - s_st_t0 > 1000) {
        for (int i = 0; i < 4; i++) {
            const rect_t b = grid_box(&L.grid, 4, i);
            hole_add(b.x, b.y, b.w, b.h, L.r);
        }
    }
}
