// The unit test of the question of the system layer (main/sysui.c: sysui_ask, sysui_ask_finger, sysui_input, the
// drawing of the question): the first button of a consent question acts only for the rule of a consent (main/input.h:
// a tap by a finger, of a touch that came down INPUT_ARM_MS or more after the question showed). A refused tap keeps
// the question open, calls no answer function, keeps the line of the question (its data), and shows a note in its own
// place; the second button answers for each tap, also in the gap next to it; a tap out of the buttons closes only a
// closable question. The test gives the fake input_injected() (the answer of the touch task for each event) and
// records the drawing (the boxes of the buttons, the texts with their width in the real fonts of main/kfont.c). The
// frames: the wide frame of the 3.49 (640 x 172), the narrow frame of the 3.49 in portrait (172 x 640), 320 x 240 and
// 240 x 320, the Tab5 (640 x 360, 360 x 640) and, in the build for a round board kind (BOARD_ROUND), the round frames of
// 412 and 466 px. In each frame, each line of the name, of the line and of the note of the questions of the OS is in the
// panel (and in the circle), on no button, on no other text (a long text has more lines or a smaller font), and all of
// each text shows; a text that is too long for the lines of the frame shows its start and "..." at its end. A tap must
// be of the touch whose IN_DOWN the question got:
// a long press or a drag on the first button, then an IN_UP and an IN_TAP with no IN_DOWN (the wake from the screen
// saver), give no answer.
// SETUP > DEVELOPER MODE (main/app_settings.c, the fakes of its other parts: fake_sysui/setup_fakes.c): ON is a
// consent question, and a tap of the web or of the console on the row or on the question does not turn it on.
// Build and run: python tests/unit/run.py
#include <stdio.h>
#include <string.h>
#include "sysui_priv.h"
#include "app_settings_priv.h"

extern const app_t app_settings;

static int s_fails;

static void expect(bool ok, const char *what)
{
    if (!ok) {
        s_fails++;
        printf("FAIL %s\n", what);
    }
}

// ---------------------------------------------------------------- the fakes of the OS
static int64_t s_now_us = 500LL * 1000 * 1000;
static bool s_inj;  // input_injected() of the next event: true = not by a finger on the panel
static int s_back_sounds;
static uint16_t s_fb[640 * 240];
uint16_t *g_fb = s_fb;
int gfx_w = 640, gfx_h = 172;
settings_t g_settings;
const mfont_t MF_N24 = {24, 30, 0, NULL, NULL}, MF_N20 = {20, 25, 0, NULL, NULL}, MF_N16 = {16, 20, 0, NULL, NULL},
              MF_TINY = {7, 9, 0, NULL, NULL};

int64_t esp_timer_get_time(void)
{
    return s_now_us;
}

bool input_injected(const input_event_t *ev)
{
    (void)ev;
    return s_inj;
}

bool input_get(input_event_t *ev)
{
    (void)ev;
    return false;
}

void input_flush(void)
{
}

void sfx_ui(sfx_t sfx)
{
    s_back_sounds += sfx == SFX_BACK;
}

// the boxes of ui_rbox() in one frame (the panel, ANSWER, the other key) and the texts of ui_text_c() (the baseline,
// the left and the right end)
typedef struct {
    int x, y, w, h;
} box_t;
#define TEXTS 32
static box_t s_box[8];
static int s_nbox;
static char s_text[TEXTS][128];
static int s_text_y[TEXTS], s_text_x0[TEXTS], s_text_x1[TEXTS], s_ntext;
static const kfont_t *s_text_f[TEXTS];

void ui_rbox(int x, int y, int w, int h, int r, uint16_t fill, uint16_t border, int t)
{
    (void)r;
    (void)fill;
    (void)border;
    (void)t;
    if (s_nbox < 8) {
        s_box[s_nbox++] = (box_t){x, y, w, h};
    }
}

// the width of a text in a font of main/kfont.c (the rule of main/uikit.c: the advance of each glyph, a space with no
// glyph = 0.3 of the ascent)
int ui_text_w(const kfont_t *f, const char *utf8)
{
    uint16_t cp[128];
    const int n = tfn_utf8_decode(utf8, cp, 128, true);
    float w = 0;
    for (int i = 0; i < n; i++) {
        const kglyph_t *g = NULL;
        for (int k = 0; k < f->n && !g; k++) {
            g = f->g[k].cp == cp[i] ? &f->g[k] : NULL;
        }
        w += g ? g->adv4 / 4.0f : (cp[i] == ' ' ? f->asc * 0.3f : 0);
    }
    return (int)ceilf(w);
}

void ui_text_c(const kfont_t *f, int cx, int base, const char *utf8, uint16_t col)
{
    (void)col;
    if (s_ntext < TEXTS) {
        const int w = ui_text_w(f, utf8);
        snprintf(s_text[s_ntext], sizeof s_text[0], "%s", utf8);
        s_text_x0[s_ntext] = cx - w / 2;
        s_text_x1[s_ntext] = cx - w / 2 + w;
        s_text_f[s_ntext] = f;
        s_text_y[s_ntext++] = base;
    }
}

const kfont_t *ui_text_fit(const kfont_t *const *fonts, int n, const char *utf8, int w)
{
    for (int i = 0; i < n; i++) {
        if (ui_text_w(fonts[i], utf8) <= w) {
            return fonts[i];
        }
    }
    return fonts[n - 1];
}

// the last full circle (the avatar of the question)
static box_t s_circle;

void ui_circle(int cx, int cy, int r, uint16_t c, int a)
{
    (void)c;
    if (a == 255) {
        s_circle = (box_t){cx - r, cy - r, 2 * r, 2 * r};
    }
}

void ui_ring(int cx, int cy, int r, int t, uint16_t c, int a)
{
    (void)cx;
    (void)cy;
    (void)r;
    (void)t;
    (void)c;
    (void)a;
}

uint16_t ui_mix(uint16_t a, uint16_t b, int t)
{
    (void)b;
    (void)t;
    return a;
}

uint16_t ui_name_colour(const char *utf8)
{
    (void)utf8;
    return 0;
}

void ui_initial(const char *utf8, char out[4])
{
    (void)utf8;
    snprintf(out, 4, "T");
}

void mf_text_c(const mfont_t *f, int cx, int base, const char *s, uint16_t col, int alpha, int spacing)
{
    (void)f;
    (void)cx;
    (void)base;
    (void)s;
    (void)col;
    (void)alpha;
    (void)spacing;
}

int mf_width(const mfont_t *f, const char *s, int spacing)
{
    (void)f;
    (void)spacing;
    return 10 * (int)strlen(s);
}

const mglyph_t *mf_glyph(const mfont_t *f, int cp)
{
    (void)f;
    (void)cp;
    return NULL;
}

void gfx_fill(uint16_t c)
{
    (void)c;
}

void gfx_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)c;
}

void gfx_frame_rect(int x, int y, int w, int h, int t, uint16_t c)
{
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)t;
    (void)c;
}

uint16_t *gfx_swap(void)
{
    return g_fb;
}

// (a round board kind: the frame of the question is the native frame of the panel)
bool gfx_native(void)
{
    return true;
}

orient_t gfx_orient(void)
{
    return (orient_t)0;
}

int gfx_frame_bits(void)
{
    return 0;
}

void board_lcd_flush(const uint16_t *fb, int orient)
{
    (void)fb;
    (void)orient;
}

void board_backlight(uint8_t level)
{
    (void)level;
}

void board_speaker(bool on)
{
    (void)on;
}

void audio_drone(int ch, float hz, int vol, wave_t wave)
{
    (void)ch;
    (void)hz;
    (void)vol;
    (void)wave;
}

void audio_set_volume(int vol)
{
    (void)vol;
}

bool audio_mic_on(void)
{
    return false;
}

bool audio_mic_start(void)
{
    return false;
}

void audio_mic_stop(void)
{
}

void audio_mic_tap(void (*cb)(const int16_t *s, int n))
{
    (void)cb;
}

void settings_apply(void)
{
}

uint32_t store_get(const char *key, uint32_t def)
{
    (void)key;
    return def;
}

void app_screen_wake(void)
{
}

void menu_redraw_all(void)
{
}

void walkie_call_update(int64_t now)
{
    (void)now;
}

void vTaskDelay(TickType_t ticks)
{
    s_now_us += (int64_t)ticks * 1000;
}

JRESULT jd_prepare(JDEC *jd, UINT (*in)(JDEC *, BYTE *, UINT), void *pool, UINT sz, void *dev)
{
    (void)jd;
    (void)in;
    (void)pool;
    (void)sz;
    (void)dev;
    return JDR_INP;
}

JRESULT jd_decomp(JDEC *jd, UINT (*out)(JDEC *, void *, JRECT *), BYTE scale)
{
    (void)jd;
    (void)out;
    (void)scale;
    return JDR_INP;
}

// ---------------------------------------------------------------- the steps of the test
static int s_answers, s_last = -9;

static void answer(int k)
{
    s_answers++;
    s_last = k;
}

static int64_t now_ms(void)
{
    return s_now_us / 1000;
}

static void wait_ms(int ms)
{
    s_now_us += (int64_t)ms * 1000;
}

// one frame of the question: the boxes and the texts of the drawing
static void frame(void)
{
    s_nbox = 0;
    s_ntext = 0;
    sysui_draw();
}

// t with no spaces
static void no_spaces(const char *t, char *out, int n)
{
    int k = 0;
    for (; *t && k < n - 1; t++) {
        if (*t != ' ') {
            out[k++] = *t;
        }
    }
    out[k] = 0;
}

// the text t in the frame, in one line or in more lines (texts one under the other; a line breaks at a space or in a
// word). *first = the index of its first line. Returns the count of the lines (0 = not drawn). cut: the start of t
// and "..." at the end of its last line.
static int text_lines_as(const char *t, int *first, bool cut)
{
    char want[160], got[160], part[128];
    no_spaces(t, want, sizeof want);
    for (int i = 0; i < s_ntext; i++) {
        got[0] = 0;
        for (int j = i; j < s_ntext; j++) {
            if (j > i && s_text_y[j] <= s_text_y[j - 1]) {
                break;
            }
            no_spaces(s_text[j], part, sizeof part);
            if (strlen(got) + strlen(part) >= sizeof got) {
                break;
            }
            strcat(got, part);
            const size_t gl = strlen(got);
            const bool dots = gl > 3 && !strcmp(got + gl - 3, "...");
            if (cut ? dots && gl - 3 < strlen(want) && !strncmp(got, want, gl - 3) : !strcmp(got, want)) {
                *first = i;
                return j - i + 1;
            }
            if (strncmp(got, want, gl)) {
                break;
            }
        }
    }
    return 0;
}

static int text_lines(const char *t, int *first)
{
    return text_lines_as(t, first, false);
}

// the text t is in the frame: *y = the baseline of its last line
static bool drawn(const char *t, int *y)
{
    int i = 0;
    const int n = text_lines(t, &i);
    if (n) {
        *y = s_text_y[i + n - 1];
    }
    return n > 0;
}

// the lines of the note of a refused tap on the first button yes ("A REMOTE TAP CANNOT <yes>"). *first = the index of
// the first line. Returns the count (0 = no note).
static int note_lines(const char *yes, int *first)
{
    char note[48];
    snprintf(note, sizeof note, "A REMOTE TAP CANNOT %s", yes);
    return text_lines(note, first);
}

// the note is in the panel (each line with its width), under the line of the question (its baseline ly), and over the
// first button (in the wide frame the buttons are at the right of the text: the note is at their left)
static bool note_ok(const char *yes_text, const box_t *panel, const box_t *yes, int ly)
{
    int k = 0;
    const int n = note_lines(yes_text, &k);
    const bool beside = yes->y <= ly + 10;
    bool ok = n > 0;
    for (int j = k; j < k + n; j++) {
        ok = ok && s_text_x0[j] >= panel->x && s_text_x1[j] <= panel->x + panel->w && s_text_y[j] > ly &&
             (beside ? s_text_x1[j] <= yes->x && s_text_y[j] < panel->y + panel->h : s_text_y[j] < yes->y);
    }
    return ok;
}

// an event at (x, y) now; inj: not by a finger on the panel. Returns the answer of sysui_input (the question takes it)
static bool event(input_kind_t k, int x, int y, bool inj)
{
    const input_event_t ev = {.kind = k, .x = (int16_t)x, .y = (int16_t)y, .t_ms = now_ms()};
    s_inj = inj;
    return sysui_input(&ev);
}

// a touch at the middle of box b: IN_DOWN, (after ms) IN_UP and IN_TAP
static void tap_box(const box_t *b, bool inj, int ms)
{
    const int x = b->x + b->w / 2, y = b->y + b->h / 2;
    event(IN_DOWN, x, y, inj);
    wait_ms(ms);
    event(IN_UP, x, y, inj);
    event(IN_TAP, x, y, inj);
}

static void ask(bool finger, bool closable)
{
    s_answers = 0;
    s_last = -9;
    sysui_ask("TRUST THE KEY TEST?", "AB12 CD34 EF56 TEST", "ALLOW", "NO", 120, answer);
    if (closable) {
        sysui_ask_closable();
    }
    if (finger) {
        sysui_ask_finger();
    }
    frame();  // (the first frame: the question shows)
}

// A tap answers only for the touch whose IN_DOWN the question got. The user rests a finger on ALLOW (a long press:
// IN_DOWN, IN_LONG, IN_UP, no IN_TAP), the web turns the screen off, and 17 s later a tap wakes the screen: main.c
// frame_saver() takes its IN_DOWN, and only its IN_UP and IN_TAP come to the question. The same after a drag.
static void test_own_touch(const char *name, const box_t *yes)
{
    char what[160];
    const int x = yes->x + yes->w / 2, y = yes->y + yes->h / 2;
    ask(true, false);
    wait_ms(2000);
    event(IN_DOWN, x, y, false);
    wait_ms(LONG_PRESS_MS + 50);
    event(IN_LONG, x, y, false);
    wait_ms(400);
    event(IN_UP, x, y, false);
    wait_ms(17000);
    event(IN_UP, x, y, false);
    event(IN_TAP, x, y, false);
    snprintf(what, sizeof what, "%s: a long press on ALLOW, then a tap with no IN_DOWN 17 s later: no answer", name);
    expect(s_answers == 0 && sysui_active(), what);
    // a drag on ALLOW (no tap), then at once a tap with no IN_DOWN
    event(IN_DOWN, x, y, false);
    wait_ms(30);
    event(IN_MOVE, x, y + 30, false);
    wait_ms(30);
    event(IN_UP, x, y + 30, false);
    wait_ms(60);
    event(IN_UP, x, y, false);
    event(IN_TAP, x, y, false);
    snprintf(what, sizeof what, "%s: a drag on ALLOW, then a tap with no IN_DOWN 0.1 s later: no answer", name);
    expect(s_answers == 0 && sysui_active(), what);
    // a touch with an IN_LONG gives no answer (the touch task sends no IN_TAP after it: the rule does not take one)
    event(IN_DOWN, x, y, false);
    wait_ms(20);
    event(IN_LONG, x, y, false);
    event(IN_UP, x, y, false);
    event(IN_TAP, x, y, false);
    snprintf(what, sizeof what, "%s: a touch with an IN_LONG, then its IN_UP and an IN_TAP: no answer", name);
    expect(s_answers == 0 && sysui_active(), what);
    // a touch that is longer than LONG_PRESS_MS cannot give its tap (a tap with the time of another touch)
    event(IN_DOWN, x, y, false);
    wait_ms(LONG_PRESS_MS + 20);
    event(IN_UP, x, y, false);
    event(IN_TAP, x, y, false);
    snprintf(what, sizeof what, "%s: a tap LONG_PRESS_MS or more after its IN_DOWN: no answer", name);
    expect(s_answers == 0 && sysui_active(), what);
    // the next touch of the finger answers
    tap_box(yes, false, 80);
    snprintf(what, sizeof what, "%s: after these, the next touch of the finger answers ALLOW", name);
    expect(s_answers == 1 && s_last == 1 && !sysui_active(), what);
}

static void test_layout(const char *name)
{
    char what[160];
    // a tap of the web on ALLOW: no answer, the question stays, the line stays, a note in its own place, a sound
    ask(true, false);
    expect(s_nbox >= 3, "the drawing has the panel and the two buttons");
    const box_t yes = s_box[1], no = s_box[2], panel = s_box[0];
    wait_ms(2000);
    const int sounds = s_back_sounds;
    tap_box(&yes, true, 50);
    frame();
    int ly = 0, nk = 0;
    snprintf(what, sizeof what, "%s: a web tap on ALLOW gives no answer, the question stays", name);
    expect(s_answers == 0 && sysui_active() && s_back_sounds == sounds + 1, what);
    snprintf(what, sizeof what, "%s: the line of the question (the key ID) stays", name);
    expect(drawn("AB12 CD34 EF56 TEST", &ly), what);
    snprintf(what, sizeof what, "%s: the note of a remote tap, in the panel, under the line and over the buttons",
             name);
    expect(note_ok("ALLOW", &panel, &yes, ly), what);
    // a finger: the touch came down 0.1 s after the question showed: no answer, no sound, no note
    ask(true, false);
    wait_ms(100);
    tap_box(&yes, false, 80);
    frame();
    snprintf(what, sizeof what, "%s: a finger that came down 0.1 s after the question: no answer, no note", name);
    expect(s_answers == 0 && sysui_active() && note_lines("ALLOW", &nk) == 0, what);
    // the next touch of the finger (INPUT_ARM_MS after the question showed): the answer
    wait_ms(INPUT_ARM_MS);
    tap_box(&yes, false, 80);
    snprintf(what, sizeof what, "%s: a finger INPUT_ARM_MS after the question: ALLOW", name);
    expect(s_answers == 1 && s_last == 1 && !sysui_active(), what);
    // a finger that was down before the question (the question did not see its IN_DOWN)
    ask(true, false);
    wait_ms(2000);
    event(IN_UP, yes.x + yes.w / 2, yes.y + yes.h / 2, false);
    event(IN_TAP, yes.x + yes.w / 2, yes.y + yes.h / 2, false);
    snprintf(what, sizeof what, "%s: a touch that came down before the question: no answer", name);
    expect(s_answers == 0 && sysui_active(), what);
    // NO answers for each tap (also of the web)
    tap_box(&no, true, 50);
    snprintf(what, sizeof what, "%s: a web tap on NO answers NO", name);
    expect(s_answers == 1 && s_last == 0 && !sysui_active(), what);
    // a closable consent question: a tap out of the buttons closes it; a question that is not closable stays
    ask(true, true);
    wait_ms(2000);
    const box_t out = {panel.x + 1, panel.y + 1, 2, 2};  // (the top left corner of the panel: no button)
    tap_box(&out, true, 50);
    snprintf(what, sizeof what, "%s: a tap out of the buttons closes a closable question", name);
    expect(s_answers == 1 && s_last == -1 && !sysui_active(), what);
    ask(true, false);
    wait_ms(2000);
    tap_box(&out, false, 50);
    snprintf(what, sizeof what, "%s: a tap out of the buttons: a question that is not closable stays", name);
    expect(s_answers == 0 && sysui_active(), what);
    sysui_ask_close();
    // a finger in the gap between the buttons, 3 px from the edge of NO (the margins of both boxes have the point):
    // NO, not ALLOW
    ask(true, false);
    wait_ms(2000);
    const bool stack = no.y >= yes.y + yes.h;  // (the buttons one over the other, else side by side)
    const box_t edge = {stack ? no.x + no.w / 2 : no.x - 3, stack ? no.y - 3 : no.y + no.h / 2, 0, 0};
    tap_box(&edge, false, 80);
    snprintf(what, sizeof what, "%s: a finger 3 px from the edge of NO answers NO", name);
    expect(s_answers == 1 && s_last == 0 && !sysui_active(), what);
    // a question that is not a consent (a WALKIE call): its first button answers for each tap
    ask(false, false);
    tap_box(&yes, true, 50);
    snprintf(what, sizeof what, "%s: a question that is not a consent: a web tap on its first button answers", name);
    expect(s_answers == 1 && s_last == 1, what);
    test_own_touch(name, &yes);
    // the question ends at its own time
    ask(true, false);
    wait_ms(121000);
    sysui_update(now_ms());
    snprintf(what, sizeof what, "%s: the question ends at its time with no answer (-1)", name);
    expect(s_answers == 1 && s_last == -1 && !sysui_active(), what);
    sysui_update(now_ms());
}

// ---------------------------------------------------------------- each text fits in the frame
// the box of the capital letters of the text i (the ascent over its baseline)
static box_t text_box(int i)
{
    const int asc = s_text_f[i]->asc;
    return (box_t){s_text_x0[i], s_text_y[i] - asc, s_text_x1[i] - s_text_x0[i], asc + 2};
}

static bool boxes_meet(const box_t *a, const box_t *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

// the corners of b are in the circle of the round panel (the frame gfx_w x gfx_h, 2 px from its edge)
static bool in_circle(const box_t *b)
{
    const int r = gfx_w / 2 - 2;
    for (int k = 0; k < 4; k++) {
        const int dx = (k & 1 ? b->x + b->w : b->x) - gfx_w / 2, dy = (k & 2 ? b->y + b->h : b->y) - gfx_h / 2;
        if (dx * dx + dy * dy > r * r) {
            return false;
        }
    }
    return true;
}

// the text i: in the panel (and in the circle of a round panel), not on a button, not on the avatar (the initial of
// the avatar is in it), not on another text
static bool text_fits(int i, const box_t *panel, const box_t *yes, const box_t *no)
{
    const box_t b = text_box(i);
    bool ok = b.x >= panel->x + 2 && b.x + b.w <= panel->x + panel->w - 2 && b.y >= panel->y &&
              b.y + b.h <= panel->y + panel->h && (!BOARD_ROUND || in_circle(&b)) && !boxes_meet(&b, yes) &&
              !boxes_meet(&b, no) && (i == 0 || !boxes_meet(&b, &s_circle));
    for (int j = i + 1; j < s_ntext; j++) {
        const box_t o = text_box(j);
        ok = ok && !boxes_meet(&b, &o);
    }
    return ok;
}

// A question of the OS with its note of a refused tap: all of the name, of the line and of the note shows, and each
// line of them fits (text_fits).
static void test_fit(const char *frame_name, const char *title, const char *line, const char *yes)
{
    char what[320];
    int k = 0;
    s_answers = 0;
    sysui_ask(title, line, yes, "NO", 120, answer);
    sysui_ask_finger();
    frame();
    const box_t panel = s_box[0], yb = s_box[1], nb = s_box[2];
    wait_ms(2000);
    tap_box(&yb, true, 50);
    frame();
    snprintf(what, sizeof what, "%s: \"%s\" / \"%s\": the name, the line and the note show in full", frame_name, title,
             line);
    expect(text_lines(title, &k) > 0 && text_lines(line, &k) > 0 && note_lines(yes, &k) > 0, what);
    for (int i = 0; i < s_ntext; i++) {
        snprintf(what, sizeof what,
                 "%.30s: \"%.120s\" fits (in the panel, not on a button, the avatar or another text)", frame_name,
                 s_text[i]);
        expect(text_fits(i, &panel, &yb, &nb), what);
    }
    sysui_ask_close();
}

// A line that is too long for the lines of the frame (a permission line of 95 wide letters): all of it shows, or its
// start and "..." at the end of its last line (not cut in the middle with no mark); each line fits (text_fits).
static void test_fit_cut(const char *frame_name, const char *line)
{
    char what[320];
    int k = 0;
    sysui_ask("UPDATE MEGA WORLD WAR MAMMOTH MEMORY MASTERS?", line, "UPDATE", "NO", 120, answer);
    frame();
    const box_t panel = s_box[0], yb = s_box[1], nb = s_box[2];
    snprintf(what, sizeof what, "%s: a line that is too long shows in full, or its start with \"...\"", frame_name);
    expect(text_lines("UPDATE MEGA WORLD WAR MAMMOTH MEMORY MASTERS?", &k) > 0 &&
                   (text_lines(line, &k) > 0 || text_lines_as(line, &k, true) > 0),
           what);
    for (int i = 0; i < s_ntext; i++) {
        snprintf(what, sizeof what,
                 "%.30s: \"%.120s\" fits (in the panel, not on a button, the avatar or another text)", frame_name,
                 s_text[i]);
        expect(text_fits(i, &panel, &yb, &nb), what);
    }
    sysui_ask_close();
}

// the questions of the OS (the longest texts that they can have)
static void test_fit_all(const char *frame_name)
{
    test_fit(frame_name, "TRUST THE KEY TEST?", "AB12 CD34 EF56 TEST", "ALLOW");
    test_fit(frame_name, "TRUST THE KEY ABCDEFGHIJKLMNO?", "7B9A-563C-BE1B-2251 STORE+DEV+TEST", "ALLOW");
    test_fit(frame_name, "TURN ON THE DEVELOPER MODE?", "APPS WITH NO SIGNATURE CAN RUN", "TURN ON");
    test_fit(frame_name, "A NEW HOME FROM YOUR MANAGER", "USE IT, OR KEEP YOUR OWN HOME?", "USE IT");
    test_fit(frame_name, "INSTALL SERIAL MONITOR?", "IT CAN USE THE MICROPHONE, THE CAMERA AND GPIO", "INSTALL");
    test_fit(frame_name, "LEAVE THE POOL FAMILY HOME?", "THE LINK OF THE POOL STOPS", "LEAVE");
    // a name of 23 bytes and a permission line of many classes (the line of the STORE has up to 95 bytes)
    test_fit(frame_name, "UPDATE MEGA WORLD WAR MAMMOTH MEMORY MASTERS?",
             "IT CAN USE THE MICROPHONE, THE CAMERA, GPIO, THE SD CARD, THE RADIO, BLUETOOTH, THE USB PORT", "UPDATE");
    test_fit_cut(frame_name, "MMMMMMMMM MMMMMMMMM MMMMMMMMM MMMMMMMMM MMMMMMMMM MMMMMMMMM MMMMMMMMM MMMMMMMMM "
                             "MMMMMMMMM MMMMM");
}

// one frame: the layout test and the fit test
static void test_frame(int w, int h)
{
    char name[32];
    gfx_w = w;
    gfx_h = h;
    snprintf(name, sizeof name, "%d x %d", w, h);
    test_layout(name);
    test_fit_all(name);
}

#if !BOARD_ROUND  // (the test of SETUP: the layout of the 3.49)
// ---------------------------------------------------------------- SETUP > DEVELOPER MODE
// an event as main.c gives it: the system layer first; SETUP gets it only when the layer does not take it
static void setup_event(input_kind_t k, int x, int y, bool inj)
{
    const input_event_t ev = {.kind = k, .x = (int16_t)x, .y = (int16_t)y, .t_ms = now_ms()};
    s_inj = inj;
    if (!sysui_input(&ev)) {
        app_settings.input(&ev);
    }
}

static void setup_tap(int x, int y, bool inj)
{
    setup_event(IN_DOWN, x, y, inj);
    wait_ms(60);
    setup_event(IN_UP, x, y, inj);
    setup_event(IN_TAP, x, y, inj);
}

// a tap on the button of the row DEVELOPER MODE (the list is at the place where the row is the first one)
static bool tap_dev_row(bool inj)
{
    int i = 0;
    while (i < set_nrows && set_rows[i] != R_DEV) {
        i++;
    }
    if (i == set_nrows) {
        return false;
    }
    set_scroll = (float)(i * ROW_H);
    setup_tap(RIGHT_X + RIGHT_W / 2, set_row_y(i) + CTL_Y + CTL_H / 2, inj);
    return true;
}

static void test_setup_dev(void)
{
    int y = 0;
    app_settings.init();
    app_settings.enter(now_ms());
    tfn_set_dev_mode(false);
    // a tap of the web on the row: the question, the mode stays off
    expect(tap_dev_row(true), "SETUP has the row DEVELOPER MODE");
    expect(!tfn_dev_mode() && sysui_active(), "SETUP: a tap on DEVELOPER MODE asks first; the mode stays off");
    frame();
    expect(s_nbox >= 3 && drawn("APPS WITH NO SIGNATURE CAN RUN", &y), "SETUP: the question of the developer mode");
    const box_t yes = s_box[1], no = s_box[2];
    // a tap of the web on TURN ON (after the arm time): no answer, the note
    wait_ms(2000);
    setup_tap(yes.x + yes.w / 2, yes.y + yes.h / 2, true);
    frame();
    expect(!tfn_dev_mode() && sysui_active() && drawn("A REMOTE TAP CANNOT TURN ON", &y),
           "SETUP: a tap of the web on TURN ON does not turn the developer mode on");
    // a finger on TURN ON: the mode is on
    setup_tap(yes.x + yes.w / 2, yes.y + yes.h / 2, false);
    expect(tfn_dev_mode() && !sysui_active(), "SETUP: a finger on TURN ON turns the developer mode on");
    // the mode is on: a tap (also of the web) turns it off at once, with no question
    tap_dev_row(true);
    expect(!tfn_dev_mode() && !sysui_active(), "SETUP: a tap on DEVELOPER MODE when it is on: off, no question");
    // NO: the question closes, the mode stays off
    tap_dev_row(false);
    frame();
    wait_ms(2000);
    setup_tap(no.x + no.w / 2, no.y + no.h / 2, false);
    expect(!tfn_dev_mode() && !sysui_active(), "SETUP: NO closes the question, the developer mode stays off");
    app_settings.leave();
}

#endif

// The frames: the 3.49 (640 x 172, 172 x 640 in portrait), the CoreS3 and the AMOLED (320 x 240, 240 x 320), the Tab5
// (640 x 360, 360 x 640). A build for a round board kind (BOARD_ROUND): the round frames of 412 and 466 px.
int main(void)
{
#if BOARD_ROUND
    test_frame(412, 412);
    test_frame(466, 466);
#else
    test_frame(640, 172);
    test_setup_dev();  // (the layout of SETUP of the 3.49: the frame 640 x 172)
    test_frame(172, 640);
    test_frame(320, 240);
    test_frame(240, 320);
    test_frame(640, 360);
    test_frame(360, 640);
#endif
    printf("SYSUI ASK TESTS%s %s\n", BOARD_ROUND ? " (ROUND)" : "", s_fails ? "FAILED" : "OK");
    return s_fails ? 1 : 0;
}
