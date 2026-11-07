// Drum sounds from synth voices. A drum is up to 4 layers: a pitch sweep for the
// body (kick, toms), filtered noise for the snap and the metal (snare, hats,
// cymbals), and short bursts for claps. Each kit has its own recipes.

#include <math.h>
#include "tfn/audio.h"
#include "drumkit.h"

typedef struct {
    uint8_t wave;
    uint16_t f0, f1;   // Hz (noise: the rate of new random values)
    uint16_t ms;       // length; 0 = no layer
    uint8_t vol, att;  // 0..100, attack in ms
    uint8_t lp, hp;    // filters (see audio_voice_t), 0 = off
    uint8_t dly;       // ms after the hit
} layer_t;

#define NL 4
typedef struct {
    layer_t l[NL];
} drum_t;

#define L(w, a, b, ms, v, at, lp, hp, d) {w, a, b, ms, v, at, lp, hp, d}

const char *const DRUM_NAMES[DK_COUNT] = {
        "KICK", "SNARE", "CLAP", "HAT", "OPEN", "TOM L", "TOM H", "CRASH", "RIDE", "RIM", "BELL", "SHAKE",
};
const char *const KIT_NAMES[KIT_COUNT] = {"808", "909", "ROCK", "LO-FI", "JUNK"};

static const drum_t KITS[KIT_COUNT][DK_COUNT] =
        {
                [KIT_808] =
                        {
                                [DK_KICK] = {{L(W_SINE, 150, 42, 420, 100, 1, 0, 0, 0),
                                              L(W_NOISE, 3000, 3000, 6, 18, 1, 0, 0, 0)}},
                                [DK_SNARE] = {{L(W_TRI, 240, 170, 90, 45, 1, 0, 0, 0),
                                               L(W_NOISE, 7000, 5000, 200, 40, 1, 220, 70, 0)}},
                                [DK_CLAP] = {{L(W_NOISE, 6000, 6000, 14, 42, 1, 200, 110, 0),
                                              L(W_NOISE, 6000, 6000, 14, 38, 1, 200, 110, 11),
                                              L(W_NOISE, 6000, 6000, 14, 34, 1, 200, 110, 22),
                                              L(W_NOISE, 6000, 5000, 180, 30, 2, 190, 100, 30)}},
                                [DK_CHAT] = {{L(W_NOISE, 12000, 12000, 45, 22, 1, 0, 200, 0),
                                              L(W_SQUARE, 3200, 3200, 30, 5, 1, 0, 200, 0)}},
                                [DK_OHAT] = {{L(W_NOISE, 12000, 11000, 320, 18, 1, 0, 200, 0),
                                              L(W_SQUARE, 3300, 3300, 200, 4, 1, 0, 210, 0)}},
                                [DK_TOM_LO] = {{L(W_SINE, 130, 85, 320, 85, 1, 0, 0, 0),
                                                L(W_NOISE, 3000, 3000, 20, 10, 1, 120, 0, 0)}},
                                [DK_TOM_HI] = {{L(W_SINE, 210, 140, 260, 80, 1, 0, 0, 0),
                                                L(W_NOISE, 3000, 3000, 20, 10, 1, 120, 0, 0)}},
                                [DK_CRASH] = {{L(W_NOISE, 9000, 6500, 1400, 28, 3, 0, 150, 0),
                                               L(W_SQUARE, 4100, 3900, 700, 4, 2, 0, 200, 0)}},
                                [DK_RIDE] = {{L(W_NOISE, 11000, 10000, 900, 10, 1, 0, 220, 0),
                                              L(W_TRI, 2900, 2900, 700, 10, 1, 0, 0, 0),
                                              L(W_SQUARE, 4300, 4300, 500, 3, 1, 0, 220, 0)}},
                                [DK_RIM] = {{L(W_PULSE, 1750, 1650, 25, 30, 1, 0, 60, 0),
                                             L(W_TRI, 480, 420, 30, 25, 1, 0, 0, 0)}},
                                [DK_COWBELL] =
                                        {{L(W_SQUARE, 540, 540, 260, 16, 1, 170, 60, 0),
                                          L(W_SQUARE, 800, 800, 260, 16, 1, 170, 60, 0)}},
                                [DK_SHAKER] = {{L(W_NOISE, 10000, 10000, 70, 16, 25, 0, 190, 0)}},
                        },
                [KIT_909] =
                        {
                                [DK_KICK] = {{L(W_TRI, 220, 48, 280, 100, 1, 0, 0, 0),
                                              L(W_SINE, 110, 50, 250, 60, 1, 0, 0, 0),
                                              L(W_NOISE, 5000, 5000, 8, 30, 1, 0, 90, 0)}},
                                [DK_SNARE] = {{L(W_TRI, 330, 190, 80, 40, 1, 0, 0, 0),
                                               L(W_TRI, 180, 160, 100, 30, 1, 0, 0, 0),
                                               L(W_NOISE, 9000, 7000, 230, 50, 1, 0, 90, 0)}},
                                [DK_CLAP] = {{L(W_NOISE, 8000, 8000, 12, 44, 1, 0, 130, 0),
                                              L(W_NOISE, 8000, 8000, 12, 40, 1, 0, 130, 9),
                                              L(W_NOISE, 8000, 8000, 12, 36, 1, 0, 130, 18),
                                              L(W_NOISE, 8000, 6000, 220, 32, 2, 0, 120, 27)}},
                                [DK_CHAT] = {{L(W_NOISE, 14000, 14000, 50, 26, 1, 0, 215, 0)}},
                                [DK_OHAT] = {{L(W_NOISE, 14000, 13000, 380, 20, 1, 0, 215, 0)}},
                                [DK_TOM_LO] = {{L(W_SINE, 160, 95, 280, 80, 1, 0, 0, 0),
                                                L(W_NOISE, 2500, 2500, 30, 14, 1, 140, 0, 0)}},
                                [DK_TOM_HI] = {{L(W_SINE, 260, 170, 230, 78, 1, 0, 0, 0),
                                                L(W_NOISE, 2500, 2500, 30, 14, 1, 140, 0, 0)}},
                                [DK_CRASH] = {{L(W_NOISE, 12000, 8000, 1600, 30, 2, 0, 170, 0)}},
                                [DK_RIDE] = {{L(W_NOISE, 13000, 12000, 1100, 12, 1, 0, 225, 0),
                                              L(W_SQUARE, 5200, 5200, 800, 4, 1, 0, 225, 0)}},
                                [DK_RIM] = {{L(W_PULSE, 2200, 2100, 20, 32, 1, 0, 90, 0),
                                             L(W_TRI, 600, 550, 25, 20, 1, 0, 0, 0)}},
                                [DK_COWBELL] = {{L(W_PULSE, 587, 587, 200, 15, 1, 190, 0, 0),
                                                 L(W_PULSE, 845, 845, 200, 15, 1, 190, 0, 0)}},
                                [DK_SHAKER] = {{L(W_NOISE, 12000, 12000, 60, 18, 15, 0, 200, 0)}},
                        },
                [KIT_ROCK] =
                        {
                                [DK_KICK] = {{L(W_SINE, 110, 55, 220, 100, 1, 0, 0, 0),
                                              L(W_NOISE, 1800, 1200, 25, 40, 1, 110, 0, 0),
                                              L(W_TRI, 70, 50, 180, 40, 1, 0, 0, 0)}},
                                [DK_SNARE] = {{L(W_TRI, 200, 180, 120, 40, 1, 0, 0, 0),
                                               L(W_NOISE, 6000, 5000, 260, 55, 1, 210, 50, 0),
                                               L(W_NOISE, 3000, 2500, 60, 30, 1, 150, 0, 0)}},
                                [DK_CLAP] = {{L(W_NOISE, 5000, 5000, 15, 40, 1, 170, 70, 0),
                                              L(W_NOISE, 5000, 5000, 15, 36, 1, 170, 70, 13),
                                              L(W_NOISE, 5000, 4000, 120, 30, 1, 170, 70, 26)}},
                                [DK_CHAT] = {{L(W_NOISE, 10000, 10000, 60, 20, 1, 230, 170, 0)}},
                                [DK_OHAT] = {{L(W_NOISE, 10000, 9000, 450, 17, 1, 230, 170, 0)}},
                                [DK_TOM_LO] = {{L(W_SINE, 100, 80, 450, 85, 1, 0, 0, 0),
                                                L(W_TRI, 200, 160, 200, 20, 1, 0, 0, 0),
                                                L(W_NOISE, 2000, 2000, 40, 15, 1, 120, 0, 0)}},
                                [DK_TOM_HI] = {{L(W_SINE, 170, 140, 380, 80, 1, 0, 0, 0),
                                                L(W_TRI, 340, 280, 160, 18, 1, 0, 0, 0),
                                                L(W_NOISE, 2500, 2500, 40, 15, 1, 130, 0, 0)}},
                                [DK_CRASH] = {{L(W_NOISE, 8000, 6000, 2000, 30, 3, 240, 120, 0)}},
                                [DK_RIDE] = {{L(W_NOISE, 9000, 9000, 1400, 9, 1, 0, 200, 0),
                                              L(W_TRI, 3400, 3400, 1200, 10, 1, 0, 0, 0),
                                              L(W_SINE, 5100, 5100, 900, 6, 1, 0, 0, 0)}},
                                [DK_RIM] = {{L(W_PULSE, 1200, 1100, 30, 30, 1, 200, 60, 0),
                                             L(W_NOISE, 4000, 4000, 20, 15, 1, 0, 0, 0)}},
                                [DK_COWBELL] = {{L(W_SQUARE, 560, 560, 300, 14, 1, 150, 0, 0),
                                                 L(W_SQUARE, 845, 845, 300, 12, 1, 150, 0, 0)}},
                                [DK_SHAKER] = {{L(W_NOISE, 8000, 8000, 90, 14, 35, 220, 160, 0)}},
                        },
                [KIT_LOFI] =
                        {
                                [DK_KICK] = {{L(W_SQUARE, 90, 40, 250, 55, 1, 60, 0, 0),
                                              L(W_SINE, 120, 45, 300, 70, 1, 0, 0, 0)}},
                                [DK_SNARE] = {{L(W_NOISE, 3500, 2500, 180, 45, 1, 140, 40, 0),
                                               L(W_SQUARE, 200, 180, 60, 20, 1, 100, 0, 0)}},
                                [DK_CLAP] = {{L(W_NOISE, 3000, 3000, 15, 40, 1, 130, 60, 0),
                                              L(W_NOISE, 3000, 3000, 15, 36, 1, 130, 60, 12),
                                              L(W_NOISE, 3000, 2500, 140, 30, 1, 130, 60, 24)}},
                                [DK_CHAT] = {{L(W_NOISE, 5000, 5000, 50, 22, 1, 170, 150, 0)}},
                                [DK_OHAT] = {{L(W_NOISE, 5000, 4500, 300, 18, 1, 170, 150, 0)}},
                                [DK_TOM_LO] = {{L(W_SQUARE, 110, 80, 250, 35, 1, 70, 0, 0),
                                                L(W_SINE, 110, 80, 250, 50, 1, 0, 0, 0)}},
                                [DK_TOM_HI] = {{L(W_SQUARE, 170, 130, 220, 35, 1, 80, 0, 0),
                                                L(W_SINE, 170, 130, 220, 50, 1, 0, 0, 0)}},
                                [DK_CRASH] = {{L(W_NOISE, 4500, 3000, 1100, 28, 2, 170, 110, 0)}},
                                [DK_RIDE] = {{L(W_NOISE, 5500, 5500, 700, 12, 1, 190, 170, 0),
                                              L(W_SQUARE, 2200, 2200, 500, 5, 1, 160, 0, 0)}},
                                [DK_RIM] = {{L(W_SQUARE, 900, 850, 30, 25, 1, 150, 0, 0)}},
                                [DK_COWBELL] = {{L(W_SQUARE, 440, 440, 200, 16, 1, 110, 0, 0),
                                                 L(W_SQUARE, 660, 660, 200, 14, 1, 110, 0, 0)}},
                                [DK_SHAKER] = {{L(W_NOISE, 4000, 4000, 80, 16, 30, 170, 140, 0)}},
                        },
                [KIT_JUNK] =
                        {
                                // a box, a tin can, a slap, keys, coins, a pot, a pan, a trash lid, a bottle, a stick, a glass, rice
                                [DK_KICK] = {{L(W_SINE, 95, 70, 150, 90, 1, 0, 0, 0),
                                              L(W_NOISE, 900, 700, 80, 45, 1, 90, 0, 0)}},
                                [DK_SNARE] = {{L(W_SQUARE, 820, 760, 120, 16, 1, 0, 60, 0),
                                               L(W_SQUARE, 1290, 1210, 100, 12, 1, 0, 80, 0),
                                               L(W_NOISE, 6000, 6000, 90, 30, 1, 0, 120, 0)}},
                                [DK_CLAP] = {{L(W_NOISE, 2500, 2000, 40, 50, 1, 150, 40, 0)}},
                                [DK_CHAT] = {{L(W_SQUARE, 5300, 5300, 40, 8, 1, 0, 230, 0),
                                              L(W_SQUARE, 7100, 7100, 40, 6, 1, 0, 230, 0),
                                              L(W_NOISE, 11000, 11000, 40, 10, 1, 0, 220, 0)}},
                                [DK_OHAT] = {{L(W_SQUARE, 4800, 4800, 300, 6, 1, 0, 225, 0),
                                              L(W_SQUARE, 6700, 6600, 250, 5, 1, 0, 225, 0),
                                              L(W_NOISE, 11000, 10000, 200, 8, 1, 0, 225, 0)}},
                                [DK_TOM_LO] = {{L(W_SINE, 180, 175, 600, 60, 1, 0, 0, 0),
                                                L(W_SINE, 432, 430, 450, 25, 1, 0, 0, 0),
                                                L(W_SINE, 745, 740, 300, 15, 1, 0, 0, 0)}},
                                [DK_TOM_HI] = {{L(W_SINE, 330, 325, 500, 55, 1, 0, 0, 0),
                                                L(W_SINE, 790, 785, 350, 22, 1, 0, 0, 0),
                                                L(W_SINE, 1370, 1360, 250, 12, 1, 0, 0, 0)}},
                                [DK_CRASH] = {{L(W_NOISE, 7000, 4000, 1300, 30, 2, 0, 100, 0),
                                               L(W_SQUARE, 1030, 990, 900, 8, 1, 0, 120, 0),
                                               L(W_SQUARE, 1570, 1510, 800, 6, 1, 0, 120, 0)}},
                                [DK_RIDE] = {{L(W_SINE, 1650, 1650, 500, 30, 1, 0, 0, 0),
                                              L(W_SINE, 3310, 3310, 300, 8, 1, 0, 0, 0)}},
                                [DK_RIM] = {{L(W_NOISE, 2500, 2500, 20, 35, 1, 180, 80, 0),
                                             L(W_TRI, 950, 900, 30, 20, 1, 0, 0, 0)}},
                                [DK_COWBELL] = {{L(W_SINE, 2090, 2090, 700, 25, 1, 0, 0, 0),
                                                 L(W_SINE, 5230, 5230, 400, 10, 1, 0, 0, 0)}},
                                [DK_SHAKER] = {{L(W_NOISE, 9000, 9000, 110, 14, 50, 0, 210, 0)}},
                        },
};

static const uint8_t GM[DK_COUNT] = {36, 38, 39, 42, 46, 45, 48, 49, 51, 37, 56, 70};

int drum_gm_note(int drum)
{
    return (drum >= 0 && drum < DK_COUNT) ? GM[drum] : 36;
}

static void hit(int kit, int drum, int vel, int tune, uint32_t delay, bool from_tick)
{
    if (kit < 0 || kit >= KIT_COUNT || drum < 0 || drum >= DK_COUNT) {
        return;
    }
    if (vel < 1) {
        vel = 1;
    }
    if (vel > 127) {
        vel = 127;
    }
    float k = tune ? powf(2.0f, tune / 12.0f) : 1.0f;
    // a soft hit is darker and quieter
    float amp = 0.2f + 0.8f * vel / 127.0f;
    const drum_t *d = &KITS[kit][drum];
    for (int i = 0; i < NL; i++) {
        const layer_t *l = &d->l[i];
        if (!l->ms) {
            continue;
        }
        uint8_t lp = l->lp;
        if (vel < 80 && l->wave == W_NOISE) {
            int dark = (lp ? lp : 255) - (80 - vel);
            lp = (uint8_t)(dark < 40 ? 40 : dark);
        }
        audio_voice_t v = {
                .delay = delay + (uint32_t)l->dly * AUDIO_SAMPLE_RATE / 1000,
                .dur = (uint32_t)l->ms * AUDIO_SAMPLE_RATE / 1000,
                .f0 = l->f0 * k,
                .f1 = l->f1 * k,
                .wave = l->wave,
                .vol = (uint8_t)(l->vol * amp + 0.5f),
                .attack_ms = l->att,
                .shape = AUDIO_PERC,
                .lp = lp,
                .hp = l->hp,
        };
        if (v.f0 > 20000) {
            v.f0 = 20000;
        }
        if (v.f1 > 20000) {
            v.f1 = 20000;
        }
        if (from_tick) {
            audio_voice(&v);
        } else {
            audio_play(&v);
        }
    }
}

void drum_voice(int kit, int drum, int vel, int tune, uint32_t delay)
{
    hit(kit, drum, vel, tune, delay, true);
}

void drum_play(int kit, int drum, int vel, int tune)
{
    hit(kit, drum, vel, tune, 0, false);
}
