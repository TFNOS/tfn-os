// The simulator: the codec devices of main/audio.c. The real synthesizer of audio.c runs in its own task; its blocks
// go to the sound card of the PC (sim_sound_*.c). A write waits while all the buffers of the card are full, as the I2S
// DMA of the board waits: so the sound sets the pace of the audio task, and the tick of the apps runs at its real rate.
// With no sound card (headless, or none on the PC), the clock of the board gives the same pace. The microphone gives
// noise at the level of the window, and a sine of a set frequency (for TUNER), at the pace of the I2S reads of the
// board.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "sim.h"

static const char *TAG = "simaudio";

typedef struct {
    esp_codec_dev_type_t type;
    bool open;
    int rate, channels;
    int vol;                    // 0..100 (esp_codec_dev_set_out_vol)
    bool out_mute;
    int64_t t0_us, frames;      // the pace without a sound card (and of the microphone)
    double phase;               // the microphone tone
    uint32_t noise;
} sim_codec_t;

static const audio_codec_if_t s_codec_token = {1};
static const audio_codec_data_if_t s_data_token = {2};
static const audio_codec_ctrl_if_t s_ctrl_token = {3};
static const audio_codec_gpio_if_t s_gpio_token = {4};
const audio_codec_data_if_t *audio_codec_new_i2s_data(audio_codec_i2s_cfg_t *cfg) { (void)cfg; return &s_data_token; }
const audio_codec_ctrl_if_t *audio_codec_new_i2c_ctrl(audio_codec_i2c_cfg_t *cfg) { (void)cfg; return &s_ctrl_token; }
const audio_codec_gpio_if_t *audio_codec_new_gpio(void) { return &s_gpio_token; }
const audio_codec_if_t *es8311_codec_new(es8311_codec_cfg_t *cfg) { (void)cfg; return &s_codec_token; }
const audio_codec_if_t *es7210_codec_new(es7210_codec_cfg_t *cfg) { (void)cfg; return &s_codec_token; }
const audio_codec_if_t *aw88298_codec_new(aw88298_codec_cfg_t *cfg) { (void)cfg; return &s_codec_token; }
const audio_codec_if_t *es8388_codec_new(es8388_codec_cfg_t *cfg) { (void)cfg; return &s_codec_token; }
const audio_codec_if_t *es7243_codec_new(es7243_codec_cfg_t *cfg) { (void)cfg; return &s_codec_token; }
const audio_codec_if_t *es7243e_codec_new(es7243e_codec_cfg_t *cfg) { (void)cfg; return &s_codec_token; }

// ---------------------------------------------------------------- the window settings
static volatile bool s_on = true;               // the sound of the PC on
static volatile float s_master = 0.6f;          // the volume of the PC (0..1)
static volatile float s_mic_level, s_mic_hz;
static volatile int s_meter;                    // the output level 0..100
static bool s_headless;                         // no sound card (tfnsim_options)
static uint32_t s_codecs;                       // the codec devices made (a fixed start of each noise)

void sim_audio_headless(void)
{
    s_headless = true;
}

void sim_audio_config(bool on, float master) { s_on = on; s_master = master < 0 ? 0 : master > 1 ? 1 : master; }
void sim_audio_mic(float level, float tone_hz) { s_mic_level = level < 0 ? 0 : level > 1 ? 1 : level; s_mic_hz = tone_hz; }
int sim_audio_level(void) { return s_meter; }

// the meter: the level of the sound of the board (after its volume and amplifier, before the volume of the PC)
static void meter(const int16_t *lr, int n, float gain)
{
    int peak = 0;
    for (int i = 0; i < 2 * n; i += 2) {
        int v = abs(lr[i]);
        if (v > peak) peak = v;
    }
    int m = (int)(peak * gain * 100 / 32768);
    s_meter = m > s_meter ? m : (s_meter * 7 + m) / 8;
}

// ---------------------------------------------------------------- the checks (simulator/sound_check.py, sim_check.c)
static volatile int s_late_ms;          // the audio task is late by this time after each block (tfnsim_audio_late)
static volatile bool s_hold, s_parked;  // a check holds the audio task after its block; it waits now

void sim_audio_late(int ms)
{
    s_late_ms = ms < 0 ? 0 : ms;
}

void sim_audio_hold(bool on)
{
    s_hold = on;
}

bool sim_audio_parked(void)
{
    return s_parked;
}

// after the write of a block (the audio task is out of its lock: main/audio.c): the late time, then the hold
static void out_wait(void)
{
    if (s_late_ms > 0) {
        sim_sleep_us((int64_t)s_late_ms * 1000);
    }
    if (s_hold) {
        s_parked = true;
        while (s_hold) {
            sim_sleep_us(1000);
        }
        s_parked = false;
    }
}

// the pace without a sound card: wait until the time of the frames that came
static void pace(sim_codec_t *c, int frames)
{
    int64_t now = sim_now_us();
    if (!c->t0_us || now - (c->t0_us + c->frames * 1000000 / c->rate) > 100000) {
        c->t0_us = now;                                 // (a start, or a long stop: no catch-up)
        c->frames = 0;
    }
    c->frames += frames;
    int64_t due = c->t0_us + c->frames * 1000000 / c->rate;
    if (due > now) sim_sleep_us(due - now);
}

// ---------------------------------------------------------------- the devices
esp_codec_dev_handle_t esp_codec_dev_new(esp_codec_dev_cfg_t *cfg)
{
    if (!cfg || !cfg->codec_if) return NULL;
    sim_codec_t *c = calloc(1, sizeof *c);
    c->type = cfg->dev_type;
    c->vol = 60;
    c->rate = 24000;
    c->channels = 2;
    c->noise = 0x12345678u ^ (++s_codecs * 0x9E3779B9u);
    return c;
}

void esp_codec_dev_delete(esp_codec_dev_handle_t dev) { free(dev); }

int esp_codec_dev_open(esp_codec_dev_handle_t dev, esp_codec_dev_sample_info_t *fs)
{
    sim_codec_t *c = dev;
    if (!c) return ESP_CODEC_DEV_INVALID_ARG;
    if (fs && fs->sample_rate) c->rate = (int)fs->sample_rate;
    if (fs && fs->channel) c->channels = fs->channel;
    c->open = true;
    c->t0_us = 0;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_close(esp_codec_dev_handle_t dev)
{
    sim_codec_t *c = dev;
    if (c) c->open = false;
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_write(esp_codec_dev_handle_t dev, void *data, int len)
{
    sim_codec_t *c = dev;
    if (!c || !c->open || !(c->type & ESP_CODEC_DEV_TYPE_OUT)) return ESP_CODEC_DEV_WRONG_STATE;
    int frames = len / (2 * c->channels);
    sim_rec_audio((const int16_t *)data, frames, c->channels);
    if (c->channels != 2 || frames <= 0) {
        pace(c, frames > 0 ? frames : 1);
        out_wait();
        return ESP_CODEC_DEV_OK;
    }
    // the volume of the codec (the board: about 0.5 dB a step near the top) and the amplifier, then the PC volume
    float v = c->out_mute ? 0 : (float)c->vol / 100.0f;
    float board = v * v * (sim_board_speaker_on() ? 1.0f : 0.0f);
    meter((const int16_t *)data, frames, board);
    if (!s_headless && sim_sound_open(c->rate)) {
        sim_sound_frames((const int16_t *)data, frames, board * s_master * (s_on ? 1.0f : 0.0f));
    } else {
        pace(c, frames);
    }
    out_wait();
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_read(esp_codec_dev_handle_t dev, void *data, int len)
{
    sim_codec_t *c = dev;
    if (!c || !c->open || !(c->type & ESP_CODEC_DEV_TYPE_IN)) return ESP_CODEC_DEV_WRONG_STATE;
    int frames = len / (2 * c->channels);
    int16_t *s = data;
    float amp = s_mic_level * 9000.0f, hz = s_mic_hz;
    double step = hz > 0 ? 6.283185307179586 * hz / c->rate : 0;
    for (int i = 0; i < frames; i++) {
        c->noise = c->noise * 1664525u + 1013904223u;
        float n = ((int)(c->noise >> 16) - 32768) / 32768.0f;
        float v = hz > 0 ? amp * (0.85f * (float)sin(c->phase) + 0.15f * n) : amp * n;
        c->phase += step;
        if (c->phase > 6.283185307179586) c->phase -= 6.283185307179586;
        for (int ch = 0; ch < c->channels; ch++) s[i * c->channels + ch] = (int16_t)v;
    }
    pace(c, frames);
    return ESP_CODEC_DEV_OK;
}

int esp_codec_dev_set_out_vol(esp_codec_dev_handle_t dev, int volume)
{
    sim_codec_t *c = dev;
    if (c) c->vol = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    return ESP_CODEC_DEV_OK;
}
int esp_codec_dev_get_out_vol(esp_codec_dev_handle_t dev, int *volume)
{
    sim_codec_t *c = dev;
    if (c && volume) *volume = c->vol;
    return ESP_CODEC_DEV_OK;
}
int esp_codec_dev_set_out_mute(esp_codec_dev_handle_t dev, bool mute)
{
    sim_codec_t *c = dev;
    if (c) c->out_mute = mute;
    return ESP_CODEC_DEV_OK;
}
int esp_codec_dev_set_in_gain(esp_codec_dev_handle_t dev, float db) { (void)dev; (void)db; return ESP_CODEC_DEV_OK; }
int esp_codec_dev_set_in_channel_gain(esp_codec_dev_handle_t dev, uint16_t mask, float db) { (void)dev; (void)mask; (void)db; return ESP_CODEC_DEV_OK; }
int esp_codec_dev_set_in_mute(esp_codec_dev_handle_t dev, bool mute) { (void)dev; (void)mute; return ESP_CODEC_DEV_OK; }
