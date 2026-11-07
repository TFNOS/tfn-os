// NOTIFICATIONS (notify.h): the list of the notices, the rules, the banner and the center.
#include "notify.h"
#include "tfn/theme.h"
#include "tfn_extras.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "audio.h"
#include "gfx.h"
#include "kfont.h"
#include "settings.h"
#include "alarm.h"
#include "tz.h"
#include "storage.h"
#include "sysui.h"
#include "uikit.h"
#include "watch.h"
#include "../libs/kscroll/kscroll.h"

static const char *TAG = "notify";

#define NT_FILE   STORAGE_ROOT "/notify.bin"
#define NT_RATE   10                // at most this many notices of one app in an hour (not for NT_SYSTEM)
#define NT_NANS   8                 // the answers of the buttons that wait for the manager
#define NT_NAPPS  16                // the apps that sent notices (the SETUP page)
#define BANNER_MS 4000              // the banner stays this long (a finger on it holds it)
#define WAIT_MS   (30 * 60 * 1000)  // a notice waits this long for its banner (the screen off, the panel open)

typedef struct {
    char cb[NT_LINK];
    uint8_t a;  // the button: 0, 1, 2
    uint32_t t;
} nans_t;

static notice_t *s_n;    // PSRAM, NT_MAX places; [0] = the newest
static int64_t *s_mono;  // the board time (ms) of each notice (the rate)
static int s_cnt;
static uint32_t s_next = 1, s_gen;  // the next id; + 1 for each change of the list
static nans_t s_ans[NT_NANS];
static int s_nans;
static char s_apps[NT_NAPPS][NT_NAME];
static int s_napps;
static SemaphoreHandle_t s_lock;
static volatile bool s_dirty, s_badges;
static int64_t s_dirty_at;
static volatile uint32_t s_new_id;  // the newest notice that waits for a banner
static volatile int64_t s_new_mono;
static volatile int s_new_cnt;  // the notices that came for a banner since the last banner start

#define LOCK()   xSemaphoreTake(s_lock, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(s_lock)

static int64_t mono_ms(void)
{
    return esp_timer_get_time() / 1000;
}

// ---------------------------------------------------------------- the settings
static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}
// the store key of an app: bits 0-1 the mode (NM_*), bit 2 WAKE, bit 3 NO SOUND (0 = the defaults: ON, no wake, sound)
static void app_key(const char *app, char key[16])
{
    snprintf(key, 16, "na_%08lx", (unsigned long)name_hash(app));
}
static uint32_t app_bits(const char *app)
{
    char k[16];
    app_key(app, k);
    return store_get(k, 0);
}
static void app_bits_set(const char *app, uint32_t mask, uint32_t val)
{
    char k[16];
    app_key(app, k);
    store_set(k, (store_get(k, 0) & ~mask) | (val & mask));
}
int notify_mode(const char *app)
{
    int m = (int)(app_bits(app) & 3);
    return m > NM_OFF ? NM_ON : m;
}
void notify_set_mode(const char *app, int m)
{
    app_bits_set(app, 3, (uint32_t)(m < 0 || m > NM_OFF ? NM_ON : m));
}
bool notify_wake(const char *app)
{
    return (app_bits(app) & 4) != 0;
}
void notify_set_wake(const char *app, bool on)
{
    app_bits_set(app, 4, on ? 4 : 0);
}
bool notify_sound(const char *app)
{
    return !(app_bits(app) & 8);
}
void notify_set_sound(const char *app, bool on)
{
    app_bits_set(app, 8, on ? 0 : 8);
}
bool notify_all_on(void)
{
    return store_get("nt_off", 0) == 0;
}
void notify_set_all(bool on)
{
    store_set("nt_off", on ? 0 : 1);
}
int notify_dnd(void)
{
    uint32_t v = store_get("nt_dnd", 0);
    return v > 2 ? 0 : (int)v;
}
void notify_set_dnd(int v)
{
    store_set("nt_dnd", (uint32_t)(v < 0 || v > 2 ? 0 : v));
}
bool notify_in_games(void)
{
    return store_get("nt_game", 0) != 0;
}
void notify_set_in_games(bool on)
{
    store_set("nt_game", on ? 1 : 0);
}

// DO NOT DISTURB now (the local hour: the home zone of the board, alarm.h; no banner and no sound then)
static bool dnd_now(void)
{
    int d = notify_dnd();
    time_t t = time(NULL);
    if (d == 2 || t < 1700000000) {
        return false;
    }
    char tz[64];
    tz_rule_t z;
    alarm_tz_get(tz, sizeof tz);
    int64_t loc = (int64_t)t + (tz_parse(tz, &z) ? tz_offset(&z, (int64_t)t, NULL) : 0);
    int h = (int)((loc / 3600) % 24);
    if (h < 0) {
        h += 24;
    }
    int a = d == 0 ? 23 : 22, b = d == 0 ? 7 : 8;
    return h >= a || h < b;
}

// ---------------------------------------------------------------- the file
typedef struct {
    char magic[4];
    uint16_t size_n, size_a;  // sizeof notice_t, sizeof nans_t (a change of the struct: the file is not read)
    uint32_t cnt, next, nans, napps;
} nhead_t;

static void file_save(void)
{
    LOCK();
    nhead_t h = {{'N', 'T', 'F', '1'}, sizeof(notice_t), sizeof(nans_t), (uint32_t)s_cnt, s_next,
                 (uint32_t)s_nans,     (uint32_t)s_napps};
    size_t n =
            sizeof h + (size_t)s_cnt * sizeof(notice_t) + (size_t)s_nans * sizeof(nans_t) + (size_t)s_napps * NT_NAME;
    uint8_t *b = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (b) {
        uint8_t *p = b;
        memcpy(p, &h, sizeof h), p += sizeof h;
        memcpy(p, s_n, (size_t)s_cnt * sizeof(notice_t)), p += (size_t)s_cnt * sizeof(notice_t);
        memcpy(p, s_ans, (size_t)s_nans * sizeof(nans_t)), p += (size_t)s_nans * sizeof(nans_t);
        memcpy(p, s_apps, (size_t)s_napps * NT_NAME);
    }
    s_dirty = false;
    UNLOCK();
    if (!b) {
        return;
    }
    if (!storage_write(NT_FILE, b, n)) {
        ESP_LOGW(TAG, "could not write %s", NT_FILE);
    }
    heap_caps_free(b);
}

static void file_load(void)
{
    size_t max = sizeof(nhead_t) + NT_MAX * sizeof(notice_t) + NT_NANS * sizeof(nans_t) + NT_NAPPS * NT_NAME, got = 0;
    uint8_t *b = heap_caps_malloc(max, MALLOC_CAP_SPIRAM);
    if (!b) {
        return;
    }
    if (storage_read(NT_FILE, b, max, &got) && got >= sizeof(nhead_t)) {
        nhead_t h;
        memcpy(&h, b, sizeof h);
        size_t need = sizeof h + (size_t)h.cnt * sizeof(notice_t) + (size_t)h.nans * sizeof(nans_t) +
                      (size_t)h.napps * NT_NAME;
        if (!memcmp(h.magic, "NTF1", 4) && h.size_n == sizeof(notice_t) && h.size_a == sizeof(nans_t) &&
            h.cnt <= NT_MAX && h.nans <= NT_NANS && h.napps <= NT_NAPPS && got == need) {
            const uint8_t *p = b + sizeof h;
            memcpy(s_n, p, h.cnt * sizeof(notice_t)), p += h.cnt * sizeof(notice_t);
            memcpy(s_ans, p, h.nans * sizeof(nans_t)), p += h.nans * sizeof(nans_t);
            memcpy(s_apps, p, h.napps * NT_NAME);
            s_cnt = (int)h.cnt;
            s_nans = (int)h.nans;
            s_napps = (int)h.napps;
            s_next = h.next ? h.next : 1;
            for (int i = 0; i < s_cnt; i++) {
                s_n[i].app[NT_NAME - 1] = s_n[i].title[NT_TITLE - 1] = s_n[i].text[NT_TEXT - 1] = 0;
                s_n[i].link[NT_LINK - 1] = s_n[i].cb[NT_LINK - 1] = 0;
                for (int k = 0; k < NT_NACT; k++) {
                    s_n[i].act[k][NT_ACT - 1] = 0;
                }
                s_mono[i] = -3600000;  // (old notices do not count for the rate)
            }
            for (int i = 0; i < s_napps; i++) {
                s_apps[i][NT_NAME - 1] = 0;
            }
        } else {
            ESP_LOGW(TAG, "%s is not of this firmware: no notices", NT_FILE);
        }
    }
    heap_caps_free(b);
}

void notify_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_n = heap_caps_calloc(NT_MAX, sizeof *s_n, MALLOC_CAP_SPIRAM);
    s_mono = heap_caps_calloc(NT_MAX, sizeof *s_mono, MALLOC_CAP_SPIRAM);
    if (!s_lock || !s_n || !s_mono) {
        ESP_LOGE(TAG, "no memory for the notices");
        heap_caps_free(s_n);
        s_n = NULL;
        return;
    }
    file_load();
    ESP_LOGI(TAG, "%d notices, %d unread", s_cnt, notify_unread(NULL));
}

static void changed(void)  // (under the lock)
{
    s_gen++;
    s_dirty = true;
    s_dirty_at = mono_ms();
    s_badges = true;
}

// ---------------------------------------------------------------- the list
uint32_t notify_post_ex(const char *app, const char *title, const char *text, const char *link,
                        const char *const act[NT_NACT], const char *cb)
{
    if (!s_n || !app || !app[0] || !notify_all_on() || notify_mode(app) == NM_OFF) {
        return 0;
    }
    const int64_t mono = mono_ms();
    LOCK();
    int recent = 0;
    for (int i = 0; i < s_cnt; i++) {
        if (!strcmp(s_n[i].app, app) && mono - s_mono[i] < 3600000) {
            recent++;
        }
    }
    if (recent >= NT_RATE && strcmp(app, NT_SYSTEM)) {
        UNLOCK();
        ESP_LOGW(TAG, "%s sent %d notices in an hour: this one is not kept", app, recent);
        return 0;
    }
    if (s_cnt == NT_MAX) {
        s_cnt--;
    }
    memmove(&s_n[1], &s_n[0], (size_t)s_cnt * sizeof *s_n);
    memmove(&s_mono[1], &s_mono[0], (size_t)s_cnt * sizeof *s_mono);
    notice_t *n = &s_n[0];
    memset(n, 0, sizeof *n);
    n->id = s_next++;
    if (!s_next) {
        s_next = 1;
    }
    time_t now = time(NULL);
    n->t = now > 1700000000 ? (uint32_t)now : 0;
    snprintf(n->app, sizeof n->app, "%s", app);
    snprintf(n->title, sizeof n->title, "%s", title ? title : "");
    snprintf(n->text, sizeof n->text, "%s", text ? text : "");
    snprintf(n->link, sizeof n->link, "%s", link ? link : "");
    for (int k = 0; k < NT_NACT; k++) {
        snprintf(n->act[k], sizeof n->act[k], "%s", act && act[k] ? act[k] : "");
    }
    snprintf(n->cb, sizeof n->cb, "%s", cb ? cb : "");
    s_mono[0] = mono;
    s_cnt++;
    bool known = false;
    for (int i = 0; i < s_napps; i++) {
        known |= !strcmp(s_apps[i], app);
    }
    if (!known && s_napps < NT_NAPPS) {
        snprintf(s_apps[s_napps++], NT_NAME, "%s", app);
    }
    const uint32_t id = n->id;
    changed();
    UNLOCK();
    if (notify_mode(app) == NM_ON) {
        s_new_mono = mono;
        s_new_id = id;
        s_new_cnt++;
        if (notify_wake(app) && !dnd_now()) {
            app_screen_wake();
        }
    }
    ESP_LOGI(TAG, "%s: %s", app, title ? title : "");
    return id;
}

uint32_t notify_post(const char *app, const char *title, const char *text, const char *link)
{
    return notify_post_ex(app, title, text, link, NULL, NULL);
}

uint32_t tfn_notify(const char *title, const char *text, const char *link)
{
    const char *w = tfn_watch_app_now();  // (a watcher of an app on the watch task: watch.h)
    if (w) {
        return notify_post(w, title, text, link);
    }
    const app_t *a = app_current();
    return a && a->name ? notify_post(a->name, title, text, link) : 0;
}

int notify_count(void)
{
    return s_n ? s_cnt : 0;
}

int notify_unread(const char *app)
{
    if (!s_n) {
        return 0;
    }
    int k = 0;
    LOCK();
    for (int i = 0; i < s_cnt; i++) {
        if (!s_n[i].read && (!app || !strcasecmp(s_n[i].app, app))) {
            k++;
        }
    }
    UNLOCK();
    return k;
}

bool notify_get(int i, notice_t *out)
{
    if (!s_n) {
        return false;
    }
    LOCK();
    bool ok = i >= 0 && i < s_cnt;
    if (ok) {
        *out = s_n[i];
    }
    UNLOCK();
    return ok;
}

static bool find_copy(uint32_t id, notice_t *out)
{
    bool ok = false;
    LOCK();
    for (int i = 0; i < s_cnt && !ok; i++) {
        if (s_n[i].id == id) {
            *out = s_n[i];
            ok = true;
        }
    }
    UNLOCK();
    return ok;
}

void notify_remove(uint32_t id)
{
    if (!s_n) {
        return;
    }
    LOCK();
    for (int i = 0; i < s_cnt; i++) {
        if (s_n[i].id != id) {
            continue;
        }
        memmove(&s_n[i], &s_n[i + 1], (size_t)(s_cnt - i - 1) * sizeof *s_n);
        memmove(&s_mono[i], &s_mono[i + 1], (size_t)(s_cnt - i - 1) * sizeof *s_mono);
        s_cnt--;
        changed();
        break;
    }
    UNLOCK();
}

void notify_clear(void)
{
    if (!s_n) {
        return;
    }
    LOCK();
    s_cnt = 0;
    changed();
    UNLOCK();
}

void notify_seen_app(const char *app)
{
    if (!s_n || !app) {
        return;
    }
    LOCK();
    bool any = false;
    for (int i = 0; i < s_cnt; i++) {
        if (!s_n[i].read && !strcasecmp(s_n[i].app, app)) {
            s_n[i].read = 1, any = true;
        }
    }
    if (any) {
        changed();
    }
    UNLOCK();
}

static void mark_read(uint32_t id)
{
    LOCK();
    for (int i = 0; i < s_cnt; i++) {
        if (s_n[i].id == id && !s_n[i].read) {
            s_n[i].read = 1;
            changed();
        }
    }
    UNLOCK();
}

int notify_apps(char names[][NT_NAME], int max)
{
    if (!s_n) {
        return 0;
    }
    LOCK();
    int n = s_napps < max ? s_napps : max;
    for (int i = 0; i < n; i++) {
        memcpy(names[i], s_apps[i], NT_NAME);
    }
    UNLOCK();
    return n;
}

// the answer of a button for the manager (it reads them with GET /api/notify)
static void answer(const notice_t *n, int a)
{
    if (!n->cb[0]) {
        return;
    }
    LOCK();
    if (s_nans == NT_NANS) {
        memmove(&s_ans[0], &s_ans[1], (NT_NANS - 1) * sizeof s_ans[0]);
        s_nans--;
    }
    nans_t *w = &s_ans[s_nans++];
    snprintf(w->cb, sizeof w->cb, "%s", n->cb);
    w->a = (uint8_t)a;
    time_t now = time(NULL);
    w->t = now > 1700000000 ? (uint32_t)now : 0;
    changed();
    UNLOCK();
    ESP_LOGI(TAG, "the answer %d of %s waits for the manager", a, n->cb);
}

static int jstr(char *o, int max, const char *s)
{
    int k = 0;
    if (k < max) {
        o[k++] = '"';
    }
    for (; *s && k < max - 8; s++) {
        uint8_t c = (uint8_t)*s;
        if (c == '"' || c == '\\') {
            o[k++] = '\\';
            o[k++] = (char)c;
        } else if (c < 0x20) {
            k += snprintf(o + k, max - k, "\\u%04x", c);
        } else {
            o[k++] = (char)c;
        }
    }
    if (k < max) {
        o[k++] = '"';
    }
    return k;
}

int notify_json(char *o, int max, bool clear_answers)
{
    int k = snprintf(o, max, "{\"notices\":[");
    if (!s_n) {
        return k + snprintf(o + k, max - k, "],\"answers\":[]}");
    }
    LOCK();
    for (int i = 0; i < s_cnt && k < max - 600; i++) {
        const notice_t *n = &s_n[i];
        k += snprintf(o + k, max - k, "%s{\"id\":%lu,\"t\":%lu,\"read\":%d,\"app\":", i ? "," : "",
                      (unsigned long)n->id, (unsigned long)n->t, n->read);
        k += jstr(o + k, max - k, n->app);
        k += snprintf(o + k, max - k, ",\"title\":");
        k += jstr(o + k, max - k, n->title);
        k += snprintf(o + k, max - k, ",\"cb\":");
        k += jstr(o + k, max - k, n->cb);
        k += snprintf(o + k, max - k, "}");
    }
    k += snprintf(o + k, max - k, "],\"answers\":[");
    for (int i = 0; i < s_nans && k < max - 200; i++) {
        k += snprintf(o + k, max - k, "%s{\"cb\":", i ? "," : "");
        k += jstr(o + k, max - k, s_ans[i].cb);
        k += snprintf(o + k, max - k, ",\"a\":%d,\"t\":%lu}", s_ans[i].a, (unsigned long)s_ans[i].t);
    }
    if (clear_answers && s_nans) {
        s_nans = 0;
        changed();
    }
    UNLOCK();
    k += snprintf(o + k, max - k, "]}");
    return k < max ? k : max - 1;
}

// ---------------------------------------------------------------- text helpers
// one line of s that fits w px (a cut ends with "..."); the line breaks of s are spaces
static void fit_line(const kfont_t *f, const char *s, int w, char *out, int n)
{
    snprintf(out, n, "%s", s);
    for (char *p = out; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '\t') {
            *p = ' ';
        }
    }
    if (ui_text_w(f, out) <= w) {
        return;
    }
    int len = (int)strlen(out);
    char t[NT_TEXT + 8];
    while (len > 0) {
        do {
            len--;
        } while (len > 0 && ((uint8_t)out[len] & 0xC0) == 0x80);
        out[len] = 0;
        snprintf(t, sizeof t, "%s...", out);
        if (ui_text_w(f, t) <= w) {
            break;
        }
    }
    snprintf(out, n, "%s", len > 0 ? t : "...");
}

// the age of a notice: "NOW", "5 MIN", "3 H", "2 D" ("" = the time is not known)
static void age_text(uint32_t t, char out[12])
{
    time_t now = time(NULL);
    out[0] = 0;
    if (!t || now < (time_t)t) {
        return;
    }
    long s = (long)(now - (time_t)t);
    if (s < 60) {
        snprintf(out, 12, "NOW");
    } else if (s < 3600) {
        snprintf(out, 12, "%ld MIN", s / 60);
    } else if (s < 86400) {
        snprintf(out, 12, "%ld H", s / 3600);
    } else {
        snprintf(out, 12, "%ld D", s / 86400);
    }
}

static bool big_ui(void)
{
    return gfx_native();
}  // (the AMOLED native frame: its px are small)
static bool round_ui(void)
{
    return gfx_w == gfx_h && gfx_w >= 400;
}

// the icon of the app of a notice (the system: a bell in a box)
static void notice_icon(const char *name, int x, int y, int s)
{
    const app_t *a = strcmp(name, NT_SYSTEM) ? app_find(name) : NULL;
    if (a && a->draw_icon) {
        gfx_clip_box(x, y, x + s, y + s);
        a->draw_icon(x, y, s);
        gfx_clip_box_off();
        gfx_clip_reset();
        return;
    }
    ui_rrect(x, y, s, s, s / 4, rgb(70, 86, 120));
    const int cx = x + s / 2, cy = y + s / 2, r = s / 4;
    ui_circle(cx, cy - r / 3, r, rgb(240, 236, 220), 255);  // a bell: a round top, a bar, the clapper
    ui_rrect(cx - r, cy - r / 3, 2 * r, r, 2, rgb(240, 236, 220));
    ui_rrect(cx - r - r / 3, cy + r / 2, 2 * r + 2 * (r / 3), r / 3 + 1, 1, rgb(240, 236, 220));
    ui_circle(cx, cy + r, r / 3 + 1, rgb(240, 236, 220), 255);
}

static bool is_game(const app_t *a)
{
    static const app_t *volatile s_last;
    static volatile bool s_game;
    if (!a) {
        return false;
    }
    if (a == s_last) {
        return s_game;
    }
    const char *names[160];  // (on the stack: the watch task asks too)
    uint8_t cats[160];
    int n = menu_list(names, cats, 160);
    bool g = false;
    for (int i = 0; i < n; i++) {
        if (names[i] && !strcasecmp(names[i], a->name)) {
            g = cats[i] == 0;
        }
    }
    s_game = g;
    s_last = a;
    return g;
}

bool notify_game_now(void)
{
    const app_t *a = app_current();
    return a && strcmp(app_screen_state(), "off") && is_game(a);
}

// ---------------------------------------------------------------- the banner
static uint32_t s_b_id;  // the notice of the banner (0 = none)
static notice_t s_b;
static int64_t s_b_t0, s_b_last;
static float s_b_k;                         // 0 = out of view (up), 1 = in place
static bool s_b_out;                        // it goes up
static bool s_bt_own, s_bt_eat, s_bt_gone;  // a touch of the banner; its IN_TAP / IN_LONG; a swipe up put it away
static int s_bt_x0, s_bt_y0, s_bt_dy;
static int s_bt_miny, s_bt_maxd;  // the touch: the most upward dy, the largest move (px)
static int64_t s_bt_t0;           // the touch: the time of its IN_DOWN
static int s_b_group;             // > 1: the banner shows the count of the unread notices (a burst of notices)
static int64_t s_b_chime;         // the time of the last chime (one chime for a burst)
static int64_t s_chime_mono;      // the same time on the mono_ms clock (no file save while the chime plays)

static void banner_geo(int *x, int *y, int *w, int *h)
{
    *h = big_ui() ? 76 : 58;
    *w = gfx_w - 16 > 470 ? 470 : gfx_w - 16;
    if (round_ui()) {
        *w = gfx_w * 284 / 412;  // (284 at y 62: all of it in the circle; 300 at y 44 cut its corners;
    }
    *x = (gfx_w - *w) / 2;  // a wider round frame: in proportion)
    *y = round_ui() ? gfx_w * 62 / 412 : 6;
}

bool notify_banner_on(void)
{
    return s_b_id != 0;
}

// the sound of a notice: one soft ping at a low volume. Three sines of E6 start together and fade out in 0.12, 0.35 and 0.9 s: their sum
// fades fast at first and then slowly, as a small bell does; a quiet octave gives the bright start.
static void chime(const char *app)
{
    if (!notify_sound(app)) {
        return;
    }
    s_chime_mono = mono_ms();
    sfx_tone(1319, 1319, 120, W_SINE, 12);
    sfx_tone(1319, 1319, 350, W_SINE, 10);
    sfx_tone(1319, 1319, 900, W_SINE, 9);
    sfx_tone(2638, 2638, 160, W_SINE, 4);
}

// 0 = the banner shows now, 1 = it waits (the screen, the panel, a message), 2 = no banner for it
static int banner_block(const notice_t *n)
{
    if (notify_mode(n->app) != NM_ON || dnd_now()) {
        return 2;
    }
    const app_t *cur = app_current();
    if (cur && cur->name && !strcasecmp(cur->name, n->app)) {
        return 2;  // (the app is open: it shows it itself)
    }
    if (cur && is_game(cur) && !notify_in_games()) {
        return 2;
    }
    if (!strcmp(app_screen_state(), "off")) {
        return 1;
    }
    if (sysui_active() || sysui_panel_on() || notify_center_on()) {
        return 1;
    }
    return 0;
}

static void banner_end(void)
{
    s_b_id = 0;
    s_b_k = 0;
    s_bt_own = false;
    menu_redraw_all();
}

static void open_notice(const notice_t *n);

// ---------------------------------------------------------------- the center
static bool s_c_on;
static kscroll_t s_cks;
static notice_t *s_c_rows;  // PSRAM: a copy of the list (NT_MAX)
static int s_c_n, s_c_y[NT_MAX], s_c_h[NT_MAX];
static uint32_t s_c_gen = 0xFFFFFFFF;
static int s_c_press = -1;  // the control at the touch: C_CLOSE, C_CLEAR, row * 4 + 1 + button, row * 4
static int s_c_sw = -1;     // the row that moves to the side
static float s_c_sx;
static bool s_c_touch, s_c_scroll, s_c_side, s_c_eat;
static bool s_c_downs;  // an IN_DOWN came before the IN_TAP (a real finger)
static int s_c_x0, s_c_y0;
static int64_t s_c_clear_arm, s_c_note_until;
static char s_c_note[48];
enum {
    C_CLOSE = -10,
    C_CLEAR = -11
};

typedef struct {
    int x0, y0, x1, y1, head, row, rowb, icon, pad;
} cgeo_t;
static cgeo_t cgeo(void)
{
    cgeo_t g = {0, 0, gfx_w, gfx_h, 40, 64, 44, 40, 10};
    if (big_ui()) {
        g = (cgeo_t){0, 0, gfx_w, gfx_h, 56, 88, 60, 56, 14};
    }
    // (round: the box where the circle is wide enough for the head row and the rows: nothing cut)
    if (round_ui()) {
        g.x0 = gfx_w * 66 / 412, g.x1 = gfx_w - g.x0, g.y0 = gfx_w * 56 / 412, g.y1 = gfx_h - g.y0;
    }
    return g;
}

bool notify_center_on(void)
{
    return s_c_on;
}

static void center_rows(bool force)
{
    if (!s_c_rows) {
        s_c_rows = heap_caps_calloc(NT_MAX, sizeof *s_c_rows, MALLOC_CAP_SPIRAM);
    }
    if (!s_c_rows || !s_n) {
        return;
    }
    LOCK();
    const bool same = s_c_gen == s_gen;
    if (!same || force) {
        memcpy(s_c_rows, s_n, (size_t)s_cnt * sizeof *s_n);
        s_c_n = s_cnt;
        s_c_gen = s_gen;
    }
    UNLOCK();
    const cgeo_t g = cgeo();
    int y = 0;
    for (int i = 0; i < s_c_n; i++) {
        s_c_y[i] = y;
        s_c_h[i] = g.row + (s_c_rows[i].act[0][0] ? g.rowb : 0);
        y += s_c_h[i] + 6;
    }
    ks_set_view(&s_cks, (float)(g.y1 - g.y0 - g.head));
    ks_set_max(&s_cks, (float)(y - (g.y1 - g.y0 - g.head) + 6));
}

void notify_center_open(void)
{
    if (s_c_on) {
        return;
    }
    const cgeo_t g = cgeo();
    ks_init(&s_cks, (float)(g.y1 - g.y0 - g.head), 0);
    center_rows(true);
    s_c_on = true;
    s_c_press = s_c_sw = -1;
    s_c_touch = s_c_eat = false;
    s_c_clear_arm = 0;
    if (s_b_id) {
        banner_end();
    }
    LOCK();
    bool any = false;
    for (int i = 0; i < s_cnt; i++) {
        if (!s_n[i].read) {
            s_n[i].read = 1, any = true;  // (the center shows them all)
        }
    }
    if (any) {
        changed();
    }
    UNLOCK();
}

void notify_center_close(void)
{
    if (!s_c_on) {
        return;
    }
    s_c_on = false;
    s_c_touch = false;
    menu_redraw_all();
}

static void note(const char *t)
{
    snprintf(s_c_note, sizeof s_c_note, "%s", t);
    s_c_note_until = mono_ms() + 2500;
}

// the buttons of a row: the box of the button k (of n) in the row i
static void row_button(const cgeo_t *g, int i, int k, int n, int top, int *x, int *y, int *w, int *h)
{
    const int x0 = g->x0 + g->pad + g->icon + g->pad, x1 = g->x1 - g->pad, gap = 8;
    *w = (x1 - x0 - gap * (n - 1)) / n;
    *x = x0 + k * (*w + gap);
    *h = g->rowb - 10;
    *y = top + g->row - 4;
    (void)i;
}

static int center_hit(int x, int y)
{
    const cgeo_t g = cgeo();
    if (y < g.y0 + g.head) {
        if (x >= g.x1 - 54) {
            return C_CLOSE;
        }
        if (s_c_n && x >= g.x1 - 54 - 150 && x < g.x1 - 58) {
            return C_CLEAR;
        }
        return -1;
    }
    const int ly = y - (g.y0 + g.head) + ks_ipos(&s_cks);
    for (int i = 0; i < s_c_n; i++) {
        if (ly < s_c_y[i] || ly >= s_c_y[i] + s_c_h[i]) {
            continue;
        }
        const notice_t *n = &s_c_rows[i];
        int na = 0;
        while (na < NT_NACT && n->act[na][0]) {
            na++;
        }
        const int top = g.y0 + g.head + s_c_y[i] - ks_ipos(&s_cks);
        for (int k = 0; k < na; k++) {
            int bx, by, bw, bh;
            row_button(&g, i, k, na, top, &bx, &by, &bw, &bh);
            if (x >= bx - 4 && x < bx + bw + 4 && y >= by - 4 && y < by + bh + 6) {
                return i * 4 + 1 + k;
            }
        }
        return i * 4;
    }
    return -1;
}

static void center_act(int c)
{
    if (c == C_CLOSE) {
        sfx_ui(SFX_BACK);
        notify_center_close();
        return;
    }
    if (c == C_CLEAR) {
        const int64_t now = mono_ms();
        if (now - s_c_clear_arm > 3000) {  // (a loss: the second tap clears)
            s_c_clear_arm = now;
            sfx_ui(SFX_CLICK);
            return;
        }
        s_c_clear_arm = 0;
        notify_clear();
        sfx_ui(SFX_BACK);
        return;
    }
    if (c < 0 || c / 4 >= s_c_n) {
        return;
    }
    const notice_t n = s_c_rows[c / 4];
    const int b = c % 4 - 1;
    if (b >= 0) {  // a button: the answer to the manager, the notice goes
        answer(&n, b);
        notify_remove(n.id);
        sfx_ui(SFX_COIN);
        char t[48];
        snprintf(t, sizeof t, "SENT: %s", n.act[b]);
        note(t);
        return;
    }
    if (n.act[0][0]) {  // (a notice with buttons: only a button acts)
        sfx_ui(SFX_TICK);
        return;
    }
    sfx_ui(SFX_CLICK);
    notify_center_close();
    open_notice(&n);
}

static void center_input(const input_event_t *ev)
{
    const cgeo_t g = cgeo();
    switch (ev->kind) {
    case IN_DOWN:
        s_c_touch = s_c_downs = true;
        s_c_scroll = s_c_side = false;
        s_c_eat = false;
        s_c_x0 = ev->x;
        s_c_y0 = ev->y;
        s_c_sw = -1;
        s_c_sx = 0;
        if (ev->y >= g.y0 + g.head && ks_down(&s_cks, (float)ev->y, ev->t_ms)) {
            s_c_eat = true;
        }
        s_c_press = s_c_eat ? -1 : center_hit(ev->x, ev->y);
        break;
    case IN_MOVE: {
        if (!s_c_touch) {
            break;
        }
        const int dx = ev->x - s_c_x0, dy = ev->y - s_c_y0;
        if (!s_c_scroll && !s_c_side && s_c_press >= 0 && s_c_press % 4 == 0 && abs(dx) > 14 &&
            abs(dx) > abs(dy) * 3 / 2) {
            s_c_side = true;  // a row goes to the side (a remove at the lift past 40 %)
            s_c_sw = s_c_press / 4;
            s_c_press = -1;
            input_tap_cancel();
        }
        if (s_c_side) {
            s_c_sx = (float)dx;
        } else if (s_c_y0 >= g.y0 + g.head && ks_move(&s_cks, (float)ev->y, ev->t_ms)) {
            s_c_scroll = true;
            s_c_press = -1;
            input_tap_cancel();
        }
        break;
    }
    case IN_UP:
        s_c_touch = false;
        ks_up(&s_cks, ev->t_ms);
        if (s_c_side && s_c_sw >= 0 && s_c_sw < s_c_n) {
            if (fabsf(s_c_sx) > (g.x1 - g.x0) * 0.4f) {
                notify_remove(s_c_rows[s_c_sw].id);
                sfx_ui(SFX_BACK);
            }
        }
        s_c_side = false;
        s_c_sw = -1;
        s_c_sx = 0;
        break;
    case IN_TAP: {
        const int c = center_hit(ev->x, ev->y);
        if (!s_c_downs) {
            center_act(c);  // (a tap with no touch before it: the console of the tests)
        } else if (!s_c_eat && ks_tap_ok(&s_cks) && c == s_c_press) {
            center_act(c);
        }
        s_c_press = -1;
        s_c_downs = false;
        break;
    }
    default:
        break;
    }
}

static void center_draw(void)
{
    const cgeo_t g = cgeo();
    const kfont_t *ft = big_ui() ? &KF_TEXT : &KF_NAME, *fs = big_ui() ? &KF_NAME : &KF_LABEL;
    gfx_fill(THEME_NOTE_BG);
    const int top = g.y0 + g.head, sc = ks_ipos(&s_cks);
    if (!s_c_n) {
        ui_text_c(ft, (g.x0 + g.x1) / 2, (top + g.y1) / 2 + 6, "NO NOTIFICATIONS", THEME_NOTE_EMPTY);
    }
    for (int i = 0; i < s_c_n; i++) {
        int y = top + s_c_y[i] - sc;
        if (y + s_c_h[i] < top || y > g.y1) {
            continue;
        }
        const notice_t *n = &s_c_rows[i];
        int x0 = g.x0 + g.pad + (i == s_c_sw ? (int)s_c_sx : 0), w = g.x1 - g.x0 - 2 * g.pad;
        const bool down = s_c_press == i * 4;
        ui_rbox(x0, y, w, s_c_h[i], 12, down ? THEME_NOTE_CARD_DOWN : THEME_NOTE_CARD, THEME_NOTE_CARD_EDGE, 1);
        notice_icon(n->app, x0 + g.pad, y + (g.row - g.icon) / 2, g.icon);
        const int tx = x0 + g.pad + g.icon + g.pad, tw = x0 + w - g.pad - tx;
        char age[12], line[NT_TEXT + 8];
        age_text(n->t, age);
        const int aw = age[0] ? ui_text_w(fs, age) : 0;
        fit_line(fs, n->app, tw - aw - 10, line, sizeof line);
        ui_text(fs, tx, y + g.row * 30 / 100, line, THEME_NOTE_TEXT_DIM);
        if (age[0]) {
            ui_text(fs, tx + tw - aw, y + g.row * 30 / 100, age, THEME_NOTE_TEXT_DIM);
        }
        fit_line(ft, n->title, tw, line, sizeof line);
        ui_text(ft, tx, y + g.row * 60 / 100, line, THEME_NOTE_TEXT);
        fit_line(fs, n->text, tw, line, sizeof line);
        ui_text(fs, tx, y + g.row * 88 / 100, line, THEME_NOTE_BODY);
        int na = 0;
        while (na < NT_NACT && n->act[na][0]) {
            na++;
        }
        for (int k = 0; k < na; k++) {
            int bx, by, bw, bh;
            row_button(&g, i, k, na, y, &bx, &by, &bw, &bh);
            const bool bd = s_c_press == i * 4 + 1 + k;
            ui_rbox(bx, by, bw, bh, 10,
                    bd       ? THEME_NOTE_ACT_DOWN
                    : k == 0 ? THEME_NOTE_ACT_FIRST
                             : THEME_NOTE_ACT,
                    THEME_NOTE_ACT_EDGE, 1);
            fit_line(fs, n->act[k], bw - 10, line, sizeof line);
            ui_text_c(fs, bx + bw / 2, by + (bh + fs->asc) / 2 - 1, line, THEME_NOTE_ACT_TEXT);
        }
    }
    int at, sz, al;
    if (ks_bar(&s_cks, g.y1 - top - 8, &at, &sz, &al)) {
        ui_rrect_a(g.x1 - 6, top + 4 + at, 3, sz, 1, THEME_NOTE_SCROLL, al > 255 ? 255 : al);
    }
    // the head over the list (the text of the list does not obey a clip box)
    gfx_fill_rect(g.x0, 0, g.x1 - g.x0, top, THEME_NOTE_BG);
    if (round_ui()) {
        gfx_fill_rect(0, 0, gfx_w, g.y0, THEME_NOTE_BG);
    }
    const bool note_on = mono_ms() < s_c_note_until;
    if (round_ui()) {  // (round: the title small and centred over the head row)
        ui_text_c(&KF_LABEL, gfx_w / 2, g.y0 - 10, note_on ? s_c_note : "NOTIFICATIONS",
                  note_on ? THEME_NOTE_OK : THEME_NOTE_TEXT);
    } else {
        ui_text(ft, g.x0 + g.pad, top - (g.head - ft->asc) / 2, note_on ? s_c_note : "NOTIFICATIONS",
                note_on ? THEME_NOTE_OK : THEME_NOTE_TEXT);
    }
    ui_rbox(g.x1 - 50, g.y0 + 4, 44, g.head - 8, 10, s_c_press == C_CLOSE ? THEME_NOTE_BTN_DOWN : THEME_NOTE_BTN,
            THEME_NOTE_BTN_EDGE, 1);
    ui_text_c(ft, g.x1 - 28, g.y0 + (g.head + ft->asc) / 2 - 1, "X", THEME_NOTE_TEXT);
    if (s_c_n) {
        const bool armed = mono_ms() - s_c_clear_arm < 3000;
        ui_rbox(g.x1 - 54 - 148, g.y0 + 4, 140, g.head - 8, 10,
                armed                  ? THEME_NOTE_ARMED
                : s_c_press == C_CLEAR ? THEME_NOTE_BTN_DOWN
                                       : THEME_NOTE_BTN,
                armed ? THEME_NOTE_ARMED_EDGE : THEME_NOTE_BTN_EDGE, 1);
        ui_text_c(fs, g.x1 - 54 - 78, g.y0 + (g.head + fs->asc) / 2 - 1, armed ? "TAP AGAIN" : "CLEAR ALL",
                  THEME_NOTE_TEXT);
    }
}

// ---------------------------------------------------------------- the open of a notice
static void open_notice(const notice_t *n)
{
    if (!strcmp(n->app, NT_SYSTEM) || n->act[0][0]) {  // (a notice of the system or with buttons: the center)
        notify_center_open();
        return;
    }
    const app_t *a = app_find(n->app);
    notify_remove(n->id);
    if (a) {
        app_open_arg(a, n->link);
    }
}

// ---------------------------------------------------------------- the frame
void notify_tick(int64_t now)
{
    if (!s_n) {
        return;
    }
    const int64_t m = mono_ms();
    // a new notice: its banner now, later (the screen, the panel) or never (a game, DO NOT DISTURB, the app is open)
    const uint32_t nid = s_new_id;
    if (nid && nid != s_b_id) {
        notice_t n;
        if (!find_copy(nid, &n) || m - s_new_mono > WAIT_MS) {
            s_new_id = 0;
        } else {
            const int b = banner_block(&n);
            if (b == 2) {
                s_new_id = 0;
                const app_t *cur = app_current();
                if (cur && cur->name && !strcasecmp(cur->name, n.app)) {
                    mark_read(n.id);
                }
            } else if (b == 0) {
                s_new_id = 0;
                const int burst = s_new_cnt;
                s_new_cnt = 0;
                if (!strcmp(app_screen_state(), "dim")) {
                    app_screen_wake();
                }
                // THE BURST (many notices together must not replace each other quickly): a
                // notice that comes while a banner is in view, or with others in the same frame, does not replace the
                // banner; the banner stays and shows the count of the unread notices ("12 NOTIFICATIONS")
                const bool shown = s_b_id && !s_b_out;
                s_b = n;
                s_b_id = n.id;
                s_b_t0 = now;
                s_b_group = 0;
                if (shown || burst > 1) {
                    LOCK();
                    for (int i = 0; i < s_cnt; i++) {
                        s_b_group += !s_n[i].read;
                    }
                    UNLOCK();
                    if (s_b_group < 2) {
                        s_b_group = 0;
                    }
                }
                if (!shown) {
                    s_b_out = false;
                    s_bt_own = s_bt_gone = false;
                    s_bt_dy = 0;
                }
                if (now - s_b_chime > 1500) {
                    s_b_chime = now;
                    chime(n.app);
                }
            }
        }
    }
    if (s_b_id) {
        const float dt = s_b_last ? (float)(now - s_b_last) : 16.0f;
        if (!s_bt_own && now - s_b_t0 > BANNER_MS) {
            s_b_out = true;
        }
        const float goal = s_b_out ? 0.0f : 1.0f;
        s_b_k += (goal - s_b_k) * (1.0f - powf(0.985f, dt > 60 ? 60 : dt));
        if (s_b_out && s_b_k < 0.02f) {
            banner_end();
        } else {
            menu_redraw_all();  // (the menu repaints only what changes: all of it under the banner)
        }
    }
    s_b_last = now;
    if (s_c_on) {
        center_rows(false);
        ks_update(&s_cks, now);
    }
    if (s_badges) {
        s_badges = false;
        menu_redraw_all();  // (a badge changed)
    }
    // A write of the flash stops the sound task for 23-30 ms, and the
    // speaker queue holds 15 ms: each write gives a gap in the sound. A notice that comes with the screen off chimes at
    // the wake, and its save comes at the same moment (3 gaps in the first 0.3 s). So the file waits until the chime
    // (0.9 s) ends.
    if (s_dirty && m - s_dirty_at > 1500 && m - s_chime_mono > 1200) {
        file_save();
    }
}

bool notify_input(const input_event_t *ev)
{
    if (s_c_on) {
        center_input(ev);
        return true;
    }
    if (ev->kind == IN_DOWN) {
        s_bt_eat = false;
    }
    if (s_bt_eat && (ev->kind == IN_TAP || ev->kind == IN_LONG)) {
        return true;
    }
    if (!s_b_id) {
        return false;
    }
    int bx, by, bw, bh;
    banner_geo(&bx, &by, &bw, &bh);
    switch (ev->kind) {
    case IN_DOWN:
        if (s_b_out || ev->x < bx - 6 || ev->x >= bx + bw + 6 || ev->y >= by + bh + 8) {
            return false;
        }
        s_bt_own = true;
        s_bt_gone = false;
        s_bt_x0 = ev->x;
        s_bt_y0 = ev->y;
        s_bt_dy = 0;
        s_bt_miny = s_bt_maxd = 0;
        s_bt_t0 = ev->t_ms;
        return true;
    case IN_MOVE:
    case IN_UP: {
        if (!s_bt_own) {
            return false;
        }
        // the path of the touch: the most upward point and the largest move (an IN_UP has the last point too)
        const int dx = ev->x - s_bt_x0, dy = ev->y - s_bt_y0;
        if (dy < s_bt_miny) {
            s_bt_miny = dy;
        }
        if (abs(dx) > s_bt_maxd) {
            s_bt_maxd = abs(dx);
        }
        if (abs(dy) > s_bt_maxd) {
            s_bt_maxd = abs(dy);
        }
        if (ev->kind == IN_MOVE) {
            s_bt_dy = dy;
            if (s_bt_dy < -14 && !s_bt_gone) {  // a swipe up: the banner goes (the notice stays in the list)
                s_bt_gone = true;
                s_b_out = true;
                mark_read(s_b_id);
            }
            return true;
        }
        s_bt_own = false;
        s_bt_eat = true;
        // A FAST FLICK UP: the panel sees only one or two points of a fast flick, so the last move is small and a
        // test of the last move would take the flick as a tap. So any move up of 8 px or more puts the banner away,
        // and only a still, short touch opens the notice.
        if (!s_bt_gone && s_bt_miny <= -8) {
            s_bt_gone = true;
            s_b_out = true;
            mark_read(s_b_id);
        } else if (!s_bt_gone && s_bt_maxd < 14 && ev->t_ms - s_bt_t0 < 700) {  // a tap: open the notice
            const notice_t n = s_b;
            s_b_out = true;
            sfx_ui(SFX_CLICK);
            if (s_b_group > 1) {
                notify_center_open();  // (the count of a burst: all of them, in the center)
            } else {
                open_notice(&n);
            }
        }
        s_bt_dy = 0;
        return true;
    }
    default:
        return s_bt_own;
    }
}

void notify_draw(int64_t now)
{
    (void)now;
    if (s_c_on) {
        center_draw();
        return;
    }
    if (!s_b_id || s_b_k < 0.01f) {
        return;
    }
    int bx, by, bw, bh;
    banner_geo(&bx, &by, &bw, &bh);
    int y = by - (int)lroundf((1.0f - s_b_k) * (float)(by + bh + 10));
    if (s_bt_own && s_bt_dy < 0) {
        y += s_bt_dy;
    }
    const kfont_t *ft = big_ui() ? &KF_TEXT : &KF_NAME, *fs = big_ui() ? &KF_NAME : &KF_LABEL;
    ui_rbox(bx, y, bw, bh, 16, THEME_NOTE_BANNER_BG, THEME_NOTE_BANNER_EDGE, 1);
    const int is = bh - 20;
    notice_icon(s_b.app, bx + 10, y + 10, is);
    char age[12], line[NT_TEXT + 8];
    age_text(s_b.t, age);
    const int tx = bx + 10 + is + 12, aw = age[0] ? ui_text_w(fs, age) : 0, tw = bx + bw - 12 - tx;
    if (s_b_group > 1) {
        // a burst: "12 NOTIFICATIONS", the count on the icon, and the newest one under it (a tap opens the center)
        char cnt[12], head[32];
        snprintf(cnt, sizeof cnt, "%d", s_b_group);
        snprintf(head, sizeof head, "%d NOTIFICATIONS", s_b_group);
        const int cr = is / 4 + 4;
        gfx_fill_circle(bx + 10 + is - 8, y + 12, cr, THEME_NOTE_COUNT);
        ui_text_c(fs, bx + 10 + is - 8, y + 12 + fs->asc / 2 - 1, cnt, THEME_NOTE_COUNT_TEXT);
        ui_text(ft, tx, y + bh * 44 / 100, head, THEME_NOTE_BANNER_TEXT);
        char newest[NT_TEXT + NT_NAME + 4];
        snprintf(newest, sizeof newest, "%s: %s", s_b.app, s_b.title[0] ? s_b.title : s_b.text);
        fit_line(fs, newest, tw, line, sizeof line);
        ui_text(fs, tx, y + bh * 80 / 100, line, THEME_NOTE_BANNER_BODY);
        return;
    }
    fit_line(ft, s_b.title[0] ? s_b.title : s_b.app, tw - aw - 8, line, sizeof line);
    ui_text(ft, tx, y + bh * 44 / 100, line, THEME_NOTE_BANNER_TEXT);
    if (age[0]) {
        ui_text(fs, bx + bw - 12 - aw, y + bh * 44 / 100, age, THEME_NOTE_BANNER_DIM);
    }
    fit_line(fs, s_b.text, tw, line, sizeof line);
    ui_text(fs, tx, y + bh * 80 / 100, line, THEME_NOTE_BANNER_BODY);
}
