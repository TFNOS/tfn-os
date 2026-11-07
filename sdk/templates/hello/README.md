# HELLO: the template of a TFN OS app in its own repo

This folder is a complete app repo. It builds with the TFN OS SDK package only: it needs no file of the TFN OS repo.

| File | What it is |
| --- | --- |
| `app_hello.c` | The source: the smallest complete app (a ball that goes to each tap, and a count of the taps). |
| `app.json` | The manifest of the app ([app_json.md](../../docs/app_json.md)). |
| `build.sh` | The build with the SDK package: `sh build.sh <tfn-sdk.zip or the package folder> [kinds]`. |
| `.github/workflows/build.yml` | The build on GitHub on each push (the ESP-IDF container, ubuntu-latest). |
| `.gitignore` | The build output (`out/`) stays out of git. |

## Make your app

1. Copy this folder to a new folder, and make it a git repo (`git init`).
2. Use an id of your own. Change these names together:
   - `name` in `app.json` and `.name` of the `app_t` (the menu name);
   - `sym` in `app.json` and the name of the `app_t` (`app_<id>`);
   - the file name `app_hello.c` and `srcs` in `app.json`.
3. Write the app ([app_repo.md](../../docs/app_repo.md), [api.md](../../docs/api.md)).

## Build on your computer

You need ESP-IDF at the version of the SDK package (v6.1) with the compilers of the kinds
(`install.sh esp32s3,esp32p4`), Python 3.8 or newer, and the SDK package `tfn-sdk.zip`.

```
. $IDF_PATH/export.sh
sh build.sh tfn-sdk.zip lcd349
```

The packages go to `out/<id>/<kind>/` (`app.elf`, `icon.bin`, `manifest.txt`). To send one to a board in the
developer mode, use `tfn_app.py` of the package with `--push <board ip> --open` ([app_repo.md](../../docs/app_repo.md),
section 4).

## Build on GitHub

Push the repo to GitHub. The workflow `build` gets `tfn-sdk.zip` of the newest TFN OS release and builds the app for
each kind of the package. The packages are the artifact `app` of the run. To use another SDK package, set the repo
variable `TFN_SDK_URL` to its address.
