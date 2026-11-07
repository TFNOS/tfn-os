#pragma once

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

typedef enum {
    BM_OFF,          // the app is not open
    BM_STARTING,     // the Bluetooth stack starts
    BM_ADVERTISING,  // visible as a MIDI device, no connection
    BM_CONNECTED,    // connected, but the MIDI app of the other side has not opened the link
    BM_LINKED,       // the other side gets the notifications: messages go out
    BM_FAILED,       // the Bluetooth stack did not start
} bm_state_t;

// The app opens and closes the MIDI device (start the stack, advertise as MIDI).
void ble_midi_open(void);
void ble_midi_close(void);
bm_state_t ble_midi_state(void);
const char *ble_midi_name(void);         // the name that the computer shows
void ble_midi_peer(char *buf, int len);  // the address of the other side ("" if none)
int ble_midi_mtu(void);                  // the ATT MTU of the link (23 if none)
void ble_midi_disconnect(void);          // end the connection (for example a gamepad host)

// Queue one message (2 or 3 bytes: a channel message) with its time (ms, any clock that
// counts up). When the queue is nearly full, a new message takes the place of the older
// message of the same controller (the newest value wins). Returns false if it is dropped.
bool ble_midi_send(const uint8_t *msg, int len, uint32_t t_ms);
// Send the queued messages (call once per frame). Several messages go into one packet.
void ble_midi_flush(void);
uint32_t ble_midi_sent(void);      // messages sent
uint32_t ble_midi_received(void);  // messages received

// MIDI clock from the other side: the tempo in BPM, or 0 when no clock came in the last
// second. ble_midi_starts() counts Start and Continue messages (to align the beat).
float ble_midi_clock_bpm(void);
uint32_t ble_midi_starts(void);
