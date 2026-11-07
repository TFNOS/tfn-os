# COUNTER: a tally counter

"+" and "-" change the value by the step (1, 5 or 10). RESET needs a hold of 1 s with a fill bar: a short tap only
shows the hint. The value stays in the saves.

## What it shows

| Topic | Where in `app_counter.c` | Rule |
| --- | --- | --- |
| A utility with big thumb keys: 160 x 152 px on the 3.49, 140 x 70 px on the 320 x 240 frame | `R_MINUS`, `R_PLUS`, `key_at()` | [visual.md](../../docs/visual.md) section 5 |
| No action that you cannot undo on one tap: RESET is a hold of 1 s with a fill, and it stops when the finger leaves the key | `input()`, `update()`, `render()` | visual.md section 5, the project rule |
| The saves: the value is written 1.5 s after the last change and in `leave()`, not in each tap | `save_now()`, `leave()` | [behave.md](../../docs/behave.md) section 4 |
| The settings panel items: LANGUAGE and STEP, both with a save key | `ITEMS[]`, `PANEL` | behave.md section 7 |
| English and Hebrew: `KF_*` for the text, `MF_*` for the number | `tr()`, `render()` | behave.md section 8 |
| The menu icon: tally marks with a slash, a loop of 2 s | `icon()` | visual.md section 8 |

The default language is Hebrew (the rule of the project for apps with two languages).

## Build

From `tfn-os/`:

```
python sdk/tools/mk_app.py sdk/examples/counter/app_counter.c --sym app_counter --name COUNTER --group UTIL --kinds lcd349,cores3 --nosign
```

In its own folder, with the SDK package only (`app.json` is the manifest; [app_repo.md](../../docs/app_repo.md)):

```
python <sdk>/tools/tfn_app.py build counter --kinds lcd349,cores3 --idf <esp-idf folder>
```

Add `--push <board ip> --open` to send it to a board in the developer mode. The package goes to `sdk/out/counter/`.

## Tests

A host test of the project (not in this repo) checks the keys, the range, the hold of RESET, the save delay and `leave()`.
