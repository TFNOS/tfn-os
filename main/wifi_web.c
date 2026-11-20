// WiFi: the board pages of wifi_web_register (the files, the MIDI songs, the gamepad page).
#include "wifi_priv.h"
#include "web_util.h"

static const char *TAG = "wifi";

#define MIDI_MAX_BYTES (256 * 1024)
#define MIDI_MAX_FILES 100

static const char *mime(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return "application/octet-stream";
    }
    if (!strcasecmp(dot, ".wav")) {
        return "audio/wav";
    }
    if (!strcasecmp(dot, ".mid")) {
        return "audio/midi";
    }
    if (!strcasecmp(dot, ".txt")) {
        return "text/plain";
    }
    return "application/octet-stream";
}

static const char PAGE_HEAD[] =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'>"
        "<title>Arcade</title><style>"
        "body{font-family:system-ui,sans-serif;margin:0;background:#0d0f1e;color:#e8ecff}"
        "header{background:linear-gradient(90deg,#3a1d6e,#123d6e);padding:1.2em 1.5em}"
        "h1{margin:0;font-size:1.5em}h2{margin:1.2em 1.5em .4em;color:#ffd84a;font-size:1.1em}"
        ".f{margin:.5em 1.5em;padding:.8em "
        "1em;background:#181c34;border-radius:10px;display:flex;flex-wrap:wrap;gap:.6em;align-items:center}"
        ".n{flex:1 1 12em;font-weight:600}.s{color:#8a93c0;font-size:.85em}"
        "a.b,button{background:#2d6cdf;color:#fff;border:0;border-radius:6px;padding:.4em "
        ".8em;text-decoration:none;font-size:.9em;cursor:pointer}"
        "button.m{background:#1e9e5a}a.d{background:#7a2b3a}audio{height:32px;max-width:100%}"
        "p.i{margin:1em 1.5em;color:#8a93c0}nav{margin:.6em 1.5em;display:flex;gap:.5em;flex-wrap:wrap}"
        "nav a{background:#232a4d;color:#fff;padding:.35em .8em;border-radius:6px;text-decoration:none}"
        "input[type=file]{color:#cfd5ff}nav a.cur{background:#5b8cff}nav a:hover{background:#2f3870}"
        "input,select,textarea{background:#0b0d1a;color:#e8ecff;border:1px solid "
        "#2a3058;border-radius:8px;padding:.35em .5em;font:inherit}"
        "button:hover,a.b:hover{filter:brightness(1.15)}</style>"
        "<script src='https://cdn.jsdelivr.net/npm/lamejs@1.2.1/lame.min.js'></script>"
        "<script>"
        "async function mp3(u,n,b){b.textContent='...';try{"
        "const d=new DataView(await (await fetch(u)).arrayBuffer());"
        "const rate=d.getUint32(24,true),len=(d.byteLength-44)>>1,pcm=new Int16Array(len);"
        "for(let i=0;i<len;i++)pcm[i]=d.getInt16(44+2*i,true);"
        "const e=new lamejs.Mp3Encoder(1,rate,160),out=[];"
        "for(let i=0;i<len;i+=1152){const c=e.encodeBuffer(pcm.subarray(i,i+1152));if(c.length)out.push(c)}"
        "const f=e.flush();if(f.length)out.push(f);"
        "const a=document.createElement('a');a.href=URL.createObjectURL(new Blob(out,{type:'audio/mpeg'}));"
        "a.download=n;a.click();b.textContent='MP3'}catch(x){b.textContent='MP3 (needs internet)'}}"
        "function del(u){if(confirm('Delete this file?'))location=u}"
        "async function up(){const fs=document.getElementById('mf').files,st=document.getElementById('us');"
        "let ok=0;for(const f of fs){if(f.size>262144){st.textContent=f.name+': too big (256 KB at most)';continue}"
        "st.textContent='Uploading '+f.name+'...';"
        "const r=await fetch('/midi?name='+encodeURIComponent(f.name),{method:'POST',body:f});"
        "if(r.ok)ok++;else st.textContent=f.name+': '+await r.text()}"
        "if(ok)location.reload()}"
        "</script>"
        // in the central manager (an iframe) the manager has the menu: no header and no menu here
        "<style>.emb header,.emb "
        "nav{display:none}</style><script>if(window!==top)document.documentElement.classList.add('emb')</script>"
        "</head><body><header><h1>&#127918; Arcade</h1>"
        "<div class=s>" BOARD_NAME_SHORT "</div></header>"
        "<nav><a href='/files'>Files</a><a href='/'>Songs</a><a href='/#midi'>MIDI songs</a><a href='/walkie'>Walkie "
        "&amp; friends</a>"
#if CONFIG_TFN_EXTRA_VPN
        "<a href='/vpn'>VPN</a>"
#endif
#if CONFIG_TFN_EXTRA_HA
        "<a href='/ha'>Home Assistant</a>"
#endif
#if CONFIG_TFN_EXTRA_STOCKS
        "<a href='/stocks'>Stocks</a>"
#endif
        "<a href='/apps'>Apps</a><a href='/companions'>Companions</a>"
#if CONFIG_TFN_EXTRA_CHAT
        "<a href='/chat'>Chat</a>"
#endif
#if ARCADE_HAS_BOOKS
        "<a href='/books'>Books</a>"
#endif
        "<a href='/pad'>Gamepad</a>"
#if BOARD_CAMERA == 1
        "<a href='/cam'>Camera</a>"
#endif
        "</nav>"
        // the current page: marked in the menu (a.pathname: the full address, also under the manager)
        "<script>document.querySelectorAll('nav "
        "a').forEach(a=>{if(a.pathname==location.pathname&&!a.hash)a.classList.add('cur')})</script>";

// the header and the menu of the board page (web_walkie.c uses it too)
void wifi_send_page_head(httpd_req_t *req)
{
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
}

static void list_dir(httpd_req_t *req, const char *dir, const char *title)
{
    char line[640];
    snprintf(line, sizeof(line), "<h2>%s</h2>", title);
    httpd_resp_sendstr_chunk(req, line);
    DIR *d = opendir(dir);
    int n = 0;
    const char *rel = dir + strlen(STORAGE_ROOT);
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char path[300];
            struct stat st;
            snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
            if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
                continue;
            }
            const char *dot = strrchr(e->d_name, '.');
            bool wav = dot && !strcasecmp(dot, ".wav");
            if (!wav && dot && !strcasecmp(dot, ".mid")) {
                // a MIDI file with a WAV of the same name is shown in the WAV row
                char w[300];
                snprintf(w, sizeof(w), "%.*s.wav", (int)(strlen(path) - 4), path);
                struct stat st2;
                if (stat(w, &st2) == 0) {
                    continue;
                }
            }
            char base[64];
            snprintf(base, sizeof(base), "%.*s", (int)(dot ? dot - e->d_name : (int)strlen(e->d_name)), e->d_name);
            snprintf(line, sizeof(line), "<div class=f><span class=n>%s<br><span class=s>%ld KB</span></span>",
                     e->d_name, (long)(st.st_size / 1024));
            httpd_resp_sendstr_chunk(req, line);
            if (wav) {
                snprintf(line, sizeof(line),
                         "<audio controls preload=none src='/f%s/%s'></audio>"
                         "<a class=b href='/f%s/%s' download>WAV</a>"
                         "<button class=m onclick=\"mp3('/f%s/%s','%s.mp3',this)\">MP3</button>",
                         rel, e->d_name, rel, e->d_name, rel, e->d_name, base);
                httpd_resp_sendstr_chunk(req, line);
                char mid[300];
                snprintf(mid, sizeof(mid), "%s/%s.mid", dir, base);
                struct stat st3;
                if (stat(mid, &st3) == 0) {
                    snprintf(line, sizeof(line), "<a class=b href='/f%s/%s.mid' download>MIDI</a>", rel, base);
                    httpd_resp_sendstr_chunk(req, line);
                }
            } else {
                snprintf(line, sizeof(line), "<a class=b href='/f%s/%s' download>DOWNLOAD</a>", rel, e->d_name);
                httpd_resp_sendstr_chunk(req, line);
            }
            snprintf(line, sizeof(line), "<a class='b d' href='#' onclick=\"del('/del%s/%s')\">DELETE</a></div>", rel,
                     e->d_name);
            httpd_resp_sendstr_chunk(req, line);
            n++;
        }
        closedir(d);
    }
    if (n == 0) {
        httpd_resp_sendstr_chunk(req, "<p class=i>Nothing here yet.</p>");
    }
}

// MIDI songs for TILES: an upload box, then each file with its summary
static void list_midi(httpd_req_t *req)
{
    char line[700];
    httpd_resp_sendstr_chunk(req,
                             "<h2 id=midi>MIDI songs for TILES</h2>"
                             "<p class=i>TILES plays the first channel as tiles and the other channels as the band. "
                             "In TILES, open MY SONGS (or RANDOM).</p>"
                             "<div class=f><input type=file id=mf accept='.mid,.midi,audio/midi' multiple>"
                             "<button class=m onclick=up()>UPLOAD</button><span id=us class=s></span></div>");
    DIR *d = opendir(STORAGE_MIDI);
    int n = 0;
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            char path[300], info[120];
            struct stat st;
            snprintf(path, sizeof(path), "%s/%s", STORAGE_MIDI, e->d_name);
            if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
                continue;
            }
            if (!midifile_summary(path, info, sizeof(info))) {
                snprintf(line, sizeof(line), "<span class=s style='color:#ff8080'>%s</span>", info);
            } else {
                snprintf(line, sizeof(line), "<span class=s>%s</span>", info);
            }
            static char row[1400];  // one web task: a static buffer saves stack
            snprintf(row, sizeof(row),
                     "<div class=f><span class=n>%.96s<br>%.700s</span>"
                     "<a class=b href='/f/midi/%.96s' download>DOWNLOAD</a>"
                     "<a class='b d' href='#' onclick=\"del('/del/midi/%.96s')\">DELETE</a></div>",
                     e->d_name, line, e->d_name, e->d_name);
            httpd_resp_sendstr_chunk(req, row);
            n++;
        }
        closedir(d);
    }
    if (n == 0) {
        httpd_resp_sendstr_chunk(req, "<p class=i>No MIDI songs yet.</p>");
    }
}

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr_chunk(req, PAGE_HEAD);
    list_dir(req, STORAGE_EXPORTS, "Songs from PADS and STUDIO");
    list_dir(req, STORAGE_SAMPLES, "Voice samples");
    list_midi(req);
    char line[120];
    snprintf(line, sizeof(line), "<p class=i>%u KB free on the board.</p></body></html>",
             (unsigned)(storage_free() / 1024));
    httpd_resp_sendstr_chunk(req, line);
    httpd_resp_sendstr_chunk(req, NULL);
    return ESP_OK;
}

static esp_err_t del_get(httpd_req_t *req)
{
    // /del/exports/name.wav -> /data/exports/name.wav (and the MIDI file with it); never a file of the system
    const char *uri = web_data_route_path(req->uri, "/del");
    if (!uri) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name");
        return ESP_FAIL;
    }
    char path[256];
    snprintf(path, sizeof(path), "%s%s", STORAGE_ROOT, uri);
    unlink(path);
    char *dot = strrchr(path, '.');
    if (dot && !strcasecmp(dot, ".wav")) {
        strcpy(dot, ".mid");
        unlink(path);
    }
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t file_get(httpd_req_t *req)
{
    // /f/exports/name.wav -> /data/exports/name.wav; never a file of the system
    const char *uri = web_data_route_path(req->uri, "/f");
    if (!uri) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad name");
        return ESP_FAIL;
    }
    char path[256];
    snprintf(path, sizeof(path), "%s%s", STORAGE_ROOT, uri);
    FILE *f = fopen(path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "no such file");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, mime(path));
    char *buf = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!buf) {
        fclose(f);
        return ESP_FAIL;
    }
    size_t n;
    while ((n = fread(buf, 1, 4096, f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            break;
        }
    }
    free(buf);
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

static int count_midi(void)
{
    int n = 0;
    DIR *d = opendir(STORAGE_MIDI);
    if (!d) {
        return 0;
    }
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        n++;
    }
    closedir(d);
    return n;
}

// POST /midi?name=song.mid with the raw file as the body
static esp_err_t midi_post(httpd_req_t *req)
{
    char q[512] = "", raw[400] = "", name[48];  // an encoded Hebrew name is long
    httpd_req_get_url_query_str(req, q, sizeof(q));
    httpd_query_key_value(q, "name", raw, sizeof(raw));
    // Keep a safe file name: letters, digits, - _ ( ) (a space becomes _); then ".mid".
    // The board font has only Latin letters: Hebrew letters change to Latin ones
    // (a simple transliteration), other letters are left out, and then a short code
    // from the full name keeps the names different.
    static const char *const HEB[27] = {
            "a", "b", "g", "d", "h", "v", "z", "ch", "t",  "y", "k", "k",  "l", "m",
            "m", "n", "n", "s", "a", "f", "p", "tz", "tz", "k", "r", "sh", "t",
    };
    uint8_t dec[sizeof(raw)];
    int nd = 0;
    for (int i = 0; raw[i] && nd < (int)sizeof(dec) - 1; i++) {
        if (raw[i] == '%' && raw[i + 1] && raw[i + 2]) {
            char hx[3] = {raw[i + 1], raw[i + 2], 0};
            dec[nd++] = (uint8_t)strtol(hx, NULL, 16);
            i += 2;
        } else {
            dec[nd++] = (uint8_t)(raw[i] == '+' ? ' ' : raw[i]);
        }
    }
    int stem = nd;
    for (int i = nd - 1; i >= 0; i--) {
        if (dec[i] == '.') {
            stem = i;
            break;
        }
    }
    uint32_t hash = 2166136261u;
    for (int i = 0; i < stem; i++) {
        hash = (hash ^ dec[i]) * 16777619u;
    }
    int k = 0, dropped = 0;
    for (int i = 0; i < stem && k < 30; i++) {
        uint8_t c = dec[i];
        if (c == 0xD7 && i + 1 < stem && dec[i + 1] >= 0x90 && dec[i + 1] <= 0xAA) {
            for (const char *t = HEB[dec[i + 1] - 0x90]; *t && k < 30; t++) {
                name[k++] = *t;
            }
            i++;
        } else if (c >= 0x80) {
            dropped++;  // another script: the code below keeps the name unique
            while (i + 1 < stem && (dec[i + 1] & 0xC0) == 0x80) {
                i++;
            }
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                   c == '(' || c == ')') {
            name[k++] = (char)c;
        } else if (c == ' ') {
            name[k++] = '_';  // no spaces: the names go into links
        }
    }
    name[k] = 0;
    if (dropped > 0 || k == 0) {
        snprintf(name + k, sizeof(name) - k, "%s%04X", k ? "_" : "song_", (unsigned)(hash & 0xFFFF));
    }
    if (req->content_len < 14 || req->content_len > MIDI_MAX_BYTES) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "The file must be a MIDI file of 256 KB or less.");
        return ESP_FAIL;
    }
    if (count_midi() >= MIDI_MAX_FILES || storage_free() < req->content_len + 64 * 1024) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No space: delete some songs first.");
        return ESP_FAIL;
    }
    char *buf = heap_caps_malloc(req->content_len, MALLOC_CAP_SPIRAM);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No memory.");
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            break;
        }
        got += (size_t)r;
    }
    if (got != req->content_len || memcmp(buf, "MThd", 4) != 0) {
        free(buf);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "This is not a MIDI file.");
        return ESP_FAIL;
    }
    // never overwrite: song.mid, then song_2.mid, song_3.mid ...
    char path[96];
    snprintf(path, sizeof(path), "%s/%s.mid", STORAGE_MIDI, name);
    struct stat st;
    for (int n = 2; n < 100 && stat(path, &st) == 0; n++) {
        snprintf(path, sizeof(path), "%s/%s_%d.mid", STORAGE_MIDI, name, n);
    }
    bool ok = storage_write(path, buf, got);
    free(buf);
    if (!ok) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Could not write the file.");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "MIDI saved: %s (%u bytes)", path, (unsigned)got);
    httpd_resp_sendstr(req, "ok");
    return ESP_OK;
}

static esp_err_t pad_page_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, pad_config_page());
    return ESP_OK;
}

static esp_err_t pad_api_get(httpd_req_t *req)
{
    char *buf = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return ESP_FAIL;
    }
    int n = pad_config_json(buf, 8192);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n > 0 ? n : 0);
    free(buf);
    return ESP_OK;
}

static esp_err_t pad_api_post(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad size");
        return ESP_FAIL;
    }
    char *buf = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM);
    if (!buf) {
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < req->content_len) {
        int r = httpd_req_recv(req, buf + got, req->content_len - got);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (r <= 0) {
            break;
        }
        got += (size_t)r;
    }
    buf[got] = 0;
    bool ok = got == req->content_len && pad_config_set_form(buf, (int)got);
    free(buf);
    if (!ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad settings");
        return ESP_FAIL;
    }
    httpd_resp_sendstr(req, "saved");
    return ESP_OK;
}

// The routes of this file (web_server.c calls it first).
void wifi_web_register(httpd_handle_t h)
{
    httpd_uri_t idx = {.uri = "/", .method = HTTP_GET, .handler = index_get};
    httpd_uri_t fil = {.uri = "/f/*", .method = HTTP_GET, .handler = file_get};
    httpd_uri_t del = {.uri = "/del/*", .method = HTTP_GET, .handler = del_get};
    httpd_register_uri_handler(h, &idx);
    httpd_register_uri_handler(h, &fil);
    httpd_register_uri_handler(h, &del);
    httpd_uri_t mid = {.uri = "/midi", .method = HTTP_POST, .handler = midi_post};
    httpd_uri_t pad = {.uri = "/pad", .method = HTTP_GET, .handler = pad_page_get};
    httpd_uri_t pag = {.uri = "/api/pad", .method = HTTP_GET, .handler = pad_api_get};
    httpd_uri_t pap = {.uri = "/api/pad", .method = HTTP_POST, .handler = pad_api_post};
    httpd_register_uri_handler(h, &mid);
    httpd_register_uri_handler(h, &pad);
    httpd_register_uri_handler(h, &pag);
    httpd_register_uri_handler(h, &pap);
}
