// TFN OS: the ears and the small brains, the parts that the OS uses (the API of the apps: tfn/listen.h).
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "tfn/listen.h"

// own_now() (tfn_app.c): the memory of a call on the listen task goes into the set of the watcher of the module
bool tfa_listen_task_now(int *w);
// a watcher goes (tfn_watch.c): its module stops and closes before its code goes
void tfa_listen_slot_gone(int w);
// the voice needs its memory now: the module closes when it is idle (tfn_speech.c, before a voice is made)
void tfa_listen_quiet(void);

// THE TALK KEY (main_keys.c): the BOOT key went down / is held this long / went up. The setting "talk_key" (store, 0 =
// the long press opens CHAT as before, 1 = the long press listens) and the ears decide. true: the key is the talk key
// now (the long press of CHAT does not run); while the ears or a brain work, the key waits (the sign WAIT) and the
// recording starts when they end.
bool listen_key_down(void);
void listen_key_held(int ms);
void listen_key_up(void);
// the main loop: the end of a talk-key turn (the command, the answer), and the sign "LISTENING" over the app
void listen_tick(int64_t now);
void listen_overlay(void);
// the sign or the answer of the talk key shows over the app: a system layer (no HI frame of the app under it)
bool listen_shows(void);
// the speech becomes text, or a brain answers: the main loop draws about 10 frames a second (as speech_calm(): the LCD
// flush at full speed takes core 1 from the listen task: 12 s for a turn in place of 3 s)
bool listen_calm(void);
// a turn of the ears runs (it records, makes the text, a brain answers, or a talk-key turn waits for its end): the
// screen stays on (main_saver.c), so the turn ends and its answer shows
bool listen_awake(void);
// the open app changes (main.c): its handler goes
void listen_app_changed(void);
// the console: "listen" (the state), "listen ask <text>", "listen key <text>" (a typed talk-key turn), "listen down" /
// "listen up" (a virtual talk key), "listen close"
void listen_console(const char *a, const char *b);
