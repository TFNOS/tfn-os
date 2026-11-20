// TFN OS: the ears and the small brains (tfn/listen.h, tfn_listen.h). The watcher of the app TALK gives its module
// (listen_offer: hear = speech to text, think = a small brain answers); this file records the microphone, calls the
// module on the listen task, and keeps the results for the apps. It also runs THE TALK KEY (hold BOOT in any app):
// the main brain of TALK understands the sentence; a command of the board runs here (the volume, the brightness, open
// an app, the time ...); else the handler of the open app gets it, or a notice shows the answer and the voice says it.
// - The listen task: core 1, priority 2 (as the web server: they take turns), its stack in PSRAM (the module never
//   writes the flash). The memory of its calls goes into the set of the watcher (own_now() in tfn_app.c asks
//   tfa_listen_task_now()). The module closes 45 s after its last call, at an app change, or when the voice needs
//   the memory.
// - The recording: 24 kHz mono, at most 10 s, into a PSRAM buffer of the OS; 0.4 s more after the stop.
#include "tfn_app_priv.h"
#include "tfn_listen.h"

#if BOARD_TTS
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "alarm.h"
#include "app.h"
#include "audio.h"
#include "board.h"
#include "gfx.h"
#include "mfont.h"
#include "settings.h"
#include "sysui.h"
#include "tfn_speech.h"
#include "tfn_util.h"
#include "tz.h"
#include "uikit.h"
#include "wifi.h"
#include "power.h"

static const char *TAG = "listen";

#define REC_MAX_S     10
#define TAIL_MS       400
#define KEY_LONG_MS   700    // the long press of BOOT (as CHAT before)
#define IDLE_CLOSE_MS 45000  // (and at each app change: an app that opens gets the memory)
#define TEXT_N        256

static listen_ops_t s_ops;
static volatile int s_w = -1;  // the watcher of the module (-1: none)
static TaskHandle_t s_task;
static volatile int s_state = LS_OFF;
static int16_t *s_rec;
static volatile int64_t s_stop_at;  // 0: it records; else the end of the recording (the tail)
static volatile bool s_cancel, s_has_brain, s_open, s_mic_mine;
static volatile int s_job;  // 2 = an ask (s_ask) waits for the listen task
static char s_brain[96], s_text[TEXT_N], s_ans[TEXT_N], s_ask[TEXT_N];
static int64_t s_last_ms;
static volatile int s_w_call = -1;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
// the talk key: 1 = down (it records, not yet shown), 2 = a talk (shown), 3 = the turn waits for listen_tick
static volatile int s_key;
// the talk key went down while the ears or a brain worked: 1 = down, 2 = held long (the sign WAIT). The recording
// starts when they end (listen_tick), if the key is still down
static int s_wait;
static volatile bool s_taken;  // the app read the result of its turn (listen_text, listen_answer)
static listen_handler_fn s_handler;
static int64_t s_note_until;
// a test with no sound in the room (console "listen file <path>"): the next turn uses this recorded voice (24 kHz,
// 16 bit, mono, raw) in place of the sound of the microphone
static char s_file[96];
static volatile bool s_warm_more;  // the module has more steps of its preparation (listen_ops_t.warm)

// ---------------------------------------------------------------- the module calls (the set of the watcher)
// The task watchdog of the idle task of core 1 is off while the ears or a brain work (power_idle1_hold): core 1 is
// busy by design then. The listen task pauses 1 ms each 0.5 s, but the worker of the voice (VOICE, also on core 1)
// takes those pauses, and the watchdog printed a warning about once in 15 minutes of DETECTIVE (no restart).
static void idle1_watch(bool on)
{
    power_idle1_hold(!on);
}

static bool call_begin(void)
{
    portENTER_CRITICAL(&s_mux);
    const bool ok = s_w >= 0;
    if (ok) {
        s_w_call = s_w;
    }
    portEXIT_CRITICAL(&s_mux);
    return ok;
}

static void call_end(void)
{
    s_w_call = -1;
    s_last_ms = tfn_now_ms();
}

bool tfa_listen_task_now(int *w)
{
    const int c = s_w_call;
    if (s_task && c >= 0 && xTaskGetCurrentTaskHandle() == s_task) {
        *w = c;
        return true;
    }
    return false;
}

static void module_close(void)
{
    if (s_open && call_begin()) {
        s_ops.close();
        call_end();
    }
    s_open = false;
}

// ---------------------------------------------------------------- the listen task
static void rec_end(void)
{
    audio_mic_record_stop();
    if (s_mic_mine) {
        audio_mic_stop();
        s_mic_mine = false;
    }
}

// The voice gives the memory of its model before the speech to text: in an app with big pictures both do not fit
// (DETECTIVE crashed when a question came soon after a spoken answer). At most 1 s.
static void voice_off(void)
{
    if (!tfa_speech_open()) {
        return;
    }
    speech_stop();
    tfa_speech_quiet();
    for (int i = 0; i < 100 && tfa_speech_open(); i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// a brain answers (in a call of the module); -2 = no memory now: the voice frees its model, then a new try (3 s)
static int think(const char *brain, const char *text, char *out, int cap)
{
    int r = s_ops.think(brain, text, out, cap);
    for (int i = 0; r == -2 && i < 8; i++) {
        voice_off();
        vTaskDelay(pdMS_TO_TICKS(400));
        r = s_ops.think(brain, text, out, cap);
    }
    return r;
}

// one step of the preparation of the module while the microphone records (the model loads while the person talks)
static void warm_step(void)
{
    voice_off();
    if (!call_begin()) {
        s_warm_more = false;
        return;
    }
    s_open = true;
    s_warm_more = s_ops.warm();
    s_last_ms = tfn_now_ms();
    call_end();
}

// the test file into the recording (the listen task): the number of samples
static uint32_t file_into_rec(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "no test file %s", path);
        return 0;
    }
    const uint32_t n = (uint32_t)fread(s_rec, 2, (size_t)AUDIO_SAMPLE_RATE * REC_MAX_S, f);
    fclose(f);
    ESP_LOGI(TAG, "the test file %s: %.1f s", path, (double)n / AUDIO_SAMPLE_RATE);
    return n;
}

static void do_turn(void)
{
    uint32_t got = audio_mic_recorded();
    rec_end();
    if (s_file[0]) {
        got = file_into_rec(s_file);
        s_file[0] = 0;
    }
    // (no cut of the silence: a click at the start of the microphone set the gate too high and cut words, and the
    // speech to text takes about 3 s also for 0.4 s of sound: the layers come from the card)
    const uint32_t at = 0, n = got;
    s_text[0] = s_ans[0] = 0;
    int r = -1;
    const int64_t t0 = tfn_now_ms();
    int64_t t1 = t0;
    if (n >= AUDIO_SAMPLE_RATE / 4 && call_begin()) {
        idle1_watch(false);
        s_state = LS_HEAR;
        s_open = true;
        voice_off();
        r = s_ops.hear(s_rec + at, (int)n, AUDIO_SAMPLE_RATE, s_text, sizeof s_text);
        // no memory: the voice closes its model (it made a text just now). When the voice is closed, the memory is
        // the app's: two more tries, then the turn ends (no 6 s of waits for nothing)
        for (int i = 0; r == -2 && i < 12 && (i < 2 || tfa_speech_open()); i++) {
            speech_stop();
            tfa_speech_quiet();
            vTaskDelay(pdMS_TO_TICKS(500));
            r = s_ops.hear(s_rec + at, (int)n, AUDIO_SAMPLE_RATE, s_text, sizeof s_text);
        }
        t1 = tfn_now_ms();
        if (r == -2) {
            ESP_LOGW(TAG, "no memory for the ears: PSRAM %u free, the largest block %u",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        }
        if (!r && s_text[0] && (s_has_brain || s_key)) {
            s_state = LS_THINK;
            r = think(s_has_brain ? s_brain : NULL, s_text, s_ans, sizeof s_ans);
        }
        call_end();
        idle1_watch(true);
    }
    heap_caps_free(s_rec);
    s_rec = NULL;
    ESP_LOGI(TAG, "heard [%s] -> [%s] (%d): %.1f of %.1f s of sound, the text %d ms, the brain %d ms", s_text, s_ans, r,
             (double)n / AUDIO_SAMPLE_RATE, (double)got / AUDIO_SAMPLE_RATE, (int)(t1 - t0), (int)(tfn_now_ms() - t1));
    s_state = r && n >= AUDIO_SAMPLE_RATE / 4 ? LS_FAULT : LS_DONE;
}

static void do_ask(void)
{
    s_ans[0] = 0;
    int r = -1;
    const int64_t t0 = tfn_now_ms();
    if (call_begin()) {
        idle1_watch(false);
        s_open = true;
        r = think(s_has_brain ? s_brain : NULL, s_ask, s_ans, sizeof s_ans);
        call_end();
        idle1_watch(true);
    }
    ESP_LOGI(TAG, "asked [%s] -> [%s] (%d): the brain %d ms", s_ask, s_ans, r, (int)(tfn_now_ms() - t0));
    s_state = r ? LS_FAULT : LS_DONE;
}

static void listen_task(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        const int64_t now = tfn_now_ms();
        if (s_state == LS_REC) {
            if (s_cancel) {
                rec_end();
                heap_caps_free(s_rec);
                s_rec = NULL;
                s_state = LS_OFF;
            } else if ((s_stop_at && now >= s_stop_at) || !audio_mic_recording()) {
                do_turn();
            } else if (s_warm_more) {
                warm_step();
            }
        } else if (s_job == 2) {
            s_job = 0;
            do_ask();
        } else if (s_open && now - s_last_ms > IDLE_CLOSE_MS) {
            module_close();
        }
        s_cancel = false;
    }
}

bool listen_offer(const listen_ops_t *ops)
{
    if (!ops || !ops->hear || !ops->think || !ops->close || tfa_wcur < 0 || !tfa_on_watch_task()) {
        return false;
    }
    // (the stack in PSRAM: the module never writes the flash; 32 KB: the speech to text runs on it)
    if (!s_task && xTaskCreatePinnedToCoreWithCaps(listen_task, "listen", 32768, NULL, 2, &s_task, 1,
                                                   MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_task = NULL;
        return false;
    }
    s_ops = *ops;
    s_open = false;
    s_w = tfa_wcur;
    ESP_LOGI(TAG, "the ears of %s are ready", tfa_w[tfa_wcur].id);
    return true;
}

void tfa_listen_slot_gone(int w)
{
    if (w < 0 || w != s_w) {
        return;
    }
    portENTER_CRITICAL(&s_mux);
    s_w = -1;  // (no new call)
    portEXIT_CRITICAL(&s_mux);
    for (int i = 0; i < 6000 && s_w_call >= 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));  // (a speech to text ends in some seconds)
    }
    if (s_open && s_ops.close) {
        const int old = tfa_wcur;
        tfa_wcur = w;  // (its frees go to its own set)
        s_ops.close();
        tfa_wcur = old;
    }
    s_open = false;
    memset(&s_ops, 0, sizeof s_ops);
    if (s_state == LS_HEAR || s_state == LS_THINK) {
        s_state = LS_FAULT;
    }
}

void tfa_listen_quiet(void)
{
    if (s_open && s_state != LS_REC && s_state != LS_HEAR && s_state != LS_THINK && s_w_call < 0) {
        s_last_ms = 0;  // (the listen task closes the module at its next round)
        // the caller (the voice, an app that opens) wants the memory now: wait for the close (at most 0.5 s)
        for (int i = 0; i < 50 && s_open && s_task && xTaskGetCurrentTaskHandle() != s_task; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}

bool listen_awake(void)
{
    return s_state == LS_REC || s_state == LS_HEAR || s_state == LS_THINK || s_key || s_wait;
}

// ---------------------------------------------------------------- the calls of the apps
bool listen_ready(void)
{
    return s_w >= 0;
}

static bool busy(void)
{
    return s_state == LS_REC || s_state == LS_HEAR || s_state == LS_THINK || s_job;
}

static bool start(const char *brain)
{
    if (!listen_ready() || busy()) {
        return false;
    }
    s_rec = heap_caps_malloc((size_t)AUDIO_SAMPLE_RATE * REC_MAX_S * 2, MALLOC_CAP_SPIRAM);
    if (!s_rec) {
        s_state = LS_FAULT;
        return false;
    }
    s_has_brain = brain && brain[0];
    snprintf(s_brain, sizeof s_brain, "%s", s_has_brain ? brain : "");
    s_text[0] = s_ans[0] = 0;
    s_taken = false;
    s_stop_at = 0;
    s_cancel = false;
    s_warm_more = s_ops.warm != NULL;
    s_mic_mine = !audio_mic_on();
    if (s_mic_mine) {
        audio_mic_start();
    }
    audio_mic_record(s_rec, (uint32_t)AUDIO_SAMPLE_RATE * REC_MAX_S);
    s_state = LS_REC;
    return true;
}

bool listen_start(const char *brain)
{
    return start(brain);
}

void listen_stop(void)
{
    if (s_state == LS_REC && !s_stop_at) {
        s_stop_at = tfn_now_ms() + TAIL_MS;
    }
}

void listen_cancel(void)
{
    if (s_state == LS_REC) {
        s_cancel = true;
    } else if (s_state == LS_DONE || s_state == LS_FAULT) {
        s_state = LS_OFF;
    }
}

int listen_state(void)
{
    return s_state;
}

float listen_level(void)
{
    return s_state == LS_REC ? audio_mic_level() : 0.0f;
}

void listen_text(char *out, int cap)
{
    s_taken = true;
    snprintf(out, (size_t)cap, "%s", s_state == LS_DONE ? s_text : "");
}

void listen_answer(char *out, int cap)
{
    s_taken = true;
    snprintf(out, (size_t)cap, "%s", s_state == LS_DONE ? s_ans : "");
}

bool listen_ask(const char *brain, const char *text)
{
    if (!listen_ready() || busy() || !text) {
        return false;
    }
    s_has_brain = brain && brain[0];
    snprintf(s_brain, sizeof s_brain, "%s", s_has_brain ? brain : "");
    snprintf(s_ask, sizeof s_ask, "%s", text);
    snprintf(s_text, sizeof s_text, "%s", text);
    s_taken = false;
    s_state = LS_THINK;
    s_job = 2;
    return true;
}

void listen_handler(listen_handler_fn fn)
{
    s_handler = fn;
}

void listen_app_changed(void)
{
    s_handler = NULL;
    tfa_listen_quiet();  // (the models close when idle: the app that opens gets their memory)
}

bool listen_calm(void)
{
    return s_state == LS_HEAR || s_state == LS_THINK;
}

#include "tfn_listen_key.inc"  // the talk key, the commands of the board, the sign, the console
#else                          // ---------------------------------------------------------------- a board with no ears

bool tfa_listen_task_now(int *w)
{
    (void)w;
    return false;
}
void tfa_listen_slot_gone(int w)
{
    (void)w;
}
void tfa_listen_quiet(void)
{
}
bool listen_offer(const listen_ops_t *ops)
{
    (void)ops;
    return false;
}
bool listen_ready(void)
{
    return false;
}
bool listen_start(const char *brain)
{
    (void)brain;
    return false;
}
void listen_stop(void)
{
}
void listen_cancel(void)
{
}
int listen_state(void)
{
    return LS_OFF;
}
float listen_level(void)
{
    return 0.0f;
}
void listen_text(char *out, int cap)
{
    if (cap > 0) {
        out[0] = 0;
    }
}
void listen_answer(char *out, int cap)
{
    if (cap > 0) {
        out[0] = 0;
    }
}
bool listen_ask(const char *brain, const char *text)
{
    (void)brain;
    (void)text;
    return false;
}
void listen_handler(listen_handler_fn fn)
{
    (void)fn;
}
bool listen_key_down(void)
{
    return false;
}
void listen_key_held(int ms)
{
    (void)ms;
}
void listen_key_up(void)
{
}
void listen_tick(int64_t now)
{
    (void)now;
}
void listen_overlay(void)
{
}
void listen_app_changed(void)
{
}
bool listen_shows(void)
{
    return false;
}
bool listen_awake(void)
{
    return false;
}
bool listen_calm(void)
{
    return false;
}
void listen_console(const char *a, const char *b)
{
    (void)a;
    (void)b;
}
#endif
