// FILES of the board web page (/files): a browser of all the files of the board storage (/data) and of the SD card,
// with a download of each file and an upload into the open folder. One GET and one POST handler (the server has a
// limit of handlers):
//   GET  /files                                  the page
//   GET  /files?op=ls&root=data|card&path=/dir   JSON {"ok","root","path","free","items":[{"n","d","s","t"}]}
//   GET  /files?op=get&root=data|card&path=/f    the file (an attachment with its name)
//   POST /files?op=put&root=data|card&path=/f    the body is the file: written to "<f>.part", then renamed (a broken
//                                                upload never replaces a file); JSON {"ok","msg"}
// A path starts with '/', has no "..", no '\' and no control characters. The card is mounted when it is asked for.
// The files and the folders of the system in the board storage (web_data_sys_path: the settings, the signature cache,
// the policy, the pool, the installed apps) are refused: a write there could change the trust of the board.
#include "web_util.h"
#include "web_files.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "board.h"
#include "storage.h"
#include "wifi.h"

#define WF_PATH      256          // the longest path under a root
#define WF_MAX_ITEMS 2000         // the entries of one folder in the list
#define WF_DATA_KEEP (64 * 1024)  // an upload to the board storage leaves this much free (the saves of the apps)

static const char *TAG = "files";

static const char PAGE[] =
        "<h2>Files</h2>"
        "<div class=f><button id=bb onclick=\"go('data',S)\">BOARD</button>"
        "<button id=bk onclick=\"go('card',S)\">SD CARD</button>"
        "<span class=s id=fr></span></div>"
        "<div class=f><span class=n id=bc></span><button onclick=up1()>UP</button></div>"
        "<div class=f><input type=file id=uf multiple><button class=m onclick=upl()>UPLOAD HERE</button>"
        "<span class=s id=us></span></div>"
        "<div id=ls><p class=i>Loading...</p></div>"
        "<script>"
        "const S='\\x2f';let R='data',P=S;"
        "const E=s=>s.replace(/[&<>\"']/g,c=>'&#'+c.charCodeAt(0)+';');"
        "const Q=(o,p)=>'files?op='+o+'&root='+R+'&path='+encodeURIComponent(p);"
        "function sz(b){return b<1024?b+' B':b<1048576?(b/1024).toFixed(1)+' KB':(b/1048576).toFixed(1)+' MB'}"
        "function dt(t){return t>315532800?new Date(t*1000).toISOString().slice(0,16).replace('T',' '):''}"
        "function j(p,n){return(p==S?'':p)+S+n}"
        "function up1(){if(P!=S)go(R,P.slice(0,P.lastIndexOf(S))||S)}"
        "function go(r,p){R=r;P=p;history.pushState(null,'','#'+r+':'+encodeURIComponent(p));load()}"
        "function rd(){try{const "
        "m=/^#(data|card):(.*)$/.exec(location.hash);if(m){R=m[1];P=decodeURIComponent(m[2])||S}}catch(x){}load()}"
        "onpopstate=rd;"
        "async function load(){"
        "document.getElementById('bb').className=R=='data'?'m':'';document.getElementById('bk').className=R=='card'?'m'"
        ":'';"
        "document.getElementById('bc').textContent=(R=='data'?'BOARD':'SD CARD')+'  '+P;"
        "const L=document.getElementById('ls');L.innerHTML='<p class=i>Loading...</p>';"
        "let d;try{d=await(await fetch(Q('ls',P))).json()}catch(x){d={ok:false,msg:'no answer'}}"
        "if(!d.ok){N=[];L.innerHTML='<p class=i>'+E(d.msg||'error')+'</p>';return}"
        "document.getElementById('fr').textContent=d.free>=0?sz(d.free*1024)+' free':'';"
        "d.items.sort((a,b)=>b.d-a.d||a.n.localeCompare(b.n));N=d.items.map(i=>i.n);"
        "if(!d.items.length){L.innerHTML='<p class=i>This folder is empty.</p>';return}"
        "let h='';for(const i of d.items){const p=j(P,i.n);"
        "if(i.d)h+='<div class=f><span class=n>&#128193; <a href=\"#\" style=\"color:#9fc1ff\" "
        "data-p=\"'+E(p)+'\">'+E(i.n)+'</a></span></div>';"
        "else h+='<div class=f><span class=n>'+E(i.n)+'<br><span class=s>'+sz(i.s)+'  '+dt(i.t)+'</span></span>"
        "<a class=b href=\"'+E(Q('get',p))+'\" download=\"'+E(i.n)+'\">DOWNLOAD</a></div>'}"
        "L.innerHTML=h;L.querySelectorAll('a[data-p]').forEach(a=>a.onclick=e=>{e.preventDefault();go(R,a.dataset.p)})}"
        "let N=[];"
        "async function upl(){const fs=[...document.getElementById('uf').files],st=document.getElementById('us');"
        "if(!fs.length){st.textContent='Choose files first.';return}"
        "const ex=fs.filter(f=>N.includes(f.name)).map(f=>f.name);"
        "if(ex.length&&!confirm('Replace '+ex.join(', ')+'?'))return;"
        "let k=0;for(const f of fs){k++;st.textContent='Uploading '+f.name+' ('+k+S+fs.length+')...';"
        "let d;try{d=await(await "
        "fetch(Q('put',j(P,f.name))+'&t='+Math.floor(f.lastModified/1000),{method:'POST',body:f})).json()}"
        "catch(x){d={ok:false,msg:'no answer'}}"
        "if(!d.ok){st.textContent=f.name+': '+(d.msg||'error');load();return}}"
        "st.textContent=fs.length+' uploaded.';document.getElementById('uf').value='';load()}"
        "rd()"
        "</script></body></html>";

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

// the root and the path of the query -> the full path; NULL, or the reason of a refusal (a bad root or path, or a
// file or a folder of the system in the board storage: web_data_sys_path)
static const char *wf_path(const char *q, char *full, int len, bool *card)
{
    static const char BAD[] = "a bad path";
    char root[8] = "", raw[3 * WF_PATH], p[WF_PATH];
    if (httpd_query_key_value(q, "root", root, sizeof root) != ESP_OK) {
        return BAD;
    }
    if (httpd_query_key_value(q, "path", raw, sizeof raw) != ESP_OK) {
        return BAD;
    }
    *card = !strcmp(root, "card");
    if (!*card && strcmp(root, "data")) {
        return BAD;
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
            return BAD;
        }
        p[k++] = (char)c;
    }
    p[k] = 0;
    if (p[0] != '/' || strstr(p, "..") || strstr(p, "//")) {
        return BAD;
    }
    while (k > 1 && p[k - 1] == '/') {
        p[--k] = 0;
    }
    if (!*card && web_data_sys_path(p)) {
        return "a file of the system: the web cannot read or change it";
    }
    snprintf(full, len, "%s%s", *card ? BOARD_SD_ROOT : STORAGE_ROOT, k == 1 ? "" : p);
    return NULL;
}

static void json_err(httpd_req_t *req, const char *msg)
{
    char o[160];
    snprintf(o, sizeof o, "{\"ok\":false,\"msg\":\"%s\"}", msg);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, o);
}

static void wf_ls(httpd_req_t *req, const char *full, bool card, const char *root_rel)
{
    DIR *d = opendir(full);
    if (!d) {
        json_err(req, "no such folder");
        return;
    }
    long free_kb = -1;
    if (card) {
        uint32_t tot = 0, fr = 0;
        if (board_sd_info(&tot, &fr)) {
            free_kb = (long)fr;
        }
    } else {
        free_kb = (long)(storage_free() / 1024);
    }
    httpd_resp_set_type(req, "application/json");
    char line[96];
    snprintf(line, sizeof line, "{\"ok\":true,\"root\":\"%s\",\"free\":%ld,\"path\":", card ? "card" : "data", free_kb);
    httpd_resp_send_chunk(req, line, HTTPD_RESP_USE_STRLEN);
    web_json_str(req, root_rel[0] ? root_rel : "/");
    httpd_resp_send_chunk(req, ",\"items\":[", HTTPD_RESP_USE_STRLEN);
    char *path = heap_caps_malloc(WF_PATH + 300, MALLOC_CAP_SPIRAM);
    struct dirent *de;
    int n = 0;
    while (path && (de = readdir(d)) && n < WF_MAX_ITEMS) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        snprintf(path, WF_PATH + 300, "%s/%s", full, de->d_name);
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

static void wf_get(httpd_req_t *req, const char *full)
{
    struct stat st;
    FILE *f = stat(full, &st) == 0 && !S_ISDIR(st.st_mode) ? fopen(full, "rb") : NULL;
    char *buf = f ? heap_caps_malloc(8192, MALLOC_CAP_SPIRAM) : NULL;
    if (!buf) {
        if (f) {
            fclose(f);
        }
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such file");
        return;
    }
    // the name for the browser: plain ASCII in filename=, the UTF-8 name in filename*=
    const char *name = strrchr(full, '/') ? strrchr(full, '/') + 1 : full;
    static char cd[480];  // (the web server runs one request at a time)
    int k = snprintf(cd, sizeof cd, "attachment; filename=\"");
    for (const char *s = name; *s && k < 150; s++) {
        cd[k++] = (*s >= ' ' && *s < 127 && *s != '"' && *s != '\\') ? *s : '_';
    }
    k += snprintf(cd + k, sizeof cd - k, "\"; filename*=UTF-8''");
    for (const unsigned char *s = (const unsigned char *)name; *s && k < (int)sizeof cd - 4; s++) {
        if ((*s >= 'a' && *s <= 'z') || (*s >= 'A' && *s <= 'Z') || (*s >= '0' && *s <= '9') || strchr("-._~", *s)) {
            cd[k++] = (char)*s;
        } else {
            k += snprintf(cd + k, sizeof cd - k, "%%%02X", *s);
        }
    }
    cd[k] = 0;
    char clen[16];
    snprintf(clen, sizeof clen, "%ld", (long)st.st_size);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", cd);
    httpd_resp_set_hdr(req, "X-File-Size", clen);
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

static void json_ok(httpd_req_t *req, bool ok, const char *msg)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, ok ? "{\"ok\":true,\"msg\":" : "{\"ok\":false,\"msg\":");
    web_json_str(req, msg);
    httpd_resp_sendstr_chunk(req, "}");
    httpd_resp_sendstr_chunk(req, NULL);
}

// the body of the request -> the file full (its folder must exist); false with the reason in msg
static bool wf_put(httpd_req_t *req, const char *full, bool card, const char *q, char *msg, int len)
{
    struct stat st;
    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(msg, len, "a folder has this name");
        return false;
    }
    uint64_t room = 0;
    if (card) {
        uint32_t tot = 0, fr = 0;
        if (board_sd_info(&tot, &fr)) {
            room = (uint64_t)fr * 1024;
        }
    } else {
        uint64_t fr = storage_free();
        room = fr > WF_DATA_KEEP ? fr - WF_DATA_KEEP : 0;
    }
    if (req->content_len > room) {
        snprintf(msg, len, "not enough room (%lu KB free)", (unsigned long)(room / 1024));
        return false;
    }
    if (card) {
        // the folders of the path that are not there yet (a copy of a whole folder tree to the card, for
        // example the files of M5PaperDOS-X into /M5DOS, needs them)
        char dir[WF_PATH + 32];
        snprintf(dir, sizeof dir, "%s", full);
        for (char *s = dir + strlen(BOARD_SD_ROOT) + 1; (s = strchr(s, '/')) != NULL; s++) {
            *s = 0;
            mkdir(dir, 0777);
            *s = '/';
        }
    }
    char part[WF_PATH + 32];
    snprintf(part, sizeof part, "%s.part", full);
    FILE *f = fopen(part, "wb");
    char *buf = f ? heap_caps_malloc(8192, MALLOC_CAP_SPIRAM) : NULL;
    size_t got = 0;
    int waits = 0;
    int64_t t0 = esp_timer_get_time();
    while (buf && got < req->content_len) {
        int r = httpd_req_recv(req, buf, 8192);
        if (r == HTTPD_SOCK_ERR_TIMEOUT && ++waits < 6) {
            continue;  // (about 30 s with no data: stop)
        }
        if (r <= 0 || fwrite(buf, 1, (size_t)r, f) != (size_t)r) {
            break;
        }
        got += (size_t)r;
    }
    free(buf);
    bool closed = f && fclose(f) == 0;
    if (!closed || got != req->content_len) {
        unlink(part);
        snprintf(msg, len, f ? "the file did not come complete (%u of %u bytes)" : "cannot write in this folder",
                 (unsigned)got, (unsigned)req->content_len);
        return false;
    }
    unlink(full);
    if (rename(part, full)) {
        unlink(part);
        snprintf(msg, len, "cannot give the file its name");
        return false;
    }
    char tv[24] = "";
    httpd_query_key_value(q, "t", tv, sizeof tv);
    long long t = atoll(tv);
    if (t > 315532800) {  // (after 1980: the first time of FAT)
        struct utimbuf u = {.actime = (time_t)t, .modtime = (time_t)t};
        utime(full, &u);
    }
    long long ms = (esp_timer_get_time() - t0) / 1000;
    snprintf(msg, len, "%u bytes in %lld ms", (unsigned)got, ms);
    return true;
}

static esp_err_t files_post(httpd_req_t *req)
{
    static char q[3 * WF_PATH + 64];  // (the web server runs one request at a time)
    char op[8] = "", full[WF_PATH + 16], msg[120] = "";
    bool card = false;
    q[0] = 0;
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "op", op, sizeof op);
    if (strcmp(op, "put")) {
        json_ok(req, false, "op: put");
        return ESP_OK;
    }
    const char *bad = wf_path(q, full, sizeof full, &card);
    if (bad || !strcmp(full, card ? BOARD_SD_ROOT : STORAGE_ROOT)) {
        json_ok(req, false, bad ? bad : "a bad path");
        return ESP_OK;
    }
    if (card && !(board_sd_mounted() || board_sd_mount())) {
        json_ok(req, false, "no SD card in the board");
        return ESP_OK;
    }
    bool ok = wf_put(req, full, card, q, msg, sizeof msg);
    ESP_LOGI(TAG, "put %s: %s", full, msg);
    json_ok(req, ok, msg);
    return ESP_OK;
}

static esp_err_t files_get(httpd_req_t *req)
{
    static char q[3 * WF_PATH + 64];  // (the web server runs one request at a time)
    char op[8] = "", full[WF_PATH + 16];
    bool card = false;
    q[0] = 0;
    httpd_req_get_url_query_str(req, q, sizeof q);
    httpd_query_key_value(q, "op", op, sizeof op);
    if (!op[0]) {
        return web_send_page(req, PAGE);
    }
    const char *bad = wf_path(q, full, sizeof full, &card);
    if (bad) {
        json_err(req, bad);
        return ESP_OK;
    }
    if (card && !(board_sd_mounted() || board_sd_mount())) {
        json_err(req, "no SD card in the board");
        return ESP_OK;
    }
    const char *rel = full + strlen(card ? BOARD_SD_ROOT : STORAGE_ROOT);
    if (!strcmp(op, "ls")) {
        wf_ls(req, full, card, rel);
    } else if (!strcmp(op, "get")) {
        wf_get(req, full);
    } else {
        json_err(req, "op: ls or get");
    }
    return ESP_OK;
}

void web_files_register(void *server)
{
    httpd_uri_t u = {.uri = "/files", .method = HTTP_GET, .handler = files_get};
    httpd_uri_t p = {.uri = "/files", .method = HTTP_POST, .handler = files_post};
    httpd_register_uri_handler((httpd_handle_t)server, &u);
    httpd_register_uri_handler((httpd_handle_t)server, &p);
}
