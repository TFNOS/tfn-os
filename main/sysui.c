// The system layer over all apps (sysui.h): a message from the manager, the locate sound, the start picture, the speaker
// sweep. The web server task only puts a request in; the app task (sysui_update) takes it.
#include "sysui_priv.h"
#include "tfn/theme.h"
#include "../libs/bidi/bidi.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"  // (EXT_RAM_BSS_ATTR: the consent state of the question in PSRAM: the internal RAM is short)
#endif
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif

static const char *TAG = "sysui";
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------- the requests (from any task)
#define MSG_MAX 600
static char s_pend[MSG_MAX];
static volatile int s_pend_secs;
static volatile bool s_pend_new;
static volatile int s_loc_req = -1, s_loc_req_hz;
void sysui_message(const char *utf8, int secs)
{
    portENTER_CRITICAL(&s_mux);
    snprintf(s_pend, sizeof s_pend, "%s", utf8 ? utf8 : "");
    s_pend_secs = secs < 0 ? 0 : secs > 3600 ? 3600 : secs;
    s_pend_new = true;
    portEXIT_CRITICAL(&s_mux);
    app_screen_wake();
}
void sysui_locate(int secs, int hz)
{
    s_loc_req_hz = hz;
    s_loc_req = secs < 0 ? 0 : secs > 300 ? 300 : secs;
    app_screen_wake();
}

// ---------------------------------------------------------------- the state (the app task)
static char s_msg[MSG_MAX];
static bool s_msg_on, s_loc_on, s_was_on;
static uint16_t *s_snap;  // the frozen, darkened frame under the layer (PSRAM)
static int64_t s_msg_until, s_loc_until, s_loc_t0;
static int s_loc_hz;
// the default locate tone: the 3.49 speaker is loudest from about 900 to 1200 Hz (the microphone sweep);
// the other boards: not measured yet (the manager FIND THE LOUDEST TONE saves a tone for each board)
#if BOARD_BAR_SCREEN
#define LOC_HZ_DEF 1100
#else
#define LOC_HZ_DEF 2700
#endif

int sysui_locate_hz(void)
{
    int hz = (int)store_get("loc_hz", LOC_HZ_DEF);
    return hz < 200 || hz > 8000 ? LOC_HZ_DEF : hz;
}

// the question (a WALKIE call)
static bool s_ask_on;
#define ASK_TEXT 96  // the bytes of the line of a question (the permission line of the STORE: "IT CAN " + the list)
static char s_ask_t[48], s_ask_l[ASK_TEXT], s_ask_y[32], s_ask_n[32];  // (a Hebrew button: 2 bytes a letter)
static bool s_ask_radio;  // sysui_ask_radio(): radio waves in place of the first letter
static int64_t s_ask_until, s_ask_t0;
static void (*s_ask_cb)(int);
static bool s_ask_closable;  // sysui_ask_closable(): a tap out of the buttons or the PWR key closes it
// the consent state (PSRAM: the layer reads it only once in each frame or event)
static bool s_ask_finger EXT_RAM_BSS_ATTR;  // sysui_ask_finger(): the first button is a consent (the rule of input.h)
static bool s_ask_remote EXT_RAM_BSS_ATTR;  // a tap that is not by a finger came on the first button of the consent
static input_consent_t s_ask_cons EXT_RAM_BSS_ATTR;  // the consent rule of the first button (input.h)
void sysui_ask(const char *title, const char *line, const char *yes, const char *no, int secs, void (*done)(int))
{
    snprintf(s_ask_t, sizeof s_ask_t, "%s", title ? title : "");
    snprintf(s_ask_l, sizeof s_ask_l, "%s", line ? line : "");
    snprintf(s_ask_y, sizeof s_ask_y, "%s", yes ? yes : "YES");
    snprintf(s_ask_n, sizeof s_ask_n, "%s", no ? no : "NO");
    s_ask_t0 = esp_timer_get_time() / 1000;
    s_ask_until = secs > 0 ? s_ask_t0 + secs * 1000LL : 0;
    s_ask_cb = done;
    s_ask_closable = false;
    s_ask_finger = false;
    s_ask_remote = false;
    s_ask_radio = false;
    input_consent_reset(&s_ask_cons);
    s_ask_on = true;
    ESP_LOGI(TAG, "question: %s / %s", s_ask_t, s_ask_l);
}
void sysui_ask_finger(void)
{
    s_ask_finger = true;
}
void sysui_ask_radio(void)
{
    s_ask_radio = true;
}
bool sysui_ask_is(void (*done)(int))
{
    return s_ask_on && s_ask_cb == done;
}
// a tap on the first button of a consent that cannot answer (c: input_consent_event()): no answer. A tap that is not
// by a finger on the panel gives a sound and a note under the line of the question (ask_draw); a finger that came down
// too soon, or a tap of a touch whose IN_DOWN the question did not get, gives nothing (the next touch answers)
static void ask_refuse(int c)
{
    if (c != INPUT_CONSENT_NOT_FINGER) {
        ESP_LOGI(TAG,
                 "question: %s no answer (the touch came before the question or in its first %d ms, or its IN_DOWN "
                 "went to another part)",
                 s_ask_y, INPUT_ARM_MS);
        return;
    }
    s_ask_remote = true;
    sfx_ui(SFX_BACK);
    ESP_LOGW(TAG, "question: %s refused (not a finger on the panel)", s_ask_y);
}
static void ask_end(int k)
{
    if (!s_ask_on) {
        return;
    }
    s_ask_on = false;
    void (*cb)(int) = s_ask_cb;
    s_ask_cb = NULL;
    if (cb) {
        cb(k);
    }
}
void sysui_ask_close(void)
{
    ask_end(-1);
}
// The hold question of the menu must have a way out. A question that is not a
// call can close with a tap out of its buttons or the PWR key (done(-1)); a call keeps only its two buttons.
void sysui_ask_closable(void)
{
    s_ask_closable = true;
}
bool sysui_ask_back(void)
{
    if (!s_ask_on || !s_ask_closable) {
        return false;
    }
    ask_end(-1);
    return true;
}

static void locate_stop(void)
{
    if (!s_loc_on) {
        return;
    }
    s_loc_on = false;
    audio_drone(1, 0, 0, W_SQUARE);
    settings_apply();
    ESP_LOGI(TAG, "locate: off");
}
void sysui_update(int64_t now)
{
    walkie_call_update(now);  // the WALKIE calls (they can open a question)
    if (s_pend_new) {
        portENTER_CRITICAL(&s_mux);
        memcpy(s_msg, s_pend, sizeof s_msg);
        int secs = s_pend_secs;
        s_pend_new = false;
        portEXIT_CRITICAL(&s_mux);
        s_msg_on = s_msg[0] != 0;
        s_msg_until = secs ? now + secs * 1000LL : 0;
        ESP_LOGI(TAG, "message (%d s): %s", secs, s_msg);
    }
    if (s_loc_req >= 0) {
        int secs = s_loc_req;
        s_loc_req = -1;
        if (!secs) {
            locate_stop();
        } else {
            s_loc_hz = s_loc_req_hz > 0 ? s_loc_req_hz : sysui_locate_hz();
            s_loc_on = true;
            s_loc_t0 = now;
            s_loc_until = now + secs * 1000LL;
            board_speaker(true);
            audio_set_volume(100);
            ESP_LOGI(TAG, "locate: %d s at %d Hz", secs, s_loc_hz);
        }
    }
    if (s_msg_on && s_msg_until && now > s_msg_until) {
        s_msg_on = false;
    }
    if (s_ask_on && s_ask_until && now > s_ask_until) {
        ask_end(-1);
    }
    // the layer comes: a frozen, darkened copy of the frame (the app does not draw while the layer shows); the layer
    // goes: the menu repaints all (it repaints only what changes)
    bool on = s_msg_on || s_loc_on || s_ask_on;
    if (on && !s_was_on) {
#if BOARD_HI_FRAME
        gfx_hi_flatten();  // (a HI picture: first the normal layout)
#endif
        s_snap = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM);
        if (s_snap) {
            for (int i = 0, n = gfx_w * gfx_h; i < n; i++) {
                uint16_t v = (uint16_t)((g_fb[i] >> 8) | (g_fb[i] << 8));
                v = (uint16_t)(((v >> 2) & 0x39E7));  // a quarter of the light
                s_snap[i] = (uint16_t)((v >> 8) | (v << 8));
            }
        }
    } else if (!on && s_was_on) {
        heap_caps_free(s_snap);
        s_snap = NULL;
        menu_redraw_all();
    }
    s_was_on = on;
    if (s_loc_on) {
        if (now > s_loc_until) {
            locate_stop();
            return;
        }
        // two loud tones and a short gap, again and again: easy to follow by ear
        int ph = (int)((now - s_loc_t0) % 600);
        if (ph < 200) {
            audio_drone(1, (float)s_loc_hz, 100, W_SQUARE);
        } else if (ph < 260) {
            audio_drone(1, 0, 0, W_SQUARE);
        } else if (ph < 460) {
            audio_drone(1, (float)s_loc_hz * 0.75f, 100, W_SQUARE);
        } else {
            audio_drone(1, 0, 0, W_SQUARE);
        }
    }
}
bool sysui_active(void)
{
    return s_msg_on || s_loc_on || s_ask_on;
}
typedef struct {
    int x, y, w, h;
} sbox_t;
static void ask_geo(sbox_t *yes, sbox_t *no);
static bool in_sbox(const sbox_t *b, int x, int y)
{
    return x >= b->x - 8 && x < b->x + b->w + 8 && y >= b->y - 8 && y < b->y + b->h + 8;
}
// the distance of (x, y) to the box b in px (0 = in the box)
static int sbox_dist(const sbox_t *b, int x, int y)
{
    const int dx = x < b->x ? b->x - x : x >= b->x + b->w ? x - (b->x + b->w - 1) : 0;
    const int dy = y < b->y ? b->y - y : y >= b->y + b->h ? y - (b->y + b->h - 1) : 0;
    return dx > dy ? dx : dy;
}
// the button of a tap at (x, y): 1 = the first, 0 = the second, -1 = none. The margins of the two boxes meet in the gap
// between the buttons: there the nearer button takes the tap, and the second (safe) button takes the middle.
static int ask_button(int x, int y)
{
    sbox_t yes, no;
    ask_geo(&yes, &no);
    const bool in_y = in_sbox(&yes, x, y), in_n = in_sbox(&no, x, y);
    if (in_y && in_n) {
        return sbox_dist(&yes, x, y) < sbox_dist(&no, x, y) ? 1 : 0;
    }
    return in_y ? 1 : in_n ? 0 : -1;
}
bool sysui_input(const input_event_t *ev)
{
    if (!sysui_active()) {
        return false;
    }
    if (s_ask_on && !s_loc_on) {  // the question: only its two buttons
        const int c = input_consent_event(&s_ask_cons, ev);
        if (ev->kind == IN_TAP) {
            const int b = ask_button(ev->x, ev->y);
            if (b == 1 && s_ask_finger && c != INPUT_CONSENT_OK) {
                ask_refuse(c);
            } else if (b == 1) {
                sfx_ui(SFX_CLICK);
                ask_end(1);
            } else if (b == 0) {
                sfx_ui(SFX_CLICK);
                ask_end(0);
            } else if (s_ask_closable) {
                sfx_ui(SFX_BACK);
                ask_end(-1);
            }
        }
        return true;
    }
    if (ev->kind == IN_TAP) {
        s_msg_on = false;
        locate_stop();
    }
    return true;
}

// ---------------------------------------------------------------- text: UTF-8, the quest font (English and Hebrew), lines
static inline uint16_t mix565(uint16_t d, uint16_t s, int a)
{
    uint32_t a5 = ((uint32_t)a + 4) >> 3;
    uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81F, ss = (s | ((uint32_t)s << 16)) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * a5) >> 5)) & 0x07E0F81F;
    return (uint16_t)(r | (r >> 16));
}
static float adv(const mfont_t *f, uint16_t c)
{
    const mglyph_t *g = mf_glyph(f, c);
    return g ? g->adv4 / 4.0f : (c == ' ' ? f->asc * 0.3f : 0);
}
static void glyph(const mfont_t *f, float x, int base, uint16_t c, uint16_t col)
{
    const mglyph_t *g = mf_glyph(f, c);
    if (!g) {
        return;
    }
    int gx = (int)lroundf(x) + g->ox, gy = base + g->oy;
    const uint8_t *src = f->a4 + g->off;
    uint16_t nc = sw16(col);
    for (int j = 0; j < g->h; j++) {
        int y = gy + j;
        if ((unsigned)y >= (unsigned)gfx_h) {
            continue;
        }
        uint16_t *row = &g_fb[y * gfx_w];
        for (int k = 0; k < g->w; k++) {
            int px = gx + k;
            if ((unsigned)px >= (unsigned)gfx_w) {
                continue;
            }
            int idx = j * g->w + k, v = (idx & 1) ? src[idx >> 1] >> 4 : src[idx >> 1] & 15;
            if (!v) {
                continue;
            }
            int a = v * 17;
            row[px] = a >= 255 ? col : sw16(mix565(sw16(row[px]), nc, a));
        }
    }
}
// lines: start / length in the code points; a line breaks at a space or a new line
#define MAX_CP    600
#define MAX_LINES 12
static int wrap(const mfont_t *f, const uint16_t *cp, int n, float maxw, int *ls, int *ll)
{
    int nl = 0, i = 0;
    while (i < n && nl < MAX_LINES) {
        while (i < n && cp[i] == ' ') {
            i++;
        }
        int start = i, last_sp = -1;
        float w = 0;
        while (i < n && cp[i] != '\n') {
            float a = adv(f, cp[i]);
            if (w + a > maxw && i > start) {
                break;
            }
            if (cp[i] == ' ') {
                last_sp = i;
            }
            w += a;
            i++;
        }
        int end = i;
        if (i < n && cp[i] != '\n' && last_sp > start) {
            end = last_sp;
            i = last_sp + 1;
        }
        if (i < n && cp[i] == '\n') {
            i++;
        }
        ls[nl] = start;
        ll[nl] = end - start;
        nl++;
    }
    return nl;
}
static float run_w(const mfont_t *f, const uint16_t *cp, int n)
{
    float w = 0;
    for (int i = 0; i < n; i++) {
        w += adv(f, cp[i]);
    }
    return w;
}
// one line centred at cx: a line with Hebrew in the order of the Unicode Bidirectional Algorithm (libs/bidi/bidi.h:
// from the right, a Latin phrase or a number in its own order, the brackets of a right-to-left part mirrored). A line
// draws BIDI_MAX letters at most.
static void line_draw(const mfont_t *f, const uint16_t *cp, int n, int cx, int base, uint16_t col)
{
    int16_t ord[BIDI_MAX];
    uint8_t mir[BIDI_MAX];
    if (n > BIDI_MAX) {
        n = BIDI_MAX;
    }
    const bool rtl = bidi_order(cp, n, ord, mir);
    float x = cx - run_w(f, cp, n) / 2;
    for (int i = 0; i < n; i++) {
        const uint16_t c = !rtl ? cp[i] : mir[i] ? bidi_mirror(cp[ord[i]]) : cp[ord[i]];
        glyph(f, x, base, c, col);
        x += adv(f, c);
    }
}

// ---------------------------------------------------------------- the texts of the question
// Each text of the question (the name, the line, the note of a refused tap) fits in the width of the panel: the first
// font in which the text has one line; else the first font in which it has two lines, then three (a tall frame: up
// to ASK_LINES_TALL lines). A line breaks at a space. A word that is wider than the panel (a key ID) breaks after a
// '-', else between two characters. A text that needs more lines in the smallest font ends its last line with "...".
#define ASK_LINES      3
#define ASK_LINES_TALL 6
typedef struct {
    const kfont_t *f;
    int n;                                         // the count of the lines (0 = an empty text)
    bool cut;                                      // the text has more lines: the last line ends with "..."
    uint8_t s[ASK_LINES_TALL], l[ASK_LINES_TALL];  // the start and the length (bytes) of each line
} askt_t;
// the n bytes of s, and "..." when dots, as a text in b (ASK_TEXT + 4 bytes)
static void ask_part(char *b, const char *s, int n, bool dots)
{
    if (n > ASK_TEXT - 1) {
        n = ASK_TEXT - 1;
    }
    memcpy(b, s, (size_t)n);
    snprintf(b + n, 4, "%s", dots ? "..." : "");
}
static bool ask_fits(const kfont_t *f, const char *s, int n, int w)
{
    char b[ASK_TEXT + 4];
    ask_part(b, s, n, false);
    return ui_text_w(f, b) <= w;
}
// the end of the UTF-8 character at p
static int ask_next(const char *s, int p)
{
    p++;
    while ((s[p] & 0xC0) == 0x80) {
        p++;
    }
    return p;
}
// the end of the line that starts at p (not a space): whole words; else the longest part of the first word (one
// character at least), and in it the last '-' when it has one
static int ask_line_end(const kfont_t *f, const char *s, int p, int w)
{
    const int len = (int)strlen(s);
    int end = -1;
    for (int q = p + 1; q <= len; q++) {
        if (q == len || s[q] == ' ') {
            if (!ask_fits(f, s + p, q - p, w)) {
                break;
            }
            end = q;
        }
    }
    if (end > p) {
        return end;
    }
    int cut = ask_next(s, p);
    while (cut < len && s[cut] != ' ') {
        const int q = ask_next(s, cut);
        if (!ask_fits(f, s + p, q - p, w)) {
            break;
        }
        cut = q;
    }
    for (int q = cut; q > p + 1; q--) {
        if (s[q - 1] == '-') {
            return q;
        }
    }
    return cut;
}
// the lines of s in the font f and the width w. Returns their count (it can be more than ASK_LINES_TALL: t keeps the
// first ASK_LINES_TALL lines).
static int ask_wrap(const kfont_t *f, const char *s, int w, askt_t *t)
{
    const int len = (int)strlen(s);
    int n = 0, p = 0;
    t->f = f;
    t->cut = false;
    while (p < len) {
        while (p < len && s[p] == ' ') {
            p++;
        }
        if (p >= len) {
            break;
        }
        const int e = ask_line_end(f, s, p, w);
        if (n < ASK_LINES_TALL) {
            t->s[n] = (uint8_t)p;
            t->l[n] = (uint8_t)(e - p);
        }
        n++;
        p = e;
    }
    t->n = n < ASK_LINES_TALL ? n : ASK_LINES_TALL;
    return n;
}
// the lines of s in at most maxl lines (ASK_LINES or ASK_LINES_TALL)
static askt_t ask_fit(const kfont_t *const *fonts, int nf, const char *s, int w, int maxl)
{
    askt_t t;
    for (int lines = 1; lines <= maxl; lines++) {
        for (int i = 0; i < nf; i++) {
            if (ask_wrap(fonts[i], s, w, &t) <= lines) {
                return t;
            }
        }
    }
    // more lines than maxl: the first ones, and "..." at the end of the last one (characters go until it fits)
    ask_wrap(fonts[nf - 1], s, w, &t);
    t.n = maxl;
    t.cut = true;
    const int last = maxl - 1;
    char b[ASK_TEXT + 4];
    ask_part(b, s + t.s[last], t.l[last], true);
    while (t.l[last] > 1 && ui_text_w(t.f, b) > w) {
        int q = t.l[last] - 1;
        while (q > 1 && (s[t.s[last] + q] & 0xC0) == 0x80) {
            q--;  // (the start of a UTF-8 character)
        }
        t.l[last] = (uint8_t)q;
        ask_part(b, s + t.s[last], t.l[last], true);
    }
    return t;
}
// the height that a text adds to the height of one line (0 for one line)
static int ask_more(const askt_t *t)
{
    return t->n > 1 ? (t->n - 1) * t->f->line : 0;
}
#define ASK_NOTE 56  // the bytes of the note of a refused tap ("A REMOTE TAP CANNOT " and the first button)
static void ask_note_text(char out[ASK_NOTE])
{
    snprintf(out, ASK_NOTE, "A REMOTE TAP CANNOT %s", s_ask_y);
}
static void ask_text_draw(const askt_t *t, const char *s, int cx, int base, uint16_t col)
{
    for (int i = 0; i < t->n; i++) {
        char b[ASK_TEXT + 4];
        ask_part(b, s + t->s[i], t->l[i], t->cut && i == t->n - 1);
        ui_text_c(t->f, cx, base + i * t->f->line, b, col);
    }
}

// ---------------------------------------------------------------- drawing
// The question: a wide frame (the 3.49 in landscape) has the name at the left and the buttons at the right; else the
// name at the top and the buttons below it (one over the other when the frame is narrow). A round panel uses the
// square in its circle. tby, lby, nby: the baselines of the first lines of the name, of the line and of the note of a
// refused tap (under the line of the question; KF_LABEL). A text with more lines makes the block higher; a tall frame
// with no room for it makes the avatar smaller.
typedef struct {
    sbox_t panel, yes, no;
    int acx, acy, ar, tcx, tw, tby, lby, nby;
    bool wide;
    askt_t tt, lt, nt;  // the lines of the name, of the line and of the note
} askg_t;
static void ask_texts(askg_t *g, int maxl)
{
    static const kfont_t *const TF[] = {&KF_TITLE, &KF_BIG, &KF_TEXT, &KF_NAME, &KF_LABEL};  // (34, 23, 17, 15, 13 px)
    static const kfont_t *const LF[] = {&KF_TEXT, &KF_LABEL};
    static const kfont_t *const NF[] = {&KF_LABEL};
    char note[ASK_NOTE];
    ask_note_text(note);
    g->tt = ask_fit(TF, 5, s_ask_t, g->tw, maxl);
    g->lt = ask_fit(LF, 2, s_ask_l, g->tw, maxl);
    g->nt = ask_fit(NF, 1, note, g->tw, maxl);
}
static askg_t ask_layout_wide(int W, int H)
{
    askg_t g;
    g.wide = true;
    g.panel = (sbox_t){8, 6, W - 16, H - 12};
    int bw = W * 34 / 100, bx = W - 14 - bw;
    g.yes = (sbox_t){bx, 18, bw, (H - 12) * 44 / 100};
    g.no = (sbox_t){bx, g.yes.y + g.yes.h + 10, bw, H - 18 - (g.yes.y + g.yes.h + 10)};
    g.ar = H * 22 / 100;
    g.acx = 22 + g.ar + 10;
    g.acy = H / 2;
    g.tcx = (g.acx + g.ar + 14 + bx - 12) / 2;
    g.tw = bx - 12 - (g.acx + g.ar + 14);
    ask_texts(&g, ASK_LINES);
    // (more lines: the texts go up by half of the added height, and stay under the top of the panel)
    const int mt = ask_more(&g.tt), ml = ask_more(&g.lt), mn = ask_more(&g.nt);
    g.tby = H / 2 + 2 - (mt + ml + mn) / 2;
    if (g.tby - g.tt.f->asc < g.panel.y + 6) {
        g.tby = g.panel.y + 6 + g.tt.f->asc;
    }
    g.lby = g.tby + mt + 28;
    g.nby = g.lby + ml + 22;
    // (the note goes below the bottom of the panel: all the texts go up, but stay under the top of the panel)
    const int over = g.nby + mn + 4 - (g.panel.y + g.panel.h), room = g.tby - g.tt.f->asc - (g.panel.y + 6);
    const int up = over < room ? over : room;
    if (up > 0) {
        g.tby -= up;
        g.lby -= up;
        g.nby -= up;
    }
    return g;
}
static int ask_clamp(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}
static askg_t ask_layout_tall(int W, int H)
{
    askg_t g;
    sbox_t a = gfx_round() ? (sbox_t){W * 15 / 100, H * 13 / 100, W * 70 / 100, H * 74 / 100}
                           : (sbox_t){8, 8, W - 16, H - 16};
    g.wide = false;
    g.panel = a;
    g.tcx = W / 2;
    g.tw = a.w - 20;
    ask_texts(&g, ASK_LINES_TALL);  // (a tall frame has the height for more lines)
    const bool narrow = a.w < 260;
    const int bh = ask_clamp(a.h / 4, 48, 84), mt = ask_more(&g.tt), ml = ask_more(&g.lt);
    // the whole block (the avatar, the name, the line, the buttons) in the middle of the panel; the gap between the line
    // and the buttons has the note
    const int gap = 22 + ask_more(&g.nt),
              body = KF_BIG.asc + mt + KF_TEXT.line + 2 + ml + gap + (narrow ? bh + 10 + bh * 3 / 4 : bh);
    g.ar = ask_clamp(a.w * 11 / 100, 22, 46);
    if (14 + 2 * g.ar + 12 + body > a.h) {
        g.ar = ask_clamp((a.h - body - 26) / 2, 14, g.ar);  // (no room: a smaller avatar)
    }
    int top = a.y + (a.h - (14 + 2 * g.ar + 12 + body)) / 2;
    if (top < a.y) {
        top = a.y;
    }
    g.acx = W / 2;
    g.acy = top + 14 + g.ar;
    g.tby = g.acy + g.ar + 12 + KF_BIG.asc;
    g.lby = g.tby + mt + KF_TEXT.line + 2;
    const int by = g.lby + ml + gap;
    if (narrow) {
        g.yes = (sbox_t){a.x + 10, by, a.w - 20, bh};
        g.no = (sbox_t){a.x + 10, by + bh + 10, a.w - 20, bh * 3 / 4};
    } else {
        int bw = (a.w - 30) / 2;
        g.yes = (sbox_t){a.x + 10, by, bw, bh};
        g.no = (sbox_t){a.x + 20 + bw, by, bw, bh};
    }
    g.nby = g.lby + ml + KF_LABEL.line;  // (in the gap between the line and the buttons)
    return g;
}
static askg_t ask_layout(void)
{
    return gfx_w >= 2 * gfx_h ? ask_layout_wide(gfx_w, gfx_h) : ask_layout_tall(gfx_w, gfx_h);
}
static void ask_geo(sbox_t *yes, sbox_t *no)
{
    askg_t g = ask_layout();
    *yes = g.yes;
    *no = g.no;
}
// the text of a button in its middle: in the button font mf (a smaller one when the text is wider than the button); a
// text with Hebrew (the button font has no Hebrew), or a text that is too wide in each button font, in the quest font,
// the biggest that fits
static void ask_button_text(const sbox_t *b, const char *s, const mfont_t *mf, uint16_t col)
{
    bool ascii = true;
    for (const char *p = s; *p && ascii; p++) {
        ascii = (unsigned char)*p < 0x80;
    }
    if (ascii) {
        // (a long text in a smaller button font: the next size down that fits; else the quest font below)
        static const mfont_t *const DOWN[] = {&MF_N24, &MF_N20, &MF_N16};
        for (int i = 0; i < 2 && mf_width(mf, s, 1) > b->w - 12; i++) {
            if (mf == DOWN[i]) {
                mf = DOWN[i + 1];
            }
        }
        if (mf_width(mf, s, 1) <= b->w - 12) {
            mf_text_c(mf, b->x + b->w / 2, b->y + b->h / 2 + mf->asc / 2, s, col, 255, 1);
            return;
        }
    }
    static const kfont_t *const BF[] = {&KF_BIG, &KF_TEXT, &KF_NAME, &KF_LABEL};
    const kfont_t *f = BF[3];
    for (int i = 0; i < 4; i++) {
        if (ui_text_w(BF[i], s) <= b->w - 12) {
            f = BF[i];
            break;
        }
    }
    ui_text_c(f, b->x + b->w / 2, b->y + b->h / 2 + f->asc / 2, s, col);
}

static void ask_draw(void)
{
    if (s_snap) {
        memcpy(g_fb, s_snap, (size_t)gfx_w * gfx_h * 2);
    } else {
        gfx_fill(THEME_ASK_BACK);
    }
    askg_t g = ask_layout();
    const int64_t now = esp_timer_get_time() / 1000, t = now - s_ask_t0;
    input_consent_shown(&s_ask_cons, now);
    uint16_t panel = THEME_ASK_BG, edge = THEME_ASK_EDGE;
    int rad = g.wide ? 14 : 18;
    ui_rbox(g.panel.x, g.panel.y, g.panel.w, g.panel.h, rad, panel, edge, 2);
    // the avatar: the first letter of the name in a circle, with rings that go out (a ring of a phone)
    uint16_t ac = ui_name_colour(s_ask_t);
    for (int k = 0; k < 2; k++) {
        float ph = fmodf(t / 1300.0f + k * 0.5f, 1.0f);
        ui_ring(g.acx, g.acy, g.ar + 4 + (int)(ph * g.ar * 0.8f), 3, ac, (int)(200 * (1 - ph)));
    }
    ui_circle(g.acx, g.acy, g.ar, ac, 255);
    if (s_ask_radio) {
        // (a radio: a dot and two waves)
        ui_circle(g.acx, g.acy, g.ar / 5 + 1, THEME_ASK_INITIAL, 255);
        ui_ring(g.acx, g.acy, g.ar * 45 / 100, 3, THEME_ASK_INITIAL, 255);
        ui_ring(g.acx, g.acy, g.ar * 72 / 100, 3, THEME_ASK_INITIAL, 200);
    } else {
        char ini[4];
        ui_initial(s_ask_t, ini);
        const kfont_t *fi = g.ar >= 34 ? &KF_TITLE : &KF_BIG;
        ui_text_c(fi, g.acx, g.acy + fi->asc / 2, ini, THEME_ASK_INITIAL);
    }
    // the name and the line (each in the lines of ask_fit)
    ask_text_draw(&g.tt, s_ask_t, g.tcx, g.tby, THEME_ASK_NAME);
    ask_text_draw(&g.lt, s_ask_l, g.tcx, g.lby, THEME_ASK_LINE);
    if (s_ask_remote) {
        // (a fact, not a request: the line of the question with its data stays)
        char note[ASK_NOTE];
        ask_note_text(note);
        ask_text_draw(&g.nt, note, g.tcx, g.nby, THEME_ASK_WARN);
    }
    // the buttons: ANSWER glows (a consent: only when a finger can answer, after INPUT_ARM_MS)
    const bool armed = !s_ask_finger || now >= s_ask_cons.shown_ms + INPUT_ARM_MS;
    int glow = armed ? 128 + (int)(127 * sinf(t / 260.0f)) : 0;
    ui_rbox(g.yes.x, g.yes.y, g.yes.w, g.yes.h, 12, ui_mix(THEME_ASK_YES, THEME_ASK_YES_GLOW, glow), THEME_ASK_YES_EDGE,
            2);
    ui_rbox(g.no.x, g.no.y, g.no.w, g.no.h, 12, THEME_ASK_NO, THEME_ASK_NO_EDGE, 2);
    ask_button_text(&g.yes, s_ask_y, g.yes.h >= 56 && g.yes.w >= 150 ? &MF_N24 : &MF_N16, THEME_ASK_YES_TEXT);
    ask_button_text(&g.no, s_ask_n, g.no.h >= 50 && g.no.w >= 150 ? &MF_N20 : &MF_N16, THEME_ASK_NO_TEXT);
    if (s_ask_closable) {
        // the close mark: top left on the wide bar (the buttons are at the right), else top right
        int cx = g.wide ? g.panel.x + 24 : g.panel.x + g.panel.w - 24, cy = g.panel.y + 24;
        ui_circle(cx, cy, 15, THEME_ASK_CLOSE, 255);
        for (int d = -6; d <= 6; d++) {
            gfx_fill_rect(cx + d - 1, cy + d - 1, 3, 3, THEME_ASK_CLOSE_MARK);
            gfx_fill_rect(cx + d - 1, cy - d - 1, 3, 3, THEME_ASK_CLOSE_MARK);
        }
    }
}
static void box(int *bx, int *by, int *bw, int *bh)
{
    if (gfx_round()) {
        int s = gfx_w * 70 / 100;
        *bx = (gfx_w - s) / 2;
        *by = (gfx_h - s) / 2;
        *bw = s;
        *bh = s;
        return;
    }
    *bx = 8;
    *by = 6;
    *bw = gfx_w - 16;
    *bh = gfx_h - 12;
}
void sysui_draw(void)
{
    if (s_loc_on) {
        int64_t t = esp_timer_get_time() / 1000;
        bool hi = ((t - s_loc_t0) / 250) & 1;
        gfx_fill(hi ? THEME_FIND_BG_HI : THEME_FIND_BG);
        const mfont_t *f = (const mfont_t *)&KF_TITLE;
        uint16_t cp[8];
        int n = tfn_utf8_decode("HERE!", cp, 8, false);
        line_draw(f, cp, n, gfx_w / 2, gfx_h / 2 + f->asc / 2, hi ? THEME_FIND_TEXT_HI : THEME_FIND_TEXT);
        mf_text_c(&MF_TINY, gfx_w / 2, gfx_h / 2 + f->asc / 2 + 20, "TAP TO STOP",
                  hi ? THEME_FIND_HINT_HI : THEME_FIND_HINT, 255, 1);
        return;
    }
    if (s_ask_on) {
        ask_draw();
        return;
    }
    if (!s_msg_on) {
        return;
    }
    if (s_snap) {
        memcpy(g_fb, s_snap, (size_t)gfx_w * gfx_h * 2);
    } else {
        gfx_fill(THEME_MSG_BACK);
    }
    static uint16_t cp[MAX_CP];
    int n = tfn_utf8_decode(s_msg, cp, MAX_CP, false);
    int bx, by, bw, bh;
    box(&bx, &by, &bw, &bh);
    // the biggest font that fits
    static const kfont_t *const FONTS[] = {&KF_TITLE, &KF_BIG, &KF_TEXT, &KF_NAME,
                                           &KF_LABEL};  // (34, 23, 17, 15, 13 px)
    int ls[MAX_LINES], ll[MAX_LINES], nl = 0;
    const mfont_t *f = NULL;
    for (size_t k = 0; k < sizeof FONTS / sizeof FONTS[0]; k++) {
        f = (const mfont_t *)FONTS[k];
        nl = wrap(f, cp, n, (float)(bw - 16), ls, ll);
        if (nl * f->line + 14 <= bh) {
            break;
        }
    }
    int th = nl * f->line, ty = by + (bh - th - 12) / 2;
    uint16_t panel = THEME_MSG_BG, edge = THEME_MSG_EDGE;
    int pw = bw, ph = th + 24, px = bx, py = ty - 6;
    if (py < 0) {
        py = 0;
    }
    if (gfx_round()) {
        pw = bw;
        px = bx;
    }
    gfx_fill_rect(px, py, pw, ph, panel);
    gfx_frame_rect(px, py, pw, ph, 2, edge);
    for (int i = 0; i < nl; i++) {
        line_draw(f, cp + ls[i], ll[i], gfx_w / 2, ty + 6 + i * f->line + f->asc, THEME_MSG_TEXT);
    }
    if (ph + py + 12 < gfx_h) {
        mf_text_c(&MF_TINY, gfx_w / 2, py + ph + 11, "TAP TO CLOSE", THEME_MSG_HINT, 255, 1);
    }
}

// ---------------------------------------------------------------- the start picture
typedef struct {
    const uint8_t *p;
    uint32_t n, pos;
    int ox, oy;
} jsrc_t;
static UINT j_in(JDEC *jd, BYTE *buf, UINT n)
{
    jsrc_t *s = (jsrc_t *)jd->device;
    if (s->pos + n > s->n) {
        n = s->n - s->pos;
    }
    if (buf) {
        memcpy(buf, s->p + s->pos, n);
    }
    s->pos += n;
    return n;
}
static UINT j_out(JDEC *jd, void *bm, JRECT *r)
{
    jsrc_t *s = (jsrc_t *)jd->device;
    const uint8_t *c = (const uint8_t *)bm;
    for (int y = r->top; y <= r->bottom; y++) {
        for (int x = r->left; x <= r->right; x++, c += 3) {
            int X = x + s->ox, Y = y + s->oy;
            if ((unsigned)X < (unsigned)gfx_w && (unsigned)Y < (unsigned)gfx_h) {
                g_fb[Y * gfx_w + X] = rgb(c[0], c[1], c[2]);
            }
        }
    }
    return 1;
}
void sysui_splash(void)
{
    if (!store_get("splash_on", 0)) {
        return;
    }
    FILE *fp = fopen("/data/splash.jpg", "rb");
    if (!fp) {
        return;
    }
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = (n > 0 && n <= 400000) ? heap_caps_malloc((size_t)n, MALLOC_CAP_SPIRAM) : NULL;
    void *work = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
    bool ok = buf && work && fread(buf, 1, (size_t)n, fp) == (size_t)n;
    fclose(fp);
    if (ok) {
        gfx_fill(0);
        jsrc_t src = {buf, (uint32_t)n, 0, 0, 0};
        JDEC jd;
        JRESULT r = jd_prepare(&jd, j_in, work, 4096, &src);
        if (r == JDR_OK) {
            src.ox = ((int)gfx_w - (int)jd.width) / 2;  // a picture of another size: in the middle
            src.oy = ((int)gfx_h - (int)jd.height) / 2;
            r = jd_decomp(&jd, j_out, 0);
        }
        ok = r == JDR_OK;
        if (!ok) {
            ESP_LOGE(TAG, "splash: jpeg error %d", (int)r);
        }
    }
    heap_caps_free(buf);
    heap_caps_free(work);
    if (!ok) {
        return;
    }
    board_lcd_flush(gfx_swap(), gfx_orient() | gfx_frame_bits());
    board_backlight(g_settings.brightness);
    int secs = (int)store_get("splash_s", 3);
    if (secs < 1) {
        secs = 1;
    }
    if (secs > 15) {
        secs = 15;
    }
    ESP_LOGI(TAG, "splash: %d s", secs);
    int64_t until = esp_timer_get_time() / 1000 + secs * 1000LL;
    input_event_t ev;
    while (esp_timer_get_time() / 1000 < until) {
        bool touch = false;
        while (input_get(&ev)) {
            touch = true;
        }
        if (touch) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    input_flush();
}

// ---------------------------------------------------------------- the speaker sweep
// the level is the RMS without the DC part: the microphone start gives an offset that decays for some time
static volatile double s_acc, s_sum;
static volatile uint32_t s_cnt;
static void sweep_tap(const int16_t *s, int n)
{
    double a = 0, m = 0;
    for (int i = 0; i < n; i++) {
        a += (double)s[i] * s[i];
        m += s[i];
    }
    s_acc += a;
    s_sum += m;
    s_cnt += (uint32_t)n;
}
bool sysui_sweep(int f0, int f1, int df, int ms, float *out, int nmax, int *n)
{
    *n = 0;
    if (df <= 0 || f1 < f0 || ms < 40) {
        return false;
    }
    if (!audio_mic_on() && !audio_mic_start()) {
        return false;
    }
    board_speaker(true);
    audio_set_volume(80);
    audio_mic_tap(sweep_tap);
    vTaskDelay(pdMS_TO_TICKS(600));  // the microphone settles (no tone)
    for (int f = f0; f <= f1 && *n < nmax; f += df) {
        audio_drone(1, (float)f, 80, W_SINE);
        vTaskDelay(pdMS_TO_TICKS(ms / 3));
        s_acc = 0;
        s_sum = 0;
        s_cnt = 0;
        vTaskDelay(pdMS_TO_TICKS(ms - ms / 3));
        double c = s_cnt, mean = c ? s_sum / c : 0, var = c ? s_acc / c - mean * mean : 0;
        out[(*n)++] = var > 0 ? (float)sqrt(var) : 0;
    }
    audio_drone(1, 0, 0, W_SINE);
    audio_mic_tap(NULL);
    audio_mic_stop();
    settings_apply();
    return true;
}
