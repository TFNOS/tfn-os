// The values of a build that the person who runs a set of boards chooses: the key of the app store and the network
// defaults.
//
// A build reads main/tfn_local.h first, when that file exists (git does not track it; main/tfn_local.example.h shows
// the form). It can define each value below for a private setup. A build with no tfn_local.h uses the public defaults
// of this file.
//
// Then it reads main/tfn_release_keys.h, when that file exists: the public keys of a public release build
// (.github/workflows/release.yml writes it with sdk/tools/release_cfg.py; git does not track it). A value of
// tfn_local.h stays: the release file defines only the values that are not defined.
#pragma once

#if defined(__has_include)
#if __has_include("tfn_local.h")
#include "tfn_local.h"
#endif
#if __has_include("tfn_release_keys.h")
#include "tfn_release_keys.h"
#endif
#endif

// The public key of the app store: an uncompressed P-256 point (65 byte values, the first one 0x04). The private key
// stays with the store. With no key (all zeros), the check of each signature fails, so the board installs no signed app.
#ifndef TFN_STORE_PUBKEY_BYTES
#define TFN_STORE_PUBKEY_BYTES 0
#endif
// A second built-in key of the app store (the same form): room for a new store key. With no key (all zeros), only key
// 1 and the keys that the board owner adds (SETUP > TRUSTED PUBLISHERS, main/tfn_pub.h) count.
#ifndef TFN_STORE_PUBKEY2_BYTES
#define TFN_STORE_PUBKEY2_BYTES 0
#endif

// The address of the static store (main/tfn_sstore.h): a web host that keeps the signed index.json, index.sig and
// the app files of sdk/tools/store_index.py. A board with no manager uses it (the store source AUTO), and the board
// owner can set another address. HTTPS only.
#ifndef TFN_STATIC_STORE_URL
#define TFN_STATIC_STORE_URL "https://omeriko9.github.io/tfn-os/store/"
#endif
// The store source of a board with no setting (main/tfn_sstore.h): 0 = AUTO (the manager when the board knows one,
// else the static store), 1 = MGR (the manager only: the board never asks the static store), 2 = STATIC.
#ifndef TFN_STORE_SOURCE_DEFAULT
#define TFN_STORE_SOURCE_DEFAULT 0
#endif

// The public keys of the signed firmware updates (main/ota.c, ota_sig.h): up to 4 uncompressed P-256 points, 65 byte
// values each (the first one 0x04). Slot 1 = the release key, slot 2 = the backup key, slots 3 and 4 = room for a key
// change. A slot with 0 holds no key. With no key, the board reports each update as "sig_key" (no trusted key). A
// build in CONFIG_TFN_OTA_SIG_ENFORCE with no key stops with an error (main/ota.c).
// sdk/tools/fw_sign.py keygen writes these lines for a new key.
#ifndef TFN_OTA_PUBKEY1_BYTES
#define TFN_OTA_PUBKEY1_BYTES 0
#endif
#ifndef TFN_OTA_PUBKEY2_BYTES
#define TFN_OTA_PUBKEY2_BYTES 0
#endif
#ifndef TFN_OTA_PUBKEY3_BYTES
#define TFN_OTA_PUBKEY3_BYTES 0
#endif
#ifndef TFN_OTA_PUBKEY4_BYTES
#define TFN_OTA_PUBKEY4_BYTES 0
#endif

// The tunnel address of the VPN hub, when the VPN settings of the board have none.
#ifndef TFN_VPN_HUB_IP
#define TFN_VPN_HUB_IP "10.0.0.1"
#endif

// The addresses of the CHAT assistant gateway: the board tries the first one, then the second one (for example in
// the VPN). Empty: the user sets them on the CHAT page of the board web.
#ifndef CHAT_URL_DEFAULT
#define CHAT_URL_DEFAULT ""
#endif
#ifndef CHAT_URL2_DEFAULT
#define CHAT_URL2_DEFAULT ""
#endif

// An example tunnel address of a board (only the placeholder text of the web pages).
#ifndef TFN_VPN_BOARD_IP_EXAMPLE
#define TFN_VPN_BOARD_IP_EXAMPLE "10.0.0.11"
#endif
