#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/usb_audio.h.
#include "tfn/usb_audio.h"

// USB AUDIO: the board as a USB sound input for a computer ("TFN SOUND", USB Audio Class 2, one channel, 48 kHz,
// 16 bit: no driver on Windows 10/11, macOS or Linux). The sound that goes to the computer is the microphone of the
// board, the sound of the board (games, music, ON AIR), or both (usb_audio_set_src).
// It uses TinyUSB on the USB OTG controller, as USB MIDI does. The two cannot be open at the same time. While USB AUDIO
// is open, the console port (and the flash port) is not on the computer; usb_audio_close() gives the PHY back to the
// console. A reset also gives it back.
// SOUND TO USB (the setting, store key "snd_usb"): while a computer reads the sound of the board, the speaker is quiet.

#include <stdbool.h>
#include <stdint.h>

// SOUND OUT (store key "snd_usb"): 0 the speaker, 1 a computer (SOUND TO USB), 2 USB-C headphones (only the CoreS3,
// main/usb_phones.h). At the start the board opens the mode (call after audio_init).
void usb_audio_boot(void);
int usb_audio_mode(void);
void usb_audio_set_mode(int mode);
