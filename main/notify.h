#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/notify.h.
#include "tfn/notify.h"

#include <stdbool.h>
#include <stdint.h>
#include "app.h"
#include "input.h"

#define NT_MAX    40  // the notices that the board keeps (a new one pushes out the oldest)
#define NT_NAME   20  // the menu name of the app
#define NT_TITLE  72
#define NT_TEXT   200
#define NT_LINK   64
#define NT_ACT    20
#define NT_NACT   3         // the buttons of a notice
#define NT_SYSTEM "SYSTEM"  // the app name of the notices of the firmware and the manager

typedef struct {
    uint32_t id;           // > 0, one number for each notice of this board
    uint32_t t;            // the time of the notice (epoch seconds; 0 = the board did not know the time)
    char app[NT_NAME];     // the menu name of the app that it opens (NT_SYSTEM: the notice opens the center)
    char title[NT_TITLE];  // UTF-8 (English or Hebrew)
    char text[NT_TEXT];
    char link[NT_LINK];         // the argument for the app (app_launch_arg()); "" = the app opens as it is
    char act[NT_NACT][NT_ACT];  // the buttons ("" = none)
    char cb[NT_LINK];           // the manager reads the answer of a button with this key ("" = no answer)
    uint8_t read;               // 1 = seen (no badge, no banner)
} notice_t;

void notify_init(void);  // main.c, after the storage and the settings: the notices of the file

// A new notice (any task). Returns its id, or 0 when the notices of this app are off (SETUP > NOTIFICATIONS), all
// notices are off, or the app sent more than NT_RATE notices in the last hour.
uint32_t notify_post(const char *app, const char *title, const char *text, const char *link);
// The same with up to three buttons (act: NULL or "" = none) and the key of the answer for the manager.
uint32_t notify_post_ex(const char *app, const char *title, const char *text, const char *link,
                        const char *const act[NT_NACT], const char *cb);

int notify_count(void);                 // the notices that the board keeps
int notify_unread(const char *app);     // the unread notices of an app (NULL: of all apps): the badge
bool notify_get(int i, notice_t *out);  // a copy of the notice i (0 = the newest)
void notify_remove(uint32_t id);
void notify_clear(void);
void notify_seen_app(const char *app);  // the app opened: its notices are read (main.c)
// The answers of the buttons for the manager (web_sys.c, GET /api/notify): JSON; clear = they go after this read.
int notify_json(char *out, int max, bool clear_answers);

// The settings of an app (the store key "na_<crc32 of the name>"); and the switches of all notices.
enum {
    NM_ON,
    NM_LIST,
    NM_OFF
};  // ON = banner, list, badge; LIST = list and badge; OFF = nothing
int notify_mode(const char *app);
void notify_set_mode(const char *app, int m);
bool notify_wake(const char *app);
void notify_set_wake(const char *app, bool on);
bool notify_sound(const char *app);
void notify_set_sound(const char *app, bool on);
bool notify_all_on(void);
void notify_set_all(bool on);
int notify_dnd(void);  // 0 = 23:00-07:00 (the default), 1 = 22:00-08:00, 2 = OFF
void notify_set_dnd(int v);
bool notify_in_games(void);  // true = a banner also in a game
void notify_set_in_games(bool on);
int notify_apps(char names[][NT_NAME], int max);  // the apps that sent notices (the SETUP page)

// main.c: the app task.
void notify_tick(int64_t now);               // the banner, the sound, the file
bool notify_input(const input_event_t *ev);  // true = the banner or the center took the touch
void notify_draw(int64_t now);               // the banner over the frame of the app, or the whole center
bool notify_center_on(void);                 // the center shows (the app stops under it)
void notify_center_open(void);
void notify_center_close(void);
bool notify_banner_on(void);  // a banner shows (no pull of the panel on it)
bool notify_game_now(void);   // a game shows now (the background rounds wait: watch.h)
