// The meeting of two boards on one radio channel (see link_meet.h).
#include "link_meet.h"

// the visits after the channel of the last peer: the usual AP channels first, then the others
static const uint8_t MEET_ORDER[] = {6, 11, 3, 9, 4, 8, 2, 10, 5, 7, 12, 13};
#define MEET_N ((int)sizeof MEET_ORDER)

static bool meet_valid(int ch)
{
    return ch >= 1 && ch <= 13;
}

// the time on the meeting channel: the wait (LINK_MEET_WAIT_MS, or LINK_MEET_SLOW_MS) and a random part
static uint32_t meet_wait(link_meet_t *m, unsigned flags)
{
    m->rnd ^= m->rnd << 13;
    m->rnd ^= m->rnd >> 17;
    m->rnd ^= m->rnd << 5;
    const uint32_t base = (flags & LINK_MEET_F_SLOW) ? LINK_MEET_SLOW_MS : LINK_MEET_WAIT_MS;
    return base + m->rnd % LINK_MEET_JITTER;
}

void link_meet_reset(link_meet_t *m, int ch, uint32_t now, uint32_t seed)
{
    m->rnd = seed ? seed : 0x9E3779B9u;
    m->ch = ch;
    m->want = 0;
    m->heard = false;
    m->heard_ms = now;
    m->next_ms = ch == LINK_MEET_CH ? now + meet_wait(m, 0) : now;  // (on the meeting channel: wait there first)
    m->k = 0;
    m->rx_ms = 0;
    m->home = 0;
    m->seek_ms = now;
    m->seek_mc = false;
    m->visit = 0;
    m->stray = 0;
    m->stray_visit = 0;
    m->stray_n = 0;
    m->fixed = 0;
    m->ap_ch = 0;
    m->ap_ms = now;
    m->here = 0;
    m->here_ms = now;
    if (meet_valid(ch) && ch != LINK_MEET_CH) {
        m->last = ch;  // (the channel of the radio: the last AP of this board)
    } else if (!meet_valid(m->last)) {
        m->last = 0;
    }
}

// the hold of the search ends at until (a later end of an earlier packet stays). A new hold starts the time of the
// visits in a hold (LINK_MEET_F_SEEK): the first one comes LINK_MEET_SEEK_MS later.
static void meet_hold(link_meet_t *m, uint32_t now, uint32_t until)
{
    if (!m->heard || (int32_t)(now - m->heard_ms) >= LINK_MEET_HOLD_MS) {
        m->seek_ms = now;
    }
    const uint32_t from = until - LINK_MEET_HOLD_MS;
    if (!m->heard || (int32_t)(from - m->heard_ms) > 0) {
        m->heard_ms = from;
    }
    m->heard = true;
}

// A peer with no AP on channel their_ch (not the meeting channel). Returns the channel of the pair: the meeting channel,
// or their_ch when the peer does not move (this board heard it on that visit channel in LINK_MEET_STRAY_N visits and
// never on the meeting channel: an older firmware stays on the channel of its last AP).
static int meet_stray(link_meet_t *m, int their_ch)
{
    if (m->fixed == their_ch) {
        return their_ch;
    }
    if (m->ch != their_ch || m->ch == LINK_MEET_CH) {
        return LINK_MEET_CH;  // (heard from the next channel, or a channel byte of an earlier packet)
    }
    if (m->stray != their_ch) {
        m->stray = their_ch;
        m->stray_n = 1;
    } else if (m->stray_visit != m->visit) {
        m->stray_n++;
    }
    m->stray_visit = m->visit;
    if (m->stray_n >= LINK_MEET_STRAY_N) {
        m->fixed = their_ch;
        return their_ch;
    }
    m->last = their_ch;
    m->k = 0;  // (the next visit is this channel again)
    return LINK_MEET_CH;
}

// a peer on the channel of this board, not on a visit: the pair is on this channel now
static void meet_here(link_meet_t *m, uint32_t now)
{
    m->here = m->ch;
    m->here_ms = now;
}

bool link_meet_heard(link_meet_t *m, int their_ch, bool their_ap, bool my_ap, uint32_t now)
{
    const bool answer = !their_ap && (!m->rx_ms || (int32_t)(now - m->rx_ms) > LINK_MEET_ANSWER_MS);
    m->rx_ms = now ? now : 1;
    const bool same = meet_valid(their_ch) && their_ch == m->ch && !m->home;
    if (my_ap || !meet_valid(their_ch)) {
        if (same) {
            meet_here(m, now);
        }
        meet_hold(m, now, now + LINK_MEET_HOLD_MS);
        return answer;
    }
    if (their_ap && same) {
        m->ap_ch = their_ch;
        m->ap_ms = now;
    }
    if (same && !m->want && m->here == m->ch && (int32_t)(now - m->here_ms) < LINK_MEET_HOLD_MS) {
        // a pair stays (link_meet.h): a board that lost its AP in a match keeps the channel of the match
        meet_here(m, now);
        meet_hold(m, now, now + LINK_MEET_HOLD_MS);
        return answer;
    }
    if (!their_ap && m->ap_ch == m->ch && (int32_t)(now - m->ap_ms) < LINK_MEET_HOLD_MS) {
        // with a board on an AP on this channel: a board with no AP (also from the next channel) does not move it
        if (same) {
            meet_here(m, now);
        }
        meet_hold(m, now, now + LINK_MEET_HOLD_MS);
        return answer;
    }
    // the channel of the pair: the channel of a board on an AP, else the meeting channel (a board that searches can
    // hear the other board on a visit, but it leaves that channel at the end of the visit)
    int target = LINK_MEET_CH;
    if (their_ap && their_ch == m->ch) {
        m->ap_ch = their_ch;  // (also on a visit: the board that the visit looks for)
        m->ap_ms = now;
    }
    if (their_ap) {
        target = their_ch;
        m->last = their_ch;
        m->k = 0;  // (the next visit is this channel)
    } else if (their_ch == LINK_MEET_CH) {
        m->stray_n = 0;  // (a peer that comes to the meeting channel moves)
    } else {
        target = meet_stray(m, their_ch);
    }
    const bool home_ap = m->home && m->ap_ch == m->home && (int32_t)(now - m->ap_ms) < LINK_MEET_HOLD_MS;
    if (m->home && !their_ap && (target != m->ch || home_ap)) {
        // a visit in a hold: the pair stays on the home channel (the visit can hear a board on the next channel, or
        // other boards on the meeting channel while a board on an AP is at home; a board on an AP is the board that
        // the visit looks for)
        meet_hold(m, now, now + LINK_MEET_HOLD_MS);
        return answer;
    }
    if (m->home && target == m->ch) {
        m->seek_ms = now;  // (a visit in a hold that hears a peer on its channel: the pair is here now)
    }
    m->home = 0;
    if (m->ch != target) {
        m->want = target;
        meet_hold(m, now, now + LINK_MEET_HOLD_MS);
    } else if (their_ch == m->ch) {
        meet_here(m, now);
        meet_hold(m, now, now + LINK_MEET_HOLD_MS);
    } else {
        // the other board is on the next channel and comes here when it hears this board: a short hold, because it
        // can also go on with its search (then this board searches again soon)
        meet_hold(m, now, now + LINK_MEET_SHORT_MS);
    }
    return answer;
}

// the next channel to visit: slot 0 of a round is the channel of the last peer, then MEET_ORDER (not the meeting
// channel, not the channel skip, and the channel of the last peer only one time in a round)
static int meet_next_visit(link_meet_t *m, int skip)
{
    m->visit++;
    for (int guard = 0; guard <= 2 * MEET_N + 1; guard++) {
        const int k = m->k;
        m->k = (uint8_t)((m->k + 1) % (MEET_N + 1));
        const int c = k == 0 ? m->last : MEET_ORDER[k - 1];
        if (meet_valid(c) && c != LINK_MEET_CH && c != skip && (k == 0 || c != m->last)) {
            return c;
        }
    }
    return MEET_ORDER[0];
}

// the time of a visit of channel c: two visit times on the channel of a peer that can stay there (one packet of a turn
// game comes in each 500 ms: a second visit that hears nothing would cost a round)
static uint32_t meet_visit_ms(const link_meet_t *m, int c)
{
    return c == m->stray && m->stray_n ? 2 * LINK_MEET_VISIT_MS : LINK_MEET_VISIT_MS;
}

// a hold (a peer is here): 0 = stay; else a visit that comes back (LINK_MEET_F_SEEK), or the way back from it
static int meet_hold_step(link_meet_t *m, unsigned flags, uint32_t now, bool *search)
{
    if (m->home) {
        if ((int32_t)(now - m->next_ms) < 0) {
            return 0;
        }
        const int h = m->home;
        m->home = 0;
        *search = true;
        return h;
    }
    if (!(flags & LINK_MEET_F_SEEK) || (int32_t)(now - m->seek_ms) < LINK_MEET_SEEK_MS) {
        return 0;
    }
    m->seek_ms = now;
    m->home = m->ch;
    *search = true;
    m->seek_mc = !m->seek_mc;
    if (m->ch != LINK_MEET_CH && m->seek_mc) {
        m->visit++;
        m->next_ms = now + LINK_MEET_VISIT_MS;
        return LINK_MEET_CH;  // (a hold on another channel: the boards with no AP wait on the meeting channel)
    }
    const int c = meet_next_visit(m, m->ch);
    m->next_ms = now + meet_visit_ms(m, c);
    return c;
}

int link_meet_step(link_meet_t *m, unsigned flags, uint32_t now, bool *search)
{
    *search = false;
    if (!(flags & LINK_MEET_F_MOVE)) {
        return 0;  // (an agreed channel waits until the radio can move)
    }
    if (m->want) {
        const int w = m->want;
        m->want = 0;
        if (w != m->ch) {
            m->seek_ms = now;  // (on the agreed channel: the first visit in a hold comes LINK_MEET_SEEK_MS later)
            return w;
        }
    }
    if (!(flags & LINK_MEET_F_SEARCH)) {
        return 0;
    }
    // (signed: a packet that came after the time of this step, in the task of the radio, is a hold too)
    if (m->heard && (int32_t)(now - m->heard_ms) < LINK_MEET_HOLD_MS) {
        const int c = meet_hold_step(m, flags, now, search);
        return c == m->ch ? 0 : c;
    }
    m->home = 0;  // (the hold ended in a visit: the search goes on from here)
    if ((int32_t)(now - m->next_ms) < 0) {
        return 0;
    }
    *search = true;
    if (m->ch != LINK_MEET_CH) {
        m->next_ms = now + meet_wait(m, flags);
        return LINK_MEET_CH;
    }
    const int c = meet_next_visit(m, 0);
    m->next_ms = now + meet_visit_ms(m, c);
    return c;
}

void link_meet_moved(link_meet_t *m, int ch)
{
    m->ch = ch;
}
