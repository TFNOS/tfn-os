// The private header of HOME 2.0 of the menu: the files menu_home.c, menu_home_ops.c and menu_home_draw.c include it
// (only they). Until P3 R8 HOME 2.0 was one file (menu_home.c).
#pragma once

#include "app_menu_priv.h"

enum {
    HM_OFF,
    HM_SHEET,
    HM_PICK,
    HM_INFO,
    HM_EDIT,
    HM_NAME
};
enum {
    HA_NONE,
    HA_OPEN,
    HA_FAV,
    HA_GROUP,
    HA_MOVE,
    HA_INFO,
    HA_DEL,
    HA_HIDE,
    HA_LOCK,
    HA_NOHIDE,
    HA_RENAME,
    HA_LOOK,
    HA_APART,
    HA_TO,
    HA_MORE,
    HA_NEWG,
    HA_ADDG,
    HA_UNDO,
    HA_DONE,
    HA_CLOSE
};
typedef struct {
    int x, y, w, h, act, arg;
    const char *t;
    bool off, red;
} hbtn_t;
#define HB_MAX   10
#define HE_MAX   (N_CAP + G_MAX + 4)
#define HT_UP    (-2)  // the special tiles of the edit mode (not entries of s_lay)
#define HT_SHOW  (-3)
#define UNDO_MAX 8
#define MERGE_MS 400  // a dragged app over another app this long: a new group
#define EDGE_MS  600  // a dragged tile at the edge this long: the next page
#define LIFT_MS  140  // a move this soon after the press is a swipe of the page, later a drag of the tile
typedef struct {
    int x0, y0, cols, rows, t, gx, gy, lab, bar_y, bar_h;
} hgrid_t;

// the shared functions and data of the files
// (menu_home.c)
extern int men_hm;
extern const app_t *men_hm_app;
extern int men_hm_grp;
extern int men_hm_page;
extern int men_hm_hot;
extern int men_hm_focus;
extern hbtn_t men_hb[HB_MAX];
extern int men_nhb;
extern char *men_undo[UNDO_MAX];
extern int men_nundo;
extern bool men_hm_skip_tap;
extern int men_hm_lev;
extern int16_t men_he[HE_MAX];
extern int men_nhe;
extern bool men_ed_touch, men_ed_drag, men_ed_swipe, men_ed_secret;
extern int men_ed_x0, men_ed_y0, men_ed_x, men_ed_y, men_ed_press;
extern int men_ed_hover, men_ed_edge_dir;
extern bool men_ed_merge;
extern bool men_pk_on;
extern float men_ed_pagef;
void men_hm_area(int *x0, int *y0, int *x1, int *y1);
bool men_hm_wide(void);
void men_hm_xbox(int *x, int *y);
void men_hm_sheet_geo(int *ix, int *iy, int *is, int *bx, int *by, int *bw, int *bh, int *cols, int *rows);
bool men_hm_required(const app_t *a);
hgrid_t men_hm_grid(void);
int men_he_pages(void);
void men_he_rect(int i, int *x, int *y, int *t);
int men_he_slot(int x, int y);
int men_he_find_pk(void);
void men_hm_change(void);
void men_hm_edit_done(void);
int men_hm_btn_at(int x, int y);
int men_hm_targets(int *cx, int *cy, int max);
// (menu_home_ops.c)
int men_ai_of(const app_t *a);
int men_ent_find(int kind, int parent, int ref);
int men_ent_any(int kind, int ref);
void men_hm_materialize(void);
void men_hm_norm(void);
bool men_hm_has_slot(void);
int men_hm_group_make(int at, int p, const char *base);
void men_op_move(int k, int before);
bool men_op_to_group(int k, int g);
void men_op_out(int k);
int men_op_new_group(int kd, int kt);
int men_hid_count(void);
void men_op_hide(int ai);
void men_op_show_all(void);
void men_op_apart(int g);
void men_hm_drop_empty(void);
void men_hm_after(void);
int men_hm_group_by_name(const char *n);
void men_hm_reparse(const char *t);
char *men_hm_text_now(void);
void men_undo_push(void);
void men_undo_clear(void);
