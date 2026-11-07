// The menu on a round panel: the carousel of the Watcher and the 1.75C.
#include "app_menu_priv.h"

// The menu on a round panel (gfx_round(): the SenseCAP Watcher).
//
// One item is big in the middle; its neighbours sit on a low curve at the sides. A row of dots on
// the lower rim shows the place in the level. The wheel moves the selection (a tick each step), a
// push of the wheel opens the item, a hold of the wheel goes up one level (main.c, menu_back()).
// Touch: a swipe moves the row, a tap on the middle item opens it, a tap on a side item selects
// it, a hold on the middle app = favorite on / off.

// (the menu has APP_ROUND_HI: the frame is 466 x 466 on the AMOLED-1.75C, 412 x 412 on the Watcher; the
// sizes below are for 412 and RS() scales them to the frame)
#define RS(v)     ((v) * gfx_w / 412)
#define R_SPAN    RS(150)  // px between two neighbours (at the middle)
#define R_BIG     RS(150)  // the size of the middle tile
#define R_SMALL   RS(72)   // the size of the tiles at one step and more
#define R_CY      RS(200)  // the centre line of the middle tile
#define R_TIME_Y  RS(76)   // the baseline of the time (KF_TITLE), like a watch face
#define R_TITLE_Y RS(100)  // the baseline of the title (the name of the level, KF_LABEL)
#define R_UP_BASE 49       // (not used: the path of the parents goes before the title)

#include "mfont.h"  // (the small lines: the Rubik face is easier to read than the 8 px letters)
#include <time.h>
#include "alarm.h"
#include "tz.h"
#include "tfn/theme.h"
// the path of the parents fits in the circle at R_UP_BASE
static bool r_up_fits(const char *s)
{
    return ui_text_w(&KF_LABEL, s) <= RS(170);
}

float men_rpos;                   // the place now (animated), in items
int men_rsel;                     // the selected item
static int s_rsel_of[G_MAX + 2];  // the selection of each level (the favorites = G_MAX, the top level = G_MAX + 1)
bool men_rtouch, men_rdrag;
static int s_rx0, s_rpress;  // the press: x, and the item under it (-1 = none)
static float s_rpos0;
static int64_t s_rdown_t;
bool men_rhold_done;

static int rlevel_slot(void)
{
    return men_level < 0 ? G_MAX + 1 : men_level;
}

void men_r_clamp(void)
{
    if (men_rsel > men_n_items - 1) {
        men_rsel = men_n_items - 1;
    }
    if (men_rsel < 0) {
        men_rsel = 0;
    }
}

// After a level change (go_level, build_items): the group we left (up), the selection that this level had, or its
// first app.
void men_r_level_changed(int from_level)
{
    int up = -1;
    for (int i = 0; i < men_n_items; i++) {
        if (men_items[i].kind == IT_CAT && from_level >= 0 && men_items[i].cat == from_level) {
            up = i;  // the group we left
        }
    }
    if (up >= 0) {
        men_rsel = up;
    } else if (men_level >= 0) {
        men_rsel = s_rsel_of[rlevel_slot()];
        if (men_rsel == 0 && men_n_items > 1) {
            men_rsel = 1;  // not BACK: the first app
        }
    } else {
        men_rsel = 0;
    }
    men_r_clamp();
    men_rpos = (float)men_rsel;
    men_rtouch = men_rdrag = false;
}

// The menu opens again (from an app): the item stays, the animation starts there.
void men_r_enter(void)
{
    men_r_clamp();
    men_rpos = (float)men_rsel;
    men_rtouch = men_rdrag = false;
}

static void r_select(int i)
{
    if (i < 0) {
        i = 0;
    }
    if (i > men_n_items - 1) {
        i = men_n_items - 1;
    }
    if (i != men_rsel) {
        sfx_tone(2400, 2400, 6, W_SQUARE, 18);
    }
    men_rsel = i;
    s_rsel_of[rlevel_slot()] = i;
}

static void r_open(int i)
{
    s_rsel_of[rlevel_slot()] = i;
    men_open_item(i);  // a group: go_level() calls r_level_changed()
}

// the size and the place of item i (its top-left corner), u = i - s_rpos
static void r_geom(int i, int *x, int *y, int *s)
{
    float u = i - men_rpos, a = fabsf(u);
    float sz = a < 1.0f ? R_BIG - (R_BIG - R_SMALL) * a : R_SMALL - (a - 1.0f) * 10.0f;
    float cx = gfx_w / 2 + (a < 1.0f ? u * R_SPAN : (u > 0 ? 1.0f : -1.0f) * (R_SPAN + (a - 1.0f) * 62.0f));
    float cy = R_CY + a * a * 14.0f;
    *s = (int)sz;
    *x = (int)(cx - sz / 2);
    *y = (int)(cy - sz / 2);
}

static int r_item_at(int x, int y)
{
    int best = -1;
    for (int i = 0; i < men_n_items; i++) {
        int ix, iy, s;
        r_geom(i, &ix, &iy, &s);
        if (fabsf(i - men_rpos) > 1.7f) {
            continue;
        }
        if (x >= ix - 8 && x < ix + s + 8 && y >= iy - 8 && y < iy + s + 8) {
            if (best < 0 || fabsf(i - men_rpos) < fabsf(best - men_rpos)) {
                best = i;
            }
        }
    }
    return best;
}

void men_round_input(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        men_rtouch = true;
        men_rdrag = false;
        men_rhold_done = false;
        s_rx0 = ev->x;
        s_rpos0 = men_rpos;
        s_rdown_t = ev->t_ms;
        s_rpress = r_item_at(ev->x, ev->y);
        break;
    case IN_MOVE:
        if (!men_rtouch) {
            break;
        }
        if (!men_rdrag && abs(ev->x - s_rx0) > DRAG_PX) {
            men_rdrag = true;
        }
        if (men_rdrag) {
            men_rpos = s_rpos0 - (ev->x - s_rx0) / (float)R_SPAN;
            if (men_rpos < -0.4f) {
                men_rpos = -0.4f;
            }
            if (men_rpos > men_n_items - 0.6f) {
                men_rpos = men_n_items - 0.6f;
            }
            r_select((int)roundf(men_rpos));
        }
        break;
    case IN_UP:
        men_rtouch = false;
        break;
    case IN_TAP: {
        // the tile under the mean point of the touch (the edge of a thumb touches first: not the point of IN_DOWN)
        if (men_rdrag || men_rhold_done) {
            break;
        }
        int i = r_item_at(ev->x, ev->y);
        if (i >= 0 && i == men_rsel) {
            r_open(men_rsel);
        } else if (i >= 0) {
            r_select(i);
        }
        break;
    }
    default:
        break;
    }
}

void men_round_update(int64_t now, float dt)
{
    int d = input_wheel();
    if (d && men_n_items) {
        r_select(men_rsel + d);
    }
    if (input_wheel_presses() && men_n_items) {
        r_open(men_rsel);
    }
    // HOME 2.0 (menu_home.c): a hold on an app or a group opens its sheet; a hold on the background, the edit mode
    if (men_rtouch && !men_rdrag && !men_rhold_done && now - s_rdown_t >= HOLD_MS) {
        if (s_rpress >= 0 && s_rpress < men_n_items) {
            if (men_home_hold_item(s_rpress, men_level)) {
                men_rhold_done = true;
                men_rsel = s_rpress;
                men_r_clamp();
                return;
            }
        } else {
            men_rhold_done = true;
            men_home_hold_bg(s_rdown_t, s_rx0, gfx_h / 2);
            return;
        }
    }
    if (!men_rtouch) {
        float k = 14.0f * dt;
        men_rpos += (men_rsel - men_rpos) * (k > 1 ? 1 : k);
    }
}

static uint8_t s_rta[3] = {40, 60, 140};  // the colour of the sky now (round_render)
static uint16_t r_bg(int y)
{
    int k = abs(y - R_CY) * 255 / gfx_h;
    return rgb((uint8_t)((s_rta[0] * (255 - k) / 255) / 4 + 6), (uint8_t)((s_rta[1] * (255 - k) / 255) / 4 + 8),
               (uint8_t)((s_rta[2] * (255 - k) / 255) / 3 + 22));
}
// round corners of radius r on the square tile at (x, y), size s: the parts out of the arcs get the colour c (c = 0:
// the sky of each row)
static void r_corners(int x, int y, int s, int r, uint16_t c)
{
    for (int k = 0; k < r; k++) {
        const float dy = r - k - 0.5f;
        const int cut = r - (int)(sqrtf((float)(r * r) - dy * dy) + 0.5f);
        if (cut <= 0) {
            continue;
        }
        const int yt = y + k, yb = y + s - 1 - k;
        gfx_fill_rect(x, yt, cut, 1, c ? c : r_bg(yt));
        gfx_fill_rect(x + s - cut, yt, cut, 1, c ? c : r_bg(yt));
        gfx_fill_rect(x, yb, cut, 1, c ? c : r_bg(yb));
        gfx_fill_rect(x + s - cut, yb, cut, 1, c ? c : r_bg(yb));
    }
}

static void r_tile(int i, int x, int y, int s, bool sel, int64_t now)
{
    const int rr = s / 6;  // the radius of the corners
    if (sel) {
        // the glow and the ring of the selected tile (the corners of the tile get the colour of the ring)
        // (only the bands out of the tile: the tile covers the middle; the full boxes cost 25 ms a frame)
        ui_rrect_band(x - 12, y - 12, s + 24, s + 24, rr + 12, THEME_MENU_ROUND_ACCENT, 40, 12);
        ui_rrect_band(x - 7, y - 7, s + 14, s + 14, rr + 7, THEME_MENU_ROUND_ACCENT, 70, 7);
        ui_rrect_band(x - 4, y - 4, s + 8, s + 8, rr + 4, THEME_MENU_ROUND_ACCENT, 255, 4);
    }
    const item_t *it = &men_items[i];
    if (it->kind == IT_APP) {
        it->app->draw_icon(x, y, s);
        if (men_fav_index(it->app) >= 0) {
            int r = s / 9 + 4;
            men_star(x + s - r - 2, y + r + 2, r + 3, THEME_MENU_FAV_EDGE);
            men_star(x + s - r - 2, y + r + 2, r, THEME_MENU_FAV);
        }
        men_badge(it->app, x, y, s);
        if (sel && men_rtouch && !men_rdrag && s_rpress == i) {
            int64_t held = now - s_rdown_t;
            if (held >= HOLD_SHOW_MS) {
                int w = (int)((held - HOLD_SHOW_MS) * s / (HOLD_MS - HOLD_SHOW_MS));
                if (w > s) {
                    w = s;
                }
                gfx_fill_rect(x, y + s - 9, s, 9, THEME_MENU_HOLD_TRACK);
                gfx_fill_rect(x, y + s - 9, w, 9, THEME_MENU_HOLD_FILL);
            }
        }
    } else {
        gfx_clip_box(x, y, x + s, y + s);  // (the square frame of the group tile is out: the corners are round)
        men_draw_cat_tile(it->cat, x, y, s, false, it->kind == IT_BACK, sel);
        gfx_clip_box_off();
    }
    if (!sel) {
        gfx_darken_rect(x, y, s, s, 1);
    }
    r_corners(x, y, s, rr, sel ? THEME_MENU_ROUND_ACCENT : 0);
}

static const char *r_name(int i)
{
    const item_t *it = &men_items[i];
    return it->kind == IT_APP ? it->app->name : it->kind == IT_BACK ? "BACK" : men_gname(it->cat);
}

void men_round_render(int64_t now)
{
    int cx = gfx_w / 2, cy = gfx_h / 2;
    // the sky: rings of colour from the middle out (the group colour mixed in)
    uint8_t ta[3] = {40, 60, 140}, tb[3] = {10, 12, 32};
    if (men_level >= 0) {
        men_cat_colors(men_gstyle(men_level), ta, tb);
    }
    memcpy(s_rta, ta, 3);
    for (int y = 0; y < gfx_h; y += 4) {
        int k = abs(y - R_CY) * 255 / gfx_h;
        uint8_t r = (uint8_t)((ta[0] * (255 - k) / 255) / 4 + 6), g = (uint8_t)((ta[1] * (255 - k) / 255) / 4 + 8),
                b = (uint8_t)((ta[2] * (255 - k) / 255) / 3 + 22);
        // (only the circle shows: the part of the band in it; the band row nearest the middle is the widest)
        const int dy = y + 2 < cy ? cy - (y + 4)
                       : y > cy   ? y - cy
                                  : 0,
                  hw = dy < cx ? (int)sqrtf((float)(cx * cx - dy * dy)) + 2 : 0;
        if (hw > 0) {
            gfx_fill_rect(cx - hw, y, 2 * hw, 4, rgb(r, g, b));
        }
    }
    for (int i = 0; i < N_STARS; i++) {
        int sx = (int)(men_stars[i].x - now * men_stars[i].speed / 60 - men_rpos * men_stars[i].speed * 12) % gfx_w;
        int sy = men_stars[i].y % gfx_h;
        if (sx < 0) {
            sx += gfx_w;
        }
        uint8_t v = men_stars[i].bright;
        gfx_fill_rect(sx, sy, men_stars[i].speed > 2 ? 2 : 1, 1, rgb(v, v, v + 40 > 255 ? 255 : v + 40));
    }
    // the rim: a thin ring in the group colour
    gfx_ring(cx, cy, gfx_w / 2, gfx_w / 2 - 3, rgb(ta[0] / 2 + 40, ta[1] / 2 + 40, ta[2] / 2 + 60));

    // the top, like a watch face: the battery and the WiFi, the time (big), the title (the level; in a group in a group,
    // the path of its parents before it)
    const char *title = men_level < 0 ? "TFN OS" : men_gname(men_level);
    char up[96], line[160];
    men_level_path(men_level, true, up, sizeof up, r_up_fits);
    snprintf(line, sizeof line, "%s%s%s", up, up[0] ? "  >  " : "", title);
    men_draw_wifi(cx + RS(50), RS(24));  // (the round panel: the WiFi signal at the right of the battery)
    men_draw_battery(cx + RS(28), RS(24));
    static int64_t s_rclock_at = -100000;
    static char s_rclock[8] = "--:--";
    static bool s_rsntp;
    if (now - s_rclock_at >= 1000) {
        s_rclock_at = now;
        if (!s_rsntp && wifi_rssi() != 0) {  // (the WiFi is up: the time from the network, once)
            s_rsntp = true;
            clock_sync();
        }
        if (clock_time_ok()) {
            char tz[64];
            alarm_tz_get(tz, sizeof tz);
            tz_rule_t z;
            const int64_t utc = (int64_t)time(NULL);
            const int64_t l = utc + (tz_parse(tz, &z) ? tz_offset(&z, utc, NULL) : 0);
            const int m = (int)(((l / 60) % 1440 + 1440) % 1440);
            snprintf(s_rclock, sizeof s_rclock, "%02d:%02d", m / 60, m % 60);
        }
    }
    ui_text_c(&KF_TITLE, cx, R_TIME_Y, s_rclock, THEME_MENU_ROUND_TEXT);
    ui_text_c(&KF_LABEL, cx, R_TITLE_Y, ui_text_w(&KF_LABEL, line) <= RS(230) ? line : title,
              rgb(ta[0] / 2 + 125, ta[1] / 2 + 125, ta[2] / 2 + 125));

    // the tiles: the far ones first
    int order[N_CAP + G_MAX + 4], n = 0;
    for (int i = 0; i < men_n_items; i++) {
        if (fabsf(i - men_rpos) <= 1.7f) {
            order[n++] = i;
        }
    }
    for (int a = 0; a < n; a++) {
        for (int b = a + 1; b < n; b++) {
            if (fabsf(order[b] - men_rpos) > fabsf(order[a] - men_rpos)) {
                int t = order[a];
                order[a] = order[b];
                order[b] = t;
            }
        }
    }
    for (int k = 0; k < n; k++) {
        int i = order[k], x, y, s;
        r_geom(i, &x, &y, &s);
        r_tile(i, x, y, s, i == men_rsel && fabsf(i - men_rpos) < 0.5f, now);
    }

    // the name of the selected item, under the middle tile (BACK: where it goes)
    if (men_n_items) {
        const char *nm = r_name(men_rsel);
        const kfont_t *f = ui_text_w(&KF_BIG, nm) <= RS(300) ? &KF_BIG : &KF_NAME;
        const int base = R_CY + R_BIG / 2 + RS(16) + f->asc;
        ui_text_c(f, cx, base, nm, THEME_MENU_ROUND_TEXT);
        if (men_items[men_rsel].kind == IT_BACK) {
            char to[24];
            men_back_to(to, sizeof to);
            ui_text_c(&KF_LABEL, cx, base + RS(20), to, THEME_MENU_ROUND_TEXT_SOFT);
        }
    } else {
        ui_text_c(&KF_BIG, cx, R_CY + 8, "NO APPS", THEME_MENU_ROUND_EMPTY);
    }

    // the place in the level: dots on the lower rim (the selected one big)
    if (men_n_items > 12) {
        // (many items: the dots would run together) a thin arc of 100 degrees and one bright dot at the place
        const float span = 100.0f, rr = gfx_w / 2 - 14;
        for (float deg = 90.0f - span / 2; deg <= 90.0f + span / 2; deg += 1.0f) {
            const float rad = deg * 0.0174533f;
            gfx_fill_rect(cx + (int)(fcos(rad) * rr), cy + (int)(fsin(rad) * rr), 2, 2, THEME_MENU_ROUND_RIM);
        }
        const float deg = 90.0f + span / 2 - span * men_rpos / (men_n_items - 1), rad = deg * 0.0174533f;
        ui_circle(cx + (int)(fcos(rad) * rr), cy + (int)(fsin(rad) * rr), 6, THEME_MENU_ROUND_ACCENT, 255);
    } else if (men_n_items > 1) {
        float span = (men_n_items - 1) * 7.0f;
        if (span > 120.0f) {
            span = 120.0f;
        }
        float step = span / (men_n_items - 1);
        for (int i = 0; i < men_n_items; i++) {
            float deg = 90.0f + span / 2 - i * step;  // from the left to the right along the bottom
            float rad = deg * 0.0174533f, rr = gfx_w / 2 - 14;
            int px = cx + (int)(fcos(rad) * rr), py = cy + (int)(fsin(rad) * rr);
            bool on = i == men_rsel;
            gfx_fill_circle(px, py, on ? 5 : 2, on ? THEME_MENU_ROUND_ACCENT : THEME_MENU_ROUND_DOT);
        }
    }
    if (men_level == LV_FAV && men_n_items <= 1) {
        ui_text_c(&KF_LABEL, cx, gfx_h - RS(52), "HOLD AN ICON: FAVORITE", THEME_MENU_ROUND_HELP);
    }
#if BOARD_WHEEL
    else {
        ui_text_c(&KF_LABEL, cx, gfx_h - RS(52), "TURN  PUSH: OPEN  HOLD: BACK", THEME_MENU_ROUND_HELP);
    }
#endif

    if (now - men_pop_at < POP_MS) {
        int w = ui_text_w(&KF_NAME, men_pop_text) + 32, h = 40;
        int px = (gfx_w - w) / 2, py = R_CY - h / 2;
        ui_rbox(px, py, w, h, 14, THEME_MENU_POP_BG, THEME_MENU_ROUND_ACCENT, 2);
        ui_text_c(&KF_NAME, cx, py + h / 2 + 6, men_pop_text, THEME_MENU_ROUND_POP_TEXT);
    }
}
