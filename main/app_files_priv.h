#pragma once
// The private header of FILES: main/app_files.c and its parts app_files_act.c, app_files_job.c,
// app_files_view.c, app_files_t5.c, app_files_draw.c and app_files_input.c. Only these files include it.

#include <ctype.h>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "tfn_util.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "app.h"
#include "audio.h"
#include "board.h"
#include "files.h"
#include "gfx.h"
#include "input.h"
#include "kbd.h"
#include "kfont.h"
#include "mfont.h"
#include "settings.h"
#include "storage.h"
#include "device_guard.h"
#include "tfn_app.h"
#include "uikit.h"
#include "tfn/theme.h"
#include "../libs/kscroll/kscroll.h"
#ifdef ESP_PLATFORM
#include "esp_vfs_fat.h"
#include "tjpgd_rom.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#ifdef _WIN32
#define MKDIR(p) mkdir(p)  // (the host test on Windows)
#else
#define MKDIR(p) mkdir(p, 0777)
#endif

#if defined(FILES_IO_COUNT) && !defined(ESP_PLATFORM)
// (the host check counts the file calls; a call on the card is slow on a board)
extern int fil_io_n;
#define stat(p, s)  (fil_io_n++, stat(p, s))
#define opendir(p)  (fil_io_n++, opendir(p))
#define readdir(d)  (fil_io_n++, readdir(d))
#define fopen(p, m) (fil_io_n++, fopen(p, m))
#endif

// ---------------------------------------------------------------- layout

#if BOARD_BAR_SCREEN
// the 640 x 172 bar: one line in each row of 43 px (5.7 mm)
#define SW        640
#define SH        172
#define HEAD_H    44  // the header: UP, the path and the free space, MORE
#define UP_W      92
#define MORE_X    548
#define PATH_X    104
#define PATH_W    432
#define PATH_B1   20  // the baselines of the path and of the space line
#define PATH_B2   39
#define LIST_Y    44
#define ROW_H     43
#define ROW_W     624
#define ICON_X    12
#define ICON_W    30
#define NAME_X    54
#define SIZE_R    612  // the right end of the size
#define SBAR_X    631
#define TWO_LINES 0
// the home page: the title, CARD TOOLS, two tiles side by side
#define HOME_H  46
#define TOOLS_X 446
#define TOOLS_W 188
#define TILE_Y  50
#define TILE_H  118
#define TILE_W  311
#define TILE2_X 323
#define TILE2_Y 50
// the action panel: a header, then 3 columns of buttons
#define PAN_H    46
#define PAN_COLS 3
#define CLOSE_W  84
// the question page (as kbd_ask_render: NO x 0 .. 220, YES x 420 .. 640)
#define ASK_Y  60
#define ASK_W  220
#define ASK_B1 28
#define ASK_B2 50
// the text page
#define TXT_TOP 44
#define TXT_X   14
#define TXT_W   604
#define TXT_LH  22
#elif BOARD_UI_TABLET
// the 640 x 360 frame of the Tab5 (three panes, see "THE TAB5: THREE PANES" below): the folders at the
// left, the things of the folder now in the middle (rows of 52 px = 8.8 mm, two lines), a preview at the right
#define SW        640
#define SH        360
#define HEAD_H    50
#define UP_W      92
#define MORE_X    548
#define PATH_X    104
#define PATH_W    432
#define PATH_B1   22
#define PATH_B2   42
#define LIST_Y    52
#define ROW_H     52
#define ROW_W     624
#define ICON_X    12
#define ICON_W    30
#define NAME_X    54
#define SIZE_R    612
#define SBAR_X    631
#define TWO_LINES 1
#define T5P       1    // the three panes
#define SIDE_W    176  // the folder pane: x 0 .. 175
#define SROW_H    52
#define MID_X     178  // the list pane: x 178 .. 413
#define MID_W     236
#define PV_X      416  // the preview pane: x 416 .. 639
#define PV_W      224
// the home page: the title, CARD TOOLS, two wide tiles, one above the other
#define HOME_H    48
#define TOOLS_X   446
#define TOOLS_W   188
#define TILE_Y    54
#define TILE_H    146
#define TILE_W    628
#define TILE2_X   6
#define TILE2_Y   206
#define PAN_H     50
#define PAN_COLS  3
#define CLOSE_W   84
#define ASK_Y     110
#define ASK_W     220
#define ASK_B1    40
#define ASK_B2    76
#define TXT_TOP   50
#define TXT_X     14
#define TXT_W     604
#define TXT_LH    22
#elif BOARD_ROUND
// The round panels (APP_ROUND_HI: 466 x 466 on the AMOLED-1.75C, 412 x 412 on the Watcher; FR() scales the values of
// 466 to the frame). The top cap has the title (the path, the name) and the space, the middle box (MB_X .. MB_X + MB_W,
// y LIST_Y .. LIST_B) has the rows, the text or the buttons, the bottom cap has the buttons UP, MORE, BACK, CLOSE or
// STOP (y BAR_Y, BAR_H). The drawing of the round pages: app_files_round.inc.
#define FR(v)     ((v) * gfx_w / 466)
#define SW        gfx_w
#define SH        gfx_h
#define MB_X      FR(50)
#define MB_W      FR(366)
#define HEAD_H    FR(96)
#define UP_W      FR(105)
#define MORE_X    FR(238)
#define PATH_X    FR(123)
#define PATH_W    FR(220)
#define PATH_B1   FR(52)
#define PATH_B2   FR(80)
#define LIST_Y    FR(96)
#define LIST_B    FR(374)  // the bottom of the middle box
#define BAR_Y     FR(382)  // the buttons of the bottom cap
#define BAR_H     FR(52)
#define ROW_H     FR(58)
#define ROW_W     MB_W
#define ICON_X    (MB_X + FR(10))
#define ICON_W    FR(36)
#define NAME_X    (MB_X + FR(56))
#define SIZE_R    (MB_X + MB_W - FR(10))
#define SBAR_X    (MB_X + MB_W + FR(4))
#define TWO_LINES 1
#define HOME_H    FR(96)
#define TOOLS_X   FR(133)
#define TOOLS_W   FR(200)
#define TILE_Y    FR(98)
#define TILE_H    FR(126)
#define TILE_W    FR(360)
#define TILE1_X   FR(53)
#define TILE2_X   FR(53)
#define TILE2_Y   FR(232)
#define PAN_H     FR(96)
#define PAN_COLS  2
#define CLOSE_W   FR(110)
#define ASK_Y     FR(226)
#define ASK_H     FR(110)  // the two answers: side by side in the middle box
#define ASK_W     (MB_W / 2)
#define ASK_B1    FR(150)
#define ASK_B2    FR(192)
#define TXT_TOP   FR(96)
#define TXT_X     (MB_X + FR(4))
#define TXT_W     (MB_W - FR(8))
#define TXT_LH    22
#else
// the 320 x 240 frame (the CoreS3, the AMOLED, the Watcher with round_fit): two lines in each row of 58 px
#define SW        320
#define SH        240
#define HEAD_H    50
#define UP_W      62
#define MORE_X    258
#define PATH_X    70
#define PATH_W    182
#define PATH_B1   22
#define PATH_B2   42
#define LIST_Y    52
#define ROW_H     58
#define ROW_W     310
#define ICON_X    10
#define ICON_W    34
#define NAME_X    54
#define SIZE_R    300
#define SBAR_X    313
#define TWO_LINES 1
#define HOME_H    46
#define TOOLS_X   150
#define TOOLS_W   166
#define TILE_Y    50
#define TILE_H    92
#define TILE_W    312
#define TILE2_X   4
#define TILE2_Y   146
#define PAN_H     50
#define PAN_COLS  2
#define CLOSE_W   58
#define ASK_Y     96
#define ASK_W     124
#define ASK_B1    36
#define ASK_B2    68
#define TXT_TOP   48
#define TXT_X     10
#define TXT_W     294
#define TXT_LH    22
#endif
#ifndef T5P
#define T5P 0
#endif
#if BOARD_ROUND
#define LST_H (LIST_B - LIST_Y)
#else
#define LST_H (SH - LIST_Y)
#endif
#if BOARD_BAR_SCREEN || BOARD_UI_TABLET
#define WIDE_UI 1  // a frame of 640 px width (3 columns of buttons)
#else
#define WIDE_UI 0
#endif
#if BOARD_ROUND
#define TXT_H (LIST_B - TXT_TOP)
#else
#define TXT_H (SH - TXT_TOP)
#endif

#define DRAG_PX      10    // finger travel that puts out the light of a row
#define ASK_GUARD_MS 450   // a touch that starts this soon after a question opens does nothing
#define HOLD_MS      1200  // the hold of YES for a big delete and for the format

// ---------------------------------------------------------------- state

#define FL_MAX    256  // the entries of one folder that show
#define FL_NAME   96
#define FL_PATH   320
#define JOB_MAX   512  // the files and folders of one copy or delete
#define JOB_REL   200
#define CHUNK     (16 * 1024)
#define TXT_MAX   (64 * 1024)
#define TXT_LINES 6000

enum {
    RT_CARD,
    RT_BOARD
};
enum {
    K_DIR,
    K_PKG,
    K_TEXT,
    K_PIC,
    K_FILE
};
typedef struct {
    char name[FL_NAME];
    uint32_t size;
    uint32_t mtime;
    uint8_t kind, lock;
} fent_t;

enum {
    P_HOME,
    P_LIST,
    P_ACT,
    P_ASK,
    P_KBD,
    P_INFO,
    P_TEXT,
    P_PIC,
    P_JOB
};
extern int fil_page;
extern int fil_root;           // RT_CARD, RT_BOARD (-1: the home page)
extern char fil_rel[FL_PATH];  // the folder in the root: "" or "/a/b"
extern fent_t fil_ent[FL_MAX];
extern int fil_n, fil_skip;  // the entries; the entries that do not show (too many, a long name)
extern bool fil_rel_lock;    // the folder now is locked

// the card and the space
extern bool fil_card_ok, fil_data_ok;
extern uint64_t fil_card_t, fil_card_f, fil_data_t, fil_data_f;

// the list: the scroll (px), the touch
extern float fil_scroll;          // the position of the list to draw (ks_pos(&s_ks), set in each update)
extern kscroll_t fil_ks, fil_kt;  // the momentum scroll of the list and of the text view (libs/kscroll)
extern bool fil_touch, fil_drag;
extern int fil_x0, fil_y0;
extern int fil_hot;    // the control under the finger (-1: none)
extern int fil_focus;  // the wheel: the row (or the button) in focus
extern int fil_drawn;  // the page of the last render (a slow step waits until its page shows)

// controls (the rows of the list are 0 .. s_n - 1)
enum {
    CT_UP = 1000,
    CT_MORE,
    CT_EMPTY,
    CT_CARD,
    CT_BOARD,
    CT_TOOLS,
    CT_CLOSE,
    CT_STOP
};

// the action panel
enum {
    A_OPEN,
    A_INSTALL,
    A_FILES,
    A_INFO,
    A_RENAME,
    A_COPY,
    A_DELETE,
    A_NEWDIR,
    A_LOOK,
    A_TOOLS,
    A_FORMAT
};
enum {
    PK_ENTRY,
    PK_MORE,
    PK_TOOLS
};
extern uint8_t fil_act[8];
extern int fil_nact, fil_pkind, fil_sel;
extern char fil_ptitle[FL_NAME + 16], fil_pnote[112];

// the question page
enum {
    Q_DELETE,
    Q_REPLACE,
    Q_FORMAT1,
    Q_FORMAT2,
    Q_INSTALL
};
extern int fil_qkind, fil_qhot, fil_qfocus, fil_qhold_ms;
extern char fil_q1[FL_NAME + 32], fil_q2[128];
extern const char *fil_qa[2];
extern bool fil_qclose, fil_qguard, fil_qarmed;
extern int64_t fil_qopen_t, fil_qhold_t0;

// the info page
extern char fil_info_l[6][16], fil_info_v[6][FL_PATH / 2];
extern int fil_info_n;
extern char fil_info_name[FL_NAME];

// a message at the bottom
extern char fil_msg[112];
extern bool fil_msg_err;
extern int64_t fil_msg_until;

// the text page
extern char *fil_txt;
extern uint32_t *fil_loff;
extern uint16_t *fil_llen;
extern int fil_nl;
extern uint32_t fil_tlen, fil_tsize;
extern float fil_tscroll;
extern char fil_tname[FL_NAME];

// the picture page
extern uint16_t *fil_pic;
extern int fil_pw, fil_ph, fil_pstate;  // 0 loading, 1 ready, 2 error
extern char fil_pname[FL_NAME], fil_perr[64], fil_ppath[FL_PATH];
extern int64_t fil_pbar_until;

// the job (copy, delete, format)
enum {
    J_NONE,
    J_COPY,
    J_DELETE,
    J_FORMAT
};
typedef struct {
    char rel[JOB_REL];
    uint32_t size;
    uint8_t dir;
} jit_t;
typedef struct {
    int kind, n, i, files, files_done;
    jit_t *it;
    char src[FL_PATH], dst[FL_PATH], cur[FL_PATH];
    FILE *fi, *fo;
    uint64_t total, done;
    bool install;
    int place, dst_root;
    char title[48], name[FL_NAME];
} fil_job_t;
extern fil_job_t fil_job;
extern char fil_p1[FL_PATH], fil_p2[FL_PATH];  // work paths

#if T5P
// the Tab5 panes: the folder pane (the folders beside the folder now; at a root: CARD and BOARD) and the preview
#define SIDE_MAX 128
#define PV_TXT   4096  // the first bytes of a text in the preview
#define PV_LINES 64
typedef struct {
    char name[FL_NAME];
    uint8_t lock;
} side_t;
extern side_t fil_side[SIDE_MAX];
extern int fil_nside, fil_side_cur;  // the lit row (-1: none)
extern bool fil_side_roots;          // the folder pane shows CARD and BOARD (the list is a root)
extern kscroll_t fil_kside;
extern int fil_pv;                                  // the thing in the preview (an index of s_ent; -1: none)
extern int fil_pv_root, fil_pv_kind, fil_pv_state;  // the state: 0 a picture to open, 1 ready, 2 a fault (s_pv_err)
extern char fil_pv_name[FL_NAME], fil_pv_rel[FL_PATH], fil_pv_err[64];
extern char fil_pv_txt[PV_TXT + 1];
extern uint32_t fil_pv_loff[PV_LINES];
extern uint16_t fil_pv_llen[PV_LINES];
extern int fil_pv_nl;
extern uint16_t *fil_pv_pic;
extern int fil_pv_w, fil_pv_h;
extern int fil_pv_id;      // the decode that the preview waits for (jj_start; 0: none)
extern bool fil_pv_drawn;  // the preview showed OPENING once (then the decode)
extern int fil_side_root;  // the folder pane has the folders of this parent (-1: not known)
extern char fil_side_par[FL_PATH];
#define SIDE_TOP  LIST_Y
#define SIDE_H    (SH - LIST_Y)
#define PV_IX     (PV_X + 10)  // the inner part of the preview
#define PV_IW     (PV_W - 20)
#define PV_BTN_H  48
#define PV_BTN_Y  (SH - PV_BTN_H - 6)
#define PV_BODY_Y (LIST_Y + 66)  // under the name and the facts line
#define PV_BODY_H (PV_BTN_Y - 6 - PV_BODY_Y)
#define PV_LH     21  // the line of the text preview (KF_NAME)
enum {
    CT_SIDE = 2000,
    CT_PVOPEN = 3000,
    CT_PVMORE,
    CT_PVBODY
};
#endif

#define STOP_X 0
#define STOP_W (WIDE_UI ? 180 : 140)
#define STOP_H 50

// the shared variables of the parts
extern bool fil_list_dirty;  // a change of the files: page_list() reads the folder again
extern int fil_pic_id;       // the decode that the picture page waits for (jj_start; 0: none)

// app_files.c
const char *fil_root_real(int r);
const char *fil_root_name(int r);
bool fil_join(char *out, size_t n, const char *a, const char *b, const char *c);
bool fil_is_dir(const char *p);
bool fil_exists(const char *p);
int fil_ci_cmp(const char *a, const char *b);
void fil_upper(char *s);
void fil_fmt_size(char *out, int n, uint64_t b);
void fil_fmt_date(char *out, int n, uint32_t t);
void fil_say(const char *m, bool err, int64_t now);
bool fil_rel_locked(const char *rel);
int fil_max_scroll(void);
bool fil_list_busy(void);
void fil_go_home(void);
void fil_list_load(void);
void fil_open_root(int r);
void fil_go_into(const char *name);
void fil_go_up(void);
void fil_card_look(void);
void fil_space_read(void);

// app_files_act.c
void fil_page_list(void);
void fil_panel_open_entry(int i);
void fil_panel_open_more(void);
void fil_panel_open_tools(void);
const char *fil_act_label(int a);
void fil_act_box(int i, int *x, int *y, int *w, int *h);
int fil_act_at(int x, int y);
int fil_ask_at(int x, int y);
void fil_ask_answer(int a, int64_t now);
void fil_sel_path(char *out, size_t n);
void fil_open_entry(int i, int64_t now);
void fil_kbd_done(int r, int64_t now);
void fil_do_act(int a, int64_t now);

// app_files_job.c
bool fil_job_scan(const char *base);
bool fil_job_alloc(void);
void fil_job_free(void);
void fil_rm_tree_now(const char *base);
void fil_job_close(bool drop_cur);
void fil_job_copy(const char *src, const char *dst, const char *title, int dst_root, bool install, int place);
void fil_job_step(void);
void fil_job_stop(int64_t now);

// app_files_view.c
void fil_open_info(int64_t now);
void fil_text_free(void);
int fil_wrap_lines(const kfont_t *f, const char *t, uint32_t n, int w, uint32_t *loff, uint16_t *llen, int max);
void fil_open_text(int64_t now);
int fil_text_max(void);
void fil_pic_free(void);
int fil_jj_start(const char *path, int bw, int bh);
void fil_jj_poll(void);
void fil_jj_leave(void);
void fil_open_pic(int64_t now);

// app_files_t5.c
void fil_pv_clear(void);
void fil_side_keep(void);
void fil_t5_after_load(void);
void fil_pv_update(void);
const char *fil_pv_open_word(void);
void fil_t5_list_input(const input_event_t *ev);

// app_files_draw.c
int fil_u8cp(const char *s, uint32_t *i, uint32_t n);
float fil_kadv(const kfont_t *f, int c);
void fil_render(int64_t now);

// app_files_input.c
void fil_input(const input_event_t *ev);
void fil_wheel(int st, int pr, int64_t now);
