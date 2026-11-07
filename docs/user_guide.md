# The user guide of TFN OS

This guide tells how to use a board with TFN OS: the first start, SETUP, the settings panel, the notices, FILES, the
STORE, the web page of the board, friends and the games for 2 boards, and the hotspot.

The names on the screen are in capitals in this guide (for example SETUP > WIFI). A "tap" is a short touch. A "hold"
is a touch that stays on the glass. The board kinds and their keys are in the [board guide](board_guide.md).

## 1. The first start

1. Install TFN OS on the board by USB (see "Build, flash and test" in the main `README.md`).
2. The board starts and shows the menu. The groups of the menu are GAMES, UTIL and SETTINGS.
3. The board has no WiFi network yet. So the hotspot starts for 2 minutes (section 9): a phone can open the web page
   of the board at once.
4. Open SETUP > WIFI and join your network (section 2.1). The board saves the network and joins it at each start.
5. Open SETUP > FRIENDS and give the board a name (section 8). The factory name is "TFN xxxx": xxxx is the end of the
   MAC address of the board, in hexadecimal.

The keys:
- **PWR** (or the strip at the left side of a system page): a short push goes back to the menu. On the boards with a
  power key, a hold of 2 s turns the board off.
- **BOOT** (the 3.49): some games use it. In the settings panel, a push of BOOT is RESUME.
- **The wheel** (the SenseCAP Watcher): a turn moves, a push selects, a hold opens the settings panel.

The safe mode: on the 3.49, hold BOOT while the board starts. Then no installed app loads until the next start. Use it
when an installed app stops the board at each start.

## 2. SETUP

SETUP (in the group SETTINGS) is a list of rows. Drag the list up or down to scroll. A short drag moves one row.

| Row | What it does |
| --- | --- |
| VOLUME, BRIGHTNESS | A drag on the row sets the value. 100 is the full volume of the codec; 101 to 125 is a boost |
| SOUND | All sound and music on or off |
| UI SOUNDS | The ticks and clicks of the menus |
| SOUND OUT, SOUND TO USB | Where the sound goes: the speaker, a computer by USB ("TFN SOUND"), or USB-C headphones (on the boards with a USB host) |
| AUTO ROTATE | The picture turns with the board |
| APP LANGUAGE | ENGLISH or HEBREW for the apps that have both |
| SCREEN SLEEP | The time before the screen goes off |
| POWER OFF | The time in standby on the battery before the board turns off |
| WAKE ON MOVE | A move of the board lights the screen, or only a touch or a key |
| SCREEN KEY | ON: BOOT turns the screen off and on, and PWR in the menu turns the board off |
| WIFI | The WiFi networks (section 2.1) |
| HOTSPOT | The own WiFi network of the board (section 9) |
| WEB PAGE | The address of the web page of the board, and QR CODE |
| WEB PASSWORD | The password of the web page (section 7.1) |
| FRIENDS | The name of the board and its friends (section 8) |
| POOL | The pool of the board: JOIN with the 6 digits, LEAVE, the members (section 8.3) |
| NOTIFICATIONS | The rules of the notices (section 4) |
| TILT TEST, TILT CALIBRATE, TILT SCALE | The check and the calibration of the motion sensor |
| TOUCH TEST | A test of the touch panel |
| DEVELOPER MODE | App packages with no signature can run (for the developers of apps). ON asks first: tap TURN ON with your finger |
| TEMPERATURE, FREE MEMORY | The temperature of the chip, and the free internal RAM and PSRAM |
| ABOUT | The version of the firmware |

Some rows are only on the boards that have the part (for example the USB headphones).

### 2.1 WIFI

1. Open SETUP > WIFI, then tap SCAN.
2. Tap a network. For an open network, the board asks "JOIN <name>?".
3. Type the password on the keyboard, then tap OK.

The board saves a network only when it gets an address. So a wrong password does not remove the network that you
saved before. The board keeps up to 10 networks, the last one in use first. FORGET removes the network in use.

The keyboard: a finger on a key lights it. After 0.2 s a big copy of the key shows above it. You can slide the finger
to another key. The key under the finger when you lift it is the key that the board types.

## 3. The settings panel

The settings panel is on top of each app.

- **To open it:** pull down from the middle of the top edge. A small white bar (the grip) shows there for 4 s when an
  app opens. On the Watcher, you can also hold the wheel button.
- **The controls:** VOLUME and BRIGHTNESS (drag on the row), MUTE, RESUME, and MENU. MENU goes back to the menu only
  after a still hold of 0.6 s, so that a thumb that misses a slider does not end the game. In the menu, this button is
  SETUP.
- **The items of the app:** many apps put their options into the panel as chips (for example the level or the sound
  of a game). The chip LANGUAGE changes the language of the app.
- **The chip NOTICES** opens the notice center (section 4).
- **To close it:** tap RESUME, tap the game under the panel, or swipe up on the panel.

While the panel shows, most apps stop, and their clock stops too. The apps that must go on (a timer, a music tool, a
call, a game with a second board) run under the panel.

## 4. The notices

A notice comes from the system (for example a new firmware or an update of an app), from an app with the permission
"notify", or from a manager.

- **The banner:** a notice drops from the top for 4 s. A tap on it opens the app at the item of the notice. A swipe up
  puts it away (the notice stays in the list).
- **The center:** the chip NOTICES of the settings panel shows all notices on one page. A tap opens a notice. A swipe
  of a row to the side removes it. CLEAR ALL removes all of them (tap again to agree).
- **The badge:** the icon of an app in the menu shows the count of its unread notices.
- **SETUP > NOTIFICATIONS:** ALL ON or OFF, DO NOT DISTURB, IN GAMES, the background checks of the apps (every 5, 10,
  15 or 30 minutes, or OFF), and for each app: ON, LIST ONLY or OFF, WAKE (light the screen) and SOUND.

There is no banner in DO NOT DISTURB, in a game (unless IN GAMES is on), or when the app of the notice is open. With the
screen off, the notice waits.

## 5. FILES

FILES (in the group UTIL) shows the files of the SD card (CARD, `/sdcard`) and of the flash of the board (BOARD,
`/data`).

- **The home page:** two tiles, CARD and BOARD, each with its size and its free space.
- **The list:** folders first, then files. A tap on the path line (or on UP) goes up one folder. A tap opens a folder,
  a text file, a JPEG picture, or an app package (a folder with `manifest.txt` and `app.elf`: INSTALL).
- **A hold on an entry:** OPEN, INFO, RENAME, DELETE, COPY TO BOARD or COPY TO CARD. DELETE asks a question first.
- **MORE:** NEW FOLDER, LOOK AGAIN, CARD TOOLS (the size of the card, FORMAT).
- **Locked:** the folder `apps` at the root of BOARD and CARD (the menu and the STORE manage it), and each name that
  starts with a dot. You can only open them and read their INFO.

A copy, a delete and an install are jobs with a progress bar and STOP.

## 6. The STORE

The STORE gets apps from a manager on your network. A board with no manager gets them from the static store: a
web site with a signed list of the apps and the app files (the default is the site of the public release,
`TFN_STATIC_STORE_URL` in `main/tfn_defaults.h`). A trusted store key must sign the list (section 6.2).

- **The tabs:** FOR YOU (the recommended apps that are not on the board), GAMES, UTIL, UPDATES (the installed apps that
  have a newer version), INSTALLED. A tap on the tab name, or a swipe up or down, goes to the next tab. A swipe left or
  right moves in the row.
- **The app page:** a tap on the app in the middle of the row opens its page: the description (in English or Hebrew),
  the size, the version and the permissions.
- **The buttons:** INSTALL TO BOARD, INSTALL TO CARD, UPDATE, OPEN, REMOVE.
- **The permissions:** before the install of an app that can do more than draw and play sounds, the STORE shows what
  it can do (for example "IT CAN USE THE PINS OF THE BOARD"). You must tap INSTALL. An update that adds a permission
  asks again.

To remove an installed app from the menu, hold its icon and choose REMOVE. Its saved scores and settings stay.

The board accepts an app only with the signature of the store key of its firmware, or in the developer mode. An app
for another board kind, or for a newer firmware, does not install.

### 6.1 With no manager

You can install an app package in these ways:
- copy the package folder to the SD card on a computer, then open it in FILES and tap INSTALL;
- the web page of the board, `http://<board address>/apps`;
- the SDK tool: `python sdk/tools/mk_app.py <app> --push <board address>` (see `sdk/README.md`).

### 6.2 The source of the STORE

SETUP > STORE SOURCE shows the source now (for example "AUTO, NOW FROM THE MANAGER"). A tap on its button goes to
the next source: AUTO, MANAGER, STATIC. The address of the static store does not change. In a protected build, it
needs the admin access.

The console of the board (USB) has the same setting and the address:
- `apps src` shows the source: AUTO (the default: the manager when the board knows one, else the static store), MGR
  (the manager only) or STATIC (the static store only), and the address of the static store.
- `apps src static https://<host>/<folder>/` sets the static store of another site (HTTPS only). `apps src auto`
  sets the default source and the default address.

The board reads `index.json` and `index.sig` of the static store (`sdk/tools/store_index.py` makes them). It uses the
list only when a store key of the board signed it: a built-in key of the firmware, or a key with the flag STORE in
SETUP > TRUSTED PUBLISHERS. Then it checks the size and the SHA-256 of each downloaded file, and the install checks
the signature of the app as for the manager.

## 7. The web page of the board

When the board is on your WiFi network, open `http://<board address>/` in a browser on the same network. SETUP > WEB
PAGE shows the address and a QR code.

| Page | What it does |
| --- | --- |
| `/` | The exports of the music apps (play, WAV, MIDI, MP3, delete) and the upload of MIDI songs |
| `/apps` | The installed apps: install a package, remove an app, the developer mode |
| `/files` | The files of the board and of the SD card |
| `/pad` | The profiles and the Bluetooth name of the gamepad |
| `/walkie` | WALKIE from the browser |
| `/login`, `/logout` | The web password (section 7.1) |

The extras add more pages when they are in the firmware (for example `/ha` for Home Assistant and `/cam` for the web
camera).

### 7.1 The web password

The web page has no password by default: each device on your WiFi network can open it and change the board. Set a
password:

1. Open SETUP > WEB PASSWORD.
2. Type a password of 4 to 63 characters, then tap OK. An empty text removes the password.

With a password, each page and each API call needs it. A browser goes to `/login` (a session cookie). A script sends
HTTP Basic with an empty user name (`curl -u :<password> ...`). After 5 wrong passwords from one address, the board
waits 30 s, and the wait doubles at each next wrong password (15 minutes at most).

If you forget the password, use the USB console: `webpass off` (or `webpass set <password>`).

## 8. Friends and the games for 2 boards

### 8.1 Friends

Two boards learn each other one time. Then the games for 2 boards and WALKIE pair at once when a friend is near.

1. Open SETUP > FRIENDS on both boards.
2. Tap LEARN A FRIEND on both boards. Each board adds the other one.

Or: in the lobby of a game, a board that is not a friend shows an ADD badge. Tap it on both boards.

SETUP > FRIENDS also has CHANGE NAME (the name that the other boards show) and the list of the friends (up to 16). A
tap on a friend removes it (the board asks first).

### 8.2 The games for 2 boards

Many games have a mode 2 BOARDS (for example PONG, FIGHT, TENNIS, CHESS, SEA BATTLE).

- **Near you:** the boards use ESP-NOW (radio packets between the boards). Both boards must be on the same WiFi
  channel: connect both boards to the same WiFi network. A board with no WiFi network looks on each channel.
- **On the same WiFi network:** the boards find each other with UDP beacons, also out of the radio range.
- **The lobby:** each board sends a HELLO. The faster board is the HOST: it runs the game. The other board sends its
  inputs and shows the state of the HOST.
- **An invite:** when you wait in a lobby, a friend on the same network who has not got the game open gets the notice
  "<name> WANTS TO PLAY WITH YOU". A tap on it opens the game.
- **The end:** the game ends when an app closes, or after 3 s with no packet from the other board.

### 8.3 Pools

A pool is a group of boards that trust each other (for example the boards of one home). The boards of a pool find
each other and play; a board of another pool cannot see them. SETUP > POOL shows the pool, its members and the role
of the board.

1. Tap JOIN. The board waits for a manager for 10 minutes.
2. The manager adds the board. The board and the manager show the same 6 digits.
3. Tap ALLOW only when the digits are the same. Else tap NO.

LEAVE takes the board out of its pool. The details: [pools.md](pools.md).

### 8.4 A pool with a computer

The pool manager `poolmgr` keeps the pool of your boards from one computer (Windows, macOS, Linux or a Raspberry Pi)
with Python 3. It makes the pool and its keys, adopts each board with the 6 digits, makes the members friends, updates
their firmware one board at a time, gives them a store on your network, and keeps an encrypted backup. Get it as
`tfn-poolmgr-<version>.zip` of a release (or the folder `poolmgr/` of the repo). The start:

1. On the computer: `python poolmgr/poolmgr.py init "OUR HOME"`.
2. On each board: SETUP > POOL > JOIN. A board with a web password keeps it: `poolmgr` asks for it at the JOIN. A
   board with no web password gets a random one (`poolmgr webpass show <board>` prints it).
3. On the computer: `python poolmgr/poolmgr.py adopt --all`. Compare the 6 digits, then tap ALLOW on each board.

The full guide: [poolmgr.md](poolmgr.md).

## 9. The hotspot

With no WiFi network, the board can make its own open WiFi network. Its name is "TFN-xxxx" (xxxx is the end of the MAC
address of the board). Join it with a phone, then open `http://192.168.4.1/`.

SETUP > HOTSPOT has three modes:
- **AUTO** (the default): the hotspot starts when no WiFi network is saved, or when the connection fails, one time
  after each start, for 2 minutes. A phone on it keeps it on. It stops 2 minutes after the last phone leaves, or when
  the WiFi connects.
- **OFF:** only the button START starts it (until the next start).
- **ON:** the hotspot is always on, also with a WiFi connection.

SETUP > WEB PAGE > QR CODE shows two codes for the camera of a phone: one joins the hotspot, one opens the page.

The hotspot has no password now: each person near the board can open the web page while the hotspot is on. Set a web
password ([security.md](security.md)).

## 10. Bluetooth and USB

- **GAMEPAD** makes the board a Bluetooth gamepad for a computer or a phone. Its Bluetooth name is "TFN PAD" (change
  it on the page `/pad` or in BT KEYS > DEVICE NAME).
- **BLE MIDI:** the MIDI apps show the board as "TFN MIDI" on Bluetooth.
- **USB:** on a computer, the board can be a USB sound input ("TFN SOUND", SETUP > SOUND TO USB) or a USB MIDI device
  ("TFN MIDI").

## 11. The console

The USB serial port of the board has a console (115200 baud). Some useful commands:

```
wifi <name> <password>     connect to a network and save it
wifi list                  the names of the saved networks (never the passwords)
hotspot on | off | auto    the mode of the hotspot
ip                         the address of the board
webpass set <password>     set the web password (webpass off: remove it)
mem                        the free memory
apps                       the installed apps
```
