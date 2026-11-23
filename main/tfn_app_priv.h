#pragma once
// The private header of the installed apps of TFN OS: main/tfn_app.c (the ABI table, the list, the policy, the
// icons, the scan, the state) and its parts tfn_load.c, tfn_install.c, tfn_watch.c, and the publisher list
// tfn_pub.c and the static store tfn_sstore.c. Only these files include it.
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sdkconfig.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "psa/crypto.h"
#include "esp_elf.h"
#include "private/elf_platform.h"
#include "private/elf_symbol.h"
#include "app.h"
#include "audio.h"
#include "tfn_defaults.h"
#include "ota.h"
#include "layout.h"
#include "board.h"
#include "gfx.h"
#include "mfont.h"
#include "settings.h"
#include "storage.h"
#include "tfn_app.h"
#include "evlog.h"
#include "device_guard.h"
#include "watch.h"
#include "notify.h"
#include "tfphys.h"
#include "tfsfx.h"
#include "link.h"
#include "wifi.h"
#include "wifi_capture.h"
#include "tfn_store.h"
#include "tfn_pub.h"

// TFN_ABI_VERSION and TFN_ABI_REV of the ABI table (tfn_app.c reads its names with its own ELFSYM)
#define ELFSYM(n, lo, hi)
#include "tfn_abi.inc"
#undef ELFSYM

#define TFN_KIND BOARD_KIND_NAME
// the kind of the build as 0 / 1 for the conditions of the ABI table (tfn_abi.inc): the header of the board
// (boards/<kind>/board_caps.h) defines only its own BOARD_KIND_<kind>
#ifdef BOARD_KIND_LCD349
#define TFN_KIND_LCD349 1
#else
#define TFN_KIND_LCD349 0
#endif
#ifdef BOARD_KIND_TAB5
#define TFN_KIND_TAB5 1
#else
#define TFN_KIND_TAB5 0
#endif
#ifdef BOARD_KIND_CORES3
#define TFN_KIND_CORES3 1
#else
#define TFN_KIND_CORES3 0
#endif
#ifdef BOARD_KIND_AM18
#define TFN_KIND_AM18 1
#else
#define TFN_KIND_AM18 0
#endif
#ifdef BOARD_KIND_WATCHER
#define TFN_KIND_WATCHER 1
#else
#define TFN_KIND_WATCHER 0
#endif
#ifdef BOARD_KIND_AM175
#define TFN_KIND_AM175 1
#else
#define TFN_KIND_AM175 0
#endif
#ifdef BOARD_KIND_CROW7
#define TFN_KIND_CROW7 1
#else
#define TFN_KIND_CROW7 0
#endif
// a big PSRAM (the Tab5: 32 MB): the icon files and the decoded frames stay at an app load while the PSRAM has room,
// and the prefetch task reads all icons at the start
#define TFA_ICON_BIG (BOARD_PSRAM_MB >= 32)
// the menus (all boards but the Tab5): a low task reads the icon files that the menu asks for (tfn_icon_ask), so that a
// scroll never waits for the card (on the 3.49 a read of an icon file took 50-190 ms in a frame of the menu)
#define TFA_ICON_ASK (!TFA_ICON_BIG)
// a task reads icon files: the flags of an icon that it reads now, the count of the rescans
#define TFA_ICON_TASK (TFA_ICON_BIG || TFA_ICON_ASK)
#define OWN_FILES     16
#define OWN_TASKS     8
typedef struct {
    void **mem;  // an open-address set of the blocks (PSRAM; NULL = an empty place, 1 = a removed one)
    int cap, used, live;
    FILE *files[OWN_FILES];
    TaskHandle_t tasks[OWN_TASKS];
    size_t bytes_now;  // (for the log; approximate)
} own_t;
extern own_t tfa_own[2];
extern int tfa_owner;
#define OWN_GONE ((void *)1)
#define WMAX     8
typedef struct {
    char id[24], name[24];  // the app (a copy: the list of the apps can change while a check runs)
    uint32_t code;          // the version of the app that the watcher is from
    uint16_t every;         // its time between two checks (minutes)
    bool on;
    esp_elf_t elf;
    watch_t real;
    own_t own;       // its memory and files (freed at the unload)
    int64_t last;    // the time of its last check (ms; 0 = none yet)
    int last_ms;     // the length of its last check
    uint8_t faults;  // checks in a row that took too long
} wslot_t;
extern SemaphoreHandle_t tfa_lock;
#define LOCK()                                                                                                         \
    do {                                                                                                               \
        if (tfa_lock)                                                                                                  \
            xSemaphoreTakeRecursive(tfa_lock, portMAX_DELAY);                                                          \
    } while (0)
#define UNLOCK()                                                                                                       \
    do {                                                                                                               \
        if (tfa_lock)                                                                                                  \
            xSemaphoreGiveRecursive(tfa_lock);                                                                         \
    } while (0)
#if TFA_ICON_BIG
#define ICON_BUDGET    (12 * 1024 * 1024)  // (the Tab5: 32 MB PSRAM, about 23 MB free at the menu; 124 icons = ~9 MB)
#define ICON_KEEP_FREE (6 * 1024 * 1024)   // the icons stay at an app load while more than this PSRAM is free
#else
// (8 MB PSRAM, about 6.5 MB free at the menu: the icons of a scroll of a long group and its read ahead stay; the load of
// an app frees them all)
#define ICON_BUDGET (3 * 1024 * 1024)
#endif
#define ICON_KEEP_MS 1500
typedef struct {
    int idx;  // the entry (-1 = free)
    esp_elf_t elf;
    uint8_t *file;
    app_t real;  // the app_t of the app, in RAM (main.c keeps its address while the app is open)
} loaded_t;
#define GUARD_MAGIC 0x54464e47u
typedef struct {
    uint32_t magic;
    char id[24];
} tfa_guard_t;
extern tfa_guard_t tfa_guard;

#if TFA_ICON_BIG
// (the Tab5: a page has about 40 icons; with 24 slots each frame decoded most of them again: 150-350 ms a frame:
// unusable. 96 slots = 1.5 MB of its 32 MB PSRAM)
#define DEC_SLOTS 96
#else
#define DEC_SLOTS 24
#endif
typedef struct {
    const uint8_t *b;  // the icon and the frame in the slot (b NULL: free)
    size_t blen;
    int f;
    uint32_t used;  // the last use (the oldest slot goes first)
    uint8_t *idx;   // one frame of indexes (PSRAM, 128 x 128 at most)
} dec_t;

// the shared functions and variables of the parts
// (the icons: tfn_icons.c)
extern uint8_t *tfa_icon[TFN_MAX];
extern size_t tfa_icon_len[TFN_MAX];
extern bool tfa_icon_bad[TFN_MAX];
extern uint32_t tfa_icon_used[TFN_MAX];
#if TFA_ICON_TASK
extern volatile bool tfa_icon_busy[TFN_MAX];
extern volatile uint32_t tfa_icon_gen;
#endif
#if TFA_ICON_BIG
void tfa_icon_prefetch_task(void *arg);
#endif
extern size_t tfa_icon_bytes;
extern dec_t tfa_dec[DEC_SLOTS];
extern void (*const tfa_icon_fns[TFN_MAX])(int, int, int);
void tfa_stub_enter(int64_t now);
void tfa_stub_input(const input_event_t *ev);
void tfa_stub_frame(int64_t now);
// (the scan: tfn_scan.c)
void tfa_do_rescan(void);
// (the files and checks, the policy, the list: tfn_app.c)
void tfa_sig_cache_save(void);
bool tfa_sig_ok(const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl);  // the built-in keys only
bool tfa_sig_ok_pk(const uint8_t *pk, const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl);
const uint8_t *tfa_builtin_key(int i);  // the built-in key i (0, 1) of the store; all zeros = no key
long tfa_mnum(const char *txt, const char *key);
extern int tfa_dropped;
extern bool tfa_safe;
bool tfa_id_ok(const char *id);
bool tfa_on_watch_task(void);
bool tfa_own_tasks_run(int j);
bool tfa_read_all(const char *path, uint8_t **out, size_t *len, size_t max);
bool tfa_read_entry(const char *dir, int place, tfn_entry_t *e);
bool tfa_rm_tree(const char *dir);
bool tfa_watch_crashed(void);
bool tfa_unload(int j);
const char *tfa_secret_owner(void);
extern TaskHandle_t tfa_wtask;
extern bool tfa_link_watch;
extern char tfa_denied[48];
extern int tfa_capture_owner;
extern int tfa_sfx_owner;  // (the slot of the app of the last tfsfx_load + 1; 0 = none)
extern int tfa_n;
extern int tfa_pol_ver;
extern int tfa_reloc_bad;
extern int tfa_wcur;
extern loaded_t tfa_ld[2];
extern tfn_entry_t *tfa_e;
extern uint32_t tfa_gen;
extern uint32_t tfa_link_perms;
extern wslot_t tfa_w[WMAX];
int tfa_cls_of(const char *id, int l);
int tfa_own_release(int j, size_t *bytes);
int tfa_own_release_o(own_t *o, size_t *bytes);
uintptr_t tfa_resolve_sym(const char *name);
void tfa_crash_key(const char *id, char key[16]);
void tfa_fnv64(const char *s, uint32_t *lo, uint32_t *hi);
void tfa_icon_files_free(void);
void tfa_thumbs_free(void);
void tfa_icons_lean_now(
        void);  // (the voice starts with the screen off: the icon files and pictures go now)           // the still pictures of the icons go (tfn_icons.c)
extern volatile bool tfa_icons_lean;  // the voice needs the PSRAM: the menu keeps few icon files (tfn_speech.c)
void tfa_watch_lock(void);            // the lock of the watchers: no load, unload or check (tfn_watch.c)
void tfa_watch_unlock(void);
void tfa_mval(const char *txt, const char *key, char *out, int max);
void tfa_seg_end_set(const uint8_t *file);
void tfa_sha256_hex(const uint8_t *d, size_t n, char hex[65]);
// the SHA-256 of a file, read in pieces (no block of the file size: an install also works while an app holds most of
// the memory). false: no file, more than max bytes, or a read fault
bool tfa_sha256_file(const char *path, size_t max, char hex[65]);
