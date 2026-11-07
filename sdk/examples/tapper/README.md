# TAPPER: a small tap game

Targets come and go. The player taps each target before its ring closes. A target that the player misses costs one
life. The game ends after 3 lost lives. The best score stays in the saves.

## What it shows

| Topic | Where in `app_tapper.c` | Rule |
| --- | --- | --- |
| The frame: one layout for the 3.49 bar and one for the 320 x 240 frame, and a `render()` that paints the whole frame | the `#if BOARD_BAR_SCREEN` block, `draw_bg()` | [visual.md](../../docs/visual.md) sections 1 and 3 |
| The anti-aliased fonts `MF_*` | `render()`, `draw_hud()` | visual.md section 4 |
| Touch targets of 44 px or more | `R_DISC` 24 (a disc of 48 px), `R_HIT` 30, a button of 220 x 56 px | visual.md section 5 |
| A game action on `IN_DOWN`, a button on `IN_TAP`, a guard of 700 ms after the end | `input()` | visual.md section 5 |
| The sounds: `sfx_play()`, `sfx_tone()` (a tone that goes up with the streak), a start tune with `audio_tune()` | `START_TUNE`, `start_game()` | [behave.md](../../docs/behave.md) section 5 |
| The saves: `store_get()` in `enter()`, `store_set()` only when the best score changes | `SAVE_BEST`, `end_game()` | behave.md section 4 |
| The menu icon with `icon_time()` | `icon()` | visual.md section 8 |

The app has English text only (the `MF_*` faces have no Hebrew). For two languages see the COUNTER example.

## Build

From `tfn-os/`:

```
python sdk/tools/mk_app.py sdk/examples/tapper/app_tapper.c --sym app_tapper --name TAPPER --kinds lcd349,cores3 --nosign
```

In its own folder, with the SDK package only (`app.json` is the manifest; [app_repo.md](../../docs/app_repo.md)):

```
python <sdk>/tools/tfn_app.py build tapper --kinds lcd349,cores3 --idf <esp-idf folder>
```

Add `--push <board ip> --open` to send it to a board in the developer mode. The package goes to `sdk/out/tapper/`.

## Tests

The project has a host test and a picture tool (`exshot`) for it (not in this repo). `mk_app.py` makes no
icon for a source out of `main/`, so `exshot` draws the icon frames too (the step `I`).
