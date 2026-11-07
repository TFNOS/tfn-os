// The firmware update over the network (OTA; see ota.h).
// - POST /api/ota, the body = the app image (build/tfn_os.bin). The board reads the first block (the image
//   header and the version), opens the menu and shows the update panel, erases the area of the image in the
//   other app slot (in 64 KB blocks; the data waits in the network buffers), writes the image as the data comes,
//   and looks for the board tag in the image. The tag must be the tag of this board: an image for another
//   board, or an old image with no OTA code, is refused (the slot is not used). esp_ota_end checks the image
//   (the checksum and the hash). Then the board sets the new slot for the next boot and restarts.
//   The answer has the times of the steps (erase_ms, write_ms): the manager learns its time estimates from them.
//   The steps themselves are in ota_flow.c (pure C, with a fake flash in tests/unit/test_ota_flow.c); this file
//   gives them the network, the flash, the hash and the texts of the answer.
// - The signed OTA (ota_sig.h): the file can end with the trailer "TFNOTA1" (1024 bytes after the image). The update
//   hashes the image as it comes, keeps the trailer and checks it before esp_ota_end: the hash, the kind, the
//   version, a signature of a key of the key list (TFN_OTA_PUBKEY1_BYTES .. 4 of tfn_defaults.h) and the security
//   version (NVS tfnota/minsv). REPORT (CONFIG_TFN_OTA_SIG_REPORT): the result goes into the log, last_sig and the
//   answer, and the update goes on. ENFORCE (CONFIG_TFN_OTA_SIG_ENFORCE): each result but "ok" stops the update
//   (the code "sig"; the slot does not become the boot slot). A build with device protection has no check (its
//   secure boot checks the image). The old firmware writes the trailer into the slot after the image and never
//   reads it.
// - The rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE): the new firmware starts as "pending". After 20 s
//   of normal run it marks itself good (ota_tick). If it restarts before that, the bootloader starts the old
//   firmware again. In ENFORCE the mark comes only after a self-test (ota_sig_self_test): the own trailer verifies
//   with the own key list, and /api/ota is registered. A failed self-test goes back to the old firmware at once, but
//   only when ESP-IDF can go back and the other slot has TFN OS of this kind. Else the firmware runs on with no restart
//   (self_back, ota_sig_back_plan): a restart loop must never occur. minsv changes only after a passed self-test.
// - The version: version.txt in the project folder (ESP-IDF puts it into the app description). A release
//   tool makes it one higher for each published build.
#include "tfn_util.h"
#include "ota.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_mmu_map.h"
#include "esp_image_format.h"
#include "nvs.h"
#include "psa/crypto.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "app.h"
#include "gfx.h"
#include "device_guard.h"
#include "ota_flow.h"
#include "ota_sig.h"
#include "tfn_defaults.h"
#include "tfn/theme.h"

static const char *TAG = "ota";

// The board tag. The update reads it from the new image, so that a 3.49 never starts a CoreS3 image.
#define OTA_KIND BOARD_KIND_NAME
__attribute__((used)) const char ota_board_tag[] = "@ARCADE-BOARD:" OTA_KIND "@";
#define TAG_KEEP OTA_FLOW_KEEP      // the bytes kept from the last block: a tag across two blocks
#define DESC_VER OTA_FLOW_DESC_VER  // the version in the image: esp_app_desc_t (at 0x20) + 16

// The mode of the signature check: OFF with device protection or CONFIG_TFN_OTA_SIG_OFF, ENFORCE with
// CONFIG_TFN_OTA_SIG_ENFORCE, else REPORT
#if (defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION) || defined(CONFIG_TFN_OTA_SIG_OFF)
#define OTA_SIG_ON 0
#else
#define OTA_SIG_ON 1
#endif
#if OTA_SIG_ON && defined(CONFIG_TFN_OTA_SIG_ENFORCE)
#define OTA_SIG_ENF 1
#else
#define OTA_SIG_ENF 0
#endif
#define OTA_SIG_MODE (OTA_SIG_ENF ? OTA_SIG_ENFORCE : OTA_SIG_ON ? OTA_SIG_REPORT : OTA_SIG_OFF)

#if OTA_SIG_ENF
// A board in ENFORCE with no key can take no update over the network: such a build must not exist (a build with no
// key is REPORT at most). The first byte of each key list entry: 0x04 for a key, 0 for an empty entry.
#define OTA_KEY_B0_(a, ...) a
#define OTA_KEY_B0(...)     OTA_KEY_B0_(__VA_ARGS__, 0)
#if OTA_KEY_B0(TFN_OTA_PUBKEY1_BYTES) != 0x04 && OTA_KEY_B0(TFN_OTA_PUBKEY2_BYTES) != 0x04 &&                          \
        OTA_KEY_B0(TFN_OTA_PUBKEY3_BYTES) != 0x04 && OTA_KEY_B0(TFN_OTA_PUBKEY4_BYTES) != 0x04
#error "CONFIG_TFN_OTA_SIG_ENFORCE needs at least one OTA key (TFN_OTA_PUBKEY1_BYTES .. 4, main/tfn_local.h). A build with no key: use CONFIG_TFN_OTA_SIG_REPORT"
#endif
#endif

// The rollback test (a test build only): CONFIG_TFN_OTA_CRASH_TEST or the C define ARCADE_OTA_CRASH_TEST
#if (defined(CONFIG_TFN_OTA_CRASH_TEST) && CONFIG_TFN_OTA_CRASH_TEST) || defined(ARCADE_OTA_CRASH_TEST)
#define OTA_CRASH_TEST 1
#else
#define OTA_CRASH_TEST 0
#endif

// the panel: the step and the percent (written by the web task, read by the main loop)
enum {
    ST_NONE,
    ST_ERASE,
    ST_WRITE,
    ST_RESTART,
    ST_FAILED
};
static volatile int s_step = ST_NONE, s_pct;
static volatile int64_t s_fail_at;
static volatile bool s_busy;
static portMUX_TYPE s_ota_mux = portMUX_INITIALIZER_UNLOCKED;
static bool ota_claim(void)
{
    taskENTER_CRITICAL(&s_ota_mux);
    bool ok = !s_busy;
    if (ok) {
        s_busy = true;
    }
    taskEXIT_CRITICAL(&s_ota_mux);
    return ok;
}

static void ota_release(void)
{
    taskENTER_CRITICAL(&s_ota_mux);
    s_busy = false;
    taskEXIT_CRITICAL(&s_ota_mux);
}

static char s_new_ver[33];
static char s_fail_msg[240];  // the reason of the last update that failed (since the start)
static char s_fail_code[12];  // its short code: net_none, net_err, net_closed, size, mem, ram, slot, write,
                              // kind, tag, sig, check, end, boot, busy

const char *ota_version(void)
{
    return esp_app_get_description()->version;
}

const char *ota_slot(void)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    return run ? run->label : "?";
}

static esp_err_t answer(httpd_req_t *req, const char *status, bool ok, const char *msg, const char *extra)
{
    char b[420];
    int n = snprintf(b, sizeof b, "{\"ok\":%s,\"msg\":\"%s\"%s}", ok ? "true" : "false", msg, extra ? extra : "");
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, b, n);
    return ESP_OK;
}

static void restart_cb(void *arg)
{
    (void)arg;
    esp_restart();
}

#if defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION
// the tags in buf[0..n): own = this board, other = the kind of another board (empty: none)
static void scan_tags(const char *buf, int n, bool *own, char other[16])
{
    ota_flow_scan_tags(ota_board_tag, buf, n, own, other);
}
#endif

// the answer of an update that failed (ota_flow_run released the claim and the flash handle)
static esp_err_t fail_c(httpd_req_t *req, const char *status, const char *code, const char *msg)
{
    ESP_LOGW(TAG, "update failed (%s): %s", code, msg);
    snprintf(s_fail_msg, sizeof s_fail_msg, "%s", msg);
    snprintf(s_fail_code, sizeof s_fail_code, "%s", code);
    s_step = ST_FAILED;
    s_fail_at = tfn_now_ms();
    char x[24];
    snprintf(x, sizeof x, ",\"code\":\"%s\"", code);
    return answer(req, status, false, msg, x);
}

// one block of the body: > 0 the bytes, 0 the manager closed the connection, RECV_NONE 60 s with no data, RECV_ERR a
// socket error 4 times (one socket error on a VPN link must not stop an update at once, and the text must not say
// "no data for 60 s"). s_recv_errno: the errno of the last socket error.
#define RECV_NONE OTA_FLOW_RECV_NONE
#define RECV_ERR  OTA_FLOW_RECV_ERR
static int s_recv_errno;
static int recv_block(httpd_req_t *req, char *dst, int want)
{
    int errs = 0;
    for (int waits = 0; waits < 12;) {
        int r = httpd_req_recv(req, dst, want);
        if (r >= 0) {
            return r;
        }
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            waits++;
            continue;
        }
        s_recv_errno = errno;
        ESP_LOGW(TAG, "update: a socket error %d (errno %d %s)", r, s_recv_errno, strerror(s_recv_errno));
        if (++errs > 3) {
            return RECV_ERR;
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    return RECV_NONE;
}

// the text of a receive fault, with the KB that came
static esp_err_t recv_fail(httpd_req_t *req, int r, int done, int total)
{
    char m[200];
    if (r == RECV_NONE) {
        snprintf(m, sizeof m,
                 "no data came for 60 s after %d of %d KB (the network link to the board stopped). The old "
                 "firmware stays: try again",
                 done / 1024, total / 1024);
        return fail_c(req, "408 Request Timeout", "net_none", m);
    }
    if (r == RECV_ERR) {
        snprintf(m, sizeof m,
                 "the network connection broke after %d of %d KB (socket error %d: %s). The old firmware "
                 "stays: try again, with a better WiFi or LAN link if it breaks again",
                 done / 1024, total / 1024, s_recv_errno, strerror(s_recv_errno));
        return fail_c(req, "400 Bad Request", "net_err", m);
    }
    snprintf(m, sizeof m,
             "the sender closed the connection after %d of %d KB (the manager stopped or restarted). The old "
             "firmware stays: try again",
             done / 1024, total / 1024);
    return fail_c(req, "400 Bad Request", "net_closed", m);
}

// The slot that an update writes. The Tab5 (Tab5Boot: the boot menu, TFN OS in ota_0 / ota_1, M5PaperDOS-X
// in ota_2): always the TFN slot that does not run (the IDF choice of the next slot would take ota_2 after ota_1).
static const esp_partition_t *ota_next_slot(void)
{
#if BOARD_BOOT_MENU
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_subtype_t other = run && run->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0
                                                  ? ESP_PARTITION_SUBTYPE_APP_OTA_1
                                                  : ESP_PARTITION_SUBTYPE_APP_OTA_0;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, other, NULL);
#else
    return esp_ota_get_next_update_partition(NULL);
#endif
}

#if defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION
static struct {
    esp_ota_handle_t handle;
    const esp_partition_t *partition;
    size_t total, done, last_len, last_offset, tail_len;
    uint8_t expected[32], last_hash[32], tail[TAG_KEEP];
    psa_hash_operation_t hash;
    int64_t last_ms;
    bool own, other;
} s_usb;
static SemaphoreHandle_t volatile s_usb_mutex;
static void ota_usb_abort_locked(void)
{
    bool owned = s_usb.total != 0;
    if (s_usb.handle) {
        esp_ota_abort(s_usb.handle);
    }
    psa_hash_abort(&s_usb.hash);
    memset(&s_usb, 0, sizeof s_usb);
    if (owned) {
        ota_release();
        s_step = ST_NONE;
    }
}
static bool ota_usb_begin_locked(size_t total, const uint8_t hash[32], char *msg, size_t cap)
{
    if (!device_guard_usb_admin()) {
        snprintf(msg, cap, "ADMIN_REQUIRED");
        return false;
    }
    if (s_usb.handle && !s_usb.done && s_usb.total == total && !memcmp(s_usb.expected, hash, 32)) {
        return true;
    }
    const esp_partition_t *dst = ota_next_slot();
    if (s_busy || !dst || total < 65536 || total > dst->size) {
        snprintf(msg, cap, "BUSY_OR_SIZE");
        return false;
    }
    memset(&s_usb, 0, sizeof s_usb);
    s_usb.hash = psa_hash_operation_init();
    if (psa_hash_setup(&s_usb.hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        snprintf(msg, cap, "NO_HASH_MEMORY");
        return false;
    }
    if (!ota_claim()) {
        psa_hash_abort(&s_usb.hash);
        memset(&s_usb, 0, sizeof s_usb);
        snprintf(msg, cap, "BUSY_OR_SIZE");
        return false;
    }
    s_usb.partition = dst;
    s_usb.total = total;
    s_usb.last_ms = tfn_now_ms();
    memcpy(s_usb.expected, hash, 32);
    app_open(&app_menu);
    app_screen_wake();
    s_step = ST_ERASE;
    s_pct = 0;
    esp_err_t e = esp_ota_begin(dst, total, &s_usb.handle);
    if (e != ESP_OK) {
        ota_usb_abort_locked();
        snprintf(msg, cap, "BEGIN_FAILED");
        return false;
    }
    s_usb.last_ms = tfn_now_ms();
    s_step = ST_WRITE;
    return true;
}
static bool ota_usb_write_locked(size_t offset, const uint8_t *data, size_t len, char *msg, size_t cap)
{
    if (!device_guard_usb_admin() || !s_usb.handle || !len || len > 512) {
        snprintf(msg, cap, "NO_UPDATE");
        return false;
    }
    uint8_t hash[32];
    size_t olen = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, data, len, hash, sizeof hash, &olen) != PSA_SUCCESS || olen != 32) {
        snprintf(msg, cap, "HASH_FAILED");
        return false;
    }
    if (offset == s_usb.last_offset && len == s_usb.last_len && !memcmp(hash, s_usb.last_hash, 32)) {
        s_usb.last_ms = tfn_now_ms();
        return true;
    }
    if (offset != s_usb.done || len > s_usb.total - s_usb.done || (!offset && (len < 0x60 || data[0] != 0xe9))) {
        snprintf(msg, cap, "OFFSET_OR_IMAGE");
        return false;
    }
    if (!offset) {
        snprintf(s_new_ver, sizeof s_new_ver, "%.32s", (const char *)data + DESC_VER);
    }
    uint8_t block[512 + TAG_KEEP];
    memcpy(block, s_usb.tail, s_usb.tail_len);
    memcpy(block + s_usb.tail_len, data, len);
    bool own = false;
    char other[16] = {0};
    scan_tags((const char *)block, (int)(s_usb.tail_len + len), &own, other);
    s_usb.own |= own;
    s_usb.other |= other[0] != 0;
    size_t used = s_usb.tail_len + len;
    s_usb.tail_len = used < TAG_KEEP ? used : TAG_KEEP;
    memcpy(s_usb.tail, block + used - s_usb.tail_len, s_usb.tail_len);
    if (esp_ota_write(s_usb.handle, data, len) != ESP_OK || psa_hash_update(&s_usb.hash, data, len) != PSA_SUCCESS) {
        ota_usb_abort_locked();
        snprintf(msg, cap, "WRITE_FAILED");
        return false;
    }
    s_usb.last_offset = offset;
    s_usb.last_len = len;
    memcpy(s_usb.last_hash, hash, 32);
    s_usb.done += len;
    s_usb.last_ms = tfn_now_ms();
    s_pct = (int)(100 * s_usb.done / s_usb.total);
    return true;
}
static bool ota_usb_finish_locked(char *msg, size_t cap)
{
    if (!device_guard_usb_admin() || !s_usb.handle) {
        snprintf(msg, cap, "NO_UPDATE");
        return false;
    }
    uint8_t hash[32];
    size_t olen = 0;
    bool valid = s_usb.done == s_usb.total && s_usb.own && !s_usb.other &&
                 psa_hash_finish(&s_usb.hash, hash, sizeof hash, &olen) == PSA_SUCCESS && olen == 32 &&
                 !memcmp(hash, s_usb.expected, 32);
    if (!valid) {
        ota_usb_abort_locked();
        snprintf(msg, cap, "INCOMPLETE_OR_WRONG_IMAGE");
        return false;
    }
    esp_ota_handle_t h = s_usb.handle;
    s_usb.handle = 0;
    if (esp_ota_end(h) != ESP_OK) {
        ota_usb_abort_locked();
        snprintf(msg, cap, "SIGNATURE_OR_IMAGE_FAILED");
        return false;
    }
    if (esp_ota_set_boot_partition(s_usb.partition) != ESP_OK) {
        ota_usb_abort_locked();
        snprintf(msg, cap, "BOOT_SELECTION_FAILED");
        return false;
    }
    s_step = ST_RESTART;
    esp_timer_handle_t timer;
    esp_timer_create_args_t args = {.callback = restart_cb, .name = "usb_restart"};
    if (esp_timer_create(&args, &timer) == ESP_OK) {
        esp_timer_start_once(timer, 750000);
    } else {
        snprintf(msg, cap, "REBOOT_REQUIRED");
        return true;
    }
    return true;
}
bool ota_usb_begin(size_t total, const uint8_t hash[32], char *msg, size_t cap)
{
    if (!s_usb_mutex) {
        s_usb_mutex = xSemaphoreCreateMutex();
    }
    if (!s_usb_mutex) {
        snprintf(msg, cap, "NO_UPDATE_MEMORY");
        return false;
    }
    xSemaphoreTake(s_usb_mutex, portMAX_DELAY);
    bool ok = ota_usb_begin_locked(total, hash, msg, cap);
    xSemaphoreGive(s_usb_mutex);
    return ok;
}
bool ota_usb_write(size_t offset, const uint8_t *data, size_t len, char *msg, size_t cap)
{
    if (!s_usb_mutex) {
        snprintf(msg, cap, "NO_UPDATE");
        return false;
    }
    xSemaphoreTake(s_usb_mutex, portMAX_DELAY);
    bool ok = ota_usb_write_locked(offset, data, len, msg, cap);
    xSemaphoreGive(s_usb_mutex);
    return ok;
}
bool ota_usb_finish(char *msg, size_t cap)
{
    if (!s_usb_mutex) {
        snprintf(msg, cap, "NO_UPDATE");
        return false;
    }
    xSemaphoreTake(s_usb_mutex, portMAX_DELAY);
    bool ok = ota_usb_finish_locked(msg, cap);
    xSemaphoreGive(s_usb_mutex);
    return ok;
}
void ota_usb_abort(void)
{
    if (!s_usb_mutex) {
        return;
    }
    xSemaphoreTake(s_usb_mutex, portMAX_DELAY);
    ota_usb_abort_locked();
    xSemaphoreGive(s_usb_mutex);
}
#endif

// the answer when the trial SHA-256 before the erase fails: too little free internal RAM (the numbers in the text)
static esp_err_t ota_ram_fail(httpd_req_t *req)
{
    char m[220];
    snprintf(m, sizeof m,
             "the board has too little free internal RAM for the check of a new firmware (%u bytes free, "
             "the largest block %u). Close the open app (the menu) or restart the board, then try again",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    snprintf(s_fail_msg, sizeof s_fail_msg, "%s", m);
    snprintf(s_fail_code, sizeof s_fail_code, "ram");
    return answer(req, "503 Service Unavailable", false, m, ",\"code\":\"ram\"");
}

// the answer when a flash write fails (done: the bytes that are in the flash)
static esp_err_t ota_write_fail(httpd_req_t *req, esp_err_t e, int done)
{
    char m[160];
    if (e == ESP_ERR_OTA_VALIDATE_FAILED) {
        snprintf(m, sizeof m, "the image is not for this chip (the ESP32-S3)");
    } else {
        snprintf(m, sizeof m, "the flash write failed at %d KB (%s). Restart the board, then try again", done / 1024,
                 esp_err_to_name(e));
    }
    return fail_c(req, "500 Internal Server Error", e == ESP_ERR_OTA_VALIDATE_FAILED ? "kind" : "write", m);
}

// the answer when the image has no tag of this board kind (other: the tag of another kind, or "")
static esp_err_t ota_tag_fail(httpd_req_t *req, const char *other)
{
    char m[120];
    if (other[0]) {
        snprintf(m, sizeof m, "this firmware is for another board kind (%s); this board is %s", other, OTA_KIND);
    } else {
        snprintf(m, sizeof m, "no board tag in the image: an old firmware or not an arcade firmware");
    }
    return fail_c(req, "400 Bad Request", other[0] ? "kind" : "tag", m);
}

// the answer when esp_ota_end fails: the reason from the error, the trial SHA-256 (hst) and the free flash map
static esp_err_t ota_end_fail(httpd_req_t *req, esp_err_t e, psa_status_t hst, size_t mfree)
{
    char m[220];
    const char *code = "check";
    if (e != ESP_ERR_OTA_VALIDATE_FAILED) {
        snprintf(m, sizeof m, "the end of the update failed (%s). The old firmware stays: try again",
                 esp_err_to_name(e));
        code = "end";
    } else if (hst != PSA_SUCCESS) {
        snprintf(m, sizeof m,
                 "the board had too little free internal RAM for the check (%u bytes free): not a damaged "
                 "image. Close the open app or restart the board, then try again",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        code = "ram";
    } else if (mfree < 1024 * 1024) {
        snprintf(m, sizeof m,
                 "the check could not map the image (%u KB free in the flash map; the board log has the map). "
                 "Probably not a damaged image: restart the board, then try again",
                 (unsigned)(mfree / 1024));
    } else {
        snprintf(m, sizeof m,
                 "the image is damaged: its checksum or hash is wrong (a fault in the transfer or in the file). "
                 "Try again; if it fails again, build the release again");
    }
    return fail_c(req, "400 Bad Request", code, m);
}

// the end of a good update: the log line, the answer with the times (and the result of the signature check), and
// the restart after 1.5 s
static esp_err_t ota_done(httpd_req_t *req, const esp_partition_t *dst, const ota_flow_out_t *out, int64_t t0,
                          int64_t t1)
{
    int64_t t2 = tfn_now_ms();
    int total = out->total;
    ESP_LOGI(TAG, "update done: %d bytes, erase %d ms, write %d ms, next boot from %s (version %s): restart", total,
             (int)(t1 - t0), (int)(t2 - t1), dst->label, s_new_ver);
    s_step = ST_RESTART;
    char m[96], x[112];
    snprintf(m, sizeof m, "%d KB written into %s, the board restarts", total / 1024, dst->label);
    int k = snprintf(x, sizeof x, ",\"erase_ms\":%d,\"write_ms\":%d", (int)(t1 - t0), (int)(t2 - t1));
#if OTA_SIG_ON
    if (out->checked && k > 0 && k < (int)sizeof x) {
        char t[24];
        ota_sig_text(&out->sig, t, sizeof t);
        snprintf(x + k, sizeof x - k, ",\"sig\":\"%s\"", t);
    }
#else
    (void)k;
#endif
    answer(req, "200 OK", true, m, x);
    static esp_timer_handle_t t;
    const esp_timer_create_args_t a = {.callback = restart_cb, .name = "ota_restart"};
    if (!t && esp_timer_create(&a, &t) != ESP_OK) {
        esp_restart();
    }
    esp_timer_start_once(t, 1500 * 1000);
    return ESP_OK;
}

// ---------------------------------------------------------------- the signed OTA (ota_sig.h)
#if OTA_SIG_ON
// The key list of the signed OTA (tfn_defaults.h, tfn_local.h). Only a new signed firmware changes it.
static const uint8_t s_ota_keys[OTA_SIG_KEYS][OTA_SIG_KEY_LEN] = {
        {TFN_OTA_PUBKEY1_BYTES},
        {TFN_OTA_PUBKEY2_BYTES},
        {TFN_OTA_PUBKEY3_BYTES},
        {TFN_OTA_PUBKEY4_BYTES},
};

static bool sig_sha256(const uint8_t *data, size_t n, uint8_t out[32])
{
    size_t ol = 0;
    return psa_hash_compute(PSA_ALG_SHA_256, data, n, out, 32, &ol) == PSA_SUCCESS && ol == 32;
}

// ECDSA P-256 / SHA-256 with the PSA code of the store signature (tfn_app.c)
static bool sig_verify(const uint8_t pub[OTA_SIG_KEY_LEN], const uint8_t *msg, size_t n, const uint8_t sig[64])
{
    if (psa_crypto_init() != PSA_SUCCESS) {
        return false;
    }
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&at, 256);
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&at, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t k = 0;
    if (psa_import_key(&at, pub, OTA_SIG_KEY_LEN, &k) != PSA_SUCCESS) {
        return false;
    }
    psa_status_t st = psa_verify_message(k, PSA_ALG_ECDSA(PSA_ALG_SHA_256), msg, n, sig, 64);
    psa_destroy_key(k);
    return st == PSA_SUCCESS;
}

static const ota_sig_crypto_t s_crypto = {.sha256 = sig_sha256, .verify = sig_verify};

// The state of the signed OTA. It is in PSRAM: the internal RAM keeps only the pointer.
typedef struct {
    char last[24];  // last_sig: the result of the last update since the start
    bool own_done;  // the check of the own trailer ran (own)
    ota_sig_result_t own;
    bool minsv_read;  // minsv has the value of the NVS
    uint32_t minsv;
    bool ids_done;  // ids has the key IDs of the key list
    uint8_t ids[OTA_SIG_KEYS][OTA_SIG_ID_LEN];
    bool used[OTA_SIG_KEYS];
#if OTA_SIG_ENF
    bool own_end;  // own has the result (own_done is set at the start of the check, own_end at its end)
#endif
} ota_sig_state_t;
static ota_sig_state_t *s_sig;

static ota_sig_state_t *sig_state(void)
{
    if (!s_sig) {
        s_sig = heap_caps_calloc(1, sizeof *s_sig, MALLOC_CAP_SPIRAM);
    }
    return s_sig;
}

// minsv: NVS tfnota/minsv (0 when the board has no value)
static uint32_t minsv_get(void)
{
    ota_sig_state_t *st = sig_state();
    if (st && st->minsv_read) {
        return st->minsv;
    }
    uint32_t v = 0;
    nvs_handle_t h;
    if (nvs_open("tfnota", NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u32(h, "minsv", &v);
        nvs_close(h);
    }
    if (st) {
        st->minsv = v;
        st->minsv_read = true;
    }
    return v;
}

static void minsv_set(uint32_t v)
{
    nvs_handle_t h;
    esp_err_t e = nvs_open("tfnota", NVS_READWRITE, &h);
    if (e == ESP_OK) {
        e = nvs_set_u32(h, "minsv", v);
        if (e == ESP_OK) {
            e = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "the signed OTA: minsv %u is not written (%s)", (unsigned)v, esp_err_to_name(e));
        return;
    }
    ota_sig_state_t *st = sig_state();
    if (st) {
        st->minsv = v;
        st->minsv_read = true;
    }
    ESP_LOGI(TAG, "the signed OTA: minsv is %u now (the security version of the own trailer)", (unsigned)v);
}

// the length of the image in the app slot p (esp_image_get_metadata reads only the segment headers) and the trailer
// directly after it into tr
static bool part_trailer(const esp_partition_t *p, uint32_t *len, uint8_t *tr)
{
    if (!p) {
        return false;
    }
    esp_partition_pos_t pos = {.offset = p->address, .size = p->size};
    esp_image_metadata_t md;
    if (esp_image_get_metadata(&pos, &md) != ESP_OK || md.image_len + OTA_SIG_TRAILER > p->size) {
        return false;
    }
    *len = md.image_len;
    return esp_partition_read(p, md.image_len, tr, OTA_SIG_TRAILER) == ESP_OK;
}

static bool own_trailer(uint32_t *len, uint8_t *tr)
{
    return part_trailer(esp_ota_get_running_partition(), len, tr);
}

// the SHA-256 of the image [0, len) in the app slot p, read in blocks of n bytes into blk
static bool part_hash(const esp_partition_t *p, uint32_t len, uint8_t *blk, size_t n, uint8_t out[32])
{
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    bool ok = psa_crypto_init() == PSA_SUCCESS && psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS;
    for (uint32_t at = 0; ok && at < len; at += n) {
        size_t k = len - at < n ? len - at : n;
        ok = esp_partition_read(p, at, blk, k) == ESP_OK && psa_hash_update(&op, blk, k) == PSA_SUCCESS;
    }
    size_t ol = 0;
    ok = ok && psa_hash_finish(&op, out, 32, &ol) == PSA_SUCCESS && ol == 32;
    psa_hash_abort(&op);
    return ok;
}

// The check of the trailer of the image in the app slot p with the own key list, the own kind and minsv, and the
// version of that image (about 0.5 s: the hash of the image from the flash and one signature). The result goes into
// out. flip = the rollback test (one changed signed byte).
#define OWN_BLOCK 4096
static void part_check(const esp_partition_t *p, ota_sig_result_t *out, bool flip)
{
    memset(out, 0, sizeof *out);
    out->key = -1;
    out->res = OTA_SIG_ERR;
    uint8_t *mem = heap_caps_malloc(OTA_SIG_TRAILER + OWN_BLOCK, MALLOC_CAP_SPIRAM);
    uint32_t len = 0;
    esp_app_desc_t d;
    if (mem && part_trailer(p, &len, mem) && esp_ota_get_partition_description(p, &d) == ESP_OK) {
        if (flip) {
            // (the rollback test: one changed bit in the time field t, a signed byte that no other check reads, so
            // no signature verifies and the self-test fails with sig_bad)
            mem[100] ^= 0x01;
            ESP_LOGW(TAG, "OTA CRASH TEST: one signed byte of the own trailer is changed: the self-test fails");
        }
        uint8_t hash[32];
        bool form = ota_sig_peek(mem, OTA_SIG_TRAILER, len + OTA_SIG_TRAILER, NULL);
        bool hok = form && part_hash(p, len, mem + OTA_SIG_TRAILER, OWN_BLOCK, hash);
        char ver[33];
        snprintf(ver, sizeof ver, "%.32s", d.version);
        ota_sig_want_t w = {
                .keys = s_ota_keys, .nkeys = OTA_SIG_KEYS, .kind = OTA_KIND, .version = ver, .minsv = minsv_get()};
        ota_sig_check(mem, OTA_SIG_TRAILER, len + OTA_SIG_TRAILER, hok ? hash : NULL, &w, &s_crypto, out);
    }
    free(mem);
}

// The check of the own trailer with the own key list. It runs one time after the start, when it is necessary.
static void own_check(ota_sig_state_t *st)
{
    st->own_done = true;
    part_check(esp_ota_get_running_partition(), &st->own, OTA_SIG_ENF && OTA_CRASH_TEST);
    char t[24];
    ota_sig_text(&st->own, t, sizeof t);
    ESP_LOGI(TAG, "the signed OTA: the own trailer: %s (%s), sec_ver %d", t, ota_sig_why(st->own.res),
             st->own.has_sv ? (int)st->own.sec_ver : -1);
#if OTA_SIG_ENF
    __atomic_store_n(&st->own_end, true, __ATOMIC_RELEASE);
#endif
}

// After the mark of a new firmware: minsv = the sec_ver of a good own trailer, when it is higher. The full own check
// runs only for a sec_ver that is higher than minsv (a lower or equal one changes nothing).
static void sig_after_mark(void)
{
    ota_sig_state_t *st = sig_state();
    uint8_t *tr = heap_caps_malloc(OTA_SIG_TRAILER, MALLOC_CAP_SPIRAM);
    uint32_t len = 0, sv = 0;
    bool higher = tr && own_trailer(&len, tr) && ota_sig_peek(tr, OTA_SIG_TRAILER, len + OTA_SIG_TRAILER, &sv) &&
                  sv > minsv_get();
    free(tr);
    if (!higher || !st) {
        return;
    }
    if (!st->own_done) {
        own_check(st);
    }
    uint32_t nv = ota_sig_new_minsv(minsv_get(), &st->own);
    if (nv != minsv_get()) {
        minsv_set(nv);
    }
}

#if OTA_SIG_ENF
// true: the app slot p has an image of TFN OS for this board kind (its board tag; esp_image_get_metadata gives the
// length). The self-test reads it before a rollback: a rollback to an image that is not TFN OS loses the board.
static bool slot_is_tfn(const esp_partition_t *p)
{
    if (!p) {
        return false;
    }
    esp_partition_pos_t pos = {.offset = p->address, .size = p->size};
    esp_image_metadata_t md;
    if (esp_image_get_metadata(&pos, &md) != ESP_OK || md.image_len > p->size) {
        return false;
    }
    char *buf = heap_caps_malloc(OWN_BLOCK + TAG_KEEP, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return false;
    }
    bool own = false;
    char other[16] = "";
    int keep = 0;
    for (uint32_t at = 0; at < md.image_len && !own; at += OWN_BLOCK) {
        int k = md.image_len - at < OWN_BLOCK ? (int)(md.image_len - at) : OWN_BLOCK;
        if (esp_partition_read(p, at, buf + keep, k) != ESP_OK) {
            break;
        }
        int n = keep + k;
        ota_flow_scan_tags(ota_board_tag, buf, n, &own, other);
        keep = n < TAG_KEEP ? n : TAG_KEEP;  // (a tag across two blocks)
        memmove(buf, buf + n - keep, (size_t)keep);
    }
    free(buf);
    return own;
}
#endif

// the JSON fields of the signed OTA (see ota.h)
static int sig_json(char *b, size_t n)
{
    ota_sig_state_t *st = sig_state();
    if (!st) {
        return snprintf(b, n,
                        ",\"sig\":\"%s\",\"keys\":[],\"minsv\":%u,\"own_sv\":-1,\"own_key\":\"\",\"own_sig\":\"\","
                        "\"last_sig\":\"\"",
                        ota_sig_mode_name(OTA_SIG_MODE), (unsigned)minsv_get());
    }
    if (!st->ids_done) {
        st->ids_done = true;
        for (int k = 0; k < OTA_SIG_KEYS; k++) {
            st->used[k] = ota_sig_key_id(&s_crypto, s_ota_keys[k], st->ids[k]);
        }
    }
    if (!st->own_done && !s_busy) {
        own_check(st);  // (one time, at the first answer after the start)
    }
    int k = snprintf(b, n, ",\"sig\":\"%s\",\"keys\":[", ota_sig_mode_name(OTA_SIG_MODE));
    bool first = true;
    for (int i = 0; i < OTA_SIG_KEYS && k > 0 && k < (int)n; i++) {
        if (!st->used[i]) {
            continue;
        }
        char h[17];
        ota_sig_id_hex(st->ids[i], h);
        k += snprintf(b + k, n - k, "%s\"%s\"", first ? "" : ",", h);
        first = false;
    }
    if (k <= 0 || k >= (int)n) {
        return k;
    }
    char own_key[17] = "";
    if (st->own_done && st->own.res == OTA_SIG_OK) {
        ota_sig_id_hex(st->own.key_id, own_key);
    }
    return k + snprintf(b + k, n - k,
                        "],\"minsv\":%u,\"own_sv\":%d,\"own_key\":\"%s\",\"own_sig\":\"%s\",\"last_sig\":\"%s\"",
                        (unsigned)minsv_get(), st->own_done && st->own.has_sv ? (int)st->own.sec_ver : -1, own_key,
                        st->own_done ? ota_sig_code(st->own.res) : "", st->last);
}
#else
static int sig_json(char *b, size_t n)
{
    return snprintf(b, n,
                    ",\"sig\":\"off\",\"keys\":[],\"minsv\":0,\"own_sv\":-1,\"own_key\":\"\",\"own_sig\":\"\","
                    "\"last_sig\":\"\"");
}
#endif

bool ota_slot_sig_ok(const void *part, char *code, size_t n)
{
#if OTA_SIG_ENF
    ota_sig_result_t r;
    part_check((const esp_partition_t *)part, &r, false);
    ota_sig_text(&r, code, n);
    return r.res == OTA_SIG_OK;
#else
    (void)part;
    snprintf(code, n, "%s", ota_sig_mode_name(OTA_SIG_MODE));
    return true;
#endif
}

// ---------------------------------------------------------------- one update over the network (ota_flow.h)
// The work memory of one update (PSRAM): the blocks, the trailer copy and the running hash (the PSA keeps the hash
// context itself in internal RAM)
typedef struct {
    ota_flow_mem_t f;  // (the first member: ota_flow_run gets its address)
    psa_hash_operation_t hop;
} ota_mem_t;

// The state of one POST /api/ota (on the stack of the web task)
typedef struct {
    httpd_req_t *req;
    const esp_partition_t *dst;
    const ota_flow_out_t *out;
    esp_ota_handle_t h;
    ota_mem_t *mem;
    int64_t t0, t1;    // the start of the erase, the start of the writes
    int next_log;      // the next log line of the progress (bytes)
    psa_status_t hst;  // the trial SHA-256 setup before esp_ota_end
    size_t mfree;      // the free flash map before esp_ota_end
} ota_post_t;

static bool po_busy(void *c)
{
    (void)c;
    return s_busy;
}

static bool po_claim(void *c)
{
    (void)c;
    return ota_claim();
}

static void po_release(void *c)
{
    (void)c;
    ota_release();
}

// a trial setup of the hash contexts that the update needs at the same time: the running hash of the signed OTA and
// the hash of the end check
static bool po_trial(void *c)
{
    (void)c;
    psa_hash_operation_t t[2] = {PSA_HASH_OPERATION_INIT, PSA_HASH_OPERATION_INIT};
    psa_status_t hs = PSA_SUCCESS;
    for (int i = 0; i < (OTA_SIG_ON ? 2 : 1) && hs == PSA_SUCCESS; i++) {
        hs = psa_hash_setup(&t[i], PSA_ALG_SHA_256);
    }
    psa_hash_abort(&t[0]);
    psa_hash_abort(&t[1]);
    return hs == PSA_SUCCESS;
}

static void po_wake(void *c)
{
    (void)c;
    app_screen_wake();  // (the screen saver puts the WiFi in power save: slow data)
}

static ota_flow_mem_t *po_alloc(void *c)
{
    ota_post_t *p = c;
    p->mem = heap_caps_malloc(sizeof *p->mem, MALLOC_CAP_SPIRAM);
    return p->mem ? &p->mem->f : NULL;
}

static void po_free(void *c, ota_flow_mem_t *m)
{
    ota_post_t *p = c;
    (void)m;
    free(p->mem);
    p->mem = NULL;
}

static int po_recv(void *c, char *dst, int want)
{
    return recv_block(((ota_post_t *)c)->req, dst, want);
}

static void po_step(void *c, int step, int done, int total)
{
    ota_post_t *p = c;
    if (step == OTA_STEP_ERASE) {
        snprintf(s_new_ver, sizeof s_new_ver, "%s", p->out->ver);
        s_pct = 0;
        s_step = ST_ERASE;
        app_open(&app_menu);  // (a game leaves core 1 to the web server; the board restarts anyway)
        app_screen_wake();
        ESP_LOGI(TAG, "update to %s: %d bytes into %s (0x%lx)", s_new_ver, total, p->dst->label,
                 (unsigned long)p->dst->address);
        p->t0 = tfn_now_ms();
    } else if (step == OTA_STEP_WRITE) {
        p->t1 = tfn_now_ms();
        s_step = ST_WRITE;
        p->next_log = 1 << 19;
    } else {
        s_pct = (int)((int64_t)done * 100 / total);
        if (done >= p->next_log) {
            ESP_LOGI(TAG, "update: %d KB of %d KB", done / 1024, total / 1024);
            p->next_log += 1 << 19;
        }
    }
}

// the size of the image: the slot area is erased first in 64 KB blocks (an erase of each 4 KB sector as the data
// comes took 64 s for 5.7 MB)
static int po_begin(void *c, int total)
{
    ota_post_t *p = c;
    return esp_ota_begin(p->dst, (size_t)total, &p->h);
}

static int po_write(void *c, const char *d, int n)
{
    return esp_ota_write(((ota_post_t *)c)->h, d, n);
}

static void po_abort(void *c)
{
    ota_post_t *p = c;
    if (p->h) {
        esp_ota_abort(p->h);
        p->h = 0;
    }
}

static bool po_hash_start(void *c)
{
    ota_post_t *p = c;
    p->mem->hop = psa_hash_operation_init();
    if (psa_hash_setup(&p->mem->hop, PSA_ALG_SHA_256) == PSA_SUCCESS) {
        return true;
    }
    psa_hash_abort(&p->mem->hop);
    return false;
}

static bool po_hash_update(void *c, const uint8_t *d, int n)
{
    return psa_hash_update(&((ota_post_t *)c)->mem->hop, d, (size_t)n) == PSA_SUCCESS;
}

static bool po_hash_finish(void *c, uint8_t out[32])
{
    ota_post_t *p = c;
    size_t ol = 0;
    bool ok = psa_hash_finish(&p->mem->hop, out, 32, &ol) == PSA_SUCCESS && ol == 32;
    psa_hash_abort(&p->mem->hop);
    return ok;
}

static void po_hash_abort(void *c)
{
    psa_hash_abort(&((ota_post_t *)c)->mem->hop);
}

// the result of the signature check: the log line and last_sig
static void po_on_sig(void *c, const ota_sig_result_t *r, bool accepted)
{
    (void)c;
#if OTA_SIG_ON
    char t[24];
    ota_sig_text(r, t, sizeof t);
    ota_sig_state_t *st = sig_state();
    if (st) {
        snprintf(st->last, sizeof st->last, "%s", t);
    }
    ESP_LOGI(TAG, "update: the signature: %s (%s; sec_ver %d, minsv %u); mode %s: %s", t, ota_sig_why(r->res),
             r->has_sv ? (int)r->sec_ver : -1, (unsigned)minsv_get(), ota_sig_mode_name(OTA_SIG_MODE),
             accepted ? "the update goes on" : "the update stops");
#else
    (void)r;
    (void)accepted;
#endif
}

static int po_end(void *c)
{
    ota_post_t *p = c;
    // (the check of esp_ota_end can fail with no reason on a good image. It maps each segment of
    // the image into the data address space: the free space of that map goes into the log, and after a fault the
    // blocks of the map)
    p->mfree = 0;
    esp_mmu_map_get_max_consecutive_free_block_size(MMU_MEM_CAP_READ | MMU_MEM_CAP_8BIT, MMU_TARGET_FLASH0, &p->mfree);
    // (the check starts a PSA SHA-256 (bootloader_sha256_start: malloc + psa_hash_setup); with no memory it fails
    // with no log line: the free internal RAM, and a trial setup)
    psa_hash_operation_t hop = PSA_HASH_OPERATION_INIT;
    p->hst = psa_hash_setup(&hop, PSA_ALG_SHA_256);
    psa_hash_abort(&hop);
    ESP_LOGI(TAG,
             "update: the check; flash map free %u KB; internal RAM free %u, largest %u, DMA largest %u; a SHA-256 "
             "setup: %d",
             (unsigned)(p->mfree / 1024), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA), (int)p->hst);
    esp_err_t e = esp_ota_end(p->h);
    p->h = 0;
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "update: esp_ota_end %s; the flash map:", esp_err_to_name(e));
        esp_mmu_map_dump_mapped_blocks(stdout);
    }
    return e;
}

static int po_set_boot(void *c)
{
    return esp_ota_set_boot_partition(((ota_post_t *)c)->dst);
}

static const ota_flow_io_t s_post_io = {
        .busy = po_busy,
        .claim = po_claim,
        .release = po_release,
        .trial = po_trial,
        .wake = po_wake,
        .alloc = po_alloc,
        .free_mem = po_free,
        .recv = po_recv,
        .step = po_step,
        .begin = po_begin,
        .write = po_write,
        .abort = po_abort,
        .hash_start = po_hash_start,
        .hash_update = po_hash_update,
        .hash_finish = po_hash_finish,
        .hash_abort = po_hash_abort,
        .on_sig = po_on_sig,
        .end = po_end,
        .set_boot = po_set_boot,
};

// the answer of a fault before the erase (no claim, or the claim is released)
static esp_err_t ota_answer_early(httpd_req_t *req, const esp_partition_t *dst, const ota_flow_out_t *o)
{
    if (o->res == OTA_FLOW_SLOT) {
        return answer(req, "409 Conflict", false,
                      "this board has no second app slot: it needs the OTA flash layout (a USB "
                      "flash, one time)",
                      ",\"code\":\"slot\"");
    }
    if (o->res == OTA_FLOW_BUSY) {
        return answer(req, "409 Conflict", false,
                      "the board still ends an earlier update (at most 60 s after its "
                      "sender stopped). Try again in a minute",
                      ",\"code\":\"busy\"");
    }
    if (o->res == OTA_FLOW_SIZE) {
        char m[200];
        snprintf(m, sizeof m,
                 "the file is %d KB; the app slot %s of this board holds %d KB. This is not a firmware of this "
                 "board kind, or the firmware grew too big for the slot",
                 o->total / 1024, dst->label, (int)(dst->size / 1024));
        return answer(req, "400 Bad Request", false, m, ",\"code\":\"size\"");
    }
    if (o->res == OTA_FLOW_CLAIM) {
        return answer(req, "409 Conflict", false, "another update started", NULL);
    }
    if (o->res == OTA_FLOW_RAM) {
        return ota_ram_fail(req);
    }
    return fail_c(req, "500 Internal Server Error", "mem",
                  "the board has no free PSRAM for a block of 8 KB. "
                  "Restart the board, then try again");
}

#if OTA_SIG_ENF
// the answer of an update that ENFORCE refused: as fail_c with the code "sig", and the field "sig" with the result of
// the check (sig_none, sig_bad ...)
static esp_err_t sig_fail(httpd_req_t *req, const ota_sig_result_t *r, const char *msg)
{
    ESP_LOGW(TAG, "update failed (sig): %s", msg);
    snprintf(s_fail_msg, sizeof s_fail_msg, "%s", msg);
    snprintf(s_fail_code, sizeof s_fail_code, "sig");
    s_step = ST_FAILED;
    s_fail_at = tfn_now_ms();
    char x[48];
    snprintf(x, sizeof x, ",\"code\":\"sig\",\"sig\":\"%s\"", ota_sig_code(r->res));
    return answer(req, "400 Bad Request", false, msg, x);
}
#endif

// the answer of each result of ota_flow_run
static esp_err_t ota_answer(httpd_req_t *req, const ota_post_t *p, const ota_flow_out_t *o)
{
    char m[180];
    switch (o->res) {
    case OTA_FLOW_OK:
        return ota_done(req, p->dst, o, p->t0, p->t1);
    case OTA_FLOW_RECV:
        return recv_fail(req, o->recv, o->done, o->total);
    case OTA_FLOW_IMAGE:
        return fail_c(req, "400 Bad Request", "kind",
                      "this file is not a firmware image (an "
                      "image starts with the byte E9: use tfn_os.bin of a build)");
    case OTA_FLOW_BEGIN:
        snprintf(m, sizeof m, "the erase of the app slot %s failed (%s). Restart the board, then try again",
                 p->dst->label, esp_err_to_name(o->err));
        return fail_c(req, "500 Internal Server Error", "slot", m);
    case OTA_FLOW_WRITE:
        return ota_write_fail(req, o->err, o->done);
    case OTA_FLOW_TAG:
        return ota_tag_fail(req, o->other);
    case OTA_FLOW_SIG:
#if OTA_SIG_ON
        snprintf(m, sizeof m, "not a signed firmware for this board (%s: %s). The old firmware stays",
                 ota_sig_code(o->sig.res), ota_sig_why(o->sig.res));
#if OTA_SIG_ENF
        return sig_fail(req, &o->sig, m);
#endif
#else
        snprintf(m, sizeof m, "not a signed firmware for this board. The old firmware stays");
#endif
        return fail_c(req, "400 Bad Request", "sig", m);
    case OTA_FLOW_END:
        return ota_end_fail(req, o->err, p->hst, p->mfree);
    case OTA_FLOW_BOOT:
        snprintf(m, sizeof m, "the board could not set the new slot %s for the next start (%s). The old firmware stays",
                 p->dst->label, esp_err_to_name(o->err));
        return fail_c(req, "500 Internal Server Error", "boot", m);
    default:
        return ota_answer_early(req, p->dst, o);
    }
}

static esp_err_t api_ota_post(httpd_req_t *req)
{
    const esp_partition_t *dst = ota_next_slot();
    ota_flow_out_t out;
    ota_post_t p = {.req = req, .dst = dst, .out = &out};
    ota_flow_cfg_t cfg = {.tag = ota_board_tag, .slot_size = dst ? (int)dst->size : 0, .mode = OTA_SIG_MODE};
#if OTA_SIG_ON
    cfg.check = ota_sig_check;
    cfg.crypto = &s_crypto;
    cfg.want = (ota_sig_want_t){.keys = s_ota_keys, .nkeys = OTA_SIG_KEYS, .kind = OTA_KIND, .minsv = minsv_get()};
#endif
    ota_flow_run(&s_post_io, &p, &cfg, (int)req->content_len, &out);
    return ota_answer(req, &p, &out);
}

#if OTA_SIG_ENF
static volatile bool s_ota_web;  // the /api/ota handler is registered (the self-test reads it)
#endif

void ota_register(void *h)
{
    httpd_uri_t u = {.uri = "/api/ota", .method = HTTP_POST, .handler = api_ota_post};
    if (httpd_register_uri_handler((httpd_handle_t)h, &u) != ESP_OK) {
        ESP_LOGW(TAG, "the /api/ota handler was not added");
    }
#if OTA_SIG_ENF
    else {
        s_ota_web = true;
    }
#endif
}

bool ota_busy(void)
{
    return s_step != ST_NONE && s_step != ST_FAILED;
}

static bool pending(void)
{
    esp_ota_img_states_t st;
    return esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK &&
           st == ESP_OTA_IMG_PENDING_VERIFY;
}

#if OTA_SIG_ENF
// The state of the self-test in this start ("self" of /api/boards): "" (no self-test: not pending), "wait", "pass",
// "back" (a rollback starts now), "kept" (failed, no rollback: this firmware runs on and is marked good), "kept_pend"
// (failed, no rollback, no /api/ota: it runs on, pending)
static const char *volatile s_self = "";

// A failed self-test. A rollback only when ESP-IDF can go back (another valid app in the otadata) and the other slot
// has a TFN OS image of this kind (ota_sig_back_plan). Else this firmware runs on with no restart: a restart only
// starts the same failed self-test again, or the bootloader starts the other slot (an image that is not TFN OS, or
// none). esp_ota_mark_app_invalid_rollback_and_reboot does not return when it works.
static void self_back(const char *why)
{
    const esp_partition_t *other = esp_ota_get_next_update_partition(NULL);
    bool can = esp_ota_check_rollback_is_possible();
    bool tfn = can && slot_is_tfn(other);
    ota_back_t plan = ota_sig_back_plan(can, tfn, s_ota_web);
    ESP_LOGE(TAG, "the self-test of the new firmware failed: %s. The rollback: %s (ESP-IDF %s, %s %s)", why,
             ota_sig_back_name(plan), can ? "can go back" : "cannot go back", other ? other->label : "no slot",
             !can  ? "not read"
             : tfn ? "has TFN OS"
                   : "has no TFN OS of this kind");
    if (plan == OTA_BACK_ROLLBACK) {
        s_self = "back";
        ESP_LOGE(TAG, "the self-test: back to the old firmware in %s (a rollback)", other->label);
        vTaskDelay(pdMS_TO_TICKS(300));  // (the log line goes out first)
        esp_err_t e = esp_ota_mark_app_invalid_rollback_and_reboot();
        ESP_LOGE(TAG, "the self-test: the rollback call gave %s: no rollback", esp_err_to_name(e));
        plan = ota_sig_back_plan(false, false, s_ota_web);
    }
    if (plan == OTA_BACK_KEEP_MARK) {
        esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
        s_self = "kept";
        ESP_LOGE(TAG,
                 "SELF-TEST FAILED, NO ROLLBACK: this firmware runs on with no restart and is marked good (%s). minsv "
                 "does not change. The update check stays %s: the next update must pass it",
                 esp_err_to_name(e), ota_sig_mode_name(OTA_SIG_MODE));
        return;
    }
    s_self = "kept_pend";
    ESP_LOGE(TAG,
             "SELF-TEST FAILED, NO ROLLBACK, NO /api/ota: this firmware runs on, pending, with no restart. At the next "
             "restart the bootloader starts the other slot or ota_0");
}

// The self-test of a new firmware in ENFORCE (ota_sig_self_test), one step for each frame. Returns true when it is
// done: the mark, no self-test (the firmware is not pending), or a failed self-test (self_back).
static bool self_test_tick(int64_t now)
{
    static int8_t pend = -1;  // the pending state, read one time at OTA_SELF_MARK_MS (-1: not read)
    static bool told_wait;
    if (now < OTA_SELF_MARK_MS) {
        return false;
    }
    if (pend < 0) {
        pend = pending() ? 1 : 0;
    }
    ota_sig_state_t *st = sig_state();
    ota_sig_res_t own = OTA_SIG_ERR;  // (no PSRAM for the state: the self-test fails)
    if (pend && st) {
        if (!st->own_done) {
            own_check(st);
        }
        if (!__atomic_load_n(&st->own_end, __ATOMIC_ACQUIRE)) {
            return false;  // (the web task does the check now)
        }
        own = st->own.res;
    }
    const char *why = "";
    ota_self_t d = ota_sig_self_test(OTA_SIG_ENFORCE, pend == 1, now, own, s_ota_web, &why);
    if (d == OTA_SELF_WAIT) {
        s_self = "wait";
        if (!told_wait) {
            told_wait = true;
            ESP_LOGW(TAG, "the self-test waits: %s", why);
        }
        return false;
    }
    if (d == OTA_SELF_BACK) {
        if (s_busy) {
            return false;  // (an update runs now: a rollback must not stop it; it ends with a restart or a fault)
        }
        self_back(why);
        return true;
    }
    if (d == OTA_SELF_MARK && st) {
        esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
        if (e != ESP_OK) {
            ESP_LOGE(TAG, "the self-test passed, but the mark failed (%s): minsv does not change", esp_err_to_name(e));
            return true;
        }
        s_self = "pass";
        char t[24];
        ota_sig_text(&st->own, t, sizeof t);
        ESP_LOGI(TAG,
                 "the self-test passed (the own trailer %s, /api/ota registered): the new firmware is marked good "
                 "(no rollback now)",
                 t);
        sig_after_mark();  // (minsv: only after the mark)
    }
    return true;
}
#endif

void ota_tick(int64_t now)
{
#if defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION
    // The console may be erasing or writing the inactive slot. Never abort or
    // clear that handle concurrently, or observe a half-initialized context.
    if (s_usb_mutex && xSemaphoreTake(s_usb_mutex, 0) == pdTRUE) {
        if (s_usb.handle && tfn_now_ms() - s_usb.last_ms > 30000) {
            ota_usb_abort_locked();
        }
        xSemaphoreGive(s_usb_mutex);
    }
#endif
    static bool done, told;
    if (!told) {
        told = true;
        const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
        ESP_LOGI(TAG, "firmware %s from %s%s", ota_version(), ota_slot(),
                 pending() ? (OTA_SIG_ENF ? " (new: the self-test and the mark after 20 s)"
                                          : " (new: it marks itself good after 20 s)")
                           : "");
        if (bad) {
            ESP_LOGW(TAG, "the firmware in %s did not start well: the bootloader went back to %s (a rollback)",
                     bad->label, ota_slot());
        }
    }
#if OTA_CRASH_TEST && !OTA_SIG_ENF
    // (a test build only: it stops before it marks itself good, so the bootloader must go back)
    if (now > 8000) {
        ESP_LOGE(TAG, "OTA CRASH TEST: stop now");
        abort();
    }
#endif
#if OTA_SIG_ENF
    if (!done) {
        done = self_test_tick(now);
    }
#else
    if (done || now < 20000) {
        return;
    }
    done = true;
    if (pending()) {
        esp_ota_mark_app_valid_cancel_rollback();
        ESP_LOGI(TAG, "the new firmware runs: marked good (no rollback now)");
#if OTA_SIG_ON
        sig_after_mark();
#endif
    }
#endif
}

void ota_overlay(void)
{
    int st = s_step, p = s_pct;
    if (st == ST_NONE) {
        return;
    }
    if (st == ST_FAILED && tfn_now_ms() - s_fail_at > 8000) {
        s_step = ST_NONE;
        return;
    }
    int u = gfx_native() ? 2 : 1;
    int w = gfx_w * 3 / 4, h = 84 * u, x = (gfx_w - w) / 2, y = (gfx_h - h) / 2;
    gfx_fill_rect(x, y, w, h, THEME_OTA_BG);
    gfx_frame_rect(x, y, w, h, 2, st == ST_FAILED ? THEME_OTA_FAULT : THEME_OTA_EDGE);
    char t[48];
    if (st == ST_FAILED) {
        snprintf(t, sizeof t, "UPDATE FAILED");
    } else {
        snprintf(t, sizeof t, "UPDATE TO %.16s", s_new_ver);
    }
    int sc = gfx_text_width(t, 2 * u) <= w - 12 * u ? 2 * u : u;
    gfx_text_center_in(x, w, y + 10 * u, t, sc, THEME_OTA_TEXT);
    int bw = w - 32 * u, bx = x + 16 * u, by = y + 36 * u;
    gfx_fill_rect(bx, by, bw, 12 * u, THEME_OTA_TRACK);
    int fill = st == ST_WRITE ? p : st == ST_RESTART ? 100 : 0;
    gfx_fill_rect(bx, by, bw * (fill > 100 ? 100 : fill) / 100, 12 * u, THEME_OTA_BAR);
    char s[64];
    if (st == ST_FAILED) {
        // (the code of the reason, in words that fit the panel)
        const char *c = s_fail_code;
        const char *why = !strcmp(c, "net_none") || !strcmp(c, "net_err") || !strcmp(c, "net_closed")
                                  ? "THE NETWORK LINK BROKE"
                          : !strcmp(c, "ram")                                             ? "TOO LITTLE FREE RAM"
                          : !strcmp(c, "size") || !strcmp(c, "kind") || !strcmp(c, "tag") ? "NOT A FILE FOR THIS BOARD"
                          : !strcmp(c, "sig")                                             ? "NOT A SIGNED FIRMWARE"
                          : !strcmp(c, "check")                                           ? "THE FILE IS DAMAGED"
                                                                                          : "A FAULT OF THE BOARD";
        snprintf(s, sizeof s, "%s. THE OLD FIRMWARE STAYS", why);
    } else if (st == ST_RESTART) {
        snprintf(s, sizeof s, "DONE. THE BOARD RESTARTS NOW");
    } else if (st == ST_ERASE) {
        snprintf(s, sizeof s, "PREPARE THE FLASH. DO NOT TURN OFF");
    } else {
        snprintf(s, sizeof s, "WRITE %d %%. DO NOT TURN OFF", p);
    }
    gfx_text_center_in(x, w, y + 58 * u, s, u, THEME_OTA_TEXT_DIM);
}

int ota_json(char *b, size_t n)
{
    const esp_app_desc_t *d = esp_app_get_description();
    char sha[9];
    esp_app_get_elf_sha256(sha, sizeof sha);
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    // "fw": the build (date, time, ELF hash: unique for each build); "pend": the new firmware has not marked itself
    // good yet; "bad": the slot of a firmware that did not start well (the bootloader went back from it)
    // "ota_kb": the size of the slot that an update writes; "ota_fail" / "ota_code": the last update that failed (since
    // the start; the manager reads them when the answer of the board did not come)
    // the signed OTA (sig_json): "sig", "keys", "minsv", "own_sv", "own_key", "own_sig", "last_sig", then "self" (the
    // self-test of this start; see ota.h)
    const esp_partition_t *nx = ota_next_slot();
    int k = 0;
#if CONFIG_TFN_DEVICE_PROTECTION
    int prefix = snprintf(b, n, "\"protected\":true,");
    if (prefix < 0 || (size_t)prefix >= n) {
        return 0;
    }
    b += prefix;
    n -= prefix;
    k = prefix;
#endif
    int m = snprintf(b, n,
                     "\"ver\":\"%s\",\"fw\":\"%s %s %s\",\"slot\":\"%s\",\"ota\":%s,\"kind\":\"%s\",\"bad\":\"%s\","
                     "\"pend\":%s,\"up\":%d,\"ota_kb\":%d,\"ota_code\":\"%s\",\"ota_fail\":\"%s\"",
                     d->version, d->date, d->time, sha, ota_slot(), nx ? "true" : "false", OTA_KIND,
                     bad ? bad->label : "", pending() ? "true" : "false", (int)(esp_timer_get_time() / 1000000),
                     nx ? (int)(nx->size / 1024) : 0, s_fail_code, s_fail_msg);
    if (m < 0 || (size_t)m >= n) {
        return k + m;
    }
    m += sig_json(b + m, n - m);
    if (m < 0 || (size_t)m >= n) {
        return k + m;
    }
#if OTA_SIG_ENF
    const char *self = s_self;
#else
    const char *self = "";
#endif
    return k + m + snprintf(b + m, n - m, ",\"self\":\"%s\"", self);
}

void ota_log_info(void)
{
    const esp_partition_t *boot = esp_ota_get_boot_partition(), *next = ota_next_slot();
    esp_ota_img_states_t st = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(esp_ota_get_running_partition(), &st);
    char sha[9];
    esp_app_get_elf_sha256(sha, sizeof sha);
    ESP_LOGI(TAG, "board %s, firmware %s, running %s (state %d), next boot %s, update slot %s, build %s %s %s",
             OTA_KIND, ota_version(), ota_slot(), (int)st, boot ? boot->label : "?", next ? next->label : "none",
             esp_app_get_description()->date, esp_app_get_description()->time, sha);
    if (next) {
        esp_app_desc_t o;
        if (esp_ota_get_partition_description(next, &o) == ESP_OK) {
            ESP_LOGI(TAG, "the other slot: firmware %s, build %s %s", o.version, o.date, o.time);
        } else {
            ESP_LOGI(TAG, "the other slot: empty");
        }
    }
}
