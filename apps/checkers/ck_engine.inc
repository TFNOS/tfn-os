// CHECKERS: the rules and the computer (included by app_checkers.c; the PC tests include it alone).
// The board: 64 squares (row 0 at the top), the pieces only on the dark squares ((row + col) odd). A piece: +1 a light
// man, +2 a light king, -1 a dark man, -2 a dark king. Light moves up (to row 0), dark moves down; light starts.
// THE RULES: ISRAELI (the default; as "damka" in Israel): a man moves one step forward, it captures forward and back; a
// king moves and captures over any distance on a diagonal (it lands on any free square after the piece). ENGLISH: a
// man captures forward only, a king moves and captures one step. Both: a capture is a must, a man or a king goes on
// while it can capture (it cannot take one piece twice; the taken pieces leave at the end of the move), a man that ends
// its move on the last row becomes a king. No legal move = the game is lost. 50 plies with no capture and no move of a
// man = a draw.
#include <stdint.h>
#include <string.h>

#define CK_MAXM 64  // the most moves of one position
#define CK_PATH 12  // the most captures of one move
typedef struct {
    int8_t b[64];
    int8_t side;
    uint8_t quiet;
} ckpos_t;
typedef struct {
    uint8_t from, to, ncap, promo;
    uint8_t path[CK_PATH + 1], caps[CK_PATH];
} ckmove_t;  // path: the landings

static bool ck_israeli = true;  // the rules: Israeli (true) or English
static inline bool ck_dark(int sq)
{
    return ((sq >> 3) + (sq & 7)) & 1;
}
static inline bool ck_on(int r, int c)
{
    return r >= 0 && r < 8 && c >= 0 && c < 8;
}

static void ck_start(ckpos_t *p)
{
    memset(p, 0, sizeof *p);
    for (int sq = 0; sq < 64; sq++) {
        if (!ck_dark(sq)) {
            continue;
        }
        const int r = sq >> 3;
        if (r < 3) {
            p->b[sq] = -1;
        } else if (r > 4) {
            p->b[sq] = 1;
        }
    }
    p->side = 1;
}

static const int8_t DR[4] = {-1, -1, 1, 1}, DC[4] = {-1, 1, -1, 1};

// the capture moves of the piece at sq (DFS); the moves end where no capture follows
typedef struct {
    ckmove_t *out;
    int n, max;
} ckgen_t;
static void ck_caps(const ckpos_t *p, ckgen_t *G, ckmove_t *cur, int sq, int piece, uint64_t taken)
{
    const int r = sq >> 3, c = sq & 7, s = piece > 0 ? 1 : -1;
    const bool king = piece == 2 || piece == -2;
    bool any = false;
    for (int d = 0; d < 4; d++) {
        if (!king && !ck_israeli && DR[d] != (s > 0 ? -1 : 1)) {
            continue;  // (English men: forward only)
        }
        int rr = r + DR[d], cc = c + DC[d];
        // (the square where the piece started is empty now: the piece left it)
        if (king && ck_israeli) {
            while (ck_on(rr, cc) && (p->b[rr * 8 + cc] == 0 || rr * 8 + cc == cur->from)) {
                rr += DR[d];
                cc += DC[d];
            }
        }
        if (!ck_on(rr, cc)) {
            continue;
        }
        const int over = rr * 8 + cc;
        if (over == cur->from || p->b[over] * s >= 0 || (taken >> over & 1)) {
            continue;  // (an enemy piece, not taken yet)
        }
        int lr = rr + DR[d], lc = cc + DC[d];
        while (ck_on(lr, lc) && (p->b[lr * 8 + lc] == 0 || lr * 8 + lc == cur->from)) {
            const int land = lr * 8 + lc;
            if (cur->ncap < CK_PATH) {
                ckmove_t m = *cur;
                m.caps[m.ncap] = (uint8_t)over;
                m.path[m.ncap] = (uint8_t)land;
                m.ncap++;
                m.to = (uint8_t)land;
                const int before = G->n;
                ck_caps(p, G, &m, land, piece, taken | (1ull << over));
                (void)before;
                any = true;
            }
            if (!(king && ck_israeli)) {
                break;  // (a man and an English king land just after the piece)
            }
            lr += DR[d];
            lc += DC[d];
        }
    }
    if (!any && cur->ncap > 0 && G->n < G->max) {
        ckmove_t m = *cur;
        const int last = s > 0 ? 0 : 7;
        m.promo = !king && (m.to >> 3) == last;
        G->out[G->n++] = m;
    }
}
static int ck_gen(const ckpos_t *p, ckmove_t *out, int max)
{
    ckgen_t G = {out, 0, max};
    const int s = p->side;
    for (int sq = 0; sq < 64; sq++) {
        const int pc = p->b[sq];
        if (pc * s <= 0) {
            continue;
        }
        ckmove_t m;
        memset(&m, 0, sizeof m);
        m.from = (uint8_t)sq;
        ck_caps(p, &G, &m, sq, pc, 0);
    }
    if (G.n) {
        return G.n;  // (a capture is a must)
    }
    for (int sq = 0; sq < 64 && G.n < max; sq++) {
        const int pc = p->b[sq];
        if (pc * s <= 0) {
            continue;
        }
        const bool king = pc == 2 || pc == -2;
        const int r = sq >> 3, c = sq & 7;
        for (int d = 0; d < 4; d++) {
            if (!king && DR[d] != (s > 0 ? -1 : 1)) {
                continue;
            }
            int rr = r + DR[d], cc = c + DC[d];
            while (ck_on(rr, cc) && p->b[rr * 8 + cc] == 0 && G.n < max) {
                ckmove_t m;
                memset(&m, 0, sizeof m);
                m.from = (uint8_t)sq;
                m.to = (uint8_t)(rr * 8 + cc);
                m.path[0] = m.to;
                m.promo = !king && rr == (s > 0 ? 0 : 7);
                out[G.n++] = m;
                if (!(king && ck_israeli)) {
                    break;
                }
                rr += DR[d];
                cc += DC[d];
            }
        }
    }
    return G.n;
}
static void ck_make(ckpos_t *p, const ckmove_t *m)
{
    int pc = p->b[m->from];
    const bool man = pc == 1 || pc == -1;
    p->b[m->from] = 0;
    for (int i = 0; i < m->ncap; i++) {
        p->b[m->caps[i]] = 0;
    }
    if (m->promo) {
        pc *= 2;
    }
    p->b[m->to] = (int8_t)pc;
    p->quiet = (m->ncap || man) ? 0 : (uint8_t)(p->quiet + 1);
    p->side = (int8_t)-p->side;
}
static bool ck_same(const ckmove_t *a, const ckmove_t *b)
{
    return a->from == b->from && a->to == b->to && a->ncap == b->ncap && !memcmp(a->caps, b->caps, a->ncap);
}
// the end: 1 = the side to move lost (no move), 2 = a draw (the quiet rule), 0 = the game goes on
static int ck_over(const ckpos_t *p)
{
    ckmove_t m[CK_MAXM];
    if (!ck_gen(p, m, CK_MAXM)) {
        return 1;
    }
    return p->quiet >= 50 ? 2 : 0;
}

// ---------------------------------------------------------------- the computer
static int ck_eval(const ckpos_t *p)
{
    int v = 0, nl = 0, nd = 0;
    for (int sq = 0; sq < 64; sq++) {
        const int pc = p->b[sq];
        if (!pc) {
            continue;
        }
        const int r = sq >> 3, c = sq & 7, s = pc > 0 ? 1 : -1;
        int w;
        if (pc == 2 || pc == -2) {
            w = ck_israeli ? 330 : 250;
        } else {
            const int adv = s > 0 ? 7 - r : r;                         // (the rows that the man went forward)
            w = 100 + adv * 4 + ((s > 0 ? r == 7 : r == 0) ? 12 : 0);  // (the back row guards the crown)
        }
        if (c >= 2 && c <= 5 && r >= 2 && r <= 5) {
            w += 6;  // (the centre)
        }
        if (c == 0 || c == 7) {
            w -= 4;  // (the edge)
        }
        v += s * w;
        if (s > 0) {
            nl++;
        } else {
            nd++;
        }
    }
    // (ahead in material: trade down)
    if (nl != nd) {
        v += (v > 0 ? 1 : -1) * (24 - nl - nd) * 3;
    }
    return v * p->side;
}
typedef struct {
    int64_t stop_ms;
    uint32_t nodes;
    bool out;
    int noise;
    uint32_t seed;
} ckthink_t;
static int64_t ck_now_ms(void);
static int ck_search(const ckpos_t *p, int depth, int alpha, int beta, ckthink_t *T, int ply)
{
    if ((++T->nodes & 1023) == 0 && ck_now_ms() > T->stop_ms) {
        T->out = true;
    }
    if (T->out) {
        return 0;
    }
    ckmove_t m[CK_MAXM];
    const int n = ck_gen(p, m, CK_MAXM);
    if (!n) {
        return -100000 + ply;
    }
    if (p->quiet >= 50) {
        return 0;
    }
    const bool cap = m[0].ncap > 0;
    if (depth <= 0 && !cap) {
        return ck_eval(p);
    }
    if (depth <= -6) {
        return ck_eval(p);  // (the end of the chase of the captures)
    }
    // (the order: the most captures first, then a promotion)
    for (int i = 1; i < n; i++) {
        ckmove_t t = m[i];
        int k = i;
        while (k > 0 && (m[k - 1].ncap * 4 + m[k - 1].promo) < (t.ncap * 4 + t.promo)) {
            m[k] = m[k - 1];
            k--;
        }
        m[k] = t;
    }
    int best = -1000000;
    for (int i = 0; i < n; i++) {
        ckpos_t q = *p;
        ck_make(&q, &m[i]);
        const int v = -ck_search(&q, depth - 1, -beta, -alpha, T, ply + 1);
        if (T->out) {
            return 0;
        }
        if (v > best) {
            best = v;
        }
        if (v > alpha) {
            alpha = v;
        }
        if (alpha >= beta) {
            break;
        }
    }
    return best;
}
// the move of the computer: depth by depth until the time ends; noise: a random weight for the easy levels
static bool ck_think(const ckpos_t *p, int max_depth, int ms, int noise, uint32_t seed, ckmove_t *best, int *depth_out)
{
    ckmove_t m[CK_MAXM];
    const int n = ck_gen(p, m, CK_MAXM);
    if (!n) {
        return false;
    }
    *best = m[0];
    *depth_out = 0;
    if (n == 1) {
        return true;
    }
    ckthink_t T = {ck_now_ms() + ms, 0, false, noise, seed};
    for (int d = 1; d <= max_depth; d++) {
        int bv = -1000000, bi = 0;
        for (int i = 0; i < n; i++) {
            ckpos_t q = *p;
            ck_make(&q, &m[i]);
            int v = -ck_search(&q, d - 1, -1000000, 1000000, &T, 1);
            if (T.out) {
                break;
            }
            if (noise) {
                T.seed = T.seed * 1664525u + 1013904223u;
                v += (int)(T.seed >> 16) % (2 * noise + 1) - noise;
            }
            if (v > bv) {
                bv = v;
                bi = i;
            }
        }
        if (T.out) {
            break;
        }
        *best = m[bi];
        *depth_out = d;
        // (the best move first in the next depth)
        ckmove_t t = m[0];
        m[0] = m[bi];
        m[bi] = t;
    }
    return true;
}
