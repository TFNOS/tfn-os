// BOULEVARD: the bots. bot_act() gives the next intent of the player who must decide now (a bot, or a human whose
// board left the game). The EASY bot buys when it has money to spare, builds now and then, pays to leave the jam,
// and takes a trade that gives it more by the prices. The GOOD bot keeps a reserve, completes its groups with trades,
// builds to three floors first, stays in the jam when the board is dangerous, and refuses a trade that gives a
// rival a full group.

static struct {
    uint16_t plan_turn, trade_turn;  // the turn (+1) of its last plan and its last trade offer
    uint16_t asked_turn[MAXP];       // the turn (+1) of its last offer to each player
    uint8_t asked_n[MAXP];           // the offers to that player that it refused (the price goes up)
} s_bot[MAXP];
static void bot_reset(void)
{
    memset(s_bot, 0, sizeof s_bot);
}

static bool bot_good(const bgame_t *g, int who)
{
    return g->P[who].kind != PK_EASY;
}
// a small number from the state (the same choice on the same state: the host tests can repeat a game)
static uint32_t bot_hash(const bgame_t *g, int who, int salt)
{
    uint32_t h = (uint32_t)g->turn * 2654435761u ^ (uint32_t)(who + 1) * 40503u ^ (uint32_t)salt * 97u ^ g->rng;
    h ^= h >> 15;
    h *= 0x2c1b3c6dU;
    h ^= h >> 12;
    return h;
}
static int unowned_count(const bgame_t *g)
{
    int k = 0;
    for (int i = 0; i < NPROP; i++) {
        k += g->R[i].owner == NOBODY;
    }
    return k;
}
// the danger of the board for who: the highest rent of a rival
static int32_t bot_danger(const bgame_t *g, int who)
{
    int32_t m = 0;
    for (int i = 0; i < NPROP; i++) {
        const int o = g->R[i].owner;
        if (o == NOBODY || o == who) {
            continue;
        }
        const int32_t r = rent_of(g, i, 7, 1);
        if (r > m) {
            m = r;
        }
    }
    return m;
}
static int32_t bot_reserve(const bgame_t *g, int who)
{
    if (!bot_good(g, who)) {
        return 120;
    }
    const int32_t d = bot_danger(g, who);
    int32_t r = g->round < 6 ? 60 : 150;
    if (d > 200) {
        r = 150 + d / 3;
    }
    return r > 600 ? 600 : r;
}
// how far the group of prop is from complete for who (0 = it would be complete with prop)
static int missing_with(const bgame_t *g, int who, int prop)
{
    const int gr = PT(prop)->group;
    int miss = 0;
    for (int k = 0; k < GROUP_N[gr]; k++) {
        const int i = GROUP_PROPS[gr][k];
        if (i != prop && g->R[i].owner != who) {
            miss++;
        }
    }
    return miss;
}
// the value of the properties of who after a change of owners (add: they become who's; rem: they leave who): the
// prices, and a bonus for each full group (a colour group: twice its prices; transport, services: a half)
static int32_t holdings(const bgame_t *g, int who, uint32_t add, uint32_t rem, bool bonus_only)
{
    int32_t v = 0;
    for (int gr = 0; gr < NGROUP; gr++) {
        int have = 0;
        int32_t sum = 0, mine = 0;
        for (int k = 0; k < GROUP_N[gr]; k++) {
            const int i = GROUP_PROPS[gr][k];
            const bool m = (g->R[i].owner == who && !(rem >> i & 1)) || (add >> i & 1);
            have += m;
            sum += PT(i)->price;
            if (m) {
                mine += PT(i)->price;
            }
        }
        if (!bonus_only) {
            v += mine;
        }
        if (have == GROUP_N[gr]) {
            v += gr < G_TRANS ? sum * 2 : sum / 2;
        }
    }
    return v;
}
static bool bot_accept(const bgame_t *g, int who, const btrade_t *t)
{
    // who is t->to: it gets t->give and t->cash_give; it gives t->get and t->cash_get
    int32_t gv = 0, lv = 0;
    for (int i = 0; i < NPROP; i++) {
        if (t->give >> i & 1) {
            gv += PT(i)->price;
        }
        if (t->get >> i & 1) {
            lv += PT(i)->price;
        }
    }
    const int32_t cash_in = t->cash_give - t->cash_get - fee_of(g, t->give);
    if (g->P[who].cash + cash_in < 0) {
        return false;
    }
    if (!bot_good(g, who)) {
        return gv + cash_in > lv + lv / 10;
    }
    // the good bot: its own change of value, the cash, and the full groups that the rival gets from it (in a long game
    // it fears them less: a game must end)
    const int32_t me = holdings(g, who, t->give, t->get, false) - holdings(g, who, 0, 0, false);
    const int32_t rival = holdings(g, t->from, t->get, t->give, true) - holdings(g, t->from, 0, 0, true);
    const int fear = g->round < 15 ? 75 : g->round < 30 ? 50 : 30;
    const int32_t net = me + cash_in - (rival > 0 ? rival * fear / 100 : 0);
    if (cash_in < 0 && g->P[who].cash + cash_in < bot_reserve(g, who) / 2) {
        return false;
    }
    return net > 40;
}

// the plan of the floors and the redeems (the PH_END of a bot): true = it changed the plan
static bool bot_build(const bgame_t *g, int who, uint8_t *plan)
{
    plan_now(g, plan);
    const bool good = bot_good(g, who);
    int32_t cash = g->P[who].cash;
    const int32_t keep = bot_reserve(g, who) + (good ? 50 : 300);
    bool any = false;
    // redeem the properties of full groups first, then the others
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < NPROP; i++) {
            if (g->R[i].owner != who || !(plan[i] & 0x80)) {
                continue;
            }
            const bool full = owns_group(g, who, PT(i)->group);
            if ((pass == 0) != full) {
                continue;
            }
            const int32_t c = redeem_cost(i);
            if (cash - c < keep + (good ? 0 : 200)) {
                continue;
            }
            plan[i] &= 0x7F;
            cash -= c;
            any = true;
        }
    }
    // the good bot with a full group under 3 floors and too little cash: it mortgages its other properties (the services
    // and the lone streets first) to pay for the floors
    if (good) {
        int32_t need = 0;
        for (int gr = 0; gr < G_TRANS; gr++) {
            if (!owns_group(g, who, gr)) {
                continue;
            }
            int lv = 0;
            bool mort = false;
            for (int j = 0; j < GROUP_N[gr]; j++) {
                lv += plan[GROUP_PROPS[gr][j]] & 7;
                mort |= (plan[GROUP_PROPS[gr][j]] & 0x80) != 0;
            }
            if (!mort && lv < 3 * GROUP_N[gr]) {
                need += (3 * GROUP_N[gr] - lv) * PT(GROUP_PROPS[gr][0])->build;
            }
        }
        for (int pass = 0; pass < 2 && need && cash - keep < need; pass++) {
            for (int i = 0; i < NPROP && cash - keep < need; i++) {
                if (g->R[i].owner != who || (plan[i] & 0x87)) {
                    continue;
                }
                const int gr = PT(i)->group;
                if (gr < G_TRANS && owns_group(g, who, gr)) {
                    continue;
                }
                const bool weak = gr == G_UTIL || (gr < G_TRANS && missing_with(g, who, i) >= 2);
                if ((pass == 0) != weak) {
                    continue;
                }
                plan[i] |= 0x80;
                cash += mort_value(i);
                any = true;
            }
        }
    }
    // the floors: the groups by their value for the bot (the good bot: orange, red, light blue, pink, yellow, ...)
    static const uint8_t ORDER_GOOD[G_TRANS] = {G_ORANGE, G_RED, G_LBLUE, G_PINK, G_YELLOW, G_DBLUE, G_GREEN, G_BROWN};
    static const uint8_t ORDER_EASY[G_TRANS] = {G_BROWN, G_LBLUE, G_PINK, G_ORANGE, G_RED, G_YELLOW, G_GREEN, G_DBLUE};
    int floors = 0;
    const int max_floors = good ? 30 : (int)(bot_hash(g, who, 5) % 2) + 1;
    for (int cap = 3; cap <= 5; cap += 2) {
        for (int k = 0; k < G_TRANS; k++) {
            const int gr = good ? ORDER_GOOD[k] : ORDER_EASY[k];
            if (!owns_group(g, who, gr)) {
                continue;
            }
            bool mort = false;
            for (int j = 0; j < GROUP_N[gr]; j++) {
                mort |= (plan[GROUP_PROPS[gr][j]] & 0x80) != 0;
            }
            if (mort) {
                continue;
            }
            const int32_t b = PT(GROUP_PROPS[gr][0])->build;
            for (int guard = 0; guard < 20 && floors < max_floors; guard++) {
                // the street of the group with the fewest floors (even building)
                int pick = -1, lo = 9;
                for (int j = 0; j < GROUP_N[gr]; j++) {
                    const int i = GROUP_PROPS[gr][j];
                    if ((plan[i] & 7) < lo) {
                        lo = plan[i] & 7;
                        pick = i;
                    }
                }
                if (pick < 0 || lo >= cap || cash - b < keep) {
                    break;
                }
                plan[pick]++;
                cash -= b;
                floors++;
                any = true;
            }
        }
        if (!good || cash < keep + 600) {
            break;  // (to a tower only with much money)
        }
    }
    if (!any) {
        return false;
    }
    int32_t cost;
    return plan_ok(g, who, plan, &cost);
}
// the plan that raises at least need (a debt or a buy): mortgages and sales with the least loss
static bool bot_raise(const bgame_t *g, int who, int32_t need, uint8_t *plan)
{
    plan_now(g, plan);
    int32_t cash = g->P[who].cash;
    for (int guard = 0; guard < 200 && cash < need; guard++) {
        int best = -1, act = 0;
        int32_t best_cost = 1 << 30, gain = 0;
        for (int i = 0; i < NPROP; i++) {
            if (g->R[i].owner != who) {
                continue;
            }
            const int lv = plan[i] & 7;
            const int gr = PT(i)->group;
            // a mortgage: no floor in its group (in the plan)
            if (!(plan[i] & 0x80) && !lv) {
                bool floors = false;
                if (gr < G_TRANS) {
                    for (int j = 0; j < GROUP_N[gr]; j++) {
                        floors |= (plan[GROUP_PROPS[gr][j]] & 7) != 0;
                    }
                }
                if (!floors) {
                    const int32_t cost = (gr < G_TRANS && owns_group(g, who, gr)) ? PT(i)->price * 3 : PT(i)->price;
                    if (cost < best_cost) {
                        best_cost = cost;
                        best = i;
                        act = 1;
                        gain = mort_value(i);
                    }
                }
            }
            // a sale of a floor: the street with the most floors of its group
            if (lv) {
                int hi = 0;
                for (int j = 0; j < GROUP_N[gr]; j++) {
                    const int l = plan[GROUP_PROPS[gr][j]] & 7;
                    if (l > hi) {
                        hi = l;
                    }
                }
                if (lv == hi) {
                    const int32_t cost = PT(i)->build * 2;
                    if (cost < best_cost) {
                        best_cost = cost;
                        best = i;
                        act = 2;
                        gain = PT(i)->build / 2;
                    }
                }
            }
        }
        if (best < 0) {
            break;
        }
        if (act == 1) {
            plan[best] |= 0x80;
        } else {
            plan[best]--;
        }
        cash += gain;
    }
    int32_t cost;
    return cash >= need && plan_ok(g, who, plan, &cost);
}
// a trade offer of the good bot: the last street of one of its groups, for cash (or a street that the other needs)
static bool bot_offer(const bgame_t *g, int who, btrade_t *t)
{
    if (!bot_good(g, who)) {
        return false;
    }
    const int32_t spare = g->P[who].cash - bot_reserve(g, who);
    for (int gr = 0; gr < G_TRANS; gr++) {
        if (GROUP_N[gr] - count_owned(g, who, gr) != 1 || group_built(g, gr)) {
            continue;
        }
        int miss = -1;
        for (int k = 0; k < GROUP_N[gr]; k++) {
            if (g->R[GROUP_PROPS[gr][k]].owner != who) {
                miss = GROUP_PROPS[gr][k];
            }
        }
        const int q = g->R[miss].owner;
        if (q == NOBODY || !alive(g, q)) {
            continue;
        }
        if (s_bot[who].asked_turn[q] && g->turn < s_bot[who].asked_turn[q] + 3 * g->n) {
            continue;
        }
        memset(t, 0, sizeof *t);
        t->from = (uint8_t)who;
        t->to = (uint8_t)q;
        t->get = 1u << miss;
        // a street of who that completes a group of q: a swap (with some cash when the prices differ)
        for (int i = 0; i < NPROP; i++) {
            if (g->R[i].owner != who || PT(i)->kind != TK_STREET || PT(i)->group == gr) {
                continue;
            }
            if (group_built(g, PT(i)->group) || missing_with(g, q, i)) {
                continue;
            }
            t->give = 1u << i;
            break;
        }
        const int32_t price = PT(miss)->price;
        int32_t cash = (price * (150 + 50 * s_bot[who].asked_n[q])) / 100;
        if (t->give) {
            cash = price > PT(__builtin_ctz(t->give))->price ? price - PT(__builtin_ctz(t->give))->price + 50 : 0;
        }
        if (cash > spare) {
            cash = spare;
        }
        if (!t->give && cash < price) {
            continue;
        }
        t->cash_give = cash < 0 ? 0 : cash;
        if (!trade_ok(g, t)) {
            continue;
        }
        return true;
    }
    return false;
}

// the next intent of the player who must decide now; false = nothing to do (wait)
static bool bot_act(const bgame_t *g, bin_t *out)
{
    memset(out, 0, sizeof *out);
    const int who = g_actor(g);
    if (who == NOBODY || !alive(g, who)) {
        return false;
    }
    const bpl_t *P = &g->P[who];
    const bool good = bot_good(g, who);
    switch (g->phase) {
    case PH_ROLL:
        if (P->jail) {
            const bool early = unowned_count(g) > 8;
            if (good) {
                if (early || bot_danger(g, who) < 300) {
                    if (P->cards) {
                        out->kind = IN_CARD;
                        return true;
                    }
                    if (P->cash > BAIL + bot_reserve(g, who)) {
                        out->kind = IN_BAIL;
                        return true;
                    }
                }
            } else {
                if (P->cards) {
                    out->kind = IN_CARD;
                    return true;
                }
                if (P->cash > 200) {
                    out->kind = IN_BAIL;
                    return true;
                }
            }
        }
        out->kind = IN_ROLL;
        return true;
    case PH_BUY: {
        const int pr = g->buy_prop;
        const int32_t price = PT(pr)->price;
        bool buy = P->cash >= price;
        if (buy && good) {
            const int miss = missing_with(g, who, pr);
            const int32_t left = P->cash - price;
            buy = left >= bot_reserve(g, who) || (miss == 0 && left >= 30) ||
                  (PT(pr)->group < G_TRANS && miss <= 1 && left >= 60);
        } else if (buy) {
            buy = P->cash - price >= 80 && bot_hash(g, who, pr) % 10 < 8;
        }
        out->kind = buy ? IN_BUY : IN_PASS;
        return true;
    }
    case PH_END:
        if (s_bot[who].plan_turn != g->turn + 1) {
            s_bot[who].plan_turn = (uint16_t)(g->turn + 1);
            if (bot_build(g, who, out->plan)) {
                out->kind = IN_PLAN;
                return true;
            }
        }
        if (good && s_bot[who].trade_turn != g->turn + 1) {
            s_bot[who].trade_turn = (uint16_t)(g->turn + 1);
            if (bot_offer(g, who, &out->tr)) {
                s_bot[who].asked_turn[out->tr.to] = (uint16_t)(g->turn + 1);
                if (s_bot[who].asked_n[out->tr.to] < 3) {
                    s_bot[who].asked_n[out->tr.to]++;
                }
                out->kind = IN_TRADE;
                return true;
            }
        }
        out->kind = IN_END;
        return true;
    case PH_DEBT: {
        const int32_t need = g->debt[0].amount;
        if (P->cash >= need) {
            out->kind = IN_PAY;
            return true;
        }
        if (g_liquid(g, who) < need) {
            out->kind = IN_BROKE;
            return true;
        }
        if (bot_raise(g, who, need, out->plan)) {
            out->kind = IN_PLAN;
            return true;
        }
        out->kind = IN_BROKE;  // (no plan found: the host refuses it while it can pay)
        return true;
    }
    case PH_TRADE:
        out->kind = IN_ANSWER;
        out->arg = bot_accept(g, who, &g->tr) ? 1 : 0;
        if (out->arg) {
            s_bot[g->tr.from].asked_n[who] = 0;
        }
        return true;
    }
    return false;
}
