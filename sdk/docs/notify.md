# Notifications, background checks and secrets (TFN OS, ABI rev 5)

This guide is for the developer of an app file. It tells how an app sends a notification, how an app checks the
network in the background, and how it keeps a token or a password. The board system is in `main/notify.h` and
`main/watch.h`. The API reference is `api.md`.

## 1. Send a notification

```c
#include "tfn/notify.h"
uint32_t id = tfn_notify("Rain at 17:20", "Light rain for about 40 minutes", "rain");
```

- The app needs the permission class `notify`. `mk_app.py` writes it into the manifest when the app calls
  `tfn_notify`. The STORE shows "SEND YOU NOTIFICATIONS" before the install.
- The notice gets the name of the app that runs now. (A watcher, section 3: the name of its app.)
- The result is 0 when the user turned the notices of the app off (SETUP > NOTIFICATIONS), or when the app sent
  10 notices in the last hour. Do not send a notice for each small change: send one for each new item.
- The text is UTF-8, English or Hebrew. The title has 71 bytes at most, the text 199, the link 63.

What the user sees:

- **The banner** drops from the top for 4 s. No banner in a game (a setting), in DO NOT DISTURB, or when the app of
  the notice is open (then the app shows the item itself).
- **The center** (the chip NOTICES of the settings panel) keeps the last 40 notices.
- **The badge** on the icon of the app counts its unread notices. It goes when the app opens.

## 2. Open the app at the item of a notice

A tap on a notice opens its app with the link as an argument. The app reads it in `enter()`:

```c
static void enter(int64_t now)
{
    const char *arg = app_launch_arg();        // "" = an open from the menu
    if (!strncmp(arg, "chan=", 5)) open_channel(arg + 5);
}
```

The argument stays until the next open of an app. Keep the link short: an id, not the content.

## 3. Check the network in the background (a watcher)

A watcher is a second, small part of the app: `watch.elf`. The board keeps it loaded, also while other apps run,
and calls its `check()` in the background rounds. A round wakes the radio and the CPU one time for all the watchers
that are due (SETUP > NOTIFICATIONS > BACKGROUND CHECKS: every 5, 10, 15 or 30 minutes, or OFF).

```c
// watch_mine.c
#include "tfn/notify.h"
#include "tfn/settings.h"
#include "tfn/watch.h"

static void check(void)
{
    // read the network (libs/tfnet), compare with the last item (store_get / store_set with the prefix of the app),
    // and send one notice for each new item
    tfn_notify("A new message", "From Dana: dinner on Friday?", "msg=812");
}

const watch_t watch_mine = {15, NULL, check};    // every_min, init, check
```

Build it with the app:

- An app of `main/apps.json`: `"watch": {"srcs": ["watch_mine.c"], "sym": "watch_mine", "every": 15}`.
- An app out of `main/`: `mk_app.py app.c --sym app_mine --name MINE --watch watch_mine.c --watch-sym watch_mine
  --watch-every 15`.

`mk_app.py` builds `watch.elf` with its own entry (`sdk/tools/host/watch_entry.c`), adds the class `background`
("CHECKS THE NETWORK IN THE BACKGROUND") and the lines `watch=`, `watch_bytes=`, `watch_sha256=` to the manifest.

The rules of a watcher:

| Rule | Why |
|---|---|
| No names of the screen, the sound or the touch (`gfx_`, `ui_`, `sfx_`, `audio_`, `input_`, `menu_`, `sysui_`, `kbd_`, `imu_` and others) | It runs while another app draws and plays. `mk_app.py` and the loader refuse them. |
| About 8 s for a check at most. 20 s is a fault, and 3 faults in a row turn the watcher off. | All watchers share one task. |
| No task of its own | The watch task runs all checks, one after the other. |
| A crash in a check turns the watcher off at the next start | The board must not crash again and again. A notice tells the user. A new version of the app turns it on again. |
| No round during a 2-board link or a game (up to 15 minutes later) | A TLS handshake takes 0.5 to 1 s of CPU. |

The app and its watcher share the saved values (`store_get` / `store_set` with the saves prefix of the app), the
files of the app and the secret store. They do not share memory: they are two loaded files.

## 4. Keep a token or a password (the secret store)

```c
#include "tfn/watch.h"
tfn_secret_set("token", tok);               // the app, after the login
char tok[200];
if (tfn_secret_get("token", tok, sizeof tok) > 0) { ... }    // the app or its watcher
tfn_secret_set("token", NULL);              // the log out
```

- The class `secret` ("KEEPS A SECRET KEY ON THE BOARD").
- Each app reads only its own values. The console `storeall` does not show them.
- 400 bytes a value at most.
- The board has no flash encryption: a person with the board and a USB cable can read the flash. Keep only what the
  user can revoke (an app password, a token), and tell the user where to revoke it.

## 5. Test on a board

- `notify <app>|<title>|<text>[|<link>|<button 1>|<button 2>|<button 3>]` on the USB console posts a notice;
  `notify list` lists them; `notify clear` removes them.
- `watch` lists the loaded watchers (the time of the last check, its length, the faults); `watch now` runs a round
  now with all of them.
- `sdk/examples/watchdemo` (WATCH DEMO) is an app with a watcher and a secret.
