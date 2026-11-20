// TFN OS: the voice service (tfn/speech.h, tfn_speech.h). The watcher of the app VOICE gives its module (speech_offer);
// this file keeps a queue of jobs (a text to say, a text to make into a voice file), calls the module on the speech
// task, plays the pieces and the files, and runs THE READER (a long text, made ahead line by line into a cache).
// - The speech task: core 0, the priority of the main loop, its stack in PSRAM. While it calls the module it holds the
//   lock of the watchers (no unload in a call), and the memory of the module goes into the set of its watcher
//   (own_now() in tfn_app.c asks tfa_speech_task_now()).
// - The play task: core 1, priority 1. It puts the voice files of the reader into the sound system, one after the
//   other (a file is read from the card shortly before the voice before it ends).
// - While the voice is made, the CPU stays at full speed (a PM lock: with the screen off it is at 80 MHz), the main
//   loop draws about 10 frames a second (speech_calm(): the LCD flush and the drawing take both cores, and a voice
//   below them got 0.06x real time in TTS LAB), and the menu keeps few icon files (the model needs about 4 MB).
// - A text waits while a game shows (up to 10 minutes); the reader does not make or play while a game shows. The
//   module closes (its 2.7 MB model goes) 3 s after the last job: an app that opens then gets the memory.
// - Each piece plays after the piece before it, with 40 ms of silence before it and 150 ms after it (the sound system
//   fades the last 83 ms of a clip). A line of the reader has 250 ms after it.
#include "tfn_app_priv.h"
#include "tfn_speech.h"
#include "tfn_listen.h"

#if BOARD_TTS
#include "esp_pm.h"
#include "freertos/queue.h"
#include "app.h"
#include "audio.h"
#include "notify.h"
#include "settings.h"

static const char *TAG = "speech";

#define SQ_N         8    // jobs in the queue
#define TEXT_MAX     480  // bytes of one text (a notice: its app, its title, its text; a line of the reader)
#define PATH_MAX_SP  128
#define PIECES       10  // sounds that play or wait (the pieces of the texts, the files)
#define LEAD_MS      40
#define TAIL_MS      150
#define LINE_TAIL_MS 250
#define WAIT_GAME    (10 * 60 * 1000)
#define RD_LINES_MAX 20000
#define RD_SOON_MS   700  // the play task reads the next line when the voice before it has this much left
#define RD_TRIES     3    // a line that fails 3 times is passed (no voice for it)
#define RD_SKIPS     6    // 6 lines passed in a row: the reader shows a fault (no voice data, no memory)
#define SR           AUDIO_SAMPLE_RATE

static speech_ops_t s_ops;
static volatile int s_w = -1;  // the watcher of the module (-1: no module)
static TaskHandle_t s_task, s_ptask;
static QueueHandle_t s_q;
static SemaphoreHandle_t s_mx;  // the sounds (s_pc, s_play_end) and the reader (s_rd)
static volatile bool s_stop, s_busy, s_open;
static volatile bool s_quiet_req;  // the ears want the memory of the model (tfa_speech_quiet)
static esp_pm_lock_handle_t s_pm_cpu, s_pm_awake;
static bool s_pm_held;
static volatile bool s_pm_kick;  // (the calls of the apps ask; only the speech task takes and gives the PM locks)
static char s_state[64] = "NO VOICE MODULE (THE APP VOICE)";

// The lock s_mx with a watch: a task that waits more than 2 s for it writes the holder (its task and the line of
// its take) to the log, then waits on. (The WIKI frame and the console wait for this lock: a long hold stops them.)
static volatile TaskHandle_t s_mx_task;
static volatile int s_mx_line;
static volatile int64_t s_mx_t;

static void mx_take(int line)
{
    if (xSemaphoreTake(s_mx, pdMS_TO_TICKS(2000)) != pdTRUE) {
        const TaskHandle_t h = s_mx_task;
        ESP_LOGE(TAG, "the reader lock: '%s' (line %d) waits 2 s; '%s' holds it from line %d for %d ms (state %d)",
                 pcTaskGetName(NULL), line, h ? pcTaskGetName(h) : "?", s_mx_line,
                 (int)(esp_timer_get_time() / 1000 - s_mx_t), h ? (int)eTaskGetState(h) : -1);
        xSemaphoreTake(s_mx, portMAX_DELAY);
        ESP_LOGW(TAG, "the reader lock: '%s' (line %d) got it after %d ms", pcTaskGetName(NULL), line,
                 (int)(esp_timer_get_time() / 1000 - s_mx_t));
    }
    s_mx_task = xTaskGetCurrentTaskHandle();
    s_mx_line = line;
    s_mx_t = esp_timer_get_time() / 1000;
}

static void mx_give(void)
{
    const int held = (int)(esp_timer_get_time() / 1000 - s_mx_t);
    const int line = s_mx_line;
    s_mx_task = NULL;
    xSemaphoreGive(s_mx);
    if (held > 500) {
        ESP_LOGW(TAG, "the reader lock: '%s' held it %d ms (from line %d)", pcTaskGetName(NULL), held, line);
    }
}
#define MX_TAKE() mx_take(__LINE__)
#define MX_GIVE() mx_give()

enum {
    J_SAY,
    J_MAKE
};
typedef struct {
    uint8_t kind;
    char path[PATH_MAX_SP];
    char text[TEXT_MAX];
} job_t;

// the head of a voice file: then n samples (16-bit mono, SR)
typedef struct {
    char magic[4];  // "TVC2" (TVC1: the files of 1.0.132, some with lost parts: made again)
    uint32_t hash;  // FNV-1a of the text
    uint32_t n;
    uint32_t rate;
} tvc_head_t;

typedef struct {
    int16_t *pcm;
    int n;
    int64_t start_ms, end_ms;
    int line;  // a line of the reader (-1: not)
} piece_t;
static piece_t s_pc[PIECES];
static int64_t s_play_end;  // ms: the end of the last sound that plays or waits

// THE READER
typedef struct {
    bool on;
    uint32_t gen;  // + 1 at each open and close (a make of an old reader is not kept)
    char text[PATH_MAX_SP], cache[PATH_MAX_SP];
    int n;
    uint32_t *off;  // n + 1 offsets of the lines in the text file
    int32_t *dur;   // ms of the voice of each line: 0 = not made yet, -1 = no voice (an empty line, a fault)
    int next;       // the next line for the play task
    int hold_ms, ahead_max_ms;
    bool play, waiting, fault;
    const app_t *app;  // the app that opened it (the make goes on in it, in the menu and with the screen off)
    int fail_line, fail_n, skips;  // the faults of the make (rd_make_one)
    int64_t make_ms, made_ms, made_chars;
} reader_t;
static reader_t s_rd;
static volatile bool s_rd_cancel;

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static uint32_t fnv(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}

static void *ps_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

// ---------------------------------------------------------------- the sounds
// the sounds that ended go; with s_mx
static void pieces_reap(bool all)
{
    (void)all;
    const int64_t t = now_ms();
    for (int i = 0; i < PIECES; i++) {
        piece_t *p = &s_pc[i];
        if (p->pcm && t > p->end_ms + 200) {
            heap_caps_free(p->pcm);
            p->pcm = NULL;
        }
    }
}

// the sounds of the reader (reader true) or of the said texts and the files stop; with s_mx
static void sounds_off(bool reader)
{
    for (int i = 0; i < PIECES; i++) {
        piece_t *p = &s_pc[i];
        if (p->pcm && (p->line >= 0) == reader) {
            audio_stop_mem(p->pcm, (size_t)p->n * 2);
            heap_caps_free(p->pcm);
            p->pcm = NULL;
        }
    }
    int64_t e = 0;
    for (int i = 0; i < PIECES; i++) {
        if (s_pc[i].pcm && s_pc[i].end_ms > e) {
            e = s_pc[i].end_ms;
        }
    }
    s_play_end = e;
}

static void reader_sounds_off(void)
{
    sounds_off(true);
}

static void says_off(void)
{
    sounds_off(false);
}

static piece_t *piece_free(void)
{
    pieces_reap(false);
    for (int i = 0; i < PIECES; i++) {
        if (!s_pc[i].pcm) {
            return &s_pc[i];
        }
    }
    return NULL;
}

// play b (total samples, the silence is in it) after the sound before it; the piece keeps b; with s_mx
static void play_after(piece_t *p, int16_t *b, int total, int line)
{
    const int64_t t = now_ms(), start = s_play_end > t ? s_play_end : t;
    audio_voice_t v = {.delay = (uint32_t)((start - t) * (SR / 1000)),
                       .dur = (uint32_t)total,
                       .wave = W_SAMPLE,
                       .vol = 100,
                       .shape = AUDIO_SUSTAIN,
                       .sample = b,
                       .sample_len = (uint32_t)total,
                       .rate = 1.0f};
    audio_play(&v);
    p->pcm = b;
    p->n = total;
    p->line = line;
    p->start_ms = start;
    p->end_ms = start + total / (SR / 1000);
    s_play_end = p->end_ms;
}

// a buffer with lead ms of silence, n samples, tail ms of silence (PSRAM); *total = its samples
static int16_t *padded(int n, int tail_ms, int *total)
{
    const int lead = LEAD_MS * (SR / 1000), tail = tail_ms * (SR / 1000);
    *total = lead + n + tail;
    int16_t *b = ps_alloc((size_t)*total * 2);
    if (!b) {
        tfa_listen_quiet();  // (the idle ears of TALK give their memory: a line of the reader needs up to 1 MB)
        b = ps_alloc((size_t)*total * 2);
    }
    if (b) {
        memset(b, 0, (size_t)lead * 2);
        memset(b + lead + n, 0, (size_t)tail * 2);
    }
    return b;
}

static void pm_hold(bool on)
{
    if (on == s_pm_held || !s_pm_cpu) {
        return;
    }
    if (on) {
        esp_pm_lock_acquire(s_pm_cpu);
        esp_pm_lock_acquire(s_pm_awake);
    } else {
        esp_pm_lock_release(s_pm_cpu);
        esp_pm_lock_release(s_pm_awake);
    }
    s_pm_held = on;
}

// a piece of the module (a text to say): a copy with the silence around it, played after the sound before it
static bool sink_play(void *user, const int16_t *pcm, int n)
{
    (void)user;
    if (s_stop || n <= 0) {
        return false;
    }
    piece_t *p = NULL;
    while (!s_stop) {
        MX_TAKE();
        p = piece_free();
        MX_GIVE();
        if (p) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(50));  // (the module is far ahead of the sound: it waits)
    }
    if (!p) {
        return false;
    }
    int total;
    int16_t *b = padded(n, TAIL_MS, &total);
    if (!b) {
        ESP_LOGW(TAG, "no memory for a piece of %d samples", n);
        return true;  // (this piece is lost; the next can come)
    }
    memcpy(b + LEAD_MS * (SR / 1000), pcm, (size_t)n * 2);
    MX_TAKE();
    if (p->pcm) {  // (the play task took the slot meanwhile)
        p = piece_free();
    }
    if (p) {
        play_after(p, b, total, -1);
    } else {
        heap_caps_free(b);
    }
    MX_GIVE();
    return true;
}

// ---------------------------------------------------------------- the module
// A CALL of the module (say, close) runs on the speech task with NO lock of the watchers: a call can take minutes (a
// long text while the memory is low), and each app install waits for that lock (the web task stopped
// behind it). The code of the module must stay while a call runs: when its watcher goes,
// tfa_speech_slot_gone() takes the module away (no new call), asks the call to stop (s_call_stop: the module looks
// at it between its pieces), and waits for its end.
enum {
    CALL_JOB = 1,  // a text to say or to make (speech_stop stops it)
    CALL_READER    // a line of the reader (speech_read_close / _open stop it)
};
static portMUX_TYPE s_call_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool s_in_call, s_call_stop;
static volatile int s_call_kind, s_w_call = -1;  // (s_w_call: the watcher of the call: its memory set)

static bool call_begin(int kind)
{
    portENTER_CRITICAL(&s_call_mux);
    const bool ok = s_w >= 0;
    if (ok) {
        s_in_call = true;
        s_w_call = s_w;
        s_call_kind = kind;
    }
    portEXIT_CRITICAL(&s_call_mux);
    return ok;
}

static void call_end(void)
{
    s_call_kind = 0;
    s_w_call = -1;
    s_in_call = false;
}

// the module closes
static void module_close(void)
{
    if (call_begin(CALL_JOB)) {
        if (s_open && s_ops.close) {
            s_ops.close();
        }
        call_end();
    }
    s_open = false;
}

// the module makes the voice of a text into sink (the CPU at full speed, a calm main loop); kind: CALL_*
static int module_say(const char *text, speech_sink_fn sink, void *user, int kind)
{
    pm_hold(true);
    s_busy = true;
    tfa_icons_lean = true;  // (the menu frees its icon files at its next frame: the model needs the PSRAM)
    if (!strcmp(app_screen_state(), "off")) {
        tfa_icons_lean_now();  // (the screen is off: no next frame)
    }
    int r = -1;
    s_call_stop = kind == CALL_READER ? s_rd_cancel : s_stop;
    if (call_begin(kind)) {
        if (s_ops.say && !s_call_stop) {
            s_open = true;
            r = s_ops.say(text, sink, user, &s_call_stop);
        }
        call_end();
    }
    s_busy = false;
    return r;
}

// a word in capitals only (the screen fonts of many apps have no small letters): small letters, so that the voice says
// the word and does not spell it; a word of 1-3 letters stays (TV, USA)
static void soften(char *s)
{
    for (char *w = s; *w;) {
        char *e = w;
        int up = 0, low = 0;
        while (*e && *e != ' ') {
            up += *e >= 'A' && *e <= 'Z';
            low += *e >= 'a' && *e <= 'z';
            e++;
        }
        if (!low && up > 3) {
            for (char *c = w + 1; c < e; c++) {
                *c = (char)(*c >= 'A' && *c <= 'Z' ? *c + 32 : *c);
            }
        }
        w = *e ? e + 1 : e;
    }
}

// ---------------------------------------------------------------- the voice files
typedef struct {
    FILE *f;
    int n;
    bool bad;
} fsink_t;

static bool sink_file(void *user, const int16_t *pcm, int n)
{
    fsink_t *s = user;
    if (n > 0 && fwrite(pcm, 2, (size_t)n, s->f) != (size_t)n) {
        s->bad = true;
        return false;
    }
    s->n += n;
    return true;
}

// the voice of text into path (a .tmp file, then the name); its samples, or < 0; kind: CALL_*
static int make_file(const char *text, const char *path, int kind)
{
    char tmp[PATH_MAX_SP + 8];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    fsink_t s = {.f = fopen(tmp, "wb")};
    if (!s.f) {
        ESP_LOGW(TAG, "cannot write %s", tmp);
        return -3;
    }
    tvc_head_t h = {.magic = {'T', 'V', 'C', '2'}, .hash = fnv(text), .rate = SR};
    fwrite(&h, sizeof h, 1, s.f);
    static char soft[TEXT_MAX];  // (the speech task only)
    snprintf(soft, sizeof soft, "%s", text);
    soften(soft);
    const int r = module_say(soft, sink_file, &s, kind);
    h.n = (uint32_t)s.n;
    const bool ok =
            !r && !s.bad && !s_call_stop && s.n > 0 && !fseek(s.f, 0, SEEK_SET) && fwrite(&h, sizeof h, 1, s.f) == 1;
    fclose(s.f);
    remove(path);
    if (!ok || rename(tmp, path)) {
        remove(tmp);
        return r ? r : -4;
    }
    return s.n;
}

// the head of a voice file (false: no file, another text)
static bool file_head(const char *path, const char *text, tvc_head_t *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    const bool ok = fread(h, sizeof *h, 1, f) == 1 && !memcmp(h->magic, "TVC2", 4) && h->rate == SR && h->n > 0 &&
                    (!text || h->hash == fnv(text));
    fclose(f);
    return ok;
}

// a voice file into a padded buffer; NULL: no file, no memory
static int16_t *file_load(const char *path, int tail_ms, int *total)
{
    tvc_head_t h;
    if (!file_head(path, NULL, &h) || h.n > 120 * SR) {
        return NULL;
    }
    int16_t *b = padded((int)h.n, tail_ms, total);
    if (!b) {
        return NULL;
    }
    FILE *f = fopen(path, "rb");
    const bool ok = f && !fseek(f, sizeof h, SEEK_SET) && fread(b + LEAD_MS * (SR / 1000), 2, h.n, f) == h.n;
    if (f) {
        fclose(f);
    }
    if (!ok) {
        heap_caps_free(b);
        return NULL;
    }
    return b;
}

// ---------------------------------------------------------------- the jobs
static bool may_wait_game(void)
{
    const int64_t t0 = now_ms();
    while (!s_stop && notify_game_now() && now_ms() - t0 < WAIT_GAME) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    return !s_stop && s_w >= 0;
}

static void job_run(job_t *j)
{
    // a game on the screen: the job waits (the voice takes both cores)
    if (!may_wait_game()) {
        return;
    }
    const int64_t t1 = now_ms();
    int r;
    if (j->kind == J_SAY) {
        if (audio_muted()) {
            return;
        }
        app_screen_wake();
        vTaskDelay(pdMS_TO_TICKS(120));
        snprintf(s_state, sizeof s_state, "SPEAKS");
        r = module_say(j->text, sink_play, NULL, CALL_JOB);
    } else {
        snprintf(s_state, sizeof s_state, "MAKES A VOICE FILE");
        r = make_file(j->text, j->path, CALL_JOB);
        r = r > 0 ? 0 : r;
    }
    snprintf(s_state, sizeof s_state, r ? "THE LAST TEXT FAILED (%d)" : "READY", r);
    ESP_LOGI(TAG, "%s, %d bytes of text: %s in %d ms", j->kind == J_SAY ? "say" : j->path, (int)strlen(j->text),
             r ? "a fault" : "made", (int)(now_ms() - t1));
}

static bool queue_job(int kind, const char *text, const char *path)
{
    if (!s_q || s_w < 0 || !text || !text[0]) {
        return false;
    }
    job_t *j = ps_alloc(sizeof *j);
    if (!j) {
        return false;
    }
    j->kind = (uint8_t)kind;
    snprintf(j->text, sizeof j->text, "%s", text);
    snprintf(j->path, sizeof j->path, "%s", path ? path : "");
    if (xQueueSend(s_q, &j, 0) != pdTRUE) {
        heap_caps_free(j);
        ESP_LOGW(TAG, "the queue is full: a text is not said");
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- the reader
static void line_path(const reader_t *r, int line, char *out, int max, bool dir)
{
    if (dir) {
        snprintf(out, (size_t)max, "%s/d%03d", r->cache, line / 100);
    } else {
        snprintf(out, (size_t)max, "%s/d%03d/%05d.tvc", r->cache, line / 100, line);
    }
}

// the text of a line (from o0 to o1 in the file; soft capitals, no line end); false: no file
static bool line_text(const char *file, uint32_t o0, uint32_t o1, char *out, int max)
{
    FILE *f = fopen(file, "rb");
    if (!f) {
        return false;
    }
    int len = (int)(o1 - o0);
    len = len < max - 1 ? len : max - 1;
    const bool ok = !fseek(f, (long)o0, SEEK_SET) && fread(out, 1, (size_t)len, f) == (size_t)len;
    fclose(f);
    out[ok ? len : 0] = 0;
    while (len > 0 && (out[len - 1] == '\n' || out[len - 1] == '\r' || out[len - 1] == ' ')) {
        out[--len] = 0;
    }
    soften(out);
    return ok;
}

// the line that plays now (or next); with s_mx
static int rd_now_line(void)
{
    const int64_t t = now_ms();
    int best = -1;
    int64_t bs = 0;
    for (int i = 0; i < PIECES; i++) {
        const piece_t *p = &s_pc[i];
        if (p->pcm && p->line >= 0 && t < p->end_ms && (best < 0 || p->start_ms < bs)) {
            best = p->line;
            bs = p->start_ms;
        }
    }
    return best >= 0 ? best : s_rd.next;
}

// the voice that is made and not yet played (ms), and the first line with no voice; with s_mx
static int rd_ahead(int *first_unmade)
{
    const int64_t t = now_ms();
    int64_t a = 0;
    for (int i = 0; i < PIECES; i++) {
        const piece_t *p = &s_pc[i];
        if (p->pcm && p->line >= 0 && t < p->end_ms) {
            a += p->end_ms - (t > p->start_ms ? t : p->start_ms);
        }
    }
    int l = s_rd.next;
    for (; l < s_rd.n && s_rd.dur[l]; l++) {
        a += s_rd.dur[l] > 0 ? s_rd.dur[l] + LEAD_MS + LINE_TAIL_MS : 0;
    }
    *first_unmade = l;
    return (int)a;
}

// the speed of the voice (made ms / ms to make it) in 1/1000, and the ms of voice of a byte of text
static void rd_rates(int *r_pm, int *ms_char)
{
    *r_pm = s_rd.make_ms > 2000 ? (int)(s_rd.made_ms * 1000 / s_rd.make_ms) : 450;
    *r_pm = *r_pm < 50 ? 50 : *r_pm;
    *ms_char = s_rd.made_chars > 40 ? (int)(s_rd.made_ms / s_rd.made_chars) : 62;
}

// the time until the play can start (0: now); with s_mx
static int rd_wait_ms(void)
{
    int fu;
    const int a = rd_ahead(&fu);
    if (fu >= s_rd.n) {
        return 0;
    }
    int r_pm, msc;
    rd_rates(&r_pm, &msc);
    int64_t u = 0;  // the voice of the lines with no voice yet (an estimate)
    for (int l = fu; l < s_rd.n; l++) {
        u += s_rd.dur[l] ? 0 : (int64_t)(s_rd.off[l + 1] - s_rd.off[l]) * msc;
    }
    // no stop when the make of u (u / r) ends before the play of a + u: a >= u * (1 / r - 1); the wait = that - a
    const int64_t need = u * (1000 - r_pm) / r_pm;
    if (need <= a) {
        return 0;
    }
    if (s_rd.hold_ms <= a) {
        return 0;
    }
    if (s_rd.hold_ms < need) {
        return (int)((s_rd.hold_ms - a) * 1000 / r_pm);
    }
    return (int)(need - a);
}

// the make may go on now: not in a game, and in the app of the reader, the menu or with the screen off
static bool rd_may_make(void)
{
    if (notify_game_now()) {
        return false;
    }
    const app_t *a = app_current();
    return a == s_rd.app || a == &app_menu || !a;
}

// the speech task: make the voice of the next line with no voice; false: nothing to make now
static bool rd_make_one(void)
{
    static char text[TEXT_MAX], path[PATH_MAX_SP + 64];
    MX_TAKE();
    int fu = 0;
    const int ahead = s_rd.on ? rd_ahead(&fu) : 0;
    const bool go = s_rd.on && fu < s_rd.n && ahead < s_rd.ahead_max_ms && rd_may_make() && s_w >= 0;
    reader_t r = s_rd;  // (a copy of the names; the line table can go at a close: only its 2 offsets are used)
    const uint32_t o0 = go ? s_rd.off[fu] : 0, o1 = go ? s_rd.off[fu + 1] : 0;
    r.off = NULL;
    r.dur = NULL;
    MX_GIVE();
    if (!go) {
        return false;
    }
    int32_t dur = -1;
    tvc_head_t h;
    const int64_t t0 = now_ms();
    bool made = false;
    if (line_text(r.text, o0, o1, text, sizeof text)) {
        line_path(&r, fu, path, sizeof path, false);
        if (!text[0]) {
            dur = -1;
        } else if (file_head(path, text, &h)) {
            dur = (int32_t)((int64_t)h.n * 1000 / SR);
        } else {
            line_path(&r, fu, path, sizeof path, true);
            mkdir(r.cache, 0777);
            mkdir(path, 0777);
            line_path(&r, fu, path, sizeof path, false);
            snprintf(s_state, sizeof s_state, "READS ALOUD: MAKES LINE %d OF %d", fu + 1, r.n);
            s_rd_cancel = false;
            tfa_listen_quiet();  // (the idle ears of TALK give their memory: else the line fails and, after its tries,
                                 // the reader skips it)
            const int n = make_file(text, path, CALL_READER);
            snprintf(s_state, sizeof s_state, "READY");
            if (n <= 0) {
                // (the voice did not start or made nothing: no memory for the model now, an app holds it; try the
                // line again after 2 s, with the module closed: its memory goes back)
                MX_TAKE();
                if (s_rd.on && s_rd.gen == r.gen && !s_rd_cancel) {
                    s_rd.fail_n = s_rd.fail_line == fu ? s_rd.fail_n + 1 : 1;
                    s_rd.fail_line = fu;
                    if (s_rd.fail_n >= RD_TRIES) {
                        s_rd.dur[fu] = -1;
                        s_rd.fail_n = 0;
                        s_rd.fault = ++s_rd.skips >= RD_SKIPS;
                    }
                }
                MX_GIVE();
                vTaskDelay(pdMS_TO_TICKS(2000));
                return false;
            }
            dur = (int32_t)((int64_t)n * 1000 / SR);
            made = true;
        }
    } else {
        MX_TAKE();
        if (s_rd.gen == r.gen) {
            s_rd.fault = true;
        }
        MX_GIVE();
        return false;
    }
    MX_TAKE();
    if (s_rd.on && s_rd.gen == r.gen) {
        s_rd.dur[fu] = dur;
        if (made) {
            s_rd.skips = 0;
            s_rd.make_ms += now_ms() - t0;
            s_rd.made_ms += dur;
            s_rd.made_chars += (int64_t)strlen(text);
        }
    }
    MX_GIVE();
    if (made) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    return true;
}

// the play task: the next line into the sound system when its time comes
static void rd_play_tick(void)
{
    static char path[PATH_MAX_SP + 64];
    MX_TAKE();
    pieces_reap(false);
    if (!s_rd.on || !s_rd.play || s_rd.next >= s_rd.n || notify_game_now()) {
        MX_GIVE();
        return;
    }
    const int64_t t = now_ms();
    // the lines with no voice (empty, a fault) are passed
    while (s_rd.next < s_rd.n && s_rd.dur[s_rd.next] < 0) {
        s_rd.next++;
    }
    if (s_rd.next >= s_rd.n || s_play_end - t > RD_SOON_MS) {
        MX_GIVE();
        return;
    }
    if (!s_rd.dur[s_rd.next]) {
        s_rd.waiting = true;  // (the voice of the next line is not ready: wait for enough voice ahead again)
        MX_GIVE();
        return;
    }
    if (s_rd.waiting && rd_wait_ms() > 0) {
        MX_GIVE();
        return;
    }
    s_rd.waiting = false;
    const int line = s_rd.next;
    const uint32_t gen = s_rd.gen;
    line_path(&s_rd, line, path, sizeof path, false);
    MX_GIVE();
    int total;
    int16_t *b = file_load(path, LINE_TAIL_MS, &total);
    MX_TAKE();
    piece_t *p = b ? piece_free() : NULL;
    if (p && s_rd.on && s_rd.play && s_rd.gen == gen && s_rd.next == line) {
        play_after(p, b, total, line);
        s_rd.next = line + 1;
    } else {
        heap_caps_free(b);
        if (!b && s_rd.gen == gen && s_rd.next == line) {
            s_rd.dur[line] = 0;  // (the file went: make it again)
        }
    }
    MX_GIVE();
}

static void play_task(void *arg)
{
    (void)arg;
    for (;;) {
        rd_play_tick();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static bool rd_active(void)
{
    return (s_rd.on && s_rd.play && s_rd.next < s_rd.n) || s_play_end > now_ms();
}

// ---------------------------------------------------------------- the speech task
static void speech_task(void *arg)
{
    (void)arg;
    int idle = 0;
    for (;;) {
        job_t *j = NULL;
        if (s_pm_kick) {
            s_pm_kick = false;
            pm_hold(true);
        }
        const bool worked = rd_make_one();
        if (worked) {
            idle = 0;
        }
        if (xQueueReceive(s_q, &j, pdMS_TO_TICKS(worked ? 0 : 500)) == pdTRUE && j) {  // (NULL: a wake)
            idle = 0;
            if (!s_stop) {
                tfa_listen_quiet();  // (the ears of TALK give their memory when they are idle: the model needs it)
                job_run(j);
            }
            heap_caps_free(j);
            continue;
        }
        MX_TAKE();
        pieces_reap(false);
        if (s_stop) {
            says_off();
            s_stop = false;
        }
        const int64_t pe = s_play_end;
        MX_GIVE();
        idle = s_busy || worked ? 0 : idle + 1;
        if (s_open && (idle >= 6 || (s_quiet_req && !s_busy))) {  // (3 s with no job, or the ears need the memory)
            module_close();
        }
        s_quiet_req = false;
        if (!s_open && now_ms() > pe + 300) {
            tfa_icons_lean = false;
            if (!rd_active()) {
                pm_hold(false);
            }
        }
    }
}

bool speech_offer(const speech_ops_t *ops)
{
    if (!ops || !ops->say || !ops->close || tfa_wcur < 0 || !tfa_on_watch_task()) {
        return false;
    }
    if (!s_q) {
        s_q = xQueueCreate(SQ_N, sizeof(job_t *));
        s_mx = xSemaphoreCreateMutex();
        esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, "speech", &s_pm_cpu);
        esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "speech", &s_pm_awake);
    }
    // (their stacks in PSRAM: they never write the flash; 24 KB: the module runs on it)
    if (s_q && s_mx && !s_task &&
        xTaskCreatePinnedToCoreWithCaps(speech_task, "speech", 24576, NULL, 1, &s_task, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_task = NULL;
    }
    if (s_task && !s_ptask &&
        xTaskCreatePinnedToCoreWithCaps(play_task, "spplay", 4096, NULL, 1, &s_ptask, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_ptask = NULL;
    }
    if (!s_q || !s_task || !s_ptask) {
        return false;
    }
    s_ops = *ops;
    s_open = false;
    s_w = tfa_wcur;
    snprintf(s_state, sizeof s_state, "READY");
    ESP_LOGI(TAG, "the voice of %s is ready", tfa_w[tfa_wcur].id);
    return true;
}

void tfa_speech_slot_gone(int w)
{
    if (w >= 0 && w == s_w) {
        portENTER_CRITICAL(&s_call_mux);
        s_w = -1;  // (no new call)
        portEXIT_CRITICAL(&s_call_mux);
        s_call_stop = true;
        for (int i = 0; i < 6000 && s_in_call; i++) {
            vTaskDelay(pdMS_TO_TICKS(10));  // (the call ends at its next piece: some seconds)
        }
        if (s_in_call) {
            ESP_LOGE(TAG, "the call of the voice module did not end in 60 s");
        }
        if (s_open && s_ops.close) {
            const int old = tfa_wcur;
            tfa_wcur = w;   // (its frees go to its own set)
            s_ops.close();  // (also its tasks end before its code goes)
            tfa_wcur = old;
        }
        s_w = -1;
        s_open = false;
        memset(&s_ops, 0, sizeof s_ops);
        snprintf(s_state, sizeof s_state, "NO VOICE MODULE (THE APP VOICE)");
    }
}

bool tfa_speech_task_now(int *w)
{
    const int c = s_w_call;
    if (s_task && c >= 0 && xTaskGetCurrentTaskHandle() == s_task) {
        *w = c;
        return true;
    }
    return false;
}

void speech_init(void)
{
}

bool speech_ready(void)
{
    return s_w >= 0;
}

bool speech_calm(void)
{
    return s_busy;
}

void tfa_speech_quiet(void)
{
    if (s_open && s_q) {
        s_quiet_req = true;
        job_t *wake = NULL;
        xQueueSend(s_q, &wake, 0);  // (the task closes the model at once, not after 3 s with no job)
    }
}

bool tfa_speech_open(void)
{
    return s_open;
}

const char *speech_state(void)
{
    return s_state;
}

void speech_say(const char *text)
{
    queue_job(J_SAY, text, NULL);
}

void speech_stop(void)
{
    s_stop = true;
    if (s_call_kind == CALL_JOB) {
        s_call_stop = true;
    }
    job_t *j;
    while (s_q && xQueueReceive(s_q, &j, 0) == pdTRUE) {
        heap_caps_free(j);
    }
}

bool speech_make(const char *text, const char *path)
{
    return path && path[0] && strlen(path) < PATH_MAX_SP && queue_job(J_MAKE, text, path);
}

bool speech_file_ok(const char *path, const char *text)
{
    tvc_head_t h;
    return path && file_head(path, text, &h);
}

int speech_play_file(const char *path)
{
    if (!s_mx || !path) {
        return 0;
    }
    int total;
    int16_t *b = file_load(path, TAIL_MS, &total);
    if (!b) {
        return 0;
    }
    MX_TAKE();
    piece_t *p = piece_free();
    if (p) {
        play_after(p, b, total, -1);
    } else {
        heap_caps_free(b);
    }
    MX_GIVE();
    s_pm_kick = true;  // (the speech task lets it go after the end)
    return p ? total / (SR / 1000) : 0;
}

// ---------------------------------------------------------------- the reader: the calls of the apps
static void rd_free(void)
{
    heap_caps_free(s_rd.off);
    heap_caps_free(s_rd.dur);
    s_rd.off = NULL;
    s_rd.dur = NULL;
    s_rd.n = 0;
}

// the line table of a text file (offsets of the lines); false: no file
static bool rd_index(const char *path, uint32_t **off_out, int *n_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    int cap = 1024, n = 0;
    uint32_t *off = ps_alloc((size_t)cap * 4);
    char *buf = ps_alloc(4096);
    bool ok = off && buf;
    uint32_t pos = 0;
    if (ok) {
        off[n++] = 0;
    }
    size_t got;
    while (ok && (got = fread(buf, 1, 4096, f)) > 0) {
        for (size_t i = 0; i < got && ok; i++) {
            if (buf[i] != '\n') {
                continue;
            }
            if (n >= RD_LINES_MAX + 1) {
                break;
            }
            if (n == cap) {
                uint32_t *o = heap_caps_realloc(off, (size_t)cap * 2 * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                ok = o != NULL;
                off = ok ? o : off;
                cap *= 2;
            }
            if (ok) {
                off[n++] = pos + (uint32_t)i + 1;
            }
        }
        pos += (uint32_t)got;
    }
    fclose(f);
    heap_caps_free(buf);
    if (ok && n > 0 && off[n - 1] < pos) {  // (a last line with no line end)
        if (n == cap) {
            uint32_t *o = heap_caps_realloc(off, (size_t)(cap + 1) * 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
            ok = o != NULL;
            off = ok ? o : off;
        }
        if (ok) {
            off[n++] = pos;
        }
    }
    if (!ok || n < 2) {
        heap_caps_free(off);
        return false;
    }
    *off_out = off;
    *n_out = n - 1;
    return true;
}

bool speech_read_open(const speech_read_cfg_t *c)
{
    if (!s_mx || s_w < 0 || !c || !c->text || !c->cache || strlen(c->text) >= PATH_MAX_SP ||
        strlen(c->cache) >= PATH_MAX_SP - 20) {
        return false;
    }
    speech_read_close();
    uint32_t *off = NULL;
    int n = 0;
    if (!rd_index(c->text, &off, &n)) {
        ESP_LOGW(TAG, "reader: no text %s", c->text);
        return false;
    }
    int32_t *dur = heap_caps_calloc((size_t)n, 4, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dur) {
        heap_caps_free(off);
        return false;
    }
    MX_TAKE();
    rd_free();
    s_rd.gen++;
    snprintf(s_rd.text, sizeof s_rd.text, "%s", c->text);
    snprintf(s_rd.cache, sizeof s_rd.cache, "%s", c->cache);
    s_rd.off = off;
    s_rd.dur = dur;
    s_rd.n = n;
    s_rd.next = c->line > 0 && c->line < n ? c->line : 0;
    s_rd.hold_ms = c->hold_s > 0 ? c->hold_s * 1000 : 0;
    s_rd.ahead_max_ms = (c->ahead_s > 0 ? c->ahead_s : 30 * 60) * 1000;
    s_rd.play = c->play;
    s_rd.waiting = true;
    s_rd.fault = false;
    s_rd.fail_line = -1;
    s_rd.fail_n = s_rd.skips = 0;
    s_rd.app = app_current();
    s_rd.make_ms = s_rd.made_ms = s_rd.made_chars = 0;
    s_rd.on = true;
    MX_GIVE();
    s_pm_kick = true;
    ESP_LOGI(TAG, "reader: %s, %d lines, from %d", c->text, n, s_rd.next);
    return true;
}

void speech_read_play(bool on)
{
    if (!s_mx) {
        return;
    }
    MX_TAKE();
    if (s_rd.on && s_rd.play != on) {
        if (!on) {
            s_rd.next = rd_now_line();  // (the line that played starts again at the next play)
            reader_sounds_off();
        }
        s_rd.play = on;
        s_rd.waiting = true;
    }
    MX_GIVE();
    if (on) {
        s_pm_kick = true;
    }
}

void speech_read_close(void)
{
    if (!s_mx) {
        return;
    }
    s_rd_cancel = true;
    if (s_call_kind == CALL_READER) {
        s_call_stop = true;
    }
    MX_TAKE();
    if (s_rd.on) {
        reader_sounds_off();
        s_rd.on = false;
        s_rd.gen++;
        rd_free();
    }
    MX_GIVE();
}

void speech_read_get(speech_read_t *st)
{
    memset(st, 0, sizeof *st);
    if (!s_mx) {
        return;
    }
    MX_TAKE();
    if (s_rd.on) {
        int fu, r_pm, msc;
        st->play = s_rd.play;
        st->lines = s_rd.n;
        st->line = rd_now_line();
        st->ahead_ms = rd_ahead(&fu);
        st->made = fu;
        rd_rates(&r_pm, &msc);
        st->rate_pc = r_pm / 10;
        const bool sounding = s_play_end > now_ms();
        if (s_rd.fault) {
            st->state = SPR_FAULT;
        } else if (st->line >= s_rd.n && !sounding) {
            st->state = SPR_END;
        } else if (!s_rd.play) {
            st->state = SPR_MAKE;
        } else if (s_rd.waiting && !sounding) {
            st->state = SPR_WAIT;
            st->wait_ms = rd_wait_ms();
        } else {
            st->state = SPR_PLAY;
        }
    }
    MX_GIVE();
}

// ---------------------------------------------------------------- the notices
bool speech_notices_on(void)
{
    return store_get("sp_ntc", 0) != 0;
}

void speech_set_notices(bool on)
{
    store_set("sp_ntc", on ? 1 : 0);
}

void speech_notice(const char *app, const char *title, const char *text)
{
    if (s_w < 0 || !speech_notices_on()) {
        return;
    }
    char t[TEXT_MAX];
    snprintf(t, sizeof t, "%s. %s. %s", app ? app : "", title ? title : "", text ? text : "");
    soften(t);
    queue_job(J_SAY, t, NULL);
}

#else
// (a board with no voice: the ABI names)
bool speech_offer(const speech_ops_t *ops)
{
    (void)ops;
    return false;
}
bool speech_ready(void)
{
    return false;
}
void speech_say(const char *text)
{
    (void)text;
}
void speech_stop(void)
{
}
bool speech_make(const char *text, const char *path)
{
    (void)text;
    (void)path;
    return false;
}
bool speech_file_ok(const char *path, const char *text)
{
    (void)path;
    (void)text;
    return false;
}
int speech_play_file(const char *path)
{
    (void)path;
    return 0;
}
bool speech_read_open(const speech_read_cfg_t *c)
{
    (void)c;
    return false;
}
void speech_read_play(bool on)
{
    (void)on;
}
void speech_read_close(void)
{
}
void speech_read_get(speech_read_t *st)
{
    memset(st, 0, sizeof *st);
}
#endif
