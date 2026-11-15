// SETTINGS > NOTIFICATIONS (notify_page.h): the switches of the notices. A list in the shared momentum scroll
// (libs/kscroll); each switch is a pill that a tap moves to its next value (36 px high: 4.6 mm or more on each board).
#include "notify_page.h"
#include "tfn/theme.h"
#include <stdio.h>
#include <string.h>
#include "audio.h"
#include "board.h"
#include "gfx.h"
#include "notify.h"
#include "watch.h"
#include "../libs/kscroll/kscroll.h"
#include "set_round.h"

// THE ROUND FRAME (gfx_round()): the title on the top cap, the rows in the middle box (the name, then the pills under
// it), BACK on the bottom cap.
#if BOARD_ROUND
#define NP_RND gfx_round()
#else
#define NP_RND false
#endif
#define BACK_W 56
#define PILL_H (NP_RND ? RS(42) : 36)
#define NAPPS  (NT_MAX)

enum {
    RW_ALL,
    RW_DND,
    RW_GAME,
    RW_BG,
    RW_APP
};
static char s_apps[NAPPS][NT_NAME];
static int s_napps, s_y0, s_x0, s_press = -1;
static bool s_drag, s_back;
static kscroll_t s_ks;

static bool wide(void)
{
    return gfx_w >= 480;
}
static int row_h(void)
{
#if BOARD_ROUND
    if (NP_RND) {
        return RS(94);
    }
#endif
    return wide() ? 48 : 70;
}
static int head_h(void)
{
#if BOARD_ROUND
    if (NP_RND) {
        return RB_Y;
    }
#endif
    return 30;
}
// the height of the list on the screen
static int list_h(void)
{
#if BOARD_ROUND
    if (NP_RND) {
        return RB_B - RB_Y;
    }
#endif
    return gfx_h - head_h();
}
static int nrows(void)
{
    return RW_APP + s_napps;
}

void notify_page_open(void)
{
    s_napps = notify_apps(s_apps, NAPPS);
    bool sys = false;
    for (int i = 0; i < s_napps; i++) {
        sys |= !strcmp(s_apps[i], NT_SYSTEM);
    }
    if (!sys && s_napps < NAPPS) {
        snprintf(s_apps[s_napps++], NT_NAME, "%s", NT_SYSTEM);  // (the firmware updates)
    }
    ks_init(&s_ks, (float)list_h(), 0);
    s_press = -1;
    s_drag = s_back = false;
}

// the pills of a row: k = 0 .. 2; x, w of the pill; the row top y (on the screen)
static int pills(int r, int k, int *x, int *w)
{
#if BOARD_ROUND
    if (NP_RND) {
        // (the round frame: one pill at the right, or three across the middle box)
        if (r < RW_APP) {
            *w = k ? 0 : RS(180);
            *x = RB_X + RB_W - RS(180);
            return 1;
        }
        const int W[3] = {RS(138), RS(106), RS(106)};
        int xx = RB_X;
        for (int i = 0; i < k; i++) {
            xx += W[i] + RS(8);
        }
        *x = xx;
        *w = W[k];
        return 3;
    }
#endif
    const int x0 = BACK_W + 8, x1 = gfx_w - 8;
    if (r < RW_APP) {
        if (k) {
            return 0;
        }
        *w = wide() ? 200 : 150;
        *x = x1 - *w;
        return 1;
    }
    if (wide()) {
        static const int W[3] = {140, 110, 110};
        int xx = x1 - (W[0] + W[1] + W[2] + 16);
        for (int i = 0; i < k; i++) {
            xx += W[i] + 8;
        }
        *x = xx;
        *w = W[k];
    } else {
        static const int W[3] = {104, 72, 72};
        int xx = x0;
        for (int i = 0; i < k; i++) {
            xx += W[i] + 6;
        }
        *x = xx;
        *w = W[k];
    }
    return 3;
}
static int pill_y(int top)
{
    if (NP_RND) {
        return top + RS(38);
    }
    return wide() ? top + (row_h() - PILL_H) / 2 : top + 28;
}

static void value(int r, int k, char *b, int n, bool *on)
{
    *on = true;
    if (r == RW_ALL) {
        *on = notify_all_on();
        snprintf(b, n, "%s", *on ? "ON" : "OFF");
        return;
    }
    if (r == RW_DND) {
        static const char *const D[3] = {"23:00 - 07:00", "22:00 - 08:00", "OFF"};
        int d = notify_dnd();
        *on = d != 2;
        snprintf(b, n, "%s", D[d]);
        return;
    }
    if (r == RW_GAME) {
        *on = notify_in_games();
        snprintf(b, n, "%s", *on ? "A BANNER" : "NO BANNER");
        return;
    }
    if (r == RW_BG) {  // the background checks of the apps (watch.h)
        int e = watch_every();
        *on = e != 0;
        if (e) {
            snprintf(b, n, "EVERY %d MIN", e);
        } else {
            snprintf(b, n, "OFF");
        }
        return;
    }
    const char *a = s_apps[r - RW_APP];
    if (k == 0) {
        static const char *const M[3] = {"ON", "LIST ONLY", "OFF"};
        int m = notify_mode(a);
        *on = m == NM_ON;
        snprintf(b, n, "%s", M[m]);
    } else if (k == 1) {
        *on = notify_wake(a);
        snprintf(b, n, "%s", *on ? "WAKE: YES" : "WAKE: NO");
    } else {
        *on = notify_sound(a);
        snprintf(b, n, "%s", *on ? "SOUND: ON" : "SOUND: OFF");
    }
}

static void toggle(int r, int k)
{
    if (r == RW_ALL) {
        notify_set_all(!notify_all_on());
    } else if (r == RW_DND) {
        notify_set_dnd((notify_dnd() + 1) % 3);
    } else if (r == RW_GAME) {
        notify_set_in_games(!notify_in_games());
    } else if (r == RW_BG) {
        int e = watch_every();
        watch_set_every(e == 5 ? 10 : e == 10 ? 15 : e == 15 ? 30 : e == 30 ? 0 : 5);
    } else {
        const char *a = s_apps[r - RW_APP];
        if (k == 0) {
            notify_set_mode(a, (notify_mode(a) + 1) % 3);
        } else if (k == 1) {
            notify_set_wake(a, !notify_wake(a));
        } else {
            notify_set_sound(a, !notify_sound(a));
        }
    }
    sfx_ui(SFX_CLICK);
}

// the pill under (x, y): r * 4 + k, or -1
static int hit(int x, int y)
{
    if (y < head_h() || (NP_RND && y >= head_h() + list_h())) {
        return -1;
    }
    const int sc = ks_ipos(&s_ks);
    for (int r = 0; r < nrows(); r++) {
        const int top = head_h() + r * row_h() - sc, py = pill_y(top);
        for (int k = 0; k < 3; k++) {
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

void notify_page_update(int64_t now)
{
    ks_set_max(&s_ks, (float)(nrows() * row_h() - list_h() + 6));
    ks_update(&s_ks, now);
}

bool notify_page_input(const input_event_t *ev, int64_t now)
{
    (void)now;
    switch (ev->kind) {
    case IN_DOWN:
        s_x0 = ev->x;
        s_y0 = ev->y;
        s_drag = false;
#if BOARD_ROUND
        s_back = NP_RND ? rr_back_at(ev->x, ev->y) : ev->x < BACK_W;
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
    case IN_UP:
        if (!s_back) {
            ks_up(&s_ks, ev->t_ms);
        }
        break;
    case IN_TAP:
#if BOARD_ROUND
        if (s_back && (NP_RND ? rr_back_at(ev->x, ev->y) : ev->x < BACK_W)) {
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
            toggle(s_press / 4, s_press % 4);
        }
        s_press = -1;
        break;
    default:
        break;
    }
    return false;
}

#if BOARD_ROUND
// the round frame: the rows (the name, the pills under it) between the caps, then the caps
static void render_round(void)
{
    gfx_fill(THEME_NOTE_PAGE_BG);
    const int sc = ks_ipos(&s_ks), rh = row_h(), y0 = head_h(), y1 = y0 + list_h();
    for (int r = 0; r < nrows(); r++) {
        const int top = y0 + r * rh - sc;
        if (top + rh < y0 || top > y1) {
            continue;
        }
        const char *name = r == RW_ALL                              ? "ALL NOTIFICATIONS"
                           : r == RW_DND                            ? "DO NOT DISTURB"
                           : r == RW_GAME                           ? "IN GAMES"
                           : r == RW_BG                             ? "BACKGROUND CHECKS"
                           : !strcmp(s_apps[r - RW_APP], NT_SYSTEM) ? "SYSTEM (UPDATES)"
                                                                    : s_apps[r - RW_APP];
        ui_rrect(RB_X - RS(6), top + 2, RB_W + RS(12), rh - 6, RS(12), THEME_NOTE_PAGE_LINE);
        ui_text(rr_fit(&KF_TEXT, name, RB_W - RS(10)), RB_X + RS(4), top + RS(28), name, THEME_NOTE_PAGE_TEXT);
        for (int k = 0; k < 3; k++) {
            int px, pw;
            if (k >= pills(r, k, &px, &pw)) {
                break;
            }
            char b[24];
            bool on;
            value(r, k, b, sizeof b, &on);
            const bool down = s_press == r * 4 + k;
            rr_btn(px, pill_y(top), pw, PILL_H, b,
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
    rr_head("NOTIFICATIONS", "", THEME_NOTE_PAGE_TITLE, THEME_NOTE_PAGE_TEXT);
    rr_back("< BACK", false, THEME_NOTE_PAGE_BACK, THEME_NOTE_PAGE_BACK, THEME_NOTE_PAGE_BACK_TEXT,
            THEME_NOTE_PAGE_BACK_TEXT);
}
#endif

void notify_page_render(int64_t now)
{
    (void)now;
#if BOARD_ROUND
    if (NP_RND) {
        render_round();
        return;
    }
#endif
    gfx_fill(THEME_NOTE_PAGE_BG);
    const int sc = ks_ipos(&s_ks), rh = row_h();
    for (int r = 0; r < nrows(); r++) {
        const int top = head_h() + r * rh - sc;
        if (top + rh < head_h() || top > gfx_h) {
            continue;
        }
        if (r == RW_APP) {
            gfx_fill_rect(BACK_W + 8, top - 1, gfx_w - BACK_W - 16, 1, THEME_NOTE_PAGE_LINE);
        }
        const char *name = r == RW_ALL                              ? "ALL NOTIFICATIONS"
                           : r == RW_DND                            ? "DO NOT DISTURB"
                           : r == RW_GAME                           ? "IN GAMES"
                           : r == RW_BG                             ? "BACKGROUND CHECKS"
                           : !strcmp(s_apps[r - RW_APP], NT_SYSTEM) ? "SYSTEM (UPDATES)"
                                                                    : s_apps[r - RW_APP];
        const int ty = wide() || r < RW_APP ? top + (rh - 14) / 2 : top + 8;
        gfx_text(BACK_W + 10, ty, name, 2, THEME_NOTE_PAGE_TEXT);
        for (int k = 0; k < 3; k++) {
            int px, pw;
            if (k >= pills(r, k, &px, &pw)) {
                break;
            }
            char b[24];
            bool on;
            value(r, k, b, sizeof b, &on);
            const int py = pill_y(top);
            const bool down = s_press == r * 4 + k;
            const uint16_t fill = down ? THEME_NOTE_PAGE_PILL_DOWN
                                  : on ? THEME_NOTE_PAGE_PILL_ON
                                       : THEME_NOTE_PAGE_PILL;
            gfx_fill_rect(px, py, pw, PILL_H, fill);
            gfx_fill_rect(px, py, pw, 1, THEME_NOTE_PAGE_PILL_EDGE);
            gfx_fill_rect(px, py + PILL_H - 1, pw, 1, THEME_NOTE_PAGE_PILL_EDGE);
            gfx_text_center_in(px, pw, py + PILL_H / 2 - 4, b, 1,
                               on ? THEME_NOTE_PAGE_PILL_TEXT_ON : THEME_NOTE_PAGE_PILL_TEXT);
        }
    }
    int at, sz, al;
    if (ks_bar(&s_ks, list_h() - 8, &at, &sz, &al) && al > 64) {
        gfx_fill_rect(gfx_w - 4, head_h() + 4 + at, 2, sz, THEME_NOTE_PAGE_SCROLL);
    }
    // the head and BACK over the list
    gfx_fill_rect(BACK_W, 0, gfx_w - BACK_W, head_h(), THEME_NOTE_PAGE_BG);
    gfx_text(BACK_W + 8, 8, "NOTIFICATIONS", 2, THEME_NOTE_PAGE_TITLE);
    gfx_fill_rect(0, 0, BACK_W, gfx_h, THEME_NOTE_PAGE_BACK);
    gfx_text_center_in(0, BACK_W, gfx_h / 2 - 4, "BACK", 1, THEME_NOTE_PAGE_BACK_TEXT);
}
