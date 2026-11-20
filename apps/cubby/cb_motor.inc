// CUBBY: the sound of the motor and the treads (included by app_cubby.c; round 3: a real motor sound when the robot
// moves). The app makes two short samples at the start (no sound files):
// - THE MOTOR: 0.1 s of a small electric gear motor: the hum of the rotor (120 Hz and its harmonics), the whine of the
//   gears (11 x), the rough noise of the brushes (it pulses with the rotor). A sine window on the sample.
// - THE CLEAT: 30 ms of a click of a tread cleat on the ground (a short noise and a ring that falls).
// Each frame the game puts the samples of the next 70 ms into the queue of the mixer (audio_play with a delay: no tick
// of the audio task, so the music keeps its tick): a motor sample each half sample, at the rate of the speed (the
// pitch goes up with the speed), and the clicks of the cleats at a rate that the speed of the treads gives. The
// samples of two frames can be a few ms off; the sine window keeps the power of two samples that overlap the same (a
// steady sound, a little rough like a real motor).

#define MOT_LEN   2400  // the motor sample (0.1 s at 24000 Hz = 12 turns of the rotor at 120 Hz)
#define CLEAT_LEN 720   // the cleat click (30 ms)
#define MOT_GROUP 47    // (the mixer group of the motor; the effects use 46)
#define MOT_AHEAD (AUDIO_SAMPLE_RATE * 70 / 1000)
static int16_t s_mot_pcm[MOT_LEN], s_cleat_pcm[CLEAT_LEN];  // (in the app: the unload of the app stops their sounds)
static bool s_mot_ok;
// set by the game: the rate of the motor (1 = 120 Hz), the volume (0..100, 0 = off), the clicks a second
static float s_mot_rate = 1, s_mot_vol, s_mot_cps;
// the sample clock (samples since the start of the timer) of the next motor sample and the next click; the volume now
static int64_t s_mot_next, s_cleat_next;
static float s_mot_v;
static uint32_t s_mot_rnd = 0x9E3779B9u;

static inline float mot_noise(void)
{
    s_mot_rnd ^= s_mot_rnd << 13;
    s_mot_rnd ^= s_mot_rnd >> 17;
    s_mot_rnd ^= s_mot_rnd << 5;
    return (float)(int32_t)s_mot_rnd * (1.0f / 2147483648.0f);
}

// make the two samples
static void motor_make(void)
{
    // the harmonics of the rotor (their parts), the gear whine at 11 x
    static const float H[12] = {0.55f, 0.85f, 0.40f, 0.50f, 0.12f, 0.26f, 0.06f, 0.16f, 0, 0, 0.13f, 0.05f};
    const float w0 = 2 * PI_F * 120.0f / AUDIO_SAMPLE_RATE;
    float lp = 0, lp2 = 0;
    s_mot_rnd = 0x9E3779B9u;
    for (int i = 0; i < MOT_LEN; i++) {
        float ph = w0 * i, v = 0;
        for (int h = 0; h < 12; h++) {
            if (H[h] > 0) {
                v += H[h] * fsin(ph * (h + 1) + h * 0.7f);
            }
        }
        // the brushes: noise, softer at the top (two low-pass steps), loud at each half turn of the rotor
        float n = mot_noise();
        lp += (n - lp) * 0.35f;
        lp2 += (lp - lp2) * 0.5f;
        float pulse = 0.35f + 0.65f * absf(fsin(ph));
        v += lp2 * 1.3f * pulse;
        float win = fsin(PI_F * (i + 0.5f) / MOT_LEN);
        s_mot_pcm[i] = (int16_t)clampf(v * win * 6200.0f, -30000, 30000);
    }
    // the cleat: a hard noise attack, a ring at 900 Hz and one at 2300 Hz, a fast fall
    float a = 0;
    for (int i = 0; i < CLEAT_LEN; i++) {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        float env = i < 24 ? i / 24.0f : 1;
        float n = mot_noise();
        a += (n - a) * 0.6f;
        float v = a * expf(-t * 260.0f) * 1.2f + 0.7f * fsin(2 * PI_F * 900.0f * t) * expf(-t * 120.0f) +
                  0.3f * fsin(2 * PI_F * 2300.0f * t) * expf(-t * 200.0f);
        s_cleat_pcm[i] = (int16_t)clampf(v * env * 15000.0f, -30000, 30000);
    }
    s_mot_ok = true;
    s_mot_next = s_cleat_next = 0;
    s_mot_v = 0;
}

// a sample into the queue of the mixer: it starts delay samples after the next block. (dur: a third more than the
// sample, so the fade of the mixer at the end of a voice comes after the sample, not on its window)
static void motor_voice(const int16_t *pcm, int n, float rate, int vol, int64_t delay)
{
    audio_voice_t v = {0};
    v.wave = W_SAMPLE;
    v.sample = pcm;
    v.sample_len = (uint32_t)n;
    v.rate = rate;
    v.dur = (uint32_t)((float)n / rate * 1.34f) + 64;
    v.vol = (uint8_t)(vol < 0 ? 0 : vol > 100 ? 100 : vol);
    v.attack_ms = 1;
    v.shape = AUDIO_SUSTAIN;
    v.group = MOT_GROUP;
    v.delay = (uint32_t)(delay < 0 ? 0 : delay > AUDIO_SAMPLE_RATE / 2 ? AUDIO_SAMPLE_RATE / 2 : delay);
    audio_play(&v);
}

// each frame (the game task): the samples of the next MOT_AHEAD samples. now_us: the time of the timer
static void motor_frame(int64_t now_us)
{
    if (!s_mot_ok) {
        return;
    }
    int64_t now = now_us * (AUDIO_SAMPLE_RATE / 1000) / 1000, end = now + MOT_AHEAD;
    // (the volume glides: no click at a start or a stop)
    s_mot_v += (s_mot_vol - s_mot_v) * 0.35f;
    if (s_mot_v < 0.7f) {
        s_mot_next = s_cleat_next = 0;
        return;
    }
    float rate = clampf(s_mot_rate, 0.4f, 2.6f);
    // (a gap of more than a frame, or the start: the samples start at the next block)
    if (s_mot_next < now || s_mot_next > end + MOT_LEN) {
        s_mot_next = now + AUDIO_BLOCK;
    }
    while (s_mot_next < end) {
        motor_voice(s_mot_pcm, MOT_LEN, rate, (int)(s_mot_v + 0.5f), s_mot_next - now - AUDIO_BLOCK);
        s_mot_next += (int64_t)(MOT_LEN * 0.5f / rate);
    }
    float cps = s_mot_cps;
    if (cps < 0.5f) {
        s_cleat_next = 0;
        return;
    }
    if (s_cleat_next < now || s_cleat_next > end + AUDIO_SAMPLE_RATE) {
        s_cleat_next = now + AUDIO_BLOCK;
    }
    while (s_cleat_next < end) {
        float r = 0.9f + 0.2f * (mot_noise() * 0.5f + 0.5f);
        int vol = (int)(s_mot_v * (0.45f + 0.2f * mot_noise()));
        motor_voice(s_cleat_pcm, CLEAT_LEN, r, vol, s_cleat_next - now - AUDIO_BLOCK);
        s_cleat_next += (int64_t)(AUDIO_SAMPLE_RATE / cps * (0.85f + 0.3f * (mot_noise() * 0.5f + 0.5f)));
    }
}
// the end (the leave of the app, the panel): no new samples
static void motor_end(void)
{
    s_mot_vol = 0;
    s_mot_v = 0;
    s_mot_next = s_cleat_next = 0;
}
