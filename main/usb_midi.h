#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/usb_midi.h.
#include "tfn/usb_midi.h"

// USB MIDI: the board as a USB MIDI device ("TFN MIDI", class compliant: no driver on
// Windows, macOS, Linux, iOS or Android). It uses TinyUSB on the USB OTG controller.
// The ESP32-S3 has one USB PHY for the native USB port. The serial console (USB Serial/JTAG)
// also uses it, so while USB MIDI is open, the console port (and the flash port) is not on the
// computer. usb_midi_close() gives the PHY back to the console, and the computer finds the
// console port again. A reset also gives it back (the chip starts with the console).

#include <stdbool.h>
#include <stdint.h>

uint32_t usb_midi_received(void);
