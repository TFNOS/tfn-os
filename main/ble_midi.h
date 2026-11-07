#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/ble_midi.h.
#include "tfn/ble_midi.h"

// BLE-MIDI 1.0 (the MIDI over Bluetooth LE specification): the board as a Bluetooth MIDI
// controller. It uses the NimBLE stack of the gamepad (bt_pad.c): the MIDI service is in the
// GATT table next to the HID service, and the board advertises the MIDI service while the
// MIDI app is open (bt_pad_adv_midi()).
// - Service 03B80E5A-EDE8-4B33-A751-6CE34EC4C700, characteristic 7772E5DB-3868-4112-A1A9-
//   F2669D106BF3 (read, write without response, notify).
// - A packet: a header byte (bit 7 = 1, bits 5..0 = timestamp bits 12..7), then for each
//   message a timestamp byte (bit 7 = 1, bits 6..0 = timestamp bits 6..0) and the message.
//   The timestamp is in ms, 13 bits. This code sends no running status.

#include <stdbool.h>
#include <stdint.h>

uint32_t ble_midi_dropped(void);  // messages dropped (no link, or full)

// ---- for bt_pad.c (the ESP build only)
int ble_midi_register(void);  // add the GATT service (before the host starts)
struct ble_gap_event;
void ble_midi_gap_event(struct ble_gap_event *ev);  // every GAP event of the connection
extern const uint8_t BLE_MIDI_SVC_UUID128[16];      // little-endian, for the advertising

// ---- pure functions (the host tests use them too)
typedef struct {
    uint32_t t_ms;
    uint8_t len;
    uint8_t b[3];
} bm_msg_t;

// Pack messages m[0..n-1] (in time order) into one packet of at most cap bytes.
// Returns the packet length (0 if not even one message fits); *used = the messages taken.
int bm_pack(const bm_msg_t *m, int n, uint8_t *out, int cap, int *used);

// Parse one received packet. fn gets each MIDI message (also realtime bytes, 1 byte) with
// its 13-bit timestamp. Returns the number of messages, or -1 for a bad packet.
typedef void (*bm_rx_fn)(const uint8_t *msg, int len, uint16_t ts, void *arg);
int bm_parse(const uint8_t *p, int len, bm_rx_fn fn, void *arg);

// The length of a MIDI message that starts with this status byte (1..3; 0 = SysEx).
int bm_msg_len(uint8_t status);

#ifndef ESP_PLATFORM
// Host tests: the packets go to this function (when it is set), and the link is up when
// bm_host_linked is true.
extern void (*bm_host_sink)(const uint8_t *pkt, int len);
extern bool bm_host_linked;
extern int bm_host_mtu;
void bm_host_receive(const uint8_t *pkt, int len);  // a packet from the other side
#endif
