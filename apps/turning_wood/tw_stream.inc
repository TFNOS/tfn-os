// THE TURNING WOOD: the music. MP3 loops on the SD card (the data pack: m_<name>.mp3, mono, 24000 Hz, 96 kbps CBR,
// rendered by a music script). A task on core 0 decodes the file (minimp3) into a ring of samples;
// one sample voice of the mixer plays the ring as a loop (the pattern of apps/demo); the audio tick moves its read
// place. The loop: after the skip of the coder, `len` samples, then the start again (sample exact). Included by
// app_wood.c. On the PC (no tasks) the music is silent.
#include "tw_music_tab.inc"

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sndrel/sndrel.h"
// (the copy of minimp3 in this app has names of its own and its own work memory: tfsfx has its own copy in the
// firmware, and the host tests link all apps together)
#define mp3dec_init         tw_mp3dec_init
#define mp3dec_decode_frame tw_mp3dec_decode_frame
#define mp3dec_f32_to_s16   tw_mp3dec_f32_to_s16
#define mp3d_scratch        tw_mp3d_scratch
#define mp3d_mono           tw_mp3d_mono
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3/minimp3.h"

#define MU_RING    24000  // the samples of the voice loop (1 s; a whole number of audio blocks)
#define MU_BLK     120    // AUDIO_BLOCK
#define MU_AHEAD   14400  // the decoder stays up to 0.6 s ahead of the voice
#define MU_START   4800   // the voice starts with 0.2 s of samples
#define MU_GROUP   79
#define MU_FADE_MS 400  // the release of the voice at a new track
#define MU_END_MS  60   // the release of the voice at the end of the music
#define MU_WIN     4096

static int16_t *s_mu_pcm;
static uint8_t *s_mu_win;
static int16_t *s_mu_frame;
static mp3dec_t s_mu_dec;
static FILE *s_mu_f;
static long s_mu_fpos, s_mu_fsize;  // the bytes of the file read now; the size of the file
static int s_mu_rfault;             // the read faults in a row (the file opens again each time)
static char s_mu_path[96];
static volatile int s_mu_req = -1, s_mu_now = -1;  // the track that the game asks for; the track that plays
static volatile uint32_t s_mu_rd, s_mu_wr;         // the samples read by the voice; written by the decoder
static volatile bool s_mu_von, s_mu_vstart, s_mu_quit, s_mu_alive;
static sndrel_t s_mu_rel = SNDREL_INIT;      // the end of the voice: a new track, the end of the music (libs/sndrel)
static volatile int s_mu_fade = MU_FADE_MS;  // the release of that end (ms)
static bool s_mu_ticked;                     // mu_start set the tick
static int s_mu_wn, s_mu_drop, s_mu_fail;
static uint32_t s_mu_pos;  // the place in the loop of the next sample
static TaskHandle_t s_mu_task;
static int s_mu_vol = 70;
static volatile int s_mu_under;  // the blocks that the decoder was late for (the log at the end)

static void mu_tick(void)
{
    if (sndrel_tick(&s_mu_rel)) {
        audio_release_group(MU_GROUP, s_mu_fade);  // (in each block of the end: the voice fades, no voice starts)
        s_mu_von = s_mu_vstart = false;
        return;
    }
    if (audio_muted()) {
        s_mu_von = s_mu_vstart = false;
        return;
    }
    if (s_mu_von) {
        if ((int32_t)(s_mu_wr - s_mu_rd) < MU_BLK) {
            s_mu_under++;
        }
        memset(s_mu_pcm + s_mu_rd % MU_RING, 0, MU_BLK * sizeof(int16_t));  // (an underrun plays silence)
        s_mu_rd += MU_BLK;
    } else if (s_mu_vstart) {
        audio_voice_t v = {0};
        v.dur = 0xF0000000u;
        v.wave = W_SAMPLE;
        v.vol = (uint8_t)s_mu_vol;
        v.attack_ms = 300;
        v.shape = AUDIO_SUSTAIN;
        v.group = MU_GROUP;
        v.sample = s_mu_pcm;
        v.sample_len = MU_RING + 1;
        v.rate = 1.0f;
        v.loop = true;
        audio_voice(&v);
        s_mu_rd = 0;
        s_mu_von = true;
        s_mu_vstart = false;
    }
}
static void mu_seek0(void)
{
    if (!s_mu_f) {
        return;
    }
    fseek(s_mu_f, 0, SEEK_SET);
    s_mu_fpos = 0;
    mp3dec_init(&s_mu_dec);
    s_mu_wn = 0;
    s_mu_drop = TW_MUSIC[s_mu_now].skip;
    s_mu_pos = 0;
}
static bool mu_step(void)
{
    if (s_mu_req != s_mu_now) {
        // a new track: the voice fades out, the ring empties, the file opens
        if (s_mu_von || s_mu_vstart) {
            // (the tick releases the voice and counts the blocks of its fade; then the ring is free for the track)
            s_mu_fade = MU_FADE_MS;
            sndrel_wait(&s_mu_rel, MU_FADE_MS, SNDREL_LIMIT_MS);
            s_mu_von = s_mu_vstart = false;
            sndrel_reset(&s_mu_rel);
        }
        if (s_mu_f) {
            fclose(s_mu_f);
            s_mu_f = NULL;
        }
        s_mu_now = s_mu_req;
        s_mu_fail = 0;
        memset(s_mu_pcm, 0, (MU_RING + 1) * sizeof(int16_t));
        s_mu_rd = s_mu_wr = 0;
        if (s_mu_now >= 0) {
            snprintf(s_mu_path, sizeof s_mu_path, TW_DIR "%s", TW_MUSIC[s_mu_now].file);
            s_mu_f = fopen(s_mu_path, "rb");
            if (!s_mu_f) {
                ESP_LOGW(TAG, "no music %s", s_mu_path);
            } else {
                ESP_LOGI(TAG, "music %s", TW_MUSIC[s_mu_now].file);
                fseek(s_mu_f, 0, SEEK_END);
                s_mu_fsize = ftell(s_mu_f);
                fseek(s_mu_f, 0, SEEK_SET);
            }
            s_mu_rfault = 0;
            mu_seek0();
        }
        return true;
    }
    // (the decoder starts after the loader of tfsfx: the card reads of the effects come first)
    if (!s_mu_f && s_mu_now >= 0 && s_mu_rfault > 0 && s_mu_rfault <= 25) {
        // (the file did not open again after a read fault: one more try after a short wait)
        s_mu_rfault++;
        vTaskDelay(pdMS_TO_TICKS(20));
        s_mu_f = fopen(s_mu_path, "rb");
        if (s_mu_f) {
            fseek(s_mu_f, s_mu_fpos, SEEK_SET);
        }
        return false;
    }
    if (!s_mu_f || audio_muted() || !tfsfx_done()) {
        return false;
    }
    if ((int32_t)(s_mu_wr - s_mu_rd) >= MU_AHEAD) {
        return false;
    }
    if (s_mu_wn < MU_WIN / 2) {
        const int k = (int)fread(s_mu_win + s_mu_wn, 1, (size_t)(MU_WIN - s_mu_wn), s_mu_f);
        if (k > 0) {
            s_mu_wn += k;
            s_mu_fpos += k;
            s_mu_rfault = 0;
        } else if (s_mu_fpos < s_mu_fsize) {
            // (a read fault before the end of the file, for example no internal RAM for the DMA buffer of the card
            // driver for a moment: the file opens again at the same place after a short wait; the music does not
            // start again from its start. After 25 faults in a row the music stops. The code of AFTER HOURS 3D.)
            fclose(s_mu_f);
            s_mu_f = NULL;
            if (++s_mu_rfault > 25) {
                ESP_LOGW(TAG, "music: read faults in %s: the music stops", s_mu_path);
                return false;
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            s_mu_f = fopen(s_mu_path, "rb");
            if (s_mu_f) {
                fseek(s_mu_f, s_mu_fpos, SEEK_SET);
            }
            if (s_mu_rfault == 1) {
                ESP_LOGW(TAG, "music: a read fault at %ld of %s", s_mu_fpos, s_mu_path);
            }
            return true;  // (no decode now: a part frame at the end of the window is not the end)
        }
    }
    mp3dec_frame_info_t info;
    const int n = s_mu_wn > 4 ? mp3dec_decode_frame(&s_mu_dec, s_mu_win, s_mu_wn, s_mu_frame, &info) : 0;
    if (s_mu_wn <= 4 || info.frame_bytes <= 0) {
        if (!TW_MUSIC[s_mu_now].loop) {
            fclose(s_mu_f);
            s_mu_f = NULL;
            return false;
        }
        // (A read fault of the card (an SD timeout) can give no data again and again. If this task
        // starts the file again with no wait, core 0 stays busy and the board stops. So 3 starts with no sample stop
        // the music, and each start waits a little.)
        if (s_mu_pos == 0 && ++s_mu_fail >= 3) {
            ESP_LOGW(TAG, "music: no data from %s: the music stops", TW_MUSIC[s_mu_now].file);
            fclose(s_mu_f);
            s_mu_f = NULL;
            return false;
        }
        mu_seek0();
        return false;
    }
    memmove(s_mu_win, s_mu_win + info.frame_bytes, (size_t)(s_mu_wn - info.frame_bytes));
    s_mu_wn -= info.frame_bytes;
    for (int i = 0; i < n; i++) {
        if (s_mu_drop > 0) {
            s_mu_drop--;
            continue;
        }
        if (s_mu_pos >= TW_MUSIC[s_mu_now].len) {
            if (TW_MUSIC[s_mu_now].loop) {
                mu_seek0();
            } else {
                fclose(s_mu_f);
                s_mu_f = NULL;
            }
            break;
        }
        s_mu_pcm[s_mu_wr % MU_RING] = s_mu_frame[i];
        if (s_mu_wr % MU_RING == 0) {
            s_mu_pcm[MU_RING] = s_mu_frame[i];  // (the sample after the loop point)
        }
        s_mu_wr++;
        s_mu_pos++;
        s_mu_fail = 0;
    }
    if (!s_mu_von && !s_mu_vstart && s_mu_wr >= MU_START) {
        s_mu_vstart = true;
    }
    return true;
}
static void mu_task(void *arg)
{
    (void)arg;
    while (!s_mu_quit) {
        if (!mu_step()) {
            vTaskDelay(pdMS_TO_TICKS(4));
        }
    }
    s_mu_alive = false;
    for (;;) {
        vTaskDelay(portMAX_DELAY);
    }
}
static void mu_start(void)
{
    s_mu_pcm = heap_caps_calloc(MU_RING + 1, sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_mu_win = heap_caps_malloc(MU_WIN, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    s_mu_frame = heap_caps_malloc(MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_mu_pcm || !s_mu_win || !s_mu_frame) {
        return;
    }
    mp3d_mono = 1;
    s_mu_req = s_mu_now = -1;
    s_mu_under = 0;
    s_mu_quit = false;
    s_mu_alive = true;
    sndrel_reset(&s_mu_rel);
    audio_set_tick(mu_tick);
    s_mu_ticked = true;
    if (xTaskCreatePinnedToCoreWithCaps(mu_task, "wood_mu", 4096, NULL, 4, &s_mu_task, 0,
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
        s_mu_task = NULL;
        s_mu_alive = false;
    }
}
static void mu_end(void)
{
    ESP_LOGI(TAG, "music: %d late blocks", s_mu_under);
    s_mu_quit = true;
    for (int i = 0; i < 100 && s_mu_alive; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (s_mu_task && !s_mu_alive) {
        vTaskDeleteWithCaps(s_mu_task);
    }
    s_mu_task = NULL;
    if (s_mu_ticked) {
        // (the voice reads the ring: the tick releases it and counts the blocks of its fade before the ring goes)
        s_mu_fade = MU_END_MS;
        sndrel_wait(&s_mu_rel, MU_END_MS, SNDREL_LIMIT_MS);
        audio_set_tick(NULL);
        s_mu_ticked = false;
    }
    if (s_mu_f) {
        fclose(s_mu_f);
        s_mu_f = NULL;
    }
    heap_caps_free(s_mu_pcm);
    s_mu_pcm = NULL;
    heap_caps_free(s_mu_win);
    s_mu_win = NULL;
    heap_caps_free(s_mu_frame);
    s_mu_frame = NULL;
    s_mu_von = s_mu_vstart = false;
}
static void mu_play(int track)
{
    s_mu_req = track;
}
#else
static void mu_start(void)
{
}
static void mu_end(void)
{
}
static int s_mu_req_pc = -1;
static void mu_play(int track)
{
    s_mu_req_pc = track;
}
#endif
