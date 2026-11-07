#pragma once

// USB-C HEADPHONES (only the CoreS3: its USB-C can give 5 V, board_usb_power_out). The board is the USB host of a USB
// audio device (USB-C headphones, a USB-C to 3.5 mm adapter with a sound chip, a USB speaker). The sound of the board
// goes there instead of the speaker. While it is open: no console port, and the USB-C does not charge the battery.

#include <stdbool.h>

typedef enum {
    UP_OFF,      // not open
    UP_WAIT,     // open, 5 V out, no USB audio device yet
    UP_PLAYING,  // a device plays the sound of the board
    UP_FAILED,   // the USB host did not start, or the device has no format that the board can send
} up_state_t;

bool usb_phones_supported(void);
void usb_phones_open(void);
void usb_phones_close(void);
up_state_t usb_phones_state(void);
// the name of the device (its USB product string), "" with no device
const char *usb_phones_name(void);
// UP_FAILED: the reason ("CABLE": a PC or a charger cable gives 5 V, unplug it first; "HOST"; "FORMAT")
const char *usb_phones_why(void);
// UP_WAIT: true while a cable gives 5 V (the board waits for it to go out, then gives 5 V to the headphones)
bool usb_phones_cable(void);
// the report for the web API (/api/usbphones): the state, the format, the counts and the last events
// (each USB device that came: its VID:PID and its interfaces) as JSON into out; returns the length
int usb_phones_report(char *out, int max);
