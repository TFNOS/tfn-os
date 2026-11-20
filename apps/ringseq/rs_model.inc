// RING SEQUENCER: the data of a pattern, the music theory (keys, modes, chords), Euclid, the variation, the undo and
// the save file. (Part of app_ringseq.c.)

#define NR    7   // the rings (the tracks)
#define NS    16  // the most steps of a ring
#define NMIN  3   // the fewest steps of a ring
#define NSLOT 8   // the pattern slots
#define NUNDO 24  // the undo steps

enum {
    R_KICK,
    R_SNARE,
    R_HAT,
    R_PERC,
    R_BASS,
    R_CHORD,
    R_LEAD
};
static bool melodic(int r)
{
    return r >= R_BASS;
}

// One ring: its steps, its sound and its level. vel: 0 = off, 1 = soft (a ghost note), 2 = normal, 3 = accent.
// note: the scale degree of a step (the melodic rings); it stays when the step goes off.
typedef struct {
    uint8_t n;    // the steps (NMIN..NS)
    uint8_t snd;  // the sound (an index in the sound list of the ring)
    uint8_t lvl;  // the level 0..100
    uint8_t mute, solo;
    uint8_t rsv[3];
    uint8_t vel[NS];
    int8_t note[NS];
} ring_t;

// One pattern. poly: 0 = RHYTHM (the steps of each ring share one bar: one arm), 1 = METER (each step is a 16th note:
// each ring has its own cycle).
typedef struct {
    uint8_t bpm, swing, key, mode, poly;
    uint8_t rsv[3];
    ring_t r[NR];
} pat_t;

#define SAVE_MAGIC 0x31515352u  // "RSQ1"
#define SAVE_VER   1
typedef struct {
    uint32_t magic;
    uint16_t ver, size;
    uint8_t cur;
    uint8_t used[NSLOT];
    uint8_t rsv[3];
    pat_t slot[NSLOT];
} save_t;

static save_t *S;                 // PSRAM
static pat_t *s_undo;             // PSRAM, NUNDO patterns (a ring buffer)
static int s_undo_n, s_undo_top;  // the count, the next place
static bool s_dirty;              // the save file is old
static int8_t s_lastnote[NR];     // the degree that a new step of a melodic ring gets
#define P (&S->slot[S->cur])

// ---------------------------------------------------------------- the names (English, Hebrew)

static const char *const RNAME[NR][2] = {
        {"KICK", "קיק"}, {"SNARE", "סנר"},      {"HAT", "היי-האט"}, {"PERC", "הקשה"},
        {"BASS", "בס"},  {"CHORDS", "אקורדים"}, {"LEAD", "מלודיה"},
};
// the colour of each ring (bright: the AMOLED black around it)
static const uint8_t RCOL[NR][3] = {
        {255, 72, 96}, {255, 158, 54}, {250, 226, 80}, {84, 236, 150}, {56, 196, 255}, {132, 116, 255}, {236, 104, 255},
};

// the drum sounds of each drum ring: (kit, drum, name)
typedef struct {
    uint8_t kit, drum;
    const char *name;
} dsnd_t;
static const dsnd_t DS_KICK[] = {{KIT_808, DK_KICK, "808 KICK"},   {KIT_909, DK_KICK, "909 KICK"},
                                 {KIT_ROCK, DK_KICK, "ROCK KICK"}, {KIT_LOFI, DK_KICK, "LO-FI KICK"},
                                 {KIT_JUNK, DK_KICK, "JUNK KICK"}, {KIT_808, DK_TOM_LO, "808 TOM"}};
static const dsnd_t DS_SNARE[] = {{KIT_909, DK_SNARE, "909 SNARE"},   {KIT_808, DK_CLAP, "808 CLAP"},
                                  {KIT_ROCK, DK_SNARE, "ROCK SNARE"}, {KIT_LOFI, DK_SNARE, "LO-FI SNARE"},
                                  {KIT_909, DK_RIM, "909 RIM"},       {KIT_JUNK, DK_SNARE, "JUNK SNARE"},
                                  {KIT_909, DK_CLAP, "909 CLAP"}};
static const dsnd_t DS_HAT[] = {{KIT_909, DK_CHAT, "909 HAT"},  {KIT_808, DK_CHAT, "808 HAT"},
                                {KIT_909, DK_OHAT, "OPEN HAT"}, {KIT_808, DK_SHAKER, "SHAKER"},
                                {KIT_ROCK, DK_RIDE, "RIDE"},    {KIT_LOFI, DK_CHAT, "LO-FI HAT"}};
static const dsnd_t DS_PERC[] = {{KIT_808, DK_RIM, "RIM"},          {KIT_808, DK_COWBELL, "COWBELL"},
                                 {KIT_909, DK_TOM_HI, "HIGH TOM"},  {KIT_ROCK, DK_TOM_LO, "LOW TOM"},
                                 {KIT_LOFI, DK_CLAP, "LO-FI CLAP"}, {KIT_909, DK_SHAKER, "SHAKER"},
                                 {KIT_909, DK_CRASH, "CRASH"}};
static const dsnd_t *const DSND[4] = {DS_KICK, DS_SNARE, DS_HAT, DS_PERC};
static const uint8_t DSND_N[4] = {6, 7, 6, 7};

// the instruments of each melodic ring (libs/instr: 0 SAW LEAD, 1 SQUARE, 2 SOFT, 3 CHIP, 4 PLUCK, 5 BELL, 6 KALIMBA,
// 7 ORGAN, 8 E-PIANO, 9 STRINGS, 10 WARM PAD, 11 BRASS, 12 SUB BASS, 13 ACID, 14 FUNK, 15 808 BASS)
typedef struct {
    uint8_t instr;
    const char *name;
} isnd_t;
static const isnd_t IS_BASS[] = {{12, "SUB BASS"}, {15, "808 BASS"}, {13, "ACID"}, {14, "FUNK BASS"}, {4, "PLUCK"}};
static const isnd_t IS_CHORD[] = {{8, "E-PIANO"}, {10, "WARM PAD"}, {9, "STRINGS"},
                                  {7, "ORGAN"},   {6, "KALIMBA"},   {11, "BRASS"}};
static const isnd_t IS_LEAD[] = {{4, "PLUCK"},  {6, "KALIMBA"}, {5, "BELL"}, {0, "SAW LEAD"},
                                 {1, "SQUARE"}, {2, "SOFT"},    {3, "CHIP"}, {11, "BRASS"}};
static const isnd_t *const ISND[3] = {IS_BASS, IS_CHORD, IS_LEAD};
static const uint8_t ISND_N[3] = {5, 6, 8};

static int snd_count(int r)
{
    return melodic(r) ? ISND_N[r - R_BASS] : DSND_N[r];
}
static const char *snd_name(int r, int i)
{
    i %= snd_count(r);
    return melodic(r) ? ISND[r - R_BASS][i].name : DSND[r][i].name;
}

// ---------------------------------------------------------------- the keys and the modes

#define NMODE 8
static const int8_t MODES[NMODE][7] = {
        {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
        {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 3, 5, 7, 10, 0, 0},
};
static const char *const MODE_NAME[NMODE][2] = {
        {"MAJOR", "מז'ור"}, {"MINOR", "מינור"},          {"DORIAN", "דורי"},     {"PHRYGIAN", "פריגי"},
        {"LYDIAN", "לידי"}, {"MIXOLYDIAN", "מיקסולידי"}, {"HARMONIC", "הרמוני"}, {"PENTATONIC", "פנטטוני"},
};
static const char *const KEY_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

static int mode_size(int m)
{
    return m == 7 ? 5 : 7;
}
static int floordiv(int a, int b)
{
    return a >= 0 ? a / b : -((-a + b - 1) / b);
}
// the semitones of a scale degree over the key note (any degree: more octaves up or down)
static int deg_semi(int mode, int d)
{
    int sz = mode_size(mode), o = floordiv(d, sz);
    return 12 * o + MODES[mode][d - o * sz];
}
// the root of each melodic ring: the key in octave 2 (bass) and 4 (lead)
static int ring_root(const pat_t *p, int r)
{
    int k = p->key % 12;
    if (r == R_BASS) {
        return 36 + k;
    }
    return 60 + (k > 7 ? k - 12 : k);
}
// the range of the degrees of a melodic ring
static int note_lo(int r)
{
    return r == R_CHORD ? 0 : r == R_BASS ? -3 : -4;
}
static int note_hi(int r)
{
    return r == R_CHORD ? 6 : r == R_BASS ? 10 : 11;
}
static int note_midi(const pat_t *p, int r, int d)
{
    return ring_root(p, r) + deg_semi(p->mode % NMODE, d);
}
// the three notes of the chord of degree d (the triad of the mode; the pentatonic uses the minor), close to F3..E4
static int chord_notes(const pat_t *p, int d, int *out)
{
    int m = p->mode % NMODE == 7 ? 1 : p->mode % NMODE, k = p->key % 12;
    for (int i = 0; i < 3; i++) {
        int v = k + deg_semi(m, d + 2 * i);
        int lo = 53;
        v = lo + ((v - lo) % 12 + 12) % 12;
        out[i] = v;
    }
    return 3;
}
// the name of a note ("A3") or of a chord ("iv", "VI", "ii°")
static void note_label(const pat_t *p, int r, int d, char *b, int len)
{
    if (r == R_CHORD) {
        static const char *const RN[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
        static const char *const rn[7] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
        int m = p->mode % NMODE == 7 ? 1 : p->mode % NMODE, dd = ((d % 7) + 7) % 7;
        int third = deg_semi(m, dd + 2) - deg_semi(m, dd), fifth = deg_semi(m, dd + 4) - deg_semi(m, dd);
        snprintf(b, len, "%s%s", third == 4 ? RN[dd] : rn[dd], fifth == 6 ? "°" : "");
        return;
    }
    int midi = note_midi(p, r, d);
    snprintf(b, len, "%s%d", KEY_NAME[midi % 12], midi / 12 - 1);
}

// ---------------------------------------------------------------- the steps

// Euclid: k hits spread as evenly as they can go over n steps, the first hit on step rot
static bool euclid_hit(int j, int k, int n)
{
    return k > 0 && (j * k) % n < k;
}
static int ring_hits(const ring_t *g)
{
    int k = 0;
    for (int i = 0; i < g->n; i++) {
        k += g->vel[i] > 0;
    }
    return k;
}
static int ring_first(const ring_t *g)
{
    for (int i = 0; i < g->n; i++) {
        if (g->vel[i]) {
            return i;
        }
    }
    return 0;
}
static void ring_euclid(ring_t *g, int k)
{
    int n = g->n, rot = ring_first(g);
    if (k < 0) {
        k = 0;
    }
    if (k > n) {
        k = n;
    }
    uint8_t old[NS];
    memcpy(old, g->vel, sizeof old);
    for (int i = 0; i < n; i++) {
        bool hit = euclid_hit(((i - rot) % n + n) % n, k, n);
        g->vel[i] = hit ? (old[i] ? old[i] : 2) : 0;
    }
    for (int i = n; i < NS; i++) {
        g->vel[i] = 0;
    }
}
// turn the steps of a ring by d (d > 0: later)
static void ring_rotate(ring_t *g, int d)
{
    int n = g->n;
    d = ((d % n) + n) % n;
    if (!d) {
        return;
    }
    uint8_t v[NS];
    int8_t no[NS];
    for (int i = 0; i < n; i++) {
        v[(i + d) % n] = g->vel[i];
        no[(i + d) % n] = g->note[i];
    }
    memcpy(g->vel, v, n);
    memcpy(g->note, no, n);
}
// a new step count: the hits keep their places where they can (a ring made by Euclid stays even)
static void ring_resize(ring_t *g, int n)
{
    if (n < NMIN) {
        n = NMIN;
    }
    if (n > NS) {
        n = NS;
    }
    if (n == g->n) {
        return;
    }
    int k = ring_hits(g), on = g->n;
    bool even = true;  // is the pattern now an Euclid pattern?
    ring_t t = *g;
    ring_euclid(&t, k);
    for (int i = 0; i < on; i++) {
        even = even && ((t.vel[i] > 0) == (g->vel[i] > 0));
    }
    for (int i = on; i < n; i++) {
        g->vel[i] = 0;
        g->note[i] = g->note[i % on];
    }
    g->n = (uint8_t)n;
    if (even) {
        int kk = (k * n + on / 2) / on;
        ring_euclid(g, k > 0 && kk < 1 ? 1 : kk);
    }
    for (int i = n; i < NS; i++) {
        g->vel[i] = 0;
    }
}

// ---------------------------------------------------------------- the patterns

static void ring_set(ring_t *g, int n, int snd, int lvl, const uint8_t *vel, const int8_t *note)
{
    memset(g, 0, sizeof *g);
    g->n = (uint8_t)n;
    g->snd = (uint8_t)snd;
    g->lvl = (uint8_t)lvl;
    for (int i = 0; i < n; i++) {
        g->vel[i] = vel ? vel[i] : 0;
        g->note[i] = note ? note[i] : 0;
    }
}
static void pat_empty(pat_t *p)
{
    memset(p, 0, sizeof *p);
    p->bpm = 112;
    p->swing = 54;
    p->key = 9;
    p->mode = 1;
    static const uint8_t N[NR] = {16, 16, 16, 5, 8, 4, 6};
    static const uint8_t L[NR] = {90, 78, 60, 66, 80, 56, 70};
    for (int r = 0; r < NR; r++) {
        ring_set(&p->r[r], N[r], 0, L[r], NULL, NULL);
    }
}
// the demo of the first start: A minor, 112 BPM, a 5 against 4 in PERC and 6 against 4 in LEAD
static void pat_demo(pat_t *p)
{
    pat_empty(p);
    static const uint8_t KV[16] = {3, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 1, 2, 0, 0, 0};
    static const uint8_t SV[16] = {0, 0, 0, 0, 3, 0, 0, 1, 0, 0, 0, 0, 3, 0, 1, 0};
    static const uint8_t HV[16] = {1, 0, 2, 0, 1, 0, 2, 1, 1, 0, 2, 0, 1, 0, 2, 0};
    static const uint8_t PV[5] = {2, 0, 0, 2, 0};
    static const uint8_t BV[8] = {3, 0, 2, 1, 2, 0, 2, 2};
    static const int8_t BN[8] = {0, 0, 0, 7, 0, 0, -2, -1};
    static const uint8_t CV[4] = {2, 0, 2, 0};
    static const int8_t CN[4] = {0, 0, 5, 0};
    static const uint8_t LV[6] = {2, 1, 2, 2, 0, 1};
    static const int8_t LN[6] = {4, 2, 0, 4, 5, 3};
    ring_set(&p->r[R_KICK], 16, 0, 90, KV, NULL);
    ring_set(&p->r[R_SNARE], 16, 0, 78, SV, NULL);
    ring_set(&p->r[R_HAT], 16, 0, 60, HV, NULL);
    ring_set(&p->r[R_PERC], 5, 0, 66, PV, NULL);
    ring_set(&p->r[R_BASS], 8, 0, 80, BV, BN);
    ring_set(&p->r[R_CHORD], 4, 1, 56, CV, CN);
    ring_set(&p->r[R_LEAD], 6, 0, 70, LV, LN);
}
// the test patterns of the PC test and the board check (store key ringseq_tst): 1 = KICK 4 steps, 2 = PERC 5 steps,
// 3 = all rings full with accents (the peak level), 4 = METER: KICK 4 steps against HAT 5 steps
static void pat_test(pat_t *p, int t)
{
    if (t == 5) {
        pat_demo(p);
        return;
    }
    if (t >= 200) {  // 200 + 16 r + i: ring r alone with its sound i (the mastering of the levels)
        pat_test(p, 100 + (t - 200) / 16);
        p->r[(t - 200) / 16].snd = (uint8_t)((t - 200) % 16);
        return;
    }
    if (t >= 100) {  // 100 + r: the loud test with only ring r
        pat_test(p, 3);
        for (int r = 0; r < NR; r++) {
            p->r[r].mute = r != t - 100;
        }
        return;
    }
    pat_empty(p);
    p->bpm = 120;
    p->swing = 50;
    for (int r = 0; r < NR; r++) {
        memset(p->r[r].vel, 0, NS);
    }
    if (t == 1 || t == 4) {
        ring_set(&p->r[R_KICK], 4, 0, 100, NULL, NULL);
        ring_euclid(&p->r[R_KICK], 4);
    }
    if (t == 2) {
        ring_set(&p->r[R_PERC], 5, 0, 100, NULL, NULL);
        ring_euclid(&p->r[R_PERC], 5);
    }
    if (t == 4) {
        p->poly = 1;
        ring_set(&p->r[R_HAT], 5, 0, 100, NULL, NULL);
        p->r[R_HAT].vel[0] = 3;
    }
    if (t == 3) {
        p->bpm = 132;
        static const int8_t LN[16] = {0, 4, 7, 9, 11, 7, 4, 2, 0, 5, 9, 11, 9, 7, 4, 2};
        for (int r = 0; r < NR; r++) {
            ring_t *g = &p->r[r];
            g->n = r == R_CHORD ? 8 : 16;
            g->lvl = 100;
            g->snd = r == R_CHORD ? 1 : 0;
            for (int i = 0; i < g->n; i++) {
                g->vel[i] = 3;
                g->note[i] = (int8_t)(r == R_CHORD ? (i * 3) % 7 : r == R_BASS ? (i % 4 == 3 ? 7 : 0) : LN[i]);
            }
        }
    }
}

// ---------------------------------------------------------------- the random numbers (a small xorshift)

static uint32_t s_rng = 0x9E3779B9u;
static uint32_t rnd(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static int rndi(int n)
{
    return n > 0 ? (int)(rnd() % (uint32_t)n) : 0;
}

// ---------------------------------------------------------------- the undo

static void undo_push(void)
{
    if (!s_undo) {
        return;
    }
    s_undo[s_undo_top] = *P;
    s_undo_top = (s_undo_top + 1) % NUNDO;
    if (s_undo_n < NUNDO) {
        s_undo_n++;
    }
}
static bool undo_pop(void)
{
    if (!s_undo || s_undo_n == 0) {
        return false;
    }
    s_undo_top = (s_undo_top + NUNDO - 1) % NUNDO;
    s_undo_n--;
    *P = s_undo[s_undo_top];
    s_dirty = true;
    return true;
}

// ---------------------------------------------------------------- the variation

// One random musical change of the pattern: a drum ring turns or gets a new count, a ghost note, a new note in the
// bass or the lead, a chord changes to a near chord, a new polyrhythm in PERC. *what gets the ring that changed.
static bool vary_drums(pat_t *p, int *what)
{
    int c = rndi(5);
    if (c == 0) {  // HAT or PERC turns by one or two steps
        int r = rndi(2) ? R_HAT : R_PERC;
        ring_rotate(&p->r[r], rndi(2) ? 1 : -1 - rndi(2));
        *what = r;
        return ring_hits(&p->r[r]) > 0 && ring_hits(&p->r[r]) < p->r[r].n;
    }
    if (c == 1) {  // a new polyrhythm in PERC
        static const uint8_t NN[] = {3, 5, 6, 7, 9, 10, 12};
        ring_t *g = &p->r[R_PERC];
        int n = NN[rndi(7)];
        if (n == g->n) {
            return false;
        }
        int k = ring_hits(g) ? ring_hits(g) : 2;
        g->n = (uint8_t)n;
        ring_euclid(g, k * n / 8 < 2 ? 2 : (k * n / 8 > n - 1 ? n - 1 : k * n / 8));
        *what = R_PERC;
        return true;
    }
    if (c == 2) {  // HAT: one hit more or one less
        ring_t *g = &p->r[R_HAT];
        int k = ring_hits(g) + (rndi(2) ? 1 : -1);
        if (k < 2 || k > g->n) {
            return false;
        }
        ring_euclid(g, k);
        *what = R_HAT;
        return true;
    }
    ring_t *g = &p->r[c == 3 ? R_SNARE : R_KICK];
    *what = c == 3 ? R_SNARE : R_KICK;
    int i = rndi(g->n);
    if (i == 0) {
        return false;
    }
    if (g->vel[i] == 0) {
        g->vel[i] = c == 3 ? 1 : 2;  // a ghost of the snare, a push of the kick
    } else if (g->vel[i] < 3) {
        g->vel[i] = 0;
    } else {
        return false;
    }
    return true;
}
static bool vary_tones(pat_t *p, int *what)
{
    int c = rndi(5);
    if (c == 0) {  // CHORDS: one chord to a near chord (I and vi, IV and ii, V and iii)
        ring_t *g = &p->r[R_CHORD];
        int i = rndi(g->n);
        if (!g->vel[i]) {
            return false;
        }
        static const int8_t ALT[7] = {5, 3, 4, 1, 2, 0, 4};
        g->note[i] = ALT[((g->note[i] % 7) + 7) % 7];
        *what = R_CHORD;
        return true;
    }
    int r = c == 1 ? R_BASS : R_LEAD;
    ring_t *g = &p->r[r];
    *what = r;
    int i = rndi(g->n);
    if (c == 4) {  // LEAD: the melody upside down round its first note
        int pivot = g->note[ring_first(g)];
        for (int k = 0; k < g->n; k++) {
            int v = 2 * pivot - g->note[k];
            g->note[k] = (int8_t)(v < note_lo(r) ? note_lo(r) : v > note_hi(r) ? note_hi(r) : v);
        }
        return true;
    }
    if (c == 3 && g->n > 1) {  // LEAD: two notes change places
        int j = (i + 1) % g->n;
        int8_t t = g->note[i];
        g->note[i] = g->note[j];
        g->note[j] = t;
        uint8_t v = g->vel[i];
        g->vel[i] = g->vel[j];
        g->vel[j] = v;
        return g->note[i] != g->note[j] || g->vel[i] != g->vel[j];
    }
    if (!g->vel[i]) {
        return false;
    }
    static const int8_t STEP[] = {-2, -1, 1, 2, 4, -3};
    int v = g->note[i] + STEP[rndi(r == R_BASS ? 6 : 4)];
    if (v < note_lo(r) || v > note_hi(r)) {
        return false;
    }
    g->note[i] = (int8_t)v;
    return true;
}
static int vary(pat_t *p)
{
    s_rng ^= (uint32_t)esp_timer_get_time();
    for (int t = 0; t < 24; t++) {
        pat_t keep = *p;
        int what = 0;
        bool ok = rndi(2) ? vary_drums(p, &what) : vary_tones(p, &what);
        if (ok && memcmp(&keep, p, sizeof keep)) {
            return what;
        }
        *p = keep;
    }
    return -1;
}

// ---------------------------------------------------------------- the save file (/data/ringseq.bin)

#define SAVE_PATH "/data/ringseq.bin"

static void pat_fix(pat_t *p)
{
    if (p->bpm < 40 || p->bpm > 240) {
        p->bpm = 112;
    }
    if (p->swing < 50 || p->swing > 75) {
        p->swing = 50;
    }
    p->key %= 12;
    p->mode %= NMODE;
    p->poly = p->poly ? 1 : 0;
    for (int r = 0; r < NR; r++) {
        ring_t *g = &p->r[r];
        if (g->n < NMIN || g->n > NS) {
            g->n = 16;
        }
        g->snd %= snd_count(r);
        if (g->lvl > 100) {
            g->lvl = 80;
        }
        for (int i = 0; i < NS; i++) {
            if (g->vel[i] > 3) {
                g->vel[i] = 2;
            }
            if (melodic(r) && (g->note[i] < note_lo(r) || g->note[i] > note_hi(r))) {
                g->note[i] = 0;
            }
        }
    }
}
static void save_load(void)
{
    size_t len = 0;
    if (storage_read(SAVE_PATH, S, sizeof *S, &len) && len == sizeof *S && S->magic == SAVE_MAGIC &&
        S->ver == SAVE_VER && S->size == sizeof *S) {
        S->cur %= NSLOT;
        for (int i = 0; i < NSLOT; i++) {
            pat_fix(&S->slot[i]);
        }
        S->used[S->cur] = 1;
        return;
    }
    memset(S, 0, sizeof *S);
    S->magic = SAVE_MAGIC;
    S->ver = SAVE_VER;
    S->size = sizeof *S;
    for (int i = 0; i < NSLOT; i++) {
        pat_empty(&S->slot[i]);
    }
    pat_demo(&S->slot[0]);
    S->used[0] = 1;
}
static void save_now(void)
{
    if (!s_dirty) {
        return;
    }
    s_dirty = false;
    if (!storage_write(SAVE_PATH, S, sizeof *S)) {
        ESP_LOGW("ringseq", "the save failed");
    }
}
