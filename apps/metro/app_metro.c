// METRO (UTIL): a metronome.
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_metro at the end of this file);
// the audio task calls tick() before each block of 5 ms (from enter to leave).
// The memory: the file statics of this file; no heap.
//
// - The clicks come from the audio tick (audio_set_tick, each 5 ms block): each click starts at its exact sample, so
//   there is no jitter from the frames. The tick also writes the time of each beat for the picture.
// - The tempo 30 .. 300 BPM: - / + (hold: fast), a drag up or down on the number, TAP (the mean of the last taps),
//   or a knock on the table when it is stopped (the IMU hits). The Italian name of the tempo under the number.
// - 1 .. 9 beats in a bar; a tap on a beat dot changes it: ACCENT, BEAT, MUTE (8 or 9 beats on the 320 frame and on the
//   Watcher: the tap opens the beat picker with big dots). SUB: 1, 2, 3 or 4 clicks in a beat.
//   SOUND: CLICK, WOOD, BEEP, DRUM. TRAIN: the tempo goes up by itself (+2 or +5 BPM each 4 or 8 bars).
// - The look: a wooden pendulum metronome; its weight slides up for a slow tempo (as on a real one); a light on each
//   beat. A tap on the metronome starts and stops it. It runs on with the screen off (app_t.background).
// - The round panel of the Watcher has its own layout and the wheel (see "the round layout" below).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "drumkit/drumkit.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "strokefont/strokefont.h"
#include "interfont/ck_fonts.inc"     // CK_XL: the Inter digits of CLOCK (the tempo on the Tab5 and the round panels)
#include "interfont/stocks_font.inc"  // SF_P80, SF_P60: the Inter digits of STOCKS (the tempo on the 3.49 and 320 x 240)

#if BOARD_BAR_SCREEN
#define SCR_W 640
#define SCR_H 172
#elif BOARD_UI_TABLET
#define SCR_W 640  // (the Tab5: the 640 x 360 frame; a bigger number, metronome and buttons)
#define SCR_H 360
#else
#define SCR_W 320
#define SCR_H 240
#endif

enum {
    LV_ACC,
    LV_BEAT,
    LV_MUTE
};
enum {
    SND_CLICK,
    SND_WOOD,
    SND_BEEP,
    SND_DRUM,
    N_SND
};
static const char *const SND_NAME[N_SND] = {"CLICK", "WOOD", "BEEP", "DRUM"};
static const char *const SUB_NAME[4] = {"1/4", "1/8", "TRIP", "1/16"};
enum {
    TR_OFF,
    TR_2_4,
    TR_5_4,
    TR_5_8,
    N_TR
};
static const char *const TR_NAME[N_TR] = {"OFF", "+2/4", "+5/4", "+5/8"};
static const uint8_t TR_ADD[N_TR] = {0, 2, 5, 5}, TR_BARS[N_TR] = {0, 4, 4, 8};

// the settings (the app writes them, the audio tick reads them)
static volatile int s_bpm = 100, s_beats = 4, s_sub = 1, s_snd = SND_CLICK, s_train = TR_OFF;
static volatile uint8_t s_lv[9];
static volatile bool s_play;
// the audio tick state
static uint32_t s_clock, s_next;
static int s_step = -1, s_bars;
static bool s_was;
// the beats for the picture: the time (us) and the number of the last beat
static volatile int64_t s_beat_us;
static volatile int s_beat_n, s_beat_i;
static volatile bool s_bpm_dirty;  // the trainer changed the tempo (the app saves it)

static int64_t s_now, s_taps[8];
static int s_ntaps;
static float s_swing, s_flash, s_pend;  // the pendulum angle when it stops, the light, the pendulum now
static int s_hold;                      // a - / + button held: -1 / +1
static int64_t s_hold_at, s_hold_next;
static int s_drag_y, s_drag_bpm;
static bool s_drag;
static bool s_pick;      // the beat picker is open (see "the beat picker")
static bool s_down_act;  // the last IN_DOWN did an action (- / +, TAP, the drag): its IN_TAP does nothing
static int64_t s_touch_at;

// ---------------------------------------------------------------- the sound (only from the audio tick)

static void tone(float f, int ms, int vol, uint8_t wave, uint32_t d)
{
    audio_voice_t v = {0};
    v.delay = d;
    v.dur = (uint32_t)(AUDIO_SAMPLE_RATE * ms / 1000);
    v.f0 = v.f1 = f;
    v.wave = wave;
    v.vol = (uint8_t)vol;
    v.attack_ms = 1;
    v.shape = AUDIO_PERC;
    audio_voice(&v);
}

// level: 0 = the accent, 1 = a beat, 2 = a sub click
static void click(int level, uint32_t d)
{
    switch (s_snd) {
    case SND_CLICK:
        drum_voice(KIT_909, DK_RIM, level == 0 ? 127 : level == 1 ? 100 : 55, level == 0 ? 7 : level == 1 ? 0 : -5, d);
        break;
    case SND_WOOD:
        tone(level == 0 ? 2200 : level == 1 ? 1600 : 1300, 28, level == 2 ? 30 : 70, W_SINE, d);
        tone(level == 0 ? 4400 : 3200, 8, level == 2 ? 8 : 20, W_TRI, d);
        break;
    case SND_BEEP:
        tone(level == 0 ? 1760 : level == 1 ? 880 : 880, level == 2 ? 20 : 45, level == 2 ? 25 : 60, W_PULSE, d);
        break;
    default:
        if (level == 0) {
            drum_voice(KIT_808, DK_KICK, 120, 0, d);
        }
        drum_voice(KIT_808, level == 2 ? DK_SHAKER : DK_CHAT, level == 2 ? 60 : 100, 0, d);
        if (level == 1 && s_beats % 2 == 0) {
            drum_voice(KIT_808, DK_RIM, 50, 0, d);
        }
        break;
    }
}

static uint32_t step_len(void)
{
    int bpm = s_bpm < 30 ? 30 : s_bpm > 300 ? 300 : s_bpm;
    return (uint32_t)(AUDIO_SAMPLE_RATE * 60.0f / bpm / s_sub);
}

static void tick(void)
{
    bool play = s_play;
    uint32_t end = s_clock + AUDIO_BLOCK;
    if (play && !s_was) {
        s_next = s_clock + AUDIO_BLOCK / 2;  // start in this block
        s_step = -1;
        s_bars = 0;
    }
    s_was = play;
    while (play && (int32_t)(s_next - end) < 0) {
        uint32_t d = (int32_t)(s_next - s_clock) > 0 ? s_next - s_clock : 0;
        int n = s_beats * s_sub;
        s_step = (s_step + 1) % n;
        int beat = s_step / s_sub, sub = s_step % s_sub;
        if (s_step == 0) {
            s_bars++;
            int tr = s_train;
            if (tr != TR_OFF && s_bars > 1 && (s_bars - 1) % TR_BARS[tr] == 0 && s_bpm < 300) {
                s_bpm = s_bpm + TR_ADD[tr] > 300 ? 300 : s_bpm + TR_ADD[tr];
                s_bpm_dirty = true;
            }
        }
        int lv = s_lv[beat];
        if (sub == 0) {
            if (lv != LV_MUTE) {
                click(lv == LV_ACC ? 0 : 1, d);
            }
            s_beat_us = esp_timer_get_time() + (int64_t)d * 1000000 / AUDIO_SAMPLE_RATE;
            s_beat_i = beat;
            s_beat_n++;
        } else if (lv != LV_MUTE) {
            click(2, d);
        }
        s_next += step_len();
    }
    s_clock = end;
}

static void stop_tick(void)
{
    audio_set_tick(NULL);
}

// ---------------------------------------------------------------- the settings

static void save(void)
{
    uint32_t acc = 0;
    for (int i = 0; i < 9; i++) {
        acc |= (uint32_t)(s_lv[i] & 3) << (2 * i);
    }
    store_set("mt_bpm", (uint32_t)s_bpm);
    store_set("mt_beats", (uint32_t)s_beats);
    store_set("mt_sub", (uint32_t)s_sub);
    store_set("mt_snd", (uint32_t)s_snd);
    store_set("mt_train", (uint32_t)s_train);
    store_set("mt_acc", acc);
}

static void load(void)
{
    int v = (int)store_get("mt_bpm", 100);
    s_bpm = v >= 30 && v <= 300 ? v : 100;
    v = (int)store_get("mt_beats", 4);
    s_beats = v >= 1 && v <= 9 ? v : 4;
    v = (int)store_get("mt_sub", 1);
    s_sub = v >= 1 && v <= 4 ? v : 1;
    s_snd = (int)(store_get("mt_snd", 0) % N_SND);
    s_train = (int)(store_get("mt_train", 0) % N_TR);
    uint32_t acc = store_get("mt_acc", 0xFFFFFFFF);
    for (int i = 0; i < 9; i++) {
        int lv = acc == 0xFFFFFFFF ? (i == 0 ? LV_ACC : LV_BEAT) : (int)((acc >> (2 * i)) & 3);
        s_lv[i] = (uint8_t)(lv > LV_MUTE ? LV_BEAT : lv);
    }
}

static const char *tempo_name(int bpm)
{
    return bpm < 40    ? "GRAVE"
           : bpm < 60  ? "LARGO"
           : bpm < 66  ? "LARGHETTO"
           : bpm < 76  ? "ADAGIO"
           : bpm < 108 ? "ANDANTE"
           : bpm < 120 ? "MODERATO"
           : bpm < 156 ? "ALLEGRO"
           : bpm < 176 ? "VIVACE"
           : bpm < 200 ? "PRESTO"
                       : "PRESTISSIMO";
}

static void set_bpm(int b)
{
    s_bpm = b < 30 ? 30 : b > 300 ? 300 : b;
}

// ---------------------------------------------------------------- the layout

typedef struct {
    int x, y, w, h;
} box_t;
// (the thumb, round 3: a beat dot is DOT_MIN px wide or more, else a tap on the dots opens the beat picker; the hit
// zones of the rows touch but do not overlap)
#if BOARD_BAR_SCREEN
static const box_t NUM = {6, 6, 176, 100};  // the tempo number (a drag changes it)
static const box_t MINUS = {8, 116, 52, 48}, TAP = {66, 116, 58, 48}, PLUS = {130, 116, 52, 48};
static const box_t PEND = {190, 4, 118, 164};  // the metronome (a tap: start / stop)
static const box_t DOTS = {314, 8, 324, 40};   // 9 beats: 36 px (4.8 mm) each
static const box_t CH[4] = {{314, 58, 158, 50}, {480, 58, 158, 50}, {314, 114, 158, 50}, {480, 114, 158, 50}};
#define DOT_MIN 36
#define NUM_H   62.0f  // the height of the tempo number
#elif BOARD_UI_TABLET
static const box_t NUM = {8, 6, 336, 140};
static const box_t MINUS = {10, 152, 100, 72}, TAP = {118, 152, 118, 72}, PLUS = {244, 152, 100, 72};
static const box_t PEND = {370, 4, 240, 226};
static const box_t DOTS = {10, 234, 620, 52};  // 9 beats: 68 px (11.6 mm) each
static const box_t CH[4] = {{10, 292, 149, 62}, {167, 292, 149, 62}, {324, 292, 149, 62}, {481, 292, 149, 62}};
#define DOT_MIN 56
#define NUM_H   92.0f
#else
static const box_t NUM = {4, 4, 168, 92};
static const box_t MINUS = {6, 100, 50, 50}, TAP = {60, 100, 56, 50}, PLUS = {120, 100, 50, 50};
static const box_t PEND = {178, 4, 138, 148};
static const box_t DOTS = {6, 158, 308, 32};
static const box_t CH[4] = {{6, 198, 74, 40}, {84, 198, 74, 40}, {162, 198, 74, 40}, {240, 198, 74, 40}};
#define DOT_MIN 42  // (4.6 mm on the AMOLED)
#define NUM_H   52.0f
#endif
#define BTN_PAD 4  // the hit zone of - / TAP / + and of the dots: this much more
#define CH_PAD  3
enum {
    CH_BEATS,
    CH_SUB,
    CH_SND,
    CH_TRAIN
};

static bool in_box(const box_t *b, int x, int y, int pad)
{
    return x >= b->x - pad && x < b->x + b->w + pad && y >= b->y - pad && y < b->y + b->h + pad;
}

static uint16_t mix(uint16_t a, uint16_t b, int k)
{
    a = (uint16_t)((a >> 8) | (a << 8));
    b = (uint16_t)((b >> 8) | (b << 8));
    int r = ((a >> 11) * (255 - k) + (b >> 11) * k) / 255,
        g = (((a >> 5) & 63) * (255 - k) + ((b >> 5) & 63) * k) / 255;
    int bl = ((a & 31) * (255 - k) + (b & 31) * k) / 255;
    uint16_t c = (uint16_t)((r << 11) | (g << 5) | bl);
    return (uint16_t)((c >> 8) | (c << 8));
}

static void rbox(const box_t *b, uint16_t c, uint16_t edge)
{
    gfx_fill_rect(b->x + 3, b->y, b->w - 6, b->h, c);
    gfx_fill_rect(b->x, b->y + 3, b->w, b->h - 6, c);
    gfx_fill_circle(b->x + 3, b->y + 3, 3, c);
    gfx_fill_circle(b->x + b->w - 4, b->y + 3, 3, c);
    gfx_fill_circle(b->x + 3, b->y + b->h - 4, 3, c);
    gfx_fill_circle(b->x + b->w - 4, b->y + b->h - 4, 3, c);
    gfx_fill_rect(b->x + 4, b->y, b->w - 8, 1, edge);
}

static void button(const box_t *b, const char *t, bool on)
{
    rbox(b, on ? rgb(150, 70, 40) : rgb(48, 36, 30), on ? rgb(255, 170, 110) : rgb(110, 84, 64));
    const mfont_t *f = mf_width(&MF_N16, t, 1) <= b->w - 8 ? &MF_N16 : &MF_SMALL;
    mf_text_c(f, b->x + b->w / 2, b->y + b->h / 2 + 6, t, rgb(255, 238, 220), 255, 1);
}

static void chip(const box_t *b, const char *label, const char *val)
{
    rbox(b, rgb(40, 31, 27), rgb(110, 84, 64));
    mf_text_c(&MF_TINY, b->x + b->w / 2, b->y + 12, label, rgb(170, 140, 110), 255, 0);
    const mfont_t *f = mf_width(&MF_N16, val, 1) <= b->w - 6 ? &MF_N16 : &MF_SMALL;
    mf_text_c(f, b->x + b->w / 2, b->y + b->h - 9, val, rgb(255, 230, 200), 255, 1);
}

// ---------------------------------------------------------------- drawing

// a filled quad (a trapezoid) from 4 points, by rows (convex)
static void quad(const float *p, uint16_t c)
{
    float y0 = p[1], y1 = p[1];
    for (int i = 1; i < 4; i++) {
        if (p[i * 2 + 1] < y0) {
            y0 = p[i * 2 + 1];
        }
        if (p[i * 2 + 1] > y1) {
            y1 = p[i * 2 + 1];
        }
    }
    for (int y = (int)ceilf(y0); y <= (int)y1; y++) {
        float xa = 1e9f, xb = -1e9f;
        for (int i = 0; i < 4; i++) {
            float ax = p[i * 2], ay = p[i * 2 + 1], bx = p[(i + 1) % 4 * 2], by = p[(i + 1) % 4 * 2 + 1];
            if ((y >= ay && y <= by) || (y >= by && y <= ay)) {
                float x = by == ay ? ax : ax + (bx - ax) * (y - ay) / (by - ay);
                if (x < xa) {
                    xa = x;
                }
                if (x > xb) {
                    xb = x;
                }
            }
        }
        if (xb >= xa) {
            gfx_fill_rect((int)xa, y, (int)(xb - xa) + 1, 1, c);
        }
    }
}

static void draw_metronome(int64_t now)
{
    const box_t *b = &PEND;
    float cx = b->x + b->w / 2.0f, base = b->y + b->h - 4, top = b->y + 10;
    float hw = b->w * 0.34f, tw = b->w * 0.07f;
    // the light on the beat
    if (s_flash > 0.01f) {
        // a soft glow: rings that are lighter to the middle, only inside the box of the metronome
        gfx_clip_box(b->x, b->y, b->x + b->w, b->y + b->h);
        int r = (int)(b->h * 0.46f);
        for (int k = 0; k < 4; k++) {
            int a = (int)(s_flash * (30 + k * 26));
            gfx_fill_circle((int)cx, (int)(base - b->h * 0.42f), r - k * r / 5,
                            mix(rgb(26, 18, 15), rgb(150, 80, 36), a));
        }
        gfx_clip_box_off();
    }
    // the case: a wooden pyramid with a lighter face and a dark slot
    float cs[8] = {cx - hw, base, cx + hw, base, cx + tw, top, cx - tw, top};
    quad(cs, rgb(92, 50, 28));
    float face[8] = {cx - hw * 0.78f, base - 6, cx + hw * 0.78f, base - 6,
                     cx + tw * 0.7f,  top + 10, cx - tw * 0.7f,  top + 10};
    quad(face, rgb(132, 76, 42));
    float slot[8] = {cx - hw * 0.22f, base - 30, cx + hw * 0.22f, base - 30,
                     cx + tw * 0.35f, top + 18,  cx - tw * 0.35f, top + 18};
    quad(slot, rgb(40, 22, 14));
    // the scale marks on the slot
    for (int i = 0; i < 9; i++) {
        float y = top + 26 + i * (base - 60 - top) / 9.0f;
        gfx_fill_rect((int)(cx + hw * 0.24f), (int)y, 4, 1, rgb(220, 190, 140));
    }
    gfx_fill_rect((int)(cx - hw - 4), (int)base, (int)(2 * hw + 8), 4, rgb(60, 32, 18));
    // the pendulum: it turns around a point low in the case
    float px = cx, py = base - 24, len = base - 24 - top - 4;
    float a = s_pend;
    float sx = fsin(a), cy = -fcos(a);
    float ex = px + sx * len, ey = py + cy * len;
    stroke_line(px, py, ex, ey, 1.3f, rgb(225, 215, 200));
    // the weight: higher on the rod for a slow tempo
    float k = 0.25f + 0.62f * (1 - (s_bpm - 30) / 270.0f);
    float wx = px + sx * len * k, wy = py + cy * len * k, ww = b->w * 0.08f, wh = b->w * 0.07f;
    float wq[8] = {wx - ww - cy * 0, wy - wh, wx + ww, wy - wh, wx + ww * 0.7f, wy + wh, wx - ww * 0.7f, wy + wh};
    quad(wq, mix(rgb(200, 180, 120), rgb(255, 240, 180), (int)(s_flash * 255)));
    gfx_fill_rect((int)(wx - ww * 0.8f), (int)wy - 1, (int)(ww * 1.6f), 2, rgb(150, 120, 70));
    gfx_fill_circle((int)px, (int)py, 4, rgb(200, 170, 110));
    if (!s_play) {
        int tw2 = mf_width(&MF_SMALL, "TAP TO START", 1) + 14;
        gfx_fill_rect((int)cx - tw2 / 2, (int)base - 18, tw2, 16, rgb(30, 18, 12));
        mf_text_c(&MF_SMALL, (int)cx, (int)base - 6, "TAP TO START", rgb(255, 220, 170), 255, 1);
    }
    (void)now;
}

// THE TEMPO in the Inter digits (a real anti-aliased face, not the stroke font): its digits in the middle of a band of
// h px from y (the band of the old stroke digits: the words under it keep their places)
static void tempo_text(const mfont_t *f, int cx, float y, float h, const char *t, uint16_t c)
{
    const mglyph_t *g = mf_glyph(f, '0');
    const int dh = g ? -g->oy : f->asc;
    mf_text_c(f, cx, (int)(y + (h + (float)dh) / 2), t, c, 255, 0);
}

static void draw_number(int64_t now)
{
    const box_t *b = &NUM;
    char t[8];
    snprintf(t, sizeof t, "%d", s_bpm);
#if BOARD_BAR_SCREEN
    const mfont_t *f = &SF_P80;  // (digits of 59 px for the band of 62)
#elif BOARD_UI_TABLET
    const mfont_t *f = &CK_XL;  // (69 px for 92)
#else
    const mfont_t *f = &SF_P60;  // (44 px for 52)
#endif
    float h = NUM_H, y = b->y + 6;
    uint16_t c = mix(rgb(255, 236, 210), rgb(255, 170, 90), (int)(s_flash * 255));
    if (s_drag) {
        c = rgb(255, 200, 120);
    }
    tempo_text(f, b->x + b->w / 2, y, h, t, c);
    mf_text_c(&MF_TINY, b->x + b->w / 2, (int)(y + h + 17), "BPM", rgb(170, 140, 110), 255, 0);
    mf_text_c(&MF_N16, b->x + b->w / 2, (int)(y + h + 35), tempo_name(s_bpm), rgb(255, 210, 160), 255, 1);
    (void)now;
}

static void draw_dots(void)
{
    const box_t *b = &DOTS;
    int n = s_beats, step = b->w / n, r = step / 2 - 3;
#if BOARD_UI_TABLET
    if (r > 22) {
        r = 22;
    }
#else
    if (r > 15) {
        r = 15;
    }
#endif
    int cur = s_play ? s_beat_i : -1;
    for (int i = 0; i < n; i++) {
        int x = b->x + step * i + step / 2, y = b->y + b->h / 2;
        int lv = s_lv[i];
        bool on = i == cur && s_flash > 0.2f;
        uint16_t c = lv == LV_ACC ? rgb(255, 140, 60) : lv == LV_BEAT ? rgb(230, 200, 160) : rgb(70, 54, 46);
        if (on) {
            gfx_fill_circle(x, y, r + 3, rgb(255, 230, 160));
        }
        if (lv == LV_MUTE) {
            gfx_ring(x, y, r, r - 2, c);
        } else {
            gfx_fill_circle(x, y, lv == LV_ACC ? r : r * 3 / 4, c);
        }
    }
}

// ---------------------------------------------------------------- the app

static void metro_enter(int64_t now)
{
    s_now = now;
    load();
    s_play = false;
    s_was = false;
    s_clock = 0;
    s_ntaps = 0;
    s_pend = s_swing = 0;
    s_flash = 0;
    s_hold = 0;
    s_drag = false;
    s_pick = false;
    imu_hits_clear();
#if BOARD_ROUND
    input_wheel();  // (drop the steps that wait)
    input_wheel_presses();
#endif
    audio_set_tick(tick);
}

static void metro_leave(void)
{
    s_play = false;
    audio_set_tick(stop_tick);
    save();
}

static void tap_tempo(int64_t now)
{
    if (s_ntaps && now - s_taps[s_ntaps - 1] > 2000) {
        s_ntaps = 0;
    }
    if (s_ntaps == 8) {
        memmove(s_taps, s_taps + 1, 7 * sizeof(s_taps[0]));
        s_ntaps = 7;
    }
    s_taps[s_ntaps++] = now;
    if (s_ntaps >= 2) {
        int64_t span = s_taps[s_ntaps - 1] - s_taps[0];
        set_bpm((int)lroundf(60000.0f * (s_ntaps - 1) / (float)span));
    }
    s_flash = 1;
}

// a tap on the metronome: start or stop
static void play_toggle(void)
{
    s_play = !s_play;
    if (!s_play) {
        s_swing = s_pend;
    }
    sfx_play(SFX_CLICK);
}

// a tap on beat dot i: ACCENT, BEAT, MUTE
static void dot_cycle(int i)
{
    s_lv[i] = (uint8_t)((s_lv[i] + 1) % 3);
    save();
}

// a tap on chip k: the next value
static void chip_cycle(int k)
{
    if (k == CH_BEATS) {
        int nb = s_beats % 9 + 1;
        for (int i = s_beats; i < nb; i++) {
            s_lv[i] = LV_BEAT;
        }
        s_beats = nb;
    } else if (k == CH_SUB) {
        s_sub = s_sub % 4 + 1;
    } else if (k == CH_SND) {
        s_snd = (s_snd + 1) % N_SND;
    } else {
        s_train = (s_train + 1) % N_TR;
    }
    save();
    sfx_play(SFX_CLICK);
}

// ---------------------------------------------------------------- the beat picker (round 3, the thumb)
// With 8 or 9 beats a beat dot is too small for a thumb on the 320 frame and on the Watcher (under 4.6 mm). Then a tap
// on the dots opens this picker: the beats as big dots in one or two rows (each 54 px or more). A tap on a dot changes
// it (ACCENT, BEAT, MUTE); DONE, or a tap out of the dots, closes the picker. While it is open, it gets all touches.

// the area of the dots, DONE, and the baseline of the title (the frame of the board)
#if BOARD_ROUND
// the round layout is made for 412 x 412; ru() gives the size in the frame now (gfx_w: 466 on the 1.75C)
static inline int ru(int v)
{
    return v * gfx_w / 412;
}
static inline float ruf(float v)
{
    return v * gfx_w / 412.0f;
}
#endif
static void pk_area(box_t *a, box_t *done, int *title_y)
{
#if BOARD_ROUND
    if (gfx_round()) {
        *a = (box_t){ru(70), ru(110), ru(272), ru(160)};
        *done = (box_t){ru(136), ru(284), ru(140), ru(56)};
        *title_y = ru(98);
        return;
    }
#endif
#if BOARD_UI_TABLET
    *a = (box_t){20, 50, 600, 228};
    *done = (box_t){220, 288, 200, 62};
    *title_y = 36;
#elif BOARD_BAR_SCREEN
    *a = (box_t){150, 30, 340, 96};
    *done = (box_t){250, 128, 140, 40};
    *title_y = 22;
#else
    *a = (box_t){10, 36, 300, 152};
    *done = (box_t){60, 194, 200, 44};
    *title_y = 28;
#endif
}

// the cell of beat i of n: one row for 5 beats or fewer, else two rows (the second row in the middle)
static void pk_cell(int i, int n, const box_t *a, box_t *c)
{
    int cols = n <= 5 ? n : (n + 1) / 2, rows = n <= 5 ? 1 : 2;
    int r = i / cols, k = i % cols, in_row = r == rows - 1 ? n - cols * r : cols;
    int cw = a->w / cols, ch = a->h / rows;
    *c = (box_t){a->x + (a->w - cw * in_row) / 2 + k * cw, a->y + r * ch, cw, ch};
}

static void pk_draw(void)
{
    box_t a, d;
    int ty;
    pk_area(&a, &d, &ty);
    int fw = SCR_W, fh = SCR_H;
#if BOARD_ROUND
    if (gfx_round()) {
        fw = gfx_w;
        fh = gfx_h;
    }
#endif
    gfx_darken_rect(0, 0, fw, fh, 2);
    mf_text_c(&MF_SMALL, a.x + a.w / 2, ty, "TAP A BEAT: ACCENT, BEAT, MUTE", rgb(255, 220, 170), 255, 1);
    int n = s_beats, cur = s_play ? s_beat_i : -1;
    for (int i = 0; i < n; i++) {
        box_t c;
        pk_cell(i, n, &a, &c);
        box_t k = {c.x + 3, c.y + 3, c.w - 6, c.h - 6};
        rbox(&k, rgb(48, 36, 30), rgb(110, 84, 64));
        int cx = c.x + c.w / 2, cy = c.y + c.h / 2 - 6, r = (c.w < c.h ? c.w : c.h) / 2 - 12;
        if (r > 22) {
            r = 22;
        }
        int lv = s_lv[i];
        uint16_t col = lv == LV_ACC ? rgb(255, 140, 60) : lv == LV_BEAT ? rgb(230, 200, 160) : rgb(90, 70, 58);
        if (i == cur && s_flash > 0.2f) {
            gfx_fill_circle(cx, cy, r + 4, rgb(255, 230, 160));
        }
        if (lv == LV_MUTE) {
            gfx_ring(cx, cy, r, r - 2, col);
        } else {
            gfx_fill_circle(cx, cy, lv == LV_ACC ? r : r * 3 / 4, col);
        }
        char t[4];
        snprintf(t, sizeof t, "%d", i + 1);
        mf_text_c(&MF_TINY, cx, c.y + c.h - 7, t, rgb(170, 140, 110), 255, 0);
    }
    rbox(&d, rgb(150, 70, 40), rgb(255, 170, 110));
    mf_text_c(&MF_N16, d.x + d.w / 2, d.y + d.h / 2 + 6, "DONE", rgb(255, 238, 220), 255, 1);
}

// the events of the open picker: a tap on a dot changes it; DONE or a tap out of the dots closes the picker
static void pk_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    box_t a, d;
    int ty;
    pk_area(&a, &d, &ty);
    for (int i = 0; i < s_beats; i++) {
        box_t c;
        pk_cell(i, s_beats, &a, &c);
        if (in_box(&c, ev->x, ev->y, 0)) {
            dot_cycle(i);
            sfx_play(SFX_TICK);
            return;
        }
    }
    s_pick = false;
    sfx_play(SFX_CLICK);
}

#if BOARD_UI_TABLET
// the pull zone of the settings panel (PL_TAB_L of sysui.c: x 200 .. 440, y < 40): no drag of the number starts there
static bool in_pull(int x, int y)
{
    return y < 40 && x >= 200 && x < 440;
}
#elif !BOARD_BAR_SCREEN && !BOARD_ROUND
// the pull zone of the settings panel (the middle of the top edge): no drag of the number starts there
static bool in_pull(int x, int y)
{
    return y < 24 && x >= 120 && x < 200;
}
#else
static bool in_pull(int x, int y)
{
    (void)x;
    (void)y;
    return false;
}
#endif

#if BOARD_ROUND
// ---------------------------------------------------------------- the round layout (the Watcher: 412 x 412, a wheel)
// The tempo is big in the middle, the beat dots are on an arc at the top, and the wooden metronome stands at the
// bottom. The round buttons go around the inner rim: - and + at the sides; BEATS, SUB, SOUND, TRAIN and TAP at the
// bottom. The wheel changes the tempo (a fast turn: 2 BPM a step). A push of the wheel starts or stops the metronome.
// (APP_ROUND_HI; the numbers are for the 412 frame, r_layout() sizes them from gfx_w)
#define RC    (gfx_w / 2)  // the middle of the round frame
#define R_DOT ru(170)      // the radius of the arc of the beat dots

typedef struct {
    int x, y, r;
} rbtn_t;
static const rbtn_t R0_MINUS = {56, 206, 32}, R0_PLUS = {356, 206, 32}, R0_TAP = {206, 362, 28};
static const rbtn_t R0_CH[4] = {{76, 281, 32}, {131, 336, 32}, {281, 336, 32}, {336, 281, 32}};  // as CH[]
static const box_t R0_NUM = {96, 76, 220, 118};     // the tempo (a drag changes it)
static const box_t R0_PEND = {144, 196, 124, 134};  // the metronome (a tap: start / stop)
static rbtn_t s_r_minus, s_r_plus, s_r_tap, s_r_ch[4];
static box_t s_r_num, s_r_pend;
static int64_t s_r_save;  // 0, or the time to save the tempo (after the last wheel step)

static rbtn_t rb_size(rbtn_t b)
{
    return (rbtn_t){ru(b.x), ru(b.y), ru(b.r)};
}
static box_t box_size(box_t b)
{
    return (box_t){ru(b.x), ru(b.y), ru(b.w), ru(b.h)};
}
// the targets in the frame now (each render and each touch)
static void r_layout(void)
{
    s_r_minus = rb_size(R0_MINUS);
    s_r_plus = rb_size(R0_PLUS);
    s_r_tap = rb_size(R0_TAP);
    for (int k = 0; k < 4; k++) {
        s_r_ch[k] = rb_size(R0_CH[k]);
    }
    s_r_num = box_size(R0_NUM);
    s_r_pend = box_size(R0_PEND);
}

// the point at the angle deg (clockwise from the top) and the radius r from the middle
static void r_pol(float deg, float r, float *x, float *y)
{
    float a = deg * 0.0174533f;
    *x = RC + fsin(a) * r;
    *y = RC - fcos(a) * r;
}

static bool r_hit(const rbtn_t *b, int x, int y, int pad)
{
    int dx = x - b->x, dy = y - b->y, r = b->r + pad;
    return dx * dx + dy * dy <= r * r;
}

// the middle of beat dot i of n on the top arc (the first beat at the left)
static void r_dot(int i, int n, float *x, float *y)
{
    float step = n > 1 ? 112.0f / (n - 1) : 0;
    if (step > 18) {
        step = 18;
    }
    r_pol(((n - 1) * -0.5f + i) * step, R_DOT, x, y);
}

// all the rows of the frame: black corners, and the circle with a gradient from top to bot
static void r_bg(uint16_t top, uint16_t bot)
{
    for (int y = 0; y < 2 * RC; y++) {
        float d = y + 0.5f - RC;
        int hw = (int)sqrtf((float)(RC * RC) - d * d);
        gfx_fill_rect(0, y, RC - hw, 1, 0);
        gfx_fill_rect(RC - hw, y, 2 * hw, 1, mix(top, bot, y * 255 / (2 * RC)));
        gfx_fill_rect(RC + hw, y, RC - hw, 1, 0);
    }
}

static void r_button(const rbtn_t *b, bool on)
{
    gfx_fill_circle(b->x, b->y, b->r, on ? rgb(150, 70, 40) : rgb(48, 36, 30));
    gfx_ring(b->x, b->y, b->r, b->r - 2, on ? rgb(255, 170, 110) : rgb(110, 84, 64));
}

static void r_chip(const rbtn_t *b, const char *label, const char *val)
{
    r_button(b, false);
    mf_text_c(&MF_TINY, b->x, b->y - ru(8), label, rgb(170, 140, 110), 255, 0);
    const mfont_t *f = mf_width(&MF_N16, val, 1) <= 56 ? &MF_N16 : &MF_SMALL;
    mf_text_c(f, b->x, b->y + ru(13), val, rgb(255, 230, 200), 255, 1);
}

// the wheel: each frame
static void r_wheel(int64_t now)
{
    int d = input_wheel();
    if (d) {
        set_bpm(s_bpm + d * (abs(d) >= 3 ? 2 : 1));
        s_r_save = now + 1000;
        if (!s_play) {
            sfx_play(SFX_TICK);
        }
    }
    for (int n = input_wheel_presses(); n > 0; n--) {
        play_toggle();
    }
    if (s_r_save && now >= s_r_save) {
        s_r_save = 0;
        save();
    }
}

static void r_input(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
    int64_t now = ev->t_ms;
    s_touch_at = now;
    r_layout();
    if (ev->kind == IN_DOWN) {
        s_down_act = true;
        if (r_hit(&s_r_minus, x, y, 6) || r_hit(&s_r_plus, x, y, 6)) {
            s_hold = r_hit(&s_r_plus, x, y, 6) ? 1 : -1;
            set_bpm(s_bpm + s_hold);
            s_hold_at = now;
            s_hold_next = now + 450;
            sfx_play(SFX_TICK);
        } else if (r_hit(&s_r_tap, x, y, 6)) {
            tap_tempo(now);
        } else if (in_box(&s_r_num, x, y, 0)) {
            s_drag = true;
            s_drag_y = y;
            s_drag_bpm = s_bpm;
        } else {
            s_down_act = false;
        }
        return;
    }
    if (ev->kind == IN_MOVE && s_drag) {
        set_bpm(s_drag_bpm + (s_drag_y - y) / 3);  // up = faster
        return;
    }
    if (ev->kind == IN_UP) {
        if (s_hold || s_drag) {
            save();
        }
        s_hold = 0;
        s_drag = false;
        return;
    }
    if (ev->kind != IN_TAP || s_down_act) {
        return;
    }
    // the nearest beat dot within 26 px
    int best = -1;
    float bd = ruf(26) * ruf(26);
    for (int i = 0; i < s_beats; i++) {
        float dx, dy;
        r_dot(i, s_beats, &dx, &dy);
        dx -= x;
        dy -= y;
        if (dx * dx + dy * dy < bd) {
            bd = dx * dx + dy * dy;
            best = i;
        }
    }
    if (best >= 0) {
        // (the thumb: 52 px = 4.6 mm between two dots on the arc; closer dots: the picker)
        float step = s_beats > 1 ? 112.0f / (s_beats - 1) : 18;
        if (step > 18) {
            step = 18;
        }
        if (2 * R_DOT * fsin(step * 0.5f * 0.0174533f) < 52) {
            s_pick = true;
            sfx_play(SFX_CLICK);
        } else {
            dot_cycle(best);
        }
        return;
    }
    for (int k = 0; k < 4; k++) {
        if (r_hit(&s_r_ch[k], x, y, 5)) {
            chip_cycle(k);
            return;
        }
    }
    if (in_box(&s_r_pend, x, y, 0)) {
        play_toggle();
    }
}

// the wooden metronome at the bottom: the case, the scale, the pendulum and its weight
static void r_metronome(void)
{
    const float K = gfx_w / 412.0f;  // (the numbers are for the 412 frame)
    const float cx = RC, base = 326 * K, top = 208 * K, hw = 46 * K, tw = 11 * K;
    if (s_flash > 0.01f) {
        for (int k = 0; k < 4; k++) {
            int a = (int)(s_flash * (30 + k * 26));
            gfx_fill_circle(RC, ru(262), ru(76 - k * 15), mix(rgb(24, 16, 14), rgb(150, 80, 36), a));
        }
    }
    float cs[8] = {cx - hw, base, cx + hw, base, cx + tw, top, cx - tw, top};
    quad(cs, rgb(92, 50, 28));
    float face[8] = {cx - hw * 0.78f, base - 5 * K, cx + hw * 0.78f, base - 5 * K,
                     cx + tw * 0.7f,  top + 8 * K,  cx - tw * 0.7f,  top + 8 * K};
    quad(face, rgb(132, 76, 42));
    float slot[8] = {cx - hw * 0.24f, base - 22 * K, cx + hw * 0.24f, base - 22 * K,
                     cx + tw * 0.4f,  top + 14 * K,  cx - tw * 0.4f,  top + 14 * K};
    quad(slot, rgb(40, 22, 14));
    for (int i = 0; i < 8; i++) {
        float y = top + 20 * K + i * (base - 44 * K - top) / 8.0f;
        gfx_fill_rect((int)(cx + hw * 0.26f), (int)y, ru(4), 1, rgb(220, 190, 140));
    }
    gfx_fill_rect((int)(cx - hw - 6 * K), (int)base, (int)(2 * hw + 12 * K), ru(4), rgb(60, 32, 18));
    // the pendulum turns around a point low in the case; the weight is higher on the rod for a slow tempo
    float px = cx, py = base - 14 * K, len = py - 198 * K;
    float sx = fsin(s_pend), cy = -fcos(s_pend);
    stroke_line(px, py, px + sx * len, py + cy * len, 1.3f * K, rgb(225, 215, 200));
    float k = 0.25f + 0.62f * (1 - (s_bpm - 30) / 270.0f);
    float wx = px + sx * len * k, wy = py + cy * len * k, ww = 11 * K, wh = 9 * K;
    float wq[8] = {wx - ww, wy - wh, wx + ww, wy - wh, wx + ww * 0.7f, wy + wh, wx - ww * 0.7f, wy + wh};
    quad(wq, mix(rgb(200, 180, 120), rgb(255, 240, 180), (int)(s_flash * 255)));
    gfx_fill_rect((int)(wx - ww * 0.8f), (int)wy - 1, (int)(ww * 1.6f), 2, rgb(150, 120, 70));
    gfx_fill_circle((int)px, (int)py, ru(4), rgb(200, 170, 110));
    if (!s_play) {
        int w = mf_width(&MF_SMALL, "TAP TO START", 1) + 14;
        gfx_fill_rect(RC - w / 2, ru(265) - 13, w, 17, rgb(30, 18, 12));
        mf_text_c(&MF_SMALL, RC, ru(265), "TAP TO START", rgb(255, 220, 170), 255, 1);
    }
}

static void r_render(int64_t now)
{
    r_layout();
    r_bg(rgb(34, 23, 20), rgb(16, 11, 9));
    gfx_ring(RC, RC, RC, RC - ru(7), rgb(62, 38, 24));  // a wooden rim
    r_metronome();
    // the tempo, BPM over it, the name of the tempo under it
    char t[8];
    snprintf(t, sizeof t, "%d", s_bpm);
    const float h = ruf(84);
    uint16_t c = s_drag ? rgb(255, 200, 120) : mix(rgb(255, 236, 210), rgb(255, 170, 90), (int)(s_flash * 255));
    tempo_text(&CK_XL, RC, ruf(84), h, t, c);
    mf_text_c(&MF_TINY, RC, ru(76), "BPM", rgb(170, 140, 110), 255, 0);
    mf_text_c(&MF_N16, RC, ru(192), tempo_name(s_bpm), rgb(255, 210, 160), 255, 1);
    // the beat dots
    int cur = s_play ? s_beat_i : -1;
    for (int i = 0; i < s_beats; i++) {
        float fx, fy;
        r_dot(i, s_beats, &fx, &fy);
        int x = (int)lroundf(fx), y = (int)lroundf(fy), lv = s_lv[i];
        uint16_t col = lv == LV_ACC ? rgb(255, 140, 60) : lv == LV_BEAT ? rgb(230, 200, 160) : rgb(90, 70, 58);
        if (i == cur && s_flash > 0.2f) {
            gfx_fill_circle(x, y, ru(17), rgb(255, 230, 160));
        }
        if (lv == LV_MUTE) {
            gfx_ring(x, y, ru(11), ru(11) - 2, col);
        } else {
            gfx_fill_circle(x, y, ru(lv == LV_ACC ? 13 : 10), col);
        }
    }
    // the buttons around the inner rim
    const uint16_t cream = rgb(255, 238, 220);
    const int a = ru(12);
    r_button(&s_r_minus, s_hold < 0);
    stroke_line(s_r_minus.x - a, s_r_minus.y, s_r_minus.x + a, s_r_minus.y, ruf(3), cream);
    r_button(&s_r_plus, s_hold > 0);
    stroke_line(s_r_plus.x - a, s_r_plus.y, s_r_plus.x + a, s_r_plus.y, ruf(3), cream);
    stroke_line(s_r_plus.x, s_r_plus.y - a, s_r_plus.x, s_r_plus.y + a, ruf(3), cream);
    r_button(&s_r_tap, s_ntaps > 0 && now - s_taps[s_ntaps - 1] < 300);
    mf_text_c(&MF_N16, s_r_tap.x, s_r_tap.y + 7, "TAP", cream, 255, 1);
    snprintf(t, sizeof t, "%d", s_beats);
    r_chip(&s_r_ch[CH_BEATS], "BEATS", t);
    r_chip(&s_r_ch[CH_SUB], "SUB", SUB_NAME[s_sub - 1]);
    r_chip(&s_r_ch[CH_SND], "SOUND", SND_NAME[s_snd]);
    r_chip(&s_r_ch[CH_TRAIN], "TRAIN", TR_NAME[s_train]);
}
#endif

static void metro_input(const input_event_t *ev)
{
    if (s_pick) {
        s_touch_at = ev->t_ms;
        pk_input(ev);
        return;
    }
#if BOARD_ROUND
    if (gfx_round()) {
        r_input(ev);
        return;
    }
#endif
    int x = ev->x, y = ev->y;
    int64_t now = ev->t_ms;
    s_touch_at = now;
    if (ev->kind == IN_DOWN) {
        // the steppers and TAP act at the touch (the time counts); the other targets act at the tap
        s_down_act = true;
        if (in_box(&MINUS, x, y, BTN_PAD) || in_box(&PLUS, x, y, BTN_PAD)) {
            s_hold = in_box(&PLUS, x, y, BTN_PAD) ? 1 : -1;
            set_bpm(s_bpm + s_hold);
            s_hold_at = now;
            s_hold_next = now + 450;
            sfx_play(SFX_TICK);
        } else if (in_box(&NUM, x, y, 0) && !in_pull(x, y)) {
            s_drag = true;
            s_drag_y = y;
            s_drag_bpm = s_bpm;
        } else if (in_box(&TAP, x, y, BTN_PAD)) {
            tap_tempo(now);
        } else {
            s_down_act = false;
        }
        return;
    }
    if (ev->kind == IN_MOVE && s_drag) {
        set_bpm(s_drag_bpm + (s_drag_y - y) / 3);  // up = faster
        return;
    }
    if (ev->kind == IN_UP) {
        if (s_hold || s_drag) {
            save();
        }
        s_hold = 0;
        s_drag = false;
        return;
    }
    if (ev->kind != IN_TAP || s_down_act) {
        return;
    }
    if (in_box(&PEND, x, y, 0)) {
        play_toggle();
        return;
    }
    if (in_box(&DOTS, x, y, BTN_PAD)) {
        if (DOTS.w / s_beats < DOT_MIN) {
            s_pick = true;  // the dots are too small for a thumb: the picker
            sfx_play(SFX_CLICK);
            return;
        }
        int i = (x - DOTS.x) / (DOTS.w / s_beats);
        if (i < 0) {
            i = 0;
        }
        if (i >= s_beats) {
            i = s_beats - 1;
        }
        dot_cycle(i);
        return;
    }
    for (int k = 0; k < 4; k++) {
        if (!in_box(&CH[k], x, y, CH_PAD)) {
            continue;
        }
        chip_cycle(k);
        return;
    }
}

static void metro_update(int64_t now)
{
    float dt = (now - s_now) / 1000.0f;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    s_now = now;
#if BOARD_ROUND
    if (gfx_round()) {
        r_wheel(now);
    }
#endif
    // a held - / + : faster and faster
    if (s_hold && now >= s_hold_next) {
        set_bpm(s_bpm + s_hold * (now - s_hold_at > 1500 ? 5 : 1));
        s_hold_next = now + 80;
    }
    // knocks on the table set the tempo when it is stopped (the speaker can shake the board while it plays)
    imu_hit_t h;
    while (imu_pop_hit(&h)) {
        int64_t ht = h.t_us > 0 ? h.t_us / 1000 : now;
        if (!s_play && llabs(ht - s_touch_at) > 250) {
            tap_tempo(ht);
        }
    }
    if (s_bpm_dirty) {
        s_bpm_dirty = false;
        save();
    }
    // the pendulum: at each beat it is at one end (left on even beats); it swings less at a fast tempo
    static int last_n;
    float amp = 0.42f - 0.18f * (s_bpm - 30) / 270.0f;
    if (s_play && s_beat_n > 0) {
        float beat_s = 60.0f / s_bpm;
        float ph = (float)((esp_timer_get_time() - s_beat_us) / 1e6) / beat_s;
        if (ph < 0) {
            ph = 0;
        }
        if (ph > 1.5f) {
            ph = 1.5f;
        }
        float sgn = (s_beat_n & 1) ? 1.0f : -1.0f;
        s_pend = sgn * amp * fcos(3.14159f * ph);
        if (s_beat_n != last_n) {
            last_n = s_beat_n;
            s_flash = s_lv[s_beat_i] == LV_MUTE ? 0.3f : s_lv[s_beat_i] == LV_ACC ? 1.0f : 0.7f;
        }
    } else {
        s_swing += (0 - s_swing) * (dt * 4 > 1 ? 1 : dt * 4);
        s_pend = s_swing;
    }
    s_flash -= dt * 3.5f;
    if (s_flash < 0) {
        s_flash = 0;
    }
}

static void metro_render(int64_t now)
{
#if BOARD_ROUND
    if (gfx_round()) {
        r_render(now);
        if (s_pick) {
            pk_draw();
        }
        return;
    }
#endif
    // a warm dark room
    for (int y = 0; y < SCR_H; y += 4) {
        int k = y * 255 / SCR_H;
        gfx_fill_rect(0, y, SCR_W, 4,
                      rgb((uint8_t)(30 - k * 10 / 255), (uint8_t)(20 - k * 7 / 255), (uint8_t)(18 - k * 6 / 255)));
    }
    draw_metronome(now);
    draw_number(now);
    button(&MINUS, "-", s_hold < 0);
    button(&TAP, "TAP", s_ntaps > 0 && now - s_taps[s_ntaps - 1] < 300);
    button(&PLUS, "+", s_hold > 0);
    draw_dots();
    char t[8];
    snprintf(t, sizeof t, "%d", s_beats);
    chip(&CH[CH_BEATS], "BEATS", t);
    chip(&CH[CH_SUB], "SUB", SUB_NAME[s_sub - 1]);
    chip(&CH[CH_SND], "SOUND", SND_NAME[s_snd]);
    chip(&CH[CH_TRAIN], "TRAIN", TR_NAME[s_train]);
    if (s_pick) {
        pk_draw();
    }
}

static bool metro_background(void)
{
    return s_play;
}
static bool metro_awake(void)
{
    return s_play;
}

#include "tficon/tficon.h"
// the menu icon: a metronome; the arm swings (two beats in 2 s) and the light flashes at each beat
static void metro_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0x5d7bff), tfi_hex(0x1b2a8a));
    float u = s / 100.0f, ph = tfi_ph(2);
    // the body: a shadow, the wood, the face
    float bd[8] = {x + 38 * u, y + 16 * u, x + 62 * u, y + 16 * u, x + 78 * u, y + 86 * u, x + 22 * u, y + 86 * u};
    float sd[8] = {bd[0], bd[1] + 3 * u, bd[2], bd[3] + 3 * u, bd[4], bd[5] + 3 * u, bd[6], bd[7] + 3 * u};
    tfi_convex(bd, 4, tfi_hex(0xd2691e), 256);
    float fc[8] = {x + 42 * u, y + 22 * u, x + 58 * u, y + 22 * u, x + 70 * u, y + 70 * u, x + 30 * u, y + 70 * u};
    tfi_convex(fc, 4, tfi_hex(0xffe2b8), 256);
    tfi_drop(x + (int)(22 * u), y + (int)(72 * u), (int)(56 * u), (int)(14 * u), (int)(3 * u), (int)(3 * u), 80);
    tfi_rbox(x + (int)(22 * u), y + (int)(72 * u), (int)(56 * u), (int)(14 * u), (int)(3 * u), tfi_hex(0xb35418),
             tfi_hex(0x7a3410), 256);
    // the arm: a swing of 1 s each way
    float sw = fsin(ph * 6.2831853f), a = -1.5708f + 0.42f * sw;
    float px = x + 50 * u, py = y + 72 * u, L = 50 * u;
    tfi_line(px, py, px + fcos(a) * L, py + fsin(a) * L, 3.2f * u, true, tfi_hex(0x3a3f4b), 256);
    float wx = px + fcos(a) * L * 0.62f, wy = py + fsin(a) * L * 0.62f;
    tfi_rbox((int)(wx - 6 * u), (int)(wy - 4 * u), (int)(12 * u), (int)(9 * u), (int)(2 * u), tfi_hex(0xfff0c8),
             tfi_hex(0xd8b060), 256);
    tfi_disc(px, py, 4 * u, tfi_hex(0x6a707e), tfi_hex(0x2a2e38), 256);
    // the beat light at each end of the swing
    float beat = sw * sw * sw * sw;
    beat *= beat;
    tfd_glow(x + 50 * u, y + 13 * u, 13 * u, tfi_hex(0xfff59d), (int)(240 * beat));
    tfi_disc(x + 50 * u, y + 13 * u, 4 * u, tfd_mix(tfi_hex(0x8a5a1a), rgb(255, 255, 255), (int)(256 * beat)),
             tfd_mix(tfi_hex(0x5a3a10), tfi_hex(0xffd54f), (int)(256 * beat)), 256);
    tfi_end();
}

const app_t app_metro = {
        .name = "METRO",
        .landscape = true,
        .enter = metro_enter,
        .leave = metro_leave,
        .input = metro_input,
        .update = metro_update,
        .render = metro_render,
        .draw_icon = metro_icon,
        .awake = metro_awake,
        .background = metro_background,
        .round = APP_ROUND_HI,
        .wheel = true,
        .run = true,  // the settings panel: the beat goes on under it
};
