# The store page of an app (the description)

Each app in the store has a description. The user reads it on the app page of the board STORE (a tap on the app) and
in the STORE tab of the manager, and decides to install the app or not. This is the procedure for each app:
**no app goes into the store without a description.**

## 1. The file: store.json

Put the description in the file `store.json` in the folder of the app (the folder of its first source, for example
`apps/beeper/store.json` or `sdk/examples/wifiscan/store.json`). The key is the store id of the app (its `sym`
without `app_`). Two apps that share a folder (SUNSET is in the folder of CRUISE) have two keys in one file.

```json
{
 "beeper": {
  "en": "Tel Aviv, 1997. Take jobs from your pager and drive across a top-down city ...",
  "he": "תל אביב, 1997. ..."
 }
}
```

Use UTF-8, LF line ends and an indent of 1. Then:

- `python sdk/tools/mk_app.py <APP>` puts the texts into `manifest.txt` (the lines `desc=` and `desc_he=`; the signature
  covers them).
- `--publish` stops when the app has no English description. With no Hebrew description, it gives a note: a board in
  Hebrew then shows the English text.
- `--desc "..."` and `--desc-he "..."` replace the texts of `store.json` (for a quick test).

## 2. How to write it

- 2 or 3 short sentences, 260 characters or fewer in each language (the tools stop at 300).
- Say what the app is, what the user does in it, the controls (tilt, touch, the BOOT key) and what is special.
- Give the facts that help a person decide: English and Hebrew in the app; play with friends on 2-6 boards; it needs an
  SD card (and the size of its data); it needs WiFi or an account; violence, said plainly for parents.
- Do not say which board kinds it runs on: the store shows each app only to the boards that can run it.
- Use plain words, the present tense and the active voice. Do not use hype words (amazing, best, ultimate).
- The board fonts have only ASCII, the Hebrew letters and these marks: `׳ ״ – — ’ “ ” … ♪`. No emoji, no TAB and no line
  break. The tools stop at a character that the board cannot show.
- In Hebrew, keep the app name in its English form (BEEPER 97). The page lays out a Latin name in a Hebrew line from the
  left and mirrors the brackets, but a text with no brackets around mixed words is the easiest to read.

## 3. The way of the text

1. `mk_app.py` writes `desc=` / `desc_he=` into the manifest of the package.
2. The manager keeps the newest description of each app in `store/desc.json` (a published manifest with a description
   sets it).
3. A board with firmware 1.0.51 or newer asks for the catalog with `d=1`: two more columns, the English and the Hebrew
   text. The STORE app shows them on the app page, in the language of the panel item LANGUAGE (the default: Hebrew).
4. An older firmware gets no descriptions and keeps only the first 64 apps of the catalog. The manager sends it the
   required apps, the apps that the board has and the recommended apps first.

## 4. A change of the text with no new version

- Change `store.json`, then send all the descriptions to the store: `python sdk/tools/store_desc.py --send` (the
  manager comes from the store setup of `sdk/tools/store_conf.py`, for example the environment variable
  `TFN_MANAGER`, or give `--manager http://<manager>:<port>`). With no
  `--send`, the tool only checks: it lists each app of the store that has no description or a fault (exit 1).
- Or use EDIT under the app in the STORE tab of the manager. Then copy the text into `store.json` too: the next package
  of the app sets its own text.

## 5. An app of another developer (APPS & SIGN)

A package with no `desc=` gets a warning in the check of APPS & SIGN. Give the description in `store.json` (or with
`--desc`) and make the package again, or write it with EDIT in the STORE tab after PUT IN THE STORE.

## 6. The developer key and the trusted publishers

A developer can sign a package with an own P-256 key. The signature tells the board who made the package. It does not
replace the review of the store: the store signature (`sig`) stays the signature that a board trusts by default.

### Make a developer key

```
openssl genpkey -algorithm EC -pkeyopt ec_paramgen_curve:P-256 -pkeyopt ec_param_enc:named_curve -out my_dev.pem
```

The key must have the named curve (`ec_param_enc:named_curve`): with no option, some openssl programs (for example
LibreSSL on macOS) write the curve in full, and the tools refuse that key. An openssl with no `ec_param_enc`:
`openssl ecparam -name prime256v1 -param_enc named_curve -genkey -noout -out my_dev.pem`.

Keep `my_dev.pem` private: never put it into git or into a package. Give the public part only (`publisher.txt`).

### Sign a package with it

```
python sdk/tools/mk_app.py MYAPP --kinds lcd349 --dev-key my_dev.pem --dev-name MY NAME --nosign
python <sdk>/tools/tfn_app.py build my_app --kinds lcd349 --dev-key my_dev.pem --dev-name MY NAME
```

`--dev-key` adds these parts (the other options do not change):
- `publisher=<key ID>` in `manifest.txt` (the key ID = the first 8 bytes of the SHA-256 of the 65-byte public key, as
  16 hex digits);
- the file `sigs` next to `sig`: one line `TFNSIG2 <key ID> <r || s>` (hex), the signature of the key over
  `manifest.txt`;
- `out/<id>/publisher.txt`: the key file for a board (`TFNPUB1`, `name=`, `flags=dev`, `key=` the public key).

`--dev-name` is the name of the key on the screen of a board (1 to 15 letters: A-Z a-z 0-9 space - _ .; default DEV).

### What a board does with it (SETUP > TRUSTED PUBLISHERS)

The board owner adds a key ON THE SCREEN of the board. A key comes from a file in `/sdcard/pub/` (SETUP > TRUSTED
PUBLISHERS > ADD), or from `POST /api/pub?op=add` (the body = the key file). The board shows the key ID in 4 groups
and the name; nothing changes until the board owner taps ALLOW. A prompt stays 2 minutes. A remove comes from the
screen, the console (`pub rm <key ID>`) or `POST /api/pub?op=rm&id=<key ID>` (only with a web password).

Each added key has flags:

| Flag | What the board does |
| --- | --- |
| `store` | Its signature in `sig` is enough (as the built-in store key). |
| `dev` | Its signature in `sigs` names the developer, but the app also needs a store signature. |
| `test` | Its signature in `sigs` is enough: for the own tests of the board owner. |

So an app is OK on a board when `sig` verifies with a built-in key or a `store` key, or when a `sigs` line of a `test`
key verifies. `publisher.txt` has `flags=dev`; for the own tests of a board owner, change it to `flags=test` before
the add. With only a `dev` signature, the board shows the app as **BY <name>, NOT REVIEWED** and does not run it
(it runs only in the developer mode of the board). A board with no added key works as before.

### The store review (the countersign)

Send the package (with `sigs`) to the owner of the store. The owner reviews the source and signs the same
`manifest.txt` (the file `sig`, APPS & SIGN). The manifest names the developer key, so the store signature says "this
code of this developer is reviewed". The store keeps and sends both files (`sig` and `sigs`). An older firmware reads
only `sig`, and an older board does not take the file `sigs`: the tools and the manager skip it there with a note.

Only the built-in keys of the firmware sign the permission policy. An added key never does.

## 7. The static store (a web host with no server)

The public store of TFN OS is a folder of plain files on a web host (GitHub Pages and the files of each GitHub
release): `sdk/tools/store_index.py` makes it, and `.github/workflows/release.yml` makes it for each release.

```
python sdk/tools/store_index.py make <packages> --out <store> --key <store key .pem> [--release V] [--kinds lcd349,cores3]
python sdk/tools/store_index.py verify <store> --pub <public key: .pem or 130 hex digits>
```

`<packages>` is the output folder of `tfn_app.py build` or `mk_app.py` (`<id>/<kind>/`). The store has:

| File | What it is |
| --- | --- |
| `index.json` | The apps: id, name, version, group, the descriptions, and for each kind the files with their size and SHA-256, the ABI, the permissions and a zip of the package. Also the store key (`pubkey`, `key_id`). |
| `index.sig` | 64 bytes: the ECDSA P-256 / SHA-256 signature (r, then s) of the bytes of `index.json`. |
| `apps/<id>/<kind>/` | `manifest.txt`, `app.elf`, `icon.bin`, `sig` (and `watch.elf`): the package as the board installs it. |
| `apps/<id>/<id>-<kind>.zip` | The same files in one zip, for a person. |

`make` signs each `manifest.txt` again with the store key (the file `sig`: the form that the board checks). The store
takes no package of a developer key (`publisher=`): the store signs only the packages that it builds. The signature of
the index covers the versions and the SHA-256 of each file, so a host cannot change a file or give an old version with
no fault in `verify`. The board trusts the store key only when its firmware has it built in
(`TFN_STORE_PUBKEY_BYTES`): the release build puts the public value of the official store key there.

The web page `store.html` of the site reads the index, checks its signature, and gives the zip of an app after a check
of its size and SHA-256. The person installs the files with the web page of the board (`/apps`, "Install an app
file") or from the SD card (FILES > INSTALL). The STORE app of the board reads only a manager now.
