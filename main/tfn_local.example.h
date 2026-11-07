// An example of main/tfn_local.h: copy it to main/tfn_local.h and put your own values in it. Git does not track
// main/tfn_local.h. Each value that this file does not define keeps its default of main/tfn_defaults.h.
#pragma once

// The public key of your app store: the 65 bytes of an uncompressed P-256 point (sdk/tools/ makes the key pair).
// #define TFN_STORE_PUBKEY_BYTES 0x04, 0x11, 0x22, ...
// A second built-in store key (for example a new store key next to the old one), in the same form.
// #define TFN_STORE_PUBKEY2_BYTES 0x04, 0x33, 0x44, ...

// The public keys of your signed firmware updates (sdk/tools/fw_sign.py keygen writes the line of a new key): slot 1 =
// the release key, slot 2 = the backup key.
// #define TFN_OTA_PUBKEY1_BYTES 0x04, 0x11, 0x22, ...
// #define TFN_OTA_PUBKEY2_BYTES 0x04, 0x33, 0x44, ...

// The tunnel address of your VPN hub, and an example address of a board for the web pages.
// #define TFN_VPN_HUB_IP "10.0.0.1"
// #define TFN_VPN_BOARD_IP_EXAMPLE "10.0.0.11"

// The addresses of your CHAT assistant gateway.
// #define CHAT_URL_DEFAULT "ws://gateway.local:8790/xiaozhi"
// #define CHAT_URL2_DEFAULT ""
