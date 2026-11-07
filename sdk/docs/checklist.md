# TFN OS checklist: before an app goes to the store

Do each step. Write the result (the pictures, the fps, the memory numbers) in the notes of the app. The user tests
the app on a real board only after this list is complete. The details of each rule are in [visual.md](visual.md) and
[behave.md](behave.md). The commands are in [tools.md](tools.md).

## 0. The store page

- [ ] The folder of the app has a `store.json` with the description in English and in Hebrew ([store.md](store.md)):
  2-3 short sentences, 260 characters or fewer each: what it is, what the user does, the controls, and the facts that
  help a person decide (the languages, friends on more boards, the SD card and its data size, WiFi or an account,
  violence). `mk_app.py --publish` stops when there is no English text.
- [ ] A picture of the app page in STORE, in English and in Hebrew: the text fits, and no word goes under the buttons.

## 1. The build

- [ ] `python sdk/tools/mk_app.py <APP>` makes a package for each kind of the app (`lcd349`, `cores3`, `am18`,
      `watcher`, and `tab5` when the app has a Tab5 layout) with no error.
- [ ] The ABI check passes: the app calls only ABI names. If it needs a new name, stop: send the request of
      `sdk/out/<id>/abi_request.json` as a GitHub issue ([raw.md](raw.md) section 6); after the approval, a new
      firmware has it.
- [ ] Raw ESP-IDF calls obey the rules of raw.md section 5: the raw code inside `#ifdef ESP_PLATFORM`, no change of the
      WiFi connection, blocking calls in a task that ends in `leave()`, each callback, timer and handler given back in
      `leave()`, each queue, socket and key deleted.
- [ ] The output line of `mk_app.py` shows the permission classes that you expect (`perms ...`), and no more.
- [ ] The app compiles with no compiler warnings on each kind (`python tests/host/espcheck.py <file.c>` for an app of `main/`).
- [ ] The `app_t` has `enter`, `input`, `update` and `render`.
- [ ] The `app_t` flags are correct: `landscape`, `needs`, `native`, `round` or `round_fit`, `wheel`, `key`, `run`.

## 2. The pictures on the 4 board kinds

- [ ] Real pictures of EACH screen of the app (the title, the play, each panel, the end) on the 3.49, the CoreS3, the
      AMOLED and the Watcher, each in its natural position. Use the console `shot` or `/api/screen`. Look at each
      picture.
- [ ] The same pictures in English and in Hebrew (when the app has the two languages).
- [ ] No text on text, no text on a button, no text that the edge cuts, no target in the corners of the Watcher zoom.
- [ ] Big numbers and main text use the atlas fonts (`MF_*`, `KF_*`, or a font of the app), not `gfx_text`.
- [ ] `render()` draws the whole frame: no old picture shows in any part of the screen.
- [ ] The menu icon looks good in all 16 frames (`python sdk/tools/icon_png.py sdk/out/<id>/<kind>/icon.bin icon.png`),
      it stays in its box, and its motion loops with no jump.

## 3. The touch

- [ ] Each target has the size of visual.md, section 5, on each kind (36 px and 4.6 mm at least; about 10 mm for the
      keys that a thumb uses all the time).
- [ ] A fat-finger test: taps with a random error of sd 2.5 mm on each target give 0% of the wrong action.
- [ ] A small grid has a two-tap magnifier.
- [ ] No action that you cannot undo comes from one tap on a small target.
- [ ] Finger-controlled objects and the main keys are about 100 px from the edge where the thumb holds the 3.49.
- [ ] No control is in the pull zone of the settings panel (the middle of the top edge).
- [ ] Slow taps (350-500 ms) and taps with a small move do what the person wants.

## 4. The behaviour

- [ ] The options of the app are panel items (`sysui_panel_items()`). A loss action is a `PI_ACT2`.
- [ ] Under the panel: the app stops (or `run = true` is correct for it). A tilt game takes the zero again in `resume`.
- [ ] Each sound is silent when the user mutes the board. `leave()` stops the drones, the tunes and the music.
- [ ] A game with its data on the SD card uses real MP3 effects (CC0) through `libs/tfsfx`, not only synthesized
  sounds ([sound.md](sound.md)); `leave()` calls `tfsfx_free()` after the music stops.
- [ ] The saves use keys with the prefix `<id>_` (15 characters at most). A save from an old version still loads.
- [ ] `awake()`, `keep_on()` and `background()` give `true` only while it is necessary.
- [ ] An idle screen does not draw busy frames (behave.md, section 9).
- [ ] Each task of the app ends in `leave()`.

## 5. The speed and the memory

- The space on the SD card is not a limit. Choose the
  better picture and the better sound: painted pictures, MP3 music, big data packs are OK. Do not make the look or
  the sound worse to save space. The limits that stay are the memory (PSRAM, internal RAM) and the speed.
- [ ] 30 fps or more on each kind in the heaviest scene. Read the log line `<APP NAME>: <x> fps` on the board.
- [ ] The PSRAM that the app needs fits the board kind with the least free PSRAM (the console `mem` at the menu).
- [ ] The memory is flat: open and close the app 20 times or more (`open <APP NAME>`, then `open MENU`). The free
      internal RAM and the free PSRAM of `mem` at the menu are the same at the start and at the end.
- [ ] The loader line `loaded <id> ...` shows a load time of some ms, and `closed <id>, PSRAM free <n>` comes after each
      close (no task keeps the app in memory).

## 6. The feel (games)

- [ ] The class of the graphics is chosen and written in the `MANUAL.md` section, with the art tools that rebuild
  the data ([graphics.md](graphics.md)); a look test went to the reviewer before the full game.
- [ ] The camera moves (zoom, shake), and the player stays in the frame.
- [ ] The tilt has its zero at the start, and small angles do something.
- [ ] Each input has its own effect.
- [ ] No wall of tutorial text.
- [ ] A game of time has wide windows (about 100 ms of human error) and one skill at a time.
- [ ] A person who does not know the rules can play it.

## 7. The signature and the store

- [ ] The package has `sig`: build it with no `--nosign` (the store key of your boards: `--sign-key <pem>`, see
      [tools.md](tools.md)).
- [ ] Install it on a board with the developer mode OFF: the state in `/api/apps` is `ok` (not `dev` or `bad_sig`).
- [ ] Install it to the board storage and to the SD card (`--push <ip>` and `--push <ip> --card`). It opens from both.
- [ ] Remove it (`apps rm <id>`, or the menu hold REMOVE), then install it again: it works, and its saves stay.
- [ ] Publish it: `python sdk/tools/mk_app.py <APP> --publish`. Then set its store state in each profile of the manager
      (STORE tab):

| Store state | What the board does |
| --- | --- |
| `hidden` | The app is not in the store of the board. The manager removes it from the board. |
| `available` | The app is in the store. The user decides. |
| `recommended` | The manager installs the app once. The user can remove it. |
| `required` | The manager installs the app and keeps it. The board does not remove it. |

A board with no profile uses the profile FRIENDS. An app with no state of its own gets the default state of the
profile (`store_default`) when the profile has one. Else: in FRIENDS it is `available`; in another profile it is
`available` when the menu of the profile has it (by name or by a rest line of its group), and `hidden` when not.

- [ ] A section for the app in `MANUAL.md` (the play, the controls, the saves, the tests).
