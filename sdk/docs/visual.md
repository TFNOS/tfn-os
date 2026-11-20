# TFN OS visual guide

This guide gives the rules for the look and the feel of an app. The rules come from many rounds of tests by the user
on the real boards. An app that breaks them goes back for more work.

The function names in this guide are in the ABI (see [api.md](api.md)) or in the headers (`static inline`).

The ways to make the pictures of a game (code-drawn, pixel art, Blender pictures, painted places, a Blender stage
with live 3D actors), the art tools of each game and the costs on the board are in [graphics.md](graphics.md).

## 1. The board kinds and their screens

One app file is for one board kind. `sdk/tools/mk_app.py` compiles the same source once for each kind. A kind has its
own screen, so each app has a layout for each kind.

| Kind | Board | Panel | The frame that the app draws | Natural position |
| --- | --- | --- | --- | --- |
| `lcd349` | Waveshare ESP32-S3-Touch-LCD-3.49 | 172 x 640 bar | 640 x 172 (`landscape = true`) or 172 x 640 | The bar in two hands. The app chooses landscape or portrait. |
| `cores3` | M5Stack CoreS3 | 320 x 240 | 320 x 240 (`landscape = true`) or 240 x 320 | Landscape: the board stands on a desk. |
| `am18` | Waveshare ESP32-S3-Touch-AMOLED-1.8 | 368 x 448 AMOLED | The CoreS3 frame. The board scales it x1.4 (bilinear) to the panel. An app with `native = true` draws the full panel: 448 x 368 or 368 x 448. | As the CoreS3 frame. |
| `watcher` | SenseCAP Watcher | 412 x 412 round | The CoreS3 frame (zoom or `round_fit`), or a round layout of 412 x 412 (`round = true`). | Upright, fixed. It has no motion sensor and no screen turn. It has a wheel with a button. |
| `tab5` | M5Stack Tab5 (ESP32-P4) | 720 x 1280 | 640 x 360 (`landscape = true`) or 360 x 640. The board scales the frame x2 to the full panel. | The app chooses. A DEV kind: no release of the firmware for it now. It has no BOOT key and no native radio. |

### The Watcher

- An app with no round layout draws the CoreS3 frame (320 x 240). The board zooms the frame to fill the width of the
  circle. Then only a circle of 160 frame px radius around (160, 120) shows: the zoom cuts the corners. Put no text and no
  target in the corners.
- `round_fit = true`: the board shows the whole CoreS3 frame at 1:1 inside the circle. All of the frame shows, but it
  is small.
- `round = true`: the app draws the native 412 x 412 frame. Only the circle of diameter `gfx_w` shows. Use
  `gfx_round()` and `gfx_in_circle(x, y, margin)` to keep text and targets in the circle.
- The wheel: an app with `wheel = true` reads the wheel itself (`input_wheel()`, `input_wheel_presses()`). For the
  other apps, the wheel is a left / right tilt and its button is a tap in the middle of the screen.

### The compile-time names of a kind (`main/board.h`)

| Name | `lcd349` | `cores3` | `am18` | `watcher` | `tab5` |
| --- | --- | --- | --- | --- | --- |
| `BOARD_BAR_SCREEN` | 1 | 0 | 0 | 0 | 0 |
| `LCD_W` x `LCD_H` (the physical portrait frame) | 172 x 640 | 240 x 320 | 240 x 320 | 240 x 320 | 360 x 640 |
| `BOARD_NATIVE_W` x `BOARD_NATIVE_H` | 0 | 0 | 368 x 448 | 412 x 412 | 0 |
| `BOARD_ROUND`, `BOARD_WHEEL`, `BOARD_FIXED_ORIENT` | 0 | 0 | 0 | 1 | 0 |
| The kind switch of the build | `CONFIG_TFN_TARGET_WS349` | `CONFIG_TFN_TARGET_CORES3SE` | `CONFIG_TFN_TARGET_AMOLED18` | `CONFIG_TFN_TARGET_WATCHER` | `CONFIG_TFN_TARGET_TAB5` |

At run time, `gfx_w` and `gfx_h` give the size of the frame now, `gfx_native()` tells if the app draws the native
panel, and `gfx_round()` tells if the app draws the round frame. Use `gfx_w` and `gfx_h` where you can, not fixed
numbers.

A layout for each kind:

```c
#include "tfn/app.h"
#include "tfn/board.h"
#include "tfn/gfx.h"

#if BOARD_BAR_SCREEN            // the 3.49: 640 x 172
#define SCORE_X  560
#define SCORE_Y  40
#elif BOARD_ROUND               // the Watcher with .round = true: 412 x 412, only the circle shows
#define SCORE_X  206
#define SCORE_Y  80
#elif CONFIG_TFN_TARGET_TAB5 // the Tab5: 640 x 360 (x2 on the panel)
#define SCORE_X  560
#define SCORE_Y  48
#else                           // the CoreS3 frame: the CoreS3, the AMOLED (x1.4), the Watcher (zoom)
#define SCORE_X  260
#define SCORE_Y  36
#endif
```

## 2. The coordinates

- The app draws in the LOGICAL frame of its orientation: (0, 0) is the top left corner, x goes to the right, y goes
  down. The board turns the frame to the panel.
- The touch events (`input_event_t` in `input()`) have logical coordinates.
- `input_points()` gives PHYSICAL coordinates. Change them with `gfx_touch_to_logical(px, py, &x, &y)`.
- The board picks the orientation when the app opens: the side of the board that is up, in the family of
  `app_t.landscape`. An app that must follow the board while it is open (an hourglass, a level) calls
  `imu_upright()` and `gfx_set_orient()` itself.
- The menu icon uses LANDSCAPE logical coordinates: a box of `s` x `s` px from (`x`, `y`). See section 8.

## 3. The frame

- `render()` draws the WHOLE frame each time. The board has two frame buffers, and they change places after each
  frame. The buffer that you get has the frame before the last one, not the last one. A part that you do not draw
  shows an old picture.
- The frame buffer `g_fb` has `gfx_w` x `gfx_h` pixels in RGB565 with the two bytes swapped. Do not make a colour by
  hand. Use the helpers:

| Helper | What it gives |
| --- | --- |
| `rgb(r, g, b)` | A colour from 8-bit red, green and blue. |
| `hsv(h, s, v)` | A colour from a hue of 0..359 and a saturation and value of 0..255. |
| `ui_mix(a, b, t)` | A mix of two colours: t = 0 gives a, t = 255 gives b. |
| `gfx_darken_rect(x, y, w, h, shift)` | A darker area: 1 = half, 2 = a quarter (a panel behind text). |

```c
static void render(int64_t now)
{
    gfx_fill(rgb(18, 14, 40));                                  // all of the frame, first
    float t = (float)now / 1000.0f;
    gfx_fill_circle(gfx_w / 2, gfx_h / 2, 30, hsv((int)(t * 60) % 360, 200, 255));
    ui_rrect(20, 20, 120, 56, 14, rgb(60, 40, 120));            // a soft box for a button
    mf_text_c(&MF_N20, 80, 56, "PLAY", rgb(255, 255, 255), 255, 1);
}
```

- The `gfx_*` shapes obey the clip box (`gfx_clip_box()`, `gfx_clip_bottom()`). The `mf_*` and `ui_*` functions
  write `g_fb` directly: they obey only the screen edges.

## 4. The fonts

Big numbers and all main text use the anti-aliased atlas fonts. NEVER use the old 5 x 7 font of dots (`gfx_text`) or
the stroke font (`stroke_text`) for main text: such text looks like the drawing of a child. Use `gfx_text`
only for a debug line.

### The Bungee faces (`mfont.h`)

One copy in the firmware, shared by the menu and the games. Draw them with `mf_text()` (x = the left end),
`mf_text_c()` (x = the centre), `mf_text_o()` / `mf_text_oc()` (a shadow and a 1 px outline: the look of a game word).
`base` is the baseline. `mf_width()` gives the width.

| Font | Height of the capitals | Line | Characters |
| --- | --- | --- | --- |
| `MF_N34` | 35 px | 46 px | ASCII 32-95: capitals, digits, signs |
| `MF_N28` | 29 px | 38 px | ASCII 32-95 |
| `MF_N24` | 25 px | 33 px | ASCII 32-95 |
| `MF_N20` | 21 px | 27 px | ASCII 32-95 |
| `MF_N16` | 17 px | 22 px | ASCII 32-95 |
| `MF_LABEL` | 14 px | 18 px | ASCII 32-126 |
| `MF_SMALL` | 12 px | 15 px | ASCII 32-126 |
| `MF_TINY` | 10 px | 13 px | ASCII 32-126 |

The `MF_N*` faces have no small letters: write their text in capitals.

```c
// a score with a shadow of 2 px and a dark outline
char b[16];
snprintf(b, sizeof b, "%d", score);
mf_text_oc(&MF_N34, gfx_w / 2, 52, b, rgb(255, 240, 200), rgb(30, 16, 40), 2, 1);
```

### The Rubik faces (`kfont.h`): English and Hebrew

Five faces with Latin and Hebrew letters (UTF-8). Draw them with `ui_text_c()` (x = the centre) and measure them with
`ui_text_w()`. The function lays out a line with Hebrew from the right, by the Unicode Bidirectional Algorithm
(`libs/bidi/bidi.h`): a Latin phrase ("TFN SOUND") or a number ("-5", "10:30") keeps its own order, a bracket or a
colon goes to the side where it belongs, and a bracket of a right-to-left part shows its mirror. So write each Hebrew
text in its logical order, as you type it: never turn a bracket or the words of a Latin phrase round to make them look
right. An app that draws a Hebrew line with its own font can include `bidi/bidi.h` and draw the letters in the order
of `bidi_order()`.

| Font | Height of the capitals | Line |
| --- | --- | --- |
| `KF_TITLE` | 34 px | 43 px |
| `KF_BIG` | 23 px | 29 px |
| `KF_TEXT` | 17 px | 22 px |
| `KF_NAME` | 15 px | 19 px |
| `KF_LABEL` | 13 px | 17 px |

```c
const char *line = s_hebrew ? "לחץ כדי להתחיל" : "TAP TO START";
ui_text_c(&KF_BIG, gfx_w / 2, gfx_h - 30, line, rgb(255, 255, 255));
```

A text that must fit a box: measure it with `ui_text_w()` (or `mf_width()`), and take the next smaller face when it is
too wide.

### Your own face

An app can have its own atlas as a `static const mfont_t` in its source, and draw it with `mf_text()`. STOCKS does
this with Inter (`libs/interfont/stocks_font.inc`, made from a TTF file by
`libs/interfont/tools/export_inter.py`). Use a font with an open licence (OFL), and add its
licence to `NOTICE.md`.

## 5. Touch targets

A thumb touches an area of about 10 mm. One px has another size on each board:

| Frame | One px | px in 1 mm | 4.6 mm | 5.6 mm | 10 mm |
| --- | --- | --- | --- | --- | --- |
| 3.49, 640 x 172 | 0.133 mm | 7.5 | 35 px | 42 px | 75 px |
| CoreS3, 320 x 240 | 0.128 mm | 7.8 | 36 px | 44 px | 78 px |
| AMOLED, the 320 x 240 frame (x1.4) | 0.111 mm | 9.0 | 42 px | 50 px | 90 px |
| AMOLED, native 448 x 368 | 0.079 mm | 12.7 | 58 px | 71 px | 127 px |
| Watcher, round 412 or `round_fit` | 0.089 mm | 11.2 | 52 px | 63 px | 112 px |
| Watcher, the zoom of the 320 x 240 frame | 0.115 mm | 8.7 | 40 px | 49 px | 87 px |
| Tab5, 640 x 360 (x2 on a 5 inch panel) | 0.173 mm | 5.8 | 27 px | 32 px | 58 px |

The rules:

- The keys that a thumb uses all the time (jump, fire, the stick): about 10 mm or more.
- Each other target: 36 px AND 4.6 mm at least on each board. The aim is 44 px and 5.6 mm. One 320 x 240 layout
  serves the CoreS3, the AMOLED and the Watcher zoom, so it needs 42 px at least (the AMOLED).
- A precise target acts when the finger comes off the glass (`IN_TAP` or `IN_UP`), not at `IN_DOWN`.
- A grid of small cells (a board game, a map) needs a two-tap magnifier (a loupe): the first tap shows a big view,
  the second tap chooses. With no loupe, a thumb on cells of 15 px hits the next cell most of the time.
- An action that you cannot undo (a shot, a move, a delete, NEW GAME next to CONTINUE) never comes from one tap on a
  small target. Use a second tap ("TAP AGAIN"), a loupe, or a hold of 1-1.5 s with a fill. A long press of 350 ms is
  not enough: a slow thumb tap is that long.
- Keep finger-controlled objects and the main keys about 100 px from the edge where the thumb holds the 3.49.
- Keep controls out of the pull zone of the settings panel: a small box in the middle of the top edge.
- Do a fat-finger test. A thumb with a random error of sd 2.5 mm, aimed at each safe target, must give 0% of the wrong
  action. Write a probe for the host build that taps each target with that error and counts the wrong actions.

## 6. The feel of a game

- **A camera that moves.** The user calls a good game "captivating". That means: a zoom in and out about every 3-5 s, close shots, a screen shake, a camera
  roll. A static scene is a failure. Each zoom keeps the player in the frame and near the centre.
- **Tilt.** Call `imu_set_level()` at the start of a game: the board position then is the zero. Small angles (1-8
  degrees) must do something that the player sees. Read the tilt with `imu_tilt_logical()`. Call
  `imu_set_level()` again in the `resume` function of the panel items (see behave.md).
- **Distinct inputs.** Each input (tilt, shake, touch, the BOOT key) has its own clear effect and look.
- **No walls of tutorial text.** Show the play. Do not explain an obvious action.
- **Games of time** (a press at the correct moment): one skill at a time, wide windows, and about 100 ms of human
  error in each window. A game with a time window and a length together is too hard.
- **Tune for a person who does not know the rules**, not for a bot that knows the trick. A bot result is not proof
  of fun.
- **A platform jump.** On the 3.49, a jump can use the BOOT key (`app_t.key`, `app_key_presses()`). Act on the touch
  down for a game action, not on a tap (a tap comes only after the finger goes up).

## 7. The speed

- The aim is 30 fps or more on each kind. The frame time is then 33 ms or less.
- Measure it on the board. The console log shows `<APP NAME>: <x> fps` each 300 frames.
- The frame buffers are in PSRAM. One copy of the full screen costs about 11 ms on the 3.49. A full-screen alpha layer,
  big glows and many layers of shadows each cost 10-25 ms. Put a dark look into the colours of a still layer, not into
  a pass over the screen.
- Draw a still background once into a buffer, and copy only the rows that you need.
- An idle app does not draw busy frames (see behave.md, "Battery").

## 8. The menu icon

The menu shows the icon of an installed app from `icon.bin`. `mk_app.py` makes it on the PC with the `draw_icon()` of
the app: 16 frames, 125 ms each, at 128 x 128 px. The board plays the 16 frames in a loop (2 s).

- Draw only inside the box of `s` x `s` px from (`x`, `y`), in landscape logical coordinates. Scale all sizes with `s`.
- Animate with `icon_time()` (seconds). Do not use a counter of your own or `esp_timer_get_time()`. A motion that
  repeats each 2 s (or 1 s, 0.5 s) loops with no jump.
- A pixel that you do not draw is a hole (the menu shows through it).
- At most 255 colours over all frames. With more, the tool takes fewer bits of each colour.
- `draw_icon()` must not need `enter()`: the tool calls only `init()` of the app before it.
- Use the `gfx_*` shapes in an icon. The tool puts a clip box around the icon, and the `mf_*` and `ui_*` functions
  do not obey it: if you use them, keep them inside the box yourself.
- Check the frames: `python sdk/tools/icon_png.py sdk/out/<id>/<kind>/icon.bin icon.png`.

```c
// the icon: a ball that bounces each second
static void icon(int x, int y, int s)
{
    float t = icon_time();
    gfx_fill_rect(x + s / 10, y + s / 10, s * 8 / 10, s * 8 / 10, rgb(40, 30, 80));
    float h = fsin(t * 3.14159f);                               // a period of 1 s (the bounce: |sin|)
    if (h < 0) h = -h;
    int by = y + s * 3 / 4 - (int)(h * s * 0.45f);
    gfx_fill_circle(x + s / 2, by, s / 8, rgb(255, 200, 80));
}
```

## 9. Check with real pictures

Look at each screen of the app on ALL board kinds, in their natural positions, in English and in Hebrew, before the app
goes to the store. A host picture is a good first check, but a real picture from the board is the proof.

- On the PC: the simulator (`simulator/run.py`, `simulator/smoke.py`) and the golden frames (`tests/golden/run.py`).
  The host picture tool tshot (tools.md) takes steps: taps, drags, frames and shots.
- On the board: the console `shot` (the next frame as base64 lines), or `GET http://<board>/api/screen` (the frame
  as RGB565, the size in the headers `X-W` and `X-H`).
