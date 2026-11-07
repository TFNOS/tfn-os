#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/app.h.
#include "tfn/app.h"

#include <stdbool.h>
#include <stdint.h>
#include "input.h"
#include "esp_timer.h"
#include <string.h>

// THE APP LIST of this build: main/apps.json is the ONE list of the apps (a new app = one entry there).
// main/apps_gen.py writes apps_gen.h and apps_list.inc into the build folder (build*/apps/; the host fuzz:
// tests/host/apps_gen/). apps_gen.h: ARCADE_APP_<ID> 1 for an app of this build, 0 for the others (ID = the sym
// without "app_", in capitals: ARCADE_APP_TOUCHLAB), and ARCADE_HAS_SIDEB / _BOOKS. ARCADE_APPS_LIST
// is the list file: one ARCADE_APP(ID, sym, group, is_new, x349, y349, xs3, ys3, test_ms) line for each app, in
// the menu order (app_menu.c ALL[] and NEW_APPS, main.c ALL_APPS and the self-test, tests/host/fuzz.c).
// A host test with only -I main has no apps_gen.h: then the list is main/apps_all.inc (ALL apps, also the
// apps that are off in the firmware) and ARCADE_APP_ON(ID) is true for each app.
#if __has_include("apps_gen.h")
#include "apps_gen.h"
#define ARCADE_APPS_LIST "apps_list.inc"
#else
#define ARCADE_APPS_GEN  0
#define ARCADE_APPS_LIST "apps_all.inc"
#endif
// For #if: true when the app ID is in this build (always true when there is no generated list).
#define ARCADE_APP_ON(id) (!ARCADE_APPS_GEN || ARCADE_APP_##id)

// SIDE B is out of the build (it needs 116 KB of flash). Its files stay: idf.py -D ARCADE_SIDEB=1 builds
// it. BOOKS (an EPUB reader) is not in this tree.
// The values below are only for a host test with no generated list.
#ifndef ARCADE_HAS_SIDEB
#define ARCADE_HAS_SIDEB 0
#endif
#ifndef ARCADE_HAS_BOOKS
#define ARCADE_HAS_BOOKS 0
#endif

// The apps of this build (the list of main/apps.json: see ARCADE_APPS_LIST above).
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) extern const app_t sym;
#include ARCADE_APPS_LIST
#undef ARCADE_APP

// The open of an app at one item (a notice: notify.c): the app reads the argument in its enter() with
// app_launch_arg() ("" = the app opens as it is; the argument stays until the next open).
void app_open_arg(const app_t *app, const char *arg);
// The app that is open now.
const app_t *app_current(void);
// A match with another board now (main.c: a game of 2 BOARDS, a turn game, the own link of an app with a board that
// answers): the screen stays on, the app runs under the panel and the notices, and a WALKIE call gets BUSY.
bool app_in_match(void);
// WALKIE for the web page (web_walkie.c): its state as JSON (returns the length), and a switch
// request (0 = I HEAR THEM, 1 = THEY HEAR ME), done in the next update of the app.
int walkie_web_state(char *out, int max);
void walkie_web_set(int which, bool on);
// The screen saver now (tests): off = true: the screen goes off at once, else it dims first.
void app_saver_now(bool off);
// the frames a second of the app that runs (measured each 300 frames; 0 = not yet)
float app_fps(void);
// the menu repaints the whole frame at the next frame (it repaints only what changes; main.c drew over it)
void menu_redraw_all(void);
// the startup app of the menu profile (START in /data/menu.txt), NULL = the menu
const app_t *menu_start_app(void);
int app_names(const char **out, int max);
// the apps that this board can run, in the menu order, with the menu group (0 GAMES, 1 UTIL, 2 SETTINGS)
int menu_list(const char **names, uint8_t *cats, int max);
// The menu tree (app_menu.c, menu_tree.c): the layout now as text (returns the length); a new layout (from the
// manager; "RESET" = the built-in layout; false and the reason in err when it is not a layout); the profile name
// ("" = the built-in layout) and the hash of the layout file (0 = the built-in layout).
int menu_layout_get(char *out, int max);
bool menu_layout_set(const char *text, int len, char *err, int emax);
void menu_home_drop(void);            // HOME 2.0: the own home of the board goes (a forced push of the manager)
bool menu_home_own(void);             // the board shows its own home (/data/home.txt), not the profile
bool menu_builtin(const char *name);  // TFN OS: the app is built into this firmware
int menu_builtin_names(const char **out, int max);  // the names of the built-in apps (the menu apps of this build)
const char *menu_profile(void);
uint32_t menu_profile_hash(void);
// The PWR key in the menu: up one level. Returns false at the top level.
bool menu_back(void);
// The app with this name (no case), or NULL (the console command "open").
const app_t *app_find(const char *name);
