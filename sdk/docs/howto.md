# How to make a game or a utility for TFN OS

This guide tells a new developer how to make each kind of app of this repo, from the first file to the store: a game
of each graphics class (A to I) and each kind of utility. Each part names the app to copy, the files, the tools, the
steps, the data formats, the checks and the faults that we found on the boards.

Read these first. This guide does not repeat them:

| Document | Read it for |
| --- | --- |
| [../README.md](../README.md) | The first app in 10 minutes (the template HELLO, `mk_app.py`, the developer mode). |
| [behave.md](behave.md) | The `app_t`, the order of the calls, the input, the saves, the sound, the screen saver, the panel. |
| [visual.md](visual.md) | The screens of each board kind, the frame, the fonts, the touch targets, the icon. |
| [graphics.md](graphics.md) | The 9 graphics classes, the games of each class, the costs on the board, the rules of the look. |
| [checklist.md](checklist.md) | The checks before an app goes to the store. |
| [api.md](api.md) | Each firmware name that an app file can call (the ABI). |
| [tools.md](tools.md) | `mk_app.py`, the host tools, the board console, the board API. |
| `MANUAL.md` (the repo root) | The section of each app: how it works, its tools and its tests. |

The local paths, the commands of your PC (ESP-IDF 6.1, Blender, Edge, FluidSynth) and your private work folders
go into `LOCAL.md` at the repo root. Git ignores that file: each developer keeps an own copy.

## Contents

1. [The parts of every app](#1-the-parts-of-every-app)
2. [The work loop: build, install, test](#2-the-work-loop-build-install-test)
3. [Games: a recipe for each graphics class](#3-games-a-recipe-for-each-graphics-class)
4. [Utilities: a recipe for each kind](#4-utilities-a-recipe-for-each-kind)
5. [Sound and music](#5-sound-and-music)
6. [The data pack on the SD card](#6-the-data-pack-on-the-sd-card)
7. [Speed and memory on the board](#7-speed-and-memory-on-the-board)
8. [The tests and the QA](#8-the-tests-and-the-qa)
9. [To the store](#9-to-the-store)
10. [Faults that we found (do not do them again)](#10-faults-that-we-found-do-not-do-them-again)

---

## 1. The parts of every app

### 1.1 The folder

A new app is a folder `apps/<id>/` and one entry in `main/apps.json`. The id has only `a-z`, `0-9` and `_`.

| File | What it has |
| --- | --- |
| `app_<id>.c` | The `const app_t app_<id>`, `enter`, `leave`, `input`, `update`, `render`, `draw_icon`. |
| `<pfx>_*.inc` | The parts of a big app (the rules, the drawing, the UI, the data). `app_<id>.c` includes them. Use a short prefix of 2 letters (`a3_`, `tw_`, `wx_`). |
| `<pfx>_data.c`, `<pfx>_scene.c` | Data that a tool MADE (pictures, tables, meshes). Its first line says which tool made it and "do not edit". |
| `<pfx>_sfx.c` / `.h` | The MP3 effects in the app file (made by `libs/tfsfx/mk_sfx_c.py`, section 5). |
| `sfx/` | The MP3 effect files and `SOURCES.md` (the source and the CC0 licence of each). |
| `tools/` | The tools that make the data of the app again (art, music, levels, tables). A `README.md` gives the order. |
| `store.json` | The store page in English and Hebrew ([store.md](store.md)). No app goes into the store without it. |

Shared code goes into `libs/<module>/`, not into the folder of one app. `main/` is TFN OS only.

An app includes only the PUBLIC headers of TFN OS: `#include "tfn/gfx.h"` (the folder `main/tfn/`). They have the
names of the app API (the ABI names, their types and their macros). `main/<name>.h` is the OS view of a header: it adds
the names that only the OS uses, and an app must not include it. An app also includes no `.c` or `.inc` file of
`main/`: it calls the ABI names. CI checks it (`tests/style/check_api.py`). If an app needs a name that is not public,
ask for an ABI name ([raw.md](raw.md)).

### 1.2 The entry in `main/apps.json`

```json
{"name": "MY GAME", "sym": "app_mygame", "group": "GAMES", "on": false, "new": true,
 "srcs": ["app_mygame.c", "mg_sfx.c"], "uses": ["tfsfx"], "psram": ["app_mygame.c"],
 "test349": [320, 86], "tests3": [160, 120], "emoji": "🎯"}
```

- `on`: `true` puts the app into the firmware. A new app is almost always `false`: it comes to the boards as an app
  file from the store (or from the SD card in a test). The firmware flash is nearly full.
- `new`: `true` puts the app at the front of its group with the NEW pill. New games and games in progress go
  first. Keep it until the game is done.
- `srcs`: the own files of the app (the `.c` files only; the `.inc` files come in by `#include`).
- `uses`: the shared modules of `libs/` (the `modules` part of the file lists them). The firmware build and the host
  tests compile them, but `mk_app.py` does NOT compile them into an app file. A module whose names are in the ABI
  (`midi_bg`, `duo`, `instr`, `drumkit`, `tfsfx`, `tfphys`, `tfnet`, `tfmqtt`, `zimread`, `zstd`) needs nothing more:
  include its `.h` file in the SDK form (`#include "midi_bg/midi_bg.h"`, not `"../../libs/midi_bg/midi_bg.h"` and
  not `"midi_bg.h"`: CI checks it). A lib whose names are not in the ABI (for example `kscroll`) is a header or a source that the app
  compiles itself, only for the app file build:

  ```c
  // mg_libs.c: the copies of the modules in the app file (in the firmware this file is empty)
  #if defined(ESP_PLATFORM) && defined(__PIC__)
  #include "tfnet/tfnet.c"
  #endif
  typedef int mg_libs_t;                  // (a C file must have one declaration)
  ```

  Check a module in [api.md](api.md): when its functions are not there, include it. `mk_app.py` stops at the ABI check
  when you forget it.
- `needs`: other apps whose code it links.
- `psram`: the sources with static state. Their `.bss` goes to PSRAM. Each source with big static arrays must be here.
- `only`: the board kinds of the app (for example `["lcd349"]` for an app that needs the bar screen or the SD card).
- `test349`, `tests3`: the tap point of the self-test (logical) on the 3.49 and the 320 x 240 boards.
- `emoji`: the icon of the app on the manager page.

Then run `python main/apps_gen.py --check`. MANUAL.md "The app list (apps.json)" has all the fields.

### 1.3 The skeleton of a game

This skeleton has the parts that every game of the store needs: the two layouts, a title, the play, the end, the
panel items, the language, the saves, a sound and the icon. Copy it and change it.

```c
// MY GAME (GAMES, <date>): one line that tells the game, and the words of the request for it.
#include <stdio.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"

#if BOARD_BAR_SCREEN                    // the 3.49: 640 x 172
#define TITLE_Y 70
#define BTN_W   200
#else                                   // the 320 x 240 frame: CoreS3, AMOLED (x1.4), Watcher (zoom or round_fit)
#define TITLE_Y 90
#define BTN_W   160
#endif
#define BTN_H 56                        // 44 px and 5.6 mm is the aim for a touch target (visual.md section 5)

enum { ST_TITLE, ST_PLAY, ST_END };
static int s_state;
static int s_score;
static uint32_t s_best;
static bool s_he;                       // Hebrew
static int64_t s_t0, s_last;

static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
enum { MI_LANG, MI_RESTART };
static const pitem_t ITEMS[] = {
    {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, LANG_VALS, "mygame_lang"},
    {PI_ACT2, MI_RESTART, 0, 0, {"RESTART", "מחדש"}, NULL, NULL},
};
static void start(int64_t now);
static void item_set(int id, int v)
{
    if (id == MI_LANG) s_he = v == 1;
    if (id == MI_RESTART) s_state = ST_TITLE;
}
static const pitems_t PANEL = {ITEMS, 2, NULL, NULL, item_set, NULL, NULL};

static void start(int64_t now)
{
    s_state = ST_PLAY;
    s_score = 0;
    s_t0 = now;
}

static void enter(int64_t now)
{
    s_he = store_get("mygame_lang", 0) == 1;
    s_best = store_get("mygame_best", 0);
    s_state = ST_TITLE;
    s_last = now;
    sysui_panel_items(&PANEL);
}

static void leave(void)
{
    if ((uint32_t)s_score > s_best) store_set("mygame_best", (uint32_t)s_score);
}

static void input(const input_event_t *ev)
{
    if (s_state == ST_TITLE && ev->kind == IN_TAP) {      // a precise target: act on IN_TAP
        start(ev->t_ms);
    } else if (s_state == ST_PLAY && ev->kind == IN_DOWN) { // a game action: act at once
        s_score++;
        sfx_play(SFX_CLICK);
    } else if (s_state == ST_END && ev->kind == IN_TAP) {
        s_state = ST_TITLE;
    }
}

static void update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.05f) dt = 0.05f;                           // a slow frame must not make a big jump
    (void)dt;
    if (s_state == ST_PLAY && now - s_t0 > 20000) {
        s_state = ST_END;
        if ((uint32_t)s_score > s_best) {
            s_best = (uint32_t)s_score;
            store_set("mygame_best", s_best);             // a save at a change, not in each frame
        }
    }
}

static void render(int64_t now)
{
    gfx_fill(rgb(16, 18, 34));                            // the WHOLE frame, each time
    char line[40];
    if (s_state == ST_TITLE) {
        ui_text_c(&KF_TITLE, gfx_w / 2, TITLE_Y, s_he ? "המשחק שלי" : "MY GAME", rgb(255, 230, 160));
        ui_rrect(gfx_w / 2 - BTN_W / 2, gfx_h - BTN_H - 12, BTN_W, BTN_H, 18, rgb(60, 140, 90));
        ui_text_c(&KF_BIG, gfx_w / 2, gfx_h - 28, s_he ? "התחל" : "START", rgb(255, 255, 255));
    } else {
        snprintf(line, sizeof line, "%d", s_score);
        mf_text_c(&MF_N34, gfx_w / 2, gfx_h / 2 + 14, line, rgb(255, 255, 255), 255, 1);
    }
    (void)now;
}

static void icon(int x, int y, int s)
{
    gfx_fill_rect(x, y, s, s, rgb(30, 40, 90));           // paint each pixel of the box: no holes
    gfx_fill_circle(x + s / 2, y + s / 2, s / 3 + (int)(s / 20 * fsin(icon_time() * 3)), rgb(255, 190, 70));
}

const app_t app_mygame = {
    .name = "MY GAME",
    .landscape = true,
    .enter = enter,
    .leave = leave,
    .input = input,
    .update = update,
    .render = render,
    .draw_icon = icon,
    .round_fit = true,                  // the Watcher: the whole 320 x 240 frame in the circle
};
```

The rules in this skeleton:
- Each store key starts with `<id>_` and has 15 characters at most (`mygame_best`).
- The language key ends with `_lang`: then SETUP > APP LANGUAGE (the language of all apps) gives its value until the
  user sets the language of the app in its panel.
- The options (LANGUAGE, TILT, ZOOM, RESTART, QUIT) go only into the panel. A loss action is `PI_ACT2` (a hold of 1 s).
- Big numbers use an anti-aliased face (`MF_N*`, `KF_*` or your own atlas). Never the dot font of `gfx_text` for big
  text: it looks rough, like the drawing of a child.
- Hebrew text needs the `KF_*` faces and `ui_text_c()` (a Hebrew line goes from the right). The `MF_*` faces have
  no Hebrew letters.
- A game has its own opening tune (`libs/jingle/jingle.h`), not the start tune of the firmware.

### 1.4 The board kinds and the layouts

| Kind (`--kinds`) | Board | The frame of the app | Notes |
| --- | --- | --- | --- |
| `lcd349` | Waveshare ESP32-S3-Touch-LCD-3.49 | 640 x 172 (`BOARD_BAR_SCREEN` 1) | The main board. Has an SD card slot. |
| `cores3` | M5Stack CoreS3 SE | 320 x 240 | Quad PSRAM: slower. A CoreS3 can have no SD card. |
| `am18` | Waveshare AMOLED 1.8 | 320 x 240 x 1.4, or 368 x 448 native (`.native`) | The frame is scaled each frame. |
| `watcher` | SenseCAP Watcher | 412 x 412 round (`.round`), or the 320 x 240 frame (zoom or `.round_fit`) | No motion sensor. A wheel. |
| `tab5` | M5Stack Tab5 (ESP32-P4, RISC-V) | 640 x 360 (or 360 x 640), scaled x2 to the 720 x 1280 panel | A DEV kind; no release kind now. No BOOT key, no native radio. Not tested on a board. |

- Write the layout of each kind with `#if BOARD_BAR_SCREEN` / `#elif BOARD_ROUND` / `#elif CONFIG_TFN_TARGET_TAB5` /
  `#else` (visual.md section 1). With no Tab5 branch, the Tab5 uses the `#else` layout in its bigger frame.
- Use `gfx_w` and `gfx_h` for the size of the frame. Do not put 640 or 320 into the code where a size of the frame
  is meant.
- An app that needs the bar screen sets `.needs = APP_NEEDS_BAR` and `"only": ["lcd349"]`. An app that needs the motion
  sensor sets `APP_NEEDS_IMU`.
- Check each board in its natural position: the 3.49 as a long bar, the CoreS3 as a landscape box.

---

## 2. The work loop: build, install, test

### 2.1 One time

1. Build a DEV firmware of each kind that you make the app for: `.\dev.ps1 -Kinds lcd349,cores3` (PowerShell, from
   `tfn-os/`). `mk_app.py` takes the compile options from `build_dev/<kind>/compile_commands.json`.
2. Build the host icon tool: `sh sdk/tools/host/build.sh lcd349 cores3`. Build it again after a new app in
   `apps.json` or a change of `main/`: else `mk_app.py` stops at the icon ("No app ..."). (The `am18` kind of this tool
   needs a private include folder that is not in the repo. The Tab5 uses the icon of `cores3`.)
3. Put your board in the developer mode: SETUP > DEVELOPER MODE on the board, then TURN ON with your finger, or the
   USB console `apps dev 1`. (The board page `http://<board>/apps` can only turn it off.)
4. A board with a web password: give `--password <pw>` to `mk_app.py`, or set the environment variable
   `TFN_BOARD_PASSWORD`.

### 2.2 Each change

```
python sdk/tools/mk_app.py MYGAME --kinds lcd349 --nosign --push <board ip> --open          (to /data of the board)
python sdk/tools/mk_app.py MYGAME --kinds lcd349 --nosign --push <board ip> --card --open   (to the SD card)
```

- The board closes the open app, installs the new file and opens it (the hot reload).
- `/data` of a board can be full. Then use `--card`.
- Read the board log (the USB console, 115200 baud, DTR and RTS low): `loaded <id> ...: <n> bytes in <ms> ms, PSRAM
  <n>, internal RAM <n>`, the fps line each 300 frames, and `closed <id>, PSRAM free <n>`.
- After each install, check that the log has the text of the NEW build. A task of an old copy that did not end keeps
  the old code in memory until a reset.

### 2.3 The PC first

The PC finds most faults in seconds. A board run takes minutes.

- The all-app fuzz: `sh tests/host/build.sh`, then `./fuzz349.exe 3000 <seed>`, `./fuzzs3.exe 3000 <seed>`,
  `./fuzzwt.exe 3000 <seed>` (ASan + UBSan; random touches, tilts and turns; a frame that is not fully painted
  fails). It builds every app of `apps.json`, also the apps that are off.
- The simulator: `python simulator/run.py` (the real firmware sources in a window), `python simulator/smoke.py`
  (each app as a user drives it). See `simulator/README.md`.
- An own test of the game: a small C program that includes the app source with `tests/host/stubs.c` and plays it with
  real touches (a "bot"). The big games each have one (`play.c`, `run.py bot`). It must play the whole game, through
  the touch, with ASan and UBSan. A time cheat in a bot hides faults: a bot taps where a person can tap.
- Pictures of each screen: `tshot_<b>.exe "<APP NAME>" "<steps>" <prefix>` (tools.md "tshot"; a private tool,
  not in the repo), or the simulator and the board (`shot`). Look at each picture yourself.

### 2.4 The board

- Tests on a board are silent: the console `quiet 1` first, `quiet 0` at the end (also after a fault).
- `wake` before each `shot` (with the screen off, no new frame comes).
- A picture of the screen: the console `shot`, or `GET /api/screen` (RGB565, high byte first).
- A virtual finger through the real touch path: `finger x y ms`, `fmove x y`, `finger up`.
- The memory: the console `mem` at the menu, before and after 4 opens and closes of the app. The numbers must be the
  same (no leak).

---

## 3. Games: a recipe for each graphics class

[graphics.md](graphics.md) tells what each class is and which games use it. Here are the steps. First choose the
class with graphics.md section 5. For a new look, make a look test first: pictures at the real screen size, or a web
page for the reviewer. The reviewer decides on the look before the full game.

### 3.1 Class A: code-drawn 2D

**Copy:** `sdk/examples/tapper` (small), `apps/tiles`, `apps/splat` (a full arcade game), `apps/murano` (a rich
look from code).

**Steps:**
1. Write the rules and the drawing in C. Draw with `gfx.h` (rectangles, circles, rings), `libs/tfdraw` (soft
   discs, thick lines, round boxes with alpha, gradients, glows, polygons), `uikit.h` (round boxes, soft circles).
2. Smooth motion: positions in `float`, the time step from `now`, an ease for each move.
3. A "cinematic" camera for an action game: a zoom and a pan of the world, a shake on a hit, a short hit stop. The
   player stays in the frame and near the middle.
4. Use `fsin()` / `fcos()` of `gfx.h`. `sinf()` takes about 12 us in an app file.

**Data:** none. **Speed:** easy (40 - 80 fps on the 3.49).

**Faults to avoid:** a flat look is not enough for a big game ("not the same"). Give the look depth: gradients,
glows, soft shadows, a light that changes.

### 3.2 Class B: cartoon puppets (`libs/toon`)

**Copy:** `apps/fight`.

`libs/toon` draws puppets (a big head, noodle arms, gloves, squash and stretch) into a low-resolution buffer, with
a stage and a VHS filter. A new puppet is a set of shapes and a pose table in C. Use it for a cartoon fighter or a
cartoon cast. The camera check `cam_check.c` (FIGHT) proves that the zoom never hides the action.

### 3.3 Class C: code-drawn quest scenes

**Copy:** `apps/side_b` (the best quest look), `apps/room`, `apps/keeper`.

**Steps:**
1. The story and the puzzles first, as a table: the places, the things, the steps (section 3.10).
2. Each place is a list of shapes in `<pfx>_art.inc`: polygons, gradients, ink lines, light.
3. The still layer of a place goes once into a strip cache. Each frame draws the people, the talk close-ups and the
   UI over it.
4. The talk close-ups are IN the place (the background behind the people), not on a plain card.
5. Give each quest its own look: three quests with the same graphics failed the review.

**Faults to avoid:** labels on labels, labels on buttons, text that does not fit; a zoom that shows more than the
place. Make an automatic check of the labels and the text for each place (KEEPER had `label_check.c`, `text_check.c`).

### 3.4 Class D: offline art generators (pixel art, canvas pictures)

**Copy:** `apps/gummy_siege` (canvas pictures by headless Edge), `apps/cruise` (pixel art; its art pipeline
is a private tool, not in the repo).

**Steps (the canvas way of GUMMY SIEGE):**
1. Draw each picture in JavaScript on a canvas (`tools/art/bake.js`) at 3 times the size, then make it smaller (smooth
   edges).
2. `tools/art/bake.py` runs headless Edge on `bake.js`, gets each canvas as a PNG and packs them into `art.bin`.
3. `tools/mk_data.py` writes `gs_data.c` (the pictures in the app file) from `art.bin`, the levels and the effects.

The format of `art.bin` (GSA1): `"GSA1"`, the count (u16), then for each picture the name (16 bytes), the kind (u8:
0 RGB, 1 RGBA, 2 P8 = a palette), 0, the width and the height (u16 each), the length of the deflated data (u32), the
zlib data. On the board: `tinfl_decompress()` (in the ABI) into a PSRAM buffer at the start.

**The pixel-art way of CRUISE:** Python draws each sprite at its real pixel size (`sprites.py`); `export_all.py`
writes C arrays. This look passed the review. Each new pose or place is new art.

**Faults to avoid:** low effort art. Scenes with no dense detail and clones of one scene fail the review. Use full
resolution, an own angle for each scene, specific detail.

### 3.5 Class E: pre-rendered pictures with 2D play

**Copy:** `apps/chess` (Blender pieces), `apps/village` (Blender renders at 3 zoom levels), `apps/quest` (three.js).

**Steps:**
1. Model the scene or the pieces in Blender (a Python script that makes the scene: `blender -b -P <script>.py --
   <args>`). Render with Cycles.
2. `tools/mk_art.py` or `tools/mk_data.py` cuts each piece to its box, makes it the right size, and gives it a
   palette, or keeps a background as JPEG.
3. On the board: a JPEG background goes through the JPEG decoder into a PSRAM buffer at the start (`third_party/tfjpeg`, or
   the ROM decoder in the firmware apps). Sprites are rows of runs (skip / solid / alpha).
4. A zoom needs more renders (STORM WATCH has 3 levels).

### 3.6 Class F: 2.5D (Blender sprites on a world that the code draws)

**Copy:** `apps/riviera` (a road race), `apps/dirt_kings` (a top-down arena that turns), `apps/ziki_rush` (a
side view with parallax).

**Steps (RIVIERA GP):**
1. `tools/race3d.py` makes the scene in Blender; `blender -b -P race3d.py -- still <out.png> <w> <h>` gives the
   target look (one frame of the race as Blender sees it). Show it to the reviewer first.
2. Render the assets: each car and prop at its headings, the panorama, the textures.
3. `tools/mk_data.py` writes `rv_data.c` / `rv_data.h`: the panorama and the textures as JPEG, each sprite at 9
   sizes, cut to its alpha, as rows of runs, with its pixels per metre and its anchor (the ground point under it).
4. On the board the code draws the road row by row (a texture on each row), the sky panorama by the heading, and
   scales the right size of each sprite into the world, far to near.

**DIRT KINGS:** one Blender picture of each arena from straight above (a view at an angle cannot turn); the board
turns and zooms it in tiles of 8 x 8 px each frame; the trucks at 64 headings (`tools/dk_arena.py`, `mk_data.py`).

**ZIKI RUSH:** Blender parallax layers for each zone, the track texture, the sprites
(`"<blender>" -b -P zk_art.py -- <zone|all> [parts] [samples]`, then `mk_data.py`).

### 3.7 Class G: real-time 3D polygons

**Copy:** the 3D core of `apps/ah3d` (`a3_r3d.inc`: the vector math, the camera, the triangles with a 16-bit
z-buffer, smooth colour, soft edges; `a3_mesh.inc`: a mesh builder for round shapes; `a3_hud.inc`: the HUD as
cached masks). Its 3D core is the shared lib `libs/tf3d/tf3d.inc` (the CUBBY engine; each game sets its options). `apps/iron_arena` has an older
engine.

**How the engine works:**
- Core 0 makes a display list (the triangles after the transform, the light and the clip) for each frame.
- The frame is cut into tiles of 64 x 16 px. Each tile is drawn in a buffer in the internal RAM, by both cores, then
  copied to the frame in PSRAM. The per-pixel work never touches PSRAM.
- Round meshes are made by code (spheres, capsules, round boxes). Smooth colour by the light at each corner.

**Steps:**
1. Start from the copy of the core. Do not write a new rasterizer.
2. Count the triangles. The cost of a triangle is its setup (about 800 cycles) and its rows (60 - 100 cycles), not
   its pixels. Far things need fewer triangles (LOD).
3. The controls ([behave.md](behave.md) section 3): no precise jumps on touch; a swipe to the
   right turns the view to the right; the stick moves relative to the view.

**Speed:** CUBBY 25 - 32 fps on the 3.49, about 22 on the CoreS3. An open 3D world alone is not fun (THE MODEL
failed its play test). 3D is a tool, not the game.

### 3.8 Class H: painted places in an SD data pack

**Copy:** `apps/turning_wood` (all its tools are in git: `apps/turning_wood/tools/`, its `README.md` gives the order),
`apps/four_seasons` (scenes in Python, palette pictures and layers), `apps/after_hours` (65536 colours).

**Steps (THE TURNING WOOD):**
1. `tools/design/game.py`: the places, the things, the actions and the gates. `tools/design/prove.py` proves it with
   the puzzle solver (section 3.10): PROOF OK = no dead end and each action necessary.
2. `tools/paint/bake.py`: an offline painter (numpy) paints each place, close-up, sprite and icon into the data pack.
   The soft finish (`soft.py`: a guided filter, bloom, a film curve, 256 colours and a fine dither) makes the pictures
   "less synthetic".
3. `tools/design/check_spots.py`: the ways out and the things against the pictures (SPOTS OK).
4. `tools/design/gen_c.py`: the tables of the game in C (`tw_game.inc`).
5. The music (section 5) and the data pack in the store (section 6).

**The formats:**

| Format | Content |
| --- | --- |
| TWP1 (a picture) | the width, the height, a palette of RGB565 colours, the indices (one byte a pixel) packed with zlib |
| TWL1 (a layer or a sprite) | x, y, w, h, a palette (index 0 = clear), the indices with zlib |
| AHP1 (AFTER HOURS) | 640 x 240 RGB565 (high byte first, the order of the frame buffer) + zlib: 65536 colours |
| AHL1 (AFTER HOURS) | a layer or a sprite in RGB565 (0 = clear) + zlib |
| FOUR SEASONS `.fsn` | for each season a palette and the base picture (1 byte a pixel, zlib), and the layers of each thing |

The board inflates a picture straight into its buffer with `tinfl_decompress()` (see `pk_inflate()` in
`apps/after_hours/ah_pack.inc`). A picture of 640 x 240 is wider than the screen: a small tilt pans the view.

**The gentle animation:** each place has spots (a monitor band, steam, motes, water, a blink) in a table from the
design (`tw_anim.inc`, `ah_anim.inc`). Put it in each picture. Sub-pixel motion (two pixels mixed) looks
smoother than a jump of one pixel.

**The ways out:** a soft double chevron at each way out (`exit_signs()`): open = it pulses, closed = faint, out of the
view = at the edge. Each way out must be clear in the picture.

### 3.9 Class I: a Blender stage with live 3D actors

**Copy:** `apps/ah3d` (AFTER HOURS 3D, on main). The tools of the stage (`blender/kit.py`, `blender/render.py`,
`blender/places/*.py`, `bake.py`, `test/`) are private tools, NOT in git. This part tells how they work, so that a
developer can make them again.

**The idea:** each place is a Blender scene. Each camera shot renders on the PC: the colour, the depth of each pixel
and a tap map. The board shows the picture and draws only the things that move (the hero, the doors, the parts of
the puzzles, other people) in 3D, with a z-buffer that starts from the depth of the picture. So the hero walks behind
a desk with no masks.

**The steps:**
1. **The rules.** Write the game as a table and prove it with the solver (section 3.10).
2. **The kit.** A Python module for Blender that makes the parts of a place from a few calls: boxes, walls with
   holes, materials, lights, `thing` (a thing to tap: its object index goes into the tap map), `exit` (a way out),
   `actor` (an object that the board draws in 3D: it is not in the picture), `part` (a moving part of a puzzle),
   `anim` (a spot of gentle animation), `shot` (a camera), `closeup` (a camera on a puzzle).
3. **The places.** One script for each place (`blender/places/<pid>.py`) that uses the kit. A quick check render
   with the actors in it helps the work on a place.
4. **The render.** Blender 4.3 Cycles with OptiX: the colour at 2x size (256 samples), the planar depth (Z, exact)
   and the tap map (the object index, IndexOB) at 1x. A border of 32 px at the sides and 16 px at the top and the
   bottom lets the tilt pan the view (a close-up has no border).
5. **The bake** (`bake.py`). One `.a3s` file for each shot and each mode (`<pid>_<shot>_<mode>.a3s`). The A3S1 format:
   `"A3S1"`, u16 W, u16 H, f32 f (the focal length in px), f32 eye[3], R[3], U[3], F[3] (the camera in the engine frame:
   x right, y up, z forward), f32 light[16] (the key direction toward the light, the key colour, the light from above,
   from below, the key strength, the ambient strength, the rim, the exposure: so that a 3D actor has the light of the
   picture), f32 floor_lum, u32 the zlib length of the words, u32 the zlib length of the tap map; then the words (u32:
   RGB565 in the low half, the depth `ZK / z` in the high half; zlib) and the tap map (u8; zlib). The bake also writes
   the C tables
   (`a3_scene.c`): the places, the shots, the things, the ways out, the actors, the walk grid (cells of 0.2 m), the
   meshes of the actors from Blender.
6. **The engine** (`apps/ah3d`). The class G tile renderer. Each tile starts as the picture with its depth; the actors
   draw only where they are nearer. The gentle animation goes into the tile before the actors. A camera filter (NIGHT
   TAPES) also runs in the tiles.
7. **The play.** A tap on the floor: the hero walks there (A* on the walk grid, then a string pull). A tap on a thing
   (the tap map gives its index): the hero walks to it and acts. A double tap on a way out skips the walk. A small
   tilt pans the view in the border.

**The checks before the bake** (NIGHT TAPES has them): each thing and way out is seen in a walk view; the walk floor
joins all spots; a tap in each view can bring each other view. The PC bot plays the whole game with real taps (also
with ASan and UBSan); a board tool plays the same taps on a board and compares each step.

**Faults to avoid:** a loop that draws a whole glow disc in each tile (5.8 s a frame); a tap circle of a big actor
near the camera that covers the screen; too small a mesh store (the people of NIGHT TAPES filled it and the doors had
no mesh); a bot that taps before the pan of a wide shot stops.

### 3.10 The puzzles of a quest (the solver)

Each quest of classes C, H and I proves its rules before the build, with `sdk/tools/puzzle/solver.py` (tools.md "The
puzzle solver"):
- `solve()`: the best solution. `dead_ends()`: the states from which the player can never win: a good design has
  none. `necessary()`: each puzzle must be necessary.
- `WalkPuzzle` is a base for a game where the player walks between places.
- Riddles with no clear clue are too hard: show which clue fits which puzzle (the clue at hand in its close-up), but
  give no hint text in the play. A hint button gives ONE step (the next place), not the whole route.

---

## 4. Utilities: a recipe for each kind

A utility goes into the group `UTIL`. The same skeleton (section 1.3) applies, with no title and no score. The rules:
big keys (44 px), a hold of 1 - 1.5 s with a fill for a reset or a delete, the language in the panel, and no frame
each 16 ms when nothing moves (section 4.1).

### 4.1 A simple tool screen

**Copy:** `sdk/examples/counter` (big keys, RESET with a hold, delayed saves, panel items, English and Hebrew),
`apps/level`, `apps/ohms`, `apps/hourglass` (a timer with a rich look).

- Save the state at a change, or with a delay of a few seconds, not in each frame.
- When nothing moves and nobody touches for 3 s, wait in `update()`: `vTaskDelay(pdMS_TO_TICKS(140))` (about 7
  frames a second). A tool that draws 60 frames a second empties the battery.
- A list that scrolls: `libs/kscroll` (a momentum scroll, as on a phone).
- `awake()` returns `true` only while the screen must stay on (a timer that ends soon).

### 4.2 Text input (the keyboard)

**Copy:** `apps/clock` (names of alarms), `apps/weather` (a city search).

```c
#include "tfn/kbd.h"
static char s_name[24];
static bool s_typing;

static void open_name(void)
{
    kbd_open("NAME", s_name, sizeof s_name, false);   // false: show the text (true: a password)
    s_typing = true;
}
static void input(const input_event_t *ev)
{
    if (s_typing) {
        int r = kbd_input(ev);
        if (r == KBD_OK || r == KBD_CANCEL) s_typing = false;
        return;
    }
    /* ... the screen of the app ... */
}
static void render(int64_t now)
{
    if (s_typing) {
        kbd_render(now);
        return;
    }
    /* ... */
}
```

- `kbd_*` is in the ABI from revision 11. Older apps (WEATHER, MQTT) include their own copy of `main/kbd.c`; a new app
  calls the ABI names.
- The key preview is the rule for every keyboard: a big copy of the key after 0.2 s, slide, type at the release. Do
  not make an own keyboard with a loupe.
- The Watcher: `kbd_wheel(steps, presses)`.

### 4.3 A network client (HTTP, HTTPS)

**Copy:** `apps/weather` (the request task in `wx_data.inc`), `apps/stocks`, `apps/on_air`.

- `libs/tfnet` is a small HTTP and HTTPS client (`tn_get()`, `tn_fetch()`, `tn_request()`) and a JSON reader
  (`tn_js_*`). It is a module of the firmware: put `"uses": ["tfnet"]` in the entry of the app, include
  `"tfnet/tfnet.h"` (the SDK form: the same include in the repo and out of it) and call its functions (ABI
  names). It has a shared list of 14 root certificates. Each root costs about 2 KB of internal RAM while a TLS link is
  open: an app with few hosts gives only their roots (`tn_url_t.roots`, `tn_fetch_ca()`; GMAIL `gm_roots.inc`).

- Never call the network in `update()` or `render()`: a request can take seconds. Start a task (`wxnet` in WEATHER:
  `xTaskCreatePinnedToCoreWithCaps(..., 12288, ..., 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`), give it a request, and
  read the result in `update()`. Give the task a `cancel` flag, and wait for its end in `leave()`.
- The ABI has no JSON library. Find the values in the answer yourself (a small parser, or a search for the keys).
- The app needs the permission class `net` (the store asks the user; `mk_app.py` finds the class by itself).
- A token or a password goes into the secret store (`tfn_secret_set()`), never into a file or the code (section 4.4).
- Show the state: WAIT, NO WIFI, a fault with its cause, the age of the data.

### 4.4 A check in the background and notifications

**Read:** [notify.md](notify.md). **Copy:** `apps/gmail`, `apps/reddit`, `apps/telegram`.

- An app file can have a second small part, `watch.elf`: the board keeps it loaded and calls its `check()` in the
  background rounds (one wake for all apps). It cannot draw, play a sound or read the touch, and it has about 8 s.
- A new item: `tfn_notify(title, text, link)`. A tap on the banner opens the app at the `link` (`app_launch_arg()`).
- The permission classes: `background`, `notify`, `secret`.
- The account apps (GMAIL, TELEGRAM, REDDIT) run on the board: they do not need a computer or a server.

### 4.5 A service of the system (it must work while another app is open)

Some work must go on when the app is closed: an alarm that rings, a timer of the kitchen. An app file cannot do it: its
code goes when it closes. Then the work goes into a small service in the firmware, and the app only edits it.

**Copy:** `main/alarm.c` with `apps/clock`. The service keeps the alarms in the NVS, finds the next ring, rings also
when the board is muted, wakes the screen and opens CLOCK with the argument `"ring"` (`app_launch_arg()`). The app
edits the list (`alarm_set()`) and stops or snoozes the ring (`alarm_stop()`, `alarm_snooze()`).

**The cost:** a new service is firmware: its names go into the ABI (a new revision, `abi_gen.py`), and the boards get
it only with the next firmware release. Write the service, its ABI names, its PC stubs (`tests/host/stubs.c`) and
the app in one branch. With no alarm, the service must do nothing (no task wake): an optional feature has ZERO effect
when it is not in use (AGENTS.md section 7.3).

### 4.6 Pins, buses and serial ports (hardware tools)

**Read:** [raw.md](raw.md). **Copy:** `apps/i2c_scanner`, `apps/serial_monitor`, `apps/ohms`.

- The raw ESP-IDF names (`i2c_master_*`, `uart_*`, `gpio_*`, the ADC) are in the ABI with permission classes (`bus`,
  `gpio`, `adc`).
- The free pins of each board kind are in `libs/rfpulse/rfp_board.h` (`rfp_board_uart()` for SERIAL MONITOR). Never
  drive a pin that the board uses (the screen, the touch, the SD card, the sound).
- I2C: probe only safe addresses. I2C SCANNER skips 0x20-0x27, 0x38-0x3F, 0x70-0x75 for IDENTIFY (they can be
  expanders that drive power).
- A name that the ABI does not have: `mk_app.py` writes a request (raw.md section 6). It needs a firmware release.

### 4.7 Radio tools

**Read:** [radio.md](radio.md). **Copy:** `apps/sdr`, `apps/rf_decoder`,
`apps/ir_decoder`, `sdk/examples/wifiscan`.

The layers: a source (`libs/rfsrc`: IQ blocks or a power sweep), the measurement (`libs/sdrdsp`), pulse receivers on
a pin (`libs/rfpulse`) and the decoders (`libs/rfdecode`, `libs/irdecode`, pure C with PC tests).

### 4.8 Sound in and out (tools)

**Copy:** `apps/tuner`, `apps/mic`, `apps/metro`, `apps/sound_lens`.

- The microphone: `audio_mic_start()`, `audio_mic_level()`, `audio_mic_tap()` (the samples), `audio_mic_record*()`;
  stop it in `leave()`.
- A metronome or an instrument: `audio_drone()`, `sfx_tone()`, the instruments of `libs/instr` and `libs/drumkit`.
- An app that must sound with the panel down sets `.run = true`.

### 4.9 Two boards

**Copy:** `apps/pong` (`NET_*` code), `apps/rally`, `main/app_walkie.c` (a firmware app).

- `net2.h`: the lobby (each board sends HELLO; the faster board is the HOST), the state packets of the HOST, the
  inputs of the CLIENT in the time of the HOST, the effect events. Only friends pair (SETTINGS > FRIENDS).
- `duo.h`: one button at each end of the 3.49 for two players on one board.
- The link app id is one letter for each game. Check that no other app uses your letter (FRIENDS took the letter of
  FIGHT once).
- A host test: two copies of the game in one program with a fake radio (your own test harness: a radio
  stub, and a header that gives the shared names of `duo.c` and `net2.c` a prefix for each side).

### 4.10 A reader of big files on the SD card

**Copy:** `apps/books` (EPUB), the music stream of `apps/after_hours/ah_stream.inc` (a task reads a file in blocks).

- Check the card: `board_sd_mounted()`; `board_sd_mount()` mounts the card when it is not mounted (a card that went
  in later; no card: `false` after about 0.2 s). The root is `BOARD_SD_ROOT` (`/sdcard`). With no card, show a
  clear screen (NO SD CARD), not an empty page.
- Never read a whole big file into memory. Keep an index (a table of offsets) in a small file, `fseek()` to a part,
  and read blocks of 16 - 64 KB into PSRAM.
- The card is FAT32: one file can have 4 GB at most. Split bigger data into parts.
- A read fault of the card can come (the WiFi takes the internal RAM of the DMA buffer in bursts). Try again for
  about 3 s before a fault shows (section 6).

### 4.11 A web page of the app

BOOKS has a page on the board (`apps/books/books_web.c`: upload a book, list, delete). BOOKS is a firmware app: the
firmware registers its handlers. The ABI of the app files has the request functions (`httpd_req_*`, `httpd_resp_*`,
`httpd_register_uri_handler`) but no name that gives the handle of the web server of the board. A web page of an app
file thus needs a new ABI name (raw.md section 6) and a firmware release first.

---

## 5. Sound and music

| Need | Use | Reference |
| --- | --- | --- |
| A click, a hit, a ready sound | `sfx_play(SFX_*)`, `sfx_tone(f0, f1, ms, wave, vol)` | behave.md section 5 |
| The opening tune of a game | `libs/jingle/jingle.h` (the note names of `main/tunes.c`) | `apps/murano` |
| Music as notes (MIDI) | `libs/midi_bg` (`midi_bg_init(tracks, n, voices)`, `midi_bg_service()` each frame, `midi_bg_end()` in `leave()`) | `apps/cubby/tools/music`, `apps/murano/tools/music` |
| Real recorded effects | `libs/tfsfx`: MP3 effects (mono, 24 kHz, 48 kbps), from the SD card or in the app file; the module is in the firmware (ABI rev 22) | [sound.md](sound.md) |
| Music as MP3 on the card | a task on core 0 decodes the file (minimp3) into a ring; one sample voice plays the ring | `apps/after_hours/ah_stream.inc`, `apps/turning_wood/tw_stream.inc` |
| The voice of the lines of a quest (English) | `libs/qvoice/qvoice.inc`: `qv_open(id, lines, n)` at the enter, `qv_tick()` each frame, `qv_say(k)` when line k shows, `qv_cut()` at a skip, `qv_close()` at the leave. TFN OS makes the voice of all lines ahead (in the menu, never while a game shows); the game only plays the files. No voice in the app package | `apps/side_b` (`voice_tick`, `voice_line`) |
| Voice commands in a quest (English speech) | `libs/qtalk/qtalk.inc` (the talk key, the ears of `tfn/listen.h`, the parse of the codes of a small brain; the PC: the keyboard and the brain in the app) and, for a quest of `libs/qeng`, `libs/qtalk/qt_qeng.inc` (the run of the codes through `go_hot`, the replies, the plate of the feedback): give the code words, the lines, the colors and the own verbs; `qtq_open()` / `qtq_update()` / `qtq_input()` / `qtq_draw()`. The brain is a TNY2 file on the card, trained from the tables of the game | `apps/side_b/sideb_talk.inc`, `apps/last_show/show_talk.inc` |

- A game on the SD card uses MP3 effects. CC0 effects only (Kenney and others); write the source
  of each in `sfx/SOURCES.md`.
- `python libs/tfsfx/mk_sfx_c.py <sfx folder> <out path> <PREFIX> name1 name2 ...` puts the effects into the app file
  (`tfsfx_load_mem()`): a board with no SD card has them too.
- The MP3 music has its own copy of minimp3 in the app file (tfsfx has its copy in the firmware). Give the copy of the
  music its own names (`#define mp3dec_init ah_mp3dec_init` before the include): the host tests link all apps
  together. The music decoder of the quests starts after `tfsfx_done()` (the card reads of the effects come first).
- The music tools render MIDI to MP3 on the PC with FluidSynth and the MuseScore General sound font (MIT; the licence
  goes into the data pack as `m_licence.txt` and into `NOTICE.md`). Render each track alone and mix, then loop at bar
  lines. Master the mix: no clipping (a level over 100 % gives distortion).
- Some games get MIDI music from a composer. Do not write new music for a game when its composer writes the MIDI.
  The MIDI source folders are READ ONLY: copy what you need into the work folder.
- A test of the music: no late music blocks in the board log (the music task logs a late block).

---

## 6. The data pack on the SD card

Space on the SD card is not a limit: a game can have a pack of 10 - 40 MB. Memory and speed are limits.

1. **The folder.** `/sdcard/<pack>/` (for example `/sdcard/wood/`, `/sdcard/ah3d/`). Name the files in the pack
   (`m_night.mp3`, `s_lobby_0_n.a3s`); keep a `manifest.json` on the PC side for the tools.
2. **To your board.** A tool that sends each file with the manager (`api/sd/put`, about 440 KB/s; a file with the same
   size and time is not sent again), or the board page FILES. Note the push tool of each game in your `LOCAL.md`.
3. **To the store** (the person who runs the store): `python sdk/tools/store_data.py <app id> <local file>=<card path> ...`. The keeper of
   the manager copies the missing files to each board that has the app and an SD card, never while the app runs.
   MANUAL.md "The data packs of the store".
4. **In the app.**
   - With no card or no pack: a clear screen (NO DATA / NO SD CARD), not a crash. Test it on a board with no card.
   - Open and read with a retry: `sdmmc_cmd: allocate_dma_buf: not enough mem` can come when the WiFi takes the
     internal RAM. The firmware keeps one DMA buffer for the SD driver (`main/sd_dma.h`), but an app must still wait
     and try again (about 3 s) and load a place again after a fault.
   - Load a new place in the dark of a cut (0.2 - 1 s), not in the middle of a move.
   - A music task that cannot read its file must stop after 3 tries, not spin (a spin stopped the 3.49 with the task
     watchdog).
5. **The licence files.** Each pack with music of a sound font has `m_licence.txt`.

---

## 7. Speed and memory on the board

| Fact | What to do |
| --- | --- |
| PSRAM is slow: about 40 MB/s for the frame; a copy of a 640 x 172 frame takes 11 - 13 ms. | Do per-pixel work in tiles in the internal RAM, not in passes over the frame. |
| A write to a frame line that is not in the cache reads the line first, and the line goes back later (the quad PSRAM of the CoreS3: a fill is about 10 MB/s). The frame buffer keeps the frame of two frames ago. | Do not paint a pixel two times: skip the boxes that an opaque key or plate covers (ORBITAL `holes_make()`). For static parts, write only the pixels that change (`if (*p != c) *p = c;`): a line that is only read does not go back. |
| The internal RAM is short: about 12 - 18 KB free with the WiFi on. | Make a buffer or a task stack in the internal RAM only after a check of `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)`. A task stack in PSRAM crashed during flash writes. |
| The code and the static data of an app file go to PSRAM. | Put each source with big arrays into `"psram"` of `apps.json`. |
| `sinf` / `cosf` take about 12 us a call in an app file. | `fsin()` / `fcos()` of `gfx.h`. |
| An app file has no `__lshrdi3`. | No 64-bit shift by a variable. |
| In an app file `floorf`, `ceilf`, `fminf`, `fmaxf` and the float division (`__divsf3`) are calls into the firmware (about 1 us each on the 3.49: a capsule of 30 rows took 0.5 ms in JELLY COURIER). | Inline forms (`apps/jelly/jl_gfx.inc`: `jl_floorf`, `frcp`, `jsin`); a reciprocal made once outside a loop. Check the imports: `xtensa-esp32s3-elf-nm -D sdk/out/<id>/lcd349/app.elf`. |
| The second core did not help a 2D app file on the 3.49 (JELLY COURIER: each core took the time of one core alone). | Measure before a worker: the code and the data of an app file share one cache and the PSRAM bus. |
| A worker task on core 1 that never rests makes the task watchdog warn. | Rest a few ms each 20 frames. |
| A loop that is not clipped to its tile does the whole work in each tile. | Clip each loop to the tile box. |
| The menu needs the internal RAM after the app closes. | Free each buffer in `leave()`; end each task in `leave()` and wait for it. |

Speed targets: 30 fps or more in play. Measure on the board (the fps line of the log), on the 3.49 and on the CoreS3
(the slower one). Measure the free memory at the menu before and after the app.

---

## 8. The tests and the QA

Find the faults yourself: do not leave them to the reviewer. Before you say "done":

1. **The rules.** A proof of the puzzles (solver), a PC bot that plays the whole game through the touch.
2. **The pictures.** A picture of each screen and each place, on the 3.49 and the 320 x 240 frame, in English and in
   Hebrew. Look at each picture. Automatic checks: a label on a label or on a button, text that does not fit, a
   frame that is not fully painted.
3. **The touch.** Targets of 44 px and 5.6 mm (36 px and 4.6 mm at least). A thumb probe (a random thumb with sd 2.5 mm
   gives 0 % of the wrong action on a safe target). No loss action on one tap of a small target: a second tap or a
   hold. A finger-controlled object about 100 px from the edge where the thumb rests.
4. **The saves.** Each save that CONTINUE can load; a NEW GAME after a CONTINUE; the save after a leave in the middle.
5. **The board.** The fuzz and the bot pass; then a run on a board (silent), the fps, the memory before and after,
   real pictures (`shot`).
6. **The feel.** A naive person, not a bot that knows the trick: wide time windows (about 100 ms of human error), one
   skill at a time, no tutorial text for obvious play, a tilt zero at the start of a game (`imu_set_level()`), small
   angles (1 - 8 degrees) that do something.

A bot result is not proof of fun. Only a test by a person on the board is.

---

## 9. To the store

1. `store.json` in the folder of the app ([store.md](store.md)): 2 - 3 sentences in English and Hebrew, 260
   characters or fewer, the facts that help a person decide (the languages, friends, the SD card and the size of the
   data, WiFi or an account, violence).
2. The checklist ([checklist.md](checklist.md)).
3. Put the class of the graphics and the tools that make the data again into the `MANUAL.md` section of the app, and
   add the app to [graphics.md](graphics.md) (a game) or to the tables of this guide (a new kind of app).
4. Open a pull request with the source of the app (`CONTRIBUTING.md`). Test it on your board with a package made
   with `--nosign` (the developer mode). The store signs the app after its review, and publishes it and its data
   pack.

---

## 10. Faults that we found (do not do them again)

| Fault | The cause | The rule |
| --- | --- | --- |
| "THE FILE OF MURANO IS DAMAGED" after a new install | a task of the old copy did not end before the new install | End each task in `leave()` and wait for it; check the log text of the new build after each install. |
| A new game did not show in the menu of a board | the menu of the board had its own order with no rest line | Keep `"new": true`; check the real menu picture on each test board. |
| 5.8 s frames, the task watchdog | a glow loop drew its whole box in each tile | Clip each loop to the tile. |
| The doors of the places had no mesh | the mesh store was full | Give the store room for all actors; make the PC test fail on a lost mesh. |
| The bot left the path on the board | a tap during the pan of a wide shot | A bot waits until the view is at rest. |
| The board stopped (watchdog) | a music task spun on a read fault of the card | Stop the music after 3 starts with no sample. |
| A save did not stay | the NVS of the board was full | Big saves go into a file (`/data/<id>/save.bin` or the card); the firmware has a spill store for a full NVS. |
| `ui_rrect_a` did not link | the name is not in the ABI of the app files | Check the ABI (api.md) before you use a firmware function; make an own blend. |
| A patch broke a file | a bash heredoc turned `\n` in a string into a new line | Write patch scripts with an editor, not with a heredoc. |
| A WiFi install said "no manifest.txt came" | `mk_app.py` stopped at the icon tool after it removed the old files; a direct `tfn_pkg.push()` sent the folder with `app.elf` only | Push only with `mk_app.py` / `tfn_app.py`: they make a new package in a stage folder and check it before the first request. The board refuses a file before `manifest.txt`. |
| CRLF files got LF lines | a Python write on Windows | Open with `newline=""`; check `git diff --cached --stat` before a commit. |
| Text cut on the CoreS3 | a line that fits 640 px does not fit 320 px | Pictures of both frames in both languages; `ui_text_fit()`. |
| A forward jump was very hard to do | a precise jump on touch | No precise jumps on touch; move at once to the stick; coyote time and a jump buffer. |
