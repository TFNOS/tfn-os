#pragma once

// The firmware update over the network (OTA, ota.c). The flash has two app slots (ota_0 and
// ota_1, partitions.csv). POST /api/ota with the app image as the body writes the other slot, checks the
// image and the board, and restarts into it. A manager on the LAN sends it.

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

void ota_register(void *h);  // h: the httpd_handle_t of wifi.c
// Each frame (main.c): after 20 s of normal run, the new firmware is marked good (no rollback after that).
void ota_tick(int64_t now_ms);
// The update panel over the app while an update runs (main.c, after the app draws)
void ota_overlay(void);
// JSON fields for /api/boards (no braces): "ver" (version.txt), "fw" (date, time, ELF hash), "slot", "ota" (the
// board can update), "kind" (the board tag), "bad" (the slot of a rollback), "pend" (not marked good yet), "up" (s),
// and the signed OTA (ota_sig.h): "sig" (off / report / enforce), "keys" (the key IDs of the key list, 16 hex digits
// each), "minsv" (the lowest security version that the board takes), "own_sv" (the security version of the own
// trailer; -1: none), "own_key" (the key ID that signed the own image; "": none), "own_sig" (the code of the check of
// the own trailer: ok, sig_none ...), "last_sig" (the last update since the start: "ok:<key ID>" or a code; "": none),
// "self" (the self-test of ENFORCE in this start: "" none, "wait", "pass", "back", "kept" = it failed with no rollback
// and the firmware runs on marked good, "kept_pend" = the same with no /api/ota, still pending)
int ota_json(char *b, size_t n);
// The check of the trailer of the image in an app slot (part: a const esp_partition_t *) with the key list, the kind
// and minsv of this firmware (main/layout.c checks its copy in ota_0 with it). true: the image can start in the mode
// of this build: the mode is not ENFORCE, or the trailer verifies. code = the result ("ok:<key ID>", "sig_none" ...;
// the mode name when the build is not ENFORCE).
bool ota_slot_sig_ok(const void *part, char *code, size_t n);
// The version of this firmware (version.txt, for example "1.0.3") and the app slot that runs ("ota_0")
const char *ota_version(void);
bool ota_busy(void);  // true while an update is written (main/layout.c waits)
const char *ota_slot(void);
// The console command "ota": the slots and their states in the log
void ota_log_info(void);
// Authenticated USB maintenance uses the same slots and signed-image checks.
bool ota_usb_begin(size_t total, const uint8_t sha256[32], char *message, size_t cap);
bool ota_usb_write(size_t offset, const uint8_t *data, size_t len, char *message, size_t cap);
bool ota_usb_finish(char *message, size_t cap);
void ota_usb_abort(void);
