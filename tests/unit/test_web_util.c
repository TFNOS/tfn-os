// The unit test of main/web_util.c: each function against a copy of the code that it replaced, and the rule of the
// end of an answer: an empty chunk ends a chunked answer of ESP-IDF, so web_chunk() and web_send_page() never send
// one before the end. Build and run: python tests/unit/run.py
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "web_util.h"
#include "wifi.h"

// ---------------------------------------------------------------- the fake web server
esp_err_t httpd_resp_set_type(httpd_req_t *r, const char *type)
{
    snprintf(r->type, sizeof r->type, "%s", type);
    return ESP_OK;
}

// (as ESP-IDF: an empty chunk is the end of the answer; a chunk after the end is a fault of the code)
esp_err_t httpd_resp_send_chunk(httpd_req_t *r, const char *buf, httpd_len_t len)
{
    if (buf && len < 0) {
        len = (httpd_len_t)strlen(buf);
    }
    if (r->ended) {
        r->after++;
        return ESP_OK;
    }
    if (!buf || len <= 0) {
        r->ended = 1;
        return ESP_OK;
    }
    memcpy(r->out + r->n, buf, (size_t)len);
    r->n += (int)len;
    r->out[r->n] = 0;
    return ESP_OK;
}

esp_err_t httpd_resp_sendstr_chunk(httpd_req_t *r, const char *str)
{
    return httpd_resp_send_chunk(r, str, str ? -1 : 0);
}

void wifi_send_page_head(struct httpd_req *req)
{
    httpd_resp_sendstr_chunk(req, "<head>");
}

// ---------------------------------------------------------------- the old code (the reference)
static void old_json_str(httpd_req_t *req, const char *s, int size)
{
    char o[160];
    int k = 0;
    o[k++] = '"';
    for (; *s; s++) {
        if (k > size - 8) {
            httpd_resp_send_chunk(req, o, k);
            k = 0;
        }
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            o[k++] = '\\';
            o[k++] = (char)c;
        } else if (c < ' ') {
            k += snprintf(o + k, 8, "\\u%04x", c);
        } else {
            o[k++] = (char)c;
        }
    }
    o[k++] = '"';
    httpd_resp_send_chunk(req, o, k);
}

static void old_url_dec(char *s)
{
    char *o = s;
    for (; *s; s++) {
        if (*s == '+') {
            *o++ = ' ';
        } else if (*s == '%' && s[1] && s[2]) {
            char h[3] = {s[1], s[2], 0};
            *o++ = (char)strtol(h, NULL, 16);
            s += 2;
        } else {
            *o++ = *s;
        }
    }
    *o = 0;
}

static esp_err_t old_page(httpd_req_t *req, const char *body)
{
    httpd_resp_set_type(req, "text/html");
    wifi_send_page_head(req);
    httpd_resp_sendstr_chunk(req, body);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

// ---------------------------------------------------------------- the checks
static int s_fails;
static httpd_req_t s_a, s_b;

static void reset(void)
{
    memset(&s_a, 0, sizeof s_a);
    memset(&s_b, 0, sizeof s_b);
}

static void same(const char *what, const char *in)
{
    if (s_a.n != s_b.n || memcmp(s_a.out, s_b.out, (size_t)s_a.n) || strcmp(s_a.type, s_b.type) ||
        s_a.ended != s_b.ended || s_a.after != s_b.after) {
        s_fails++;
        if (s_fails < 10) {
            printf("FAIL %s: input %.40s\n", what, in);
        }
    }
}

static void check_json(const char *in)
{
    // the old writers had two chunk sizes (web_files.c 160, tfn_web.c 136): the whole answer must be the same
    int sizes[2] = {160, 2 * 64 + 8};
    for (int k = 0; k < 2; k++) {
        reset();
        old_json_str(&s_a, in, sizes[k]);
        web_json_str(&s_b, in);
        same("web_json_str", in);
    }
}

static void check_url(const char *in)
{
    char a[700], b[700];
    snprintf(a, sizeof a, "%s", in);
    snprintf(b, sizeof b, "%s", in);
    old_url_dec(a);
    web_url_decode(b);
    if (strcmp(a, b)) {
        s_fails++;
        printf("FAIL web_url_decode: input %.40s\n", in);
    }
}

// the answer of s_b is text, it ended one time, and no chunk came after the end
static void one_end(const char *what, const char *text)
{
    if (strcmp(s_b.out, text) || s_b.ended != 1 || s_b.after) {
        s_fails++;
        printf("FAIL %s: \"%s\", ended %d, %d chunks after the end\n", what, s_b.out, s_b.ended, s_b.after);
    }
}

static void random_text(char *t, int n, unsigned *seed)
{
    static const char pick[] = "a%+\"\\\x01\x1f 09AFfz\xc3\xa9\xd7\x90";
    for (int k = 0; k < n; k++) {
        *seed = *seed * 1103515245u + 12345u;
        t[k] = pick[(*seed >> 16) % (sizeof pick - 1)];
    }
    t[n] = 0;
}

// web_data_sys_path: the files and the folders of the system in the board storage, in each form that FAT opens as
// the same file, are refused; the files of the user are not
static void check_sys_paths(void)
{
    static const char *const refused[] = {"/store2.bin",
                                          "/STORE2.BIN",
                                          "/Store2.Bin",
                                          "/store2.bin.",
                                          "/store2.bin. . ",
                                          "/store2.bin.part",
                                          "/tfn_sigok.bin",
                                          "/TFN_SI~1.BIN",
                                          "/tfn_policy.txt",
                                          "/tfn_policy.sig",
                                          "/tfn_pool.bin",
                                          "/tfn_pool.tmp",
                                          "/tfn_pool.tmp.part",
                                          "/apps",
                                          "/APPS",
                                          "/apps/",
                                          "/apps/x/app.elf",
                                          "/Apps./x",
                                          "//apps/x",
                                          "/ apps",
                                          "/ai_accounts.enc",
                                          "/ai_accounts.tmp",
                                          "/ai_limits.key",
                                          "/AI_LIMITS.KEW",
                                          "/ai_limits.kew.part",
                                          "/.",
                                          "/./store2.bin",
                                          "/a~b"};
    static const char *const allowed[] = {"/",
                                          "",
                                          "/notes.txt",
                                          "/store2.bin2",
                                          "/store2",
                                          "/xstore2.bin",
                                          "/appsx",
                                          "/my/apps",
                                          "/my/store2.bin",
                                          "/midi/a.mid",
                                          "/menu.txt",
                                          "/splash.jpg",
                                          "/tfn_sigok.bin.bak",
                                          "/a_very_long_name_of_a_file_of_the_user.txt"};
    for (size_t i = 0; i < sizeof refused / sizeof refused[0]; i++) {
        if (!web_data_sys_path(refused[i])) {
            printf("FAIL web_data_sys_path(\"%s\") is false, must be true\n", refused[i]);
            s_fails++;
        }
    }
    for (size_t i = 0; i < sizeof allowed / sizeof allowed[0]; i++) {
        if (web_data_sys_path(allowed[i])) {
            printf("FAIL web_data_sys_path(\"%s\") is true, must be false\n", allowed[i]);
            s_fails++;
        }
    }
}

// web_data_route_path: the old routes GET /f/<path> and GET /del/<path> refuse the files of the system and each bad
// path; the media files of the main page stay open
static void check_routes(void)
{
    static const char *const refused[][2] = {{"/del/tfn_pool.bin", "/del"},
                                             {"/del/TFN_POLICY.TXT", "/del"},
                                             {"/f/store2.bin", "/f"},
                                             {"/f/tfn_pool.bin", "/f"},
                                             {"/del/tfn_policy.sig", "/del"},
                                             {"/del/apps/x/app.elf", "/del"},
                                             {"/f/apps/x/manifest.txt", "/f"},
                                             {"/f/ai_accounts.enc", "/f"},
                                             {"/del/ai_limits.key", "/del"},
                                             {"/del//tfn_pool.bin", "/del"},
                                             {"/del/./tfn_pool.bin", "/del"},
                                             {"/del/tfn_pool.bin.", "/del"},
                                             {"/f/TFN_SI~1.BIN", "/f"},
                                             {"/f/\\tfn_pool.bin", "/f"},
                                             {"/f/exports\\x.wav", "/f"},
                                             {"/f/exports/../store2.bin", "/f"},
                                             {"/f/exports/a\x01.wav", "/f"},
                                             {"/f", "/f"},
                                             {"/fx/a.wav", "/f"},
                                             {"/del", "/del"},
                                             {"/x/exports/a.wav", "/del"}};
    for (size_t i = 0; i < sizeof refused / sizeof refused[0]; i++) {
        if (web_data_route_path(refused[i][0], refused[i][1])) {
            printf("FAIL web_data_route_path(\"%s\", \"%s\") is not NULL\n", refused[i][0], refused[i][1]);
            s_fails++;
        }
    }
    char lng[260] = "/f/exports/";
    memset(lng + 11, 'a', 200);
    lng[211] = 0;
    if (web_data_route_path(lng, "/f")) {
        printf("FAIL web_data_route_path of a path of 200 characters is not NULL\n");
        s_fails++;
    }
    static const char *const allowed[][3] = {
            {"/f/exports/song.wav", "/f", "/exports/song.wav"}, {"/del/exports/song.wav", "/del", "/exports/song.wav"},
            {"/f/samples/v1.wav", "/f", "/samples/v1.wav"},     {"/del/midi/a_b.mid", "/del", "/midi/a_b.mid"},
            {"/f/songs/x.mid", "/f", "/songs/x.mid"},           {"/f/touchlab/f1.txt", "/f", "/touchlab/f1.txt"},
            {"/f/my song (1).wav", "/f", "/my song (1).wav"}};
    for (size_t i = 0; i < sizeof allowed / sizeof allowed[0]; i++) {
        const char *r = web_data_route_path(allowed[i][0], allowed[i][1]);
        if (!r || strcmp(r, allowed[i][2])) {
            printf("FAIL web_data_route_path(\"%s\", \"%s\") = \"%s\", must be \"%s\"\n", allowed[i][0], allowed[i][1],
                   r ? r : "NULL", allowed[i][2]);
            s_fails++;
        }
    }
}

int main(void)
{
    static const char *fixed[] = {
            "",     "%",   "%4", "%41", "%41%", "%zz", "+", "a+b%20c", "\"\\", "\x01\x1f\x7f", "\xc3\xa9 \xd7\x90",
            "%%%%", "100%"};
    for (size_t k = 0; k < sizeof fixed / sizeof fixed[0]; k++) {
        check_json(fixed[k]);
        check_url(fixed[k]);
    }
    char one[2] = {0, 0};
    for (int c = 1; c < 256; c++) {
        one[0] = (char)c;
        check_json(one);
        check_url(one);
    }
    unsigned seed = 1;
    char t[640];
    for (int round = 0; round < 20000; round++) {
        random_text(t, (int)(seed % 600), &seed);
        check_json(t);
        check_url(t);
    }
    // web_send_page: the same answer as the reference for a body with text; an empty body sends no chunk before the
    // end (an empty chunk ends the answer: a second end is garbage for the next answer of a kept connection)
    const char *bodies[] = {"<p>x</p>", fixed[10]};
    for (int k = 0; k < 2; k++) {
        reset();
        old_page(&s_a, bodies[k]);
        if (web_send_page(&s_b, bodies[k]) != ESP_OK) {
            s_fails++;
        }
        same("web_send_page", bodies[k]);
    }
    reset();
    web_send_page(&s_b, "");
    one_end("web_send_page with an empty body", "<head>");
    // web_chunk: an empty text and NULL send nothing (no end); a text goes out as it is
    reset();
    web_chunk(&s_b, "");
    web_chunk(&s_b, NULL);
    web_chunk(&s_b, "{\"a\":");
    web_chunk(&s_b, "1}");
    httpd_resp_sendstr_chunk(&s_b, NULL);
    one_end("web_chunk", "{\"a\":1}");
    check_sys_paths();
    check_routes();
    printf("WEB UTIL TESTS %s\n", s_fails ? "FAILED" : "OK");
    return s_fails ? 1 : 0;
}
