// TFN OS: the voice service (tfn/speech.h: the module of the app VOICE; main/tfn_speech.c). The OS side. The boards
// with no voice (BOARD_TTS 0) get the empty versions below: no code, no RAM.
#pragma once
#include <stdbool.h>
#include "board.h"
#include "tfn/speech.h"

#if BOARD_TTS
void speech_init(void);
bool speech_calm(void);  // the voice is made now: the main loop draws fewer frames, the menu keeps few icons
// a new notice of an app: said when READ NOTICES ALOUD is on (the caller checked its mode and DO NOT DISTURB)
void speech_notice(const char *app, const char *title, const char *text);
bool speech_notices_on(void);  // the setting READ NOTICES ALOUD (store key sp_ntc)
void speech_set_notices(bool on);
const char *speech_state(void);  // a short text for the console and SETUP
// (tfn_watch.c, tfn_app.c) the watcher w goes; the speech task calls the module of the watcher *w now
void tfa_speech_slot_gone(int w);
bool tfa_speech_task_now(int *w);
// (tfn_listen.c) the ears of TALK need the memory: the voice closes its model now if it makes no voice (a wake of the
// speech task). tfa_speech_open: the model is open.
void tfa_speech_quiet(void);
bool tfa_speech_open(void);
#else
static inline void speech_init(void)
{
}
static inline bool speech_calm(void)
{
    return false;
}
static inline void speech_notice(const char *app, const char *title, const char *text)
{
    (void)app;
    (void)title;
    (void)text;
}
static inline bool speech_notices_on(void)
{
    return false;
}
static inline void speech_set_notices(bool on)
{
    (void)on;
}
static inline const char *speech_state(void)
{
    return "THIS BOARD HAS NO VOICE";
}
static inline void tfa_speech_slot_gone(int w)
{
    (void)w;
}
static inline bool tfa_speech_task_now(int *w)
{
    (void)w;
    return false;
}
static inline void tfa_speech_quiet(void)
{
}
static inline bool tfa_speech_open(void)
{
    return false;
}
#endif
