// The menu of a small portrait panel (BOARD_UI_GRID: the AMOLED-1.8): a status bar and a grid of big icons.
#include "app_menu_priv.h"

#if BOARD_UI_GRID
#include <time.h>
#include "alarm.h"
#include "tz.h"
#include "esp_timer.h"
#include "tfn/theme.h"
// (the menu draws the native frame: one px is BOARD_NATIVE_MM_PX = 0.079 mm, not the px of the frame of the apps)
#define KS_UM_PX 79
#include "../libs/kscroll/kscroll.h"

// THE GRID MENU. The other small boards show one item at a time (menu_spot.c). This panel (368 x 448, 29 x 35 mm) has
// room for more: the menu is a grid made for it, like the home screen of a phone.
//  - THE STATUS BAR at the top (GR_SB px): the time at the left (big), the count of the unread notices on an orange
//    pill, the WiFi signal and the battery (an icon, its percent, a bolt while it charges) at the right.
//  - THE TITLE: the name of the level (big) and its count of apps. In a group: a round BACK at the left; a tap on the
//    left part of the title row goes up one level (a box of 14 x 5 mm).
//  - THE GRID: the items of the level (the groups and the apps; no BACK item: the title has it). A level with 4 items
//    or fewer fills the height with the count of columns that gives the biggest icon (the top level upright: 2
//    columns); a longer level has 3 columns (4 on the side) that scroll with the momentum of a phone (libs/kscroll).
//    A cell is about 9 x 11 mm: a thumb hits one cell. The icons have round corners; the favorite star and the badge
//    of the unread notices show on them.
//  - THE ROOM: the black of the AMOLED with a soft light of the colour of the group at the top.
// Touch: a press makes the icon smaller (a spring); the release on the same icon opens it (the icon grows to the middle
// and the screen goes dark). A drag scrolls. A swipe to the right goes up one level. A hold (HOLD_MS) on an icon opens
// its sheet (HOME 2.0, menu_home.c); a hold on an empty place opens the edit mode.

#define GR_SB        44   // the status bar
#define GR_TITLE     58   // the title row under it
#define GR_LAUNCH_MS 170  // the open of an app: the icon grows, then the app opens
#define GR_SLIDE_MS  220  // a new level: its grid slides in
#define GR_RMAX      40   // the biggest radius of the round corners of an icon
#define GR_SWIPE     70   // px to the right: a swipe goes up one level

typedef struct {
    bool land, fit;  // fit: the rows fill the height (no scroll)
    int top;         // the top of the grid
    int cols, gx0, cw, ch, icon;
    const kfont_t *f;  // the font of the names
} glay_t;

static int16_t s_gv[N_CAP + G_MAX + 4];  // the grid list: an index of ITEMS for each cell
static int s_ngv;
static kscroll_t s_ks;
static float s_scroll_of[G_MAX + 3];  // the scroll of each level
static int64_t s_sw_t;                // a new level: the time (its grid slides in)
static int s_sw_dir;                  // 1: from the right (into a group), -1: from the left (up)
static int s_pi = -1;                 // the item under the finger at the press (ITEMS), -1: none
static bool s_touch, s_moved, s_hold_done, s_back_hot, s_on_back;
static int64_t s_down_t;
static int s_dx, s_dy, s_lx, s_ly;
static int s_ai = -1;         // the item of the press spring
static float s_as = 1, s_av;  // its scale and speed
static int s_li = -1;         // the launch: the item, its app, its start, its box at the start
static const app_t *s_lapp;
static bool s_lgo;
static int64_t s_lt;
static int s_lx0, s_ly0, s_ls;
static char s_clock[16] = "--:--";
static int64_t s_clock_at = -100000;
static bool s_sntp;

// ---------------------------------------------------------------- the geometry
static int gr_slot(int level)
{
    return level < 0 ? 0 : level <= G_MAX ? level + 1 : 0;
}
static void gr_list(void)
{
    s_ngv = 0;
    for (int i = 0; i < men_n_items; i++) {
        if (men_items[i].kind != IT_BACK) {
            s_gv[s_ngv++] = (int16_t)i;
        }
    }
}
static glay_t gr_lay(void)
{
    glay_t L;
    L.land = gfx_w > gfx_h;
    L.top = GR_SB + GR_TITLE;
    L.fit = s_ngv <= 4;
    L.cols = L.land ? 4 : 3;
    L.gx0 = 10;
    L.cw = (gfx_w - 2 * L.gx0) / L.cols;
    if (L.fit) {
        // the rows fill the height: the count of the columns that gives the biggest icon (the name under it)
        L.f = &KF_BIG;
        L.icon = 0;
        for (int c = 1; c <= 4; c++) {
            const int rows = (s_ngv + c - 1) / c > 0 ? (s_ngv + c - 1) / c : 1, cw = (gfx_w - 2 * L.gx0) / c;
            const int ch = (gfx_h - L.top - 6) / rows, by_h = ch - 46, by_w = cw - 36;
            const int icon = by_h < by_w ? by_h : by_w;
            if (icon > L.icon) {
                L.icon = icon;
                L.cols = c;
                L.cw = cw;
                L.ch = ch;
            }
        }
    } else {
        L.f = &KF_NAME;
        L.icon = L.cw - 24;
        L.ch = L.icon + 50;  // (the name: two lines of KF_NAME)
    }
    return L;
}
static int gr_rows(const glay_t *L)
{
    return (s_ngv + L->cols - 1) / L->cols;
}
// the cell of the grid list index k: its top-left (y with the scroll)
static void gr_cell(const glay_t *L, int k, float scroll, int *x, int *y)
{
    *x = L->gx0 + (k % L->cols) * L->cw;
    *y = L->top + 4 + (k / L->cols) * L->ch - (int)lroundf(scroll);

    if (L->fit && k / L->cols == gr_rows(L) - 1 && s_ngv % L->cols) {
        *x += (L->cols - s_ngv % L->cols) * L->cw / 2;  // (a last row that is not full: in the middle)
    }
}
// the box of the icon in a cell
static void gr_icon_box(const glay_t *L, int x, int y, int *ix, int *iy)
{
    *ix = x + (L->cw - L->icon) / 2;
    *iy = y + (L->fit ? (L->ch - L->icon - 40) / 2 : 6);
}
// the grid item (an index of ITEMS) at a point, -1: none
static int gr_hit(const glay_t *L, int px, int py)
{
    if (px < L->gx0 || px >= L->gx0 + L->cols * L->cw || py < L->top) {
        return -1;
    }
    for (int k = 0; k < s_ngv; k++) {
        int x, y;
        gr_cell(L, k, ks_pos(&s_ks), &x, &y);
        if (px >= x && px < x + L->cw && py >= y && py < y + L->ch) {
            return s_gv[k];
        }
    }
    return -1;
}
// the left part of the title row in a group: BACK
static bool gr_back_at(int px, int py)
{
    return men_level >= 0 && py >= GR_SB - 6 && py < GR_SB + GR_TITLE + 4 && px < gfx_w * 45 / 100;
}

// ---------------------------------------------------------------- the time
// the local time of the home zone of the board (alarm.c, the CLOCK app sets it): "HH:MM", "--:--" when it is not known
static void gr_clock(int64_t now)
{
    if (now - s_clock_at < 1000) {
        return;
    }
    s_clock_at = now;
    if (!s_sntp && wifi_rssi() != 0) {  // (the WiFi is up: the time from the network, once)
        s_sntp = true;
        clock_sync();
    }
    if (!clock_time_ok()) {
        snprintf(s_clock, sizeof s_clock, "--:--");
        return;
    }
    char tz[64];
    alarm_tz_get(tz, sizeof tz);
    tz_rule_t z;
    const int64_t utc = (int64_t)time(NULL);
    const int64_t l = utc + (tz_parse(tz, &z) ? tz_offset(&z, utc, NULL) : 0);
    const int m = (int)(((l / 60) % 1440 + 1440) % 1440);
    snprintf(s_clock, sizeof s_clock, "%02d:%02d", m / 60, m % 60);
}

// ---------------------------------------------------------------- the level
void men_gr_level_changed(int from)
{
    s_scroll_of[gr_slot(from)] = s_ks.raw;
    ks_init(&s_ks, (float)gfx_h, 0);
    s_ks.raw = s_scroll_of[gr_slot(men_level)];  // (ks_set_max of the next update puts it in the list)
    s_sw_dir = men_level >= 0 && men_level < men_lay.ng && men_gparent(men_level) == from ? 1 : -1;
    if (from == LV_FAV || men_level == LV_FAV) {
        s_sw_dir = men_level == LV_FAV ? 1 : -1;
    }
    s_sw_t = men_now;
    s_pi = s_ai = -1;
    s_as = 1;
    s_av = 0;
    s_touch = s_back_hot = false;
}
// the console "menusel": the item shows in the grid
void men_gr_reveal(int i)
{
    gr_list();
    glay_t L = gr_lay();
    for (int k = 0; k < s_ngv; k++) {
        if (s_gv[k] == i && !L.fit) {
            float y = (float)((k / L.cols) * L.ch), view = (float)(gfx_h - L.top);
            if (y < s_ks.raw || y + L.ch > s_ks.raw + view) {
                s_ks.raw = y - view / 3 > 0 ? y - view / 3 : 0;
            }
        }
    }
}
void men_gr_enter(int64_t now)
{
    (void)now;
    float keep = s_ks.raw;  // (back from an app: the grid where the user left it)
    ks_init(&s_ks, (float)gfx_h, 0);
    s_ks.raw = keep > 0 ? keep : 0;
    s_pi = s_ai = s_li = -1;
    s_lgo = false;
    s_as = 1;
    s_av = 0;
    s_sw_t = 0;
    s_touch = s_back_hot = false;
    s_clock_at = -100000;
}

// ---------------------------------------------------------------- the actions
static void gr_open(int i, const glay_t *L)
{
    if (i < 0 || i >= men_n_items) {
        return;
    }
    if (men_items[i].kind != IT_APP) {
        men_open_item(i);  // (a group: into it, or its one app)
        return;
    }
    for (int k = 0; k < s_ngv; k++) {
        if (s_gv[k] == i) {
            int x, y;
            gr_cell(L, k, ks_pos(&s_ks), &x, &y);
            gr_icon_box(L, x, y, &s_lx0, &s_ly0);
            s_ls = L->icon;
        }
    }
    s_li = i;
    s_lapp = men_items[i].app;
    s_lgo = false;
    s_lt = men_now;
    sfx_ui(SFX_CLICK);
}
static void gr_up(void)
{
    if (men_level >= 0) {
        sfx_ui(SFX_BACK);
        men_go_level(men_gparent(men_level));
    }
}
void men_gr_input(const input_event_t *ev)
{
    if (s_li >= 0) {
        return;  // (an app opens now)
    }
    glay_t L = gr_lay();
    switch (ev->kind) {
    case IN_DOWN:
        s_touch = true;
        s_moved = s_hold_done = false;
        s_down_t = ev->t_ms;
        s_dx = s_lx = ev->x;
        s_dy = s_ly = ev->y;
        s_pi = -1;
        s_on_back = s_back_hot = gr_back_at(ev->x, ev->y);
        if (!s_on_back && !ks_down(&s_ks, ev->y, ev->t_ms)) {
            s_pi = gr_hit(&L, ev->x, ev->y);
        }
        if (s_pi >= 0) {
            s_ai = s_pi;
        }
        break;
    case IN_MOVE:
        s_lx = ev->x;
        s_ly = ev->y;
        if (abs(ev->x - s_dx) > 10 || abs(ev->y - s_dy) > 10) {
            s_moved = true;
        }
        if (s_on_back) {
            s_back_hot = gr_back_at(ev->x, ev->y);
            break;
        }
        if (ks_move(&s_ks, ev->y, ev->t_ms)) {
            input_tap_cancel();
            s_pi = -1;
        }
        if (s_pi >= 0 && gr_hit(&L, ev->x, ev->y) != s_pi) {
            s_pi = -1;  // (out of the cell)
        }
        break;
    case IN_UP: {
        s_touch = false;
        if (s_on_back) {
            if (s_back_hot && !s_hold_done) {
                gr_up();
            }
            s_on_back = s_back_hot = false;
            break;
        }
        const bool moved = ks_up(&s_ks, ev->t_ms);
        const int i = s_pi, dx = s_lx - s_dx, dy = s_ly - s_dy;
        s_pi = -1;
        if (!s_hold_done && dx > GR_SWIPE && abs(dy) < dx / 2) {
            gr_up();  // (a swipe to the right: up one level)
        } else if (!moved && !s_hold_done && i >= 0 && gr_hit(&L, ev->x, ev->y) == i) {
            gr_open(i, &L);
        }
        break;
    }
    default:
        break;  // (IN_TAP, IN_LONG: the menu acts at the release, and its hold has its own time)
    }
}
void men_gr_update(int64_t now, float dt)
{
    gr_list();
    gr_clock(now);
    glay_t L = gr_lay();
    const float view = (float)(gfx_h - L.top);
    ks_set_view(&s_ks, view);
    ks_set_max(&s_ks, L.fit ? 0 : (float)(gr_rows(&L) * L.ch + 16) - view);
    ks_update(&s_ks, now);
    // the hold: the sheet of an icon, the edit mode on an empty place (only while a finger is on the panel: a touch
    // whose IN_UP went to another layer, such as the notices, is not a hold)
    int16_t px[INPUT_MAX_POINTS], py[INPUT_MAX_POINTS];
    if (s_touch && input_points(px, py, INPUT_MAX_POINTS) == 0 && now - s_down_t >= HOLD_MS) {
        s_touch = false;
        s_pi = -1;
    }
    if (s_touch && !s_on_back && !s_moved && !s_hold_done && !ks_dragging(&s_ks) && now - s_down_t >= HOLD_MS) {
        s_hold_done = true;
        if (s_pi >= 0) {
            int i = s_pi;
            s_pi = -1;
            if (men_home_hold_item(i, men_level)) {
                s_ai = -1;
            }
        } else {
            men_home_hold_bg(s_down_t, s_dx, s_dy);
        }
        if (men_home_active()) {
            return;
        }
    }
    // the press: a spring to a smaller icon while the finger is on it (smaller still while a hold fills)
    float goal = 1.0f;
    if (s_ai >= 0 && s_ai == s_pi && s_touch && !s_hold_done) {
        float hold = (float)(now - s_down_t - HOLD_SHOW_MS) / (HOLD_MS - HOLD_SHOW_MS);
        goal = 0.9f - 0.06f * (hold < 0 ? 0 : hold > 1 ? 1 : hold);
    }
    if (s_ai >= 0) {
        float h = dt / 4;
        for (int k = 0; k < 4; k++) {
            float acc = -900.0f * (s_as - goal) - 28.0f * s_av;
            s_av += acc * h;
            s_as += s_av * h;
        }
        if (goal >= 1 && fabsf(s_as - 1) < 0.002f && fabsf(s_av) < 0.01f) {
            s_as = 1;
            s_av = 0;
            s_ai = -1;
        }
    }
    // the launch: the app opens at its end (main.c changes the app in the next frame)
    if (s_li >= 0 && !s_lgo && now - s_lt >= GR_LAUNCH_MS) {
        s_lgo = true;
        app_open(s_lapp);
    }
}

// ---------------------------------------------------------------- the room
// The black of the AMOLED and a soft light of the colour of the group from the top: a gradient of the rows to black at
// 62 % of the height. Each row is a pair of near colours in turn (a dither: no bands in the dark light), so a frame
// writes the room with no read of a picture (the copy of a 330 KB picture in PSRAM took 15 to 21 ms a frame).
#define GR_ROWS (BOARD_NATIVE_W > BOARD_NATIVE_H ? BOARD_NATIVE_W : BOARD_NATIVE_H)
static uint32_t s_rowv[GR_ROWS];  // the two colours of each row (LCD byte order): pixel 0 in the low half
static int s_room_key = -1;
static int gr_look(void)
{
    return men_level == LV_FAV ? ST_STAR : men_level < 0 ? ST_TOP : men_gstyle(men_level);
}
static int gr_q(float v)
{
    return v < 0 ? 0 : v > 255 ? 255 : (int)v;
}
static void gr_room_make(void)
{
    const int look = gr_look(), key = (look << 22) | (gfx_w << 11) | gfx_h;
    if (key == s_room_key) {
        return;
    }
    s_room_key = key;
    uint8_t a[3], b[3];
    men_cat_colors(look, a, b);
    const float end = gfx_h * 0.62f;
    for (int y = 0; y < gfx_h && y < GR_ROWS; y++) {
        const float u = y / end, f = u < 1 ? (1 - u) * (1 - u) * 0.40f : 0;
        const float r = a[0] * f, g = a[1] * f, bl = (a[2] * 0.8f + b[2] * 0.2f) * f;
        // the two colours: half a step of RGB565 under and over the colour (the mean is the colour)
        const uint16_t c0 = nat(gr_q(r - 4), gr_q(g - 2), gr_q(bl - 4));
        const uint16_t c1 = nat(gr_q(r + 4), gr_q(g + 2), gr_q(bl + 4));
        s_rowv[y] = f < 0.004f ? 0 : (uint32_t)lcd(c0) | (uint32_t)lcd(c1) << 16;
    }
}
// the room in the rows y0..y1 and the columns x0..x1 (the dither of a pixel: (x + y) & 1)
static void gr_bg(int x0, int y0, int x1, int y1)
{
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > gfx_w ? gfx_w : x1;
    y1 = y1 > gfx_h ? gfx_h : y1;
    for (int y = y0; y < y1 && x1 > x0; y++) {
        const uint32_t v = s_rowv[y];
        uint16_t *p = &g_fb[y * gfx_w];
        if (!v) {
            memset(p + x0, 0, (size_t)(x1 - x0) * 2);
            continue;
        }
        const uint16_t c[2] = {(uint16_t)v, (uint16_t)(v >> 16)};
        int x = x0;
        if (x & 1) {
            p[x] = c[(x + y) & 1];
            x++;
        }
        uint32_t *q = (uint32_t *)(p + x);
        const uint32_t w = (y & 1) ? (v >> 16) | (v << 16) : v;
        for (; x + 1 < x1; x += 2) {
            *q++ = w;
        }
        if (x < x1) {
            p[x] = c[(x + y) & 1];
        }
    }
}

// ---------------------------------------------------------------- the icons
// THE ICON CACHE. Some icons of the apps take 2 to 4 ms to draw (pixel art, scenes): 12 of them took 32 ms a frame. So
// each icon draws once into a picture in PSRAM (its size s x s, with no round corners) and each frame copies it. A
// table of the cover of the round corners (for the size now) makes the soft edge in the copy. The pictures are still:
// an icon draws again when its picture is GR_IC_AGE ms old and the grid does not move (one icon in a frame).
// The first draw of an icon can read its file from the SD card (20-60 ms each). With 30 pictures, a scroll of GAMES
// (86 icons) read many files in one frame: frames of 450-880 ms. So: the cache holds a full group (GR_NC), an icon
// draws in an own buffer (s_scr, also at the edges of the frame), a frame that moves makes new pictures only for
// GR_NEW_US (the others show an empty tile until a later frame), and a still menu makes the pictures of the group
// that it does not have yet, one in each frame (gr_prefetch).
#define GR_NC     96     // the pictures of the icons that stay (92 x 92: 17 KB each)
#define GR_IC_AGE 4000   // ms: then the picture of an icon draws again (the small motions of some icons)
#define GR_NEW_US 12000  // us: the time for new pictures in a frame that moves
typedef struct {
    uintptr_t key;  // the app (IT_APP), or 1 | the group << 1 | its count << 8 (IT_CAT)
    int16_t s;
    uint32_t use;
    int64_t made;
    uint16_t *px;
} gicon_t;
static gicon_t s_ic[GR_NC];
static uint32_t s_ic_clock;
static uint8_t s_cov[GR_RMAX * GR_RMAX];  // the cover of the top left corner (0..255) for the size s_cov_s
static int s_cov_s = -1;
static bool s_ic_refresh;  // this frame can draw one old icon again
static uint16_t *s_cbuf;   // the corners under an icon (gr_under: the icons that do not use the cache)
static uint16_t *s_scr;    // gfx_w x s_scr_s: the icon draws here, then goes into its picture
static int s_scr_s;
static bool s_ic_limit;  // this frame moves: new pictures only for GR_NEW_US
static int64_t s_fr_t0;  // the start of the frame (esp_timer us)
static int s_fr_new;     // the new pictures of this frame
static bool s_ic_wait;   // gr_ic: no picture because of the time limit (an empty tile now)
static bool s_ic_pend;   // the last frame showed an empty tile: the next frame draws again
void men_gr_rooms_free(void)
{
    for (int k = 0; k < GR_NC; k++) {
        free(s_ic[k].px);
        s_ic[k].px = NULL;
        s_ic[k].key = 0;
    }
    free(s_cbuf);
    s_cbuf = NULL;
    free(s_scr);
    s_scr = NULL;
    s_scr_s = 0;
    s_room_key = -1;
}
static int gr_rad(int s)
{
    return s * 23 / 100 > GR_RMAX ? GR_RMAX : s * 23 / 100;
}
static uintptr_t gr_key(const item_t *it)
{
    if (it->kind == IT_APP) {
        return (uintptr_t)it->app;
    }
    return 1u | (uintptr_t)it->cat << 1 | (uintptr_t)men_cat_count[it->cat] << 8;
}
static void gr_cov(int s)
{
    if (s == s_cov_s) {
        return;
    }
    s_cov_s = s;
    const int r = gr_rad(s);
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            s_cov[j * GR_RMAX + i] = (uint8_t)men_rr_cover(i, j, 0, 0, s, s, r);
        }
    }
}
// The round corners of an icon that does not use the cache (a press, the launch, an icon at the edge of the frame):
// gr_under() keeps the 4 corner boxes (r x r) of the frame before the icon draws, gr_round() puts them back out of the
// round shape, with a soft edge (the same way as the Tab5 menu).
static void gr_under(int x, int y, int s, int r)
{
    if (!s_cbuf) {
        s_cbuf = heap_caps_malloc(4 * GR_RMAX * GR_RMAX * 2, MALLOC_CAP_SPIRAM);
    }
    if (!s_cbuf || r > GR_RMAX) {
        return;
    }
    for (int c = 0; c < 4; c++) {
        for (int j = 0; j < r; j++) {
            for (int i = 0; i < r; i++) {
                int px = (c & 1) ? x + s - 1 - i : x + i, py = (c & 2) ? y + s - 1 - j : y + j;
                bool in = (unsigned)px < (unsigned)gfx_w && (unsigned)py < (unsigned)gfx_h;
                s_cbuf[(c * GR_RMAX + j) * GR_RMAX + i] = in ? g_fb[py * gfx_w + px] : 0;
            }
        }
    }
}
static void gr_round(int x, int y, int s, int r, int cy0, int cy1)
{
    if (!s_cbuf || r > GR_RMAX) {
        return;
    }
    for (int j = 0; j < r; j++) {
        for (int i = 0; i < r; i++) {
            int cov = men_rr_cover(x + i, y + j, x, y, s, s, r);
            if (cov >= 255) {
                continue;
            }
            for (int c = 0; c < 4; c++) {
                int px = (c & 1) ? x + s - 1 - i : x + i, py = (c & 2) ? y + s - 1 - j : y + j;
                if (px < 0 || px >= gfx_w || py < cy0 || py >= cy1) {
                    continue;
                }
                uint16_t *q = &g_fb[py * gfx_w + px];
                uint16_t to = lcd(s_cbuf[(c * GR_RMAX + j) * GR_RMAX + i]);
                *q = lcd(cov <= 0 ? to : nmix(to, lcd(*q), cov));
            }
        }
    }
}
// a box toward black (a = 0..255)
static void gr_dim(int x0, int y0, int x1, int y1, int a)
{
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > gfx_w ? gfx_w : x1;
    y1 = y1 > gfx_h ? gfx_h : y1;
    for (int y = y0; y < y1 && a > 0; y++) {
        uint16_t *p = &g_fb[y * gfx_w + x0];
        for (int x = x0; x < x1; x++, p++) {
            *p = lcd(nmix(lcd(*p), 0, a));
        }
    }
}
static void gr_draw_raw(const item_t *it, int ix, int iy, int s)
{
    if (it->kind == IT_APP) {
        it->app->draw_icon(ix, iy, s);
    } else {
        men_group_tile(it->cat, ix, iy, s, false, true);
    }
}
// the picture of item i at (ix, iy), size s, with round corners, drawn now; the rows cy0..cy1 (the title covers the
// rest)
static void gr_tile(int i, int ix, int iy, int s, int cy0, int cy1, bool dark)
{
    const int r = gr_rad(s);
    int c0 = iy > cy0 ? iy : cy0, c1 = iy + s < cy1 ? iy + s : cy1;
    if (c1 <= c0) {
        return;
    }
    gr_under(ix, iy, s, r);
    gfx_clip_box(ix, c0, ix + s, c1);
    gr_draw_raw(&men_items[i], ix, iy, s);
    gfx_clip_box_off();
    gfx_clip_reset();
    gr_round(ix, iy, s, r, c0, c1);
    if (dark) {
        gr_dim(ix, c0, ix + s, c1, 80);
    }
}
// a free place in the cache for a picture of size s (the one used last long ago), -1: no memory
static int gr_ic_slot(int s)
{
    int k = 0;
    for (int n = 0; n < GR_NC; n++) {
        if (!s_ic[n].px || (s_ic[k].px && s_ic[n].use < s_ic[k].use)) {
            k = n;
        }
    }
    if (s_ic[k].px && s_ic[k].s != s) {
        free(s_ic[k].px);
        s_ic[k].px = NULL;
    }
    if (!s_ic[k].px) {
        s_ic[k].px = heap_caps_malloc((size_t)s * s * 2, MALLOC_CAP_SPIRAM);
    }
    return s_ic[k].px ? k : -1;
}
// Draw item it (size s) into the picture of slot k. The icon draws in s_scr at the frame rows ry .. ry + s (g_fb
// points there for the time of the draw: the frame does not change), on the room of those rows.
static bool gr_ic_make(int k, const item_t *it, int s, int ry)
{
    if (s > gfx_w || s > gfx_h) {
        return false;
    }
    if (!s_scr || s_scr_s < s) {
        free(s_scr);
        s_scr = heap_caps_malloc((size_t)gfx_w * s * 2, MALLOC_CAP_SPIRAM);
        s_scr_s = s_scr ? s : 0;
    }
    if (!s_scr) {
        return false;
    }
    ry = ry < 0 ? 0 : ry > gfx_h - s ? gfx_h - s : ry;
    uint16_t *const fb = g_fb;
    g_fb = s_scr - (ptrdiff_t)ry * gfx_w;
    (void)tfn_icon_missed();
    gr_bg(0, ry, s, ry + s);  // (an icon with clear parts: the room under it)
    gfx_clip_box(0, ry, s, ry + s);
    gr_draw_raw(it, 0, ry, s);
    gfx_clip_box_off();
    gfx_clip_reset();
    g_fb = fb;
    if (tfn_icon_missed()) {  // (its file is not in memory yet: a task reads it; no picture now)
        s_ic_wait = true;
        return false;
    }
    for (int j = 0; j < s; j++) {
        memcpy(&s_ic[k].px[j * s], &s_scr[j * gfx_w], (size_t)s * 2);
    }
    return true;
}
// the slot of the picture of item it at size s, -1: none
static int gr_ic_find(const item_t *it, int s)
{
    const uintptr_t key = gr_key(it);
    for (int n = 0; n < GR_NC; n++) {
        if (s_ic[n].px && s_ic[n].key == key && s_ic[n].s == s) {
            return n;
        }
    }
    return -1;
}
// a new picture of item it in a free slot (or in slot k: an old picture again); NULL: no memory
static const uint16_t *gr_ic_new(const item_t *it, int k, int s, int ry)
{
    if (k >= 0) {  // (an old picture draws again; with no file in memory it stays as it is)
        if (!gr_ic_make(k, it, s, ry)) {
            s_ic_wait = false;
            s_ic[k].made = men_now;
            s_ic[k].use = ++s_ic_clock;
            return s_ic[k].px;
        }
    } else if ((k = gr_ic_slot(s)) < 0) {
        return NULL;
    } else if (!gr_ic_make(k, it, s, ry)) {
        s_ic[k].key = 0;
        return NULL;
    }
    s_ic[k].key = gr_key(it);
    s_ic[k].s = (int16_t)s;
    s_ic[k].made = men_now;
    s_ic[k].use = ++s_ic_clock;
    s_fr_new++;
    return s_ic[k].px;
}
// the picture of an icon in the cache (it draws now when it has none, or an old one when the frame lets it); NULL: no
// picture (no memory; or no time in this frame, or its file is not in memory yet: then s_ic_wait)
static const uint16_t *gr_ic(int i, int iy, int s)
{
    const item_t *it = &men_items[i];
    int k = gr_ic_find(it, s);
    const bool redo = k >= 0 && s_ic_refresh && men_now - s_ic[k].made >= GR_IC_AGE;
    if (k >= 0 && !redo) {
        s_ic[k].use = ++s_ic_clock;
        return s_ic[k].px;
    }
    if (k < 0 && s_ic_limit && s_fr_new > 0 && esp_timer_get_time() - s_fr_t0 > GR_NEW_US) {
        s_ic_wait = true;
        return NULL;
    }
    s_ic_refresh = s_ic_refresh && !redo;
    return gr_ic_new(it, k, s, iy);
}
// A still menu: the picture of one icon of the group that has none (the order of the group; not more than the cache
// keeps). An icon with no file in memory asks for it (up to 4 in a frame: the task reads them in turn). The frame does
// not change.
static void gr_prefetch(const glay_t *L)
{
    const int n = s_ngv < GR_NC - 6 ? s_ngv : GR_NC - 6;
    int asks = 0;
    for (int k = 0; k < n && asks < 4; k++) {
        const item_t *it = &men_items[s_gv[k]];
        if (gr_ic_find(it, L->icon) >= 0) {
            continue;
        }
        s_ic_wait = false;
        if (gr_ic_new(it, -1, L->icon, L->top + 6) || !s_ic_wait) {
            return;
        }
        asks++;
    }
}
// a picture of the cache at (ix, iy) with its round corners, in the rows cy0..cy1 (the columns: in the frame)
static void gr_blit(const uint16_t *src, int ix, int iy, int s, int cy0, int cy1)
{
    gr_cov(s);
    const int r = gr_rad(s), i0 = ix < 0 ? -ix : 0, i1 = ix + s > gfx_w ? gfx_w - ix : s;
    for (int j = 0; j < s && i1 > i0; j++) {
        const int y = iy + j;
        if (y < cy0 || y >= cy1 || y < 0 || y >= gfx_h) {
            continue;
        }
        const uint16_t *sr = src + j * s;
        uint16_t *d = &g_fb[y * gfx_w + ix];
        const int cj = j < r ? j : j >= s - r ? s - 1 - j : -1;
        if (cj < 0) {
            memcpy(d + i0, sr + i0, (size_t)(i1 - i0) * 2);
            continue;
        }
        for (int i = i0; i < i1; i++) {
            const int ci = i < r ? i : i >= s - r ? s - 1 - i : -1;
            const int cv = ci < 0 ? 255 : s_cov[cj * GR_RMAX + ci];
            if (cv >= 255) {
                d[i] = sr[i];
            } else if (cv > 0) {
                d[i] = lcd(nmix(lcd(d[i]), lcd(sr[i]), cv));
            }
        }
    }
}
// the marks on an app icon: the favorite star, the badge of the unread notices (no NEW mark: the new apps come
// first in the list, and with about 100 new apps a mark on each one tells nothing)
static void gr_marks(const app_t *a, int ix, int iy, int s)
{
    if (men_fav_index(a) >= 0) {
        men_star(ix + s - 9, iy + s - 9, 12, THEME_MENU_FAV_EDGE);
        men_star(ix + s - 9, iy + s - 9, 9, THEME_MENU_FAV);
    }
    men_badge(a, ix, iy, s);
}
// The name of an item in the width w: one line; else two lines (the split at the space that gives the narrowest wider
// line); else a smaller font; else cut.
static void gr_label(const kfont_t *f, int cx, int base, const char *name, int w, uint16_t col)
{
    char a[32];
    snprintf(a, sizeof a, "%s", name);
    const int n = (int)strlen(a), lh = f->asc + 4;
    for (int q = 0; q < 2; q++, f = &KF_LABEL) {
        if (ui_text_w(f, a) <= w) {
            ui_text_c(f, cx, base, a, col);
            return;
        }
        int best = -1, bw = 1 << 20;
        for (int i = 1; i < n - 1; i++) {
            if (a[i] != ' ') {
                continue;
            }
            a[i] = 0;
            int m = ui_text_w(f, a), m2 = ui_text_w(f, a + i + 1);
            a[i] = ' ';
            m = m2 > m ? m2 : m;
            if (m < bw) {
                bw = m;
                best = i;
            }
        }
        if (best >= 0 && bw <= w) {
            a[best] = 0;
            ui_text_c(f, cx, base, a, col);
            ui_text_c(f, cx, base + lh, a + best + 1, col);
            return;
        }
    }
    int cut = n;
    while (cut > 1 && ui_text_w(&KF_LABEL, a) > w) {
        a[--cut] = 0;
    }
    ui_text_c(&KF_LABEL, cx, base, a, col);
}
// a cell of the grid: the icon (sc: the scale of the press), its marks, the hold bar, the name
static void gr_item(const glay_t *L, int i, int x, int y, int cy0, int64_t now)
{
    const item_t *it = &men_items[i];
    int ix, iy;
    gr_icon_box(L, x, y, &ix, &iy);
    const float sc = i == s_ai ? s_as : 1.0f;
    const int s = (int)lroundf(L->icon * sc), sx = ix + (L->icon - s) / 2, sy = iy + (L->icon - s) / 2;
    s_ic_wait = false;
    const uint16_t *pic = s == L->icon && i != s_pi ? gr_ic(i, sy, s) : NULL;
    if (pic) {
        gr_blit(pic, sx, sy, s, cy0, gfx_h);
    } else if (s_ic_wait) {  // (no time for its picture in this frame: an empty tile, the picture comes later)
        gr_bg(sx, sy > cy0 ? sy : cy0, sx + s, sy + s);
        ui_rrect_a(sx, sy, s, s, gr_rad(s), THEME_MENU_GR_SOFT, 40);
        s_ic_pend = true;
    } else {
        gr_bg(sx, sy > cy0 ? sy : cy0, sx + s, sy + s);  // (the room under it: gr_bg_grid skips the cached icons)
        gr_tile(i, sx, sy, s, cy0, gfx_h, i == s_pi);
    }
    if (sy + s <= cy0) {
        return;
    }
    if (it->kind == IT_APP) {
        gr_marks(it->app, sx, sy, s);
    }
    if (s_pi == i && s_touch && !s_hold_done && !s_moved && now - s_down_t >= HOLD_SHOW_MS) {
        int w = (int)((now - s_down_t - HOLD_SHOW_MS) * L->icon / (HOLD_MS - HOLD_SHOW_MS));
        w = w > L->icon ? L->icon : w;
        ui_rrect(ix, iy + L->icon + 4, L->icon, 6, 3, THEME_MENU_HOLD_TRACK);
        if (w > 6) {
            ui_rrect(ix, iy + L->icon + 4, w, 6, 3, THEME_MENU_HOLD_FILL);
        }
    }
    gr_label(L->f, x + L->cw / 2, iy + L->icon + 8 + L->f->asc, men_item_name(it), L->cw - 6, THEME_MENU_GR_TEXT);
}

// ---------------------------------------------------------------- the status bar and the title
// the battery: a round icon with its level and a bolt while it charges, the percent at its left; returns the left edge
static int gr_battery(int xr, int cy)
{
    bool chg = false;
    const int pct = board_battery(&chg, NULL);
    if (pct < 0) {
        return xr;
    }
    const uint16_t c = pct > 20 ? THEME_MENU_GR_ICON : pct > 10 ? THEME_MENU_BAT_WARN : THEME_MENU_BAT_LOW;
    const int bw = 32, bh = 17, bx = xr - bw - 3, by = cy - bh / 2;
    ui_rrect(bx, by, bw, bh, 5, c);
    ui_rrect(bx + 2, by + 2, bw - 4, bh - 4, 3, THEME_MENU_GR_BG);
    ui_rrect(bx + bw + 1, cy - 3, 3, 7, 1, c);
    const int w = ((bw - 8) * pct + 50) / 100;
    if (w > 0) {
        ui_rrect(bx + 4, by + 4, w, bh - 8, 2, chg ? THEME_MENU_BAT_OK : c);
    }
    if (chg) {
        const float x0 = bx + bw / 2.0f, y0 = (float)by;
        float p[12] = {x0 + 2.5f, y0 + 1.0f,  x0 - 4.5f, y0 + 9.5f, x0 - 0.5f, y0 + 9.5f,
                       x0 - 2.5f, y0 + 16.0f, x0 + 4.5f, y0 + 7.5f, x0 + 0.5f, y0 + 7.5f};
        men_fill_poly(p, 6, THEME_MENU_BAT_BOLT);
    }
    char t[16];
    snprintf(t, sizeof t, "%d%%", pct);
    const int tw = ui_text_w(&KF_TEXT, t);
    ui_text(&KF_TEXT, bx - 8 - tw, cy + KF_TEXT.asc / 2 - 1, t, THEME_MENU_GR_SOFT);
    return bx - 8 - tw;
}
// the WiFi signal: 4 round bars (none with no network); returns the left edge
static int gr_wifi(int xr, int cy)
{
    const int rssi = wifi_rssi();
    if (!rssi) {
        return xr;
    }
    const int bars = rssi > -55 ? 4 : rssi > -65 ? 3 : rssi > -75 ? 2 : 1, x = xr - 25;
    for (int b = 0; b < 4; b++) {
        const int h = 6 + b * 4;
        ui_rrect(x + b * 7, cy + 8 - h, 5, h, 2, b < bars ? THEME_MENU_GR_ICON : THEME_MENU_GR_DIM);
    }
    return x;
}
static void gr_status(void)
{
    const int cy = GR_SB / 2;
    ui_text(&KF_BIG, 18, cy + KF_BIG.asc / 2 - 2, s_clock, THEME_MENU_GR_TEXT);
    const int n = notify_unread(NULL);
    if (n > 0) {
        char t[16];
        snprintf(t, sizeof t, n > 99 ? "99+" : "%d", n);
        const int tw = ui_text_w(&KF_NAME, t), w = tw + 18 > 28 ? tw + 18 : 28;
        const int x = 18 + ui_text_w(&KF_BIG, s_clock) + 10;
        ui_rrect(x, cy - 13, w, 26, 13, THEME_MENU_GR_NOTE);
        ui_text_c(&KF_NAME, x + w / 2, cy + KF_NAME.asc / 2 - 1, t, THEME_MENU_GR_NOTE_TXT);
    }
    const int l = gr_battery(gfx_w - 16, cy);
    gr_wifi(l - 12, cy);
}
static void gr_title(int off)
{
    char title[24], cnt[24];
    snprintf(title, sizeof title, "%s",
             men_level == LV_FAV ? "FAVORITES"
             : men_level < 0     ? "TFN OS"
                                 : men_gname(men_level));
    int napps = 0;
    for (int k = 0; k < s_ngv; k++) {
        napps += men_items[s_gv[k]].kind == IT_APP ? 1 : men_cat_count[men_items[s_gv[k]].cat];
    }
    snprintf(cnt, sizeof cnt, napps == 1 ? "%d APP" : "%d APPS", napps);
    const int cy = GR_SB + GR_TITLE / 2;
    int x = 20 + off;
    if (men_level >= 0) {
        // BACK: a soft round plate with an arrow
        const int r = 19, bx = 16 + r + off;
        ui_circle(bx, cy, r, THEME_MENU_GR_BACK, s_back_hot ? 70 : 34);
        float p[6] = {bx - 7.0f, (float)cy, bx + 4.0f, cy - 10.0f, bx + 4.0f, cy + 10.0f};
        men_fill_poly(p, 3, THEME_MENU_GR_ICON);
        x = bx + r + 12;
    }
    const int cw = ui_text_w(&KF_NAME, cnt), right = gfx_w - 20;
    static const kfont_t *const TF[4] = {&KF_TITLE, &KF_BIG, &KF_TEXT, &KF_NAME};
    const kfont_t *f = ui_text_fit(TF, 4, title, right - x - cw - 14);
    ui_text(f, x, cy + f->asc / 2 - 1, title, THEME_MENU_GR_TEXT);
    ui_text(&KF_NAME, right - cw, cy + KF_NAME.asc / 2 - 1, cnt, THEME_MENU_GR_SOFT);
}

// ---------------------------------------------------------------- the frame
static void gr_launch(int64_t now)
{
    if (s_li < 0 || !s_lapp) {
        return;
    }
    float k = (float)(now - s_lt) / GR_LAUNCH_MS;
    k = k > 1 ? 1 : k;
    const float e = 1 - (1 - k) * (1 - k);
    gr_dim(0, 0, gfx_w, gfx_h, (int)(200 * e));
    const int s = (int)lroundf(s_ls * (1 + 1.0f * e));
    float cx = s_lx0 + s_ls / 2.0f, cy = s_ly0 + s_ls / 2.0f;
    cx += (gfx_w / 2.0f - cx) * e * 0.7f;
    cy += (gfx_h / 2.0f - cy) * e * 0.7f;
    gr_tile(s_li, (int)lroundf(cx - s / 2.0f), (int)lroundf(cy - s / 2.0f), s, 0, gfx_h, false);
}
// The room under the grid, but not under the icons that the cache copies (they cover it: the frame writes about 200 KB
// less of PSRAM). A row in the middle part of an icon skips all of the icon; a row of its round corners skips only
// the part between the corners. (off: the slide of a new level)
static void gr_bg_grid(const glay_t *L, int off)
{
    const float scroll = ks_pos(&s_ks);
    const int r = gr_rad(L->icon), s = L->icon;
    int xs[8], ys = 0, nx = 0, row = -1;
    for (int y = L->top; y < gfx_h; y++) {
        // the icons of the grid row at y (the rows have the same icon top)
        const int gr = (int)((y - L->top - 4 + scroll) / L->ch);
        if (gr != row) {
            row = gr;
            nx = 0;
            for (int c = 0; c < L->cols && gr >= 0; c++) {
                const int k = gr * L->cols + c;
                if (k >= s_ngv || s_gv[k] == s_pi || s_gv[k] == s_li || s_gv[k] == s_ai) {
                    continue;
                }
                int x, cy, ix, iy;
                gr_cell(L, k, scroll, &x, &cy);
                gr_icon_box(L, x + off, cy, &ix, &iy);
                if (ix >= 0 && iy >= 0 && ix + s <= gfx_w && iy + s <= gfx_h && nx < 8) {
                    xs[nx++] = ix;
                    ys = iy;
                }
            }
        }
        const int j = y - ys;
        if (!nx || j < 0 || j >= s) {
            gr_bg(0, y, gfx_w, y + 1);
            continue;
        }
        const int cut = j < r || j >= s - r ? r : 0;  // (a row of the corners: the bg under the corners)
        int x0 = 0;
        for (int n = 0; n < nx; n++) {
            gr_bg(x0, y, xs[n] + cut, y + 1);
            x0 = xs[n] + s - cut;
        }
        gr_bg(x0, y, gfx_w, y + 1);
    }
}
static void gr_grid(const glay_t *L, int off, int64_t now)
{
    const float scroll = ks_pos(&s_ks);
    for (int k = 0; k < s_ngv; k++) {
        int x, y;
        gr_cell(L, k, scroll, &x, &y);
        x += off;
        if (y + L->ch < L->top - 4 || y > gfx_h || s_gv[k] == s_li) {
            continue;
        }
        gr_item(L, s_gv[k], x, y, L->top, now);
    }
    // the names that the scroll moves under the title: cut at the top of the grid (the room again above it, and a
    // soft edge under it)
    gr_bg(0, 0, gfx_w, L->top);
    for (int j = 0; j < 10; j++) {
        const int y = L->top + j, a = 255 - j * 25;
        const uint32_t v = s_rowv[y];
        uint16_t *p = &g_fb[y * gfx_w];
        for (int xx = 0; xx < gfx_w; xx++) {
            p[xx] = lcd(nmix(lcd(p[xx]), lcd((uint16_t)(((xx + y) & 1) ? v >> 16 : v)), a));
        }
    }
}

// ---------------------------------------------------------------- the still frames
// When nothing moves, the frame buffer (gfx_swap: two buffers in turn) has the frame before the last one, and that
// frame is the same: the menu then draws nothing (an AMOLED board on its battery: no work for a still menu). s_rec
// keeps the value of the state of each buffer (gr_sig). A full frame comes each GR_FULL frames (the pictures of the
// icons draw again when they are old; the layers of main.c can draw on the frame), after menu_redraw_all() and
// after a gap (the sheet of HOME 2.0, the settings panel or an app drew on the buffers).
#define GR_FULL 30
typedef struct {
    const uint16_t *fb;
    uint32_t sig;
} grec_t;
static grec_t s_rec[2];
static int s_full_n;
static int64_t s_last_frame = -100000;
void men_gr_redraw_all(void)
{
    memset(s_rec, 0, sizeof s_rec);
}
static uint32_t gr_mix(uint32_t h, uint32_t v)
{
    for (int k = 0; k < 4; k++) {
        h = (h ^ ((v >> (k * 8)) & 0xFF)) * 16777619u;
    }
    return h;
}
static uint32_t gr_fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
// the value of all that the frame shows (0: the frame moves: draw it)
static uint32_t gr_sig(int off, int64_t now)
{
    if (off || s_li >= 0 || s_ai >= 0 || s_pi >= 0 || s_back_hot || s_ic_pend || ks_moving(&s_ks) ||
        ks_dragging(&s_ks) || now - men_pop_at < POP_MS) {
        return 0;
    }
    int at, size, alpha;
    if (ks_bar(&s_ks, 100, &at, &size, &alpha)) {
        return 0;
    }
    bool chg = false;
    const int pct = board_battery(&chg, NULL);
    uint32_t h = 2166136261u;
    h = gr_mix(h, (uint32_t)(gfx_w << 16 | gfx_h));
    h = gr_mix(h, (uint32_t)men_level);
    h = gr_mix(h, gr_fbits(ks_pos(&s_ks)));
    h = gr_mix(h, (uint32_t)pct << 1 | chg);
    h = gr_mix(h, (uint32_t)wifi_rssi() / 10u);
    h = gr_mix(h, (uint32_t)notify_unread(NULL));
    for (const char *c = s_clock; *c; c++) {
        h = gr_mix(h, (uint8_t)*c);
    }
    h = gr_mix(h, (uint32_t)s_ngv);
    for (int k = 0; k < s_ngv; k++) {
        const item_t *it = &men_items[s_gv[k]];
        h = gr_mix(h, (uint32_t)gr_key(it));
        if (it->kind == IT_APP) {
            h = gr_mix(h, (uint32_t)(notify_unread(it->app->name) << 1 | (men_fav_index(it->app) >= 0)));
        }
    }
    return h | 1;
}
// true: this buffer has the frame now (no drawing)
static bool gr_still(int off, int64_t now)
{
    const bool gap = now - s_last_frame > 150;
    s_last_frame = now;
    if (gap || ++s_full_n >= GR_FULL) {
        s_full_n = 0;
        men_gr_redraw_all();
    }
    const uint32_t sig = gr_sig(off, now);
    int slot = s_rec[0].fb == g_fb ? 0 : s_rec[1].fb == g_fb ? 1 : -1;
    const bool same = sig && slot >= 0 && s_rec[slot].sig == sig;
    if (slot < 0) {
        slot = s_rec[0].fb == NULL ? 0 : s_rec[1].fb == NULL ? 1 : 0;
    }
    s_rec[slot].fb = g_fb;
    s_rec[slot].sig = sig;
    s_ic_refresh = sig != 0;  // (an old icon draws again only in a still frame)
    return same;
}
void men_gr_render(int64_t now)
{
    glay_t L = gr_lay();
    int off = 0;  // the slide of a new level
    if (s_sw_t && now - s_sw_t < GR_SLIDE_MS) {
        const float k = 1 - (float)(now - s_sw_t) / GR_SLIDE_MS;
        off = (int)lroundf(s_sw_dir * 60 * k * k * k);
    }
    s_fr_t0 = esp_timer_get_time();
    s_fr_new = 0;
    s_ic_limit = off || ks_moving(&s_ks) || ks_dragging(&s_ks);
    gr_room_make();      // (gr_prefetch draws on the room too)
    tfn_icon_ask(true);  // (no card read in a frame of the menu)
    if (gr_still(off, now)) {
        if (!s_touch) {
            gr_prefetch(&L);
        }
        tfn_icon_ask(false);
        return;
    }
    s_ic_pend = false;
    gr_bg_grid(&L, off);
    gr_grid(&L, off, now);
    if (s_ngv == 0) {
        ui_text_c(&KF_BIG, gfx_w / 2, L.top + 90, "NO APPS", THEME_MENU_GR_SOFT);
    }
    if (men_level == LV_FAV && s_ngv == 0) {
        ui_text_c(&KF_NAME, gfx_w / 2, L.top + 130, "HOLD AN ICON: FAVORITE", THEME_MENU_GR_SOFT);
    }
    int at, size, alpha;
    if (ks_bar(&s_ks, gfx_h - L.top - 12, &at, &size, &alpha)) {
        ui_rrect_a(gfx_w - 6, L.top + 6 + at, 4, size, 2, THEME_MENU_GR_SCROLL, alpha * 150 / 256);
    }
    gr_status();
    gr_title(off);
    gr_launch(now);
    if (now - men_pop_at < POP_MS) {  // the message of a change (the sheet: a favorite, a move)
        const int w = ui_text_w(&KF_TEXT, men_pop_text) + 40, h = 48, px = (gfx_w - w) / 2, py = gfx_h / 2 - h / 2;
        ui_rrect(px, py, w, h, h / 2, THEME_MENU_POP_BG);
        ui_text_c(&KF_TEXT, gfx_w / 2, py + h / 2 + KF_TEXT.asc / 2 - 1, men_pop_text, THEME_MENU_POP_TEXT);
    }
    tfn_icon_ask(false);
}
#endif  // BOARD_UI_GRID
