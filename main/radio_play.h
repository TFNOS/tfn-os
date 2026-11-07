#pragma once
// PLAY BY RADIO and the radio invite: one place for all 2-board lobbies (main.c calls radio_play_update each frame).
//
// THE TWO WIFI NETWORKS: a board on a WiFi network (an AP) stays on the channel of its AP, and ESP-NOW hears only that
// channel (link_meet.h). A friend with no AP finds this board by its search. But when the friend is on another AP on
// another channel, neither board can move: the lobby waits for ever. So, when a lobby waits (friends_lobby_waits) for
// RADIO_HINT_MS with this board on an AP and no friend in the game, a question shows: "YOUR FRIEND IS ON ANOTHER WIFI?"
// with PLAY BY RADIO. PLAY BY RADIO leaves the AP as the console "wifi disconnect" does (the saved networks do not
// change), so the link of this board can search and meet the friend. When the link stops (the end of the 2-board
// session) or the app closes, the board joins its WiFi again (as "wifi reconnect"). A NOT NOW, or a tap out of the
// buttons, closes the question for the rest of this wait. The question closes by itself when a friend comes.
//
// THE RADIO INVITE (friends.h): each frame this part posts the notice of an invite that came by radio, and in the menu
// it keeps the listen of the link on (link_listen).
#include <stdbool.h>
#include <stdint.h>

#define RADIO_HINT_MS 10000  // the wait of a lobby before the question

// The rule of the question (no side effects; the host tests use it): waits = a lobby waits, age_ms = the time of the
// wait, on_ap = this board is on an AP, friend = a friend is in the same game (friends_in_game), in_use = a link is in
// use (link_in_use).
static inline bool radio_hint_want(bool waits, uint32_t age_ms, bool on_ap, bool friend_in_game, bool in_use)
{
    return waits && age_ms >= RADIO_HINT_MS && on_ap && !friend_in_game && !in_use;
}

// Each frame (the app task; also while the screen is off): menu = the menu is the app now.
void radio_play_update(int64_t now_ms, bool menu);
// PLAY BY RADIO runs now: the board left its WiFi for a 2-board session.
bool radio_play_on(void);
