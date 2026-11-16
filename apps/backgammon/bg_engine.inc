// BACKGAMMON: the rules and the computer (included by app_backgammon.c; the PC tests include it alone).
// The board: p[24] (index 0 = the 1 point of LIGHT; > 0: light checkers, < 0: red checkers). LIGHT moves down (to
// index 0, its home is 0..5); RED moves up (to 23, its home is 18..23). bar[2], off[2] (0 light, 1 red).
// A TURN: the dice; doubles = four moves. The rules: a checker on the bar enters first; a point with two or more
// checkers of the other side is closed; one checker of the other side is hit (to its bar); bearing off only when all
// 15 are home (a die higher than the highest checker takes it off). The turn must use as many dice as it can; when
// only one die can be used, the higher one when it can. The end: 1 point, a GAMMON (the loser bore off none) 2, a
// BACKGAMMON (and it still has a checker on the bar or in the winner's home) 3.
#include <stdint.h>
#include <string.h>

typedef struct {
    int8_t p[24];
    uint8_t bar[2], off[2];
} bgpos_t;
typedef struct {
    int8_t from, to;
    uint8_t die;
    uint8_t hit;
} bgstep_t;  // from 24 = the bar; to -1 = off
typedef struct {
    bgstep_t s[4];
    uint8_t n;
} bgturn_t;
#define BG_MAXT 700  // the most turns (distinct positions at the end) of one roll

static void bg_start(bgpos_t *b)
{
    memset(b, 0, sizeof *b);
    b->p[23] = 2;
    b->p[12] = 5;
    b->p[7] = 3;
    b->p[5] = 5;  // (light)
    b->p[0] = -2;
    b->p[11] = -5;
    b->p[16] = -3;
    b->p[18] = -5;  // (red)
}
static inline int bg_cnt(const bgpos_t *b, int i, int side)
{
    const int v = b->p[i];
    return side ? (v < 0 ? -v : 0) : (v > 0 ? v : 0);
}
static inline int bg_opp(const bgpos_t *b, int i, int side)
{
    return bg_cnt(b, i, !side);
}
static bool bg_all_home(const bgpos_t *b, int side)
{
    if (b->bar[side]) {
        return false;
    }
    for (int i = 0; i < 24; i++) {
        if (!bg_cnt(b, i, side)) {
            continue;
        }
        if (side ? i < 18 : i > 5) {
            return false;
        }
    }
    return true;
}
// one step with one die: true when it is legal (out: the step)
static bool bg_step_ok(const bgpos_t *b, int side, int from, int die, bgstep_t *out)
{
    int to;
    if (from == 24) {
        if (!b->bar[side]) {
            return false;
        }
        to = side ? die - 1 : 24 - die;
    } else {
        if (b->bar[side] || !bg_cnt(b, from, side)) {
            return false;
        }
        to = side ? from + die : from - die;
        if (to < 0 || to > 23) {
            // bearing off: all home; the exact die, or a higher die from the highest checker
            if (!bg_all_home(b, side)) {
                return false;
            }
            const int dist = side ? 24 - from : from + 1;  // (the pips to bear off)
            if (die != dist) {
                if (die < dist) {
                    return false;
                }
                for (int i = 0; i < 24; i++) {
                    if (!bg_cnt(b, i, side)) {
                        continue;
                    }
                    const int d2 = side ? 24 - i : i + 1;
                    if (d2 > dist) {
                        return false;  // (a checker farther away must move first)
                    }
                }
            }
            out->from = (int8_t)from;
            out->to = -1;
            out->die = (uint8_t)die;
            out->hit = 0;
            return true;
        }
    }
    const int o = bg_opp(b, to, side);
    if (o >= 2) {
        return false;
    }
    out->from = (int8_t)from;
    out->to = (int8_t)to;
    out->die = (uint8_t)die;
    out->hit = o == 1;
    return true;
}
static void bg_apply(bgpos_t *b, int side, const bgstep_t *s)
{
    const int sg = side ? -1 : 1;
    if (s->from == 24) {
        b->bar[side]--;
    } else {
        b->p[s->from] -= (int8_t)sg;
    }
    if (s->to < 0) {
        b->off[side]++;
        return;
    }
    if (s->hit) {
        b->p[s->to] = 0;
        b->bar[!side]++;
    }
    b->p[s->to] += (int8_t)sg;
}
// the turns of a roll: each legal turn that ends in a distinct position
typedef struct {
    bgturn_t *out;
    bgpos_t *end;
    uint32_t *hash;
    int n, max, most, side;
    bool dbl;
} bggen_t;
static uint32_t bg_hash(const bgpos_t *b)
{
    uint32_t h = 2166136261u;
    const uint8_t *q = (const uint8_t *)b;
    for (size_t i = 0; i < sizeof *b; i++) {
        h = (h ^ q[i]) * 16777619u;
    }
    return h;
}
// maxfrom: with doubles the steps go from the farthest source down (the same moves in another order are the same turn)
static void bg_dfs(bggen_t *G, const bgpos_t *b, const int *dice, int nd, bgturn_t *cur, int maxfrom)
{
    bool any = false;
    for (int k = 0; k < nd; k++) {
        if (k > 0 && dice[k] == dice[k - 1]) {
            continue;  // (the same die again: the same moves)
        }
        for (int from = G->dbl ? maxfrom : 24; from >= 0; from--) {
            bgstep_t s;
            if (!bg_step_ok(b, G->side, from, dice[k], &s)) {
                continue;
            }
            any = true;
            bgpos_t q = *b;
            bg_apply(&q, G->side, &s);
            int rest[4], nr = 0;
            for (int j = 0; j < nd; j++) {
                if (j != k) {
                    rest[nr++] = dice[j];
                }
            }
            cur->s[cur->n++] = s;
            bg_dfs(G, &q, rest, nr, cur, from);
            cur->n--;
        }
    }
    if (any) {
        return;
    }
    // the end of a turn: keep it when it uses the most dice (the others go when a longer turn comes)
    if (cur->n < G->most) {
        return;
    }
    if (cur->n > G->most) {
        G->most = cur->n;
        G->n = 0;
    }
    const uint32_t h = bg_hash(b);
    for (int i = 0; i < G->n; i++) {
        if (G->hash[i] == h && !memcmp(&G->end[i], b, sizeof *b)) {
            return;  // (the same end)
        }
    }
    if (G->n < G->max) {
        G->out[G->n] = *cur;
        G->end[G->n] = *b;
        G->hash[G->n] = h;
        G->n++;
    }
}
static int bg_turns(const bgpos_t *b, int side, int d1, int d2, bgturn_t *out, bgpos_t *end, int max)
{
    static uint32_t hash[BG_MAXT];
    bggen_t G = {out, end, hash, 0, max > BG_MAXT ? BG_MAXT : max, 0, side, d1 == d2};
    bgturn_t cur;
    memset(&cur, 0, sizeof cur);
    int dice[4], nd;
    if (d1 == d2) {
        nd = 4;
        dice[0] = dice[1] = dice[2] = dice[3] = d1;
    } else {
        nd = 2;
        dice[0] = d1 > d2 ? d1 : d2;
        dice[1] = d1 > d2 ? d2 : d1;
    }
    bg_dfs(&G, b, dice, nd, &cur, 24);
    if (!G.most) {
        return 0;  // (no legal step: the turn passes)
    }
    // only one die can be used: the higher one when it can
    if (G.most == 1 && d1 != d2) {
        const int hi = d1 > d2 ? d1 : d2;
        bool hi_ok = false;
        for (int i = 0; i < G.n; i++) {
            if (out[i].s[0].die == hi) {
                hi_ok = true;
            }
        }
        if (hi_ok) {
            int k = 0;
            for (int i = 0; i < G.n; i++) {
                if (out[i].s[0].die == hi) {
                    out[k] = out[i];
                    end[k] = end[i];
                    k++;
                }
            }
            G.n = k;
        }
    }
    return G.n;
}
// the end of a game: the winner (0, 1) or -1; *score: 1, 2 or 3
static int bg_winner(const bgpos_t *b, int *score)
{
    for (int s = 0; s < 2; s++) {
        if (b->off[s] < 15) {
            continue;
        }
        const int l = !s;
        int sc = 1;
        if (!b->off[l]) {
            sc = 2;
            bool back = b->bar[l] > 0;
            for (int i = 0; i < 24; i++) {
                if (bg_cnt(b, i, l) && (s ? i >= 18 : i <= 5)) {
                    back = true;  // (in the winner's home)
                }
            }
            if (back) {
                sc = 3;
            }
        }
        *score = sc;
        return s;
    }
    return -1;
}
static int bg_pips(const bgpos_t *b, int side)
{
    int n = b->bar[side] * 25;
    for (int i = 0; i < 24; i++) {
        n += bg_cnt(b, i, side) * (side ? 24 - i : i + 1);
    }
    return n;
}

// ---------------------------------------------------------------- the computer
// the value of a position for side (higher = better): the race, the blots that the other side can hit, the points
// made (more in the home, the prime), the anchors, the bar, the checkers off
static int bg_eval(const bgpos_t *b, int side)
{
    const int o = !side;
    int v = (bg_pips(b, o) - bg_pips(b, side)) * 4;
    v += (b->off[side] - b->off[o]) * 30;
    v += (b->bar[o] - b->bar[side]) * 60;
    // contact: one of my checkers is still behind one of his (else a race: the pips only). rel: my point - 1 (I move
    // to rel 0, he moves to rel 23)
    int my_far = -1, his_far = 24;
    for (int i = 0; i < 24; i++) {
        const int rel = side ? 23 - i : i;
        if (bg_cnt(b, i, side) && rel > my_far) {
            my_far = rel;
        }
        if (bg_cnt(b, i, o) && rel < his_far) {
            his_far = rel;
        }
    }
    if (b->bar[side]) {
        my_far = 24;
    }
    if (b->bar[o]) {
        his_far = -1;
    }
    const bool contact = my_far > his_far;
    if (!contact) {
        return v;
    }
    int run = 0;
    for (int i = 0; i < 24; i++) {
        const int rel = side ? 23 - i : i;
        const int n = bg_cnt(b, i, side);
        if (n >= 2) {
            int w = 18;
            if (rel <= 5) {
                w += 14 + (rel >= 3 ? 10 : 0);  // (my home, the 4 and 5 points)
            }
            if (rel == 6) {
                w += 10;  // (the bar point)
            }
            if (rel >= 18) {
                w += 12;  // (an anchor in his home)
            }
            v += w;
            run++;
            if (run >= 3) {
                v += 8 * (run - 2);  // (a prime)
            }
        } else {
            run = 0;
        }
        if (n == 1) {
            // a blot: the shots of the other side (his checkers in front of it within 12 pips, and his bar)
            int shots = 0;
            for (int j = 0; j < 24; j++) {
                if (!bg_cnt(b, j, o)) {
                    continue;
                }
                const int d = side ? i - j : j - i;  // (his move toward my blot)
                if (d >= 1 && d <= 6) {
                    shots += 11;
                } else if (d >= 7 && d <= 12) {
                    shots += 3;
                }
            }
            if (b->bar[o]) {
                const int d = side ? i + 1 : 24 - i;
                if (d >= 1 && d <= 6) {
                    shots += 11;
                }
            }
            if (shots > 36) {
                shots = 36;
            }
            v -= shots * (6 + (24 - rel)) / 6;  // (a deep blot costs more)
        }
    }
    return v;
}
static int bg_pick(const bgpos_t *b, int side, int d1, int d2, int level, uint32_t *seed, bgturn_t *best);
// the expected value after the other side's best answer (all 21 rolls; HARD looks this far for its best turns)
static int bg_reply(const bgpos_t *b, int side, bgturn_t *tt, bgpos_t *ee)
{
    int total = 0, wsum = 0;
    for (int a = 1; a <= 6; a++) {
        for (int c = a; c <= 6; c++) {
            const int w = a == c ? 1 : 2;
            const int n = bg_turns(b, !side, a, c, tt, ee, BG_MAXT);
            int worst = 1000000;
            if (!n) {
                worst = bg_eval(b, side);
            }
            for (int i = 0; i < n; i++) {
                const int v = bg_eval(&ee[i], side);
                if (v < worst) {
                    worst = v;
                }
            }
            total += worst * w;
            wsum += w;
        }
    }
    return total / wsum;
}
static bgturn_t *s_bg_tt[2];
static bgpos_t *s_bg_ee[2];
static int bg_pick(const bgpos_t *b, int side, int d1, int d2, int level, uint32_t *seed, bgturn_t *best)
{
    bgturn_t *tt = s_bg_tt[0];
    bgpos_t *ee = s_bg_ee[0];
    const int n = bg_turns(b, side, d1, d2, tt, ee, BG_MAXT);
    if (!n) {
        return 0;
    }
    int vals[BG_MAXT];
    for (int i = 0; i < n; i++) {
        vals[i] = bg_eval(&ee[i], side);
        if (level == 0) {
            *seed = *seed * 1664525u + 1013904223u;
            vals[i] += (int)(*seed >> 16) % 161 - 80;
        }  // (EASY: noise)
    }
    int bi = 0;
    for (int i = 1; i < n; i++) {
        if (vals[i] > vals[bi]) {
            bi = i;
        }
    }
    if (level >= 2 && n > 1 && s_bg_tt[1]) {
        // HARD: the best 5 by the value, each with the answer of the other side
        int top[5], nt = 0;
        for (int k = 0; k < 5 && k < n; k++) {
            int m = -1;
            for (int i = 0; i < n; i++) {
                bool used = false;
                for (int j = 0; j < nt; j++) {
                    if (top[j] == i) {
                        used = true;
                    }
                }
                if (!used && (m < 0 || vals[i] > vals[m])) {
                    m = i;
                }
            }
            top[nt++] = m;
        }
        int bv = -1000000;
        for (int j = 0; j < nt; j++) {
            const int v = bg_reply(&ee[top[j]], side, s_bg_tt[1], s_bg_ee[1]);
            if (v > bv) {
                bv = v;
                bi = top[j];
            }
        }
    }
    *best = tt[bi];
    return n;
}
