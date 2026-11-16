// BOULEVARD: the rules of the game (the engine). The board that hosts a game runs it (also a game alone). It has no
// drawing and no radio: an intent of a player (bin_t) goes in, the state (bgame_t) changes, and the events (bev_t) go
// into the log that the view and the clients play. All the money moves with pay_raw(): the bank and the players keep
// TOTAL_MONEY together at each step (g_check() tests it and the other rules).

enum {
    PK_HUMAN,
    PK_EASY,
    PK_GOOD
};
enum {
    PH_ROLL,
    PH_BUY,
    PH_END,
    PH_DEBT,
    PH_TRADE,
    PH_OVER
};
enum {
    MODE_FULL,
    MODE_TIME,
    MODE_TARGET
};
// the reasons of a payment (the view shows them)
enum {
    PW_RENT,
    PW_TAX,
    PW_CARD,
    PW_BAIL,
    PW_START,
    PW_BUY,
    PW_BUILD,
    PW_SELL,
    PW_MORT,
    PW_REDEEM,
    PW_TRADE,
    PW_FEE,
    PW_BROKE,
    PW_EACH
};
// the events
enum {
    EV_NONE,
    EV_TURN,
    EV_ROLL,
    EV_MOVE,
    EV_LAND,
    EV_PAY,
    EV_BUY,
    EV_NOBUY,
    EV_CARD,
    EV_JAIL,
    EV_STAY,
    EV_FREE,
    EV_BUILD,
    EV_MORT,
    EV_OWNER,
    EV_TRADE,
    EV_DEBT,
    EV_BROKE,
    EV_WIN,
    EV_CARDGET
};
// EV_MOVE flags
#define MV_STEP  1  // hop tile by tile (else a jump)
#define MV_BACK  2
#define MV_START 4  // it passed START (the bonus follows as an EV_PAY)
// EV_JAIL / EV_FREE reasons
enum {
    JW_WORKS,
    JW_CARD,
    JW_DOUBLES
};
enum {
    FW_DOUBLES,
    FW_PAID,
    FW_CARD,
    FW_THIRD
};
// EV_TRADE results
enum {
    TR_OFFER,
    TR_ACCEPT,
    TR_DECLINE,
    TR_CANCEL
};
// EV_WIN reasons (WW_ROUNDS: a FULL game ends after FULL_MAX_ROUNDS rounds; the richest wins)
enum {
    WW_LAST,
    WW_TIME,
    WW_TARGET,
    WW_ROUNDS
};
#define FULL_MAX_ROUNDS 200
// the intents
enum {
    IN_NONE,
    IN_ROLL,
    IN_BUY,
    IN_PASS,
    IN_END,
    IN_BAIL,
    IN_CARD,
    IN_PLAN,
    IN_TRADE,
    IN_ANSWER,
    IN_CANCEL,
    IN_PAY,
    IN_BROKE,
    IN_RESIGN
};

#define TOTAL_MONEY 1000000
#define GAME_MAGIC  0x42563031u  // "BV01"

typedef struct {
    int32_t cash;
    uint8_t pos, jail, out, kind, away, look, cards, rank;
} bpl_t;
typedef struct {
    uint8_t owner, level, mort;
} bpr_t;
typedef struct {
    uint8_t from, to;
    uint32_t give, get;           // bit i = property i (give: of from; get: of to)
    int32_t cash_give, cash_get;  // from -> to; to -> from
    uint8_t card_give, card_get;  // the free cards (bit d = the card of deck d)
} btrade_t;
typedef struct {
    uint8_t who, to, why;
    int32_t amount;
} bdebt_t;
typedef struct {
    uint8_t type, a, b, c;
    int32_t v;
} bev_t;
typedef struct {
    uint8_t kind, arg;
    uint8_t plan[NPROP];  // IN_PLAN: level (bits 0-2) | mortgaged (bit 7) of each property
    btrade_t tr;          // IN_TRADE
} bin_t;

typedef struct {
    uint32_t magic;
    uint8_t n, cur, phase, ret, after;  // ret: the phase after the debts; after: the phase after the landing
    uint8_t d1, d2, dbl, again;         // the dice, the doubles of this turn, another roll comes
    uint8_t mode, winner, win_why;
    uint8_t card_deck, card_id;
    uint8_t buy_prop;  // PH_BUY: the property
    uint8_t ndebt, tr_ret, nout;
    uint16_t turn, round;
    uint32_t time_ms, limit_ms;
    int32_t target, bank;
    bpl_t P[MAXP];
    bpr_t R[NPROP];
    uint8_t deck[2][NCARD], top[2];
    bdebt_t debt[MAXP + 2];
    btrade_t tr;
    uint32_t rng;
    uint16_t ack[MAXP];  // the last intent number of each seat that the host took
    uint32_t evn;        // the number of the last event
} bgame_t;

// ---- the tables of the board
static int8_t PROP_OF[NT];  // the property of a tile (-1: none)
static uint8_t TILE_OF[NPROP];
static uint8_t GROUP_N[NGROUP], GROUP_PROPS[NGROUP][4];
static void tables_init(void)
{
    static bool done;
    if (done) {
        return;
    }
    done = true;
    int k = 0;
    memset(GROUP_N, 0, sizeof GROUP_N);
    for (int t = 0; t < NT; t++) {
        PROP_OF[t] = -1;
        const uint8_t kd = TILES[t].kind;
        if (kd == TK_STREET || kd == TK_TRANS || kd == TK_UTIL) {
            PROP_OF[t] = (int8_t)k;
            TILE_OF[k] = (uint8_t)t;
            const uint8_t gr = TILES[t].group;
            GROUP_PROPS[gr][GROUP_N[gr]++] = (uint8_t)k;
            k++;
        }
    }
}
static inline const tile_t *PT(int prop)
{
    return &TILES[TILE_OF[prop]];
}

// ---- the event log (the host): the view of this board and the clients read it by number
#define EVLOG 128
static bev_t s_evlog[EVLOG];
static void ev(bgame_t *g, int type, int a, int b, int c, int32_t v)
{
    g->evn++;
    s_evlog[g->evn % EVLOG] = (bev_t){(uint8_t)type, (uint8_t)a, (uint8_t)b, (uint8_t)c, v};
}

// ---- the random numbers (xorshift)
static uint32_t rnd(bgame_t *g)
{
    uint32_t x = g->rng ? g->rng : 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g->rng = x;
    return x;
}
static int rnd_n(bgame_t *g, int n)
{
    return (int)(rnd(g) % (uint32_t)n);
}

// ---- questions about the state
static bool alive(const bgame_t *g, int p)
{
    return p >= 0 && p < g->n && !g->P[p].out;
}
static int alive_count(const bgame_t *g)
{
    int k = 0;
    for (int p = 0; p < g->n; p++) {
        k += !g->P[p].out;
    }
    return k;
}
static int count_owned(const bgame_t *g, int who, int gr)
{
    int k = 0;
    for (int i = 0; i < GROUP_N[gr]; i++) {
        k += g->R[GROUP_PROPS[gr][i]].owner == who;
    }
    return k;
}
static bool owns_group(const bgame_t *g, int who, int gr)
{
    return count_owned(g, who, gr) == GROUP_N[gr];
}
static bool group_built(const bgame_t *g, int gr)
{
    for (int i = 0; i < GROUP_N[gr]; i++) {
        if (g->R[GROUP_PROPS[gr][i]].level) {
            return true;
        }
    }
    return false;
}
static bool group_mort(const bgame_t *g, int gr)
{
    for (int i = 0; i < GROUP_N[gr]; i++) {
        if (g->R[GROUP_PROPS[gr][i]].mort) {
            return true;
        }
    }
    return false;
}
static int32_t mort_value(int prop)
{
    return PT(prop)->price / 2;
}
static int32_t redeem_cost(int prop)
{
    return mort_value(prop) + (mort_value(prop) + 9) / 10;
}
static int32_t rent_of(const bgame_t *g, int prop, int dice, int mult)
{
    const bpr_t *r = &g->R[prop];
    if (r->owner == NOBODY || r->mort) {
        return 0;
    }
    const tile_t *T = PT(prop);
    if (T->kind == TK_STREET) {
        if (r->level) {
            return (int32_t)T->rent[r->level] * mult;
        }
        int32_t v = T->rent[0];
        if (owns_group(g, r->owner, T->group)) {
            v *= 2;
        }
        return v * mult;
    }
    if (T->kind == TK_TRANS) {
        return (int32_t)TRANS_RENT[count_owned(g, r->owner, G_TRANS)] * mult;
    }
    if (mult == 10) {
        return dice * 10;
    }
    return dice * (count_owned(g, r->owner, G_UTIL) == 2 ? 10 : 4);
}
// the worth: the cash, the properties (a mortgaged one: half) and the floors at their price
static int32_t g_worth(const bgame_t *g, int p)
{
    int32_t w = g->P[p].cash;
    for (int i = 0; i < NPROP; i++) {
        if (g->R[i].owner != p) {
            continue;
        }
        w += g->R[i].mort ? mort_value(i) : PT(i)->price;
        w += (int32_t)g->R[i].level * PT(i)->build;
    }
    return w;
}
// the most cash that the player can raise: the cash, the floors sold at half, all the properties mortgaged
static int32_t g_liquid(const bgame_t *g, int p)
{
    int32_t w = g->P[p].cash;
    for (int i = 0; i < NPROP; i++) {
        if (g->R[i].owner != p) {
            continue;
        }
        w += (int32_t)g->R[i].level * (PT(i)->build / 2);
        if (!g->R[i].mort) {
            w += mort_value(i);
        }
    }
    return w;
}
// the player who must decide now (NOBODY: the game is over)
static int g_actor(const bgame_t *g)
{
    if (g->phase == PH_OVER) {
        return NOBODY;
    }
    if (g->phase == PH_DEBT && g->ndebt) {
        return g->debt[0].who;
    }
    if (g->phase == PH_TRADE) {
        return g->tr.to;
    }
    return g->cur;
}

// ---- the money
static void pay_raw(bgame_t *g, int from, int to, int32_t v, int why)
{
    if (v <= 0 || from == to) {
        return;
    }
    if (from == BANK) {
        g->bank -= v;
    } else {
        g->P[from].cash -= v;
    }
    if (to == BANK) {
        g->bank += v;
    } else {
        g->P[to].cash += v;
    }
    ev(g, EV_PAY, from, to, why, v);
}
// a charge: it is paid now when the cash is there, else it waits in the queue of the debts
static void charge(bgame_t *g, int who, int to, int32_t v, int why)
{
    if (v <= 0 || !alive(g, who)) {
        return;
    }
    if (to != BANK && !alive(g, to)) {
        to = BANK;
    }
    if (g->P[who].cash >= v && !g->ndebt) {
        pay_raw(g, who, to, v, why);
        return;
    }
    if (g->P[who].cash >= v) {
        // (an older debt waits: this one waits behind it, so the debts go in their order)
        bool mine = false;
        for (int i = 0; i < g->ndebt; i++) {
            mine |= g->debt[i].who == who;
        }
        if (!mine) {
            pay_raw(g, who, to, v, why);
            return;
        }
    }
    if (g->ndebt < MAXP + 2) {
        g->debt[g->ndebt++] = (bdebt_t){(uint8_t)who, (uint8_t)to, (uint8_t)why, v};
        ev(g, EV_DEBT, who, to, why, v);
    }
}
// the end of an action: the debts first, then the phase
static void settle(bgame_t *g, int post)
{
    if (g->phase == PH_OVER) {
        return;
    }
    if (g->ndebt) {
        g->ret = (uint8_t)post;
        g->phase = PH_DEBT;
    } else {
        g->phase = (uint8_t)post;
    }
}

// ---- the decks
static void deck_shuffle(bgame_t *g, int d)
{
    for (int i = 0; i < NCARD; i++) {
        g->deck[d][i] = (uint8_t)i;
    }
    for (int i = NCARD - 1; i > 0; i--) {
        int j = rnd_n(g, i + 1);
        uint8_t t = g->deck[d][i];
        g->deck[d][i] = g->deck[d][j];
        g->deck[d][j] = t;
    }
    g->top[d] = 0;
}
static bool card_held(const bgame_t *g, int d)
{
    for (int p = 0; p < g->n; p++) {
        if (g->P[p].cards & (1 << d)) {
            return true;
        }
    }
    return false;
}
static int deck_draw(bgame_t *g, int d)
{
    for (int k = 0; k < NCARD; k++) {
        const int id = g->deck[d][g->top[d]];
        g->top[d] = (uint8_t)((g->top[d] + 1) % NCARD);
        if (CARDS[d][id].effect == CE_FREE && card_held(g, d)) {
            continue;
        }
        return id;
    }
    return 1;
}

// ---- the setup of a new game
static void g_new(bgame_t *g, int n, const uint8_t *kinds, const uint8_t *looks, uint32_t seed, int mode)
{
    tables_init();
    memset(g, 0, sizeof *g);
    g->magic = GAME_MAGIC;
    g->n = (uint8_t)(n < 2 ? 2 : n > MAXP ? MAXP : n);
    g->rng = seed | 1;
    for (int i = 0; i < 7; i++) {
        rnd(g);
    }
    g->bank = TOTAL_MONEY;
    for (int p = 0; p < g->n; p++) {
        g->P[p].kind = kinds[p];
        g->P[p].look = looks ? looks[p] : (uint8_t)p;
        pay_raw(g, BANK, p, START_CASH, PW_START);
    }
    for (int i = 0; i < NPROP; i++) {
        g->R[i] = (bpr_t){NOBODY, 0, 0};
    }
    deck_shuffle(g, 0);
    deck_shuffle(g, 1);
    g->mode = (uint8_t)mode;
    g->limit_ms = 30u * 60 * 1000;
    g->target = 4000;
    g->winner = NOBODY;
    g->cur = (uint8_t)rnd_n(g, g->n);
    g->phase = PH_ROLL;
    g->buy_prop = NOBODY;
    ev(g, EV_TURN, g->cur, 0, 0, 0);
}

// ---- the end of the game
static int32_t s_rank_worth[MAXP];
static void game_over(bgame_t *g, int why)
{
    int best = -1;
    int32_t bw = -1;
    for (int p = 0; p < g->n; p++) {
        if (g->P[p].out) {
            continue;
        }
        const int32_t w = g_worth(g, p);
        if (w > bw) {
            bw = w;
            best = p;
        }
    }
    // the ranks: the players in the game by their worth, then the players out (the last one out first)
    int r = 1;
    for (int k = 0; k < g->n; k++) {
        int pick = -1;
        int32_t pw = -1;
        for (int p = 0; p < g->n; p++) {
            if (g->P[p].out || g->P[p].rank) {
                continue;
            }
            const int32_t w = g_worth(g, p);
            if (w > pw) {
                pw = w;
                pick = p;
            }
        }
        if (pick < 0) {
            break;
        }
        g->P[pick].rank = (uint8_t)r++;
        s_rank_worth[pick] = pw;
    }
    for (int p = 0; p < g->n; p++) {
        if (g->P[p].out && g->P[p].rank) {
            g->P[p].rank = (uint8_t)(g->P[p].rank + r - 1 - (g->n - g->nout));
        }
    }
    g->winner = (uint8_t)(best < 0 ? 0 : best);
    g->win_why = (uint8_t)why;
    g->phase = PH_OVER;
    ev(g, EV_WIN, g->winner, why, 0, bw);
}
static bool check_end(bgame_t *g)
{
    if (g->phase == PH_OVER) {
        return true;
    }
    if (alive_count(g) <= 1) {
        game_over(g, WW_LAST);
        return true;
    }
    if (g->mode == MODE_TIME && g->time_ms >= g->limit_ms) {
        game_over(g, WW_TIME);
        return true;
    }
    if (g->mode == MODE_FULL && g->round >= FULL_MAX_ROUNDS) {
        game_over(g, WW_ROUNDS);
        return true;
    }
    if (g->mode == MODE_TARGET) {
        for (int p = 0; p < g->n; p++) {
            if (!g->P[p].out && g_worth(g, p) >= g->target) {
                game_over(g, WW_TARGET);
                return true;
            }
        }
    }
    return false;
}
static void next_turn(bgame_t *g)
{
    if (check_end(g)) {
        return;
    }
    const int old = g->cur;
    int p = old;
    for (int k = 0; k < g->n; k++) {
        p = (p + 1) % g->n;
        if (!g->P[p].out) {
            break;
        }
    }
    if (p <= old) {
        g->round++;
    }
    g->cur = (uint8_t)p;
    g->turn++;
    g->dbl = 0;
    g->again = 0;
    g->phase = PH_ROLL;
    g->buy_prop = NOBODY;
    ev(g, EV_TURN, p, 0, 0, 0);
}

// ---- the moves
static void move_by(bgame_t *g, int p, int steps)
{
    bpl_t *P = &g->P[p];
    const int from = P->pos, to = (from + steps) % NT;
    const bool start = from + steps >= NT;
    P->pos = (uint8_t)to;
    ev(g, EV_MOVE, p, from, to, MV_STEP | (start ? MV_START : 0));
    if (start) {
        pay_raw(g, BANK, p, START_BONUS, PW_START);
    }
}
static void move_to(bgame_t *g, int p, int tile)
{
    bpl_t *P = &g->P[p];
    const int from = P->pos;
    const bool start = tile <= from;  // (forward: it passes START, also "go to START")
    P->pos = (uint8_t)tile;
    ev(g, EV_MOVE, p, from, tile, MV_STEP | (start ? MV_START : 0));
    if (start) {
        pay_raw(g, BANK, p, START_BONUS, PW_START);
    }
}
static void go_jail(bgame_t *g, int p, int why)
{
    bpl_t *P = &g->P[p];
    const int from = P->pos;
    P->pos = JAM_TILE;
    P->jail = 1;
    g->dbl = 0;
    g->again = 0;
    g->after = PH_END;
    ev(g, EV_MOVE, p, from, JAM_TILE, MV_BACK);
    ev(g, EV_JAIL, p, why, 0, 0);
}

// ---- the landing
static void land(bgame_t *g, int p, int depth, int mult);
static void apply_card(bgame_t *g, int p, int d, int id, int depth)
{
    const card_t *C = &CARDS[d][id];
    switch (C->effect) {
    case CE_GO:
        move_to(g, p, C->a);
        land(g, p, depth + 1, 1);
        break;
    case CE_NEAR_TRANS:
    case CE_NEAR_UTIL: {
        const int kd = C->effect == CE_NEAR_TRANS ? TK_TRANS : TK_UTIL;
        int t = g->P[p].pos;
        for (int k = 0; k < NT; k++) {
            t = (t + 1) % NT;
            if (TILES[t].kind == kd) {
                break;
            }
        }
        move_to(g, p, t);
        land(g, p, depth + 1, C->effect == CE_NEAR_TRANS ? 2 : 10);
        break;
    }
    case CE_BACK3: {
        bpl_t *P = &g->P[p];
        const int from = P->pos;
        P->pos = (uint8_t)((from + NT - 3) % NT);
        ev(g, EV_MOVE, p, from, P->pos, MV_STEP | MV_BACK);
        land(g, p, depth + 1, 1);
        break;
    }
    case CE_JAIL:
        go_jail(g, p, JW_CARD);
        break;
    case CE_FREE:
        g->P[p].cards |= (uint8_t)(1 << d);
        ev(g, EV_CARDGET, p, d, 0, 0);
        break;
    case CE_MONEY:
        if (C->a > 0) {
            pay_raw(g, BANK, p, C->a, PW_CARD);
        } else {
            charge(g, p, BANK, -C->a, PW_CARD);
        }
        break;
    case CE_EACH:
        for (int q = 0; q < g->n; q++) {
            if (q == p || g->P[q].out) {
                continue;
            }
            if (C->a > 0) {
                charge(g, q, p, C->a, PW_EACH);
            } else {
                charge(g, p, q, -C->a, PW_EACH);
            }
        }
        break;
    case CE_REPAIRS: {
        int32_t sum = 0;
        for (int i = 0; i < NPROP; i++) {
            if (g->R[i].owner != p || !g->R[i].level) {
                continue;
            }
            sum += g->R[i].level == 5 ? C->b : (int32_t)g->R[i].level * C->a;
        }
        charge(g, p, BANK, sum, PW_CARD);
        break;
    }
    }
}
static void land(bgame_t *g, int p, int depth, int mult)
{
    const int t = g->P[p].pos;
    const tile_t *T = &TILES[t];
    ev(g, EV_LAND, p, t, 0, 0);
    switch (T->kind) {
    case TK_STREET:
    case TK_TRANS:
    case TK_UTIL: {
        const int pr = PROP_OF[t];
        const bpr_t *r = &g->R[pr];
        if (r->owner == NOBODY) {
            g->buy_prop = (uint8_t)pr;
            break;
        }
        if (r->owner == p || r->mort || g->P[r->owner].out) {
            break;
        }
        charge(g, p, r->owner, rent_of(g, pr, g->d1 + g->d2, mult), PW_RENT);
        break;
    }
    case TK_TAX:
        charge(g, p, BANK, T->price, PW_TAX);
        break;
    case TK_CITY:
    case TK_SURP: {
        if (depth > 3) {
            break;
        }
        const int d = T->kind == TK_CITY ? 0 : 1;
        const int id = deck_draw(g, d);
        g->card_deck = (uint8_t)d;
        g->card_id = (uint8_t)id;
        ev(g, EV_CARD, p, d, id, 0);
        apply_card(g, p, d, id, depth);
        break;
    }
    case TK_WORKS:
        go_jail(g, p, JW_WORKS);
        break;
    default:
        break;
    }
}
// after a roll and its landing: a buy, the debts, then another roll or the end of the turn
static void after_land(bgame_t *g)
{
    if (g->phase == PH_OVER) {
        return;
    }
    const int post = g->buy_prop != NOBODY ? PH_BUY : g->after;
    settle(g, post);
}
static void do_roll(bgame_t *g)
{
    const int p = g->cur;
    bpl_t *P = &g->P[p];
    g->d1 = (uint8_t)(1 + rnd_n(g, 6));
    g->d2 = (uint8_t)(1 + rnd_n(g, 6));
    const bool dbl = g->d1 == g->d2;
    g->buy_prop = NOBODY;
    ev(g, EV_ROLL, p, g->d1, g->d2, 0);
    g->again = 0;
    if (P->jail) {
        if (dbl) {
            P->jail = 0;
            ev(g, EV_FREE, p, FW_DOUBLES, 0, 0);
        } else if (P->jail >= 3) {
            P->jail = 0;
            ev(g, EV_FREE, p, FW_THIRD, 0, 0);
            charge(g, p, BANK, BAIL, PW_BAIL);
        } else {
            P->jail++;
            ev(g, EV_STAY, p, P->jail, 0, 0);
            settle(g, PH_END);
            return;
        }
        g->after = PH_END;  // (out of the jam: no more roll in this turn)
    } else if (dbl) {
        if (++g->dbl >= 3) {
            go_jail(g, p, JW_DOUBLES);
            settle(g, PH_END);
            return;
        }
        g->again = 1;
        g->after = PH_ROLL;
    } else {
        g->after = PH_END;
    }
    move_by(g, p, g->d1 + g->d2);
    land(g, p, 0, 1);
    after_land(g);
}

// ---- the bankruptcy: the floors go back to the bank at half, then all goes to the creditor (a player or the bank)
static void bankrupt(bgame_t *g, int p, int to)
{
    if (!alive(g, p)) {
        return;
    }
    if (to != BANK && !alive(g, to)) {
        to = BANK;
    }
    for (int i = 0; i < NPROP; i++) {
        bpr_t *r = &g->R[i];
        if (r->owner != p || !r->level) {
            continue;
        }
        const int old = r->level;
        pay_raw(g, BANK, p, (int32_t)old * (PT(i)->build / 2), PW_SELL);
        r->level = 0;
        ev(g, EV_BUILD, p, i, 0, old);
    }
    pay_raw(g, p, to, g->P[p].cash, PW_BROKE);
    for (int i = 0; i < NPROP; i++) {
        bpr_t *r = &g->R[i];
        if (r->owner != p) {
            continue;
        }
        if (to == BANK) {
            r->owner = NOBODY;
            r->mort = 0;
        } else {
            r->owner = (uint8_t)to;
        }
        ev(g, EV_OWNER, r->owner, i, r->mort, 0);
    }
    if (to != BANK) {
        g->P[to].cards |= g->P[p].cards;
    }
    g->P[p].cards = 0;
    g->P[p].out = 1;
    g->P[p].jail = 0;
    g->nout++;
    g->P[p].rank = (uint8_t)(g->n - g->nout + 1);
    ev(g, EV_BROKE, p, to, 0, 0);
}
// the flow after a player went out
static void fix_flow(bgame_t *g)
{
    if (g->phase == PH_OVER) {
        return;
    }
    // the debts of a player that is out go away; a debt to a player that is out goes to the bank
    int k = 0;
    for (int i = 0; i < g->ndebt; i++) {
        bdebt_t d = g->debt[i];
        if (!alive(g, d.who)) {
            continue;
        }
        if (d.to != BANK && !alive(g, d.to)) {
            d.to = BANK;
        }
        g->debt[k++] = d;
    }
    g->ndebt = (uint8_t)k;
    if (check_end(g)) {
        return;
    }
    if (g->phase == PH_TRADE && (!alive(g, g->tr.from) || !alive(g, g->tr.to))) {
        g->phase = g->tr_ret;
    }
    if (g->phase == PH_DEBT && !g->ndebt) {
        g->phase = g->ret;
    }
    if (!alive(g, g->cur)) {
        if (g->phase == PH_DEBT) {
            g->ret = PH_END;
            return;
        }  // (the others pay first; then the turn goes on)
        next_turn(g);
        return;
    }
    if (g->phase == PH_BUY && g->buy_prop == NOBODY) {
        g->phase = g->after;
    }
}

// ---- the plan: the new floors, sales and mortgages of one player (the MANAGE screen, the bots)
static bool plan_ok(const bgame_t *g, int who, const uint8_t *plan, int32_t *cost_out)
{
    int32_t cost = 0;
    for (int i = 0; i < NPROP; i++) {
        const bpr_t *r = &g->R[i];
        const int lv = plan[i] & 7, mt = plan[i] >> 7;
        if (r->owner != who) {
            if (lv != r->level || mt != r->mort) {
                return false;
            }
            continue;
        }
        if (lv > 5 || (mt && lv)) {
            return false;
        }
        if (lv && PT(i)->kind != TK_STREET) {
            return false;
        }
        if (lv > r->level) {
            cost += (int32_t)(lv - r->level) * PT(i)->build;
        }
        if (lv < r->level) {
            cost -= (int32_t)(r->level - lv) * (PT(i)->build / 2);
        }
        if (mt && !r->mort) {
            cost -= mort_value(i);
        }
        if (!mt && r->mort) {
            cost += redeem_cost(i);
        }
    }
    for (int gr = 0; gr < G_TRANS; gr++) {
        int mn = 9, mx = 0;
        bool any_mort = false, all_mine = true, any_mine = false;
        for (int k = 0; k < GROUP_N[gr]; k++) {
            const int i = GROUP_PROPS[gr][k], lv = plan[i] & 7;
            if (lv < mn) {
                mn = lv;
            }
            if (lv > mx) {
                mx = lv;
            }
            any_mort |= (plan[i] >> 7) != 0;
            all_mine &= g->R[i].owner == who;
            any_mine |= g->R[i].owner == who;
        }
        if (mx == 0 || !any_mine) {
            continue;  // (no floor, or a group of the others: the plan does not change it)
        }
        if (!all_mine || any_mort || mx - mn > 1) {
            return false;
        }
    }
    if (cost_out) {
        *cost_out = cost;
    }
    return g->P[who].cash - cost >= 0;
}
static void plan_now(const bgame_t *g, uint8_t *plan)
{
    for (int i = 0; i < NPROP; i++) {
        plan[i] = (uint8_t)(g->R[i].level | (g->R[i].mort ? 0x80 : 0));
    }
}
static void apply_plan(bgame_t *g, int who, const uint8_t *plan)
{
    // the cash in first (the sales of floors, the mortgages), then the cash out (the redeems, the floors)
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NPROP; i++) {
            bpr_t *r = &g->R[i];
            if (r->owner != who) {
                continue;
            }
            const int lv = plan[i] & 7, mt = plan[i] >> 7;
            if (pass == 0 && lv < r->level) {
                const int old = r->level;
                pay_raw(g, BANK, who, (int32_t)(old - lv) * (PT(i)->build / 2), PW_SELL);
                r->level = (uint8_t)lv;
                ev(g, EV_BUILD, who, i, lv, old);
            }
            if (pass == 0 && mt && !r->mort) {
                r->mort = 1;
                pay_raw(g, BANK, who, mort_value(i), PW_MORT);
                ev(g, EV_MORT, who, i, 1, 0);
            }
            if (pass == 1 && !mt && r->mort) {
                pay_raw(g, who, BANK, redeem_cost(i), PW_REDEEM);
                r->mort = 0;
                ev(g, EV_MORT, who, i, 0, 0);
            }
            if (pass == 1 && lv > r->level) {
                const int old = r->level;
                pay_raw(g, who, BANK, (int32_t)(lv - old) * PT(i)->build, PW_BUILD);
                r->level = (uint8_t)lv;
                ev(g, EV_BUILD, who, i, lv, old);
            }
        }
    }
}

// ---- the trade
static int32_t fee_of(const bgame_t *g, uint32_t props)
{
    int32_t f = 0;
    for (int i = 0; i < NPROP; i++) {
        if ((props >> i & 1) && g->R[i].mort) {
            f += (mort_value(i) + 9) / 10;
        }
    }
    return f;
}
static bool trade_ok(const bgame_t *g, const btrade_t *t)
{
    if (!alive(g, t->from) || !alive(g, t->to) || t->from == t->to) {
        return false;
    }
    if (t->cash_give < 0 || t->cash_get < 0) {
        return false;
    }
    if (!t->give && !t->get && !t->cash_give && !t->cash_get && !t->card_give && !t->card_get) {
        return false;
    }
    if ((t->give | t->get) >> NPROP) {
        return false;
    }
    for (int i = 0; i < NPROP; i++) {
        const bool a = t->give >> i & 1, b = t->get >> i & 1;
        if (a && g->R[i].owner != t->from) {
            return false;
        }
        if (b && g->R[i].owner != t->to) {
            return false;
        }
        if ((a || b) && PT(i)->kind == TK_STREET && group_built(g, PT(i)->group)) {
            return false;
        }
    }
    if ((t->card_give & ~g->P[t->from].cards) || (t->card_get & ~g->P[t->to].cards)) {
        return false;
    }
    if (g->P[t->from].cash - t->cash_give + t->cash_get - fee_of(g, t->get) < 0) {
        return false;
    }
    if (g->P[t->to].cash - t->cash_get + t->cash_give - fee_of(g, t->give) < 0) {
        return false;
    }
    return true;
}
static void trade_do(bgame_t *g, const btrade_t *t)
{
    pay_raw(g, t->from, t->to, t->cash_give, PW_TRADE);
    pay_raw(g, t->to, t->from, t->cash_get, PW_TRADE);
    for (int i = 0; i < NPROP; i++) {
        if (t->give >> i & 1) {
            g->R[i].owner = t->to;
            ev(g, EV_OWNER, t->to, i, g->R[i].mort, 1);
        }
        if (t->get >> i & 1) {
            g->R[i].owner = t->from;
            ev(g, EV_OWNER, t->from, i, g->R[i].mort, 1);
        }
    }
    pay_raw(g, t->from, BANK, fee_of(g, t->get), PW_FEE);
    pay_raw(g, t->to, BANK, fee_of(g, t->give), PW_FEE);
    g->P[t->from].cards = (uint8_t)((g->P[t->from].cards & ~t->card_give) | t->card_get);
    g->P[t->to].cards = (uint8_t)((g->P[t->to].cards & ~t->card_get) | t->card_give);
}

// ---- the intents: true = it was valid now and the state changed
static bool g_can_manage(const bgame_t *g, int who)
{
    if (!alive(g, who)) {
        return false;
    }
    if (g->phase == PH_DEBT) {
        return g->ndebt && g->debt[0].who == who;
    }
    return who == g->cur && (g->phase == PH_ROLL || g->phase == PH_BUY || g->phase == PH_END);
}
static bool g_apply(bgame_t *g, int who, const bin_t *in)
{
    if (g->phase == PH_OVER || !alive(g, who)) {
        return false;
    }
    bpl_t *P = &g->P[who];
    const bool cur = who == g->cur;
    switch (in->kind) {
    case IN_ROLL:
        if (!cur || g->phase != PH_ROLL) {
            return false;
        }
        do_roll(g);
        return true;
    case IN_BUY: {
        if (!cur || g->phase != PH_BUY || g->buy_prop == NOBODY) {
            return false;
        }
        const int pr = g->buy_prop;
        if (g->R[pr].owner != NOBODY || P->cash < PT(pr)->price) {
            return false;
        }
        pay_raw(g, who, BANK, PT(pr)->price, PW_BUY);
        g->R[pr].owner = (uint8_t)who;
        ev(g, EV_BUY, who, pr, 0, PT(pr)->price);
        g->buy_prop = NOBODY;
        g->phase = g->after;
        return true;
    }
    case IN_PASS:
        if (!cur || g->phase != PH_BUY) {
            return false;
        }
        ev(g, EV_NOBUY, who, g->buy_prop, 0, 0);
        g->buy_prop = NOBODY;
        g->phase = g->after;
        return true;
    case IN_END:
        if (!cur || g->phase != PH_END) {
            return false;
        }
        next_turn(g);
        return true;
    case IN_BAIL:
        if (!cur || g->phase != PH_ROLL || !P->jail || P->cash < BAIL) {
            return false;
        }
        pay_raw(g, who, BANK, BAIL, PW_BAIL);
        P->jail = 0;
        ev(g, EV_FREE, who, FW_PAID, 0, 0);
        return true;
    case IN_CARD: {
        if (!cur || g->phase != PH_ROLL || !P->jail || !P->cards) {
            return false;
        }
        const int d = (P->cards & 1) ? 0 : 1;
        P->cards &= (uint8_t)~(1 << d);
        P->jail = 0;
        ev(g, EV_FREE, who, FW_CARD, d, 0);
        return true;
    }
    case IN_PLAN: {
        if (!g_can_manage(g, who)) {
            return false;
        }
        int32_t cost;
        if (!plan_ok(g, who, in->plan, &cost)) {
            return false;
        }
        apply_plan(g, who, in->plan);
        return true;
    }
    case IN_TRADE: {
        if (!cur || (g->phase != PH_ROLL && g->phase != PH_END)) {
            return false;
        }
        btrade_t t = in->tr;
        t.from = (uint8_t)who;
        if (!trade_ok(g, &t)) {
            return false;
        }
        g->tr = t;
        g->tr_ret = g->phase;
        g->phase = PH_TRADE;
        ev(g, EV_TRADE, t.from, t.to, TR_OFFER, 0);
        return true;
    }
    case IN_ANSWER:
        if (g->phase != PH_TRADE || who != g->tr.to) {
            return false;
        }
        if (in->arg && trade_ok(g, &g->tr)) {
            trade_do(g, &g->tr);
            ev(g, EV_TRADE, g->tr.from, g->tr.to, TR_ACCEPT, 0);
        } else {
            ev(g, EV_TRADE, g->tr.from, g->tr.to, TR_DECLINE, 0);
        }
        g->phase = g->tr_ret;
        return true;
    case IN_CANCEL:
        if (g->phase != PH_TRADE || who != g->tr.from) {
            return false;
        }
        ev(g, EV_TRADE, g->tr.from, g->tr.to, TR_CANCEL, 0);
        g->phase = g->tr_ret;
        return true;
    case IN_PAY: {
        if (g->phase != PH_DEBT || !g->ndebt || g->debt[0].who != who) {
            return false;
        }
        const bdebt_t d = g->debt[0];
        if (P->cash < d.amount) {
            return false;
        }
        pay_raw(g, who, d.to, d.amount, d.why);
        memmove(g->debt, g->debt + 1, sizeof(bdebt_t) * (size_t)(--g->ndebt));
        if (!g->ndebt) {
            g->phase = g->ret;
            if (!alive(g, g->cur)) {
                next_turn(g);
            }
        }
        return true;
    }
    case IN_BROKE:
        if (g->phase != PH_DEBT || !g->ndebt || g->debt[0].who != who) {
            return false;
        }
        if (g_liquid(g, who) >= g->debt[0].amount) {
            return false;  // (it can pay: no bankruptcy)
        }
        bankrupt(g, who, g->debt[0].to);
        fix_flow(g);
        return true;
    case IN_RESIGN: {
        const int to = (g->phase == PH_DEBT && g->ndebt && g->debt[0].who == who) ? g->debt[0].to : BANK;
        bankrupt(g, who, to);
        fix_flow(g);
        return true;
    }
    }
    return false;
}
// the game clock (MODE_TIME): the host adds the time of the game; the end comes at the end of a turn
static void g_tick(bgame_t *g, uint32_t dt_ms)
{
    if (g->phase != PH_OVER) {
        g->time_ms += dt_ms;
    }
}

// ---- the check of all the rules (tests; the host calls it after each intent in a debug build)
static const char *g_check(const bgame_t *g)
{
    static char why[96];
    int64_t sum = g->bank;
    for (int p = 0; p < g->n; p++) {
        const bpl_t *P = &g->P[p];
        sum += P->cash;
        if (P->cash < 0) {
            snprintf(why, sizeof why, "player %d has cash %ld", p, (long)P->cash);
            return why;
        }
        if (P->pos >= NT || P->jail > 3) {
            snprintf(why, sizeof why, "player %d pos %d jail %d", p, P->pos, P->jail);
            return why;
        }
        if (P->out && (P->cash || P->cards)) {
            snprintf(why, sizeof why, "player %d is out with cash or a card", p);
            return why;
        }
    }
    if (sum != TOTAL_MONEY) {
        snprintf(why, sizeof why, "the money is %lld, not %d", (long long)sum, TOTAL_MONEY);
        return why;
    }
    for (int i = 0; i < NPROP; i++) {
        const bpr_t *r = &g->R[i];
        if (r->owner == NOBODY) {
            if (r->level || r->mort) {
                snprintf(why, sizeof why, "prop %d of the bank has floors", i);
                return why;
            }
            continue;
        }
        if (r->owner >= g->n || g->P[r->owner].out) {
            snprintf(why, sizeof why, "prop %d has the owner %d", i, r->owner);
            return why;
        }
        if (r->level > 5 || (r->mort && r->level) || (r->level && PT(i)->kind != TK_STREET)) {
            snprintf(why, sizeof why, "prop %d level %d mort %d", i, r->level, r->mort);
            return why;
        }
    }
    for (int gr = 0; gr < G_TRANS; gr++) {
        if (!group_built(g, gr)) {
            continue;
        }
        int mn = 9, mx = 0;
        const int o = g->R[GROUP_PROPS[gr][0]].owner;
        for (int k = 0; k < GROUP_N[gr]; k++) {
            const bpr_t *r = &g->R[GROUP_PROPS[gr][k]];
            if (r->owner != o || r->mort) {
                snprintf(why, sizeof why, "group %d is built but not one owner", gr);
                return why;
            }
            if (r->level < mn) {
                mn = r->level;
            }
            if (r->level > mx) {
                mx = r->level;
            }
        }
        if (mx - mn > 1) {
            snprintf(why, sizeof why, "group %d is not even (%d..%d)", gr, mn, mx);
            return why;
        }
    }
    for (int d = 0; d < 2; d++) {
        int k = 0;
        for (int p = 0; p < g->n; p++) {
            k += (g->P[p].cards >> d) & 1;
        }
        if (k > 1) {
            snprintf(why, sizeof why, "the free card %d is held %d times", d, k);
            return why;
        }
    }
    if (g->phase == PH_DEBT && !g->ndebt) {
        return "PH_DEBT with no debt";
    }
    if (g->phase != PH_OVER && !alive(g, g->cur)) {
        return "the current player is out";
    }
    if (g->phase == PH_BUY && (g->buy_prop >= NPROP || g->R[g->buy_prop].owner != NOBODY)) {
        return "PH_BUY with no free property";
    }
    if (g->phase == PH_TRADE && (!alive(g, g->tr.from) || !alive(g, g->tr.to))) {
        return "PH_TRADE with a player out";
    }
    return NULL;
}
