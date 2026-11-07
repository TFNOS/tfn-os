// The unit test of the app upload of the board web API: op=put and op=commit of POST /api/apps (main/tfn_web.c).
// run.py compiles a copy of main/tfn_web.c with the fakes of tests/unit/fake_web/ (the two app folders are d/apps and
// c/apps in the work folder of the test) and this file: the web server, the install, the clock and the board for that
// copy. The upload folder of the web is <place>/.tmp_w-<id> (TFN_TMP_WEB).
// The checks:
//   1. the board refuses a file before manifest.txt, and no folder stays (a client that sends app.elf only);
//   2. a manifest.txt that stays in a folder from another upload lets no file in;
//   3. a put that breaks (also of manifest.txt) removes the upload folder: its space comes back;
//   4. a broken "sigs" removes only that file, and the commit installs the package;
//   5. an install that fails removes the upload folder, and the upload ends;
//   6. a card commit after puts with no place takes the new upload, not an old folder of the card;
//   7. uploads at the same time: two apps, the limit of the table, a manifest.txt that went;
//   8. a normal card upload, and no card;
//   9. the folders of the other installers (the STORE, a move, the USB console) of the same app stay as they are;
//  10. an upload with no put for 2 minutes ends at the next request: its folder goes, its client gets the cause;
//  11. with 4 uploads at a time, a new upload ends the upload with the oldest put (not the newest one).
// Build and run: python tests/unit/run.py (argument 1: the work folder)
#include <dirent.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "board.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_vfs_fat.h"
#include "tfn_app.h"
#include "tfn_pool.h"
#include "tfn_pub.h"
#include "tfn_store.h"
#include "tfn_web.h"
#include "web_util.h"
#include "wifi.h"

static int s_n, s_fail;

static void check(bool ok, const char *what)
{
    s_n++;
    if (!ok) {
        s_fail++;
        printf("FAIL %s\n", what);
    }
}

// ---------------------------------------------------------------- the fake web server
#define URI_MAX 16
static httpd_uri_t s_uris[URI_MAX];
static int s_nuri;

esp_err_t httpd_register_uri_handler(httpd_handle_t handle, const httpd_uri_t *uri_handler)
{
    (void)handle;
    if (s_nuri < URI_MAX) {
        s_uris[s_nuri++] = *uri_handler;
    }
    return ESP_OK;
}

esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{
    snprintf(r->type, sizeof r->type, "%s", type);
    return ESP_OK;
}

esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, httpd_len_t len)
{
    if (!buf) {
        return ESP_OK;
    }
    if (len < 0) {
        len = (httpd_len_t)strlen(buf);
    }
    if (r->n + len >= (httpd_len_t)sizeof r->out) {
        len = (httpd_len_t)sizeof r->out - 1 - r->n;
    }
    memcpy(r->out + r->n, buf, (size_t)len);
    r->n += (int)len;
    r->out[r->n] = 0;
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r, const char *str)
{
    return httpd_resp_send_chunk(r, str, -1);
}

esp_err_t httpd_resp_send(httpd_req_t *r, const char *buf, httpd_len_t len)
{
    return httpd_resp_send_chunk(r, buf, len);
}

esp_err_t httpd_resp_sendstr(httpd_req_t *r, const char *str)
{
    return httpd_resp_send_chunk(r, str, -1);
}

esp_err_t httpd_resp_set_hdr(httpd_req_t *r, const char *field, const char *value)
{
    (void)r;
    (void)field;
    (void)value;
    return ESP_OK;
}

esp_err_t httpd_resp_send_err(httpd_req_t *r, httpd_err_code_t error, const char *msg)
{
    (void)error;
    return httpd_resp_sendstr(r, msg);
}

esp_err_t httpd_resp_set_status(httpd_req_t *r, const char *status)
{
    (void)r;
    (void)status;
    return ESP_OK;
}

esp_err_t httpd_req_get_url_query_str(httpd_req_t *r, char *buf, size_t len)
{
    if (!r->query) {
        return ESP_FAIL;
    }
    snprintf(buf, len, "%s", r->query);
    return ESP_OK;
}

esp_err_t httpd_query_key_value(const char *qry, const char *key, char *val, size_t val_size)
{
    const size_t kl = strlen(key);
    for (const char *p = qry; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1;
            const size_t n = strcspn(v, "&");
            snprintf(val, val_size, "%.*s", (int)n, v);
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *r, const char *field, char *val, size_t val_size)
{
    (void)r;
    (void)field;
    (void)val;
    (void)val_size;
    return ESP_FAIL;
}

int httpd_req_recv(httpd_req_t *r, char *buf, size_t buf_len)
{
    size_t end = r->content_len;
    if (r->body_cut && r->body_cut < end) {
        end = r->body_cut;
    }
    if (r->body_pos >= end) {
        return r->body_pos < r->content_len ? -1 : 0;  // (-1: the link broke)
    }
    size_t n = end - r->body_pos < buf_len ? end - r->body_pos : buf_len;
    memcpy(buf, r->body + r->body_pos, n);
    r->body_pos += n;
    return (int)n;
}

// ---------------------------------------------------------------- the other parts of the board
static bool s_card_in = true;
static uint64_t s_free = 8u << 20;  // the free space of the board storage (esp_vfs_fat_info)

void fake_log(char level, const char *tag, const char *fmt, ...)
{
    if (!getenv("TFN_WEB_LOG")) {
        return;
    }
    va_list ap;
    va_start(ap, fmt);
    printf("   %c %s: ", level, tag);
    vprintf(fmt, ap);
    printf("\n");
    va_end(ap);
}

// the clock of the board (us): each request of the test is one second after the last one (post()); a test step can
// move it more
static int64_t s_now = 1000000;

int64_t esp_timer_get_time(void)
{
    return s_now;
}

uint32_t esp_random(void)
{
    return 4;
}

esp_err_t esp_vfs_fat_info(const char *base_path, uint64_t *out_total_bytes, uint64_t *out_free_bytes)
{
    (void)base_path;
    *out_total_bytes = 9u << 20;
    *out_free_bytes = s_free;
    return ESP_OK;
}

bool board_sd_mounted(void)
{
    return s_card_in;
}

bool board_sd_mount(void)
{
    return s_card_in;
}

bool board_sd_info(uint32_t *total_kb, uint32_t *free_kb)
{
    *total_kb = *free_kb = 1000;
    return s_card_in;
}

const char *wifi_ip(void)
{
    return "";
}

void wifi_send_page_head(struct httpd_req *req)
{
    (void)req;
}

esp_err_t web_send_page(httpd_req_t *req, const char *body)
{
    return httpd_resp_sendstr(req, body);
}

void web_json_str(httpd_req_t *req, const char *s)
{
    httpd_resp_sendstr(req, s);
}

void web_url_decode(char *s)
{
    (void)s;
}

const char test_apps_html[] asm("_binary_apps_html_start") = "<p>apps</p>";

bool tfn_pool_mgr_ok(void *req)
{
    (void)req;
    return true;
}

bool tfn_store_set_mgr(const char *lan, const char *vpn, const char *tok, const char *auth, const char *req, char *msg,
                       int len)
{
    (void)lan;
    (void)vpn;
    (void)tok;
    (void)auth;
    (void)req;
    snprintf(msg, len, "no manager in this test");
    return false;
}

bool tfn_store_tok_ok(const char *tok)
{
    (void)tok;
    return true;
}

bool tfn_store_required(const char *id)
{
    (void)id;
    return false;
}

void tfn_pub_web_register(void *h)
{
    (void)h;
}

int tfn_json(char *o, int max)
{
    return snprintf(o, max, "{}");
}

void tfn_rescan(void)
{
}

int tfn_count(void)
{
    return 0;
}

bool tfn_dev_mode(void)
{
    return false;
}

void tfn_set_dev_mode(bool on)
{
    (void)on;
}

bool tfn_policy_blob(const char *b, size_t n, char *msg, int len)
{
    (void)b;
    (void)n;
    snprintf(msg, len, "no policy in this test");
    return false;
}

int tfn_secret_get_for(const char *id, const char *key, char *out, int max)
{
    (void)id;
    (void)key;
    (void)out;
    (void)max;
    return 0;
}

bool tfn_secret_set_for(const char *id, const char *key, const char *value)
{
    (void)id;
    (void)key;
    (void)value;
    return false;
}

const tfn_entry_t *tfn_entry_by_id(const char *id)
{
    (void)id;
    return NULL;
}

bool tfn_move(const char *id, int place, char *msg, int len)
{
    (void)id;
    (void)place;
    snprintf(msg, len, "no move in this test");
    return false;
}

bool tfn_remove(const char *id, char *msg, int len)
{
    (void)id;
    snprintf(msg, len, "no remove in this test");
    return false;
}

// ---------------------------------------------------------------- the files
static bool exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

static bool write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    fputs(text, f);
    return fclose(f) == 0;
}

static void rm_tree(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) {
        unlink(dir);
        return;
    }
    struct dirent *de;
    char p[300];
    while ((de = readdir(d))) {
        if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
            snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
            rm_tree(p);
        }
    }
    closedir(d);
    rmdir(dir);
}

// the upload folders (.tmp_*) that are in the two app folders now
static int tmp_count(void)
{
    int n = 0;
    const char *roots[2] = {TFN_DATA_DIR, TFN_CARD_DIR};
    for (int r = 0; r < 2; r++) {
        DIR *d = opendir(roots[r]);
        struct dirent *de;
        while (d && (de = readdir(d))) {
            n += !strncmp(de->d_name, ".tmp_", 5);
        }
        if (d) {
            closedir(d);
        }
    }
    return n;
}

// as the real one (main/tfn_install.c): the files of an upload folder, then the folder; never a subfolder
bool tfn_tmp_drop(const char *tmp)
{
    const char *name = strrchr(tmp, '/');
    DIR *d = name && !strncmp(name + 1, ".tmp_", 5) ? opendir(tmp) : NULL;
    if (!d) {
        return false;
    }
    struct dirent *de;
    struct stat st;
    char p[300];
    while ((de = readdir(d))) {
        snprintf(p, sizeof p, "%s/%s", tmp, de->d_name);
        if (!(stat(p, &st) == 0 && S_ISDIR(st.st_mode))) {
            unlink(p);
        }
    }
    closedir(d);
    return rmdir(tmp) == 0;
}

// the bytes of a file ("" when it is not there)
static const char *read_file(const char *path)
{
    static char b[64];
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(b, 1, sizeof b - 1, f) : 0;
    if (f) {
        fclose(f);
    }
    b[n] = 0;
    return b;
}

// the install: the folder of the commit and the version in its manifest.txt; on success the folder goes (the real
// install moves it to the folder of the app), on a refusal it stays (as in the real install)
static struct {
    bool refuse;   // the next install fails ("is open")
    char dir[64];  // the folder of the last install
    char ver[16];  // ver= of its manifest.txt
    int place, n;
} s_inst;

bool tfn_install(const char *tmp, int place, char *msg, int len)
{
    char p[96], line[64];
    snprintf(s_inst.dir, sizeof s_inst.dir, "%s", tmp);
    s_inst.place = place;
    s_inst.ver[0] = 0;
    s_inst.n++;
    snprintf(p, sizeof p, "%s/manifest.txt", tmp);
    FILE *f = fopen(p, "rb");
    while (f && fgets(line, sizeof line, f)) {
        if (!strncmp(line, "ver=", 4)) {
            snprintf(s_inst.ver, sizeof s_inst.ver, "%.*s", (int)strcspn(line + 4, "\r\n"), line + 4);
        }
    }
    if (f) {
        fclose(f);
    }
    snprintf(p, sizeof p, "%s/app.elf", tmp);
    if (!f || !exists(p)) {
        snprintf(msg, len, "no manifest.txt or no app.elf in %s", tmp);
        return false;
    }
    if (s_inst.refuse) {
        snprintf(msg, len, "the app is open: close it first");
        return false;
    }
    rm_tree(tmp);
    snprintf(msg, len, "installed (%s)", place == TFN_CARD ? "card" : "board");
    return true;
}

// ---------------------------------------------------------------- the requests
static httpd_req_t s_req;
static esp_err_t (*s_post)(httpd_req_t *r);

// one POST /api/apps?<query> with the body (cut: the link breaks after so many bytes; 0 = never): the ok of the answer
static bool post(const char *query, const char *body, size_t n, size_t cut)
{
    s_now += 1000000;
    memset(&s_req, 0, sizeof s_req);
    s_req.method = HTTP_POST;
    s_req.uri = "/api/apps";
    s_req.query = query;
    s_req.body = body;
    s_req.content_len = n;
    s_req.body_cut = cut;
    s_post(&s_req);
    return strstr(s_req.out, "\"ok\":true") != NULL;
}

static char s_big[9000];  // the body of an app.elf

// a put of one file: place "" = no place in the request
static bool put(const char *id, const char *file, const char *place, const char *body, size_t cut)
{
    char q[160];
    snprintf(q, sizeof q, "op=put&id=%s&file=%s%s%s", id, file, place[0] ? "&place=" : "", place);
    return post(q, body, body == s_big ? sizeof s_big : strlen(body), cut);
}

static bool commit(const char *id, const char *place)
{
    char q[96];
    snprintf(q, sizeof q, "op=commit&id=%s&place=%s", id, place);
    return post(q, "", 0, 0);
}

static bool answer_has(const char *text)
{
    return strstr(s_req.out, text) != NULL;
}

// ---------------------------------------------------------------- the checks
// the web upload folder of an id: on the board storage (WD) and on the card (WC)
#define WD(id) TFN_DATA_DIR "/" TFN_TMP_WEB id
#define WC(id) TFN_CARD_DIR "/" TFN_TMP_WEB id

static void test_order(void)
{
    check(!put("e1", "app.elf", "", s_big, 0), "1 app.elf before manifest.txt: refused");
    check(answer_has("manifest.txt first"), "1 the answer names manifest.txt");
    check(!exists(WD("e1")), "1 no folder of the refused put");
    check(!put("e1", "app.elf", "card", s_big, 0), "1 app.elf before manifest.txt (card): refused");
    check(!exists(WC("e1")), "1 no folder of the refused put on the card");
    // a manifest.txt of another upload (for example from the time before a restart)
    mkdir(WD("s2"), 0777);
    check(write_file(WD("s2") "/manifest.txt", "id=s2\nver=1\n"), "2 the old manifest.txt is there");
    check(!put("s2", "app.elf", "", s_big, 0), "2 app.elf into an old upload folder: refused");
    check(!exists(WD("s2") "/app.elf"), "2 no app.elf in the old folder");
    rm_tree(WD("s2"));
}

static void test_broken(void)
{
    check(put("b3", "manifest.txt", "", "id=b3\nver=3\n", 0), "3 manifest.txt");
    check(answer_has("manifest.txt: 12 bytes in"), "3 the answer has the byte count");
    s_free = 3000;
    check(!put("b3", "app.elf", "", s_big, 3000), "3 app.elf that breaks: not ok");
    check(answer_has("storage is full"), "3 the answer tells that the storage is full");
    s_free = 8u << 20;
    check(!exists(WD("b3")), "3 the upload folder went (the space came back)");
    check(!put("b3", "icon.bin", "", "icon", 0), "3 a file after the broken put: refused");
    check(!exists(WD("b3")), "3 still no folder");
    check(!put("b3m", "manifest.txt", "card", "id=b3m\nver=3\n", 5), "3 manifest.txt that breaks: not ok");
    check(!exists(WC("b3m")), "3 the folder of the broken manifest.txt went");
    // "sigs" is optional: the client goes on to the commit
    check(put("g4", "manifest.txt", "", "id=g4\nver=4\n", 0), "4 manifest.txt");
    check(put("g4", "app.elf", "", s_big, 0), "4 app.elf");
    check(put("g4", "icon.bin", "", "icon", 0), "4 icon.bin");
    check(!put("g4", "sigs", "", "TFNSIG2 abcdef\n", 4), "4 sigs that breaks: not ok");
    check(!exists(WD("g4") "/sigs"), "4 the broken sigs went");
    check(exists(WD("g4") "/manifest.txt") && exists(WD("g4") "/app.elf") && exists(WD("g4") "/icon.bin"),
          "4 the other files stay");
    check(commit("g4", "board") && !strcmp(s_inst.ver, "4"), "4 the commit installs the package");
}

static void test_commit(void)
{
    check(put("f5", "manifest.txt", "", "id=f5\nver=5\n", 0), "5 manifest.txt");
    check(put("f5", "app.elf", "", s_big, 0), "5 app.elf");
    s_inst.refuse = true;
    check(!commit("f5", "board"), "5 the install fails");
    s_inst.refuse = false;
    check(answer_has("close it first"), "5 the answer of the install");
    check(!exists(WD("f5")), "5 the upload folder of the failed install went");
    check(!put("f5", "app.elf", "", s_big, 0), "5 a file after the commit: refused (the upload ended)");
    // an old upload folder on the card (an install that failed there), then a new upload with no place
    mkdir(WC("c6"), 0777);
    check(write_file(WC("c6") "/manifest.txt", "id=c6\nver=1\n") && write_file(WC("c6") "/app.elf", "old"),
          "6 the old folder of the card is there");
    check(put("c6", "manifest.txt", "", "id=c6\nver=2\n", 0), "6 manifest.txt with no place");
    check(!exists(WC("c6")), "6 the old folder of the card went");
    check(put("c6", "app.elf", "", s_big, 0), "6 app.elf with no place");
    check(commit("c6", "card"), "6 the card commit");
    check(!strcmp(s_inst.dir, WD("c6")) && !strcmp(s_inst.ver, "2") && s_inst.place == TFN_CARD,
          "6 the commit took the new upload (version 2), to the card");
}

static void test_parallel(void)
{
    check(put("pa", "manifest.txt", "", "id=pa\nver=7\n", 0), "7 manifest.txt of A");
    check(put("pb", "manifest.txt", "card", "id=pb\nver=8\n", 0), "7 manifest.txt of B");
    check(put("pa", "app.elf", "", s_big, 0), "7 app.elf of A");
    check(put("pb", "app.elf", "card", s_big, 0), "7 app.elf of B");
    check(commit("pa", "board") && !strcmp(s_inst.ver, "7"), "7 the commit of A");
    check(commit("pb", "card") && !strcmp(s_inst.dir, WC("pb")) && !strcmp(s_inst.ver, "8"), "7 the commit of B");
    // more uploads at a time than the table holds: the first one ends, and its folder goes
    const char *ids[5] = {"u1", "u2", "u3", "u4", "u5"};
    char man[32];
    for (int i = 0; i < 5; i++) {
        snprintf(man, sizeof man, "id=%s\nver=9\n", ids[i]);
        check(put(ids[i], "manifest.txt", "", man, 0), "7 manifest.txt of 5 uploads");
    }
    check(!exists(WD("u1")) && !put("u1", "app.elf", "", s_big, 0), "7 the first of 5 uploads ended");
    for (int i = 1; i < 5; i++) {
        check(put(ids[i], "app.elf", "", s_big, 0) && commit(ids[i], "board"), "7 the other 4 uploads install");
    }
    // the manifest.txt of an upload went (a remove through the files of the card, a card that went out)
    check(put("m8", "manifest.txt", "", "id=m8\nver=1\n", 0), "7 manifest.txt of m8");
    rm_tree(WD("m8"));
    mkdir(WD("m8"), 0777);
    check(!put("m8", "app.elf", "", s_big, 0), "7 app.elf after the manifest.txt went: refused");
    check(answer_has("not in") && answer_has("send the package again"), "7 the answer tells what happened");
    check(!exists(WD("m8")), "7 the folder of that upload went");
}

static void test_card(void)
{
    check(put("k9", "manifest.txt", "card", "id=k9\nver=9\n", 0), "8 manifest.txt to the card");
    check(put("k9", "app.elf", "card", s_big, 0), "8 app.elf to the card");
    check(answer_has("app.elf: 9000 bytes in"), "8 the answer has the byte count");
    check(commit("k9", "card") && !strcmp(s_inst.dir, WC("k9")), "8 the card commit");
    s_card_in = false;
    check(!put("k10", "manifest.txt", "card", "id=k10\nver=1\n", 0), "8 no card: refused");
    check(answer_has("no SD card"), "8 the answer tells that no card is in");
    s_card_in = true;
}

// a folder of another installer with a manifest.txt and an app.elf (the text tells whose)
static bool other_folder(const char *dir, const char *who)
{
    char p[96];
    mkdir(dir, 0777);
    snprintf(p, sizeof p, "%s/manifest.txt", dir);
    bool ok = write_file(p, "id=x9\nver=0\n");
    snprintf(p, sizeof p, "%s/app.elf", dir);
    return ok && write_file(p, who);
}

static bool other_same(const char *dir, const char *who)
{
    char p[96];
    snprintf(p, sizeof p, "%s/app.elf", dir);
    return !strcmp(read_file(p), who);
}

static void test_others(void)
{
    // a download of the STORE on the card and on the board, the copy of a move, a USB upload: all of the same app
    const char *dirs[4] = {TFN_CARD_DIR "/" TFN_TMP_STORE "x9", TFN_DATA_DIR "/" TFN_TMP_STORE "x9",
                           TFN_CARD_DIR "/" TFN_TMP_MOVE "x9", TFN_DATA_DIR "/" TFN_TMP_USB "x9"};
    const char *who[4] = {"store card", "store board", "move", "usb"};
    for (int i = 0; i < 4; i++) {
        check(other_folder(dirs[i], who[i]), "9 the folder of another installer is there");
    }
    check(put("x9", "manifest.txt", "", "id=x9\nver=3\n", 0) && put("x9", "app.elf", "", s_big, 0),
          "9 the web upload of the same app with no place");
    check(commit("x9", "card") && !strcmp(s_inst.dir, WD("x9")) && !strcmp(s_inst.ver, "3"),
          "9 the card commit took the web upload");
    check(put("x9", "manifest.txt", "card", "id=x9\nver=4\n", 0) && put("x9", "app.elf", "card", s_big, 0) &&
                  commit("x9", "card") && !strcmp(s_inst.dir, WC("x9")),
          "9 a web upload to the card");
    s_inst.refuse = true;
    check(put("x9", "manifest.txt", "", "id=x9\nver=5\n", 0) && put("x9", "app.elf", "", s_big, 0) &&
                  !commit("x9", "board") && !exists(WD("x9")),
          "9 a web install that fails: its folder went");
    s_inst.refuse = false;
    for (int i = 0; i < 4; i++) {
        check(other_same(dirs[i], who[i]), "9 the folders of the other installers stay as they were");
        rm_tree(dirs[i]);
    }
}

static void test_idle(void)
{
    check(put("i10", "manifest.txt", "", "id=i10\nver=1\n", 0), "10 manifest.txt of an upload that stops");
    s_now += 100 * 1000000LL;
    check(put("j10", "manifest.txt", "card", "id=j10\nver=2\n", 0), "10 manifest.txt of another upload, 100 s after");
    check(exists(WD("i10")), "10 after 100 s the upload still runs");
    s_now += 30 * 1000000LL;
    check(put("j10", "app.elf", "card", s_big, 0), "10 app.elf of the other upload, 130 s after the first one");
    check(!exists(WD("i10")), "10 the upload with no put for 2 minutes ended: its folder went");
    check(!put("i10", "app.elf", "", s_big, 0), "10 a put of the upload that ended: refused");
    check(answer_has("ended (no put came for 120 s)") && answer_has("send the package again"),
          "10 the answer tells the cause");
    check(!exists(WD("i10")), "10 the refused put made no folder");
    const int n = s_inst.n;
    check(!commit("i10", "board") && answer_has("ended (no put came for 120 s)") && s_inst.n == n,
          "10 the commit of the upload that ended: the cause, and no install");
    check(commit("j10", "card") && !strcmp(s_inst.ver, "2"), "10 the other upload installs");
    // an upload that goes on (a put each 100 s) does not end
    check(put("k11", "manifest.txt", "", "id=k11\nver=3\n", 0), "10 manifest.txt of a slow upload");
    s_now += 100 * 1000000LL;
    check(put("k11", "app.elf", "", s_big, 0), "10 app.elf 100 s after");
    s_now += 100 * 1000000LL;
    check(put("k11", "icon.bin", "", "icon", 0), "10 icon.bin 100 s after");
    s_now += 100 * 1000000LL;
    check(commit("k11", "board") && !strcmp(s_inst.ver, "3"), "10 the slow upload installs");
}

static void test_oldest(void)
{
    const char *ids[4] = {"a12", "b12", "c12", "d12"};
    char man[32];
    for (int i = 0; i < 4; i++) {
        snprintf(man, sizeof man, "id=%s\nver=%d\n", ids[i], i + 1);
        check(put(ids[i], "manifest.txt", "", man, 0), "11 manifest.txt of 4 uploads");
    }
    check(put("a12", "app.elf", "", s_big, 0), "11 app.elf of the first upload (now b12 has the oldest put)");
    check(put("e12", "manifest.txt", "", "id=e12\nver=5\n", 0), "11 manifest.txt of a fifth upload");
    check(exists(WD("a12")) && !exists(WD("b12")), "11 the upload with the oldest put ended, not the first one");
    check(!commit("b12", "board") && answer_has("ended (more than 4 uploads at a time)"),
          "11 the commit of the upload that ended tells the cause");
    check(commit("a12", "board") && !strcmp(s_inst.ver, "1"), "11 the first upload installs");
    const char *rest[3] = {"c12", "d12", "e12"};
    for (int i = 0; i < 3; i++) {
        check(put(rest[i], "app.elf", "", s_big, 0) && commit(rest[i], "board"), "11 the other uploads install");
    }
    check(!put("b12", "app.elf", "", s_big, 0) && answer_has("no manifest.txt came before it"),
          "11 after its commit, the upload that ended has no row: the normal text");
}

int main(int argc, char **argv)
{
    const char *work = argc > 1 ? argv[1] : "tfn_web_work";
    mkdir(work, 0777);
    if (chdir(work) != 0) {
        printf("FAIL no work folder %s\n", work);
        return 1;
    }
    rm_tree("d");
    rm_tree("c");
    mkdir("d", 0777);
    mkdir("c", 0777);
    mkdir(TFN_DATA_DIR, 0777);
    mkdir(TFN_CARD_DIR, 0777);
    for (size_t i = 0; i < sizeof s_big; i++) {
        s_big[i] = (char)('A' + i % 26);
    }
    tfn_web_register(NULL);
    for (int i = 0; i < s_nuri; i++) {
        if (!strcmp(s_uris[i].uri, "/api/apps") && s_uris[i].method == HTTP_POST) {
            s_post = s_uris[i].handler;
        }
    }
    if (!s_post) {
        printf("FAIL no handler of POST /api/apps\n");
        return 1;
    }
    test_order();
    test_broken();
    test_commit();
    test_parallel();
    test_card();
    test_others();
    test_idle();
    test_oldest();
    check(tmp_count() == 0, "no upload folder stays at the end");
    printf("TFN WEB %s (%d checks, %d failed)\n", s_fail ? "FAILED" : "OK", s_n, s_fail);
    return s_fail ? 1 : 0;
}
