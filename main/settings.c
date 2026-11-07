#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"
#include "board.h"
#include "audio.h"
#include "settings.h"
#include "device_guard.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "storage.h"
#define NVS_NS "arcade"

// ---------------------------------------------------------------- the spill store
// A board with a full NVS keeps the old keys but takes no new key (nvs_set_u32: ESP_ERR_NVS_NOT_ENOUGH_SPACE), so
// the newer settings of all apps would be lost. Such a key goes to SPILL_FILE in /data (records of 16 + 4 bytes; a change
// writes its record in place). store_get looks in the spill first. The records are in PSRAM after the first use (the
// file system must be mounted: before that, only the NVS).
#define SPILL_FILE STORAGE_ROOT "/store2.bin"
#define SPILL_MAX  1024
typedef struct {
    char key[16];
    uint32_t val;
} spill_t;
static spill_t *s_spill;
static int s_nspill;
static bool s_spill_loaded;
static SemaphoreHandle_t s_spill_mx;
static const char *SPILL_TAG = "store";

// The keys of the trust of the board: the developer mode and the HMAC key of the signature cache. Only the NVS keeps
// them. The spill file is in /data, and a write into /data must not change them: spill_load ignores their records,
// store_set never puts them in the spill, and store_make_room never moves them out of the NVS.
static bool spill_never(const char *key)
{
    return !strcmp(key, "tfn_dev") || (!strncmp(key, "tfn_hk", 6) && key[6] >= '0' && key[6] <= '9' && !key[7]);
}

static void spill_lock(void)
{
    if (!s_spill_mx) {
        s_spill_mx = xSemaphoreCreateMutex();
    }
    if (s_spill_mx) {
        xSemaphoreTake(s_spill_mx, portMAX_DELAY);
    }
}
static void spill_unlock(void)
{
    if (s_spill_mx) {
        xSemaphoreGive(s_spill_mx);
    }
}
static void spill_write_all(void)
{
    FILE *f = fopen(SPILL_FILE, "wb");
    if (!f) {
        return;
    }
    fwrite(s_spill, sizeof(spill_t), (size_t)s_nspill, f);
    fclose(f);
}
static void spill_load(void)
{
    if (s_spill_loaded || !storage_ok()) {
        return;
    }
    s_spill_loaded = true;
    s_spill = heap_caps_calloc(SPILL_MAX, sizeof(spill_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_spill) {
        return;
    }
    FILE *f = fopen(SPILL_FILE, "rb");
    if (!f) {
        return;
    }
    int skipped = 0;
    while (s_nspill < SPILL_MAX && fread(&s_spill[s_nspill], sizeof(spill_t), 1, f) == 1) {
        s_spill[s_nspill].key[15] = 0;
        if (s_spill[s_nspill].key[0] && !spill_never(s_spill[s_nspill].key)) {
            s_nspill++;
        } else {
            skipped++;
        }
    }
    fclose(f);
    if (skipped) {
        spill_write_all();  // (spill_put writes record i at place i of the file: the file must hold the same records)
    }
    ESP_LOGI(SPILL_TAG, "the spill store has %d keys", s_nspill);
}
static int spill_find(const char *key)
{
    for (int i = 0; i < s_nspill; i++) {
        if (!strcmp(s_spill[i].key, key)) {
            return i;
        }
    }
    return -1;
}
static void spill_put(int i)  // (one record in place, or a new one at the end)
{
    FILE *f = fopen(SPILL_FILE, i < s_nspill - 1 ? "r+b" : "ab");
    if (!f) {
        f = fopen(SPILL_FILE, "wb");
    }
    if (!f) {
        return;
    }
    if (i < s_nspill - 1) {
        fseek(f, (long)(i * sizeof(spill_t)), SEEK_SET);
    }
    fwrite(&s_spill[i], sizeof(spill_t), 1, f);
    fclose(f);
}
// (the console: storeall, storedel, nvs)
int store_spill_count(void)
{
    spill_lock();
    spill_load();
    const int n = s_nspill;
    spill_unlock();
    return n;
}
bool store_spill_get(int i, char key[16], uint32_t *val)
{
    spill_lock();
    const bool ok = i >= 0 && i < s_nspill;
    if (ok) {
        memcpy(key, s_spill[i].key, 16);
        *val = s_spill[i].val;
    }
    spill_unlock();
    return ok;
}
bool store_spill_del(const char *key)
{
    spill_lock();
    spill_load();
    const int i = spill_find(key);
    if (i >= 0) {
        memmove(&s_spill[i], &s_spill[i + 1], (size_t)(s_nspill - i - 1) * sizeof(spill_t));
        s_nspill--;
        spill_write_all();
    }
    spill_unlock();
    return i >= 0;
}

settings_t g_settings = {
        .volume = CONFIG_TFN_VOLUME,
        .brightness = CONFIG_TFN_BRIGHTNESS,
        .mute = false,
        .auto_rotate = true,
        .sleep = 2,
};

const uint16_t SLEEP_SECONDS[SLEEP_CHOICES] = {0, 30, 60, 120, 300};
const char *const SLEEP_NAMES[SLEEP_CHOICES] = {"NEVER", "30 S", "1 MIN", "2 MIN", "5 MIN"};

// the language of all apps (SETUP > APP LANGUAGE): 0 English, 1 Hebrew, 0xFF = not set. A language key of
// an app (a name that ends with "_lang") that is not saved gives this value (the default of the app when not set); a
// change clears the language keys of the apps, so each app follows it until its own LANGUAGE item sets it apart.
static uint8_t s_lang_all = 0xFF;
static bool lang_key(const char *key)
{
    size_t n = strlen(key);
    return n > 5 && !strcmp(key + n - 5, "_lang");
}
uint32_t store_get(const char *key, uint32_t def)
{
    spill_lock();
    spill_load();
    const int si = s_spill ? spill_find(key) : -1;
    const uint32_t sv = si >= 0 ? s_spill[si].val : 0;
    spill_unlock();
    if (si >= 0) {
        return sv;
    }
    nvs_handle_t h;
    uint32_t v = def;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK) {
        if (nvs_get_u32(h, key, &v) != ESP_OK) {
            v = def;
            if (s_lang_all != 0xFF && lang_key(key)) {
                v = s_lang_all;
            }
        }
        nvs_close(h);
    } else if (s_lang_all != 0xFF && lang_key(key)) {
        v = s_lang_all;
    }
    return v;
}
int settings_lang_all(void)
{
    return s_lang_all == 0xFF ? -1 : s_lang_all;
}
void settings_lang_all_set(int lang)
{
    s_lang_all = (uint8_t)(lang ? 1 : 0);
    store_set("lang_all", s_lang_all);
    // the language keys of the apps go (the names first: an erase while the iterator runs is not safe)
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) {
        return;
    }
    char (*names)[NVS_KEY_NAME_MAX_SIZE] = malloc(64 * NVS_KEY_NAME_MAX_SIZE);
    int n = 0;
    nvs_iterator_t it = NULL;
    esp_err_t e = names ? nvs_entry_find(NVS_DEFAULT_PART_NAME, NVS_NS, NVS_TYPE_U32, &it) : ESP_FAIL;
    while (e == ESP_OK && n < 64) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        if (lang_key(info.key)) {
            snprintf(names[n++], NVS_KEY_NAME_MAX_SIZE, "%s", info.key);
        }
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    for (int i = 0; i < n; i++) {
        nvs_erase_key(h, names[i]);
    }
    nvs_commit(h);
    nvs_close(h);
    free(names);
    // (the language keys in the spill store go too)
    spill_lock();
    spill_load();
    int k = 0;
    for (int i = 0; i < s_nspill; i++) {
        if (!lang_key(s_spill[i].key)) {
            s_spill[k++] = s_spill[i];
        }
    }
    if (k != s_nspill) {
        s_nspill = k;
        spill_write_all();
    }
    spill_unlock();
}

void store_set(const char *key, uint32_t val)
{
    if (!key || strlen(key) > 15) {
        return;
    }
    spill_lock();
    spill_load();
    int si = s_spill ? spill_find(key) : -1;
    if (si >= 0) {  // (a key of the spill store stays there)
        if (s_spill[si].val != val) {
            s_spill[si].val = val;
            spill_put(si);
        }
        spill_unlock();
        return;
    }
    nvs_handle_t h;
    esp_err_t e = ESP_FAIL;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        uint32_t old;
        if (nvs_get_u32(h, key, &old) != ESP_OK || old != val) {
            e = nvs_set_u32(h, key, val);
            if (e == ESP_OK) {
                e = nvs_commit(h);
            }
        } else {
            e = ESP_OK;
        }
        nvs_close(h);
    }
    if (e != ESP_OK && s_spill && s_nspill < SPILL_MAX && !spill_never(key)) {  // (no room in the NVS: the spill)
        memset(&s_spill[s_nspill], 0, sizeof(spill_t));
        snprintf(s_spill[s_nspill].key, sizeof s_spill[s_nspill].key, "%s", key);
        s_spill[s_nspill].val = val;
        s_nspill++;
        spill_put(s_nspill - 1);
        ESP_LOGW(SPILL_TAG, "the NVS has no room (%s): %s goes to the spill store (%d keys)", esp_err_to_name(e), key,
                 s_nspill);
    }
    spill_unlock();
}

// ---------------------------------------------------------------- the room of the system in the NVS
// The NVS also keeps the keys of the system: the Bluetooth pairings (NimBLE writes "nimble_bond" itself), the WiFi, the
// FRIENDS. If the settings of the apps fill the NVS, a new pairing of a computer is not saved
// (ble_nvs_write_key_value rc=28), and the computer ends each link after a restart. So, after /data mounts: when fewer
// than ROOM_MIN entries are free, keys of the apps go to the spill store until ROOM_GOAL entries are free. The keys that
// the start reads before /data mounts (EARLY) stay in the NVS. The spill file is written and checked before a key goes
// out of the NVS. An NVS key that the spill store also has (an update that did not fit went to the spill store; the old
// value stayed in the NVS, and store_get reads the spill first) is an old copy: it goes, with no write.
#define ROOM_MIN  64
#define ROOM_GOAL 160
static const char *const EARLY[] = {"lang_all", "volume",    "bright",  "mute",    "autorot",   "sleep",
                                    "def_v",    "pads_set3", "drm_set", "ia_on",   "imu_scale", "tc_on",
                                    "pw_ls",    "pw_off",    "wk_call", "wk_ring", "ui_snd",    NULL};
static bool early_key(const char *k)
{
    if (!strncmp(k, "lay_", 4)) {
        return true;  // (the change of the flash layout)
    }
    for (int i = 0; EARLY[i]; i++) {
        if (!strcmp(k, EARLY[i])) {
            return true;
        }
    }
    return false;
}
int store_make_room(void)
{
    nvs_stats_t st;
    if (nvs_get_stats(NULL, &st) != ESP_OK || st.available_entries >= ROOM_MIN || !storage_ok()) {
        return 0;
    }
    int want = ROOM_GOAL - (int)st.available_entries;
    char (*names)[NVS_KEY_NAME_MAX_SIZE] =
            heap_caps_malloc((size_t)want * NVS_KEY_NAME_MAX_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint32_t *vals = heap_caps_malloc((size_t)want * sizeof(uint32_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char (*stale)[NVS_KEY_NAME_MAX_SIZE] =
            heap_caps_malloc((size_t)want * NVS_KEY_NAME_MAX_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    nvs_handle_t h;
    int n = 0, ns = 0;
    spill_lock();
    spill_load();
    if (names && vals && stale && s_spill && nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK) {
        // the names first (an erase while the iterator runs is not safe)
        nvs_iterator_t it = NULL;
        esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, NVS_NS, NVS_TYPE_U32, &it);
        while (e == ESP_OK && n + ns < want && s_nspill + n < SPILL_MAX) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            if (spill_find(info.key) >= 0) {
                if (early_key(info.key)) {
                    e = nvs_entry_next(&it);
                    continue;
                }  // (the start reads the NVS copy)
                snprintf(stale[ns++], NVS_KEY_NAME_MAX_SIZE, "%s", info.key);
            } else if (!early_key(info.key) && !spill_never(info.key) && strlen(info.key) < 16 &&
                       nvs_get_u32(h, info.key, &vals[n]) == ESP_OK) {
                snprintf(names[n], NVS_KEY_NAME_MAX_SIZE, "%s", info.key);
                n++;
            }
            e = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        // the spill file with the new records; then (only when it is written in full) the keys go out of the NVS
        const int old = s_nspill;
        for (int i = 0; i < n; i++) {
            memset(&s_spill[s_nspill], 0, sizeof(spill_t));
            snprintf(s_spill[s_nspill].key, sizeof s_spill[s_nspill].key, "%s", names[i]);
            s_spill[s_nspill].val = vals[i];
            s_nspill++;
        }
        bool ok = false;
        FILE *f = n ? fopen(SPILL_FILE, "wb") : NULL;
        if (f) {
            ok = fwrite(s_spill, sizeof(spill_t), (size_t)s_nspill, f) == (size_t)s_nspill;
            ok = fclose(f) == 0 && ok;
        }
        if (ok) {
            for (int i = 0; i < n; i++) {
                nvs_erase_key(h, names[i]);
            }
            nvs_commit(h);
        } else if (n) {
            s_nspill = old;  // (the NVS keeps all; the file as it was)
            spill_write_all();
            ESP_LOGE(SPILL_TAG, "the NVS is full, and the spill file could not take %d keys of the apps", n);
            n = 0;
        }
        for (int i = 0; i < ns; i++) {
            nvs_erase_key(h, stale[i]);
        }
        if (ns) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
    spill_unlock();
    free(names);
    free(vals);
    free(stale);
    if (n || ns) {
        ESP_LOGW(SPILL_TAG,
                 "the NVS had %u free entries: %d keys of the apps went to the spill store (%d keys), %d old "
                 "copies went",
                 (unsigned)st.available_entries, n, s_nspill, ns);
    }
    return n + ns;
}

// the one-time reset of the saved choices of the apps (Hebrew, TILT ON): defaults_once()
#include "settings_defaults.inc"

void settings_load(void)
{
    esp_err_t err = nvs_flash_init();
    if (!device_guard_enabled() && (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    defaults_once();  // (one time on each board: settings_defaults.inc)
    {
        uint32_t la = store_get("lang_all", 0xFF);
        s_lang_all = la > 1 ? 0xFF : (uint8_t)la;
    }

    g_settings.volume = (uint8_t)store_get("volume", g_settings.volume);
    g_settings.brightness = (uint8_t)store_get("bright", g_settings.brightness);
    g_settings.mute = store_get("mute", g_settings.mute) != 0;
    g_settings.auto_rotate = store_get("autorot", g_settings.auto_rotate) != 0;
    g_settings.sleep = (uint8_t)store_get("sleep", g_settings.sleep);
    if (g_settings.sleep >= SLEEP_CHOICES) {
        g_settings.sleep = 2;
    }
    if (g_settings.brightness < 10) {
        g_settings.brightness = 10;
    }
}

void settings_save(void)
{
    store_set("volume", g_settings.volume);
    store_set("bright", g_settings.brightness);
    store_set("mute", g_settings.mute);
    store_set("autorot", g_settings.auto_rotate);
    store_set("sleep", g_settings.sleep);
}

void settings_apply(void)
{
    audio_set_volume(g_settings.volume);
    audio_set_mute(g_settings.mute);
    board_backlight(g_settings.brightness);
}
