// SETTINGS > FRIENDS: the name of this board, the friends (tap one: remove it), and LEARN A FRIEND
// (the two boards send their names by radio; each one adds the other with a tap).

#include <stdio.h>
#include <string.h>
#include "uikit.h"
#include "audio.h"
#include "friends.h"
#include "friends_page.h"
#include "gfx.h"
#include "kbd.h"
#include "link.h"
#include "wifi.h"
#include "tfn/theme.h"
#include "set_round.h"

#define FP_APP     FRIENDS_LINK_APP  // the link app id of LEARN (as FR_APP in friends.c)
#define FP_K_LEARN 1
#define FP_K_ADDED 2
#define N_CAND     6

enum {
    FP_LIST,
    FP_NAME,
    FP_LEARN,
    FP_REMOVE
};
static int s_mode, s_remove, s_fpage;
static char s_name[FRIEND_NAME];
static struct {
    uint8_t mac[6];
    char name[FRIEND_NAME];
    int64_t seen;
    bool added;
} s_cand[N_CAND];
static int64_t s_hello_at, s_msg_at;
// the last ADD: its packet goes with each LEARN packet for FP_ADDED_MS (one packet can be lost while the other board
// visits another channel; a second copy shows the same note)
#define FP_ADDED_MS 2000
static uint8_t s_added_mac[6];
static int64_t s_added_at;
static char s_msg[48];

static void button(int x, int y, int w, int h, const char *t, uint16_t bg, uint16_t fg)
{
    gfx_fill_rect(x, y, w, h, bg);
    gfx_frame_rect(x, y, w, h, 1, THEME_FRIENDS_BTN_EDGE);
    char c[24];
    int n = (w - 4) / 6;  // (a long name is cut to the button)
    snprintf(c, sizeof c, "%.*s", n < 23 ? n : 23, t);
    gfx_text_center_in(x, w, y + h / 2 - 4, c, 1, fg);
}
// the layout (the 3.49: 640 x 172; the CoreS3: 320 x 240). THE THUMB: all buttons are 44 px high. The 3.49:
// CHANGE NAME and LEARN A FRIEND at the top right, the friends in 2 rows of 4. The 320 x 240 frame: CHANGE at the top
// right, the friends in 3 rows of 3, LEARN A FRIEND at the bottom right.
#define BACK_W 56
#define BTN_H  44
#if BOARD_ROUND
// THE ROUND FRAME (gfx_round()): the title on the top cap, the friends in 3 rows of 3 in the middle box, CHANGE, MORE
// and LEARN A FRIEND in one row under them, BACK on the bottom cap; REMOVE?: NO at the left, YES at the right.
#define FP_RND gfx_round()
#else
#define FP_RND false
#endif
static bool wide(void)
{
    return gfx_w > 400;
}
static void friend_btn(int i, int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    if (FP_RND) {
        int g = RS(8);
        *w = (RB_W - 2 * g) / 3;
        *h = RS(52);
        *x = RB_X + (i % 3) * (*w + g);
        *y = RB_Y + (i / 3) * (*h + g);
        return;
    }
#endif
    int cols = wide() ? 4 : 3, gap = wide() ? 8 : 6, top = wide() ? 56 : 50;
    *w = (gfx_w - BACK_W - 8 - 4 - (cols - 1) * gap) / cols;
    *h = BTN_H;
    *x = BACK_W + 8 + (i % cols) * (*w + gap);
    *y = top + (i / cols) * (*h + (wide() ? 8 : 4));
}
// the row of buttons of the round frame under the friends: 0 CHANGE, 1 MORE, 2 LEARN A FRIEND
#if BOARD_ROUND
static void round_row(int i, int *x, int *y, int *w, int *h)
{
    int g = RS(8);
    *w = (RB_W - 2 * g) / 3;
    *h = RS(54);
    *x = RB_X + i * (*w + g);
    *y = RB_Y + 3 * RS(60) + RS(4);
}
#endif
static void learn_btn(int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    if (FP_RND) {
        round_row(2, x, y, w, h);
        return;
    }
#endif
    *h = BTN_H;
    if (wide()) {
        *w = 120;
        *x = gfx_w - *w - 4;
        *y = 4;
    } else {
        *w = 156;
        *x = gfx_w - *w - 4;
        *y = gfx_h - *h;
    }
}
static void name_btn(int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    if (FP_RND) {
        round_row(0, x, y, w, h);
        return;
    }
#endif
    *h = BTN_H;
    if (wide()) {
        *w = 116;
        *x = gfx_w - 120 - 4 - 8 - *w;
        *y = 4;
    } else {
        *w = 84;
        *x = gfx_w - *w - 4;
        *y = 2;
    }
}
// the friends on one page (16 friends: two pages), and the page button (only with more friends)
static int per_page(void)
{
    return FP_RND ? 9 : wide() ? 8 : 9;
}
static int pages(void)
{
    return (friends_count() + per_page() - 1) / per_page();
}
static void page_btn(int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    if (FP_RND) {
        round_row(1, x, y, w, h);
        return;
    }
#endif
    *h = BTN_H;
    if (wide()) {
        *w = 116;
        *x = gfx_w - 120 - 4 - 8 - 116 - 8 - *w;
        *y = 4;
    } else {
        *w = 96;
        *x = BACK_W + 4;
        *y = gfx_h - *h;
    }
}
// REMOVE?: NO at the far left, YES at the far right (0 = YES, 1 = NO)
static void yes_no(int i, int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    if (FP_RND) {
        *w = RS(140);
        *h = RS(60);
        *x = i ? gfx_w / 2 - RS(8) - *w : gfx_w / 2 + RS(8);
        *y = RS(250);
        return;
    }
#endif
    *w = wide() ? 140 : 112;
    *h = BTN_H;
    *x = i ? (wide() ? 70 : 12) : gfx_w - *w - (wide() ? 70 : 12);
    *y = gfx_h / 2 + 4;
}

void friends_page_open(void)
{
    s_mode = FP_LIST;
    s_fpage = 0;
}

static void learn_start(int64_t now)
{
    s_mode = FP_LEARN;
    memset(s_cand, 0, sizeof s_cand);
    s_hello_at = 0;
    s_added_at = 0;
    s_msg[0] = 0;
    if (!link_start()) {
        snprintf(s_msg, sizeof s_msg, "THE RADIO DID NOT START");
        return;
    }
    link_meet_learn(FP_K_LEARN);  // (a LEARN packet holds the search of the link: the two boards stay on one channel)
}
static void learn_stop(void)
{
    link_stop();
    s_mode = FP_LIST;
}

void friends_page_update(int64_t now)
{
    if (s_mode != FP_LEARN || !link_ready()) {
        return;
    }
    // my name, each 300 ms (the link finds the channel of the other board)
    if (now - s_hello_at > 300) {
        s_hello_at = now;
        const char *my = friends_my_name();
        link_send(FP_APP, FP_K_LEARN, my, (int)strlen(my) + 1);
        if (s_added_at && now - s_added_at < FP_ADDED_MS) {
            link_send(FP_APP, FP_K_ADDED, s_added_mac, 6);
        }
    }
    link_msg_t m;
    int guard = 0;
    while (guard++ < 16 && link_recv(&m)) {
        if (m.app != FP_APP) {
            continue;
        }
        if (m.kind == FP_K_ADDED) {
            uint8_t me[6];
            link_my_mac(me);
            if (m.len >= 6 && !memcmp(m.data, me, 6)) {
                const char *who = "THE OTHER BOARD";
                for (int i = 0; i < N_CAND; i++) {
                    if (s_cand[i].seen && !memcmp(s_cand[i].mac, m.mac, 6)) {
                        who = s_cand[i].name;
                    }
                }
                snprintf(s_msg, sizeof s_msg, "%.15s ADDED YOU", who);
                s_msg_at = now;
            }
            continue;
        }
        if (m.kind != FP_K_LEARN || m.len < 2) {
            continue;
        }
        m.data[m.len - 1] = 0;
        int k = -1;
        for (int i = 0; i < N_CAND; i++) {
            if (s_cand[i].seen && !memcmp(s_cand[i].mac, m.mac, 6)) {
                k = i;
            }
        }
        if (k < 0) {
            for (int i = 0; i < N_CAND; i++) {
                if (!s_cand[i].seen || now - s_cand[i].seen > 4000) {
                    k = i;
                    break;
                }
            }
        }
        if (k < 0) {
            continue;
        }
        memcpy(s_cand[k].mac, m.mac, 6);
        snprintf(s_cand[k].name, FRIEND_NAME, "%.15s", (const char *)m.data);
        s_cand[k].seen = now;
        s_cand[k].added = friends_find(m.mac) >= 0;
    }
}

// returns true when the page closes
bool friends_page_input(const input_event_t *ev, int64_t now)
{
    if (s_mode == FP_NAME) {
        int r = kbd_input(ev);
        if (r == KBD_OK) {
            friends_set_my_name(s_name);
        }
        if (r != KBD_NONE) {
            s_mode = FP_LIST;
        }
        return false;
    }
    if (ev->kind != IN_TAP) {
        return false;
    }
    int x = ev->x, y = ev->y;
    if (s_mode == FP_REMOVE) {
        int bx, by, bw, bhh;
        yes_no(0, &bx, &by, &bw, &bhh);
        if (ui_hit(x, y, bx, by, bw, bhh, 2)) {
            friends_remove(s_remove);
            sfx_ui(SFX_BACK);
        }
        s_mode = FP_LIST;
        return false;
    }
    if (FP_RND ? rr_back_at(x, y) : x < BACK_W) {
        if (s_mode == FP_LEARN) {
            learn_stop();
            return false;
        }
        return true;
    }
    if (s_mode == FP_LEARN) {
        for (int i = 0, n = 0; i < N_CAND; i++) {
            if (!s_cand[i].seen || now - s_cand[i].seen > 4000) {
                continue;
            }
            int bx, by, bw, bhh;
            friend_btn(n++, &bx, &by, &bw, &bhh);
            if (ui_hit(x, y, bx, by, bw, bhh, 2) && !s_cand[i].added) {
                if (friends_add(s_cand[i].mac, s_cand[i].name)) {
                    s_cand[i].added = true;
                    memcpy(s_added_mac, s_cand[i].mac, 6);
                    s_added_at = now ? now : 1;
                    link_send(FP_APP, FP_K_ADDED, s_added_mac, 6);
                    sfx_ui(SFX_COIN);
                } else {
                    snprintf(s_msg, sizeof s_msg, "THE LIST IS FULL (%d)", FRIENDS_MAX);
                    s_msg_at = now;
                }
                return false;
            }
        }
        return false;
    }
    int bx, by, bw, bhh;
    name_btn(&bx, &by, &bw, &bhh);
    if (ui_hit(x, y, bx, by, bw, bhh, 2)) {
        snprintf(s_name, sizeof s_name, "%s", friends_my_name());
        kbd_open("THE NAME OF THIS BOARD", s_name, sizeof s_name, false);
        s_mode = FP_NAME;
        return false;
    }
    learn_btn(&bx, &by, &bw, &bhh);
    if (ui_hit(x, y, bx, by, bw, bhh, 2)) {
        learn_start(now);
        sfx_ui(SFX_TICK);
        return false;
    }
    if (pages() > 1) {
        page_btn(&bx, &by, &bw, &bhh);
        if (ui_hit(x, y, bx, by, bw, bhh, 2)) {
            s_fpage = (s_fpage + 1) % pages();
            sfx_ui(SFX_TICK);
            return false;
        }
    }
    if (s_fpage >= pages()) {
        s_fpage = 0;
    }
    for (int i = s_fpage * per_page(); i < friends_count() && i < (s_fpage + 1) * per_page(); i++) {
        friend_btn(i - s_fpage * per_page(), &bx, &by, &bw, &bhh);
        if (ui_hit(x, y, bx, by, bw, bhh, 2)) {
            s_remove = i;
            s_mode = FP_REMOVE;
            return false;
        }
    }
    return false;
}

#if BOARD_ROUND
static void render_round(int64_t now);
#endif

void friends_page_render(int64_t now)
{
    if (s_mode == FP_NAME) {
        kbd_render(now);
        return;
    }
#if BOARD_ROUND
    if (FP_RND) {
        render_round(now);
        return;
    }
#endif
    gfx_fill(THEME_FRIENDS_BG);
    // BACK
    gfx_fill_rect(0, 0, BACK_W, gfx_h, THEME_FRIENDS_BACK);
    gfx_text_center_in(0, BACK_W, gfx_h / 2 - 4, "BACK", 1, THEME_FRIENDS_BACK_TEXT);
    char b[64];
    if (s_mode == FP_LEARN) {
        gfx_text(BACK_W + 8, 8, "LEARN A FRIEND", 2, THEME_FRIENDS_TITLE);
        if (wide()) {
            gfx_text(BACK_W + 8, 30, "OPEN LEARN A FRIEND ON THE OTHER BOARD TOO. TAP ITS NAME.", 1,
                     THEME_FRIENDS_HELP);
        } else {
            gfx_text(BACK_W + 8, 26, "OPEN LEARN A FRIEND ON THE", 1, THEME_FRIENDS_HELP);
            gfx_text(BACK_W + 8, 36, "OTHER BOARD TOO. TAP ITS NAME.", 1, THEME_FRIENDS_HELP);
        }
        int n = 0;
        for (int i = 0; i < N_CAND; i++) {
            if (!s_cand[i].seen || now - s_cand[i].seen > 4000) {
                continue;
            }
            int x, y, w, h;
            friend_btn(n++, &x, &y, &w, &h);
            snprintf(b, sizeof b, s_cand[i].added ? "%s: A FRIEND" : "ADD %s", s_cand[i].name);
            button(x, y, w, h, b, s_cand[i].added ? THEME_FRIENDS_BTN_ADDED : THEME_FRIENDS_BTN_ADD,
                   THEME_FRIENDS_BTN_TEXT);
        }
        if (!n) {
            snprintf(b, sizeof b, "LOOKING%.*s  (THIS BOARD IS %s)", (int)(now / 400) % 4, "...", friends_my_name());
            gfx_text(BACK_W + 8, gfx_h / 2, b, 1, THEME_FRIENDS_WARN);
        }
        if (s_msg[0] && now - s_msg_at < 4000) {
            gfx_text(BACK_W + 8, gfx_h - 16, s_msg, 1, THEME_FRIENDS_OK);
        }
        return;
    }
    gfx_text(BACK_W + 8, wide() ? 8 : 6, "FRIENDS", 2, THEME_FRIENDS_TITLE);
    snprintf(b, sizeof b, "THIS BOARD: %s", friends_my_name());
    gfx_text(BACK_W + 8, wide() ? 30 : 28, b, 1, THEME_FRIENDS_NAME);
    int x, y, w, h;
    name_btn(&x, &y, &w, &h);
    button(x, y, w, h, wide() ? "CHANGE NAME" : "CHANGE", THEME_FRIENDS_BTN, THEME_FRIENDS_BTN_TEXT);
    int near[FRIENDS_MAX], nn = friends_near(near, FRIENDS_MAX);
    if (s_fpage >= pages()) {
        s_fpage = 0;
    }
    for (int i = s_fpage * per_page(); i < friends_count() && i < (s_fpage + 1) * per_page(); i++) {
        bool is_near = false;
        for (int k = 0; k < nn; k++) {
            is_near |= near[k] == i;
        }
        friend_btn(i - s_fpage * per_page(), &x, &y, &w, &h);
        button(x, y, w, h, friends_get(i)->name, THEME_FRIENDS_BTN_FRIEND, THEME_FRIENDS_BTN_TEXT);
        if (is_near) {
            gfx_fill_circle(x + 8, y + h / 2, 3, THEME_FRIENDS_NEAR);
        }
    }
    if (!friends_count()) {
        if (wide()) {
            gfx_text(BACK_W + 8, gfx_h / 2 - 8, "NO FRIENDS YET. LEARN ONE HERE, OR TAP THE ADD BADGE IN A LOBBY.", 1,
                     THEME_FRIENDS_WARN);
        } else {
            gfx_text(BACK_W + 8, gfx_h / 2 - 14, "NO FRIENDS YET. LEARN ONE HERE,", 1, THEME_FRIENDS_WARN);
            gfx_text(BACK_W + 8, gfx_h / 2 - 2, "OR TAP THE ADD BADGE IN A LOBBY.", 1, THEME_FRIENDS_WARN);
        }
    } else if (wide()) {
        gfx_text(BACK_W + 8, gfx_h - 14, "TAP A FRIEND TO REMOVE IT", 1, THEME_FRIENDS_TEXT_DIM);
    } else if (pages() < 2) {
        gfx_text(BACK_W + 4, gfx_h - 30, "TAP A FRIEND:", 1, THEME_FRIENDS_TEXT_DIM);
        gfx_text(BACK_W + 4, gfx_h - 18, "REMOVE IT", 1, THEME_FRIENDS_TEXT_DIM);
    }
    if (pages() > 1) {
        page_btn(&x, &y, &w, &h);
        snprintf(b, sizeof b, "MORE %d/%d", s_fpage + 1, pages());
        button(x, y, w, h, b, THEME_FRIENDS_BTN, THEME_FRIENDS_BTN_TEXT);
    }
    learn_btn(&x, &y, &w, &h);
    button(x, y, w, h, "LEARN A FRIEND", THEME_FRIENDS_BTN_ADD, THEME_FRIENDS_BTN_TEXT);
    if (s_mode == FP_REMOVE) {
        int bx = wide() ? 60 : 4, bw = gfx_w - 2 * bx, by = gfx_h / 2 - 34;
        gfx_fill_rect(bx, by, bw, 92, THEME_FRIENDS_ASK_BG);
        gfx_frame_rect(bx, by, bw, 92, 2, THEME_FRIENDS_ASK_EDGE);
        snprintf(b, sizeof b, "REMOVE %s?", friends_get(s_remove) ? friends_get(s_remove)->name : "");
        gfx_text_center_in(bx, bw, by + 10, b, 1, THEME_FRIENDS_TEXT);
        yes_no(0, &x, &y, &w, &h);
        button(x, y, w, h, "YES", THEME_FRIENDS_BTN_DANGER, THEME_FRIENDS_BTN_TEXT);
        yes_no(1, &x, &y, &w, &h);
        button(x, y, w, h, "NO", THEME_FRIENDS_BTN, THEME_FRIENDS_BTN_TEXT);
    }
}

#if BOARD_ROUND
// the round frame: the same parts as friends_page_render(), in the places of the head comment of FP_RND
static void render_round(int64_t now)
{
    gfx_fill(THEME_FRIENDS_BG);
    char b[64];
    int x, y, w, h;
    if (s_mode == FP_LEARN) {
        rr_head("LEARN A FRIEND", "OPEN IT ON THE OTHER BOARD TOO", THEME_FRIENDS_TITLE, THEME_FRIENDS_HELP);
        int n = 0;
        for (int i = 0; i < N_CAND; i++) {
            if (!s_cand[i].seen || now - s_cand[i].seen > 4000) {
                continue;
            }
            friend_btn(n++, &x, &y, &w, &h);
            snprintf(b, sizeof b, s_cand[i].added ? "%s: A FRIEND" : "ADD %s", s_cand[i].name);
            rr_btn(x, y, w, h, b, s_cand[i].added ? THEME_FRIENDS_BTN_ADDED : THEME_FRIENDS_BTN_ADD,
                   THEME_FRIENDS_BTN_TEXT);
        }
        if (!n) {
            snprintf(b, sizeof b, "LOOKING%.*s", (int)(now / 400) % 4, "...");
            rr_line(&KF_BIG, RS(220), b, THEME_FRIENDS_WARN);
            snprintf(b, sizeof b, "THIS BOARD IS %s", friends_my_name());
            rr_line(&KF_TEXT, RS(256), b, THEME_FRIENDS_TEXT_DIM);
        } else {
            rr_line(&KF_TEXT, RS(330), "TAP ITS NAME", THEME_FRIENDS_HELP);
        }
        if (s_msg[0] && now - s_msg_at < 4000) {
            rr_line(&KF_TEXT, RS(362), s_msg, THEME_FRIENDS_OK);
        }
        rr_back("STOP", false, THEME_FRIENDS_BACK, THEME_FRIENDS_BACK, THEME_FRIENDS_BACK_TEXT,
                THEME_FRIENDS_BACK_TEXT);
        return;
    }
    snprintf(b, sizeof b, "THIS BOARD: %s", friends_my_name());
    rr_head("FRIENDS", b, THEME_FRIENDS_TITLE, THEME_FRIENDS_NAME);
    int near[FRIENDS_MAX], nn = friends_near(near, FRIENDS_MAX);
    if (s_fpage >= pages()) {
        s_fpage = 0;
    }
    for (int i = s_fpage * per_page(); i < friends_count() && i < (s_fpage + 1) * per_page(); i++) {
        bool is_near = false;
        for (int k = 0; k < nn; k++) {
            is_near |= near[k] == i;
        }
        friend_btn(i - s_fpage * per_page(), &x, &y, &w, &h);
        rr_btn(x, y, w, h, friends_get(i)->name, THEME_FRIENDS_BTN_FRIEND, THEME_FRIENDS_BTN_TEXT);
        if (is_near) {
            ui_circle(x + RS(10), y + h / 2, RS(4), THEME_FRIENDS_NEAR, 255);
        }
    }
    if (!friends_count()) {
        rr_line(&KF_TEXT, RS(170), "NO FRIENDS YET", THEME_FRIENDS_WARN);
        rr_line(&KF_TEXT, RS(198), "LEARN ONE HERE, OR TAP", THEME_FRIENDS_WARN);
        rr_line(&KF_TEXT, RS(226), "THE ADD BADGE IN A LOBBY", THEME_FRIENDS_WARN);
    } else {
        rr_line(&KF_LABEL, RS(374), "TAP A FRIEND TO REMOVE IT", THEME_FRIENDS_TEXT_DIM);
    }
    name_btn(&x, &y, &w, &h);
    rr_btn(x, y, w, h, "CHANGE NAME", THEME_FRIENDS_BTN, THEME_FRIENDS_BTN_TEXT);
    if (pages() > 1) {
        page_btn(&x, &y, &w, &h);
        snprintf(b, sizeof b, "MORE %d/%d", s_fpage + 1, pages());
        rr_btn(x, y, w, h, b, THEME_FRIENDS_BTN, THEME_FRIENDS_BTN_TEXT);
    }
    learn_btn(&x, &y, &w, &h);
    rr_btn(x, y, w, h, "LEARN A FRIEND", THEME_FRIENDS_BTN_ADD, THEME_FRIENDS_BTN_TEXT);
    rr_back("< BACK", false, THEME_FRIENDS_BACK, THEME_FRIENDS_BACK, THEME_FRIENDS_BACK_TEXT, THEME_FRIENDS_BACK_TEXT);
    if (s_mode == FP_REMOVE) {
        gfx_darken_rect(0, 0, gfx_w, gfx_h, 1);
        ui_rbox(RS(60), RS(160), RS(346), RS(170), RS(18), THEME_FRIENDS_ASK_BG, THEME_FRIENDS_ASK_EDGE, 2);
        snprintf(b, sizeof b, "REMOVE %s?", friends_get(s_remove) ? friends_get(s_remove)->name : "");
        rr_line(&KF_BIG, RS(214), b, THEME_FRIENDS_TEXT);
        yes_no(0, &x, &y, &w, &h);
        rr_btn(x, y, w, h, "YES", THEME_FRIENDS_BTN_DANGER, THEME_FRIENDS_BTN_TEXT);
        yes_no(1, &x, &y, &w, &h);
        rr_btn(x, y, w, h, "NO", THEME_FRIENDS_BTN, THEME_FRIENDS_BTN_TEXT);
    }
}
#endif

void friends_page_leave(void)
{
    if (s_mode == FP_LEARN) {
        learn_stop();
    }
    s_mode = FP_LIST;
}
