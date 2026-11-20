// TFN OS: the ears and the small brains (speech to text and a tiny language model on the board, with no network).
// Only the boards with BOARD_TTS (an octal PSRAM and an SD card) have them; on the others listen_ready() gives false.
// The engine is a module: the watcher (watch.h) of the store app TALK gives its functions to TFN OS in its init()
// (listen_offer). TFN OS calls them on its listen task (the memory of the calls goes into the set of that watcher).
// English only.
//
// THE TALK KEY: in any app, hold the BOOT key and talk, then let go (not in an app that reads that key itself,
// app_t.key). The main brain of TALK understands the sentence: a command of the board (the volume, the brightness,
// open an app, the time ...) runs at once; a text for the open app goes to its handler (listen_handler), else the
// answer shows in a notice and the voice says it.
#pragma once
#include <stdbool.h>
#include <stdint.h>

// ---- the module (the watcher of TALK)
typedef struct {
    // speech -> text: pcm = mono samples at rate Hz (16000 or 24000). The text in small letters. 0 = OK, -2 = no
    // memory now (TFN OS tries again some times), other < 0 = a fault.
    int (*hear)(const int16_t *pcm, int n, int rate, char *text, int cap);
    // a brain (a TNY2 file on the card; NULL = the main brain of TALK) answers a text. 0 = OK.
    int (*think)(const char *brain, const char *text, char *out, int cap);
    // free all memory (the models). The next call opens them again.
    void (*close)(void);
    // (NULL: none) one step of the preparation while the microphone records: open the model, read a part of it into
    // memory. true: more steps wait. TFN OS calls it until it gives false or the recording ends.
    bool (*warm)(void);
} listen_ops_t;
bool listen_offer(const listen_ops_t *ops);

// ---- the apps
enum {
    LS_OFF,    // nothing (or the result was taken)
    LS_REC,    // the microphone records
    LS_HEAR,   // the speech becomes text
    LS_THINK,  // a brain answers (listen_ask, or the talk key)
    LS_DONE,   // the text (and the answer) are ready
    LS_FAULT,  // no ears (no TALK, no data on the card), no memory
};
bool listen_ready(void);  // the ears are here (TALK is installed, its data is on the card)
// Record from now until listen_stop (+ 0.4 s) or 10 s (start it when the finger goes down: no word start is lost). Then the text is
// made on the listen task. brain: NULL = text only; a TNY2 file = that brain also answers the text (listen_answer).
bool listen_start(const char *brain);
void listen_stop(void);
void listen_cancel(void);  // stop and drop all (no text)
int listen_state(void);    // LS_*
float listen_level(void);  // the level of the microphone now, 0..1 (while LS_REC)
// The results (LS_DONE): the heard text, the answer of the brain ("" with no brain). They stay until the next start.
void listen_text(char *out, int cap);
void listen_answer(char *out, int cap);
// A brain answers a text with no speech (a typed text, a test): LS_THINK, then LS_DONE (listen_answer).
bool listen_ask(const char *brain, const char *text);

// THE TALK KEY in an app: the handler gets the heard text and the code of the main brain ("@open chess", or a
// sentence) when it is not a command of the board. It gives true when it used it (no notice). NULL = none. An app
// sets it in its enter(); TFN OS clears it when the app closes.
typedef bool (*listen_handler_fn)(const char *heard, const char *answer);
void listen_handler(listen_handler_fn fn);
