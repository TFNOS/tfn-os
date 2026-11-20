// TFN OS: the board API of the installed apps. Two handlers.
//   GET  /api/apps                               the installed apps and the space (tfn_json)
//   POST /api/apps?op=put&id=<id>&file=<name>    body = one file of the app (manifest.txt, app.elf, icon.bin, sig);
//                                                it goes to /data/apps/.tmp_w-<id>/ (place=card: the card).
//                                                manifest.txt comes first: it starts the upload (the board refuses
//                                                another file before it). ok only when the file is in the folder with
//                                                all its bytes after the close. A put that fails removes the upload
//                                                folder; so does an upload with no put for 2 minutes.
//   POST /api/apps?op=commit&id=<id>&place=board|card    check the files and install them (tfn_install); an install
//                                                that fails removes the upload folder
//   POST /api/apps?op=rm&id=<id>                 remove an installed app
//   POST /api/apps?op=dev&on=0                   the developer mode off (on=1 is refused: ON is a consent at the board,
//                                                SETUP > DEVELOPER MODE with a finger or the USB console "apps dev 1")
//   POST /api/apps?op=move&id=<id>&place=board|card     move an installed app to the other place
//   POST /api/apps?op=scan                       read the folders again (a card in, files copied by hand)
//   POST /api/apps?op=policy                     body = a signed permission policy of the manager (tfn_policy_blob)
//   POST /api/apps?op=mgr&lan=&vpn=&tok=&auth=&req=   the manager: its addresses, the token of this board, the
//                                                required apps (main/tfn_store.c). auth = the token, after the first.
//   The developer mode off (op=dev) and the removal of a required app need tok=<the token> once a token is set.
//   GET  /apps                                   the page (main/web/apps.html)
//   GET  /app/<id>                               the account form of an app (the manifest lines form=, form_help=,
//                                                oauth=); POST /app/<id>: the values go to the secret store of the
//                                                app (a secret field is never shown back); op=logout: they go;
//   GET  /app/<id>/cb?...                        the end of a web login (OAuth): the query goes to its secret "cb"
//   answer {"ok":true|false,"msg":"..."}
//
// The files of the SD card (the manager tab APPS & SIGN). All need tok=<the token> once a token is set.
// path = a path in the card, URL-encoded, with a / at the start ("/" = the root of the card).
//   GET  /api/fs?op=ls&path=/dir           {"ok":true,"parts":1,"path":"/dir","total":kb,"free":kb,"items":[{"n":"x","d":0|1,
//                                          "s":bytes,"t":time},...]} (t: the file time as seconds; the board has no
//                                          time zone, so it is the clock time of the place where the file was written)
//   GET  /api/fs?op=get&path=/dir/file     the bytes of the file
//   POST /api/fs?op=put&path=/dir/file&t=  body = the file; it goes to <file>.part first, then to its name; the
//                                          folders of the path are made; t = the file time (0: no change)
//        &at=<offset>&total=<size>         a part of a big file (a data pack): it goes to the end of <file>.part (at
//                                          must be the size of <file>.part now: else a refusal), and the last part
//                                          (at + body = total) gives the file its name. The answer has "at": the size
//                                          of <file>.part after the request (total after the last part). Between two
//                                          parts the web task answers the other requests (one put of 148 MB held it
//                                          about 11 min); after a break the client goes on at "at". "parts":1 in the
//                                          answer of op=ls: the board takes the parts
//   POST /api/fs?op=rm&path=/x             a file, or a folder with all its content
//   POST /api/fs?op=mkdir&path=/dir        a folder (and the folders above it)
// A change in /apps makes the board read its installed apps again.
#include "web_util.h"
#include "storage.h"
#include "esp_vfs_fat.h"
#include "esp_attr.h"
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include "board.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tfn_app.h"
#include "tfn_pool.h"
#include "tfn_pub.h"
#include "tfn_store.h"
#include "tfn_web.h"
#include "wifi.h"
#include "esp_random.h"

static const char *TAG = "tfn_web";

static void head(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    httpd_resp_set_type(req, "application/json");
}

static bool word_ok(const char *s, int max)
{
    if (!s[0] || (int)strlen(s) > max) {
        return false;
    }
    for (const char *p = s; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.')) {
            return false;
        }
    }
    return s[0] != '.';
}

static esp_err_t api_get(httpd_req_t *req)
{
    // (about 230 bytes for each app: room for TFN_MAX apps; 12 KB was full with 66 apps, and the list lost its end)
    const int max = 4096 + TFN_MAX * 300;
    char *b = heap_caps_malloc(max, MALLOC_CAP_SPIRAM);
    if (!b) {
        return ESP_FAIL;
    }
    int k = tfn_json(b, max);
    head(req);
    httpd_resp_send(req, b, k);
    free(b);
    return ESP_OK;
}

static void answer(httpd_req_t *req, bool ok, const char *msg)
{
    char o[240], m[180];
    int k = 0;
    for (const char *p = msg; *p && k < (int)sizeof m - 1; p++) {
        m[k++] = (*p == '"' || *p == '\\') ? '\'' : *p;
    }
    m[k] = 0;
    snprintf(o, sizeof o, "{\"ok\":%s,\"msg\":\"%s\"}", ok ? "true" : "false", m);
    head(req);
    httpd_resp_sendstr(req, o);
}

// ---------------------------------------------------------------- op=put and op=commit: the upload of a package
// A client sends manifest.txt first, then the other files, then op=commit. The files go into the web upload folder
// <place>/.tmp_w-<id> (TFN_TMP_WEB). Only this code writes in it: the other installers (the STORE, a move, the USB
// console) have their own folders, and the web server runs one request at a time.
// The table keeps the uploads that run now: the id and the place of each manifest.txt that came complete, and the
// time of the last put. Another file goes only into such an upload: a manifest.txt that stays in a folder from the
// time before a restart lets no file in. The commit ends the upload. A put that fails ends it too and removes its
// folder, because each client stops at that fault and sends the package again from manifest.txt. The file "sigs" is
// optional: a client goes on to the commit when the board does not take it, so its fault removes only that file.
// At each put and commit, an upload with no put for UP_IDLE_S seconds ends (its client stopped), and its folder goes.
// With UP_MAX uploads at a time, a new upload ends the upload with the oldest put. The row of an upload that ended so
// stays until the table needs it: the next put or the commit of its client gets the cause.
#define UP_MAX    4    // the uploads that run at the same time
#define UP_ROWS   8    // the rows of the table: the uploads that run and the uploads that ended
#define UP_IDLE_S 120  // an upload with no put for this time ends
enum {
    UP_FREE,  // a free row
    UP_RUN,   // an upload that runs
    UP_IDLE,  // an upload that ended: no put for UP_IDLE_S seconds
    UP_FULL   // an upload that ended: a new upload needed its place (UP_MAX at a time)
};
typedef struct {
    char id[24];
    bool card;
    uint8_t state;
    int64_t t;  // the time (us) of the last put, or of the end
} up_t;
static EXT_RAM_BSS_ATTR up_t s_up[UP_ROWS];

static void up_dir(char *out, int len, const char *id, bool card)
{
    snprintf(out, len, "%s/" TFN_TMP_WEB "%s", card ? TFN_CARD_DIR : TFN_DATA_DIR, id);
}

// the row of id in one place: an upload that runs (run true), or an upload that ended (run false)
static up_t *up_row(const char *id, bool card, bool run)
{
    for (int i = 0; i < UP_ROWS; i++) {
        up_t *u = &s_up[i];
        if (u->state != UP_FREE && (u->state == UP_RUN) == run && u->card == card && !strcmp(u->id, id)) {
            return u;
        }
    }
    return NULL;
}

static up_t *up_find(const char *id, bool card)
{
    return up_row(id, card, true);
}

// the rows of id (uploads that run or ended) go, in one place (card 0 or 1) or in both places (card -1). The folders
// stay: the caller removes them, or gives them to the install.
static void up_end(const char *id, int card)
{
    for (int i = 0; i < UP_ROWS; i++) {
        up_t *u = &s_up[i];
        if (u->state != UP_FREE && !strcmp(u->id, id) && (card < 0 || u->card == card)) {
            u->state = UP_FREE;
        }
    }
}

// an upload that runs ends (why: UP_IDLE or UP_FULL), and its folder goes; the row keeps the cause for its client
static void up_stop(up_t *u, int why, int64_t now)
{
    char dir[48];
    up_dir(dir, sizeof dir, u->id, u->card);
    tfn_tmp_drop(dir);
    u->state = (uint8_t)why;
    u->t = now;
    ESP_LOGW(TAG, "put: the upload of %s ends (%s), and its folder goes", u->id,
             why == UP_IDLE ? "no put for a long time" : "too many uploads at a time");
}

// the uploads with no put for UP_IDLE_S seconds end: their clients stopped, and their files would take the space
static void up_age(int64_t now)
{
    for (int i = 0; i < UP_ROWS; i++) {
        if (s_up[i].state == UP_RUN && now - s_up[i].t > (int64_t)UP_IDLE_S * 1000000) {
            up_stop(&s_up[i], UP_IDLE, now);
        }
    }
}

// the row for a new upload: a free row, else the row of the upload that ended first. With UP_MAX uploads that run,
// the upload with the oldest put ends first.
static up_t *up_new(int64_t now)
{
    up_t *oldest = NULL, *spare = NULL;
    int run = 0;
    for (int i = 0; i < UP_ROWS; i++) {
        if (s_up[i].state == UP_RUN) {
            run++;
            oldest = !oldest || s_up[i].t < oldest->t ? &s_up[i] : oldest;
        }
    }
    if (run >= UP_MAX) {
        up_stop(oldest, UP_FULL, now);
    } else {
        oldest = NULL;
    }
    for (int i = 0; i < UP_ROWS; i++) {
        up_t *u = &s_up[i];
        if (u->state == UP_FREE) {
            return u;
        }
        if (u->state != UP_RUN && u != oldest && (!spare || u->t < spare->t)) {
            spare = u;  // (the row of the upload that just ended keeps its cause)
        }
    }
    return spare ? spare : &s_up[0];
}

// a new upload: its manifest.txt came complete
static void up_add(const char *id, bool card, int64_t now)
{
    up_t *u = up_find(id, card);
    if (!u) {
        up_end(id, card);  // (the row of an older upload of id that ended here)
        u = up_new(now);
        snprintf(u->id, sizeof u->id, "%s", id);
        u->card = card;
        u->state = UP_RUN;
    }
    u->t = now;
}

static volatile bool s_fs_put_on;  // a file comes to the card now (op=put of /api/fs, fs_api)

// The main task calls this while the web server task writes s_up[] and s_fs_put_on, with no lock. A read during a
// write can give a wrong answer for one frame only: the screen saver keeps its hold 5 s after the last true.
bool tfn_web_upload_busy(void)
{
    if (s_fs_put_on) {
        return true;
    }
    const int64_t now = esp_timer_get_time();
    for (int i = 0; i < UP_ROWS; i++) {
        if (s_up[i].state == UP_RUN && now - s_up[i].t <= (int64_t)UP_IDLE_S * 1000000) {
            return true;  // (an upload that its client left counts only until UP_IDLE_S)
        }
    }
    return false;
}

// the cause of the end of the upload of id in one place (its row of an upload that ended); false when there is none.
// file: the file of a put, or NULL for the commit.
static bool up_cause(const char *id, bool card, const char *file, char *msg, int len)
{
    const up_t *u = up_row(id, card, false);
    if (!u) {
        return false;
    }
    char why[48];
    if (u->state == UP_IDLE) {
        snprintf(why, sizeof why, "no put came for %d s", UP_IDLE_S);
    } else {
        snprintf(why, sizeof why, "more than %d uploads at a time", UP_MAX);
    }
    snprintf(msg, len, "%s%sthe upload of %s ended (%s), and the board removed its files: send the package again",
             file ? file : "", file ? ": " : "", id, why);
    return true;
}

// manifest.txt starts an upload: the files of an older upload of id in tmp go, and the upload of id in the other
// place ends with its folder (only this code writes in the two folders)
static void put_begin(const char *id, const char *tmp, bool card)
{
    static const char *const FILES[] = TFN_APP_FILES;
    for (int i = 0; i < TFN_APP_NFILES; i++) {
        char old[72];
        snprintf(old, sizeof old, "%s/%s", tmp, FILES[i]);
        unlink(old);
    }
    if (card || board_sd_mounted()) {
        char other[48];
        up_dir(other, sizeof other, id, !card);
        tfn_tmp_drop(other);
    }
    up_end(id, !card);
}

// a file after manifest.txt: true when the upload of id in this place runs and its manifest.txt is in tmp
static bool put_order_ok(const char *id, const char *file, const char *tmp, bool card, char *msg, int len)
{
    char man[72];
    struct stat st;
    if (!up_find(id, card)) {
        if (!up_cause(id, card, file, msg, len)) {
            snprintf(msg, len, "%s: no manifest.txt came before it in %s: an install sends manifest.txt first", file,
                     tmp);
        }
        return false;
    }
    snprintf(man, sizeof man, "%s/manifest.txt", tmp);
    if (stat(man, &st) != 0) {
        // (only this code writes in tmp: a remove through the files of the card, or a card that went out)
        up_end(id, card);
        tfn_tmp_drop(tmp);
        snprintf(msg, len, "%s: the manifest.txt of this upload is not in %s now: send the package again", file, tmp);
        return false;
    }
    return true;
}

// a put that failed: its upload ends and its folder goes. A fault of "sigs" removes only that file.
static void put_failed(const char *id, const char *file, const char *tmp, bool card)
{
    if (!strcmp(file, "sigs")) {
        char p[72];
        snprintf(p, sizeof p, "%s/sigs", tmp);
        unlink(p);
        return;
    }
    if (up_find(id, card) || !strcmp(file, "manifest.txt")) {
        tfn_tmp_drop(tmp);
    }
    up_end(id, card);
}

// the text of a put that did not end well (the body of want bytes did not come, or the file did not close)
static void put_fault(const char *file, bool on_card, bool opened, size_t got, size_t want, char *msg, int len)
{
    if (!opened) {
        snprintf(msg, len, "%s: the board cannot write %s (errno %d: %s)", file,
                 on_card ? "on the SD card" : "in its storage", errno, strerror(errno));
        return;
    }
    if (got < want) {
        uint64_t tot = 0, fr = 0;
        if (!on_card) {
            esp_vfs_fat_info(STORAGE_ROOT, &tot, &fr);
        }
        if (!on_card && fr < want) {
            snprintf(msg, len, "%s: the board storage is full (%u KB free, the file is %u KB)", file,
                     (unsigned)(fr / 1024), (unsigned)(want / 1024));
        } else {
            snprintf(msg, len, "%s: only %u of %u KB came or were written (the link broke, or the %s is full)", file,
                     (unsigned)(got / 1024), (unsigned)(want / 1024), on_card ? "card" : "storage");
        }
        return;
    }
    snprintf(msg, len, "%s: the file did not close well (the %s is full or damaged)", file,
             on_card ? "card" : "storage");
}

// the file of a put after its close: true when it is at path with all the got bytes
static bool put_kept(const char *file, const char *path, size_t got, char *msg, int len)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        snprintf(msg, len, "%s: the board wrote %u bytes, but the file is not in its folder now (errno %d)", file,
                 (unsigned)got, errno);
        return false;
    }
    if ((size_t)st.st_size != got) {
        snprintf(msg, len, "%s: the board wrote %u bytes, but the file has %ld bytes", file, (unsigned)got,
                 (long)st.st_size);
        return false;
    }
    return true;
}

// the body of a put into the file path: true when all of it came and stays in the file (msg tells the bytes and the
// time, or why not)
static bool put_body(httpd_req_t *req, const char *file, const char *path, bool on_card, char *msg, int len)
{
    FILE *f = fopen(path, "wb");
    char *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    size_t got = 0;
    int64_t t0 = esp_timer_get_time();
    int waits = 0;
    while (f && buf && got < req->content_len) {
        int r = httpd_req_recv(req, buf, 4096);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++waits < 6) {
            continue;  // (about 30 s of no data: stop)
        }
        if (r <= 0 || fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            break;
        }
        got += (size_t)r;
    }
    const bool opened = f != NULL;
    const bool closed = opened && fclose(f) == 0;
    free(buf);
    if (!closed || got != req->content_len) {
        put_fault(file, on_card, opened, got, req->content_len, msg, len);
        return false;
    }
    if (!put_kept(file, path, got, msg, len)) {
        return false;
    }
    snprintf(msg, len, "%s: %u bytes in %lld ms", file, (unsigned)got, (long long)((esp_timer_get_time() - t0) / 1000));
    return true;
}

// op=put: one file of an app into the folder tmp. -1: the request has its answer already (a bad request);
// else 1 when the file is complete, 0 when not (msg tells why). noinline: its buffers stay out of the frame of
// api_post(), which is on the stack of the web task during the install of op=commit.
static __attribute__((noinline)) int apps_put(httpd_req_t *req, const char *id, const char *file, const char *tmp,
                                              bool on_card, char *msg, int len)
{
    static const char *const OK_FILES[] = TFN_APP_FILES;
    bool known = false;
    for (int i = 0; i < TFN_APP_NFILES; i++) {
        known |= !strcmp(file, OK_FILES[i]);
    }
    if (!word_ok(id, 20) || !known) {
        answer(req, false,
               "op=put needs id (a-z 0-9 _) and file = manifest.txt, app.elf, icon.bin, sig, watch.elf or sigs");
        return -1;
    }
    const bool first = !strcmp(file, "manifest.txt");
    bool ok = false;
    up_age(esp_timer_get_time());
    if (req->content_len > (4u << 20)) {
        snprintf(msg, len, "%s is %u KB: an app file holds 4096 KB at most", file, (unsigned)(req->content_len / 1024));
    } else if (on_card && !board_sd_mounted() && !board_sd_mount()) {
        snprintf(msg, len, "no SD card in the board (the install goes to the card)");
    } else if (!first && !put_order_ok(id, file, tmp, on_card, msg, len)) {
        ESP_LOGW(TAG, "put %s: %s", id, msg);
        return 0;  // (no upload of this client in tmp: no file of it to remove)
    } else {
        mkdir(on_card ? TFN_CARD_DIR : TFN_DATA_DIR, 0777);
        mkdir(tmp, 0777);
        if (first) {
            put_begin(id, tmp, on_card);
        }
        char path[72];
        snprintf(path, sizeof path, "%s/%s", tmp, file);
        ok = put_body(req, file, path, on_card, msg, len);
    }
    if (ok && first) {
        up_add(id, on_card, esp_timer_get_time());
    } else if (ok) {
        up_t *u = up_find(id, on_card);
        if (u) {
            u->t = esp_timer_get_time();  // (the time after the body: a big file can take minutes)
        }
    } else {
        ESP_LOGW(TAG, "put %s: %s", id, msg);
        put_failed(id, file, tmp, on_card);
    }
    return ok;
}

// op=commit: install the upload of id (tfn_install) and end it. A card commit with no upload of id on the card takes
// the upload of id in the board storage (a client can send op=put with no place). An install that fails removes the
// folder: the client sends the package again.
static bool apps_commit(const char *id, bool on_card, char *tmp, int tlen, char *msg, int len)
{
    if (!word_ok(id, 20)) {
        snprintf(msg, len, "op=commit needs id");
        return false;
    }
    up_age(esp_timer_get_time());
    const bool from_board = on_card && !up_find(id, true) && up_find(id, false);
    if (from_board) {
        up_dir(tmp, tlen, id, false);
    }
    const bool card = on_card && !from_board;
    if (!up_find(id, card) && (up_cause(id, card, NULL, msg, len) || (card && up_cause(id, false, NULL, msg, len)))) {
        up_end(id, -1);
        return false;  // (its folder went at the end of the upload)
    }
    up_end(id, -1);
    const bool ok = tfn_install(tmp, on_card ? TFN_CARD : TFN_BOARD, msg, len);
    if (!ok) {
        tfn_tmp_drop(tmp);
    }
    return ok;
}

// op=mgr: the addresses of the manager, the token of this board and the required apps (tfn_store_set_mgr)
static bool apps_mgr(httpd_req_t *req, const char *q, char *msg, int len)
{
    // (the pool first: a pool board takes op=mgr only from its pool manager; a public build with no pool never)
    if (!tfn_pool_mgr_ok(req)) {
        snprintf(msg, len, "not the pool manager");
        return false;
    }
    static char lan[72], vpn[72], auth[40], newtok[40];
    static EXT_RAM_BSS_ATTR char rq[2048];
    lan[0] = vpn[0] = auth[0] = newtok[0] = rq[0] = 0;
    httpd_query_key_value(q, "lan", lan, sizeof lan);
    httpd_query_key_value(q, "vpn", vpn, sizeof vpn);
    httpd_query_key_value(q, "auth", auth, sizeof auth);
    httpd_query_key_value(q, "newtok", newtok, sizeof newtok);
    httpd_query_key_value(q, "req", rq, sizeof rq);
    // (a long list comes in the body: the address of a request holds only 512 bytes)
    if (req->content_len > 0 && req->content_len < sizeof rq) {
        size_t got = 0;
        int waits = 0;
        while (got < req->content_len) {
            int r = httpd_req_recv(req, rq + got, req->content_len - got);
            if (r == HTTPD_SOCK_ERR_TIMEOUT && ++waits < 6) {
                continue;
            }
            if (r <= 0) {
                break;
            }
            got += (size_t)r;
        }
        rq[got] = 0;
        for (char *p = rq; *p; p++) {
            if (*p == '\n' || *p == '\r') {
                *p = 0;
            }
        }
    }
    // (the values come URL-encoded: the manager sends plain letters, digits, ':', '/', '.', ',', '_' only)
    for (char *p = lan; *p; p++) {
        if (*p == '%') {
            *p = 0;
        }
    }
    for (char *p = vpn; *p; p++) {
        if (*p == '%') {
            *p = 0;
        }
    }
    return tfn_store_set_mgr(lan, vpn, newtok, auth, rq, msg, len);
}

// op=policy: the body is the signed permission policy of the manager (tfn_policy_blob)
static bool apps_policy(httpd_req_t *req, char *msg, int len)
{
    bool ok = false;
    // the permission policy of the manager: the body ="TFNP1\n<signature>\n<text>"; its signature
    // makes it safe, so it needs no token (tfn_policy_blob keeps only a newer one)
    size_t n = req->content_len;
    char *b = n > 0 && n < 300000 ? heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM) : NULL;
    size_t got = 0;
    int waits = 0;
    while (b && got < n) {
        int r = httpd_req_recv(req, b + got, n - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++waits < 6) {
            continue;
        }
        if (r <= 0) {
            break;
        }
        got += (size_t)r;
    }
    if (!b) {
        snprintf(msg, len, "op=policy needs the policy file (the body, 300 KB at most)");
    } else if (got != n) {
        snprintf(msg, len, "the policy did not come complete");
    } else {
        ok = tfn_policy_blob(b, n, msg, len);
    }
    free(b);
    return ok;
}

static esp_err_t api_post(httpd_req_t *req)
{
    static char q[640];  // (the web server runs one request at a time)
    char op[12] = "", id[24] = "", file[16] = "", place[8] = "", on[4] = "", msg[160] = "", tok[40] = "";
    bool ok = false;
    q[0] = 0;
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "tok", tok, sizeof tok);
    httpd_query_key_value(q, "op", op, sizeof op);
    httpd_query_key_value(q, "id", id, sizeof id);
    httpd_query_key_value(q, "file", file, sizeof file);
    httpd_query_key_value(q, "place", place, sizeof place);
    httpd_query_key_value(q, "on", on, sizeof on);
    char tmp[48];
    // (an install to the card gets its files on the card (place=card also with op=put): a big app file
    // would fill the small board storage. A request with no place for op=put uses the board storage)
    const bool on_card = !strcmp(place, "card");
    up_dir(tmp, sizeof tmp, id, on_card);
    if (!strcmp(op, "put")) {
        int r = apps_put(req, id, file, tmp, on_card, msg, sizeof msg);
        if (r < 0) {
            return ESP_OK;
        }
        ok = r > 0;
    } else if (!strcmp(op, "commit")) {
        ok = apps_commit(id, on_card, tmp, sizeof tmp, msg, sizeof msg);
    } else if (!strcmp(op, "move")) {
        if (!word_ok(id, 20)) {
            snprintf(msg, sizeof msg, "op=move needs id");
        } else {
            ok = tfn_move(id, !strcmp(place, "card") ? TFN_CARD : TFN_BOARD, msg, sizeof msg);
        }
    } else if (!strcmp(op, "rm")) {
        if (!word_ok(id, 20)) {
            snprintf(msg, sizeof msg, "op=rm needs id");
        } else if (tfn_store_required(id) && !tfn_store_tok_ok(tok)) {
            snprintf(msg, sizeof msg, "the manager keeps %s (required)", id);
        } else {
            ok = tfn_remove(id, msg, sizeof msg);
        }
    } else if (!strcmp(op, "dev")) {
        // (ON is a consent: a person at the board, SETUP with a finger or the USB console. With no token the web is
        // open to each device of the LAN, and the first client can set the token: so no ON from the web)
        if (on[0] == '1') {
            snprintf(msg, sizeof msg,
                     "the developer mode turns on only at the board: SETUP > DEVELOPER MODE (a finger on the screen) "
                     "or the USB console (apps dev 1)");
        } else if (!tfn_store_tok_ok(tok)) {
            snprintf(msg, sizeof msg, "the developer mode: the token of the manager is necessary");
        } else {
            tfn_set_dev_mode(false);
            ok = true;
            snprintf(msg, sizeof msg, "the developer mode is %s", tfn_dev_mode() ? "on" : "off");
        }
    } else if (!strcmp(op, "mgr")) {
        ok = apps_mgr(req, q, msg, sizeof msg);
    } else if (!strcmp(op, "scan")) {
        tfn_rescan();
        ok = true;
        snprintf(msg, sizeof msg, "%d installed apps", tfn_count());
    } else if (!strcmp(op, "policy")) {
        ok = apps_policy(req, msg, sizeof msg);
    } else {
        snprintf(msg, sizeof msg, "op = put, commit, move, rm, dev, mgr, scan or policy");
    }
    if (strcmp(op, "put") && strcmp(op, "mgr")) {
        ESP_LOGI(TAG, "%s %s: %s", op, id, msg);
    }
    answer(req, ok, msg);
    return ESP_OK;
}

// the page body: main/web/apps.html, in the firmware (CMake EMBED_TXTFILES: a 0 at the end)
extern const char apps_html_start[] asm("_binary_apps_html_start");
static esp_err_t page_get(httpd_req_t *req)
{
    return web_send_page(req, apps_html_start);
}

// ---------------------------------------------------------------- the account forms of the apps (/app/<id>)
// The manifest: form=key|LABEL[|s];key2|LABEL 2 (s = a secret: a password input, never shown back); form_help= a line
// for the user (its https:// words become links); oauth= the address of a web login, with {cb} (this board's
// /app/<id>/cb), {state} (a new random value, kept as the secret "state") and {<key>} (the value of a field).
#define FORM_MAX 6
typedef struct {
    char key[20], label[48];
    bool secret;
    char opts[80];  // a choice field (|c:A,B,C): its values; "" = a text field
} ffield_t;
typedef struct {
    ffield_t f[FORM_MAX];
    int n;
    char help[720], oauth[400];
    char tmp[840];  // (the help with its return address: PSRAM, not the stack of the web task)
} form_t;

static bool form_read(const tfn_entry_t *e, form_t *F)
{
    memset(F, 0, sizeof *F);
    char path[72];
    snprintf(path, sizeof path, "%s/manifest.txt", e->dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    char *ln = heap_caps_malloc(1024, MALLOC_CAP_SPIRAM);
    if (!ln) {
        fclose(f);
        return false;
    }
    while (fgets(ln, 1024, f)) {
        ln[strcspn(ln, "\r\n")] = 0;
        if (!strncmp(ln, "form_help=", 10)) {
            snprintf(F->help, sizeof F->help, "%s", ln + 10);
        } else if (!strncmp(ln, "oauth=", 6)) {
            snprintf(F->oauth, sizeof F->oauth, "%s", ln + 6);
        } else if (!strncmp(ln, "form=", 5)) {
            for (char *p = ln + 5; *p && F->n < FORM_MAX;) {
                char *end = strchr(p, ';');
                if (end) {
                    *end = 0;
                }
                ffield_t *x = &F->f[F->n];
                char *b1 = strchr(p, '|');
                if (b1) {
                    *b1 = 0;
                    char *b2 = strchr(b1 + 1, '|');
                    if (b2) {
                        *b2 = 0;
                        x->secret = b2[1] == 's';
                        if (b2[1] == 'c' && b2[2] == ':') {
                            snprintf(x->opts, sizeof x->opts, "%s", b2 + 3);
                        }
                    }
                    snprintf(x->key, sizeof x->key, "%s", p);
                    snprintf(x->label, sizeof x->label, "%s", b1 + 1);
                    if (x->key[0]) {
                        F->n++;
                    }
                }
                if (!end) {
                    break;
                }
                p = end + 1;
            }
        }
    }
    fclose(f);
    heap_caps_free(ln);
    return F->n > 0 || F->oauth[0];
}

// a text for HTML (the help line: https:// words become links)
static void send_esc(httpd_req_t *req, const char *s, bool links)
{
    char o[256];
    int k = 0;
    while (*s) {
        if (links && !strncmp(s, "https://", 8)) {
            const char *e = s;
            while (*e && *e != ' ' && *e != ')' && *e != ',') {
                e++;
            }
            if (k) {
                o[k] = 0;
                httpd_resp_sendstr_chunk(req, o);
                k = 0;
            }
            char a[300];
            snprintf(a, sizeof a, "<a style='color:#7fb2ff' target=_blank href='%.*s'>%.*s</a>", (int)(e - s), s,
                     (int)(e - s), s);
            httpd_resp_sendstr_chunk(req, a);
            s = e;
            continue;
        }
        const char *r = *s == '<'    ? "&lt;"
                        : *s == '>'  ? "&gt;"
                        : *s == '&'  ? "&amp;"
                        : *s == '\'' ? "&#39;"
                        : *s == '"'  ? "&quot;"
                                     : NULL;
        if (r) {
            int l = (int)strlen(r);
            memcpy(o + k, r, l);
            k += l;
        } else {
            o[k++] = *s;
        }
        s++;
        if (k > (int)sizeof o - 8) {
            o[k] = 0;
            httpd_resp_sendstr_chunk(req, o);
            k = 0;
        }
    }
    if (k) {
        o[k] = 0;
        httpd_resp_sendstr_chunk(req, o);
    }
}

// the app id of /app/<id>[/cb]; cb: true for the end of a web login
static bool form_id(httpd_req_t *req, char id[24], bool *cb)
{
    const char *u = req->uri + 5;  // (after "/app/")
    int n = 0;
    while (u[n] && u[n] != '/' && u[n] != '?' && n < 23) {
        n++;
    }
    memcpy(id, u, n);
    id[n] = 0;
    *cb = !strncmp(u + n, "/cb", 3);
    return word_ok(id, 20);
}

static esp_err_t form_get(httpd_req_t *req)
{
    char id[24];
    bool cb = false;
    const tfn_entry_t *e = form_id(req, id, &cb) ? tfn_entry_by_id(id) : NULL;
    // the host that the browser used (the return address of a web login): before the answer starts (the web server
    // keeps the head of the request only until then)
    char host[64] = "";
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof host) != ESP_OK) {
        snprintf(host, sizeof host, "%s", wifi_ip());
    }
    httpd_resp_set_type(req, "text/html");
    wifi_send_page_head(req);
    form_t *F = heap_caps_malloc(sizeof *F, MALLOC_CAP_SPIRAM);
    if (!e || !F || !form_read(e, F)) {
        httpd_resp_sendstr_chunk(req, "<p class=i>No app with an account form here.</p>");
        heap_caps_free(F);
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
    }
    char b[700];
    if (cb) {  // the end of a web login: the query to the app
        char q[401] = "";
        httpd_req_get_url_query_str(req, q, sizeof q);
        bool ok = q[0] && tfn_secret_set_for(id, "cb", q);
        snprintf(b, sizeof b, "<h2>%s</h2><p class=i>%s</p>", e->name,
                 ok ? "The login is done. Go back to the board: the app goes on by itself."
                    : "The login did not come back. Try again.");
        httpd_resp_sendstr_chunk(req, b);
        ESP_LOGI(TAG, "%s: a web login came back (%d bytes)", id, (int)strlen(q));
        heap_caps_free(F);
        httpd_resp_sendstr_chunk(req, NULL);
        return ESP_OK;
    }
    snprintf(b, sizeof b, "<h2>%s: the account</h2>", e->name);
    httpd_resp_sendstr_chunk(req, b);
    char st[160];
    if (tfn_secret_get_for(id, "status", st, sizeof st) > 0) {
        httpd_resp_sendstr_chunk(req, "<p class=i>The app says: ");
        send_esc(req, st, false);
        httpd_resp_sendstr_chunk(req, "</p>");
        // a step that runs now ("...": the app works): the page reloads by itself until the next step
        const int sl = (int)strlen(st);
        if (sl > 3 && !strcmp(st + sl - 3, "...")) {
            httpd_resp_sendstr_chunk(
                    req, "<script>setTimeout(function(){location.replace(location.pathname)},3000)</script>");
        }
    }
    if (F->help[0]) {  // ({cb} in the help: the return address of a web login on this board)
        char *hb = F->tmp;
        const int hbn = (int)sizeof F->tmp;
        const char *cb = strstr(F->help, "{cb}");
        if (cb) {
            snprintf(hb, (size_t)hbn, "%.*shttp://%s/app/%s/cb%s", (int)(cb - F->help), F->help, host, id, cb + 4);
        } else {
            snprintf(hb, (size_t)hbn, "%s", F->help);
        }
        httpd_resp_sendstr_chunk(req, "<p class=i>");
        send_esc(req, hb, true);
        httpd_resp_sendstr_chunk(req, "</p>");
    }
    if (F->n) {
        snprintf(b, sizeof b, "<form method=post action='%s'>",
                 id);  // (relative: the page also shows in the frame of the manager)
        httpd_resp_sendstr_chunk(req, b);
        for (int i = 0; i < F->n; i++) {
            const ffield_t *x = &F->f[i];
            char v[160] = "";
            bool set = tfn_secret_get_for(id, x->key, v, sizeof v) > 0;
            if (x->opts[0]) {  // a choice: a drop-down list, the value now selected
                snprintf(b, sizeof b, "<div class=f><span class=n>%s</span><select name='%s' style='flex:2 1 14em'>",
                         x->label, x->key);
                httpd_resp_sendstr_chunk(req, b);
                char o[80];
                snprintf(o, sizeof o, "%s", x->opts);
                for (char *p = o, *c; p && *p; p = c ? c + 1 : NULL) {
                    c = strchr(p, ',');
                    if (c) {
                        *c = 0;
                    }
                    snprintf(b, sizeof b, "<option%s>%s</option>", set && !strcmp(v, p) ? " selected" : "", p);
                    httpd_resp_sendstr_chunk(req, b);
                }
                httpd_resp_sendstr_chunk(req, "</select></div>");
                continue;
            }
            snprintf(b, sizeof b,
                     "<div class=f><span class=n>%s</span><input name='%s' type=%s autocomplete=off style='flex:2 1 "
                     "14em' placeholder='%s' value='",
                     x->label, x->key, x->secret ? "password" : "text", x->secret && set ? "SET: type to change" : "");
            httpd_resp_sendstr_chunk(req, b);
            if (!x->secret && set) {
                send_esc(req, v, false);
            }
            httpd_resp_sendstr_chunk(req, "'></div>");
        }
        httpd_resp_sendstr_chunk(
                req,
                "<div class=f><button class=m>SAVE</button><button name=op value=logout>LOG OUT</button></div></form>");
    }
    if (F->oauth[0]) {  // the web login: the address with its values
        // the state: one random value until a login uses it (a second view of the page must not break a login that runs)
        char state[20], url[600];
        if (tfn_secret_get_for(id, "state", state, sizeof state) != 16) {
            uint32_t r1 = esp_random(), r2 = esp_random();
            snprintf(state, sizeof state, "%08lx%08lx", (unsigned long)r1, (unsigned long)r2);
            tfn_secret_set_for(id, "state", state);
        }
        char cbu[120];  // (the app needs the same return address for the token: cb_uri)
        snprintf(cbu, sizeof cbu, "http://%s/app/%s/cb", host, id);
        tfn_secret_set_for(id, "cb_uri", cbu);
        int k = 0;
        for (const char *p = F->oauth; *p && k < (int)sizeof url - 80;) {
            if (*p == '{') {
                const char *e2 = strchr(p, '}');
                if (e2) {
                    char key[24], v[200] = "";
                    snprintf(key, sizeof key, "%.*s", (int)(e2 - p - 1), p + 1);
                    if (!strcmp(key, "cb")) {
                        snprintf(v, sizeof v, "http%%3A%%2F%%2F%s%%2Fapp%%2F%s%%2Fcb", host, id);
                    } else if (!strcmp(key, "state")) {
                        snprintf(v, sizeof v, "%s", state);
                    } else {
                        tfn_secret_get_for(id, key, v, sizeof v);
                    }
                    k += snprintf(url + k, sizeof url - k, "%s", v);
                    p = e2 + 1;
                    continue;
                }
            }
            url[k++] = *p++;
        }
        url[k] = 0;
        httpd_resp_sendstr_chunk(req, "<div class=f><span class=n>The login of the web service (a new page)</span><a "
                                      "class=b target=_blank href='");
        send_esc(req, url, false);
        httpd_resp_sendstr_chunk(req, "'>LOG IN</a></div>");
    }
    heap_caps_free(F);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t form_post(httpd_req_t *req)
{
    char id[24];
    bool cb = false;
    const tfn_entry_t *e = form_id(req, id, &cb) ? tfn_entry_by_id(id) : NULL;
    form_t *F = heap_caps_malloc(sizeof *F, MALLOC_CAP_SPIRAM);
    char *body = heap_caps_calloc(1, 2049, MALLOC_CAP_SPIRAM);
    int saved = 0;
    bool out = false;
    if (e && F && body && form_read(e, F)) {
        int n = req->content_len < 2048 ? (int)req->content_len : 2048, got = 0;
        while (got < n) {
            int r = httpd_req_recv(req, body + got, n - got);
            if (r <= 0) {
                break;
            }
            got += r;
        }
        char v[420];
        out = httpd_query_key_value(body, "op", v, sizeof v) == ESP_OK && !strcmp(v, "logout");
        for (int i = 0; i < F->n; i++) {
            if (out) {
                tfn_secret_set_for(id, F->f[i].key, NULL);
                continue;
            }
            if (httpd_query_key_value(body, F->f[i].key, v, sizeof v) != ESP_OK) {
                continue;
            }
            web_url_decode(v);
            if (!v[0]) {
                continue;  // (an empty field keeps its value)
            }
            if (tfn_secret_set_for(id, F->f[i].key, v)) {
                saved++;
            }
        }
        if (out) {
            tfn_secret_set_for(id, "cb", NULL);
            tfn_secret_set_for(id, "status", NULL);
            tfn_secret_set_for(id, "logout", "1");  // (the app drops its own tokens)
        }
    }
    heap_caps_free(F);
    heap_caps_free(body);
    ESP_LOGI(TAG, "%s: the account form: %s", id, out ? "log out" : saved ? "saved" : "no change");
    httpd_resp_set_status(req, "303 See Other");
    char loc[48];
    snprintf(loc, sizeof loc, "%s", id);  // (relative: .../app/<id>, also through the proxy of the manager)
    httpd_resp_set_hdr(req, "Location", loc);
    httpd_resp_sendstr(req, "");
    return ESP_OK;
}

// ---------------------------------------------------------------- the files of the SD card (/api/fs)
#ifndef FS_ROOT
#define FS_ROOT "/sdcard"  // (the unit test gives its own folder)
#endif
#define FS_PATH 200

static int hexv(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// the path of the query (URL-encoded) -> FS_ROOT + path; false for a path out of the card
static bool fs_path(const char *q, char *full, int len)
{
    char raw[3 * FS_PATH], p[FS_PATH];
    if (httpd_query_key_value(q, "path", raw, sizeof raw) != ESP_OK) {
        return false;
    }
    int k = 0;
    for (const char *s = raw; *s && k < (int)sizeof p - 1; s++) {
        int c = (unsigned char)*s;
        if (c == '+') {
            c = ' ';
        } else if (c == '%' && hexv(s[1]) >= 0 && hexv(s[2]) >= 0) {
            c = hexv(s[1]) * 16 + hexv(s[2]);
            s += 2;
        }
        if (c < ' ' || c == '\\' || c == 127) {
            return false;
        }
        p[k++] = (char)c;
    }
    p[k] = 0;
    if (p[0] != '/' || strstr(p, "..") || strstr(p, "//")) {
        return false;
    }
    while (k > 1 && p[k - 1] == '/') {
        p[--k] = 0;  // ("/dir/" = "/dir")
    }
    snprintf(full, len, "%s%s", FS_ROOT, k == 1 ? "" : p);
    return true;
}

static bool fs_card(void)
{
    return board_sd_mounted() || board_sd_mount();
}

static void fs_ls(httpd_req_t *req, const char *full)
{
    DIR *d = opendir(full);
    if (!d) {
        answer(req, false, "no such folder");
        return;
    }
    uint32_t tot = 0, fr = 0;
    board_sd_info(&tot, &fr);
    head(req);
    char line[200];
    snprintf(line, sizeof line, "{\"ok\":true,\"parts\":1,\"total\":%lu,\"free\":%lu,\"path\":", (unsigned long)tot,
             (unsigned long)fr);
    httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);
    web_json_str(req, full[sizeof FS_ROOT - 1] ? full + sizeof FS_ROOT - 1 : "/");
    httpd_resp_send_chunk(req, ",\"items\":[", HTTPD_RESP_USE_STRLEN);
    char *path = malloc(FS_PATH + 300);
    struct dirent *de;
    int n = 0;
    while (path && (de = readdir(d)) && n < 1000) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        snprintf(path, FS_PATH + 300, "%s/%s", full, de->d_name);
        struct stat st;
        if (stat(path, &st)) {
            continue;
        }
        httpd_resp_send_chunk(req, n ? ",{\"n\":" : "{\"n\":", HTTPD_RESP_USE_STRLEN);
        web_json_str(req, de->d_name);
        snprintf(line, sizeof line, ",\"d\":%d,\"s\":%lu,\"t\":%lld}", S_ISDIR(st.st_mode) ? 1 : 0,
                 (unsigned long)st.st_size, (long long)st.st_mtime);
        httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);
        n++;
    }
    free(path);
    closedir(d);
    httpd_resp_send_chunk(req, "]}", HTTPD_RESP_USE_STRLEN);
    httpd_resp_send_chunk(req, NULL, 0);
}

static void fs_get(httpd_req_t *req, const char *full)
{
    FILE *f = fopen(full, "rb");
    char *buf = f ? heap_caps_malloc(8192, MALLOC_CAP_SPIRAM) : NULL;
    if (!buf) {
        if (f) {
            fclose(f);
        }
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such file");
        return;
    }
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_type(req, "application/octet-stream");
    size_t r;
    while ((r = fread(buf, 1, 8192, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, r) != ESP_OK) {
            break;
        }
    }
    httpd_resp_send_chunk(req, NULL, 0);
    free(buf);
    fclose(f);
}

// the folders above a path ("/sdcard/a/b/c.txt": /sdcard/a and /sdcard/a/b)
static void fs_mkdirs(const char *full, bool last)
{
    char p[FS_PATH + 16];
    snprintf(p, sizeof p, "%s", full);
    for (char *s = p + sizeof FS_ROOT; *s; s++) {
        if (*s != '/') {
            continue;
        }
        *s = 0;
        mkdir(p, 0777);
        *s = '/';
    }
    if (last) {
        mkdir(p, 0777);
    }
}

// the size of a file (0: no file)
static long long fs_size(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (long long)st.st_size : 0;
}

// the body that a refused request did not read (else the client gets no answer)
static void fs_drain(httpd_req_t *req, char *buf, size_t cap)
{
    size_t got = 0;
    while (buf && got < req->content_len) {
        const int r = httpd_req_recv(req, buf, cap);
        if (r <= 0) {
            break;
        }
        got += (size_t)r;
    }
}

// op=put: the body -> the file full, or a part of it (at=, total=: see the head of the file). *at = the size of
// "<full>.part" after the request (a part), or the size of the file (the last part, a whole file)
static bool fs_put(httpd_req_t *req, const char *full, const char *q, char *msg, int len, long long *at)
{
    char part[FS_PATH + 24], tv[24] = "", av[24] = "", nv[24] = "";
    snprintf(part, sizeof part, "%s.part", full);
    fs_mkdirs(full, false);
    const bool parts = httpd_query_key_value(q, "total", nv, sizeof nv) == ESP_OK;
    httpd_query_key_value(q, "at", av, sizeof av);
    const long long from = parts ? atoll(av) : 0, total = parts ? atoll(nv) : (long long)req->content_len;
    char *buf = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (parts && (from < 0 || from != (from ? fs_size(part) : 0) || from + (long long)req->content_len > total)) {
        fs_drain(req, buf, 8192);
        free(buf);
        *at = fs_size(part);
        snprintf(msg, len, "the part file has %lld bytes: send at=%lld (of %lld)", *at, *at, total);
        return false;
    }
    FILE *f = fopen(part, from ? "ab" : "wb");
    size_t got = 0;
    int64_t t0 = esp_timer_get_time();
    int waits = 0;
    while (f && buf && got < req->content_len) {
        int r = httpd_req_recv(req, buf, 8192);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++waits < 6) {
            continue;  // (about 30 s of no data: stop)
        }
        if (r <= 0 || fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            break;
        }
        got += (size_t)r;
    }
    bool closed = f && fclose(f) == 0;
    free(buf);
    if (!closed || got != req->content_len) {
        if (parts) {
            *at = fs_size(part);  // (the bytes that came stay: the client goes on from here)
        } else {
            unlink(part);
        }
        snprintf(msg, len,
                 f ? "the file did not come complete (%u of %u bytes), or the card is full" : "cannot write the file",
                 (unsigned)got, (unsigned)req->content_len);
        return false;
    }
    if (parts && from + (long long)got < total) {
        *at = from + (long long)got;
        snprintf(msg, len, "a part: %lld of %lld bytes", *at, total);
        return true;
    }
    unlink(full);
    if (rename(part, full)) {
        unlink(part);
        snprintf(msg, len, "cannot give the file its name");
        return false;
    }
    httpd_query_key_value(q, "t", tv, sizeof tv);
    long long t = atoll(tv);
    if (t > 315532800) {  // (after 1980: the first time of FAT)
        struct utimbuf u = {.actime = (time_t)t, .modtime = (time_t)t};
        utime(full, &u);
    }
    long long ms = (esp_timer_get_time() - t0) / 1000;
    *at = total;
    snprintf(msg, len, "%u bytes in %lld ms (%lld KB/s)", (unsigned)got, ms, ms ? (long long)got / ms : 0);
    return true;
}

// a file, or a folder with all its content (at most 8 folders deep)
static bool fs_rm(const char *full, int depth)
{
    struct stat st;
    if (stat(full, &st)) {
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        return unlink(full) == 0;
    }
    if (depth > 8) {
        return false;
    }
    DIR *d = opendir(full);
    char *p = malloc(FS_PATH + 300);
    bool ok = d && p;
    struct dirent *de;
    while (ok && (de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        snprintf(p, FS_PATH + 300, "%s/%s", full, de->d_name);
        ok = fs_rm(p, depth + 1);
    }
    if (d) {
        closedir(d);
    }
    free(p);
    return ok && rmdir(full) == 0;
}

static esp_err_t fs_api(httpd_req_t *req)
{
    static char q[3 * FS_PATH + 160];  // (the web server runs one request at a time)
    char op[8] = "", tok[40] = "", full[FS_PATH + 16], msg[160] = "";
    bool ok = false, post = req->method == HTTP_POST;
    long long at = -1;  // (a put sets it)
    q[0] = 0;
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "op", op, sizeof op);
    httpd_query_key_value(q, "tok", tok, sizeof tok);
    if (!tfn_store_tok_ok(tok)) {
        answer(req, false, "the files of the card: only from the manager (tok)");
        return ESP_OK;
    }
    if (!fs_path(q, full, sizeof full)) {
        answer(req, false, "path: a path in the card, with a / at the start, no ..");
        return ESP_OK;
    }
    if (!fs_card()) {
        answer(req, false, "no SD card in the board");
        return ESP_OK;
    }
    bool root = !full[sizeof FS_ROOT - 1];
    if (!post && !strcmp(op, "ls")) {
        fs_ls(req, full);
        return ESP_OK;
    }
    if (!post && !strcmp(op, "get")) {
        fs_get(req, full);
        return ESP_OK;
    }
    if (post && !strcmp(op, "put") && !root) {
        s_fs_put_on = true;  // (a big file, for example a data pack of the store: the WiFi keeps full power)
        ok = fs_put(req, full, q, msg, sizeof msg, &at);
        s_fs_put_on = false;
    } else if (post && !strcmp(op, "rm") && !root) {
        ok = fs_rm(full, 0);
        snprintf(msg, sizeof msg, ok ? "removed" : "cannot remove all of it");
    } else if (post && !strcmp(op, "mkdir") && !root) {
        fs_mkdirs(full, true);
        struct stat st;
        ok = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        snprintf(msg, sizeof msg, ok ? "the folder is there" : "cannot make the folder");
    } else {
        snprintf(msg, sizeof msg, "op = ls or get (GET); put, rm or mkdir (POST), not on the root");
    }
    if (!strncmp(full, FS_ROOT "/apps", sizeof FS_ROOT + 4) &&
        (!full[sizeof FS_ROOT + 4] || full[sizeof FS_ROOT + 4] == '/')) {
        tfn_rescan();  // (the installed apps of the card changed)
    }
    ESP_LOGI(TAG, "fs %s %s: %s", op, full, msg);
    if (at < 0) {
        answer(req, ok, msg);
        return ESP_OK;
    }
    char o[80];  // (a put: the answer has the size of the file, or of its part file)
    snprintf(o, sizeof o, "{\"ok\":%s,\"at\":%lld,\"msg\":\"", ok ? "true" : "false", at);
    head(req);
    httpd_resp_sendstr_chunk(req, o);
    httpd_resp_sendstr_chunk(req, msg);  // (the messages of fs_put have no quote and no backslash)
    httpd_resp_sendstr_chunk(req, "\"}");
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

void tfn_web_register(void *h)
{
    httpd_uri_t pg = {.uri = "/apps", .method = HTTP_GET, .handler = page_get};
    httpd_register_uri_handler(h, &pg);
    httpd_uri_t g = {.uri = "/api/apps", .method = HTTP_GET, .handler = api_get};
    httpd_uri_t p = {.uri = "/api/apps", .method = HTTP_POST, .handler = api_post};
    httpd_register_uri_handler(h, &g);
    httpd_register_uri_handler(h, &p);
    httpd_uri_t ag = {.uri = "/app/*", .method = HTTP_GET, .handler = form_get};
    httpd_uri_t ap = {.uri = "/app/*", .method = HTTP_POST, .handler = form_post};
    httpd_register_uri_handler(h, &ag);
    httpd_register_uri_handler(h, &ap);
    httpd_uri_t fg = {.uri = "/api/fs", .method = HTTP_GET, .handler = fs_api};
    httpd_uri_t fp = {.uri = "/api/fs", .method = HTTP_POST, .handler = fs_api};
    httpd_register_uri_handler(h, &fg);
    httpd_register_uri_handler(h, &fp);
    tfn_pub_web_register(h);  // (/api/pub: the trusted publisher list, main/tfn_pub.c)
}
