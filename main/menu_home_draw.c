// The menu: HOME 2.0, the drawing (the sheet, the picker, INFO, the edit mode).
#include "menu_home_priv.h"
#include "tfn/theme.h"

// ---------------------------------------------------------------- drawing
static void hm_bg(int look)
{
    uint8_t a[3], b[3];
    men_cat_colors(look, a, b);
    for (int y = 0; y < gfx_h; y += 2) {
        int k = y * 255 / (gfx_h > 1 ? gfx_h - 1 : 1);
        int r = (a[0] * (255 - k) + b[0] * k) / 255 / 5, g = (a[1] * (255 - k) + b[1] * k) / 255 / 5,
            bl = (a[2] * (255 - k) + b[2] * k) / 255 / 5;
        gfx_fill_rect(0, y, gfx_w, gfx_h - y < 2 ? gfx_h - y : 2, rgb(r + 8, g + 8, bl + 14));
    }
}
// a text in a width: the face that fits (else cut with ..)
static const mfont_t *hm_font(const char *s, int w, const mfont_t *first)
{
    if (first == &MF_LABEL && men_mtext_w(&MF_LABEL, s, 1) <= w) {
        return &MF_LABEL;
    }
    if (first != &MF_TINY && men_mtext_w(&MF_SMALL, s, 1) <= w) {
        return &MF_SMALL;
    }
    return &MF_TINY;
}
static void hm_text_fit(const mfont_t *f, int cx, int base, const char *s, int w, uint16_t col, int a)
{
    char t[40];
    snprintf(t, sizeof t, "%s", s);
    int n = (int)strlen(t);
    while (n > 2 && men_mtext_w(f, t, 1) > w) {
        t[--n] = 0;
        t[n - 1] = '.';
    }
    men_mtext_c(f, cx, base, t, col, a, 1);
}
static int s_hm_fbtn = -1;  // the button of the wheel focus (home_render)
static void hm_text_left(const mfont_t *f, int x, int base, const char *s, int w, uint16_t col)
{
    char t[40];
    snprintf(t, sizeof t, "%s", s);
    int n = (int)strlen(t);
    while (n > 2 && men_mtext_w(f, t, 1) > w) {
        t[--n] = 0;
        t[n - 1] = '.';
    }
    men_mtext(f, x, base, t, col, 255, 1);
}
static void hm_draw_btn(const hbtn_t *b, int i)
{
    if (b->act == HA_NONE) {
        return;
    }
    bool hot = men_hm_hot == i, foc = s_hm_fbtn == i;
    uint16_t fill = b->off              ? lcd(THEME_MENU_HM_BTN_OFF)
                    : b->red            ? (hot ? lcd(THEME_MENU_HM_BTN_RED_HOT) : lcd(THEME_MENU_HM_BTN_RED))
                    : b->act == HA_DONE ? (hot ? lcd(THEME_MENU_HM_BTN_DONE_HOT) : lcd(THEME_MENU_HM_BTN_DONE))
                                        : (hot ? lcd(THEME_MENU_HM_BTN_HOT) : lcd(THEME_MENU_HM_BTN));
    men_rr_fill(b->x, b->y, b->w, b->h, 12, fill, 255);
    if (foc) {
        men_rr_fill(b->x - 3, b->y - 3, b->w + 6, 3, 1, lcd(THEME_MENU_HM_FOCUS), 255);
        men_rr_fill(b->x - 3, b->y + b->h, b->w + 6, 3, 1, lcd(THEME_MENU_HM_FOCUS), 255);
    }
    bool st = b->act == HA_FAV && men_fav_index(men_hm_app) < 0 && !b->off;
    int so = st ? 12 : 0;  // (the star at the left: the words move right)
    const mfont_t *f = hm_font(b->t, b->w - 14 - 2 * so, &MF_LABEL);
    uint16_t col = b->off ? lcd(THEME_MENU_HM_BTN_TEXT_OFF) : lcd(THEME_MENU_HM_BTN_TEXT);
    hm_text_fit(f, b->x + b->w / 2 + so, b->y + b->h / 2 + f->asc / 2, b->t, b->w - 10 - 2 * so, col, 255);
    if (b->act == HA_LOCK) {  // a small lock
        int lx = b->x + b->w - 22, ly = b->y + 8;
        gfx_fill_rect(lx, ly + 7, 14, 10, THEME_MENU_HM_LOCK);
        gfx_fill_rect(lx + 3, ly, 2, 8, THEME_MENU_HM_LOCK);
        gfx_fill_rect(lx + 9, ly, 2, 8, THEME_MENU_HM_LOCK);
        gfx_fill_rect(lx + 3, ly, 8, 2, THEME_MENU_HM_LOCK);
    }
    if (st) {
        men_star(b->x + 14, b->y + b->h / 2, 7, THEME_MENU_FAV);
    }
}
static void hm_draw_x(void)
{
    int bx, by;
    men_hm_xbox(&bx, &by);
    men_rr_fill(bx, by, 44, 44, 22, lcd(THEME_MENU_HM_X), 255);
    for (int k = -9; k <= 9; k++) {
        for (int w = -1; w <= 1; w++) {
            pix(bx + 22 + k + w, by + 22 + k, lcd(THEME_MENU_HM_X_MARK), 255);
            pix(bx + 22 + k + w, by + 22 - k, lcd(THEME_MENU_HM_X_MARK), 255);
        }
    }
}
// an app icon in a box (clipped; a star for a favorite)
static void hm_icon(const app_t *a, int x, int y, int s)
{
    gfx_clip_box(x, y, x + s, y + s);
    a->draw_icon(x, y, s);
    gfx_clip_box_off();
    gfx_clip_reset();
    if (men_fav_index(a) >= 0) {
        int st = s / 9 + 4;
        men_star(x + s - st - 2, y + st + 2, st, THEME_MENU_FAV);
    }
    men_badge(a, x, y, s);
}
static void hm_info_lines(char l[6][40], int *n)
{
    *n = 0;
    const tfn_entry_t *e = men_hm_app ? tfn_entry_of(men_hm_app) : NULL;
    if (e) {
        snprintf(l[(*n)++], 40, "VERSION %s", e->ver);
        snprintf(l[(*n)++], 40, "%lu KB ON THE %s", (unsigned long)((e->bytes + 1023) / 1024),
                 e->place == TFN_CARD ? "CARD" : "BOARD");
        snprintf(l[(*n)++], 40, "INSTALLED FROM THE STORE");
        if (men_hm_required(men_hm_app)) {
            snprintf(l[(*n)++], 40, "THE MANAGER KEEPS IT");
        }
    } else if (men_hm_app) {
        int ai = men_ai_of(men_hm_app);
        snprintf(l[(*n)++], 40, "BUILT IN");
        snprintf(l[(*n)++], 40, "GROUP %s", ai >= 0 ? men_cat_name[men_all[ai].cat] : "-");
    }
    if (men_hm_app && men_fav_index(men_hm_app) >= 0) {
        snprintf(l[(*n)++], 40, "A FAVORITE");
    }
    if (men_home) {
        snprintf(l[(*n)++], 40, "YOUR OWN HOME");
    } else if (men_prof_name[0]) {
        snprintf(l[(*n)++], 40, "PROFILE %.20s", men_prof_name);
    }
}
static void hm_render_sheet(int64_t now)
{
    (void)now;
    int look = men_hm_grp >= 0 ? men_gstyle(men_hm_grp) : men_level >= 0 ? men_gstyle(men_level) : ST_GAMES;
    hm_bg(look);
    int ix, iy, is, bx, by, bw, bh, cols, rows;
    men_hm_sheet_geo(&ix, &iy, &is, &bx, &by, &bw, &bh, &cols, &rows);
    if (men_hm_app) {
        hm_icon(men_hm_app, ix, iy, is);
    } else if (men_hm_grp >= 0) {
        men_group_tile(men_hm_grp, ix, iy, is, false, true);
    }
    const char *name = men_hm_app ? men_hm_app->name : men_hm_grp >= 0 ? men_lay.g[men_hm_grp].name : "";
    uint16_t white = lcd(THEME_MENU_TEXT), soft = lcd(THEME_MENU_TEXT_SOFT);
    char sub[40] = "";
    if (men_hm == HM_PICK) {
        snprintf(sub, sizeof sub, "ADD TO A GROUP");
    } else if (men_hm_app) {
        snprintf(sub, sizeof sub, "%s", tfn_is_stub(men_hm_app) ? "INSTALLED" : "BUILT IN");
    } else {
        int c = men_hm_grp >= 0 ? men_cat_count[men_hm_grp] : 0;
        snprintf(sub, sizeof sub, c == 1 ? "%d APP" : "%d APPS", c);
    }
    if (men_hm_wide()) {
        const mfont_t *f = hm_font(name, 160, &MF_LABEL);
        hm_text_fit(f, ix + is / 2, iy + is + 22, name, 164, white, 255);
        hm_text_fit(&MF_TINY, ix + is / 2, iy + is + 40, sub, 164, soft, 255);
    } else {
        int xbx, xby;
        men_hm_xbox(&xbx, &xby);
        int tx = ix + is + 10, tw = (gfx_round() ? bx + bw : xbx) - tx - 6;
        const mfont_t *f = hm_font(name, tw, &MF_LABEL);
        if (tw < 90) {  // (the 3.49 upright: the name under the app)
            hm_text_left(hm_font(name, gfx_w - 16, &MF_SMALL), ix, iy + is + 16, name, gfx_w - 16, white);
            hm_text_left(&MF_TINY, ix, iy + is + 32, sub, gfx_w - 16, soft);
        } else {
            hm_text_left(f, tx, iy + is / 2 + 2, name, tw, white);
            hm_text_left(&MF_TINY, tx, iy + is / 2 + 20, sub, tw, soft);
        }
    }
    if (men_hm == HM_INFO) {
        char l[6][40];
        int n;
        hm_info_lines(l, &n);
        for (int i = 0; i < n; i++) {
            men_mtext(&MF_SMALL, bx + 6, by + 20 + i * 22, l[i], i ? soft : white, 255, 1);
        }
        men_mtext(&MF_TINY, bx + 6, by + bh - 4, "TAP TO GO BACK", soft, 160, 1);
    } else {
        for (int i = 0; i < men_nhb; i++) {
            hm_draw_btn(&men_hb[i], i);
        }
    }
    hm_draw_x();
}
static void hm_render_tile(int i, int x, int y, int t, bool lifted, int64_t now)
{
    int tt = men_he[i];
    uint16_t white = lcd(THEME_MENU_TEXT);
    hgrid_t g = men_hm_grid();
    const char *lab = "";
    if (tt == HT_UP) {
        men_group_tile(men_hm_lev, x, y, t, true, false);
        lab = men_pk_on ? "TAKE OUT" : "UP";
    } else if (tt == HT_SHOW) {
        men_rr_fill(x, y, t, t, t / 8 + 2, lcd(THEME_MENU_HM_SHOW), 255);
        char n[12];
        snprintf(n, sizeof n, "%d", men_hid_count());
        men_mtext_c(&MF_LABEL, x + t / 2, y + t / 2 + 4, n, white, 255, 1);
        men_mtext_c(&MF_TINY, x + t / 2, y + t - 8, "SHOW", lcd(THEME_MENU_TEXT_SOFT), 255, 1);
        lab = "HIDDEN";
    } else {
        const lent_t *e = &men_lay.e[tt];
        if (e->kind == E_APP) {
            hm_icon(men_all[e->ref].app, x, y, t);
            lab = men_all[e->ref].app->name;
        } else if (e->kind == E_GRP) {
            men_group_tile(e->ref, x, y, t, false, lifted);
            lab = men_lay.g[e->ref].name;
        } else {
            men_group_tile(LV_FAV, x, y, t, false, false);
            lab = "FAVORITES";
        }
    }
    if (lifted) {
        float p = 0.5f + 0.5f * sinf(now / 120.0f);
        uint16_t c = lcd(THEME_MENU_HM_LIFT);
        for (int k = 0; k < 3; k++) {
            int a = (int)(160 + 90 * p);
            for (int u = -k - 2; u < t + k + 2; u++) {
                pix(x + u, y - k - 2, c, a);
                pix(x + u, y + t + k + 1, c, a);
            }
            for (int v = -k - 2; v < t + k + 2; v++) {
                pix(x - k - 2, y + v, c, a);
                pix(x + t + k + 1, y + v, c, a);
            }
        }
    }
    hm_text_fit(&MF_TINY, x + t / 2, y + t + 12, lab, t + g.gx - 2, white, 230);
}
static void hm_render_edit(int64_t now)
{
    int look = men_hm_lev >= 0 ? men_gstyle(men_hm_lev) : ST_STAR;
    hm_bg(look);
    hgrid_t g = men_hm_grid();
    int per = g.cols * g.rows, pages = men_he_pages(), pki = men_he_find_pk();
    int slot = men_ed_drag && !men_ed_merge ? men_he_slot(men_ed_x, men_ed_y) : -1;
    float tsec = now / 1000.0f;
    int lo = ((int)floorf(men_ed_pagef)) * per, hi = ((int)ceilf(men_ed_pagef) + 1) * per;
    if (lo < 0) {
        lo = 0;
    }
    for (int i = lo; i < men_nhe && i < hi; i++) {
        int x, y, t;
        men_he_rect(i, &x, &y, &t);
        if (x + t < -4 || x > gfx_w + 4) {
            continue;
        }
        if (men_ed_drag && i == pki) {  // its old place: a faint box
            for (int u = 0; u < t; u += 4) {
                pix(x + u, y, lcd(THEME_MENU_HM_SLOT), 90);
                pix(x + u, y + t - 1, lcd(THEME_MENU_HM_SLOT), 90);
                pix(x, y + u, lcd(THEME_MENU_HM_SLOT), 90);
                pix(x + t - 1, y + u, lcd(THEME_MENU_HM_SLOT), 90);
            }
            continue;
        }
        // the wiggle: a small move of each tile, each with its own phase
        int wx = men_he[i] >= 0 ? (int)lroundf(1.3f * sinf(tsec * 17 + i * 1.7f)) : 0,
            wy = men_he[i] >= 0 ? (int)lroundf(1.0f * cosf(tsec * 15 + i * 2.3f)) : 0;
        bool hov = men_ed_drag && men_ed_hover == i;
        if (hov && men_ed_merge) {
            int gr = t / 10;
            men_rr_fill(x - gr - 3, y - gr - 3, t + 2 * gr + 6, t + 2 * gr + 6, t / 6, lcd(THEME_MENU_HM_MERGE), 200);
        }
        hm_render_tile(i, x + wx, y + wy, t, !men_ed_drag && i == pki, now);
        if (men_hm_focus >= men_nhb && s_hm_fbtn < 0 && men_hm_focus - men_nhb == i - men_hm_page * per) {
            men_rr_fill(x - 3, y + t + 16, t + 6, 3, 1, lcd(THEME_MENU_HM_FOCUS), 255);
        }
    }
    // the place of the drop: a bar
    if (slot >= 0 && slot / per == men_hm_page) {
        int x, y, t, j = slot < men_nhe ? slot : slot - 1;
        men_he_rect(j, &x, &y, &t);
        int bx = slot < men_nhe ? x - g.gx / 2 - 2 : x + t + g.gx / 2 - 2;
        men_rr_fill(bx, y + 4, 4, t - 8, 2, lcd(THEME_MENU_HM_DROP), 255);
    }
    // the bar: the level and the page, the buttons
    char lev[40];
    snprintf(lev, sizeof lev, "%s  %d / %d", men_hm_lev >= 0 ? men_lay.g[men_hm_lev].name : "HOME", men_hm_page + 1,
             pages);
    int ax0, ay0, ax1, ay1;
    men_hm_area(&ax0, &ay0, &ax1, &ay1);
    uint16_t soft = lcd(THEME_MENU_HM_TEXT_SOFT);
    if (men_hm_wide()) {
        men_mtext(&MF_SMALL, 14, g.bar_y + g.bar_h / 2 + 6, men_pk_on && !men_ed_drag ? "TAP A PLACE" : "EDIT HOME",
                  lcd(THEME_MENU_HM_HINT), 255, 2);
    }
    int yl = g.y0 + g.rows * (g.t + g.lab + g.gy) + (men_hm_wide() ? 0 : 6);
    if (yl > ay1 - 2) {
        yl = ay1 - 2;
    }
    if (men_hm_wide()) {
        yl = gfx_h - 4;
    }
    if (!men_hm_wide() && men_pk_on && !men_ed_drag) {
        men_mtext_c(&MF_TINY, (ax0 + ax1) / 2, yl, "TAP A PLACE", lcd(THEME_MENU_HM_HINT), 255, 1);
    } else {
        men_mtext_c(&MF_TINY, (ax0 + ax1) / 2, yl, lev, soft, 255, 1);
    }
    for (int i = 0; i < men_nhb; i++) {
        hm_draw_btn(&men_hb[i], i);
    }
    // the dragged tile: in the finger, bigger, with a shadow
    if (men_ed_drag && pki >= 0) {
        int t = g.t * 115 / 100, x = men_ed_x - t / 2, y = men_ed_y - t / 2;
        men_soft_ellipse(x + t / 2 + 4, y + t + 4, t / 2, t / 10 + 2, lcd(THEME_MENU_SHADOW), 140);
        hm_render_tile(pki, x, y, t, false, now);
    }
}
static void home_render_inner(int64_t now);
void men_home_render(int64_t now)
{
    tfn_icon_ask(true);  // (no card read in a frame of the menu: tfn_icons.c)
    home_render_inner(now);
    tfn_icon_ask(false);
    (void)tfn_icon_missed();  // (also the trim of the icon files that the task read)
}
static void home_render_inner(int64_t now)
{
    if (men_hm == HM_NAME) {
        kbd_render(now);
        return;
    }
    s_hm_fbtn = -1;
    if (men_hm_focus >= 0) {
        int cx[HB_MAX + 40], cy[HB_MAX + 40], n = men_hm_targets(cx, cy, HB_MAX + 40);
        if (men_hm_focus < n) {
            s_hm_fbtn = men_hm_btn_at(cx[men_hm_focus], cy[men_hm_focus]);
        }
    }
    if (men_hm == HM_EDIT) {
        hm_render_edit(now);
    } else {
        hm_render_sheet(now);
    }
    // the message of a change
    if (now - men_pop_at < POP_MS) {
        const mfont_t *f = men_mtext_w(&MF_LABEL, men_pop_text, 1) + 28 <= gfx_w - 8 ? &MF_LABEL : &MF_TINY;
        int w = men_mtext_w(f, men_pop_text, 1) + (f == &MF_LABEL ? 28 : 16), h = f == &MF_LABEL ? 30 : 24,
            px = (gfx_w - w) / 2, py = gfx_h / 2 - h / 2;
        men_rr_fill(px - 2, py - 2, w + 4, h + 4, 12, lcd(THEME_MENU_POP_EDGE), 255);
        men_rr_fill(px, py, w, h, 10, lcd(THEME_MENU_POP_BG), 255);
        men_mtext_c(f, gfx_w / 2, py + (f == &MF_LABEL ? 20 : 16), men_pop_text, lcd(THEME_MENU_POP_TEXT), 255, 1);
    }
}

// a new app list (an install or a removal) while a screen is open: the edit mode reads its layout again by the names
void men_home_gen_changed(void)
{
    if (men_hm == HM_EDIT) {
        char *t = men_hm_text_now();
        men_all_build();
        if (t) {
            men_hm_reparse(t);
            free(t);
        }
        men_pk_on = false;
        men_hm_change();
    } else {
        men_home_close();
        men_all_build();
        men_lay_reload = true;
    }
}
// tests (the console "menuedit"): the edit mode at the level now, with no finger down
void men_home_test_edit(int64_t now)
{
    men_home_hold_bg(now, gfx_w / 2, 4);
    men_ed_touch = men_ed_secret = men_hm_skip_tap = false;
}
// the menu closes (the manager opens an app): an edit is kept, the other screens close
void men_home_leave(void)
{
    if (men_hm == HM_EDIT) {
        men_hm_edit_done();
    } else if (men_hm != HM_OFF) {
        men_home_close();
    }
}
