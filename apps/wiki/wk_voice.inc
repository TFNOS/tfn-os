// WIKI: READ ALOUD (tfn/speech.h: the voice of the board, the app VOICE). Only app_wiki.c includes this file.
// An English article (a file of the English Wikipedia: "_en_" in its name) on a board with the voice: a key at the
// bottom of the article starts THE READER of TFN OS with the text of the article, one sentence a line (a group in
// brackets with the sounds of a name, the IPA, goes out). The voice is slower than real time, so the key shows
// PREPARING and the time until the play; then a tap pauses or plays again. The reading goes on when the app closes
// (the panel item STOP READING stops it). The text: /sdcard/voice/wiki/text.txt; the voice of each article stays in
// /sdcard/voice/wiki/e<entry> (the next time the article plays at once).
#define RD_DIR  "/sdcard/voice/wiki"
#define RD_TEXT RD_DIR "/text.txt"
#define RD_LINE 300   // bytes of a line at most (a longer sentence goes in parts)
#define RD_BLK  8192  // bytes of the text of a block at most
#define RD_HOLD 40    // the play waits for at most this much voice ahead (s)
#ifdef _WIN32
#define wk_mkdir(p) mkdir(p)  // (the PC tools: the mkdir of Windows has no mode)
#else
#define wk_mkdir(p) mkdir(p, 0777)
#endif
static uint32_t s_rd_entry = ZIM_NONE;  // the article of the reader (store wiki_rd)
static char s_rd_a[RD_BLK], s_rd_b[RD_BLK];
static bool s_rd_bad;  // a write of the text file failed

// (fputc and ferror are not names of the app ABI)
static void rd_out(FILE *f, const char *s, size_t n)
{
    s_rd_bad |= fwrite(s, 1, n, f) != n;
}

static bool rd_avail(void)
{
    return s_sc == SC_ARTICLE && !s_msg[0] && s_doc.nw && s_zlang == 0 && s_entry != ZIM_NONE && speech_ready();
}

// the reader reads this article now (its state in *st)
static bool rd_mine(speech_read_t *st)
{
    speech_read_get(st);
    return st->state != SPR_OFF && s_entry != ZIM_NONE && s_rd_entry == s_entry;
}

// a group in brackets that is not for the ear: the sounds of a name (the IPA letters U+0250..U+02FF), a slash
static bool rd_bad_group(const char *a, const char *b)
{
    for (const char *p = a; p < b; p++) {
        const uint8_t c = (uint8_t)*p;
        if (c == '/' || c == 0xC9 || c == 0xCA || c == 0xCB) {
            return true;
        }
    }
    return false;
}

// the text t (n bytes) with no bad groups into o; its length
static int rd_clean(const char *t, int n, char *o, int max)
{
    int k = 0;
    for (int i = 0; i < n && k < max - 1;) {
        if (t[i] == '(') {
            int d = 0, j = i;
            for (; j < n; j++) {
                d += t[j] == '(';
                d -= t[j] == ')';
                if (!d) {
                    break;
                }
            }
            if (j < n && rd_bad_group(t + i, t + j)) {
                while (k > 0 && o[k - 1] == ' ') {
                    k--;
                }
                i = j + 1;
                continue;
            }
        }
        o[k++] = t[i++];
    }
    o[k] = 0;
    return k;
}

// the word before the dot at s[end] is a short form (Mr., J., etc.): the sentence goes on
static bool rd_abbrev(const char *s, int end)
{
    static const char *const AB[] = {"Mr",  "Mrs", "Ms",   "Dr",  "St",  "Jr",  "Sr",  "vs",  "etc", "e.g",
                                     "i.e", "No",  "Mt",   "ca",  "U.S", "Inc", "Co",  "Ltd", "Gen", "Col",
                                     "Lt",  "Rev", "Prof", "Jan", "Feb", "Mar", "Apr", "Aug", "Sep", "Sept",
                                     "Oct", "Nov", "Dec",  "c",   "b",   "d",   "fl"};
    int a = end;
    while (a > 0 && s[a - 1] != ' ' && s[a - 1] != '(') {
        a--;
    }
    const int len = end - a;
    if (len <= 1) {
        return true;  // (one letter: J. Smith)
    }
    for (size_t i = 0; i < sizeof AB / sizeof AB[0]; i++) {
        if ((int)strlen(AB[i]) == len && !strncmp(s + a, AB[i], (size_t)len)) {
            return true;
        }
    }
    return false;
}

// one sentence into the file (parts of RD_LINE bytes at most: at a comma, else at a space); dot: a period at its end
static void rd_put(FILE *f, const char *s, int n, bool dot)
{
    while (n > 0 && *s == ' ') {
        s++;
        n--;
    }
    while (n > 0 && s[n - 1] == ' ') {
        n--;
    }
    while (n > RD_LINE) {
        int cut = -1;
        for (int i = RD_LINE; i > RD_LINE / 3 && cut < 0; i--) {
            cut = s[i] == ',' || s[i] == ';' ? i + 1 : -1;
        }
        for (int i = RD_LINE; i > RD_LINE / 3 && cut < 0; i--) {
            cut = s[i] == ' ' ? i : -1;
        }
        cut = cut < 0 ? RD_LINE : cut;
        while (cut > 1 && ((uint8_t)s[cut] & 0xC0) == 0x80) {
            cut--;  // (not inside a UTF-8 letter)
        }
        rd_out(f, s, (size_t)cut);
        rd_out(f, "\n", 1);
        s += cut;
        n -= cut;
        while (n > 0 && *s == ' ') {
            s++;
            n--;
        }
    }
    if (n <= 0) {
        return;
    }
    rd_out(f, s, (size_t)n);
    if (dot && !strchr(".!?:;", s[n - 1])) {
        rd_out(f, ".", 1);  // (a heading or a list item: the voice stops at its end)
    }
    rd_out(f, "\n", 1);
}

// the text of a block (t, n bytes) into sentences
static void rd_block(FILE *f, const char *t, int n)
{
    int a = 0;
    for (int i = 0; i < n; i++) {
        const char c = t[i];
        if ((c == '.' || c == '!' || c == '?') && (i + 1 == n || t[i + 1] == ' ') && !(c == '.' && rd_abbrev(t, i))) {
            rd_put(f, t + a, i + 1 - a, false);
            a = i + 1;
        }
    }
    rd_put(f, t + a, n - a, true);
}

// the text of the article for the voice: the title, then each block; false: no card, a write fault
static bool rd_write(void)
{
    wk_mkdir("/sdcard/voice");
    wk_mkdir(RD_DIR);
    FILE *f = fopen(RD_TEXT, "wb");
    if (!f) {
        return false;
    }
    s_rd_bad = false;
    rd_put(f, s_ptitle, (int)strlen(s_ptitle), true);
    for (uint32_t b = 0; b < s_doc.nblk; b++) {
        const uint32_t w0 = s_doc.blk_first[b], w1 = b + 1 < s_doc.nblk ? s_doc.blk_first[b + 1] : s_doc.nw;
        int n = 0;
        for (uint32_t i = w0; i < w1 && n < RD_BLK - 300; i++) {
            const wword_t *w = &s_doc.w[i];
            if (n && !(w->flags & WF_GLUE)) {
                s_rd_a[n++] = ' ';
            }
            const int l = w->len < 256 ? w->len : 256;
            memcpy(s_rd_a + n, s_doc.tx + w->off, (size_t)l);
            n += l;
        }
        rd_block(f, s_rd_b, rd_clean(s_rd_a, n, s_rd_b, RD_BLK));
    }
    fclose(f);
    return !s_rd_bad;
}

static void rd_start(void)
{
    if (!rd_write()) {
        ESP_LOGI(TAG, "read aloud: no text file");
        return;
    }
    zim_drop_cache(s_z);  // (the article is in s_doc; the voice model needs one PSRAM block of 2.7 MB)
    char cache[48];
    snprintf(cache, sizeof cache, RD_DIR "/e%lu", (unsigned long)s_entry);
    speech_read_cfg_t c = {.text = RD_TEXT, .cache = cache, .hold_s = RD_HOLD, .play = true};
    if (speech_read_open(&c)) {
        s_rd_entry = s_entry;
        store_set("wiki_rd", (int32_t)s_entry);
    }
}

// the key: at the bottom right of the article (the round frame: at the bottom of the view, in the middle)
static void rd_box(int *x, int *y, int *w, int *h)
{
    *h = s_h >= 300 || gfx_round() ? 44 : 34;
    *w = gfx_round() ? s_w * 50 / 100 : s_w >= 600 ? 250 : 210;
    *x = gfx_round() ? (s_w - *w) / 2 : s_w - *w - 10;
    *y = (gfx_round() ? s_bot : s_h) - *h - 8;
}

static bool rd_hit(int x, int y)
{
    int bx, by, bw, bh;
    rd_box(&bx, &by, &bw, &bh);
    return rd_avail() && x >= bx - 6 && x < bx + bw + 6 && y >= by - 6 && y < by + bh + 6;
}

static void rd_tap(void)
{
    speech_read_t st;
    if (!rd_mine(&st) || st.state == SPR_END || st.state == SPR_FAULT) {
        rd_start();
    } else {
        speech_read_play(!st.play);
    }
}

static void rd_draw(void)
{
    if (!rd_avail()) {
        return;
    }
    int x, y, w, h;
    rd_box(&x, &y, &w, &h);
    speech_read_t st;
    const bool mine = rd_mine(&st);
    char b[64], m[64];
    uint16_t edge = rgb(250, 215, 30);
    if (!mine) {
        snprintf(b, sizeof b, "%s", T("READ ALOUD", "הקרא בקול"));
    } else if (st.state == SPR_WAIT) {
        const int s = (st.wait_ms + 999) / 1000;
        snprintf(b, sizeof b, "%s %d:%02d", T("PREPARING", "מכין"), s / 60, s % 60);
        edge = rgb(255, 160, 90);
    } else if (st.state == SPR_PLAY) {
        snprintf(b, sizeof b, "%s", T("READING: PAUSE", "מקריא: עצור"));
        edge = rgb(90, 200, 255);
    } else if (st.state == SPR_MAKE) {
        snprintf(b, sizeof b, "%s", T("PAUSED: PLAY", "בהשהיה: המשך"));
    } else if (st.state == SPR_END) {
        snprintf(b, sizeof b, "%s", T("READ AGAIN", "הקרא שוב"));
    } else {
        snprintf(b, sizeof b, "%s", T("NO VOICE DATA", "אין נתוני קול"));
        edge = rgb(255, 120, 120);
    }
    ui_rrect(x, y, w, h, h / 2, edge);
    ui_rrect(x + 2, y + 2, w - 4, h - 4, h / 2 - 2, rgb(20, 24, 42));
    if (mine && st.lines > 0 && st.state == SPR_WAIT) {  // (the lines with voice: a bar in the key)
        const int pw = (w - 2 * h / 2) * (st.made > st.lines ? st.lines : st.made) / st.lines;
        gfx_fill_rect(x + h / 2, y + h - 6, pw, 2, edge);
    }
    static const kfont_t *const FS[] = {&KF_NAME, &KF_LABEL};
    const kfont_t *f = wk_fit(FS, 2, b, w - h);
    ui_text_c(f, x + w / 2, y + h / 2 + f->asc / 2 - 1, rtl_text(b, m, sizeof m), rgb(240, 244, 255));
}
