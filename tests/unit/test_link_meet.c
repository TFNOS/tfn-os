// The unit test of the meeting of two boards on one radio channel (main/link_meet.c). Part 1: the rules (the search
// pattern, the hold, the agreement, the visits in a hold, the slow wait, a peer that does not move). Part 2: a simulation
// of 2 or 3 boards with a radio model: a board hears a packet on its own channel, and on the next channel only with a
// chance (P_NEXT, as a real radio at a short distance); no packet comes from 2 channels away. Each board sends its packet each period (a 2 BOARDS HELLO 250 ms, a turn-game invite 500 ms)
// and again at each arrival of a search step (link.c meet_resend). The scenes are the scenes of the board matrix:
// both with no AP (one home, two homes), one board on an AP, adjacent channels, an older firmware that does not move,
// a third board that holds the meeting channel while the friend is on an AP, a change of the WiFi in a match (a board
// loses its AP, gets it again, or joins an AP on the channel of the pair: the gap must be less than the 3 s end of
// net2), and the AP channels 1..13; each scene
// runs with many random starts. PASS: each run pairs (each board hears the other on ONE channel) in the time limit, and
// then the two boards stay on one channel.
// Build and run: python tests/unit/run.py.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "link_meet.h"

static int s_fail, s_n;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        s_n++;                                                                                                         \
        if (!(c)) {                                                                                                    \
            s_fail++;                                                                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                                        \
        }                                                                                                              \
    } while (0)

// ---------------------------------------------------------------- part 1: the rules

// one step at time t; returns the channel (0 = stay) and moves the state there
static int step(link_meet_t *m, uint32_t t, bool can, bool *search)
{
    const int ch = link_meet_step(m, (can ? LINK_MEET_F_MOVE : 0u) | LINK_MEET_F_SEARCH, t, search);
    if (ch) {
        link_meet_moved(m, ch);
    }
    return ch;
}

static void test_pattern(void)
{
    link_meet_t m;
    memset(&m, 0, sizeof m);
    bool search = false;
    // a board with no AP on channel 3 (its last AP): at once to the meeting channel, then channel 3 first, then the
    // visits in their order
    const uint32_t W = LINK_MEET_WAIT_MS + LINK_MEET_JITTER;  // (the longest wait)
    link_meet_reset(&m, 3, 1000, 7);
    CHECK(step(&m, 1000, true, &search) == LINK_MEET_CH && search);
    CHECK(step(&m, 1000 + LINK_MEET_WAIT_MS - 1, true, &search) == 0);
    CHECK(step(&m, 1000 + W, true, &search) == 3 && search);
    CHECK(step(&m, 1000 + W + LINK_MEET_VISIT_MS - 1, true, &search) == 0);
    CHECK(step(&m, 1000 + W + LINK_MEET_VISIT_MS, true, &search) == LINK_MEET_CH);
    CHECK(step(&m, 1000 + 2 * W + LINK_MEET_VISIT_MS, true, &search) == 6);
    // the waits on the meeting channel are not all the same (the jitter)
    {
        link_meet_t j;
        memset(&j, 0, sizeof j);
        link_meet_reset(&j, 5, 0, 12345);
        uint32_t t = 0, mn = 99999, mx = 0, at = 0;
        for (int i = 0; i < 400; i++) {
            t += 5;
            bool sr;
            const int ch = step(&j, t, true, &sr);
            if (ch == LINK_MEET_CH) {
                at = t;
            } else if (ch && at) {
                mn = t - at < mn ? t - at : mn;
                mx = t - at > mx ? t - at : mx;
            }
        }
        CHECK(mn >= LINK_MEET_WAIT_MS && mx < LINK_MEET_WAIT_MS + LINK_MEET_JITTER + 5 && mx > mn);
        CHECK(LINK_MEET_WAIT_MS >= LINK_MEET_VISIT_MS);  // (two waits always overlap)
    }
    // one round visits each channel 2..13 one time
    int seen[14] = {0};
    uint32_t t = 5000;
    for (int i = 0; i < 40; i++) {
        t += 1000;
        const int ch = step(&m, t, true, &search);
        if (ch && ch != LINK_MEET_CH) {
            seen[ch]++;
        }
    }
    for (int c = 2; c <= 13; c++) {
        CHECK(seen[c] >= 1);
    }
    CHECK(seen[1] == 0);
    // a board that cannot move (an AP, a connect, a phone on the hotspot): no step
    link_meet_reset(&m, 3, 1000, 7);
    CHECK(step(&m, 1000, false, &search) == 0 && m.ch == 3);
    // a start on the meeting channel: it waits there first
    m.last = 0;
    link_meet_reset(&m, LINK_MEET_CH, 1000, 7);
    CHECK(step(&m, 1000, true, &search) == 0);
    CHECK(step(&m, 1000 + W, true, &search) == 6);
}

static void test_hold_and_agree(void)
{
    link_meet_t m;
    memset(&m, 0, sizeof m);
    bool search = false;
    // a peer on this channel: no search for the hold time
    link_meet_reset(&m, LINK_MEET_CH, 0, 7);
    link_meet_heard(&m, LINK_MEET_CH, false, false, 100);
    CHECK(step(&m, 100 + LINK_MEET_HOLD_MS - 1, true, &search) == 0 && m.ch == LINK_MEET_CH);
    CHECK(step(&m, 100 + LINK_MEET_HOLD_MS, true, &search) != 0 && search);  // (the peer is gone: search again)
    // the answer: a new peer with no AP gets one; a peer on an AP, or a peer heard in the last second, does not
    link_meet_reset(&m, LINK_MEET_CH, 0, 7);
    CHECK(link_meet_heard(&m, LINK_MEET_CH, false, false, 100));
    CHECK(!link_meet_heard(&m, LINK_MEET_CH, false, false, 600));
    CHECK(link_meet_heard(&m, LINK_MEET_CH, false, false, 600 + LINK_MEET_ANSWER_MS + 1));
    link_meet_reset(&m, 6, 0, 7);
    CHECK(!link_meet_heard(&m, 6, true, false, 100));
    link_meet_reset(&m, 3, 0, 7);
    CHECK(link_meet_heard(&m, 3, false, true, 100));  // (a board on an AP answers a visitor)
    // a packet that came after the time of the step (the task of the radio): a hold, not a search
    link_meet_reset(&m, LINK_MEET_CH, 0, 7);
    link_meet_heard(&m, LINK_MEET_CH, false, false, 5005);
    CHECK(step(&m, 5000, true, &search) == 0 && m.ch == LINK_MEET_CH);
    // a peer on an AP: go to its channel, also when it is lower or higher
    link_meet_reset(&m, 5, 0, 7);
    link_meet_heard(&m, 9, true, false, 10);
    CHECK(step(&m, 10, true, &search) == 9 && !search);
    // the channel of the last peer is the first visit of the next round
    m.heard = false;
    CHECK(step(&m, 20, true, &search) == LINK_MEET_CH);
    CHECK(step(&m, 20 + LINK_MEET_WAIT_MS + LINK_MEET_JITTER, true, &search) == 9);
    // two boards with no AP: both go to the meeting channel (also when they hear each other on one channel)
    link_meet_reset(&m, 8, 0, 7);
    link_meet_heard(&m, 7, false, false, 10);
    CHECK(step(&m, 10, true, &search) == LINK_MEET_CH && !search);
    link_meet_reset(&m, 7, 0, 7);
    link_meet_heard(&m, 7, false, false, 10);
    CHECK(step(&m, 10, true, &search) == LINK_MEET_CH && !search);
    // on the meeting channel: a board on the next channel (it comes here): a short hold only
    link_meet_reset(&m, LINK_MEET_CH, 0, 7);
    link_meet_heard(&m, LINK_MEET_CH + 1, false, false, 10);
    CHECK(step(&m, 10 + LINK_MEET_SHORT_MS - 1, true, &search) == 0 && m.ch == LINK_MEET_CH);
    CHECK(step(&m, 10 + LINK_MEET_SHORT_MS + LINK_MEET_JITTER, true, &search) != 0 && search);
    // a short hold does not cut a full hold that came first
    link_meet_reset(&m, LINK_MEET_CH, 0, 7);
    link_meet_heard(&m, LINK_MEET_CH, false, false, 10);
    link_meet_heard(&m, LINK_MEET_CH + 1, false, false, 20);
    CHECK(step(&m, 10 + LINK_MEET_HOLD_MS - 1, true, &search) == 0);
    // this board on an AP: no agreement, no move
    link_meet_reset(&m, 3, 0, 7);
    link_meet_heard(&m, 6, true, true, 10);
    CHECK(step(&m, 10, false, &search) == 0 && m.ch == 3 && m.want == 0);
    // a channel byte that is not known (0, 14, 15): a hold, no move
    link_meet_reset(&m, 4, 0, 7);
    link_meet_heard(&m, 0, false, false, 10);
    link_meet_heard(&m, 15, true, false, 10);
    CHECK(step(&m, 10, true, &search) == 0 && m.ch == 4);
    // a pair stays: this board on its AP on channel 6 with a peer with no AP there loses the AP; the peer on channel 6
    // in the hold does not move it
    link_meet_reset(&m, 6, 0, 7);
    link_meet_heard(&m, 6, false, true, 10);
    link_meet_heard(&m, 6, false, false, 300);
    CHECK(step(&m, 300, true, &search) == 0 && m.ch == 6 && m.want == 0);
    // the same with the peer on the AP that goes: the board with no AP stays
    link_meet_reset(&m, 6, 0, 7);
    link_meet_heard(&m, 6, true, false, 10);
    link_meet_heard(&m, 6, false, false, 300);
    CHECK(step(&m, 300, true, &search) == 0 && m.ch == 6 && m.want == 0);
    // but the first packet of a peer with no AP on a visit channel (no hold there): the meeting channel
    link_meet_reset(&m, 6, 0, 7);
    link_meet_heard(&m, 6, false, false, 10);
    CHECK(m.want == LINK_MEET_CH);
    // an agreed channel waits while the radio cannot move, then goes
    link_meet_reset(&m, 8, 0, 7);
    link_meet_heard(&m, 2, true, false, 10);
    CHECK(step(&m, 10, false, &search) == 0 && m.ch == 8);
    CHECK(step(&m, 20, true, &search) == 2);
}

static void test_seek_slow_stray(void)
{
    link_meet_t m;
    memset(&m, 0, sizeof m);
    bool search = false;
    const unsigned F = LINK_MEET_F_MOVE | LINK_MEET_F_SEARCH;
    // SEEK: a hold on the meeting channel (other boards in the same app) still visits one channel each
    // LINK_MEET_SEEK_MS, then comes back to the home channel; with no SEEK flag the hold stays
    link_meet_reset(&m, LINK_MEET_CH, 0, 7);
    uint32_t t = 10;
    link_meet_heard(&m, LINK_MEET_CH, false, false, t);
    CHECK(link_meet_step(&m, F, LINK_MEET_SEEK_MS + 5, &search) == 0);
    CHECK(link_meet_step(&m, F | LINK_MEET_F_SEEK, LINK_MEET_SEEK_MS + 5, &search) == 0);
    t = 10 + LINK_MEET_SEEK_MS;  // (the hold started at 10)
    link_meet_heard(&m, LINK_MEET_CH, false, false, t);
    int c = link_meet_step(&m, F | LINK_MEET_F_SEEK, t, &search);
    CHECK(c == 6 && search && m.home == LINK_MEET_CH);
    link_meet_moved(&m, c);
    CHECK(link_meet_step(&m, F | LINK_MEET_F_SEEK, t + LINK_MEET_VISIT_MS - 1, &search) == 0);
    c = link_meet_step(&m, F | LINK_MEET_F_SEEK, t + LINK_MEET_VISIT_MS, &search);
    CHECK(c == LINK_MEET_CH && search && m.home == 0);
    link_meet_moved(&m, c);
    // a visit in a hold that hears a board on an AP there: it stays (the friend that it looks for)
    t += LINK_MEET_SEEK_MS;
    link_meet_heard(&m, LINK_MEET_CH, false, false, t);
    c = link_meet_step(&m, F | LINK_MEET_F_SEEK, t, &search);
    CHECK(c == 11);
    link_meet_moved(&m, c);
    link_meet_heard(&m, 11, true, false, t + 100);
    CHECK(link_meet_step(&m, F | LINK_MEET_F_SEEK, t + LINK_MEET_VISIT_MS + 10, &search) == 0 && m.ch == 11);
    // a visit in a hold that hears a board with no AP from the next channel: the way back stays
    link_meet_reset(&m, LINK_MEET_CH, 0, 7);
    link_meet_heard(&m, 9, true, false, 10);
    CHECK(link_meet_step(&m, F, 10, &search) == 9);
    link_meet_moved(&m, 9);
    t = 10 + LINK_MEET_SEEK_MS;
    link_meet_heard(&m, 9, true, false, t);
    // (a hold on another channel: the first visit is the meeting channel; other boards there do not keep this board)
    c = link_meet_step(&m, F | LINK_MEET_F_SEEK, t, &search);
    CHECK(c == LINK_MEET_CH && m.home == 9);
    link_meet_moved(&m, c);
    link_meet_heard(&m, LINK_MEET_CH, false, false, t + 100);
    link_meet_heard(&m, LINK_MEET_CH + 1, false, false, t + 120);
    CHECK(m.want == 0 && m.home == 9);
    CHECK(link_meet_step(&m, F | LINK_MEET_F_SEEK, t + LINK_MEET_VISIT_MS, &search) == 9);
    link_meet_moved(&m, 9);
    // (the next visit in the hold is another channel, not the meeting channel and not 9)
    t += LINK_MEET_SEEK_MS;
    link_meet_heard(&m, 9, true, false, t);
    c = link_meet_step(&m, F | LINK_MEET_F_SEEK, t + LINK_MEET_SEEK_MS, &search);
    CHECK(c != 9 && c != LINK_MEET_CH && m.home == 9);
    // SLOW (the hotspot is on): the wait on the meeting channel is LINK_MEET_SLOW_MS
    link_meet_reset(&m, 3, 0, 7);
    CHECK(link_meet_step(&m, F | LINK_MEET_F_SLOW, 0, &search) == LINK_MEET_CH);
    link_meet_moved(&m, LINK_MEET_CH);
    CHECK(link_meet_step(&m, F | LINK_MEET_F_SLOW, LINK_MEET_SLOW_MS - 1, &search) == 0);
    CHECK(link_meet_step(&m, F | LINK_MEET_F_SLOW, LINK_MEET_SLOW_MS + LINK_MEET_JITTER, &search) == 3);
    // a peer with no AP that does not move (an older firmware on channel 8): the first visit that hears it goes to the
    // meeting channel, the next visit is channel 8 again; the second visit that hears it makes 8 the agreed channel
    link_meet_reset(&m, 4, 0, 7);
    CHECK(link_meet_step(&m, F, 0, &search) == LINK_MEET_CH);
    link_meet_moved(&m, LINK_MEET_CH);
    m.k = 6;  // (the visit of channel 8 is next)
    c = link_meet_step(&m, F, 1000, &search);
    CHECK(c == 8);
    link_meet_moved(&m, c);
    link_meet_heard(&m, 8, false, false, 1100);
    link_meet_heard(&m, 8, false, false, 1300);  // (the same visit: one count)
    CHECK(m.stray_n == 1 && m.fixed == 0);
    CHECK(link_meet_step(&m, F, 1400, &search) == LINK_MEET_CH);
    link_meet_moved(&m, LINK_MEET_CH);
    t = 1300 + LINK_MEET_HOLD_MS;
    c = link_meet_step(&m, F, t, &search);
    CHECK(c == 8);
    link_meet_moved(&m, c);
    link_meet_heard(&m, 8, false, false, t + 100);
    CHECK(m.fixed == 8 && m.want == 0);
    CHECK(link_meet_step(&m, F, t + LINK_MEET_VISIT_MS + 100, &search) == 0 && m.ch == 8);
    link_meet_heard(&m, 8, false, false, t + 3000);
    CHECK(link_meet_step(&m, F, t + 3100, &search) == 0 && m.ch == 8);
    // a peer that comes to the meeting channel moves: no agreement on its visit channel
    link_meet_reset(&m, 4, 0, 7);
    link_meet_moved(&m, 8);
    link_meet_heard(&m, 8, false, false, 100);
    CHECK(m.stray_n == 1);
    link_meet_moved(&m, LINK_MEET_CH);
    link_meet_heard(&m, LINK_MEET_CH, false, false, 300);
    CHECK(m.stray_n == 0);
}

// ---------------------------------------------------------------- part 2: the simulation of 2 or 3 boards

#define SIM_TICK 5  // ms
#define SIM_MAX  3
typedef struct {
    link_meet_t m;
    uint32_t open;   // the time when the app opens (no send, no step before)
    bool ap;         // on a WiFi network: the channel stays
    bool still;      // an older firmware: it does not search and does not move
    bool seek;       // a lobby waits: LINK_MEET_F_SEEK until the pair
    int period;      // ms between two packets of the app
    uint32_t next;   // the next send
    uint32_t heard;  // the last packet of board 0 or 1 on the own channel (0 = none)
    bool answering;  // (an answer does not answer an answer at once)
} sim_board_t;

static uint32_t s_rng = 1;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}

#define P_NEXT 30  // percent: a packet on the next channel comes (a short distance)
#define P_SAME 95  // percent: a packet on the same channel comes

static sim_board_t s_b[SIM_MAX];
static int s_nb;

// board i sends: each other board hears it (the channel model). Each packet is a packet of a peer (the worst case: the
// third board is in the same app).
static void sim_send(int i, uint32_t t)
{
    sim_board_t *a = &s_b[i];
    for (int j = 0; j < s_nb; j++) {
        sim_board_t *b = &s_b[j];
        if (j == i || (int32_t)(t - b->open) < 0) {
            continue;  // (the app of b is not open)
        }
        const int d = abs(a->m.ch - b->m.ch);
        const int p = d == 0 ? P_SAME : d == 1 ? P_NEXT : 0;
        if ((int)(rnd() % 100) >= p) {
            continue;
        }
        if (d == 0 && i < 2 && j < 2) {
            b->heard = t ? t : 1;
        }
        if (b->still) {
            continue;
        }
        const bool answer = link_meet_heard(&b->m, a->m.ch, a->ap, b->ap, t);
        if (answer && !b->answering) {
            b->answering = true;  // (link.c meet_tick: the last packets again)
            sim_send(j, t);
            b->answering = false;
        }
    }
}

// one tick of board i (its step, its send)
static void sim_tick(int i, uint32_t t)
{
    sim_board_t *a = &s_b[i];
    if ((int32_t)(t - a->open) < 0) {
        return;
    }
    if (!a->still) {
        bool search = false;
        const unsigned f = (a->ap ? 0u : LINK_MEET_F_MOVE) | LINK_MEET_F_SEARCH | (a->seek ? LINK_MEET_F_SEEK : 0u);
        const int ch = link_meet_step(&a->m, f, t, &search);
        if (ch) {
            link_meet_moved(&a->m, ch);
            if (search) {
                sim_send(i, t);  // (the last packet again on the new channel)
            }
        }
    }
    if ((int32_t)(t - a->next) >= 0) {
        a->next = t + (uint32_t)a->period;
        sim_send(i, t);
    }
}

typedef struct {
    const char *name;
    bool ap_a, ap_b;
    int ch_a, ch_b;  // the start channels (0 = random 1..13)
    int period;
    int limit_ms;  // the pair must come in this time
    bool still_b;  // board b is an older firmware (no search; on ch_b, or on a random channel 2..13)
    int third;     // a third board of the same app on this channel (0 = none): it sends each 15 ms, it does not move
    bool seek;     // the boards are in a lobby (LINK_MEET_F_SEEK until the pair)
    int change;    // a change of the WiFi in the match (SC_*; 0 = none)
} scene_t;

// the changes of the WiFi in a match: CHANGE_MS after the pair; the gap of each board after it must be less than the
// 3 s end of net2 (GAP_MAX_MS)
enum {
    SC_A_LOSES = 1,  // board A loses its AP
    SC_A_BACK,       // board A loses its AP, and CHANGE_MS later it joins it again on the same channel
    SC_B_JOINS,      // board B (no AP) joins an AP on the channel of the pair
};
#define CHANGE_MS  3000
#define GAP_MAX_MS 3000

// one run: the time of the pair (ms), or -1; *apart: the ticks on two channels after the pair (and the end of a visit);
// *gap: the longest time after the change of the WiFi (sc->change) in which a board heard no packet of the other board
static int sim_run(const scene_t *sc, int *apart, int *gap)
{
    memset(s_b, 0, sizeof s_b);
    s_nb = sc->third ? 3 : 2;
    int ch[3] = {sc->ch_a ? sc->ch_a : 1 + (int)(rnd() % 13), sc->ch_b ? sc->ch_b : 1 + (int)(rnd() % 13), sc->third};
    if (sc->still_b && !sc->ch_b) {
        ch[1] = 2 + (int)(rnd() % 12);
    }
    const bool ap[3] = {sc->ap_a, sc->ap_b, false};
    const uint32_t t0 = 1000 + rnd() % 997;
    for (int i = 0; i < s_nb; i++) {
        s_b[i].ap = ap[i];
        s_b[i].still = (i == 1 && sc->still_b) || i == 2;
        s_b[i].seek = sc->seek && i < 2;
        s_b[i].period = i == 2 ? 15 : sc->period;
        s_b[i].open = i == 2 ? 0 : t0 + (uint32_t)(i ? rnd() % 3000 : 0);  // (the friend opens the game later)
        s_b[i].next = s_b[i].open + rnd() % (uint32_t)s_b[i].period;
        link_meet_reset(&s_b[i].m, ch[i], s_b[i].open, rnd());
    }
    const uint32_t t_both = s_b[1].open;
    int paired = -1;
    *apart = 0;
    *gap = 0;
    uint32_t t_change = 0;
    const uint32_t end = t0 + (uint32_t)sc->limit_ms + 20000;
    for (uint32_t t = t0; t < end; t += SIM_TICK) {
        if (t_change && t == t_change) {
            if (sc->change == SC_B_JOINS) {
                s_b[1].ap = true;  // (on its channel now: the channel of the pair)
            } else {
                s_b[0].ap = false;
            }
        }
        if (t_change && sc->change == SC_A_BACK && t == t_change + CHANGE_MS) {
            s_b[0].ap = true;
        }
        for (int i = 0; i < s_nb; i++) {
            sim_tick(i, t);
        }
        for (int i = 0; t_change && t >= t_change && i < 2; i++) {
            const int g = (int)(t - s_b[i].heard);
            *gap = g > *gap ? g : *gap;
        }
        const bool both = s_b[0].heard && s_b[1].heard && t - s_b[0].heard < 600 && t - s_b[1].heard < 600;
        // (with an older firmware the pair is the agreement on its channel: a meeting on a visit is not a pair)
        const bool agreed = !sc->still_b || s_b[0].m.fixed == s_b[0].m.ch;
        if (paired < 0 && both && agreed && s_b[0].m.ch == s_b[1].m.ch) {
            paired = (int)(t - t_both);
            s_b[0].seek = s_b[1].seek = false;  // (the lobby ends: a match runs)
            if (sc->change) {
                t_change = t + CHANGE_MS;
            }
        }
        if (paired >= 0 && (int)(t - t_both) > paired + LINK_MEET_VISIT_MS && s_b[0].m.ch != s_b[1].m.ch) {
            (*apart)++;
        }
    }
    return paired;
}

static void test_scene(const scene_t *sc, int runs)
{
    int ok = 0, worst = 0, sum = 0, apart_runs = 0, gap_worst = 0;
    for (int r = 0; r < runs; r++) {
        int apart = 0, gap = 0;
        const int t = sim_run(sc, &apart, &gap);
        gap_worst = gap > gap_worst ? gap : gap_worst;
        if (t < 0 || t > sc->limit_ms) {
            printf("    run %d: pair at %d ms\n", r, t);
        }
        if (t >= 0 && t <= sc->limit_ms) {
            ok++;
            sum += t;
            worst = t > worst ? t : worst;
        }
        apart_runs += apart > 0;
    }
    printf("  %-42s pair %3d/%d, mean %5d ms, worst %5d ms, apart after the pair %d\n", sc->name, ok, runs,
           ok ? sum / ok : 0, worst, apart_runs);
    CHECK(ok == runs);
    CHECK(apart_runs == 0);
    if (sc->change) {
        printf("  %-42s the longest gap after the change of the WiFi %d ms\n", "", gap_worst);
        CHECK(gap_worst < GAP_MAX_MS);
    }
}

static void test_sim(void)
{
    static const scene_t SCENES[] = {
            {"b: no AP, same channel", false, false, 3, 3, 250, 2500, false, 0, false, 0},
            {"b2: no AP, two homes (3, 11)", false, false, 3, 11, 250, 2500, false, 0, false, 0},
            {"b2: no AP, random channels", false, false, 0, 0, 250, 2500, false, 0, false, 0},
            {"b2: no AP, random channels, lobby", false, false, 0, 0, 250, 3500, false, 0, true, 0},
            {"adjacent: no AP (7, 8)", false, false, 7, 8, 250, 2500, false, 0, false, 0},
            {"c2: A no AP, B on an AP (ch 3)", false, true, 6, 3, 250, 16500, false, 0, false, 0},
            {"c2: A no AP, B on an AP (random)", false, true, 0, 0, 250, 16500, false, 0, false, 0},
            {"c2: B no AP, A on an AP (random)", true, false, 0, 0, 250, 16500, false, 0, false, 0},
            {"c2: B no AP, A on an AP, lobby", true, false, 0, 0, 250, 16500, false, 0, true, 0},
            {"turn game: no AP, random", false, false, 0, 0, 500, 3500, false, 0, false, 0},
            {"turn game: A no AP, B on an AP", false, true, 0, 0, 500, 30000, false, 0, false, 0},
            // (an older firmware with no AP stays on the channel of its last AP: two visits find it)
            {"old firmware B, no AP, turn game", false, false, 0, 0, 500, 40000, true, 0, true, 0},
            {"old firmware B, no AP, 2 BOARDS HELLO", false, false, 0, 0, 250, 40000, true, 0, true, 0},
            // (a third board of the same app holds the meeting channel; the friend is on an AP: one visit each
            // LINK_MEET_SEEK_MS, a lost visit costs one more round)
            {"third board on ch 1, B on an AP (ch 6)", false, true, 0, 6, 250, 20000, false, 1, true, 0},
            {"third board on ch 1, B on an AP (random)", false, true, 0, 0, 500, 90000, false, 1, true, 0},
            // (a change of the WiFi in a match: the pair stays on its channel)
            {"A on an AP (ch 6) loses it in a match", true, false, 6, 0, 250, 16500, false, 0, false, SC_A_LOSES},
            {"A on an AP (random) loses it in a match", true, false, 0, 0, 250, 16500, false, 0, false, SC_A_LOSES},
            {"A on an AP loses it, turn game", true, false, 0, 0, 500, 30000, false, 0, false, SC_A_LOSES},
            {"A loses its AP and joins it again", true, false, 0, 0, 250, 16500, false, 0, false, SC_A_BACK},
            {"B joins an AP on the pair channel", false, false, 0, 0, 250, 2500, false, 0, false, SC_B_JOINS},
            {"B joins an AP (pair with A on an AP)", true, false, 0, 0, 250, 16500, false, 0, false, SC_B_JOINS},
    };
    for (size_t i = 0; i < sizeof SCENES / sizeof SCENES[0]; i++) {
        test_scene(&SCENES[i], 400);
    }
    // each AP channel: a board with no AP finds it
    for (int c = 1; c <= 13; c++) {
        char name[48];
        snprintf(name, sizeof name, "an AP on channel %d", c);
        const scene_t sc = {name, false, true, 0, c, 250, 16500, false, 0, false, 0};
        test_scene(&sc, 60);
    }
}

int main(void)
{
    test_pattern();
    test_hold_and_agree();
    test_seek_slow_stray();
    test_sim();
    printf("LINK MEET %s: %d checks, %d failed\n", s_fail ? "FAIL" : "OK", s_n, s_fail);
    return s_fail ? 1 : 0;
}
