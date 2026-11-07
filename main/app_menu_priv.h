// The private header of the menu (the app MENU): the files app_menu.c, menu_look.c, menu_tree.c, menu_round.c,
// menu_spot.c, menu_home*.c (through menu_home_priv.h) and menu_tab5*.c (through menu_tab5_priv.h) include it (only
// they). Until P3 R8 the menu was one unit (app_menu.c and its .inc parts).
#pragma once

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "esp_random.h"
#include "app.h"
#include "audio.h"
#include "board.h"
#include "tfn_app.h"
#include "tfn_store.h"
#include "sysui.h"
#include "notify.h"
#include "kfont.h"
#include "uikit.h"
#include "kbd.h"
#include "gfx.h"
#include "settings.h"
#include "storage.h"
#include "imu.h"
#include "wifi.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mfont.h"

#define ICON         84
#define LABEL_H      16  // the name under an icon (scale 2 text is 14 px high)
#define BAR_H        28  // portrait: the top bar (page dots, battery)
#define FOOT_H       18  // portrait: the help line at the bottom
#define LAND_FOOT    30  // landscape: the help line and the battery at the bottom
#define N_STARS      40
#define DRAG_PX      10      // finger travel that starts a scroll (same as the tap limit)
#define STEP_PX      24      // a drag of this much (about 3 mm) goes to the next page
#define FLICK        150.0f  // px/s: a faster swipe also goes to the next page
#define HOLD_MS      550     // hold an app icon this long: favorite on / off
#define HOLD_SHOW_MS 150     // the hold bar shows after this time
#define FAV_MAX      16      // the most favorites
#define POP_MS       1300
#define G_MAX        32    // the most groups in a layout (menu_tree.c)
#define SECRET_MS    8000  // no SETUP in the layout: a hold this long on an empty place of the top level opens it

// The groups of the built-in layout (menu_tree.c lay_default: the group index = CAT_*). The manager page also
// gets them (menu_list).
enum {
    CAT_GAMES,
    CAT_UTIL,
    CAT_SETTINGS,
    N_CATS
};

// the count of the built-in apps of this build (the entries of BUILT in app_menu.c: all groups but SYSTEM)
#define MEN_IN_GAMES    +1
#define MEN_IN_UTIL     +1
#define MEN_IN_SETTINGS +1
#define MEN_IN_SYSTEM
enum {
    N_BUILT = 0
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) MEN_IN_##group
#include ARCADE_APPS_LIST
#undef ARCADE_APP
};
typedef struct {
    const app_t *app;
    uint8_t cat;
} aent_t;
#define N_CAP (N_BUILT + TFN_MAX)
#define N_ALL men_nall

// One tile on the screen.
enum {
    IT_APP,
    IT_CAT,
    IT_BACK
};
typedef struct {
    uint8_t kind;
    uint8_t cat;       // IT_CAT, IT_BACK: the group (an index of the layout, or LV_FAV)
    const app_t *app;  // IT_APP
} item_t;

typedef struct {
    int16_t x, y;
    uint8_t speed, bright;
} men_star_t;  // (s_stars)

#if BOARD_UI_TABLET
// THE TAB5 MENU (menu_tab5.c). LV_NEW: the virtual group of the new apps (its tab, NEW).
#define LV_NEW (G_MAX + 1)
#endif

// ---------------------------------------------------------------- the tree of the groups (menu_tree.c)
#define LV_FAV G_MAX        // the level of the favorites (a group with no entry in the layout)
#define E_APPS (2 * N_CAP)  // the most app lines of a layout (the copies too)
#define E_MAX  (E_APPS + G_MAX + N_CATS + 2)
#define V_MAX  (E_MAX + N_CAP)  // the view of a layout: each rest line is its apps
#ifndef MENU_FILE
#define MENU_FILE "/data/menu.txt"
#endif
#ifndef HOME_FILE
#define HOME_FILE "/data/home.txt"
#endif
#define MENU_TEXT_MAX 4096
// The looks of a group (a colour pair and a symbol: cat_colors, cat_symbol in menu_look.c). 0 .. 23 come in a layout;
// ST_TOP is the look of the main menu (only the BACK tile that goes to it).
enum {
    ST_GAMES,
    ST_UTIL,
    ST_SETTINGS,
    ST_STAR,
    ST_QUEST,
    ST_MUSIC,
    ST_KIDS,
    ST_ACTION,  // 0 .. 7
    ST_SILLY,
    ST_DUO,
    ST_PARTY,
    ST_RETRO,
    ST_PUZZLE,
    ST_BOARD,
    ST_SPORTS,
    ST_RACING,  // 8 .. 15
    ST_TILT,
    ST_SEA,
    ST_CALM,
    ST_SPACE,
    ST_LEARN,
    ST_TALK,
    ST_HEART,
    ST_NEW,  // 16 .. 23
    ST_N,
    ST_TOP = ST_N
};
enum {
    FAVM_GROUP,
    FAVM_TOP,
    FAVM_OFF
};
enum {
    E_APP,
    E_GRP,
    E_FAV,
    E_REST
};
typedef struct {
    char name[16];
    int8_t parent;
    uint8_t style;
} lgrp_t;  // parent: -1 = the main menu
typedef struct {
    uint8_t kind;
    int8_t parent;
    int16_t ref;
} lent_t;  // ref: the ALL index (E_APP), the group
           // (E_GRP), the built-in group CAT_* (E_REST)
typedef struct {
    char prof[24];
    char start[16];
    uint8_t fav, ng;
    uint16_t ne, cut;
    lgrp_t g[G_MAX];
    lent_t e[E_MAX];
    uint32_t hid[(N_CAP + 31) / 32];
} layout_t;  // hid: the hidden apps (ALL index bits)

// ---------------------------------------------------------------- colours (native RGB565) (menu_spot.c)
static inline uint16_t nat(int r, int g, int b)
{
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3));
}
static inline uint16_t lcd(uint16_t c)
{
    return (uint16_t)((c >> 8) | (c << 8));
}
static inline uint16_t nmix(uint16_t d, uint16_t s, int a)  // a 0..255
{
    uint32_t a5 = ((uint32_t)a + 4) >> 3;
    uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81F, ss = (s | ((uint32_t)s << 16)) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * a5) >> 5)) & 0x07E0F81F;
    return (uint16_t)(r | (r >> 16));
}
static inline void pix(int x, int y, uint16_t c, int a)  // c native, a 0..255
{
    if ((unsigned)x >= (unsigned)gfx_w || (unsigned)y >= (unsigned)gfx_h || a <= 0) {
        return;
    }
    uint16_t *p = &g_fb[y * gfx_w + x];
    *p = lcd(a >= 255 ? c : nmix(lcd(*p), c, a));
}

#ifndef ESP_PLATFORM
extern int g_menu_dtest;  // host tests only (menu_spot.c)
#endif

// ---------------------------------------------------------------- the shared functions and data of the files
// (app_menu.c: the list of the apps, the favorites, the items of a level, the state, the app MENU)
extern aent_t men_all[N_CAP];
extern int men_nall;
const char *men_all_name(int i);
void men_all_build(void);
bool men_builtin_new(const app_t *a);
bool men_tfn_new(const tfn_entry_t *e);
extern item_t men_items[N_CAP + G_MAX + 4];
extern int men_n_items;
extern int men_level;
extern int men_cat_count[G_MAX + 1];
extern float men_pos;
extern int men_sel;
extern int men_sel_of[G_MAX + 2];
extern float men_vel;
extern bool men_touch, men_drag;
extern int men_a0, men_last_a;
extern int men_x0, men_y0;
extern bool men_vsw;
extern int64_t men_vsw_at;
extern int men_vsw_dir;
extern int64_t men_bump_at;
extern float men_pos0;
extern int64_t men_last_t, men_prev, men_now;
extern int men_pressed;
extern int64_t men_down_t;
extern bool men_hold_done;
extern char men_pop_text[32];
extern int64_t men_pop_at;
extern float men_tilt, men_tilt_base, men_glow_t;
extern int men_shown;
extern int men_was_w, men_was_h;
extern men_star_t men_stars[N_STARS];
int men_fav_index(const app_t *a);
bool men_fav_toggle(const app_t *a);
bool men_can_run(int i);
extern bool men_home_later;
void men_add_app(const app_t *a);
bool men_in_items(const app_t *a);
void men_build_items(void);
void men_go_level(int level);
void men_pop(const char *t);
extern const app_t *men_hold_app;
extern char men_hold_id[24];
void men_inst_remove_answer(int a);
void men_open_item(int i);
// (menu_look.c: the group looks (colours, symbols), the group tile, the status line, the badge)
void men_fill_poly(const float *p, int n, uint16_t c);
void men_star(float cx, float cy, float r, uint16_t c);
void men_cat_colors(int st, uint8_t *top, uint8_t *bot);
uint16_t men_grad(const uint8_t *a, const uint8_t *b, int k, int n);
void men_tile_set(const uint8_t *a, const uint8_t *b, int y, int s, int step);
void men_cat_symbol(int cat, int x, int y, int s, uint16_t bg, float tm);
void men_level_path(int g, bool up, char *o, int n, bool (*fits)(const char *));
void men_back_to(char *o, int n);
void men_draw_cat_tile(int cat, int x, int y, int s, bool pressed, bool back, bool live);
int men_draw_wifi(int x, int y);
int men_draw_battery(int x, int y);
int men_draw_status(int x, int y);
void men_badge(const app_t *a, int x, int y, int s);
// (menu_tree.c: the tree of the groups (the layout of a profile, the own home))
extern layout_t men_lay;
bool men_lay_hid(const layout_t *L, int i);
extern bool men_home;
extern uint32_t men_home_base;
extern volatile bool men_home_ask;
extern lent_t men_view[V_MAX];
extern int men_nview;
extern volatile bool men_lay_reload;
extern char men_prof_name[24];
extern uint32_t men_prof_hash;
extern const char *const men_cat_name[N_CATS];
void *men_ps_alloc(size_t n);
bool men_lay_parse(const char *t, int len, layout_t *L, char *err, int emax);
int men_lay_text(const layout_t *L, char *o, int max, bool home, uint32_t base);
void men_lay_view(void);
void men_lay_load(void);
bool men_fav_on(void);
int men_gparent(int g);
int men_gstyle(int g);
const char *men_gname(int g);
int men_gstyle_up(int g);
bool men_g_under(int p, int g);
void men_count_groups(void);
bool men_setup_in_menu(void);
// (menu_round.c: the round menu (the Watcher, the 1.75C))
extern float men_rpos;
extern int men_rsel;
extern bool men_rtouch, men_rdrag;
extern bool men_rhold_done;
void men_r_clamp(void);
void men_r_level_changed(int from_level);
void men_r_enter(void);
void men_round_input(const input_event_t *ev);
void men_round_update(int64_t now, float dt);
void men_round_render(int64_t now);
// (menu_spot.c: the Spotlight menu (the other boards))
void men_drawn_reset(void);
int men_mtext_w(const mfont_t *f, const char *s, int spacing);
void men_mtext(const mfont_t *f, int x, int base, const char *s, uint16_t col, int alpha, int spacing);
void men_mtext_c(const mfont_t *f, int cx, int base, const char *s, uint16_t col, int alpha, int spacing);
int men_rr_cover(int px, int py, int x, int y, int w, int h, int r);
void men_rr_fill(int x, int y, int w, int h, int r, uint16_t c, int a);
void men_soft_ellipse(int cx, int cy, int rx, int ry, uint16_t col, int amax);
extern int men_bgw, men_bgh, men_bg_key;
void men_rooms_free(void);
int men_level_slot(void);
void men_spot_clamp(void);
void men_spot_level_changed(void);
void men_spot_input(const input_event_t *ev);
void men_spot_update(int64_t now, float dt);
void men_group_tile(int cat, int x, int y, int s, bool back, bool live);
bool men_is_new(const app_t *a);
const char *men_item_name(const item_t *it);
void men_spot_render(int64_t now);
// (menu_home.c, menu_home_ops.c, menu_home_draw.c: HOME 2.0 (the sheet, the edit mode))
bool men_home_active(void);
bool men_home_save(void);
void men_home_sheet_group(int g);
void men_home_close(void);
void men_home_back(void);
void men_home_input(const input_event_t *ev);
void men_home_update(int64_t now, float dt);
void men_home_render(int64_t now);
void men_home_gen_changed(void);
void men_home_test_edit(int64_t now);
void men_home_leave(void);
bool men_home_hold_item(int i, int level);
void men_home_hold_bg(int64_t down_t, int x, int y);
void men_home_menu_tick(void);
// (menu_tab5.c, menu_tab5_draw.c: the Tab5 menu)
#if BOARD_UI_TABLET
void men_tb_new_items(void);
void men_tb_rooms_free(void);
void men_tb_level_changed(int from);
bool men_tb_back(void);
void men_tb_reveal(int i);
void men_tb_enter(int64_t now);
void men_tb_input(const input_event_t *ev);
void men_tb_update(int64_t now, float dt);
void men_tb_redraw_all(void);
void men_tb_render(int64_t now);
#endif
