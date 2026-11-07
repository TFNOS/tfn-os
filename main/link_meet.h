#pragma once
// THE MEETING OF TWO BOARDS ON ONE RADIO CHANNEL (link.c). ESP-NOW hears only the channel of the radio. A radio also
// hears the next channel, but only at a short distance and with few packets: a pair on two channels loses most of its
// packets, and at a distance it ends. So the two boards of a link must be on one channel:
// - A board on a WiFi network (an AP) stays on the channel of its AP.
// - A board with no AP SEARCHES: it waits on the meeting channel LINK_MEET_CH and visits the other channels in turn
//   (first the channel of the last peer or of its own last AP, then the usual AP channels). The wait on the meeting
//   channel is longer than a visit, so the waits of two boards with no AP always overlap: they meet in the first
//   round. A board with no AP finds a board on an AP within one round of visits.
// - A packet of a peer (link.c: a board in the same app, the chosen friend, LEARN A FRIEND) stops the search for
//   LINK_MEET_HOLD_MS. While no match runs (a lobby waits, or the board only listens), the board still does one visit
//   each LINK_MEET_SEEK_MS in a hold and then comes back: other boards near it cannot keep it from a board on an AP.
//   A hold on another channel visits the meeting channel each second time (the boards with no AP wait there).
// - A board that hears a NEW peer with no AP (no packet of a peer for LINK_MEET_ANSWER_MS) ANSWERS at once with its
//   last packets: the peer can be on a visit, and a board that waits sends only a name beacon each second.
// - The two boards AGREE on one channel: a board with no AP goes to the channel of a board on an AP. Two boards with no
//   AP go to the meeting channel (they can hear each other on a visit, at the edge of a visit, or on two next channels).
//   A peer with no AP that this board hears on the same visit channel in LINK_MEET_STRAY_N visits, and never on the
//   meeting channel, does not move (an older firmware): its channel is the agreed channel.
// - A PAIR STAYS: two boards that hear each other on the channel of their hold (not on a visit) do not move, also when
//   one of them loses its AP or joins an AP on that channel in a match (else one board goes to the meeting channel and
//   the other one stays for its hold: the match ends).
// This part has no radio calls (the host tests run it): link.c moves the radio and tells the result.
#include <stdbool.h>
#include <stdint.h>

#define LINK_MEET_CH        1     // the meeting channel (also the channel of the hotspot of AUTO)
#define LINK_MEET_WAIT_MS   600   // the time on the meeting channel between two visits (more than a visit)
#define LINK_MEET_JITTER    100   // a random part of the wait (0..99 ms): two boards do not keep one rhythm
#define LINK_MEET_VISIT_MS  550   // the time on a visited channel: more than the 500 ms of an invite of the turn games
#define LINK_MEET_HOLD_MS   4000  // no search for this time after a packet of a peer (more than the 3 s end of net2)
#define LINK_MEET_SHORT_MS  1000  // the hold after a packet from the next channel (the other board must come here)
#define LINK_MEET_ANSWER_MS 1000  // no packet of a peer for this time: the next one gets an answer
#define LINK_MEET_SEEK_MS   3000  // LINK_MEET_F_SEEK: the time between two visits in a hold
#define LINK_MEET_SLOW_MS   1200  // LINK_MEET_F_SLOW: the wait on the meeting channel (a phone can join the hotspot)
#define LINK_MEET_STRAY_N   2     // the visits that hear a peer that does not move

// the flags of link_meet_step
// LINK_MEET_F_MOVE: the radio can change its channel now (no AP, no connect, no scan, no phone on the hotspot)
#define LINK_MEET_F_MOVE   1u
#define LINK_MEET_F_SEARCH 2u  // the search can run (the radio listens all the time)
#define LINK_MEET_F_SEEK   4u  // no match runs: a visit each LINK_MEET_SEEK_MS also in a hold
#define LINK_MEET_F_SLOW   8u  // the hotspot is on: the wait on the meeting channel is LINK_MEET_SLOW_MS

typedef struct {
    int ch;                // the channel of the radio now
    int want;              // the agreed channel (0 = none): the next step goes there
    int last;              // the channel of the last peer (the first visit of a round)
    uint32_t heard_ms;     // the last packet of a peer
    bool heard;            // a packet of a peer came after the reset
    uint32_t next_ms;      // the time of the next step of the search
    uint8_t k;             // the next visit of the round
    uint32_t rnd;          // the random state of the jitter
    uint32_t rx_ms;        // the last packet of a peer (0 = none after the reset)
    int home;              // a visit in a hold (LINK_MEET_F_SEEK): the channel to go back to (0 = none)
    uint32_t seek_ms;      // the start of the last visit in a hold
    bool seek_mc;          // the next visit in a hold away from the meeting channel goes to the meeting channel
    uint16_t visit;        // the number of the visit now (it goes up at each visit)
    int stray;             // a peer with no AP that a visit heard on the visit channel: that channel (0 = none)
    uint16_t stray_visit;  // the visit that heard it last
    uint8_t stray_n;       // the visits that heard it
    int fixed;             // the channel of a peer that does not move (0 = none)
    int ap_ch;             // the channel of the last board on an AP that this board heard on its own channel
    uint32_t ap_ms;        // the time of that packet
    int here;              // the channel where this board and a peer last heard each other in a hold (0 = none)
    uint32_t here_ms;      // the time of that packet
} link_meet_t;

// The link starts on channel ch (the channel of the radio: a board away from its AP rests on the channel of that AP,
// so it is the first visit): no peer yet, the search can start at once. seed: a random number of this board.
void link_meet_reset(link_meet_t *m, int ch, uint32_t now, uint32_t seed);
// A packet of a peer came by radio. their_ch: its channel byte (1..13; else not known), their_ap: it is on an AP,
// my_ap: this board is on an AP. Returns true when this board must answer (a new peer with no AP: link.c sends its last
// packets again).
bool link_meet_heard(link_meet_t *m, int their_ch, bool their_ap, bool my_ap, uint32_t now);
// The step of each poll: the channel to go to now (0 = stay). flags: LINK_MEET_F_*. *search: the move is a step of the
// search (link.c then sends the last packets again on the new channel).
int link_meet_step(link_meet_t *m, unsigned flags, uint32_t now, bool *search);
// The radio is on channel ch now (after a move of link_meet_step, or a change of the WiFi).
void link_meet_moved(link_meet_t *m, int ch);
