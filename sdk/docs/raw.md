# Raw ESP-IDF functions in an app

An app file can call only the names of the ABI. The ABI (version 1, revision 21: about 1500 names) has about 800 raw
ESP-IDF functions in addition to the functions of TFN OS (`gfx_*`, `ui_*`, `sfx_*`, `store_*`): sockets,
HTTP, TLS, crypto, NVS, the pins, the serial ports, I2C, SPI, PWM, the analog inputs, BLE, the WiFi radio, FreeRTOS
and more. A function that
can do harm has a **permission class**: the store of the board asks the user before the install, and the board links
it only for an app whose manifest has the class.

You need no firmware change and no request for these functions: write the app, build it, test it. Only a function
that the firmware does not have needs a request (section 6).

The example: [examples/wifiscan](../examples/wifiscan/README.md) (WIFI SCAN) calls six raw ESP-IDF WiFi functions.

## 1. How the board links an app

1. The firmware has a table of the ABI names (`main/tfn_abi.inc`, made from `main/tfn_abi.json` by
   `sdk/tools/abi_gen.py`). The table has no strings: each entry is the hash of a name (FNV-1a 64) and its address.
2. When an app opens, the loader finds each name that the app file needs by its hash (`tfn_resolve_sym()` in
   `main/tfn_app.c`).
3. A name that is not in the table stops the load: "NEEDS A NEWER FIRMWARE (A MISSING NAME)". The public firmware of
   tfn-os has no name of the parts that are not in its tree (for example the scene tables `SCN_<KEY>`), also at the
   same ABI revision: an app that calls such a name passes the revision check of a public board and stops here.
4. A name of a class that the manifest of the app does not give stops the load: "<APP> ASKS FOR <name> WITHOUT ITS
   PERMISSION".

`mk_app.py` does the ABI check on the PC before it makes the package, so you see a problem first.

To find out if a name is in the ABI and its class:

- `main/tfn_abi.json` of the source: `symbols` (name -> the board kinds), `need` (name -> its default class),
  `revs` (name -> the revision that added it);
- the API reference [api.md](api.md);
- the console of a board: `apps policy <name>` (free, a class, or "not in the ABI");
- a manager, if you use one: the tab PERMISSIONS (find a name).

## 2. What is in the ABI

`sdk/tools/abi_gen.py` makes the ABI from three sources:

1. the names that the built-in apps call (it finds them in the objects of the DEV build);
2. the **raw ESP-IDF families** of `main/tfn_abi_raw.json`: each global function of the firmware of a kind whose name
   matches a family and no deny pattern. Their code is already in the firmware, so only the table grows (about 12 KB);
3. the **extra names** of `main/tfn_abi_extra.json`: the names of the approved ABI requests (section 6).

The families and the extra names, with their default classes (`need` of `main/tfn_abi.json` gives the
class of each name):

| Family | Default class | Names (about) |
| --- | --- | --- |
| `esp_wifi_scan_*`, `esp_wifi_sta_get_ap_info` | `wifi.scan` | 6 |
| `esp_wifi_get_config`, `get_mode`, `get_ps`, ... | `wifi.config` | 3 |
| `esp_wifi_set_channel`, `set_ps`, `set_promiscuous*`, `80211_tx`, ... | `wifi.radio` | 10 |
| `esp_wifi_get_mac` | free | 1 |
| `esp_now_send`, `esp_now_add_peer`, ... | `espnow` | 3 |
| `lwip_*` (sockets), `esp_http_client_*`, `esp_tls_*`, `esp_transport_*`, `sntp_*` (read only) | `net` | 109 |
| `esp_event_handler_(instance_)register` / `unregister` | `events` | 2 |
| `ble_gap_*`, `ble_gattc_*`, `ble_gatts_notify`, `ble_uuid_*` (the public calls) | `ble` | 25 |
| `nvs_*` (not `nvs_flash_*`) | `nvs` | 18 |
| `gpio_*`; `uart_*` (the serial ports, revision 14: SERIAL MONITOR); `rfp_*` (the pulse receivers on a pin) | `gpio` | 46 |
| `i2c_master_*` (with `i2c_master_probe`, `i2c_master_get_bus_handle`: revision 15), `i2c_new_master_bus`, `i2c_del_master_bus`, `spi_device_*`, `spi_bus_add_device` | `bus` | 21 |
| `ledc_*` | `pwm` | 8 |
| `adc_oneshot_*`, `adc_cali_*` | `adc` | 7 |
| `psa_*` (the public PSA crypto calls), `mbedtls_*` | free | 490 |
| FreeRTOS: queues, semaphores, event groups, ring buffers, task notify, `xTaskGetTickCount`, ... | free | 70 |
| `esp_timer_*`, `esp_log_*`, `heap_caps_get_*`, `esp_netif_get_*`, `esp_random`, `esp_read_mac`, `esp_err_to_name` | free | 40 |
| `bt_kbd_*`, `bt_mouse_*`, `bt_media_*`, `bt_pad_*` (BT KEYS, revision 7) | `hid` | 9 |
| `rfn_*`, `rfsrc_*` (the native radio of SDR, [radio.md](radio.md)) | `radio.rx` | 9 |
| `audio_alert` (an alarm that plays also when the board is muted, revision 9) | `sound.alert` | 1 |
| `tfn_notify` ([notify.md](notify.md)) | `notify` | 1 |
| `tfn_secret_get`, `tfn_secret_set`, `device_guard_accounts_*` | `secret` | 4 |
| a `watch.elf` part of the app ([notify.md](notify.md)) | `background` | 1 |

Not in the ABI, never (the deny patterns of `main/tfn_abi_raw.json`): the flash and the updates (`spi_flash_*`,
`esp_ota_*`, `esp_partition_*`), the efuses, sleep and restart, interrupts (`*isr*`), the HAL and the low-level drivers
(`*_hal_*`, `*_ll_*`), the start and the stop of the WiFi and its connection (`esp_wifi_start`, `_connect`,
`_set_config`, ...), the init of ESP-NOW and of NVS flash, the sound hardware (I2S), the creation of tasks other than
`xTaskCreatePinnedToCore` (the loader follows only that one), and the internal functions of the libraries.

**mbedTLS 4 (ESP-IDF 6.1):** the old crypto functions (`mbedtls_sha256`, `mbedtls_aes_*`, ...) are private in mbedTLS 4.
Use the PSA calls: `psa_hash_compute()`, `psa_cipher_encrypt()`, `psa_sign_hash()`, ... (`#include "psa/crypto.h"`).

## 3. The permission classes and the policy

Some functions can do harm, show private data, or change the board for the other apps. Such a name has a class:

| Class | The store of the board says "IT CAN ..." |
| --- | --- |
| `wifi.scan` | SCAN THE WIFI NETWORKS |
| `wifi.radio` | CHANGE THE WIFI RADIO (CHANNEL, POWER, RAW PACKETS) |
| `wifi.config` | READ THE WIFI SETTINGS (ALSO THE PASSWORD) |
| `espnow` | SEND ESP-NOW MESSAGES TO BOARDS NEAR IT |
| `net` | USE THE NETWORK AND THE INTERNET |
| `events` | GET THE EVENTS OF THE SYSTEM (WIFI, NETWORK) |
| `ble` | USE BLUETOOTH |
| `nvs` | READ AND WRITE THE SETTINGS STORE (ALSO THE WIFI PASSWORDS) |
| `gpio` | USE THE PINS OF THE BOARD |
| `bus` | USE THE I2C AND SPI BUSES OF THE BOARD |
| `pwm` | USE THE PWM OUTPUTS (THE SCREEN LIGHT TOO) |
| `adc` | READ THE ANALOG INPUTS |

What a class does:

1. The manifest of the app has the classes of its names: `perms=gpio,net`. `mk_app.py` writes it. A store that
   signs the app can write `perms=` and `rev=` again from its published policy and the ABI of the newest release. The
   signature covers the manifest.
2. The STORE of the board shows the text before the install: "INSTALL X? IT CAN USE THE PINS OF THE BOARD". The user
   must tap INSTALL with a finger on the screen (a tap of the web page or of the console does not count). An update
   that adds a class asks again: UPDATE ALL skips such an app and tells the count ("3 UPDATED, 1 ASK FIRST"); UPDATE on
   the app asks the question.
3. The loader links a name of a class only for an app whose manifest has that class.

**The classes above are the defaults of the firmware.** A manager can change them with no new firmware (the tab
PERMISSIONS): the text of a class, a new class (32 at most), a name into a class, a name free. PUBLISH signs the
policy with the store key; each board takes it within a minute (the manager sends it, and the STORE of the board asks
for it) and keeps it in `/data/tfn_policy.txt`. A name that the policy does not name keeps the class of the firmware.
The console of a board shows it: `apps policy` (the classes) and `apps policy <name>`.

When the policy moves a name into a class, an installed app that calls it cannot open until it is signed again
(the new signature writes the new `perms=`).

For a file with no signature (the developer mode), `perms=` must follow the policy of the board: build it with
`mk_app.py ... --policy <the URL of the manager, or a policy file>`, else `mk_app.py` uses the defaults.

## 4. The revision

Each run of `abi_gen.py` that adds names gives the ABI the next revision (`rev`). Each new name gets that revision.

- `mk_app.py` writes the highest revision of the names of the app into the manifest (for example `rev=16` for an app
  that calls `alarm_set()`).
- The firmware knows its revision (`TFN_ABI_REV`, and `rev` in `GET /api/apps`).
- The manager store gives the app only to a board whose firmware has that revision or a newer one.
- A file that comes to an older board in another way (the SD card, a push) shows "needs a newer firmware".

The revisions (`revs` of `main/tfn_abi.json` gives the revision of each name):

| Rev | Added |
| --- | --- |
| 1 | the WiFi scan names of WIFI SCAN |
| 2 | the raw ESP-IDF families (about 780 names) |
| 3 | the names of a private extra (not in the public SDK) |
| 4 | `app_launch_arg`, `tfn_notify`, the export names |
| 5 | `tfn_secret_get`, `tfn_secret_set` |
| 6 | `usb_audio_*` |
| 7 | `bt_kbd_*`, `bt_mouse_*`, `bt_media_*`, `bt_pad_*` |
| 8 | the promiscuous WiFi names, `friends_invite`, SCENES data |
| 9 | `app_standby_hold`, `audio_alert` |
| 10 | `app_screen_pocket`, `sfx_ui`, `xTimer*`, `lwip_getpeername` |
| 11 | `kbd_*` (the keyboard of the system), more of the math library |
| 12 | `rfd_*` (`libs/rfdecode`) |
| 13 | `ird_*` (`libs/irdecode`) |
| 14 | `uart_*`, `rfp_board_uart`, `rfp_board_i2c` |
| 15 | `i2c_master_probe`, `i2c_master_get_bus_handle`, `i2c_del_master_bus`, more ring buffer names |
| 16 | `alarm_*` (the alarms of CLOCK) |
| 17 | `device_guard_*` (only a firmware with the device protection ON has them) |
| 18 | `timer_get`, `timer_set` (the timers of CLOCK) |
| 20 | `uxTaskGetSystemState`, `uxTaskGetNumberOfTasks`, `vTaskGetInfo`, `nvs_get_stats`, `heap_caps_print_heap_info` |
| 21 | `gfx_hi`, `gfx_hi_want` (the HI frame), `kbd_set_hebrew`, `tz_split`, `tz_parse`, `tz_offset` (the time zones) |

Revision 19 has no name in `revs`.

## 5. How to write raw code

- **Include the real ESP-IDF headers**: `"driver/gpio.h"`, `"esp_http_client.h"`, `"lwip/sockets.h"`,
  `"psa/crypto.h"`, `"host/ble_gap.h"`, ... `mk_app.py` gives the compiler the include folders of each component of
  the firmware build (a response file, `sdk/out/_includes_<kind>.rsp`).
- **Put the raw code inside `#ifdef ESP_PLATFORM`**: the includes and the calls. `mk_app.py` also compiles the app on
  the PC to draw its icon, and the PC has no ESP-IDF headers. WIFI SCAN shows the pattern: on the PC, the same
  functions give made-up data.
- Then build: `python sdk/tools/mk_app.py path/app_x.c --sym app_x --name X --kinds lcd349 --nosign --push <board ip>
  --open` (the board in the developer mode). The output line shows the revision and the classes, for example `rev 14,
  perms gpio`.

The firmware also uses ESP-IDF. A raw call can break the system for all apps, so obey these rules:

- **The firmware owns the WiFi connection.** Read its state with `wifi_state()`. Do not scan while it is
  `WIFI_CONNECTING` (the scan stops the connection). `esp_wifi_set_channel()` and `esp_wifi_set_ps()` change the radio
  for the whole board: set the old value again in `leave()`.
- **A scan result belongs to the code that started the scan.** Read your result with `esp_wifi_scan_get_ap_records()`;
  if you do not read it, free it with `esp_wifi_clear_ap_list()`.
- **A blocking call goes into a task of the app**, never into `update()` or `render()`. Make the task with
  `xTaskCreatePinnedToCore()` (the loader follows it); it must end in `leave()` (behave.md, the tasks). Stop a blocking
  call in `leave()`, then wait for the task.
- **Each callback goes before the app closes.** An event handler (`esp_event_handler_register`), a timer
  (`esp_timer_create`), a BLE callback: give it back in `leave()` (`..._unregister`, `esp_timer_delete`). After the
  close the code of the app is not in memory, and the next call stops the board.
- **Delete what you make**: queues, semaphores, ring buffers, event groups, sockets, HTTP clients, PSA keys
  (`psa_destroy_key`), NVS handles. The system frees only the `malloc` / `heap_caps_malloc` memory and the files of an
  app.
- **The pins and the buses of the board**: the screen, the touch, the IMU and the SD card use some of them (see the
  board files in `main/board*.c`). An app that changes them breaks the board until a restart.
- **NVS**: use a namespace of your own (the id of the app). Do not write the namespaces of the system.
- **Hand data from a task to the app task with a flag** (or a lock), not with a shared buffer that both write.
- **The types are the types of the ESP-IDF version of the firmware** (6.1).

## 6. A function that the firmware does not have

An ABI request is a GitHub issue. The ABI is add-only: a name that goes in stays in all later releases of the same
ABI version. So each name needs a reason.

1. `mk_app.py` stops and writes the request: `sdk/out/<id>/abi_request.json` (the names).
2. Open an issue with the form "An ABI request" (`.github/ISSUE_TEMPLATE/abi_request.yml`). Give the names, the
   content of `abi_request.json`, a link to the source of the app, what the app does with each name, the permission
   class that you propose, and the board kinds.
3. The maintainer reviews the request (section 7) and writes the decision in the issue: APPROVE with a class (or
   free), or DENY.
4. After the approval, a pull request adds the names to `main/tfn_abi_extra.json` (`name`, `perm`, `why`, `by`) and a
   PC stub of each name to `sdk/tools/host/raw/raw_stubs.c`. Then `python sdk/tools/abi_gen.py --extra-only` writes
   `main/tfn_abi.json` and `main/tfn_abi.inc` (no DEV build is necessary). The new names get the next revision. Do not
   add the names in a pull request before the approval.
5. The next firmware release has the names. A name that ESP-IDF does not have stops the build: such a request is
   denied.
6. After that release, build the app again: its manifest has the new revision. A store gives it only to the boards
   with the new firmware, and an older board shows "needs a newer firmware".

## 7. The review of a request (for the reviewers)

- Does the app really need the raw name? A firmware function (a wrapper) is better when the firmware must clean up
  after the app (for example, set the channel back when the app closes or stops).
- What class, or free? A name needs a class when it can change the radio, the network or the power of the board, read
  private data (networks near the user, passwords, other devices), send data out or let other devices in, change data
  that is not the data of the app, or use a hardware part that the system also uses.
- Does the name keep a pointer into the app (a callback)? Then the app must give it back in `leave()`.
- Could it harm the board in a way that a restart does not repair (the flash, the efuses)? Then DENY.
