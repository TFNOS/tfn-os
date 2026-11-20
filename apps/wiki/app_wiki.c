// WIKI (UTIL): the offline Wikipedia on the SD card.
// The task: the app task of TFN OS calls the hooks of app_wiki.
// The memory: the file statics; the ZIM reader (s_z), the document of the article (s_doc), its lines (s_ln) and
// the pictures of the Tab5 are in PSRAM: they come when a file or a page opens and go at leave.
//
// A ZIM file of Kiwix (for example the top articles of the Hebrew Wikipedia, 2.3 GB) in /sdcard/wiki/ (or its
// split parts .zimaa, .zimab ...). With more than one file, the setting LANGUAGE picks the file of its language
// ("_en_" or "_he_" in the name; the store data pack of WIKI gives the top English articles).
//  - READ ALOUD (wk_voice.inc): an English article on a board with the voice (tfn/speech.h).
//  - HOME: SEARCH (the keyboard with the Hebrew letters), RANDOM, CONTINUE (the last article).
//  - RESULTS: the titles that start with the text (the title listing of the file: a binary search on the card).
//  - THE ARTICLE: the text of the page (no tables; the pictures only on the Tab5), right to left, with its links (a
//    tap on a blue word opens its page); a finger scrolls the text; BACK goes to the page before.
// The reader: libs/zimread (zstd clusters in PSRAM); the HTML: wk_html.inc.
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kbd.h"
#include "tfn/kfont.h"
#include "tfn/listen.h"
#include "tfn/settings.h"
#include "tfn/speech.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
// (ui_text is not a name of the app ABI: the left-aligned text from ui_text_c and ui_text_w)
static void txt_at(const kfont_t *f, int x, int base, const char *s, uint16_t c)
{
    ui_text_c(f, x + ui_text_w(f, s) / 2, base, s, c);
}
#include "zimread/zimread.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
static uint32_t rnd(void)
{
    return esp_random();
}
static uint32_t ms_now(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}
#else
#define ESP_LOGI(tag, ...)                                                                                             \
    do {                                                                                                               \
        printf(__VA_ARGS__);                                                                                           \
        printf("\n");                                                                                                  \
    } while (0)
static uint32_t rnd(void)
{
    return (uint32_t)rand() * 32768u + (uint32_t)rand();
}
static uint32_t ms_now(void)
{
    return 0;
}
#endif
#ifndef WIKI_DIR
#define WIKI_DIR "/sdcard/wiki"
#endif
static const char *TAG = "wiki";

#include "wk_html.inc"
#include "rndlay/rndlay.h"

// ---------------------------------------------------------------- the settings and the words of the screens
static int s_lang = 1, s_big;
static const char *T(const char *en, const char *he)
{
    return s_lang ? he : en;
}

enum {
    SC_NOFILE,
    SC_HOME,
    SC_KBD,
    SC_RESULTS,
    SC_ARTICLE
};
static int s_sc;
static int64_t s_now;

// ---------------------------------------------------------------- the layout of each board
// THE ROUND FRAME (APP_ROUND_HI): the title in the top cap (s_top), the article in a middle column of 0.78 x s_w
// (its rows stay inside the circle) down to s_bot, the keys SEARCH and BACK in the bottom cap, the home keys in the
// middle of the circle
static int s_w, s_h, s_top, s_bot, s_margin;
static void layout_board(void)
{
    s_w = gfx_w;
    s_h = gfx_h;
    s_top = s_h >= 300 ? 52 : (s_w >= 3 * s_h ? 36 : 40);
    s_bot = s_h;
    s_margin = s_w >= 600 ? 12 : 8;
    if (gfx_round()) {
        s_top = s_w * 20 / 100;
        s_bot = s_w * 78 / 100;
        s_margin = s_w * 11 / 100;
    }
}
#if BOARD_UI_TABLET
// THE TAB5 (the tablet look): the article is a column of a book width (about 60 letters a line). A page with
// 2 or more headings shows its CONTENTS in a pane at the side where the reader starts (the right side in Hebrew); a tap
// on a heading goes to it, the heading of the part on the screen is lit. The search is a field in the top bar and in
// the middle of HOME. AL and AR: the sides of the article column (layout_doc sets them).
#define PANE_W   176
#define PANE_RH  36            // a row of the pane (6 mm)
#define PANE_Y0  (s_top + 34)  // the first row (below the word CONTENTS)
#define SEARCH_W 184           // the search field of the top bar
static int s_al, s_ar;
static bool s_pane;
static bool pane_tap(int x, int y);
#define AL s_al
#define AR s_ar
#else
#define AL s_margin
#define AR (s_w - s_margin)
#endif
static const kfont_t *body_font(void)
{
    return s_big ? &KF_BIG : &KF_TEXT;
}
static const kfont_t *head_font(int blk)
{
    return blk == BK_H2 ? (s_big ? &KF_TITLE : &KF_BIG) : (s_big ? &KF_BIG : &KF_NAME);
}

// ---------------------------------------------------------------- the file
static zim_t *s_z;
static char s_file[64], s_err[80];
static int s_zlang = -1;  // the language of the open file (its name): 0 English, 1 Hebrew, -1 not known

static int name_lang(const char *n)
{
    return strstr(n, "_en_") ? 0 : strstr(n, "_he_") ? 1 : -1;
}
static uint32_t s_ntitles;

static bool find_file(char *path, int max)
{
    DIR *d = opendir(WIKI_DIR);
    if (!d) {
        return false;
    }
    char best[64] = "", mine[64] = "";  // (the first name; the first name of the language of the setting)
    struct dirent *de;
    while ((de = readdir(d))) {
        const char *n = de->d_name;
        size_t l = strlen(n);
        bool whole = l > 4 && !strcasecmp(n + l - 4, ".zim");
        bool part = l > 6 && !strcasecmp(n + l - 6, ".zimaa");
        if (!whole && !part) {
            continue;
        }
        char name[64];
        snprintf(name, sizeof name, "%.*s", (int)(part ? l - 2 : l), n);  // (name.zimaa -> name.zim)
        if (!best[0] || strcmp(name, best) < 0) {
            snprintf(best, sizeof best, "%s", name);
        }
        if (name_lang(name) == s_lang && (!mine[0] || strcmp(name, mine) < 0)) {
            snprintf(mine, sizeof mine, "%s", name);
        }
    }
    closedir(d);
    if (mine[0]) {
        snprintf(best, sizeof best, "%s", mine);
    }
    if (!best[0]) {
        return false;
    }
    s_zlang = name_lang(best);
    snprintf(s_file, sizeof s_file, "%s", best);
    snprintf(path, (size_t)max, "%s/%s", WIKI_DIR, best);
    return true;
}

static void file_open(void)
{
    char path[128];
    s_err[0] = 0;
    if (!find_file(path, sizeof path)) {
        s_sc = SC_NOFILE;
        return;
    }
    uint32_t t0 = ms_now();
    s_z = zim_open(path, s_err, sizeof s_err);
    if (!s_z) {
        s_sc = SC_NOFILE;
        return;
    }
    s_ntitles = zim_title_count(s_z);
    ESP_LOGI(TAG, "open %s: %u entries, %u titles, %d part(s), %u ms", s_file, (unsigned)zim_entry_count(s_z),
             (unsigned)s_ntitles, zim_parts(s_z), (unsigned)(ms_now() - t0));
    s_sc = SC_HOME;
}

// ---------------------------------------------------------------- the article: the document and its lines
typedef struct {
    uint32_t w0;
    uint16_t nw;
    int16_t h;
    int32_t y;
    uint8_t blk, first;
} wline_t;
static wdoc_t s_doc;
static wline_t *s_ln;
static uint32_t s_nln, s_ln_cap;
static int32_t s_doc_h;
static uint32_t s_entry = ZIM_NONE;
static char s_ptitle[ZIM_TITLE_MAX];
static float s_scroll, s_vel;

static int space_w(const kfont_t *f)
{
    return f->line / 4 + 1;
}

// the largest font of the list (big to small) in which the text is w px or less (the last one if none): ui_text_fit is
// not in the ABI of the app files
static const kfont_t *wk_fit(const kfont_t *const *fs, int n, const char *t, int w)
{
    for (int i = 0; i < n - 1; i++) {
        if (ui_text_w(fs[i], t) <= w) {
            return fs[i];
        }
    }
    return fs[n - 1];
}

// a whole title for ui_text_c: a copy (the text engine orders a Hebrew line itself, libs/bidi/bidi.h: the brackets and
// the signs of a Latin word go to their side; before it this function moved them in the text)
static const char *rtl_text(const char *in, char *out, int max)
{
    strlcpy(out, in, (size_t)max);
    return out;
}

static void word_text(const wword_t *w, char *buf, int max)
{
    int n = w->len < max - 1 ? w->len : max - 1;
    memcpy(buf, s_doc.tx + w->off, (size_t)n);
    buf[n] = 0;
}

// the place of the words of one line (logical order a .. b): right to left, a run of left-to-right words keeps its order
static void place_line(uint32_t a, uint32_t b, int left, int right, const kfont_t *f, bool rtl)
{
    int sp = space_w(f);
    if (!rtl) {
        int x = left;
        for (uint32_t i = a; i < b; i++) {
            if (i > a && !(s_doc.w[i].flags & WF_GLUE)) {
                x += sp;
            }
            s_doc.w[i].x = (int16_t)x;
            x += s_doc.w[i].w;
        }
        return;
    }
    // the resolved direction: a neutral word is left to right only between two left-to-right words of the line
    for (uint32_t i = a; i < b; i++) {
        wword_t *w = &s_doc.w[i];
        if (!(w->flags & WF_NEUT)) {
            continue;
        }
        int before = 0, after = 0;
        // (a number counts as right to left here: the bidi rule N1)
        for (uint32_t k = i; k-- > a;) {
            if (!(s_doc.w[k].flags & WF_NEUT)) {
                before = (s_doc.w[k].flags & (WF_LTR | WF_NUM)) == WF_LTR ? 1 : -1;
                break;
            }
        }
        for (uint32_t k = i + 1; k < b; k++) {
            if (!(s_doc.w[k].flags & WF_NEUT)) {
                after = (s_doc.w[k].flags & (WF_LTR | WF_NUM)) == WF_LTR ? 1 : -1;
                break;
            }
        }
        if (before == 1 && after == 1) {
            w->flags |= WF_LTR;
        } else {
            w->flags &= (uint8_t)~WF_LTR;
        }
    }
    int cur = right;
    uint32_t i = a;
    while (i < b) {
        wword_t *w = &s_doc.w[i];
        if (i > a && !(w->flags & WF_GLUE)) {
            cur -= sp;
        }
        if (w->flags & WF_LTR) {
            uint32_t j = i + 1;
            int run = w->w;
            while (j < b && (s_doc.w[j].flags & WF_LTR)) {  // (the next left-to-right words join the run)
                run += s_doc.w[j].w + ((s_doc.w[j].flags & WF_GLUE) ? 0 : sp);
                j++;
            }
            int x = cur - run;
            for (uint32_t k = i; k < j; k++) {
                if (k > i && !(s_doc.w[k].flags & WF_GLUE)) {
                    x += sp;
                }
                s_doc.w[k].x = (int16_t)x;
                x += s_doc.w[k].w;
            }
            cur -= run;
            i = j;
        } else {
            cur -= w->w;
            w->x = (int16_t)cur;
            i++;
        }
    }
}

static bool add_line(uint32_t a, uint32_t b, int32_t y, int h, uint8_t blk, bool first)
{
    if (s_nln >= s_ln_cap) {
        return false;
    }
    s_ln[s_nln++] = (wline_t){a, (uint16_t)(b - a), (int16_t)h, y, blk, first};
    return true;
}

// the direction of a paragraph: its first strong letter (Hebrew: right to left; Latin: left to right; digits and signs
// are weak)
static bool para_rtl(uint32_t a, uint32_t end)
{
    for (uint32_t i = a; i < end; i++) {
        const wword_t *w = &s_doc.w[i];
        for (uint32_t k = 0; k < w->len; k++) {
            unsigned char ch = (unsigned char)s_doc.tx[w->off + k];
            if (ch >= 0x80) {
                return true;
            }
            if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')) {
                return false;
            }
        }
    }
    return true;
}

#if BOARD_UI_TABLET
static int32_t pics_before(uint32_t bi, int32_t y);  // (wk_pics.inc)
#endif
static void layout_doc(void)
{
    s_nln = 0;
    if (!s_ln) {
        s_ln_cap = s_doc.nw / 2 + 64;
        s_ln = wk_alloc(s_ln_cap * sizeof(wline_t));
        if (!s_ln) {
            s_ln_cap = 0;
            return;
        }
    }
    char buf[256];
    int32_t y = 6;
#if BOARD_UI_TABLET
    int nh = 0;
    for (uint32_t bi = 0; bi < s_doc.nblk; bi++) {
        uint32_t a = s_doc.blk_first[bi];
        if (a < s_doc.nw && s_doc.w[a].blk == BK_H2) {
            nh++;
        }
    }
    s_pane = nh >= 2;
    if (!s_pane) {
        s_al = 56;
        s_ar = s_w - 56;
    }  // (no pane: the column in the middle)
    else if (s_lang) {
        s_al = 24;
        s_ar = s_w - PANE_W - 24;
    }  // (the pane at the right)
    else {
        s_al = PANE_W + 24;
        s_ar = s_w - 24;
    }
#endif
    for (uint32_t bi = 0; bi < s_doc.nblk; bi++) {
        uint32_t a = s_doc.blk_first[bi], end = bi + 1 < s_doc.nblk ? s_doc.blk_first[bi + 1] : s_doc.nw;
#if BOARD_UI_TABLET
        y = pics_before(bi, y);  // (the pictures before this block)
#endif
        if (a >= end) {
            continue;
        }
        uint8_t kind = s_doc.w[a].blk;
        const kfont_t *f = kind == BK_H2 || kind == BK_H3 ? head_font(kind) : body_font();
        int lh = f->line + 3;
        int indent = kind == BK_LI ? 20 : kind == BK_DD ? 28 : 0;
        int left = AL, right = AR - indent;
        bool rtl = para_rtl(a, end);
        if (!rtl) {
            left = AL + indent;
            right = AR;
        }
        if (kind == BK_H2) {
            y += 10;
        } else if (kind == BK_H3) {
            y += 6;
        }
        int sp = space_w(f), x = 0;
        uint32_t ls = a;
        for (uint32_t i = a; i < end; i++) {
            word_text(&s_doc.w[i], buf, sizeof buf);
            s_doc.w[i].w = (uint16_t)ui_text_w(f, buf);
            int add = s_doc.w[i].w + ((i > ls && !(s_doc.w[i].flags & WF_GLUE)) ? sp : 0);
            if (i > ls && x + add > right - left) {
                // the break: before this word, or before the first word of its glued group (no line starts with a
                // comma or a closing bracket)
                uint32_t bp = i;
                while (bp > ls + 1 && (s_doc.w[bp].flags & WF_GLUE)) {
                    bp--;
                }
                place_line(ls, bp, left, right, f, rtl);
                if (!add_line(ls, bp, y, lh, kind, ls == a)) {
                    goto full;
                }
                y += lh;
                ls = bp;
                x = 0;
                for (uint32_t k = bp; k <= i; k++) {
                    x += s_doc.w[k].w + ((k > bp && !(s_doc.w[k].flags & WF_GLUE)) ? sp : 0);
                }
            } else {
                x += add;
            }
        }
        place_line(ls, end, left, right, f, rtl);
        if (!add_line(ls, end, y, lh, kind, ls == a)) {
            goto full;
        }
        y += lh + (kind == BK_LI ? 2 : 8);
    }
#if BOARD_UI_TABLET
    y = pics_before(UINT32_MAX, y);  // (the pictures after the last block)
#endif
full:
    s_doc_h = y + 40;
}

static int view_h(void)
{
    return s_bot - s_top;
}
static void clamp_scroll(void)
{
    float mx = (float)(s_doc_h - view_h());
    if (mx < 0) {
        mx = 0;
    }
    if (s_scroll > mx) {
        s_scroll = mx;
        s_vel = 0;
    }
    if (s_scroll < 0) {
        s_scroll = 0;
        s_vel = 0;
    }
}

// ---------------------------------------------------------------- the history and the load of a page
#define HIST 32
static uint32_t s_hist[HIST];
static float s_hist_scroll[HIST];
static int s_nhist;
static uint32_t s_pending = ZIM_NONE;
static float s_pending_scroll;
static int s_pending_frames;
static char s_msg[96];  // a message on the article page (a fault)
#if BOARD_UI_TABLET
#include "wk_pics.inc"
#endif

static void open_entry(uint32_t idx, bool push, float scroll)
{
    if (push && s_entry != ZIM_NONE && s_sc == SC_ARTICLE) {
        if (s_nhist == HIST) {
            memmove(s_hist, s_hist + 1, sizeof(s_hist[0]) * (HIST - 1));
            memmove(s_hist_scroll, s_hist_scroll + 1, sizeof(s_hist_scroll[0]) * (HIST - 1));
            s_nhist--;
        }
        s_hist[s_nhist] = s_entry;
        s_hist_scroll[s_nhist++] = s_scroll;
    }
    s_pending = idx;
    s_pending_scroll = scroll;
    s_pending_frames = 2;  // (one frame with LOADING, then the work)
}

static void load_now(void)
{
    uint32_t idx = s_pending;
    s_pending = ZIM_NONE;
    s_msg[0] = 0;
    uint32_t t0 = ms_now();
    uint32_t f = zim_follow(s_z, idx);
    zim_entry_t e;
    if (f == ZIM_NONE || !zim_entry(s_z, f, &e)) {
        snprintf(s_msg, sizeof s_msg, "%s", T("THE PAGE IS NOT IN THE FILE", "הדף לא בקובץ"));
        s_sc = SC_ARTICLE;
        return;
    }
    if (strncmp(zim_mime(s_z, e.mime), "text/html", 9)) {
        snprintf(s_msg, sizeof s_msg, "%s", T("NOT A TEXT PAGE", "זה לא דף טקסט"));
        s_sc = SC_ARTICLE;
        return;
    }
#if BOARD_UI_TABLET
    pics_free();
#endif
    doc_free(&s_doc);
    free(s_ln);
    s_ln = NULL;
    uint32_t len = 0;
    const uint8_t *html = zim_blob(s_z, e.cluster, e.blob, &len);
    uint32_t t1 = ms_now();
    if (!html) {
        zim_drop_cache(s_z);
        html = zim_blob(s_z, e.cluster, e.blob, &len);
    }
    if (!html || !html_to_doc(html, len, &s_doc)) {
        snprintf(s_msg, sizeof s_msg, "%s",
                 T("THE PAGE DID NOT LOAD (MEMORY OR CARD)", "הדף לא נטען: זיכרון או כרטיס"));
        s_sc = SC_ARTICLE;
        return;
    }
    uint32_t t2 = ms_now();
    layout_doc();
    uint32_t t3 = ms_now();
    s_entry = f;
    snprintf(s_ptitle, sizeof s_ptitle, "%s", e.title);
    s_scroll = s_pending_scroll;
    s_vel = 0;
    clamp_scroll();
    s_sc = SC_ARTICLE;
    store_set("wiki_last", f);
    store_set("wiki_lastn", zim_entry_count(s_z));
    zim_stats_t st;
    zim_stats(s_z, &st);
    ESP_LOGI(TAG,
             "page %u: html %u KB in %u ms (decode %u ms), doc %u ms (%u words, %u links%s), layout %u ms (%u lines)",
             (unsigned)f, (unsigned)(len / 1024), (unsigned)(t1 - t0), (unsigned)st.last_decode_ms, (unsigned)(t2 - t1),
             (unsigned)s_doc.nw, (unsigned)s_doc.nlk, s_doc.cut ? ", cut" : "", (unsigned)(t3 - t2), (unsigned)s_nln);
    speech_read_t rs;
    speech_read_get(&rs);
    if (rs.state == SPR_MAKE || rs.state == SPR_WAIT || rs.state == SPR_PLAY) {
        zim_drop_cache(s_z);  // (the voice reads: it needs the PSRAM of the cluster)
    }
}

static void go_back(void)
{
    if (s_nhist > 0) {
        s_nhist--;
        open_entry(s_hist[s_nhist], false, s_hist_scroll[s_nhist]);
    } else {
        s_sc = SC_HOME;
    }
}

static void open_random(void)
{
    if (!s_ntitles) {
        return;
    }
    for (int tries = 0; tries < 12; tries++) {
        uint32_t idx = zim_follow(s_z, zim_title_entry(s_z, rnd() % s_ntitles));
        zim_entry_t e;
        if (idx != ZIM_NONE && zim_entry(s_z, idx, &e) && !strncmp(zim_mime(s_z, e.mime), "text/html", 9)) {
            open_entry(idx, true, 0);
            return;
        }
    }
}

// ---------------------------------------------------------------- the search
static char s_query[96];
#define RES_MAX 60
static uint32_t s_res[RES_MAX];
static char s_res_title[RES_MAX][64];
static int s_nres;
static bool s_exact;
static float s_rscroll;

static void search_open(void)
{
    s_sc = SC_KBD;
    kbd_open(T("SEARCH", "חיפוש"), s_query, sizeof s_query, false);
    kbd_set_hebrew(s_zlang == 1 || (s_zlang < 0 && s_lang));  // (the letters of the file)
    kbd_set_ok_question("");
}

static void search_run(void)
{
    s_nres = 0;
    s_rscroll = 0;
    if (!s_query[0] || !s_ntitles) {
        return;
    }
    uint32_t lb = zim_title_lower_bound(s_z, s_query);
    size_t ql = strlen(s_query);
    zim_entry_t e;
    s_exact = false;
    for (uint32_t i = lb; i < s_ntitles && s_nres < RES_MAX; i++) {
        uint32_t idx = zim_title_entry(s_z, i);
        if (!zim_entry(s_z, idx, &e)) {
            break;
        }
        bool match = !strncmp(e.title, s_query, ql);
        if (!match && s_nres >= (s_exact ? 0 : 8)) {
            break;  // (no match: the 8 nearest titles)
        }
        if (match) {
            s_exact = true;
        }
        s_res[s_nres] = idx;
        strlcpy(s_res_title[s_nres], e.title, sizeof s_res_title[0]);
        s_nres++;
        if (!match && s_exact) {
            break;
        }
    }
    if (s_exact) {
        // (only the matches)
        int k = 0;
        for (int i = 0; i < s_nres; i++) {
            if (!strncmp(s_res_title[i], s_query, ql)) {
                s_res[k] = s_res[i];
                memcpy(s_res_title[k], s_res_title[i], sizeof s_res_title[0]);
                k++;
            }
        }
        s_nres = k;
    }
}

// the cells of the results: 2 columns on the wide screen, else 1
static int res_cols(void)
{
    return s_w >= 3 * s_h || s_w >= 600 ? 2 : 1;
}
static int res_h(void)
{
    return s_h >= 300 ? 60 : 44;
}
static void res_cell(int i, int *x, int *y, int *w, int *h)
{
    int cols = res_cols(), rh = res_h();
    int r = i / cols, c = i % cols;
    if (gfx_round()) {  // (one column: the width of each row follows the circle)
        *h = 46;
        *y = s_top + 4 + i * 52 - (int)s_rscroll;
        rl_row(*y, *h, 16, x, w);
        return;
    }
    *w = (s_w - s_margin * 2 - (cols - 1) * 6) / cols;
    *h = rh - 6;
    *x = s_w - s_margin - (c + 1) * (*w) - c * 6;  // (right to left: the first result at the right)
    *y = s_top + 4 + r * rh - (int)s_rscroll;
}
static int res_rows_h(void)
{
    if (gfx_round()) {
        return s_nres * 52 + 8;
    }
    return (s_nres + res_cols() - 1) / res_cols() * res_h() + 8;
}

// ---------------------------------------------------------------- the top bar and the buttons
// the top bar: BACK at the right (the side of a Hebrew reader), SEARCH at the left
// the round frame: the two keys of the bottom cap (0 = SEARCH at the left, 1 = BACK at the right): their boxes
static void bar_box(int which, int *x, int *y, int *w, int *h)
{
    *w = s_w * 116 / 466;
    *h = 48;
    *x = s_w / 2 + (which ? 6 : -6 - *w);
    *y = s_w * 400 / 466 - *h / 2;
}
static void bar_btn(int *x, int *w, int which)
{
    *w = s_w >= 600 ? 112 : 84;
#if BOARD_UI_TABLET
    if (!which) {
        *w = SEARCH_W;  // (the search field)
    }
#endif
    *x = which ? s_w - *w - 2 : 2;
}
static int bar_at(int x, int y)
{
    if (gfx_round()) {
        for (int b = 0; b < 2; b++) {
            int bx, by, bw, bh;
            bar_box(b, &bx, &by, &bw, &bh);
            if (x >= bx - 6 && x < bx + bw + 6 && y >= by - 6 && y < by + bh + 6) {
                return b;
            }
        }
        return -1;
    }
    if (y >= s_top) {
        return -1;
    }
    int bx, bw;
    for (int b = 0; b < 2; b++) {
        bar_btn(&bx, &bw, b);
        if (x >= bx - 4 && x < bx + bw + 4) {
            return b;  // 0 = SEARCH, 1 = BACK
        }
    }
    return -1;
}
static int s_bar_down = -1;

#if BOARD_UI_TABLET
// a search field: a round box, a magnifier glass at the side where the text starts, and a word (a tap opens the
// keyboard)
static void search_field(int x, int y, int w, int h, bool hot, const char *word)
{
    const kfont_t *f = h >= 56 ? &KF_BIG : &KF_NAME;
    uint16_t in = hot ? rgb(250, 215, 30) : rgb(26, 32, 54), fg = hot ? rgb(0, 0, 0) : rgb(170, 182, 220);
    ui_rrect(x, y, w, h, h / 2, hot ? rgb(250, 215, 30) : rgb(96, 112, 170));
    ui_rrect(x + 2, y + 2, w - 4, h - 4, h / 2 - 2, in);
    int gx = s_lang ? x + w - h / 2 - 4 : x + h / 2 + 4, gy = y + h / 2 - 2;
    int r = h >= 56 ? 11 : 8;
    ui_circle(gx, gy, r, fg, 256);
    ui_circle(gx, gy, r - 3, in, 256);
    for (int k = 0; k < r / 2 + 1; k++) {
        gfx_fill_rect(gx + r - 3 + k, gy + r - 3 + k, 3, 3, fg);
    }
    int cx = s_lang ? x + (w - h) / 2 + 6 : x + h + (w - h) / 2 - 6;
    ui_text_c(f, cx, y + h / 2 + f->asc / 2 - 1, word, fg);
}
#endif

// the round frame: the caps over the view, the title in the top cap, SEARCH and BACK in the bottom cap
static void draw_bar_round(const char *title)
{
    rl_caps(s_top, s_bot, rgb(12, 14, 24), rgb(60, 70, 110));
    if (title && title[0]) {
        static const kfont_t *const FS[] = {&KF_BIG, &KF_TEXT, &KF_NAME};
        const int room = 2 * rl_half(s_w * 50 / 466, 26);
        const kfont_t *f = wk_fit(FS, 3, title, room);
        char t[ZIM_TITLE_MAX], m[ZIM_TITLE_MAX];
        snprintf(t, sizeof t, "%s", title);
        while (ui_text_w(f, t) > room && strlen(t) > 4) {
            size_t l = strlen(t) - 1;
            while (l > 0 && ((unsigned char)t[l] & 0xC0) == 0x80) {
                l--;
            }
            t[l] = 0;
        }
        ui_text_c(f, s_w / 2, s_top / 2 + 4 + f->asc / 2, rtl_text(t, m, sizeof m), rgb(250, 220, 120));
    }
    for (int b = 0; b < 2; b++) {
        int bx, by, bw, bh;
        bar_box(b, &bx, &by, &bw, &bh);
        const bool hot = s_bar_down == b;
        ui_rrect(bx, by, bw, bh, bh / 2, hot ? rgb(250, 215, 30) : rgb(46, 56, 92));
        ui_text_c(&KF_TEXT, bx + bw / 2, by + bh / 2 + 7, b ? T("BACK", "חזרה") : T("SEARCH", "חיפוש"),
                  hot ? rgb(0, 0, 0) : rgb(235, 240, 255));
    }
}
static void draw_bar(const char *title)
{
    if (gfx_round()) {
        draw_bar_round(title);
        return;
    }
    gfx_fill_rect(0, 0, s_w, s_top, rgb(22, 26, 44));
    gfx_fill_rect(0, s_top - 1, s_w, 1, rgb(60, 70, 110));
    int bx, bw;
    for (int b = 0; b < 2; b++) {
        bar_btn(&bx, &bw, b);
        bool hot = s_bar_down == b;
#if BOARD_UI_TABLET
        if (b == 0) {
            search_field(bx, 6, bw, s_top - 12, hot, T("SEARCH", "חיפוש"));
            continue;
        }
#endif
        ui_rrect(bx, 4, bw, s_top - 8, 8, hot ? rgb(250, 215, 30) : rgb(46, 56, 92));
        ui_text_c(&KF_NAME, bx + bw / 2, s_top / 2 + 6, b ? T("< BACK", "< חזרה") : T("SEARCH", "חיפוש"),
                  hot ? rgb(0, 0, 0) : rgb(235, 240, 255));
    }
    if (title && title[0]) {
        static const kfont_t *const FS[] = {&KF_BIG, &KF_NAME, &KF_LABEL};
#if BOARD_UI_TABLET
        int room = s_w - 2 * (SEARCH_W + 12);  // (the title in the middle, clear of the field)
#else
        int room = s_w - 2 * (bw + 12);
#endif
        const kfont_t *f = wk_fit(FS, 3, title, room);
        char t[ZIM_TITLE_MAX];
        snprintf(t, sizeof t, "%s", title);
        while (ui_text_w(f, t) > room && strlen(t) > 4) {
            size_t l = strlen(t) - 1;
            while (l > 0 && ((unsigned char)t[l] & 0xC0) == 0x80) {
                l--;
            }
            t[l] = 0;
        }
        char m[ZIM_TITLE_MAX];
        ui_text_c(f, s_w / 2, s_top / 2 + f->asc / 2, rtl_text(t, m, sizeof m), rgb(250, 220, 120));
    }
}

// the home buttons: SEARCH, RANDOM, CONTINUE (when there is a last page)
enum {
    HB_SEARCH,
    HB_RANDOM,
    HB_CONT,
    HB_N
};
static uint32_t s_last = ZIM_NONE;
static char s_last_title[64];
static void home_btn(int i, int *x, int *y, int *w, int *h)
{
    int n = s_last != ZIM_NONE ? 3 : 2;
#if BOARD_UI_TABLET
    // the Tab5: the search field in the middle, RANDOM and CONTINUE in a row under it (RANDOM first: at the right
    // in Hebrew)
    const int fw = 440, fx = (s_w - fw) / 2;
    if (i == HB_SEARCH) {
        *x = fx;
        *y = 128;
        *w = fw;
        *h = 64;
        return;
    }
    *y = 212;
    *h = 72;
    *w = n == 2 ? fw : (fw - 12) / 2;
    *x = n == 2 || (i == HB_RANDOM) != (s_lang != 0) ? fx : fx + fw - *w;
    return;
#endif
    if (gfx_round()) {  // the round frame: a column of keys in the middle of the circle
        *w = s_w * 62 / 100;
        *h = 58;
        *x = (s_w - *w) / 2;
        *y = s_h / 2 - 48 + i * (*h + 12);
        return;
    }
    if (s_w >= 3 * s_h) {  // the bar screen: in a row, at the bottom
        *h = 70;
        *y = s_h - *h - 8;
        *w = (s_w - 2 * s_margin - (n - 1) * 10) / n;
        *x = s_w - s_margin - (i + 1) * (*w) - i * 10;
    } else {
        *h = s_h >= 300 ? 70 : 48;
        *w = s_w - 2 * s_margin;
        *x = s_margin;
        *y = s_h - (n - i) * (*h + 8);
    }
}
static int home_at(int x, int y)
{
    int n = s_last != ZIM_NONE ? 3 : 2;
    for (int i = 0; i < n; i++) {
        int bx, by, bw, bh;
        home_btn(i, &bx, &by, &bw, &bh);
        if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
            return i;
        }
    }
    return -1;
}
static int s_home_down = -1;

#include "wk_voice.inc"

// ---------------------------------------------------------------- the panel
static const char *const SIZE_V[] = {"NORMAL", "רגיל", "BIG", "גדול"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
enum {
    PI_SEARCH,
    PI_RANDOM,
    PI_HOME,
    PI_SIZE,
    PI_LANG,
    PI_STOPRD
};
static const pitem_t ITEMS[] = {
        {PI_ACT, PI_SEARCH, 0, 0, {"SEARCH", "חיפוש"}, NULL, NULL},
        {PI_ACT, PI_RANDOM, 0, 0, {"RANDOM PAGE", "דף אקראי"}, NULL, NULL},
        {PI_ACT, PI_HOME, 0, 0, {"HOME", "דף הבית"}, NULL, NULL},
        {PI_PICK, PI_SIZE, 2, 0, {"TEXT SIZE", "גודל הטקסט"}, SIZE_V, "wiki_big"},
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "wiki_lang"},
        {PI_ACT, PI_STOPRD, 0, 0, {"STOP READING", "עצור את ההקראה"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == PI_STOPRD) {
        speech_read_t st;
        return rd_mine(&st) || (s_rd_entry != ZIM_NONE && st.state != SPR_OFF);
    }
    return s_z != NULL || id >= PI_SIZE;
}

// the setting LANGUAGE changed: the file of that language opens (when there is one)
static void file_again(void)
{
    char path[128];
    const int old = s_zlang;
    if (!find_file(path, sizeof path) || s_zlang == old) {
        s_zlang = old;
        return;
    }
#if BOARD_UI_TABLET
    pics_free();
#endif
    doc_free(&s_doc);
    free(s_ln);
    s_ln = NULL;
    s_nln = 0;
    zim_close(s_z);
    s_z = NULL;
    s_entry = ZIM_NONE;
    s_nhist = 0;
    s_last = ZIM_NONE;
    file_open();
}
static void item_set(int id, int v)
{
    if (id == PI_SEARCH) {
        search_open();
    } else if (id == PI_RANDOM) {
        open_random();
    } else if (id == PI_HOME) {
        s_sc = SC_HOME;
    } else if (id == PI_SIZE) {
        s_big = v;
        if (s_sc == SC_ARTICLE && s_doc.nw) {
            float k = s_doc_h ? s_scroll / s_doc_h : 0;
            layout_doc();
            s_scroll = k * s_doc_h;
            clamp_scroll();
        }
    } else if (id == PI_STOPRD) {
        speech_read_close();
        s_rd_entry = ZIM_NONE;
    } else if (id == PI_LANG) {
        s_lang = v;
        file_again();
#if BOARD_UI_TABLET
        if (s_sc == SC_ARTICLE && s_doc.nw) {
            float k = s_doc_h ? s_scroll / s_doc_h : 0;
            layout_doc();
            s_scroll = k * s_doc_h;
            clamp_scroll();
        }  // (the pane goes to the other side)
#endif
    }
}
static const pitems_t PANEL = {ITEMS, 6, item_shown, NULL, item_set, NULL, NULL};

// ---------------------------------------------------------------- enter, leave, update
#include "wk_talk.inc"  // (THE TALK KEY: "look up Paris")

static void enter(int64_t now)
{
    s_now = now;
    layout_board();
    s_lang = store_get("wiki_lang", 1) ? 1 : 0;
    s_big = (int)store_get("wiki_big", 0) ? 1 : 0;
    s_nhist = 0;
    s_entry = ZIM_NONE;
    s_pending = ZIM_NONE;
    s_msg[0] = 0;
    s_bar_down = s_home_down = -1;
    s_query[0] = 0;
    s_nres = 0;
    memset(&s_doc, 0, sizeof s_doc);
    s_ln = NULL;
    s_rd_entry = (uint32_t)store_get("wiki_rd", (int32_t)ZIM_NONE);
    sysui_panel_items(&PANEL);
    file_open();
    listen_handler(wt_heard);
    s_last = ZIM_NONE;
    if (s_z && store_get("wiki_lastn", 0) == zim_entry_count(s_z)) {
        uint32_t l = store_get("wiki_last", ZIM_NONE);
        zim_entry_t e;
        if (l < zim_entry_count(s_z) && zim_entry(s_z, l, &e)) {
            s_last = l;
            strlcpy(s_last_title, e.title, sizeof s_last_title);
        }
    }
}

static void leave(void)
{
    listen_handler(NULL);
#if BOARD_UI_TABLET
    pics_free();
#endif
    doc_free(&s_doc);
    free(s_ln);
    s_ln = NULL;
    zim_close(s_z);
    s_z = NULL;
}

static void update(int64_t now)
{
    float dt = (float)(now - s_now) / 1000.0f;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    s_now = now;
    wt_lean();
    if (s_pending != ZIM_NONE && --s_pending_frames <= 0) {
        load_now();
    }
#if BOARD_UI_TABLET
    else {
        pics_step();
    }
#endif
    if (s_sc == SC_ARTICLE && s_vel != 0) {  // the scroll goes on after the finger (it slows down)
        s_scroll += s_vel * dt;
        s_vel *= 1.0f - 4.0f * dt;
        if (s_vel > -8 && s_vel < 8) {
            s_vel = 0;
        }
        clamp_scroll();
    }
    if (s_sc == SC_RESULTS) {
        float mx = (float)(res_rows_h() - (s_bot - s_top));
        if (s_rscroll > mx) {
            s_rscroll = mx > 0 ? mx : 0;
        }
        if (s_rscroll < 0) {
            s_rscroll = 0;
        }
    }
}

// ---------------------------------------------------------------- the input
static bool s_drag;
static int s_drag_y0, s_last_y;
static float s_drag_s0;
static int64_t s_last_t;

static void open_link_at(int x, int y)
{
    int32_t dy = (int32_t)(y - s_top + s_scroll);
    for (uint32_t li = 0; li < s_nln; li++) {
        wline_t *l = &s_ln[li];
        if (dy < l->y - 4 || dy >= l->y + l->h + 4) {
            continue;
        }
        // the nearest link word of the line (a fat finger: up to 24 px from the word)
        int best = 1 << 30;
        uint16_t link = 0;
        for (uint32_t i = l->w0; i < l->w0 + l->nw; i++) {
            const wword_t *w = &s_doc.w[i];
            if (!w->link) {
                continue;
            }
            int d = x < w->x ? w->x - x : x > w->x + w->w ? x - (w->x + w->w) : 0;
            if (d < best) {
                best = d;
                link = w->link;
            }
        }
        if (link && best <= 24) {
            uint32_t idx = zim_find_url(s_z, 'C', link_url(&s_doc, link));
            if (idx == ZIM_NONE) {
                idx = zim_find_url(s_z, 'A', link_url(&s_doc, link));
            }
            if (idx != ZIM_NONE) {
                sfx_ui(SFX_CLICK);
                open_entry(idx, true, 0);
            } else {
                sfx_ui(SFX_BACK);
            }
        }
        return;
    }
}

static void input(const input_event_t *ev)
{
    if (s_sc == SC_KBD) {
        int r = kbd_input(ev);
        if (r == KBD_OK) {
            search_run();
            s_sc = SC_RESULTS;
        } else if (r == KBD_CANCEL) {
            s_sc = s_entry != ZIM_NONE ? SC_ARTICLE : SC_HOME;
        }
        return;
    }
    if (s_sc == SC_NOFILE) {
        return;
    }
#if BOARD_UI_TABLET
    if (s_bigpic >= 0) {  // (the picture shows big: a tap closes it)
        if (ev->kind == IN_TAP) {
            sfx_ui(SFX_BACK);
            free(s_big_px);
            s_big_px = NULL;
            s_bigpic = -1;
        }
        return;
    }
#endif
    if (s_pending != ZIM_NONE) {
        return;
    }
    int x = ev->x, y = ev->y;
    switch (ev->kind) {
    case IN_DOWN:
        s_bar_down = bar_at(x, y);
        s_home_down = s_sc == SC_HOME ? home_at(x, y) : -1;
        s_drag = false;
        s_drag_y0 = s_last_y = y;
        s_drag_s0 = s_sc == SC_RESULTS ? s_rscroll : s_scroll;
        s_last_t = ev->t_ms;
        s_vel = 0;
        return;
    case IN_MOVE:
        if (s_bar_down >= 0 || s_home_down >= 0) {
            return;
        }
        if (!s_drag && abs(y - s_drag_y0) > 8) {
            s_drag = true;
            input_tap_cancel();
        }
        if (s_drag) {
            float s = s_drag_s0 - (float)(y - s_drag_y0);
            if (s_sc == SC_RESULTS) {
                s_rscroll = s;
            } else if (s_sc == SC_ARTICLE) {
                if (ev->t_ms > s_last_t) {
                    s_vel = (float)(s_last_y - y) * 1000.0f / (float)(ev->t_ms - s_last_t);
                }
                s_scroll = s;
                clamp_scroll();
            }
            s_last_y = y;
            s_last_t = ev->t_ms;
        }
        return;
    case IN_UP:
        if (s_drag && s_sc == SC_ARTICLE && ev->t_ms - s_last_t > 80) {
            s_vel = 0;  // (the finger stopped first)
        }
        s_drag = false;
        s_home_down = -1;
        if (s_bar_down >= 0 && bar_at(x, y) != s_bar_down) {
            s_bar_down = -1;
        }
        return;
    case IN_TAP:
        break;
    default:
        return;
    }
    // a tap
    int bar = bar_at(x, y);
    if (bar >= 0 && s_sc != SC_HOME && bar == s_bar_down) {
        s_bar_down = -1;
        sfx_ui(SFX_CLICK);
        if (bar == 0) {
            search_open();
        } else if (s_sc == SC_RESULTS) {
            s_sc = s_entry != ZIM_NONE ? SC_ARTICLE : SC_HOME;
        } else {
            go_back();
        }
        return;
    }
    s_bar_down = -1;
    if (s_sc == SC_HOME) {
        int b = home_at(x, y);
        if (b == HB_SEARCH) {
            search_open();
        } else if (b == HB_RANDOM) {
            open_random();
        } else if (b == HB_CONT && s_last != ZIM_NONE) {
            open_entry(s_last, false, 0);
        }
        if (b >= 0) {
            sfx_ui(SFX_CLICK);
        }
    } else if (s_sc == SC_RESULTS) {
        for (int i = 0; i < s_nres; i++) {
            int cx, cy, cw, ch;
            res_cell(i, &cx, &cy, &cw, &ch);
            if (x >= cx && x < cx + cw && y >= cy && y < cy + ch && y >= s_top) {
                sfx_ui(SFX_CLICK);
                open_entry(s_res[i], true, 0);
                return;
            }
        }
    } else if (s_sc == SC_ARTICLE && y >= s_top) {
        if (rd_hit(x, y)) {
            sfx_ui(SFX_CLICK);
            rd_tap();
            return;
        }
#if BOARD_UI_TABLET
        if (pane_tap(x, y)) {
            return;
        }
        int pi = pic_at(x, y);
        if (pi >= 0) {  // (a picture: big)
            sfx_ui(SFX_CLICK);
            big_open(pi);
            return;
        }
#endif
        open_link_at(x, y);
    }
}

// ---------------------------------------------------------------- the drawing
static void draw_center_lines(const char *a, const char *b, uint16_t ca)
{
    if (gfx_round()) {  // (the lines follow the circle)
        const int y = rl_para(&KF_BIG, a, s_h / 2 - 6, 30, 30, s_h, ca);
        if (b && b[0]) {
            rl_para(&KF_TEXT, b, y + 4, 24, 30, s_h, rgb(170, 180, 210));
        }
        return;
    }
    ui_text_c(&KF_BIG, s_w / 2, s_h / 2 - 6, a, ca);
    if (b && b[0]) {
        ui_text_c(&KF_NAME, s_w / 2, s_h / 2 + 24, b, rgb(170, 180, 210));
    }
}

#if BOARD_UI_TABLET
// ---------------------------------------------------------------- the CONTENTS pane (the Tab5)
#define PANE_MAX 64
// the first lines of the H2 headings of the page; the count
static int pane_heads(uint32_t *li, int max)
{
    int n = 0;
    for (uint32_t i = 0; i < s_nln && n < max; i++) {
        if (s_ln[i].blk == BK_H2 && s_ln[i].first) {
            li[n++] = i;
        }
    }
    return n;
}
// the lit row: 0 = the start of the page, k = the heading k - 1 (the last heading at the top of the screen or above it)
static int pane_cur(const uint32_t *li, int n)
{
    int c = 0;
    for (int k = 0; k < n; k++) {
        if (s_ln[li[k]].y <= (int32_t)s_scroll + 12) {
            c = k + 1;
        }
    }
    return c;
}
// the first row in the pane: the lit row stays in it
static int pane_first(int rows, int cur)
{
    int vis = (s_h - PANE_Y0) / PANE_RH, f = cur - vis / 2;
    if (f > rows - vis) {
        f = rows - vis;
    }
    if (f < 0) {
        f = 0;
    }
    return f;
}
static int pane_x(void)
{
    return s_lang ? s_w - PANE_W : 0;
}
// the words of one line in their order (a heading)
static void line_text(uint32_t li, char *out, int max)
{
    const wline_t *l = &s_ln[li];
    char b[96];
    int o = 0;
    out[0] = 0;
    for (uint32_t i = l->w0; i < l->w0 + l->nw; i++) {
        word_text(&s_doc.w[i], b, sizeof b);
        int need = (int)strlen(b) + 1;
        if (o + need >= max) {
            break;
        }
        if (o && !(s_doc.w[i].flags & WF_GLUE)) {
            out[o++] = ' ';
        }
        memcpy(out + o, b, strlen(b) + 1);
        o += (int)strlen(b);
    }
}
static void trim_to(const kfont_t *f, char *t, int w)
{
    while (ui_text_w(f, t) > w && strlen(t) > 2) {
        size_t l = strlen(t) - 1;
        while (l > 0 && ((unsigned char)t[l] & 0xC0) == 0x80) {
            l--;
        }
        t[l] = 0;
    }
}
static void draw_pane(void)
{
    uint32_t li[PANE_MAX];
    int n = pane_heads(li, PANE_MAX), cur = pane_cur(li, n), first = pane_first(n + 1, cur);
    int px = pane_x();
    gfx_fill_rect(px, s_top, PANE_W, s_h - s_top, rgb(17, 21, 36));
    gfx_fill_rect(s_lang ? px : px + PANE_W - 1, s_top, 1, s_h - s_top, rgb(54, 64, 102));
    const char *head = T("CONTENTS", "תוכן העניינים");
    int hw = ui_text_w(&KF_LABEL, head);
    txt_at(&KF_LABEL, s_lang ? px + PANE_W - 14 - hw : px + 14, s_top + 22, head, rgb(140, 150, 190));
    char t[128], m[128];
    for (int r = first; r <= n; r++) {
        int y = PANE_Y0 + (r - first) * PANE_RH;
        if (y + PANE_RH > s_h) {
            break;
        }
        bool lit = r == cur;
        if (lit) {
            ui_rrect(px + 6, y + 2, PANE_W - 12, PANE_RH - 4, 8, rgb(38, 52, 96));
            gfx_fill_rect(s_lang ? px + PANE_W - 9 : px + 6, y + 8, 3, PANE_RH - 16, rgb(250, 215, 30));
        }
        if (r == 0) {
            snprintf(t, sizeof t, "%s", T("(THE START)", "פתיחה"));
        } else {
            line_text(li[r - 1], t, sizeof t);
        }
        const kfont_t *f = ui_text_w(&KF_NAME, t) <= PANE_W - 30 ? &KF_NAME : &KF_LABEL;
        trim_to(f, t, PANE_W - 30);
        rtl_text(t, m, sizeof m);
        int tw = ui_text_w(f, m);
        bool rtl = r ? para_rtl(s_ln[li[r - 1]].w0, s_ln[li[r - 1]].w0 + s_ln[li[r - 1]].nw) : s_lang != 0;
        txt_at(f, rtl ? px + PANE_W - 16 - tw : px + 16, y + PANE_RH / 2 + f->asc / 2 - 1, m,
               lit ? rgb(250, 220, 120) : rgb(200, 208, 230));
    }
}
// a tap in the pane: true when the tap was in it (the article goes to the heading of the row)
static bool pane_tap(int x, int y)
{
    if (!s_pane || x < pane_x() || x >= pane_x() + PANE_W) {
        return false;
    }
    if (y < PANE_Y0) {
        return true;
    }
    uint32_t li[PANE_MAX];
    int n = pane_heads(li, PANE_MAX), first = pane_first(n + 1, pane_cur(li, n));
    int r = first + (y - PANE_Y0) / PANE_RH;
    if (r > n) {
        return true;
    }
    s_scroll = r ? (float)(s_ln[li[r - 1]].y - 4) : 0;
    s_vel = 0;
    clamp_scroll();
    sfx_ui(SFX_CLICK);
    return true;
}
#endif

// right to left: the mirror of each bracket of the text
static void mirror_brackets(char *buf)
{
    for (char *q = buf; *q; q++) {
        static const char MIR[] = "()[]{}<>";
        const char *m = strchr(MIR, *q);
        if (m) {
            *q = MIR[(m - MIR) ^ 1];
        }
    }
}

static void draw_article(void)
{
    char buf[256];
    gfx_fill_rect(0, s_top, s_w, s_h - s_top, rgb(12, 14, 24));
    if (s_msg[0]) {
        draw_center_lines(s_msg, s_ptitle, rgb(255, 160, 120));
        return;
    }
    int32_t top = (int32_t)s_scroll;
    for (uint32_t li = 0; li < s_nln; li++) {
        const wline_t *l = &s_ln[li];
        int32_t y = l->y - top + s_top;
        if (y + l->h < s_top) {
            continue;
        }
        if (y > s_bot) {
            break;
        }
#if BOARD_UI_TABLET
        if (l->blk == BK_PIC) {
            draw_pic(l, y);
            continue;
        }
#endif
        const kfont_t *f = l->blk == BK_H2 || l->blk == BK_H3 ? head_font(l->blk) : body_font();
        int base = y + f->asc;
        if (l->blk == BK_LI && l->first) {  // (a bullet at the first line of each list item)
            bool rtl = para_rtl(l->w0, l->w0 + l->nw);
            ui_circle(rtl ? AR - 8 : AL + 8, base - f->asc / 3, 3, rgb(150, 170, 220), 256);
        }
        if (l->blk == BK_H2) {
            gfx_fill_rect(AL, y + l->h - 2, AR - AL, 1, rgb(70, 80, 120));
        }
        for (uint32_t i = l->w0; i < l->w0 + l->nw; i++) {
            const wword_t *w = &s_doc.w[i];
            word_text(w, buf, sizeof buf);
            if ((w->flags & WF_NEUT) && !(w->flags & WF_LTR) && para_rtl(l->w0, l->w0 + l->nw)) {
                mirror_brackets(buf);
            }
            uint16_t col = l->blk == BK_H2 || l->blk == BK_H3 ? rgb(250, 215, 120)
                           : w->link                          ? rgb(120, 180, 255)
                           : (w->flags & WF_BOLD)             ? rgb(255, 255, 255)
                                                              : rgb(214, 220, 236);
            ui_text_c(f, w->x + w->w / 2, base, buf, col);
        }
    }
    // the end of the page
    int32_t ey = s_doc_h - 36 - top + s_top;
    if (ey < s_bot && s_doc.cut) {
        ui_text_c(&KF_LABEL, (AL + AR) / 2, ey + 14,
                  T("(THE PAGE IS LONG: ITS END IS NOT HERE)", "הדף ארוך: הסוף שלו לא כאן"), rgb(170, 150, 120));
    }
    // the scroll bar
    if (s_doc_h > view_h()) {
        int vh = view_h(), bh = vh * vh / s_doc_h;
        if (bh < 16) {
            bh = 16;
        }
        int by = s_top + (int)((float)(vh - bh) * s_scroll / (float)(s_doc_h - vh));
        if (gfx_round()) {
            rl_bar(by - s_top, bh, vh, rgb(40, 48, 80), rgb(90, 100, 150));
            return;
        }
#if BOARD_UI_TABLET
        gfx_fill_rect(s_pane && s_lang ? s_w - PANE_W - 8 : s_w - 6, by, 4, bh,
                      rgb(90, 100, 150));  // (beside the pane)
#else
        gfx_fill_rect(s_w - 4, by, 3, bh, rgb(90, 100, 150));
#endif
    }
#if BOARD_UI_TABLET
    if (s_pane) {
        draw_pane();
    }
#endif
}

static void draw_home(void)
{
    gfx_fill(rgb(12, 14, 24));
    int ty = s_w >= 3 * s_h ? 36 : (s_h >= 300 ? 70 : 44);
    if (gfx_round()) {
        rl_disc(rgb(12, 14, 24), rgb(60, 70, 110));
        ty = s_w * 118 / 466;
    }
    ui_text_c(&KF_TITLE, s_w / 2, ty, T("WIKIPEDIA", "ויקיפדיה"), rgb(250, 220, 120));
    char line[96];
    snprintf(line, sizeof line, s_lang ? "%u כותרים - %s" : "%u titles - %s", (unsigned)s_ntitles, s_file);
    if (ui_text_w(&KF_LABEL, line) > s_w - 2 * s_margin) {
        snprintf(line, sizeof line, s_lang ? "%u כותרים" : "%u titles", (unsigned)s_ntitles);
    }
    ui_text_c(&KF_LABEL, s_w / 2, ty + 24, line, rgb(150, 160, 200));
    int n = s_last != ZIM_NONE ? 3 : 2;
    for (int i = 0; i < n; i++) {
        int bx, by, bw, bh;
        home_btn(i, &bx, &by, &bw, &bh);
        bool hot = s_home_down == i;
#if BOARD_UI_TABLET
        if (i == HB_SEARCH) {
            search_field(bx, by, bw, bh, hot, T("SEARCH WIKIPEDIA", "חיפוש בוויקיפדיה"));
            continue;
        }
#endif
        uint16_t bg = hot              ? rgb(250, 215, 30)
                      : i == HB_SEARCH ? rgb(40, 110, 180)
                      : i == HB_RANDOM ? rgb(60, 70, 120)
                                       : rgb(40, 120, 80);
        ui_rrect(bx, by, bw, bh, 12, bg);
        const char *lab = i == HB_SEARCH   ? T("SEARCH", "חיפוש")
                          : i == HB_RANDOM ? T("RANDOM", "אקראי")
                                           : T("CONTINUE", "המשך");
        uint16_t fg = hot ? rgb(0, 0, 0) : rgb(255, 255, 255);
        if (i == HB_CONT) {
            ui_text_c(&KF_BIG, bx + bw / 2, by + bh / 2 - 2, lab, fg);
            char t[64];
            snprintf(t, sizeof t, "%s", s_last_title);
            while (ui_text_w(&KF_LABEL, t) > bw - 12 && strlen(t) > 4) {
                size_t l = strlen(t) - 1;
                while (l && ((unsigned char)t[l] & 0xC0) == 0x80) {
                    l--;
                }
                t[l] = 0;
            }
            char m[64];
            ui_text_c(&KF_LABEL, bx + bw / 2, by + bh / 2 + 18, rtl_text(t, m, sizeof m), fg);
        } else {
            ui_text_c(&KF_BIG, bx + bw / 2, by + bh / 2 + 9, lab, fg);
        }
    }
}

static void draw_results(void)
{
    gfx_fill_rect(0, s_top, s_w, s_h - s_top, rgb(12, 14, 24));
    if (!s_nres) {
        draw_center_lines(T("NO TITLE STARTS WITH THIS TEXT", "אין כותר שמתחיל כך"), s_query, rgb(255, 180, 120));
        return;
    }
    if (!s_exact) {
        ui_text_c(&KF_LABEL, s_w / 2, s_top + 14, T("NO EXACT START: THE NEAREST TITLES", "אין התאמה: הכותרים הקרובים"),
                  rgb(255, 190, 120));
    }
    for (int i = 0; i < s_nres; i++) {
        int x, y, w, h;
        res_cell(i, &x, &y, &w, &h);
        if (!s_exact) {
            y += 18;
        }
        if (y + h < s_top || y > s_bot) {
            continue;
        }
        ui_rrect(x, y, w, h, 8, rgb(30, 38, 66));
        static const kfont_t *const FS[] = {&KF_NAME, &KF_LABEL};
        const kfont_t *f = wk_fit(FS, 2, s_res_title[i], w - 12);
        char m[64];
        ui_text_c(f, x + w / 2, y + h / 2 + f->asc / 2 - 1, rtl_text(s_res_title[i], m, sizeof m), rgb(225, 232, 250));
    }
}

static void render(int64_t now)
{
    s_now = now;
    if (s_sc == SC_KBD) {
        kbd_render(now);
        return;
    }
    if (s_sc == SC_NOFILE) {
        gfx_fill(rgb(12, 14, 24));
        if (gfx_round()) {
            rl_disc(rgb(12, 14, 24), rgb(60, 70, 110));
            ui_text_c(&KF_TITLE, s_w / 2, s_w * 118 / 466, T("WIKIPEDIA", "ויקיפדיה"), rgb(250, 220, 120));
        }
        draw_center_lines(
                s_err[0] ? T("THE FILE DID NOT OPEN", "הקובץ לא נפתח") : T("NO WIKIPEDIA FILE", "אין קובץ ויקיפדיה"),
                s_err[0] ? s_err : T("PUT A .ZIM FILE IN /sdcard/wiki/", "שים קובץ ZIM בתיקייה /sdcard/wiki/"),
                rgb(255, 200, 120));
        return;
    }
#if BOARD_UI_TABLET
    if (s_bigpic >= 0 && s_sc == SC_ARTICLE) {
        draw_big();
        return;
    }
#endif
    if (s_sc == SC_HOME) {
        draw_home();
    } else if (s_sc == SC_RESULTS) {
        draw_results();
        draw_bar(s_query);
    } else {
        draw_article();
        draw_bar(s_ptitle);
        rd_draw();
    }
    if (s_pending != ZIM_NONE) {
        int w = s_w >= 600 ? 220 : 160;
        ui_rrect(s_w / 2 - w / 2, s_h / 2 - 24, w, 48, 12, rgb(40, 50, 90));
        ui_text_c(&KF_BIG, s_w / 2, s_h / 2 + 9, T("LOADING", "טוען"), rgb(255, 255, 255));
    }
}

#include "tficon/tficon.h"
// the menu icon: a globe of knowledge turns over an open book
static void icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_store();
    tfi_tile(x, y, s, tfi_hex(0x7f8cff), tfi_hex(0x232a7a));
    float u = s / 100.0f, ph = tfi_ph(2);
    // the book
    float bk[8] = {x + 14 * u, y + 72 * u, x + 50 * u, y + 78 * u, x + 50 * u, y + 88 * u, x + 14 * u, y + 82 * u};
    float bk2[8] = {x + 50 * u, y + 78 * u, x + 86 * u, y + 72 * u, x + 86 * u, y + 82 * u, x + 50 * u, y + 88 * u};
    tfi_convex(bk, 4, tfi_hex(0xfff8ec), 256);
    tfi_convex(bk2, 4, tfi_hex(0xe8dcc6), 256);
    tfi_line(x + 14 * u, y + 82 * u, x + 50 * u, y + 88 * u, 2 * u, false, tfi_hex(0xb03a2a), 256);
    tfi_line(x + 50 * u, y + 88 * u, x + 86 * u, y + 82 * u, 2 * u, false, tfi_hex(0xb03a2a), 256);
    // the globe: the sea, the lines of longitude that turn, the lines of latitude
    float cx = x + 50 * u, cy = y + 42 * u, rad = 28 * u;
    tfi_drop_disc(cx, cy, rad, 3 * u, 80);
    tfi_disc(cx, cy, rad, tfi_hex(0x7fd6ff), tfi_hex(0x1666c8), 256);
    uint16_t ln = rgb(255, 255, 255);
    for (int k = 0; k < 4; k++) {
        float q = ph + k / 4.0f;
        q -= (float)(int)q;
        float a = q * 3.14159f;  // (a meridian at this angle: x = rad cos(a) sin(t))
        float sa = fsin(a);
        if (sa < 0.02f) {
            continue;
        }
        float c = fcos(a);
        float px = cx + rad * c, py = cy - rad * 0;
        (void)px;
        (void)py;
        float lx0 = cx, ly0 = cy - rad;
        for (int j = 1; j <= 12; j++) {
            float t = j / 12.0f * 3.14159f;
            float lx1 = cx + rad * c * fsin(t), ly1 = cy - rad * fcos(t);
            tfi_line(lx0, ly0, lx1, ly1, 1.6f * u, false, ln, sa > 0.6f ? 200 : 110);
            lx0 = lx1;
            ly0 = ly1;
        }
    }
    for (int k = -1; k <= 1; k++) {
        float yy = cy + k * 13 * u, hw = sqrtf(rad * rad - (yy - cy) * (yy - cy));
        tfd_rect((int)(cx - hw + 1 * u), (int)(yy - 0.8f * u), (int)(2 * hw - 2 * u),
                 (int)(1.6f * u) > 1 ? (int)(1.6f * u) : 1, ln, 150);
    }
    tfi_ring(cx, cy, rad - 0.8f * u, 1.6f * u, ln, 120);
    tfi_spark(cx - 10 * u, cy - 12 * u, 5 * u, 90);
    tfi_end();
}

static bool awake(void)
{
    return s_pending != ZIM_NONE;
}

const app_t app_wiki = {
        .name = "WIKI",
        .landscape = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
        .round = APP_ROUND_HI,  // (the round boards: the native round frame and the layout of layout_board)
};
