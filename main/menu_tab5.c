// The menu of the Tab5: the tabs of the groups at the left, a grid of big icons.
#include "menu_tab5_priv.h"

#if BOARD_UI_TABLET
// THE TAB5 MENU: not a carousel as on the small devices, but a phone-like menu with icons and the category
// selection on the left. Only for BOARD_UI_TABLET (the Tab5). It uses menu_spot.c (the text, the shapes, the
// group tiles) and menu_home.c (the sheet and the edit mode).
//
// The screen (the frame 360 x 640 upright, 640 x 360 on its side):
//  - THE RAIL at the left: a tab for each group of the top level of the layout (FAVORITES, GAMES, UTIL, SETTINGS and
//    the groups of a profile or of the own home), and NEW (all the new apps, when there are some and the layout has no
//    group of that name). HOME holds the apps that a layout puts on the top level. A tap on a tab shows its apps; a tab
//    whose group has one app (SETTINGS: SETUP) opens that app at once. A tap on the tab that shows now goes to the top
//    of the group. A hold on a group tab opens the sheet of the group (HOME 2.0). The rail scrolls when it is too long.
//  - THE STATUS LINE at the top right: the WiFi, the time (24 h, "--:--" when the board does not know it) and the
//    battery. The title of the group (and BACK in a group in a group) at the left of the top.
//  - THE GRID: the apps and the groups in the group, as big icons with their names (2 lines for a long name), the new
//    apps first (lay_view). It scrolls with the momentum of iOS (libs/kscroll). The NEW mark, the favorite star and
//    the badge of the unread notices show on the icons.
// Touch: a press makes the icon smaller (a spring); the release on the same icon opens it (the icon grows and the
// screen goes dark, then the app opens); a drag scrolls. A hold (HOLD_MS) on an icon opens its sheet; a hold on an
// empty place opens the edit mode. The level of the menu is the level of HOME 2.0 (s_level): a tab = a group of the
// top level, LV_FAV, LV_NEW (the virtual group of the new apps) or -1 (HOME).

// ---------------------------------------------------------------- the geometry
tlay_t men_tb_lay(void)
{
    tlay_t L;
    L.land = gfx_w > gfx_h;
    L.rail_w = L.land ? 96 : 84;
    L.tab_h = L.land ? 68 : 84;
    L.tab_y0 = L.land ? 6 : 10;
    L.sym = L.land ? 30 : 42;
    L.top = L.land ? 50 : 94;
    L.gx0 = L.rail_w + 6;
    L.gx1 = gfx_w - 8;
    L.cols = L.land ? 6 : 3;
    L.cw = (L.gx1 - L.gx0) / L.cols;
    L.icon = L.cw - 22 > 70 ? 70 : L.cw - 22;
    L.ch = L.icon + 44;
    return L;
}

// ---------------------------------------------------------------- the tabs
ttab_t men_tbt[G_MAX + 4];
int men_ntbt;
int men_tb_new_n;  // the new apps (the NEW tab)

// the new apps of the layout (the view, each app once, can run, in the order of the view): ITEMS of LV_NEW
void men_tb_new_items(void)
{
    men_n_items = 0;
    for (int k = 0; k < men_nview && men_n_items < (int)(sizeof men_items / sizeof men_items[0]); k++) {
        const lent_t *e = &men_view[k];
        if (e->kind != E_APP || !men_can_run(e->ref) || !men_is_new(men_all[e->ref].app) ||
            men_in_items(men_all[e->ref].app)) {
            continue;
        }
        men_add_app(men_all[e->ref].app);
    }
}
static int tb_new_count(void)
{
    static uint32_t seen[(N_CAP + 31) / 32];
    memset(seen, 0, sizeof seen);
    int n = 0;
    for (int k = 0; k < men_nview; k++) {
        const lent_t *e = &men_view[k];
        if (e->kind != E_APP || !men_can_run(e->ref) || (seen[e->ref >> 5] >> (e->ref & 31) & 1)) {
            continue;
        }
        seen[e->ref >> 5] |= 1u << (e->ref & 31);
        if (men_is_new(men_all[e->ref].app)) {
            n++;
        }
    }
    return n;
}
// the tabs of the layout now (each frame: the counts change with the favorites and the installs)
static void tb_tabs(void)
{
    men_ntbt = 0;
    bool home = false, new_grp = false;
    for (int k = 0; k < men_nview; k++) {
        const lent_t *e = &men_view[k];
        if (e->parent != -1) {
            continue;
        }
        if (e->kind == E_APP && men_can_run(e->ref)) {
            home = true;
        }
        if (e->kind == E_GRP && !strcmp(men_lay.g[e->ref].name, "NEW")) {
            new_grp = true;
        }
    }
    if (men_lay.fav == FAVM_TOP && men_cat_count[LV_FAV] > 0) {
        home = true;  // (the favorites on the top level)
    }
    if (home) {
        men_tbt[men_ntbt++] = (ttab_t){TB_HOME, -1};
    }
    men_tb_new_n = new_grp ? 0 : tb_new_count();
    bool new_done = men_tb_new_n == 0;
    for (int k = 0; k < men_nview && men_ntbt < G_MAX + 3; k++) {
        const lent_t *e = &men_view[k];
        if (e->parent != -1) {
            continue;
        }
        if (e->kind == E_FAV) {
            if (men_lay.fav == FAVM_GROUP && men_cat_count[LV_FAV] > 0) {
                men_tbt[men_ntbt++] = (ttab_t){TB_FAV, LV_FAV};
            }
            if (!new_done) {
                men_tbt[men_ntbt++] = (ttab_t){TB_NEW, LV_NEW};
                new_done = true;
            }  // (NEW after FAVORITES)
        } else if (e->kind == E_GRP && men_cat_count[e->ref] > 0) {
            if (!new_done) {
                men_tbt[men_ntbt++] = (ttab_t){TB_NEW, LV_NEW};
                new_done = true;
            }
            men_tbt[men_ntbt++] = (ttab_t){TB_GRP, (int16_t)e->ref};
        }
    }
    if (!new_done && men_ntbt < G_MAX + 4) {
        men_tbt[men_ntbt++] = (ttab_t){TB_NEW, LV_NEW};
    }
}
// the tab of a level (a group in a group: the tab of its group on the top level); -1: none
int men_tb_tab_of(int level)
{
    int top = level;
    if (level >= 0 && level < men_lay.ng) {
        for (int d = 0; d <= G_MAX && men_gparent(top) >= 0; d++) {
            top = men_gparent(top);
        }
    }
    for (int t = 0; t < men_ntbt; t++) {
        if (men_tbt[t].lev == top) {
            return t;
        }
    }
    return -1;
}
int men_tb_tab_look(const ttab_t *t)
{
    return t->kind == TB_HOME ? ST_TOP : t->kind == TB_FAV ? ST_STAR : t->kind == TB_NEW ? ST_NEW : men_gstyle(t->lev);
}
const char *men_tb_tab_name(const ttab_t *t)
{
    return t->kind == TB_HOME  ? "HOME"
           : t->kind == TB_FAV ? "FAVORITES"
           : t->kind == TB_NEW ? "NEW"
                               : men_gname(t->lev);
}
// the look of the level now (the colours of the room)
int men_tb_look(void)
{
    if (men_level == LV_NEW) {
        return ST_NEW;
    }
    if (men_level == LV_FAV) {
        return ST_STAR;
    }
    if (men_level < 0) {
        return ST_TOP;
    }
    return men_gstyle(men_level);
}

// ---------------------------------------------------------------- the state
int16_t men_tbv[N_CAP + G_MAX + 4];  // the grid: indexes of ITEMS
int men_ntbv;
kscroll_t men_tb_ks, men_tb_rks;         // the scroll of the grid, of the rail
static float s_tb_scroll_of[G_MAX + 3];  // the scroll of each level (tb_slot)
static int s_tb_keep = -2;               // the last level that had a tab (back after the edit mode)
float men_tb_hl = -1;                    // the highlight of the rail: its top (rail px; it slides)
int64_t men_tb_sw_t;                     // a new level: the time (its grid slides in)
int men_tb_sw_dir;                       // 1: from below, -1: from above
int men_tb_t;                            // what the finger is on
int men_tb_pi = -1, men_tb_ptab = -1;    // the item (ITEMS), the tab under the finger at the press
bool men_tb_back_hot, men_tb_hold_done, men_tb_moved;
int64_t men_tb_down_t;
static int s_tb_dx, s_tb_dy;
int men_tb_ai = -1;              // the item of the press spring
float men_tb_as = 1, men_tb_av;  // its scale and speed
int men_tb_li = -1;              // the launch: the item, its app, its start, its box at the start
const app_t *men_tb_lapp;
static bool s_tb_lgo;  // (the app is asked for: the dark frame stays until the menu leaves)
int64_t men_tb_lt;
int men_tb_lx, men_tb_ly, men_tb_ls;
char men_tb_clock[16] = "--:--";
static int64_t s_tb_clock_at = -100000;
static bool s_tb_sntp;

static int tb_slot(int level)
{
    return level < 0 ? 0 : level <= LV_NEW ? level + 1 : 0;
}

// the grid list: the items of the level (no BACK: the title has it; the top level: only its apps)
static void tb_list(void)
{
    men_ntbv = 0;
    for (int i = 0; i < men_n_items; i++) {
        if (men_items[i].kind == IT_BACK) {
            continue;
        }
        if (men_level < 0 && men_items[i].kind != IT_APP) {
            continue;
        }
        men_tbv[men_ntbv++] = (int16_t)i;
    }
}

// ---------------------------------------------------------------- the time
// the local time of the home zone of the board (alarm.c, the CLOCK app sets it): "HH:MM", "--:--" when it is not known
static void tb_clock(int64_t now)
{
    if (now - s_tb_clock_at < 1000) {
        return;
    }
    s_tb_clock_at = now;
    if (!s_tb_sntp && wifi_rssi() != 0) {  // (the WiFi is up: the time from the network, once)
        s_tb_sntp = true;
        clock_sync();
    }
    if (!clock_time_ok()) {
        snprintf(men_tb_clock, sizeof men_tb_clock, "--:--");
        return;
    }
    char tz[64];
    alarm_tz_get(tz, sizeof tz);
    tz_rule_t z;
    const int64_t utc = (int64_t)time(NULL);
    const int64_t l = utc + (tz_parse(tz, &z) ? tz_offset(&z, utc, NULL) : 0);
    const int m = (int)(((l / 60) % 1440 + 1440) % 1440);
    snprintf(men_tb_clock, sizeof men_tb_clock, "%02d:%02d", m / 60, m % 60);
}

// ---------------------------------------------------------------- the level
void men_tb_level_changed(int from)
{
    s_tb_scroll_of[tb_slot(from)] = men_tb_ks.raw;
    ks_init(&men_tb_ks, (float)gfx_h, 0);
    men_tb_ks.raw = s_tb_scroll_of[tb_slot(men_level)];  // (ks_set_max of the next update puts it in the list)
    int a = men_tb_tab_of(from), b = men_tb_tab_of(men_level);
    men_tb_sw_dir =
            (a == b ? (men_level >= 0 && men_level < men_lay.ng && men_gparent(men_level) == from) : b > a) ? 1 : -1;
    men_tb_sw_t = men_now;
    men_tb_t = TT_NONE;
    men_tb_pi = men_tb_ptab = -1;
    men_tb_ai = -1;
    men_tb_as = 1;
    men_tb_av = 0;
}
// the level must have a tab (the first time, after the edit mode, after a new layout): else the last level with a tab,
// else the first group, else the first tab
static void tb_fix_level(void)
{
    if (men_home_active() || !men_ntbt) {
        return;
    }
    if (men_tb_tab_of(men_level) >= 0) {
        s_tb_keep = men_level;
        return;
    }
    int want = -2;
    if (s_tb_keep != -2 && s_tb_keep != men_level && men_tb_tab_of(s_tb_keep) >= 0) {
        want = s_tb_keep;
    }
    for (int t = 0; t < men_ntbt && want == -2; t++) {
        if (men_tbt[t].kind == TB_GRP) {
            want = men_tbt[t].lev;
        }
    }
    if (want == -2) {
        want = men_tbt[0].lev;
    }
    if (want != men_level) {
        men_go_level(want);
        men_tb_sw_t = 0;  // (no slide: the menu comes so)
    }
    s_tb_keep = men_level;
}
// the PWR key or the home bar in the menu (menu_back): a group in a group goes up; a tab of the top level: nothing
bool men_tb_back(void)
{
    if (men_level >= 0 && men_level < men_lay.ng && men_gparent(men_level) >= 0) {
        men_go_level(men_gparent(men_level));
        return true;
    }
    return false;
}
// the level in the layout of an app of the NEW tab (the sheet: MOVE, ADD TO GROUP): its first place in the view
static int tb_parent_of(const app_t *a)
{
    for (int k = 0; k < men_nview; k++) {
        if (men_view[k].kind == E_APP && men_all[men_view[k].ref].app == a) {
            return men_view[k].parent;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- the boxes
static int tb_rows(const tlay_t *L)
{
    return (men_ntbv + L->cols - 1) / L->cols;
}
// the cell of the grid list index k (its top-left; y with the scroll and the slide)
void men_tb_cell(const tlay_t *L, int k, float scroll, int *x, int *y)
{
    *x = L->gx0 + (k % L->cols) * L->cw;
    *y = L->top + 6 + (k / L->cols) * L->ch - (int)lroundf(scroll);
}
// the grid item (an index of ITEMS) at a point, -1: none
static int tb_hit(const tlay_t *L, int px, int py)
{
    if (px < L->gx0 || px >= L->gx0 + L->cols * L->cw || py < L->top) {
        return -1;
    }
    int col = (px - L->gx0) / L->cw;
    float cy = py - L->top - 6 + ks_pos(&men_tb_ks);
    if (cy < 0) {
        return -1;
    }
    int row = (int)(cy / L->ch), k = row * L->cols + col;
    return k >= 0 && k < men_ntbv ? men_tbv[k] : -1;
}
static int tb_tab_at(const tlay_t *L, int py)
{
    float y = py - L->tab_y0 + ks_pos(&men_tb_rks);
    if (y < 0) {
        return -1;
    }
    int t = (int)(y / L->tab_h);
    return t < men_ntbt ? t : -1;
}
// BACK in a group in a group: its box (false: the level has none)
bool men_tb_back_box(const tlay_t *L, int *x, int *y, int *w, int *h)
{
    if (!(men_level >= 0 && men_level < men_lay.ng && men_gparent(men_level) >= 0)) {
        return false;
    }
    int tw = ui_text_w(&KF_NAME, men_gname(men_gparent(men_level)));
    if (tw > 64) {
        tw = 64;
    }
    *x = L->gx0;
    *w = tw + 34;
    *h = 42;
    *y = L->land ? 3 : TB_SB_H(L) + 8;
    return true;
}
static bool in_tb_box(int px, int py, int x, int y, int w, int h)
{
    return px >= x - 4 && px < x + w + 4 && py >= y - 4 && py < y + h + 4;
}

// ---------------------------------------------------------------- the actions
static void tb_open(int i, const tlay_t *L)
{
    if (i < 0 || i >= men_n_items) {
        return;
    }
    if (men_items[i].kind != IT_APP) {
        men_open_item(i);
        return;
    }  // (a group: into it, or its one app)
    for (int k = 0; k < men_ntbv; k++) {
        if (men_tbv[k] != i) {
            continue;
        }
        int x, y;
        men_tb_cell(L, k, ks_pos(&men_tb_ks), &x, &y);
        men_tb_lx = x + (L->cw - L->icon) / 2;
        men_tb_ly = y;
        men_tb_ls = L->icon;
    }
    men_tb_li = i;
    men_tb_lapp = men_items[i].app;
    s_tb_lgo = false;
    men_tb_lt = men_now;
    sfx_ui(SFX_CLICK);
}
static void tb_tab_tap(int t)
{
    if (t < 0 || t >= men_ntbt) {
        return;
    }
    const ttab_t *tb = &men_tbt[t];
    if (men_tb_tab_of(men_level) == t) {
        sfx_ui(SFX_TICK);
        if (men_level != tb->lev) {
            men_go_level(tb->lev);  // (in a group in it: back to the group of the tab)
        } else {
            ks_to(&men_tb_ks, 0);  // (the top of the grid)
        }
        return;
    }
    if (tb->kind == TB_GRP && men_cat_count[tb->lev] == 1) {
        // a group with one app (SETTINGS): the app opens at once
        for (int k = 0; k < men_nview; k++) {
            const lent_t *e = &men_view[k];
            if (e->kind == E_APP && men_can_run(e->ref) && men_g_under(e->parent, tb->lev)) {
                sfx_ui(SFX_CLICK);
                app_open(men_all[e->ref].app);
                return;
            }
        }
    }
    sfx_ui(SFX_TICK);
    men_go_level(tb->lev);
}
static void tb_hold_item(int i)
{
    int lev = men_level == LV_NEW ? tb_parent_of(men_items[i].app) : men_level;
    if (men_home_hold_item(i, lev)) {
        men_tb_ai = -1;
    }
}
static void tb_hold_bg(void)
{
    // (no SETUP in the layout: the edit mode of the top level, where a hold of 8 s still opens SETUP)
    if (!men_setup_in_menu() || men_level == LV_NEW) {
        men_level = -1;
    }
    men_home_hold_bg(men_tb_down_t, s_tb_dx, s_tb_dy);
}
// the console "menusel": the app shows in the grid
void men_tb_reveal(int i)
{
    tlay_t L = men_tb_lay();
    tb_list();
    for (int k = 0; k < men_ntbv; k++) {
        if (men_tbv[k] == i) {
            float y = (float)((k / L.cols) * L.ch);
            float view = (float)(gfx_h - L.top);
            if (y < men_tb_ks.raw || y + L.ch > men_tb_ks.raw + view) {
                men_tb_ks.raw = y - view / 3 > 0 ? y - view / 3 : 0;
            }
        }
    }
}

// ---------------------------------------------------------------- the keys (the Tab5 keyboard)
// The arrows move a focus over the grid (left/right one cell, up/down one row), ENTER opens it, TAB / SHIFT+TAB or
// PGDN / PGUP go to the next / the last tab, ESC or BACKSPACE go back (a group in a group), HOME / END: the first /
// the last app. The focus has a ring for KF_SHOW_MS after the last key (the menu then draws all: no part frames);
// a touch hides it.
#define KF_SHOW_MS 8000
int men_tb_kf = -1;  // the focus: an index of the grid list (s_tbv), -1: none
static int64_t s_tb_kf_t = -100000;
bool men_tb_kf_on(void)
{
    return men_tb_kf >= 0 && men_tb_kf < men_ntbv && men_now - s_tb_kf_t < KF_SHOW_MS;
}
static void tb_kf_reveal(const tlay_t *L)
{
    float y = (float)((men_tb_kf / L->cols) * L->ch);
    float view = (float)(gfx_h - L->top);
    if (y < men_tb_ks.raw) {
        men_tb_ks.raw = y > 0 ? y : 0;
    } else if (y + L->ch + 12 > men_tb_ks.raw + view) {
        men_tb_ks.raw = y + L->ch + 12 - view;
    }
}
static void tb_key(int key, int mods)
{
    tlay_t L = men_tb_lay();
    tb_list();
    if (key == INK_TAB || key == INK_PGDN || key == INK_PGUP) {
        if (!men_ntbt) {
            return;
        }
        int d = key == INK_PGUP || (key == INK_TAB && (mods & INM_SHIFT)) ? -1 : 1;
        int cur = men_tb_tab_of(men_level), t = cur < 0 ? 0 : ((cur + d) % men_ntbt + men_ntbt) % men_ntbt;
        sfx_ui(SFX_TICK);
        men_go_level(men_tbt[t].lev);
        men_tb_kf = 0;
        s_tb_kf_t = men_now;
        return;
    }
    if (key == INK_ESC || key == INK_BKSP) {
        if (men_tb_back()) {
            sfx_ui(SFX_BACK);
        }
        men_tb_kf = -1;
        return;
    }
    if (!men_ntbv) {
        return;
    }
    const bool shown = men_tb_kf_on();
    if (men_tb_kf < 0 || men_tb_kf >= men_ntbv) {
        men_tb_kf = 0;
    }
    if (key == INK_ENTER) {
        if (shown) {
            tb_open(men_tbv[men_tb_kf], &L);
        }
        s_tb_kf_t = men_now;
        return;
    }
    int k = men_tb_kf;
    if (shown) {
        if (key == INK_LEFT) {
            k--;
        } else if (key == INK_RIGHT) {
            k++;
        } else if (key == INK_UP) {
            k -= L.cols;
        } else if (key == INK_DOWN) {
            k += L.cols;
        } else if (key == INK_HOME) {
            k = 0;
        } else if (key == INK_END) {
            k = men_ntbv - 1;
        } else {
            return;
        }
    } else if (key != INK_LEFT && key != INK_RIGHT && key != INK_UP && key != INK_DOWN && key != INK_HOME &&
               key != INK_END) {
        return;
    }
    if (k < 0) {
        k = key == INK_UP ? men_tb_kf % L.cols : 0;
    }
    if (k >= men_ntbv) {
        k = key == INK_DOWN ? men_ntbv - 1 : men_ntbv - 1;
    }
    if (k != men_tb_kf || !shown) {
        sfx_ui(SFX_TICK);
    }
    men_tb_kf = k;
    s_tb_kf_t = men_now;
    tb_kf_reveal(&L);
}

// ---------------------------------------------------------------- input and update
void men_tb_enter(int64_t now)
{
    (void)now;
    float keep = men_tb_ks.raw;  // (back from an app: the grid where the user left it)
    ks_init(&men_tb_ks, (float)gfx_h, 0);
    men_tb_ks.raw = keep > 0 ? keep : 0;
    men_tb_t = TT_NONE;
    men_tb_pi = men_tb_ptab = men_tb_li = men_tb_ai = -1;
    s_tb_lgo = false;
    men_tb_as = 1;
    men_tb_av = 0;
    men_tb_sw_t = 0;
    men_tb_hl = -1;
    s_tb_clock_at = -100000;
}
void men_tb_input(const input_event_t *ev)
{
    if (men_tb_li >= 0) {
        return;  // (an app opens now)
    }
    if (ev->kind == IN_KEY) {
        tb_key(ev->x, ev->y);
        return;
    }
    if (ev->kind == IN_DOWN) {
        men_tb_kf = -1;  // (a touch: no key focus)
    }
    tlay_t L = men_tb_lay();
    int bx, by, bw, bh;
    bool back = men_tb_back_box(&L, &bx, &by, &bw, &bh);
    switch (ev->kind) {
    case IN_DOWN:
        men_tb_t = TT_NONE;
        men_tb_pi = men_tb_ptab = -1;
        men_tb_back_hot = men_tb_hold_done = men_tb_moved = false;
        men_tb_down_t = ev->t_ms;
        s_tb_dx = ev->x;
        s_tb_dy = ev->y;
        if (ev->x < L.rail_w) {
            men_tb_t = TT_RAIL;
            if (!ks_down(&men_tb_rks, ev->y, ev->t_ms)) {
                men_tb_ptab = tb_tab_at(&L, ev->y);
            }
        } else if (back && in_tb_box(ev->x, ev->y, bx, by, bw, bh)) {
            men_tb_t = TT_BACK;
            men_tb_back_hot = true;
        } else {
            men_tb_t = TT_GRID;
            if (!ks_down(&men_tb_ks, ev->y, ev->t_ms)) {
                men_tb_pi = tb_hit(&L, ev->x, ev->y);
            }
            if (men_tb_pi >= 0) {
                men_tb_ai = men_tb_pi;
            }
        }
        break;
    case IN_MOVE:
        if (abs(ev->x - s_tb_dx) > 10 || abs(ev->y - s_tb_dy) > 10) {
            men_tb_moved = true;
        }
        if (men_tb_t == TT_RAIL) {
            if (ks_move(&men_tb_rks, ev->y, ev->t_ms)) {
                input_tap_cancel();
                men_tb_ptab = -1;
            }
        } else if (men_tb_t == TT_GRID) {
            if (ks_move(&men_tb_ks, ev->y, ev->t_ms)) {
                input_tap_cancel();
                men_tb_pi = -1;
            }
            if (men_tb_pi >= 0 && tb_hit(&L, ev->x, ev->y) != men_tb_pi) {
                men_tb_pi = -1;  // (out of the cell)
            }
        } else if (men_tb_t == TT_BACK) {
            men_tb_back_hot = in_tb_box(ev->x, ev->y, bx, by, bw, bh);
        }
        break;
    case IN_UP: {
        int t = men_tb_t;
        men_tb_t = TT_NONE;
        if (t == TT_RAIL) {
            bool moved = ks_up(&men_tb_rks, ev->t_ms);
            int p = men_tb_ptab;
            men_tb_ptab = -1;
            if (!moved && !men_tb_hold_done && p >= 0 && tb_tab_at(&L, ev->y) == p && ev->x < L.rail_w + 8) {
                tb_tab_tap(p);
            }
        } else if (t == TT_GRID) {
            bool moved = ks_up(&men_tb_ks, ev->t_ms);
            int i = men_tb_pi;
            men_tb_pi = -1;
            if (!moved && !men_tb_hold_done && i >= 0 && tb_hit(&L, ev->x, ev->y) == i) {
                tb_open(i, &L);
            }
        } else if (t == TT_BACK) {
            if (back && !men_tb_hold_done && in_tb_box(ev->x, ev->y, bx, by, bw, bh)) {
                sfx_ui(SFX_BACK);
                men_go_level(men_gparent(men_level));
            }
            men_tb_back_hot = false;
        }
        break;
    }
    default:
        break;  // (IN_TAP, IN_LONG: the menu acts at the release, and its hold has its own time)
    }
}
void men_tb_update(int64_t now, float dt)
{
    tb_tabs();
    tb_fix_level();
    if (men_home_active()) {
        return;
    }
    tb_list();
    tlay_t L = men_tb_lay();
    const float view = (float)(gfx_h - L.top);
    ks_set_view(&men_tb_ks, view);
    ks_set_max(&men_tb_ks, (float)(tb_rows(&L) * L.ch + 6 + (L.land ? 10 : 24)) - view);
    ks_update(&men_tb_ks, now);
    ks_set_view(&men_tb_rks, (float)gfx_h);
    ks_set_max(&men_tb_rks, (float)(L.tab_y0 + men_ntbt * L.tab_h + 8 - gfx_h));
    ks_update(&men_tb_rks, now);
    // the hold: the sheet of an icon or of a group tab, the edit mode on an empty place
    if (men_tb_t != TT_NONE && men_tb_t != TT_BACK && !men_tb_moved && !men_tb_hold_done && !ks_dragging(&men_tb_ks) &&
        !ks_dragging(&men_tb_rks) && now - men_tb_down_t >= HOLD_MS) {
        men_tb_hold_done = true;
        if (men_tb_t == TT_GRID && men_tb_pi >= 0) {
            int i = men_tb_pi;
            men_tb_pi = -1;
            tb_hold_item(i);
        } else if (men_tb_t == TT_GRID) {
            tb_hold_bg();
        } else if (men_tb_ptab >= 0 && men_tbt[men_tb_ptab].kind == TB_GRP) {
            int g = men_tbt[men_tb_ptab].lev;
            men_tb_ptab = -1;
            men_home_sheet_group(g);
        }
        if (men_home_active()) {
            return;
        }
    }
    // the press: a spring to a smaller icon while the finger is on it (smaller still while a hold fills)
    float goal = 1.0f;
    if (men_tb_ai >= 0 && men_tb_ai == men_tb_pi && men_tb_t == TT_GRID && !men_tb_hold_done) {
        float hold = (float)(now - men_tb_down_t - HOLD_SHOW_MS) / (HOLD_MS - HOLD_SHOW_MS);
        goal = 0.9f - 0.06f * (hold < 0 ? 0 : hold > 1 ? 1 : hold);
    }
    if (men_tb_ai >= 0) {
        float h = dt / 4;
        for (int k = 0; k < 4; k++) {
            float acc = -900.0f * (men_tb_as - goal) - 28.0f * men_tb_av;
            men_tb_av += acc * h;
            men_tb_as += men_tb_av * h;
        }
        if (goal >= 1 && fabsf(men_tb_as - 1) < 0.002f && fabsf(men_tb_av) < 0.01f) {
            men_tb_as = 1;
            men_tb_av = 0;
            men_tb_ai = -1;
        }
    }
    // the launch: the app opens at its end (main.c changes the app in the next frame)
    if (men_tb_li >= 0 && !s_tb_lgo && now - men_tb_lt >= TB_LAUNCH_MS) {
        s_tb_lgo = true;
        app_open(men_tb_lapp);
    }
    // the highlight of the rail slides to the tab now
    int cur = men_tb_tab_of(men_level);
    if (cur >= 0) {
        float to = (float)(L.tab_y0 + cur * L.tab_h);
        if (men_tb_hl < 0) {
            men_tb_hl = to;
        }
        men_tb_hl += (to - men_tb_hl) * (1 - expf(-dt * 16));
        if (fabsf(to - men_tb_hl) < 0.3f) {
            men_tb_hl = to;
        }
    }
    tb_clock(now);
}
#endif  // BOARD_UI_TABLET
