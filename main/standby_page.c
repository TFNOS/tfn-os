// SETUP > STANDBY (standby_page.h): the settings of STANDBY (standby.h). A list in the shared momentum scroll
// (libs/kscroll), as SETUP > NOTIFICATIONS; each setting is a pill that a tap moves to its next value (36 px high: 4.6
// mm or more on each board). The values go to the NVS at once (sb_cfg_save).
#include "standby_page.h"
#include "tfn/theme.h"
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio.h"
#include "board.h"
#include "gfx.h"
#include "standby_priv.h"
#include "../libs/kscroll/kscroll.h"
#include "set_round.h"

// THE ROUND FRAME (gfx_round()): the title on the top cap, the rows in the middle box (the name, then the pill under
// it), BACK on the bottom cap.
#if BOARD_ROUND
#define NP_RND gfx_round()
#else
#define NP_RND false
#endif
#define BACK_W 56
#define PILL_H (NP_RND ? RS(42) : 36)

enum {
    RW_ON,
    RW_FACE,
    RW_NIGHT,
    RW_HOURS,
    RW_ACCENT,
    RW_SLOT,  // .. RW_SLOT + SB_SLOTS - 1
    RW_SHOW = RW_SLOT + SB_SLOTS,
    RW_INFO,  // the place and the credit of the weather data (no pill)
    RW_N
};
// the night hours (from, to): the pill gives the next pair
static const uint8_t HOURS[][2] = {{23, 6}, {22, 6}, {22, 7}, {23, 7}, {21, 6}, {0, 7}, {20, 6}};
#define NHOURS ((int)(sizeof HOURS / sizeof HOURS[0]))
static int s_y0, s_x0, s_press = -1;
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

// the pill of a row: x, w (no pill: false)
static bool pill(int r, int *x, int *w)
{
    if (r == RW_INFO) {
        return false;
    }
#if BOARD_ROUND
    if (NP_RND) {
        *w = RS(190);
        *x = RB_X + RB_W - RS(190);
        return true;
    }
#endif
    *w = wide() ? 220 : 160;
    *x = gfx_w - 8 - *w;
    return true;
}
static int pill_y(int top)
{
    if (NP_RND) {
        return top + RS(38);
    }
    return wide() ? top + (row_h() - PILL_H) / 2 : top + 28;
}

static void name_of(int r, char *b, int n)
{
    static const char *const N[] = {"STANDBY", "FACE", "AUTO NIGHT", "NIGHT HOURS", "ACCENT"};
    if (r < RW_SLOT) {
        snprintf(b, n, "%s", N[r]);
    } else if (r < RW_SHOW) {
        snprintf(b, n, "SLOT %d", r - RW_SLOT + 1);
    } else if (r == RW_SHOW) {
        snprintf(b, n, "TRY IT");
    } else {
        const sb_place_t *p = sb_place();
        snprintf(b, n, "PLACE: %s. WEATHER DATA BY OPEN-METEO.COM", p->ok ? p->name : "NOT KNOWN YET");
    }
}

static void value(int r, char *b, int n, bool *on)
{
    *on = true;
    switch (r) {
    case RW_ON:
        *on = sb_cfg.on;
        snprintf(b, n, "%s", sb_cfg.on ? "ON" : "OFF");
        return;
    case RW_FACE:
        snprintf(b, n, "%s", sb_face_name(sb_cfg.face));
        return;
    case RW_NIGHT:
        *on = sb_cfg.autonight;
        snprintf(b, n, "%s", sb_cfg.autonight ? "ON" : "OFF");
        return;
    case RW_HOURS:
        snprintf(b, n, "%02d:00 - %02d:00", sb_cfg.nfrom, sb_cfg.nto);
        return;
    case RW_ACCENT:
        snprintf(b, n, "%s", sb_accent_name(sb_cfg.accent));
        return;
    case RW_SHOW:
        snprintf(b, n, "SHOW IT NOW");
        return;
    default:
        *on = sb_cfg.slot[r - RW_SLOT] != SL_NONE;
        snprintf(b, n, "%s", sb_slot_name(sb_cfg.slot[r - RW_SLOT]));
        return;
    }
}

static void toggle(int r)
{
    int h = 0;
    switch (r) {
    case RW_ON:
        sb_cfg.on = !sb_cfg.on;
        break;
    case RW_FACE:
        sb_cfg.face = (uint8_t)((sb_cfg.face + 1) % SB_FACES);
        break;
    case RW_NIGHT:
        sb_cfg.autonight = !sb_cfg.autonight;
        break;
    case RW_HOURS:
        while (h < NHOURS && !(HOURS[h][0] == sb_cfg.nfrom && HOURS[h][1] == sb_cfg.nto)) {
            h++;
        }
        h = (h + 1) % NHOURS;
        sb_cfg.nfrom = HOURS[h][0];
        sb_cfg.nto = HOURS[h][1];
        break;
    case RW_ACCENT:
        sb_cfg.accent = (uint8_t)((sb_cfg.accent + 1) % SB_ACCENTS);
        break;
    case RW_SHOW:
        sb_preview();  // (the menu opens, then STANDBY)
        app_open(&app_menu);
        break;
    default:
        sb_cfg.slot[r - RW_SLOT] = (uint8_t)((sb_cfg.slot[r - RW_SLOT] + 1) % SL_KINDS);
        break;
    }
    if (r != RW_SHOW) {
        sb_cfg_save();
    }
    sfx_ui(SFX_CLICK);
}

void standby_page_open(void)
{
    ks_init(&s_ks, (float)list_h(), 0);
    s_press = -1;
    s_drag = s_back = false;
}

// the row of the pill under (x, y), or -1
static int hit(int x, int y)
{
    if (y < head_h() || (NP_RND && y >= head_h() + list_h())) {
        return -1;
    }
    const int sc = ks_ipos(&s_ks);
    for (int r = 0; r < RW_N; r++) {
        const int top = head_h() + r * row_h() - sc, py = pill_y(top);
        int px, pw;
        if (pill(r, &px, &pw) && x >= px - 4 && x < px + pw + 4 && y >= py - 6 && y < py + PILL_H + 6) {
            return r;
        }
    }
    return -1;
}

void standby_page_update(int64_t now)
{
    ks_set_max(&s_ks, (float)(RW_N * row_h() - list_h() + 6));
    ks_update(&s_ks, now);
}

static bool back_at(int x, int y)
{
#if BOARD_ROUND
    if (NP_RND) {
        return rr_back_at(x, y);
    }
#endif
    (void)y;
    return x < BACK_W;
}

bool standby_page_input(const input_event_t *ev, int64_t now)
{
    (void)now;
    switch (ev->kind) {
    case IN_DOWN:
        s_x0 = ev->x;
        s_y0 = ev->y;
        s_drag = false;
        s_back = back_at(ev->x, ev->y);
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
        if (s_back && back_at(ev->x, ev->y)) {
            sfx_ui(SFX_BACK);
            return true;
        }
        if (s_back || !ks_tap_ok(&s_ks)) {
            break;
        }
        if (s_press >= 0 && hit(ev->x, ev->y) == s_press) {
            toggle(s_press);
        }
        s_press = -1;
        break;
    default:
        break;
    }
    return false;
}

#if BOARD_ROUND
// the round frame: the rows (the name, the pill under it) between the caps, then the caps
static void render_round(void)
{
    gfx_fill(THEME_NOTE_PAGE_BG);
    const int sc = ks_ipos(&s_ks), rh = row_h(), y0 = head_h(), y1 = y0 + list_h();
    for (int r = 0; r < RW_N; r++) {
        const int top = y0 + r * rh - sc;
        if (top + rh < y0 || top > y1) {
            continue;
        }
        char name[96];
        name_of(r, name, sizeof name);
        ui_rrect(RB_X - RS(6), top + 2, RB_W + RS(12), rh - 6, RS(12), THEME_NOTE_PAGE_LINE);
        ui_text(rr_fit(&KF_TEXT, name, RB_W - RS(10)), RB_X + RS(4), top + RS(28), name, THEME_NOTE_PAGE_TEXT);
        int px, pw;
        if (!pill(r, &px, &pw)) {
            continue;
        }
        char b[32];
        bool on;
        value(r, b, sizeof b, &on);
        rr_btn(px, pill_y(top), pw, PILL_H, b,
               s_press == r ? THEME_NOTE_PAGE_PILL_DOWN
               : on         ? THEME_NOTE_PAGE_PILL_ON
                            : THEME_NOTE_PAGE_PILL,
               on ? THEME_NOTE_PAGE_PILL_TEXT_ON : THEME_NOTE_PAGE_PILL_TEXT);
    }
    int at, sz, al;
    if (ks_bar(&s_ks, list_h() - 8, &at, &sz, &al) && al > 64) {
        ui_rrect(RB_X + RB_W + RS(10), y0 + 4 + at, 4, sz, 2, THEME_NOTE_PAGE_SCROLL);
    }
    gfx_fill_rect(0, 0, gfx_w, y0, THEME_NOTE_PAGE_BG);
    gfx_fill_rect(0, y1, gfx_w, gfx_h - y1, THEME_NOTE_PAGE_BG);
    rr_head("STANDBY", "", THEME_NOTE_PAGE_TITLE, THEME_NOTE_PAGE_TEXT);
    rr_back("< BACK", false, THEME_NOTE_PAGE_BACK, THEME_NOTE_PAGE_BACK, THEME_NOTE_PAGE_BACK_TEXT,
            THEME_NOTE_PAGE_BACK_TEXT);
}
#endif

// the info row: the place and the credit in the dot font, in one or two lines
static void info_row(int top, const char *s)
{
    const int maxc = (gfx_w - BACK_W - 20) / 6, len = (int)strlen(s);
    char a[96];
    snprintf(a, sizeof a, "%.*s", len > maxc ? maxc : len, s);
    gfx_text(BACK_W + 10, top + 10, a, 1, THEME_NOTE_PAGE_TEXT);
    if (len > maxc) {
        gfx_text(BACK_W + 10, top + 24, s + maxc, 1, THEME_NOTE_PAGE_TEXT);
    }
}

void standby_page_render(int64_t now)
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
    for (int r = 0; r < RW_N; r++) {
        const int top = head_h() + r * rh - sc;
        if (top + rh < head_h() || top > gfx_h) {
            continue;
        }
        char name[96];
        name_of(r, name, sizeof name);
        int px, pw;
        if (!pill(r, &px, &pw)) {
            info_row(top, name);
            continue;
        }
        gfx_text(BACK_W + 10, wide() ? top + (rh - 14) / 2 : top + 8, name, 2, THEME_NOTE_PAGE_TEXT);
        char b[32];
        bool on;
        value(r, b, sizeof b, &on);
        const int py = pill_y(top);
        const uint16_t fill = s_press == r ? THEME_NOTE_PAGE_PILL_DOWN
                              : on         ? THEME_NOTE_PAGE_PILL_ON
                                           : THEME_NOTE_PAGE_PILL;
        gfx_fill_rect(px, py, pw, PILL_H, fill);
        gfx_fill_rect(px, py, pw, 1, THEME_NOTE_PAGE_PILL_EDGE);
        gfx_fill_rect(px, py + PILL_H - 1, pw, 1, THEME_NOTE_PAGE_PILL_EDGE);
        gfx_text_center_in(px, pw, py + PILL_H / 2 - 4, b, 1,
                           on ? THEME_NOTE_PAGE_PILL_TEXT_ON : THEME_NOTE_PAGE_PILL_TEXT);
    }
    int at, sz, al;
    if (ks_bar(&s_ks, list_h() - 8, &at, &sz, &al) && al > 64) {
        gfx_fill_rect(gfx_w - 4, head_h() + 4 + at, 2, sz, THEME_NOTE_PAGE_SCROLL);
    }
    // the head and BACK over the list
    gfx_fill_rect(BACK_W, 0, gfx_w - BACK_W, head_h(), THEME_NOTE_PAGE_BG);
    gfx_text(BACK_W + 8, 8, "STANDBY", 2, THEME_NOTE_PAGE_TITLE);
    gfx_fill_rect(0, 0, BACK_W, gfx_h, THEME_NOTE_PAGE_BACK);
    gfx_text_center_in(0, BACK_W, gfx_h / 2 - 4, "BACK", 1, THEME_NOTE_PAGE_BACK_TEXT);
}

const char *standby_state_text(void)
{
    static char b[32];
    if (!sb_cfg.on) {
        return "OFF";
    }
    snprintf(b, sizeof b, "ON, %s", sb_face_name(sb_cfg.face));
    return b;
}
