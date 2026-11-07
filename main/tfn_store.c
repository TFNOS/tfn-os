// TFN OS: the board side of the manager store.
//
// The manager tells each board how to reach it (op=mgr of /api/apps): its address on the LAN and on the VPN, a token
// for this board, and the apps that the profile of the board requires. The first manager sets the token (the board
// has none yet); a later change must give it. The token protects the developer mode and the removal of a required
// app; a signed install needs none (the board checks the signature of each file anyway).
//
// The board pulls: GET <manager>/api/store/catalog?mac=&kind=&abi=&rev= (text lines, see tfn_store_catalog) and
// GET <manager>/api/store/file?id=&kind=&file= (one file of a package). It tries the LAN address first, then the VPN
// address (the VPN must be up). Nothing goes to the internet.
//
// A board with no manager address, or a board with the store source STATIC, uses the static store in place of the
// manager (main/tfn_sstore.h): tfn_store_catalog, tfn_store_install and tfn_store_icon below choose the source.
#include "esp_attr.h"
#include <stdio.h>
#include "arc_mac.h"
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "nvs.h"
#include "tfn_app.h"
#include "tfn_store.h"
#include "tfn_pub.h"
#include "tfn_sstore.h"
#include "vpn.h"
#include "wifi.h"

static const char *TAG = "tfn_store";

static char s_lan[64], s_vpn[64], s_tok[40], s_prof[24];
// the ids of the required apps, with commas (256 bytes hold about 25 ids; a longer list would stop the keeper)
static EXT_RAM_BSS_ATTR char s_req[2048];
static bool s_vpn_only;  // tests (the console "apps vpn 1"): the LAN address is not tried
void tfn_store_vpn_only(bool on)
{
    s_vpn_only = on;
}

static void nv_get(nvs_handle_t h, const char *k, char *out, size_t max)
{
    size_t n = max;
    if (nvs_get_str(h, k, out, &n) != ESP_OK) {
        out[0] = 0;
    }
}

void tfn_store_init(void)
{
    nvs_handle_t h;
    if (nvs_open("tfn", NVS_READONLY, &h) != ESP_OK) {
        return;
    }
    nv_get(h, "lan", s_lan, sizeof s_lan);
    nv_get(h, "vpn", s_vpn, sizeof s_vpn);
    nv_get(h, "tok", s_tok, sizeof s_tok);
    nv_get(h, "req", s_req, sizeof s_req);
    nvs_close(h);
    ESP_LOGI(TAG, "manager %s / %s, token %s, required [%s]", s_lan[0] ? s_lan : "-", s_vpn[0] ? s_vpn : "-",
             s_tok[0] ? "set" : "none", s_req);
}

bool tfn_store_configured(void)
{
    return s_lan[0] || s_vpn[0];
}
bool tfn_store_tok_ok(const char *tok)
{
    return !s_tok[0] || (tok && !strcmp(tok, s_tok));
}
const char *tfn_store_profile(void)
{
    return s_prof;
}

static bool url_ok(const char *u)
{
    if (!u[0]) {
        return true;
    }
    if (strncmp(u, "http://", 7) || strlen(u) >= 63) {
        return false;
    }
    for (const char *p = u; *p; p++) {
        if (*p <= ' ' || *p == '"' || *p == '\\') {
            return false;
        }
    }
    return true;
}

bool tfn_store_set_mgr(const char *lan, const char *vpn, const char *tok, const char *auth, const char *req, char *msg,
                       int len)
{
    if (s_tok[0] && (!auth || strcmp(auth, s_tok))) {
        snprintf(msg, len, "the token is not right");
        return false;
    }
    if (!url_ok(lan) || !url_ok(vpn) || strlen(tok) >= sizeof s_tok) {
        snprintf(msg, len, "bad values (the manager address or the token)");
        return false;
    }
    if (strlen(req) >= sizeof s_req) {
        snprintf(msg, len, "the required list is too long (%d bytes; the board keeps %d)", (int)strlen(req),
                 (int)sizeof s_req - 1);
        return false;
    }
    for (const char *p = req; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == ',')) {
            snprintf(msg, len, "bad required list");
            return false;
        }
    }
    bool same = !strcmp(lan, s_lan) && !strcmp(vpn, s_vpn) && (!tok[0] || !strcmp(tok, s_tok)) && !strcmp(req, s_req);
    snprintf(s_lan, sizeof s_lan, "%s", lan);
    snprintf(s_vpn, sizeof s_vpn, "%s", vpn);
    if (tok[0]) {
        snprintf(s_tok, sizeof s_tok, "%s", tok);
    }
    snprintf(s_req, sizeof s_req, "%s", req);
    if (!same) {
        nvs_handle_t h;
        if (nvs_open("tfn", NVS_READWRITE, &h) == ESP_OK) {
            nvs_set_str(h, "lan", s_lan);
            nvs_set_str(h, "vpn", s_vpn);
            nvs_set_str(h, "tok", s_tok);
            nvs_set_str(h, "req", s_req);
            nvs_commit(h);
            nvs_close(h);
        }
        ESP_LOGI(TAG, "the manager: %s / %s, required [%s]", s_lan, s_vpn, s_req);
    }
    snprintf(msg, len, same ? "no change" : "saved");
    return true;
}

bool tfn_store_required(const char *id)
{
    size_t n = strlen(id);
    for (const char *p = s_req; *p;) {
        const char *e = strchr(p, ',');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        if (l == n && !strncmp(p, id, n)) {
            return true;
        }
        p = e ? e + 1 : p + l;
    }
    return false;
}

int tfn_store_required_json(char *o, int max)
{
    int k = snprintf(o, max, "[");
    bool first = true;
    for (const char *p = s_req; *p && k < max - 30;) {
        const char *e = strchr(p, ',');
        int l = e ? (int)(e - p) : (int)strlen(p);
        if (l > 0) {
            k += snprintf(o + k, max - k, "%s\"%.*s\"", first ? "" : ",", l, p);
        }
        first = false;
        p = e ? e + 1 : p + l;
    }
    k += snprintf(o + k, max - k, "]");
    return k;
}

// ---------------------------------------------------------------- HTTP
// GET a path of the manager: to a buffer (to_file NULL) or to a file. Tries the LAN address, then the VPN address.
typedef struct {
    char *buf;
    int len, max;
    FILE *f;
    int64_t got, total;
    void (*progress)(int);
    int pbase, pspan;
} sink_t;

static esp_err_t on_data(esp_http_client_event_t *ev)
{
    sink_t *s = ev->user_data;
    if (ev->event_id != HTTP_EVENT_ON_DATA || !s) {
        return ESP_OK;
    }
    if (s->f) {
        if (fwrite(ev->data, 1, ev->data_len, s->f) != (size_t)ev->data_len) {
            return ESP_FAIL;
        }
    } else {
        int n = ev->data_len < s->max - 1 - s->len ? ev->data_len : s->max - 1 - s->len;
        if (n > 0) {
            memcpy(s->buf + s->len, ev->data, n);
            s->len += n;
            s->buf[s->len] = 0;
        }
    }
    s->got += ev->data_len;
    if (s->progress && s->total > 0) {
        s->progress(s->pbase + (int)(s->got * s->pspan / s->total));
    }
    return ESP_OK;
}

static int get_from(const char *base, const char *path, sink_t *s, char *msg, int len)
{
    char url[256];
    snprintf(url, sizeof url, "%s%s", base, path);
    esp_http_client_config_t c = {
            .url = url, .event_handler = on_data, .user_data = s, .timeout_ms = 8000, .buffer_size = 2048};
    esp_http_client_handle_t h = esp_http_client_init(&c);
    if (!h) {
        snprintf(msg, len, "no memory");
        return -1;
    }
    esp_err_t e = esp_http_client_perform(h);
    int code = esp_http_client_get_status_code(h);
    s->total = esp_http_client_get_content_length(h);
    esp_http_client_cleanup(h);
    if (e != ESP_OK) {
        snprintf(msg, len, "the manager does not answer (%s)", esp_err_to_name(e));
        return -1;
    }
    if (code != 200) {
        snprintf(msg, len, "the manager answers %d", code);
        return -1;
    }
    return 0;
}

static int mgr_get(const char *path, sink_t *s, char *msg, int len)
{
    if (!tfn_store_configured()) {
        snprintf(msg, len, "THE MANAGER HAS NOT SEEN THIS BOARD YET");
        return -1;
    }
    if (wifi_state() != WIFI_CONNECTED) {
        snprintf(msg, len, "NO WIFI");
        return -1;
    }
    const char *bases[2] = {s_vpn_only ? "" : s_lan, vpn_hub_ip() ? s_vpn : ""};
    for (int i = 0; i < 2; i++) {
        if (!bases[i][0]) {
            continue;
        }
        s->len = 0;
        s->got = 0;
        if (s->f) {
            fflush(s->f);
            fseek(s->f, 0, SEEK_SET);
        }
        if (get_from(bases[i], path, s, msg, len) == 0) {
            return 0;
        }
        ESP_LOGW(TAG, "%s%s: %s", bases[i], path, msg);
    }
    if (!msg[0]) {
        snprintf(msg, len, "THE STORE IS NOT REACHABLE");
    }
    return -1;
}

static void my_mac(char out[13])
{
    uint8_t m[6] = {0};
    arc_mac(m);
    snprintf(out, 13, "%02X%02X%02X%02X%02X%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
}

// the permission policy of the manager (the tab PERMISSIONS): a newer one than the board has comes
// before the catalog (the classes of its apps); "TFNP0" = nothing newer
static void policy_sync(void)
{
    char path[64], msg[120] = "";
    snprintf(path, sizeof path, "/api/store/policy?have=%d&abi=%d", tfn_policy_ver(), tfn_abi());
    sink_t s = {.max = 300000};
    s.buf = heap_caps_malloc(s.max, MALLOC_CAP_SPIRAM);
    if (!s.buf) {
        return;
    }
    s.buf[0] = 0;
    if (mgr_get(path, &s, msg, sizeof msg) == 0 && s.len > 6 && !strncmp(s.buf, "TFNP1\n", 6)) {
        tfn_policy_blob(s.buf, (size_t)s.len, msg, sizeof msg);
        ESP_LOGI(TAG, "policy: %s", msg);
    }
    free(s.buf);
}

// the catalog: line 1 "TFN1<TAB>profile<TAB>count", then for each app:
// id, name, ver, code, group, state (available | recommended | required), bytes, ram, emoji, perms, the description in
// English, the description in Hebrew (TAB between). d=1 asks for the descriptions and for all the apps (a request
// with no d=1 gets no descriptions and 64 apps at most, see the manager store_catalog_text).
static int mgr_catalog(tfn_cat_t *out, int max, char *msg, int len)
{
    char mac[13], path[128];
    my_mac(mac);
    policy_sync();
    snprintf(path, sizeof path, "/api/store/catalog?mac=%s&kind=%s&abi=%d&rev=%d&d=1", mac, tfn_kind(), tfn_abi(),
             tfn_abi_rev());
    sink_t s = {.max = 192 * 1024};  // (about 0.7 KB for an app with its two descriptions)
    s.buf = heap_caps_malloc(s.max, MALLOC_CAP_SPIRAM);
    if (!s.buf) {
        snprintf(msg, len, "no memory");
        return -1;
    }
    s.buf[0] = 0;
    msg[0] = 0;
    if (mgr_get(path, &s, msg, len) < 0) {
        free(s.buf);
        return -1;
    }
    int n = 0;
    char *save = NULL, *line = strtok_r(s.buf, "\n", &save);
    if (!line || strncmp(line, "TFN1\t", 5)) {
        free(s.buf);
        snprintf(msg, len, "the manager sent no catalog");
        return -1;
    }
    char *pp = line + 5, *tab = strchr(pp, '\t');
    snprintf(s_prof, sizeof s_prof, "%.*s", tab ? (int)(tab - pp) : (int)strlen(pp), pp);
    while ((line = strtok_r(NULL, "\n", &save)) && n < max) {
        // id, name, ver, code, group, state, bytes, ram, emoji, perms (the permission classes; an old firmware
        // reads only the first 9 fields), the descriptions (English, Hebrew)
        char *f[12] = {0};
        int k = 0;
        for (char *p = line; k < 12; k++) {
            f[k] = p;
            char *t = strchr(p, '\t');
            if (!t) {
                k++;
                break;
            }
            *t = 0;
            p = t + 1;
        }
        if (k < 8) {
            continue;
        }
        tfn_cat_t *c = &out[n];
        memset(c, 0, sizeof *c);
        snprintf(c->id, sizeof c->id, "%s", f[0]);
        snprintf(c->name, sizeof c->name, "%s", f[1]);
        snprintf(c->ver, sizeof c->ver, "%s", f[2]);
        c->code = (uint32_t)strtoul(f[3], NULL, 10);
        snprintf(c->group, sizeof c->group, "%s", f[4]);
        c->state = !strcmp(f[5], "required")      ? TFN_ST_REQUIRED
                   : !strcmp(f[5], "recommended") ? TFN_ST_RECOMMENDED
                                                  : TFN_ST_AVAILABLE;
        c->bytes = (uint32_t)strtoul(f[6], NULL, 10);
        c->ram = (uint32_t)strtoul(f[7], NULL, 10);
        if (k > 8 && f[8]) {
            snprintf(c->emoji, sizeof c->emoji, "%s", f[8]);
        }
        bool unknown = false;
        if (k > 9 && f[9]) {
            c->perms = tfn_perms_parse(f[9], &unknown);
        }
        if (unknown) {
            continue;  // (a class of a newer firmware: the manager sends none)
        }
        size_t le = k > 10 ? strlen(f[10]) : 0, lh = k > 11 ? strlen(f[11]) : 0;
        if (le + lh) {
            c->desc = heap_caps_malloc(le + lh + 2, MALLOC_CAP_SPIRAM);
            if (c->desc) {
                memcpy(c->desc, k > 10 ? f[10] : "", le + 1);
                memcpy(c->desc + le + 1, k > 11 ? f[11] : "", lh + 1);
            }
        }
        n++;
    }
    if (s.len >= s.max - 1) {
        ESP_LOGW(TAG, "the catalog is longer than %d bytes: the apps after %d are not in it", s.max, n);
    }
    if (n == max) {
        ESP_LOGW(TAG, "the catalog has %d apps or more: the apps after them are not in the list", max);
    }
    free(s.buf);
    return n;
}

// the manifest of the download names a developer key (publisher=): only then the file "sigs" can exist (tfn_pub.h)
static bool tmp_has_publisher(const char *tmp)
{
    char fn[72];
    uint8_t id[TFN_PUB_ID];
    snprintf(fn, sizeof fn, "%s/manifest.txt", tmp);
    FILE *f = fopen(fn, "rb");
    char *b = f ? heap_caps_malloc(4096, MALLOC_CAP_SPIRAM) : NULL;
    size_t n = b ? fread(b, 1, 4096, f) : 0;
    bool yes = b && tfn_pub_manifest_pub(b, n, id);
    if (f) {
        fclose(f);
    }
    free(b);
    return yes;
}

static bool mgr_install(const char *id, int place, void (*progress)(int pct), char *msg, int len)
{
    static const char *const FILES[] = TFN_APP_FILES;
    // (the progress: app.elf is most of it; watch.elf: a watcher; sigs: the developer signatures)
    static const int SHARE[] = {2, 72, 14, 2, 10, 0};
    const char *root = place == TFN_CARD ? TFN_CARD_DIR : TFN_DATA_DIR;
    char tmp[48];
    mkdir(root, 0777);
    snprintf(tmp, sizeof tmp, "%s/" TFN_TMP_STORE "%s", root, id);
    tfn_tmp_drop(tmp);  // (the files of a download that did not end: the folder starts empty)
    mkdir(tmp, 0777);
    int base = 0;
    msg[0] = 0;
    for (int i = 0; i < TFN_APP_NFILES; i++) {
        char path[96], fn[72];
        if (!strcmp(FILES[i], "sigs") && !tmp_has_publisher(tmp)) {
            continue;  // (the store apps of a store key alone: no request for "sigs")
        }
        snprintf(path, sizeof path, "/api/store/file?id=%s&kind=%s&file=%s", id, tfn_kind(), FILES[i]);
        snprintf(fn, sizeof fn, "%s/%s", tmp, FILES[i]);
        sink_t s = {.progress = progress, .pbase = base, .pspan = SHARE[i]};
        s.f = fopen(fn, "wb");
        if (!s.f) {
            snprintf(msg, len, "cannot write %s (full?)", place == TFN_CARD ? "the card" : "the board");
            tfn_tmp_drop(tmp);
            return false;
        }
        int r = mgr_get(path, &s, msg, len);
        fclose(s.f);
        if (r < 0) {
            remove(fn);
            // (no icon, signature, watcher or developer signature: not a must. A manager that does not know the
            // file "sigs" answers 400)
            if (i >= 2 && (strstr(msg, "404") || (i == TFN_APP_NFILES - 1 && strstr(msg, "answers 400")))) {
                msg[0] = 0;
                continue;
            }
            tfn_tmp_drop(tmp);  // (the files of an install that did not end would take the free space)
            return false;
        }
        base += SHARE[i];
        if (progress) {
            progress(base);
        }
    }
    const bool ok = tfn_install(tmp, place, msg, len);
    if (!ok) {
        tfn_tmp_drop(tmp);  // (a download that the install refused takes no space)
    }
    return ok;
}

static uint8_t *mgr_icon(const char *id, size_t *len)
{
    char path[96], msg[96] = "";
    snprintf(path, sizeof path, "/api/store/file?id=%s&kind=%s&file=icon.bin", id, tfn_kind());
    sink_t s = {.max = 96 * 1024};
    s.buf = heap_caps_malloc(s.max, MALLOC_CAP_SPIRAM);
    if (!s.buf) {
        return NULL;
    }
    if (mgr_get(path, &s, msg, sizeof msg) < 0 || !tfn_icon_ok((const uint8_t *)s.buf, (size_t)s.len)) {
        free(s.buf);
        return NULL;
    }
    *len = (size_t)s.len;
    // (the icon in a buffer of its own size: a STORE with 54 apps kept 54 x 96 KB of PSRAM)
    void *fit = heap_caps_realloc(s.buf, (size_t)s.len, MALLOC_CAP_SPIRAM);
    return (uint8_t *)(fit ? fit : s.buf);
}

// ---------------------------------------------------------------- the source of the store
int tfn_store_catalog(tfn_cat_t *out, int max, char *msg, int len)
{
    return tfn_sstore_active() ? tfn_sstore_catalog(out, max, msg, len) : mgr_catalog(out, max, msg, len);
}

bool tfn_store_install(const char *id, int place, void (*progress)(int pct), char *msg, int len)
{
    // (the mark of the id covers the download too: the STORE worker and the console "apps get" use the same folder)
    if (!tfn_inst_begin(id, msg, len)) {
        return false;
    }
    const bool ok = tfn_sstore_active() ? tfn_sstore_install(id, place, progress, msg, len)
                                        : mgr_install(id, place, progress, msg, len);
    tfn_inst_end(id);
    return ok;
}

uint8_t *tfn_store_icon(const char *id, size_t *len)
{
    return tfn_sstore_active() ? tfn_sstore_icon(id, len) : mgr_icon(id, len);
}
