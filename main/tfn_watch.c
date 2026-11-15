// TFN OS: the watchers (watch.h: watch.elf of an app on the watch task) and the secret store (main/tfn_app.c has the rest; tfn_app_priv.h).
#include "tfn_app_priv.h"

static const char *TAG = "tfn";

// ---------------------------------------------------------------- the watchers (watch.h)
#define WGUARD_MAGIC   0x57544348u
#define WATCH_FAULT_MS 20000  // a check this long is a fault; three in a row turn the watcher off
static RTC_NOINIT_ATTR struct {
    uint32_t magic;
    char id[24];
    uint32_t code;
} s_wguard;                        // the crash guard of a check (a reset in it turns that watcher off)
static SemaphoreHandle_t s_wlock;  // a check, and a load or unload of the watchers: not at the same time
static uint32_t s_wgen;            // the list of the apps that the watchers follow
static volatile bool s_wforce;     // (tests) a round now with all the watchers

bool tfa_watch_crashed(void)
{
    return s_wguard.magic == WGUARD_MAGIC;
}
static uint32_t fnv32(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) {
        h = (h ^ (uint8_t)*s) * 16777619u;
    }
    return h;
}
// the store key of a watcher that is off: its value is the version (code) of the app at the fault
static void wf_key(const char *id, char k[16])
{
    snprintf(k, 16, "wf_%08lx", (unsigned long)fnv32(id));
}

int watch_every(void)
{
    uint32_t v = store_get("wt_every", 5);
    return v == 0 ? 0 : v < 5 ? 5 : v > 60 ? 60 : (int)v;
}
void watch_set_every(int m)
{
    store_set("wt_every", (uint32_t)(m < 0 ? 0 : m));
}
void tfn_watch_now(void)
{
    s_wforce = true;
}

const char *tfn_watch_app_now(void)
{
    return tfa_wcur >= 0 && tfa_on_watch_task() ? tfa_w[tfa_wcur].name : NULL;
}

static void watch_unload(int w)
{
    wslot_t *W = &tfa_w[w];
    if (!W->on) {
        return;
    }
    esp_elf_deinit(&W->elf);
    size_t bytes = 0;
    int n = tfa_own_release_o(&W->own, &bytes);
    ESP_LOGI(TAG, "watcher of %s: unloaded (%d blocks, %u bytes freed)", W->id, n, (unsigned)bytes);
    memset(W, 0, sizeof *W);
}

// the watcher of an app is wanted: a watch.elf, the class "background" (the policy can take it away), not off
static bool watch_wanted(const tfn_entry_t *e)
{
    if (!e->watch_min || !e->watch_sha[0] || !tfn_runs(e) || tfa_safe) {
        return false;
    }
    int b = tfa_cls_of("background", 10);
    if (b < 0 || !(e->perms & (1u << b))) {
        return false;
    }
    char k[16];
    wf_key(e->id, k);
    return store_get(k, 0) != e->code;
}

// the load of the watcher of e into the free place w (the watch task, with the list lock)
static bool watch_load(const tfn_entry_t *e, int w)
{
    char path[72];
    uint8_t *file = NULL;
    size_t n = 0;
    snprintf(path, sizeof path, "%s/watch.elf", e->dir);
    if (!tfa_read_all(path, &file, &n, 1 << 20)) {
        ESP_LOGW(TAG, "%s: no watch.elf%s", e->id, e->place == TFN_CARD ? " (is the card in?)" : "");
        return false;
    }
    char hex[65];
    tfa_sha256_hex(file, n, hex);
    if (strcmp(hex, e->watch_sha)) {
        free(file);
        ESP_LOGE(TAG, "%s: watch.elf is damaged (its hash is not the one of the manifest)", e->id);
        return false;
    }
    wslot_t *W = &tfa_w[w];
    memset(W, 0, sizeof *W);
    esp_elf_init(&W->elf);
    tfa_reloc_bad = 0;
    tfa_link_perms = e->perms;
    tfa_denied[0] = 0;
    tfa_link_watch = true;
    tfa_seg_end_set(file);
    int r = esp_elf_relocate(&W->elf, file);
    tfa_link_watch = false;
    if (!r && (tfa_reloc_bad || tfa_denied[0])) {
        r = -EINVAL;
    }
    if (r) {
        esp_elf_deinit(&W->elf);
        free(file);
        if (tfa_denied[0]) {
            ESP_LOGE(TAG, "%s: the watcher asks for %s: a watcher cannot have it", e->id, tfa_denied);
        } else {
            ESP_LOGE(TAG, "%s: the watcher does not link (%d)", e->id, r);
        }
        return false;
    }
    snprintf(W->id, sizeof W->id, "%s", e->id);
    snprintf(W->name, sizeof W->name, "%s", e->name);
    W->code = e->code;
    W->on = true;
    tfa_wcur = w;  // (from its constructors on, its memory goes into its own set)
    s_wguard.code = e->code;
    memcpy(s_wguard.id, e->id, sizeof s_wguard.id);
    s_wguard.id[sizeof s_wguard.id - 1] = 0;
    s_wguard.magic = WGUARD_MAGIC;
    const watch_t *got = NULL;
    char *argv[2] = {(char *)&got, (char *)(intptr_t)TFN_ABI_VERSION};
    esp_elf_request(&W->elf, 0, 2, argv);
    free(file);
    if (!got || !got->check) {
        s_wguard.magic = 0;
        tfa_wcur = -1;
        ESP_LOGE(TAG, "%s: watch.elf gave no watcher", e->id);
        watch_unload(w);
        return false;
    }
    W->real = *got;
    W->every = got->every_min < 5 ? 5 : got->every_min;
    if (W->real.init) {
        W->real.init();
    }
    s_wguard.magic = 0;
    tfa_wcur = -1;
    ESP_LOGI(TAG, "watcher of %s %s: loaded, a check each %d min", e->id, e->ver, W->every);
    return true;
}

// the watchers follow the list of the apps (an install, a removal, a new version, the permission)
static void watch_sync(void)
{
    LOCK();
    for (int w = 0; w < WMAX; w++) {
        if (!tfa_w[w].on) {
            continue;
        }
        const tfn_entry_t *e = NULL;
        for (int i = 0; i < tfa_n; i++) {
            if (!strcmp(tfa_e[i].id, tfa_w[w].id)) {
                e = &tfa_e[i];
            }
        }
        if (!e || e->code != tfa_w[w].code || !watch_wanted(e)) {
            watch_unload(w);
        }
    }
    for (int i = 0; i < tfa_n; i++) {
        const tfn_entry_t *e = &tfa_e[i];
        if (!watch_wanted(e)) {
            continue;
        }
        bool have = false;
        for (int w = 0; w < WMAX; w++) {
            have |= tfa_w[w].on && !strcmp(tfa_w[w].id, e->id);
        }
        if (have) {
            continue;
        }
        int w = 0;
        while (w < WMAX && tfa_w[w].on) {
            w++;
        }
        if (w == WMAX) {
            ESP_LOGW(TAG, "no place for more than %d watchers", WMAX);
            break;
        }
        watch_load(e, w);
    }
    UNLOCK();
}

static void watch_off(int w, const char *why)
{
    char k[16], t[64];
    wf_key(tfa_w[w].id, k);
    store_set(k, tfa_w[w].code);
    snprintf(t, sizeof t, "%s: THE BACKGROUND CHECK IS OFF", tfa_w[w].name);
    notify_post(NT_SYSTEM, t, why, "");
    ESP_LOGE(TAG, "the watcher of %s is off: %s", tfa_w[w].id, why);
    watch_unload(w);
}

static volatile int64_t s_wnext;      // ms: the next round (0 = at once)
static volatile bool s_wrun, s_wany;  // a round runs now; a watcher is on

bool watch_busy(void)
{
    return s_wrun || (s_wany && watch_every() && esp_timer_get_time() / 1000 >= s_wnext);
}

static void watch_task(void *arg)
{
    (void)arg;
    int64_t wait_from = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        const int64_t now = esp_timer_get_time() / 1000;
        if (s_wgen != tfa_gen) {
            xSemaphoreTake(s_wlock, portMAX_DELAY);
            s_wgen = tfa_gen;
            watch_sync();
            xSemaphoreGive(s_wlock);
        }
        const int every = watch_every();
        bool any = false, force = s_wforce;
        for (int w = 0; w < WMAX; w++) {
            any |= tfa_w[w].on;
        }
        s_wany = any;
        if (!any || (!force && (!every || now < s_wnext))) {
            continue;
        }
        if (!wifi_ip()[0]) {  // (no network: a try each minute)
            s_wnext = now + 60000;
            continue;
        }
        // a 2-board link, or a game on the screen: the round waits (up to 15 minutes)
        if (!force && (link_in_use() || notify_game_now()) && (!wait_from || now - wait_from < 15 * 60000)) {
            if (!wait_from) {
                wait_from = now;
            }
            continue;
        }
        wait_from = 0;
        s_wforce = false;
        xSemaphoreTake(s_wlock, portMAX_DELAY);
        s_wrun = true;
        int ran = 0;
        for (int w = 0; w < WMAX; w++) {
            wslot_t *W = &tfa_w[w];
            const int per = W->every > every ? W->every : every;
            if (!W->on || (!force && W->last && now - W->last < (int64_t)per * 60000 - 30000)) {
                continue;
            }
            s_wguard.code = W->code;
            memcpy(s_wguard.id, W->id, sizeof s_wguard.id);  // (both 24 bytes)
            s_wguard.id[sizeof s_wguard.id - 1] = 0;
            s_wguard.magic = WGUARD_MAGIC;
            tfa_wcur = w;
            const int64_t t0 = esp_timer_get_time() / 1000;
            W->real.check();
            W->last_ms = (int)(esp_timer_get_time() / 1000 - t0);
            tfa_wcur = -1;
            s_wguard.magic = 0;
            W->last = now;
            ran++;
            if (W->last_ms > WATCH_FAULT_MS) {
                ESP_LOGW(TAG, "the check of %s took %d ms", W->id, W->last_ms);
                if (++W->faults >= 3) {
                    watch_off(w, "IT TOOK TOO LONG THREE TIMES. A NEW VERSION OF THE APP TURNS IT ON AGAIN.");
                }
            } else {
                W->faults = 0;
            }
        }
        s_wrun = false;
        xSemaphoreGive(s_wlock);
        if (ran) {
            ESP_LOGI(TAG, "a background round: %d checks (the stack of the task: %u bytes never used)", ran,
                     (unsigned)uxTaskGetStackHighWaterMark(NULL));
        }
        s_wnext = now + (int64_t)(every ? every : 5) * 60000;
    }
}

void watch_init(void)
{
    // the crash guard of the checks: a reset in a check turns that watcher off (until a new version of its app)
    if (s_wguard.magic == WGUARD_MAGIC) {
        s_wguard.id[sizeof s_wguard.id - 1] = 0;
        esp_reset_reason_t why = esp_reset_reason();
        if ((why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT) &&
            tfa_id_ok(s_wguard.id)) {
            char k[16], t[64];
            wf_key(s_wguard.id, k);
            store_set(k, s_wguard.code);
            snprintf(t, sizeof t, "%s: THE BACKGROUND CHECK IS OFF", s_wguard.id);
            notify_post(NT_SYSTEM, t, "IT STOPPED THE BOARD. A NEW VERSION OF THE APP TURNS IT ON AGAIN.", "");
            ESP_LOGE(TAG, "the watcher of %s stopped the board (reset %d): it is off", s_wguard.id, (int)why);
        }
    }
    s_wguard.magic = 0;
    if (tfa_safe || tfa_wtask || !tfa_e) {
        return;
    }
    s_wlock = xSemaphoreCreateMutex();
    // (an internal stack: a check writes files and uses TLS, and a task with its stack in PSRAM cannot run while the
    // flash writes)
    if (s_wlock) {
        xTaskCreatePinnedToCore(watch_task, "watch", 8192, NULL, 1, &tfa_wtask, 0);
    }
}

int tfn_watch_json(char *o, int max)
{
    int k = snprintf(o, max, "{\"every\":%d,\"watchers\":[", watch_every());
    const int64_t now = esp_timer_get_time() / 1000;
    for (int w = 0, n = 0; w < WMAX && k < max - 160; w++) {
        const wslot_t *W = &tfa_w[w];
        if (!W->on) {
            continue;
        }
        k += snprintf(o + k, max - k,
                      "%s{\"id\":\"%s\",\"every\":%d,\"last_s\":%lld,\"last_ms\":%d,\"faults\":%d,\"bytes\":%u}",
                      n++ ? "," : "", W->id, W->every, W->last ? (long long)((now - W->last) / 1000) : -1LL, W->last_ms,
                      W->faults, (unsigned)W->own.live);
    }
    k += snprintf(o + k, max - k, "]}");
    return k < max ? k : max - 1;
}

// ---------------------------------------------------------------- the secret store (watch.h)
// the app that asks: the watcher on the watch task, else the open app file (a built-in app has no secrets)
const char *tfa_secret_owner(void)
{
    if (tfa_wcur >= 0 && tfa_on_watch_task()) {
        return tfa_w[tfa_wcur].id;
    }
    const tfn_entry_t *e = tfn_entry_of(app_current());
    return e ? e->id : NULL;
}
static void secret_key(const char *id, const char *key, char out[16])
{
    char b[80];
    uint32_t lo, hi;
    snprintf(b, sizeof b, "%s:%s", id, key);
    tfa_fnv64(b, &lo, &hi);
    snprintf(out, 16, "s%06lx%08lx", (unsigned long)(hi & 0xFFFFFF), (unsigned long)lo);
}
const tfn_entry_t *tfn_entry_by_id(const char *id)
{
    for (int i = 0; i < tfa_n; i++) {
        if (!strcmp(tfa_e[i].id, id)) {
            return &tfa_e[i];
        }
    }
    return NULL;
}
int tfn_secret_get(const char *key, char *out, int max)
{
    return tfn_secret_get_for(tfa_secret_owner(), key, out, max);
}
bool tfn_secret_set(const char *key, const char *value)
{
    return tfn_secret_set_for(tfa_secret_owner(), key, value);
}
int tfn_secret_get_for(const char *id, const char *key, char *out, int max)
{
    if (!id || !key || !out || max <= 0) {
        return -1;
    }
    char k[16];
    secret_key(id, key, k);
    nvs_handle_t h;
    if (nvs_open("tfnsec", NVS_READONLY, &h) != ESP_OK) {
        return -1;
    }
    size_t n = (size_t)max;
    esp_err_t r = nvs_get_str(h, k, out, &n);
    nvs_close(h);
    return r == ESP_OK ? (int)n - 1 : -1;
}
bool tfn_secret_set_for(const char *id, const char *key, const char *value)
{
    if (!id || !key || (value && strlen(value) > 400)) {
        return false;
    }
    char k[16];
    secret_key(id, key, k);
    nvs_handle_t h;
    if (nvs_open("tfnsec", NVS_READWRITE, &h) != ESP_OK) {
        return false;
    }
    esp_err_t r = value && *value ? nvs_set_str(h, k, value) : nvs_erase_key(h, k);
    if (r == ESP_ERR_NVS_NOT_FOUND) {
        r = ESP_OK;
    }
    if (r == ESP_OK) {
        r = nvs_commit(h);
    }
    nvs_close(h);
    return r == ESP_OK;
}
