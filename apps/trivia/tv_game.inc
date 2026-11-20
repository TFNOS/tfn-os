// TRIVIA: the rules of a round (included by app_trivia.c): 10 questions, a timer, points for the speed, a streak.
// MODE_SOLO = one player; MODE_PASS = two players on one board (they take turns: each one answers every second
// question); MODE_NET = two boards (tv_net.inc: the same question on both, the first right answer gets a bonus).

enum {
    ST_TITLE,
    ST_LEVEL,
    ST_LOBBY,
    ST_PASS,
    ST_Q,
    ST_END,
    ST_NOMEM,
    ST_OTHER
};
enum {
    QP_IN,
    QP_ASK,
    QP_LOCK,
    QP_SHOW
};
#define IN_MS   900    // the question comes in (the card slides, the answers pop); the timer waits
#define LOCK_MS 450    // the chosen answer glows before the right answer shows
#define NEXT_MS 700    // in the result: a tap goes on only after this time
#define AUTO_MS 15000  // in the result: the next question comes by itself after this time
static const int BASE_PTS[3] = {100, 150, 200};
static const int TIME_MS[3] = {20000, 25000, 30000};

typedef struct {
    int mode, lvl, n, cur, players;
    uint16_t q[ROUND_Q];
    int score[2], streak[2], right[2], best_streak[2], gain[2], bonus[2];
    int8_t pick[2];  // the answer of each player to this question (-1: none yet)
    int tl[2];       // the ms left on the timer at the answer of each player
    int first;       // MODE_NET: the player who gave the first right answer (-1: none)
    uint8_t res[ROUND_Q];
} game_t;
static game_t G;

static int s_st;         // ST_*
static int64_t s_st_t0;  // the start of the state now
static int s_qp;         // QP_*
static int64_t s_qp_t0;  // the start of the phase now
static int s_t_total;    // the ms of the timer of this question
static quest_t s_q;      // the question now
static int s_me;         // the player of this board (0; the CLIENT of two boards: 1)
static int s_lvl;        // the level that the player chose (0..2)
static int s_mode_pick;  // the mode that the title chose (MODE_*), before the choice of the level
static bool s_new_best;
static int64_t s_lost_t;     // the link to the other board ended at this time (a banner for 3 s)
static int s_time_mul = 10;  // the panel TIME: 10 = normal, 16 = long (tenths)
static bool s_sound = true;

static void sfx(int id, int vol)
{
    if (s_sound) {
        tfsfx_play(id, vol, 1.0f);
    }
}
static void go(int st, int64_t now)
{
    s_st = st;
    s_st_t0 = now;
}
// the player who answers now (MODE_PASS: the turns; the other modes: this board)
static int active_player(void)
{
    return G.mode == MODE_PASS ? G.cur % 2 : s_me;
}
static int round10(int v)
{
    return (v + 5) / 10 * 10;
}
// the points of a right answer: the base of the level, the speed, the streak
static int points(int lvl, int tl, int total, int streak_after)
{
    int p = BASE_PTS[lvl] + (total > 0 ? BASE_PTS[lvl] * tl / total : 0);
    p = round10(p);
    if (streak_after >= 3) {
        const int b = 50 * (streak_after - 2);
        p += b > 250 ? 250 : b;
    }
    return p;
}

static void save_round(void)
{
    if (G.mode == MODE_NET) {
        return;
    }
    S.r_on = G.cur < G.n ? 1 : 0;
    S.r_mode = (uint8_t)G.mode;
    S.r_lvl = (uint8_t)G.lvl;
    S.r_cur = (uint8_t)(G.cur < ROUND_Q ? G.cur : 0);
    memcpy(S.r_q, G.q, sizeof S.r_q);
    for (int p = 0; p < 2; p++) {
        S.r_score[p] = (uint16_t)G.score[p];
        S.r_streak[p] = (uint8_t)G.streak[p];
        S.r_right[p] = (uint8_t)G.right[p];
        S.r_best_streak[p] = (uint8_t)G.best_streak[p];
    }
    memcpy(S.r_res, G.res, sizeof S.r_res);
    save_write();
}
static void q_begin(int64_t now);
// a new round of the mode and the level (MODE_SOLO, MODE_PASS; the HOST of MODE_NET too)
static void round_new(int mode, int lvl, int64_t now)
{
    memset(&G, 0, sizeof G);
    G.mode = mode;
    G.lvl = lvl;
    G.n = ROUND_Q;
    G.players = mode == MODE_SOLO ? 1 : 2;
    G.first = -1;
    round_pick(lvl, G.q, G.n);
    s_new_best = false;
    save_round();
    if (mode == MODE_PASS) {
        go(ST_PASS, now);
    } else {
        q_begin(now);
    }
}
// CONTINUE: the round of the save, from its next question
static void round_continue(int64_t now)
{
    memset(&G, 0, sizeof G);
    G.mode = S.r_mode;
    G.lvl = S.r_lvl;
    G.n = ROUND_Q;
    G.cur = S.r_cur;
    G.players = G.mode == MODE_SOLO ? 1 : 2;
    G.first = -1;
    memcpy(G.q, S.r_q, sizeof G.q);
    for (int p = 0; p < 2; p++) {
        G.score[p] = S.r_score[p];
        G.streak[p] = S.r_streak[p];
        G.right[p] = S.r_right[p];
        G.best_streak[p] = S.r_best_streak[p];
    }
    memcpy(G.res, S.r_res, sizeof G.res);
    s_new_best = false;
    if (G.mode == MODE_PASS) {
        go(ST_PASS, now);
    } else {
        q_begin(now);
    }
}
// the round stops before its end (the panel QUIT): the questions that did not show go back to the level
static void round_drop(void)
{
    if (G.mode != MODE_NET && G.n) {
        for (int k = G.cur + 1; k < G.n; k++) {
            const int i = G.q[k] - LVL_FIRST[G.lvl];
            if (i >= 0 && i < LVL_N[G.lvl]) {
                seen_set(G.lvl, i, false);
            }
        }
    }
    S.r_on = 0;
    save_write();
}

static void q_begin(int64_t now)
{
    quest_get(G.q[G.cur], &s_q);
    theme_to(s_q.subj);
    G.pick[0] = G.pick[1] = -1;
    G.tl[0] = G.tl[1] = 0;
    G.gain[0] = G.gain[1] = 0;
    G.bonus[0] = G.bonus[1] = 0;
    G.first = -1;
    s_t_total = TIME_MS[G.lvl] * s_time_mul / 10;
    s_qp = QP_IN;
    s_qp_t0 = now;
    go(ST_Q, now);
    sfx(FX_WHOOSH, 45);
}
// the ms left on the timer now
static int q_left(int64_t now)
{
    if (s_qp == QP_IN) {
        return s_t_total;
    }
    if (s_qp != QP_ASK) {
        return 0;
    }
    const int l = s_t_total - (int)(now - s_qp_t0);
    return l < 0 ? 0 : l;
}
// player p answers (place a, or -1 = no answer); the result comes after the lock
static void q_answer(int p, int a, int tl, int64_t now)
{
    if (G.pick[p] >= 0) {
        return;
    }
    G.pick[p] = (int8_t)a;
    G.tl[p] = tl;
    if (p == s_me || G.mode == MODE_PASS) {
        sfx(FX_TAP, 55);
    }
}
static void q_judge(int64_t now);
// the result: the points, the streak, the sounds, the sparks (the boxes are in the layout: tv_ui.inc)
static void q_reveal(int64_t now)
{
    s_qp = QP_SHOW;
    s_qp_t0 = now;
    q_judge(now);
    seen_mark(G.q[G.cur]);
    if (G.mode != MODE_NET) {
        G.cur++;  // the save goes on from the next question (CONTINUE)
        S.asked++;
        S.right += G.res[G.cur - 1] == 1;
        if (G.cur >= G.n) {
            S.rounds++;
            const int sc = G.mode == MODE_SOLO ? G.score[0] : 0;
            if (G.mode == MODE_SOLO && sc > (int)S.best[G.lvl]) {
                S.best[G.lvl] = (uint32_t)sc;
                s_new_best = true;
            }
        }
        save_round();
        G.cur--;  // (the screen shows this question until the tap)
    }
}
static void box_center(int p, int *cx, int *cy);
// the points of each player who answered (a right answer: the base, the speed, the streak; MODE_NET: the first right)
static void q_judge(int64_t now)
{
    (void)now;
    int first = -1, best_tl = -1;
    for (int p = 0; p < G.players; p++) {
        if (G.mode == MODE_PASS && p != active_player()) {
            continue;
        }
        const bool ok = G.pick[p] == s_q.right;
        if (ok) {
            G.streak[p]++;
            if (G.streak[p] > G.best_streak[p]) {
                G.best_streak[p] = G.streak[p];
            }
            G.right[p]++;
            G.gain[p] = points(G.lvl, G.tl[p], s_t_total, G.streak[p]);
            if (G.tl[p] > best_tl) {
                best_tl = G.tl[p];
                first = p;
            }
        } else {
            G.streak[p] = 0;
            G.gain[p] = 0;
        }
        if (G.mode != MODE_NET || p == 0) {
            G.res[G.cur] = ok ? 1 : (G.pick[p] < 0 ? 3 : 2);
        }
    }
    if (G.mode == MODE_NET && first >= 0) {
        G.first = first;
        G.bonus[first] = BASE_PTS[G.lvl] / 2;
        G.gain[first] += G.bonus[first];
    }
    for (int p = 0; p < 2; p++) {
        G.score[p] += G.gain[p];
    }
}
// the sounds and the sparks of the result, on this board (also on the CLIENT: tv_net.inc calls it)
static void q_reveal_fx(void)
{
    const int p = G.mode == MODE_PASS ? active_player() : s_me;
    int cx, cy;
    box_center(s_q.right, &cx, &cy);
    if (G.pick[p] == s_q.right) {
        sfx(FX_RIGHT, 70);
        burst(cx, cy - 26, C_GOOD);  // (from the top of the box: the sparks fly over its text, not on it)
        if (G.streak[p] >= 3) {
            sfx(FX_STREAK, 45);
        }
        if (G.mode == MODE_NET && G.first == p) {
            sfx(FX_FIRST, 60);
        }
    } else {
        sfx(FX_WRONG, 55);
    }
}
// after the result: the next question, the next player, or the end
static void q_next(int64_t now)
{
    G.cur++;
    part_clear();
    if (G.cur >= G.n) {
        go(ST_END, now);
        const bool good = G.mode == MODE_SOLO  ? G.right[0] >= 6
                          : G.mode == MODE_NET ? G.score[s_me] >= G.score[1 - s_me]
                                               : true;
        sfx(good ? FX_WIN : FX_LOSE, 60);
        if (good) {
            confetti(G.mode == MODE_SOLO && !s_new_best ? 30 : 60);
        }
        theme_to(-1);
        return;
    }
    if (G.mode == MODE_PASS) {
        go(ST_PASS, now);
        theme_to(-1);
    } else {
        q_begin(now);
    }
}
static int64_t s_tick_s;  // the last second of the tick sound
// the clock of a question (MODE_SOLO, MODE_PASS, the HOST of MODE_NET)
static void q_update(int64_t now)
{
    if (s_qp == QP_IN && now - s_qp_t0 >= IN_MS) {
        s_qp = QP_ASK;
        s_qp_t0 = now;
        s_tick_s = 99;
    }
    if (s_qp == QP_ASK) {
        const int left = q_left(now);
        const bool all = G.mode == MODE_NET ? (G.pick[0] >= 0 && G.pick[1] >= 0) : G.pick[active_player()] >= 0;
        if (all) {
            s_qp = QP_LOCK;
            s_qp_t0 = now;
        } else if (left <= 0) {
            for (int p = 0; p < G.players; p++) {
                if (G.pick[p] < 0 && (G.mode != MODE_PASS || p == active_player())) {
                    G.pick[p] = -1;
                }
            }
            q_reveal(now);
            q_reveal_fx();
        } else if (left <= 5000 && left / 1000 != s_tick_s) {
            s_tick_s = left / 1000;
            sfx(FX_TICK, 35);
        }
    } else if (s_qp == QP_LOCK && now - s_qp_t0 >= LOCK_MS) {
        q_reveal(now);
        q_reveal_fx();
    } else if (s_qp == QP_SHOW && now - s_qp_t0 >= AUTO_MS && G.mode != MODE_NET) {
        q_next(now);
    }
}
