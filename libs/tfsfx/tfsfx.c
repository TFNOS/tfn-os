// libs/tfsfx: the MP3 sound effects of an app from the SD card (see tfsfx.h, sdk/docs/sound.md).
// A loader task reads each file and decodes it with minimp3 (mono) into PSRAM; a file of another rate keeps its rate
// (the voice plays it at rate * hz / AUDIO_SAMPLE_RATE). An effect is ready when its length is set (the loader writes
// the samples first). The release at the end goes through the tick of the audio task (audio_release_group may not run
// on another task), then the memory goes.
// A module of the firmware: the public functions are ABI names, so an app file calls them and has no copy of the
// module.
// All the state is zero at the start (.bss only, no .data): the "psram" list of main/apps.json puts it in PSRAM.
#include "tfsfx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#if defined(ESP_PLATFORM) || defined(TFN_SIMULATOR)
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "tfn/audio.h"
// (the copy of minimp3 in this module has names of its own: an app such as ON AIR has its own copy, and a build of all
// the apps (the host fuzz, the simulator) must not have a name two times)
#define mp3dec_init         tfsfx_mp3dec_init
#define mp3dec_decode_frame tfsfx_mp3dec_decode_frame
#define mp3dec_f32_to_s16   tfsfx_mp3dec_f32_to_s16
#define mp3d_scratch        tfsfx_mp3d_scratch
#define mp3d_mono           tfsfx_mp3d_mono
#define MINIMP3_IMPLEMENTATION
#include "minimp3/minimp3.h"

static const char *TAG = "tfsfx";

#define FX_MAX_SAMPLES (AUDIO_SAMPLE_RATE * 3)  // (3 s: a longer file is cut)
#define FX_MAX_FILE    (256 * 1024)

typedef struct {
    int16_t *pcm;
    volatile uint32_t n;  // the samples (0 = not ready)
    float base;           // the rate of the file: its hz / AUDIO_SAMPLE_RATE
    int64_t last_us;      // the last start (the 25 ms guard)
} fx_t;

static fx_t s_fx[TFSFX_MAX];
static const char *const *s_names;
static const uint8_t *const *s_mem;  // tfsfx_load_mem: the MP3 data in memory (NULL: the files of s_dir)
static const uint32_t *s_mlen;
static char s_dir[64];
static int s_n;
static uint8_t s_group;  // (load_start sets it)
static volatile int s_ready;
static volatile bool s_busy, s_stop, s_rel;  // (s_busy: the loader works; false = done)
static int64_t s_end_us;                     // the end of the last effect that started (the wait of tfsfx_free)
static mp3dec_t s_dec;
static mp3d_sample_t s_frame[MINIMP3_MAX_SAMPLES_PER_FRAME];

static void *ps_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static int16_t *decode_mp3(const uint8_t *mp3, long len, const char *path, uint32_t *out_n, float *base);
// one file -> mono samples in PSRAM (NULL: no file or no sound in it)
static int16_t *decode(const char *path, uint32_t *out_n, float *base)
{
    *out_n = 0;
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGW(TAG, "%s: no file", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *mp3 = len > 0 && len <= FX_MAX_FILE ? ps_alloc((size_t)len) : NULL;
    bool ok = mp3 && fread(mp3, 1, (size_t)len, f) == (size_t)len;
    fclose(f);
    if (!ok) {
        ESP_LOGW(TAG, "%s: %ld bytes: not read (%d KB at most)", path, len, FX_MAX_FILE / 1024);
        free(mp3);
        return NULL;
    }
    int16_t *pcm = decode_mp3(mp3, len, path, out_n, base);
    free(mp3);
    return pcm;
}
// MP3 data -> mono samples in PSRAM (NULL: no sound in it)
static int16_t *decode_mp3(const uint8_t *mp3, long len, const char *path, uint32_t *out_n, float *base)
{
    *out_n = 0;
    // the samples: a first pass counts them (minimp3 with no pcm only reads the frame heads)
    mp3dec_frame_info_t info;
    uint32_t total = 0;
    int hz = 0;
    mp3dec_init(&s_dec);
    for (long pos = 0; pos < len && !s_stop;) {
        int k = mp3dec_decode_frame(&s_dec, mp3 + pos, (int)(len - pos), NULL, &info);
        if (!info.frame_bytes) {
            break;
        }
        pos += info.frame_bytes;
        total += (uint32_t)k;
        if (!hz) {
            hz = info.hz;
        }
    }
    if (total > FX_MAX_SAMPLES) {
        total = FX_MAX_SAMPLES;
    }
    int16_t *pcm =
            total >= 64 ? ps_alloc(total * sizeof(int16_t) + MINIMP3_MAX_SAMPLES_PER_FRAME * sizeof(int16_t)) : NULL;
    uint32_t n = 0;
    if (pcm) {
        mp3dec_init(&s_dec);
        mp3d_mono = 1;  // (a stereo file mixes to mono: the speaker is mono)
        for (long pos = 0; pos < len && n < total && !s_stop;) {
            int k = mp3dec_decode_frame(&s_dec, mp3 + pos, (int)(len - pos), s_frame, &info);
            if (!info.frame_bytes) {
                break;
            }
            pos += info.frame_bytes;
            if (k <= 0) {
                continue;
            }
            if (n + (uint32_t)k > total) {
                k = (int)(total - n);
            }
            memcpy(pcm + n, s_frame, (size_t)k * sizeof(int16_t));
            n += (uint32_t)k;
        }
        // the silence at the start (the delay of the MP3 coder, about 50 ms) goes: a hit must sound at once
        uint32_t lead = 0, lmax = n / 2 < AUDIO_SAMPLE_RATE / 6 ? n / 2 : AUDIO_SAMPLE_RATE / 6;
        while (lead < lmax && pcm[lead] > -48 && pcm[lead] < 48) {
            lead++;
        }
        if (lead) {
            memmove(pcm, pcm + lead, (size_t)(n - lead) * sizeof(int16_t));
            n -= lead;
        }
        // a short fade at the end (a cut file must not click)
        uint32_t fade = n < 480 ? n / 2 : 240;
        for (uint32_t i = 0; i < fade; i++) {
            pcm[n - 1 - i] = (int16_t)(pcm[n - 1 - i] * (int32_t)i / (int32_t)fade);
        }
    }
    if (n < 64) {
        ESP_LOGW(TAG, "%s: no sound in it", path);
        free(pcm);
        return NULL;
    }
    *base = hz > 0 ? (float)hz / AUDIO_SAMPLE_RATE : 1.0f;
    *out_n = n;
    return pcm;
}

static void load_all(void)
{
    for (int i = 0; i < s_n && !s_stop; i++) {
        char path[112];
        snprintf(path, sizeof path, "%s/%s.mp3", s_dir, s_names[i]);
        uint32_t n = 0;
        float base = 1.0f;
        int16_t *p = s_mem ? (s_mem[i] && s_mlen[i] ? decode_mp3(s_mem[i], (long)s_mlen[i], path, &n, &base) : NULL)
                           : decode(path, &n, &base);
        if (!p) {
            continue;
        }
        s_fx[i].pcm = p;
        s_fx[i].base = base;
        __atomic_store_n(&s_fx[i].n, n, __ATOMIC_RELEASE);  // (ready: the samples are there)
        s_ready++;
    }
    ESP_LOGI(TAG, "%s: %d of %d effects", s_dir, s_ready, s_n);
}

#ifdef ESP_PLATFORM
static void loader(void *arg)
{
    (void)arg;
    // (a task of the firmware, not of the app: the loader of app files (main/tfn_load.c) calls tfsfx_free at the
    // unload of the app of the last tfsfx_load, so this task never reads the names or the data of an app that went)
    load_all();
    s_busy = false;
    // (a task made with xTaskCreatePinnedToCoreWithCaps must go with vTaskDeleteWithCaps: vTaskDelete leaves its stack
    // (4 KB of PSRAM) and a part of its control block taken at each tfsfx_load)
    vTaskDeleteWithCaps(NULL);
}
#endif

static void load_start(const char *dir, const char *const *names, int n, uint8_t group);
void tfsfx_load_mem(const uint8_t *const *data, const uint32_t *len, const char *const *names, int n, uint8_t group)
{
    tfsfx_free();
    s_mem = data;
    s_mlen = len;
    load_start(NULL, names, n, group);
}
void tfsfx_load(const char *dir, const char *const *names, int n, uint8_t group)
{
    tfsfx_free();
    s_mem = NULL;
    s_mlen = NULL;
    load_start(dir, names, n, group);
}
static void load_start(const char *dir, const char *const *names, int n, uint8_t group)
{
    s_names = names;
    s_n = n > TFSFX_MAX ? TFSFX_MAX : n < 0 ? 0 : n;
    s_group = group ? group : 200;
    snprintf(s_dir, sizeof s_dir, "%s", dir ? dir : "(memory)");
    s_ready = 0;
    s_stop = false;
    s_busy = true;
#ifdef ESP_PLATFORM
    // (the stack in PSRAM: the loader reads the card and never writes the flash; the decoder work memory is static)
    if (xTaskCreatePinnedToCoreWithCaps(loader, "tfsfx", 4096, NULL, 2, NULL, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) !=
        pdPASS) {
        ESP_LOGW(TAG, "no loader task: the effects load now");
        load_all();
        s_busy = false;
    }
#else
    load_all();  // (the host: at once)
    s_busy = false;
#endif
}

int tfsfx_ready(void)
{
    return s_ready;
}
bool tfsfx_done(void)
{
    return !s_busy;
}

int tfsfx_ms(int id)
{
    if (id < 0 || id >= s_n) {
        return 0;
    }
    uint32_t n = __atomic_load_n(&s_fx[id].n, __ATOMIC_ACQUIRE);
    return n ? (int)((float)n / s_fx[id].base * 1000.0f / AUDIO_SAMPLE_RATE) : 0;
}

void tfsfx_play(int id, int vol, float rate)
{
    if (id < 0 || id >= s_n || s_rel || audio_muted()) {
        return;
    }
    uint32_t n = __atomic_load_n(&s_fx[id].n, __ATOMIC_ACQUIRE);
    if (!n) {
        return;
    }
    int64_t now = esp_timer_get_time();
    if (now - s_fx[id].last_us < 25000) {
        return;
    }
    s_fx[id].last_us = now;
    float r = (rate < 0.25f ? 0.25f : rate > 4.0f ? 4.0f : rate) * s_fx[id].base;
    audio_voice_t v = {0};
    v.wave = W_SAMPLE;
    v.sample = s_fx[id].pcm;
    v.sample_len = n;
    v.rate = r;
    v.dur = (uint32_t)((float)n / r) + 64;  // (the voice ends at the end of the samples)
    v.vol = (uint8_t)(vol < 0 ? 0 : vol > 100 ? 100 : vol);
    v.attack_ms = 1;
    v.shape = AUDIO_SUSTAIN;
    v.group = s_group;
    audio_play(&v);
    int64_t end = now + (int64_t)((float)n / r * 1000000.0f / AUDIO_SAMPLE_RATE);
    if (end > s_end_us) {
        s_end_us = end;
    }
}

void tfsfx_play_var(int id, int vol, float var)
{
    float u = (float)(esp_random() & 0xFFFF) / 65535.0f * 2.0f - 1.0f;
    int v = vol * (85 + (int)(esp_random() % 16)) / 100;
    tfsfx_play(id, v, 1.0f + var * u);
}

#if defined(ESP_PLATFORM) || defined(TFN_SIMULATOR)
// The soft end of the effects that still sound: for a moment the tick of the audio task releases the group in each
// block (also an effect that the queue starts in the block after the first release) and counts the blocks of the fade.
#define REL_MS      30
#define BLOCK_MS    (AUDIO_BLOCK * 1000 / AUDIO_SAMPLE_RATE)
#define REL_BLOCKS  ((REL_MS + BLOCK_MS - 1) / BLOCK_MS + 3)  // (the fade, the block of a late start, one more)
#define REL_WAIT_US 300000  // (the time limit with no block: the audio task does not run, for example the sound sleeps)
static volatile int s_rel_left;  // the blocks that the release still counts
static void rel_tick(void)
{
    audio_release_group(s_group, REL_MS);
    if (s_rel_left > 0) {
        s_rel_left--;
    }
}
#endif

void tfsfx_free(void)
{
    // (the board and the simulator: the audio task plays the samples at the same time. The effects that still sound
    // get a soft end; then audio_stop_mem stops each sound that still reads the samples, also when the audio task did
    // not count the fade in the time limit. So the free never comes before the end of a voice.)
    s_stop = true;
#if defined(ESP_PLATFORM) || defined(TFN_SIMULATOR)
    for (int i = 0; i < 300 && s_busy; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));  // (the loader stops after a frame)
    }
    if (esp_timer_get_time() < s_end_us) {
        s_rel = true;
        int left = REL_BLOCKS;
        s_rel_left = left;
        audio_set_tick(rel_tick);
        int64_t end = esp_timer_get_time() + REL_WAIT_US;
        while (s_rel_left > 0) {
            if (s_rel_left != left) {
                left = s_rel_left;  // (a block came: the time limit starts again)
                end = esp_timer_get_time() + REL_WAIT_US;
            } else if (esp_timer_get_time() >= end) {
                break;
            }
            vTaskDelay(1);
        }
        audio_set_tick(NULL);  // (rel_tick gets no call after this)
    }
#endif
    for (int i = 0; i < TFSFX_MAX; i++) {
        const uint32_t n = __atomic_load_n(&s_fx[i].n, __ATOMIC_ACQUIRE);
        __atomic_store_n(&s_fx[i].n, 0, __ATOMIC_RELEASE);  // (no new play of the effect)
#if defined(ESP_PLATFORM) || defined(TFN_SIMULATOR)
        audio_stop_mem(s_fx[i].pcm, (size_t)n * sizeof(int16_t));  // (no sound reads its samples after this)
#else
        (void)n;  // (the host tests: no mixer)
#endif
        free(s_fx[i].pcm);
        s_fx[i].pcm = NULL;
        s_fx[i].last_us = 0;
    }
    s_n = s_ready = 0;
    s_end_us = 0;
    s_rel = false;
    s_busy = false;
}
