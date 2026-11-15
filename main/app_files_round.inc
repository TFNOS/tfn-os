// FILES on a round panel (BOARD_ROUND, APP_ROUND_HI): the drawing of each page in the circle (the values in
// app_files_priv.h, the part BOARD_ROUND). The top cap has the title and the space, the middle box has the rows, the
// text or the buttons, the bottom cap has the buttons. Only app_files_draw.c includes this file (after the helpers).

static void reading_render(int cx, int cy);

// The half width of the circle at the height y (0 out of the circle).
static int rnd_half(int y)
{
    int r = gfx_w / 2, dy = y - gfx_h / 2;
    return dy * dy >= r * r ? 0 : (int)sqrtf((float)(r * r - dy * dy));
}

// The buttons of the bottom cap: one in the middle (r = NULL), or two side by side.
static void rnd_bar(const char *l, bool lhot, const char *r, bool rhot)
{
    int cx = SW / 2, g = FR(5);
    if (!r) {
        button(cx - FR(80), BAR_Y, FR(160), BAR_H, l, lhot, 0);
        return;
    }
    button(cx - g - FR(105), BAR_Y, FR(105), BAR_H, l, lhot, 0);
    button(cx + g, BAR_Y, FR(105), BAR_H, r, rhot, 0);
}

// The top cap: a title (fit to the circle) and a note under it.
static void rnd_head(const char *title, bool tail, const char *note, uint16_t nc)
{
    gfx_fill_rect(0, 0, SW, HEAD_H, THEME_FILES_HEAD);
    gfx_fill_rect(0, HEAD_H - 1, SW, 1, THEME_FILES_LINE);
    char t[FL_PATH + 16];
    int w = 2 * rnd_half(PATH_B1 - KF_TEXT.asc) - FR(36);
    if (tail) {
        fit_tail(&KF_TEXT, title, w, t, sizeof t);
    } else {
        fit(&KF_TEXT, title, w, t, sizeof t);
    }
    ui_text_c(&KF_TEXT, SW / 2, PATH_B1, t, THEME_FILES_TEXT);
    if (note && note[0]) {
        fit(&KF_LABEL, note, 2 * rnd_half(PATH_B2 - KF_LABEL.asc) - FR(30), t, sizeof t);
        ui_text_c(&KF_LABEL, SW / 2, PATH_B2, t, nc);
    }
}

// ---- the home page: the two tiles in the middle box, CARD TOOLS on the bottom cap
static void rnd_home(void)
{
    gfx_fill(THEME_FILES_BG);
    mf_text_c(&MF_N24, SW / 2, FR(70), "FILES", THEME_FILES_ACCENT, 255, 0);
    tile(TILE1_X, TILE_Y, RT_CARD, fil_hot == CT_CARD, fil_focus == 0);
    tile(TILE2_X, TILE2_Y, RT_BOARD, fil_hot == CT_BOARD, fil_focus == 1);
    button(TOOLS_X, BAR_Y, TOOLS_W, BAR_H, "CARD TOOLS", fil_hot == CT_TOOLS, 0);
    if (fil_focus == 2) {
        ui_rbox(TOOLS_X, BAR_Y, TOOLS_W, BAR_H, 10, THEME_FILES_BTN, THEME_FILES_ACCENT, 3);
    }
}

// ---- the list: the path and the space on the top cap, the rows in the middle box, UP and MORE on the bottom cap
static void rnd_row(int i, int y)
{
    const fent_t *e = &fil_ent[i];
    bool hot = fil_hot == i;
    ui_rrect(MB_X, y + 2, MB_W, ROW_H - 4, 10, hot ? THEME_FILES_HOT : THEME_FILES_ROW);
    if (fil_focus == i) {
        ui_rbox(MB_X, y + 2, MB_W, ROW_H - 4, 10, hot ? THEME_FILES_HOT : THEME_FILES_ROW, THEME_FILES_ACCENT, 3);
    }
    int ih = ICON_W * 80 / 100;
    ico_entry(e, ICON_X, y + (ROW_H - ih) / 2, ICON_W, ih);
    bool folder = e->kind == K_DIR || (e->kind == K_PKG && e->lock);
    char r[48] = "", nm[FL_NAME + 8];
    int right = SIZE_R - (folder ? 18 : 0) - (e->lock ? 22 : 0), mid = y + ROW_H / 2;
    fit(&KF_TEXT, e->name, right - NAME_X - 4, nm, sizeof nm);
    ui_text(&KF_TEXT, NAME_X, mid - 2, nm, e->lock ? THEME_FILES_TEXT_DIM : THEME_FILES_TEXT);
    if (e->kind == K_DIR) {
        snprintf(r, sizeof r, "%s", e->lock ? "LOCKED FOLDER" : "FOLDER");
    } else if (e->kind == K_PKG) {
        snprintf(r, sizeof r, "%s", e->lock ? "LOCKED" : "APP PACKAGE");
    } else {
        char a[16], d[24];
        fil_fmt_size(a, sizeof a, e->size);
        fil_fmt_date(d, sizeof d, e->mtime);
        if (strlen(d) > 10) {
            d[10] = 0;  // (the date only)
        }
        snprintf(r, sizeof r, "%s    %s", a, d);
    }
    ui_text(&KF_LABEL, NAME_X, mid + 17, r, e->kind == K_PKG && !e->lock ? THEME_FILES_VIOLET : THEME_FILES_TEXT_DIM);
    if (folder) {
        chevron(SIZE_R - 4, mid, 5, 1, THEME_FILES_FAINT);
    }
    if (e->lock) {
        ico_lock(right + 10, mid, 15, THEME_FILES_TEXT_DIM);
    }
}

static void rnd_list_head(void)
{
    char p[FL_PATH + 16], sp[48];
    snprintf(p, sizeof p, "%s%s", fil_root_name(fil_root), fil_rel);
    if (fil_root == RT_CARD) {
        space_text(sp, sizeof sp, fil_card_t, fil_card_f);
    } else {
        space_text(sp, sizeof sp, fil_data_t, fil_data_f);
    }
    if (fil_rel_lock) {
        char f[24];
        fil_fmt_size(f, sizeof f, fil_root == RT_CARD ? fil_card_f : fil_data_f);
        snprintf(sp, sizeof sp, "LOCKED: %.15s FREE", f);
    }
    rnd_head(p, true, sp, fil_rel_lock ? THEME_FILES_ACCENT : THEME_FILES_TEXT_DIM);
    gfx_fill_rect(0, LIST_B, SW, SH - LIST_B, THEME_FILES_BG);
    rnd_bar("UP", fil_hot == CT_UP, "MORE", fil_hot == CT_MORE);
}

static void rnd_list(void)
{
    gfx_fill(THEME_FILES_BG);
    if (fil_list_busy()) {
        reading_render(SW / 2, LIST_Y + LST_H / 2);
        rnd_list_head();
        return;
    }
    int sc = (int)lroundf(fil_scroll);
    for (int i = 0; i < fil_n; i++) {
        int y = LIST_Y + i * ROW_H - sc;
        if (y + ROW_H <= LIST_Y || y >= LIST_B) {
            continue;
        }
        rnd_row(i, y);
    }
    if (fil_skip) {
        char t[64];
        snprintf(t, sizeof t, "%d MORE ARE NOT IN THE LIST (THE LIMIT IS %d)", fil_skip, FL_MAX);
        int y = LIST_Y + fil_n * ROW_H - sc;
        if (y < LIST_B) {
            ui_text_c(&KF_LABEL, SW / 2, y + ROW_H / 2 + 5, t, THEME_FILES_TEXT_DIM);
        }
    }
    if (!fil_n && !fil_skip) {
        int cy = LIST_Y + LST_H / 2;
        ui_text_c(&KF_TEXT, SW / 2, cy - 4, "AN EMPTY FOLDER", THEME_FILES_TEXT_DIM);
        if (!fil_rel_lock) {
            ui_text_c(&KF_LABEL, SW / 2, cy + 20, "HOLD HERE FOR A NEW FOLDER", THEME_FILES_FAINT);
        }
    }
    scroll_bar(LIST_Y, LST_H, fil_scroll, fil_max_scroll(), (fil_n + (fil_skip ? 1 : 0)) * ROW_H);
    rnd_list_head();
}

// ---- the panel of the actions: the name and the note on the top cap, the buttons in the middle box, CLOSE below
static void rnd_act(void)
{
    gfx_fill(THEME_FILES_BG);
    uint16_t nc = strncmp(fil_pnote, "LOCKED", 6) && strncmp(fil_pnote, "NO CARD", 7) ? THEME_FILES_TEXT_DIM
                                                                                      : THEME_FILES_ACCENT;
    rnd_head(fil_ptitle, fil_pkind == PK_MORE, "", nc);
    if (fil_pkind == PK_ENTRY && fil_sel >= 0 && fil_sel < fil_n) {
        int ih = ICON_W * 80 / 100;
        ico_entry(&fil_ent[fil_sel], SW / 2 - ICON_W / 2, FR(6), ICON_W, ih);
    }
    char t[FL_PATH + 16], u[FL_PATH + 16];
    wrap2(&KF_LABEL, fil_pnote, 2 * rnd_half(PATH_B2) - FR(40), t, u, sizeof t);
    ui_text_c(&KF_LABEL, SW / 2, u[0] ? PATH_B2 - 4 : PATH_B2, t, nc);
    if (u[0]) {
        ui_text_c(&KF_LABEL, SW / 2, PATH_B2 + 12, u, nc);
    }
    for (int i = 0; i < fil_nact; i++) {
        int x, y, w, h;
        fil_act_box(i, &x, &y, &w, &h);
        int a = fil_act[i];
        button(x, y, w, h, fil_act_label(a), fil_hot == i, a == A_DELETE || a == A_FORMAT ? 1 : a == A_INSTALL ? 2 : 0);
        if (fil_focus == i) {
            ui_rbox(x, y, w, h, 10, a == A_DELETE || a == A_FORMAT ? THEME_FILES_FAULT_BG : THEME_FILES_BTN,
                    THEME_FILES_ACCENT, 3);
            label_c(x, y, w, h, fil_act_label(a), THEME_FILES_TEXT);
        }
    }
    rnd_bar("CLOSE", fil_hot == CT_CLOSE, NULL, false);
}

// ---- the question: the two lines over the middle, the two answers side by side, CLOSE on the bottom cap
static void rnd_ask(int64_t now)
{
    gfx_fill(THEME_FILES_BG);
    int w = 2 * rnd_half(ASK_B1 - KF_BIG.asc) - FR(40);
    static const kfont_t *const F1[] = {&KF_BIG, &KF_TEXT, &KF_NAME};
    const kfont_t *f = ui_text_fit(F1, 3, fil_q1, w);
    char t[FL_NAME + 48], u[FL_NAME + 48];
    fit(f, fil_q1, w, t, sizeof t);
    ui_text_c(f, SW / 2, ASK_B1, t, THEME_FILES_TEXT);
    wrap2(&KF_TEXT, fil_q2, MB_W, t, u, sizeof t);
    ui_text_c(&KF_TEXT, SW / 2, u[0] ? ASK_B2 - 10 : ASK_B2, t, THEME_FILES_TEXT_DIM);
    if (u[0]) {
        ui_text_c(&KF_TEXT, SW / 2, ASK_B2 + 12, u, THEME_FILES_TEXT_DIM);
    }
    bool danger = fil_qkind == Q_DELETE || fil_qkind == Q_REPLACE || fil_qkind == Q_FORMAT1 || fil_qkind == Q_FORMAT2;
    for (int a = 0; a < 2; a++) {
        if (!fil_qa[a]) {
            continue;
        }
        int bw = ASK_W - FR(5), x = a ? SW / 2 + FR(5) : MB_X, y = ASK_Y, h = ASK_H;
        bool hot = fil_qhot == a;
        uint16_t bg = a == 0   ? (fil_qkind == Q_INSTALL ? THEME_FILES_OK_BG : THEME_FILES_BTN)
                      : danger ? THEME_FILES_FAULT_BG
                               : THEME_FILES_OK_BG;
        ui_rrect(x, y, bw, h, 14, hot && !(a == 1 && fil_qhold_ms) ? THEME_FILES_ACCENT : bg);
        if (a == 1 && fil_qhold_ms) {
            float p = fil_qhold_t0 >= 0 && hot ? (float)(now - fil_qhold_t0) / fil_qhold_ms : 0;
            p = p > 1 ? 1 : p;
            int fw = (int)(bw * p);
            if (fw > 0) {
                ui_rrect(x, y, fw < 28 ? 28 : fw, h, 14, THEME_FILES_ACCENT);
            }
        }
        if (fil_qfocus == a) {
            ui_rbox(x, y, bw, h, 14, bg, THEME_FILES_ACCENT, 3);
        }
        const mfont_t *mf = mf_width(&MF_N24, fil_qa[a], 0) <= bw - 12 ? &MF_N24 : &MF_N16;
        int cy = y + h / 2 + mf->asc * 7 / 20 - (a == 1 && fil_qhold_ms ? 8 : 0);
        uint16_t tc = hot ? THEME_FILES_TEXT_ON_ACCENT : THEME_FILES_TEXT;
        mf_text_c(mf, x + bw / 2, cy, fil_qa[a], tc, 255, 0);
        if (a == 1 && fil_qhold_ms) {
            mf_text_c(&MF_SMALL, x + bw / 2, cy + 20, fil_qarmed ? "PUSH AGAIN" : "HOLD 1 S", tc, 255, 0);
        }
    }
    if (fil_qclose) {
        rnd_bar("CLOSE", fil_qhot == 2, NULL, false);
    }
}

// ---- the info page: the name on the top cap, the lines in the middle box, BACK on the bottom cap
static void rnd_info(void)
{
    gfx_fill(THEME_FILES_BG);
    rnd_head(fil_info_name, false, "INFO", THEME_FILES_TEXT_DIM);
    int step = (LIST_B - LIST_Y - FR(10)) / (fil_info_n > 6 ? fil_info_n : 6), y0 = LIST_Y + step - 6;
    int vx = MB_X + FR(96);
    for (int i = 0; i < fil_info_n; i++) {
        char t[FL_PATH];
        int y = y0 + i * step, xr = SW / 2 + rnd_half(y) - FR(24);
        ui_text(&KF_LABEL, MB_X + FR(8), y, fil_info_l[i], THEME_FILES_TEXT_DIM);
        if (!strcmp(fil_info_l[i], "PLACE")) {
            fit_tail(&KF_TEXT, fil_info_v[i], xr - vx, t, sizeof t);
        } else {
            fit(&KF_TEXT, fil_info_v[i], xr - vx, t, sizeof t);
        }
        ui_text(&KF_TEXT, vx, y, t, !strcmp(fil_info_l[i], "LOCKED") ? THEME_FILES_ACCENT : THEME_FILES_TEXT);
    }
    rnd_bar("BACK", fil_hot == CT_CLOSE, NULL, false);
}

// ---- the text page: the lines in the middle box (one width: the lines wrap once), the place on the top cap
static void rnd_text(void)
{
    gfx_fill(THEME_FILES_BG);
    int sc = (int)lroundf(fil_tscroll), first = sc / TXT_LH;
    if (first < 0) {
        first = 0;
    }
    char line[320];
    for (int i = first; i < fil_nl; i++) {
        int y = TXT_TOP + 4 + i * TXT_LH - sc;
        if (y + TXT_LH > LIST_B + 4) {
            break;
        }
        int n = fil_llen[i] < sizeof line - 1 ? fil_llen[i] : (int)sizeof line - 1;
        memcpy(line, fil_txt + fil_loff[i], (size_t)n);
        line[n] = 0;
        bool heb = false;
        for (int k = 0; k < n; k++) {
            if ((uint8_t)line[k] == 0xD7) {
                heb = true;
            }
        }
        if (heb) {
            text_r(&KF_TEXT, TXT_X + TXT_W, y + 17, line, THEME_FILES_TEXT);  // (a Hebrew line: at the right)
        } else {
            ui_text(&KF_TEXT, TXT_X, y + 17, line, THEME_FILES_TEXT);
        }
    }
    if (!fil_nl || (fil_nl == 1 && !fil_llen[0])) {
        ui_text_c(&KF_TEXT, SW / 2, TXT_TOP + TXT_H / 2, "AN EMPTY FILE", THEME_FILES_TEXT_DIM);
    }
    scroll_bar(TXT_TOP, TXT_H, fil_tscroll, fil_text_max(), fil_nl * TXT_LH);
    char note[96];
    int l1 = first + 1, l2 = (sc + TXT_H) / TXT_LH;
    l2 = l2 > fil_nl ? fil_nl : l2;
    l1 = l1 > l2 ? l2 : l1;
    int k = snprintf(note, sizeof note, "LINES %d-%d OF %d", l1, l2, fil_nl);
    if (fil_tsize > TXT_MAX && k > 0 && k < (int)sizeof note) {
        char a[24];
        fil_fmt_size(a, sizeof a, fil_tsize);
        snprintf(note + k, sizeof note - k, ". THE FIRST 64 KB OF %s", a);
    }
    rnd_head(fil_tname, false, note, THEME_FILES_TEXT_DIM);
    gfx_fill_rect(0, LIST_B, SW, SH - LIST_B, THEME_FILES_BG);
    rnd_bar("BACK", fil_hot == CT_CLOSE, NULL, false);
}

// ---- the picture page: the picture in the middle of the circle; a tap shows the name and BACK
static void rnd_pic(int64_t now)
{
    gfx_fill(THEME_FILES_PIC_BG);
    if (fil_pstate == 0) {
        ui_text_c(&KF_TEXT, SW / 2, SH / 2 + 6, "OPENING THE PICTURE...", THEME_FILES_TEXT_DIM);
    } else if (fil_pstate == 2) {
        char a[96], b[96];
        ui_text_c(&KF_TEXT, SW / 2, SH / 2 - 4, fil_perr, THEME_FILES_ACCENT);
        wrap2(&KF_LABEL, "A TAP SHOWS THE BAR. BACK CLOSES THE PICTURE", MB_W, a, b, sizeof a);
        ui_text_c(&KF_LABEL, SW / 2, SH / 2 + 20, a, THEME_FILES_TEXT_DIM);
        ui_text_c(&KF_LABEL, SW / 2, SH / 2 + 36, b, THEME_FILES_TEXT_DIM);
        if (fil_pbar_until <= now) {
            fil_pbar_until = now + 1000;  // (the bar stays: there is nothing to see)
        }
    } else if (fil_pic) {
        int ox = (SW - fil_pw) / 2, oy = (SH - fil_ph) / 2;
        for (int y = 0; y < fil_ph; y++) {
            int Y = oy + y;
            if (Y < 0 || Y >= gfx_h) {
                continue;
            }
            memcpy(g_fb + Y * gfx_w + ox, fil_pic + y * fil_pw, (size_t)fil_pw * 2);
        }
    }
    if (fil_pbar_until > now) {
        char note[40];
        snprintf(note, sizeof note, "%d x %d ON THE SCREEN", fil_pw, fil_ph);
        rnd_head(fil_pname, false, fil_pstate == 1 ? note : "", THEME_FILES_TEXT_DIM);
        rnd_bar("BACK", fil_hot == CT_CLOSE, NULL, false);
    }
}

// ---- the job page: the title on the top cap, the bar and the lines in the middle, STOP on the bottom cap
static void rnd_job(void)
{
    gfx_fill(THEME_FILES_BG);
    mf_text_c(&MF_N20, SW / 2, FR(72), fil_job.title, THEME_FILES_ACCENT, 255, 0);
    int by = FR(170), bx = MB_X + FR(10), bw = MB_W - FR(20);
    if (fil_job.kind == J_FORMAT || fil_job.kind == J_NONE) {
        ui_text_c(&KF_TEXT, SW / 2, by + 30, "FORMATTING THE CARD...", THEME_FILES_TEXT);
        ui_text_c(&KF_NAME, SW / 2, by + 60, "DO NOT TAKE OUT THE CARD", THEME_FILES_TEXT_DIM);
        return;
    }
    float p = fil_job.kind == J_COPY && fil_job.total ? (float)fil_job.done / fil_job.total
              : fil_job.n                             ? (float)fil_job.i / fil_job.n
                                                      : 1;
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    ui_rrect(bx, by, bw, 16, 8, THEME_FILES_LINE);
    if (p > 0) {
        ui_rrect(bx, by, (int)(bw * p) < 16 ? 16 : (int)(bw * p), 16, 8, THEME_FILES_ACCENT);
    }
    char t[96], a[24], b[24];
    if (fil_job.kind == J_COPY) {
        fil_fmt_size(a, sizeof a, fil_job.done);
        fil_fmt_size(b, sizeof b, fil_job.total);
        snprintf(t, sizeof t, "%d OF %d %s, %s OF %s", fil_job.files_done, fil_job.files,
                 fil_job.files == 1 ? "FILE" : "FILES", a, b);
    } else {
        snprintf(t, sizeof t, "%d OF %d %s", fil_job.files_done, fil_job.files, fil_job.files == 1 ? "FILE" : "FILES");
    }
    ui_text_c(&KF_NAME, SW / 2, by + FR(48), t, THEME_FILES_TEXT_DIM);
    char nm[FL_NAME + 8];
    fit(&KF_TEXT, fil_job.name, MB_W, nm, sizeof nm);
    ui_text_c(&KF_TEXT, SW / 2, by + FR(84), nm, THEME_FILES_TEXT);
    rnd_bar("STOP", fil_hot == CT_STOP, NULL, false);
}
