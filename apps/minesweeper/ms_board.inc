// MINESWEEPER: the board (included by app_mines.c). The mines come at the first dig (never at it or next to it), and a
// board stays only when the numbers alone can open all of it from there (a logic solver: no guess is necessary). The
// surprises under the tiles. The dig opens a wave (the tiles of a 0 area open one ring after the other); a dig on a
// number with all its flags opens its other neighbours.

#define MAXC 20
#if BOARD_UI_TABLET
#define MAXR 14  // (the Tab5: boards of up to 20 x 14 tiles)
#elif BOARD_ROUND
#define MAXR 12  // (the round frame: round boards of up to 12 x 12 tiles)
#else
#define MAXR 9
#endif
enum {
    CS_COV,
    CS_OPEN,
    CS_FLAG
};
enum {
    IT_NONE,
    IT_COIN,
    IT_CLOVER,
    IT_MOLE,
    IT_CHEST
};
typedef struct {
    uint8_t mine, n, st, item, hint, var, done, popped;
    float t_open, t_flag;  // the time of the reveal (in the wave), of the last flag change
} cell_t;
static cell_t B[MAXR][MAXC];
static int s_cols, s_rows, s_mines, s_open, s_flags, s_gen_tries;
static uint8_t s_hole[MAXR][MAXC];  // the round frame: 1 = no tile here (out of the round board)
static int s_ncell;                 // the tiles of the board
static bool s_first;
static float s_now, s_wave_end;  // the game clock (s); the time when the last tile of the wave opens

static inline bool inb(int c, int r)
{
    return c >= 0 && r >= 0 && c < s_cols && r < s_rows && !s_hole[r][c];
}
#define FOR_N(c, r, cc, rr)                                                                                            \
    for (int rr = (r) - 1; rr <= (r) + 1; rr++)                                                                        \
        for (int cc = (c) - 1; cc <= (c) + 1; cc++)                                                                    \
            if (inb(cc, rr) && !(cc == (c) && rr == (r)))

static void board_clear(int g)
{
    s_cols = g_cols(g);
    s_rows = g_rows(g);
    s_mines = g_mines(g);
    memset(B, 0, sizeof B);
    memset(s_hole, 0, sizeof s_hole);
    s_ncell = s_cols * s_rows;
    if (RND) {
        // (the round frame: only the tiles with their middle in the circle of the board; the mines keep the density
        // of the narrow board of this garden)
        float h = s_cols * 0.5f, rr = h * h + 0.3f;
        s_ncell = 0;
        for (int r = 0; r < s_rows; r++) {
            for (int c = 0; c < s_cols; c++) {
                float dx = c + 0.5f - h, dy = r + 0.5f - h;
                s_hole[r][c] = dx * dx + dy * dy > rr;
                s_ncell += !s_hole[r][c];
            }
        }
        s_mines = (s_ncell * NAR[g][2] + NAR[g][0] * NAR[g][1] / 2) / (NAR[g][0] * NAR[g][1]);
    }
    for (int r = 0; r < MAXR; r++) {
        for (int c = 0; c < MAXC; c++) {
            B[r][c].var = (uint8_t)(hash2(c * 3 + g, r * 7 + 1) % 12);
            B[r][c].t_open = -1;
            B[r][c].t_flag = -9;
        }
    }
    s_open = s_flags = 0;
    s_first = true;
    s_wave_end = 0;
}

// ---- the solver: K = 0 not known, 1 open (proved safe), 2 a proved mine
static uint8_t K[MAXR][MAXC];
static int16_t k_stack[MAXR * MAXC * 2];
static void k_open(int c0, int r0)
{
    if (K[r0][c0]) {
        return;
    }
    int sp = 0;
    K[r0][c0] = 1;
    k_stack[sp++] = (int16_t)(r0 * MAXC + c0);
    while (sp) {
        int i = k_stack[--sp], r = i / MAXC, c = i % MAXC;
        if (B[r][c].n) {
            continue;
        }
        FOR_N(c, r, cc, rr) if (!K[rr][cc])
        {
            K[rr][cc] = 1;
            k_stack[sp++] = (int16_t)(rr * MAXC + cc);
        }
    }
}
static inline bool adj(int c0, int r0, int c, int r)
{
    return abs(c - c0) <= 1 && abs(r - r0) <= 1 && !(c == c0 && r == r0);
}
static bool solvable(int fc, int fr)
{
    memset(K, 0, sizeof K);
    k_open(fc, fr);
    for (;;) {
        bool ch = false;
        for (int r = 0; r < s_rows; r++) {
            for (int c = 0; c < s_cols; c++) {
                if (K[r][c] != 1 || !B[r][c].n) {
                    continue;
                }
                int u = 0, m = 0;
                FOR_N(c, r, cc, rr)
                {
                    if (!K[rr][cc]) {
                        u++;
                    } else if (K[rr][cc] == 2) {
                        m++;
                    }
                }
                if (!u) {
                    continue;
                }
                if (B[r][c].n == m) {
                    FOR_N(c, r, cc, rr) if (!K[rr][cc]) k_open(cc, rr);
                    ch = true;
                } else if (B[r][c].n - m == u) {
                    FOR_N(c, r, cc, rr) if (!K[rr][cc]) K[rr][cc] = 2;
                    ch = true;
                }
            }
        }
        if (ch) {
            continue;
        }
        // the subset rule: all the unknown cells of A are next to B too; then the other unknown cells of B have the
        // difference of the two counts
        for (int ra = 0; ra < s_rows && !ch; ra++) {
            for (int ca = 0; ca < s_cols && !ch; ca++) {
                if (K[ra][ca] != 1 || !B[ra][ca].n) {
                    continue;
                }
                int ua = 0, ma = 0;
                FOR_N(ca, ra, cc, rr)
                {
                    if (!K[rr][cc]) {
                        ua++;
                    } else if (K[rr][cc] == 2) {
                        ma++;
                    }
                }
                if (!ua) {
                    continue;
                }
                for (int rb = ra - 2; rb <= ra + 2 && !ch; rb++) {
                    for (int cb = ca - 2; cb <= ca + 2 && !ch; cb++) {
                        if (!inb(cb, rb) || (cb == ca && rb == ra) || K[rb][cb] != 1 || !B[rb][cb].n) {
                            continue;
                        }
                        bool sub = true;
                        FOR_N(ca, ra, cc, rr) if (!K[rr][cc] && !adj(cb, rb, cc, rr)) sub = false;
                        if (!sub) {
                            continue;
                        }
                        int mb = 0, d = 0;
                        FOR_N(cb, rb, cc, rr)
                        {
                            if (K[rr][cc] == 2) {
                                mb++;
                            } else if (!K[rr][cc] && !adj(ca, ra, cc, rr)) {
                                d++;
                            }
                        }
                        if (!d) {
                            continue;
                        }
                        int dn = (B[rb][cb].n - mb) - (B[ra][ca].n - ma);
                        if (dn == 0) {
                            FOR_N(cb, rb, cc, rr) if (!K[rr][cc] && !adj(ca, ra, cc, rr)) k_open(cc, rr);
                            ch = true;
                        } else if (dn == d) {
                            FOR_N(cb, rb, cc, rr) if (!K[rr][cc] && !adj(ca, ra, cc, rr)) K[rr][cc] = 2;
                            ch = true;
                        }
                    }
                }
            }
        }
        if (!ch) {
            break;
        }
    }
    for (int r = 0; r < s_rows; r++) {
        for (int c = 0; c < s_cols; c++) {
            if (!B[r][c].mine && K[r][c] != 1 && !s_hole[r][c]) {
                return false;
            }
        }
    }
    return true;
}
static int item_cell(int fc, int fr)
{
    for (int k = 0; k < 400; k++) {
        int c = rndi(0, s_cols - 1), r = rndi(0, s_rows - 1);
        if (B[r][c].mine || B[r][c].item || s_hole[r][c] || (abs(c - fc) <= 1 && abs(r - fr) <= 1)) {
            continue;
        }
        return r * MAXC + c;
    }
    return -1;
}
static void board_mines(int g, int fc, int fr)
{
    for (s_gen_tries = 1;; s_gen_tries++) {
        for (int r = 0; r < s_rows; r++) {
            for (int c = 0; c < s_cols; c++) {
                B[r][c].mine = 0;
                B[r][c].n = 0;
            }
        }
        for (int left = s_mines; left > 0;) {
            int c = rndi(0, s_cols - 1), r = rndi(0, s_rows - 1);
            if (B[r][c].mine || s_hole[r][c] || (abs(c - fc) <= 1 && abs(r - fr) <= 1)) {
                continue;
            }
            B[r][c].mine = 1;
            left--;
        }
        for (int r = 0; r < s_rows; r++) {
            for (int c = 0; c < s_cols; c++) {
                int n = 0;
                FOR_N(c, r, cc, rr) n += B[rr][cc].mine;
                B[r][c].n = (uint8_t)n;
            }
        }
        if (solvable(fc, fr) || s_gen_tries >= 900) {
            break;
        }
    }
    // the surprises: coins, the mole (a safe tile), the clover (a shield), a chest
    int nc = 3 + g / 2;
    for (int k = 0; k < nc + 3; k++) {
        int it = k < nc        ? IT_COIN
                 : k == nc     ? (g >= 1 ? IT_MOLE : 0)
                 : k == nc + 1 ? (g >= 2 ? IT_CLOVER : 0)
                               : (g >= 4 ? IT_CHEST : 0);
        if (!it) {
            continue;
        }
        int i = item_cell(fc, fr);
        if (i >= 0) {
            B[i / MAXC][i % MAXC].item = (uint8_t)it;
        }
    }
}

// ---- the dig, the wave, the flag
static int16_t s_q[MAXR * MAXC], s_qd[MAXR * MAXC];
static int reveal(int c0, int r0, float t0)
{
    if (B[r0][c0].st != CS_COV) {
        return 0;
    }
    int qh = 0, qt = 0, n = 0;
    B[r0][c0].st = CS_OPEN;
    B[r0][c0].t_open = t0;
    s_open++;
    n++;
    s_q[qt] = (int16_t)(r0 * MAXC + c0);
    s_qd[qt++] = 0;
    while (qh < qt) {
        int i = s_q[qh], d = s_qd[qh++], r = i / MAXC, c = i % MAXC;
        if (B[r][c].n || B[r][c].mine) {
            continue;
        }
        FOR_N(c, r, cc, rr) if (B[rr][cc].st == CS_COV && !B[rr][cc].mine)
        {
            B[rr][cc].st = CS_OPEN;
            B[rr][cc].t_open = t0 + (d + 1) * 0.035f;
            s_open++;
            n++;
            s_q[qt] = (int16_t)(rr * MAXC + cc);
            s_qd[qt++] = (int16_t)(d + 1);
        }
    }
    for (int k = 0; k < qt; k++) {
        int i = s_q[k];
        B[i / MAXC][i % MAXC].hint = 0;
        if (B[i / MAXC][i % MAXC].t_open > s_wave_end) {
            s_wave_end = B[i / MAXC][i % MAXC].t_open;
        }
    }
    return n;
}
enum {
    DG_NONE,
    DG_OK,
    DG_MINE,
    DG_FLAGGED,
    DG_NOTYET
};
static int s_boom_c, s_boom_r;
static int dig(int g, int c, int r)
{
    if (!inb(c, r)) {
        return DG_NONE;
    }
    if (s_first) {
        board_mines(g, c, r);
        s_first = false;
    }
    cell_t *e = &B[r][c];
    if (e->st == CS_FLAG) {
        return DG_FLAGGED;
    }
    if (e->st == CS_OPEN) {
        if (!e->n) {
            return DG_NONE;
        }
        int f = 0, cov = 0;
        FOR_N(c, r, cc, rr)
        {
            f += B[rr][cc].st == CS_FLAG;
            cov += B[rr][cc].st == CS_COV;
        }
        if (!cov) {
            return DG_NONE;
        }
        if (f != e->n) {
            return DG_NOTYET;
        }
        FOR_N(c, r, cc, rr) if (B[rr][cc].st == CS_COV && B[rr][cc].mine)
        {
            s_boom_c = cc;
            s_boom_r = rr;
            return DG_MINE;
        }
        FOR_N(c, r, cc, rr) if (B[rr][cc].st == CS_COV) reveal(cc, rr, s_now);
        return DG_OK;
    }
    if (e->mine) {
        s_boom_c = c;
        s_boom_r = r;
        return DG_MINE;
    }
    reveal(c, r, s_now);
    return DG_OK;
}
// a dig would clear the neighbours of a number (the DIG button shows it)
static bool can_chord(int c, int r)
{
    if (!inb(c, r) || B[r][c].st != CS_OPEN || !B[r][c].n) {
        return false;
    }
    int f = 0, cov = 0;
    FOR_N(c, r, cc, rr)
    {
        f += B[rr][cc].st == CS_FLAG;
        cov += B[rr][cc].st == CS_COV;
    }
    return cov && f == B[r][c].n;
}
static int flag_toggle(int c, int r)
{
    if (!inb(c, r)) {
        return 0;
    }
    cell_t *e = &B[r][c];
    if (e->st == CS_COV) {
        e->st = CS_FLAG;
        s_flags++;
        e->t_flag = s_now;
        return 1;
    }
    if (e->st == CS_FLAG) {
        e->st = CS_COV;
        s_flags--;
        e->t_flag = s_now;
        return -1;
    }
    return 0;
}
static bool board_won(void)
{
    return !s_first && s_open == s_ncell - s_mines;
}
// a safe tile for a hint (next to an open tile when there is one; not a flag, not a hint already)
static int hint_pick(void)
{
    int best = -1, nb = 0;
    for (int pass = 0; pass < 2 && best < 0; pass++) {
        for (int r = 0; r < s_rows; r++) {
            for (int c = 0; c < s_cols; c++) {
                cell_t *e = &B[r][c];
                if (e->mine || e->st != CS_COV || e->hint || s_hole[r][c]) {
                    continue;
                }
                bool front = false;
                FOR_N(c, r, cc, rr) if (B[rr][cc].st == CS_OPEN) front = true;
                if (pass == 0 && !front) {
                    continue;
                }
                if (rndi(0, nb++) == 0) {
                    best = r * MAXC + c;
                }
            }
        }
    }
    return best;
}
