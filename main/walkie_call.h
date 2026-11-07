#pragma once
// WALKIE calls: a tap on a friend in WALKIE calls that friend, also when the board of
// the friend has another app open.
// - The call goes as a UDP packet to the address of the friend on the LAN, else on the VPN (lan.c), each 1.5 s, for
//   30 s at most. Two boards that both have WALKIE open also pair by radio (net2.c: a HELLO that names only the
//   other board is a call).
// - The board that gets the call: when WALKIE is open (and not paired), it answers at once. Else the setting of
//   WALKIE decides (store "wk_call"): OPEN = WALKIE opens at once, ASK = a question on the screen (ANSWER / NOT NOW,
//   with a ring), IGNORE = the caller sees "NOT NOW". A board in a talk or in a game for 2 boards answers BUSY.
// - Only friends (FRIENDS) can call.
// - WAKE (the TALKIE inbox of the manager): the manager sends it when the user talks on the page and the
//   board does not show TALKIE. The board opens TALKIE at once (no question; IGNORE still says no; a talk or a game for
//   2 boards answers BUSY); TALKIE then pairs with the manager, which keeps the voice until then.
#include <stdbool.h>
#include <stdint.h>

enum {
    WK_CALL_OPEN,
    WK_CALL_ASK,
    WK_CALL_IGNORE
};
int walkie_call_mode(void);  // store "wk_call" (default ASK)
void walkie_call_set_mode(int m);
bool walkie_call_ring(void);  // store "wk_ring": the ring of a call (default on)
void walkie_call_set_ring(bool on);

// At the start of the board (it takes the call packets of lan.c).
void walkie_call_init(void);
// Each frame, the app task (sysui_update calls it).
void walkie_call_update(int64_t now);

// The caller (WALKIE): call the friend friends_get(i); stop the call (it sends CANCEL); the state of the call.
typedef enum {
    WKC_IDLE,
    WKC_CALLING,
    WKC_DECLINED,
    WKC_BUSY,
    WKC_NO_ANSWER
} wkc_state_t;
void walkie_call_start(int friend_idx);
void walkie_call_stop(void);
void walkie_call_done(void);  // the boards are paired: the call ends (no CANCEL)
wkc_state_t walkie_call_state(int *friend_idx, int64_t *since_ms);

// The board that answered: the friend to pair with (WALKIE takes it each frame in its lobby; -1 = none).
int walkie_call_take_answer(void);

// app_walkie.c: WALKIE is paired with a board now (a call from another board gets BUSY).
bool walkie_paired(void);
