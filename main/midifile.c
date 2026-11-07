// Read Standard MIDI files for TILES (see midifile.h).
//
// 1. Read all tracks: notes (with their channel and GM program), tempo changes.
//    Every length and offset is checked; a bad part ends the track, not the program.
// 2. The melody channel: the given one, or the first channel with notes (not 10).
// 3. Melody notes -> tiles: drop grace notes, snap the starts to a 1/8 or 1/16 grid,
//    one tile for each grid point (the top note; exactly two notes = a 2-tile row),
//    the tile length = the time to the next tile, long rests become short.
// 4. The other channels -> the accompaniment: each note is fixed to the melody tile
//    that it comes in, with its place inside that tile (so it follows the player).

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "midifile.h"

#define MAX_RAW     20000  // notes that the reader keeps
#define MAX_TEMPO   256
#define MAX_EVENTS  400000  // events that the reader reads (a time limit)
#define MAX_TICK    0x7FFFFFF0u
#define GRACE_BEATS 0.08  // a melody note shorter than this ...
#define GRACE_GAP   0.2   // ... with the next note this near is a grace note: it goes away

typedef midi_note_t raw_t;  // on, off (ticks), ch, pitch, vel, prog

typedef struct {
    uint32_t tick, uspq;
} tempo_t;

typedef struct {
    raw_t *notes;
    int n, cap;
    tempo_t tempo[MAX_TEMPO];
    int nt;
    int32_t open[16][128];  // the note that is on now for each channel and pitch, -1 = none
    uint8_t prog[16];
    long events;
    uint32_t end_tick;
    uint8_t ts_num, ts_den;  // the first time signature (0 = none)
} reader_t;

static void *ps_alloc(size_t n)
{
    return heap_caps_malloc(n ? n : 1, MALLOC_CAP_SPIRAM);
}

static void set_err(char *err, int errlen, const char *msg)
{
    if (err && errlen > 0) {
        snprintf(err, errlen, "%s", msg);
    }
}

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

// a variable-length number (at most 4 bytes)
static bool varlen(const uint8_t *p, size_t len, size_t *i, uint32_t *out)
{
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
        if (*i >= len) {
            return false;
        }
        uint8_t b = p[(*i)++];
        v = (v << 7) | (b & 0x7F);
        if (!(b & 0x80)) {
            *out = v;
            return true;
        }
    }
    return false;
}

static void note_off(reader_t *r, int ch, int pitch, uint32_t tick)
{
    int32_t k = r->open[ch][pitch];
    if (k >= 0) {
        r->notes[k].off = tick;
        r->open[ch][pitch] = -1;
    }
}

static void note_on(reader_t *r, int ch, int pitch, int vel, uint32_t tick)
{
    note_off(r, ch, pitch, tick);  // the same note again: the old one ends
    if (r->n >= r->cap) {
        if (r->cap >= MAX_RAW) {
            return;
        }
        int cap = r->cap ? r->cap * 2 : 1024;
        if (cap > MAX_RAW) {
            cap = MAX_RAW;
        }
        raw_t *nn = ps_alloc((size_t)cap * sizeof(raw_t));
        if (!nn) {
            return;
        }
        if (r->notes) {
            memcpy(nn, r->notes, (size_t)r->n * sizeof(raw_t));
            free(r->notes);
        }
        r->notes = nn;
        r->cap = cap;
    }
    r->notes[r->n] = (raw_t){tick, tick, (uint8_t)ch, (uint8_t)pitch, (uint8_t)vel, r->prog[ch]};
    r->open[ch][pitch] = r->n;
    r->n++;
}

// One track chunk. A bad or cut event ends the track; the notes before it stay.
static void read_track(reader_t *r, const uint8_t *p, size_t len)
{
    size_t i = 0;
    uint32_t tick = 0;
    uint8_t status = 0;
    memset(r->open, 0xFF, sizeof(r->open));
    while (i < len) {
        if (++r->events > MAX_EVENTS) {
            break;
        }
        uint32_t delta;
        if (!varlen(p, len, &i, &delta)) {
            break;
        }
        if (delta > MAX_TICK - tick) {
            break;
        }
        tick += delta;
        if (i >= len) {
            break;
        }
        uint8_t b = p[i];
        if (b == 0xFF) {
            // meta event
            if (i + 2 > len) {
                break;
            }
            uint8_t type = p[i + 1];
            i += 2;
            uint32_t ml;
            if (!varlen(p, len, &i, &ml) || ml > len - i) {
                break;
            }
            if (type == 0x51 && ml == 3 && r->nt < MAX_TEMPO) {
                uint32_t us = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8) | p[i + 2];
                if (us > 0) {
                    r->tempo[r->nt++] = (tempo_t){tick, us};
                }
            }
            if (type == 0x58 && ml >= 2 && !r->ts_num && p[i] > 0 && p[i + 1] < 8) {
                r->ts_num = p[i];
                r->ts_den = (uint8_t)(1u << p[i + 1]);
            }
            i += ml;
            status = 0;
            if (type == 0x2F) {
                break;  // the end of the track
            }
            continue;
        }
        if (b == 0xF0 || b == 0xF7) {
            i++;
            uint32_t ml;
            if (!varlen(p, len, &i, &ml) || ml > len - i) {
                break;
            }
            i += ml;
            status = 0;
            continue;
        }
        if (b & 0x80) {
            status = b;
            i++;
        } else if (!status) {
            break;  // data with no status: a bad track
        }
        if (status >= 0xF0) {
            // other system messages: skip their data bytes
            int nd = (status == 0xF2) ? 2 : (status == 0xF1 || status == 0xF3) ? 1 : 0;
            if (i + nd > len) {
                break;
            }
            i += nd;
            status = 0;
            continue;
        }
        uint8_t hi = status & 0xF0, ch = status & 0x0F;
        int nd = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
        if (i + nd > len) {
            break;
        }
        uint8_t d1 = p[i], d2 = nd > 1 ? p[i + 1] : 0;
        if ((d1 & 0x80) || (d2 & 0x80)) {
            break;
        }
        i += nd;
        if (hi == 0x90 && d2 > 0) {
            if (d1 >= 12) {
                note_on(r, ch, d1, d2, tick);  // lower notes cannot be heard
            }
        } else if (hi == 0x80 || hi == 0x90) {
            note_off(r, ch, d1, tick);
        } else if (hi == 0xC0) {
            r->prog[ch] = d1;
        }
    }
    // notes that did not end: they end with the track
    for (int c = 0; c < 16; c++) {
        for (int k = 0; k < 128; k++) {
            if (r->open[c][k] >= 0) {
                note_off(r, c, k, tick);
            }
        }
    }
    if (tick > r->end_tick) {
        r->end_tick = tick;
    }
}

static int cmp_raw(const void *a, const void *b)
{
    const raw_t *x = a, *y = b;
    if (x->on != y->on) {
        return x->on < y->on ? -1 : 1;
    }
    return (int)y->pitch - (int)x->pitch;  // the top note first
}

static int cmp_acc(const void *a, const void *b)
{
    const midi_acc_t *x = a, *y = b;
    if (x->seg != y->seg) {
        return x->seg < y->seg ? -1 : 1;
    }
    if (x->at != y->at) {
        return x->at < y->at ? -1 : 1;
    }
    return 0;
}

static int cmp_tempo(const void *a, const void *b)
{
    const tempo_t *x = a, *y = b;
    return x->tick < y->tick ? -1 : x->tick > y->tick ? 1 : 0;
}

// the GM program -> a synth timbre
static uint8_t gm_timbre(int prog)
{
    if (prog < 16) {
        return TIMBRE_PIANO;  // pianos, bells
    }
    if (prog < 24) {
        return TIMBRE_ORGAN;
    }
    if (prog < 32) {
        return TIMBRE_PLUCK;  // guitars
    }
    if (prog < 40) {
        return TIMBRE_BASS;
    }
    if (prog < 56) {
        return TIMBRE_STRINGS;  // strings, ensembles, choirs
    }
    if (prog < 88) {
        return TIMBRE_LEAD;  // brass, reeds, pipes, synth leads
    }
    if (prog < 104) {
        return TIMBRE_STRINGS;  // pads
    }
    return TIMBRE_PLUCK;
}

// seconds of the song from the tempo map (the default is 120 BPM)
static float song_seconds(reader_t *r, double tpb, bool smpte, double tps)
{
    if (smpte) {
        return (float)(r->end_tick / tps);
    }
    qsort(r->tempo, r->nt, sizeof(tempo_t), cmp_tempo);
    double s = 0;
    uint32_t t = 0, us = 500000;
    for (int k = 0; k < r->nt && r->tempo[k].tick < r->end_tick; k++) {
        s += (double)(r->tempo[k].tick - t) / tpb * us / 1e6;
        t = r->tempo[k].tick;
        us = r->tempo[k].uspq;
    }
    s += (double)(r->end_tick - t) / tpb * us / 1e6;
    return (float)s;
}

// build: the channels with notes, their size and range, and a suggestion for the melody channel
static void build_channels(const reader_t *r, midi_song_t *out)
{
    float avg[16] = {0};
    for (int c = 0; c < 16; c++) {
        out->ch_lo[c] = 127;
    }
    for (int k = 0; k < r->n; k++) {
        const raw_t *a = &r->notes[k];
        out->channels |= (uint16_t)(1u << a->ch);
        if (out->ch_notes[a->ch] < 65535) {
            out->ch_notes[a->ch]++;
        }
        if (a->pitch < out->ch_lo[a->ch]) {
            out->ch_lo[a->ch] = a->pitch;
        }
        if (a->pitch > out->ch_hi[a->ch]) {
            out->ch_hi[a->ch] = a->pitch;
        }
        avg[a->ch] += a->pitch;
    }
    // a suggestion for the melody: many notes, high on average (not drums)
    out->suggest_ch = -1;
    float best = 0;
    for (int c = 0; c < 16; c++) {
        if (c == 9 || !out->ch_notes[c]) {
            continue;
        }
        float mean = avg[c] / out->ch_notes[c];
        float score = (mean - 40) * (out->ch_notes[c] < 400 ? out->ch_notes[c] : 400);
        if (mean >= 55 && score > best) {
            best = score;
            out->suggest_ch = (int8_t)c;
        }
    }
}

// build: the indexes of the melody notes in mel (the grace notes go away); returns their count
static int build_melody(const reader_t *r, double tpb, int melody_ch, int *mel, midi_song_t *out)
{
    int nm = 0;
    for (int k = 0; k < r->n; k++) {
        if (r->notes[k].ch == melody_ch) {
            mel[nm++] = k;
        }
    }
    out->melody_notes = nm;
    // grace notes go away
    int nk = 0;
    for (int j = 0; j < nm; j++) {
        const raw_t *a = &r->notes[mel[j]];
        double dur = (double)(a->off - a->on) / tpb;
        if (j + 1 < nm) {
            double gap = (double)(r->notes[mel[j + 1]].on - a->on) / tpb;
            if (dur < GRACE_BEATS && gap > 0 && gap < GRACE_GAP) {
                continue;
            }
        }
        mel[nk++] = mel[j];
    }
    return nk;
}

// build: the grid units in a beat: 2 (1/8 notes) if almost all starts are on them, else 4 (1/16)
static int build_grid(const reader_t *r, double tpb, const int *mel, int nm)
{
    int on8 = 0;
    for (int j = 0; j < nm; j++) {
        double b = (double)r->notes[mel[j]].on / tpb * 2.0;
        if (fabs(b - floor(b + 0.5)) <= 0.1) {
            on8++;
        }
    }
    return on8 * 100 >= nm * 85 ? 2 : 4;
}

// build: one tile for each grid point (q = its grid start, end = the end of its notes in beats); returns the count of
// tiles, and *cut is true when the song is longer than cap tiles
static int build_tiles(const reader_t *r, double tpb, const int *mel, int nm, int upb, int cap, int64_t *q, double *end,
                       midi_song_t *out, bool *cut)
{
    int nt = 0;
    for (int j = 0; j < nm;) {
        const raw_t *a = &r->notes[mel[j]];
        int64_t qa = (int64_t)floor((double)a->on / tpb * upb + 0.5);
        int top = a->pitch, second = -1, kinds = 1;
        double e = (double)a->off / tpb;
        int k = j + 1;
        for (; k < nm; k++) {
            const raw_t *b = &r->notes[mel[k]];
            int64_t qb = (int64_t)floor((double)b->on / tpb * upb + 0.5);
            if (qb != qa) {
                break;
            }
            if ((double)b->off / tpb > e) {
                e = (double)b->off / tpb;
            }
            if (b->pitch != top && b->pitch != second) {
                kinds++;
                if (b->pitch > top) {
                    second = top;
                    top = b->pitch;
                } else if (second < 0 || b->pitch > second) {
                    second = b->pitch;
                }
            }
        }
        j = k;
        if (nt >= cap) {
            *cut = true;
            break;
        }
        out->tiles[nt] = (midi_tile_t){(uint8_t)top, 1, (uint8_t)(kinds == 2 ? second : 0), 0};
        q[nt] = qa;
        end[nt] = e;
        nt++;
    }
    return nt;
}

// build: the lengths of the tiles (to the next tile; a long rest after a short note becomes short) and the speed
static void build_lengths(const int64_t *q, const double *end, int nt, int upb, midi_song_t *out)
{
    int units = 0;
    for (int i = 0; i < nt; i++) {
        double start = (double)q[i] / upb;
        int real = (int)floor((end[i] - start) * upb + 0.5);
        if (real < 1) {
            real = 1;
        }
        int64_t gap = i + 1 < nt ? q[i + 1] - q[i] : real;
        if (gap < 1) {
            gap = 1;
        }
        if (gap > 2 * upb && real * 2 < gap) {
            gap = real > upb ? real : upb;
        }
        if (gap > MIDI_MAX_TILE_LEN) {
            gap = MIDI_MAX_TILE_LEN;
        }
        out->tiles[i].len = (uint8_t)gap;
        out->pre[i] = (uint16_t)units;
        units += (int)gap;
    }
    out->ntiles = nt;
    out->units = (uint16_t)(units > 0 ? units : 1);
    // a playable speed: about 1.5 tiles each second at the start
    float mean = (float)units / nt;
    float sp = 1.5f * mean;
    out->speed = sp < 2.2f ? 2.2f : sp > 4.0f ? 4.0f : sp;
}

// build: the notes of the other channels in out->acc, fixed to the melody tiles; returns their count
static int build_acc(const reader_t *r, double tpb, int melody_ch, const int64_t *q, const double *end, int nt, int upb,
                     bool cut, midi_song_t *out)
{
    int na = 0;
    for (int k = 0; k < r->n; k++) {
        const raw_t *a = &r->notes[k];
        if (a->ch == melody_ch) {
            continue;
        }
        double b = (double)a->on / tpb;
        // the last tile that starts at or before this note (binary search)
        int lo = 0, hi = nt - 1, seg = 0;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            if ((double)q[mid] / upb <= b + 1e-9) {
                seg = mid;
                lo = mid + 1;
            } else {
                hi = mid - 1;
            }
        }
        double s0 = (double)q[seg] / upb;
        double span = seg + 1 < nt ? (double)(q[seg + 1] - q[seg]) / upb : fmax(end[seg] - s0, 1.0);
        if (cut && seg + 1 >= nt && b > s0 + span) {
            continue;  // after the cut
        }
        double f = span > 0 ? (b - s0) / span : 0;
        if (b < s0) {
            f = 0;
        }
        if (f > 0.9999) {
            f = 0.9999;
        }
        if (f < 0) {
            f = 0;
        }
        double dur = (double)(a->off - a->on) / tpb * 48.0;
        if (dur < 1) {
            dur = 1;
        }
        if (dur > 384) {
            dur = 384;
        }
        out->acc[na++] = (midi_acc_t){(uint16_t)seg,
                                      (uint16_t)(f * 65535.0),
                                      (uint16_t)dur,
                                      a->ch,
                                      a->pitch,
                                      a->vel,
                                      (uint8_t)(a->ch == 9 ? TIMBRE_DRUMS : gm_timbre(a->prog))};
    }
    return na;
}

// the melody and the accompaniment from the notes
static bool build(reader_t *r, double tpb, int melody_ch, midi_song_t *out, char *err, int errlen)
{
    // the channels with notes, their size and range, and the melody channel
    build_channels(r, out);
    if (melody_ch < 0 || melody_ch > 15 || melody_ch == 9 || !(out->channels & (1u << melody_ch))) {
        melody_ch = -1;
        for (int c = 0; c < 16 && melody_ch < 0; c++) {
            if (c != 9 && (out->channels & (1u << c))) {
                melody_ch = c;
            }
        }
    }
    if (melody_ch < 0) {
        set_err(err, errlen, "ONLY DRUMS: NO MELODY");
        return false;
    }
    out->melody_ch = melody_ch;

    qsort(r->notes, r->n, sizeof(raw_t), cmp_raw);
    int *mel = ps_alloc((size_t)r->n * sizeof(int));
    if (!mel) {
        set_err(err, errlen, "NO MEMORY");
        return false;
    }
    int nm = build_melody(r, tpb, melody_ch, mel, out);
    if (nm == 0) {
        free(mel);
        set_err(err, errlen, "NO MELODY NOTES");
        return false;
    }
    // the grid: 1/8 notes if almost all starts are on them, else 1/16
    int upb = build_grid(r, tpb, mel, nm);
    out->upb = (uint8_t)upb;
    out->long_len = (uint8_t)(2 * upb > MIDI_MAX_TILE_LEN ? MIDI_MAX_TILE_LEN : 2 * upb);

    // one tile for each grid point
    int cap = nm < MIDI_MAX_TILES ? nm : MIDI_MAX_TILES;
    out->tiles = ps_alloc((size_t)cap * sizeof(midi_tile_t));
    out->pre = ps_alloc((size_t)cap * sizeof(uint16_t));
    int64_t *q = ps_alloc((size_t)(cap + 1) * sizeof(int64_t));  // grid start of each tile
    double *end = ps_alloc((size_t)cap * sizeof(double));        // the end of its notes, beats
    if (!out->tiles || !out->pre || !q || !end) {
        free(mel);
        free(q);
        free(end);
        set_err(err, errlen, "NO MEMORY");
        return false;
    }
    bool cut = false;  // the song is longer than MIDI_MAX_TILES
    int nt = build_tiles(r, tpb, mel, nm, upb, cap, q, end, out, &cut);
    // the lengths: to the next tile; a long rest after a short note becomes short
    build_lengths(q, end, nt, upb, out);

    // the accompaniment: the other channels, fixed to the melody tiles
    int nacc = 0;
    for (int k = 0; k < r->n; k++) {
        if (r->notes[k].ch != melody_ch) {
            nacc++;
        }
    }
    if (nacc > 0) {
        out->acc = ps_alloc((size_t)nacc * sizeof(midi_acc_t));
        if (!out->acc) {
            free(mel);
            free(q);
            free(end);
            set_err(err, errlen, "NO MEMORY");
            return false;
        }
    }
    int na = build_acc(r, tpb, melody_ch, q, end, nt, upb, cut, out);
    if (na > 1) {
        qsort(out->acc, na, sizeof(midi_acc_t), cmp_acc);
    }
    out->nacc = na > MIDI_MAX_ACC ? MIDI_MAX_ACC : na;
    free(mel);
    free(q);
    free(end);
    return true;
}

// Read the header and all the track chunks of a file in memory. Returns the reader (PSRAM;
// the caller frees r->notes and r), or NULL with err set.
static reader_t *read_file(const uint8_t *data, size_t len, double *tpb_out, bool *smpte_out, double *tps_out,
                           char *err, int errlen)
{
    if (!data || len < 14 || memcmp(data, "MThd", 4)) {
        set_err(err, errlen, "NOT A MIDI FILE");
        return NULL;
    }
    uint32_t hlen = be32(data + 4);
    if (hlen < 6 || hlen > len - 8) {
        set_err(err, errlen, "BAD MIDI HEADER");
        return NULL;
    }
    int format = be16(data + 8), ntracks = be16(data + 10), div = be16(data + 12);
    if (format > 2) {
        set_err(err, errlen, "UNKNOWN MIDI FORMAT");
        return NULL;
    }
    bool smpte = (div & 0x8000) != 0;
    double tpb, tps = 0;
    if (smpte) {
        int fps = -(int8_t)(div >> 8), tpf = div & 0xFF;
        if (fps <= 0 || tpf == 0) {
            set_err(err, errlen, "BAD MIDI TIME");
            return NULL;
        }
        tps = (double)fps * tpf;
        tpb = tps / 2.0;  // SMPTE time: a beat of 120 BPM
    } else {
        if (div == 0) {
            set_err(err, errlen, "BAD MIDI TIME");
            return NULL;
        }
        tpb = div;
    }
    reader_t *r = ps_alloc(sizeof(reader_t));
    if (!r) {
        set_err(err, errlen, "NO MEMORY");
        return NULL;
    }
    memset(r, 0, sizeof(*r));
    size_t pos = 8 + hlen;
    int tracks = 0;
    while (tracks < ntracks && pos + 8 <= len) {
        const uint8_t *id = data + pos;
        uint32_t clen = be32(data + pos + 4);
        pos += 8;
        if (clen > len - pos) {
            clen = (uint32_t)(len - pos);  // a cut file: read what is there
        }
        if (!memcmp(id, "MTrk", 4)) {
            read_track(r, data + pos, clen);
            tracks++;
        }
        pos += clen;
        if (r->events > MAX_EVENTS) {
            break;
        }
    }
    *tpb_out = tpb;
    *smpte_out = smpte;
    *tps_out = tps;
    return r;
}

bool midifile_parse(const uint8_t *data, size_t len, int melody_ch, midi_song_t *out, char *err, int errlen)
{
    memset(out, 0, sizeof(*out));
    out->melody_ch = -1;
    double tpb, tps;
    bool smpte;
    reader_t *r = read_file(data, len, &tpb, &smpte, &tps, err, errlen);
    if (!r) {
        return false;
    }
    bool ok;
    if (r->n == 0) {
        set_err(err, errlen, "NO NOTES IN THE FILE");
        ok = false;
    } else {
        ok = build(r, tpb, melody_ch, out, err, errlen);
        out->seconds = song_seconds(r, tpb, smpte, tps);
    }
    free(r->notes);
    free(r);
    if (!ok) {
        midifile_free(out);
    }
    return ok;
}

// Read a whole file into PSRAM (at most MIDI_MAX_FILE bytes). NULL with err set on an error.
static uint8_t *read_bytes(const char *path, size_t *got, char *err, int errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        set_err(err, errlen, "CANNOT OPEN THE FILE");
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > MIDI_MAX_FILE) {
        fclose(f);
        set_err(err, errlen, size > MIDI_MAX_FILE ? "FILE TOO BIG" : "EMPTY FILE");
        return NULL;
    }
    uint8_t *buf = ps_alloc((size_t)size);
    if (!buf) {
        fclose(f);
        set_err(err, errlen, "NO MEMORY");
        return NULL;
    }
    *got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    return buf;
}

bool midifile_load(const char *path, int melody_ch, midi_song_t *out, char *err, int errlen)
{
    memset(out, 0, sizeof(*out));
    out->melody_ch = -1;
    size_t got = 0;
    uint8_t *buf = read_bytes(path, &got, err, errlen);
    if (!buf) {
        return false;
    }
    bool ok = midifile_parse(buf, got, melody_ch, out, err, errlen);
    free(buf);
    return ok;
}

bool midifile_read_notes(const char *path, midi_notes_t *out, char *err, int errlen)
{
    memset(out, 0, sizeof(*out));
    size_t got = 0;
    uint8_t *buf = read_bytes(path, &got, err, errlen);
    if (!buf) {
        return false;
    }
    bool ok = midifile_read_notes_data(buf, got, out, err, errlen);
    free(buf);
    return ok;
}

bool midifile_read_notes_data(const uint8_t *data, size_t len, midi_notes_t *out, char *err, int errlen)
{
    memset(out, 0, sizeof(*out));
    double tpb, tps;
    bool smpte;
    reader_t *r = read_file(data, len, &tpb, &smpte, &tps, err, errlen);
    if (!r) {
        return false;
    }
    if (r->n == 0) {
        set_err(err, errlen, "NO NOTES IN THE FILE");
        free(r->notes);
        free(r);
        return false;
    }
    out->seconds = song_seconds(r, tpb, smpte, tps);  // this also sorts the tempo map
    qsort(r->notes, r->n, sizeof(raw_t), cmp_raw);
    out->notes = r->notes;
    out->n = r->n;
    out->tpb = (float)tpb;
    // the tempo at the first note (a tempo event before or at it), else the first tempo
    uint32_t us = 500000;
    if (!smpte && r->nt > 0) {
        us = r->tempo[0].uspq;
        for (int k = 0; k < r->nt && r->tempo[k].tick <= r->notes[0].on; k++) {
            us = r->tempo[k].uspq;
        }
    }
    out->bpm = 60e6f / (float)us;
    out->ts_num = r->ts_num ? r->ts_num : 4;
    out->ts_den = r->ts_num ? r->ts_den : 4;
    for (int k = 0; k < r->n; k++) {
        out->channels |= (uint16_t)(1u << r->notes[k].ch);
    }
    free(r);
    return true;
}

void midifile_notes_free(midi_notes_t *m)
{
    free(m->notes);
    m->notes = NULL;
    m->n = 0;
}

void midifile_free(midi_song_t *s)
{
    free(s->tiles);
    free(s->pre);
    free(s->acc);
    s->tiles = NULL;
    s->pre = NULL;
    s->acc = NULL;
    s->ntiles = s->nacc = 0;
}

void midifile_note_name(int pitch, char *buf, int len)
{
    static const char *const N[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    if (pitch < 0 || pitch > 127) {
        pitch = 0;
    }
    snprintf(buf, len, "%s%d", N[pitch % 12], pitch / 12 - 1);
}

static void summary_text(const midi_song_t *s, char *out, int len)
{
    int more = 0;
    for (int c = 0; c < 16; c++) {
        if (c != s->melody_ch && (s->channels & (1u << c))) {
            more++;
        }
    }
    int sec = (int)(s->seconds + 0.5f);
    snprintf(out, len, "melody ch %d: %d notes, %d more channel%s, %d:%02d", s->melody_ch + 1, s->ntiles, more,
             more == 1 ? "" : "s", sec / 60, sec % 60);
}

bool midifile_summary(const char *path, char *out, int len)
{
    midi_song_t s;
    char err[40];
    if (!midifile_load(path, -1, &s, err, sizeof(err))) {
        snprintf(out, len, "%s", err);
        return false;
    }
    summary_text(&s, out, len);
    midifile_free(&s);
    return true;
}

bool midifile_summary_data(const uint8_t *data, size_t size, char *out, int len)
{
    midi_song_t s;
    char err[40];
    if (!midifile_parse(data, size, -1, &s, err, sizeof(err))) {
        snprintf(out, len, "%s", err);
        return false;
    }
    summary_text(&s, out, len);
    midifile_free(&s);
    return true;
}
