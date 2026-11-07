// The menu: HOME 2.0 (the sheet of an app or a group, the edit mode, the actions).
#include "menu_home_priv.h"

// HOME 2.0 (TFN OS): a long press on an icon opens a context menu, and a long press on the background starts the
// re-arrange and the new group. The screens over the menu:
//  - THE SHEET: a hold on an app in the menu. The app at the side, six big buttons: OPEN, FAVORITE, ADD TO GROUP, MOVE,
//    INFO, and DELETE (an installed app; a lock when the profile keeps it) or HIDE (a built-in app). A hold on a group:
//    OPEN, RENAME (the boards with the keyboard), LOOK, MOVE, TAKE APART.
//  - THE PICKER of ADD TO GROUP: a button for each group, MORE, NEW GROUP.
//  - INFO: the facts of the app.
//  - THE EDIT MODE: a hold on the background (or MOVE). The tiles of a level wiggle, on pages. Drag a tile to a new
//    place, or tap it and then tap a place (pick and place: a weak touch and the small bar). Hold a dragged app on
//    another app for 0.4 s: a new group of the two. Drop it on a group: into the group; on the UP tile: out of the
//    group; on FAVORITES: a favorite. A tap on a group opens it in the edit mode. + GROUP, UNDO, DONE. The tile
//    SHOW n HIDDEN brings the hidden apps back.
// A close: the X mark, the PWR key (menu_back), or a tap out of the buttons of the sheet.
// Each change goes into the own home of the board (/data/home.txt, menu_tree.c): the sheet at once, the edit mode at
// DONE. The manager keeps its profile in /data/menu.txt and has the last word (a forced push removes home.txt).

int men_hm = HM_OFF;
const app_t *men_hm_app;  // the sheet: the app (NULL: the group s_hm_grp)
int men_hm_grp = -1;
static int s_hm_from = -1;  // the level of the menu where the hold was
int men_hm_page;            // the page of the picker, of the edit mode
int men_hm_hot = -1;        // the button under the finger
int men_hm_focus = -1;      // the wheel (the Watcher): the target in focus
hbtn_t men_hb[HB_MAX];
int men_nhb;
static char s_hm_name[16];  // RENAME
char *men_undo[UNDO_MAX];   // the edit mode: the layout before each change (text, PSRAM)
int men_nundo;
static bool s_hm_dirty;  // the edit mode changed the layout
bool men_hm_skip_tap;    // a hold opened the screen: the tap of its release does nothing (a new press clears it)
// the edit mode
int men_hm_lev = -1;     // the level that the edit mode shows (-1: the top)
int16_t men_he[HE_MAX];  // its tiles: an index of s_lay.e, or HT_*
int men_nhe;
bool men_ed_touch, men_ed_drag, men_ed_swipe, men_ed_secret;
int men_ed_x0, men_ed_y0, men_ed_x, men_ed_y, men_ed_press = -1;
static int64_t s_ed_t0, s_ed_edge_t, s_ed_hover_t;
int men_ed_hover = -1, men_ed_edge_dir;
bool men_ed_merge;
static lent_t s_pk;  // the tile that is picked (a tap) or dragged
bool men_pk_on;
float men_ed_pagef;  // the page on the screen (it slides to s_hm_page)

bool men_home_active(void)
{
    return men_hm != HM_OFF;
}

// ---------------------------------------------------------------- the geometry
// the part of the frame that the screens use (the round panel: a square in the circle)
void men_hm_area(int *x0, int *y0, int *x1, int *y1)
{
    if (gfx_round()) {
        int c = gfx_w / 2, r = gfx_w / 2;
        *x0 = c - r * 70 / 100;
        *x1 = c + r * 70 / 100;
        *y0 = gfx_h / 2 - r * 72 / 100;
        *y1 = gfx_h / 2 + r * 72 / 100;
    } else {
        *x0 = 0;
        *y0 = 0;
        *x1 = gfx_w;
        *y1 = gfx_h;
    }
}
bool men_hm_wide(void)
{
    return gfx_w >= gfx_h * 2.4f;
}
// the X mark (a close): 44 x 44
void men_hm_xbox(int *x, int *y)
{
    int ax0, ay0, ax1, ay1;
    men_hm_area(&ax0, &ay0, &ax1, &ay1);
    if (gfx_round()) {
        *x = gfx_w / 2 - 22;
        *y = 10;
    } else {
        *x = ax1 - 50;
        *y = ay0 + 6;
    }
}
// the sheet, the picker, INFO: the place of the app (an icon of s px at ix, iy) and the box of the buttons
void men_hm_sheet_geo(int *ix, int *iy, int *is, int *bx, int *by, int *bw, int *bh, int *cols, int *rows)
{
    int ax0, ay0, ax1, ay1;
    men_hm_area(&ax0, &ay0, &ax1, &ay1);
    if (men_hm_wide()) {
        *is = gfx_h - 84 < 96 ? gfx_h - 84 : 96;
        *ix = 16 + (150 - *is) / 2;
        *iy = 10;
        *bx = 180;
        *by = 8;
        *bw = gfx_w - 56 - 180;
        *bh = gfx_h - 16;
        *cols = 3;
        *rows = 2;
    } else {
        *is = gfx_round() ? 56 : (gfx_w >= 360 ? 80 : 64);
        *ix = ax0 + 8;
        *iy = ay0 + (gfx_round() ? 4 : 8);
        *bx = ax0 + 8;
        *by = *iy + *is + 10;
        *bw = ax1 - ax0 - 16;
        *bh = ay1 - *by - 8;
        *cols = 2;
        *rows = 3;
        if (gfx_w < 240) {
            *cols = 1;
            *rows = 6;
        }  // (the 3.49 upright: 172 px wide)
    }
}
static void hm_add_btn(int act, int arg, const char *t, bool off, bool red)
{
    if (men_nhb >= HB_MAX) {
        return;
    }
    men_hb[men_nhb++] = (hbtn_t){0, 0, 0, 0, act, arg, t, off, red};
}
// the places of the buttons of the sheet and the picker (the grid of the sheet; the last button of a short list keeps
// its own cell)
static void hm_grid_btns(void)
{
    int ix, iy, is, bx, by, bw, bh, cols, rows;
    men_hm_sheet_geo(&ix, &iy, &is, &bx, &by, &bw, &bh, &cols, &rows);
    int gap = 8, w = (bw - gap * (cols - 1)) / cols, h = (bh - gap * (rows - 1)) / rows;
    if (h > 66) {
        by += (h - 66) * rows / 2;
        h = 66;
    }
    for (int i = 0; i < men_nhb; i++) {
        int c = i % cols, r = i / cols;
        men_hb[i].x = bx + c * (w + gap);
        men_hb[i].y = by + r * (h + gap);
        men_hb[i].w = w;
        men_hb[i].h = h;
    }
}

// ---------------------------------------------------------------- the sheet of an app or a group
static bool kbd_frame_ok(void)
{
    return (gfx_w == 640 && gfx_h == 172) || (gfx_w == 320 && gfx_h == 240);
}
bool men_hm_required(const app_t *a)
{
    const tfn_entry_t *e = tfn_entry_of(a);
    return e && tfn_store_required(e->id);
}
static void hm_sheet_btns(void)
{
    men_nhb = 0;
    if (men_hm_app) {
        bool fav = men_fav_index(men_hm_app) >= 0;
        hm_add_btn(HA_OPEN, 0, "OPEN", false, false);
        hm_add_btn(HA_FAV, 0, fav ? "UNFAVORITE" : "FAVORITE", !men_fav_on(), false);
        hm_add_btn(HA_GROUP, 0, "ADD TO GROUP", false, false);
        hm_add_btn(HA_MOVE, 0, "MOVE", false, false);
        hm_add_btn(HA_INFO, 0, "INFO", false, false);
        if (tfn_is_stub(men_hm_app)) {
            if (men_hm_required(men_hm_app)) {
                hm_add_btn(HA_LOCK, 0, "DELETE", true, true);
            } else {
                hm_add_btn(HA_DEL, 0, "DELETE", false, true);
            }
        } else {
            hm_add_btn(men_hm_app == &app_settings ? HA_NOHIDE : HA_HIDE, 0, "HIDE", men_hm_app == &app_settings, true);
        }
    } else {
        hm_add_btn(HA_OPEN, 0, "OPEN", false, false);
        hm_add_btn(HA_RENAME, 0, "RENAME", !kbd_frame_ok(), false);
        hm_add_btn(HA_LOOK, 0, "LOOK", false, false);
        hm_add_btn(HA_MOVE, 0, "MOVE", false, false);
        hm_add_btn(HA_APART, 0, "TAKE APART", false, true);
    }
    hm_grid_btns();
}
// the picker: the groups (not the level of the app), 4 or 5 on a page, MORE, NEW GROUP
static int s_pick_g[G_MAX];
static int s_npick_g;
static void hm_pick_btns(void)
{
    s_npick_g = 0;
    for (int k = 0; k < men_lay.ne; k++) {
        if (men_lay.e[k].kind == E_GRP && men_lay.e[k].ref != s_hm_from && s_npick_g < G_MAX) {
            s_pick_g[s_npick_g++] = men_lay.e[k].ref;
        }
    }
    men_nhb = 0;
    int per = s_npick_g > 5 ? 4 : 5, pages = (s_npick_g + per - 1) / per;
    if (men_hm_page >= pages) {
        men_hm_page = 0;
    }
    for (int i = men_hm_page * per; i < s_npick_g && i < (men_hm_page + 1) * per; i++) {
        hm_add_btn(HA_TO, s_pick_g[i], men_lay.g[s_pick_g[i]].name, false, false);
    }
    if (s_npick_g > 5) {
        while (men_nhb < 4) {
            hm_add_btn(HA_NONE, 0, "", true, false);
        }
        hm_add_btn(HA_MORE, 0, "MORE", false, false);
    } else {
        while (men_nhb < 5) {
            hm_add_btn(HA_NONE, 0, "", true, false);
        }
    }
    hm_add_btn(HA_NEWG, 0, "NEW GROUP", !men_hm_has_slot(), false);
    hm_grid_btns();
}

static void hm_open(int mode)
{
    men_hm = mode;
    men_hm_hot = -1;
    men_hm_focus = -1;
    if (mode == HM_SHEET) {
        hm_sheet_btns();
    } else if (mode == HM_PICK) {
        men_hm_page = 0;
        hm_pick_btns();
    } else {
        men_nhb = 0;
    }
}
static void home_sheet_app(const app_t *app, int level)
{
    men_hm_skip_tap = true;
    men_hm_app = app;
    men_hm_grp = -1;
    s_hm_from = level;
    sfx_ui(SFX_CLICK);
    hm_open(HM_SHEET);
    ESP_LOGI("menu", "home: sheet of %s (INFO at %d,%d)", app->name, men_hb[4].x + men_hb[4].w / 2,
             men_hb[4].y + men_hb[4].h / 2);
}
void men_home_sheet_group(int g)
{
    men_hm_skip_tap = true;
    men_hm_app = NULL;
    men_hm_grp = g;
    s_hm_from = men_gparent(g);
    sfx_ui(SFX_CLICK);
    hm_open(HM_SHEET);
    ESP_LOGI("menu", "home: sheet of the group %s", men_lay.g[g].name);
}

// ---------------------------------------------------------------- the edit mode
static void he_build(void)
{
    men_nhe = 0;
    if (men_hm_lev >= 0) {
        men_he[men_nhe++] = HT_UP;
    }
    for (int k = 0; k < men_lay.ne && men_nhe < HE_MAX - 1; k++) {
        const lent_t *e = &men_lay.e[k];
        if (e->parent != men_hm_lev) {
            continue;
        }
        if (e->kind == E_REST) {
            continue;
        }
        if (e->kind == E_APP && (!men_can_run(e->ref) || men_lay_hid(&men_lay, e->ref))) {
            continue;
        }
        if (e->kind == E_FAV && (men_hm_lev >= 0 || men_lay.fav != FAVM_GROUP)) {
            continue;
        }
        men_he[men_nhe++] = (int16_t)k;
    }
    if (men_hm_lev < 0 && men_hid_count() > 0) {
        men_he[men_nhe++] = HT_SHOW;
    }
}
hgrid_t men_hm_grid(void)
{
    hgrid_t g = {0};
    int ax0, ay0, ax1, ay1;
    men_hm_area(&ax0, &ay0, &ax1, &ay1);
    g.bar_y = ay0 + 4;
    g.bar_h = gfx_round() ? 40 : 42;
    g.gx = 10;
    g.gy = 8;
    g.lab = 16;
    if (men_hm_wide()) {
        g.t = 76;
        g.cols = 7;
        g.rows = 1;
    } else {
        g.t = gfx_round() ? 72 : (gfx_w >= 360 && gfx_h >= 360 ? 88 : 64);
        g.gx = 12;
        g.cols = (ax1 - ax0 + g.gx) / (g.t + g.gx);
        g.rows = (ay1 - ay0 - g.bar_h - 8 - 18 + g.gy) / (g.t + g.lab + g.gy);
        if (g.rows < 1) {
            g.rows = 1;
        }
    }
    g.x0 = ax0 + (ax1 - ax0 - (g.cols * g.t + (g.cols - 1) * g.gx)) / 2;
    g.y0 = g.bar_y + g.bar_h + 8;
    return g;
}
static int he_per(void)
{
    hgrid_t g = men_hm_grid();
    return g.cols * g.rows;
}
int men_he_pages(void)
{
    int p = he_per();
    return men_nhe ? (men_nhe + p - 1) / p : 1;
}
// the box of the tile i (on its page, with the slide of the pages)
void men_he_rect(int i, int *x, int *y, int *t)
{
    hgrid_t g = men_hm_grid();
    int per = g.cols * g.rows, page = i / per, j = i % per;
    *t = g.t;
    *x = g.x0 + (j % g.cols) * (g.t + g.gx) + (int)lroundf((page - men_ed_pagef) * gfx_w);
    *y = g.y0 + (j / g.cols) * (g.t + g.lab + g.gy);
}
// the tile at a point (the page now), -1: none; m: the margin around each tile
static int he_at(int x, int y, int m)
{
    int per = he_per();
    for (int i = men_hm_page * per; i < men_nhe && i < (men_hm_page + 1) * per; i++) {
        int tx, ty, t;
        men_he_rect(i, &tx, &ty, &t);
        if (ui_hit(x, y, tx, ty, t, t, m)) {
            return i;
        }
    }
    return -1;
}
// the place for a drop at a point: the tile to go before, or s_nhe (the end)
int men_he_slot(int x, int y)
{
    int per = he_per(), best = -1, bd = 1 << 30;
    for (int i = men_hm_page * per; i < men_nhe && i < (men_hm_page + 1) * per; i++) {
        int tx, ty, t;
        men_he_rect(i, &tx, &ty, &t);
        int cx = tx + t / 2, cy = ty + t / 2, d = (x - cx) * (x - cx) + (y - cy) * (y - cy) * 4;
        if (d < bd) {
            bd = d;
            best = x < cx ? i : i + 1;
        }
    }
    if (best < 0) {
        best = men_nhe;
    }
    if (men_hm_lev >= 0 && best < 1) {
        best = 1;  // (never before UP)
    }
    if (best < men_nhe && men_he[best] == HT_SHOW) {
        best = men_nhe;
    }
    return best;
}
int men_he_find_pk(void)
{
    if (!men_pk_on) {
        return -1;
    }
    for (int i = 0; i < men_nhe; i++) {
        if (men_he[i] >= 0 && men_lay.e[men_he[i]].kind == s_pk.kind && men_lay.e[men_he[i]].ref == s_pk.ref) {
            return i;
        }
    }
    return -1;
}
// the buttons of the bar: + GROUP, UNDO, DONE (right; the round panel: in the middle)
static void hm_edit_btns(void)
{
    men_nhb = 0;
    hm_add_btn(HA_ADDG, 0, "+ GROUP", !men_hm_has_slot(), false);
    hm_add_btn(HA_UNDO, 0, "UNDO", men_nundo == 0, false);
    hm_add_btn(HA_DONE, 0, "DONE", false, false);
    hgrid_t g = men_hm_grid();
    int ax0, ay0, ax1, ay1, w[3], tw = 0, gap = 8;
    men_hm_area(&ax0, &ay0, &ax1, &ay1);
    for (int i = 0; i < 3; i++) {
        w[i] = men_mtext_w(&MF_SMALL, men_hb[i].t, 1) + 30;
        if (w[i] < 70) {
            w[i] = 70;
        }
        tw += w[i];
    }
    tw += gap * 2;
    if (tw > ax1 - ax0 - 16) {  // (the 3.49 upright: three equal buttons, the small face)
        gap = 4;
        for (int i = 0; i < 3; i++) {
            w[i] = (ax1 - ax0 - 12 - 2 * gap) / 3;
        }
        tw = w[0] * 3 + gap * 2;
        men_hb[0].t = "+";  // (the add of a group: "+ GROUP" does not fit)
    }
    int x = gfx_round() ? (gfx_w - tw) / 2 : ax1 - 8 - tw;
    for (int i = 0; i < 3; i++) {
        men_hb[i].x = x;
        men_hb[i].y = g.bar_y;
        men_hb[i].w = w[i];
        men_hb[i].h = g.bar_h;
        x += w[i] + gap;
    }
}
static void hm_edit_refresh(void)
{
    he_build();
    int pages = men_he_pages();
    if (men_hm_page >= pages) {
        men_hm_page = pages - 1;
    }
    if (men_hm_page < 0) {
        men_hm_page = 0;
    }
    hm_edit_btns();
}
// the edit mode at a level; pick: an entry to pick (kind -1: none)
static void home_edit_open(int level, lent_t pick)
{
    if (level == LV_FAV) {
        level = -1;
    }
    men_hm_materialize();
    men_hm_lev = level;
    men_hm = HM_EDIT;
    s_hm_dirty = false;
    men_hm_hot = men_hm_focus = -1;
    men_undo_clear();
    s_pk = pick;
    men_pk_on = pick.kind != 255;
    men_ed_touch = men_ed_drag = men_ed_swipe = men_ed_secret = false;
    men_ed_press = men_ed_hover = -1;
    men_ed_merge = false;
    men_hm_page = 0;
    hm_edit_refresh();
    int i = men_he_find_pk();
    if (i >= 0) {
        men_hm_page = i / he_per();
    }
    men_ed_pagef = (float)men_hm_page;
    sfx_ui(SFX_TICK);
    ESP_LOGI("menu", "home: edit mode (level %d, %d tiles; the buttons at %d,%d %d,%d %d,%d)", level, men_nhe,
             men_hb[0].x + men_hb[0].w / 2, men_hb[0].y + men_hb[0].h / 2, men_hb[1].x + men_hb[1].w / 2,
             men_hb[1].y + men_hb[1].h / 2, men_hb[2].x + men_hb[2].w / 2, men_hb[2].y + men_hb[2].h / 2);
}
static const lent_t NO_PICK = {255, -1, -1};
void men_hm_change(void)
{
    s_hm_dirty = true;
    men_lay_view();
    men_count_groups();
    hm_edit_refresh();
}

// a drop (or a tap place) of the picked tile: on the tile t (merge: onto it), or before the slot
static void hm_drop(int t, bool merge, int slot)
{
    int i = men_he_find_pk();
    men_pk_on = false;
    if (i < 0) {
        return;
    }
    int k = men_he[i];
    const lent_t v = men_lay.e[k];
    if (merge && t >= 0 && t != i) {
        int tt = men_he[t];
        if (tt == HT_UP) {
            men_undo_push();
            men_op_out(k);
            men_hm_change();
            sfx_ui(SFX_BACK);
            men_pop("MOVED UP");
            return;
        }
        if (tt == HT_SHOW) {
            return;
        }
        const lent_t T = men_lay.e[tt];
        if (T.kind == E_FAV) {
            if (v.kind == E_APP && men_fav_on() && men_fav_index(men_all[v.ref].app) < 0) {
                men_fav_toggle(men_all[v.ref].app);
                men_count_groups();
                men_pop("ADDED TO FAVORITES");
                sfx_ui(SFX_COIN);
            }
            return;
        }
        if (T.kind == E_GRP) {
            men_undo_push();
            if (men_op_to_group(k, T.ref)) {
                men_hm_change();
                sfx_ui(SFX_COIN);
                men_pop("MOVED INTO THE GROUP");
            } else {
                free(men_undo[--men_nundo]);
                sfx_ui(SFX_BACK);
            }
            return;
        }
        if (T.kind == E_APP && v.kind == E_APP) {
            men_undo_push();
            int g = men_op_new_group(k, tt);
            if (g >= 0) {
                men_hm_change();
                sfx_ui(SFX_COIN);
                men_pop("A NEW GROUP");
            } else {
                free(men_undo[--men_nundo]);
                sfx_ui(SFX_BACK);
                men_pop("NO ROOM FOR A GROUP");
            }
            return;
        }
    }
    if (slot == i || slot == i + 1) {
        sfx_ui(SFX_TICK);
        return;
    }  // (the same place)
    men_undo_push();
    men_op_move(k, slot < men_nhe && men_he[slot] >= 0 ? men_he[slot] : -1);
    men_hm_change();
    sfx_ui(SFX_TICK);
}
void men_hm_edit_done(void)
{
    if (s_hm_dirty) {
        char lev[16] = "";
        if (men_hm_lev >= 0) {
            snprintf(lev, sizeof lev, "%s", men_lay.g[men_hm_lev].name);
        }
        men_hm_drop_empty();
        men_hm_norm();
        men_home_save();
        men_lay_load();  // (the groups get their numbers again)
        int g = lev[0] ? men_hm_group_by_name(lev) : -1;
        men_level = g;
        memset(men_sel_of, 0, sizeof men_sel_of);
        men_sel = 0;
    }
    men_undo_clear();
    men_pop_at = -100000;  // (an old message of the edit mode does not stay on the menu)
    men_home_close();
}

// ---------------------------------------------------------------- the actions
static void hm_ask_apart(int a)
{
    if (a != 1 || men_hm_grp < 0) {
        return;
    }
    men_hm_materialize();
    men_op_apart(men_hm_grp);
    men_hm_drop_empty();
    men_home_save();
    men_lay_load();
    men_level = -1;
    men_hm_after();
    men_pop("THE GROUP IS GONE");
}
// a button that is off: the reason in a message
static void hm_act_off(const hbtn_t *b)
{
    if (b->act == HA_LOCK) {
        men_pop("THE MANAGER KEEPS IT");
    } else if (b->act == HA_NOHIDE) {
        men_pop("SETUP STAYS");
    } else if (b->act == HA_FAV) {
        men_pop("FAVORITES ARE OFF");
    } else if (b->act == HA_RENAME) {
        men_pop("RENAME IN THE MANAGER");
    }
    sfx_ui(SFX_BACK);
}
// TO <group> and NEW GROUP: the app (ALL index ai) goes into the group (a new group for NEW GROUP)
static void hm_act_to(const hbtn_t *b, int ai)
{
    if (ai < 0) {
        men_home_close();
        return;
    }
    men_hm_materialize();
    int p = s_hm_from >= 0 && s_hm_from != LV_FAV ? s_hm_from : -1;
    int k = men_ent_find(E_APP, p, ai);
    if (k < 0) {
        k = men_ent_any(E_APP, ai);
    }
    int g = b->arg;
    if (b->act == HA_NEWG) {
        int pl = k >= 0 ? men_lay.e[k].parent : -1;
        g = men_hm_group_make(k >= 0 ? k : men_lay.ne, pl, "NEW GROUP");
        k = men_ent_find(E_APP, pl, ai);
    }
    bool ok = g >= 0 && k >= 0 && men_op_to_group(k, g);
    if (ok) {
        char name[16];
        snprintf(name, sizeof name, "%s", men_lay.g[g].name);
        men_home_save();
        men_lay_load();
        men_hm_after();
        static char t[32];
        snprintf(t, sizeof t, "IN %s", name);
        men_pop(t);
        sfx_ui(SFX_COIN);
    } else {
        men_lay_load();
        men_hm_after();
        men_pop("NOT MOVED");
        sfx_ui(SFX_BACK);
    }
    men_home_close();
}
// MOVE: the edit mode opens with the app (ALL index ai) or the group in the finger
static void hm_act_move(int ai)
{
    lent_t pk = NO_PICK;
    int lev = -1;
    men_hm_materialize();
    if (men_hm_app && ai >= 0) {
        int k = s_hm_from >= 0 && s_hm_from != LV_FAV ? men_ent_find(E_APP, s_hm_from, ai) : -1;
        if (k < 0) {
            k = men_ent_any(E_APP, ai);
        }
        if (k >= 0) {
            pk = men_lay.e[k];
            lev = pk.parent;
        }
    } else if (men_hm_grp >= 0) {
        int k = men_ent_any(E_GRP, men_hm_grp);
        if (k >= 0) {
            pk = men_lay.e[k];
            lev = pk.parent;
        }
    }
    home_edit_open(lev, pk);
}
// DELETE: a question before the app file goes
static void hm_act_del(void)
{
    const tfn_entry_t *e = tfn_entry_of(men_hm_app);
    men_home_close();
    if (!e) {
        return;
    }
    men_hold_app = men_hm_app;
    snprintf(men_hold_id, sizeof men_hold_id, "%s", e->id);
    static char t[48];
    snprintf(t, sizeof t, "DELETE %s?", e->name);
    sysui_ask(t, "THE SAVES STAY", "DELETE", "KEEP", 20, men_inst_remove_answer);
    sysui_ask_closable();
}
static void hm_act(int bi)
{
    if (bi < 0 || bi >= men_nhb) {
        return;
    }
    const hbtn_t *b = &men_hb[bi];
    if (b->off) {
        hm_act_off(b);
        return;
    }
    int ai = men_hm_app ? men_ai_of(men_hm_app) : -1;
    switch (b->act) {
    case HA_OPEN:
        men_home_close();
        sfx_ui(SFX_CLICK);
        if (men_hm_app) {
            app_open(men_hm_app);
        } else if (men_hm_grp >= 0) {
            men_go_level(men_hm_grp);
        }
        break;
    case HA_FAV: {
        bool on = men_fav_toggle(men_hm_app);
        men_home_close();
        sfx_play(on ? SFX_COIN : SFX_BACK);
        men_pop(on ? "ADDED TO FAVORITES" : "REMOVED FROM FAVORITES");
        men_build_items();
        break;
    }
    case HA_GROUP:
        sfx_ui(SFX_TICK);
        hm_open(HM_PICK);
        break;
    case HA_TO:
    case HA_NEWG:
        hm_act_to(b, ai);
        break;
    case HA_MORE:
        men_hm_page++;
        hm_pick_btns();
        sfx_ui(SFX_TICK);
        break;
    case HA_MOVE:
        hm_act_move(ai);
        break;
    case HA_INFO:
        sfx_ui(SFX_TICK);
        hm_open(HM_INFO);
        break;
    case HA_DEL:
        hm_act_del();
        break;
    case HA_HIDE:
        if (ai >= 0) {
            men_op_hide(ai);
            men_home_save();
            men_hm_after();
            men_pop("HIDDEN: EDIT HOME SHOWS IT");
            sfx_play(SFX_CUT);
        }
        men_home_close();
        break;
    case HA_RENAME:
        if (men_hm_grp < 0) {
            break;
        }
        snprintf(s_hm_name, sizeof s_hm_name, "%s", men_lay.g[men_hm_grp].name);
        kbd_open("THE NAME OF THE GROUP", s_hm_name, sizeof s_hm_name, false);
        kbd_set_ok_question("USE THIS NAME?");
        men_hm = HM_NAME;
        break;
    case HA_LOOK:
        if (men_hm_grp >= 0) {
            men_hm_materialize();
            men_lay.g[men_hm_grp].style = (uint8_t)((men_lay.g[men_hm_grp].style + 1) % ST_N);
            men_home_save();
            men_hm_after();
            men_bg_key = -1;
            sfx_ui(SFX_TICK);
        }
        break;
    case HA_APART: {
        if (men_hm_grp < 0) {
            break;
        }
        men_home_close();
        static char t[40];
        snprintf(t, sizeof t, "TAKE %s APART?", men_lay.g[men_hm_grp].name);
        sysui_ask(t, "ITS APPS GO UP ONE LEVEL", "TAKE APART", "KEEP", 20, hm_ask_apart);
        sysui_ask_closable();
        break;
    }
    case HA_ADDG: {
        men_undo_push();
        int g = men_hm_group_make(men_lay.ne, men_hm_lev, "NEW GROUP");
        if (g >= 0) {
            men_hm_norm();
            men_hm_change();
            men_hm_page = men_he_pages() - 1;
            sfx_ui(SFX_COIN);
            men_pop("DRAG APPS INTO IT");
        } else {
            free(men_undo[--men_nundo]);
            sfx_ui(SFX_BACK);
        }
        break;
    }
    case HA_UNDO:
        if (men_nundo > 0) {
            char *t = men_undo[--men_nundo];
            men_hm_reparse(t);
            free(t);
            men_pk_on = false;
            men_hm_change();
            sfx_ui(SFX_BACK);
        }
        break;
    case HA_DONE:
        sfx_ui(SFX_CLICK);
        men_hm_edit_done();
        break;
    default:
        break;
    }
}
// the name of RENAME: the letters of a group name (A-Z, 0-9, a space, - & ! ?)
static void hm_name_done(bool ok)
{
    men_hm = HM_SHEET;
    hm_sheet_btns();
    if (!ok || men_hm_grp < 0) {
        return;
    }
    char n[16];
    int m = 0;
    for (const char *c = s_hm_name; *c && m < 15; c++) {
        char ch = (char)toupper((unsigned char)*c);
        if ((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == ' ' || ch == '-' || ch == '&' || ch == '!' ||
            ch == '?') {
            n[m++] = ch;
        }
    }
    while (m > 0 && n[m - 1] == ' ') {
        m--;
    }
    n[m] = 0;
    if (!m) {
        return;
    }
    men_hm_materialize();
    snprintf(men_lay.g[men_hm_grp].name, sizeof men_lay.g[men_hm_grp].name, "%s", n);
    men_home_save();
    men_hm_after();
    sfx_ui(SFX_COIN);
}

void men_home_close(void)
{
    if (men_hm != HM_OFF) {
        ESP_LOGI("menu", "home: closed");
    }
    men_hm = HM_OFF;
    men_pk_on = false;
    men_ed_touch = men_ed_drag = men_ed_swipe = false;
    men_pressed = -1;
    men_touch = men_drag = false;
    men_hold_done = true;
    men_drawn_reset();
    men_bg_key = -1;
    men_build_items();
    men_spot_level_changed();
    men_hold_done = true;
    if (gfx_round()) {
        men_rtouch = men_rdrag = false;
        men_rhold_done = true;
        men_r_clamp();
        men_rpos = (float)men_rsel;
    }
}
// the PWR key (menu_back): one step back
void men_home_back(void)
{
    if (men_hm == HM_PICK || men_hm == HM_INFO) {
        hm_open(HM_SHEET);
        return;
    }
    if (men_hm == HM_NAME) {
        hm_name_done(false);
        return;
    }
    if (men_hm == HM_EDIT) {
        if (men_pk_on) {
            men_pk_on = false;
            return;
        }
        if (men_hm_lev >= 0) {
            men_hm_lev = men_gparent(men_hm_lev);
            men_hm_page = 0;
            men_ed_pagef = 0;
            hm_edit_refresh();
            return;
        }
        men_hm_edit_done();
        return;
    }
    men_home_close();
}

// ---------------------------------------------------------------- input
int men_hm_btn_at(int x, int y)
{
    for (int i = 0; i < men_nhb; i++) {
        if (men_hb[i].act != HA_NONE && ui_hit(x, y, men_hb[i].x, men_hb[i].y, men_hb[i].w, men_hb[i].h, 3)) {
            return i;
        }
    }
    return -1;
}
static bool hm_on_x(int x, int y)
{
    int bx, by;
    men_hm_xbox(&bx, &by);
    return ui_hit(x, y, bx, by, 44, 44, 4);
}
static void hm_edit_tap(int x, int y)
{
    int b = men_hm_btn_at(x, y);
    if (b >= 0) {
        hm_act(b);
        return;
    }
    int t = he_at(x, y, 4);
    if (men_pk_on) {
        int i = men_he_find_pk();
        if (t < 0) {
            hgrid_t g = men_hm_grid();
            int per = g.cols * g.rows, last = men_hm_page * per + per - 1;
            // a tap after the last tile of the last page: the end; else no place: the pick ends
            if (y >= g.y0 && men_hm_page == men_he_pages() - 1 && last >= men_nhe - 1) {
                hm_drop(-1, false, men_nhe);
                return;
            }
            men_pk_on = false;
            sfx_ui(SFX_BACK);
            return;
        }
        if (t == i) {
            men_pk_on = false;
            sfx_ui(SFX_BACK);
            return;
        }
        int tt = men_he[t];
        bool onto = tt == HT_UP || (tt >= 0 && (men_lay.e[tt].kind == E_GRP || men_lay.e[tt].kind == E_FAV));
        hm_drop(t, onto, t);
        return;
    }
    if (t < 0) {
        return;
    }
    int tt = men_he[t];
    if (tt == HT_UP) {
        men_hm_lev = men_gparent(men_hm_lev);
        men_hm_page = 0;
        men_ed_pagef = 0;
        hm_edit_refresh();
        sfx_ui(SFX_BACK);
        return;
    }
    if (tt == HT_SHOW) {
        men_undo_push();
        men_op_show_all();
        men_hm_change();
        sfx_ui(SFX_COIN);
        men_pop("THE HIDDEN APPS ARE BACK");
        return;
    }
    const lent_t *e = &men_lay.e[tt];
    if (e->kind == E_GRP) {
        men_hm_lev = e->ref;
        men_hm_page = 0;
        men_ed_pagef = 0;
        hm_edit_refresh();
        sfx_ui(SFX_CLICK);
        return;
    }
    if (e->kind == E_FAV) {
        men_pop("DRAG AN APP HERE");
        return;
    }
    s_pk = *e;
    men_pk_on = true;
    sfx_ui(SFX_TICK);
}
static void hm_edit_input(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN:
        men_ed_touch = true;
        men_ed_drag = men_ed_swipe = false;
        men_ed_x0 = men_ed_x = ev->x;
        men_ed_y0 = men_ed_y = ev->y;
        s_ed_t0 = ev->t_ms;
        men_hm_hot = men_hm_btn_at(ev->x, ev->y);
        men_ed_press = men_hm_hot >= 0 ? -1 : he_at(ev->x, ev->y, 4);
        men_ed_secret = false;
        break;
    case IN_MOVE: {
        men_ed_x = ev->x;
        men_ed_y = ev->y;
        int dx = ev->x - men_ed_x0, dy = ev->y - men_ed_y0;
        if (men_hm_hot >= 0 && (abs(dx) > 14 || abs(dy) > 14)) {
            men_hm_hot = -1;
        }
        if (!men_ed_drag && !men_ed_swipe && men_hm_hot < 0 && (abs(dx) > DRAG_PX || abs(dy) > DRAG_PX)) {
            int tt = men_ed_press >= 0 ? men_he[men_ed_press] : -1;
            bool movable = tt >= 0 && men_lay.e[tt].kind != E_REST;
            bool mine = movable && men_pk_on && men_lay.e[tt].kind == s_pk.kind && men_lay.e[tt].ref == s_pk.ref;
            if (movable && (ev->t_ms - s_ed_t0 >= LIFT_MS || mine)) {
                men_ed_drag = true;
                s_pk = men_lay.e[tt];
                men_pk_on = true;
                men_ed_hover = -1;
                men_ed_merge = false;
                s_ed_edge_t = 0;
                sfx_ui(SFX_TICK);
            } else {
                men_ed_swipe = true;
            }
            input_tap_cancel();
        }
        if (men_ed_drag) {
            int t = he_at(ev->x, ev->y, 0), i = men_he_find_pk();
            int tx, ty, ts;
            bool inner = false;
            if (t >= 0 && t != i) {
                men_he_rect(t, &tx, &ty, &ts);
                inner = abs(ev->x - (tx + ts / 2)) < ts * 32 / 100 && abs(ev->y - (ty + ts / 2)) < ts * 36 / 100;
            }
            if (inner && men_he[t] != HT_SHOW) {
                if (t != men_ed_hover) {
                    men_ed_hover = t;
                    s_ed_hover_t = ev->t_ms;
                    men_ed_merge = false;
                }
            } else {
                men_ed_hover = -1;
                men_ed_merge = false;
            }
        }
        break;
    }
    case IN_LONG: {
        // a still hold on a tile lifts it too (a drag with no move first)
        int tt = men_ed_press >= 0 ? men_he[men_ed_press] : -1;
        if (!men_ed_drag && !men_ed_swipe && men_hm_hot < 0 && tt >= 0) {
            men_ed_drag = true;
            s_pk = men_lay.e[tt];
            men_pk_on = true;
            men_ed_hover = -1;
            men_ed_merge = false;
            s_ed_edge_t = 0;
            sfx_ui(SFX_TICK);
        }
        break;
    }
    case IN_UP:
        men_ed_touch = false;
        if (men_ed_drag) {
            int slot = men_he_slot(ev->x, ev->y);
            hm_drop(men_ed_merge ? men_ed_hover : -1, men_ed_merge, slot);
            men_ed_drag = false;
            men_ed_hover = -1;
            men_ed_merge = false;
            men_ed_press = -1;
            men_hm_hot = -1;
            break;
        }
        if (men_ed_swipe) {
            int dx = ev->x - men_ed_x0, pages = men_he_pages();
            if (men_hm_wide() || abs(dx) > abs(ev->y - men_ed_y0)) {
                if (dx < -36 && men_hm_page < pages - 1) {
                    men_hm_page++;
                    sfx_ui(SFX_TICK);
                } else if (dx > 36 && men_hm_page > 0) {
                    men_hm_page--;
                    sfx_ui(SFX_TICK);
                }
            }
            men_ed_swipe = false;
        }
        men_ed_press = -1;
        break;
    case IN_TAP:
        if (men_ed_drag || men_ed_swipe) {
            break;
        }
        men_hm_hot = -1;
        hm_edit_tap(ev->x, ev->y);
        break;
    default:
        break;
    }
}
void men_home_input(const input_event_t *ev)
{
    if (men_hm == HM_NAME) {
        int r = kbd_input(ev);
        if (r != KBD_NONE) {
            hm_name_done(r == KBD_OK);
        }
        return;
    }
    if (ev->kind == IN_DOWN) {
        men_hm_skip_tap = false;
    } else if (ev->kind == IN_TAP && men_hm_skip_tap) {
        men_hm_skip_tap = false;
        return;
    }
    if (men_hm == HM_EDIT) {
        hm_edit_input(ev);
        return;
    }
    switch (ev->kind) {
    case IN_DOWN:
        men_hm_hot = men_hm_btn_at(ev->x, ev->y);
        break;
    case IN_MOVE:
        if (men_hm_hot >= 0 && men_hm_btn_at(ev->x, ev->y) != men_hm_hot) {
            men_hm_hot = -1;
        }
        break;
    case IN_TAP: {
        men_hm_hot = -1;
        if (hm_on_x(ev->x, ev->y)) {
            sfx_ui(SFX_BACK);
            men_home_back();
            break;
        }
        if (men_hm == HM_INFO) {
            sfx_ui(SFX_BACK);
            hm_open(HM_SHEET);
            break;
        }
        int b = men_hm_btn_at(ev->x, ev->y);
        if (b >= 0) {
            hm_act(b);
        } else {
            // a tap out of the buttons (and out of the app): a close (a way out of the context menu)
            int ix, iy, is, bx, by, bw, bh, c, r;
            men_hm_sheet_geo(&ix, &iy, &is, &bx, &by, &bw, &bh, &c, &r);
            if (!ui_hit(ev->x, ev->y, bx, by, bw, bh, 0)) {
                sfx_ui(SFX_BACK);
                men_home_back();
            }
        }
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------- update
// the wheel (the Watcher): the targets in order (the buttons, then the tiles of the page); a push taps the one in focus
int men_hm_targets(int *cx, int *cy, int max)
{
    int n = 0;
    for (int i = 0; i < men_nhb && n < max; i++) {
        if (men_hb[i].act != HA_NONE) {
            cx[n] = men_hb[i].x + men_hb[i].w / 2;
            cy[n] = men_hb[i].y + men_hb[i].h / 2;
            n++;
        }
    }
    if (men_hm == HM_EDIT) {
        int per = he_per();
        for (int i = men_hm_page * per; i < men_nhe && i < (men_hm_page + 1) * per && n < max; i++) {
            int x, y, t;
            men_he_rect(i, &x, &y, &t);
            cx[n] = x + t / 2;
            cy[n] = y + t / 2;
            n++;
        }
    }
    return n;
}
void men_home_update(int64_t now, float dt)
{
    if (men_hm == HM_NAME) {
        return;
    }
    int d = input_wheel(), pr = input_wheel_presses();
    if (men_hm == HM_INFO && pr) {
        hm_open(HM_SHEET);
        return;
    }
    if (d || pr) {
        int cx[HB_MAX + 40], cy[HB_MAX + 40], n = men_hm_targets(cx, cy, HB_MAX + 40);
        if (n) {
            if (d) {
                men_hm_focus = ((men_hm_focus < 0 ? 0 : men_hm_focus + d) % n + n) % n;
                sfx_ui(SFX_TICK);
            } else if (men_hm_focus >= 0 && men_hm_focus < n) {
                if (men_hm == HM_EDIT) {
                    hm_edit_tap(cx[men_hm_focus], cy[men_hm_focus]);
                } else {
                    int b = men_hm_btn_at(cx[men_hm_focus], cy[men_hm_focus]);
                    if (b >= 0) {
                        hm_act(b);
                    }
                }
                if (men_hm_focus >= men_hm_targets(cx, cy, HB_MAX + 40)) {
                    men_hm_focus = -1;
                }
            }
        }
    }
    if (men_hm != HM_EDIT) {
        return;
    }
    // the pages slide
    float k = 1 - expf(-dt * 12);
    men_ed_pagef += (men_hm_page - men_ed_pagef) * k;
    if (fabsf(men_hm_page - men_ed_pagef) < 0.002f) {
        men_ed_pagef = (float)men_hm_page;
    }
    // no SETUP in the layout: the hold of 8 s that opened the edit mode still opens SETUP (the way back to SETUP)
    if (men_ed_secret && men_ed_touch && !men_ed_drag && !men_ed_swipe && now - s_ed_t0 >= SECRET_MS) {
        men_ed_secret = false;
        men_undo_clear();
        men_home_close();
        men_lay_load();
        men_hm_after();
        sfx_ui(SFX_CLICK);
        app_open(&app_settings);
        return;
    }
    if (men_ed_drag) {
        if (men_ed_hover >= 0 && !men_ed_merge && now - s_ed_hover_t >= MERGE_MS) {
            men_ed_merge = true;
            sfx_tone(880, 990, 40, W_SINE, 30);
        }
        // at an edge: the next page
        int ax0, ay0, ax1, ay1, dir = 0;
        men_hm_area(&ax0, &ay0, &ax1, &ay1);
        if (men_ed_x < ax0 + 26) {
            dir = -1;
        } else if (men_ed_x > ax1 - 26) {
            dir = 1;
        }
        if (dir != men_ed_edge_dir) {
            men_ed_edge_dir = dir;
            s_ed_edge_t = now;
        }
        if (dir && now - s_ed_edge_t >= EDGE_MS) {
            int p = men_hm_page + dir;
            if (p >= 0 && p < men_he_pages()) {
                men_hm_page = p;
                sfx_ui(SFX_TICK);
            }
            s_ed_edge_t = now;
        }
    }
}

// ---------------------------------------------------------------- the menu: the holds, a new profile
// a hold on an item of the menu (spot and round): an app or a group: the sheet; true when it took the hold
bool men_home_hold_item(int i, int level)
{
    if (i < 0 || i >= men_n_items) {
        return false;
    }
    const item_t *it = &men_items[i];
    if (it->kind == IT_APP) {
        home_sheet_app(it->app, level);
        return true;
    }
    if (it->kind == IT_CAT && it->cat != LV_FAV) {
        men_home_sheet_group(it->cat);
        return true;
    }
    return false;
}
// a hold on the background: the edit mode (still held 8 s with no SETUP in the layout: SETUP opens, as before)
void men_home_hold_bg(int64_t down_t, int x, int y)
{
    bool secret = men_level < 0 && !men_setup_in_menu();
    home_edit_open(men_level, NO_PICK);
    men_hm_skip_tap = true;
    men_ed_touch = true;
    s_ed_t0 = down_t;
    men_ed_x0 = men_ed_x = x;
    men_ed_y0 = men_ed_y = y;
    men_ed_press = -1;
    men_ed_secret = secret;
}
static void home_ask_answer(int a)
{
    if (a == 1) {
        menu_home_drop();
        ESP_LOGI("menu", "home: the new profile of the manager");
    } else if (a == 0) {
        men_home_save();
        ESP_LOGI("menu", "home: kept (on the new profile)");
    } else {
        men_home_later = true;
    }
}
// each update of the menu (not in a screen of HOME 2.0): the question of a new profile
void men_home_menu_tick(void)
{
    if (!men_home_ask || sysui_active()) {
        return;
    }
    men_home_ask = false;
    sysui_ask("A NEW HOME FROM YOUR MANAGER", "USE IT, OR KEEP YOUR OWN HOME?", "USE IT", "KEEP MINE", 30,
              home_ask_answer);
    sysui_ask_closable();
}
