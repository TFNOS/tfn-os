## What changed

<!-- One or two sentences. Link the issue (for example "Closes #12"). -->

## Why

<!-- The problem or the request. -->

## How I tested it

<!-- The commands and their results, the board kinds (3.49, CoreS3, AMOLED-1.75C, AMOLED-1.8, Tab5, Watcher, or
the simulator only) and what you saw. Mark each check of CONTRIBUTING.md section 3 that you ran. -->

- [ ] `python main/apps_gen.py --check`
- [ ] `python tests/contracts/check_all.py`
- [ ] `python tests/style/check_format.py`, `check_comments.py`, `check_api.py`, `check_fsize.py`,
      `check_board_if.py`
- [ ] `CC=gcc python tests/unit/run.py`
- [ ] The all-app fuzz on the 3.49, CoreS3 and Watcher frames (`sh tests/host/build.sh`, then the `fuzz*.exe` files)
- [ ] `python tests/golden/run.py` and `python simulator/smoke.py --headless`
- [ ] The firmware build of the changed kinds (`idf.py build`)
- [ ] A test on a real board (write the board kind above)

## The effect on the boards that do not use the feature

<!-- An optional feature must have ZERO effect when it is OFF (CONTRIBUTING.md section 2.1 and section 4, rule 10).
Give the proof: the same bootloader and partition table (MD5), the image size and the internal RAM of each kind with
the feature OFF, no feature code in the OFF image (`nm`), and an update from the newest release and back on your own
board. For a fix or a change that all boards get, tell what changes for a user. -->

## Checklist

- [ ] The branch starts from a fresh `main` and has one task.
- [ ] No secrets, WiFi data, flash dumps, build output or private files (CONTRIBUTING.md section 8).
- [ ] The docs (`README.md`, `docs/`, `sdk/docs/`) are up to date when the behaviour changed.
- [ ] The feature is complete: the tools, the apps that use it and the tests are in this PR (CONTRIBUTING.md 2.1).
- [ ] Each app that the feature is for, or that uses its new API, is in the repo (CONTRIBUTING.md 2.1).
- [ ] The contract lists lose no item, or the PR tells the planned change and its migration (CONTRIBUTING.md 5).
- [ ] The ABI only gets new names, and each new name has an approved ABI request issue (CONTRIBUTING.md 6.1).
- [ ] The apps include only the public headers `main/tfn/*.h` (CONTRIBUTING.md 6.2).
- [ ] The shared code tests board capabilities, not board names (CONTRIBUTING.md 7).
- [ ] No permanent or hardware step (eFuses, secure boot, flash encryption, bootloader, partition table) without an
      agreed plan, a tested tool and the evidence from my own board (CONTRIBUTING.md 9).
- [ ] The branch is rebased on a fresh `main`, and the PR text has a review guide.

## Review guide

<!-- The files in the order of their risk, the code that runs when the feature is OFF, the edge cases that you tested,
the commands that repeat your tests, and what you could not test. -->

## Not done yet

<!-- Write "nothing". If something is not done, the PR is not ready (CONTRIBUTING.md 2.1). -->
