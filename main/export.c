#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "board.h"
#include "settings.h"
#include "storage.h"
#include "export.h"

bool export_on_card(void)
{
    return store_get("sv_place", 0) == 0 && board_sd_mounted();
}
bool export_place_ask(void)
{
    return !store_get("sv_asked", 0) && board_sd_mounted();
}

void export_place_set(bool card)
{
    store_set("sv_place", card ? 0 : 1);
    store_set("sv_asked", 1);
}

void export_path(const char *sub, const char *name, bool rd, char *out, int len)
{
    bool card = export_on_card();
    snprintf(out, len, "%s/%s/%s", card ? BOARD_SD_ROOT : STORAGE_ROOT, sub, name);
    struct stat st;
    if (rd) {
        if (stat(out, &st) == 0 || (!card && !board_sd_mounted())) {
            return;
        }
        char o[96];
        snprintf(o, sizeof o, "%s/%s/%s", card ? STORAGE_ROOT : BOARD_SD_ROOT, sub, name);
        if (stat(o, &st) == 0) {
            snprintf(out, len, "%s", o);
        }
        return;
    }
    char d[48];
    snprintf(d, sizeof d, "%s/%s", card ? BOARD_SD_ROOT : STORAGE_ROOT, sub);
    if (stat(d, &st) != 0) {
        mkdir(d, 0777);
    }
}

void export_next_name(const char *prefix, char *out, int len)
{
    char dir[48];
    snprintf(dir, sizeof dir, "%s/exports", export_on_card() ? BOARD_SD_ROOT : STORAGE_ROOT);
    struct stat st;
    if (stat(dir, &st) != 0) {
        mkdir(dir, 0777);
    }
    uint32_t n = store_get("exp_n", 0) + 1;
    for (int k = 0; k < 1000; k++, n++) {
        snprintf(out, len, "%s/%s_%03lu.wav", dir, prefix, (unsigned long)n);
        if (stat(out, &st) != 0) {
            break;
        }
    }
    store_set("exp_n", n);
    snprintf(out, len, "%s/%s_%03lu", dir, prefix, (unsigned long)n);
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = v;
    p[1] = v >> 8;
    p[2] = v >> 16;
    p[3] = v >> 24;
}
static void put16(uint8_t *p, uint16_t v)
{
    p[0] = v;
    p[1] = v >> 8;
}

static volatile int s_progress = 100;

static bool write_wav(const char *path, const int16_t *samples, uint32_t n, uint32_t rate, bool report)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return false;
    }
    uint8_t h[44];
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + n * 2);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);  // PCM
    put16(h + 22, 1);  // mono
    put32(h + 24, rate);
    put32(h + 28, rate * 2);
    put16(h + 32, 2);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, n * 2);
    bool ok = fwrite(h, 1, 44, f) == 44;
    // little-endian CPU: the samples go as they are, in parts
    for (uint32_t i = 0; ok && i < n; i += 8192) {
        uint32_t k = n - i < 8192 ? n - i : 8192;
        ok = fwrite(samples + i, 2, k, f) == k;
        if (report) {
            s_progress = (int)((uint64_t)(i + k) * 95 / n);
        }
    }
    fclose(f);
    return ok;
}

bool export_wav(const char *path, const int16_t *samples, uint32_t n, uint32_t rate)
{
    return write_wav(path, samples, n, rate, false);
}

bool midi_begin(midi_t *m, int cap)
{
    m->ev = heap_caps_malloc(sizeof(midi_ev_t) * cap, MALLOC_CAP_SPIRAM);
    m->n = 0;
    m->cap = m->ev ? cap : 0;
    return m->ev != NULL;
}

static void add(midi_t *m, uint32_t t, uint8_t st, uint8_t d1, uint8_t d2)
{
    if (m->n < m->cap) {
        m->ev[m->n++] = (midi_ev_t){t, st, d1, d2};
    }
}

void midi_note(midi_t *m, uint32_t t0, uint32_t t1, int ch, int note, int vel)
{
    if (note < 0 || note > 127) {
        return;
    }
    if (t1 <= t0) {
        t1 = t0 + 1;
    }
    add(m, t0, (uint8_t)(0x90 | ch), (uint8_t)note, (uint8_t)(vel > 127 ? 127 : vel));
    add(m, t1, (uint8_t)(0x80 | ch), (uint8_t)note, 0);
}

void midi_program(midi_t *m, int ch, int program)
{
    add(m, 0, (uint8_t)(0xC0 | ch), (uint8_t)program, 0);
}

static int cmp_ev(const void *a, const void *b)
{
    const midi_ev_t *x = a, *y = b;
    if (x->tick != y->tick) {
        return x->tick < y->tick ? -1 : 1;
    }
    // note-off before note-on at the same tick
    int ox = (x->status & 0xF0) == 0x80 ? 0 : 1, oy = (y->status & 0xF0) == 0x80 ? 0 : 1;
    return ox - oy;
}

static int varlen(uint8_t *p, uint32_t v)
{
    uint8_t tmp[5];
    int n = 0;
    tmp[n++] = v & 0x7F;
    while (v >>= 7) {
        tmp[n++] = (uint8_t)(0x80 | (v & 0x7F));
    }
    for (int i = 0; i < n; i++) {
        p[i] = tmp[n - 1 - i];
    }
    return n;
}

bool midi_save(midi_t *m, const char *path, int bpm)
{
    qsort(m->ev, m->n, sizeof(midi_ev_t), cmp_ev);
    uint8_t *trk = heap_caps_malloc(m->n * 8 + 64, MALLOC_CAP_SPIRAM);
    if (!trk) {
        return false;
    }
    int p = 0;
    uint32_t us = 60000000u / (uint32_t)(bpm > 0 ? bpm : 120);
    trk[p++] = 0;
    trk[p++] = 0xFF;
    trk[p++] = 0x51;
    trk[p++] = 3;  // tempo
    trk[p++] = us >> 16;
    trk[p++] = us >> 8;
    trk[p++] = us;
    uint32_t last = 0;
    for (int i = 0; i < m->n; i++) {
        p += varlen(trk + p, m->ev[i].tick - last);
        last = m->ev[i].tick;
        trk[p++] = m->ev[i].status;
        trk[p++] = m->ev[i].d1;
        if ((m->ev[i].status & 0xF0) != 0xC0) {
            trk[p++] = m->ev[i].d2;
        }
    }
    trk[p++] = 0;
    trk[p++] = 0xFF;
    trk[p++] = 0x2F;
    trk[p++] = 0;  // end of track
    uint8_t hdr[22] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k'};
    hdr[18] = p >> 24;
    hdr[19] = p >> 16;
    hdr[20] = p >> 8;
    hdr[21] = p;
    FILE *f = fopen(path, "wb");
    bool ok = f && fwrite(hdr, 1, 22, f) == 22 && fwrite(trk, 1, p, f) == (size_t)p;
    if (f) {
        fclose(f);
    }
    free(trk);
    return ok;
}

void midi_end(midi_t *m)
{
    free(m->ev);
    m->ev = NULL;
    m->n = m->cap = 0;
}

// ---------------------------------------------------------------- save in the background

static struct {
    char base[48];
    int16_t *samples;
    uint32_t n, rate;
    midi_t midi;
    bool has_midi;
    int bpm;
} s_job;

static void save_task(void *arg)
{
    char path[64];
    snprintf(path, sizeof(path), "%s.wav", s_job.base);
    bool ok = write_wav(path, s_job.samples, s_job.n, s_job.rate, true);
    if (ok && s_job.has_midi) {
        snprintf(path, sizeof(path), "%s.mid", s_job.base);
        ok = midi_save(&s_job.midi, path, s_job.bpm);
    }
    free(s_job.samples);
    s_job.samples = NULL;
    if (s_job.has_midi) {
        midi_end(&s_job.midi);
    }
    s_progress = ok ? 100 : -1;
    vTaskDelete(NULL);
}

bool export_save_async(const char *base, int16_t *samples, uint32_t n, uint32_t rate, midi_t *midi, int bpm)
{
    if (s_progress >= 0 && s_progress < 100) {
        return false;
    }
    snprintf(s_job.base, sizeof(s_job.base), "%s", base);
    s_job.samples = samples;
    s_job.n = n;
    s_job.rate = rate;
    s_job.has_midi = midi != NULL;
    if (midi) {
        s_job.midi = *midi;
        midi->ev = NULL;  // the task owns the events now
        midi->n = midi->cap = 0;
    }
    s_job.bpm = bpm;
    s_progress = 0;
    // low priority on the second core: the game loop and the sound keep their time
    if (xTaskCreatePinnedToCore(save_task, "save", 6144, NULL, 1, NULL, 1) != pdPASS) {
        s_progress = -1;
        free(samples);
        if (s_job.has_midi) {
            midi_end(&s_job.midi);
        }
        return false;
    }
    return true;
}

int export_progress(void)
{
    return s_progress;
}
