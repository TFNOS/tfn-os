// The simulator: the recorder of the screen and the sound (for example for the video of an app). It writes each
// frame of the LCD and each block of the synthesizer to two files, with one clock: the count of the sound samples that
// the audio task wrote before the frame. So the picture and the sound of an app stay in step, also when the PC is slow.
//   <prefix>.vid: for each frame a header of 24 bytes ("FRM1", the sample count u64, the time of the PC in us u64,
//                 the width u16, the height u16) and the logical frame (RGB565, the high byte first, as the frame of
//                 the firmware)
//   <prefix>.pcm: the sound as mono 16-bit samples at the rate of the mixer (24000 Hz), before the volume of the board
#include <stdio.h>
#include <string.h>
#include "board.h"
#include "gfx.h"
#include "sim.h"

static sim_mutex_t s_lock = SIM_MUTEX_INIT;
static FILE *s_vid, *s_pcm;
static volatile int64_t s_samples;      // the samples written since the start of the recording
static volatile uint32_t s_nframes;

// prefix NULL or "": stop. The result: the frames of the recording that stops (or 1 = started, -1 = an error).
SIM_API int tfnsim_rec(const char *prefix)
{
    sim_mutex_lock(&s_lock);
    int r = (int)s_nframes;
    if (s_vid) fclose(s_vid);
    if (s_pcm) fclose(s_pcm);
    s_vid = s_pcm = NULL;
    if (prefix && *prefix) {
        char p[1024];
        snprintf(p, sizeof p, "%s.vid", prefix);
        s_vid = sim_os_fopen(p, "wb");
        snprintf(p, sizeof p, "%s.pcm", prefix);
        s_pcm = sim_os_fopen(p, "wb");
        if (!s_vid || !s_pcm) {
            if (s_vid) fclose(s_vid);
            if (s_pcm) fclose(s_pcm);
            s_vid = s_pcm = NULL;
            r = -1;
        } else {
            setvbuf(s_vid, NULL, _IOFBF, 1 << 20);
            r = 1;
        }
        s_samples = 0;
        s_nframes = 0;
    }
    sim_mutex_unlock(&s_lock);
    return r;
}

// the music off (1): midi_bg plays no notes and audio.c no tunes, so a recording has only the effects of an app
volatile int g_sim_music_off;
SIM_API void tfnsim_music_off(int off) { g_sim_music_off = off; }

// the sample count now (the clock of the recording)
SIM_API long long tfnsim_rec_samples(void) { return s_samples; }

// sim_board.c: a frame of the LCD (the logical frame w x h: the 3.49 640 x 172 in landscape, 172 x 640 in portrait; a
// round layout the native frame; a HI frame the panel size)
void sim_rec_frame(const uint16_t *fb, int fw, int fh)
{
    if (!s_vid) return;
    sim_mutex_lock(&s_lock);
    if (s_vid) {
        uint16_t w = (uint16_t)fw, h = (uint16_t)fh;
        int64_t smp = s_samples, us = sim_now_us();
        fwrite("FRM1", 1, 4, s_vid);
        fwrite(&smp, 8, 1, s_vid);
        fwrite(&us, 8, 1, s_vid);
        fwrite(&w, 2, 1, s_vid);
        fwrite(&h, 2, 1, s_vid);
        fwrite(fb, 2, (size_t)w * h, s_vid);
        s_nframes++;
    }
    sim_mutex_unlock(&s_lock);
}

// sim_audio.c: a block of the synthesizer (stereo frames; the left channel is kept: the board mixes mono)
void sim_rec_audio(const int16_t *lr, int frames, int channels)
{
    if (!s_pcm || frames <= 0) return;
    sim_mutex_lock(&s_lock);
    if (s_pcm) {
        int16_t m[512];
        for (int i = 0; i < frames;) {
            int k = frames - i < 512 ? frames - i : 512;
            for (int j = 0; j < k; j++) m[j] = lr[(i + j) * channels];
            fwrite(m, 2, (size_t)k, s_pcm);
            i += k;
        }
        s_samples += frames;
    }
    sim_mutex_unlock(&s_lock);
}
