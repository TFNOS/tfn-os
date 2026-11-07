// The link test: a small copy of the sound API of the OS (main/audio.h)
#pragma once
#include <stdint.h>

typedef enum {
    W_SQUARE,
} wave_t;
void sfx_tone(uint16_t f0, uint16_t f1, uint16_t dur_ms, wave_t wave, uint8_t vol);
