// CHESS (app_chess.c): the rules and the computer. A 0x88 board (sq = rank * 16 + file; rank 0 = the first rank of
// white), the pieces +1..+6 white (pawn, knight, bishop, rook, queen, king), -1..-6 black. The moves of a position are
// made with all the rules (castling, en passant, the promotions); a move that leaves the own king in check is not legal.
// The computer: negamax with alpha-beta, iterative deepening to a time or a depth, a captures-only quiescence search,
// the order: the best move of the last depth, captures (the most valuable victim by the least valuable attacker),
// two killer moves a ply; the value: the material and the tables of the squares (Michniewski's simplified values), a
// king table for the endgame.
enum {
    EP_ = 0,
    PAWN = 1,
    KNIGHT,
    BISHOP,
    ROOK,
    QUEEN,
    KING
};
#define WHITE  1
#define BLACK  (-1)
#define NOSQ   (-1)
#define MAXPLY 40
#define MAXMV  160
#define MATE   30000
#define INF    32000
typedef struct {
    uint8_t f, t, promo, flags;
} cmove_t;  // flags: 1 capture, 2 en passant, 4 castle, 8 a double push
typedef struct {
    int8_t cap;
    uint8_t castle;
    int8_t ep;
    uint8_t half;
    int8_t ksq_w, ksq_b;
} cundo_t;
typedef struct {
    int8_t b[128];
    int side;        // WHITE / BLACK to move
    uint8_t castle;  // 1 white short, 2 white long, 4 black short, 8 black long
    int8_t ep;       // the square behind a pawn that moved two (or NOSQ)
    uint8_t half;    // moves since a capture or a pawn move (50 moves: 100)
    int full;        // the number of the move
    int8_t ksq[2];   // the kings: [0] white, [1] black
} cpos_t;
static inline int kidx(int side)
{
    return side == WHITE ? 0 : 1;
}
static const int8_t NDIR[8] = {33, 31, 18, 14, -33, -31, -18, -14};
static const int8_t KDIR[8] = {1, -1, 16, -16, 17, 15, -17, -15};
static uint8_t castle_mask(int sq)
{
    switch (sq) {
    case 0x04:
        return 3;
    case 0x00:
        return 2;
    case 0x07:
        return 1;
    case 0x74:
        return 12;
    case 0x70:
        return 8;
    case 0x77:
        return 4;
    default:
        return 0;
    }
}
static void pos_start(cpos_t *p)
{
    static const int8_t BACK[8] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
    memset(p, 0, sizeof *p);
    for (int f = 0; f < 8; f++) {
        p->b[f] = BACK[f];
        p->b[16 + f] = PAWN;
        p->b[0x60 + f] = -PAWN;
        p->b[0x70 + f] = (int8_t)-BACK[f];
    }
    p->side = WHITE;
    p->castle = 15;
    p->ep = NOSQ;
    p->half = 0;
    p->full = 1;
    p->ksq[0] = 0x04;
    p->ksq[1] = 0x74;
}
// a FEN (the tests)
static bool pos_fen(cpos_t *p, const char *s)
{
    memset(p, 0, sizeof *p);
    p->ep = NOSQ;
    p->full = 1;
    int r = 7, f = 0;
    for (; *s && *s != ' '; s++) {
        if (*s == '/') {
            r--;
            f = 0;
            continue;
        }
        if (*s >= '1' && *s <= '8') {
            f += *s - '0';
            continue;
        }
        const char *pc = strchr("pnbrqk", *s >= 'a' ? *s : *s + 32);
        if (!pc || r < 0 || f > 7) {
            return false;
        }
        const int v = (int)(pc - "pnbrqk") + 1;
        p->b[r * 16 + f] = (int8_t)(*s >= 'a' ? -v : v);
        if (v == KING) {
            p->ksq[*s >= 'a' ? 1 : 0] = (int8_t)(r * 16 + f);
        }
        f++;
    }
    if (*s) {
        s++;
    }
    p->side = *s == 'b' ? BLACK : WHITE;
    while (*s && *s != ' ') {
        s++;
    }
    if (*s) {
        s++;
    }
    for (; *s && *s != ' '; s++) {
        p->castle |= *s == 'K' ? 1 : *s == 'Q' ? 2 : *s == 'k' ? 4 : *s == 'q' ? 8 : 0;
    }
    if (*s) {
        s++;
    }
    if (*s >= 'a' && *s <= 'h') {
        p->ep = (int8_t)((s[1] - '1') * 16 + (s[0] - 'a'));
        s += 2;
    }
    return true;
}
// is sq attacked by the side "by"?
static bool attacked(const cpos_t *p, int sq, int by)
{
    // pawns
    const int pd = by == WHITE ? -16 : 16;
    for (int k = -1; k <= 1; k += 2) {
        const int s = sq + pd + k;
        if (!(s & 0x88) && p->b[s] == by * PAWN) {
            return true;
        }
    }
    for (int i = 0; i < 8; i++) {
        const int s = sq + NDIR[i];
        if (!(s & 0x88) && p->b[s] == by * KNIGHT) {
            return true;
        }
    }
    for (int i = 0; i < 8; i++) {
        const int s = sq + KDIR[i];
        if (!(s & 0x88) && p->b[s] == by * KING) {
            return true;
        }
    }
    for (int i = 0; i < 8; i++) {
        const int d = KDIR[i], diag = i >= 4;
        for (int s = sq + d; !(s & 0x88); s += d) {
            const int v = p->b[s];
            if (!v) {
                continue;
            }
            if (v == by * QUEEN || v == by * (diag ? BISHOP : ROOK)) {
                return true;
            }
            break;
        }
    }
    return false;
}
static inline bool in_check(const cpos_t *p, int side)
{
    return attacked(p, p->ksq[kidx(side)], -side);
}
static int gen_moves(const cpos_t *p, cmove_t *m, bool caps_only)
{
    int n = 0;
    const int us = p->side;
#define ADD(F, T, P, FL)                                                                                               \
    do {                                                                                                               \
        if (n < MAXMV)                                                                                                 \
            m[n++] = (cmove_t){(uint8_t)(F), (uint8_t)(T), (uint8_t)(P), (uint8_t)(FL)};                               \
    } while (0)
    for (int sq = 0; sq < 128; sq++) {
        if (sq & 0x88) {
            sq += 7;
            continue;
        }
        const int v = p->b[sq] * us;
        if (v <= 0) {
            continue;
        }
        if (v == PAWN) {
            const int fwd = us == WHITE ? 16 : -16, r = sq >> 4, last = us == WHITE ? 7 : 0,
                      start = us == WHITE ? 1 : 6;
            const int t = sq + fwd;
            if (!(t & 0x88) && !p->b[t]) {
                if ((t >> 4) == last) {
                    for (int pr = QUEEN; pr >= KNIGHT; pr--) {
                        ADD(sq, t, pr, 0);
                    }
                } else if (!caps_only) {
                    ADD(sq, t, 0, 0);
                    if (r == start && !p->b[t + fwd]) {
                        ADD(sq, t + fwd, 0, 8);
                    }
                }
            }
            for (int k = -1; k <= 1; k += 2) {
                const int c = t + k;
                if (c & 0x88) {
                    continue;
                }
                if (p->b[c] * us < 0) {
                    if ((c >> 4) == last) {
                        for (int pr = QUEEN; pr >= KNIGHT; pr--) {
                            ADD(sq, c, pr, 1);
                        }
                    } else {
                        ADD(sq, c, 0, 1);
                    }
                } else if (c == p->ep) {
                    ADD(sq, c, 0, 3);
                }
            }
        } else if (v == KNIGHT || v == KING) {
            const int8_t *D = v == KNIGHT ? NDIR : KDIR;
            for (int i = 0; i < 8; i++) {
                const int t = sq + D[i];
                if (t & 0x88) {
                    continue;
                }
                const int w = p->b[t] * us;
                if (w > 0) {
                    continue;
                }
                if (w < 0) {
                    ADD(sq, t, 0, 1);
                } else if (!caps_only) {
                    ADD(sq, t, 0, 0);
                }
            }
            if (v == KING && !caps_only) {
                // castling: the squares between empty, the king not in check and not through an attacked square
                const int home = us == WHITE ? 0x04 : 0x74, sh = us == WHITE ? 1 : 4, lo = us == WHITE ? 2 : 8;
                if (sq == home && (p->castle & (sh | lo)) && !attacked(p, home, -us)) {
                    if ((p->castle & sh) && !p->b[home + 1] && !p->b[home + 2] && p->b[home + 3] == us * ROOK &&
                        !attacked(p, home + 1, -us) && !attacked(p, home + 2, -us)) {
                        ADD(home, home + 2, 0, 4);
                    }
                    if ((p->castle & lo) && !p->b[home - 1] && !p->b[home - 2] && !p->b[home - 3] &&
                        p->b[home - 4] == us * ROOK && !attacked(p, home - 1, -us) && !attacked(p, home - 2, -us)) {
                        ADD(home, home - 2, 0, 4);
                    }
                }
            }
        } else {
            const int i0 = v == BISHOP ? 4 : 0, i1 = v == ROOK ? 4 : 8;
            for (int i = i0; i < i1; i++) {
                const int d = KDIR[i];
                for (int t = sq + d; !(t & 0x88); t += d) {
                    const int w = p->b[t] * us;
                    if (w > 0) {
                        break;
                    }
                    if (w < 0) {
                        ADD(sq, t, 0, 1);
                        break;
                    }
                    if (!caps_only) {
                        ADD(sq, t, 0, 0);
                    }
                }
            }
        }
    }
#undef ADD
    return n;
}
static void make_move(cpos_t *p, cmove_t m, cundo_t *u)
{
    const int us = p->side, f = m.f, t = m.t;
    u->cap = p->b[t];
    u->castle = p->castle;
    u->ep = p->ep;
    u->half = p->half;
    u->ksq_w = p->ksq[0];
    u->ksq_b = p->ksq[1];
    int8_t pc = p->b[f];
    p->half++;
    if (pc * us == PAWN || u->cap) {
        p->half = 0;
    }
    if (m.flags & 2) {
        const int cs = t + (us == WHITE ? -16 : 16);
        u->cap = p->b[cs];
        p->b[cs] = 0;
    }  // (en passant)
    if (m.promo) {
        pc = (int8_t)(us * m.promo);
    }
    p->b[t] = pc;
    p->b[f] = 0;
    if (m.flags & 4) {  // (the rook of a castling)
        if (t > f) {
            p->b[t - 1] = p->b[t + 1];
            p->b[t + 1] = 0;
        } else {
            p->b[t + 1] = p->b[t - 2];
            p->b[t - 2] = 0;
        }
    }
    if (pc * us == KING) {
        p->ksq[kidx(us)] = (int8_t)t;
    }
    p->castle &= (uint8_t)~(castle_mask(f) | castle_mask(t));
    p->ep = (m.flags & 8) ? (int8_t)((f + t) / 2) : NOSQ;
    if (us == BLACK) {
        p->full++;
    }
    p->side = -us;
}
static void unmake_move(cpos_t *p, cmove_t m, const cundo_t *u)
{
    p->side = -p->side;
    const int us = p->side, f = m.f, t = m.t;
    if (us == BLACK) {
        p->full--;
    }
    int8_t pc = p->b[t];
    if (m.promo) {
        pc = (int8_t)(us * PAWN);
    }
    p->b[f] = pc;
    if (m.flags & 2) {
        p->b[t] = 0;
        p->b[t + (us == WHITE ? -16 : 16)] = u->cap;
    } else {
        p->b[t] = u->cap;
    }
    if (m.flags & 4) {
        if (t > f) {
            p->b[t + 1] = p->b[t - 1];
            p->b[t - 1] = 0;
        } else {
            p->b[t - 2] = p->b[t + 1];
            p->b[t + 1] = 0;
        }
    }
    p->castle = u->castle;
    p->ep = u->ep;
    p->half = u->half;
    p->ksq[0] = u->ksq_w;
    p->ksq[1] = u->ksq_b;
}
// the legal moves (the UI, the end of a game)
static int legal_moves(cpos_t *p, cmove_t *out)
{
    cmove_t m[MAXMV];
    const int n = gen_moves(p, m, false);
    int k = 0;
    for (int i = 0; i < n; i++) {
        cundo_t u;
        make_move(p, m[i], &u);
        if (!in_check(p, -p->side)) {
            out[k++] = m[i];
        }
        unmake_move(p, m[i], &u);
    }
    return k;
}
static uint64_t perft(cpos_t *p, int d)
{
    cmove_t m[MAXMV];
    const int n = gen_moves(p, m, false);
    uint64_t c = 0;
    for (int i = 0; i < n; i++) {
        cundo_t u;
        make_move(p, m[i], &u);
        if (!in_check(p, -p->side)) {
            c += d <= 1 ? 1 : perft(p, d - 1);
        }
        unmake_move(p, m[i], &u);
    }
    return c;
}

// ---------------------------------------------------------------- the value of a position (for the side to move)
static const int16_t PVAL[7] = {0, 100, 320, 330, 500, 900, 0};
// the tables (from white's side; the first row = the eighth rank)
static const int8_t PST[7][64] = {
        {0},
        {0,  0,   0,  0, 0,  0,  0,  0,   50,  50, 50, 50, 50, 50, 50, 50, 10, 10, 20, 30, 30,  20,
         10, 10,  5,  5, 10, 25, 25, 10,  5,   5,  0,  0,  0,  20, 20, 0,  0,  0,  5,  -5, -10, 0,
         0,  -10, -5, 5, 5,  10, 10, -20, -20, 10, 10, 5,  0,  0,  0,  0,  0,  0,  0,  0},
        {-50, -40, -30, -30, -30, -30, -40, -50, -40, -20, 0,   0,   0,   0,   -20, -40, -30, 0,   10,  15, 15, 10,
         0,   -30, -30, 5,   15,  20,  20,  15,  5,   -30, -30, 0,   15,  20,  20,  15,  0,   -30, -30, 5,  10, 15,
         15,  10,  5,   -30, -40, -20, 0,   5,   5,   0,   -20, -40, -50, -40, -30, -30, -30, -30, -40, -50},
        {-20, -10, -10, -10, -10, -10, -10, -20, -10, 0,   0,   0,   0,   0,   0,   -10, -10, 0,   5,   10, 10, 5,
         0,   -10, -10, 5,   5,   10,  10,  5,   5,   -10, -10, 0,   10,  10,  10,  10,  0,   -10, -10, 10, 10, 10,
         10,  10,  10,  -10, -10, 5,   0,   0,   0,   0,   5,   -10, -20, -10, -10, -10, -10, -10, -10, -20},
        {0,  0, 0, 0, 0, 0, 0, 0,  5,  10, 10, 10, 10, 10, 10, 5,  -5, 0, 0, 0, 0, 0, 0, -5, -5, 0, 0, 0, 0, 0, 0, -5,
         -5, 0, 0, 0, 0, 0, 0, -5, -5, 0,  0,  0,  0,  0,  0,  -5, -5, 0, 0, 0, 0, 0, 0, -5, 0,  0, 0, 5, 5, 0, 0, 0},
        {-20, -10, -10, -5,  -5,  -10, -10, -20, -10, 0,  0, 0,   0,   0,   0,   -10, -10, 0,   5,   5,  5, 5,
         0,   -10, -5,  0,   5,   5,   5,   5,   0,   -5, 0, 0,   5,   5,   5,   5,   0,   -5,  -10, 5,  5, 5,
         5,   5,   0,   -10, -10, 0,   5,   0,   0,   0,  0, -10, -20, -10, -10, -5,  -5,  -10, -10, -20},
        {-30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -30, -40, -40, -50, -50, -40,
         -40, -30, -30, -40, -40, -50, -50, -40, -40, -30, -20, -30, -30, -40, -40, -30, -30, -20, -10, -20, -20, -20,
         -20, -20, -20, -10, 20,  20,  0,   0,   0,   0,   20,  20,  20,  30,  10,  0,   0,   10,  30,  20},
};
static const int8_t KING_END[64] = {-50, -40, -30, -20, -20, -30, -40, -50, -30, -20, -10, 0,   0,   -10, -20, -30,
                                    -30, -10, 20,  30,  30,  20,  -10, -30, -30, -10, 30,  40,  40,  30,  -10, -30,
                                    -30, -10, 30,  40,  40,  30,  -10, -30, -30, -10, 20,  30,  30,  20,  -10, -30,
                                    -30, -30, 0,   0,   0,   0,   -30, -30, -50, -30, -30, -30, -30, -30, -30, -50};
static int evaluate(const cpos_t *p)
{
    int s = 0, nonpawn[2] = {0, 0};
    for (int sq = 0; sq < 128; sq++) {
        if (sq & 0x88) {
            sq += 7;
            continue;
        }
        const int v = p->b[sq];
        if (!v || v == KING || v == -KING) {
            continue;
        }
        if (v != PAWN && v != -PAWN) {
            nonpawn[v > 0 ? 0 : 1] += PVAL[v > 0 ? v : -v];
        }
    }
    const bool end = nonpawn[0] <= 1300 && nonpawn[1] <= 1300;
    for (int sq = 0; sq < 128; sq++) {
        if (sq & 0x88) {
            sq += 7;
            continue;
        }
        const int v = p->b[sq];
        if (!v) {
            continue;
        }
        const int a = v > 0 ? v : -v, r = sq >> 4, f = sq & 7;
        const int idx = v > 0 ? (7 - r) * 8 + f : r * 8 + f;  // (the table from the side of the piece)
        const int val = PVAL[a] + (a == KING && end ? KING_END[idx] : PST[a][idx]);
        s += v > 0 ? val : -val;
    }
    return s * p->side;
}

// ---------------------------------------------------------------- the search
static struct {
    cpos_t p;
    int64_t t_end;       // (ms) the time limit
    volatile bool stop;  // (the screen sets it: NEW, the end of the app)
    uint32_t nodes;
    cmove_t killer[MAXPLY][2];
    cmove_t best, root_best;
    int noise;  // (EASY: a random value at the root)
    uint32_t rnd;
} S;
static cmove_t s_mv[MAXPLY + 1][MAXMV] EXT_RAM_BSS_ATTR;  // (the move lists of each ply)
static int64_t search_now_ms(void);
static inline bool mv_eq(cmove_t a, cmove_t b)
{
    return a.f == b.f && a.t == b.t && a.promo == b.promo;
}
static int order_score(const cpos_t *p, cmove_t m, int ply, cmove_t hint)
{
    if (mv_eq(m, hint)) {
        return 100000;
    }
    if (m.flags & 1) {
        const int vic = m.flags & 2 ? PAWN : abs(p->b[m.t]), att = abs(p->b[m.f]);
        return 10000 + PVAL[vic] * 10 - PVAL[att] / 10;
    }
    if (m.promo) {
        return 9000 + PVAL[m.promo];
    }
    if (ply < MAXPLY && (mv_eq(m, S.killer[ply][0]) || mv_eq(m, S.killer[ply][1]))) {
        return 8000;
    }
    return 0;
}
static void pick(cmove_t *m, int *sc, int n, int i)
{
    int b = i;
    for (int j = i + 1; j < n; j++) {
        if (sc[j] > sc[b]) {
            b = j;
        }
    }
    if (b != i) {
        const cmove_t t = m[i];
        m[i] = m[b];
        m[b] = t;
        const int ts = sc[i];
        sc[i] = sc[b];
        sc[b] = ts;
    }
}
static int quiesce(int alpha, int beta, int ply)
{
    S.nodes++;
    if ((S.nodes & 1023) == 0 && search_now_ms() > S.t_end) {
        S.stop = true;
    }
    if (S.stop) {
        return 0;
    }
    const int stand = evaluate(&S.p);
    if (stand >= beta) {
        return beta;
    }
    if (stand > alpha) {
        alpha = stand;
    }
    if (ply >= MAXPLY) {
        return stand;
    }
    cmove_t *m = s_mv[ply];
    const int n = gen_moves(&S.p, m, true);
    int sc[MAXMV];
    for (int i = 0; i < n; i++) {
        sc[i] = order_score(&S.p, m[i], ply, (cmove_t){0});
    }
    for (int i = 0; i < n; i++) {
        pick(m, sc, n, i);
        cundo_t u;
        make_move(&S.p, m[i], &u);
        if (in_check(&S.p, -S.p.side)) {
            unmake_move(&S.p, m[i], &u);
            continue;
        }
        const int v = -quiesce(-beta, -alpha, ply + 1);
        unmake_move(&S.p, m[i], &u);
        if (S.stop) {
            return 0;
        }
        if (v >= beta) {
            return beta;
        }
        if (v > alpha) {
            alpha = v;
        }
    }
    return alpha;
}
static int search(int depth, int alpha, int beta, int ply)
{
    S.nodes++;
    if ((S.nodes & 1023) == 0 && search_now_ms() > S.t_end) {
        S.stop = true;
    }
    if (S.stop) {
        return 0;
    }
    const bool chk = in_check(&S.p, S.p.side);
    if (chk && ply < MAXPLY - 6) {
        depth++;  // (a check: one ply more)
    }
    if (depth <= 0 || ply >= MAXPLY - 1) {
        return quiesce(alpha, beta, ply);
    }
    if (ply > 0 && S.p.half >= 100) {
        return 0;
    }
    cmove_t *m = s_mv[ply];
    const int n = gen_moves(&S.p, m, false);
    int sc[MAXMV];
    for (int i = 0; i < n; i++) {
        sc[i] = order_score(&S.p, m[i], ply, ply == 0 ? S.root_best : (cmove_t){0});
    }
    int legal = 0, best = -INF;
    for (int i = 0; i < n; i++) {
        pick(m, sc, n, i);
        cundo_t u;
        make_move(&S.p, m[i], &u);
        if (in_check(&S.p, -S.p.side)) {
            unmake_move(&S.p, m[i], &u);
            continue;
        }
        legal++;
        // (the root with noise: a window lower by 2 x noise, so each move that the noise can make the best has its exact value)
        const int a2 = ply == 0 && S.noise && alpha > -INF + 2 * S.noise ? alpha - 2 * S.noise : alpha;
        int v = -search(depth - 1, -beta, -a2, ply + 1);
        unmake_move(&S.p, m[i], &u);
        if (S.stop) {
            return 0;
        }
        if (ply == 0 && S.noise) {
            S.rnd = S.rnd * 1664525u + 1013904223u;
            v += (int)(S.rnd >> 16) % (2 * S.noise + 1) - S.noise;
        }
        if (v > best) {
            best = v;
            if (ply == 0) {
                S.best = m[i];
            }
        }
        if (v > alpha) {
            alpha = v;
            if (alpha >= beta) {
                if (!(m[i].flags & 1) && ply < MAXPLY) {
                    S.killer[ply][1] = S.killer[ply][0];
                    S.killer[ply][0] = m[i];
                }
                break;
            }
        }
    }
    if (!legal) {
        return chk ? -MATE + ply : 0;
    }
    return best;
}
// the best move of a position: iterative deepening to max_depth or the time (ms); false: no legal move
static bool think(const cpos_t *p, int max_depth, int ms, int noise, uint32_t seed, cmove_t *out, int *depth_done,
                  uint32_t *nodes)
{
    S.p = *p;
    S.stop = false;
    S.nodes = 0;
    S.noise = noise;
    S.rnd = seed;
    memset(S.killer, 0, sizeof S.killer);
    S.root_best = (cmove_t){0};
    S.t_end = search_now_ms() + ms;
    cmove_t lm[MAXMV];
    cpos_t q = *p;
    if (!legal_moves(&q, lm)) {
        return false;
    }
    *out = lm[0];
    *depth_done = 0;
    for (int d = 1; d <= max_depth; d++) {
        S.best = (cmove_t){0};
        const int v = search(d, -INF, INF, 0);
        if (S.stop && d > 1) {
            break;
        }
        if (S.best.f || S.best.t) {
            *out = S.best;
            S.root_best = S.best;
            *depth_done = d;
        }
        if (v > MATE - 100 || v < -MATE + 100) {
            break;  // (a mate is found)
        }
    }
    *nodes = S.nodes;
    return true;
}
