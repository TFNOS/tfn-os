#pragma once
// TFN OS: the installed apps (app files from the store, the manager or an SD card). See main/tfn_app.c.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "app.h"

// the most installed apps (about 22 bytes of internal RAM for each place: the menu tables)
#define TFN_MAX 256
int tfn_dropped(void);  // the apps of the last scan that had no place in the list (0 = all are listed)
#define TFN_DATA_DIR "/data/apps"
#define TFN_CARD_DIR "/sdcard/apps"
// The files of an app folder (the install, the move, the upload, the store download). "sigs" = the signatures of
// the developers (main/tfn_pub.h); an old firmware does not copy it, and needs it not.
#define TFN_APP_FILES  {"manifest.txt", "app.elf", "icon.bin", "sig", "watch.elf", "sigs"}
#define TFN_APP_NFILES 6
// The upload folder of each installer is <place>/<prefix><id> (place: TFN_DATA_DIR or TFN_CARD_DIR). Each installer
// has its own folder: two installs of the same app on two tasks never write the same files. An id has no '-'.
#define TFN_TMP_USB   ".tmp_"    // the files that came over the USB console ("sdusb put", then "apps commit")
#define TFN_TMP_WEB   ".tmp_w-"  // the web upload (POST /api/apps?op=put, main/tfn_web.c)
#define TFN_TMP_STORE ".tmp_s-"  // a download of the store (the manager or the static store)
#define TFN_TMP_MOVE  ".tmp_m-"  // the copy of a move to the other place
// Remove the upload folders (.tmp_*) of the installs that did not end (tfn_init does it at the start).
void tfn_tmp_clean(void);
// Remove one upload folder (a name that starts with .tmp_): its files, then the folder. An upload folder has no
// subfolders: the remove never goes into one, so a folder with a subfolder stays (false). A folder with another name
// stays too.
bool tfn_tmp_drop(const char *tmp);
// One install of an app at a time: an install, a move, a store download and a remove mark the app id first.
// tfn_inst_begin() gives false (msg tells why) when another task has the mark of the id now. The task that has the
// mark can take it again (a store download calls tfn_install). Each true needs one tfn_inst_end().
bool tfn_inst_begin(const char *id, char *msg, int len);
void tfn_inst_end(const char *id);
// true while a task has a mark of tfn_inst_begin (an install, a move, a store download or a remove runs)
bool tfn_inst_busy(void);

enum {
    TFN_OK,
    TFN_DEV,
    TFN_BAD_SIG,
    TFN_BAD_ABI,
    TFN_BAD_KIND,
    TFN_BAD_FILE,
    TFN_STOPPED
};
enum {
    TFN_BOARD,
    TFN_CARD
};

typedef struct {
    char id[24], name[24], ver[16], group[12], saves[16];
    uint32_t code, bytes, ram;
    uint32_t perms;     // the permission classes of its manifest (bits: tfn_perm_text)
    uint16_t abi, rev;  // the ABI version and the revision that it needs
    uint8_t place, state;
    char dir[40];  // /data/apps/<id> or /sdcard/apps/<id>
    char elf_sha[65];
    char watch_sha[65];  // watch.elf (watch.h): its SHA-256 ("" = the app has no watcher)
    uint16_t watch_min;  // the time between two checks of the watcher (minutes; 0 = none)
    uint8_t form;        // the manifest has form= (the account form on the board page /app/<id>)
    uint8_t is_new;      // the manifest has new=1 ("new" in main/apps.json): first in the menu, the NEW pill
    uint8_t pub_st;      // the signature that passed: TFN_PUB_OK_BUILTIN .. TFN_PUB_NOT_REVIEWED (tfn_pub.h)
    char pub[16];        // the name of the added key that passed ("" = a built-in key, or none)
    app_t stub;          // the menu item: the name, the flags, the icon (tfn_resolve gives the real app)
} tfn_entry_t;

// main.c, at start: the safe mode (the BOOT key held), the crash guard, the list of the installed apps.
void tfn_init(void);
bool tfn_safe_mode(void);
// The installed apps (all states; the menu takes the ones with tfn_runs()).
int tfn_count(void);
const tfn_entry_t *tfn_entry(int i);
bool tfn_runs(const tfn_entry_t *e);
const app_t *tfn_find(const char *name);  // the menu item of an installed app (TFN_OK / TFN_DEV), or NULL
bool tfn_is_stub(const app_t *a);
const tfn_entry_t *tfn_entry_of(const app_t *a);  // the entry of a menu item or of a loaded app (NULL: built in)
// main.c switch_app: a menu item of an installed app -> the loaded app (NULL and a line in msg when it cannot load);
// any other app comes back as it is. tfn_left: after leave() of the old app (the app file is freed).
const app_t *tfn_resolve(const app_t *a, char *msg, int len);
void tfn_left(const app_t *a);
// is `running` (an open app) the app of the menu item `item`? (an installed app: its loaded real app; else the same)
bool tfn_same_app(const app_t *item, const app_t *running);
// The list changes (an install, a removal, a card in or out): the number goes up; the menu builds its list again.
uint32_t tfn_gen(void);
void tfn_rescan(void);  // any task: a scan of the folders at the next tfn_tick()
void tfn_tick(void);    // the app task, each frame (main.c)
// Install: the files of an app are in dir_tmp (manifest.txt, app.elf, icon.bin, sig); check them and move them to
// the place (TFN_BOARD or TFN_CARD). Remove: the files of an installed app.
bool tfn_install(const char *dir_tmp, int place, char *msg, int len);
bool tfn_remove(const char *id, char *msg, int len);
bool tfn_move(const char *id, int place, char *msg, int len);  // to the other place (TFN_BOARD or TFN_CARD)
// The board kind ("lcd349", "cores3", "am18", "watcher") and the ABI version of this firmware.
const char *tfn_kind(void);
int tfn_abi(void);
int tfn_abi_rev(void);  // the revision of the ABI (more names; main/tfn_abi_extra.json)
// The permission classes (sdk/docs/raw.md): "wifi.scan,..." -> the bits (*unknown = true for a class that this
// firmware does not know); the text that the STORE shows for the bits ("SCAN THE WIFI NETWORKS"; "" = none).
uint32_t tfn_perms_parse(const char *list, bool *unknown);
void tfn_perms_text(uint32_t bits, char *out, int len);
int tfn_policy_ver(void);  // the permission policy now (0: the default of the build; the manager tab PERMISSIONS)
// a signed policy of the manager ("TFNP1\n" + 128 hex digits of the signature + "\n" + the text): kept when newer
bool tfn_policy_blob(const char *b, size_t n, char *msg, int len);
void tfn_policy_reset(void);            // (tests) the default policy of the build again
void tfn_policy_log(const char *name);  // the console "apps policy [<name>]"
// The developer mode (SETUP): app files without the signature of a trusted store can run.
bool tfn_dev_mode(void);
// INSTALL TO of STORE (the store key st_place: 0 = CARD, the default; 1 = BOARD): true = a new app goes to the card when a card is in and has room. The board STORE
// and the keeper of the manager (/api/apps "inst") obey it.
bool tfn_inst_place_card(void);
// the secret store of the app with this id (watch.h has the calls of the apps): the form of the board page (tfn_web.c)
int tfn_secret_get_for(const char *id, const char *key, char *out, int max);
bool tfn_secret_set_for(const char *id, const char *key, const char *value);
const tfn_entry_t *tfn_entry_by_id(const char *id);
void tfn_set_dev_mode(bool on);
// The menu group of an installed app (0 GAMES, 1 UTIL, 2 SETTINGS).
int tfn_group_of(const app_t *a);
// An icon.bin in memory (STORE: the icons of the catalog): the check, the draw of the frame now (false: a bad
// icon), and the plain tile with the first letter of the name.
bool tfn_icon_ok(const uint8_t *b, size_t len);
bool tfn_icon_draw(const uint8_t *b, size_t len, int x, int y, int s);
void tfn_icon_plain(const char *name, int x, int y, int s);
// The menus: with tfn_icon_ask(true), an app icon whose file is not in memory does not read the card in the draw: a low
// task reads it, and the icon draws a plain tile now. tfn_icon_missed() = an icon drew a plain tile for that reason
// since the last call (the call clears it, and frees the old icon files that the task read). tfn_icon_want(a) asks the
// task for the icon of a menu item before it comes into view (a read ahead of a scroll). The Tab5 (32 MB PSRAM) reads
// all icons at the start: there these do nothing.
void tfn_icon_ask(bool on);
bool tfn_icon_missed(void);
void tfn_icon_want(const app_t *a);
// The menu calls tfn_icon_moving() in each frame while its row moves (a finger or a glide): the icons show their
// first frame until 0.4 s after the last call, then they animate again (a calm slide, and less work in each frame).
void tfn_icon_moving(void);
// The decoded frames of the icons (a slot for each icon that shows; about 16 KB of PSRAM each) go: an app that is not
// the menu opens, or the icons of the STORE go.
void tfn_icon_cache_free(void);
// The state for /api/apps (JSON; returns the length).
int tfn_json(char *o, int max);
// two versions "1.0.40": < 0, 0, > 0 (a part that is not a number, as DEV-..., counts as the newest)
int tfn_ver_cmp(const char *a, const char *b);
// a package of this version is newer than the firmware (a built-in app gets an UPDATE from the store only then)
bool tfn_newer_than_fw(const char *ver);
