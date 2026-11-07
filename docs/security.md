# The security model of TFN OS

This page tells what TFN OS protects now, what it does not protect, and the target design. Each part says clearly
what is DONE and what is NOT DONE. To report a fault, read `SECURITY.md`.

## 1. The trust model now

TFN OS trusts the WiFi network of the board. A device on the same network can reach the web server of the board,
and a person near the board can reach its radio (ESP-NOW, Bluetooth, the hotspot). Use TFN OS on a network that you
trust, and set a web password.

| Part | The protection now | State |
| --- | --- | --- |
| App packages | An ECDSA P-256 signature of the store key over the manifest; the manifest has the SHA-256 of each file | DONE |
| The store key | One public key in the firmware (`TFN_STORE_PUBKEY_BYTES`, `main/tfn_defaults.h`). With no key, the board installs no signed app | DONE |
| The static store | `index.json` with the size and the SHA-256 of each file, signed by a trusted STORE key (`index.sig`), over HTTPS (`main/tfn_sstore.h`) | DONE |
| The developer mode | Off by default. When it is on, app packages with no signature can run. Only a person at the board turns it on: SETUP > DEVELOPER MODE asks first, and only a finger on the glass answers (a tap of the web page or of the console does not count), or the USB console line `apps dev 1`. The web can only turn it off | DONE |
| The permission classes | A name of the ABI that can do harm has a class. The STORE asks the user; the loader links the name only for an app with that class. The web install does not ask (section 2.5) | PART |
| The permission policy | A manager can publish a newer policy, signed with the store key | DONE |
| The web password | Optional, off by default (section 2) | DONE, but OFF by default |
| The firmware update (OTA) | The board tag, the image hash and the rollback. NO signature | PART |
| The hotspot | An open network (no WPA2) | NOT DONE |
| The radio between boards | With no pool: a magic number and a version in each packet; the friends by MAC; no proof of the sender. In a pool: link v4 (a pool tag, a counter, a SipHash tag), the encrypted beacon, the STRICT rules ([pools.md](pools.md)) | PART (the pools) |
| Device protection | An option for the 3.49 only, off by default (`sdk/docs/device-protection.md`) | DONE, optional |

## 2. What the code does now

### 2.1 The app packages

`main/tfn_install.c` checks each package before the install: the board kind, the signature (`sig`) with the store
key, the ABI version and revision, and the SHA-256 of `app.elf` and `icon.bin` against the manifest. A package that
fails a check does not install. The store key is a build value: a build reads the git-ignored `main/tfn_local.h` first
(`main/tfn_local.example.h` shows the form). The person who runs a store and its boards makes the key pair and keeps
the private key.

An installed app is native code on the ESP32-S3. It can read all the memory of the board. The permission classes stop
the calls of the ABI, not the access to the memory. So install only the apps of a store that you trust, and use the
developer mode only for your own apps. The developer mode ON is a consent, as ALLOW of a key: the board does not
accept a request of the web to turn it on (`POST /api/apps?op=dev&on=1`), because with it a device on the LAN could
install and run an app with no signature. Only the NVS keeps the developer mode and the HMAC key of the signature
cache (`tfn_dev`, `tfn_hk0` .. `tfn_hk7`). The spill store of the settings (`/data/store2.bin`) never holds them, and
the board ignores their records in that file. If the NVS has no room for the key of the signature cache, the board
makes room one time and writes the key again; if that fails, the key holds until the next start, and the log tells it.

The web routes do not read or write the files and the folders of the system in `/data` (`web_data_sys_path()` in
`main/web_util.c`): the spill store, the signature cache, the permission policy, the pool record, the account vault
(`ai_accounts.enc`, `ai_accounts.tmp`), the old key files of the vault (`ai_limits.key`, `ai_limits.kew`: the board
reads them into a new vault), the installed apps and the `.part` name of each. The check ignores the case of a name
and the dots and spaces at its end, as FAT does, and refuses a short FAT name (a first name with `~`). These are the
web routes that touch `/data` (the handlers of the firmware; an app with a web page adds its own):

| Route | What it does in `/data` | The guard |
| --- | --- | --- |
| `GET /` | Lists `/data/exports`, `/data/samples` and `/data/midi` | Reads only |
| `GET /f/<path>` | Sends the file `/data/<path>` | `web_data_route_path()`: no system file, no `..`, no backslash, no control character |
| `GET /del/<path>` | Deletes the file `/data/<path>` (and the `.mid` file of a `.wav` file) | `web_data_route_path()`, as `/f` |
| `GET` and `POST /files` | Lists, sends and writes each file of `/data` (and of the SD card) | `web_data_sys_path()`; no `..`, no `//`, no backslash |
| `POST /midi` | Writes `/data/midi/<name>.mid` | The board makes the name (letters, digits, `-`, `_`, `(`, `)`) |
| `GET` and `POST /api/splash` | Reads, writes or deletes `/data/splash.jpg` | One fixed name |
| `POST /api/menu` | Writes or deletes `/data/menu.txt` and `/data/home.txt` | Fixed names |
| `GET` and `POST /api/notify` | Writes `/data/notify.bin` | A fixed name |
| `POST /api/apps` (`op=put`, `commit`, `rm`, `move`, `policy`, `scan`) | Writes and deletes `/data/apps/<id>/`; writes `/data/tfn_policy.txt` and `.sig` | A package needs a good signature (2.1); a new policy needs the store signature; `rm` of a required app needs the token |
| `POST /api/pool` | Writes and erases `/data/tfn_pool.bin` | The pool rules ([pools.md](pools.md)); a JOIN needs the tap at the board (2.5) |
| Each route that changes a setting | The NVS, or the spill store `/data/store2.bin` when the NVS is full | `store_set()` (`main/settings.c`) |

### 2.2 The web server of the board

`main/web_auth.c` and `main/web_auth_core.c` add a gate in front of each web handler (also the handlers of the
installed apps):
- The board keeps only a salted PBKDF2-HMAC-SHA256 of the password (2000 rounds), never the password.
- A browser logs in at `/login` and gets a session cookie (HttpOnly, SameSite=Strict). A script sends HTTP Basic.
- After 5 wrong passwords from one address, the board waits 30 s. The wait doubles at each next wrong one.
- The WebSocket `/ws/audio` is checked before its handshake.

The gate is OFF until the user sets a password (SETUP > WEB PASSWORD, or the console `webpass set`).

These holes of the web stay (NOT DONE). They need the web password, or the redesign of S1 (section 3):
- With no web password, the board answers each device on the LAN. That device can delete each media file
  (`GET /del/...`), read and write each file of `/data` that is not a system file (`/files`), install an app with a
  good signature, change the settings and tap the screen (`/api/ctl`).
- Many routes change the board on a `GET` and send `Access-Control-Allow-Origin: *`. A page of another site in a
  browser on the LAN can send such a `GET` (for example an `<img>` tag with `/del/exports/song.wav`), when it knows
  the address of the board.
- A web client that sends long presses or swipes (`/api/ctl`) all the time can block each consent at the board: a
  real finger that comes down while a virtual finger is on joins that touch, and its tap does not count
  (`main/input.h`). No consent acts; the USB console commands of 2.5 still work.

### 2.3 The update over the air

`main/ota.c` refuses an image for another board kind or an image with no board tag, and `esp_ota_end()` checks the
hash of the image. A new firmware that restarts in its first 20 s goes back to the old one (the rollback). The
release image has a signature (the trailer TFNOTA1, `main/ota_sig.c`): in the mode ENFORCE, the board installs only an
image that a key of its list signed, with a security version that is not lower than its own minimum. In the mode
REPORT, the board checks the signature, logs the result and installs the image. The public release is ENFORCE.

### 2.4 The crash guard and the safe mode

The crash guard of `main/tfn_app.c` stops an installed app after two resets in it. The safe mode (the BOOT key of the
3.49 at the start) loads no installed app. These protect the board from a bad app, not from an attacker.

### 2.5 The consent at the board

These steps need a person at the board: ALLOW of a publisher key (section 4.4) or of a pool JOIN, LEAVE of a pool, the
developer mode ON, and the question of the STORE for a new permission class. Only the tap of a finger on the glass
answers (`main/input.h`). These do not count: a tap of the web page or of the console, a touch that came down before
the question showed or in its first 0.8 s, and a tap of a touch whose start the question did not get (for example the
touch that wakes the screen). The USB console has its own command for each step (`pub allow`, `pool allow`,
`pool leave`, `apps get`, `apps dev 1`), because a person with the USB cable can write any firmware to the board.

These paths have no consent at the board (NOT DONE):
- The web install of an app: `POST /api/apps?op=put`, then `op=commit` (the path of the manager and of
  `sdk/tools/tfn_pkg.py`). A package with a good signature installs with all its permission classes, also as an
  update to a version with a new class. With no web password, each device on the LAN can do it (S1).
- The question of the STORE shows the classes of the catalog. The install does not compare the classes of the
  manifest that it gets with the classes that the user accepted.
- FILES > INSTALL of a package (from the SD card or from `/data`, `main/app_files_act.c`): the question shows the
  name and the version, not the permission classes of the manifest. A package with a good signature installs with all
  its classes. A tap of the web page (`/api/ctl?tap`) can answer the question.
- The token JOIN of a LEGACY build (`CONFIG_TFN_POOL_LEGACY`, [pools.md](pools.md) section 3): a board with no pool
  joins the pool of the manager that has its store token, with no tap. A board with no token takes the token from the
  first client that sends `POST /api/apps?op=mgr`. So the first device on the LAN can set the token of a new board and
  then make the board join its pool. The public build has this option off.

## 3. The requirements of the public code (S1 to S5)

The public firmware must not have these faults. Each line gives the fault, its effect in a setup with no trusted
network, the fix, and the state. A manager or a VPN hub is not a part of the public code: section 4 gives the
requirements of the public pool manager.

| # | Fault | Effect with no trusted network | The fix | State |
| --- | --- | --- | --- | --- |
| S1 | The board web password is OFF by default. Many endpoints send `Access-Control-Allow-Origin: *` and change the board on GET. | Each device on the LAN has full control of the board. A web page in a browser can reach the board only in rare cases (plain HTTP, a known address). | A first-start password for new boards, a notice to set one on old boards, POST for each change, no `ACAO: *`, a check of `Origin` and `Host`. | NOT DONE (the password is optional) |
| S2 | The hotspot is open (no WPA2) and starts by itself when there is no WiFi. | Each person near the board for 2 minutes after a start with no WiFi. | WPA2 with a random key, shown as a QR code on the screen. | NOT DONE |
| S3 | A change of the Home Assistant address keeps the saved token (`main/ha.c`). | The people of S1 can send the token to their own server. | Clear the token when the address changes; the same rule for each secret form of an app. | NOT DONE |
| S4 | The firmware update (`POST /api/ota`) has no signature. | The people of S1 can install any firmware. | Signed images (section 4.2, OTA). | NOT DONE |
| S5 | The radio packets between boards have no proof of the sender. | Each person near the board can send a packet as a friend. | Link v4 (section 4.2). | PART: done for the boards in a pool ([pools.md](pools.md)); a board with no pool is as before |

## 4. The target design: pools, keys and signed updates

This part is the TARGET. Stage 2 (the pools, the signed roster, the JOIN with the 6 digits, link v4) is in the code:
[pools.md](pools.md). The pool manager of stage 4 is in the code too: [poolmgr.md](poolmgr.md). The other parts of
this section are the target.

### 4.1 The concepts

| Concept | What it is |
| --- | --- |
| Device identity | Each board makes its own key pair at the first start (an X25519 key and a signing key). The device ID comes from the public keys. The MAC is only a radio address. |
| Pool | A group with its own pool ID, pool key and pool admin key. A signed roster lists the members and their roles (admin, member, child). A board is in 0 or 1 pool. |
| Friend | A trust between two boards, in the same pool or not. Both screens show the same 4 check digits at the pairing. |
| Manager | Optional. A pool member with the admin role. A board accepts it only after a tap on ALLOW on its own screen. |
| Single board | No pool, no manager, no hub, no beacons. The default of the public firmware. |

### 4.2 How each feature changes

| Feature | Now | Target |
| --- | --- | --- |
| FRIENDS | MAC + name, learned with a tap | device key + name + MAC hint + flags (can call, can WAKE); a pairing with check digits |
| Lobbies, 2-board games | the HELLO lists the MACs of the friends | the HELLO lists device IDs, with a tag of the pool key or the friend key |
| WALKIE / TALKIE | plain voice; each friend can WAKE | encrypted voice; WAKE only from roles or friends with the WAKE flag |
| Radio and LAN packets | no proof, a small replay window | link v4: a pool tag, a sender ID, a counter, a MAC tag; a board drops the packets of other pools |
| LAN beacon | name + open app, on each WiFi network | only inside a pool, encrypted with the pool key; a single board sends none |
| Manager | finds and claims each board | adopted on the board screen; one for each pool; its own key |
| Store | one store key | a list of trusted publisher keys: the official key + the keys that the board owner adds |
| OTA | no signature | signed images (the ESP-IDF "signed apps without secure boot": no eFuse change); a key list with room for a second key |
| VPN | an optional WireGuard tunnel to one hub (the extra VPN) | optional for each pool, an extra; each hub with a firewall |
| Web UI | open by default | a first-start password shown on the screen; POST for changes; no `ACAO: *` |

### 4.3 The stages

| Stage | Content |
| --- | --- |
| 0 | The fixes of section 3 |
| 1 | The device keys; signed OTA; the list of trusted publisher keys |
| 2 | Pools, the signed pool profile, the pairing with check digits, link v4 |
| 3 | Friends across pools, encrypted WALKIE, a signed hub directory |
| 4 | The public pool manager `poolmgr` ([poolmgr.md](poolmgr.md)): a command line program on the computer of the pool. It has no web page and no admin route on the network: a change of the pool comes from a command on the computer, or from its background command after a LEAVE on a board. Each board joins with the 6 digits and a tap on ALLOW. Key R is only in an encrypted file. The program checks each firmware file before it sends it, and the board checks it again |

### 4.4 The store and the release keys

- The board trusts a list of publisher keys: the official key (in the public build) and the keys that the
  owner of the board adds on the screen (a pool store, a developer for tests).
- A key gets into the list only with the consent of a person at the board. A web request
  (`POST /api/pub?op=add`) or a key file of the SD card only starts a wait of 2 minutes and a prompt with the key ID.
  Then a tap on ALLOW on the screen, or the USB console line `pub allow <key ID>`, adds the key. The tap must be a
  finger on the glass (a tap of the web page or of the console does not count), of a touch that comes down 0.8 s or
  more after the prompt shows, and it adds only the request that the screen shows (each request has its own number:
  a new request, also of the same key with other flags, starts a new prompt and a new wait). The console counts as
  the tap because a person with the USB cable can write any firmware to the board anyway (the same rule as
  `pool allow` for the JOIN of a pool). `pub allow` adds only the key that waits, and only when its key ID is the
  given one. The web has no way to the ALLOW. The public pool manager uses it for its pool store key:
  `poolmgr store trust --console <port>` (`docs/poolmgr.md`).
- The official store is a static, signed index, not a server.
- A developer signs a package with a developer key. The store countersigns it after a review of the source.
- A release has signed images and checksums. The release key stays offline. No secure boot and no flash encryption by
  default: they are permanent changes of the chip (`AGENTS.md` section 7.4).
