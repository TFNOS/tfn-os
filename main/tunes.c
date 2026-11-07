// The start tunes of the games (see tunes.h). Each tune is about 1 to 1.6 s, in the
// style of its game. Times and lengths are in ms; the notes are MIDI notes (60 = C4).
// The audio task starts each note at its time (audio_tune()).

#include "audio.h"
#include "tunes.h"

// note names (MIDI numbers)
enum {
    C2 = 36,
    D2 = 38,
    E2 = 40,
    F2 = 41,
    G2 = 43,
    A2 = 45,
    Bb2 = 46,
    B2 = 47,
    C3 = 48,
    D3 = 50,
    Eb3 = 51,
    E3 = 52,
    F3 = 53,
    G3 = 55,
    A3 = 57,
    Bb3 = 58,
    B3 = 59,
    C4 = 60,
    Db4 = 61,
    D4 = 62,
    Eb4 = 63,
    E4 = 64,
    F4 = 65,
    Gb4 = 66,
    G4 = 67,
    Ab4 = 68,
    A4 = 69,
    Bb4 = 70,
    B4 = 71,
    C5 = 72,
    Db5 = 73,
    D5 = 74,
    Eb5 = 75,
    E5 = 76,
    F5 = 77,
    Gb5 = 78,
    G5 = 79,
    Ab5 = 80,
    A5 = 81,
    Bb5 = 82,
    B5 = 83,
    C6 = 84,
    Db6 = 85,
    D6 = 86,
    E6 = 88,
    F6 = 89,
    G6 = 91,
    A6 = 93,
    B6 = 95,
    C7 = 96,
    E7 = 100,
};

#define N(t, d, n, w, v)     {(t), (d), (n), 0, (w), (v)}
#define SL(t, d, n, b, w, v) {(t), (d), (n), (b), (w), (v)}  // the pitch slides b semitones
#define HOLD(w)              ((w) | TUNE_HOLD)

// drums
#define KICK(t)  SL(t, 150, B2, -24, W_SINE, 70)
#define SNARE(t) SL(t, 130, 100, -8, W_NOISE, 20), SL(t, 80, G3, -7, W_TRI, 40)
#define HAT(t)   N(t, 30, 122, W_NOISE, 9)
#define CRASH(t) N(t, 600, 112, W_NOISE, 14)

// RUN: a fast chiptune run up the scale, 150 BPM
static const tune_note_t RUN[] = {
        N(0, 90, C5, W_PULSE, 22),
        N(100, 90, E5, W_PULSE, 22),
        N(200, 90, G5, W_PULSE, 22),
        N(300, 90, C6, W_PULSE, 22),
        N(400, 90, B5, W_PULSE, 22),
        N(500, 90, G5, W_PULSE, 22),
        N(600, 90, A5, W_PULSE, 22),
        N(700, 90, B5, W_PULSE, 22),
        N(800, 450, C6, HOLD(W_PULSE), 20),
        N(800, 450, G5, HOLD(W_SQUARE), 10),
        N(0, 180, C3, W_TRI, 55),
        N(200, 150, C4, W_TRI, 50),
        N(400, 180, G2, W_TRI, 55),
        N(600, 150, G3, W_TRI, 50),
        N(800, 450, C3, HOLD(W_TRI), 55),
        KICK(0),
        HAT(200),
        SNARE(400),
        HAT(600),
        KICK(800),
};

// HOP: bouncy springs up an F major chord, then a bell
static const tune_note_t HOP[] = {
        SL(0, 130, F4, 12, W_SQUARE, 18),
        SL(200, 130, A4, 12, W_SQUARE, 18),
        SL(400, 130, C5, 12, W_SQUARE, 18),
        SL(540, 80, C4, 19, W_TRI, 45),
        N(620, 380, F5, HOLD(W_PULSE), 20),
        N(620, 380, A5, HOLD(W_SQUARE), 9),
        N(620, 500, F6, W_SINE, 34),
        N(0, 140, F2, W_TRI, 58),
        N(200, 140, C3, W_TRI, 58),
        N(400, 140, F2, W_TRI, 58),
        N(620, 380, F3, HOLD(W_TRI), 55),
        KICK(0),
        KICK(400),
        KICK(620),
};

// KNIGHT: a castle fanfare in D minor, with timpani
static const tune_note_t KNIGHT[] = {
        N(0, 70, D5, W_PULSE, 22),           N(100, 70, D5, W_PULSE, 22),         N(200, 70, D5, W_PULSE, 22),
        N(300, 300, A5, HOLD(W_PULSE), 22),  N(300, 300, D5, HOLD(W_SQUARE), 9),  N(650, 110, G5, W_PULSE, 20),
        N(800, 110, F5, W_PULSE, 20),        N(950, 110, G5, W_PULSE, 20),        N(1100, 550, A5, HOLD(W_PULSE), 22),
        N(1100, 550, F5, HOLD(W_SQUARE), 9), N(1100, 550, D5, HOLD(W_SQUARE), 9), SL(0, 250, D3, -2, W_TRI, 75),
        SL(300, 300, A2, -2, W_TRI, 75),     SL(950, 90, A2, -1, W_TRI, 60),      SL(1025, 90, A2, -1, W_TRI, 65),
        SL(1100, 600, D2, -2, W_TRI, 85),
};

// DRIVE: the engine goes up, then a rock riff with power chords
static const tune_note_t DRIVE[] = {
        SL(0, 650, E2, 24, W_SAW, 22),
        SL(0, 650, E3, 24, W_SQUARE, 7),
        N(650, 130, E3, W_SAW, 16),
        N(650, 130, B3, W_SAW, 14),
        N(800, 130, E3, W_SAW, 16),
        N(800, 130, B3, W_SAW, 14),
        N(950, 130, G3, W_SAW, 16),
        N(950, 130, D4, W_SAW, 14),
        N(1100, 250, A3, W_SAW, 16),
        N(1100, 250, E4, W_SAW, 14),
        N(1400, 450, E3, HOLD(W_SAW), 16),
        N(1400, 450, B3, HOLD(W_SAW), 14),
        N(1400, 450, E4, HOLD(W_SAW), 10),
        KICK(650),
        SNARE(800),
        KICK(950),
        SNARE(1100),
        KICK(1250),
        KICK(1400),
        CRASH(1400),
};

// PONG: the ball goes from side to side, faster, then a bright A major chord
static const tune_note_t PONG[] = {
        N(0, 50, A4, W_SQUARE, 22),          N(260, 50, A5, W_SQUARE, 22),         N(480, 50, A4, W_SQUARE, 22),
        N(660, 50, A5, W_SQUARE, 22),        N(800, 50, A4, W_SQUARE, 22),         N(900, 50, A5, W_SQUARE, 22),
        N(980, 50, A4, W_SQUARE, 22),        N(1080, 420, A5, HOLD(W_SQUARE), 16), N(1080, 420, Db6, HOLD(W_PULSE), 12),
        N(1080, 420, E6, HOLD(W_PULSE), 11), N(1080, 420, A2, HOLD(W_TRI), 55),    KICK(1080),
};

// STACK: one note higher for each block that lands, then the top
static const tune_note_t STACK[] = {
        N(0, 100, C5, W_PULSE, 20),
        N(120, 100, D5, W_PULSE, 20),
        N(240, 100, E5, W_PULSE, 20),
        N(360, 100, F5, W_PULSE, 20),
        N(480, 100, G5, W_PULSE, 20),
        N(600, 100, A5, W_PULSE, 20),
        N(720, 100, B5, W_PULSE, 20),
        SL(0, 60, C3, -5, W_TRI, 45),
        SL(120, 60, D3, -5, W_TRI, 45),
        SL(240, 60, E3, -5, W_TRI, 45),
        SL(360, 60, F3, -5, W_TRI, 45),
        SL(480, 60, G3, -5, W_TRI, 45),
        SL(600, 60, A3, -5, W_TRI, 45),
        SL(720, 60, B3, -5, W_TRI, 45),
        N(840, 450, C6, HOLD(W_PULSE), 20),
        N(840, 450, G5, HOLD(W_SQUARE), 9),
        N(840, 450, E5, HOLD(W_SQUARE), 9),
        N(840, 450, C3, HOLD(W_TRI), 55),
        KICK(840),
        CRASH(840),
};

// PRISM: a sparkling arpeggio up a C major 7 chord, a soft echo down, on a pad
static const tune_note_t PRISM[] = {
        N(0, 300, C5, W_SINE, 32),       N(70, 300, E5, W_SINE, 32),      N(140, 300, G5, W_SINE, 32),
        N(210, 300, B5, W_SINE, 32),     N(280, 300, C6, W_SINE, 32),     N(350, 300, E6, W_SINE, 30),
        N(420, 300, G6, W_SINE, 28),     N(490, 300, B6, W_SINE, 26),     N(560, 500, C7, W_SINE, 26),
        N(680, 300, G6, W_SINE, 20),     N(750, 300, E6, W_SINE, 20),     N(820, 300, C6, W_SINE, 20),
        N(890, 400, G5, W_SINE, 20),     N(0, 1200, C4, HOLD(W_TRI), 28), N(0, 1200, G4, HOLD(W_TRI), 20),
        N(0, 1200, E4, HOLD(W_TRI), 18),
};

// TILT: a marble rolls down, knocks, and a chime rings
static const tune_note_t TILT[] = {
        N(0, 70, G5, W_TRI, 45),        N(55, 70, Gb5, W_TRI, 45),        N(110, 70, F5, W_TRI, 45),
        N(165, 70, E5, W_TRI, 45),      N(220, 70, Eb5, W_TRI, 45),       N(275, 70, D5, W_TRI, 45),
        N(330, 70, Db5, W_TRI, 45),     N(385, 70, C5, W_TRI, 45),        N(450, 25, 108, W_NOISE, 22),
        SL(450, 60, C4, -7, W_TRI, 50), N(520, 450, E6, W_SINE, 40),      N(640, 550, C6, W_SINE, 40),
        N(640, 550, G5, W_SINE, 28),    N(520, 600, C3, HOLD(W_TRI), 50),
};

// SPLAT: a slide whistle up and down, a big splat, then a silly oom-pah band
static const tune_note_t SPLAT[] = {
        SL(0, 280, C5, 12, W_SINE, 45),       SL(300, 280, C6, -14, W_SINE, 45),    SL(600, 220, 96, -30, W_NOISE, 32),
        SL(600, 160, C3, -7, W_TRI, 70),      N(850, 150, C3, W_TRI, 55),           N(850, 120, E5, W_SQUARE, 18),
        N(1000, 150, G2, W_TRI, 55),          N(1000, 120, G5, W_SQUARE, 18),       N(1150, 380, C3, HOLD(W_TRI), 55),
        N(1150, 380, C6, HOLD(W_SQUARE), 15), SL(1150, 200, 100, -30, W_NOISE, 24),
};

// SHAKE: a music box waltz in the snow globe
static const tune_note_t SHAKE[] = {
        N(0, 600, E6, W_SINE, 34),    N(180, 600, G6, W_SINE, 34),  N(360, 600, C7, W_SINE, 32),
        N(540, 600, B6, W_SINE, 32),  N(720, 600, G6, W_SINE, 34),  N(900, 600, A6, W_SINE, 32),
        N(1080, 800, G6, W_SINE, 34), N(1080, 800, C7, W_SINE, 16), N(1080, 800, E7, W_SINE, 12),
        N(0, 300, C3, W_TRI, 40),     N(180, 160, G4, W_SINE, 14),  N(360, 160, G4, W_SINE, 14),
        N(540, 300, G2, W_TRI, 40),   N(720, 160, F4, W_SINE, 14),  N(900, 160, F4, W_SINE, 14),
        N(1080, 600, C3, W_TRI, 40),
};

// RALLY: a sporty brass fanfare in B flat
static const tune_note_t RALLY[] = {
        N(0, 70, F4, W_PULSE, 22),
        N(80, 70, Bb4, W_PULSE, 22),
        N(160, 70, D5, W_PULSE, 22),
        N(240, 260, F5, HOLD(W_PULSE), 22),
        N(240, 260, D5, HOLD(W_SQUARE), 9),
        N(540, 100, Eb5, W_PULSE, 20),
        N(660, 100, F5, W_PULSE, 20),
        N(780, 100, G5, W_PULSE, 20),
        N(900, 450, Bb5, HOLD(W_PULSE), 22),
        N(900, 450, F5, HOLD(W_SQUARE), 9),
        N(900, 450, D5, HOLD(W_SQUARE), 9),
        N(0, 200, Bb2, W_TRI, 55),
        N(240, 260, Bb2, HOLD(W_TRI), 55),
        N(540, 120, Eb3, W_TRI, 55),
        N(780, 100, F3, W_TRI, 55),
        N(900, 450, Bb2, HOLD(W_TRI), 55),
        KICK(240),
        SNARE(540),
        KICK(660),
        SNARE(780),
        KICK(900),
        CRASH(900),
};

// ZAP: three laser shots, then a fast A minor arpeggio
static const tune_note_t ZAP[] = {
        SL(0, 110, E7, -36, W_SQUARE, 16),
        SL(150, 110, 103, -36, W_SQUARE, 16),
        SL(300, 110, 98, -36, W_SQUARE, 16),
        N(450, 80, A4, W_SAW, 14),
        N(520, 80, C5, W_SAW, 14),
        N(590, 80, E5, W_SAW, 14),
        N(660, 80, A5, W_SAW, 14),
        N(730, 80, C6, W_SAW, 14),
        N(800, 80, E6, W_SAW, 13),
        N(870, 450, A5, HOLD(W_SAW), 13),
        N(870, 450, E5, HOLD(W_SAW), 10),
        SL(870, 400, 110, -24, W_NOISE, 10),
        N(450, 380, A2, W_TRI, 55),
        N(870, 450, A2, HOLD(W_TRI), 55),
        KICK(450),
        KICK(870),
};

// ECHO: a call of three tones and a softer answer, an octave higher
static const tune_note_t ECHO[] = {
        N(0, 130, C5, W_PULSE, 20),          N(160, 130, E5, W_PULSE, 20),       N(320, 130, G5, W_PULSE, 20),
        N(80, 130, C5, W_PULSE, 7),          N(240, 130, E5, W_PULSE, 7),        N(400, 130, G5, W_PULSE, 7),
        N(520, 220, C6, W_SINE, 36),         N(680, 220, E6, W_SINE, 36),        N(840, 220, G6, W_SINE, 36),
        N(1040, 420, C6, HOLD(W_PULSE), 18), N(1040, 420, G5, HOLD(W_SINE), 25), N(1040, 420, C5, HOLD(W_TRI), 50),
};

// WOBBLE: jelly notes that bend up and down, then a boing
static const tune_note_t WOBBLE[] = {
        SL(0, 160, C5, 3, W_TRI, 55),
        SL(180, 160, E5, -3, W_TRI, 55),
        SL(360, 160, G5, 3, W_TRI, 55),
        SL(540, 160, E5, -3, W_TRI, 55),
        SL(720, 160, C6, 2, W_TRI, 55),
        SL(900, 260, C5, 12, W_SQUARE, 16),
        SL(900, 260, C3, 12, W_TRI, 60),
        N(0, 140, C3, W_TRI, 50),
        N(360, 140, G2, W_TRI, 50),
        N(720, 140, C3, W_TRI, 50),
        KICK(900),
};

// KABOOM: the fuse burns and ticks, a big boom, then a dramatic chord
static const tune_note_t KABOOM[] = {
        N(0, 650, 116, HOLD(W_NOISE), 8),  N(0, 25, C7, W_PULSE, 16),        N(160, 25, F6, W_PULSE, 16),
        N(320, 25, C7, W_PULSE, 16),       N(480, 25, F6, W_PULSE, 16),      SL(650, 700, 80, -30, W_NOISE, 45),
        SL(650, 600, A2, -20, W_SINE, 85), N(950, 450, D4, HOLD(W_SAW), 13), N(950, 450, F4, HOLD(W_SAW), 11),
        N(950, 450, A4, HOLD(W_SAW), 11),  N(950, 450, D2, HOLD(W_TRI), 60),
};

// WHACK: ribbit ribbit, then a carnival tune in G
static const tune_note_t WHACK[] = {
        SL(0, 60, D3, 5, W_SQUARE, 18),      SL(90, 70, E3, 5, W_SQUARE, 18),     N(260, 90, D5, W_PULSE, 20),
        N(370, 90, G5, W_PULSE, 20),         N(480, 90, B5, W_PULSE, 20),         N(590, 90, D6, W_PULSE, 20),
        N(700, 90, B5, W_PULSE, 20),         N(810, 90, G5, W_PULSE, 20),         N(920, 90, A5, W_PULSE, 20),
        N(1030, 380, G5, HOLD(W_PULSE), 20), N(1030, 380, B5, HOLD(W_SQUARE), 9), N(260, 100, G2, W_TRI, 55),
        N(480, 100, D3, W_TRI, 55),          N(700, 100, G2, W_TRI, 55),          N(920, 100, D3, W_TRI, 55),
        N(1030, 380, G2, HOLD(W_TRI), 55),   SL(1030, 40, C6, -12, W_TRI, 50),
};

// TILES: a short piano phrase in C, with the left hand
static const tune_note_t TILES[] = {
        N(0, 450, E5, W_TRI, 42),   N(120, 450, G5, W_TRI, 42), N(240, 450, C6, W_TRI, 42), N(360, 450, B5, W_TRI, 40),
        N(480, 450, G5, W_TRI, 40), N(600, 450, A5, W_TRI, 40), N(720, 450, B5, W_TRI, 40), N(840, 750, C6, W_TRI, 40),
        N(840, 750, G5, W_TRI, 24), N(840, 750, E5, W_TRI, 24), N(0, 600, C3, W_TRI, 34),   N(480, 400, G2, W_TRI, 34),
        N(840, 750, C3, W_TRI, 34), N(840, 750, C4, W_TRI, 22),
};

// MERGE: two voices come together on one note
static const tune_note_t MERGE[] = {
        N(0, 160, G5, W_SINE, 40),        N(160, 160, F5, W_SINE, 40),         N(320, 160, E5, W_SINE, 40),
        N(480, 160, D5, W_SINE, 40),      N(640, 160, C5, W_SINE, 40),         N(0, 160, C4, W_TRI, 50),
        N(160, 160, E4, W_TRI, 50),       N(320, 160, G4, W_TRI, 50),          N(480, 160, B4, W_TRI, 50),
        N(640, 160, C5, W_TRI, 50),       SL(780, 120, 108, -12, W_NOISE, 10), N(820, 480, C6, HOLD(W_SINE), 35),
        N(820, 480, C5, HOLD(W_TRI), 50), N(820, 480, G5, HOLD(W_SINE), 22),
};

// BUBBLE: bubbles rise and pop
static const tune_note_t BUBBLE[] = {
        SL(0, 70, C5, 7, W_SINE, 45),      SL(110, 70, G5, 7, W_SINE, 45),    SL(220, 70, E5, 7, W_SINE, 45),
        SL(330, 70, A5, 7, W_SINE, 45),    SL(440, 70, D6, 7, W_SINE, 45),    SL(550, 70, G6, 7, W_SINE, 42),
        N(60, 15, 112, W_NOISE, 12),       N(170, 15, 112, W_NOISE, 12),      N(280, 15, 112, W_NOISE, 12),
        N(390, 15, 112, W_NOISE, 12),      N(500, 15, 112, W_NOISE, 12),      N(610, 15, 112, W_NOISE, 12),
        N(700, 420, C6, HOLD(W_SINE), 35), N(700, 420, E6, HOLD(W_SINE), 25), SL(700, 300, G6, 5, W_SINE, 18),
        N(0, 200, C3, W_TRI, 40),          N(350, 200, G2, W_TRI, 40),        N(700, 420, C3, HOLD(W_TRI), 45),
};

// BURGER: the diner bell, then a swing phrase with a walking bass
static const tune_note_t BURGER[] = {
        N(0, 300, E7, W_SINE, 25),
        N(160, 450, C7, W_SINE, 25),
        N(400, 120, E5, W_PULSE, 18),
        N(533, 60, G5, W_PULSE, 18),
        N(600, 120, A5, W_PULSE, 18),
        N(733, 60, G5, W_PULSE, 18),
        N(800, 120, E5, W_PULSE, 18),
        N(933, 60, D5, W_PULSE, 18),
        N(1000, 450, C5, HOLD(W_PULSE), 14),
        N(1000, 450, E5, HOLD(W_SQUARE), 8),
        N(1000, 450, G5, HOLD(W_SQUARE), 8),
        N(1000, 450, A5, HOLD(W_SQUARE), 8),
        N(400, 180, C3, W_TRI, 55),
        N(600, 180, E3, W_TRI, 55),
        N(800, 180, G3, W_TRI, 55),
        N(1000, 450, C3, HOLD(W_TRI), 55),
        HAT(400),
        HAT(533),
        HAT(600),
        HAT(733),
        HAT(800),
        HAT(933),
        SL(600, 90, 100, -8, W_NOISE, 10),
        KICK(1000),
};

// PINBALL: three bumper pops, a fast run up, then the bells of a high score
static const tune_note_t PINBALL[] = {
        SL(0, 40, C6, -12, W_SQUARE, 18),
        SL(80, 40, E6, -12, W_SQUARE, 18),
        SL(160, 40, G6, -12, W_SQUARE, 18),
        N(260, 60, C5, W_PULSE, 22),
        N(330, 60, E5, W_PULSE, 22),
        N(400, 60, G5, W_PULSE, 22),
        N(470, 60, C6, W_PULSE, 22),
        N(560, 320, E6, HOLD(W_PULSE), 18),
        N(560, 320, C6, HOLD(W_SQUARE), 9),
        N(560, 450, C7, W_SINE, 30),
        N(700, 450, G6, W_SINE, 25),
        N(260, 20, 108, W_NOISE, 18),
        N(400, 20, 108, W_NOISE, 18),
        N(260, 150, C3, W_TRI, 55),
        N(400, 150, G2, W_TRI, 55),
        N(560, 320, C3, HOLD(W_TRI), 55),
        KICK(560),
};

// SAFE: the dial ticks, a quiet bass that goes up in A minor, the bolt opens, a bright chime
static const tune_note_t SAFE[] = {
        N(0, 15, 108, W_NOISE, 14),        N(120, 15, 108, W_NOISE, 14),
        N(240, 15, 108, W_NOISE, 14),      N(360, 15, 108, W_NOISE, 14),
        N(480, 15, 108, W_NOISE, 14),      N(600, 15, 108, W_NOISE, 14),
        N(0, 120, A2, W_TRI, 60),          N(240, 120, C3, W_TRI, 60),
        N(480, 120, D3, W_TRI, 60),        N(600, 120, Eb3, W_TRI, 60),
        N(720, 300, E3, W_TRI, 60),        N(480, 80, E5, W_PULSE, 14),
        N(600, 80, G5, W_PULSE, 14),       N(720, 300, A5, W_PULSE, 14),
        SL(1080, 120, A2, -12, W_TRI, 80), SL(1080, 100, 90, -20, W_NOISE, 30),
        N(1200, 500, A5, W_SINE, 28),      N(1200, 500, E6, W_SINE, 24),
};

// POUR: glugs that go up as the glass fills, a clink of glasses, a happy chord
static const tune_note_t POUR[] = {
        SL(0, 90, C5, 5, W_SINE, 45),      SL(140, 90, D5, 5, W_SINE, 45),    SL(280, 90, E5, 5, W_SINE, 45),
        SL(420, 90, G5, 5, W_SINE, 45),    N(0, 120, C3, W_TRI, 40),          N(280, 120, G2, W_TRI, 40),
        N(620, 300, C7, W_SINE, 30),       N(620, 200, 103, W_SINE, 18),      N(760, 420, C5, HOLD(W_TRI), 45),
        N(760, 420, E5, HOLD(W_SINE), 25), N(760, 420, G5, HOLD(W_SINE), 22), N(760, 420, C3, HOLD(W_TRI), 45),
};

// EGG RUN: "buk buk", then a fast banjo hoedown in G
static const tune_note_t EGG[] = {
        SL(0, 60, D5, 7, W_SQUARE, 16),
        SL(90, 80, E5, -5, W_SQUARE, 16),
        N(250, 70, G4, W_PULSE, 18),
        N(330, 70, B4, W_PULSE, 18),
        N(410, 70, D5, W_PULSE, 18),
        N(490, 70, G5, W_PULSE, 18),
        N(570, 70, D5, W_PULSE, 18),
        N(650, 70, B4, W_PULSE, 18),
        N(730, 70, G5, W_PULSE, 18),
        N(250, 120, G2, W_TRI, 55),
        N(410, 120, D3, W_TRI, 55),
        N(570, 120, G2, W_TRI, 55),
        N(730, 120, D3, W_TRI, 55),
        N(850, 350, G5, HOLD(W_PULSE), 18),
        N(850, 350, B4, HOLD(W_SQUARE), 9),
        N(850, 350, G2, HOLD(W_TRI), 55),
        HAT(330),
        HAT(490),
        HAT(650),
        KICK(250),
        KICK(570),
        KICK(850),
};

// MORSE: "HI" in Morse code (.... ..) on a radio tone, then a chime
static const tune_note_t MORSE[] = {
        N(0, 260, 118, W_NOISE, 5),       N(0, 70, A5, HOLD(W_SINE), 45),   N(140, 70, A5, HOLD(W_SINE), 45),
        N(280, 70, A5, HOLD(W_SINE), 45), N(420, 70, A5, HOLD(W_SINE), 45), N(700, 70, A5, HOLD(W_SINE), 45),
        N(840, 70, A5, HOLD(W_SINE), 45), N(1000, 300, E6, W_SINE, 30),     N(1080, 420, A6, W_SINE, 30),
        N(1000, 420, A3, W_TRI, 40),
};

// BRASS: two console chirps, then a short brass call that rises in F major (original)
static const tune_note_t BRASS[] = {
        N(0, 60, E6, W_SINE, 34),           N(70, 60, A6, W_SINE, 34),          N(150, 50, E6, W_SINE, 26),
        N(300, 260, C5, HOLD(W_PULSE), 18), N(300, 260, C4, HOLD(W_TRI), 45),   N(580, 260, F5, HOLD(W_PULSE), 18),
        N(580, 260, F3, HOLD(W_TRI), 45),   N(860, 700, A5, HOLD(W_PULSE), 18), N(860, 700, F5, HOLD(W_SQUARE), 8),
        N(860, 700, C5, HOLD(W_SQUARE), 8), N(860, 700, F2, HOLD(W_TRI), 55),   SL(300, 220, F2, -2, W_TRI, 70),
        SL(860, 500, F2, -2, W_TRI, 80),    N(860, 600, 112, W_NOISE, 8),
};

// TENNIS: the ball goes pock - pock - pock over the net, then a bright chord for the serve
static const tune_note_t TENNIS[] = {
        SL(0, 60, G5, -5, W_TRI, 60),        SL(300, 60, D5, -5, W_TRI, 60),
        SL(600, 60, G5, -5, W_TRI, 60),      N(0, 20, 110, W_NOISE, 10),
        N(300, 20, 110, W_NOISE, 10),        N(600, 20, 110, W_NOISE, 10),
        N(850, 90, D5, W_PULSE, 20),         N(950, 90, G5, W_PULSE, 20),
        N(1050, 90, B5, W_PULSE, 20),        N(1150, 450, D6, HOLD(W_PULSE), 18),
        N(1150, 450, B5, HOLD(W_SQUARE), 8), N(850, 280, G2, W_TRI, 55),
        N(1150, 450, G2, HOLD(W_TRI), 55),   KICK(1150),
};

// BITS: relays click into place, then a calm pentatonic phrase
static const tune_note_t BITS[] = {
        N(0, 30, 112, W_NOISE, 16),   N(110, 30, 112, W_NOISE, 16), N(180, 30, 112, W_NOISE, 16),
        N(330, 30, 112, W_NOISE, 16), N(450, 260, D5, W_SINE, 40),  N(620, 260, E5, W_SINE, 40),
        N(790, 260, G5, W_SINE, 40),  N(960, 260, A5, W_SINE, 40),  N(1130, 600, D6, W_SINE, 36),
        N(1130, 600, A5, W_SINE, 18), N(450, 500, D3, W_TRI, 40),   N(960, 770, A2, W_TRI, 40),
};

// SIGHT: a C major scale run up on a soft piano tone, then the tonic chord
static const tune_note_t SIGHT[] = {
        N(0, 220, C5, W_TRI, 48),   N(100, 220, D5, W_TRI, 48), N(200, 220, E5, W_TRI, 48), N(300, 220, F5, W_TRI, 48),
        N(400, 220, G5, W_TRI, 48), N(500, 220, A5, W_TRI, 48), N(600, 220, B5, W_TRI, 48), N(700, 700, C6, W_TRI, 46),
        N(700, 700, G5, W_TRI, 28), N(700, 700, E5, W_TRI, 28), N(0, 700, C3, W_TRI, 36),   N(700, 700, C3, W_TRI, 36),
};

// SKY: slow twinkles over a deep pad, rising like a night sky that opens
static const tune_note_t SKY[] = {
        N(0, 1400, C3, HOLD(W_TRI), 30),   N(0, 1400, G3, HOLD(W_TRI), 22), N(0, 500, G6, W_SINE, 22),
        N(220, 500, E7, W_SINE, 18),       N(420, 500, C7, W_SINE, 20),     N(640, 500, G6, W_SINE, 18),
        N(860, 600, 98, W_SINE, 18),       N(1080, 700, C7, W_SINE, 22),    N(300, 1100, E4, HOLD(W_SINE), 16),
        N(700, 900, B4, HOLD(W_SINE), 14),
};

// OHMS: a low hum charges up like a capacitor, then three bright notes, like a meter that
// finds the value
static const tune_note_t OHM[] = {
        SL(0, 500, C3, 24, W_SAW, 16),     SL(0, 500, G3, 24, W_TRI, 24),    N(520, 120, E5, W_PULSE, 22),
        N(640, 120, G5, W_PULSE, 22),      N(760, 120, C6, W_PULSE, 22),     N(900, 520, E6, HOLD(W_SINE), 30),
        N(900, 520, C6, HOLD(W_SINE), 20), N(900, 520, C3, HOLD(W_TRI), 45),
};

// ROPE: a circus march: an oom-pah bass, a chromatic climb on top, a short drum roll and a
// cymbal, like the start of a big act
static const tune_note_t ROPE[] = {
        N(0, 110, C3, W_TRI, 55),
        N(150, 90, G3, W_TRI, 40),
        N(300, 110, G2, W_TRI, 55),
        N(450, 90, G3, W_TRI, 40),
        N(0, 130, E5, W_PULSE, 22),
        N(150, 70, F5, W_PULSE, 20),
        N(225, 70, Gb5, W_PULSE, 20),
        N(300, 130, G5, W_PULSE, 22),
        N(450, 70, E5, W_PULSE, 20),
        N(525, 70, C5, W_PULSE, 20),
        N(600, 40, 100, W_NOISE, 10),
        N(650, 40, 100, W_NOISE, 11),
        N(700, 40, 100, W_NOISE, 12),
        N(750, 40, 100, W_NOISE, 13),
        N(800, 40, 100, W_NOISE, 14),
        N(850, 40, 100, W_NOISE, 15),
        N(600, 300, G4, HOLD(W_PULSE), 14),
        N(600, 300, B4, HOLD(W_PULSE), 12),
        N(920, 520, C6, HOLD(W_PULSE), 20),
        N(920, 520, G5, HOLD(W_SQUARE), 10),
        N(920, 520, C3, HOLD(W_TRI), 55),
        KICK(920),
        CRASH(920),
};

// FIGHT: an arcade sting: two low hits, a rising pulse line, and a crash on the last note
static const tune_note_t FIGHT[] = {
        {0, 120, 40, 0, W_SQUARE, 60},
        {0, 60, 100, 0, W_NOISE, 30},
        {180, 120, 40, 0, W_SQUARE, 60},
        {180, 60, 100, 0, W_NOISE, 30},
        {360, 110, 64, 0, W_PULSE, 30},
        {470, 110, 67, 0, W_PULSE, 30},
        {580, 110, 71, 0, W_PULSE, 30},
        {690, 500, 76, 0, W_PULSE | TUNE_HOLD, 32},
        {690, 500, 52, 0, W_SAW | TUNE_HOLD, 40},
        {690, 300, 90, 0, W_NOISE, 25},
};

// CRUISE: a lazy warm arpeggio over a soft bass (Amaj7, F#m7, Dmaj7), like the first minute of a
// slow sunset
static const tune_note_t CRUISE[] = {
        {0, 900, 45, 0, W_SINE | TUNE_HOLD, 40},
        {0, 220, 64, 0, W_TRI, 45},
        {200, 220, 68, 0, W_TRI, 45},
        {400, 220, 71, 0, W_TRI, 45},
        {600, 500, 75, 0, W_TRI, 50},
        {900, 900, 42, 0, W_SINE | TUNE_HOLD, 40},
        {900, 220, 61, 0, W_TRI, 45},
        {1100, 220, 64, 0, W_TRI, 45},
        {1300, 220, 68, 0, W_TRI, 45},
        {1500, 700, 73, -1, W_TRI, 48},
        {1800, 1100, 50, 0, W_SINE | TUNE_HOLD, 40},
        {1800, 260, 66, 0, W_TRI, 42},
        {2050, 260, 69, 0, W_TRI, 42},
        {2300, 900, 73, 0, W_TRI | TUNE_HOLD, 44},
        {2300, 900, 76, 0, W_SINE | TUNE_HOLD, 30},
};

// QUEST: a quiet, curious figure on a soft triangle wave over a low hum (the old water tank)
static const tune_note_t QUEST[] = {
        {0, 1400, 45, 0, W_SINE | TUNE_HOLD, 26},
        {0, 260, 69, 0, W_TRI, 34},
        {280, 260, 72, 0, W_TRI, 34},
        {560, 260, 76, 0, W_TRI, 34},
        {840, 200, 74, 0, W_TRI, 30},
        {1060, 640, 79, 0, W_TRI | TUNE_HOLD, 32},
        {1060, 640, 64, 0, W_SINE | TUNE_HOLD, 22},
};

// FIGHT: the short pieces. GO: two brass stabs, then a big chord ("FIGHT!"). KO: a low hit that falls.
// WIN: a short fanfare up to a major chord. LOSE: four slow notes down, a minor end.
static const tune_note_t FIGHT_GO[] = {
        {0, 110, 57, 0, W_SAW, 48},
        {0, 110, 64, 0, W_SAW, 40},
        {0, 70, 70, 0, W_NOISE, 28},
        {160, 110, 59, 0, W_SAW, 48},
        {160, 110, 66, 0, W_SAW, 40},
        {160, 70, 70, 0, W_NOISE, 28},
        {320, 620, 57, 0, W_SAW | TUNE_HOLD, 52},
        {320, 620, 64, 0, W_SAW | TUNE_HOLD, 44},
        {320, 620, 69, 0, W_PULSE | TUNE_HOLD, 40},
        {320, 520, 76, 0, W_PULSE | TUNE_HOLD, 30},
        {320, 300, 96, 0, W_NOISE, 34},
};
static const tune_note_t FIGHT_KO[] = {
        {0, 90, 30, 0, W_NOISE, 64},
        {0, 700, 45, -14, W_SAW, 60},
        {0, 700, 52, -14, W_SQUARE, 40},
        {120, 500, 84, 0, W_NOISE, 30},
        {420, 600, 33, 0, W_SINE | TUNE_HOLD, 50},
};
static const tune_note_t FIGHT_WIN[] = {
        {0, 110, 67, 0, W_PULSE, 46},
        {120, 110, 72, 0, W_PULSE, 46},
        {240, 110, 76, 0, W_PULSE, 46},
        {360, 700, 79, 0, W_PULSE | TUNE_HOLD, 48},
        {360, 700, 72, 0, W_SAW | TUNE_HOLD, 36},
        {360, 700, 64, 0, W_SAW | TUNE_HOLD, 34},
        {360, 700, 48, 0, W_TRI | TUNE_HOLD, 50},
};
static const tune_note_t FIGHT_LOSE[] = {
        {0, 280, 64, 0, W_TRI, 46},
        {300, 280, 63, 0, W_TRI, 44},
        {600, 280, 62, 0, W_TRI, 42},
        {900, 900, 59, -1, W_TRI | TUNE_HOLD, 46},
        {900, 900, 47, 0, W_SAW | TUNE_HOLD, 32},
        {900, 900, 55, 0, W_SINE | TUNE_HOLD, 30},
};

// WRONG ROOM: a soft lo-fi chord on an electric piano, then the two notes of a lift that does not come
static const tune_note_t WRONG[] = {
        N(0, 700, A3, HOLD(W_SINE), 22),   N(0, 700, C4, HOLD(W_TRI), 18), N(0, 700, E4, HOLD(W_TRI), 18),
        N(0, 700, G4, HOLD(W_TRI), 16),    N(380, 160, B4, W_TRI, 20),     N(560, 500, E5, HOLD(W_SINE), 22),
        N(900, 600, C5, HOLD(W_SINE), 18),
};

// FIGHT, the VS screen: two low hits, a stab that rises, a crash (the VHS fight)
static const tune_note_t FIGHT_VS[] = {
        KICK(0),
        N(0, 140, E2, W_SAW, 26),
        KICK(220),
        N(220, 140, E2, W_SAW, 26),
        SL(420, 260, E3, 12, W_SQUARE, 18),
        SL(420, 260, B3, 12, W_SAW, 14),
        CRASH(680),
        N(680, 420, E4, HOLD(W_SQUARE), 16),
};

// PIXEL, a game starts: the chirp of an old picture tube that comes on, then an 8-bit arpeggio
static const tune_note_t PIXEL_GO[] = {
        SL(0, 120, C4, 24, W_PULSE, 12), N(130, 60, C5, W_SQUARE, 18),        N(190, 60, E5, W_SQUARE, 18),
        N(250, 60, G5, W_SQUARE, 18),    N(310, 260, C6, HOLD(W_SQUARE), 16), N(310, 260, C4, HOLD(W_TRI), 22),
};

// WRONG ROOM, a level starts: the ding-dong of the office lift, a soft chord under it
static const tune_note_t WRONG_GO[] = {
        N(0, 520, E5, HOLD(W_SINE), 24),
        N(330, 700, C5, HOLD(W_SINE), 24),
        N(0, 900, A3, HOLD(W_TRI), 12),
        N(0, 900, E4, HOLD(W_TRI), 10),
};

// GRAINS: a crystal arpeggio that falls like grains, then a soft low note
static const tune_note_t GRAINS[] = {
        N(0, 120, E6, W_TRI, 26),         N(90, 120, B5, W_TRI, 26),         N(180, 120, G5, W_TRI, 26),
        N(270, 120, E5, W_TRI, 28),       N(360, 120, B4, W_TRI, 28),        N(450, 120, G4, W_TRI, 30),
        N(540, 600, E4, HOLD(W_TRI), 30), N(560, 580, E3, HOLD(W_SINE), 26), N(640, 100, B5, W_TRI, 16),
        N(760, 100, E6, W_TRI, 14),
};

#define TUNE(a) {a, (int)(sizeof(a) / sizeof(a[0]))}

static const struct {
    const tune_note_t *notes;
    int n;
} TUNES[TUNE_COUNT] = {
        [TUNE_RUN] = TUNE(RUN),
        [TUNE_HOP] = TUNE(HOP),
        [TUNE_KNIGHT] = TUNE(KNIGHT),
        [TUNE_DRIVE] = TUNE(DRIVE),
        [TUNE_PONG] = TUNE(PONG),
        [TUNE_STACK] = TUNE(STACK),
        [TUNE_PRISM] = TUNE(PRISM),
        [TUNE_TILT] = TUNE(TILT),
        [TUNE_SPLAT] = TUNE(SPLAT),
        [TUNE_SHAKE] = TUNE(SHAKE),
        [TUNE_RALLY] = TUNE(RALLY),
        [TUNE_ZAP] = TUNE(ZAP),
        [TUNE_ECHO] = TUNE(ECHO),
        [TUNE_WOBBLE] = TUNE(WOBBLE),
        [TUNE_KABOOM] = TUNE(KABOOM),
        [TUNE_WHACK] = TUNE(WHACK),
        [TUNE_TILES] = TUNE(TILES),
        [TUNE_MERGE] = TUNE(MERGE),
        [TUNE_BUBBLE] = TUNE(BUBBLE),
        [TUNE_BURGER] = TUNE(BURGER),
        [TUNE_PINBALL] = TUNE(PINBALL),
        [TUNE_SAFE] = TUNE(SAFE),
        [TUNE_POUR] = TUNE(POUR),
        [TUNE_EGG] = TUNE(EGG),
        [TUNE_MORSE] = TUNE(MORSE),
        [TUNE_BRASS] = TUNE(BRASS),
        [TUNE_TENNIS] = TUNE(TENNIS),
        [TUNE_BITS] = TUNE(BITS),
        [TUNE_SIGHT] = TUNE(SIGHT),
        [TUNE_SKY] = TUNE(SKY),
        [TUNE_OHM] = TUNE(OHM),
        [TUNE_ROPE] = TUNE(ROPE),
        [TUNE_CRUISE] = TUNE(CRUISE),
        [TUNE_FIGHT] = TUNE(FIGHT),
        [TUNE_QUEST] = TUNE(QUEST),
        [TUNE_FIGHT_GO] = TUNE(FIGHT_GO),
        [TUNE_FIGHT_KO] = TUNE(FIGHT_KO),
        [TUNE_FIGHT_WIN] = TUNE(FIGHT_WIN),
        [TUNE_FIGHT_LOSE] = TUNE(FIGHT_LOSE),
        [TUNE_GRAINS] = TUNE(GRAINS),
        [TUNE_WRONG] = TUNE(WRONG),
        [TUNE_FIGHT_VS] = TUNE(FIGHT_VS),
        [TUNE_PIXEL_GO] = TUNE(PIXEL_GO),
        [TUNE_WRONG_GO] = TUNE(WRONG_GO),
};

void tune_play(tune_t t)
{
    if ((unsigned)t < TUNE_COUNT) {
        audio_tune(TUNES[t].notes, TUNES[t].n);
    }
}

int tune_ms(tune_t t)
{
    if ((unsigned)t >= TUNE_COUNT) {
        return 0;
    }
    int end = 0;
    for (int i = 0; i < TUNES[t].n; i++) {
        int e = TUNES[t].notes[i].t + TUNES[t].notes[i].dur;
        if (e > end) {
            end = e;
        }
    }
    return end;
}
