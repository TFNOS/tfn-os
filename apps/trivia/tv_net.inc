// TRIVIA: two boards (included by app_trivia.c). The friends link of net2.h: the HOST runs the round (its level, its
// questions) and sends its state in each frame; the CLIENT shows the same question at the same time and sends its
// answer (again in each frame until the state of the HOST has it). The first right answer gets a bonus.

#define NET_ID      'Q'
#define K_ANS       N2_K_APP
#define K_AGAIN     (N2_K_APP + 1)
#define K_HERE      (N2_K_APP + 2)  // the CLIENT is here (net2 ends the link after 3 s with no packet)
#define NET_SHOW_MS 6500            // the result shows on both boards, then the HOST goes on

typedef struct __attribute__((packed)) {
    uint32_t hash;  // the bank of the HOST (the CLIENT stops when its bank is another one)
    uint8_t st, qp, lvl, cur;
    uint16_t qi, seq;
    uint16_t t_ms, t_total;  // QP_IN, QP_SHOW: the ms since the start of the phase; QP_ASK: the ms left
    int8_t pick[2];          // in QP_ASK and QP_LOCK the answer of the HOST is hidden: 4 = it answered
    int8_t first;
    uint16_t score[2], gain[2];
    uint8_t streak[2], right[2];
} nst_t;
typedef struct __attribute__((packed)) {
    uint16_t seq;
    int8_t pick;
    uint16_t tl;
} nans_t;

static int s_net = N2_OFF;
static uint16_t s_seq;    // the HOST: the count of its questions; the CLIENT: the question of the last state
static int8_t s_cl_pick;  // the CLIENT: its answer, until the state of the HOST has it
static uint16_t s_cl_tl;
static int64_t s_again_t;  // the CLIENT: it asks for a new round (K_AGAIN for 1 s)
static bool s_cl_have;     // the CLIENT: a state of a round came
static int64_t s_here_t;   // the CLIENT: the time of its last packet

static void net_off(void)
{
    if (s_net != N2_OFF) {
        net2_close();
    }
    s_net = N2_OFF;
    s_me = 0;
}
static void net_lobby(int64_t now)
{
    net_off();
    net2_open(NET_ID, 0);
    s_net = N2_LOBBY;
    s_cl_have = false;
    go(ST_LOBBY, now);
}
static uint16_t u16c(int64_t v)
{
    return (uint16_t)(v < 0 ? 0 : v > 65535 ? 65535 : v);
}
static void host_send(int64_t now)
{
    nst_t st = {0};
    st.hash = TV_BANK_HASH;
    st.st = (uint8_t)s_st;
    st.qp = (uint8_t)s_qp;
    st.lvl = (uint8_t)G.lvl;
    st.cur = (uint8_t)G.cur;
    st.qi = G.q[G.cur < ROUND_Q ? G.cur : ROUND_Q - 1];
    st.seq = s_seq;
    st.t_ms = u16c(s_qp == QP_ASK ? q_left(now) : now - s_qp_t0);
    st.t_total = u16c(s_t_total);
    const bool hide = s_qp == QP_ASK || s_qp == QP_LOCK;
    st.pick[0] = hide && G.pick[0] >= 0 ? 4 : G.pick[0];
    st.pick[1] = G.pick[1];
    st.first = (int8_t)G.first;
    for (int p = 0; p < 2; p++) {
        st.score[p] = u16c(G.score[p]);
        st.gain[p] = u16c(G.gain[p]);
        st.streak[p] = (uint8_t)G.streak[p];
        st.right[p] = (uint8_t)G.right[p];
    }
    net2_send_state(&st, sizeof st, now);
}
static int64_t s_rx_now;  // the time of the frame (the callbacks of net2_poll have no time)
static void host_rx(uint8_t kind, const uint8_t *d, int len)
{
    if (kind == K_ANS && len >= (int)sizeof(nans_t)) {
        nans_t a;
        memcpy(&a, d, sizeof a);
        if (a.seq == s_seq && s_st == ST_Q && (s_qp == QP_ASK || s_qp == QP_IN) && G.pick[1] < 0 && a.pick >= 0 &&
            a.pick < 4) {
            const int tl = a.tl > s_t_total ? s_t_total : a.tl;
            q_answer(1, a.pick, tl, s_rx_now);
        }
    } else if (kind == K_AGAIN && s_st == ST_END) {
        s_seq++;
        round_new(MODE_NET, G.lvl, s_rx_now);
    }
}
// the CLIENT: a state of the HOST
static void client_state(const nst_t *st, int64_t now)
{
    if (st->hash != TV_BANK_HASH) {
        go(ST_OTHER, now);
        return;
    }
    if (st->st != ST_Q && st->st != ST_END) {
        return;
    }
    const bool newq = !s_cl_have || st->seq != s_seq;
    if (newq && st->st == ST_Q) {
        s_cl_have = true;
        s_seq = st->seq;
        memset(&G, 0, sizeof G);
        G.mode = MODE_NET;
        G.n = ROUND_Q;
        G.players = 2;
        G.pick[0] = G.pick[1] = -1;
        s_cl_pick = -1;
        G.q[st->cur < ROUND_Q ? st->cur : 0] = st->qi;
        quest_get(st->qi, &s_q);
        seen_mark(st->qi);
        theme_to(s_q.subj);
        part_clear();
        go(ST_Q, now);
        s_qp = -1;
        sfx(FX_WHOOSH, 45);
    }
    G.lvl = st->lvl < 3 ? st->lvl : 0;
    G.cur = st->cur < ROUND_Q ? st->cur : ROUND_Q - 1;
    s_t_total = st->t_total;
    G.pick[0] = st->pick[0];
    G.pick[1] = st->pick[1] >= 0 ? st->pick[1] : s_cl_pick;
    G.first = st->first;
    for (int p = 0; p < 2; p++) {
        G.score[p] = st->score[p];
        G.gain[p] = st->gain[p];
        G.streak[p] = st->streak[p];
        G.right[p] = st->right[p];
        G.bonus[p] = G.first == p ? BASE_PTS[G.lvl] / 2 : 0;
    }
    const int oldqp = s_qp;
    s_qp = st->qp <= QP_SHOW ? st->qp : QP_SHOW;
    s_qp_t0 = s_qp == QP_ASK ? now - (s_t_total - st->t_ms) : now - st->t_ms;
    if (st->st == ST_Q && s_qp == QP_SHOW && oldqp != QP_SHOW) {
        q_reveal_fx();
    }
    if (st->st == ST_END && s_st != ST_END) {
        go(ST_END, now);
        part_clear();
        const bool won = G.score[1] >= G.score[0];
        sfx(won ? FX_WIN : FX_LOSE, 60);
        if (won) {
            confetti(50);
        }
        theme_to(-1);
        save_write();
    }
}
static void client_rx(uint8_t kind, const uint8_t *d, int len)
{
    if (kind == N2_K_STATE && len >= (int)sizeof(nst_t)) {
        nst_t st;
        memcpy(&st, d, sizeof st);
        client_state(&st, s_rx_now);
    }
}
// the CLIENT taps an answer
static void client_answer(int a, int64_t now)
{
    if (s_cl_pick >= 0 || s_qp != QP_ASK) {
        return;
    }
    s_cl_pick = (int8_t)a;
    s_cl_tl = (uint16_t)q_left(now);
    G.pick[1] = (int8_t)a;
    sfx(FX_TAP, 55);
}
// once in each frame; true = this board is the CLIENT of a round (the rules of tv_game.inc do not run here)
static bool net_update(int64_t now)
{
    if (s_net == N2_OFF) {
        return false;
    }
    s_rx_now = now;
    const n2_event_t e = net2_poll(now, s_net == N2_CLIENT ? client_rx : host_rx, NULL);
    s_net = net2_role();
    if (e == N2_EV_LOST) {
        s_lost_t = now;
        net_off();
        theme_to(-1);
        go(ST_TITLE, now);
        return true;
    }
    if (e == N2_EV_PEER_LOBBY) {
        net_lobby(now);  // the other board left the round: this board looks for it again
        theme_to(-1);
        return true;
    }
    if (e == N2_EV_PAIRED) {
        s_me = s_net == N2_HOST ? 0 : 1;
        if (s_net == N2_HOST) {
            s_seq++;
            round_new(MODE_NET, s_lvl, now);
        } else {
            s_cl_have = false;
            quest_get(-1, &s_q);
            go(ST_Q, now);
            s_qp = QP_IN;
            s_qp_t0 = now;
            memset(&G, 0, sizeof G);
            G.mode = MODE_NET;
            G.pick[0] = G.pick[1] = -1;
        }
    }
    if (s_net == N2_HOST) {
        net2_rec(true);
        return false;
    }
    if (s_net == N2_CLIENT) {
        if (s_cl_pick >= 0 && s_qp == QP_ASK) {
            const nans_t a = {s_seq, s_cl_pick, s_cl_tl};
            net2_send(K_ANS, &a, sizeof a);
        }
        if (s_again_t && now - s_again_t < 1000) {
            net2_send(K_AGAIN, NULL, 0);
            s_here_t = now;
        }
        if (now - s_here_t >= 250) {
            net2_send(K_HERE, NULL, 0);
            s_here_t = now;
        }
        return true;
    }
    return false;
}
// the HOST: after the frame of the rules, the state goes out
static void net_after(int64_t now)
{
    if (s_net != N2_HOST || (s_st != ST_Q && s_st != ST_END)) {
        return;
    }
    if (s_st == ST_Q && s_qp == QP_SHOW && now - s_qp_t0 >= NET_SHOW_MS) {
        q_next(now);
        if (s_st == ST_Q) {
            s_seq++;
        } else {
            save_write();
        }
    }
    host_send(now);
}
