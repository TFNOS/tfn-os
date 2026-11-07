// FILES: the info page, the text page and the picture page (the JPEG decode) (main/app_files.c has the rest).
#include "app_files_priv.h"

// ---------------------------------------------------------------- the info page

static void info_add(const char *l, const char *v)
{
    if (fil_info_n >= 6) {
        return;
    }
    snprintf(fil_info_l[fil_info_n], sizeof fil_info_l[0], "%.15s", l);
    snprintf(fil_info_v[fil_info_n], sizeof fil_info_v[0], "%.159s", v);
    fil_info_n++;
}

void fil_open_info(int64_t now)
{
    (void)now;
    const fent_t *e = &fil_ent[fil_sel];
    char v[FL_PATH], t[32];
    fil_info_n = 0;
    snprintf(fil_info_name, sizeof fil_info_name, "%s", e->name);
    static const char *const KIND[] = {"A FOLDER", "AN APP PACKAGE", "A TEXT", "A PICTURE (JPEG)", "A FILE"};
    info_add("KIND", KIND[e->kind]);
    if (e->kind == K_DIR || e->kind == K_PKG) {
        int n = 0;
        fil_sel_path(v, sizeof v);
        DIR *d = opendir(v);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (strcmp(de->d_name, ".") && strcmp(de->d_name, "..")) {
                    n++;
                }
            }
            closedir(d);
        }
        snprintf(t, sizeof t, "%d", n);
        info_add("ITEMS", t);
    } else {
        fil_fmt_size(t, sizeof t, e->size);
        char b[48];
        snprintf(b, sizeof b, "%s (%u BYTES)", t, (unsigned)e->size);
        info_add("SIZE", b);
    }
    fil_fmt_date(t, sizeof t, e->mtime);
    info_add("DATE", t);
    snprintf(v, sizeof v, "%s%.300s", fil_root_name(fil_root), fil_rel);
    info_add("PLACE", v);
    if (e->lock) {
        info_add("LOCKED", "A SYSTEM FILE: NO DELETE, NO RENAME");
    }
    fil_page = P_INFO;
    fil_hot = -1;
    sfx_ui(SFX_CLICK);
}

// ---------------------------------------------------------------- the text page

void fil_text_free(void)
{
    heap_caps_free(fil_txt);
    heap_caps_free(fil_loff);
    heap_caps_free(fil_llen);
    fil_txt = NULL;
    fil_loff = NULL;
    fil_llen = NULL;
    fil_nl = 0;
}

// the lines of the text t (n bytes) in w px of the font f: the start and the length of each line; returns the count
int fil_wrap_lines(const kfont_t *f, const char *t, uint32_t n, int w, uint32_t *loff, uint16_t *llen, int max)
{
    uint32_t i = 0;
    int nl = 0;
    while (i <= n && nl < max) {
        uint32_t start = i, j = i, brk = 0;
        float x = 0;
        int ncp = 0;
        while (j < n && t[j] != '\n') {
            uint32_t k = j;
            int c = fil_u8cp(t, &k, n);
            float a = fil_kadv(f, c);
            if (ncp && (x + a > w || ncp >= 90)) {
                break;
            }
            x += a;
            ncp++;
            j = k;
            if (c == ' ') {
                brk = j;
            }
        }
        uint32_t end, next;
        if (j >= n || t[j] == '\n') {
            end = j;
            next = j + 1;
        } else if (brk > start) {
            end = brk;
            next = brk;
        } else {
            end = next = j;
        }
        loff[nl] = start;
        llen[nl] = (uint16_t)(end - start);
        nl++;
        if (j >= n) {
            break;
        }
        i = next;
    }
    return nl;
}

static void text_layout(void)
{
    fil_nl = fil_wrap_lines(&KF_TEXT, fil_txt, fil_tlen, TXT_W, fil_loff, fil_llen, TXT_LINES);
}

void fil_open_text(int64_t now)
{
    char p[FL_PATH];
    fil_sel_path(p, sizeof p);
    fil_text_free();
    FILE *f = fopen(p, "rb");
    fil_txt = heap_caps_malloc(TXT_MAX + 1, MALLOC_CAP_SPIRAM);
    fil_loff = heap_caps_malloc(TXT_LINES * sizeof(uint32_t), MALLOC_CAP_SPIRAM);
    fil_llen = heap_caps_malloc(TXT_LINES * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!f || !fil_txt || !fil_loff || !fil_llen) {
        if (f) {
            fclose(f);
        }
        fil_text_free();
        fil_say(f ? "NO MEMORY" : "THE FILE DID NOT OPEN", true, now);
        return;
    }
    size_t n = fread(fil_txt, 1, TXT_MAX, f);
    fclose(f);
    fil_tsize = fil_ent[fil_sel].size;
    // the text: no CR, a tab = a space, other control bytes = a space
    uint32_t o = 0;
    for (size_t k = 0; k < n; k++) {
        uint8_t c = (uint8_t)fil_txt[k];
        if (c == '\r') {
            continue;
        }
        fil_txt[o++] = (char)(c == '\n' ? '\n' : c < 0x20 ? ' ' : c);
    }
    fil_txt[o] = 0;
    fil_tlen = o;
    text_layout();
    snprintf(fil_tname, sizeof fil_tname, "%s", fil_ent[fil_sel].name);
    ks_init(&fil_kt, TXT_H, 0);  // (fil_update sets its length)
    fil_tscroll = 0;
    fil_touch = fil_drag = false;
    fil_page = P_TEXT;
    fil_hot = -1;
    sfx_ui(SFX_CLICK);
}

int fil_text_max(void)
{
    int m = fil_nl * TXT_LH - TXT_H + 8;
    return m > 0 ? m : 0;
}

// ---------------------------------------------------------------- the picture page

int fil_pic_id;  // the decode that the picture page waits for (jj_start; 0: none)
void fil_pic_free(void)
{
    heap_caps_free(fil_pic);
    fil_pic = NULL;
    fil_pic_id = 0;  // (a decode that runs now: its result goes)
}

#ifdef ESP_PLATFORM
typedef struct {
    FILE *fp;
    int dw, dh;
    uint16_t *dst;
    int w, h;
    int n;
    int64_t t;
} jsrc_t;
static UINT j_in(JDEC *jd, BYTE *buf, UINT n)
{
    jsrc_t *s = (jsrc_t *)jd->device;
    if (buf) {
        return (UINT)fread(buf, 1, n, s->fp);
    }
    return fseek(s->fp, (long)n, SEEK_CUR) == 0 ? n : 0;
}
static UINT j_out(JDEC *jd, void *bm, JRECT *r)
{
    jsrc_t *s = (jsrc_t *)jd->device;
    const uint8_t *c = (const uint8_t *)bm;
    for (int y = r->top; y <= r->bottom; y++) {
        int Y = y * s->h / s->dh;
        for (int x = r->left; x <= r->right; x++, c += 3) {
            int X = x * s->w / s->dw;
            if (X < s->w && Y < s->h) {
                s->dst[Y * s->w + X] = rgb(c[0], c[1], c[2]);
            }
        }
    }
    // (the decode runs in the worker task: each 20 ms it gives the CPU to the other tasks for a tick)
    if ((++s->n & 15) == 0 && esp_timer_get_time() - s->t > 20000) {
        vTaskDelay(1);
        s->t = esp_timer_get_time();
    }
    return 1;
}
#endif

// decode the JPEG at path to fit in bw x bh (a small picture keeps its size): *out (PSRAM, the caller frees it), *ow,
// *oh. false: no picture, err has the cause
static bool jpeg_fit(const char *path, int bw, int bh, uint16_t **out, int *ow, int *oh, char *err, int en)
{
    *out = NULL;
#ifdef ESP_PLATFORM
    FILE *fp = fopen(path, "rb");
    void *work = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    if (!fp || !work) {
        if (fp) {
            fclose(fp);
        }
        heap_caps_free(work);
        snprintf(err, en, fp ? "NO MEMORY" : "THE FILE DID NOT OPEN");
        return false;
    }
    jsrc_t src = {fp, 1, 1, NULL, 1, 1, 0, esp_timer_get_time()};
    JDEC jd;
    JRESULT r = jd_prepare(&jd, j_in, work, 4096, &src);
    if (r == JDR_OK) {
        // the smallest decode that is not less than 2 x the box (1/1, 1/2, 1/4, 1/8), then a fit
        int s = 0;
        while (s < 3 && ((int)jd.width >> s) > 2 * bw && ((int)jd.height >> s) > 2 * bh) {
            s++;
        }
        src.dw = (int)jd.width >> s;
        src.dh = (int)jd.height >> s;
        if (src.dw < 1) {
            src.dw = 1;
        }
        if (src.dh < 1) {
            src.dh = 1;
        }
        if (src.dw <= bw && src.dh <= bh) {
            src.w = src.dw;
            src.h = src.dh;
        } else if ((float)src.dw / bw > (float)src.dh / bh) {
            src.w = bw;
            src.h = src.dh * bw / src.dw;
        } else {
            src.h = bh;
            src.w = src.dw * bh / src.dh;
        }
        if (src.w < 1) {
            src.w = 1;
        }
        if (src.h < 1) {
            src.h = 1;
        }
        src.dst = heap_caps_malloc((size_t)src.w * src.h * 2, MALLOC_CAP_SPIRAM);
        if (!src.dst) {
            r = JDR_MEM1;
        } else {
            memset(src.dst, 0, (size_t)src.w * src.h * 2);
            r = jd_decomp(&jd, j_out, (uint8_t)s);
        }
    }
    fclose(fp);
    heap_caps_free(work);
    if (r != JDR_OK) {
        heap_caps_free(src.dst);
        snprintf(err, en,
                 r == JDR_FMT3 ? "THIS KIND OF JPEG CANNOT SHOW (PROGRESSIVE)" : "THE PICTURE DID NOT SHOW (ERROR %d)",
                 (int)r);
        return false;
    }
    *out = src.dst;
    *ow = src.w;
    *oh = src.h;
    return true;
#else
    // the PC: no JPEG decoder; a test pattern of 3/4 of the box
    (void)path;
    const int w = bw * 3 / 4, h = bh * 3 / 4;
    uint16_t *p = heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM);
    if (!p) {
        snprintf(err, en, "NO MEMORY");
        return false;
    }
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int sky = y < h * 6 / 10;
            int r = sky ? 40 + y * 120 / h : 30, g = sky ? 90 + y * 100 / h : 110 + x * 60 / w,
                b = sky ? 200 - y * 60 / h : 60;
            p[y * w + x] = rgb((uint8_t)r, (uint8_t)g, (uint8_t)b);
        }
    }
    *out = p;
    *ow = w;
    *oh = h;
    return true;
#endif
}

// THE DECODE IN A WORKER: a big JPEG takes seconds in jpeg_fit(). On the board a worker task does it,
// so the app task goes on (the page shows OPENING...). One decode at a time. Each user (the picture page, the Tab5
// preview) keeps the id of its decode; jj_poll() gives a result to its user, and frees a result that nobody waits for.
// The PC: the decode runs at once in jj_start().
enum {
    JJ_IDLE,
    JJ_RUN,
    JJ_DONE
};
static struct {
    char path[FL_PATH], err[64];
    int bw, bh, w, h, id;
    uint16_t *pic;
    bool ok;
    volatile bool drop;  // (the app left while the worker decodes: the worker frees its picture)
    volatile int state;
} s_jj;
static int s_jj_ids;

static void jj_work(void)
{
    s_jj.ok = jpeg_fit(s_jj.path, s_jj.bw, s_jj.bh, &s_jj.pic, &s_jj.w, &s_jj.h, s_jj.err, sizeof s_jj.err);
    __sync_synchronize();
    if (s_jj.drop) {
        heap_caps_free(s_jj.pic);
        s_jj.pic = NULL;
        s_jj.drop = false;
        s_jj.state = JJ_IDLE;
    } else {
        s_jj.state = JJ_DONE;
    }
}

#ifdef ESP_PLATFORM
static void jj_task(void *arg)
{
    (void)arg;
    jj_work();
    vTaskDelete(NULL);
}
#endif

// start a decode of path into bw x bh; the id of the decode, or 0 when the worker is busy (try again later)
int fil_jj_start(const char *path, int bw, int bh)
{
    if (s_jj.state != JJ_IDLE) {
        return 0;
    }
    snprintf(s_jj.path, sizeof s_jj.path, "%s", path);
    s_jj.bw = bw;
    s_jj.bh = bh;
    s_jj.pic = NULL;
    s_jj.err[0] = 0;
    s_jj.drop = false;
    if (++s_jj_ids <= 0) {
        s_jj_ids = 1;
    }
    s_jj.id = s_jj_ids;
    s_jj.state = JJ_RUN;
#ifdef ESP_PLATFORM
    // (an internal stack: a picture on the board storage /data is a read of the flash)
    if (xTaskCreatePinnedToCore(jj_task, "fil_jpeg", 6144, NULL, 2, NULL, tskNO_AFFINITY) != pdPASS) {
        jj_work();
    }
#else
    jj_work();
#endif
    return s_jj.id;
}

// a decode that is complete: to its user, else it goes
void fil_jj_poll(void)
{
    if (s_jj.state != JJ_DONE) {
        return;
    }
    __sync_synchronize();
    if (s_jj.id == fil_pic_id && fil_page == P_PIC && fil_pstate == 0) {
        fil_pic = s_jj.pic;
        fil_pw = s_jj.w;
        fil_ph = s_jj.h;
        fil_pstate = s_jj.ok ? 1 : 2;
        snprintf(fil_perr, sizeof fil_perr, "%s", s_jj.err);
#if T5P
    } else if (s_jj.id == fil_pv_id && fil_pv >= 0 && fil_pv_state == 0) {
        fil_pv_pic = s_jj.pic;
        fil_pv_w = s_jj.w;
        fil_pv_h = s_jj.h;
        fil_pv_state = s_jj.ok ? 1 : 2;
        snprintf(fil_pv_err, sizeof fil_pv_err, "%s", s_jj.err);
#endif
    } else {
        heap_caps_free(s_jj.pic);
    }
    s_jj.pic = NULL;
    s_jj.state = JJ_IDLE;
}

// the app leaves: a complete decode goes now, a decode that runs goes at its end
void fil_jj_leave(void)
{
    if (s_jj.state == JJ_DONE) {
        heap_caps_free(s_jj.pic);
        s_jj.pic = NULL;
        s_jj.state = JJ_IDLE;
    } else if (s_jj.state == JJ_RUN) {
        s_jj.drop = true;
    }
}

void fil_open_pic(int64_t now)
{
    if (fil_ent[fil_sel].size > 8u * 1024 * 1024) {
        fil_open_info(now);
        fil_say("THE PICTURE IS TOO BIG TO SHOW (MORE THAN 8 MB)", true, now);
        return;
    }
    fil_pic_free();
    fil_sel_path(fil_ppath, sizeof fil_ppath);
    snprintf(fil_pname, sizeof fil_pname, "%s", fil_ent[fil_sel].name);
    fil_pstate = 0;
    fil_perr[0] = 0;
    fil_pbar_until = now + 3000;
    fil_page = P_PIC;
    fil_hot = -1;
    sfx_ui(SFX_CLICK);
}
