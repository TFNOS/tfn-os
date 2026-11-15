// SETUP > TRUSTED PUBLISHERS (tfn_pub.h): the key that waits for the tap of the board owner (ALLOW / NO), the
// built-in keys of the store, the keys that the board owner added (REMOVE asks first), and ADD = a key file of the card
// (/sdcard/pub). A list in the shared momentum scroll (libs/kscroll), with the look of SETUP > NOTIFICATIONS (the same
// theme names). ALLOW acts only for the rule of a consent (input.h): a tap by a finger on the panel, of a touch that
// came down INPUT_ARM_MS or more after the row of the key showed (ALLOW shows as on from that time). A new wait (a new
// number: the web can start one while the page shows, also for the same key with other flags) starts the rule again.
// ALLOW and NO answer only the wait that the row showed.
#include "tfn_pub.h"
#include "tfn/theme.h"
#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "board.h"
#include "gfx.h"
#include "input.h"
#include "sysui.h"
#include "../libs/kscroll/kscroll.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"  // (EXT_RAM_BSS_ATTR: the copy of the wait and the consent rule in PSRAM: the internal RAM is short)
#endif
#ifndef EXT_RAM_BSS_ATTR
#define EXT_RAM_BSS_ATTR
#endif

#include "set_round.h"
// THE ROUND FRAME (gfx_round()): the title on the top cap, the rows in the middle box (the name, two lines, the pills
// under them at the right), BACK on the bottom cap.
#if BOARD_ROUND
#define PP_RND gfx_round()
#else
#define PP_RND false
#endif
#define BACK_W 56
#define PILL_H (PP_RND ? RS(42) : 36)
#define NROWS  (2 + TFN_PUB_BUILTIN + TFN_PUB_MAX)

enum {
    RK_WAIT,     // the key that waits: ALLOW, NO
    RK_BUILTIN,  // a built-in key (no pill)
    RK_KEY,      // an added key: REMOVE
    RK_CARD      // ADD from the card
};
typedef struct {
    uint8_t kind, i;
} prow_t;

static prow_t s_rows[NROWS];
static int s_nrows, s_y0, s_x0, s_press = -1;
static bool s_drag, s_back;
static kscroll_t s_ks;
static char s_note[96];              // the answer of the last ADD or REMOVE
static uint8_t s_rm_id[TFN_PUB_ID];  // the key of the REMOVE question
static char s_rm_name[TFN_PUB_NAME];
// The consent rule of ALLOW (input.h) and the copy of the wait (wait_track(): the row of the key that the rule is for).
// PSRAM: the page reads these values only once in each frame or event.
static input_consent_t s_cons EXT_RAM_BSS_ATTR;
static bool s_wait_on EXT_RAM_BSS_ATTR;       // a key waits
static uint32_t s_wait_seq EXT_RAM_BSS_ATTR;  // the number of its wait
static tfn_pub_t s_wait_p EXT_RAM_BSS_ATTR;   // the key that waits and its seconds left (the row shows this copy)
static int s_wait_secs EXT_RAM_BSS_ATTR;
static uint32_t s_shown_seq EXT_RAM_BSS_ATTR;  // the wait of the row in the last frame (ALLOW and NO answer it)

static bool wide(void)
{
    return gfx_w >= 480;
}
static int row_h(void)
{
#if BOARD_ROUND
    if (PP_RND) {
        return RS(124);
    }
#endif
    return wide() ? 52 : 96;  // (the 320 x 240 frame: the pills under the three lines of text)
}
static int head_h(void)
{
#if BOARD_ROUND
    if (PP_RND) {
        return RB_Y;
    }
#endif
    return 30;
}
// the height of the list on the screen
static int list_h(void)
{
#if BOARD_ROUND
    if (PP_RND) {
        return RB_B - RB_Y;
    }
#endif
    return gfx_h - head_h();
}

// the wait now: the only read of tfn_pub_waiting() in this page. Another wait (a new number: the web can start one
// between two frames, also for the same key with other flags or another name) or none starts the consent rule again.
static void wait_track(void)
{
    tfn_pub_t p;
    int secs = 0;
    uint32_t seq = 0;
    const bool on = tfn_pub_waiting(&p, &secs, &seq);
    if (on != s_wait_on || (on && seq != s_wait_seq)) {
        input_consent_reset(&s_cons);
    }
    s_wait_on = on;
    s_wait_seq = on ? seq : 0;
    if (on) {
        s_wait_p = p;
        s_wait_secs = secs;
    }
}

static void rows_make(void)
{
    int n = 0;
    wait_track();
    if (s_wait_on) {
        s_rows[n++] = (prow_t){RK_WAIT, 0};
    }
    for (int i = 0; i < tfn_pub_builtin_count() && i < TFN_PUB_BUILTIN; i++) {
        s_rows[n++] = (prow_t){RK_BUILTIN, (uint8_t)i};
    }
    for (int i = 0; i < tfn_pub_count() && i < TFN_PUB_MAX; i++) {
        s_rows[n++] = (prow_t){RK_KEY, (uint8_t)i};
    }
    s_rows[n++] = (prow_t){RK_CARD, 0};
    s_nrows = n;
}

void tfn_pub_page_open(void)
{
    s_wait_on = false;
    s_wait_seq = s_shown_seq = 0;
    input_consent_reset(&s_cons);
    rows_make();
    ks_init(&s_ks, (float)list_h(), 0);
    s_press = -1;
    s_drag = s_back = false;
    s_note[0] = 0;
}

// the pills of a row: k = 0 .. 1; x, w of the pill; returns the count
static int pills(int r, int k, int *x, int *w)
{
    const int x1 = PP_RND ? RB_X + RB_W : gfx_w - 8, pw = PP_RND ? RS(116) : wide() ? 110 : 84;
    int n = s_rows[r].kind == RK_WAIT ? 2 : s_rows[r].kind == RK_BUILTIN ? 0 : 1;
    if (k >= n) {
        return n;
    }
    *w = pw;
    *x = x1 - (n - k) * (pw + 6) + 6;
    return n;
}
static int pill_y(int top)
{
    if (PP_RND) {
        return top + RS(72);
    }
    return wide() ? top + (row_h() - PILL_H) / 2 : top + 52;
}

// the answer of an ADD or a REMOVE for the row ADD A KEY (the dot font has capitals only)
static void note_up(void)
{
    for (char *c = s_note; *c; c++) {
        if (*c >= 'a' && *c <= 'z') {
            *c = (char)(*c - 32);
        }
    }
}

static void rm_answer(int k)
{
    if (k == 1) {
        tfn_pub_remove_id(s_rm_id, s_note, sizeof s_note);
        note_up();
        rows_make();
    }
}

// cons: input_consent_event() of the tap
static void act(int r, int k, int cons)
{
    const prow_t *w = &s_rows[r];
    tfn_pub_t p;
    if (w->kind == RK_WAIT && k == 0 && cons != INPUT_CONSENT_OK) {
        // ALLOW only by a finger on the panel: a tap of the console or the web (/api/ctl) cannot add a key (a fact in
        // the row ADD A KEY; the row of the key keeps its data). A finger that came down too soon, or a tap of a touch
        // whose IN_DOWN the page did not get: no answer.
        if (cons == INPUT_CONSENT_NOT_FINGER) {
            snprintf(s_note, sizeof s_note, "A REMOTE TAP CANNOT ALLOW");
            sfx_ui(SFX_BACK);
        }
        return;
    } else if (w->kind == RK_WAIT) {
        tfn_pub_answer(k == 0, s_shown_seq, s_note, sizeof s_note);  // (only for the wait that the row showed)
    } else if (w->kind == RK_KEY && tfn_pub_get(w->i, &p)) {
        char t[48], id[20];
        tfn_pub_key_id_of(&p, s_rm_id);
        tfn_pub_id_text(s_rm_id, id);
        snprintf(s_rm_name, sizeof s_rm_name, "%s", p.name);
        snprintf(t, sizeof t, "REMOVE THE KEY %s?", s_rm_name);
        sysui_ask(t, id, "REMOVE", "KEEP", 20, rm_answer);
    } else if (w->kind == RK_CARD) {
        tfn_pub_request_card(s_note, sizeof s_note);
    }
    note_up();
    rows_make();
    sfx_ui(SFX_CLICK);
}

// the pill under (x, y): r * 4 + k, or -1
static int hit(int x, int y)
{
    if (y < head_h() || (PP_RND && y >= head_h() + list_h())) {
        return -1;
    }
    const int sc = ks_ipos(&s_ks);
    for (int r = 0; r < s_nrows; r++) {
        const int top = head_h() + r * row_h() - sc, py = pill_y(top);
        for (int k = 0; k < 2; k++) {
            int px, pw;
            if (k >= pills(r, k, &px, &pw)) {
                break;
            }
            if (x >= px - 4 && x < px + pw + 4 && y >= py - 6 && y < py + PILL_H + 6) {
                return r * 4 + k;
            }
        }
    }
    return -1;
}

void tfn_pub_page_update(int64_t now)
{
    rows_make();  // (a key can come or go from the web while the page shows)
    ks_set_max(&s_ks, (float)(s_nrows * row_h() - list_h() + 6));
    ks_update(&s_ks, now);
}

bool tfn_pub_page_input(const input_event_t *ev, int64_t now)
{
    (void)now;
    wait_track();  // (the web can start a new key between two frames)
    const int cons = input_consent_event(&s_cons, ev);
    switch (ev->kind) {
    case IN_DOWN:
        s_x0 = ev->x;
        s_y0 = ev->y;
        s_drag = false;
#if BOARD_ROUND
        s_back = PP_RND ? rr_back_at(ev->x, ev->y) : ev->x < BACK_W;
#else
        s_back = ev->x < BACK_W;
#endif
        if (!s_back && ks_down(&s_ks, (float)ev->y, ev->t_ms)) {
            s_press = -1;
        } else {
            s_press = s_back ? -1 : hit(ev->x, ev->y);
        }
        break;
    case IN_MOVE:
        if (!s_back && ks_move(&s_ks, (float)ev->y, ev->t_ms)) {
            s_drag = true;
            s_press = -1;
            input_tap_cancel();
        }
        break;
    case IN_LONG:
        s_press = -1;  // (a long press gives no tap: a later tap must start its own press)
        break;
    case IN_UP:
        if (!s_back) {
            ks_up(&s_ks, ev->t_ms);
        }
        break;
    case IN_TAP:
#if BOARD_ROUND
        if (s_back && (PP_RND ? rr_back_at(ev->x, ev->y) : ev->x < BACK_W)) {
#else
        if (s_back && ev->x < BACK_W) {
#endif
            sfx_ui(SFX_BACK);
            return true;
        }
        if (s_back || !ks_tap_ok(&s_ks)) {
            break;
        }
        if (s_press >= 0 && hit(ev->x, ev->y) == s_press) {
            act(s_press / 4, s_press % 4, cons);
        }
        s_press = -1;
        break;
    default:
        break;
    }
    return false;
}

// the text of a row: the name (big) and two short lines; the pill texts
static void row_text(int r, char *name, int nn, char *l1, char *l2, int ln, const char **pt)
{
    const prow_t *w = &s_rows[r];
    tfn_pub_t p;
    uint8_t id[TFN_PUB_ID];
    char t[20], fl[20];
    l1[0] = l2[0] = 0;
    pt[0] = pt[1] = "";
    if (w->kind == RK_WAIT && s_wait_on) {
        tfn_pub_key_id_of(&s_wait_p, id);
        tfn_pub_id_text(id, t);
        tfn_pub_flags_text(s_wait_p.flags, fl, sizeof fl);
        snprintf(name, nn, "NEW: %s", s_wait_p.name);
        snprintf(l1, ln, "%s", t);
        snprintf(l2, ln, "%s, %d S", fl, s_wait_secs);
        pt[0] = "ALLOW";
        pt[1] = "NO";
    } else if (w->kind == RK_BUILTIN && tfn_pub_builtin_id(w->i, id)) {
        tfn_pub_id_text(id, t);
        snprintf(name, nn, "STORE KEY %d", w->i + 1);
        snprintf(l1, ln, "%s", t);
        snprintf(l2, ln, "BUILT IN");
    } else if (w->kind == RK_KEY && tfn_pub_get(w->i, &p)) {
        tfn_pub_key_id_of(&p, id);
        tfn_pub_id_text(id, t);
        tfn_pub_flags_text(p.flags, fl, sizeof fl);
        snprintf(name, nn, "%s", p.name);
        snprintf(l1, ln, "%s", t);
        snprintf(l2, ln, "%s", fl);
        pt[0] = "REMOVE";
    } else {
        snprintf(name, nn, "ADD A KEY");
        snprintf(l1, ln, "%s", s_note[0] ? s_note : "A KEY FILE IN /SDCARD/PUB");
        snprintf(l2, ln, "%s", s_note[0] ? "" : "OR THE WEB: POST /API/PUB");
        pt[0] = "ADD";
    }
}

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

#if BOARD_ROUND
// cut s to the width w in the font f
static void fit_k(const kfont_t *f, char *s, int w)
{
    for (size_t n = strlen(s); n && ui_text_w(f, s) > w; n--) {
        s[n - 1] = 0;
    }
}

// the round frame: the rows between the caps, then the caps
static void render_round(int64_t now)
{
    gfx_fill(THEME_NOTE_PAGE_BG);
    const int sc = ks_ipos(&s_ks), rh = row_h(), y0 = head_h(), y1 = y0 + list_h();
    for (int r = 0; r < s_nrows; r++) {
        const int top = y0 + r * rh - sc;
        if (top + rh < y0 || top > y1) {
            continue;
        }
        if (s_rows[r].kind == RK_WAIT && s_wait_on) {
            input_consent_shown(&s_cons, now);  // (the row of the key that waits is on the screen)
            s_shown_seq = s_wait_seq;
        }
        char name[32], l1[96], l2[96];
        const char *pt[2];
        row_text(r, name, sizeof name, l1, l2, sizeof l1, pt);
        ui_rrect(RB_X - RS(6), top + 2, RB_W + RS(12), rh - 6, RS(12), THEME_NOTE_PAGE_LINE);
        const int tx = RB_X + RS(4), tw = RB_W - RS(10);
        fit_k(&KF_LABEL, l1, tw);
        fit_k(&KF_LABEL, l2, tw);
        ui_text(rr_fit(&KF_TEXT, name, tw), tx, top + RS(28), name, THEME_NOTE_PAGE_TEXT);
        ui_text(&KF_LABEL, tx, top + RS(48), l1, THEME_NOTE_PAGE_PILL_TEXT);
        ui_text(&KF_LABEL, tx, top + RS(66), l2, THEME_NOTE_PAGE_PILL_TEXT);
        const bool armed = s_cons.shown_ms && now >= s_cons.shown_ms + INPUT_ARM_MS;
        for (int k = 0; k < 2; k++) {
            int px, pw;
            if (k >= pills(r, k, &px, &pw)) {
                break;
            }
            const bool on = s_rows[r].kind == RK_WAIT && k == 0 && armed, down = s_press == r * 4 + k;
            rr_btn(px, pill_y(top), pw, PILL_H, pt[k],
                   down ? THEME_NOTE_PAGE_PILL_DOWN
                   : on ? THEME_NOTE_PAGE_PILL_ON
                        : THEME_NOTE_PAGE_PILL,
                   on ? THEME_NOTE_PAGE_PILL_TEXT_ON : THEME_NOTE_PAGE_PILL_TEXT);
        }
    }
    int at, sz, al;
    if (ks_bar(&s_ks, list_h() - 8, &at, &sz, &al) && al > 64) {
        ui_rrect(RB_X + RB_W + RS(10), y0 + 4 + at, 4, sz, 2, THEME_NOTE_PAGE_SCROLL);
    }
    gfx_fill_rect(0, 0, gfx_w, y0, THEME_NOTE_PAGE_BG);
    gfx_fill_rect(0, y1, gfx_w, gfx_h - y1, THEME_NOTE_PAGE_BG);
    rr_head("TRUSTED PUBLISHERS", "", THEME_NOTE_PAGE_TITLE, THEME_NOTE_PAGE_TEXT);
    rr_back("< BACK", false, THEME_NOTE_PAGE_BACK, THEME_NOTE_PAGE_BACK, THEME_NOTE_PAGE_BACK_TEXT,
            THEME_NOTE_PAGE_BACK_TEXT);
}
#endif

void tfn_pub_page_render(int64_t now)
{
    rows_make();  // (the rows and the copy of the wait of this frame agree)
#if BOARD_ROUND
    if (PP_RND) {
        render_round(now);
        return;
    }
#endif
    gfx_fill(THEME_NOTE_PAGE_BG);
    const int sc = ks_ipos(&s_ks), rh = row_h();
    for (int r = 0; r < s_nrows; r++) {
        const int top = head_h() + r * rh - sc;
        if (top + rh < head_h() || top > gfx_h) {
            continue;
        }
        if (s_rows[r].kind == RK_WAIT && s_wait_on) {
            input_consent_shown(&s_cons, now);  // (the row of the key that waits is on the screen)
            s_shown_seq = s_wait_seq;
        }
        char name[32], l1[96], l2[96];
        const char *pt[2];
        row_text(r, name, sizeof name, l1, l2, sizeof l1, pt);
        int px = gfx_w - 8, pw = 0;
        if (!wide() || pills(r, 0, &px, &pw) == 0) {
            px = gfx_w - 8;  // (the 320 x 240 frame: the pills are under the text)
        }
        const int tw = px - (BACK_W + 10) - 8;
        fit(name, tw, 2);
        fit(l1, tw, 1);
        fit(l2, tw, 1);
        gfx_fill_rect(BACK_W + 8, top + rh - 1, gfx_w - BACK_W - 16, 1, THEME_NOTE_PAGE_LINE);
        gfx_text(BACK_W + 10, top + 6, name, 2, THEME_NOTE_PAGE_TEXT);
        gfx_text(BACK_W + 10, top + 26, l1, 1, THEME_NOTE_PAGE_PILL_TEXT);
        gfx_text(BACK_W + 10, top + 38, l2, 1, THEME_NOTE_PAGE_PILL_TEXT);
        // (ALLOW shows as on only when a finger can answer: INPUT_ARM_MS after the row showed)
        const bool armed = s_cons.shown_ms && now >= s_cons.shown_ms + INPUT_ARM_MS;
        for (int k = 0; k < 2; k++) {
            int px, pw;
            if (k >= pills(r, k, &px, &pw)) {
                break;
            }
            pill_draw(px, pill_y(top), pw, pt[k], s_press == r * 4 + k, s_rows[r].kind == RK_WAIT && k == 0 && armed);
        }
    }
    int at, sz, al;
    if (ks_bar(&s_ks, list_h() - 8, &at, &sz, &al) && al > 64) {
        gfx_fill_rect(gfx_w - 4, head_h() + 4 + at, 2, sz, THEME_NOTE_PAGE_SCROLL);
    }
    // the head and BACK over the list
    gfx_fill_rect(BACK_W, 0, gfx_w - BACK_W, head_h(), THEME_NOTE_PAGE_BG);
    gfx_text(BACK_W + 8, 8, "TRUSTED PUBLISHERS", 2, THEME_NOTE_PAGE_TITLE);
    gfx_fill_rect(0, 0, BACK_W, gfx_h, THEME_NOTE_PAGE_BACK);
    gfx_text_center_in(0, BACK_W, gfx_h / 2 - 4, "BACK", 1, THEME_NOTE_PAGE_BACK_TEXT);
}
