// The unit test of the two consent pages of SETUP: TRUSTED PUBLISHERS (main/tfn_pub_page.c) and POOL
// (main/pool_page.c).
// The test sends events and render frames, as the app task of SETUP does. It gives the fake publisher wait, the fake
// pool service, the fake input_injected() (the answer of the touch task for each event) and records the drawing (each
// text, its place and its colour).
// TRUSTED PUBLISHERS: ALLOW acts only for a finger, of a touch that came down INPUT_ARM_MS or more after the row of the
// key showed; a tap of the web gets the note; ALLOW shows as on only after the arm time; a new wait (also the same key
// with other flags) starts the rule again; ALLOW and NO answer only the wait that the row showed (its number).
// POOL: ALLOW of the 6 digits acts only for the same rule; new digits or a new JOIN step start the rule again; ALLOW
// gives the digits that the page showed; LEAVE is a consent question (sysui_ask_finger).
// Build and run: python tests/unit/run.py
#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "friends.h"
#include "gfx.h"
#include "input.h"
#include "pool_page.h"
#include "settings.h"
#include "sysui.h"
#include "tfn/theme.h"
#include "tfn_id.h"
#include "tfn_pool.h"
#include "tfn_pub.h"

static int s_fails;

static void expect(bool ok, const char *what)
{
    if (!ok) {
        s_fails++;
        printf("FAIL %s\n", what);
    }
}

// ---------------------------------------------------------------- the fakes of the OS
static int64_t s_now = 100000;  // the app clock (ms)
static bool s_inj;              // input_injected() of the next event: true = not by a finger on the panel
static int s_back_sounds;
int gfx_w = 640, gfx_h = 172;

int64_t esp_timer_get_time(void)
{
    return s_now * 1000;
}

bool input_injected(const input_event_t *ev)
{
    (void)ev;
    return s_inj;
}

void input_tap_cancel(void)
{
}

void sfx_ui(sfx_t sfx)
{
    s_back_sounds += sfx == SFX_BACK;
}

uint32_t store_get(const char *key, uint32_t def)
{
    (void)key;
    return def;  // (pool_fake 0: the page uses the API of the pool service)
}

void store_set(const char *key, uint32_t value)
{
    (void)key;
    (void)value;
}

const char *friends_my_name(void)
{
    return "DESK";
}

const char *tfn_id_str(void)
{
    return "3F2A-91C0-5B7E-0D44";
}

// the questions of the system layer (REMOVE, LEAVE): the last one, its answer function, a consent or not
static int s_asks;
static bool s_ask_finger;
static char s_ask_yes[16];
static void (*s_ask_done)(int);

void sysui_ask(const char *title, const char *line, const char *yes, const char *no, int secs, void (*done)(int))
{
    (void)title;
    (void)line;
    (void)no;
    (void)secs;
    snprintf(s_ask_yes, sizeof s_ask_yes, "%s", yes ? yes : "");
    s_ask_done = done;
    s_ask_finger = false;
    s_asks++;
}

void sysui_ask_finger(void)
{
    s_ask_finger = true;
}

// the drawing: each text of a frame with its place and its colour (gfx_text_center_in: x, w; gfx_text: x, w = 0)
typedef struct {
    char t[96];
    int x, w, y;
    uint16_t c;
} text_t;
static text_t s_text[64];
static int s_ntext;

static void text_add(int x, int w, int y, const char *s, uint16_t c)
{
    if (s_ntext < 64) {
        text_t *t = &s_text[s_ntext++];
        snprintf(t->t, sizeof t->t, "%s", s);
        t->x = x;
        t->w = w;
        t->y = y;
        t->c = c;
    }
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

void gfx_text(int x, int y, const char *s, int scale, uint16_t c)
{
    (void)scale;
    text_add(x, 0, y, s, c);
}

void gfx_text_center_in(int x, int w, int y, const char *s, int scale, uint16_t c)
{
    (void)scale;
    text_add(x, w, y, s, c);
}

int gfx_text_width(const char *s, int scale)
{
    return (int)strlen(s) * 6 * scale;
}

// (the pages draw the text of gfx_text in a real face: main/set_text.h maps those names to ui_dot_text*; here the same
// record of each text)
void ui_dot_text(int x, int y, const char *s, int sc, uint16_t c)
{
    gfx_text(x, y, s, sc, c);
}
void ui_dot_text_in(int x, int w, int y, const char *s, int sc, uint16_t c)
{
    gfx_text_center_in(x, w, y, s, sc, c);
}
void ui_dot_text_c(int y, const char *s, int sc, uint16_t c)
{
    gfx_text_center_in(0, gfx_w, y, s, sc, c);
}
int ui_dot_text_w(const char *s, int sc)
{
    return gfx_text_width(s, sc);
}

// ---------------------------------------------------------------- the fake publisher wait (tfn_pub.c)
static bool s_wait_on;
static tfn_pub_t s_wait_key;
static uint32_t s_wait_seq;
static int s_answers;
static bool s_ans_allow;
static uint32_t s_ans_seq;

bool tfn_pub_waiting(tfn_pub_t *out, int *secs, uint32_t *seq)
{
    if (!s_wait_on) {
        return false;
    }
    *out = s_wait_key;
    *secs = 100;
    if (seq) {
        *seq = s_wait_seq;
    }
    return true;
}

bool tfn_pub_answer(bool allow, uint32_t seq, char *msg, int len)
{
    s_answers++;
    s_ans_allow = allow;
    s_ans_seq = seq;
    if (!s_wait_on || seq != s_wait_seq) {
        snprintf(msg, len, "the wait of the question ended: no answer");
        return false;
    }
    s_wait_on = false;
    snprintf(msg, len, "%s", allow ? "the key is in the list" : "the board owner did not allow the key");
    return allow;
}

int tfn_pub_builtin_count(void)
{
    return 1;
}

bool tfn_pub_builtin_id(int i, uint8_t id[TFN_PUB_ID])
{
    memset(id, 0xB0, TFN_PUB_ID);
    return i == 0;
}

int tfn_pub_count(void)
{
    return 0;
}

bool tfn_pub_get(int i, tfn_pub_t *out)
{
    (void)i;
    (void)out;
    return false;
}

void tfn_pub_key_id_of(const tfn_pub_t *p, uint8_t id[TFN_PUB_ID])
{
    memcpy(id, p->pk + 1, TFN_PUB_ID);
}

bool tfn_pub_remove_id(const uint8_t id[TFN_PUB_ID], char *msg, int len)
{
    (void)id;
    snprintf(msg, len, "no key");
    return false;
}

bool tfn_pub_request_card(char *msg, int len)
{
    snprintf(msg, len, "no SD card");
    return false;
}

// ---------------------------------------------------------------- the fake pool service (tfn_pool.c)
static bool s_in_pool;
static tfn_pool_join_t s_js = TFN_POOL_JOIN_OFF;
static char s_digits[TFN_POOL_SAS_LEN] = "482913";
static char s_allowed[TFN_POOL_SAS_LEN];
static int s_allows, s_leaves;

tfn_pool_state_t tfn_pool_state(void)
{
    return s_in_pool ? TFN_POOL_OK : s_js != TFN_POOL_JOIN_OFF ? TFN_POOL_JOIN : TFN_POOL_NONE;
}

int tfn_pool_my_role(void)
{
    return s_in_pool ? TFN_POOL_ROLE_MEMBER : -1;
}

bool tfn_pool_on(void)
{
    return s_in_pool;
}

int tfn_pool_members(void)
{
    return s_in_pool ? 2 : 0;
}

bool tfn_pool_member_get(int slot, tfn_pool_member_t *out)
{
    if (!s_in_pool || slot < 0 || slot > 1) {
        return false;
    }
    memset(out, 0, sizeof *out);
    out->id[0] = (uint8_t)(0x10 + slot);
    out->mac[5] = (uint8_t)slot;
    out->role = slot ? TFN_POOL_ROLE_MEMBER : TFN_POOL_ROLE_MANAGER;
    snprintf(out->name, sizeof out->name, "%s", slot ? "DESK" : "PC");
    return true;
}

void tfn_pool_counts(uint32_t out[TFN_POOL_CNT_N])
{
    memset(out, 0, sizeof(uint32_t) * TFN_POOL_CNT_N);
}

tfn_pool_join_t tfn_pool_join_step(void)
{
    return s_js;
}

int tfn_pool_join_left_s(void)
{
    return 500;
}

const char *tfn_pool_name(void)
{
    return s_in_pool ? "HOME" : "";
}

const char *tfn_pool_join_why(void)
{
    return "";
}

bool tfn_pool_join_sas(char digits[TFN_POOL_SAS_LEN], char mgr[17])
{
    if (s_js != TFN_POOL_JOIN_ASK) {
        return false;
    }
    snprintf(digits, TFN_POOL_SAS_LEN, "%s", s_digits);
    snprintf(mgr, 17, "PC");
    return true;
}

bool tfn_pool_join_start(void)
{
    s_js = TFN_POOL_JOIN_WAIT;
    return true;
}

void tfn_pool_join_stop(void)
{
    s_js = TFN_POOL_JOIN_OFF;
}

bool tfn_pool_join_allow(const char *digits)
{
    s_allows++;
    snprintf(s_allowed, sizeof s_allowed, "%s", digits);
    return s_js == TFN_POOL_JOIN_ASK && !strcmp(digits, s_digits);
}

void tfn_pool_join_no(void)
{
    s_js = TFN_POOL_JOIN_FAIL;
}

bool tfn_pool_leave(bool by_screen)
{
    (void)by_screen;
    s_leaves++;
    s_in_pool = false;
    return true;
}

// ---------------------------------------------------------------- the steps of the test
static void wait_ms(int ms)
{
    s_now += ms;
}

static const text_t *drawn(const char *t)
{
    for (int i = 0; i < s_ntext; i++) {
        if (!strcmp(s_text[i].t, t)) {
            return &s_text[i];
        }
    }
    return NULL;
}

static bool drawn_part(const char *part)
{
    for (int i = 0; i < s_ntext; i++) {
        if (strstr(s_text[i].t, part)) {
            return true;
        }
    }
    return false;
}

// the pages (pub = TRUSTED PUBLISHERS, else POOL): one frame (update, render), a touch (DOWN, UP, TAP)
static void page_frame(bool pub)
{
    s_ntext = 0;
    if (pub) {
        tfn_pub_page_update(s_now);
        tfn_pub_page_render(s_now);
    } else {
        pool_page_update(s_now);
        pool_page_render(s_now);
    }
}

static void page_event(bool pub, input_kind_t k, int x, int y, bool inj)
{
    const input_event_t ev = {.kind = k, .x = (int16_t)x, .y = (int16_t)y, .t_ms = s_now};
    s_inj = inj;
    if (pub) {
        tfn_pub_page_input(&ev, s_now);
    } else {
        pool_page_input(&ev, s_now);
    }
}

// a tap on the pill with the text t of the last frame (false: the frame has no such pill)
static bool tap_pill(bool pub, const char *t, bool inj)
{
    const text_t *p = drawn(t);
    if (!p || !p->w) {
        return false;
    }
    const int x = p->x + p->w / 2, y = p->y + 4;
    page_event(pub, IN_DOWN, x, y, inj);
    wait_ms(60);
    page_event(pub, IN_UP, x, y, inj);
    page_event(pub, IN_TAP, x, y, inj);
    return true;
}

// ALLOW of the last frame shows as on (the colour of the text of a pill that is on)
static bool allow_on(void)
{
    const text_t *p = drawn("ALLOW");
    return p && p->c == THEME_NOTE_PAGE_PILL_TEXT_ON;
}

static tfn_pub_t key(const char *name, int flags)
{
    tfn_pub_t p;
    memset(&p, 0, sizeof p);
    p.pk[0] = 0x04;
    for (int i = 1; i < TFN_PUB_PK; i++) {
        p.pk[i] = (uint8_t)(0x5D + i);
    }
    snprintf(p.name, sizeof p.name, "%s", name);
    p.flags = (uint8_t)flags;
    return p;
}

// a new wait of the web (between two frames)
static void web_wait(const char *name, int flags)
{
    s_wait_key = key(name, flags);
    s_wait_seq++;
    s_wait_on = true;
}

static void test_pub_page(void)
{
    s_wait_seq = 6;
    web_wait("TEST1", TFN_PUB_DEV);
    tfn_pub_page_open();
    page_frame(true);
    expect(drawn("NEW: TEST1") && drawn_part("DEV, 100 S"), "PUB: the row of the key that waits");
    expect(drawn("ALLOW") && !allow_on(), "PUB: ALLOW is not on in the first frame of the row");
    // a tap of the web on ALLOW after the arm time: no answer, the note in the row ADD A KEY, a sound
    wait_ms(2000);
    page_frame(true);
    expect(allow_on(), "PUB: ALLOW is on INPUT_ARM_MS after the row showed");
    const int sounds = s_back_sounds;
    expect(tap_pill(true, "ALLOW", true), "PUB: a tap on ALLOW");
    page_frame(true);
    expect(s_answers == 0 && s_wait_on && s_back_sounds == sounds + 1 && drawn("A REMOTE TAP CANNOT ALLOW"),
           "PUB: a tap of the web on ALLOW: no answer, the note, a sound");
    // the same key with other flags (a new wait of the web): the rule starts again, the row shows the new flags
    s_wait_on = false;
    web_wait("FRIEND", TFN_PUB_STORE | TFN_PUB_TEST);
    page_frame(true);
    expect(drawn("NEW: FRIEND") && drawn_part("STORE+TEST") && !allow_on(),
           "PUB: a new wait of the same key: its flags, ALLOW not on");
    wait_ms(100);
    tap_pill(true, "ALLOW", false);
    expect(s_answers == 0 && s_wait_on, "PUB: a finger 0.1 s after the new wait showed: no answer");
    wait_ms(INPUT_ARM_MS);
    page_frame(true);
    expect(allow_on(), "PUB: ALLOW is on again after the arm time");
    tap_pill(true, "ALLOW", false);
    expect(s_answers == 1 && s_ans_allow && s_ans_seq == s_wait_seq && !s_wait_on,
           "PUB: a finger after the arm time: ALLOW for the number of the wait that the row showed");
    // a new wait between the frame and the tap: no ALLOW; NO answers only the wait of the frame
    web_wait("TEST2", TFN_PUB_DEV);
    page_frame(true);
    wait_ms(2000);
    page_frame(true);
    const uint32_t shown = s_wait_seq;
    s_wait_on = false;
    web_wait("TEST2", TFN_PUB_STORE);  // (the web: the same key with other flags, after the frame)
    tap_pill(true, "ALLOW", false);
    expect(s_answers == 1 && s_wait_on, "PUB: a new wait after the last frame: no ALLOW for a finger");
    tap_pill(true, "NO", false);
    expect(s_answers == 2 && !s_ans_allow && s_ans_seq == shown && s_wait_on,
           "PUB: NO answers only the wait that the row showed (the new wait still waits)");
    s_wait_on = false;
}

static void test_pool_page(void)
{
    // the JOIN screen: the 6 digits
    s_in_pool = false;
    s_js = TFN_POOL_JOIN_WAIT;
    pool_page_open();
    page_frame(false);
    s_js = TFN_POOL_JOIN_ASK;
    snprintf(s_digits, sizeof s_digits, "482913");
    page_frame(false);
    expect(drawn("482 913") && drawn("ALLOW") && !allow_on(), "POOL: the 6 digits, ALLOW not on in the first frame");
    wait_ms(2000);
    page_frame(false);
    expect(allow_on(), "POOL: ALLOW is on INPUT_ARM_MS after the digits showed");
    const int sounds = s_back_sounds;
    tap_pill(false, "ALLOW", true);
    page_frame(false);
    expect(s_allows == 0 && s_back_sounds == sounds + 1 && drawn("A REMOTE TAP CANNOT ALLOW"),
           "POOL: a tap of the web on ALLOW: no ALLOW, the note, a sound");
    // new digits (another pairing): the rule starts again
    snprintf(s_digits, sizeof s_digits, "123456");
    page_frame(false);
    expect(drawn("123 456") && !allow_on(), "POOL: new digits: ALLOW not on");
    wait_ms(100);
    tap_pill(false, "ALLOW", false);
    expect(s_allows == 0, "POOL: a finger 0.1 s after the new digits showed: no ALLOW");
    wait_ms(INPUT_ARM_MS);
    page_frame(false);
    tap_pill(false, "ALLOW", false);
    expect(s_allows == 1 && !strcmp(s_allowed, "123456"),
           "POOL: a finger after the arm time: ALLOW of the shown digits");
    // a new JOIN step, then the digits again: the rule starts again
    s_js = TFN_POOL_JOIN_ALLOWED;
    page_frame(false);
    s_js = TFN_POOL_JOIN_ASK;
    page_frame(false);
    wait_ms(100);
    tap_pill(false, "ALLOW", false);
    expect(s_allows == 1, "POOL: a new step of the JOIN: no ALLOW 0.1 s after the digits showed again");
    pool_page_leave();
    // LEAVE: a consent question (the system layer takes only a finger)
    s_in_pool = true;
    s_js = TFN_POOL_JOIN_OFF;
    pool_page_open();
    page_frame(false);
    s_asks = 0;
    expect(tap_pill(false, "LEAVE", true), "POOL: the pill LEAVE");
    expect(s_asks == 1 && s_ask_finger && !strcmp(s_ask_yes, "LEAVE") && s_leaves == 0,
           "POOL: LEAVE asks a consent question (sysui_ask_finger) and does not leave yet");
    if (s_ask_done) {
        s_ask_done(0);
    }
    expect(s_leaves == 0 && s_in_pool, "POOL: KEEP of the question: the board stays in the pool");
    if (s_ask_done) {
        s_ask_done(1);
    }
    expect(s_leaves == 1 && !s_in_pool, "POOL: LEAVE of the question: the board leaves the pool");
}

int main(void)
{
    test_pub_page();
    test_pool_page();
    printf("SETUP PAGES TESTS %s\n", s_fails ? "FAILED" : "OK");
    return s_fails ? 1 : 0;
}
