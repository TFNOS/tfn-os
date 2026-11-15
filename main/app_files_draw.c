// FILES: the text widths, the buttons, the icons and the drawing of each page (main/app_files.c has the rest).
#include "app_files_priv.h"

// ---- the text widths (the same rule as uikit.c: a glyph advance in quarter px; no glyph: 0, a space: 0.3 asc)
static const kglyph_t *kg(const kfont_t *f, int cp)
{
    int lo = 0, hi = f->n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (f->g[m].cp == cp) {
            return &f->g[m];
        }
        if (f->g[m].cp < cp) {
            lo = m + 1;
        } else {
            hi = m - 1;
        }
    }
    return NULL;
}
// one UTF-8 code point at s[*i] (a bad byte: '?'); *i goes to the next one
int fil_u8cp(const char *s, uint32_t *i, uint32_t n)
{
    const uint8_t *p = (const uint8_t *)s;
    uint32_t c = p[*i];
    if (c >= 0xC0 && c < 0xE0 && *i + 1 < n && (p[*i + 1] & 0xC0) == 0x80) {
        c = ((c & 0x1F) << 6) | (p[*i + 1] & 0x3F);
        *i += 2;
    } else if (c >= 0xE0 && c < 0xF0 && *i + 2 < n && (p[*i + 1] & 0xC0) == 0x80 && (p[*i + 2] & 0xC0) == 0x80) {
        c = ((c & 0x0F) << 12) | ((p[*i + 1] & 0x3F) << 6) | (p[*i + 2] & 0x3F);
        *i += 3;
    } else {
        if (c >= 0x80) {
            c = '?';
        }
        *i += 1;
    }
    return (int)c;
}
float fil_kadv(const kfont_t *f, int c)
{
    const kglyph_t *g = kg(f, c);
    return g ? g->adv4 / 4.0f : (c == ' ' ? f->asc * 0.3f : 0);
}
static float kw(const kfont_t *f, const char *s, uint32_t n)
{
    float w = 0;
    for (uint32_t i = 0; i < n;) {
        w += fil_kadv(f, fil_u8cp(s, &i, n));
    }
    return w;
}

// a copy of s that fits in w px: the end goes and an ellipsis comes, but a short extension stays (".txt")
static void fit(const kfont_t *f, const char *s, int w, char *out, int n)
{
    uint32_t len = (uint32_t)strlen(s);
    if (kw(f, s, len) <= w) {
        snprintf(out, n, "%s", s);
        return;
    }
    const char *dot = strrchr(s, '.');
    const char *ext = dot && dot > s && strlen(dot) <= 6 ? dot : "";
    uint32_t base = (uint32_t)(ext[0] ? (uint32_t)(dot - s) : len);
    float room = w - kw(f, "\xE2\x80\xA6", 3) - kw(f, ext, (uint32_t)strlen(ext));
    uint32_t i = 0, keep = 0;
    float x = 0;
    while (i < base) {
        uint32_t j = i;
        x += fil_kadv(f, fil_u8cp(s, &j, base));
        if (x > room) {
            break;
        }
        i = j;
        keep = i;
    }
    snprintf(out, n, "%.*s\xE2\x80\xA6%s", (int)keep, s, ext);
}

// a copy of s that fits in w px: the START goes (for a path: the last folders show)
static void fit_tail(const kfont_t *f, const char *s, int w, char *out, int n)
{
    uint32_t len = (uint32_t)strlen(s), i = 0;
    float room = w - kw(f, "\xE2\x80\xA6", 3);
    while (i < len && kw(f, s + i, len - i) > (i ? room : (float)w)) {
        fil_u8cp(s, &i, len);
    }
    if (i) {
        snprintf(out, n, "\xE2\x80\xA6%s", s + i);
    } else {
        snprintf(out, n, "%s", s);
    }
}

static void text_r(const kfont_t *f, int xr, int base, const char *s, uint16_t c)
{
    ui_text(f, xr - ui_text_w(f, s), base, s, c);
}

// a label in the menu face (the biggest face that fits w), in the middle of a box
static void label_c(int x, int y, int w, int h, const char *t, uint16_t c)
{
    static const mfont_t *const F[] = {&MF_N20, &MF_N16, &MF_LABEL, &MF_SMALL};
    const mfont_t *f = F[3];
    for (int i = 0; i < 4; i++) {
        if (mf_width(F[i], t, 0) <= w - 12) {
            f = F[i];
            break;
        }
    }
    mf_text_c(f, x + w / 2, y + (h + f->asc * 7 / 10) / 2, t, c, 255, 0);
}

// a button: kind 0 normal, 1 danger, 2 good
static void button(int x, int y, int w, int h, const char *t, bool hot, int kind)
{
    uint16_t bg = kind == 1 ? THEME_FILES_FAULT_BG : kind == 2 ? THEME_FILES_OK_BG : THEME_FILES_BTN;
    ui_rrect(x, y, w, h, 10, hot ? THEME_FILES_ACCENT : bg);
    label_c(x, y, w, h, t, hot ? THEME_FILES_TEXT_ON_ACCENT : THEME_FILES_TEXT);
}

// ---------------------------------------------------------------- icons

static void ico_folder(int x, int y, int w, int h, uint16_t c)
{
    uint16_t dark = ui_mix(c, rgb(0, 0, 0), 90);
    ui_rrect(x, y, w * 45 / 100, h * 40 / 100, 3, dark);
    ui_rrect(x, y + h * 20 / 100, w, h * 80 / 100, 4, c);
    ui_rrect(x + 2, y + h * 20 / 100 + 2, w - 4, 3, 1, ui_mix(c, rgb(255, 255, 255), 70));
}

static void ico_file(int x, int y, int w, int h, uint16_t c, int kind)
{
    int pw = w * 72 / 100, px = x + (w - pw) / 2, fold = pw * 32 / 100;
    ui_rrect(px, y, pw, h, 3, c);
    uint16_t dark = ui_mix(c, rgb(0, 0, 0), 110);
    ui_rrect(px + pw - fold, y, fold, fold, 2, ui_mix(c, rgb(255, 255, 255), 110));
    if (kind == K_PIC) {
        ui_circle(px + pw * 32 / 100, y + h * 44 / 100, pw / 9 + 1, rgb(255, 236, 140), 255);
        for (int k = 0; k < pw - 6; k++) {  // a hill
            int t = k * 2 < pw ? k : pw - k;
            int hh = t * h / (2 * pw) + 2;
            gfx_fill_rect(px + 3 + k, y + h - 4 - hh, 1, hh, dark);
        }
    } else {
        for (int k = 0; k < 3; k++) {
            ui_rrect(px + 4, y + h * 42 / 100 + k * h / 6, pw - 8 - (k == 2 ? pw / 4 : 0), 2, 1, dark);
        }
    }
}

static void ico_lock(int cx, int cy, int s, uint16_t c)
{
    ui_ring(cx, cy - s * 30 / 100, s * 26 / 100, s / 8 + 1, c, 255);
    ui_rrect(cx - s / 2, cy - s * 12 / 100, s, s * 62 / 100, 2, c);
    ui_circle(cx, cy + s * 16 / 100, s / 10 + 1, THEME_FILES_BG, 255);
}

static void ico_entry(const fent_t *e, int x, int y, int w, int h)
{
    switch (e->kind) {
    case K_DIR:
        ico_folder(x, y, w, h, e->lock ? THEME_FILES_FAINT : THEME_FILES_ACCENT);
        break;
    case K_PKG:
        ico_folder(x, y, w, h, e->lock ? THEME_FILES_FAINT : THEME_FILES_VIOLET);
        ui_circle(x + w / 2, y + h * 60 / 100, h / 6 + 1, rgb(255, 255, 255), 230);
        break;
    case K_TEXT:
        ico_file(x, y, w, h, THEME_FILES_BLUE, K_TEXT);
        break;
    case K_PIC:
        ico_file(x, y, w, h, THEME_FILES_GREEN, K_PIC);
        break;
    default:
        ico_file(x, y, w, h, THEME_FILES_FAINT, K_FILE);
        break;
    }
}

// the tile icons: a card (a cut corner, gold contacts) and the chip of the board
static void ico_card(int x, int y, int w, int h, uint16_t c)
{
    ui_rrect(x, y, w, h, 4, c);
    for (int k = 0; k < w / 3; k++) {
        gfx_fill_rect(x + w - w / 3 + k, y, w / 3 - k, 1, THEME_FILES_ROW);  // the cut corner
    }
    for (int k = 0; k < 4; k++) {
        ui_rrect(x + 5 + k * (w - 10) / 4, y + 5, (w - 10) / 4 - 3, h / 4, 1, rgb(230, 190, 90));
    }
}
static void ico_chip(int x, int y, int s, uint16_t c)
{
    int m = s / 6;
    for (int k = 0; k < 4; k++) {
        int o = m + 2 + k * (s - 2 * m - 4) / 3;
        gfx_fill_rect(x + o - 1, y, 3, s, ui_mix(c, rgb(0, 0, 0), 120));
        gfx_fill_rect(x, y + o - 1, s, 3, ui_mix(c, rgb(0, 0, 0), 120));
    }
    ui_rrect(x + m, y + m, s - 2 * m, s - 2 * m, 4, c);
    ui_rrect(x + s / 2 - s / 7, y + s / 2 - s / 7, s * 2 / 7, s * 2 / 7, 2, ui_mix(c, rgb(255, 255, 255), 90));
}

// ---------------------------------------------------------------- drawing

// a thick line of soft dots (the arrows, the close mark)
static void line_s(float x0, float y0, float x1, float y1, int r, uint16_t c)
{
    float dx = x1 - x0, dy = y1 - y0;
    int n = (int)sqrtf(dx * dx + dy * dy) + 1;
    for (int k = 0; k <= n; k++) {
        ui_circle((int)lroundf(x0 + dx * k / n), (int)lroundf(y0 + dy * k / n), r, c, 255);
    }
}
// a chevron: dir 0 = left, 1 = right
static void chevron(int cx, int cy, int s, int dir, uint16_t c)
{
    int d = dir ? 1 : -1;
    line_s(cx - d * s / 2.0f, cy - s, cx + d * s / 2.0f, cy, 1, c);
    line_s(cx + d * s / 2.0f, cy, cx - d * s / 2.0f, cy + s, 1, c);
}
static void cross(int cx, int cy, int s, uint16_t c)
{
    line_s(cx - s, cy - s, cx + s, cy + s, 1, c);
    line_s(cx - s, cy + s, cx + s, cy - s, 1, c);
}

// the back or up button at the top left: an arrow and a word
static void back_btn(int h, const char *word, bool hot)
{
    ui_rrect(4, 4, UP_W - 8, h - 8, 10, hot ? THEME_FILES_ACCENT : THEME_FILES_BTN);
    uint16_t c = hot ? THEME_FILES_TEXT_ON_ACCENT : THEME_FILES_TEXT;
    int w = mf_width(&MF_LABEL, word, 0), x = (UP_W - (w + 14)) / 2;
    if (w + 14 > UP_W - 18) {
        chevron(UP_W / 2 + 2, h / 2, 9, 0, c);  // (no room for the word: the arrow only)
        return;
    }
    chevron(x + 3, h / 2, 6, 0, c);
    mf_text(&MF_LABEL, x + 14, h / 2 + 5, word, c, 255, 0);
}

static void close_btn(int h, bool hot)
{
    ui_rrect(SW - CLOSE_W + 4, 4, CLOSE_W - 8, h - 8, 10, hot ? THEME_FILES_ACCENT : THEME_FILES_BTN);
    cross(SW - CLOSE_W / 2, h / 2, 7, hot ? THEME_FILES_TEXT_ON_ACCENT : THEME_FILES_TEXT);
}

static void space_text(char *out, int n, uint64_t t, uint64_t f)
{
    char a[24], b[24];
    fil_fmt_size(a, sizeof a, f);
    fil_fmt_size(b, sizeof b, t);
    snprintf(out, n, "%.15s FREE OF %.15s", a, b);
}

// a message of one or two lines (a split at a space near the middle), else an ellipsis
static void wrap2(const kfont_t *f, const char *s, int w, char *a, char *b, int n)
{
    b[0] = 0;
    if (ui_text_w(f, s) <= w) {
        snprintf(a, n, "%s", s);
        return;
    }
    int len = (int)strlen(s), best = -1, bd = 1 << 30;
    for (int i = 1; i < len - 1; i++) {
        if (s[i] != ' ') {
            continue;
        }
        snprintf(a, n, "%.*s", i, s);
        if (ui_text_w(f, a) > w || ui_text_w(f, s + i + 1) > w) {
            continue;
        }
        int d = abs(len / 2 - i);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best < 0) {
        fit(f, s, w, a, n);
        return;
    }
    snprintf(a, n, "%.*s", best, s);
    snprintf(b, n, "%s", s + best + 1);
}

static void scroll_bar_x(int x, int y0, int h, float pos, int max, int total)
{
    if (max <= 0) {
        return;
    }
    int th = h * h / (total > h ? total : h);
    if (th < 18) {
        th = 18;
    }
    float f = pos / max;
    f = f < 0 ? 0 : f > 1 ? 1 : f;
    ui_rrect(x, y0 + 3, 5, h - 6, 2, THEME_FILES_LINE);
    ui_rrect(x, y0 + 3 + (int)(f * (h - 6 - th)), 5, th, 2, THEME_FILES_TEXT_DIM);
}
static void scroll_bar(int y0, int h, float pos, int max, int total)
{
    scroll_bar_x(SBAR_X, y0, h, pos, max, total);
}

// ---- the home page
static void tile(int x, int y, int r, bool hot, bool focus)
{
    bool ok = r == RT_CARD ? fil_card_ok : fil_data_ok;
    ui_rrect(x, y, TILE_W, TILE_H, 14, hot ? THEME_FILES_HOT : THEME_FILES_ROW);
    if (focus) {
        ui_rbox(x, y, TILE_W, TILE_H, 14, hot ? THEME_FILES_HOT : THEME_FILES_ROW, THEME_FILES_ACCENT, 3);
    }
#if WIDE_UI
    int is = 46, ix = x + 18, iy = y + 18, tx = x + 82, b1 = y + 44, b2 = y + 74;
    const kfont_t *lf = &KF_TEXT;
#else
    int is = 40, ix = x + 14, iy = y + 14, tx = x + 70, b1 = y + 38, b2 = y + 62;
    const kfont_t *lf = &KF_NAME;
#endif
    if (r == RT_CARD) {
        ico_card(ix + is / 8, iy, is * 3 / 4, is, ok ? THEME_FILES_BLUE : THEME_FILES_FAINT);
    } else {
        ico_chip(ix, iy, is, ok ? THEME_FILES_GREEN : THEME_FILES_FAINT);
    }
    mf_text(&MF_N24, tx, b1, fil_root_name(r), ok ? THEME_FILES_TEXT : THEME_FILES_TEXT_DIM, 255, 0);
    if (!ok) {
        ui_text(lf, tx, b2, r == RT_CARD ? "NO CARD" : "NOT THERE", THEME_FILES_ACCENT);
        ui_text(&KF_LABEL, x + 18, y + TILE_H - 14,
                r == RT_CARD ? "PUT IN A CARD, THEN TAP HERE" : "THE BOARD FILES DID NOT MOUNT", THEME_FILES_TEXT_DIM);
        return;
    }
    uint64_t t = r == RT_CARD ? fil_card_t : fil_data_t, f = r == RT_CARD ? fil_card_f : fil_data_f;
    char line[48];
    space_text(line, sizeof line, t, f);
    ui_text(lf, tx, b2, line, THEME_FILES_TEXT);
    // the bar of the used space
    int bx = x + 18, bw = TILE_W - 36, by = y + TILE_H - (WIDE_UI ? 26 : 18), bh = WIDE_UI ? 10 : 8;
    ui_rrect(bx, by, bw, bh, bh / 2, THEME_FILES_LINE);
    int used = t ? (int)((t - f) * (uint64_t)bw / t) : 0;
    if (used > 0) {
        ui_rrect(bx, by, used < bh ? bh : used, bh, bh / 2,
                 f * 10 < t     ? THEME_FILES_FAULT
                 : r == RT_CARD ? THEME_FILES_BLUE
                                : THEME_FILES_GREEN);
    }
}

#if BOARD_ROUND
#include "app_files_round.inc"
#endif

static void home_render(void)
{
#if BOARD_ROUND
    rnd_home();
    return;
#endif
    gfx_fill(THEME_FILES_BG);
    mf_text(&MF_N24, 16, HOME_H / 2 + 12, "FILES", THEME_FILES_ACCENT, 255, 0);
    button(TOOLS_X, 4, TOOLS_W, HOME_H - 8, "CARD TOOLS", fil_hot == CT_TOOLS, 0);
    if (fil_focus == 2) {
        ui_rbox(TOOLS_X, 4, TOOLS_W, HOME_H - 8, 10, THEME_FILES_BTN, THEME_FILES_ACCENT, 3);
    }
    tile(6, TILE_Y, RT_CARD, fil_hot == CT_CARD, fil_focus == 0);
    tile(TILE2_X, TILE2_Y, RT_BOARD, fil_hot == CT_BOARD, fil_focus == 1);
}

// ---- the list
static void draw_row(int i, int y)
{
    const fent_t *e = &fil_ent[i];
    bool hot = fil_hot == i;
    ui_rrect(4, y + 2, ROW_W - 4, ROW_H - 4, 8, hot ? THEME_FILES_HOT : THEME_FILES_ROW);
    if (fil_focus == i) {
        ui_rbox(4, y + 2, ROW_W - 4, ROW_H - 4, 8, hot ? THEME_FILES_HOT : THEME_FILES_ROW, THEME_FILES_ACCENT, 3);
    }
    int ih = ICON_W * 80 / 100;
    ico_entry(e, ICON_X, y + (ROW_H - ih) / 2, ICON_W, ih);
    bool folder = e->kind == K_DIR || (e->kind == K_PKG && e->lock);
    char r[48] = "", nm[FL_NAME + 8];
#if !TWO_LINES
    if (e->kind == K_PKG && !e->lock) {
        snprintf(r, sizeof r, "APP");
    } else if (!folder) {
        fil_fmt_size(r, sizeof r, e->size);
    }
    int rx = SIZE_R - (folder ? 16 : 0);
    int rw = r[0] ? ui_text_w(&KF_NAME, r) : 0;
    if (r[0]) {
        text_r(&KF_NAME, rx, y + 28, r, e->kind == K_PKG ? THEME_FILES_VIOLET : THEME_FILES_TEXT_DIM);
    }
    if (folder) {
        chevron(SIZE_R - 4, y + ROW_H / 2, 5, 1, THEME_FILES_FAINT);
    }
    int lx = rx - rw - (r[0] ? 16 : 6);
    if (e->lock) {
        ico_lock(lx - 6, y + ROW_H / 2, 14, THEME_FILES_TEXT_DIM);
    }
    int maxw = lx - (e->lock ? 22 : 0) - NAME_X - 8;
    fit(&KF_TEXT, e->name, maxw, nm, sizeof nm);
    ui_text(&KF_TEXT, NAME_X, y + 29, nm, e->lock ? THEME_FILES_TEXT_DIM : THEME_FILES_TEXT);
#else
    int right = SIZE_R - (folder ? 18 : 0) - (e->lock ? 22 : 0);
    fit(&KF_TEXT, e->name, right - NAME_X - 4, nm, sizeof nm);
    ui_text(&KF_TEXT, NAME_X, y + 26, nm, e->lock ? THEME_FILES_TEXT_DIM : THEME_FILES_TEXT);
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
    ui_text(&KF_LABEL, NAME_X, y + 47, r, e->kind == K_PKG && !e->lock ? THEME_FILES_VIOLET : THEME_FILES_TEXT_DIM);
    if (folder) {
        chevron(SIZE_R - 4, y + ROW_H / 2, 5, 1, THEME_FILES_FAINT);
    }
    if (e->lock) {
        ico_lock(right + 10, y + ROW_H / 2, 15, THEME_FILES_TEXT_DIM);
    }
#endif
}

static void list_header(void)
{
    gfx_fill_rect(0, 0, SW, HEAD_H, THEME_FILES_HEAD);
    gfx_fill_rect(0, HEAD_H - 1, SW, 1, THEME_FILES_LINE);
    back_btn(HEAD_H, "UP", fil_hot == CT_UP);
    char p[FL_PATH + 16], q[FL_PATH + 16];
    snprintf(p, sizeof p, "%s%s", fil_root_name(fil_root), fil_rel);
    fit_tail(&KF_NAME, p, PATH_W, q, sizeof q);
    ui_text(&KF_NAME, PATH_X, PATH_B1, q, THEME_FILES_TEXT);
    char sp[48];
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
    fit(&KF_LABEL, sp, PATH_W, q, sizeof q);
    ui_text(&KF_LABEL, PATH_X, PATH_B2, q, fil_rel_lock ? THEME_FILES_ACCENT : THEME_FILES_TEXT_DIM);
    // MORE: three dots
    bool h = fil_hot == CT_MORE;
    ui_rrect(MORE_X + 4, 4, SW - MORE_X - 8, HEAD_H - 8, 10, h ? THEME_FILES_ACCENT : THEME_FILES_BTN);
    int cx = MORE_X + (SW - MORE_X) / 2;
    for (int k = -1; k <= 1; k++) {
        ui_circle(cx + k * 10, HEAD_H / 2, 3, h ? THEME_FILES_TEXT_ON_ACCENT : THEME_FILES_TEXT, 255);
    }
}

// the folder is read now (a big folder: some frames): READING and the count, in the middle of a box
static void reading_render(int cx, int cy)
{
    char t[48];
    snprintf(t, sizeof t, "%d READ", fil_n + fil_skip);
    ui_text_c(&KF_TEXT, cx, cy - 4, "READING THE FOLDER...", THEME_FILES_TEXT_DIM);
    ui_text_c(&KF_LABEL, cx, cy + 20, t, THEME_FILES_FAINT);
}

#if T5P
static void t5_list_render(void);
#endif
static void list_render(void)
{
#if BOARD_ROUND
    rnd_list();
    return;
#endif
#if T5P
    t5_list_render();
    return;
#endif
    gfx_fill(THEME_FILES_BG);
    if (fil_list_busy()) {
        reading_render(SW / 2, LIST_Y + LST_H / 2);
        list_header();
        return;
    }
    int sc = (int)lroundf(fil_scroll);
    for (int i = 0; i < fil_n; i++) {
        int y = LIST_Y + i * ROW_H - sc;
        if (y + ROW_H <= LIST_Y || y >= SH) {
            continue;
        }
        draw_row(i, y);
    }
    if (fil_skip) {
        char t[64];
        snprintf(t, sizeof t, "%d MORE ARE NOT IN THE LIST (THE LIMIT IS %d)", fil_skip, FL_MAX);
        int y = LIST_Y + fil_n * ROW_H - sc;
        if (y < SH) {
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
    if (LIST_Y > HEAD_H) {
        gfx_fill_rect(0, HEAD_H, SW, LIST_Y - HEAD_H, THEME_FILES_BG);  // (a row under the header: no part shows)
    }
    list_header();
}

#if T5P

// the folder pane
static void side_render(void)
{
    gfx_fill_rect(0, LIST_Y, SIDE_W, SIDE_H, THEME_FILES_PANE);
    const int sc = (int)lroundf(ks_pos(&fil_kside));
    const int n = fil_side_roots ? 2 : fil_nside;
    char nm[FL_NAME + 8], t[48];
    for (int i = 0; i < n; i++) {
        int y = SIDE_TOP + i * SROW_H - sc;
        if (y + SROW_H <= SIDE_TOP || y >= SH) {
            continue;
        }
        const bool cur = i == fil_side_cur, hot = fil_hot == CT_SIDE + i;
        ui_rrect(4, y + 2, SIDE_W - 8, SROW_H - 4, 8, hot ? THEME_FILES_HOT : cur ? THEME_FILES_SEL : THEME_FILES_PANE);
        if (cur) {
            ui_rrect(4, y + 8, 4, SROW_H - 16, 2, THEME_FILES_ACCENT);
        }
        if (fil_side_roots) {
            const bool ok = i == RT_CARD ? fil_card_ok : fil_data_ok;
            if (i == RT_CARD) {
                ico_card(14, y + 8, 30, 36, ok ? THEME_FILES_BLUE : THEME_FILES_FAINT);
            } else {
                ico_chip(14, y + 12, 28, ok ? THEME_FILES_GREEN : THEME_FILES_FAINT);
            }
            ui_text(&KF_TEXT, 50, y + 24, fil_root_name(i), ok ? THEME_FILES_TEXT : THEME_FILES_TEXT_DIM);
            if (ok) {
                fil_fmt_size(t, sizeof t, i == RT_CARD ? fil_card_f : fil_data_f);
                snprintf(t + strlen(t), sizeof t - strlen(t), " FREE");
            } else {
                snprintf(t, sizeof t, i == RT_CARD ? "NO CARD" : "NOT THERE");
            }
            ui_text(&KF_LABEL, 50, y + 43, t, ok ? THEME_FILES_TEXT_DIM : THEME_FILES_ACCENT);
            continue;
        }
        const side_t *e = &fil_side[i];
        ico_folder(14, y + (SROW_H - 22) / 2, 28, 22,
                   e->lock ? THEME_FILES_FAINT
                   : cur   ? THEME_FILES_ACCENT
                           : THEME_FILES_ACCENT_DIM);
        fit(&KF_TEXT, e->name, SIDE_W - 50 - 12, nm, sizeof nm);
        ui_text(&KF_TEXT, 50, y + 32, nm, e->lock ? THEME_FILES_TEXT_DIM : THEME_FILES_TEXT);
    }
    if (!fil_side_roots && !fil_nside) {
        ui_text_c(&KF_LABEL, SIDE_W / 2, SIDE_TOP + 30, "NO FOLDERS", THEME_FILES_FAINT);
    }
    int at, sz, al;
    if (ks_bar(&fil_kside, SIDE_H - 6, &at, &sz, &al) && al > 64) {
        ui_rrect(SIDE_W - 6, SIDE_TOP + 3 + at, 3, sz, 1, THEME_FILES_TEXT_DIM);
    }
}

// a row of the list pane: the icon, the name, and under it the size and the date (or the kind)
static void mid_row(int i, int y)
{
    const fent_t *e = &fil_ent[i];
    const bool hot = fil_hot == i, sel = fil_pv == i;
    const int x = MID_X, w = MID_W - 10;
    ui_rrect(x, y + 2, w, ROW_H - 4, 8, hot ? THEME_FILES_HOT : sel ? THEME_FILES_SEL : THEME_FILES_ROW);
    if (sel) {
        ui_rrect(x, y + 8, 4, ROW_H - 16, 2, THEME_FILES_ACCENT);
    }
    ico_entry(e, x + 10, y + (ROW_H - 24) / 2, 30, 24);
    const bool folder = e->kind == K_DIR || (e->kind == K_PKG && e->lock);
    int right = x + w - (folder ? 22 : 10) - (e->lock ? 20 : 0);
    char nm[FL_NAME + 8], r[48];
    fit(&KF_TEXT, e->name, right - (x + 48), nm, sizeof nm);
    ui_text(&KF_TEXT, x + 48, y + 24, nm, e->lock ? THEME_FILES_TEXT_DIM : THEME_FILES_TEXT);
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
        snprintf(r, sizeof r, "%s   %s", a, d);
    }
    ui_text(&KF_LABEL, x + 48, y + 43, r, e->kind == K_PKG && !e->lock ? THEME_FILES_VIOLET : THEME_FILES_TEXT_DIM);
    if (folder) {
        chevron(x + w - 12, y + ROW_H / 2, 5, 1, THEME_FILES_FAINT);
    }
    if (e->lock) {
        ico_lock(right + 10, y + ROW_H / 2, 14, THEME_FILES_TEXT_DIM);
    }
}

static void mid_render(void)
{
    if (fil_list_busy()) {
        reading_render(MID_X + MID_W / 2, LIST_Y + LST_H / 2);
        return;
    }
    const int sc = (int)lroundf(fil_scroll);
    for (int i = 0; i < fil_n; i++) {
        int y = LIST_Y + i * ROW_H - sc;
        if (y + ROW_H <= LIST_Y || y >= SH) {
            continue;
        }
        mid_row(i, y);
    }
    const int cx = MID_X + MID_W / 2;
    if (fil_skip) {
        char t[64];
        snprintf(t, sizeof t, "%d MORE ARE NOT HERE", fil_skip);
        int y = LIST_Y + fil_n * ROW_H - sc;
        if (y < SH) {
            ui_text_c(&KF_LABEL, cx, y + ROW_H / 2 + 5, t, THEME_FILES_TEXT_DIM);
        }
    }
    if (!fil_n && !fil_skip) {
        int cy = LIST_Y + LST_H / 2;
        ui_text_c(&KF_TEXT, cx, cy - 4, "AN EMPTY FOLDER", THEME_FILES_TEXT_DIM);
        if (!fil_rel_lock) {
            ui_text_c(&KF_LABEL, cx, cy + 20, "HOLD HERE FOR A NEW FOLDER", THEME_FILES_FAINT);
        }
    }
    scroll_bar_x(MID_X + MID_W - 7, LIST_Y, LST_H, fil_scroll, fil_max_scroll(), (fil_n + (fil_skip ? 1 : 0)) * ROW_H);
}

// the preview pane
static void pv_render(void)
{
    gfx_fill_rect(PV_X, LIST_Y, PV_W, SH - LIST_Y, THEME_FILES_PANE);
    if (fil_list_busy()) {
        return;
    }
    char t[FL_NAME + 16], u[FL_NAME + 16];
    const int cx = PV_X + PV_W / 2;
    if (fil_pv < 0 || fil_pv >= fil_n) {
        // the folder now: its count
        int nd = 0, nf = 0;
        uint64_t sum = 0;
        for (int i = 0; i < fil_n; i++) {
            if (fil_ent[i].kind == K_DIR || fil_ent[i].kind == K_PKG) {
                nd++;
            } else {
                nf++;
                sum += fil_ent[i].size;
            }
        }
        ico_folder(cx - 30, LIST_Y + 40, 60, 46, fil_rel_lock ? THEME_FILES_FAINT : THEME_FILES_ACCENT_DIM);
        snprintf(t, sizeof t, "%d %s", nd, nd == 1 ? "FOLDER" : "FOLDERS");
        ui_text_c(&KF_TEXT, cx, LIST_Y + 128, t, THEME_FILES_TEXT);
        char a[16];
        fil_fmt_size(a, sizeof a, sum);
        snprintf(t, sizeof t, "%d %s, %s", nf, nf == 1 ? "FILE" : "FILES", a);
        ui_text_c(&KF_TEXT, cx, LIST_Y + 154, t, THEME_FILES_TEXT);
        if (nf) {
            wrap2(&KF_LABEL, "TAP A FILE TO SEE IT HERE", PV_IW, t, u, sizeof t);
            ui_text_c(&KF_LABEL, cx, LIST_Y + 200, t, THEME_FILES_FAINT);
            if (u[0]) {
                ui_text_c(&KF_LABEL, cx, LIST_Y + 218, u, THEME_FILES_FAINT);
            }
        }
        return;
    }
    const fent_t *e = &fil_ent[fil_pv];
    // the name (one or two lines) and the facts
    wrap2(&KF_TEXT, e->name, PV_IW, t, u, sizeof t);
    if (u[0]) {
        char v[FL_NAME + 16];
        fit(&KF_TEXT, u, PV_IW, v, sizeof v);
        ui_text(&KF_TEXT, PV_IX, LIST_Y + 20, t, THEME_FILES_TEXT);
        ui_text(&KF_TEXT, PV_IX, LIST_Y + 41, v, THEME_FILES_TEXT);
    } else {
        ui_text(&KF_TEXT, PV_IX, LIST_Y + 28, t, THEME_FILES_TEXT);
    }
    char a[16], d[24];
    fil_fmt_size(a, sizeof a, e->size);
    fil_fmt_date(d, sizeof d, e->mtime);
    if (strlen(d) > 10) {
        d[10] = 0;
    }
    if (e->kind == K_PKG) {
        snprintf(t, sizeof t, "AN APP PACKAGE");
    } else {
        snprintf(t, sizeof t, "%s   %s", a, d);
    }
    ui_text(&KF_LABEL, PV_IX, PV_BODY_Y - 7, t, THEME_FILES_TEXT_DIM);
    gfx_fill_rect(PV_IX, PV_BODY_Y - 1, PV_IW, 1, THEME_FILES_LINE);
    const int by = PV_BODY_Y + 4, bh = PV_BODY_H - 4, mid = by + bh / 2;
    if (fil_pv_state == 2) {
        wrap2(&KF_NAME, fil_pv_err, PV_IW, t, u, sizeof t);
        ui_text_c(&KF_NAME, cx, mid - (u[0] ? 6 : -6), t, THEME_FILES_ACCENT);
        if (u[0]) {
            ui_text_c(&KF_NAME, cx, mid + 14, u, THEME_FILES_ACCENT);
        }
    } else if (e->kind == K_TEXT) {
        char line[320];
        for (int i = 0; i < fil_pv_nl; i++) {
            int base = by + 15 + i * PV_LH;
            if (base > by + bh - 3) {
                break;  // (the text does not obey a clip box: only whole lines)
            }
            int n = fil_pv_llen[i] < sizeof line - 1 ? fil_pv_llen[i] : (int)sizeof line - 1;
            memcpy(line, fil_pv_txt + fil_pv_loff[i], (size_t)n);
            line[n] = 0;
            bool heb = false;
            for (int k = 0; k < n; k++) {
                if ((uint8_t)line[k] == 0xD7) {
                    heb = true;
                }
            }
            if (heb) {
                text_r(&KF_NAME, PV_IX + PV_IW, base, line, THEME_FILES_TEXT);
            } else {
                ui_text(&KF_NAME, PV_IX, base, line, THEME_FILES_TEXT);
            }
        }
        if (!fil_pv_nl || (fil_pv_nl == 1 && !fil_pv_llen[0])) {
            ui_text_c(&KF_TEXT, cx, mid + 6, "AN EMPTY FILE", THEME_FILES_TEXT_DIM);
        }
    } else if (e->kind == K_PIC) {
        if (fil_pv_state == 0 || !fil_pv_pic) {
            ui_text_c(&KF_NAME, cx, mid + 6, "OPENING...", THEME_FILES_TEXT_DIM);
            fil_pv_drawn = true;
        } else {
            int ox = PV_IX + (PV_IW - fil_pv_w) / 2, oy = by + (bh - fil_pv_h) / 2;
            for (int y = 0; y < fil_pv_h; y++) {
                int Y = oy + y;
                if (Y < 0 || Y >= gfx_h || ox < 0 || ox + fil_pv_w > gfx_w) {
                    continue;
                }
                memcpy(g_fb + Y * gfx_w + ox, fil_pv_pic + y * fil_pv_w, (size_t)fil_pv_w * 2);
            }
        }
    } else {
        // an app package or another file: a big icon and its kind
        ico_entry(e, cx - 32, mid - 50, 64, 52);
        const char *k = e->kind == K_PKG ? (e->lock ? "A LOCKED APP" : "INSTALL PUTS IT IN THE MENU")
                                         : "NO PREVIEW FOR THIS FILE";
        wrap2(&KF_NAME, k, PV_IW, t, u, sizeof t);
        ui_text_c(&KF_NAME, cx, mid + 30, t, THEME_FILES_TEXT_DIM);
        if (u[0]) {
            ui_text_c(&KF_NAME, cx, mid + 50, u, THEME_FILES_TEXT_DIM);
        }
    }
    // OPEN (or INSTALL, INFO) and MORE
    const int bw = (PV_W - 30) / 2;
    button(PV_X + 10, PV_BTN_Y, bw, PV_BTN_H, fil_pv_open_word(), fil_hot == CT_PVOPEN,
           e->kind == K_PKG && !e->lock ? 2 : 0);
    button(PV_X + 20 + bw, PV_BTN_Y, bw, PV_BTN_H, "MORE", fil_hot == CT_PVMORE, 0);
}

static void t5_list_render(void)
{
    gfx_fill(THEME_FILES_BG);
    mid_render();
    side_render();
    pv_render();
    gfx_fill_rect(SIDE_W, LIST_Y, 1, SH - LIST_Y, THEME_FILES_LINE);
    gfx_fill_rect(PV_X - 1, LIST_Y, 1, SH - LIST_Y, THEME_FILES_LINE);
    if (LIST_Y > HEAD_H) {
        gfx_fill_rect(0, HEAD_H, SW, LIST_Y - HEAD_H, THEME_FILES_BG);
    }
    list_header();
}
#endif

// ---- the panel
static void act_render(void)
{
#if BOARD_ROUND
    rnd_act();
    return;
#endif
    gfx_fill(THEME_FILES_BG);
    gfx_fill_rect(0, 0, SW, PAN_H, THEME_FILES_HEAD);
    gfx_fill_rect(0, PAN_H - 1, SW, 1, THEME_FILES_LINE);
    int tx = 16;
    if (fil_pkind == PK_ENTRY && fil_sel >= 0 && fil_sel < fil_n) {
        int ih = ICON_W * 80 / 100;
        ico_entry(&fil_ent[fil_sel], 12, (PAN_H - ih) / 2, ICON_W, ih);
        tx = 12 + ICON_W + 10;
    }
    char t[FL_PATH + 16];
    int tw = SW - CLOSE_W - tx - 8;
    if (fil_pkind == PK_MORE) {
        fit_tail(&KF_TEXT, fil_ptitle, tw, t, sizeof t);
    } else {
        fit(&KF_TEXT, fil_ptitle, tw, t, sizeof t);
    }
    ui_text(&KF_TEXT, tx, PAN_H / 2 - 1, t, THEME_FILES_TEXT);
    uint16_t nc = strncmp(fil_pnote, "LOCKED", 6) && strncmp(fil_pnote, "NO CARD", 7) ? THEME_FILES_TEXT_DIM
                                                                                      : THEME_FILES_ACCENT;
    int rows = (fil_nact + PAN_COLS - 1) / PAN_COLS;
    if (ui_text_w(&KF_LABEL, fil_pnote) > tw && rows == 1) {
        // a long note with one row of buttons: the note in the free part under the buttons, on one or two lines
        char u[FL_PATH + 16];
        int bx, by, bw, bh;
        fil_act_box(0, &bx, &by, &bw, &bh);
        int y = (by + bh + SH) / 2;
        wrap2(&KF_NAME, fil_pnote, SW - 24, t, u, sizeof t);
        ui_text_c(&KF_NAME, SW / 2, u[0] ? y - 4 : y + 6, t, nc);
        if (u[0]) {
            ui_text_c(&KF_NAME, SW / 2, y + 16, u, nc);
        }
        ui_text(&KF_LABEL, tx, PAN_H - 8, fil_pkind == PK_TOOLS ? "NO CARD" : "LOCKED", nc);
    } else {
        fit(&KF_LABEL, fil_pnote, tw, t, sizeof t);
        ui_text(&KF_LABEL, tx, PAN_H - 8, t, nc);
    }
    close_btn(PAN_H, fil_hot == CT_CLOSE);
    for (int i = 0; i < fil_nact; i++) {
        int x, y, w, h;
        fil_act_box(i, &x, &y, &w, &h);
        int a = fil_act[i];
        button(x, y, w, h, fil_act_label(a), fil_hot == i, a == A_DELETE || a == A_FORMAT ? 1 : a == A_INSTALL ? 2 : 0);
        if (fil_focus == i) {
            ui_rbox(x, y, w, h, 10, a == A_DELETE || a == A_FORMAT ? THEME_FILES_FAULT_BG : THEME_FILES_BTN,
                    THEME_FILES_ACCENT, 3);
        }
        if (fil_focus == i) {
            label_c(x, y, w, h, fil_act_label(a), THEME_FILES_TEXT);
        }
    }
}

// ---- the question
static void ask_render(int64_t now)
{
#if BOARD_ROUND
    rnd_ask(now);
    return;
#endif
    gfx_fill(THEME_FILES_BG);
    int w = SW - 16 - (fil_qclose ? 2 * CLOSE_W : 0);
    static const kfont_t *const F1[] = {&KF_BIG, &KF_TEXT, &KF_NAME};
    const kfont_t *f = ui_text_fit(F1, 3, fil_q1, w);
    char t[FL_NAME + 48], u[FL_NAME + 48];
    fit(f, fil_q1, w, t, sizeof t);
    ui_text_c(f, SW / 2, ASK_B1, t, THEME_FILES_TEXT);
    const kfont_t *f2 = BOARD_BAR_SCREEN ? &KF_NAME : &KF_LABEL;
    wrap2(f2, fil_q2, SW - (BOARD_BAR_SCREEN ? 16 : 40), t, u, sizeof t);
    if (u[0]) {
        ui_text_c(f2, SW / 2, ASK_B2 - (BOARD_BAR_SCREEN ? 0 : 8), t, THEME_FILES_TEXT_DIM);
        ui_text_c(f2, SW / 2, ASK_B2 + (BOARD_BAR_SCREEN ? 0 : 10), u, THEME_FILES_TEXT_DIM);
    } else {
        ui_text_c(f2, SW / 2, ASK_B2, t, THEME_FILES_TEXT_DIM);
    }
    bool danger = fil_qkind == Q_DELETE || fil_qkind == Q_REPLACE || fil_qkind == Q_FORMAT1 || fil_qkind == Q_FORMAT2;
    for (int a = 0; a < 2; a++) {
        if (!fil_qa[a]) {
            continue;
        }
        int x = a ? SW - ASK_W : 0, y = ASK_Y + 2, h = SH - ASK_Y - 6, bw = ASK_W - 8;
        bool hot = fil_qhot == a;
        uint16_t bg = a == 0   ? (fil_qkind == Q_INSTALL ? THEME_FILES_OK_BG : THEME_FILES_BTN)
                      : danger ? THEME_FILES_FAULT_BG
                               : THEME_FILES_OK_BG;
        ui_rrect(x + 4, y, bw, h, 14, hot && !(a == 1 && fil_qhold_ms) ? THEME_FILES_ACCENT : bg);
        if (a == 1 && fil_qhold_ms) {
            // the fill of the hold
            float p = fil_qhold_t0 >= 0 && hot ? (float)(now - fil_qhold_t0) / fil_qhold_ms : 0;
            p = p > 1 ? 1 : p;
            int fw = (int)(bw * p);
            if (fw > 0) {
                ui_rrect(x + 4, y, fw < 28 ? 28 : fw, h, 14, THEME_FILES_ACCENT);
            }
        }
        if (fil_qfocus == a) {
            ui_rbox(x + 4, y, bw, h, 14, bg, THEME_FILES_ACCENT, 3);
        }
        bool dark = hot;  // (also on the fill of a hold: dark text on amber)
        const mfont_t *mf = mf_width(&MF_N24, fil_qa[a], 0) <= bw - 12 ? &MF_N24 : &MF_N16;
        int cy = y + h / 2 + mf->asc * 7 / 20;
        if (a == 1 && fil_qhold_ms) {
            cy -= 8;
        }
        mf_text_c(mf, x + 4 + bw / 2, cy, fil_qa[a], dark ? THEME_FILES_TEXT_ON_ACCENT : THEME_FILES_TEXT, 255, 0);
        if (a == 1 && fil_qhold_ms) {
            const char *h2 = fil_qarmed ? "PUSH AGAIN" : "HOLD 1 S";
            mf_text_c(&MF_SMALL, x + 4 + bw / 2, cy + 20, h2, dark ? THEME_FILES_TEXT_ON_ACCENT : THEME_FILES_TEXT, 255,
                      0);
        }
    }
    if (fil_qclose) {
        close_btn(48, fil_qhot == 2);
    }
}

// ---- the info page
static void view_header(const char *name, const char *note, bool hot)
{
    gfx_fill_rect(0, 0, SW, TXT_TOP, THEME_FILES_HEAD);
    gfx_fill_rect(0, TXT_TOP - 1, SW, 1, THEME_FILES_LINE);
    back_btn(TXT_TOP, "BACK", hot);
    char t[FL_NAME + 16];
    int x = UP_W + 10, w = SW - x - 10;
    fit(&KF_TEXT, name, w, t, sizeof t);
    ui_text(&KF_TEXT, x, note && note[0] ? TXT_TOP / 2 - 1 : TXT_TOP / 2 + 7, t, THEME_FILES_TEXT);
    if (note && note[0]) {
        fit(&KF_LABEL, note, w, t, sizeof t);
        ui_text(&KF_LABEL, x, TXT_TOP - 8, t, THEME_FILES_TEXT_DIM);
    }
}

static void info_render(void)
{
#if BOARD_ROUND
    rnd_info();
    return;
#endif
    gfx_fill(THEME_FILES_BG);
    view_header(fil_info_name, "INFO", fil_hot == CT_CLOSE);
#if BOARD_BAR_SCREEN
    int y0 = TXT_TOP + 22, step = 25, vx = 110;
    const kfont_t *vf = &KF_NAME;
#else
    int y0 = TXT_TOP + 30, step = 36, vx = 84;
    const kfont_t *vf = &KF_TEXT;
#endif
    for (int i = 0; i < fil_info_n; i++) {
        char t[FL_PATH];
        ui_text(&KF_LABEL, 16, y0 + i * step, fil_info_l[i], THEME_FILES_TEXT_DIM);
        if (!strcmp(fil_info_l[i], "PLACE")) {
            fit_tail(vf, fil_info_v[i], SW - vx - 12, t, sizeof t);
        } else {
            fit(vf, fil_info_v[i], SW - vx - 12, t, sizeof t);
        }
        ui_text(vf, vx, y0 + i * step, t, !strcmp(fil_info_l[i], "LOCKED") ? THEME_FILES_ACCENT : THEME_FILES_TEXT);
    }
}

// ---- the text page
static void text_render(void)
{
#if BOARD_ROUND
    rnd_text();
    return;
#endif
    gfx_fill(THEME_FILES_BG);
    int sc = (int)lroundf(fil_tscroll), first = sc / TXT_LH;
    if (first < 0) {
        first = 0;
    }
    char line[320];
    for (int i = first; i < fil_nl; i++) {
        int y = TXT_TOP + 4 + i * TXT_LH - sc;
        if (y >= SH) {
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
    if (l2 > fil_nl) {
        l2 = fil_nl;
    }
    if (l1 > l2) {
        l1 = l2;
    }
    int k = snprintf(note, sizeof note, "LINES %d-%d OF %d", l1, l2, fil_nl);
    if (fil_tsize > TXT_MAX && k > 0 && k < (int)sizeof note) {
        char a[24];
        fil_fmt_size(a, sizeof a, fil_tsize);
        snprintf(note + k, sizeof note - k, ". THE FIRST 64 KB OF %s", a);
    }
    view_header(fil_tname, note, fil_hot == CT_CLOSE);
}

// ---- the picture page
static void pic_render(int64_t now)
{
#if BOARD_ROUND
    rnd_pic(now);
    return;
#endif
    gfx_fill(THEME_FILES_PIC_BG);
    if (fil_pstate == 0) {
        ui_text_c(&KF_TEXT, SW / 2, SH / 2 + 6, "OPENING THE PICTURE...", THEME_FILES_TEXT_DIM);
    } else if (fil_pstate == 2) {
        ui_text_c(&KF_TEXT, SW / 2, SH / 2 - 4, fil_perr, THEME_FILES_ACCENT);
        ui_text_c(&KF_LABEL, SW / 2, SH / 2 + 20, "A TAP SHOWS THE BAR. BACK CLOSES THE PICTURE", THEME_FILES_TEXT_DIM);
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
#ifndef ESP_PLATFORM
        char a[64], b[64];
        wrap2(&KF_NAME, "THE PC HAS NO JPEG DECODER: A TEST PICTURE", fil_pw - 16, a, b, sizeof a);
        ui_text_c(&KF_NAME, SW / 2, SH / 2 - (b[0] ? 5 : -5), a, THEME_FILES_PIC_TEXT);
        if (b[0]) {
            ui_text_c(&KF_NAME, SW / 2, SH / 2 + 15, b, THEME_FILES_PIC_TEXT);
        }
#endif
    }
    if (fil_pbar_until > now) {
        char note[40];
        snprintf(note, sizeof note, "%d x %d ON THE SCREEN", fil_pw, fil_ph);
        view_header(fil_pname, fil_pstate == 1 ? note : "", fil_hot == CT_CLOSE);
    }
}

// ---- the job page
static void job_render(void)
{
#if BOARD_ROUND
    rnd_job();
    return;
#endif
    gfx_fill(THEME_FILES_BG);
    mf_text(&MF_N20, 16, BOARD_BAR_SCREEN ? 32 : 40, fil_job.title, THEME_FILES_ACCENT, 255, 0);
    int by = BOARD_BAR_SCREEN ? 46 : 70, bw = SW - 32;
    if (fil_job.kind == J_FORMAT || fil_job.kind == J_NONE) {
        ui_text(&KF_TEXT, 16, by + 30, "FORMATTING THE CARD...", THEME_FILES_TEXT);
        ui_text(&KF_NAME, 16, by + 56, "DO NOT TAKE OUT THE CARD", THEME_FILES_TEXT_DIM);
        return;
    }
    float p = fil_job.kind == J_COPY && fil_job.total ? (float)fil_job.done / fil_job.total
              : fil_job.n                             ? (float)fil_job.i / fil_job.n
                                                      : 1;
    p = p < 0 ? 0 : p > 1 ? 1 : p;
    ui_rrect(16, by, bw, 14, 7, THEME_FILES_LINE);
    if (p > 0) {
        ui_rrect(16, by, (int)(bw * p) < 14 ? 14 : (int)(bw * p), 14, 7, THEME_FILES_ACCENT);
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
    ui_text(&KF_NAME, 16, by + (BOARD_BAR_SCREEN ? 34 : 38), t, THEME_FILES_TEXT_DIM);
    char nm[FL_NAME + 8];
    fit(&KF_TEXT, fil_job.name, SW - 32, nm, sizeof nm);
    ui_text(&KF_TEXT, 16, by + (BOARD_BAR_SCREEN ? 58 : 64), nm, THEME_FILES_TEXT);
    button(STOP_X + 4, SH - STOP_H - 4, STOP_W, STOP_H, "STOP", fil_hot == CT_STOP, 0);
}

static void toast_render(int64_t now)
{
    if (now >= fil_msg_until || fil_page == P_KBD) {
        return;
    }
    const kfont_t *f = &KF_NAME;
    char a[112], b[112];
    wrap2(f, fil_msg, SW - 56, a, b, sizeof a);
    int lines = b[0] ? 2 : 1, h = lines == 2 ? 50 : 32;
    int w = ui_text_w(f, a), w2 = b[0] ? ui_text_w(f, b) : 0;
    w = (w > w2 ? w : w2) + 36;
    int x = (SW - w) / 2, y = SH - h - 6;
#if BOARD_ROUND
    y = LIST_B - h - 6;  // (over the bottom cap: inside the circle)
#endif
    ui_rrect(x - 2, y - 2, w + 4, h + 4, 14, THEME_FILES_BG);
    ui_rrect(x, y, w, h, 12, fil_msg_err ? THEME_FILES_FAULT_BG : THEME_FILES_OK_BG);
    ui_text_c(f, SW / 2, y + 22, a, THEME_FILES_TEXT);
    if (b[0]) {
        ui_text_c(f, SW / 2, y + 41, b, THEME_FILES_TEXT);
    }
}

void fil_render(int64_t now)
{
    switch (fil_page) {
    case P_HOME:
        home_render();
        break;
    case P_LIST:
        list_render();
        break;
    case P_ACT:
        act_render();
        break;
    case P_ASK:
        ask_render(now);
        break;
    case P_KBD:
        kbd_render(now);
        break;
    case P_INFO:
        info_render();
        break;
    case P_TEXT:
        text_render();
        break;
    case P_PIC:
        pic_render(now);
        break;
    case P_JOB:
        job_render();
        break;
    default:
        gfx_fill(THEME_FILES_BG);
        break;
    }
    toast_render(now);
    fil_drawn = fil_page;
}
