# app.json: the manifest of an app in its own folder

An app in its own folder has one file `app.json` next to its sources. `sdk/tools/tfn_app.py build <folder>` reads it
and makes the package of the app for each board kind ([app_repo.md](app_repo.md)). The file holds the fields of the
app entry in `main/apps.json` that an app file needs, the store text, and the kinds.

`sdk/tools/app_json.py` writes `app.json` for an app of the repo (from `main/apps.json` and its `store.json`). In
the repo, `main/apps.json` and `store.json` are the source: when you change them, run `app_json.py <APP>` again. CI
runs `python sdk/tools/app_json.py --check`: each `apps/<app>/app.json` must agree with them (`version`, `icon`,
`abi_rev`, `about`, `defines` and `id` can be in `app.json` only).

## The fields

| Field | Type | Need | What it is |
| --- | --- | --- | --- |
| `tfn_app` | number | yes | The version of this format: `1`. |
| `name` | text | yes | The menu name. It must be the `.name` of the `app_t`. |
| `sym` | text | yes | The name of the `app_t` in the code: `app_<id>`. The store id is `<id>` (small letters, digits, `_`). The saves of the app start with `<id>_`. |
| `id` | text | no | The store id. When you give it, it must be `sym` without `app_`. |
| `group` | text | no | `GAMES` (the default) or `UTIL`: the page of the menu. |
| `emoji` | text | no | The icon of the app in the manager. |
| `version` | text | no | The version of the package (`1.0.0` when you do not give it; `--ver` replaces it). |
| `srcs` | list of text | yes | The sources (`.c`, `.cc`, `.cpp`), relative to the folder of `app.json`. The tool compiles each one. A source can include more files of the folder (`.inc`, `.h`). |
| `defines` | list of text | no | More `-D` options for each source, for example `"MY_DEBUG=1"`. |
| `link` | list of text | no | Prebuilt libraries (`.a`) that the app file links, relative to the folder. A library is for one CPU (Xtensa: all kinds but `tab5`; RISC-V: `tab5`). |
| `watch` | object | no | A background check (`watch.elf`, [notify.md](notify.md)): `srcs` (a list), `sym` (the `watch_t`), `every` (the minutes between two checks, 15 by default). |
| `new` | true/false | no | The menu puts the app first, with its NEW pill. |
| `form`, `form_help`, `oauth` | text | no | The account form of the app on the board page (as in `main/apps.json`). |
| `class` | text | no | `"b"` for a class B game (the manager does not install it by itself). |
| `kinds` | list of text | no | The board kinds of the app: `lcd349`, `cores3`, `am18`, `watcher`, `tab5`, `am175`. When you do not give it: all kinds of the SDK package. `--kinds` replaces it. |
| `idf` | text | no | `"min"` (the default): the ESP-IDF include folders that the public headers and the usual ESP-IDF headers need. `"all"`: all include folders of ESP-IDF, for an app with raw ESP-IDF names ([raw.md](raw.md)). |
| `store` | object | no | The store text: `{"en": "...", "he": "..."}` ([store.md](store.md): the rules of the text, 300 characters at most, only the characters of the board fonts). |
| `icon` | text | no | A PNG file for the menu icon (128 x 128 is best; another size is scaled), relative to the folder. A pixel with an alpha under 128 is a hole. When there is no icon, the tool makes a plain tile with the first letter of the name. `--icon` replaces it. |
| `abi_rev` | number | no | The newest ABI revision that the app can need. The build stops when the app calls a newer name (the app would need a newer firmware than you expect). |
| `about` | text | no | A note for people. The tools do not read it. |

The tool stops at a field that is not in this list (a typing error).

The fields of `main/apps.json` that are not here are for the firmware build (`on`, `psram`, `uses`, `test349`,
`tests3`, `core`, `guard`, `requires`, `private`, `host`). The `app_t` flags of the manifest (`landscape`, `native`,
`round`, `round_fit`, `wheel`, `key`, `run`, `needs`) come from the `app_t` in `app.elf` of each kind.

## Example

```json
{
 "tfn_app": 1,
 "name": "COUNTER",
 "sym": "app_counter",
 "group": "UTIL",
 "emoji": "🔢",
 "version": "1.0.0",
 "srcs": ["app_counter.c"],
 "kinds": ["lcd349", "cores3"],
 "store": {
  "en": "A tally counter with big keys. Hold RESET for 1 s to start again.",
  "he": "מונה עם מקשים גדולים. החזיקו RESET שנייה אחת כדי להתחיל מחדש."
 },
 "icon": "icon.png"
}
```
