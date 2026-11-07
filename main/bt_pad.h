#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/bt_pad.h.
#include "tfn/bt_pad.h"

#include <stdbool.h>
#include <stdint.h>

// The board as a Bluetooth LE gamepad (HID over GATT). Windows, Android, macOS and
// Linux see a standard game controller: 16 buttons, a hat switch (8 directions) and
// 4 axes (X, Y = the left stick, Z, Rz = the right stick). The same device is also a
// keyboard (with its LEDs), the media keys and a mouse with a wheel (BT KEYS).
// The BLE stack starts at bt_pad_start(). It stops about 2 s after the app that started it closes (the
// stack keeps about 42 KB of internal RAM, and with it on, the SD card reads and the WiFi can fail). A paired computer
// reconnects when the app opens again. Pairing: "just works", bonds are kept in NVS.

#define PAD_HAT_CENTER 8  // hat values 0..7: N, NE, E, SE, S, SW, W, NW

typedef struct {
    uint16_t buttons;  // bit n = button n + 1
    uint8_t hat;       // 0..7, or PAD_HAT_CENTER
    int8_t x, y;       // left stick, -127..127 (+x right, +y down)
    int8_t z, rz;      // right stick
} pad_report_t;

// Stop the stack and give its RAM back (from the app task); the next bt_pad_start() starts it again.
void bt_pad_stop(void);
// Each frame of the app task: the stop after the app that holds the stack closed.
void bt_pad_tick(int64_t now_ms);
// Send the state to the computer. It sends only changes, and repeats the state
// each 200 ms. Safe to call each frame.
void bt_pad_send(const pad_report_t *r);

#define BT_MOD_RCTRL  0x10
#define BT_MOD_RSHIFT 0x20
#define BT_MOD_RALT   0x40
#define BT_MOD_RGUI   0x80

// The same stack also holds the BLE-MIDI service (ble_midi.c). on = advertise the MIDI
// service with the MIDI name (the MIDI app is open); off = advertise the gamepad (the
// default). A connection that is up stays up.
void bt_pad_adv_midi(bool on);
const char *bt_pad_midi_name(void);  // "TFN MIDI"
