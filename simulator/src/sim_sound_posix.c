// The simulator: the sound card of the PC on Linux and macOS. There is no output yet (a null sink): the audio task
// keeps the pace of the I2S with the clock of the board (sim_audio.c), and the meters and the recorder still work.
#ifndef _WIN32
#include "sim.h"

bool sim_sound_open(int rate)
{
    (void)rate;
    return false;
}

void sim_sound_frames(const int16_t *lr, int n, float gain)
{
    (void)lr;
    (void)n;
    (void)gain;
}
#endif
