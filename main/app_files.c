// FILES (UTIL): a file browser. It shows
// the files of the SD card (CARD, /sdcard) and of the flash of the board (BOARD, /data).
// - THE HOME PAGE: two tiles, CARD and BOARD, each with its size and its free space (NO CARD: a tap looks again).
//   CARD TOOLS: the size of the card, LOOK AGAIN, FORMAT (two questions; board_sd_format(), see below).
// - THE LIST: folders first, then files (the name, the size; the 320 x 240 frame also shows the date). The path line
//   at the top: a tap on it (or on UP) goes up one folder; UP at the root goes to the home page. A drag scrolls (one row
//   for a short drag, as in SETUP). A tap opens: a folder, a text (.txt .md .json .csv .log .ini .cfg), a picture
//   (.jpg .jpeg, the ROM JPEG decoder), an app package (a folder with manifest.txt and app.elf: INSTALL). Other files:
//   INFO. MORE (or a hold on the empty part of the list): NEW FOLDER, LOOK AGAIN, CARD TOOLS.
// - A HOLD (IN_LONG) ON AN ENTRY: its actions: OPEN, INFO, RENAME (the keyboard), DELETE (a question; a folder with
//   files in it: a hold of YES), COPY TO BOARD / COPY TO CARD (the same folder path on the other side; a check of the
//   free space; REPLACE asks). DELETE is at the left: the YES of the question is at the right, and a touch that starts
//   in the first 450 ms of a question does nothing (a double tap cannot answer it).
// - LOCKED (a lock, OPEN and INFO only): the folder "apps" at the root of BOARD and CARD (the installed apps of TFN OS:
//   the menu and the store manage them) and each name that starts with '.'. The things in a locked folder are locked.
// - COPY, DELETE and the copy for INSTALL are a job: a part in each frame (16 KB, or 8 small steps), with a progress bar
//   and STOP. INSTALL copies the package to <place>/.fil_inst first (tfn_install moves its folder), then tfn_install().
// - FORMAT calls bool board_sd_format(void) (files.h). This file has a weak copy that returns false; a board file can
//   give the real one (esp_vfs_fat_sdcard_format; it must also work when the card does not mount).
// - Saves: "fil_root" = the last place (0 the home page, 1 CARD, 2 BOARD); the app opens there.
// - The host tests: no /sdcard and /data on the PC. The roots are <FILES_HOST>/card and <FILES_HOST>/data (an
//   environment value); with no value, a sandbox folder for the board that the app makes again at each
//   enter. The PC has no JPEG decoder: the picture page shows a test pattern.

#include "app_files_priv.h"

// The format of the card: a board file gives the real function (this weak one says "not in this firmware").
__attribute__((weak)) bool board_sd_format(void)
{
    return false;
}

// ---------------------------------------------------------------- state

int fil_page;
int fil_root = -1;      // RT_CARD, RT_BOARD (-1: the home page)
char fil_rel[FL_PATH];  // the folder in the root: "" or "/a/b"
fent_t fil_ent[FL_MAX];
int fil_n, fil_skip;  // the entries; the entries that do not show (too many, a long name)
bool fil_rel_lock;    // the folder now is locked

// the card and the space
bool fil_card_ok, fil_data_ok;
uint64_t fil_card_t, fil_card_f, fil_data_t, fil_data_f;

// the list: the scroll (px), the touch
float fil_scroll;          // the position of the list to draw (ks_pos(&s_ks), set in each update)
kscroll_t fil_ks, fil_kt;  // the momentum scroll of the list and of the text view (libs/kscroll)
bool fil_touch, fil_drag;
int fil_x0, fil_y0;
int fil_hot = -1;    // the control under the finger (-1: none)
int fil_focus = -1;  // the wheel: the row (or the button) in focus
static int64_t s_prev;
int fil_drawn = -1;  // the page of the last render (a slow step waits until its page shows)

// the action panel
uint8_t fil_act[8];
int fil_nact, fil_pkind, fil_sel = -1;
char fil_ptitle[FL_NAME + 16], fil_pnote[112];

// the question page
int fil_qkind, fil_qhot = -1, fil_qfocus = -1, fil_qhold_ms;
char fil_q1[FL_NAME + 32], fil_q2[128];
const char *fil_qa[2];
bool fil_qclose, fil_qguard, fil_qarmed;
int64_t fil_qopen_t, fil_qhold_t0 = -1;

// the info page
char fil_info_l[6][16], fil_info_v[6][FL_PATH / 2];
int fil_info_n;
char fil_info_name[FL_NAME];

// a message at the bottom
char fil_msg[112];
bool fil_msg_err;
int64_t fil_msg_until;

// the text page
char *fil_txt;
uint32_t *fil_loff;
uint16_t *fil_llen;
int fil_nl;
uint32_t fil_tlen, fil_tsize;
float fil_tscroll;
char fil_tname[FL_NAME];

// the picture page
uint16_t *fil_pic;
int fil_pw, fil_ph, fil_pstate;  // 0 loading, 1 ready, 2 error
char fil_pname[FL_NAME], fil_perr[64], fil_ppath[FL_PATH];
int64_t fil_pbar_until;

// the job (copy, delete, format)
fil_job_t fil_job;
char fil_p1[FL_PATH], fil_p2[FL_PATH];  // work paths

#if T5P
// the Tab5 panes: the folder pane (the folders beside the folder now; at a root: CARD and BOARD) and the preview
side_t fil_side[SIDE_MAX];
int fil_nside, fil_side_cur;  // the lit row (-1: none)
bool fil_side_roots;          // the folder pane shows CARD and BOARD (the list is a root)
kscroll_t fil_kside;
int fil_pv = -1;                             // the thing in the preview (an index of s_ent; -1: none)
int fil_pv_root, fil_pv_kind, fil_pv_state;  // the state: 0 a picture to open, 1 ready, 2 a fault (s_pv_err)
char fil_pv_name[FL_NAME], fil_pv_rel[FL_PATH], fil_pv_err[64];
char fil_pv_txt[PV_TXT + 1];
uint32_t fil_pv_loff[PV_LINES];
uint16_t fil_pv_llen[PV_LINES];
int fil_pv_nl;
uint16_t *fil_pv_pic;
int fil_pv_w, fil_pv_h;
int fil_pv_id;           // the decode that the preview waits for (jj_start; 0: none)
bool fil_pv_drawn;       // the preview showed OPENING once (then the decode)
int fil_side_root = -1;  // the folder pane has the folders of this parent (-1: not known)
char fil_side_par[FL_PATH];
#endif

// ---------------------------------------------------------------- the roots (the board, or the host test)

#ifdef ESP_PLATFORM
const char *fil_root_real(int r)
{
    return r == RT_CARD ? BOARD_SD_ROOT : STORAGE_ROOT;
}
static bool card_mount(void)
{
    return board_sd_mount();
}
static bool card_check(void)
{
    return board_sd_check();
}
static bool data_ok(void)
{
    return storage_ok();
}
static void space_get(void)
{
    uint32_t t = 0, f = 0;
    fil_card_t = fil_card_f = 0;
    if (fil_card_ok && board_sd_info(&t, &f)) {
        fil_card_t = (uint64_t)t * 1024;
        fil_card_f = (uint64_t)f * 1024;
    }
    uint64_t dt = 0, df = 0;
    if (esp_vfs_fat_info(STORAGE_ROOT, &dt, &df) != ESP_OK) {
        dt = df = 0;
    }
    fil_data_t = dt;
    fil_data_f = df;
}
#else
static char s_host_card[FL_PATH / 2], s_host_data[FL_PATH / 2], s_host_base[FL_PATH / 2];
static bool s_host_box;  // the sandbox of the fuzz (the app makes it again at each enter)
const char *fil_root_real(int r)
{
    return r == RT_CARD ? s_host_card : s_host_data;
}
static bool host_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}
static bool card_mount(void)
{
    return host_dir(s_host_card);
}
static bool card_check(void)
{
    return host_dir(s_host_card);
}
static bool data_ok(void)
{
    return host_dir(s_host_data);
}
static void space_get(void)
{
    // (numbers like a real board: a card of 29.7 GB, the storage of 3.9 MB)
    fil_card_t = fil_card_ok ? 31166976ULL * 1024 : 0;
    fil_card_f = fil_card_ok ? 12687360ULL * 1024 : 0;
    fil_data_t = 3968ULL * 1024;
    fil_data_f = 2660ULL * 1024;
}
#endif

const char *fil_root_name(int r)
{
    return r == RT_CARD ? "CARD" : "BOARD";
}

// ---------------------------------------------------------------- small helpers

bool fil_join(char *out, size_t n, const char *a, const char *b, const char *c)
{
    int k = snprintf(out, n, "%s%s%s%s", a, b, c && c[0] ? "/" : "", c ? c : "");
    return k > 0 && (size_t)k < n;
}

bool fil_is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

bool fil_exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

int fil_ci_cmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int x = tolower((unsigned char)*a), y = tolower((unsigned char)*b);
        if (x != y || !x) {
            return x - y;
        }
    }
}

static bool ci_ends(const char *s, const char *ext)
{
    size_t n = strlen(s), k = strlen(ext);
    return n > k && !fil_ci_cmp(s + n - k, ext);
}

void fil_upper(char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s < 0x80) {
            *s = (char)toupper((unsigned char)*s);
        }
    }
}

void fil_fmt_size(char *out, int n, uint64_t b)
{
    if (b < 1024) {
        snprintf(out, n, "%u B", (unsigned)b);
    } else if (b < 10 * 1024) {
        snprintf(out, n, "%.1f KB", b / 1024.0);
    } else if (b < 1024 * 1024) {
        snprintf(out, n, "%u KB", (unsigned)(b / 1024));
    } else if (b < 1024ULL * 1024 * 1024) {
        snprintf(out, n, "%.1f MB", b / 1048576.0);
    } else {
        snprintf(out, n, "%.1f GB", b / 1073741824.0);
    }
}

void fil_fmt_date(char *out, int n, uint32_t t)
{
    time_t tt = (time_t)t;
    struct tm *tm = t > 946684800u ? localtime(&tt) : NULL;  // (2000-01-01: a FAT file with no clock)
    if (!tm) {
        snprintf(out, n, "NO DATE");
    } else if (snprintf(out, n, "%04d-%02d-%02d %02d:%02d", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                        tm->tm_hour, tm->tm_min) < 0) {
        out[0] = 0;
    }
}

void fil_say(const char *m, bool err, int64_t now)
{
    snprintf(fil_msg, sizeof fil_msg, "%s", m);
    fil_msg_err = err;
    fil_msg_until = now + (err ? 3500 : 2500);
    sfx_play(err ? SFX_BUMP : SFX_CLICK);
}

// ---------------------------------------------------------------- the lock rule

// true: rel ("/a/b") is a system place: a part that starts with '.', or "apps" as the first part (TFN_DATA_DIR,
// TFN_CARD_DIR: the installed apps)
bool fil_rel_locked(const char *rel)
{
    const char *p = rel;
    for (int k = 0; *p; k++) {
        while (*p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }
        const char *e = strchr(p, '/');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (p[0] == '.') {
            return true;
        }
        if (k == 0 && n == 4 && tolower((unsigned char)p[0]) == 'a' && tolower((unsigned char)p[1]) == 'p' &&
            tolower((unsigned char)p[2]) == 'p' && tolower((unsigned char)p[3]) == 's') {
            return true;
        }
        p += n;
    }
    return false;
}

static bool name_locked(const char *name)
{
    char r[FL_PATH];
    if (!fil_join(r, sizeof r, fil_rel, "", name)) {
        return true;
    }
    return fil_rel_locked(r);
}

// ---------------------------------------------------------------- the list

static int kind_of(const char *dir, const char *name, bool d)
{
    if (d) {
        if (fil_join(fil_p2, sizeof fil_p2, dir, "/", name)) {
            size_t k = strlen(fil_p2);
            snprintf(fil_p2 + k, sizeof fil_p2 - k, "/manifest.txt");
            bool m = fil_exists(fil_p2);
            snprintf(fil_p2 + k, sizeof fil_p2 - k, "/app.elf");
            if (m && fil_exists(fil_p2)) {
                return K_PKG;
            }
        }
        return K_DIR;
    }
    static const char *const TXT[] = {".txt", ".md", ".json", ".csv", ".log", ".ini", ".cfg"};
    for (unsigned i = 0; i < sizeof TXT / sizeof TXT[0]; i++) {
        if (ci_ends(name, TXT[i])) {
            return K_TEXT;
        }
    }
    if (ci_ends(name, ".jpg") || ci_ends(name, ".jpeg")) {
        return K_PIC;
    }
    return K_FILE;
}

static int ent_cmp(const void *a, const void *b)
{
    const fent_t *x = (const fent_t *)a, *y = (const fent_t *)b;
    bool dx = x->kind == K_DIR || x->kind == K_PKG, dy = y->kind == K_DIR || y->kind == K_PKG;
    if (dx != dy) {
        return dx ? -1 : 1;
    }
    return fil_ci_cmp(x->name, y->name);
}

int fil_max_scroll(void)
{
    int rows = fil_n + (fil_skip ? 1 : 0);
    int m = rows * ROW_H - LST_H + 4;
    return m > 0 ? m : 0;
}

// THE READ OF A FOLDER IN STEPS (a tap on a folder of 262 files must not stop the board).
// A stat on a FAT card searches the folder from its start, so a folder of n files costs about n x n directory entries:
// on the board one read of such a folder took many seconds in ONE frame (no frames, no console, a watchdog restart).
// So list_start() opens the folder, list_step() reads the entries for at most LIST_STEP_MS in each frame (the update
// goes on in the next frames), and list_done() sorts them and sets the scroll. The list shows READING ... while it reads.
#define LIST_STEP_MS 25
static DIR *s_scan;  // the folder that is read now (NULL: the list is complete)
static char s_scan_dir[FL_PATH];
static char s_scan_want[FL_NAME];  // list_done: this entry in view (go_up: the folder that we came from)
bool fil_list_dirty = true;        // a change of the files: page_list() reads the folder again

static void scan_stop(void)
{
    if (s_scan) {
        closedir(s_scan);
    }
    s_scan = NULL;
}

// open the folder now (s_root + s_rel); false: the folder is not there
static bool list_start(void)
{
    scan_stop();
    fil_n = fil_skip = 0;
    fil_rel_lock = fil_rel_locked(fil_rel);
    if (!fil_join(s_scan_dir, sizeof s_scan_dir, fil_root_real(fil_root), fil_rel, NULL)) {
        return false;
    }
#if CONFIG_TFN_DEVICE_PROTECTION
    if (!device_guard_local_path(s_scan_dir)) {
        return false;
    }
#endif
    s_scan = opendir(s_scan_dir);
    return s_scan != NULL;
}

// read entries for at most ms; true: the folder is complete (closed and sorted)
static bool list_step(int ms)
{
    const int64_t t0 = tfn_now_ms();
    int k = 0;
    while (s_scan) {
        struct dirent *de = readdir(s_scan);
        if (!de) {
            scan_stop();
            qsort(fil_ent, (size_t)fil_n, sizeof fil_ent[0], ent_cmp);
            return true;
        }
        const char *nm = de->d_name;
        if (!strcmp(nm, ".") || !strcmp(nm, "..")) {
            continue;
        }
        if (strlen(nm) >= FL_NAME || fil_n >= FL_MAX) {
            fil_skip++;
            continue;
        }
        fent_t *e = &fil_ent[fil_n];
        snprintf(e->name, sizeof e->name, "%s", nm);
        struct stat st;
        bool dd = false;
        e->size = e->mtime = 0;
        if (fil_join(fil_p1, sizeof fil_p1, s_scan_dir, "/", nm) && stat(fil_p1, &st) == 0) {
            dd = S_ISDIR(st.st_mode);
            e->size = dd ? 0 : (uint32_t)st.st_size;
            e->mtime = (uint32_t)st.st_mtime;
        }
        e->kind = (uint8_t)kind_of(s_scan_dir, nm, dd);
        e->lock = fil_rel_lock || name_locked(nm);
        fil_n++;
        k++;
#ifdef FILES_STEP_N
        if (k >= FILES_STEP_N) {
            return false;  // (the host check: a few entries in each step)
        }
#endif
        if (tfn_now_ms() - t0 >= ms) {
            return false;
        }
    }
    return true;
}

bool fil_list_busy(void)
{
    return s_scan != NULL;
}

void fil_go_home(void)
{
    scan_stop();
    fil_page = P_HOME;
    fil_root = -1;
    fil_rel[0] = 0;
    fil_hot = fil_focus = -1;
    store_set("fil_root", 0);
}

// the list is complete: the scroll, the focus, the entry to show
static void list_done(void)
{
    ks_set_max(&fil_ks, (float)fil_max_scroll());
    if (s_scan_want[0]) {
        float to = 0;
        for (int i = 0; i < fil_n; i++) {
            if (strcmp(fil_ent[i].name, s_scan_want)) {
                continue;
            }
            to = (float)(i * ROW_H - LST_H / 2 + ROW_H / 2);
            break;
        }
        ks_jump(&fil_ks, to);
        s_scan_want[0] = 0;
    }
    fil_scroll = ks_pos(&fil_ks);
    if (fil_focus >= fil_n) {
        fil_focus = fil_n - 1;
    }
#if T5P
    fil_t5_after_load();
#endif
}

// read the list again (after a change); the card can be gone: then the home page. The read goes on in the next
// updates when the folder is big (list_step).
void fil_list_load(void)
{
    if (fil_root < 0) {
        return;
    }
#if T5P
    if (fil_list_dirty) {
        fil_side_root = -1;  // (a change of the files: the folder pane reads again)
    }
#endif
    fil_list_dirty = false;
    if (!list_start()) {
        if (fil_root == RT_CARD) {
            fil_card_ok = card_check();
            space_get();
            fil_say(fil_card_ok ? "THE FOLDER IS NOT THERE" : "THE CARD IS NOT THERE", true, tfn_now_ms());
            fil_go_home();
            return;
        }
        bool ok = false;
        if (fil_rel[0]) {  // (a folder that went: back to the root)
            fil_rel[0] = 0;
            ok = list_start();
            if (ok) {
                fil_say("THE FOLDER IS NOT THERE", true, tfn_now_ms());
            }
        }
        if (!ok) {
            fil_say("THE BOARD FILES ARE NOT THERE", true, tfn_now_ms());
            fil_go_home();
            return;
        }
    }
    if (list_step(LIST_STEP_MS)) {
        list_done();
    }
}

void fil_open_root(int r)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    if (!device_guard_local_path(fil_root_real(r))) {
        fil_say("UNLOCK ADMIN ACCESS IN SETUP", true, tfn_now_ms());
        return;
    }
#endif
    fil_root = r;
    fil_rel[0] = 0;
    ks_jump(&fil_ks, 0);
    fil_scroll = 0;
    fil_focus = -1;
    fil_page = P_LIST;
    store_set("fil_root", (uint32_t)(r + 1));
    fil_list_load();
}

void fil_go_into(const char *name)
{
#if T5P
    fil_side_keep();
#endif
    size_t k = strlen(fil_rel);
    if (snprintf(fil_rel + k, sizeof fil_rel - k, "/%s", name) >= (int)(sizeof fil_rel - k)) {
        fil_rel[k] = 0;
        fil_say("THE PATH IS TOO LONG", true, tfn_now_ms());
        return;
    }
    ks_jump(&fil_ks, 0);
    fil_scroll = 0;
    fil_focus = -1;
    fil_list_load();
    sfx_ui(SFX_CLICK);
}

void fil_go_up(void)
{
    if (!fil_rel[0]) {
        fil_go_home();
        sfx_ui(SFX_BACK);
        return;
    }
    char was[FL_NAME];
    char *sl = strrchr(fil_rel, '/');
    snprintf(was, sizeof was, "%s", sl ? sl + 1 : "");
    if (sl) {
        *sl = 0;
    }
    fil_focus = -1;
    snprintf(s_scan_want, sizeof s_scan_want, "%s", was);  // (list_done puts the folder that we came from in view)
    fil_list_load();
    sfx_ui(SFX_BACK);
}

void fil_card_look(void)
{
    bool was = board_sd_mounted();
    fil_card_ok = card_mount();
    if (fil_card_ok && !was) {
        tfn_rescan();  // (a card put in after the start: the menu gets the apps on it)
    }
    fil_data_ok = data_ok();
}

void fil_space_read(void)
{
    space_get();
}

// ---------------------------------------------------------------- update

static void fil_update(int64_t now)
{
    if (fil_list_busy() && list_step(LIST_STEP_MS)) {
        list_done();  // (a big folder: the next part of its read)
    }
#if CONFIG_TFN_DEVICE_PROTECTION
    if (fil_root == RT_BOARD && !device_guard_local_path(STORAGE_ROOT)) {
        if (fil_job.kind != J_NONE) {
            fil_job_stop(now);
        }
        fil_go_home();
        fil_say("ADMIN ACCESS EXPIRED", true, now);
    }
#endif
    s_prev = now;
    ks_set_max(&fil_ks, (float)fil_max_scroll());
    ks_update(&fil_ks, now);
    fil_scroll = ks_pos(&fil_ks);
    if (fil_page == P_TEXT) {
        ks_set_max(&fil_kt, (float)fil_text_max());
        ks_update(&fil_kt, now);
        fil_tscroll = ks_pos(&fil_kt);
    }
#if BOARD_WHEEL
    {
        int st = input_wheel(), pr = input_wheel_presses();
        if (st || pr) {
            fil_wheel(st, pr, now);
        }
    }
#endif
    if (fil_page == P_ASK && fil_qhold_t0 >= 0 && fil_qhot == 1 && now - fil_qhold_t0 >= fil_qhold_ms) {
        fil_qhold_t0 = -1;
        fil_ask_answer(1, now);
    }
    fil_jj_poll();
    if (fil_page == P_PIC && fil_pstate == 0 && fil_drawn == P_PIC && !fil_pic_id) {
        fil_pic_id = fil_jj_start(fil_ppath, SW, SH);
    }
#if T5P
    ks_update(&fil_kside, now);
    fil_pv_update();
#endif
    fil_jj_poll();  // (the PC: the decode is complete at once)
    if (fil_page == P_JOB && fil_job.kind == J_FORMAT && fil_drawn != P_JOB) {
        return;  // (the page shows once before the format)
    }
    if (fil_page == P_JOB && fil_job.kind != J_NONE) {
        fil_job_step();
    }
}

// ---------------------------------------------------------------- the host sandbox

#ifndef ESP_PLATFORM
static void host_put(const char *dir, const char *name, const char *text, int times)
{
    char p[FL_PATH];
    if (!fil_join(p, sizeof p, dir, "", name)) {
        return;
    }
    FILE *f = fopen(p, "wb");
    if (!f) {
        return;
    }
    for (int i = 0; i < times; i++) {
        fputs(text, f);
    }
    fclose(f);
}
static void host_mk(const char *dir, const char *rel)
{
    char p[FL_PATH];
    if (fil_join(p, sizeof p, dir, rel, NULL)) {
        MKDIR(p);
    }
}
static void host_setup(void)
{
    const char *e = getenv("FILES_HOST");
    s_host_box = !e || !e[0];
    if (!s_host_box) {
        snprintf(s_host_base, sizeof s_host_base, "%s", e);
    } else {
        // the fuzz sandbox folder of the board, found from this source file (the host builds run in the repo)
        const char *f = __FILE__, *sl = strrchr(f, '/'), *bs = strrchr(f, '\\');
        if (bs > sl) {
            sl = bs;
        }
        int n = sl ? (int)(sl - f) + 1 : 0;
        snprintf(s_host_base, sizeof s_host_base, "%.*s../tests/host/fuzz_box/fuzz_%s", n, f,
                 BOARD_BAR_SCREEN ? "349"
                 : BOARD_ROUND    ? "wt"
                                  : "s3");
    }
    snprintf(s_host_card, sizeof s_host_card, "%s/card", s_host_base);
    snprintf(s_host_data, sizeof s_host_data, "%s/data", s_host_base);
}
// the sandbox of the fuzz: made again at each enter (only when its folder is there)
static void host_box(void)
{
    if (!s_host_box) {
        return;
    }
    char parent[FL_PATH / 2];
    snprintf(parent, sizeof parent, "%s", s_host_base);
    char *sl = strrchr(parent, '/');
    if (sl) {
        *sl = 0;
    }
    if (!fil_is_dir(parent)) {
        return;
    }
    MKDIR(s_host_base);
    const char *c = s_host_card, *d = s_host_data;
    fil_rm_tree_now(c);  // (the folder itself stays: a sync mark on it stays)
    fil_rm_tree_now(d);
    MKDIR(c);
    MKDIR(d);
    host_mk(c, "/music");
    host_mk(c, "/photos");
    host_mk(c, "/apps");
    host_mk(c, "/apps/dive");
    host_mk(c, "/.trash");
    host_mk(c, "/dive_pkg");
    host_put(c, "notes.txt",
             "A first line of the notes.\nA second, long line of the notes that must wrap on the small screen of the "
             "board.\n\n",
             30);
    host_put(c, "readme.md", "# FILES\nThe test card.\n", 1);
    host_put(c, "data.json", "{\"a\": 1, \"b\": [1, 2, 3]}\n", 1);
    host_put(c, "song.mid", "MThd", 200);
    host_put(c, "/photos/beach.jpg", "JPEG", 100);
    host_put(c, "/music/one.mid", "MThd", 50);
    host_put(c, "/dive_pkg/manifest.txt", "id=dive\nname=DIVE\nver=1.0.2\nkind=lcd349\n", 1);
    host_put(c, "/dive_pkg/app.elf", "ELF", 300);
    host_put(c, "/apps/dive/manifest.txt", "id=dive\n", 1);
    host_put(c, "/.trash/old.txt", "old\n", 1);
    host_mk(d, "/books");
    host_mk(d, "/exports");
    host_mk(d, "/apps");
    host_put(d, "menu.txt", "GROUP GAMES\n", 1);
    host_put(d, "/books/a.txt", "A book.\n", 20);
}
#endif

// ---------------------------------------------------------------- the app

static void fil_init(void)
{
#ifndef ESP_PLATFORM
    host_setup();
#endif
}

static void fil_enter(int64_t now)
{
#ifndef ESP_PLATFORM
    host_box();
#endif
    s_prev = now;
    fil_touch = fil_drag = false;
    fil_hot = fil_focus = -1;
    fil_msg_until = 0;
    ks_init(&fil_ks, LST_H, 0);
    ks_init(&fil_kt, TXT_H, 0);
    fil_scroll = fil_tscroll = 0;
    fil_drawn = -1;
#if T5P
    ks_init(&fil_kside, SIDE_H, 0);
    fil_pv_clear();
#endif
    fil_card_look();
    fil_space_read();
    fil_go_home();
    // the last place (store "fil_root": 1 CARD, 2 BOARD)
    int r = (int)store_get("fil_root", 0);
    if (r == 2 && fil_data_ok) {
        fil_open_root(RT_BOARD);
    } else if (r == 1 && fil_card_ok) {
        fil_open_root(RT_CARD);
    } else {
        store_set("fil_root", (uint32_t)(r == 1 ? 1 : 0));
    }
}

static void fil_leave(void)
{
    scan_stop();
    fil_jj_leave();
    fil_list_dirty = true;
    // a job that runs: it stops (a part of a file goes; the folder of an install goes)
    if (fil_job.kind == J_COPY || fil_job.kind == J_DELETE) {
        fil_job_close(true);
        if (fil_job.install) {
            fil_rm_tree_now(fil_job.dst);
        }
    }
    fil_job.kind = J_NONE;
    fil_job_free();
    fil_text_free();
    fil_pic_free();
#if T5P
    fil_pv_clear();
#endif
}

#include "../libs/tficon/tficon.h"
// the menu icon: a folder; a page rises out of it and goes back in
static void fil_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0x4fc3f7), tfi_hex(0x1360c2));
    float u = s / 100.0f, ph = tfi_ph(2);
    float up = 0.5f - 0.5f * fcos(ph * 6.2831853f);
    int fx = x + (int)(19 * u), fw = (int)(62 * u), fy = y + (int)(33 * u);
    // the back with its tab, the pages (one rises), the front with a soft shadow on the page
    tfi_rbox(fx, fy - (int)(8 * u), (int)(26 * u), (int)(14 * u), (int)(4 * u), tfi_hex(0xf6a826), tfi_hex(0xe08a00),
             256);
    tfi_rbox(fx, fy, fw, (int)(48 * u), (int)(5 * u), tfi_hex(0xf0a020), tfi_hex(0xcf7600), 256);
    tfi_rbox(fx + (int)(9 * u), fy + (int)(3 * u), fw - (int)(18 * u), (int)(36 * u), (int)(3 * u), tfi_hex(0xe6ecf6),
             tfi_hex(0xc6d0e2), 256);
    int py = fy + (int)((5 - up * 22) * u), lh = (int)(2.6f * u) > 1 ? (int)(2.6f * u) : 1;
    tfi_rbox(fx + (int)(6 * u), py, fw - (int)(12 * u), (int)(40 * u), (int)(3 * u), rgb(255, 255, 255),
             tfi_hex(0xdde6f2), 256);
    for (int k = 0; k < 3; k++) {
        tfd_rect(fx + (int)(12 * u), py + (int)((7 + k * 6) * u), (int)((k == 2 ? 22 : 36) * u), lh, tfi_hex(0x4d86e0),
                 256);
    }
    int ffy = fy + (int)(15 * u);
    tfd_rect(fx + (int)(2 * u), ffy - (int)(2 * u), fw - (int)(4 * u), (int)(2 * u) > 1 ? (int)(2 * u) : 1, 0, 50);
    tfi_gloss_box(fx, ffy, fw, (int)(33 * u), (int)(6 * u), tfi_hex(0xffd85e), tfi_hex(0xff9d00));
    tfi_end();
}

const app_t app_files = {
        .name = "FILES",
        .landscape = true,
        .round_fit = true,  // (the Watcher: the 320 x 240 frame at 1:1 in the circle)
        .wheel = true,
        .needs = 0,  // a layout for the bar screen and one for 320 x 240
        .init = fil_init,
        .enter = fil_enter,
        .leave = fil_leave,
        .input = fil_input,
        .update = fil_update,
        .render = fil_render,
        .draw_icon = fil_icon,
};
