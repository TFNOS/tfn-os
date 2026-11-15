// FILES: the action panel, the question page and the actions (main/app_files.c has the rest).
#include "app_files_priv.h"

// the keyboard
enum {
    KB_RENAME,
    KB_NEWDIR
};
static int s_kmode;
static char s_kbuf[64];

// ---------------------------------------------------------------- pages: the list, the panel

void fil_page_list(void)
{
    if (fil_root < 0) {
        fil_page = P_HOME;
        return;
    }
    fil_page = P_LIST;
    fil_hot = -1;
    if (fil_list_dirty) {
        fil_list_load();  // (else the list that is there: no new read of the card)
    }
}

static const char *copy_label(void)
{
    return fil_root == RT_CARD ? "COPY TO BOARD" : "COPY TO CARD";
}

void fil_panel_open_entry(int i)
{
    const fent_t *e = &fil_ent[i];
    fil_sel = i;
    fil_pkind = PK_ENTRY;
    fil_nact = 0;
    bool other = fil_root == RT_CARD ? fil_data_ok : fil_card_ok;
    snprintf(fil_ptitle, sizeof fil_ptitle, "%s", e->name);
    char sz[24];
    fil_fmt_size(sz, sizeof sz, e->size);
    if (e->lock) {
        fil_act[fil_nact++] = e->kind == K_PKG ? A_FILES : A_OPEN;
        fil_act[fil_nact++] = A_INFO;
        snprintf(fil_pnote, sizeof fil_pnote, "%s",
                 fil_rel_locked(fil_rel) || !fil_ci_cmp(e->name, "apps")
                         ? "LOCKED: THE MENU AND THE STORE CHANGE THE APPS"
                         : "LOCKED: A SYSTEM FILE");
    } else {
#if WIDE_UI
        // 3 columns: DELETE at the left of the second row (the YES of the question is at the right)
        if (e->kind == K_PKG) {
            fil_act[fil_nact++] = A_INSTALL;
            fil_act[fil_nact++] = A_FILES;
            fil_act[fil_nact++] = A_INFO;
            fil_act[fil_nact++] = A_DELETE;
            fil_act[fil_nact++] = A_RENAME;
        } else {
            fil_act[fil_nact++] = A_OPEN;
            fil_act[fil_nact++] = A_INFO;
            fil_act[fil_nact++] = A_RENAME;
            fil_act[fil_nact++] = A_DELETE;
        }
#else
        // 2 columns: DELETE at the left
        if (e->kind == K_PKG) {
            fil_act[fil_nact++] = A_INSTALL;
            fil_act[fil_nact++] = A_FILES;
            fil_act[fil_nact++] = A_DELETE;
            fil_act[fil_nact++] = A_INFO;
            fil_act[fil_nact++] = A_RENAME;
        } else {
            fil_act[fil_nact++] = A_OPEN;
            fil_act[fil_nact++] = A_INFO;
            fil_act[fil_nact++] = A_DELETE;
            fil_act[fil_nact++] = A_RENAME;
        }
#endif
        if (other) {
            fil_act[fil_nact++] = A_COPY;
        }
        if (e->kind == K_DIR) {
            snprintf(fil_pnote, sizeof fil_pnote, "A FOLDER");
        } else if (e->kind == K_PKG) {
            snprintf(fil_pnote, sizeof fil_pnote, "AN APP PACKAGE");
        } else {
            snprintf(fil_pnote, sizeof fil_pnote, "%s", sz);
        }
    }
    fil_page = P_ACT;
    fil_hot = -1;
    fil_focus = -1;
    sfx_ui(SFX_TICK);
}

void fil_panel_open_more(void)
{
    fil_pkind = PK_MORE;
    fil_nact = 0;
    if (!fil_rel_lock) {
        fil_act[fil_nact++] = A_NEWDIR;
    }
    fil_act[fil_nact++] = A_LOOK;
    if (fil_root == RT_CARD) {
        fil_act[fil_nact++] = A_TOOLS;
    }
    snprintf(fil_ptitle, sizeof fil_ptitle, "%s%.100s", fil_root_name(fil_root), fil_rel[0] ? fil_rel : "");
    snprintf(fil_pnote, sizeof fil_pnote, "%d %s%s", fil_n, fil_n == 1 ? "ITEM" : "ITEMS",
             fil_rel_lock ? ". LOCKED" : "");
    fil_page = P_ACT;
    fil_hot = fil_focus = -1;
    sfx_ui(SFX_TICK);
}

void fil_panel_open_tools(void)
{
    fil_pkind = PK_TOOLS;
    fil_nact = 0;
    fil_act[fil_nact++] = A_FORMAT;
    fil_act[fil_nact++] = A_LOOK;
    snprintf(fil_ptitle, sizeof fil_ptitle, "CARD TOOLS");
    if (fil_card_ok) {
        char t[24], f[24];
        fil_fmt_size(t, sizeof t, fil_card_t);
        fil_fmt_size(f, sizeof f, fil_card_f);
        snprintf(fil_pnote, sizeof fil_pnote, "THE CARD: %s, %s FREE", t, f);
    } else {
        snprintf(fil_pnote, sizeof fil_pnote, "NO CARD. A NEW CARD CAN NEED A FORMAT");
    }
    fil_page = P_ACT;
    fil_hot = fil_focus = -1;
    sfx_ui(SFX_TICK);
}

const char *fil_act_label(int a)
{
    switch (a) {
    case A_OPEN:
        return "OPEN";
    case A_INSTALL:
        return "INSTALL";
    case A_FILES:
        return "SHOW FILES";
    case A_INFO:
        return "INFO";
    case A_RENAME:
        return "RENAME";
    case A_COPY:
        return copy_label();
    case A_DELETE:
        return "DELETE";
    case A_NEWDIR:
        return "NEW FOLDER";
    case A_LOOK:
        return "LOOK AGAIN";
    case A_TOOLS:
        return "CARD TOOLS";
    case A_FORMAT:
        return "FORMAT";
    default:
        return "";
    }
}

// the box of button i of the panel
void fil_act_box(int i, int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    {
        // (the round panels: two columns in the middle box, from the top of the box)
        int rows = (fil_nact + 1) / 2, g = FR(8), bh = (LIST_B - PAN_H - 8 - (rows - 1) * g) / (rows ? rows : 1);
        bh = bh > FR(80) ? FR(80) : bh;
        int bw = (MB_W - g) / 2, top = PAN_H + 6 + (LIST_B - PAN_H - 8 - rows * bh - (rows - 1) * g) / 2;
        *x = MB_X + (i % 2) * (bw + g);
        *y = top + (i / 2) * (bh + g);
        *w = bw;
        *h = bh;
        return;
    }
#endif
    int rows = (fil_nact + PAN_COLS - 1) / PAN_COLS;
    int area = SH - PAN_H - 4, gap = 6;
    int bh = (area - (rows - 1) * gap) / rows;
#if BOARD_UI_TABLET
    if (bh > 96) {
        bh = 96;  // (the Tab5: bigger buttons)
    }
#else
    if (bh > 76) {
        bh = 76;
    }
#endif
    int bw = (SW - 8 - (PAN_COLS - 1) * gap) / PAN_COLS;
    *x = 4 + (i % PAN_COLS) * (bw + gap);
    *y = PAN_H + 2 + (i / PAN_COLS) * (bh + gap);
    *w = bw;
    *h = bh;
}

int fil_act_at(int x, int y)
{
#if BOARD_ROUND
    if (y >= BAR_Y - 6 && abs(x - SW / 2) < FR(90)) {
        return CT_CLOSE;  // (CLOSE: the bottom cap)
    }
#endif
    if (y < PAN_H && x >= SW - CLOSE_W) {
        return CT_CLOSE;
    }
    for (int i = 0; i < fil_nact; i++) {
        int bx, by, bw, bh;
        fil_act_box(i, &bx, &by, &bw, &bh);
        if (ui_hit(x, y, bx - 2, by - 2, bw + 4, bh + 4, 0)) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- the question page

static void ask_open(int kind, const char *a0, const char *a1, int hold_ms, bool closable, int64_t now)
{
    fil_qkind = kind;
    fil_qa[0] = a0;
    fil_qa[1] = a1;
    fil_qhold_ms = hold_ms;
    fil_qclose = closable;
    fil_qhot = -1;
    fil_qfocus = -1;
    fil_qarmed = false;
    fil_qguard = false;
    fil_qopen_t = now;
    fil_qhold_t0 = -1;
    fil_page = P_ASK;
    sfx_ui(SFX_TICK);
}

int fil_ask_at(int x, int y)
{
#if BOARD_ROUND
    if (fil_qclose && y >= BAR_Y - 6 && abs(x - SW / 2) < FR(90)) {
        return 2;
    }
    if (y < ASK_Y - 6 || y >= ASK_Y + ASK_H + 6) {
        return -1;
    }
    return x < SW / 2 ? (fil_qa[0] ? 0 : -1) : (fil_qa[1] ? 1 : -1);
#endif
    if (fil_qclose && y < ASK_Y - 4 && x >= SW - CLOSE_W && y < 48) {
        return 2;
    }
    if (y < ASK_Y) {
        return -1;
    }
    if (x < ASK_W && fil_qa[0]) {
        return 0;
    }
    if (x >= SW - ASK_W && fil_qa[1]) {
        return 1;
    }
    return -1;
}

static void do_delete(int64_t now);
static void start_copy(bool replace, int64_t now);
static void start_install(int place, int64_t now);

// the answer: 0 = the left answer, 1 = the right answer, 2 = close
void fil_ask_answer(int a, int64_t now)
{
    fil_qhot = fil_qfocus = -1;
    fil_qhold_t0 = -1;
    int k = fil_qkind;
    if (k == Q_FORMAT1 && a == 1) {
        snprintf(fil_q1, sizeof fil_q1, "LAST CHECK: FORMAT NOW?");
        snprintf(fil_q2, sizeof fil_q2, "ALL FILES ON THE CARD GO. HOLD YES FOR 1 S");
        ask_open(Q_FORMAT2, "NO", "YES", HOLD_MS, false, now);
        return;
    }
    if (k == Q_INSTALL) {
        if (a == 0 || a == 1) {
            start_install(a == 0 ? TFN_BOARD : TFN_CARD, now);
            return;
        }
        sfx_ui(SFX_BACK);
        fil_page_list();
        return;
    }
    if (a != 1) {
        sfx_ui(SFX_BACK);
        if (k == Q_FORMAT1 || k == Q_FORMAT2) {
            if (fil_root < 0) {
                fil_page = P_HOME;
            } else {
                fil_page_list();
            }
        } else {
            fil_page_list();
        }
        return;
    }
    switch (k) {
    case Q_DELETE:
        do_delete(now);
        break;
    case Q_REPLACE:
        start_copy(true, now);
        break;
    case Q_FORMAT2:
        fil_job.kind = J_FORMAT;
        snprintf(fil_job.title, sizeof fil_job.title, "FORMAT OF THE CARD");
        fil_job.name[0] = 0;
        fil_job.n = fil_job.i = 0;
        fil_page = P_JOB;
        break;
    default:
        fil_page_list();
        break;
    }
}

// ---------------------------------------------------------------- the actions

void fil_sel_path(char *out, size_t n)
{
    if (!fil_join(out, n, fil_root_real(fil_root), fil_rel, fil_ent[fil_sel].name)) {
        out[0] = 0;
    }
}

static void ask_install(int64_t now)
{
    char man[FL_PATH], name[32] = "", ver[16] = "";
    fil_sel_path(man, sizeof man);
    size_t k = strlen(man);
    snprintf(man + k, sizeof man - k, "/manifest.txt");
    FILE *f = fopen(man, "rb");
    if (f) {
        char t[512];
        size_t n = fread(t, 1, sizeof t - 1, f);
        t[n] = 0;
        fclose(f);
        for (char *p = t; p && *p;) {
            if (!strncmp(p, "name=", 5)) {
                sscanf(p + 5, "%31[^\r\n]", name);
            }
            if (!strncmp(p, "ver=", 4)) {
                sscanf(p + 4, "%15[^\r\n]", ver);
            }
            p = strchr(p, '\n');
            if (p) {
                p++;
            }
        }
    }
    fil_upper(name);
    snprintf(fil_q1, sizeof fil_q1, "INSTALL %s?", name[0] ? name : fil_ent[fil_sel].name);
    snprintf(fil_q2, sizeof fil_q2, "%s%sWHERE DO YOU KEEP THE APP?", ver, ver[0] ? ". " : "");
    ask_open(Q_INSTALL, "BOARD", fil_card_ok ? "CARD" : NULL, 0, true, now);
}

void fil_open_entry(int i, int64_t now)
{
    if (i < 0 || i >= fil_n) {
        return;
    }
    fil_sel = i;
    const fent_t *e = &fil_ent[i];
    switch (e->kind) {
    case K_DIR:
        fil_go_into(e->name);
        break;
    case K_PKG:
        if (e->lock) {
            fil_go_into(e->name);
        } else {
            ask_install(now);
        }
        break;
    case K_TEXT:
        fil_open_text(now);
        break;
    case K_PIC:
        fil_open_pic(now);
        break;
    default:
        fil_open_info(now);
        break;
    }
}

static void ask_delete(int64_t now)
{
    const fent_t *e = &fil_ent[fil_sel];
    char sz[24];
    if (e->kind == K_DIR || e->kind == K_PKG) {
        fil_sel_path(fil_p1, sizeof fil_p1);
        char base[FL_PATH];
        snprintf(base, sizeof base, "%s", fil_p1);
        if (!fil_job_alloc()) {
            fil_say("NO MEMORY", true, now);
            return;
        }
        if (!fil_job_scan(base)) {
            fil_say("TOO MANY FILES IN IT (MORE THAN 512)", true, now);
            fil_page_list();
            return;
        }
        if (fil_job.n <= 1) {
            snprintf(fil_q1, sizeof fil_q1, "DELETE THE FOLDER %s?", e->name);
            snprintf(fil_q2, sizeof fil_q2, "THE FOLDER IS EMPTY");
            ask_open(Q_DELETE, "NO", "YES", 0, false, now);
        } else {
            fil_fmt_size(sz, sizeof sz, fil_job.total);
            snprintf(fil_q1, sizeof fil_q1, "DELETE THE FOLDER AND ALL IN IT?");
            snprintf(fil_q2, sizeof fil_q2, "%.40s: %d FILES, %s. HOLD YES FOR 1 S", e->name, fil_job.files, sz);
            ask_open(Q_DELETE, "NO", "YES", HOLD_MS, false, now);
        }
        return;
    }
    fil_fmt_size(sz, sizeof sz, e->size);
    snprintf(fil_q1, sizeof fil_q1, "DELETE %s?", e->name);
    snprintf(fil_q2, sizeof fil_q2, "%s. YOU CANNOT GET IT BACK", sz);
    ask_open(Q_DELETE, "NO", "YES", 0, false, now);
}

static void do_delete(int64_t now)
{
    fil_list_dirty = true;
    fil_sel_path(fil_p1, sizeof fil_p1);
    if (!fil_p1[0]) {
        return;
    }
    if (!fil_job_alloc()) {
        fil_say("NO MEMORY", true, now);
        fil_page_list();
        return;
    }
    snprintf(fil_job.src, sizeof fil_job.src, "%s", fil_p1);
    if (!fil_job_scan(fil_job.src)) {
        fil_say("TOO MANY FILES IN IT (MORE THAN 512)", true, now);
        fil_page_list();
        return;
    }
    snprintf(fil_job.title, sizeof fil_job.title, "DELETING");
    fil_job.kind = J_DELETE;
    fil_job.i = fil_job.files_done = 0;
    fil_job.done = 0;
    fil_job.install = false;
    fil_page = P_JOB;
    fil_hot = -1;
}

// the folders of rel in the root r, made when they are not there ("/a/b" -> root/a, root/a/b)
static bool mkdirs(int r, const char *rel)
{
    char p[FL_PATH];
    if (!fil_join(p, sizeof p, fil_root_real(r), rel, NULL)) {
        return false;
    }
    size_t k0 = strlen(fil_root_real(r));
    for (size_t k = k0 + 1; p[k - 1]; k++) {
        if (p[k] != '/' && p[k] != 0) {
            continue;
        }
        char c = p[k];
        p[k] = 0;
        if (!fil_is_dir(p) && MKDIR(p) != 0) {
            return false;
        }
        p[k] = c;
        if (!c) {
            break;
        }
    }
    return true;
}

static void start_copy(bool replace, int64_t now)
{
    const fent_t *e = &fil_ent[fil_sel];
    int dr = fil_root == RT_CARD ? RT_BOARD : RT_CARD;
    char src[FL_PATH], dst[FL_PATH];
    fil_sel_path(src, sizeof src);
    if (!src[0] || !fil_join(dst, sizeof dst, fil_root_real(dr), fil_rel, e->name)) {
        fil_say("THE PATH IS TOO LONG", true, now);
        fil_page_list();
        return;
    }
    if (!replace && fil_exists(dst)) {
        snprintf(fil_q1, sizeof fil_q1, "REPLACE %s ON THE %s?", e->name, fil_root_name(dr));
        snprintf(fil_q2, sizeof fil_q2, "A FILE WITH THIS NAME IS THERE. THE COPY GOES OVER IT");
        ask_open(Q_REPLACE, "NO", "YES", 0, false, now);
        return;
    }
    if (!fil_job_alloc()) {
        fil_say("NO MEMORY", true, now);
        fil_page_list();
        return;
    }
    if (!fil_job_scan(src)) {
        fil_say("TOO MANY FILES (MORE THAN 512)", true, now);
        fil_page_list();
        return;
    }
    fil_space_read();
    uint64_t need = fil_job.total + (uint64_t)fil_job.n * 4096, have = dr == RT_CARD ? fil_card_f : fil_data_f;
    if (need > have) {
        char a[24], b[24], m[112];
        fil_fmt_size(a, sizeof a, need);
        fil_fmt_size(b, sizeof b, have);
        snprintf(m, sizeof m, "NOT ENOUGH SPACE ON THE %s: %s NEEDED, %s FREE", fil_root_name(dr), a, b);
        fil_say(m, true, now);
        fil_page_list();
        return;
    }
    if (fil_rel[0] && !mkdirs(dr, fil_rel)) {
        fil_say("THE FOLDER DID NOT MAKE", true, now);
        fil_page_list();
        return;
    }
    char t[48];
    snprintf(t, sizeof t, "COPYING TO THE %s", fil_root_name(dr));
    fil_job_copy(src, dst, t, dr, false, 0);
}

static void start_install(int place, int64_t now)
{
    if (place == TFN_CARD && !fil_card_ok) {
        fil_say("NO CARD", true, now);
        fil_page_list();
        return;
    }
    int dr = place == TFN_CARD ? RT_CARD : RT_BOARD;
    char src[FL_PATH], dst[FL_PATH];
    fil_sel_path(src, sizeof src);
    if (!fil_join(dst, sizeof dst, fil_root_real(dr), "/.fil_inst", NULL)) {
        return;
    }
    fil_rm_tree_now(dst);
    if (!fil_job_alloc() || !fil_job_scan(src)) {
        fil_say("THE PACKAGE DID NOT READ", true, now);
        fil_page_list();
        return;
    }
    fil_space_read();
    uint64_t need = fil_job.total * 2 + (uint64_t)fil_job.n * 8192, have = dr == RT_CARD ? fil_card_f : fil_data_f;
    if (need > have) {
        fil_say(dr == RT_CARD ? "NOT ENOUGH SPACE ON THE CARD" : "NOT ENOUGH SPACE ON THE BOARD", true, now);
        fil_page_list();
        return;
    }
    fil_job_copy(src, dst, "GETTING THE APP READY", dr, true, place);
}

static bool name_ok(const char *n, int64_t now)
{
    if (!n[0] || !strcmp(n, ".") || !strcmp(n, "..")) {
        fil_say("TYPE A NAME", true, now);
        return false;
    }
    if (strpbrk(n, "/\\:*?\"<>|")) {
        fil_say("A NAME CANNOT HAVE / \\ : * ? \" < > |", true, now);
        return false;
    }
    char r[FL_PATH];
    if (!fil_join(r, sizeof r, fil_rel, "", n) || fil_rel_locked(r)) {
        fil_say("THIS NAME IS FOR THE SYSTEM", true, now);
        return false;
    }
    return true;
}

void fil_kbd_done(int r, int64_t now)
{
    fil_list_dirty = true;
    if (r == KBD_NONE) {
        return;
    }
    if (r == KBD_CANCEL) {
        fil_page_list();
        return;
    }
    // (spaces at the ends go)
    char *n = s_kbuf;
    while (*n == ' ') {
        n++;
    }
    size_t k = strlen(n);
    while (k && n[k - 1] == ' ') {
        n[--k] = 0;
    }
    if (!name_ok(n, now)) {
        fil_page_list();
        return;
    }
    char to[FL_PATH];
    if (!fil_join(to, sizeof to, fil_root_real(fil_root), fil_rel, n)) {
        fil_say("THE NAME IS TOO LONG", true, now);
        fil_page_list();
        return;
    }
    if (s_kmode == KB_NEWDIR) {
        if (fil_exists(to)) {
            fil_say("A FILE WITH THIS NAME IS THERE", true, now);
        } else if (MKDIR(to) != 0) {
            fil_say("THE FOLDER DID NOT MAKE", true, now);
        } else {
            fil_say("THE FOLDER IS MADE", false, now);
        }
    } else {
        char from[FL_PATH];
        fil_sel_path(from, sizeof from);
        bool same = !fil_ci_cmp(fil_ent[fil_sel].name, n);
        if (!strcmp(fil_ent[fil_sel].name, n)) {
            fil_page_list();
            return;
        }
        if (!same && fil_exists(to)) {
            fil_say("A FILE WITH THIS NAME IS THERE", true, now);
        } else {
            // (a change of the case only: through a temporary name; FAT sees one name)
            char tmp[FL_PATH];
            bool ok;
            if (same && fil_join(tmp, sizeof tmp, fil_root_real(fil_root), fil_rel, ".fil_ren")) {
                ok = rename(from, tmp) == 0 && rename(tmp, to) == 0;
            } else {
                ok = rename(from, to) == 0;
            }
            fil_say(ok ? "THE NAME IS CHANGED" : "THE NAME DID NOT CHANGE", !ok, now);
        }
    }
    fil_page_list();
}

static void open_kbd(int mode)
{
    s_kmode = mode;
    if (mode == KB_RENAME) {
        snprintf(s_kbuf, sizeof s_kbuf, "%s", fil_ent[fil_sel].name);
        kbd_open("A NEW NAME", s_kbuf, sizeof s_kbuf, false);
        kbd_set_ok_question("RENAME TO THIS NAME?");
    } else {
        s_kbuf[0] = 0;
        kbd_open("THE NAME OF THE NEW FOLDER", s_kbuf, sizeof s_kbuf, false);
        kbd_set_ok_question("MAKE THIS FOLDER?");
    }
    fil_page = P_KBD;
    sfx_ui(SFX_CLICK);
}

void fil_do_act(int a, int64_t now)
{
    switch (a) {
    case A_OPEN:
        fil_open_entry(fil_sel, now);
        break;
    case A_FILES:
        fil_go_into(fil_ent[fil_sel].name);
        fil_page = fil_root >= 0 ? P_LIST : P_HOME;
        break;
    case A_INSTALL:
        ask_install(now);
        break;
    case A_INFO:
        fil_open_info(now);
        break;
    case A_RENAME:
        open_kbd(KB_RENAME);
        break;
    case A_COPY:
        start_copy(false, now);
        break;
    case A_DELETE:
        ask_delete(now);
        break;
    case A_NEWDIR:
        open_kbd(KB_NEWDIR);
        break;
    case A_LOOK:
        fil_list_dirty = true;
        fil_card_look();
        fil_space_read();
        if (fil_pkind == PK_TOOLS && fil_root < 0) {
            fil_say(fil_card_ok ? "THE CARD IS THERE" : "NO CARD", !fil_card_ok, now);
            fil_page = P_HOME;
        } else {
            fil_page_list();
            if (fil_page == P_LIST) {
                fil_say("THE LIST IS NEW", false, now);
            }
        }
        break;
    case A_TOOLS:
        fil_panel_open_tools();
        break;
    case A_FORMAT:
        snprintf(fil_q1, sizeof fil_q1, "FORMAT THE CARD? ALL FILES GO.");
        if (fil_card_ok) {
            char t[24];
            fil_fmt_size(t, sizeof t, fil_card_t);
            snprintf(fil_q2, sizeof fil_q2, "THE CARD OF %s GETS A NEW, EMPTY FILE SYSTEM", t);
        } else {
            snprintf(fil_q2, sizeof fil_q2, "THE CARD GETS A NEW, EMPTY FILE SYSTEM");
        }
        ask_open(Q_FORMAT1, "NO", "YES", 0, false, now);
        break;
    default:
        break;
    }
}
