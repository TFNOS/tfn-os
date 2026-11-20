// RING SEQUENCER: the sequencer in the audio task. The tick of the audio task (each block of AUDIO_BLOCK samples)
// finds the steps of each ring in the block and starts their voices with the exact delay in samples, so the screen
// cannot make the steps late. (Part of app_ringseq.c.)
//
// The position: s_bar (the bars from the start) and s_u (the place in the bar: BAR_U units for one bar). RHYTHM: step
// k of a ring of n steps starts at k * BAR_U / n. METER: each step is a 16th note (SIX_U), and the ring of n steps
// plays step (16th count) % n.

#define BAR_U  0x10000000u  // one bar
#define SIX_U  0x01000000u  // a 16th note
#define G_RING 72           // the voice group of ring r: G_RING + r

// The level of each sound (percent), so that each ring has its place in the mix: a ring alone with accents on all
// steps at level 100 gives its peak of RING_PEAK (percent of full scale). The PC test measures each sound alone and
// writes these tables (a tool of the PC test); the sum of all rings stays under the limiter.
static const uint8_t DGAIN[4][7] = {{9, 10, 12, 8, 25, 13, 50},
                                    {16, 84, 12, 40, 52, 42, 58},
                                    {100, 94, 88, 100, 61, 100, 50},
                                    {29, 46, 6, 4, 65, 100, 24}};
static const uint8_t IGAIN[3][8] = {
        {42, 25, 97, 31, 54, 50, 50, 50}, {13, 92, 53, 19, 13, 26, 50, 50}, {43, 27, 26, 65, 96, 48, 100, 72}};

static pat_t *volatile s_pl;  // the pattern that plays (the app task changes it at a slot change)
static volatile bool s_run;   // play
static bool s_was;            // (the tick: it played in the block before)
static uint32_t s_bar, s_u;
static volatile uint32_t s_hitn[NR];  // +1 for each step that sounds
static volatile uint8_t s_hitk[NR];   // the step that sounded last
static volatile uint32_t s_pub_bar, s_pub_u;
static volatile int64_t s_pub_t;  // the time (us) of the last block
// the timing record (the log of the app shows it): the blocks, the longest gap between two blocks (us), the gaps
// over 15 ms (the I2S queue of the board holds 15 ms: a longer gap can be heard), the steps
static volatile uint32_t s_tk_n, s_tk_gap, s_tk_late, s_ev_n;
// the test patterns (store key ringseq_tst): the sample of each of the first steps (the log shows them)
#define TEV_N 16
static bool s_tst;
static uint32_t s_clk;  // the samples since the play started
static uint32_t s_tev[TEV_N];
static uint8_t s_tev_r[TEV_N];
static volatile int s_tev_n;
static int64_t s_tk_last;

static uint32_t du_of(int bpm)
{
    return (uint32_t)((uint64_t)bpm * BAR_U * AUDIO_BLOCK / (240u * AUDIO_SAMPLE_RATE));
}
static int bpm_ok(const pat_t *p)
{
    return p->bpm < 40 ? 40 : p->bpm > 240 ? 240 : p->bpm;
}
static bool audible(const pat_t *p, int r)
{
    if (p->r[r].mute) {
        return false;
    }
    for (int i = 0; i < NR; i++) {
        if (p->r[i].solo) {
            return p->r[r].solo;
        }
    }
    return true;
}

static const uint8_t DVEL[4] = {0, 62, 96, 124};  // the drum velocity of each step level
static const uint8_t IVOL[4] = {0, 55, 80, 100};  // the instrument level (percent) of each step level

// Start the sound of step k of ring r: d = the delay in the block (samples), len = the length of the step (samples).
// from_tick: in the tick (audio_voice) or from the app task (a preview: audio_play).
static void sound(const pat_t *p, int r, int k, uint32_t d, uint32_t len, bool from_tick)
{
    const ring_t *g = &p->r[r];
    int v = g->vel[k] & 3;
    if (!v) {
        return;
    }
    if (!melodic(r)) {
        const dsnd_t *s = &DSND[r][g->snd % DSND_N[r]];
        int vel = DVEL[v] * g->lvl / 100 * DGAIN[r][g->snd % DSND_N[r]] / 100;
        if (vel < 1) {
            vel = 1;
        }
        if (from_tick) {
            drum_voice(s->kit, s->drum, vel, 0, d);
        } else {
            drum_play(s->kit, s->drum, vel, 0);
        }
        return;
    }
    int instr = ISND[r - R_BASS][g->snd % ISND_N[r - R_BASS]].instr;
    int vol = IVOL[v] * g->lvl / 100 * IGAIN[r - R_BASS][g->snd % ISND_N[r - R_BASS]] / 100;
    uint8_t grp = (uint8_t)(G_RING + r);
    if (r == R_CHORD) {
        int no[3];
        chord_notes(p, g->note[k], no);
        uint32_t dur = len * 92 / 100;
        for (int i = 0; i < 3; i++) {
            // a small strum; two layers at most (a chord needs many voices)
            instr_play(instr, no[i], d + i * (AUDIO_SAMPLE_RATE / 500), dur, vol, 50, 2, grp, true, from_tick, NULL);
        }
        return;
    }
    int midi = note_midi(p, r, g->note[k]);
    uint32_t dur = len * (r == R_BASS ? 80 : 70) / 100;
    if (dur > AUDIO_SAMPLE_RATE) {
        dur = AUDIO_SAMPLE_RATE;
    }
    instr_play(instr, midi, d, dur, vol, r == R_BASS ? 55 : 72, 2, grp, true, from_tick, NULL);
}

static void fire(const pat_t *p, int r, int k, uint32_t d, uint32_t len)
{
    if (k >= p->r[r].n || !p->r[r].vel[k] || !audible(p, r)) {
        return;
    }
    s_hitk[r] = (uint8_t)k;
    s_hitn[r]++;
    s_ev_n++;
    if (s_tst && s_tev_n < TEV_N) {
        s_tev[s_tev_n] = s_clk + d;
        s_tev_r[s_tev_n] = (uint8_t)r;
        s_tev_n++;
    }
    sound(p, r, k, d, len, true);
}
static uint32_t delay_of(uint64_t off, uint32_t du)
{
    uint32_t d = (uint32_t)(off * AUDIO_BLOCK / du);
    return d < AUDIO_BLOCK ? d : AUDIO_BLOCK - 1;
}
// the steps of ring r in the block [s_u, s_u + du)
static void ring_block(const pat_t *p, int r, uint32_t du, uint32_t bar_smp)
{
    const ring_t *g = &p->r[r];
    int n = g->n;
    if (n < NMIN || n > NS) {
        return;
    }
    uint64_t u0 = s_u, u1 = u0 + du;
    if (!p->poly) {
        uint32_t step = BAR_U / (uint32_t)n;
        uint32_t sw = (n % 2 == 0 && p->swing > 50) ? (uint32_t)((uint64_t)step * (p->swing - 50) / 50) : 0;
        for (int b = 0; b < 2; b++) {
            for (int k = 0; k < n; k++) {
                uint64_t s = (uint64_t)b * BAR_U + (uint64_t)k * BAR_U / (uint32_t)n + ((k & 1) ? sw : 0);
                if (s >= u0 && s < u1) {
                    fire(p, r, k, delay_of(s - u0, du), bar_smp / (uint32_t)n);
                }
            }
        }
        return;
    }
    uint32_t sw = p->swing > 50 ? (uint32_t)((uint64_t)SIX_U * (p->swing - 50) / 50) : 0;
    uint32_t j0 = (uint32_t)(u0 >> 24);
    for (uint32_t j = j0 ? j0 - 1 : 0; j <= j0 + 1; j++) {
        uint64_t s = (uint64_t)j * SIX_U + ((j & 1) ? sw : 0);
        if (s >= u0 && s < u1) {
            uint32_t g16 = s_bar * 16u + j;
            fire(p, r, (int)(g16 % (uint32_t)n), delay_of(s - u0, du), bar_smp / 16u);
        }
    }
}

static void tick_time(void)
{
    int64_t t = esp_timer_get_time();
    if (s_tk_last) {
        uint32_t gap = (uint32_t)(t - s_tk_last);
        if (gap > s_tk_gap) {
            s_tk_gap = gap;
        }
        if (gap > 15000) {
            s_tk_late++;
        }
    }
    s_tk_last = t;
    s_tk_n++;
}
static void tick(void)
{
    tick_time();
    pat_t *p = s_pl;
    if (!s_run || !p) {
        if (s_was) {
            for (int r = 0; r < NR; r++) {
                audio_release_group((uint8_t)(G_RING + r), 120);
            }
            s_was = false;
        }
        return;
    }
    if (!s_was) {
        s_bar = 0;
        s_u = 0;
        s_clk = 0;
        s_was = true;
    }
    int bpm = bpm_ok(p);
    uint32_t du = du_of(bpm), bar_smp = 240u * AUDIO_SAMPLE_RATE / (uint32_t)bpm;
    for (int r = 0; r < NR; r++) {
        ring_block(p, r, du, bar_smp);
    }
    uint32_t u1 = s_u + du;
    if (u1 >= BAR_U) {
        u1 -= BAR_U;
        s_bar++;
    }
    s_u = u1;
    s_clk += AUDIO_BLOCK;
    s_pub_bar = s_bar;
    s_pub_u = s_u;
    s_pub_t = esp_timer_get_time();
}

// the preview of one step from the app task (a tap that turns a step on, a new note, a new sound)
static void preview(int r, int k)
{
    const pat_t *p = P;
    if (audio_muted() || !p->r[r].vel[k]) {
        return;
    }
    uint32_t len = 240u * AUDIO_SAMPLE_RATE / (uint32_t)bpm_ok(p) / (uint32_t)p->r[r].n;
    sound(p, r, k, 0, len, false);
}
static void play_set(bool on)
{
    s_run = on;
}
