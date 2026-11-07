// The private header of the Tab5 menu: the files menu_tab5.c and menu_tab5_draw.c include it (only they).
// Until P3 R8 the Tab5 menu was one file (menu_tab5.c).
#pragma once

#include "app_menu_priv.h"

#if BOARD_UI_TABLET
#include <time.h>
#include "alarm.h"
#include "tz.h"
#include "../libs/kscroll/kscroll.h"

#define TB_LAUNCH_MS 170  // the open of an app: the icon grows, then the app opens
#define TB_SLIDE_MS  240  // a new tab: its grid slides in
#define TB_ROOMS     3    // the rooms (the backgrounds) that stay in PSRAM (each 460 KB)
typedef struct {
    bool land;
    int rail_w;              // the column of the tabs (all the height)
    int tab_h, tab_y0, sym;  // a tab: its height, the top of the first tab, the size of its symbol tile
    int top;                 // the top of the grid (the status line and the title are above it)
    int gx0, gx1;            // the grid: x
    int cols, cw, ch, icon;  // the cells and the icon in a cell
} tlay_t;
#define TB_SB_H(L) ((L)->land ? 46 : 34)  // the status line
enum {
    TB_HOME,
    TB_FAV,
    TB_NEW,
    TB_GRP
};
typedef struct {
    uint8_t kind;
    int16_t lev;
} ttab_t;  // lev: the level of the tab (-1, LV_FAV, LV_NEW, a group)
enum {
    TT_NONE,
    TT_GRID,
    TT_RAIL,
    TT_BACK
};

// the shared functions and data of the files
// (menu_tab5.c)
tlay_t men_tb_lay(void);
extern ttab_t men_tbt[G_MAX + 4];
extern int men_ntbt;
extern int men_tb_new_n;
int men_tb_tab_of(int level);
int men_tb_tab_look(const ttab_t *t);
const char *men_tb_tab_name(const ttab_t *t);
int men_tb_look(void);
extern int16_t men_tbv[N_CAP + G_MAX + 4];
extern int men_ntbv;
extern kscroll_t men_tb_ks, men_tb_rks;
extern float men_tb_hl;
extern int64_t men_tb_sw_t;
extern int men_tb_sw_dir;
extern int men_tb_t;
extern int men_tb_pi, men_tb_ptab;
extern bool men_tb_back_hot, men_tb_hold_done, men_tb_moved;
extern int64_t men_tb_down_t;
extern int men_tb_ai;
extern float men_tb_as, men_tb_av;
extern int men_tb_li;
extern const app_t *men_tb_lapp;
extern int64_t men_tb_lt;
extern int men_tb_lx, men_tb_ly, men_tb_ls;
extern char men_tb_clock[16];
void men_tb_cell(const tlay_t *L, int k, float scroll, int *x, int *y);
bool men_tb_back_box(const tlay_t *L, int *x, int *y, int *w, int *h);
extern int men_tb_kf;
bool men_tb_kf_on(void);
#endif  // BOARD_UI_TABLET
