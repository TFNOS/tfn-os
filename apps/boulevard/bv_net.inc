// BOULEVARD: the radio of a table of 2 to 6 boards (included by app_blvd.c). ESP-NOW broadcasts (main/link.h, app id
// 'V'): the board that opens a table is the HOST; it runs the rules and the bots, and sends the state and the events;
// the other boards (the CLIENTS) show them and send the intents of their seat. Lost packets: the host sends the state
// again each 300 ms and the newest events each second; a client sends its intent again until the state acks it.
//   TABLE  host -> all (in the room, each 0.5 s): the table, its seats (the look, the kind, the board of each seat)
//   JOIN   client -> host: a seat for this board (its look); LEAVE: the board leaves the room
//   STATE  host -> all: the compact state of the game (pack_state); EVENTS: up to 24 events from a number
//   INPUT  client -> host: the intent of the seat (a number, so the host takes each one once)
//   BYE    host -> all: the table closed

#define LINK_APP 'V'
enum {
    K_TABLE = 1,
    K_JOIN,
    K_LEAVE,
    K_STATE,
    K_EVENTS,
    K_INPUT,
    K_BYE
};
enum {
    ROLE_LOCAL,
    ROLE_HOST,
    ROLE_CLIENT
};
enum {
    SK_EMPTY,
    SK_HUMAN,
    SK_BOT
};
static int s_role;
static uint16_t s_tid;  // the table
static uint8_t s_seat_kind[MAXP], s_seat_look[MAXP], s_seat_mac[MAXP][3];
static int s_nseats;
static int s_my;  // the seat of this board
static uint8_t s_my_mac[6];
static int64_t s_seen[MAXP];  // the host: the last packet of the board of each seat
static int64_t s_last_state, s_last_events, s_last_rx, s_last_table;
static uint16_t s_in_seq;  // the client: the number of the last intent
static bin_t s_in_wait;    // the client: the intent that waits for its ack
static bool s_in_pending;
static bool s_net_on, s_host_gone;
static bool s_host_paused;  // the client: the BYE of the host said "the game waits" (it is saved)
// the tables that this board hears (the room of JOIN)
typedef struct {
    uint16_t tid;
    uint8_t n, kind[MAXP], look[MAXP], mac[MAXP][3], started;
    int64_t seen;
} table_t;
#define NTAB 6
static table_t s_tabs[NTAB];

static bool net_start(void)
{
    if (!s_net_on) {
        s_net_on = link_start();
    }
    if (s_net_on) {
        link_my_mac(s_my_mac);
    }
    return s_net_on;
}
static void net_stop(void)
{
    if (s_net_on) {
        if (s_role == ROLE_HOST) {
            // (b[2] = 1: the game is saved and waits: the host can open it again with OUR LAST GAME)
            uint8_t b[3] = {(uint8_t)s_tid, (uint8_t)(s_tid >> 8),
                            (uint8_t)(g_v.magic == GAME_MAGIC && g_v.phase != PH_OVER && s_scr != SCR_ROOM)};
            link_send(LINK_APP, K_BYE, b, 3);
        }
        link_stop();
    }
    s_net_on = false;
}

// ---- the compact state (about 180 bytes: one packet)
static int pack_state(const bgame_t *g, uint8_t *o)
{
    uint8_t *p = o;
#define P8(v) (*p++ = (uint8_t)(v))
#define P16(v)                                                                                                         \
    do {                                                                                                               \
        uint16_t _v = (uint16_t)(v);                                                                                   \
        *p++ = (uint8_t)_v;                                                                                            \
        *p++ = (uint8_t)(_v >> 8);                                                                                     \
    } while (0)
#define P32(v)                                                                                                         \
    do {                                                                                                               \
        uint32_t _v = (uint32_t)(v);                                                                                   \
        for (int _i = 0; _i < 4; _i++)                                                                                 \
            *p++ = (uint8_t)(_v >> (8 * _i));                                                                          \
    } while (0)
    P16(s_tid);
    P8(g->n);
    P8(g->cur);
    P8(g->phase);
    P8(g->ret);
    P8(g->after);
    P8(g->d1);
    P8(g->d2);
    P8(g->dbl);
    P8(g->again);
    P8(g->mode);
    P8(g->winner);
    P8(g->win_why);
    P8(g->card_deck);
    P8(g->card_id);
    P8(g->buy_prop);
    P8(g->ndebt);
    P16(g->turn);
    P16(g->round);
    P32(g->time_ms);
    P32(g->limit_ms);
    P32(g->target);
    P32(g->evn);
    for (int i = 0; i < MAXP; i++) {
        const bpl_t *b = &g->P[i];
        P32(b->cash);
        P8(b->pos);
        P8(b->jail | (b->out << 2) | (b->away << 3) | (b->cards << 4));
        P8(b->kind);
        P8(b->look);
        P8(b->rank);
        P16(g->ack[i]);
    }
    for (int i = 0; i < NPROP; i++) {
        P8((g->R[i].owner == NOBODY ? 7 : g->R[i].owner) | (g->R[i].level << 3) | (g->R[i].mort << 6));
    }
    const bdebt_t *d = &g->debt[0];
    P8(d->who);
    P8(d->to);
    P8(d->why);
    P32(d->amount);
    P8(g->tr.from);
    P8(g->tr.to);
    P32(g->tr.give);
    P32(g->tr.get);
    P32(g->tr.cash_give);
    P32(g->tr.cash_get);
    P8(g->tr.card_give | (g->tr.card_get << 4));
    return (int)(p - o);
}
// (the reads of the bytes in a fixed order: in "G8() | (G8() << 8)" C does not fix which read comes first)
static uint8_t rd8(const uint8_t **p, const uint8_t *end)
{
    return *p < end ? *(*p)++ : 0;
}
static uint16_t rd16(const uint8_t **p, const uint8_t *end)
{
    const uint16_t lo = rd8(p, end);
    const uint16_t hi = rd8(p, end);
    return (uint16_t)(lo | (hi << 8));
}
static uint32_t rd32(const uint8_t **p, const uint8_t *end)
{
    const uint32_t lo = rd16(p, end);
    const uint32_t hi = rd16(p, end);
    return lo | (hi << 16);
}
static bool unpack_state(bgame_t *g, const uint8_t *o, int len)
{
    const uint8_t *p = o, *end = o + len;
#define G8()  rd8(&p, end)
#define G16() rd16(&p, end)
#define G32() rd32(&p, end)
    if (len < 60) {
        return false;
    }
    uint16_t tid = G16();
    if (tid != s_tid) {
        return false;
    }
    g->magic = GAME_MAGIC;
    g->n = G8();
    g->cur = G8();
    g->phase = G8();
    g->ret = G8();
    g->after = G8();
    g->d1 = G8();
    g->d2 = G8();
    g->dbl = G8();
    g->again = G8();
    g->mode = G8();
    g->winner = G8();
    g->win_why = G8();
    g->card_deck = G8();
    g->card_id = G8();
    g->buy_prop = G8();
    g->ndebt = G8();
    g->turn = G16();
    g->round = G16();
    g->time_ms = G32();
    g->limit_ms = G32();
    g->target = (int32_t)G32();
    g->evn = G32();
    if (g->n < 2 || g->n > MAXP) {
        return false;
    }
    for (int i = 0; i < MAXP; i++) {
        bpl_t *b = &g->P[i];
        b->cash = (int32_t)G32();
        b->pos = G8();
        uint8_t f = G8();
        b->jail = f & 3;
        b->out = (f >> 2) & 1;
        b->away = (f >> 3) & 1;
        b->cards = f >> 4;
        b->kind = G8();
        b->look = G8();
        b->rank = G8();
        g->ack[i] = G16();
    }
    for (int i = 0; i < NPROP; i++) {
        uint8_t v = G8();
        g->R[i].owner = (v & 7) == 7 ? NOBODY : (v & 7);
        g->R[i].level = (v >> 3) & 7;
        g->R[i].mort = (v >> 6) & 1;
    }
    bdebt_t *d = &g->debt[0];
    d->who = G8();
    d->to = G8();
    d->why = G8();
    d->amount = (int32_t)G32();
    g->tr.from = G8();
    g->tr.to = G8();
    g->tr.give = G32();
    g->tr.get = G32();
    g->tr.cash_give = (int32_t)G32();
    g->tr.cash_get = (int32_t)G32();
    uint8_t c = G8();
    g->tr.card_give = c & 15;
    g->tr.card_get = c >> 4;
    return p <= end;
}
static void send_state(void)
{
    uint8_t b[LINK_DATA_MAX];
    int n = pack_state(&g_v, b);
    link_send(LINK_APP, K_STATE, b, n);
    s_last_state = s_now;
}
static void send_events(void)
{
    uint8_t b[LINK_DATA_MAX];
    uint32_t last = g_v.evn, first = last > 23 ? last - 23 : 1;
    if (last == 0) {
        return;
    }
    b[0] = (uint8_t)s_tid;
    b[1] = (uint8_t)(s_tid >> 8);
    memcpy(b + 2, &first, 4);
    int n = 0;
    for (uint32_t e = first; e <= last; e++, n++) {
        const bev_t *v = &s_evlog[e % EVLOG];
        uint8_t *q = b + 7 + n * 8;
        q[0] = v->type;
        q[1] = v->a;
        q[2] = v->b;
        q[3] = v->c;
        memcpy(q + 4, &v->v, 4);
    }
    b[6] = (uint8_t)n;
    link_send(LINK_APP, K_EVENTS, b, 7 + n * 8);
    s_last_events = s_now;
}
static void send_table(void)
{
    uint8_t b[64];
    // (b[3] = 1: the game plays: the lobby shows the table, and a board of a seat comes back to it)
    b[0] = (uint8_t)s_tid;
    b[1] = (uint8_t)(s_tid >> 8);
    b[2] = (uint8_t)s_nseats;
    b[3] = (uint8_t)(s_scr != SCR_ROOM && g_v.magic == GAME_MAGIC);
    for (int i = 0; i < MAXP; i++) {
        b[4 + i] = s_seat_kind[i];
        b[10 + i] = s_seat_look[i];
        memcpy(b + 16 + i * 3, s_seat_mac[i], 3);
    }
    b[34] = (uint8_t)s_setup_mode;
    link_send(LINK_APP, K_TABLE, b, 35);
    s_last_table = s_now;
}
// the client: an intent of its seat (again each 400 ms until the host takes it)
static void send_input(void)
{
    uint8_t b[96];
    b[0] = (uint8_t)s_tid;
    b[1] = (uint8_t)(s_tid >> 8);
    b[2] = (uint8_t)s_my;
    b[3] = (uint8_t)s_in_seq;
    b[4] = (uint8_t)(s_in_seq >> 8);
    b[5] = s_in_wait.kind;
    b[6] = s_in_wait.arg;
    memcpy(b + 7, s_in_wait.plan, NPROP);
    const btrade_t *t = &s_in_wait.tr;
    uint8_t *q = b + 7 + NPROP;
    q[0] = t->from;
    q[1] = t->to;
    memcpy(q + 2, &t->give, 4);
    memcpy(q + 6, &t->get, 4);
    memcpy(q + 10, &t->cash_give, 4);
    memcpy(q + 14, &t->cash_get, 4);
    q[18] = t->card_give;
    q[19] = t->card_get;
    link_send(LINK_APP, K_INPUT, b, 7 + NPROP + 20);
}
static void net_intent(const bin_t *in)
{
    s_in_wait = *in;
    s_in_seq++;
    s_in_pending = true;
    send_input();
}

// ---- the receive: all the packets of this app
static void net_poll(void)
{
    if (!s_net_on) {
        return;
    }
    link_msg_t m;
    while (link_recv(&m)) {
        if (m.app != LINK_APP || m.len < 2) {
            continue;
        }
        uint16_t tid = (uint16_t)(m.data[0] | (m.data[1] << 8));
        if (m.kind == K_TABLE && m.len >= 35) {
            // a table in the room (JOIN shows them); my table: the seats
            int slot = -1, old = 0;
            for (int i = 0; i < NTAB; i++) {
                if (s_tabs[i].tid == tid && s_tabs[i].seen) {
                    slot = i;
                }
            }
            if (slot < 0) {
                for (int i = 0; i < NTAB; i++) {
                    if (!s_tabs[i].seen || s_now - s_tabs[i].seen > 4000) {
                        slot = i;
                        break;
                    }
                }
            }
            if (slot < 0) {
                for (int i = 1; i < NTAB; i++) {
                    if (s_tabs[i].seen < s_tabs[old].seen) {
                        old = i;
                    }
                }
                slot = old;
            }
            table_t *t = &s_tabs[slot];
            t->tid = tid;
            t->n = m.data[2];
            t->started = m.data[3];
            t->seen = s_now;
            for (int i = 0; i < MAXP; i++) {
                t->kind[i] = m.data[4 + i];
                t->look[i] = m.data[10 + i];
                memcpy(t->mac[i], m.data + 16 + i * 3, 3);
            }
            if (s_role == ROLE_CLIENT && tid == s_tid) {
                s_last_rx = s_now;
                s_nseats = t->n;
                memcpy(s_seat_kind, t->kind, MAXP);
                memcpy(s_seat_look, t->look, MAXP);
                memcpy(s_seat_mac, t->mac, sizeof s_seat_mac);
                s_setup_mode = m.data[34];
                s_my = -1;
                for (int i = 0; i < MAXP; i++) {
                    if (s_seat_kind[i] == SK_HUMAN && !memcmp(s_seat_mac[i], s_my_mac + 3, 3)) {
                        s_my = i;
                    }
                }
            }
            continue;
        }
        if (tid != s_tid) {
            continue;
        }
        if (s_role == ROLE_HOST) {
            int seat = -1;
            for (int i = 0; i < MAXP; i++) {
                if (s_seat_kind[i] == SK_HUMAN && !memcmp(s_seat_mac[i], m.mac + 3, 3)) {
                    seat = i;
                }
            }
            if (seat >= 0) {
                // each packet of a board keeps its seat (a board that was away plays again)
                s_seen[seat] = s_now;
                if (s_scr != SCR_ROOM && g_v.magic == GAME_MAGIC && g_v.P[seat].away) {
                    g_v.P[seat].away = 0;
                }
            }
            if (m.kind == K_JOIN && m.len >= 3 && s_scr == SCR_ROOM) {
                if (seat < 0) {
                    for (int i = 0; i < MAXP; i++) {
                        if (s_seat_kind[i] == SK_EMPTY || s_seat_kind[i] == SK_BOT) {
                            seat = i;
                            s_seat_kind[i] = SK_HUMAN;
                            memcpy(s_seat_mac[i], m.mac + 3, 3);
                            if (i >= s_nseats) {
                                s_nseats = i + 1;
                            }
                            break;
                        }
                    }
                }
                if (seat >= 0) {
                    s_seat_look[seat] = m.data[2] % NLOOK;
                    s_seen[seat] = s_now;
                    fx(FX_JOIN);
                }
                send_table();
            } else if (m.kind == K_LEAVE && seat >= 0 && s_scr == SCR_ROOM) {
                s_seat_kind[seat] = SK_EMPTY;
                while (s_nseats > 1 && s_seat_kind[s_nseats - 1] == SK_EMPTY) {
                    s_nseats--;
                }
                send_table();
            } else if (m.kind == K_INPUT && seat >= 0 && m.len >= 7 + NPROP + 20 && m.data[2] == seat) {
                uint16_t seq = (uint16_t)(m.data[3] | (m.data[4] << 8));
                if (seq != g_v.ack[seat]) {
                    bin_t in;
                    memset(&in, 0, sizeof in);
                    in.kind = m.data[5];
                    in.arg = m.data[6];
                    memcpy(in.plan, m.data + 7, NPROP);
                    const uint8_t *q = m.data + 7 + NPROP;
                    in.tr.from = q[0];
                    in.tr.to = q[1];
                    memcpy(&in.tr.give, q + 2, 4);
                    memcpy(&in.tr.get, q + 6, 4);
                    memcpy(&in.tr.cash_give, q + 10, 4);
                    memcpy(&in.tr.cash_get, q + 14, 4);
                    in.tr.card_give = q[18];
                    in.tr.card_get = q[19];
                    g_v.ack[seat] = seq;
                    host_apply(seat, &in);
                }
                send_state();
            }
            continue;
        }
        // the client (only a client: a board in the lobby is LOCAL and its old table number must not start a game)
        if (s_role != ROLE_CLIENT) {
            continue;
        }
        s_last_rx = s_now;
        if (m.kind == K_BYE) {
            s_host_gone = true;
            s_host_paused = m.len >= 3 && m.data[2];
            continue;
        }
        if (m.kind == K_STATE) {
            bgame_t t = g_v;
            if (unpack_state(&t, m.data, m.len)) {
                bool first = g_v.magic != GAME_MAGIC;
                g_v = t;
                if (first) {
                    client_started();
                }
                if (s_in_pending && s_my >= 0 && g_v.ack[s_my] == s_in_seq) {
                    s_in_pending = false;
                }
            }
        } else if (m.kind == K_EVENTS && m.len >= 7) {
            uint32_t first;
            memcpy(&first, m.data + 2, 4);
            int n = m.data[6];
            for (int i = 0; i < n && 7 + i * 8 + 8 <= m.len; i++) {
                const uint8_t *q = m.data + 7 + i * 8;
                bev_t *v = &s_evlog[(first + i) % EVLOG];
                v->type = q[0];
                v->a = q[1];
                v->b = q[2];
                v->c = q[3];
                memcpy(&v->v, q + 4, 4);
            }
            if (first + n - 1 > s_ev_have) {
                s_ev_have = first + n - 1;
            }
        }
    }
}
// the host and the client in each frame: the sends again, the boards that went away
static void net_step(void)
{
    if (!s_net_on) {
        return;
    }
    net_poll();
    if (s_role == ROLE_HOST) {
        if (s_scr == SCR_ROOM) {
            if (s_now - s_last_table > 500) {
                send_table();
            }
            friends_invite(s_now);
            return;
        }
        if (g_v.magic != GAME_MAGIC) {
            return;
        }
        if (s_now - s_last_state > 300) {
            send_state();
        }
        if (s_now - s_last_events > 1000) {
            send_events();
        }
        if (s_now - s_last_table > 2000) {
            send_table();  // (the lobby of the other boards shows the game)
        }
        // a board that sends nothing for 15 s: a bot plays its seat (it can come back)
        for (int i = 0; i < g_v.n; i++) {
            if (s_seat_kind[i] == SK_HUMAN && i != s_my && !g_v.P[i].away && s_now - s_seen[i] > 15000) {
                g_v.P[i].away = 1;
            }
        }
    } else if (s_role == ROLE_CLIENT) {
        static int64_t last_in;
        if (s_in_pending && s_now - last_in > 400) {
            send_input();
            last_in = s_now;
        }
        // the JOIN: in the room it asks for a seat; in the game it keeps the seat (each 2 s)
        if (s_now - s_last_table > (s_scr == SCR_ROOM ? 1000 : 2000)) {
            uint8_t b[3] = {(uint8_t)s_tid, (uint8_t)(s_tid >> 8), (uint8_t)s_my_look};
            link_send(LINK_APP, K_JOIN, b, 3);
            s_last_table = s_now;
        }
        if (s_last_rx && s_now - s_last_rx > 10000) {
            s_host_gone = true;
        }
    } else if (s_scr == SCR_LOBBY) {
        // the list of the tables: the name beacon (1 s). A host with no AP searches the channels; it stays on the
        // channel of this board (on its AP) only when it hears a packet of it, and the list sends nothing else.
        friends_beacon(s_now);
    }
}
