// THE TFN COMPANIONS (tfn/companion.h): the radio glue (the raw frames 'C' 'P' of link.c), the clock and the file of
// the paired list for the master side of libs/tfncomp (tc_master.c). It runs only between comp_open() and comp_close(),
// in the task of the app that opened it (no own task: one caller at a time).
#include "tfn/companion.h"
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "audio.h"
#include "link.h"
#include "notify.h"
#include "settings.h"
#include "storage.h"
#ifdef ESP_PLATFORM
#include "tfn_sstore.h"
#include "tfn_store.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "psa/crypto.h"
#endif
#ifdef ESP_PLATFORM
#include "esp_random.h"
#else
#include <stdlib.h>
#endif

static const char *TAG = "comp";
#define FILE_NOW "/data/companions.bin"
#define FILE_OLD "/data/remote_cp.bin"  // (the list of the first REMOTE test copies)

static bool s_open;
static char s_name[17];
static int64_t s_open_t;     // the time of comp_open()
static bool s_was[TCM_MAX];  // the online state of each paired companion at the last comp_poll()
static void near_stop(void);
// the sounds of the companions (near_sound)
enum {
    NS_ONLINE,   // a known companion is online (in an app)
    NS_OFFLINE,  // it went offline (in an app)
    NS_NEAR,     // a known companion came near (with a notice)
    NS_NEW       // a new companion with no master is near (with a notice)
};
static void near_sound(int what);

static bool io_send(const uint8_t *mac, const uint8_t *d, int n)
{
    return link_raw_send(mac, d, n);
}

static uint32_t io_now(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void io_random(uint8_t *p, int n)
{
#ifdef ESP_PLATFORM
    esp_fill_random(p, (size_t)n);  // (the radio is on: true random numbers)
#else
    for (int i = 0; i < n; i++) {
        p[i] = (uint8_t)rand();  // (the PC tests)
    }
#endif
}

static int io_channel(void)
{
    return link_channel();
}

static void save(void)
{
    uint8_t buf[TCM_SAVE_MAX];
    const int n = tcm_save(buf, sizeof buf);
    if (n > 0) {
        storage_write(FILE_NOW, buf, (size_t)n);
    }
}

static const tcm_io_t IO = {io_send, io_now, io_random, io_channel, save};

static void load(void)
{
    uint8_t buf[TCM_SAVE_MAX];
    size_t n = 0;
    if ((storage_read(FILE_NOW, buf, sizeof buf, &n) && n > 0) ||
        (storage_read(FILE_OLD, buf, sizeof buf, &n) && n > 0)) {
        tcm_load(buf, (int)n);
    }
}

bool comp_open(void)
{
    if (s_open) {
        return true;
    }
    near_stop();  // (the listen of the near companions ends: the app takes the radio)
    uint8_t mac[6];
    link_my_mac(mac);
    snprintf(s_name, sizeof s_name, "TFN %02X%02X", mac[4], mac[5]);
    tcm_init(&IO, mac, s_name);
    load();
    if (!link_raw_open(TC_MAGIC0, TC_MAGIC1)) {
        ESP_LOGW(TAG, "no radio");
        return false;
    }
    s_open = true;
    s_open_t = esp_timer_get_time() / 1000;
    memset(s_was, 0, sizeof s_was);
    ESP_LOGI(TAG, "the companions: %d paired", tcm_count());
    return true;
}

void comp_poll(void)
{
    if (!s_open) {
        return;
    }
    link_raw_t m;
    for (int k = 0; k < 16 && link_raw_recv(&m); k++) {
        tcm_rx(m.mac, m.data, m.len, m.rssi);
    }
    tcm_tick();
    // THE SOUND OF A CHANGE (in each app of the companions): a companion comes online = the rising notes, it goes
    // offline = a low falling note. The first seconds after the open are silent (the companions answer the first ping).
    const bool quiet = esp_timer_get_time() / 1000 - s_open_t < 5000;
    for (int i = 0; i < tcm_count() && i < TCM_MAX; i++) {
        const bool on = tcm_online(i);
        if (on != s_was[i] && !quiet) {
            near_sound(on ? NS_ONLINE : NS_OFFLINE);
            ESP_LOGI(TAG, "%s: %s", tcm_get(i)->name, on ? "online" : "offline");
        }
        s_was[i] = on;
    }
}

// ---------------------------------------------------------------- the near companions
#define NEAR_MAX    8
#define NEAR_EVERY  15000  // a listen each 15 s
#define NEAR_LISTEN 1200   // of 1.2 s
#define NEAR_AGAIN  50000  // a companion not heard for 50 s (3 listens) "comes near" again
static struct {
    bool loaded, have_file, on;  // the list of the file; a listen runs now
    int64_t next, until, quiet;  // quiet: no notice before this time (the first seconds after a start)
    int64_t last_end;            // the end of the last listen
    int frames, empty;
    int n;
    uint8_t mac[TCM_MAX][6];
    char name[TCM_MAX][17];
    struct {
        uint8_t mac[6];
        int64_t seen;
    } heard[NEAR_MAX];
} s_near;

static void near_stop(void)
{
    if (s_near.on) {
        link_raw_close();
        s_near.on = false;
    }
}

// the paired companions of the file (their MACs and names), after the start and after each comp_close()
static void near_load(void)
{
    uint8_t buf[TCM_SAVE_MAX];
    size_t n = 0;
    s_near.loaded = true;
    s_near.n = 0;
    s_near.have_file = storage_read(FILE_NOW, buf, sizeof buf, &n) && n >= 4 && buf[0] == 'T' && buf[1] == 'M';
    if (!s_near.have_file) {
        return;
    }
    const int rec = buf[2] == 1 ? 48 : TCM_REC;
    for (int i = 0; i < buf[3] && i < TCM_MAX && 4 + (i + 1) * rec <= (int)n; i++) {
        memcpy(s_near.mac[i], buf + 4 + i * rec, 6);
        memcpy(s_near.name[i], buf + 4 + i * rec + 6, 16);
        s_near.name[i][16] = 0;
        s_near.n = i + 1;
    }
}

// THE SOUNDS OF THE COMPANIONS (tunes: the notes play one after the other). A notice also rings the bell of the
// notices when its banner shows: the tune of a notice starts after the bell.
static const tune_note_t TUNE_ONLINE[] = {{0, 110, 84, 0, W_SINE, 14}, {120, 300, 91, 0, W_SINE | TUNE_HOLD, 13}};
static const tune_note_t TUNE_OFFLINE[] = {{0, 140, 88, 0, W_SINE, 12}, {150, 320, 81, -2, W_SINE | TUNE_HOLD, 11}};
static const tune_note_t TUNE_NEAR[] = {
        {450, 110, 84, 0, W_SINE, 14}, {570, 110, 88, 0, W_SINE, 14}, {690, 320, 91, 0, W_SINE | TUNE_HOLD, 13}};
static const tune_note_t TUNE_NEW[] = {{450, 140, 79, 0, W_TRI, 15}, {610, 340, 74, 0, W_TRI | TUNE_HOLD, 14}};

static void near_sound(int what)
{
    switch (what) {
    case NS_ONLINE:
        audio_tune(TUNE_ONLINE, 2);
        break;
    case NS_OFFLINE:
        audio_tune(TUNE_OFFLINE, 2);
        break;
    case NS_NEAR:
        audio_tune(TUNE_NEAR, 3);
        break;
    default:
        audio_tune(TUNE_NEW, 2);
        break;
    }
}

static void near_heard(const uint8_t *mac, const uint8_t *d, int len, int64_t now)
{
    tc_msg_t m;
    if (!tc_parse(d, len, &m) || m.type != TC_HELLO) {
        return;
    }
    int slot = -1, old = 0;
    for (int i = 0; i < NEAR_MAX; i++) {
        if (!memcmp(s_near.heard[i].mac, mac, 6)) {
            slot = i;
            break;
        }
        if (s_near.heard[i].seen < s_near.heard[old].seen) {
            old = i;
        }
    }
    const bool fresh = slot < 0 || now - s_near.heard[slot].seen > NEAR_AGAIN;
    if (slot < 0) {
        slot = old;
        memcpy(s_near.heard[slot].mac, mac, 6);
    }
    s_near.heard[slot].seen = now;
    if (!fresh || now < s_near.quiet) {
        return;  // (heard before; or the first minutes after a start: the companions that are there already)
    }
    char title[48], text[96];
    for (int i = 0; i < s_near.n; i++) {
        if (!memcmp(s_near.mac[i], mac, 6)) {
            snprintf(title, sizeof title, "%s", s_near.name[i]);
            snprintf(text, sizeof text, "%s", "The companion is near and ready.");
            near_sound(NS_NEAR);
            notify_post("COMPANIONS", title, text, "");
            ESP_LOGI(TAG, "near: %s", s_near.name[i]);
            return;
        }
    }
    if (!(m.flags & TC_FL_PAIRED)) {
        snprintf(title, sizeof title, "A new companion (%02X%02X)", mac[4], mac[5]);
        snprintf(text, sizeof text, "%s", "A TFN companion with no board is near. Tap to pair it.");
        near_sound(NS_NEW);
        notify_post("COMPANIONS", title, text, "add");
        ESP_LOGI(TAG, "near: a new companion %02X%02X", mac[4], mac[5]);
    }
}

void comp_bg_tick(int64_t now)
{
    if (s_open) {
        return;  // (an app uses the companions)
    }
    if (!s_near.loaded) {
        near_load();
        s_near.next = now + 5000;
        s_near.quiet = now + 25000;
    }
    if (s_near.on) {
        link_raw_t m;
        for (int k = 0; k < 16 && link_raw_recv(&m); k++) {
            s_near.frames++;
            near_heard(m.mac, m.data, m.len, now);
        }
        if (now >= s_near.until) {
            link_raw_close();
            s_near.on = false;
            s_near.last_end = now;
            s_near.next = now + NEAR_EVERY;
            if (!s_near.frames && ++s_near.empty % 20 == 1) {
                ESP_LOGI(TAG, "near: %d listens with no companion", s_near.empty);
            }
            s_near.frames = 0;
        }
        return;
    }
    if (!s_near.have_file || now < s_near.next || link_ready() || !store_get(COMP_ALERTS_KEY, 1)) {
        if (now >= s_near.next) {
            s_near.next = now + NEAR_EVERY;
        }
        return;
    }
    if (link_raw_open(TC_MAGIC0, TC_MAGIC1)) {
        s_near.on = true;
        s_near.until = now + NEAR_LISTEN;
        if (now - s_near.last_end > 3 * NEAR_EVERY) {
            // (no listen for a long time: the screen was off, or an app used the radio. The companions of this
            // listen were maybe there all the time: they get no notice)
            s_near.quiet = now + NEAR_LISTEN + 100;
        }
    } else {
        s_near.next = now + NEAR_EVERY;
    }
}

void comp_close(void)
{
    if (!s_open) {
        return;
    }
    save();
    link_raw_close();
    s_open = false;
    near_load();  // (the list may have changed)
    // (the companions of the app were near: no notice for them in the next minutes)
    const int64_t now = esp_timer_get_time() / 1000;
    for (int i = 0; i < s_near.n && i < NEAR_MAX; i++) {
        memcpy(s_near.heard[i].mac, s_near.mac[i], 6);
        s_near.heard[i].seen = now;
    }
    s_near.next = now + NEAR_EVERY;
}

// the board page (web_comp.c): an app uses the companions now (a restore of the files waits)
bool comp_in_use(void)
{
    return s_open;
}

// the board page wrote new files: the near list reads them again
void comp_files_changed(void)
{
    near_stop();
    s_near.loaded = false;
}

// ---------------------------------------------------------------- the firmware of the companions in the store
#define FW_INDEX_MAX 4096
#define FW_IMAGE_MAX 0x1E0000  // (an app slot of the companion)
static struct {
    volatile int state;  // 0 nothing, 1 the job runs, 2 done, -1 failed
    char target[12];
    bool image;
    int fw, size;
    uint8_t sha[32];
    uint8_t *img;
    char msg[96];
} s_fw;

static int hexval(char c)
{
    return c >= '0' && c <= '9'   ? c - '0'
           : c >= 'a' && c <= 'f' ? c - 'a' + 10
           : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                  : -1;
}

// the line of the target in the index -> s_fw.fw, size, sha. False: no such line.
static bool fw_index_line(const char *t)
{
    if (strncmp(t, "tfn_compfw 1", 12)) {
        return false;
    }
    for (const char *l = strchr(t, '\n'); l; l = strchr(l + 1, '\n')) {
        char name[16], hex[65];
        int fw = 0, size = 0;
        if (sscanf(l + 1, "%15s %d %d %64s", name, &fw, &size, hex) != 4 || strcmp(name, s_fw.target)) {
            continue;
        }
        for (int i = 0; i < 32; i++) {
            const int a = hexval(hex[2 * i]), b = hexval(hex[2 * i + 1]);
            if (a < 0 || b < 0) {
                return false;
            }
            s_fw.sha[i] = (uint8_t)(a << 4 | b);
        }
        s_fw.fw = fw;
        s_fw.size = size;
        return fw > 0 && size > 0 && size <= FW_IMAGE_MAX;
    }
    return false;
}

static void fw_job(void)
{
#ifndef ESP_PLATFORM
    snprintf(s_fw.msg, sizeof s_fw.msg, "no store on the PC");
    s_fw.state = -1;
#else
    uint8_t *idx = NULL, *sig = NULL;
    size_t ni = 0, ns = 0;
    char *m = s_fw.msg;
    const int ml = sizeof s_fw.msg;
    if (!tfn_store_get_file("/api/store/compfw?file=index.txt", "compfw/index.txt", &idx, &ni, FW_INDEX_MAX, m, ml) ||
        !tfn_store_get_file("/api/store/compfw?file=index.sig", "compfw/index.sig", &sig, &ns, 64, m, ml)) {
        goto fail;
    }
    idx[ni < FW_INDEX_MAX ? ni : FW_INDEX_MAX - 1] = 0;
    if (!tfn_sstore_store_signed(idx, ni, sig, ns, m, ml)) {
        goto fail;
    }
    if (!fw_index_line((const char *)idx)) {
        snprintf(m, ml, "THE STORE HAS NO FIRMWARE FOR %s", s_fw.target);
        goto fail;
    }
    if (s_fw.image) {
        char path[64], rel[40];
        size_t n = 0;
        snprintf(path, sizeof path, "/api/store/compfw?file=%s.bin", s_fw.target);
        snprintf(rel, sizeof rel, "compfw/%s.bin", s_fw.target);
        if (!tfn_store_get_file(path, rel, &s_fw.img, &n, (size_t)s_fw.size, m, ml)) {
            goto fail;
        }
        uint8_t h[32];
        size_t hl = 0;
        if (n != (size_t)s_fw.size || psa_hash_compute(PSA_ALG_SHA_256, s_fw.img, n, h, 32, &hl) != PSA_SUCCESS ||
            memcmp(h, s_fw.sha, 32)) {
            snprintf(m, ml, "THE FIRMWARE FILE IS NOT THE FILE OF THE INDEX");
            free(s_fw.img);
            s_fw.img = NULL;
            goto fail;
        }
    }
    free(idx);
    free(sig);
    ESP_LOGI(TAG, "the store: the firmware %d of %s%s", s_fw.fw, s_fw.target, s_fw.image ? " (the file came)" : "");
    s_fw.state = 2;
    return;
fail:
    free(idx);
    free(sig);
    ESP_LOGW(TAG, "the store: the firmware of %s: %s", s_fw.target, m);
    s_fw.state = -1;
#endif
}

#ifdef ESP_PLATFORM
static void fw_task(void *arg)
{
    (void)arg;
    fw_job();
    vTaskDeleteWithCaps(NULL);
}
#endif

bool comp_fw_start(const char *target, bool image)
{
    if (s_fw.state == 1 || !target || strlen(target) >= sizeof s_fw.target) {
        return false;
    }
    if (s_fw.img) {
        free(s_fw.img);  // (a file that no app took)
    }
    memset(&s_fw, 0, sizeof s_fw);
    snprintf(s_fw.target, sizeof s_fw.target, "%s", target);
    s_fw.image = image;
    s_fw.state = 1;
#ifdef ESP_PLATFORM
    if (xTaskCreatePinnedToCoreWithCaps(fw_task, "compfw", 8192, NULL, 2, NULL, 1,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        snprintf(s_fw.msg, sizeof s_fw.msg, "no memory");
        s_fw.state = -1;
    }
#else
    fw_job();
#endif
    return true;
}

int comp_fw_poll(int *fw, uint8_t **img, int *len, char *msg, int mlen)
{
    const int st = s_fw.state;
    if (st == 1 || st == 0) {
        return 0;
    }
    if (st < 0) {
        if (msg) {
            snprintf(msg, (size_t)mlen, "%s", s_fw.msg);
        }
        return -1;
    }
    if (fw) {
        *fw = s_fw.fw;
    }
    if (img) {
        *img = s_fw.img;  // (the caller frees it)
        s_fw.img = NULL;
    }
    if (len) {
        *len = s_fw.size;
    }
    return 1;
}

const char *comp_my_name(void)
{
    return s_name;
}

int comp_with_role(int role, int *pin)
{
    int best = -1, best_pin = -1;
    for (int i = 0; i < tcm_count(); i++) {
        const tcm_comp_t *c = tcm_get(i);
        if (!c->has_info) {
            continue;
        }
        for (int k = 0; k < c->info.nwire; k++) {
            if (c->info.wire[k].role == role && (best < 0 || (tcm_online(i) && !tcm_online(best)))) {
                best = i;
                best_pin = c->info.wire[k].pin;
            }
        }
    }
    if (pin) {
        *pin = best_pin;
    }
    return best;
}
