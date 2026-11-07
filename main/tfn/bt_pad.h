#pragma once

#include <stdbool.h>
#include <stdint.h>

// The board as a Bluetooth LE gamepad (HID over GATT). Windows, Android, macOS and
// Linux see a standard game controller: 16 buttons, a hat switch (8 directions) and
// 4 axes (X, Y = the left stick, Z, Rz = the right stick). The same device is also a
// keyboard (with its LEDs), the media keys and a mouse with a wheel (BT KEYS).
// The BLE stack starts at bt_pad_start(). It stops about 2 s after the app that started it closes (the
// stack keeps about 42 KB of internal RAM, and with it on, the SD card reads and the WiFi can fail). A paired computer
// reconnects when the app opens again. Pairing: "just works", bonds are kept in NVS.

typedef enum {
    PAD_OFF,          // not started
    PAD_STARTING,     // the stack starts
    PAD_ADVERTISING,  // visible, waiting for a connection
    PAD_CONNECTED,    // connected, but the computer does not read the reports yet
    PAD_READY,        // connected and secure: reports go to the computer
    PAD_FAILED,       // the stack did not start (memory)
} pad_state_t;

// Start the stack and advertise (from the app task). Returns false on an error. The open app holds the stack.
bool bt_pad_start(void);
pad_state_t bt_pad_state(void);
const char *bt_pad_name(void);  // the name the computer shows
// Change the name (1..29 characters). Before the start, or at any time: then the board
// advertises with the new name. A computer that has the old name can keep it until it pairs again.
void bt_pad_set_name(const char *name);
// Reports sent since the start (for the screen).
uint32_t bt_pad_sent(void);
// Forget all paired computers (a new pairing is necessary).
void bt_pad_forget(void);

// ---- the keyboard, the media keys and the mouse (the reports wait in a queue; a task sends them while the BLE stack
// has free buffers; safe to call from any task)
#define BT_MOD_LCTRL  0x01
#define BT_MOD_LSHIFT 0x02
#define BT_MOD_LALT   0x04
#define BT_MOD_LGUI   0x08  // the Windows key, the Command key
// the state of the keyboard: the modifiers and up to 6 keys (HID usage IDs: a = 0x04 .. z = 0x1D, 1 = 0x1E .. 0 = 0x27,
// ENTER 0x28, ESC 0x29, BACKSPACE 0x2A, TAB 0x2B, SPACE 0x2C, F1 = 0x3A .. F12 = 0x45, RIGHT 0x4F, LEFT 0x50, DOWN 0x51,
// UP 0x52, DELETE 0x4C, HOME 0x4A, END 0x4D, PAGE UP 0x4B, PAGE DOWN 0x4E). keys NULL = no key.
void bt_kbd_report(uint8_t mods, const uint8_t keys[6]);
// a press and a release of one key (with the modifiers)
void bt_kbd_tap(uint8_t mods, uint8_t key);
// type ASCII text with the keys of the US layout (the computer uses its own layout); returns the characters queued
int bt_kbd_type(const char *text);
// the LEDs that the computer sets: NUM LOCK 1, CAPS LOCK 2, SCROLL LOCK 4
uint8_t bt_kbd_leds(void);
// a press and a release of a media key (consumer usage: PLAY/PAUSE 0xCD, NEXT 0xB5, PREVIOUS 0xB6, MUTE 0xE2,
// VOLUME UP 0xE9, VOLUME DOWN 0xEA)
void bt_media_tap(uint16_t usage);
// the mouse: relative motion (it adds up and goes out each 8 ms), the buttons (bit 0 left, 1 right, 2 middle)
void bt_mouse_move(int dx, int dy, int wheel);
void bt_mouse_buttons(uint8_t buttons);
// true: the computer uses the boot protocol (a BIOS): only the keyboard and the mouse work then
bool bt_pad_boot(void);
