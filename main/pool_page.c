// SETUP > POOL (tfn_pool.h, design P4 stage 2): the pool of this board and its members, LEAVE, and JOIN.
// THE LIST: the state row (the pool name, the role of this board, the link counts; LEAVE), then one row for each
// member (the name, the role, the MAC, the device ID). With no pool: the state row and the row JOIN A POOL.
// THE JOIN SCREEN (while a JOIN runs): the board waits for the manager, then shows the 6 digits of the pairing (the
// SAS: "MANAGER <name> WANTS TO MANAGE THIS BOARD: 482 913") with NO and ALLOW, then waits for the pool data.
// ALLOW and LEAVE act only for the rule of a consent (input.h): a tap by a finger on the panel, of a touch that came
// down INPUT_ARM_MS or more after the question (the 6 digits) showed. A tap of the console or the web (/api/ctl) cannot
// change the pool. The USB console has its own commands for that (pool allow <digits>, pool leave). ALLOW gives the
// 6 digits that the page showed: other digits (a new pairing) end the JOIN.
// The look of SETUP > TRUSTED PUBLISHERS (the same theme names, the momentum scroll of libs/kscroll).
// A host build (the simulator, the host fuzz) can show a FAKE pool: the store key "pool_fake" (fake_* below).
#include "pool_page.h"
#include "tfn_pool.h"
#include "tfn/theme.h"
#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "board.h"
#include "friends.h"
#include "gfx.h"
#include "settings.h"
#include "sysui.h"
#include "tfn_id.h"
#include "tfn/tfn_util.h"
#include "../libs/kscroll/kscroll.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"  // (EXT_RAM_BSS_ATTR: the consent rule and its digits in PSRAM: the internal RAM is short)
#endif
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif

#define BACK_W 56
#define PILL_H 36
#define NROWS  (2 + TFN_POOL_MEMBERS_MAX)

enum {
    RK_STATE,   // the pool, or NO POOL, or POOL DAMAGED: LEAVE
    RK_JOIN,    // JOIN A POOL (no pool)
    RK_MEMBER,  // a member of the roster
};
typedef struct {
    uint8_t kind, slot;
} prow_t;

// the values that the page shows (the API of tfn_pool.h, or the fake pool of a host build)
typedef struct {
    tfn_pool_state_t st;
    int role;  // the role of this board (-1 = no pool)
    int n;     // the count of the member slots
    tfn_pool_join_t js;
    int left_s;
    const char *name;  // the pool name
    const char *why;   // the cause of TFN_POOL_JOIN_FAIL
} pview_t;

static prow_t s_rows[NROWS];
static int s_top[NROWS + 1];  // the top of each row in the list (px); s_top[s_nrows] = the length of the list
static int s_nrows, s_press = -1;
static bool s_back;
static kscroll_t s_ks;
static char s_note[64];            // the answer of the last LEAVE, JOIN or ALLOW
static tfn_pool_join_t s_js_seen;  // the JOIN step of the last frame
// the consent rule of ALLOW (input.h): a new step or new digits start it again (PSRAM: the page reads it only once in
// each frame or event)
static input_consent_t s_cons EXT_RAM_BSS_ATTR;
static char s_sas[TFN_POOL_SAS_LEN], s_mgr[17];
static char s_sas_seen[TFN_POOL_SAS_LEN] EXT_RAM_BSS_ATTR;  // the digits of the consent rule

// ---------------------------------------------------------------- the fake pool of the host tests

#ifndef ESP_PLATFORM
// THE FAKE POOL (only a host build: the golden frames and the host fuzz show each screen with no manager). The store
// key pool_fake: 0 = the real API; 1 = a pool (this board a MEMBER); 2 = a pool (this board a CHILD); 3 = a damaged
// pool; 4 = JOIN: wait for the manager; 5 = JOIN: the 6 digits; 6 = JOIN: wait for the pool data; 7 = JOIN: done;
// 8 = JOIN: failed; 9 = no pool. The buttons change the key as the service changes its state.
enum {
    FK_OFF,
    FK_POOL,
    FK_CHILD,
    FK_DAMAGED,
    FK_WAIT,
    FK_ASK,
    FK_ALLOWED,
    FK_DONE,
    FK_FAIL,
    FK_NONE,
    FK_N
};
static const tfn_pool_member_t FK_M[] = {
        {.id = {0x9b, 0x10, 0x4e, 0x22, 0x7a, 0xc1, 0x05, 0x6d},
         .mac = {0xfe, 0x4d, 0x47, 0x52, 0x00, 0x01},
         .role = 1,
         .flags = 0,
         .name = "MANAGER"},
        {.id = {0x3f, 0x2a, 0x91, 0xc0, 0x5b, 0x7e, 0x0d, 0x44},
         .mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x11},
         .role = 0,
         .flags = 0,
         .name = "DESK"},
        {.id = {0x71, 0x08, 0xd2, 0x4b, 0x33, 0x90, 0xae, 0x1f},
         .mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x22},
         .role = 0,
         .flags = 0,
         .name = "KITCHEN"},
        {.id = {0x0c, 0x6e, 0x5a, 0xf1, 0x84, 0x27, 0xb9, 0x30},
         .mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x33},
         .role = 0,
         .flags = 0,
         .name = "GAMES TABLE"},
        {.id = {0xe4, 0x55, 0x19, 0x0a, 0xc8, 0x6b, 0x72, 0x9d},
         .mac = {0x02, 0x00, 0x00, 0x00, 0x00, 0x44},
         .role = 2,
         .flags = 0,
         .name = "ROUND ONE"},
};
#define FK_NM ((int)(sizeof FK_M / sizeof FK_M[0]))
#define FK_ME 1  // the slot of this board

static int fake(void)
{
    const uint32_t f = store_get("pool_fake", 0);
    return f < FK_N ? (int)f : FK_OFF;
}
static void fake_set(int f)
{
    store_set("pool_fake", (uint32_t)f);
}
static void fake_get(int f, pview_t *v)
{
    static const tfn_pool_join_t JS[FK_N] = {
            TFN_POOL_JOIN_OFF, TFN_POOL_JOIN_OFF,     TFN_POOL_JOIN_OFF,  TFN_POOL_JOIN_OFF,  TFN_POOL_JOIN_WAIT,
            TFN_POOL_JOIN_ASK, TFN_POOL_JOIN_ALLOWED, TFN_POOL_JOIN_DONE, TFN_POOL_JOIN_FAIL, TFN_POOL_JOIN_OFF};
    const bool in = f == FK_POOL || f == FK_CHILD || f == FK_DONE;
    v->st = in                           ? TFN_POOL_OK
            : f == FK_DAMAGED            ? TFN_POOL_DAMAGED
            : JS[f] != TFN_POOL_JOIN_OFF ? TFN_POOL_JOIN
                                         : TFN_POOL_NONE;
    v->role = !in ? -1 : f == FK_CHILD ? TFN_POOL_ROLE_CHILD : TFN_POOL_ROLE_MEMBER;
    v->n = in ? FK_NM : 0;
    v->js = JS[f];
    v->left_s = 581;
    v->name = in ? "HOME" : "";
    v->why = f == FK_FAIL ? "the time limit" : "";
}
#define FAKE() fake()
#else
#define FAKE() 0
#endif

// ---------------------------------------------------------------- the values of the pool

static void pv_get(pview_t *v)
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        fake_get(FAKE(), v);
        return;
    }
#endif
    v->st = tfn_pool_state();
    v->role = tfn_pool_my_role();
    v->n = tfn_pool_on() ? tfn_pool_members() : 0;
    v->js = tfn_pool_join_step();
    v->left_s = tfn_pool_join_left_s();
    v->name = tfn_pool_name();
    v->why = tfn_pool_join_why();
}

static bool pv_member(int slot, tfn_pool_member_t *m)
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        if (slot < 0 || slot >= FK_NM) {
            return false;
        }
        *m = FK_M[slot];
        return true;
    }
#endif
    return tfn_pool_member_get(slot, m);
}

static void pv_counts(uint32_t c[TFN_POOL_CNT_N])
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        static const uint32_t C[TFN_POOL_CNT_N] = {18342, 12, 0, 1, 230};
        memcpy(c, C, sizeof C);
        return;
    }
#endif
    tfn_pool_counts(c);
}

// the device ID text of 8 bytes, in the form of tfn_id_str(): "3F2A-91C0-5B7E-0D44"
static void id_text(const uint8_t id[8], char out[TFN_ID_LEN])
{
    snprintf(out, TFN_ID_LEN, "%02X%02X-%02X%02X-%02X%02X-%02X%02X", id[0], id[1], id[2], id[3], id[4], id[5], id[6],
             id[7]);
}

static bool pv_is_me(int slot, const tfn_pool_member_t *m)
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        return slot == FK_ME;
    }
#endif
    char t[TFN_ID_LEN];
    id_text(m->id, t);
    return tfn_id_str()[0] && strcmp(t, tfn_id_str()) == 0;
}

static bool pv_sas(void)
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        snprintf(s_sas, sizeof s_sas, "482913");
        snprintf(s_mgr, sizeof s_mgr, "MANAGER");
        return FAKE() == FK_ASK;
    }
#endif
    return tfn_pool_join_sas(s_sas, s_mgr);
}

static bool pv_join_start(void)
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        fake_set(FK_WAIT);
        return true;
    }
#endif
    return tfn_pool_join_start();
}

static void pv_join_stop(void)
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        fake_set(FAKE() == FK_DONE ? FK_POOL : FK_NONE);
        return;
    }
#endif
    tfn_pool_join_stop();
}

static bool pv_allow(void)
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        fake_set(FK_ALLOWED);
        return true;
    }
#endif
    return tfn_pool_join_allow(s_sas);  // (the digits that the page showed: other digits end the JOIN)
}

static void pv_no(void)
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        fake_set(FK_FAIL);
        return;
    }
#endif
    tfn_pool_join_no();
}

static bool pv_leave(void)
{
#ifndef ESP_PLATFORM
    if (FAKE()) {
        if (FAKE() == FK_CHILD) {
            return false;
        }
        fake_set(FK_NONE);
        return true;
    }
#endif
    return tfn_pool_leave(true);
}

static const char *role_name(int role)
{
    return role == TFN_POOL_ROLE_MANAGER ? "MANAGER" : role == TFN_POOL_ROLE_CHILD ? "CHILD" : "MEMBER";
}

// capitals only (the dot font of SETUP has no small letters)
static void up(char *s)
{
    for (; *s; s++) {
        if (*s >= 'a' && *s <= 'z') {
            *s = (char)(*s - 32);
        }
    }
}

// ---------------------------------------------------------------- the layout

static bool wide(void)
{
    return gfx_w >= 480;
}
static int head_h(void)
{
    return 30;
}
static bool join_view(const pview_t *v)
{
    return v->js != TFN_POOL_JOIN_OFF;
}

// the pills of a list row: the count (k = 0 only); x, w of the pill
static int row_pills(int r, const pview_t *v)
{
    if (s_rows[r].kind == RK_JOIN) {
        return 1;
    }
    return s_rows[r].kind == RK_STATE && v->st == TFN_POOL_OK && v->role != TFN_POOL_ROLE_CHILD ? 1 : 0;
}
static int row_h(int r, const pview_t *v)
{
    return !wide() && row_pills(r, v) ? 96 : 52;  // (the 320 x 240 frame: a pill under the three lines of text)
}

static void rows_make(const pview_t *v)
{
    int n = 0;
    s_rows[n++] = (prow_t){RK_STATE, 0};
    if (v->st != TFN_POOL_OK) {
        s_rows[n++] = (prow_t){RK_JOIN, 0};
    }
    for (int i = 0; i < v->n && i < TFN_POOL_MEMBERS_MAX; i++) {
        tfn_pool_member_t m;
        if (pv_member(i, &m)) {
            s_rows[n++] = (prow_t){RK_MEMBER, (uint8_t)i};
        }
    }
    s_nrows = n;
    s_top[0] = 0;
    for (int r = 0; r < n; r++) {
        s_top[r + 1] = s_top[r] + row_h(r, v);
    }
}

static void list_pill(int top, int r, int *x, int *y, int *w)
{
    *w = wide() ? 110 : 84;
    *x = gfx_w - 8 - *w;
    *y = wide() ? top + (s_top[r + 1] - s_top[r] - PILL_H) / 2 : top + 52;
}

// the pills of the JOIN screen: k = 0 (the left one), 1 (the right one); the count, and the text of each
static int join_pills(const pview_t *v, const char **t)
{
    t[0] = t[1] = "";
    switch (v->js) {
    case TFN_POOL_JOIN_ASK:
        t[0] = "NO";
        t[1] = "ALLOW";
        return 2;
    case TFN_POOL_JOIN_DONE:
        t[0] = "OK";
        return 1;
    case TFN_POOL_JOIN_FAIL:
        t[0] = "AGAIN";
        t[1] = "OK";
        return 2;
    default:
        t[0] = "CANCEL";
        return 1;
    }
}
// x, y, w of the JOIN pill k of n: one pill at the right; two pills = the left and the right end of the page
static void join_pill(int k, int n, int *x, int *y, int *w)
{
    *w = wide() ? 150 : 110;
    *y = gfx_h - PILL_H - 8;
    *x = n == 2 && k == 0 ? BACK_W + 10 : gfx_w - 10 - *w;
}

// the pill under (x, y): r * 4 + k (the list), 100 + k (the JOIN screen), or -1
static int hit(int x, int y, const pview_t *v)
{
    if (y < head_h()) {
        return -1;
    }
    int px, py, pw;
    if (join_view(v)) {
        const char *t[2];
        const int n = join_pills(v, t);
        for (int k = 0; k < n; k++) {
            join_pill(k, n, &px, &py, &pw);
            if (x >= px - 4 && x < px + pw + 4 && y >= py - 6 && y < py + PILL_H + 6) {
                return 100 + k;
            }
        }
        return -1;
    }
    const int sc = ks_ipos(&s_ks);
    for (int r = 0; r < s_nrows; r++) {
        if (!row_pills(r, v)) {
            continue;
        }
        list_pill(head_h() + s_top[r] - sc, r, &px, &py, &pw);
        if (x >= px - 4 && x < px + pw + 4 && y >= py - 6 && y < py + PILL_H + 6) {
            return r * 4;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- the actions

// (the question is a consent: sysui_ask_finger() gives LEAVE only after a finger on the panel)
static void leave_answer(int k)
{
    if (k != 1) {
        return;
    }
    snprintf(s_note, sizeof s_note, "%s", pv_leave() ? "THIS BOARD LEFT THE POOL" : "THIS BOARD CANNOT LEAVE");
}

static void act_list(int r, const pview_t *v)
{
    if (s_rows[r].kind == RK_JOIN) {
        s_note[0] = 0;
        if (!pv_join_start()) {
            snprintf(s_note, sizeof s_note, "NO JOIN NOW: THE BOARD HAS A POOL");
        }
    } else if (s_rows[r].kind == RK_STATE && v->st == TFN_POOL_OK) {
        char t[48];
        snprintf(t, sizeof t, "LEAVE THE POOL %s?", v->name);
        up(t);
        sysui_ask(t, "THE LINK OF THE POOL STOPS", "LEAVE", "KEEP", 20, leave_answer);
        sysui_ask_finger();
    }
    sfx_ui(SFX_CLICK);
}

// cons: input_consent_event() of the tap
static void act_join(int k, const pview_t *v, int cons)
{
    if (v->js == TFN_POOL_JOIN_ASK && k == 1) {
        // ALLOW only for the rule of a consent: a tap that is not by a finger gets a note (a fact: the digits stay); a
        // finger that came down too soon, or a tap of a touch whose IN_DOWN the page did not get, gets no answer
        if (cons == INPUT_CONSENT_NOT_FINGER) {
            snprintf(s_note, sizeof s_note, "A REMOTE TAP CANNOT ALLOW");
            sfx_ui(SFX_BACK);
            return;
        }
        if (cons != INPUT_CONSENT_OK) {
            return;
        }
        snprintf(s_note, sizeof s_note, "%s", pv_allow() ? "" : "THE QUESTION ENDED");
    } else if (v->js == TFN_POOL_JOIN_ASK) {
        pv_no();
    } else if (v->js == TFN_POOL_JOIN_FAIL && k == 0) {
        pv_join_stop();
        s_note[0] = 0;
        if (!pv_join_start()) {
            snprintf(s_note, sizeof s_note, "NO JOIN NOW: THE BOARD HAS A POOL");
        }
    } else {
        pv_join_stop();  // CANCEL, OK
    }
    sfx_ui(SFX_CLICK);
}

// ---------------------------------------------------------------- the page

void pool_page_open(void)
{
    pview_t v;
    pv_get(&v);
    rows_make(&v);
    ks_init(&s_ks, (float)(gfx_h - head_h()), 0);
    s_press = -1;
    s_back = false;
    s_note[0] = 0;
    s_js_seen = v.js;
    input_consent_reset(&s_cons);
}

void pool_page_leave(void)
{
    pview_t v;
    pv_get(&v);
    if (v.js != TFN_POOL_JOIN_OFF && v.js != TFN_POOL_JOIN_DONE) {
        pv_join_stop();
    }
}

void pool_page_update(int64_t now)
{
    pview_t v;
    pv_get(&v);
    rows_make(&v);  // (the manager can change the pool while the page shows)
    if (v.js != s_js_seen) {
        input_consent_reset(&s_cons);  // (a new step: the 6 digits show again from the start)
        if (s_js_seen == TFN_POOL_JOIN_OFF || v.js == TFN_POOL_JOIN_OFF) {
            s_press = -1;
        }
        s_note[0] = 0;
        s_js_seen = v.js;
    }
    ks_set_max(&s_ks, join_view(&v) ? 0 : (float)(s_top[s_nrows] - (gfx_h - head_h()) + 6));
    ks_update(&s_ks, now);
}

bool pool_page_input(const input_event_t *ev, int64_t now)
{
    (void)now;
    pview_t v;
    pv_get(&v);
    const int cons = input_consent_event(&s_cons, ev);
    switch (ev->kind) {
    case IN_DOWN:
        s_back = ev->x < BACK_W;
        if (!s_back && !join_view(&v) && ks_down(&s_ks, (float)ev->y, ev->t_ms)) {
            s_press = -1;
        } else {
            s_press = s_back ? -1 : hit(ev->x, ev->y, &v);
        }
        break;
    case IN_MOVE:
        if (!s_back && !join_view(&v) && ks_move(&s_ks, (float)ev->y, ev->t_ms)) {
            s_press = -1;
            input_tap_cancel();
        }
        break;
    case IN_LONG:
        s_press = -1;  // (a long press gives no tap: a later tap must start its own press)
        break;
    case IN_UP:
        if (!s_back && !join_view(&v)) {
            ks_up(&s_ks, ev->t_ms);
        }
        break;
    case IN_TAP:
        if (s_back && ev->x < BACK_W) {
            sfx_ui(SFX_BACK);
            if (join_view(&v)) {
                pv_join_stop();  // (BACK of the JOIN screen = CANCEL: the list again)
                return false;
            }
            return true;
        }
        if (s_back || (!join_view(&v) && !ks_tap_ok(&s_ks))) {
            break;
        }
        if (s_press >= 0 && hit(ev->x, ev->y, &v) == s_press) {
            if (s_press >= 100) {
                act_join(s_press - 100, &v, cons);
            } else {
                act_list(s_press / 4, &v);
            }
        }
        s_press = -1;
        break;
    default:
        break;
    }
    return false;
}

// ---------------------------------------------------------------- the drawing

// cut s to the width w (text size sc)
static void fit(char *s, int w, int sc)
{
    for (size_t n = strlen(s); n && gfx_text_width(s, sc) > w; n--) {
        s[n - 1] = 0;
    }
}

static void pill_draw(int px, int py, int pw, const char *t, bool down, bool on)
{
    const uint16_t fill = down ? THEME_NOTE_PAGE_PILL_DOWN : on ? THEME_NOTE_PAGE_PILL_ON : THEME_NOTE_PAGE_PILL;
    gfx_fill_rect(px, py, pw, PILL_H, fill);
    gfx_fill_rect(px, py, pw, 1, THEME_NOTE_PAGE_PILL_EDGE);
    gfx_fill_rect(px, py + PILL_H - 1, pw, 1, THEME_NOTE_PAGE_PILL_EDGE);
    gfx_text_center_in(px, pw, py + PILL_H / 2 - 4, t, 1,
                       on ? THEME_NOTE_PAGE_PILL_TEXT_ON : THEME_NOTE_PAGE_PILL_TEXT);
}

// the state row: the name, its colour, two lines, the pill text
static uint16_t state_text(const pview_t *v, char *name, int nn, char *l1, char *l2, int ln, const char **pt)
{
    uint32_t c[TFN_POOL_CNT_N];
    *pt = "";
    if (v->st == TFN_POOL_OK) {
        pv_counts(c);
        snprintf(name, nn, "POOL %s", v->name);
        snprintf(l1, ln, "THIS BOARD: %s. %d MEMBERS", role_name(v->role), v->n);
        snprintf(l2, ln, "LINK: %lu GOOD, %lu DROPPED", (unsigned long)c[TFN_POOL_CNT_OK],
                 (unsigned long)(c[TFN_POOL_CNT_OTHER] + c[TFN_POOL_CNT_WHO] + c[TFN_POOL_CNT_BAD] +
                                 c[TFN_POOL_CNT_OLD]));
        if (v->role == TFN_POOL_ROLE_CHILD) {
            snprintf(l2, ln, "ONLY THE MANAGER CAN REMOVE IT");
        } else {
            *pt = "LEAVE";
        }
        up(name);
        return THEME_SETUP_OK;
    }
    if (v->st == TFN_POOL_DAMAGED) {
        snprintf(name, nn, "POOL DAMAGED");
        snprintf(l1, ln, "THE BOARD WORKS AS WITH NO POOL");
        snprintf(l2, ln, "THE MANAGER SENDS THE POOL AGAIN");
        return THEME_SETUP_FAULT;
    }
    snprintf(name, nn, "NO POOL");
    snprintf(l1, ln, "THE LINK WORKS AS BEFORE");
    snprintf(l2, ln, "A MANAGER CAN ADD THIS BOARD");
    return THEME_NOTE_PAGE_TEXT;
}

// the text of a list row: the name (big) and its colour, two short lines, the pill text
static uint16_t row_text(int r, const pview_t *v, char *name, int nn, char *l1, char *l2, int ln, const char **pt)
{
    tfn_pool_member_t m;
    char id[TFN_ID_LEN];
    l1[0] = l2[0] = 0;
    *pt = "";
    if (s_rows[r].kind == RK_STATE) {
        const uint16_t c = state_text(v, name, nn, l1, l2, ln, pt);
        if (s_note[0]) {
            snprintf(l2, ln, "%s", s_note);
        }
        return c;
    }
    if (s_rows[r].kind == RK_JOIN) {
        snprintf(name, nn, "JOIN A POOL");
        snprintf(l1, ln, "THE MANAGER FINDS THIS BOARD,");
        snprintf(l2, ln, "THEN YOU CHECK 6 DIGITS");
        *pt = "JOIN";
        return THEME_NOTE_PAGE_TEXT;
    }
    if (!pv_member(s_rows[r].slot, &m)) {
        name[0] = 0;
        return THEME_NOTE_PAGE_TEXT;
    }
    const bool me = pv_is_me(s_rows[r].slot, &m);
    id_text(m.id, id);
    snprintf(name, nn, "%s%s", m.name, me ? " (THIS BOARD)" : "");
    up(name);
    snprintf(l1, ln, "%s  %02X:%02X:%02X:%02X:%02X:%02X", role_name(m.role), m.mac[0], m.mac[1], m.mac[2], m.mac[3],
             m.mac[4], m.mac[5]);
    snprintf(l2, ln, "ID %s", id);
    return me ? THEME_NOTE_PAGE_TITLE : THEME_NOTE_PAGE_TEXT;
}

static void list_render(const pview_t *v)
{
    const int sc = ks_ipos(&s_ks);
    for (int r = 0; r < s_nrows; r++) {
        const int top = head_h() + s_top[r] - sc, rh = s_top[r + 1] - s_top[r];
        if (top + rh < head_h() || top > gfx_h) {
            continue;
        }
        char name[48], l1[64], l2[64];
        const char *pt;
        const uint16_t nc = row_text(r, v, name, sizeof name, l1, l2, sizeof l1, &pt);
        int px = gfx_w - 8, py, pw;
        if (wide() && row_pills(r, v)) {
            list_pill(top, r, &px, &py, &pw);  // (the 320 x 240 frame: the pill is under the text)
        }
        const int tw = px - (BACK_W + 10) - 8;
        fit(name, tw, 2);
        fit(l1, tw, 1);
        fit(l2, tw, 1);
        gfx_fill_rect(BACK_W + 8, top + rh - 1, gfx_w - BACK_W - 16, 1, THEME_NOTE_PAGE_LINE);
        gfx_text(BACK_W + 10, top + 6, name, 2, nc);
        gfx_text(BACK_W + 10, top + 26, l1, 1, THEME_NOTE_PAGE_PILL_TEXT);
        gfx_text(BACK_W + 10, top + 38, l2, 1, THEME_NOTE_PAGE_PILL_TEXT);
        if (row_pills(r, v)) {
            list_pill(top, r, &px, &py, &pw);
            pill_draw(px, py, pw, pt, s_press == r * 4, false);
        }
    }
    int at, sz, al;
    if (ks_bar(&s_ks, gfx_h - head_h() - 8, &at, &sz, &al) && al > 64) {
        gfx_fill_rect(gfx_w - 4, head_h() + 4 + at, 2, sz, THEME_NOTE_PAGE_SCROLL);
    }
}

// one line of the JOIN screen: the text, its size (the biggest one to sc that fits the width), its colour
typedef struct {
    char t[64];
    int sc;
    uint16_t c;
} jline_t;
#define JL_MAX 8

// A line too wide goes on two lines at the same size (the cut at the last space that fits); with no such cut, the size
// gets smaller. The digits (size 5 and more) only get smaller. JL_MAX lines at most.
static int jl_add(jline_t *l, int n, int sc, uint16_t c, const char *t)
{
    const int w = gfx_w - BACK_W - 40;
    if (n >= JL_MAX) {
        return n;
    }
    for (;; sc--) {
        snprintf(l[n].t, sizeof l[n].t, "%s", t);
        up(l[n].t);
        l[n].sc = sc;
        l[n].c = c;
        if (gfx_text_width(l[n].t, sc) <= w) {
            return n + 1;
        }
        for (char *sp = strrchr(l[n].t, ' '); sc < 5 && sp; sp = strrchr(l[n].t, ' ')) {
            *sp = 0;
            if (gfx_text_width(l[n].t, sc) <= w) {
                return jl_add(l, n + 1, sc, c, t + (sp - l[n].t) + 1);
            }
        }
        if (sc == 1) {
            snprintf(l[n].t, sizeof l[n].t, "%s", t);  // (one long word: cut it)
            up(l[n].t);
            fit(l[n].t, w, 1);
            return n + 1;
        }
    }
}

// the lines of the JOIN screen for its step; returns their count
static int join_lines(const pview_t *v, jline_t *l)
{
    char t[64];
    int n = 0;
    const bool bar = wide() && gfx_h < 300;  // the 3.49 bar screen: 172 px high
    const int big = wide() && !bar ? 3 : 2;  // (the Tab5: 360 px high)
    switch (v->js) {
    case TFN_POOL_JOIN_WAIT:
        n = jl_add(l, n, big, THEME_NOTE_PAGE_TITLE, "WAIT FOR THE MANAGER");
        n = jl_add(l, n, 1, THEME_NOTE_PAGE_PILL_TEXT, "ON THE MANAGER: POOL > ADOPT");
        snprintf(t, sizeof t, "THIS BOARD: %s", friends_my_name());
        n = jl_add(l, n, 1, THEME_NOTE_PAGE_TEXT, t);
        if (tfn_id_str()[0]) {
            snprintf(t, sizeof t, "ID %s", tfn_id_str());
            n = jl_add(l, n, 1, THEME_NOTE_PAGE_PILL_TEXT, t);
        }
        break;
    case TFN_POOL_JOIN_REVEAL:
        n = jl_add(l, n, big, THEME_NOTE_PAGE_TITLE, "THE MANAGER ANSWERED");
        n = jl_add(l, n, 1, THEME_NOTE_PAGE_PILL_TEXT, "WAIT FOR THE 6 DIGITS");
        break;
    case TFN_POOL_JOIN_ASK:
        // (the name of the manager only when it is not the word MANAGER: "THE MANAGER MANAGER" is not good text)
        snprintf(t, sizeof t, "THE MANAGER%s%s", s_mgr[0] && strcmp(s_mgr, "MANAGER") ? " " : "",
                 strcmp(s_mgr, "MANAGER") ? s_mgr : "");
        if (bar) {  // (one line of text above the digits)
            snprintf(t + strlen(t), sizeof t - strlen(t), " WANTS TO MANAGE THIS BOARD");
            n = jl_add(l, n, 1, THEME_NOTE_PAGE_TEXT, t);
        } else {
            n = jl_add(l, n, 2, THEME_NOTE_PAGE_TEXT, t);
            n = jl_add(l, n, 1, THEME_NOTE_PAGE_PILL_TEXT, "WANTS TO MANAGE THIS BOARD");
        }
        snprintf(t, sizeof t, "%.3s %.3s", s_sas, s_sas + 3);
        n = jl_add(l, n, bar ? 5 : 8, THEME_SETUP_ACCENT, t);
        n = jl_add(l, n, 1, THEME_NOTE_PAGE_PILL_TEXT, "ALLOW ONLY WHEN THE MANAGER SHOWS THE SAME DIGITS");
        break;
    case TFN_POOL_JOIN_ALLOWED:
        n = jl_add(l, n, big, THEME_SETUP_OK, "ALLOWED");
        n = jl_add(l, n, 1, THEME_NOTE_PAGE_PILL_TEXT, "WAIT FOR THE POOL DATA OF THE MANAGER");
        break;
    case TFN_POOL_JOIN_DONE:
        n = jl_add(l, n, big, THEME_SETUP_OK, "THIS BOARD IS IN THE POOL");
        n = jl_add(l, n, 2, THEME_NOTE_PAGE_TEXT, v->name);
        break;
    default:
        n = jl_add(l, n, big, THEME_SETUP_FAULT, "THE JOIN FAILED");
        n = jl_add(l, n, 1, THEME_NOTE_PAGE_PILL_TEXT, v->why[0] ? v->why : "NO CAUSE");
        break;
    }
    if (s_note[0]) {
        n = jl_add(l, n, 1, THEME_SETUP_WARN, s_note);
    }
    return n;
}

// now: the app clock (the clock of the events of the page)
static void join_render(const pview_t *v, int64_t now)
{
    jline_t l[JL_MAX];
    if (v->js == TFN_POOL_JOIN_ASK && !pv_sas()) {
        snprintf(s_sas, sizeof s_sas, "------");
        s_mgr[0] = 0;
    }
    if (v->js == TFN_POOL_JOIN_ASK) {
        if (strcmp(s_sas, s_sas_seen)) {
            input_consent_reset(&s_cons);  // (other digits: the question starts again)
            snprintf(s_sas_seen, sizeof s_sas_seen, "%s", s_sas);
        }
        input_consent_shown(&s_cons, now);
    }
    const int n = join_lines(v, l);
    // the lines in the middle of the band between the head and the pills; a gap of 1/3 of a line between two lines
    int h = 0;
    for (int i = 0; i < n; i++) {
        h += 8 * l[i].sc + (i ? 4 + 2 * l[i].sc : 0);
    }
    const int y1 = gfx_h - PILL_H - 12;
    int y = head_h() + (y1 - head_h() - h) / 2;
    for (int i = 0; i < n; i++) {
        y += i ? 4 + 2 * l[i].sc : 0;
        gfx_text_center_in(BACK_W, gfx_w - BACK_W, y, l[i].t, l[i].sc, l[i].c);
        y += 8 * l[i].sc;
    }
    const char *t[2];
    const int np = join_pills(v, t);
    const bool armed = s_cons.shown_ms && now >= s_cons.shown_ms + INPUT_ARM_MS;
    for (int k = 0; k < np; k++) {
        int px, py, pw;
        join_pill(k, np, &px, &py, &pw);
        const bool on = v->js == TFN_POOL_JOIN_ASK ? k == 1 && armed : k == np - 1;
        pill_draw(px, py, pw, t[k], s_press == 100 + k, on);
    }
}

void pool_page_render(int64_t now)
{
    pview_t v;
    pv_get(&v);
    gfx_fill(THEME_NOTE_PAGE_BG);
    if (join_view(&v)) {
        join_render(&v, now);
    } else {
        list_render(&v);
    }
    // the head and BACK over the list
    gfx_fill_rect(BACK_W, 0, gfx_w - BACK_W, head_h(), THEME_NOTE_PAGE_BG);
    gfx_text(BACK_W + 8, 8, join_view(&v) ? "JOIN A POOL" : "POOL", 2, THEME_NOTE_PAGE_TITLE);
    // the time of the JOIN that is left, at the right of the head
    if (v.js == TFN_POOL_JOIN_WAIT || v.js == TFN_POOL_JOIN_REVEAL || v.js == TFN_POOL_JOIN_ASK) {
        char tl[16];
        snprintf(tl, sizeof tl, "%d:%02d", v.left_s / 60, v.left_s % 60);
        gfx_text(gfx_w - 10 - gfx_text_width(tl, 2), 8, tl, 2, THEME_NOTE_PAGE_PILL_TEXT);
    }
    gfx_fill_rect(0, 0, BACK_W, gfx_h, THEME_NOTE_PAGE_BACK);
    gfx_text_center_in(0, BACK_W, gfx_h / 2 - 4, "BACK", 1, THEME_NOTE_PAGE_BACK_TEXT);
}

void pool_page_row_text(char *buf, int n)
{
    pview_t v;
    pv_get(&v);
    if (v.js != TFN_POOL_JOIN_OFF && v.js != TFN_POOL_JOIN_DONE) {
        snprintf(buf, n, "A JOIN RUNS");
    } else if (v.st == TFN_POOL_OK) {
        snprintf(buf, n, "%s: %d MEMBERS", v.name, v.n);
        up(buf);
    } else {
        snprintf(buf, n, "%s", v.st == TFN_POOL_DAMAGED ? "POOL DAMAGED" : "NO POOL");
    }
}
