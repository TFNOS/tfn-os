// RING SEQUENCER: the export of a pattern as a standard MIDI file (format 1, 480 ticks for a quarter note, one track
// for each ring that sounds, the drums on channel 10). The file goes to /data/midi (the board web page lists it with a
// DOWNLOAD key; TILES and STUDIO can open it) and to /sdcard/ringseq when a card is in. (Part of app_ringseq.c.)

#if defined(_WIN32) && !defined(ESP_PLATFORM)
#define RS_MKDIR(p) mkdir(p)  // (the host tools on Windows)
#else
#define RS_MKDIR(p) mkdir(p, 0777)
#endif
#define PPQ     480
#define BAR_T   (4 * PPQ)
#define MEV_MAX 3000

typedef struct {
    uint32_t t;
    uint8_t st, d1, d2, ord;  // ord: 0 = a note off first at the same tick
} mev_t;

static mev_t *s_mev;
static int s_mev_n;

static void mev(uint32_t t, uint8_t st, uint8_t d1, uint8_t d2, uint8_t ord)
{
    if (s_mev_n < MEV_MAX) {
        s_mev[s_mev_n++] = (mev_t){t, st, d1, d2, ord};
    }
}
static int mev_cmp(const void *a, const void *b)
{
    const mev_t *x = a, *y = b;
    if (x->t != y->t) {
        return x->t < y->t ? -1 : 1;
    }
    return (int)x->ord - (int)y->ord;
}
static int vlq(uint8_t *o, uint32_t v)
{
    uint8_t b[5];
    int n = 0;
    do {
        b[n++] = (uint8_t)(v & 0x7F);
        v >>= 7;
    } while (v && n < 5);
    for (int i = 0; i < n; i++) {
        o[i] = (uint8_t)(b[n - 1 - i] | (i < n - 1 ? 0x80 : 0));
    }
    return n;
}
static int put_meta(uint8_t *o, uint8_t type, const char *s)
{
    int n = 0, len = (int)strlen(s);
    o[n++] = 0;
    o[n++] = 0xFF;
    o[n++] = type;
    o[n++] = (uint8_t)len;
    memcpy(o + n, s, len);
    return n + len;
}
static void put_be(uint8_t *o, uint32_t v, int bytes)
{
    for (int i = 0; i < bytes; i++) {
        o[i] = (uint8_t)(v >> (8 * (bytes - 1 - i)));
    }
}
// write one track chunk of the events of s_mev (sorted) with a name; returns the bytes
static int put_track(uint8_t *o, const char *name, bool tempo, int bpm)
{
    int n = 8;
    if (name) {
        n += put_meta(o + n, 0x03, name);
    }
    if (tempo) {
        uint32_t us = 60000000u / (uint32_t)bpm;
        static const uint8_t TS[] = {0, 0xFF, 0x58, 4, 4, 2, 24, 8};
        memcpy(o + n, TS, sizeof TS);
        n += sizeof TS;
        uint8_t te[7] = {0, 0xFF, 0x51, 3, (uint8_t)(us >> 16), (uint8_t)(us >> 8), (uint8_t)us};
        memcpy(o + n, te, sizeof te);
        n += sizeof te;
    }
    qsort(s_mev, s_mev_n, sizeof(mev_t), mev_cmp);
    uint32_t last = 0;
    for (int i = 0; i < s_mev_n; i++) {
        n += vlq(o + n, s_mev[i].t - last);
        last = s_mev[i].t;
        o[n++] = s_mev[i].st;
        o[n++] = s_mev[i].d1;
        if ((s_mev[i].st & 0xF0) != 0xC0) {
            o[n++] = s_mev[i].d2;
        }
    }
    static const uint8_t END[4] = {0, 0xFF, 0x2F, 0};
    memcpy(o + n, END, 4);
    n += 4;
    memcpy(o, "MTrk", 4);
    put_be(o + 4, (uint32_t)(n - 8), 4);
    return n;
}
static int gcd(int a, int b)
{
    while (b) {
        int t = a % b;
        a = b;
        b = t;
    }
    return a;
}
// the bars of the export: RHYTHM 4 bars; METER the full cycle of all rings (32 bars at most)
static int export_bars(const pat_t *p)
{
    if (!p->poly) {
        return 4;
    }
    int l = 16;
    for (int r = 0; r < NR; r++) {
        if (audible(p, r) && ring_hits(&p->r[r])) {
            l = l / gcd(l, p->r[r].n) * p->r[r].n;
            if (l > 32 * 16) {
                return 32;
            }
        }
    }
    return (l + 15) / 16;
}
static const uint8_t MVEL[4] = {0, 52, 92, 122};
// the notes of ring r into s_mev
static void ring_events(const pat_t *p, int r, int bars)
{
    const ring_t *g = &p->r[r];
    int n = g->n, ch = melodic(r) ? r - R_BASS : 9;
    uint32_t total = (uint32_t)bars * BAR_T;
    uint32_t steps = p->poly ? (uint32_t)bars * 16u : (uint32_t)bars * (uint32_t)n;
    for (uint32_t j = 0; j < steps; j++) {
        int k = (int)(j % (uint32_t)n);
        int v = g->vel[k] & 3;
        uint32_t t, len;
        if (p->poly) {
            len = PPQ / 4;
            t = j * len + ((j & 1) && p->swing > 50 ? len * (p->swing - 50) / 50 : 0);
        } else {
            uint32_t b = j / (uint32_t)n;
            len = BAR_T / (uint32_t)n;
            t = b * BAR_T + (uint32_t)k * BAR_T / (uint32_t)n;
            if ((k & 1) && n % 2 == 0 && p->swing > 50) {
                t += len * (p->swing - 50) / 50;
            }
        }
        if (!v || t >= total) {
            continue;
        }
        uint32_t dur = melodic(r) ? len * (r == R_CHORD ? 92 : r == R_BASS ? 80 : 70) / 100 : PPQ / 8;
        if (t + dur > total) {
            dur = total - t;
        }
        int notes[3], nn = 1;
        if (r == R_CHORD) {
            nn = chord_notes(p, g->note[k], notes);
        } else if (melodic(r)) {
            notes[0] = note_midi(p, r, g->note[k]);
        } else {
            notes[0] = drum_gm_note(DSND[r][g->snd % DSND_N[r]].drum);
        }
        for (int i = 0; i < nn; i++) {
            int m = notes[i] < 0 ? 0 : notes[i] > 127 ? 127 : notes[i];
            mev(t, (uint8_t)(0x90 | ch), (uint8_t)m, MVEL[v], 1);
            mev(t + dur, (uint8_t)(0x80 | ch), (uint8_t)m, 0, 0);
        }
    }
}
// the file into buf (PSRAM); returns its size (0: a fault)
static int midi_make(const pat_t *p, uint8_t *buf, int cap)
{
    int bars = export_bars(p), ntr = 1;
    int n = 14;
    s_mev_n = 0;
    n += put_track(buf + n, "RING SEQUENCER", true, bpm_ok(p));
    for (int r = 0; r < NR; r++) {
        if (!audible(p, r) || !ring_hits(&p->r[r]) || n > cap - 8 * MEV_MAX - 64) {
            continue;
        }
        s_mev_n = 0;
        int ch = melodic(r) ? r - R_BASS : 9;
        if (melodic(r)) {
            mev(0, (uint8_t)(0xC0 | ch), (uint8_t)instr_gm(ISND[r - R_BASS][p->r[r].snd % ISND_N[r - R_BASS]].instr), 0,
                0);
        }
        mev(0, (uint8_t)(0xB0 | ch), 7, (uint8_t)(p->r[r].lvl * 127 / 100), 0);
        ring_events(p, r, bars);
        n += put_track(buf + n, RNAME[r][0], false, 0);
        ntr++;
    }
    memcpy(buf, "MThd", 4);
    put_be(buf + 4, 6, 4);
    put_be(buf + 8, 1, 2);
    put_be(buf + 10, (uint32_t)ntr, 2);
    put_be(buf + 12, PPQ, 2);
    return n;
}

// Write the file. out1 gets the name, out2 the places ("BOARD WEB", "BOARD WEB + SD CARD"); false = a fault.
static bool midi_export(char *name, int nlen, bool *card)
{
    const int cap = 16 * MEV_MAX + 4096;
    uint8_t *buf = heap_caps_malloc(cap, MALLOC_CAP_SPIRAM);
    s_mev = heap_caps_malloc(sizeof(mev_t) * MEV_MAX, MALLOC_CAP_SPIRAM);
    bool ok = false;
    *card = false;
    if (buf && s_mev) {
        int len = midi_make(P, buf, cap);
        // the first free number (no key in the NVS: a write of the NVS also stops the sound)
        char path[96];
        RS_MKDIR(STORAGE_MIDI);
        for (int no = 1; no < 1000; no++) {
            struct stat st;
            snprintf(name, nlen, "ringseq_%03d.mid", no);
            snprintf(path, sizeof path, "%s/%s", STORAGE_MIDI, name);
            if (stat(path, &st) != 0) {
                break;
            }
        }
        snprintf(path, sizeof path, "%s/%s", STORAGE_MIDI, name);
        ok = storage_write(path, buf, (size_t)len);
        if (board_sd_mounted()) {
            RS_MKDIR(BOARD_SD_ROOT "/ringseq");
            snprintf(path, sizeof path, "%s/ringseq/%s", BOARD_SD_ROOT, name);
            FILE *f = fopen(path, "wb");
            if (f) {
                *card = fwrite(buf, 1, (size_t)len, f) == (size_t)len;
                fclose(f);
            }
        }
        ESP_LOGI("ringseq", "export %s: %d bytes, %d bars, board %s, card %s", name, len, export_bars(P),
                 ok ? "ok" : "fault", *card ? "ok" : "no");
        ok = ok || *card;
    }
    free(buf);
    free(s_mev);
    s_mev = NULL;
    return ok;
}
