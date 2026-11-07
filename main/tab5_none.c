// TAB5 (ESP32-P4): the names that the P4 build does not have, because the P4 has no radio of its own (the
// WiFi of the Tab5 is an ESP32-C6 through esp_hosted, with no ESP-NOW and, for now, no Bluetooth).
//
// ESP-NOW: link.c sends each packet by ESP-NOW and, when no ESP-NOW packet of the game came for LAN_FALLBACK_MS, also as
// a copy over the LAN (lan.c). Here ESP-NOW "works" and sends nothing: so each packet goes by the LAN, and the games of
// 2 BOARDS run over the WiFi LAN (the other board must be on the same LAN).
//
// BLE (NimBLE): the ABI table of the app files (tfn_abi.inc) has these names for the store apps; on the P4 each one
// fails (-1). bt_pad.c and ble_midi.c have their own "no Bluetooth" parts.
//
// (no headers: only the names matter to the linker; the callers use their own prototypes)
#include "sdkconfig.h"

#if CONFIG_IDF_TARGET_ESP32P4

#include <stdbool.h>
#include <stdint.h>

int esp_now_init(void)
{
    return 0;
}
int esp_now_deinit(void)
{
    return 0;
}
int esp_now_register_recv_cb(void *cb)
{
    (void)cb;
    return 0;
}
int esp_now_unregister_recv_cb(void)
{
    return 0;
}
int esp_now_add_peer(const void *peer)
{
    (void)peer;
    return 0;
}
bool esp_now_is_peer_exist(const uint8_t *mac)
{
    (void)mac;
    return true;
}
int esp_now_send(const uint8_t *mac, const uint8_t *data, unsigned len)
{
    (void)mac;
    (void)data;
    (void)len;
    return 0;
}

#define NO_BLE(name)                                                                                                   \
    int name(void)                                                                                                     \
    {                                                                                                                  \
        return -1;                                                                                                     \
    }
NO_BLE(ble_gap_adv_active)
NO_BLE(ble_gap_adv_rsp_set_data)
NO_BLE(ble_gap_adv_rsp_set_fields)
NO_BLE(ble_gap_adv_set_data)
NO_BLE(ble_gap_adv_set_fields)
NO_BLE(ble_gap_adv_start)
NO_BLE(ble_gap_adv_stop)
NO_BLE(ble_gap_conn_active)
NO_BLE(ble_gap_conn_find)
NO_BLE(ble_gap_disc_active)
NO_BLE(ble_gap_security_initiate)
NO_BLE(ble_gap_terminate)
NO_BLE(ble_gap_unpair)
NO_BLE(ble_gap_update_params)
NO_BLE(ble_gatts_chr_updated)
NO_BLE(ble_gatts_indicate)
NO_BLE(ble_gatts_indicate_custom)
NO_BLE(ble_gatts_notify)
NO_BLE(ble_gatts_notify_custom)
NO_BLE(ble_hs_id_copy_addr)
NO_BLE(ble_uuid_cmp)
NO_BLE(ble_uuid_flat)
NO_BLE(ble_uuid_init_from_buf)
NO_BLE(ble_uuid_length)
NO_BLE(ble_uuid_u16)

#endif  // CONFIG_IDF_TARGET_ESP32P4
