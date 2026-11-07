# Instructions for coding agents (and the people who use them)

These rules apply to each coding agent (Claude Code, Codex, Cursor and others) that works in this repo, and to the
person who uses it. They add to [CONTRIBUTING.md](CONTRIBUTING.md): read it first. When the two files do not agree,
CONTRIBUTING.md is correct.

## 1. The branch rules

- Do not commit to `main` and do not push to `main`. Do all work on a branch of your fork, from a fresh `main`
  (CONTRIBUTING.md section 2). Use one of these types: `feature/`, `fix/`, `docs/`, `chore/`.
- Keep one task on one branch. Keep the commits small, and give each commit a clear message.
- To give your work to `main`, open a pull request (PR). A maintainer reviews it and merges it. Do not merge your own
  PR.
- Never force-push `main`, never rewrite its history, and never push to the branch of another person.

## 2. What must never go into a commit

Before each commit, read the staged diff (`git diff --cached`). Stage the files by name (`git add <files>`), never
with `git add -A` or `git add .`.

Never commit:
- passwords, tokens, keys, WiFi names and passwords, home IP addresses, private paths;
- flash dumps and backups of a board (they hold the WiFi settings), logs, build output (`build*/`), `sdkconfig*`
  (only the `sdkconfig.defaults*` files are in git);
- data that you may not share: fonts, music, pictures or game data with no open licence (CONTRIBUTING.md section 10).

The `.gitignore` file covers most of these. Do not remove an entry from it without a reason in the PR.

## 3. The boards

- Flash only a board that you own, by USB on your own computer.
- A new feature keeps the current behaviour of the boards by default (OFF, or the same values as before). An optional
  feature has ZERO effect on the boards that do not turn it on (section 5.3).
- Before a change of a web handler, an `/api` answer, the access to the board web server, the network behaviour, the
  OTA, the app files or the console commands, find each tool that uses it (the web flasher in `site/`, the SDK tools
  in `sdk/tools/`, the tests in `tests/contracts/`). Keep them working in the same PR.

## 4. How to test before a PR

Run the checks of CONTRIBUTING.md section 3 that your change touches. The short list:
- `python main/apps_gen.py --check` and `python tests/contracts/check_all.py`;
- `python tests/style/check_format.py --fix <files>` (clang-format 22.1.8), then `check_comments.py`,
  `check_api.py`, `check_fsize.py`, `check_board_if.py` in `tests/style/`;
- the all-app host fuzz when you change shared code: `sh tests/host/build.sh`, then `fuzz349.exe 300 1`,
  `fuzzs3.exe 300 1` and `fuzzwt.exe 300 1` in `tests/host/`;
- `python tests/golden/run.py` and `python simulator/smoke.py --headless`;
- an app file: `python sdk/tools/mk_app.py ...` (see `sdk/README.md`).

Write in the PR what you tested, and on which board kind.

## 5. When a PR is ready for review (and when an agent must stop)

### 5.1 A PR is a complete feature

- A PR has all the parts that the feature needs to work end to end: the code, the tools, the apps that use it, the
  docs and the tests. If a part is not done, the PR is not ready: keep the work on your branch.
- If a feature is for an app, or an app uses a new API of the feature, that app goes into the repo (`apps/<app>/`,
  its entry in `main/apps.json` and its `store.json`). Shared code must not depend on an app that is not in the repo.

### 5.2 Permanent and hardware changes

eFuses, secure boot, flash encryption, the ROM download mode, the bootloader, the partition table and a move of NVS
cannot be undone. Open an issue with the plan first (the steps, who keeps the keys, the recovery). The PR includes a
tested tool that does each step in the correct order and stops at a failure, and the evidence from your own board
(CONTRIBUTING.md section 9).

### 5.3 An optional feature has ZERO effect when it is OFF

- Put the feature code behind its config option, so that it compiles out. A hook in shared code becomes an empty
  inline function when the option is OFF.
- Prove it in the PR: with the feature OFF, the bootloader and the partition table are the same (MD5), the image size
  and the internal RAM are the same (`idf.py size`), and the feature code is not in the image (`nm`).

### 5.4 Make the review easy

- One task in one PR. Rebase on a fresh `main` and fix the conflicts yourself. For the ABI, use the next free rev.
- Add a review guide to the PR text: the files in the order of their risk, the edge cases that you tested, the
  commands that repeat your tests, and what you could not test.
- Keep the code style of the repo: one statement on a line, comments and docs in simple English (CONTRIBUTING.md
  section 4.1).

### 5.5 The agent checks the PR first, and stops when necessary

Before `gh pr create`, and before a draft becomes ready for review:
1. Check the PR against 5.1 to 5.4.
2. If the PR would make the reviewer continue the work, write a missing tool or app, or give a board to a permanent
   change, do NOT open it. Tell the developer which rule the PR does not meet and which work is still necessary.
3. For an early talk about a plan, open an issue, not a PR.

## 6. Where things are

- `README.md`: the tour of the project. `docs/`: the docs of the OS. `sdk/`: the app SDK (`sdk/docs/howto.md` tells
  how to make each kind of app).
- `main/`: the OS. `main/tfn/`: the public API headers. `boards/<kind>/`: the capabilities of each board.
  `apps/<app>/`: one folder for each app. `libs/<module>/`: the code that more than one app uses.
- `main/apps.json` is the one list of the apps; `main/apps_gen.py` makes the other lists from it (do not edit those by
  hand). A new app: a new folder in `apps/`, one entry in `main/apps.json`, and a `store.json` with the store text in
  English and Hebrew (`sdk/docs/store.md`).
- `simulator/`: the desktop simulator of the 3.49. `tests/`: the checks of CI.
