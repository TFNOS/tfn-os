# How to contribute to TFN OS

Thank you for your help. This file tells you how to give a change to TFN OS: the branch and pull request (PR) rules,
the checks to run, the code standard, and the rules of the app ABI, the public API and the boards.

Each person in the project obeys the [code of conduct](CODE_OF_CONDUCT.md). To report a security fault, do not open
an issue: read [SECURITY.md](SECURITY.md). [GOVERNANCE.md](GOVERNANCE.md) tells who decides. A coding agent (an AI
tool that writes code) also obeys [AGENTS.md](AGENTS.md).

## 1. Before you start

- For a small fix, you can open a PR at once.
- For a new feature, a new board, a change of a contract (section 5) or a change of the ABI (section 6), open an
  issue first. Use the issue form of the topic: a bug, a board request, an app idea or an ABI request. The maintainer
  can then tell you if the change fits the project, before you do the work.
- For a permanent or hardware change (section 9), the issue with the plan is necessary.

## 2. The branch and pull request rules

1. Fork the repo. Make a branch from a fresh `main` for each task:
   ```
   git fetch upstream
   git switch -c <type>/<short-name> upstream/main
   ```
   Use one of these types: `feature/` (a new thing), `fix/` (a bug), `docs/`, `chore/` (tools, build, clean-up).
   Example: `fix/wifi-reconnect`.
2. Keep one task on one branch. Keep the commits small. Give each commit a clear message that tells what changed and
   why.
3. Stage the files by name (`git add <files>`), not with `git add -A` or `git add .`. Read the staged diff
   (`git diff --cached`) before each commit (section 8).
4. Run the checks of section 3 on your computer.
5. Push your branch to your fork, and open a PR into `main`. Fill in all parts of the PR form.
6. The maintainer (or a reviewer, see GOVERNANCE.md) reviews the PR. Only the maintainer merges it. Do not merge your
   own PR.
7. When `main` moves on, rebase your branch on it (`git rebase upstream/main`), fix the conflicts yourself, and push
   with `git push --force-with-lease`. Do this only on your own branch.

Never force-push `main`, and never push to the branch of another person.

### 2.1 When a PR is ready for review

- **A PR is a complete feature.** It has all the parts that the feature needs: the code, the tools, the apps that use
  it, the docs and the tests. If a part is not done, keep the work on your branch. Do not open a PR for another person
  to finish. A feature covers `main` as it was when you started. When `main` gets a new app while you work, the
  maintainer adds your feature to it: tell in the PR text how a new app must use your feature.
- **The apps of a feature are in the repo.** If a feature is for an app, that app goes into the repo, in the same PR
  or in an earlier one. Shared code must not depend on an app that is only on your computer.
- **An optional feature has zero effect when it is OFF** (section 4, rule 10). Prove it in the PR text: the same
  bootloader and partition table (MD5), the same image size and internal RAM on all kinds with the feature OFF, no
  feature code in the OFF image (`nm` of the ELF file), the fuzz on all frames. If you have a board, update it from
  the newest release to your OFF build and back, and check the console, FILES and SETUP.
- **Make the review easy.** One task in one PR. Split big work into stages; each stage is complete and safe alone.
  Add a review guide to the PR text: the files in the order of their risk, the code that runs when the feature is
  OFF, the edge cases that you tested, the commands that repeat your tests, and what you could not test.
- A shared fix that you find on the way (a race, a tool fault) is welcome. Put it in its own small PR.

## 3. The checks before a push

CI (`.github/workflows/ci.yml` and `sim.yml`) runs these checks for each push and each PR. Run them on your computer
first. You need Python 3.10 or later, and clang or gcc for the host tests.

| Check | Command | CI job |
|---|---|---|
| The app list | `python main/apps_gen.py --check`, and `python main/apps_gen.py --all` must not change `main/apps_all.inc` | ci: checks |
| The contracts (section 5) | `python tests/contracts/check_all.py` | ci: checks |
| The licences | `python tests/licence/check_notice.py` | ci: checks |
| The web flasher | `node site/test/flasher.test.js` | ci: checks |
| The clean-up proof tool | `python tests/refactor/test_same_code.py` | ci: checks |
| The unit tests of the core (ASan, UBSan) | `CC=gcc python tests/unit/run.py` | ci: checks |
| The comments | `python tests/style/check_comments.py` | ci: checks |
| The board names (section 7) | `python tests/style/check_board_if.py` | ci: checks |
| The public API (section 6.2) | `python tests/style/check_api.py` | ci: checks |
| The API reference of the SDK is current | `python sdk/tools/gen_api_doc.py --check` (to make it again: no `--check`) | ci: checks |
| The function size | `python tests/style/check_fsize.py` | ci: checks |
| The format | `python tests/style/check_format.py` (clang-format 22.1.8) | ci: checks |
| The secret scan | gitleaks 8.28.0 with `.gitleaks.toml` (the files and the new commits) | ci: secrets |
| The all-app fuzz (ASan, UBSan) | `sh tests/host/build.sh`, then `fuzz349.exe 300 1`, `fuzzs3.exe 300 1` and `fuzzwt.exe 300 1` in `tests/host/` | ci: host-fuzz |
| The golden frames and tunes | `python tests/golden/run.py` (clang) | simulator |
| The smoke test of all apps | `python simulator/smoke.py --headless --seconds 1` | simulator |
| No sound reads the memory of an app after its free | `python simulator/sound_check.py` | simulator |
| The firmware build of each kind | `idf.py build` (ESP-IDF 6.1) | firmware (a person starts it by hand) |

Notes:
- On Linux and macOS, `JOBS=8 sh tests/host/build.sh` compiles in parallel, and `ASAN_OPTIONS=detect_leaks=0` stops
  the leak report at the end of a fuzz run.
- The golden frames need clang, because gcc calls the arguments of a function in another order. When your change must
  change a picture, look at `tests/golden/out/*.diff.png`, then run `python tests/golden/run.py --update` and commit
  the new PNG files with your change.
- The simulator (`python simulator/run.py`, see `simulator/README.md`) is the fast way to try a change. It does not
  replace a test on a board.
- When you change a file in `main/`, compile it for the board kinds: `python tests/host/espcheck.py <file.c>`.
- An app that installs from the store: build it with `python sdk/tools/mk_app.py` (see `sdk/README.md`).
- The firmware workflow (`.github/workflows/firmware.yml`) builds all kinds from a clean clone in the
  `espressif/idf:v6.1` container. You can start it in your fork (Actions > firmware > Run workflow).

### 3.1 Local CI

The runner minutes of GitHub Actions are limited. `tests/ci_local.py` runs the steps of `ci.yml`, `sim.yml` and
`sdk.yml` on your computer with one command.

- Before each merge, run the fast steps: `python tests/ci_local.py --quick` (the format, the app list, the contracts,
  the unit tests, the comments, the board names, the public API, the API reference, the function size,
  `boards.json`).
- Before a release, run all steps: `python tests/ci_local.py`. The full run adds the licences, the web flasher, the
  clean-up proof tool, gitleaks, the host fuzz (3000 frames on 3 frames), the simulator build of 5 frames, the
  golden frames and tunes, the smoke test, the check of the sound memory, the SDK package of 6 kinds and the build of
  the app template.
- Use `--only NAME,..` or `--skip NAME,..` to select the steps. `--list` shows the names.
- Each step prints one line (OK, FAIL or SKIP, and the seconds). The last line is `CI LOCAL: OK (n steps)` or
  `CI LOCAL: FAIL (names)`. The exit code is 0 or 1. The full output of each step is in `tests/host/ci_local/`.
- The simulator steps and the SDK steps use a clean worktree of `HEAD` in the temp folder of the system, because a
  private file of your working tree can change the golden menu frames. Commit your change before the full run.
- The tools: clang-format 22.1.8 (`CLANG_FORMAT`), WSL with gcc for the unit tests (else SKIP), the bash of Git for
  Windows and clang for the host fuzz, Zig for the simulator (`python simulator/build.py --setup`), ESP-IDF 6.1 for
  the SDK steps (`TFN_IDF`), Node.js for the web flasher, and gitleaks (else SKIP: `winget install Gitleaks.Gitleaks`).
  The start of `tests/ci_local.py` tells each environment variable.

## 4. The code standard

The rules apply to new code and to the code that you change. The mark **(CI)** shows the rules that CI enforces.
The other rules are for the review.

1. **Format (CI: `tests/style/check_format.py`).** The code follows `.clang-format`: Linux braces, 120 columns, no
   short `if`, loop, block or function on one line, `case` at the level of `switch`. CI uses clang-format 22.1.8
   (`pip install clang-format==22.1.8`, or set `CLANG_FORMAT` to the path of that binary). Another version formats a
   little differently. Before a push, run `python tests/style/check_format.py --fix <files>`. Give it the files: with
   a folder name, it does not look inside the folder. Put a hand-made table between `// clang-format off` and
   `// clang-format on`. A file that a tool makes goes into `.clang-format-ignore`. The vendored folders keep their
   own format.
2. **Braces (CI: `tests/style/check_format.py`, `InsertBraces`).** Each body of `if`, `else`, `for`, `while` and
   `do` has braces, and the body starts on a new line. Write one statement on a line.
3. **Names.** A public function has the prefix of its module: `module_verb_noun()`. A type is `snake_case_t`. A macro
   and an enum value are `UPPER_CASE` with the prefix. A file-scope variable is `s_name`. Do not add new `g_` global
   variables.
4. **Files.** One module is one `.c` file and one `.h` file. An `.inc` file holds only generated data. Do not write
   `extern` in a `.c` file: put the declaration in a header.
5. **Size (CI: `tests/style/check_fsize.py`).** A function has 60 lines as the aim and 150 lines as the limit. A new
   function over 150 lines fails CI. `tests/style/fsize_allow.txt` lists the old long functions: a listed function
   must not grow, and the list only gets shorter. A file has 1 500 lines as the soft limit. Use not more than 4
   levels of nesting.
6. **State.** Keep the state of a module in one `static struct`, not in many loose static variables.
7. **Numbers.** A system screen takes its colours from the theme (`main/tfn/theme.h`, the `THEME_<SCREEN>_<ROLE>`
   names), not from colour numbers. Take the screen positions from a layout struct for each frame shape. Give each
   protocol or register number a name, near the reference to its datasheet.
8. **Board kind (CI: `tests/style/check_board_if.py`).** Shared code tests a capability, never the name of a board
   (section 7).
9. **No copies.** Put code that two modules need into one shared module (`libs/` for the apps, a core module for
   the OS). A new shared helper gets a unit test in `tests/unit/` against the code that it replaces.
10. **Optional features.** Put an optional feature behind a Kconfig option. It is OFF by default. It has zero effect
    on a board that keeps it OFF: the bootloader, the partition table, the settings, the behaviour, the web API, the
    console, the flash, the internal RAM and the speed stay the same. A hook in shared code becomes an empty inline
    function when the option is OFF.
11. **The defaults stay.** A new feature keeps the current behaviour of the boards by default. The user turns it on
    in SETUP.

### 4.1 Comments and documents (CI: `tests/style/check_comments.py`)

- A comment tells WHAT the code does and WHY, for the code as it is now.
- Do not write dates, quotes of a person, or history ("before", "was", "old", "no longer"). History goes into the
  commit message.
- Do not write names of persons or of their boards, host names, IP addresses, serial port names, private folders or
  private projects.
- A test reference names a file in `tests/`.
- The header of a module starts with 3 to 6 lines: the job of the module, the task that calls it, and the owner of
  its memory. Each public function in a header has a one-line comment.
- Write comments, documents and commit messages in short, simple English. ASD-STE100 (Simplified Technical English)
  is the base: one meaning for each word, the active voice, short sentences.
- Keep the licence and source notice of copied code.

The check finds four kinds of fault: a date, a quote of a person, a private place, and a note about the work of an AI
agent.

### 4.2 A clean-up commit proves that the code stays the same

A commit that changes only comments, white space or braces must not change the compiled code. Prove it with
`tests/refactor/same_code.py`, and put its last line in the commit message:

- `python tests/refactor/same_code.py text` for comment-only changes (no compiler: it removes the comments and
  compares the rest).
- `python tests/refactor/same_code.py objects` for format changes. It compiles the base version and your version of
  each changed file, and of each file that includes a changed header, with the compile commands of a DEV build
  (`build_dev/<kind>/compile_commands.json`). `__LINE__` is 0 and there is no debug data, so the objects of a format
  change are the same byte for byte. A file that compiles in neither version is listed, not proved.

## 5. The contracts

These lists are contracts. The boards, the installed app files and the tools that read a board use them:

- the ABI names of each board kind (`main/tfn_abi.json`, `main/tfn_abi.inc`);
- the NVS namespaces and keys (the settings of each user);
- the HTTP routes of the board web server;
- the console commands and their words.

`tests/contracts/check_all.py` fails when an item of a list is lost. A new item is good: run
`python tests/contracts/check_all.py --update` and commit the baseline files with your change. Do not remove or
rename an item by accident. A planned change needs a migration (for example, read the old key one time) and
`--update --accept-loss`. Tell the change in the commit message and in the PR. Open an issue first for a planned
loss.

## 6. The app ABI and the public API

### 6.1 The ABI is add-only

An app file calls only the names of the ABI of the firmware (`main/tfn_abi.json`; `sdk/docs/api.md` and
`sdk/docs/raw.md` tell more).

- Inside one ABI version, the list only gets new names. Each addition gives the next revision. An app that calls a
  name of revision N runs only on a firmware with revision N or newer.
- Never remove a name, and never change the arguments, the return type or the meaning of a name. The installed apps
  use them. To rename a function, keep the old name as an alias: `TFN_ABI_ALIAS(old, new)` (`main/tfn_abi_alias.h`).
- A removal is possible only in a new ABI version. Only the maintainer decides a new version.
- **An ABI request is an issue.** When `sdk/tools/mk_app.py` stops because your app needs a name that the firmware
  does not have, it writes `sdk/out/<id>/abi_request.json`. Open an issue with the form "ABI request" and give: the
  names, the app that needs them (a link to its source), the reason, and the permission class that you propose. Do
  not add the names to the ABI files in a PR before the maintainer approves the request.
- A name that can do harm (the WiFi settings, the pins, the radio, the settings store ...) has a permission class.
  The store of the board asks the user before the install.

### 6.2 The public API (CI: `tests/style/check_api.py`)

- The public API of TFN OS is the headers in `main/tfn/`. An app includes them by this name: `#include "tfn/gfx.h"`.
- A file of `apps/`, `libs/`, `sdk/examples/` and `sdk/templates/` includes only public headers (`main/tfn/*.h`).
  It includes no other file of `main/`: no OS header, no `.c` file and no `.inc` file of the OS.
- A public header includes only public headers, the files of `boards/<kind>/`, and the C, ESP-IDF and FreeRTOS
  headers.
- An app includes a lib of `libs/` in the SDK form: `#include "tfdraw/tfdraw.h"`. Do not write the path in the repo
  (`"../../libs/tfdraw/tfdraw.h"`) or the bare name (`"tfdraw.h"`). The same include works in an app out of the repo
  (`sdk/docs/app_repo.md`). A file of `libs/` can include another lib file by a relative path.
- `tests/style/api_allow.txt` lists the old places that still include an OS file. A new place fails CI. The list only
  gets shorter.
- If your app needs a function of the OS that is not public, open an issue. Do not include the OS header.

## 7. The boards

- The capabilities of each board are in `boards/<kind>/board_caps.h` (the screen and the UI, the sensors and the
  input, the chips, the radio, the sound, the system). `main/board.h` includes the file of the board of the build.
- Shared code (`main/` except the board layer, `libs/`, `apps/`) tests a capability (`BOARD_ROUND`,
  `BOARD_UI_TABLET` ...), never the name of a board or the Kconfig option of a board kind. CI fails on a new test of
  a board name (`tests/style/check_board_if.py`).
- A new capability is a new name in the `board_caps.h` of ALL boards, with its own value on each board (0 = no).
- A file that tests a capability includes `board.h` (or a header that includes it) first. Else the name has no
  value, and the test is always false.
- A new board is a new folder `boards/<kind>/` with its `board_caps.h` and its driver. The shared code does not
  change. Open a board request issue first.

## 8. Secrets and private data

Never commit:
- passwords, tokens, keys, WiFi names and passwords, home IP addresses, private paths;
- flash dumps and backups of a board (they hold the WiFi settings), logs, build output (`build*/`), `sdkconfig*`
  (only the `sdkconfig.defaults*` files are in git);
- data that you do not have the right to share (fonts, music, pictures, game data of other owners).

The `.gitignore` file covers most of these. CI scans the files and the new commits with gitleaks (`.gitleaks.toml`).
A key that you push by accident is public: change the key at once, then tell the maintainer.

## 9. Permanent and hardware changes

These changes cannot be undone: eFuses, secure boot, flash encryption, the ROM download mode, the bootloader, the
partition table, a move of NVS. A mistake can stop a board, or stop its updates.

- Open an issue with the plan before the code: the steps, the trust model (who keeps the keys), and the recovery when
  a key or a board is lost.
- The PR includes a tested tool that does each step in the correct order, checks each step and stops at a failure. A
  document alone is not enough.
- Test it on your own board, and put the evidence in the PR (the commands, the logs, the results).
- Tell which boards can get the change, and how (USB only, or an update over the air). A board must still get
  updates after the change.

## 10. Third-party code and data

- Put vendored code in its own folder, with its licence file.
- Add an entry to `NOTICE.md` for each third-party part: the files, the source, the copyright and the licence. A font
  that you convert to glyph data needs an entry too, and its licence file in `docs/licenses/` (CI:
  `tests/licence/check_notice.py`).
- Do not add data that you do not have the right to share.

## 11. The licence of your contribution

TFN OS uses the MIT licence ([LICENSE](LICENSE)). When you give a contribution, you agree that it has the same
licence, and you confirm that you have the right to give it.
