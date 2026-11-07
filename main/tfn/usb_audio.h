#pragma once

// USB AUDIO: the board as a USB sound input for a computer ("TFN SOUND", USB Audio Class 2, one channel, 48 kHz,
// 16 bit: no driver on Windows 10/11, macOS or Linux). The sound that goes to the computer is the microphone of the
// board, the sound of the board (games, music, ON AIR), or both (usb_audio_set_src).
// It uses TinyUSB on the USB OTG controller, as USB MIDI does. The two cannot be open at the same time. While USB AUDIO
// is open, the console port (and the flash port) is not on the computer; usb_audio_close() gives the PHY back to the
// console. A reset also gives it back.
// SOUND TO USB (the setting, store key "snd_usb"): while a computer reads the sound of the board, the speaker is quiet.

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UA_OFF,     // not open
    UA_WAIT,    // open, no computer has set up the device yet (no cable, or a charger)
    UA_LINKED,  // a computer has the device (it reads the sound only while an app records or listens)
    UA_FAILED,  // the USB driver did not start (USB MIDI open?)
} ua_state_t;

#define UA_SRC_MIC   1  // the microphone of the board
#define UA_SRC_BOARD 2  // the sound of the board (the mix of the speaker)

void usb_audio_open(int src);
void usb_audio_close(void);
void usb_audio_set_src(int src);
int usb_audio_src(void);
ua_state_t usb_audio_state(void);
// true while the computer reads the sound (an app records, or "Listen to this device")
bool usb_audio_streaming(void);
// the level of the sound that goes out (0 .. 1, the RMS of the last 10 ms)
float usb_audio_level(void);
// the samples that went out (48 kHz), and the samples that the buffer dropped (the computer did not read them)
uint32_t usb_audio_sent(void);
uint32_t usb_audio_dropped(void);
// true: the mode is 1 (a computer)
bool usb_audio_setting(void);
