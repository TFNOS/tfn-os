#pragma once

// USB MIDI: the board as a USB MIDI device ("TFN MIDI", class compliant: no driver on
// Windows, macOS, Linux, iOS or Android). It uses TinyUSB on the USB OTG controller.
// The ESP32-S3 has one USB PHY for the native USB port. The serial console (USB Serial/JTAG)
// also uses it, so while USB MIDI is open, the console port (and the flash port) is not on the
// computer. usb_midi_close() gives the PHY back to the console, and the computer finds the
// console port again. A reset also gives it back (the chip starts with the console).

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UM_OFF,     // not open
    UM_WAIT,    // open, no computer has set up the device yet (no cable, or a charger)
    UM_LINKED,  // a computer uses the device: messages go out
    UM_FAILED,  // the USB driver did not start
} um_state_t;

void usb_midi_open(void);
void usb_midi_close(void);
um_state_t usb_midi_state(void);

// Send one channel message (2 or 3 bytes). Returns false if it is dropped (no link, or full).
bool usb_midi_send(const uint8_t *msg, int len);
// Read the messages from the computer (the MIDI clock). Call once per frame.
void usb_midi_poll(void);
uint32_t usb_midi_sent(void);
// The MIDI clock from the computer: the tempo in BPM, or 0 when no clock came in the last
// second. usb_midi_starts() counts Start and Continue messages.
float usb_midi_clock_bpm(void);
uint32_t usb_midi_starts(void);
