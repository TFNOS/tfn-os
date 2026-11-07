// The simulator: the sound card of the PC on Windows (waveOut). The blocks of the synthesizer go into NBUF buffers of
// BUF_FRAMES; a write waits while all the buffers are in the card, as the I2S DMA of the board waits.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "sim.h"

#define NBUF        6
#define BUF_FRAMES  240             // 10 ms at 24 kHz: up to 60 ms in the card

static const char *TAG = "simaudio";
static HWAVEOUT s_wo;
static HANDLE s_wo_event;
static WAVEHDR s_hdr[NBUF];
static int16_t *s_buf[NBUF];
static int s_cur, s_fill;           // the buffer that fills now, its frames
static bool s_wo_tried;

bool sim_sound_open(int rate)
{
    if (s_wo_tried) {
        return s_wo != NULL;
    }
    s_wo_tried = true;
    WAVEFORMATEX wf = {0};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;
    wf.nSamplesPerSec = (DWORD)rate;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = 4;
    wf.nAvgBytesPerSec = (DWORD)rate * 4;
    s_wo_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (waveOutOpen(&s_wo, WAVE_MAPPER, &wf, (DWORD_PTR)s_wo_event, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        s_wo = NULL;
        ESP_LOGW(TAG, "no sound card: the sound is silent (the pace stays)");
        return false;
    }
    for (int i = 0; i < NBUF; i++) {
        s_buf[i] = calloc(BUF_FRAMES * 2, sizeof(int16_t));
        s_hdr[i].lpData = (LPSTR)s_buf[i];
        s_hdr[i].dwBufferLength = BUF_FRAMES * 4;
        waveOutPrepareHeader(s_wo, &s_hdr[i], sizeof(WAVEHDR));
        s_hdr[i].dwFlags |= WHDR_DONE;
    }
    return true;
}

static int16_t clip16(float v)
{
    int i = (int)lrintf(v);
    if (i > 32767) {
        return 32767;
    }
    if (i < -32768) {
        return -32768;
    }
    return (int16_t)i;
}

// a full buffer goes out, then the next one must be free (this is the pace)
void sim_sound_frames(const int16_t *lr, int n, float gain)
{
    while (n > 0) {
        WAVEHDR *h = &s_hdr[s_cur];
        while (s_fill == 0 && !(h->dwFlags & WHDR_DONE)) {
            WaitForSingleObject(s_wo_event, 20);
        }
        int k = BUF_FRAMES - s_fill < n ? BUF_FRAMES - s_fill : n;
        int16_t *d = s_buf[s_cur] + s_fill * 2;
        for (int i = 0; i < 2 * k; i++) {
            d[i] = clip16(lr[i] * gain);
        }
        s_fill += k;
        lr += 2 * k;
        n -= k;
        if (s_fill == BUF_FRAMES) {
            h->dwFlags &= ~WHDR_DONE;
            waveOutWrite(s_wo, h, sizeof(WAVEHDR));
            s_cur = (s_cur + 1) % NBUF;
            s_fill = 0;
        }
    }
}
#endif
