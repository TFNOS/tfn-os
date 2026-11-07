// FILES: the three panes of the Tab5 (the folders, the list, the preview) (main/app_files.c has the rest).
#include "app_files_priv.h"

// ---------------------------------------------------------------- THE TAB5: THREE PANES
// The list page of the Tab5 has three panes under the header (UP, the path, MORE):
// - THE FOLDER PANE (left): the folders beside the folder now (the folders of its parent), the folder now lit; a tap
//   on another folder shows its things. At a root (CARD or BOARD) it shows CARD and BOARD with their free space.
// - THE LIST PANE (middle): the things of the folder now, two lines in a row of 52 px. A tap on a folder goes into it;
//   a tap on a file shows it in the preview, a second tap on it opens it (the full text or picture page). A hold: the
//   actions (as on the other boards).
// - THE PREVIEW PANE (right): the start of a text, a picture (made small to the pane), or the facts of the file; OPEN
//   (INSTALL for an app package, INFO for other files) and MORE (the actions). With no file: the count of the folder.
#if T5P
static bool s_in_side;  // the touch started in the folder pane

void fil_pv_clear(void)
{
    heap_caps_free(fil_pv_pic);
    fil_pv_pic = NULL;
    fil_pv_id = 0;  // (a decode that runs now: its result goes)
    fil_pv = -1;
    fil_pv_nl = 0;
    fil_pv_name[0] = 0;
}

static int side_cmp(const void *a, const void *b)
{
    return fil_ci_cmp(((const side_t *)a)->name, ((const side_t *)b)->name);
}

static void side_scan(const char *par);

// go_into: the folders of the list now are the folders of the parent of the next folder: they go to the folder pane
// (no new read of the card). Not for a list that is not complete or that has entries out of it (s_skip).
void fil_side_keep(void)
{
    fil_side_root = -1;
    if (fil_list_busy() || fil_skip) {
        return;
    }
    fil_nside = 0;
    for (int i = 0; i < fil_n && fil_nside < SIDE_MAX; i++) {
        if (fil_ent[i].kind != K_DIR && fil_ent[i].kind != K_PKG) {
            continue;
        }
        memcpy(fil_side[fil_nside].name, fil_ent[i].name, sizeof fil_side[0].name);  // (the same size: FL_NAME)
        fil_side[fil_nside].lock = fil_ent[i].lock;
        fil_nside++;
    }
    fil_side_root = fil_root;
    snprintf(fil_side_par, sizeof fil_side_par, "%s", fil_rel);
}

// the folder pane for the folder now (s_root, s_rel)
static void side_read(void)
{
    fil_side_cur = -1;  // (s_nside stays: the folders that side_keep or side_scan keeps)
    fil_side_roots = !fil_rel[0];
    if (fil_side_roots) {
        fil_side_cur = fil_root;  // (the rows: RT_CARD, RT_BOARD)
        ks_set_max(&fil_kside, 0);
        ks_jump(&fil_kside, 0);
        return;
    }
    char par[FL_PATH], cur[FL_NAME];
    snprintf(par, sizeof par, "%s", fil_rel);
    char *sl = strrchr(par, '/');
    snprintf(cur, sizeof cur, "%.*s", (int)sizeof cur - 1, sl ? sl + 1 : par);
    if (sl) {
        *sl = 0;
    }
    if (fil_side_root == fil_root && !strcmp(fil_side_par, par)) {
        for (int i = 0; i < fil_nside; i++) {
            if (!strcmp(fil_side[i].name, cur)) {
                fil_side_cur = i;
            }
        }
    } else {
        side_scan(par);
        for (int i = 0; i < fil_nside; i++) {
            if (!strcmp(fil_side[i].name, cur)) {
                fil_side_cur = i;
            }
        }
    }
    float mx = (float)(fil_nside * SROW_H - SIDE_H + 4);
    ks_set_max(&fil_kside, mx > 0 ? mx : 0);
    // the folder now is in view
    if (fil_side_cur >= 0) {
        ks_jump(&fil_kside, (float)(fil_side_cur * SROW_H - SIDE_H / 2 + SROW_H / 2));
    }
}

// the folders of the parent par (one read of the folder, no stat on the board)
static void side_scan(const char *par)
{
    char dir[FL_PATH];
    fil_nside = 0;
    fil_side_root = -1;
    if (!fil_join(dir, sizeof dir, fil_root_real(fil_root), par, NULL)) {
        return;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return;
    }
    struct dirent *de;
    while ((de = readdir(d)) != NULL && fil_nside < SIDE_MAX) {
        const char *nm = de->d_name;
        if (!strcmp(nm, ".") || !strcmp(nm, "..") || strlen(nm) >= FL_NAME) {
            continue;
        }
#ifdef DT_DIR
        // (the type of the entry from readdir: no stat; a stat on a FAT card searches the folder from its start)
        if (de->d_type != DT_UNKNOWN) {
            if (de->d_type != DT_DIR) {
                continue;
            }
        } else
#endif
                if (!fil_join(fil_p1, sizeof fil_p1, dir, "/", nm) || !fil_is_dir(fil_p1))
            continue;
        side_t *e = &fil_side[fil_nside++];
        snprintf(e->name, sizeof e->name, "%s", nm);
        e->lock = fil_join(fil_p1, sizeof fil_p1, par, "/", nm) ? fil_rel_locked(fil_p1) : 1;
    }
    closedir(d);
    qsort(fil_side, (size_t)fil_nside, sizeof fil_side[0], side_cmp);
    fil_side_root = fil_root;
    snprintf(fil_side_par, sizeof fil_side_par, "%s", par);
}

// after each read of the list: the folder pane again; the preview keeps its file when it is still there
void fil_t5_after_load(void)
{
    side_read();
    if (fil_pv < 0) {
        return;
    }
    if (fil_pv_root != fil_root || strcmp(fil_pv_rel, fil_rel)) {
        fil_pv_clear();
        return;
    }
    for (int i = 0; i < fil_n; i++) {
        if (!strcmp(fil_ent[i].name, fil_pv_name) && fil_ent[i].kind == fil_pv_kind) {
            fil_pv = i;
            return;
        }
    }
    fil_pv_clear();
}

// show the file i in the preview
static void pv_select(int i)
{
    fil_pv_clear();
    const fent_t *e = &fil_ent[i];
    fil_pv = i;
    fil_pv_root = fil_root;
    fil_pv_kind = e->kind;
    fil_pv_state = 1;
    fil_pv_drawn = false;
    fil_pv_err[0] = 0;
    snprintf(fil_pv_name, sizeof fil_pv_name, "%s", e->name);
    snprintf(fil_pv_rel, sizeof fil_pv_rel, "%s", fil_rel);
    if (e->kind == K_PIC) {
        fil_pv_state = 0;  // (the decode comes in the update after OPENING shows)
        if (e->size > 8u * 1024 * 1024) {
            fil_pv_state = 2;
            snprintf(fil_pv_err, sizeof fil_pv_err, "TOO BIG TO SHOW (MORE THAN 8 MB)");
        }
    } else if (e->kind == K_TEXT) {
        char p[FL_PATH];
        FILE *f = fil_join(p, sizeof p, fil_root_real(fil_root), fil_rel, e->name) ? fopen(p, "rb") : NULL;
        if (!f) {
            fil_pv_state = 2;
            snprintf(fil_pv_err, sizeof fil_pv_err, "THE FILE DID NOT OPEN");
        } else {
            size_t n = fread(fil_pv_txt, 1, PV_TXT, f);
            fclose(f);
            uint32_t o = 0;
            for (size_t k = 0; k < n; k++) {  // (as open_text: no CR, other control bytes = a space)
                uint8_t c = (uint8_t)fil_pv_txt[k];
                if (c == '\r') {
                    continue;
                }
                fil_pv_txt[o++] = (char)(c == '\n' ? '\n' : c < 0x20 ? ' ' : c);
            }
            fil_pv_txt[o] = 0;
            fil_pv_nl = fil_wrap_lines(&KF_NAME, fil_pv_txt, o, PV_IW - 4, fil_pv_loff, fil_pv_llen, PV_LINES);
        }
    }
    sfx_ui(SFX_TICK);
}

// the update: the picture of the preview (after OPENING showed once)
void fil_pv_update(void)
{
    if (fil_page != P_LIST || fil_pv < 0 || fil_pv_kind != K_PIC || fil_pv_state != 0 || !fil_pv_drawn || fil_pv_id) {
        return;
    }
    char p[FL_PATH];
    if (!fil_join(p, sizeof p, fil_root_real(fil_pv_root), fil_pv_rel, fil_pv_name)) {
        fil_pv_state = 2;
        snprintf(fil_pv_err, sizeof fil_pv_err, "THE PATH IS TOO LONG");
        return;
    }
    fil_pv_id = fil_jj_start(p, PV_IW, PV_BODY_H);  // (0: the worker is busy; the next update tries again)
}

// the OPEN button of the preview: its word
const char *fil_pv_open_word(void)
{
    if (fil_pv_kind == K_PKG && !fil_ent[fil_pv].lock) {
        return "INSTALL";
    }
    if (fil_pv_kind == K_FILE) {
        return "INFO";
    }
    return "OPEN";
}

// the control at x, y of the list page
static int t5_at(int x, int y)
{
    if (y < HEAD_H) {
        if (x < UP_W + 6) {
            return CT_UP;
        }
        if (x >= MORE_X - 4) {
            return CT_MORE;
        }
        return CT_UP;  // (the path line: up)
    }
    if (x < SIDE_W) {
        int i = (int)floorf((y - SIDE_TOP + ks_pos(&fil_kside)) / SROW_H);
        int n = fil_side_roots ? 2 : fil_nside;
        return i >= 0 && i < n ? CT_SIDE + i : -1;
    }
    if (x >= PV_X) {
        if (fil_pv < 0 || fil_list_busy()) {
            return -1;
        }
        if (y >= PV_BTN_Y - 4) {
            return x < PV_X + PV_W / 2 ? CT_PVOPEN : CT_PVMORE;
        }
        return CT_PVBODY;
    }
    if (fil_list_busy()) {
        return CT_EMPTY;
    }
    int i = (int)floorf((y - LIST_Y + fil_scroll) / ROW_H);
    return i >= 0 && i < fil_n ? i : CT_EMPTY;
}

// a tap on the row i of the folder pane
static void side_pick(int i, int64_t now)
{
    if (fil_side_roots) {
        if (i == fil_root) {
            return;
        }
        if (i == RT_CARD && !fil_card_ok) {
            fil_card_look();
            fil_space_read();
        }
        if (i == RT_CARD ? fil_card_ok : fil_data_ok) {
            fil_open_root(i);
            sfx_ui(SFX_CLICK);
        } else {
            fil_say(i == RT_CARD ? "NO CARD. PUT IN A CARD, THEN TAP AGAIN" : "THE BOARD FILES ARE NOT THERE", true,
                    now);
        }
        return;
    }
    if (i == fil_side_cur || i < 0 || i >= fil_nside) {
        return;
    }
    char *sl = strrchr(fil_rel, '/');
    size_t k = sl ? (size_t)(sl - fil_rel) : 0;
    if (k + 1 + strlen(fil_side[i].name) >= sizeof fil_rel) {
        fil_say("THE PATH IS TOO LONG", true, now);
        return;
    }
    snprintf(fil_rel + k, sizeof fil_rel - k, "/%s", fil_side[i].name);
    ks_jump(&fil_ks, 0);
    fil_scroll = 0;
    fil_focus = -1;
    fil_list_load();
    sfx_ui(SFX_CLICK);
}

// a tap on the row i of the list pane
static void mid_tap(int i, int64_t now)
{
    const fent_t *e = &fil_ent[i];
    if (e->kind == K_DIR || (e->kind == K_PKG && e->lock)) {
        fil_go_into(e->name);
        return;
    }
    if (i == fil_pv) {  // (a second tap: the full page)
        fil_open_entry(i, now);
        return;
    }
    pv_select(i);
}

void fil_t5_list_input(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN: {
        fil_touch = true;
        fil_drag = false;
        fil_x0 = ev->x;
        fil_y0 = ev->y;
        s_in_side = ev->x < SIDE_W && ev->y >= LIST_Y;
        bool mid = ev->x >= SIDE_W && ev->x < PV_X && ev->y >= LIST_Y;
        bool caught = s_in_side ? ks_down(&fil_kside, ev->y, ev->t_ms)
                      : mid     ? ks_down(&fil_ks, ev->y, ev->t_ms)
                                : false;
        fil_hot = caught ? -1 : t5_at(ev->x, ev->y);
        break;
    }
    case IN_MOVE: {
        kscroll_t *k = s_in_side                                                 ? &fil_kside
                       : (fil_x0 >= SIDE_W && fil_x0 < PV_X && fil_y0 >= LIST_Y) ? &fil_ks
                                                                                 : NULL;
        if (k && ks_move(k, ev->y, ev->t_ms)) {
            fil_drag = true;
            input_tap_cancel();
            fil_hot = -1;
        }
        if (fil_drag) {
            fil_scroll = ks_pos(&fil_ks);
        } else if ((abs(ev->x - fil_x0) > DRAG_PX || abs(ev->y - fil_y0) > DRAG_PX) && t5_at(ev->x, ev->y) != fil_hot) {
            fil_hot = -1;
        }
        break;
    }
    case IN_UP:
        fil_touch = false;
        if (s_in_side) {
            ks_up(&fil_kside, ev->t_ms);
        } else {
            ks_up(&fil_ks, ev->t_ms);
        }
        fil_drag = false;
        fil_hot = -1;
        break;
    case IN_TAP: {
        if (fil_y0 >= LIST_Y && !(s_in_side ? ks_tap_ok(&fil_kside) : ks_tap_ok(&fil_ks))) {
            break;
        }
        int c = t5_at(ev->x, ev->y);
        if (c == CT_UP) {
            fil_go_up();
        } else if (c == CT_MORE) {
            fil_panel_open_more();
        } else if (c >= CT_SIDE && c < CT_SIDE + SIDE_MAX) {
            side_pick(c - CT_SIDE, ev->t_ms);
        } else if ((c == CT_PVOPEN || c == CT_PVBODY) && fil_pv >= 0) {
            if (c == CT_PVBODY && fil_pv_kind != K_TEXT && fil_pv_kind != K_PIC) {
                break;  // (only a text or a picture)
            }
            fil_sel = fil_pv;
            if (fil_pv_kind == K_FILE) {
                fil_open_info(ev->t_ms);
            } else {
                fil_open_entry(fil_pv, ev->t_ms);
            }
        } else if (c == CT_PVMORE && fil_pv >= 0) {
            fil_panel_open_entry(fil_pv);
        } else if (c >= 0 && c < fil_n) {
            mid_tap(c, ev->t_ms);
        }
        break;
    }
    case IN_LONG: {
        if (fil_y0 >= LIST_Y && !(s_in_side ? ks_tap_ok(&fil_kside) : ks_tap_ok(&fil_ks))) {
            break;
        }
        int c = t5_at(ev->x, ev->y);
        fil_hot = -1;
        if (c >= 0 && c < fil_n) {
            fil_panel_open_entry(c);
        } else if (c == CT_EMPTY) {
            fil_panel_open_more();
        }
        break;
    }
    default:
        break;
    }
}
#endif
