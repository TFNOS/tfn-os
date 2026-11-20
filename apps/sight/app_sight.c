// SIGHT (UTIL): learn to read music and train the ear, on the landscape screen (640 x 172 on the 3.49,
// 320 x 240 on the CoreS3).
// The task: the app task of TFN OS calls the hooks of app_sight; the audio task reads the note buffers of the
// tune player.
// The memory: the file statics; the two microphone buffers (PSRAM) come when the microphone starts and go
// when it stops (mic_free).
//
// - LEARN: a note shows on a staff, and you answer on a piano keyboard (or on note-name
//   buttons). It starts with 3 notes of the treble clef and adds one when 18 of the last 20
//   answers are correct: the treble staff and its ledger lines, the bass clef, sharps and
//   flats, then key signatures (52 steps). Each new note comes with a card.
// - SPEED: notes move along the staff to a play line; answer each one before it gets there.
//   10 levels (treble, bass, ledger lines, accidentals, keys) in a loop that gets faster.
// - EAR: the board plays an interval, a chord, or a note after a key chord, and you name it.
//   Each part adds a new answer when 18 of the last 20 are correct.
// - SING: the board shows a note; sing it or play it on an instrument. The microphone finds
//   the pitch (YIN), a needle shows the cents, and the note counts when it stays in tune for
//   0.5 s. A trace shows your pitch on the staff.
// - TUNER: the pitch of the microphone, with the note on the staff.
// - CHART: the notes of both clefs; tap a note or a key to hear it.
//
// Sound: all notes use the tune player (audio_tune), so their timing is exact at any frame
// rate. The answers use the time of the touch (IN_DOWN), not a tap after the finger goes up.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/mfont.h"
#include "tfn/uikit.h"
#include "tfn/settings.h"
#include "tfn/tunes.h"

// ---------------------------------------------------------------- layout

typedef struct {
    int16_t x, y, w, h;
} rc_t;

#define RC(x, y, w, h) ((rc_t){(int16_t)(x), (int16_t)(y), (int16_t)(w), (int16_t)(h)})

#if BOARD_BAR_SCREEN
// The thumb: the top bar is 40 px (3.49) / 42 px (the 320 frame: 4.6 mm on the AMOLED); the
// keys are wider (the black keys take 40 px or more with their margin); on the Watcher zoom (only a circle of 160 px
// round the middle shows) the top bar buttons and the keys stay in the circle.
#define SW          640
#define SH          172
#define TOP_H       40
#define TOP_DY      5  // the header text: lower by this
#define BACK_RC     RC(0, 0, 64, 40)
#define TB1_RC      RC(546, 0, 94, 40)  // the buttons of the top bar, from the right
#define TB2_RC      RC(448, 0, 94, 40)
#define TB3_RC      RC(350, 0, 94, 40)
#define STAFF_RC    RC(0, 40, 310, 132)    // the staff (paper)
#define KEYS_RC     RC(310, 40, 330, 132)  // the piano or the note names
#define STEP        7                      // px: one step of the staff (half a space)
#define Y0          131                    // the bottom line of the staff
#define CLEF_X      22                     // the clef anchor
#define KSIG_X      46                     // the first sign of a key signature
#define NOTE_X      214                    // LEARN: the note
#define SING_X      110                    // SING: the note to sing
#define PLAY_X      112                    // SPEED: the play line
#define SPAWN_X     292
#define SPEED_GAP   72
#define SPEED_V0    44.0f  // px/s at level 1
#define EAR_INFO_RC RC(0, 40, 310, 132)
#define EAR_GRID_RC RC(310, 40, 330, 132)
#define SING_NAME_Y 46
#define GAUGE_RC    RC(320, 80, 310, 30)
#define HEAR_RC     RC(314, 124, 156, 46)
#define SKIP_RC     RC(482, 124, 156, 46)  // 12 px away from HEAR (SKIP costs 15 s)
#define TOGGLE_RC   RC(466, 4, 168, 44)
#define R_AGAIN_RC  RC(150, 118, 160, 46)
#define R_MENU_RC   RC(330, 118, 160, 46)
#define NB_ACC_H    46
#define CH_X0       50  // CHART: the first column
#define BLACK_PCT   70  // the black keys: % of a white key (the drawing)
#define BLACK_PAD   5   // ... and the margin of the hit zone on each side
#define BLACK_HPCT  58  // the black keys: % of the height
#else
#if BOARD_UI_TABLET
#define MDX 107  // the Tab5: the logical frame is 427 px wide (x 1.5 = 640)
#else
#define MDX 0
#endif
#define SW     (320 + MDX)
#define SH     240
#define TOP_H  42
#define TOP_DY 5
#if BOARD_ROUND
// THE ROUND LAYOUT (the 1.75C 466 x 466, the Watcher 412 x 412: APP_ROUND_HI): a logical square of 320 x 320 (the
// round view puts it on the panel); all in the circle of radius 160 round (160, 160). The bar is the top cap, the
// staff the middle band, the keys a piano with a round bottom (each white key ends at the circle).
#undef SH
#undef TOP_H
#undef TOP_DY
#define SH      320
#define TOP_H   84
#define TOP_DY  36
#define BACK_RC RC(54, 36, 50, 44)
#define TB1_RC  RC(206, 36, 60, 44)
#define TB2_RC  RC(158, 36, 46, 44)
#define TB3_RC  RC(106, 36, 50, 44)
#define KEYS_RC RC(24, 186, 272, 110)
#else
#define BACK_RC RC(0, 0, 60, 42)
#define TB1_RC  RC(232 + MDX, 0, 88, 42)
#define TB2_RC  RC(142 + MDX, 0, 88, 42)
#define TB3_RC  RC(62 + MDX, 0, 78, 42)
#define KEYS_RC RC(0, 132, SW, 108)
#endif
#if BOARD_ROUND
#define STAFF_RC    RC(16, 86, 288, 98)
#define STEP        5
#define Y0          153
#define CLEF_X      34
#define KSIG_X      52
#define NOTE_X      212
#define SING_X      116
#define PLAY_X      104
#define SPAWN_X     290
#define SPEED_GAP   56
#define SPEED_V0    32.0f
#define EAR_INFO_RC RC(20, 86, 280, 52)
#define EAR_GRID_RC RC(20, 142, 280, 132)
#define SING_NAME_Y 190
#define GAUGE_RC    RC(124, 192, 166, 26)
#define HEAR_RC     RC(74, 240, 84, 46)
#define SKIP_RC     RC(162, 240, 84, 46)  // (SKIP costs 15 s)
#define TOGGLE_RC   RC(86, 258, 148, 44)
#define R_AGAIN_RC  RC(46, 222, 110, 48)
#define R_MENU_RC   RC(164, 222, 110, 48)
#define NB_ACC_H    42
#define CH_X0       58
#define BLACK_PCT   70
#define BLACK_PAD   6
#define BLACK_HPCT  52
#define HDR_X       108
#define RS_Y        40  // the result page: down from the 240 frame
#else
#define STAFF_RC    RC(0, 42, SW, 90)
#define STEP        5
#define Y0          107
#define CLEF_X      18
#define KSIG_X      36
#define NOTE_X      (204 + MDX / 2)
#define SING_X      100
#define PLAY_X      96
#define SPAWN_X     (296 + MDX)
#define SPEED_GAP   56
#define SPEED_V0    32.0f
#define EAR_INFO_RC RC(0, 42, SW, 52)
#define EAR_GRID_RC RC(0, 94, SW, 146)
#define SING_NAME_Y 150
#define GAUGE_RC    RC(104, 140, 210 + MDX, 30)
#define HEAR_RC     RC(104, 186, 100 + MDX / 2, 52)
#define SKIP_RC     RC(216 + MDX / 2, 186, 100 + MDX / 2, 52)  // 12 px away from HEAR (SKIP costs 15 s)
#define TOGGLE_RC   RC(196 + MDX, 4, 120, 44)
#define R_AGAIN_RC  RC(18, 180, 136 + MDX / 2, 52)
#define R_MENU_RC   RC(166 + MDX / 2, 180, 136 + MDX / 2, 52)
#define NB_ACC_H    40
#define CH_X0       42
#define BLACK_PCT   67
#define BLACK_PAD   6
#define BLACK_HPCT  55
#define HDR_X       140
#define RS_Y        0
#endif
#endif

#define SP      (2 * STEP)  // px: one space of the staff
#define N_MODES 6

#if BOARD_ROUND
// the round view: the logical square of 320 x 320 on the panel at k (see THE ROUND VIEW)
static float rv_k(void)
{
    return (float)gfx_w / 320;
}
// the half width of the circle of the square at the row y
static int rchord(int y)
{
    int dy = y - 160;
    return dy * dy >= 160 * 160 ? 0 : (int)sqrtf((float)(160 * 160 - dy * dy));
}
#endif

static bool in_rc(rc_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// THE PRESS (for the thumb): an answer is not taken where the finger first touches (the edge of the finger), but
// where it comes off the glass. While the finger is down, the key under it lights and its name shows above the finger,
// so a person can slide to the right key, or slide off the keys to take no answer. SPEED keeps the time of the press:
// the note in front at the press is not lost while the finger is down (for 1.5 s at most).
#define PRESS_HOLD_MS 1500
static bool s_press;          // a finger came down in the answer area
static int64_t s_press_t;     // the time of its IN_DOWN
static int s_press_f = -1;    // SPEED: the note in front at the press
static int s_aim = -1;        // the answer key under the finger now (answer_key), -1 = none
static int s_aim_x, s_aim_y;  // the point of the finger now
static int s_aim_btn = -1;    // EAR: the answer button under the finger now

static rc_t ear_menu_btn(int i)
{
#if BOARD_BAR_SCREEN
    return RC(6 + i * 211, 84, 205, 82);
#elif BOARD_ROUND
    int y0 = 104 + i * 58, y1 = y0 + 52, a = rchord(y0), b = rchord(y1), half = (a < b ? a : b) - 8;
    return RC(160 - half, y0, 2 * half, y1 - y0);  // (each row follows the circle)
#else
    return RC(8, 76 + i * 55, 304 + MDX, 50);
#endif
}

static rc_t title_btn(int i)
{
#if BOARD_BAR_SCREEN
    return RC(6 + i * 106, 104, 100, 62);
#elif BOARD_ROUND
    return RC(32 + (i % 2) * 132, 84 + (i / 2) * 58, 124, 52);
#else
    return RC(8 + (i % 2) * (156 + MDX / 2), 84 + (i / 2) * 52, 148 + MDX / 2, 50);
#endif
}

// ---------------------------------------------------------------- colours

#define C_BG      rgb(12, 16, 28)
#define C_PANEL   rgb(24, 30, 50)
#define C_PANEL2  rgb(36, 44, 72)
#define C_LINE    rgb(64, 76, 116)
#define C_AMBER   rgb(255, 184, 64)
#define C_CYAN    rgb(90, 220, 255)
#define C_GREEN   rgb(100, 232, 120)
#define C_RED     rgb(255, 90, 90)
#define C_WHITE   rgb(235, 240, 255)
#define C_DIM     rgb(130, 140, 172)
#define C_DARK    rgb(70, 78, 104)
#define C_VIOLET  rgb(190, 140, 255)
#define C_PAPER   rgb(244, 236, 214)
#define C_PAPER2  rgb(222, 210, 180)
#define C_INK     rgb(30, 26, 34)
#define C_INK_OK  rgb(20, 150, 60)
#define C_INK_BAD rgb(214, 40, 40)
#define C_INK_HI  rgb(214, 120, 0)
#define C_KEY_W   rgb(242, 242, 236)
#define C_KEY_B   rgb(26, 26, 32)

// ---------------------------------------------------------------- music

#define TREBLE   0
#define BASS     1
#define ACC_NONE 9  // no sign next to the note (the key gives the accidental)

static const uint8_t WS[7] = {0, 2, 4, 5, 7, 9, 11};
static const char LETTERS[] = "CDEFGAB";
static const char *const PC_NAME[12] = {"C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"};

// A note on the staff: the clef, the diatonic number (letter + 7 x octave, C4 = 28), the
// sign next to it, and the key signature (+ = sharps, - = flats).
typedef struct {
    uint8_t clef;
    int8_t dia;
    int8_t acc;
    int8_t key;
} nq_t;

static int key_acc(int key, int letter)
{
    static const char SHARPS[] = "FCGDAEB", FLATS[] = "BEADGCF";
    char l = LETTERS[letter];
    for (int i = 0; i < key && i < 7; i++) {
        if (SHARPS[i] == l) {
            return 1;
        }
    }
    for (int i = 0; i < -key && i < 7; i++) {
        if (FLATS[i] == l) {
            return -1;
        }
    }
    return 0;
}

static int nq_acc(const nq_t *q)
{
    return q->acc != ACC_NONE ? q->acc : key_acc(q->key, q->dia % 7);
}

static int nq_midi(const nq_t *q)
{
    return 12 * (q->dia / 7 + 1) + WS[q->dia % 7] + nq_acc(q);
}

static int nq_pc(const nq_t *q)
{
    return nq_midi(q) % 12;
}

// the step of the staff: 0 = the bottom line
static int nq_pos(const nq_t *q)
{
    return q->dia - (q->clef == BASS ? 18 : 30);
}

static void nq_name(const nq_t *q, char *out)
{
    int a = nq_acc(q), k = 0;
    out[k++] = LETTERS[q->dia % 7];
    if (a > 0) {
        out[k++] = '#';
    }
    if (a < 0) {
        out[k++] = 'b';
    }
    out[k++] = (char)('0' + q->dia / 7);
    out[k] = 0;
}

// "F#4" -> the diatonic number and the sign
static void parse_note(const char *s, int *dia, int *acc)
{
    int l = (int)(strchr(LETTERS, s[0]) - LETTERS);
    int a = ACC_NONE, i = 1;
    if (s[1] == '#') {
        a = 1, i = 2;
    } else if (s[1] == 'b') {
        a = -1, i = 2;
    }
    *dia = l + 7 * (s[i] - '0');
    *acc = a;
}

static const char *key_name(int key)
{
    static const char *const K[9] = {"Ab", "Eb", "Bb", "F", "C", "G", "D", "A", "E"};
    return K[key + 4];
}

// LEARN: the order in which the notes come
typedef struct {
    uint8_t clef;
    char nm[4];
    int8_t key;
} lent_t;

static const lent_t LSEQ[] = {
        // the treble staff, from the middle line out
        {TREBLE, "B4", 0},
        {TREBLE, "G4", 0},
        {TREBLE, "D5", 0},
        {TREBLE, "E4", 0},
        {TREBLE, "F5", 0},
        {TREBLE, "A4", 0},
        {TREBLE, "C5", 0},
        {TREBLE, "F4", 0},
        {TREBLE, "E5", 0},
        // the treble ledger lines
        {TREBLE, "D4", 0},
        {TREBLE, "G5", 0},
        {TREBLE, "C4", 0},
        {TREBLE, "A5", 0},
        {TREBLE, "B3", 0},
        {TREBLE, "B5", 0},
        {TREBLE, "A3", 0},
        {TREBLE, "C6", 0},
        // the bass staff
        {BASS, "D3", 0},
        {BASS, "B2", 0},
        {BASS, "F3", 0},
        {BASS, "G2", 0},
        {BASS, "A3", 0},
        {BASS, "C3", 0},
        {BASS, "E3", 0},
        {BASS, "A2", 0},
        {BASS, "G3", 0},
        // the bass ledger lines
        {BASS, "F2", 0},
        {BASS, "B3", 0},
        {BASS, "E2", 0},
        {BASS, "C4", 0},
        {BASS, "D2", 0},
        {BASS, "D4", 0},
        {BASS, "C2", 0},
        {BASS, "E4", 0},
        // sharps and flats
        {TREBLE, "F#4", 0},
        {TREBLE, "Bb4", 0},
        {TREBLE, "C#5", 0},
        {TREBLE, "Eb4", 0},
        {TREBLE, "G#4", 0},
        {BASS, "F#3", 0},
        {BASS, "Bb2", 0},
        {BASS, "C#3", 0},
        {BASS, "Eb3", 0},
        {BASS, "Ab2", 0},
        // key signatures: the note that the key changes
        {TREBLE, "F5", 1},
        {TREBLE, "B4", -1},
        {TREBLE, "C5", 2},
        {BASS, "E3", -2},
        {TREBLE, "G4", 3},
        {BASS, "A2", -3},
        {TREBLE, "D5", 4},
        {TREBLE, "D5", -4},
};
#define N_LSEQ      ((int)(sizeof(LSEQ) / sizeof(LSEQ[0])))
#define LEARN_START 3

static nq_t lent_q(int i)
{
    int dia, acc;
    parse_note(LSEQ[i].nm, &dia, &acc);
    nq_t q = {LSEQ[i].clef, (int8_t)dia, (int8_t)acc, LSEQ[i].key};
    return q;
}

// SPEED: the levels (in a loop that gets faster)
typedef struct {
    uint8_t clef;
    int8_t lo, hi;    // diatonic numbers
    uint8_t acc_pct;  // % of the notes with a sharp or a flat
    int8_t keys[4];
    uint8_t nkeys;
    const char *name;
} slevel_t;

static const slevel_t SLV[] = {
        {TREBLE, 30, 38, 0, {0}, 0, "TREBLE STAFF"},
        {TREBLE, 26, 42, 0, {0}, 0, "TREBLE + LEDGER LINES"},
        {BASS, 18, 26, 0, {0}, 0, "BASS STAFF"},
        {BASS, 14, 30, 0, {0}, 0, "BASS + LEDGER LINES"},
        {TREBLE, 30, 38, 35, {0}, 0, "TREBLE: SHARPS + FLATS"},
        {BASS, 18, 26, 35, {0}, 0, "BASS: SHARPS + FLATS"},
        {TREBLE, 28, 40, 0, {1, -1, 2, -2}, 4, "TREBLE: KEYS"},
        {BASS, 16, 28, 0, {1, -1, 2, -2}, 4, "BASS: KEYS"},
        {TREBLE, 26, 42, 0, {3, -3, 4, -4}, 4, "TREBLE: MORE KEYS"},
        {BASS, 14, 30, 0, {3, -3, 4, -4}, 4, "BASS: MORE KEYS"},
};
#define N_SLV        ((int)(sizeof(SLV) / sizeof(SLV[0])))
#define SPEED_PER_LV 12
#define LIVES        3

// EAR
static const char *const INAME[12] = {"MIN 2", "MAJ 2", "MIN 3", "MAJ 3", "4TH",   "TRITONE",
                                      "5TH",   "MIN 6", "MAJ 6", "MIN 7", "MAJ 7", "OCTAVE"};
static const uint8_t IORDER[12] = {7, 4, 12, 3, 5, 2, 8, 9, 1, 10, 11, 6};  // semitones
typedef struct {
    const char *name;
    int8_t n;
    int8_t off[4];
} chord_t;
static const chord_t CHORDS[7] = {
        {"MAJOR", 3, {0, 4, 7}},     {"MINOR", 3, {0, 3, 7}},     {"DIM", 3, {0, 3, 6}},       {"AUG", 3, {0, 4, 8}},
        {"DOM 7", 4, {0, 4, 7, 10}}, {"MAJ 7", 4, {0, 4, 7, 11}}, {"MIN 7", 4, {0, 3, 7, 10}},
};
static const uint8_t NORDER[12] = {0, 7, 4, 5, 2, 9, 11, 6, 10, 3, 8, 1};  // pitch classes
static const char *const ENAME[3] = {"INTERVALS", "CHORDS", "NOTES"};
static const uint8_t ETOTAL[3] = {12, 7, 12};

static uint32_t rnd(uint32_t n)
{
    return n ? esp_random() % n : 0;
}

static float frand(float a, float b)
{
    return a + (b - a) * (float)(esp_random() & 0xFFFF) / 65535.0f;
}

// ---------------------------------------------------------------- state

typedef enum {
    ST_TITLE,
    ST_LEARN,
    ST_SPEED,
    ST_EARMENU,
    ST_EAR,
    ST_SING,
    ST_TUNER,
    ST_CHART,
    ST_RESULT
} sstate_t;

static sstate_t s_state, s_result_of;
static int64_t s_now, s_prev;
static int s_input;  // 0 = the piano, 1 = the note names (NVS "sig_in")
static int s_latch;  // the note names: -1 = flat, +1 = sharp, 0 = none

// the 20 last answers (LEARN and EAR)
typedef struct {
    uint8_t h[20];
    uint8_t n, i;
} hist_t;

static void hist_push(hist_t *h, bool ok)
{
    h->h[h->i] = ok;
    h->i = (uint8_t)((h->i + 1) % 20);
    if (h->n < 20) {
        h->n++;
    }
}

static int hist_ok(const hist_t *h)
{
    int s = 0;
    for (int i = 0; i < h->n; i++) {
        s += h->h[i];
    }
    return s;
}

static bool hist_ready(const hist_t *h)
{
    return h->n >= 20 && hist_ok(h) >= 18;
}

// the marks on the piano: one for each pitch class
enum {
    MK_NONE,
    MK_OK,
    MK_BAD,
    MK_SHOW,
    MK_OFF,
    MK_LIT
};
static uint8_t s_mark[12];

// a banner over the staff
static char s_banner[32], s_banner2[44];
static int64_t s_banner_until;
static uint16_t s_banner_col;

static void banner(const char *a, const char *b, uint16_t col, int ms)
{
    snprintf(s_banner, sizeof(s_banner), "%s", a);
    snprintf(s_banner2, sizeof(s_banner2), "%s", b ? b : "");
    s_banner_col = col;
    s_banner_until = s_now + ms;
}

// sparks
#define N_PART 40
typedef struct {
    float x, y, vx, vy, life;
    uint16_t c;
} part_t;
static part_t s_part[N_PART];

static void sparks(int x, int y, int n, uint16_t c)
{
    for (int i = 0; i < N_PART && n > 0; i++) {
        part_t *p = &s_part[i];
        if (p->life > 0) {
            continue;
        }
        float a = frand(0, 6.2832f), v = frand(40, 150);
        p->x = (float)x;
        p->y = (float)y;
        p->vx = fcos(a) * v;
        p->vy = fsin(a) * v - 50;
        p->life = frand(0.3f, 0.6f);
        p->c = c;
        n--;
    }
}

// ---------------------------------------------------------------- sound

// A small ring of note buffers: the tune player reads the notes while they play, so a new
// tune must not write into the buffer of a tune that may still play.
#define SND_MAX  24
#define SND_RING 4
static tune_note_t s_snd[SND_RING][SND_MAX];
static int s_snd_sel, s_snd_n;
static int s_snd_end_ms;     // the end of the tune now (ms from its start)
static int64_t s_snd_until;  // the sound of the last tune ends at this time

static void snd_begin(void)
{
    s_snd_sel = (s_snd_sel + 1) % SND_RING;
    s_snd_n = 0;
    s_snd_end_ms = 0;
}

static void snd_raw(int t, int dur, int midi, int wave, int vol)
{
    if (s_snd_n >= SND_MAX || midi < 12 || midi > 120 || dur <= 0) {
        return;
    }
    s_snd[s_snd_sel][s_snd_n++] =
            (tune_note_t){(uint16_t)t, (uint16_t)dur, (uint8_t)midi, 0, (uint8_t)wave, (uint8_t)vol};
    if (t + dur > s_snd_end_ms) {
        s_snd_end_ms = t + dur;
    }
}

// a piano-like note: a triangle with a soft octave over it, both with a decay
static void snd_note(int t, int dur, int midi, int vol)
{
    snd_raw(t, dur, midi, W_TRI, vol);
    snd_raw(t, dur * 6 / 10, midi + 12, W_SINE, vol / 4);
}

static void snd_go(void)
{
    if (s_snd_n) {
        audio_tune(s_snd[s_snd_sel], s_snd_n);
        s_snd_until = s_now + s_snd_end_ms;
    }
}

static void play_midi(int midi, int dur)
{
    snd_begin();
    snd_note(0, dur, midi, 60);
    snd_go();
}

static void buzz(void)
{
    sfx_tone(220, 160, 160, W_SQUARE, 18);
}

// ---------------------------------------------------------------- the pitch detector (YIN)

// The microphone gives 24 kHz mono samples. A buffer of PD_N samples (53 ms) gives one
// pitch: the YIN difference function over a window of PD_W samples, for the periods from
// PD_TMIN (1333 Hz) to PD_TMAX (60 Hz), then a parabola through the three points at the
// minimum.
#define PD_N       1280
#define PD_W       800
#define PD_TMIN    18
#define PD_TMAX    420
#define PD_THRESH  0.12f
#define PD_RMS_MIN 0.004f

static float s_pd_x[PD_N];
static float s_pd_raw[PD_TMAX + 2];
static float s_pd_cm[PD_TMAX + 2];

// the difference function at one period
static float pd_diff(int t)
{
    const float *a = s_pd_x, *b = s_pd_x + t;
    float d = 0;
    for (int j = 0; j < PD_W; j++) {
        float df = a[j] - b[j];
        d += df * df;
    }
    return d;
}

// the pitch in Hz (0 = no pitch); *rms = the level (0..1 of full scale)
static float pd_detect(const int16_t *in, int n, float sr, float *rms)
{
    *rms = 0;
    if (n < PD_W + PD_TMAX + 2) {
        return 0;
    }
    float mean = 0;
    for (int i = 0; i < n; i++) {
        mean += in[i];
    }
    mean /= (float)n;
    float e = 0;
    for (int i = 0; i < n; i++) {
        float v = ((float)in[i] - mean) * (1.0f / 32768.0f);
        s_pd_x[i] = v;
        e += v * v;
    }
    *rms = sqrtf(e / (float)n);
    if (*rms < PD_RMS_MIN) {
        return 0;
    }

    // the difference function and its cumulative mean normalized form
    float run = 0;
    int tau = -1, dip0 = -1;
    s_pd_cm[0] = 1;
    for (int t = 1; t <= PD_TMAX + 1; t++) {
        float d = pd_diff(t);
        s_pd_raw[t] = d;
        run += d;
        s_pd_cm[t] = run > 0 ? d * (float)t / run : 1;
        // the first dip under the limit: its lowest point (noise makes small ripples in the
        // dip, so the first local minimum is not always the true one)
        if (t >= PD_TMIN && t <= PD_TMAX && s_pd_cm[t] < PD_THRESH) {
            if (dip0 < 0) {
                dip0 = t;
            }
            if (tau < 0 || s_pd_cm[t] < s_pd_cm[tau]) {
                tau = t;
            }
        }
        if (dip0 >= 0 && (s_pd_cm[t] >= PD_THRESH || t > dip0 + dip0 / 4 + 2)) {
            break;
        }
    }
    if (tau >= PD_TMAX) {
        tau = -1;
    }
    if (tau < 0) {
        int m = PD_TMIN;
        for (int t = PD_TMIN; t <= PD_TMAX; t++) {
            if (s_pd_cm[t] < s_pd_cm[m]) {
                m = t;
            }
        }
        if (s_pd_cm[m] > 0.3f) {
            return 0;
        }
        tau = m;
    }
    if (tau < 2 || tau > PD_TMAX) {
        return 0;
    }
    // A parabola fitted (least squares) through the raw difference at the minimum. A long
    // period has a wide, flat minimum: more points (tau / 16 on each side) average the noise.
    int hw = tau / 16;
    if (hw < 1) {
        hw = 1;
    }
    if (hw > 12) {
        hw = 12;
    }
    if (tau - hw < 1) {
        hw = tau - 1;
    }
    float sy = 0, suy = 0, su2y = 0, su2 = 0, su4 = 0;
    int npt = 2 * hw + 1;
    for (int u = -hw; u <= hw; u++) {
        float y = pd_diff(tau + u);
        float uu = (float)(u * u);
        sy += y;
        suy += (float)u * y;
        su2y += uu * y;
        su2 += uu;
        su4 += uu * uu;
    }
    float m2 = su2 / (float)npt;
    float b1 = suy / su2;
    float c2 = (su2y - m2 * sy) / (su4 - m2 * su2);
    float sh = 0;
    if (c2 > 1e-12f) {
        sh = -b1 / (2 * c2);
    }
    if (sh > (float)hw) {
        sh = (float)hw;
    }
    if (sh < -(float)hw) {
        sh = -(float)hw;
    }
    float per = (float)tau + sh;
    // A finer period: the minimum near k periods (the largest k in the buffer), divided by k.
    // A short period has an error of a part of a sample (noise); over k periods it is k times less.
    int k = (int)((float)(PD_TMAX - 2) / per);
    if (k >= 2) {
        int t0 = (int)lroundf(per * (float)k);
        int best = t0;
        float d[5];
        for (int i = 0; i < 5; i++) {
            d[i] = pd_diff(t0 - 2 + i);
        }
        for (int i = 1; i < 4; i++) {
            if (d[i] < d[best - t0 + 2]) {
                best = t0 - 2 + i;
            }
        }
        int bi = best - t0 + 2;
        if (bi >= 1 && bi <= 3) {
            float a2 = d[bi - 1], b2 = d[bi], c2 = d[bi + 1];
            float den2 = a2 - 2 * b2 + c2, sh2 = 0;
            if (den2 > 1e-12f) {
                sh2 = 0.5f * (a2 - c2) / den2;
            }
            if (sh2 > 1) {
                sh2 = 1;
            }
            if (sh2 < -1) {
                sh2 = -1;
            }
            float fine = ((float)best + sh2) / (float)k;
            if (fabsf(fine - per) < 0.5f) {
                per = fine;
            }
        }
    }
    return sr / per;
}

static float hz_to_midi(float hz)
{
    return 69.0f + 12.0f * log2f(hz / 440.0f);
}

// the microphone: two buffers in turn
static int16_t *s_mb[2];
static int s_mcur;
static bool s_mic_ok, s_mic_tried;
static int64_t s_mute_until;  // no pitch while the board plays a note
static bool s_pitch_ok;       // a pitch now
static float s_pitch_midi;    // the pitch now (a fractional MIDI note)
static float s_mf[3];
static int s_mf_n;
static float s_level;
#define N_TRACE 64
static float s_trace[N_TRACE];  // the pitch (fractional MIDI) of the last buffers, 0 = none
static int s_trace_i;

static void mic_open(void)
{
    s_mic_tried = true;
    for (int i = 0; i < 2; i++) {
        if (!s_mb[i]) {
            s_mb[i] = heap_caps_malloc(PD_N * sizeof(int16_t), MALLOC_CAP_SPIRAM);
        }
    }
    s_mic_ok = s_mb[0] && s_mb[1] && audio_mic_start();
    s_mcur = 0;
    s_pitch_ok = false;
    s_mf_n = 0;
    memset(s_trace, 0, sizeof(s_trace));
    if (s_mic_ok) {
        audio_mic_record(s_mb[0], PD_N);
    }
}

static void mic_close(void)
{
    if (s_mic_ok) {
        audio_mic_record_stop();
        audio_mic_stop();
    }
    s_mic_ok = false;
    s_pitch_ok = false;
}

// The two buffers (5 KB of PSRAM) stay: the microphone task can still write the block that
// it started when the recording stops, so a free here is not safe.
static void mic_free(void)
{
    mic_close();
}

static float median3(float a, float b, float c)
{
    if (a > b) {
        float t = a;
        a = b;
        b = t;
    }
    if (b > c) {
        float t = b;
        b = c;
        c = t;
    }
    if (a > b) {
        float t = a;
        a = b;
        b = t;
    }
    return b;
}

static void mic_poll(void)
{
    if (!s_mic_ok || audio_mic_recording()) {
        return;
    }
    if (audio_mic_recorded() < PD_N) {
        audio_mic_record(s_mb[s_mcur], PD_N);  // it stopped early: again
        return;
    }
    int done = s_mcur;
    s_mcur ^= 1;
    audio_mic_record(s_mb[s_mcur], PD_N);
    float hz = 0;
    if (s_now >= s_mute_until) {
        hz = pd_detect(s_mb[done], PD_N, (float)AUDIO_SAMPLE_RATE, &s_level);
    }
    float m = hz > 0 ? hz_to_midi(hz) : 0;
    if (m > 20 && m < 110) {
        // a median of the last three, so that one wrong octave does not jump
        s_mf[s_mf_n % 3] = m;
        s_mf_n++;
        float v = m;
        if (s_mf_n >= 3) {
            v = median3(s_mf[0], s_mf[1], s_mf[2]);
        }
        s_pitch_midi = v;
        s_pitch_ok = true;
    } else {
        s_pitch_ok = false;
        s_mf_n = 0;
    }
    s_trace[s_trace_i] = s_pitch_ok ? s_pitch_midi : 0;
    s_trace_i = (s_trace_i + 1) % N_TRACE;
}

// ---------------------------------------------------------------- LEARN

static int s_ln = LEARN_START;  // notes in use (NVS "sig_ln")
static int s_lseen = 0;         // notes with a card (NVS "sig_lseen")
static hist_t s_lh;
static nq_t s_q;     // the note now
static int s_q_ent;  // its entry in LSEQ
static int s_fb;     // 0 = waits for an answer, 1 = correct, 2 = wrong
static int s_fb_pc;
static int64_t s_fb_until;
static bool s_intro;  // the card of a new note
static int s_intro_i;
static int64_t s_intro_at;
static int s_streak, s_best_streak, s_answers, s_correct;

static void learn_show_intro(int i)
{
    s_intro = true;
    s_intro_i = i;
    s_intro_at = s_now;
    s_q = lent_q(i);
    s_q_ent = i;
    s_fb = 0;
    memset(s_mark, 0, sizeof(s_mark));
    s_mark[nq_pc(&s_q)] = MK_SHOW;
    play_midi(nq_midi(&s_q), 900);
}

static void learn_next(void)
{
    s_fb = 0;
    memset(s_mark, 0, sizeof(s_mark));
    int pick;
    int tries = 0;
    do {
        // the newest notes come more often
        if (s_ln > LEARN_START && rnd(100) < 35) {
            pick = s_ln - 1 - (int)rnd(2);
        } else {
            pick = (int)rnd((uint32_t)s_ln);
        }
        tries++;
    } while (pick == s_q_ent && tries < 8);
    s_q_ent = pick;
    s_q = lent_q(pick);
    // after the first key signature: a plain note can show in a key that is known now
    if (LSEQ[pick].key == 0 && s_q.acc == ACC_NONE && rnd(100) < 25) {
        int8_t keys[8];
        int nk = 0;
        for (int i = 0; i < s_ln && nk < 8; i++) {
            if (LSEQ[i].key) {
                keys[nk++] = LSEQ[i].key;
            }
        }
        if (nk) {
            s_q.key = keys[rnd((uint32_t)nk)];
        }
    }
}

static void learn_start(void)
{
    s_state = ST_LEARN;
    s_intro = false;
    s_q_ent = -1;
    s_streak = 0;
    s_answers = s_correct = 0;
    memset(&s_lh, 0, sizeof(s_lh));
    if (s_lseen < s_ln) {
        learn_show_intro(s_lseen);
    } else {
        learn_next();
    }
}

static void learn_intro_done(void)
{
    s_intro = false;
    if (s_intro_i + 1 > s_lseen) {
        s_lseen = s_intro_i + 1;
        store_set("sig_lseen", (uint32_t)s_lseen);
    }
    if (s_lseen < s_ln) {
        learn_show_intro(s_lseen);
    } else {
        learn_next();
    }
}

static void learn_answer(int pc)
{
    if (s_fb || s_intro) {
        return;
    }
    bool ok = pc == nq_pc(&s_q);
    s_fb = ok ? 1 : 2;
    s_fb_pc = pc;
    s_fb_until = s_now + (ok ? 650 : 1500);
    s_answers++;
    memset(s_mark, 0, sizeof(s_mark));
    play_midi(nq_midi(&s_q), ok ? 600 : 1100);
    if (ok) {
        s_correct++;
        s_streak++;
        if (s_streak > s_best_streak) {
            s_best_streak = s_streak;
        }
        s_mark[pc] = MK_OK;
        sparks(NOTE_X, Y0 - nq_pos(&s_q) * STEP, 10, C_GREEN);
    } else {
        s_streak = 0;
        s_mark[pc] = MK_BAD;
        s_mark[nq_pc(&s_q)] = MK_SHOW;
        buzz();
    }
    hist_push(&s_lh, ok);
    if (ok && hist_ready(&s_lh) && s_ln < N_LSEQ) {
        s_ln++;
        store_set("sig_ln", (uint32_t)s_ln);
        memset(&s_lh, 0, sizeof(s_lh));
        sfx_play(SFX_LEVEL_UP);
        banner("NEW NOTE!", s_ln >= N_LSEQ ? "ALL NOTES OPEN" : NULL, C_GREEN, 1200);
    }
}

static void learn_update(void)
{
    if (s_fb && s_now >= s_fb_until) {
        if (s_lseen < s_ln) {
            learn_show_intro(s_lseen);
        } else {
            learn_next();
        }
    }
}

// ---------------------------------------------------------------- SPEED

typedef struct {
    bool on;
    uint8_t st;  // 0 = moves, 1 = correct, 2 = missed
    float x, t;
    nq_t q;
} sn_t;

#define N_SN 8
static sn_t s_sn[N_SN];
static int s_lv, s_lives, s_spawned, s_combo;
static int8_t s_sp_key, s_last_dia;
static uint32_t s_score, s_best_spd;
static bool s_new_best;
static float s_speed;
static int64_t s_spawn_ok, s_over_at, s_mark_until;

static const slevel_t *slv(void)
{
    return &SLV[s_lv % N_SLV];
}

static void speed_level(void)
{
    const slevel_t *L = slv();
    s_sp_key = L->nkeys ? L->keys[rnd(L->nkeys)] : 0;
    s_speed = SPEED_V0 * powf(1.06f, (float)s_lv);
    s_spawned = 0;
    s_spawn_ok = s_now + 1600;
    char t[24];
    snprintf(t, sizeof(t), "LEVEL %d", s_lv + 1);
    char b[44];
    if (L->nkeys) {
        snprintf(b, sizeof(b), "%s - KEY OF %s", L->name, key_name(s_sp_key));
    } else {
        snprintf(b, sizeof(b), "%s", L->name);
    }
    banner(t, b, C_AMBER, 1600);
}

static void speed_start(void)
{
    s_state = ST_SPEED;
    memset(s_sn, 0, sizeof(s_sn));
    memset(s_mark, 0, sizeof(s_mark));
    s_lv = 0;
    s_lives = LIVES;
    s_combo = 0;
    s_score = 0;
    s_new_best = false;
    s_over_at = 0;
    s_last_dia = -1;
    speed_level();
}

static void speed_spawn(void)
{
    const slevel_t *L = slv();
    int dia;
    int tries = 0;
    do {
        dia = L->lo + (int)rnd((uint32_t)(L->hi - L->lo + 1));
    } while (dia == s_last_dia && ++tries < 6);
    s_last_dia = (int8_t)dia;
    nq_t q = {L->clef, (int8_t)dia, ACC_NONE, s_sp_key};
    if (L->acc_pct && rnd(100) < L->acc_pct) {
        int l = dia % 7, a = rnd(2) ? 1 : -1;
        if (a > 0 && (l == 2 || l == 6)) {
            a = -1;  // no E# or B#
        }
        if (a < 0 && (l == 0 || l == 3)) {
            a = 1;  // no Cb or Fb
        }
        q.acc = (int8_t)a;
    }
    for (int i = 0; i < N_SN; i++) {
        if (!s_sn[i].on) {
            s_sn[i] = (sn_t){true, 0, (float)SPAWN_X, 0, q};
            s_spawned++;
            return;
        }
    }
}

static int speed_front(void)
{
    int f = -1;
    for (int i = 0; i < N_SN; i++) {
        if (s_sn[i].on && s_sn[i].st == 0 && (f < 0 || s_sn[i].x < s_sn[f].x)) {
            f = i;
        }
    }
    return f;
}

static void speed_lose(sn_t *n)
{
    n->st = 2;
    n->t = 0;
    s_combo = 0;
    if (s_lives > 0) {
        s_lives--;
    }
    if (s_lives == 0 && !s_over_at) {
        s_over_at = s_now + 900;
    }
}

static void speed_answer(int pc, int f)
{
    if (s_over_at) {
        return;
    }
    if (f < 0 || f >= N_SN || !s_sn[f].on || s_sn[f].st != 0) {
        f = speed_front();  // (the note of the press)
    }
    if (f < 0) {
        return;
    }
    sn_t *n = &s_sn[f];
    memset(s_mark, 0, sizeof(s_mark));
    s_mark_until = s_now + 450;
    play_midi(nq_midi(&n->q), 500);
    if (pc == nq_pc(&n->q)) {
        n->st = 1;
        n->t = 0;
        s_combo++;
        s_score += (uint32_t)(10 * (1 + s_lv / 2) + 2 * (s_combo < 10 ? s_combo : 10));
        s_mark[pc] = MK_OK;
        sparks((int)n->x, Y0 - nq_pos(&n->q) * STEP, 8, C_GREEN);
    } else {
        s_mark[pc] = MK_BAD;
        s_mark[nq_pc(&n->q)] = MK_SHOW;
        buzz();
        speed_lose(n);
    }
}

static void speed_update(float dt)
{
    if (s_now >= s_mark_until) {
        memset(s_mark, 0, sizeof(s_mark));
    }
    if (s_over_at) {
        if (s_now >= s_over_at) {
            if (s_score > s_best_spd) {
                s_best_spd = s_score;
                s_new_best = true;
                store_set("sig_spd", s_best_spd);
            }
            s_result_of = ST_SPEED;
            s_state = ST_RESULT;
            sfx_play(s_new_best ? SFX_WIN : SFX_GAME_OVER);
        }
        return;
    }
    bool any = false;
    float last_x = -1000;
    for (int i = 0; i < N_SN; i++) {
        sn_t *n = &s_sn[i];
        if (!n->on) {
            continue;
        }
        any = true;
        if (n->st == 0) {
            n->x -= s_speed * dt;
            if (n->x > last_x) {
                last_x = n->x;
            }
            bool held = s_press && i == s_press_f && s_now - s_press_t < PRESS_HOLD_MS;  // (a finger is on it)
            if (n->x <= PLAY_X && !held) {
                buzz();
                memset(s_mark, 0, sizeof(s_mark));
                s_mark[nq_pc(&n->q)] = MK_SHOW;
                s_mark_until = s_now + 600;
                play_midi(nq_midi(&n->q), 400);
                speed_lose(n);
            }
        } else {
            n->t += dt;
            if (n->t > 0.45f) {
                n->on = false;
            }
        }
    }
    if (s_spawned < SPEED_PER_LV) {
        if (s_now >= s_spawn_ok && last_x < SPAWN_X - SPEED_GAP) {
            speed_spawn();
        }
    } else if (!any) {
        s_lv++;
        sfx_play(SFX_LEVEL_UP);
        speed_level();
    }
}

// ---------------------------------------------------------------- EAR

static int s_esub;
static uint8_t s_en[3] = {2, 2, 3};  // answers in use (NVS "sig_e0".."sig_e2")
static hist_t s_eh;
static int s_eans;           // the answer: an index into the order of the part
static int s_eroot, s_edir;  // the low note; 0 = up, 1 = down, 2 = together
static int s_efb, s_efb_pick;
static int64_t s_efb_until;
static int s_enew = -1;  // a new answer: the next question uses it
static int s_estreak;
static int s_opt[12], s_nopt;  // the answer buttons now (indices into the order)

static void ear_options(void)
{
    s_nopt = 0;
    int n = s_en[s_esub];
    if (s_esub == 0) {
        // the intervals in order of size
        for (int semi = 1; semi <= 12; semi++) {
            for (int i = 0; i < n; i++) {
                if (IORDER[i] == semi) {
                    s_opt[s_nopt++] = i;
                }
            }
        }
    } else if (s_esub == 1) {
        for (int i = 0; i < n; i++) {
            s_opt[s_nopt++] = i;
        }
    }
}

static void ear_play(void)
{
    snd_begin();
    if (s_esub == 0) {
        int lo = s_eroot, hi = s_eroot + IORDER[s_eans];
        if (s_edir == 0) {
            snd_note(0, 650, lo, 58);
            snd_note(700, 900, hi, 58);
        } else if (s_edir == 1) {
            snd_note(0, 650, hi, 58);
            snd_note(700, 900, lo, 58);
        } else {
            snd_note(0, 1300, lo, 50);
            snd_note(0, 1300, hi, 50);
        }
    } else if (s_esub == 1) {
        const chord_t *c = &CHORDS[s_eans];
        for (int k = 0; k < c->n; k++) {
            snd_note(k * 260, 520, s_eroot + c->off[k], 52);
        }
        int tb = c->n * 260 + 120;
        for (int k = 0; k < c->n; k++) {
            snd_note(tb, 1300, s_eroot + c->off[k], 40);
        }
    } else {
        // a C major chord gives the key, then the note
        snd_note(0, 700, 48, 40);
        snd_note(0, 700, 60, 40);
        snd_note(0, 700, 64, 40);
        snd_note(0, 700, 67, 40);
        snd_note(950, 1100, 60 + NORDER[s_eans], 60);
    }
    snd_go();
}

static void ear_next(void)
{
    s_efb = 0;
    memset(s_mark, 0, sizeof(s_mark));
    int n = s_en[s_esub];
    int a;
    if (s_enew >= 0) {
        a = s_enew;
        s_enew = -1;
    } else {
        int tries = 0;
        do {
            a = (int)rnd((uint32_t)n);
        } while (a == s_eans && n > 2 && ++tries < 6);
    }
    s_eans = a;
    if (s_esub == 0) {
        s_eroot = 48 + (int)rnd(20);
        s_edir = n < 6 ? 0 : (rnd(4) < 2 ? 0 : (int)rnd(2) + 1);
    } else if (s_esub == 1) {
        s_eroot = 48 + (int)rnd(12);
    }
    ear_play();
}

static void ear_start(int sub)
{
    s_state = ST_EAR;
    s_esub = sub;
    s_estreak = 0;
    s_enew = -1;
    s_eans = -1;
    memset(&s_eh, 0, sizeof(s_eh));
    ear_options();
    ear_next();
}

static void ear_answer(int a)
{
    if (s_efb || a < 0) {
        return;
    }
    bool ok = a == s_eans;
    s_efb = ok ? 1 : 2;
    s_efb_pick = a;
    s_efb_until = s_now + (ok ? 1100 : 2600);
    if (s_esub == 2) {
        memset(s_mark, 0, sizeof(s_mark));
        s_mark[NORDER[a]] = ok ? MK_OK : MK_BAD;
        s_mark[NORDER[s_eans]] = ok ? MK_OK : MK_SHOW;
    }
    hist_push(&s_eh, ok);
    if (ok) {
        s_estreak++;
        sfx_tone(1320, 1760, 70, W_SINE, 30);
        sparks(SW / 2, TOP_H + 30, 10, C_GREEN);
    } else {
        s_estreak = 0;
        buzz();
        ear_play();  // hear it again with the answer on the screen
    }
    if (ok && hist_ready(&s_eh) && s_en[s_esub] < ETOTAL[s_esub]) {
        int i = s_en[s_esub]++;
        static const char *const K[3] = {"sig_e0", "sig_e1", "sig_e2"};
        store_set(K[s_esub], s_en[s_esub]);
        memset(&s_eh, 0, sizeof(s_eh));
        ear_options();
        s_enew = i;
        const char *nm = s_esub == 0 ? INAME[IORDER[i] - 1] : s_esub == 1 ? CHORDS[i].name : PC_NAME[NORDER[i]];
        sfx_play(SFX_LEVEL_UP);
        banner("NEW ANSWER", nm, C_GREEN, 1500);
    }
}

// the index of a pitch class in NORDER, or -1 if it is not in use
static int ear_note_index(int pc)
{
    for (int i = 0; i < s_en[2]; i++) {
        if (NORDER[i] == pc) {
            return i;
        }
    }
    return -1;
}

static void ear_update(void)
{
    if (s_efb && s_now >= s_efb_until) {
        ear_next();
    }
}

// ---------------------------------------------------------------- SING and TUNER

#define SING_N     10
#define SING_CENTS 30          // in tune within this many cents
#define SING_HOLD  500         // ms in tune for a note
static int s_srange = 1;       // 0 = low (bass), 1 = middle, 2 = high (NVS "sig_rng")
static bool s_any_oct = true;  // any octave counts (NVS "sig_oct")
static nq_t s_sq;
static int s_si;
static float s_shold;
static int64_t s_st0, s_sok_until;
static uint32_t s_stotal, s_best_sing;
static int s_sskips;

static void sing_next(void)
{
    static const int8_t LO[3] = {18, 28, 32}, HI[3] = {29, 35, 39};
    int dia, tries = 0;
    do {
        dia = LO[s_srange] + (int)rnd((uint32_t)(HI[s_srange] - LO[s_srange] + 1));
    } while (dia == s_sq.dia && ++tries < 6);
    nq_t q = {(uint8_t)(s_srange == 0 ? BASS : TREBLE), (int8_t)dia, ACC_NONE, 0};
    if (s_ln > 34 && rnd(100) < 25) {
        int l = dia % 7;
        q.acc = (int8_t)((l == 2 || l == 6) ? -1 : 1);
    }
    s_sq = q;
    s_shold = 0;
    s_st0 = s_now;
    s_sok_until = 0;
}

static void sing_start(void)
{
    s_state = ST_SING;
    s_si = 0;
    s_stotal = 0;
    s_sskips = 0;
    s_new_best = false;
    s_sq.dia = -1;
    mic_open();
    sing_next();
}

// cents from the note to sing (any octave if set)
static float sing_cents(void)
{
    float d = s_pitch_midi - (float)nq_midi(&s_sq);
    if (s_any_oct) {
        d = fmodf(d + 6.0f, 12.0f);
        if (d < 0) {
            d += 12.0f;
        }
        d -= 6.0f;
    }
    return d * 100.0f;
}

static void sing_done(void)
{
    mic_close();
    if (!s_best_sing || s_stotal < s_best_sing) {
        s_best_sing = s_stotal;
        s_new_best = true;
        store_set("sig_sing", s_best_sing);
    }
    s_result_of = ST_SING;
    s_state = ST_RESULT;
    sfx_play(s_new_best ? SFX_WIN : SFX_LEVEL_UP);
}

static void sing_update(float dt)
{
    mic_poll();
    if (s_sok_until) {
        if (s_now >= s_sok_until) {
            if (++s_si >= SING_N) {
                sing_done();
            } else {
                sing_next();
            }
        }
        return;
    }
    if (s_pitch_ok && fabsf(sing_cents()) <= SING_CENTS) {
        s_shold += dt * 1000.0f;
    } else {
        s_shold -= dt * 2000.0f;
        if (s_shold < 0) {
            s_shold = 0;
        }
    }
    if (s_shold >= SING_HOLD) {
        s_stotal += (uint32_t)(s_now - s_st0);
        s_sok_until = s_now + 700;
        sfx_tone(1320, 1760, 90, W_SINE, 30);
        sparks(SING_X, Y0 - nq_pos(&s_sq) * STEP, 14, C_GREEN);
    }
}

// ---------------------------------------------------------------- CHART

#define CH_PER 6  // notes a page (a column of 42-45 px: a thumb target)
static int s_cclef, s_cpage, s_csel = -1;
static int64_t s_csel_until;

static int chart_count(void)
{
    return 17;  // A3..C6 or C2..E4
}

static int chart_pages(void)
{
    return (chart_count() + CH_PER - 1) / CH_PER;
}

static nq_t chart_note(int i)
{
    nq_t q = {(uint8_t)s_cclef, (int8_t)((s_cclef == BASS ? 14 : 26) + i), ACC_NONE, 0};
    return q;
}

static int chart_x(int k)
{
    rc_t s = STAFF_RC;
    return CH_X0 + (2 * k + 1) * (s.w - CH_X0 - 6) / (2 * CH_PER);
}

// ---------------------------------------------------------------- input helpers

#define WHITE_PC(i) (WS[i])
static const uint8_t BLACK_AFTER[5] = {0, 1, 3, 4, 5};
static const uint8_t BLACK_PC[5] = {1, 3, 6, 8, 10};

// the height of a key from x0 to x1 that starts at the row y (the round view: the key ends at the circle)
static int key_h(int x0, int x1, int y, int h)
{
#if BOARD_ROUND
    int ex = x0 + x1 < 320 ? x0 : x1, dx = ex - 160;  // (the outer edge of the key)
    int bot = 160 + (int)sqrtf((float)(160 * 160 - dx * dx)) - 4;
    return bot - y < h ? bot - y : h;
#else
    (void)x0, (void)x1, (void)y;
    return h;
#endif
}

static rc_t white_rc(int i)
{
    rc_t k = KEYS_RC;
    int x0 = k.x + i * k.w / 7, x1 = k.x + (i + 1) * k.w / 7;
    return RC(x0, k.y, x1 - x0, key_h(x0, x1, k.y, k.h));
}

static rc_t black_rc(int j)
{
    rc_t k = KEYS_RC;
    int bw = k.w / 7 * BLACK_PCT / 100;
    int bx = k.x + (BLACK_AFTER[j] + 1) * k.w / 7 - bw / 2;
    return RC(bx, k.y, bw, k.h * BLACK_HPCT / 100);
}

static int piano_hit(int x, int y)
{
    rc_t k = KEYS_RC;
    if (!in_rc(k, x, y)) {
        return -1;
    }
    for (int j = 0; j < 5; j++) {
        rc_t b = black_rc(j);
        // wider than the drawing: big fingers (the hit zone is 40 px or more)
        if (x >= b.x - BLACK_PAD && x < b.x + b.w + BLACK_PAD && y < b.y + b.h) {
            return BLACK_PC[j];
        }
    }
    int i = (x - k.x) * 7 / k.w;
    if (i < 0) {
        i = 0;
    }
    if (i > 6) {
        i = 6;
    }
    return WHITE_PC(i);
}

static rc_t nb_acc(int sharp)
{
    rc_t k = KEYS_RC;
    int w = k.w / 2;
    return RC(k.x + (sharp ? w : 0), k.y, sharp ? k.w - w : w, NB_ACC_H);
}

static rc_t nb_letter(int i)
{
    rc_t k = KEYS_RC;
#if BOARD_ROUND
    // (the round view: the 7 letters in one row that stays in the circle)
    int y0 = k.y + NB_ACC_H, h = 42, half = rchord(y0 + h) - 4;
    int x0 = 160 - half + i * 2 * half / 7, x1 = 160 - half + (i + 1) * 2 * half / 7;
    return RC(x0, y0, x1 - x0, h);
#else
    int x0 = k.x + i * k.w / 7, x1 = k.x + (i + 1) * k.w / 7;
    return RC(x0, k.y + NB_ACC_H, x1 - x0, k.h - NB_ACC_H);
#endif
}

// the answer key under a point, with no effect: a pitch class (the piano), the letter 0..6 + 100 (the names),
// AK_FLAT / AK_SHARP (the sign buttons of the names), -1 = nothing
#define AK_FLAT  -3
#define AK_SHARP -4
static int answer_key(int x, int y)
{
    if (s_input == 0) {
        return piano_hit(x, y);
    }
    for (int s = 0; s < 2; s++) {
        if (in_rc(nb_acc(s), x, y)) {
            return s ? AK_SHARP : AK_FLAT;
        }
    }
    for (int i = 0; i < 7; i++) {
        if (in_rc(nb_letter(i), x, y)) {
            return 100 + i;
        }
    }
    return -1;
}

// an answer: a pitch class; -1 = nothing; -2 = the touch changed a sign
static int answer_hit(int x, int y)
{
    int k = answer_key(x, y);
    if (k == AK_FLAT || k == AK_SHARP) {
        int v = k == AK_SHARP ? 1 : -1;
        s_latch = s_latch == v ? 0 : v;
        sfx_play(SFX_TICK);
        return -2;
    }
    if (k >= 100) {
        int pc = (WS[k - 100] + s_latch + 12) % 12;
        s_latch = 0;
        return pc;
    }
    return k;
}

static rc_t ear_btn(int i, int n)
{
    rc_t g = EAR_GRID_RC;
    int cols = n <= 2 ? n : (n <= 4 ? 2 : (n <= 6 ? 3 : 4));
    if (cols < 1) {
        cols = 1;
    }
    int rows = (n + cols - 1) / cols;
    int c = i % cols, r = i / cols;
    int x0 = g.x + c * g.w / cols, x1 = g.x + (c + 1) * g.w / cols;
    int y0 = g.y + r * g.h / rows, y1 = g.y + (r + 1) * g.h / rows;
#if BOARD_ROUND
    int a = rchord(y0), b = rchord(y1), half = (a < b ? a : b) - 4;  // (in the circle)
    if (x0 < 160 - half) {
        x0 = 160 - half;
    }
    if (x1 > 160 + half) {
        x1 = 160 + half;
    }
#endif
    return RC(x0, y0, x1 - x0, y1 - y0);
}

// ---------------------------------------------------------------- the app

static void go_title(void)
{
    s_press = false;
    s_aim = s_aim_btn = -1;
    mic_close();
    audio_tune_stop();
    s_state = ST_TITLE;
    s_latch = 0;
    memset(s_mark, 0, sizeof(s_mark));
}

static void start_mode(int m)
{
    sfx_play(SFX_CLICK);
    memset(s_part, 0, sizeof(s_part));
    s_banner_until = 0;
    switch (m) {
    case 0:
        tune_play(TUNE_SIGHT);
        learn_start();
        break;
    case 1:
        tune_play(TUNE_SIGHT);
        speed_start();
        break;
    case 2:
        s_state = ST_EARMENU;
        break;
    case 3:
        sing_start();
        break;
    case 4:
        s_state = ST_TUNER;
        mic_open();
        break;
    default:
        s_state = ST_CHART;
        s_csel = -1;
        memset(s_mark, 0, sizeof(s_mark));
        break;
    }
}

static void sight_init(void)
{
    s_input = store_get("sig_in", 0) ? 1 : 0;
    s_ln = (int)store_get("sig_ln", LEARN_START);
    if (s_ln < LEARN_START || s_ln > N_LSEQ) {
        s_ln = LEARN_START;
    }
    s_lseen = (int)store_get("sig_lseen", 0);
    if (s_lseen < 0 || s_lseen > s_ln) {
        s_lseen = 0;
    }
    s_best_spd = store_get("sig_spd", 0);
    s_best_sing = store_get("sig_sing", 0);
    static const char *const K[3] = {"sig_e0", "sig_e1", "sig_e2"};
    static const uint8_t START[3] = {2, 2, 3};
    for (int i = 0; i < 3; i++) {
        uint32_t v = store_get(K[i], START[i]);
        s_en[i] = (uint8_t)(v < START[i] || v > ETOTAL[i] ? START[i] : v);
    }
    s_srange = (int)(store_get("sig_rng", 1) % 3);
    s_any_oct = store_get("sig_oct", 1) != 0;
}

static void sight_enter(int64_t now)
{
    s_now = s_prev = now;
    s_state = ST_TITLE;
    s_banner_until = 0;
    s_latch = 0;
    s_press = false;
    s_aim = s_aim_btn = -1;
    memset(s_part, 0, sizeof(s_part));
    memset(s_mark, 0, sizeof(s_mark));
}

static void sight_leave(void)
{
    audio_tune_stop();
    mic_free();
}

// THE PRESS: the states that take an answer at the lift
static bool press_state(void)
{
    return (s_state == ST_LEARN && !s_intro) || s_state == ST_SPEED || s_state == ST_EAR;
}

static int ear_btn_at(int x, int y)
{
    for (int i = 0; i < s_nopt; i++) {
        if (in_rc(ear_btn(i, s_nopt), x, y)) {
            return i;
        }
    }
    return -1;
}

// the answer area of the state now, 12 px bigger (the first point is the edge of the finger)
static bool press_area(int x, int y)
{
    rc_t k = KEYS_RC;
    if (s_state == ST_EAR && s_esub != 2) {
        k = EAR_GRID_RC;
    }
    return x >= k.x - 12 && x < k.x + k.w + 12 && y >= k.y - 12 && y < k.y + k.h + 12;
}

static void press_aim(int x, int y)
{
    s_aim_x = x;
    s_aim_y = y;
    if (s_state == ST_EAR && s_esub != 2) {
        s_aim = -1;
        s_aim_btn = ear_btn_at(x, y);
    } else {
        s_aim = answer_key(x, y);
        s_aim_btn = -1;
    }
}

// the lift: the answer at the last point (the key that showed); off the keys = no answer
static void press_end(int x, int y)
{
    int f = s_press_f, pc;
    s_press = false;
    s_press_f = -1;
    s_aim = s_aim_btn = -1;
    if (!press_state()) {
        return;
    }
    switch (s_state) {
    case ST_LEARN:
        pc = answer_hit(x, y);
        if (pc >= 0) {
            learn_answer(pc);
        }
        break;
    case ST_SPEED:
        pc = answer_hit(x, y);
        if (pc >= 0) {
            speed_answer(pc, f);
        }
        break;
    case ST_EAR:
        if (s_esub == 2) {
            pc = answer_hit(x, y);
            if (pc >= 0) {
                ear_answer(ear_note_index(pc));
            }
        } else {
            int b = ear_btn_at(x, y);
            if (b >= 0) {
                ear_answer(s_opt[b]);
            }
        }
        break;
    default:
        break;
    }
}

// a finger comes down: an answer press (THE PRESS above), or a key of the piano of CHART
static void down_input(const input_event_t *ev, int x, int y)
{
    int pc;
    s_press = false;
    s_aim = s_aim_btn = -1;
    if (press_state() && press_area(x, y)) {
        s_press = true;
        s_press_t = ev->t_ms;
        s_press_f = s_state == ST_SPEED ? speed_front() : -1;
        press_aim(x, y);
        return;
    }
    switch (s_state) {
    case ST_CHART:
        pc = piano_hit(x, y);
        if (pc >= 0) {
            memset(s_mark, 0, sizeof(s_mark));
            s_mark[pc] = MK_LIT;
            s_csel = -1;
            s_csel_until = s_now + 900;
            play_midi(60 + pc, 700);
            return;
        }
        break;
    default:
        break;
    }
}

static void tap_title(int x, int y)
{
    if (in_rc(TOGGLE_RC, x, y)) {
        s_input ^= 1;
        store_set("sig_in", (uint32_t)s_input);
        sfx_play(SFX_CLICK);
        return;
    }
    for (int i = 0; i < N_MODES; i++) {
        if (in_rc(title_btn(i), x, y)) {
            start_mode(i);
            return;
        }
    }
}

static void tap_sing(int x, int y)
{
    if (in_rc(TB1_RC, x, y)) {
        s_any_oct = !s_any_oct;
        store_set("sig_oct", s_any_oct);
        sfx_play(SFX_CLICK);
    } else if (in_rc(TB2_RC, x, y)) {
        s_srange = (s_srange + 1) % 3;
        store_set("sig_rng", (uint32_t)s_srange);
        sfx_play(SFX_CLICK);
        sing_next();
    } else if (in_rc(HEAR_RC, x, y)) {
        play_midi(nq_midi(&s_sq), 900);
        s_mute_until = s_now + 1150;  // the board must not hear itself
    } else if (in_rc(SKIP_RC, x, y) && !s_sok_until) {
        s_stotal += (uint32_t)(s_now - s_st0) + 15000;
        s_sskips++;
        sfx_play(SFX_BACK);
        if (++s_si >= SING_N) {
            sing_done();
        } else {
            sing_next();
        }
    }
}

static void tap_chart(int x, int y)
{
    if (in_rc(TB3_RC, x, y)) {
        s_cclef ^= 1;
        s_cpage = 0;
        s_csel = -1;
        sfx_play(SFX_CLICK);
        return;
    }
    if (in_rc(TB2_RC, x, y) || in_rc(TB1_RC, x, y)) {
        int pg = s_cpage + (in_rc(TB1_RC, x, y) ? 1 : -1);
        if (pg >= 0 && pg < chart_pages()) {
            s_cpage = pg;
            s_csel = -1;
            sfx_play(SFX_TICK);
        }
        return;
    }
    rc_t s = STAFF_RC;
    if (in_rc(s, x, y) && x >= CH_X0) {
        int k = (x - CH_X0) * CH_PER / (s.w - CH_X0 - 6);
        int i = s_cpage * CH_PER + k;
        if (k >= 0 && k < CH_PER && i < chart_count()) {
            s_csel = i;
            s_csel_until = s_now + 1600;
            nq_t q = chart_note(i);
            memset(s_mark, 0, sizeof(s_mark));
            s_mark[nq_pc(&q)] = MK_LIT;
            play_midi(nq_midi(&q), 800);
        }
    }
}

static void sight_input(const input_event_t *ev)
{
#if BOARD_UI_TABLET
    int x = ev->x * 2 / 3, y = ev->y * 2 / 3;  // (the Tab5 view: the touch in the logical frame)
#elif BOARD_ROUND
    int x = (int)((float)ev->x / rv_k()), y = (int)((float)ev->y / rv_k());  // (the round view: the square)
#else
    int x = ev->x, y = ev->y;
#endif

    // the answers: the press shows the key, the lift answers (THE PRESS above)
    if (ev->kind == IN_DOWN) {
        down_input(ev, x, y);
        return;
    }
    if (ev->kind == IN_MOVE) {
        if (s_press) {
            press_aim(x, y);
        }
        return;
    }
    if (ev->kind == IN_UP) {
        if (s_press) {
            press_end(x, y);
        }
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }

    if (s_state != ST_TITLE && s_state != ST_RESULT && in_rc(BACK_RC, x, y)) {
        sfx_play(SFX_BACK);
        if (s_state == ST_EAR) {
            audio_tune_stop();
            s_state = ST_EARMENU;
        } else {
            go_title();
        }
        return;
    }
    switch (s_state) {
    case ST_TITLE:
        tap_title(x, y);
        break;
    case ST_LEARN:
        if (s_intro && s_now - s_intro_at > 400) {
            learn_intro_done();
            return;
        }
        if (in_rc(TB1_RC, x, y)) {
            play_midi(nq_midi(&s_q), 800);  // hear the note (no help with the name)
        }
        break;
    case ST_EARMENU:
        for (int i = 0; i < 3; i++) {
            if (in_rc(ear_menu_btn(i), x, y)) {
                sfx_play(SFX_CLICK);
                ear_start(i);
                return;
            }
        }
        break;
    case ST_EAR:
        if (in_rc(TB1_RC, x, y)) {
            ear_play();
        }
        break;
    case ST_SING:
        tap_sing(x, y);
        break;
    case ST_CHART:
        tap_chart(x, y);
        break;
    case ST_RESULT:
        if (in_rc(R_AGAIN_RC, x, y)) {
            start_mode(s_result_of == ST_SPEED ? 1 : 3);
        } else if (in_rc(R_MENU_RC, x, y)) {
            sfx_play(SFX_BACK);
            go_title();
        }
        break;
    default:
        break;
    }
}

static void sight_update(int64_t now)
{
    s_now = now;
    float dt = (float)(now - s_prev) / 1000.0f;
    s_prev = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    switch (s_state) {
    case ST_LEARN:
        learn_update();
        break;
    case ST_SPEED:
        speed_update(dt);
        break;
    case ST_EAR:
        ear_update();
        break;
    case ST_SING:
        sing_update(dt);
        break;
    case ST_TUNER:
        mic_poll();
        break;
    case ST_CHART:
        if (s_now >= s_csel_until) {
            s_csel = -1;
            memset(s_mark, 0, sizeof(s_mark));
        }
        break;
    default:
        break;
    }
    for (int i = 0; i < N_PART; i++) {
        part_t *p = &s_part[i];
        if (p->life <= 0) {
            continue;
        }
        p->x += p->vx * dt;
        p->y += p->vy * dt;
        p->vy += 240 * dt;
        p->life -= dt;
    }
}

static bool sight_awake(void)
{
    // the user sings and does not touch the screen
    return s_state == ST_SING || s_state == ST_TUNER || s_now < s_snd_until;
}

// ---------------------------------------------------------------- drawing in bands

// sight_render() draws the page in horizontal bands that fit in the cache (as app_morse.c):
// the page code runs once for each band, and these local functions clip all the drawing to
// the band, so each line of the frame comes from PSRAM one time.
#ifndef BAND
#if BOARD_BAR_SCREEN
#define BAND 12
#else
#define BAND 24
#endif
#endif

static int s_by0 = 0, s_by1 = 1 << 30;

#if BOARD_UI_TABLET
// THE TAB5 VIEW: the code draws a logical frame of 427 x 240 (the 320 x 240 layout, wider: see SW and
// MDX). The functions below put it on the 640 x 360 frame of the Tab5 at 1.5 times, the same on the two axes, so a
// circle stays round. The text has a whole scale (1 -> 2, 2 -> 3, 3 -> 4) and keeps its middle point.
// sight_input() takes the touch back to the logical frame.
static int tv_y(int v)
{
    return v >= 0 ? v * 3 / 2 : -((-v * 3 + 1) / 2);
}  // a logical x or y in the frame
static int tv_h(int h)
{
    return (h * 3 + 1) / 2;
}  // a logical width or height (the same at each place: even lines)

// a rectangle in the physical frame, only the rows of the band
static void PR(int x, int y, int w, int h, uint16_t c)
{
    if (y < s_by0) {
        h -= s_by0 - y;
        y = s_by0;
    }
    if (y + h > s_by1) {
        h = s_by1 - y;
    }
    if (h > 0 && w > 0) {
        gfx_fill_rect(x, y, w, h, c);
    }
}

// the span xa .. xb - 1 of the logical ellipse (cx, cy, rx, ry) in the physical row py; false: the row misses it
static bool tv_span(int cx, int cy, int rx, int ry, int py, int *xa, int *xb)
{
    float pcx = 1.5f * (float)cx + 0.75f, pcy = 1.5f * (float)cy + 0.75f;
    float ax = 1.5f * (float)rx + 0.75f, ay = 1.5f * (float)ry + 0.75f;
    float d = ((float)py + 0.5f - pcy) / ay;
    if (d <= -1.0f || d >= 1.0f) {
        return false;
    }
    float dx = ax * sqrtf(1.0f - d * d);
    *xa = (int)lroundf(pcx - dx);
    *xb = (int)lroundf(pcx + dx);
    return *xb > *xa;
}
#endif

#if BOARD_ROUND
// THE ROUND VIEW: the logical square (320 x 320) on the panel at k = gfx_w / 320, the same on the two axes. The
// text has a whole scale (1 -> 2; others: the scale x k); TXT keeps its left end, TXT_IN and TXT_C the middle.
static int rv(int v)
{
    return (int)floorf((float)v * rv_k());
}
static void PR(int x, int y, int w, int h, uint16_t c)
{
    if (y < s_by0) {
        h -= s_by0 - y;
        y = s_by0;
    }
    if (y + h > s_by1) {
        h = s_by1 - y;
    }
    if (h > 0 && w > 0) {
        gfx_fill_rect(x, y, w, h, c);
    }
}
// the span xa .. xb - 1 of the logical ellipse (cx, cy, rx, ry) in the panel row py; false: the row misses it
static bool rv_span(int cx, int cy, int rx, int ry, int py, int *xa, int *xb)
{
    float k = rv_k(), pcx = ((float)cx + 0.5f) * k, pcy = ((float)cy + 0.5f) * k;
    float ax = ((float)rx + 0.5f) * k, ay = ((float)ry + 0.5f) * k;
    float d = ((float)py + 0.5f - pcy) / ay;
    if (d <= -1.0f || d >= 1.0f) {
        return false;
    }
    float dx = ax * sqrtf(1.0f - d * d);
    *xa = (int)lroundf(pcx - dx);
    *xb = (int)lroundf(pcx + dx);
    return *xb > *xa;
}
static bool in_band(int y, int h)
{
    return rv(y) < s_by1 && rv(y + h) > s_by0;
}
static void R(int x, int y, int w, int h, uint16_t c)
{
    if (w > 0 && h > 0) {
        int x0 = rv(x), y0 = rv(y);
        PR(x0, y0, rv(x + w) - x0, rv(y + h) - y0, c);
    }
}
static void FILL(uint16_t c)
{
    PR(0, 0, gfx_w, gfx_h, c);
}
static void FRAME(int x, int y, int w, int h, int t, uint16_t c)
{
    if (!in_band(y, h)) {
        return;
    }
    R(x, y, w, t, c);
    R(x, y + h - t, w, t, c);
    R(x, y + t, t, h - 2 * t, c);
    R(x + w - t, y + t, t, h - 2 * t, c);
}
static void ELL(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 0 || ry < 0) {
        return;
    }
    int y0 = rv(cy - ry) - 1, y1 = rv(cy + ry + 1) + 1;
    if (y0 < s_by0) {
        y0 = s_by0;
    }
    if (y1 > s_by1) {
        y1 = s_by1;
    }
    for (int py = y0; py < y1; py++) {
        int xa, xb;
        if (rv_span(cx, cy, rx, ry, py, &xa, &xb)) {
            gfx_fill_rect(xa, py, xb - xa, 1, c);
        }
    }
}
#elif BOARD_UI_TABLET
static bool in_band(int y, int h)
{
    int y0 = tv_y(y);
    return y0 < s_by1 && y0 + tv_h(h) > s_by0;
}
#else
static bool in_band(int y, int h)
{
    return y < s_by1 && y + h > s_by0;
}
#endif

#if BOARD_ROUND
#elif BOARD_UI_TABLET
static void R(int x, int y, int w, int h, uint16_t c)
{
    if (w > 0 && h > 0) {
        PR(tv_y(x), tv_y(y), tv_h(w), tv_h(h), c);
    }
}
#else
static void R(int x, int y, int w, int h, uint16_t c)
{
    if (y < s_by0) {
        h -= s_by0 - y;
        y = s_by0;
    }
    if (y + h > s_by1) {
        h = s_by1 - y;
    }
    if (h > 0 && w > 0) {
        gfx_fill_rect(x, y, w, h, c);
    }
}
#endif

#if BOARD_ROUND
#elif BOARD_UI_TABLET
static void FILL(uint16_t c)
{
    PR(0, 0, gfx_w, gfx_h, c);
}
#else
static void FILL(uint16_t c)
{
    R(0, 0, gfx_w, gfx_h, c);
}
#endif

#if BOARD_ROUND
#elif BOARD_UI_TABLET
static void FRAME(int x, int y, int w, int h, int t, uint16_t c)
{
    if (!in_band(y, h)) {
        return;
    }
    R(x, y, w, t, c);
    R(x, y + h - t, w, t, c);
    R(x, y + t, t, h - 2 * t, c);
    R(x + w - t, y + t, t, h - 2 * t, c);
}
#else
static void FRAME(int x, int y, int w, int h, int t, uint16_t c)
{
    if (y >= s_by1 || y + h <= s_by0) {
        return;
    }
    R(x, y, w, t, c);
    R(x, y + h - t, w, t, c);
    R(x, y + t, t, h - 2 * t, c);
    R(x + w - t, y + t, t, h - 2 * t, c);
}
#endif

#if BOARD_ROUND
#elif BOARD_UI_TABLET
static void ELL(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 0 || ry < 0) {
        return;
    }
    int y0 = (int)floorf(1.5f * (float)(cy - ry)), y1 = (int)ceilf(1.5f * (float)(cy + ry + 1));
    if (y0 < s_by0) {
        y0 = s_by0;
    }
    if (y1 > s_by1) {
        y1 = s_by1;
    }
    for (int py = y0; py < y1; py++) {
        int xa, xb;
        if (tv_span(cx, cy, rx, ry, py, &xa, &xb)) {
            gfx_fill_rect(xa, py, xb - xa, 1, c);
        }
    }
}
#else
static void ELL(int cx, int cy, int rx, int ry, uint16_t c)
{
    if (rx < 0 || ry < 0 || cy - ry >= s_by1 || cy + ry < s_by0) {
        return;
    }
    if (ry == 0) {
        R(cx - rx, cy, 2 * rx + 1, 1, c);
        return;
    }
    int y0 = -ry, y1 = ry;
    if (cy + y0 < s_by0) {
        y0 = s_by0 - cy;
    }
    if (cy + y1 > s_by1 - 1) {
        y1 = s_by1 - 1 - cy;
    }
    if (cx + rx < 0 || cx - rx >= gfx_w) {
        return;
    }
    float inv = 1.0f / ((float)ry * (float)ry);
    for (int dy = y0; dy <= y1; dy++) {
        float f = 1.0f - (float)dy * (float)dy * inv;
        int dx = f > 0 ? (int)((float)rx * sqrtf(f) + 0.5f) : 0;
        gfx_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}
#endif

static void CIRC(int cx, int cy, int r, uint16_t c)
{
    ELL(cx, cy, r, r, c);
}

static uint8_t s_glyph[128][7];
static bool s_font_ok;

static void font_capture(void)
{
    if (s_font_ok || !g_fb) {
        return;
    }
    gfx_clip_reset();
    for (int ch = 1; ch < 128; ch++) {
        char t[2] = {(char)ch, 0};
        gfx_fill_rect(0, 0, 6, 7, 0);
        gfx_text(0, 0, t, 1, 0xFFFF);
        for (int r = 0; r < 7; r++) {
            uint8_t bits = 0;
            for (int col = 0; col < 5; col++) {
                if (g_fb[r * gfx_w + col] == 0xFFFF) {
                    bits |= (uint8_t)(0x10 >> col);
                }
            }
            s_glyph[ch][r] = bits;
        }
    }
    s_font_ok = true;
}

// THE ANTI-ALIASED TEXT (the 5 x 7 dot font is rough and hard to read on these screens): the Bungee faces of the
// firmware in
// place of the 5 x 7 dot font. A dot scale k (7 * k px high, 6 * k px a letter) takes the face of about the same width
// (MF_TINY, MF_N16, MF_N24, MF_N34); the capitals keep the middle of the dot line; only the rows of the band show.
// The big faces have capitals only: a small letter shows as its capital.
static const mfont_t *aa_face(int k)
{
#if BOARD_UI_TABLET
    // (the Tab5 text has the scales 2, 3, 4 of its 1.5 view: one step lighter, so a label and the line under it keep
    // their gap)
    return k <= 1 ? &MF_TINY : k == 2 ? &MF_LABEL : k == 3 ? &MF_N20 : k == 4 ? &MF_N24 : &MF_N34;
#else
    return k <= 1 ? &MF_TINY : k == 2 ? &MF_N16 : k == 3 ? &MF_N24 : &MF_N34;
#endif
}
static int aa_cp(int ch, const mfont_t *f)
{
    return (f != &MF_TINY && ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
}
// the advance of a glyph in quarters of a px (a space: 0.3 of the capital height at least)
static int aa_adv4(const mfont_t *f, const mglyph_t *g, int ch)
{
    const int sp = f->asc * 6 / 5;
    return ch == ' ' && g->adv4 < sp ? sp : g->adv4;
}
static int aa_w(const char *s, int k)
{
    const mfont_t *f = aa_face(k);
    int w4 = 0;
    for (; *s; s++) {
        const mglyph_t *g = mf_glyph(f, aa_cp((uint8_t)*s, f));
        w4 += g ? aa_adv4(f, g, *s) : 0;
    }
    return (w4 + 2) / 4;
}
// the text from x (the left end) in the dot line from y (its top) of scale k
static void aa_text(int x, int y, const char *s, int k, uint16_t c)
{
    const mfont_t *f = aa_face(k);
    const int base = y + (7 * k + f->asc) / 2;
    int x4 = x * 4;
    for (; *s; s++) {
        const mglyph_t *g = mf_glyph(f, aa_cp((uint8_t)*s, f));
        if (!g) {
            continue;
        }
        const int gx = (x4 + 2) / 4 + g->ox, gy = base + g->oy;
        const uint8_t *src = f->a4 + g->off;
        for (int j = 0; j < g->h; j++) {
            const int py = gy + j;
            if (py < s_by0 || py >= s_by1 || py < 0 || py >= gfx_h) {
                continue;
            }
            uint16_t *row = g_fb + (size_t)py * gfx_w;
            for (int i = 0; i < g->w; i++) {
                const int px = gx + i;
                if (px < 0 || px >= gfx_w) {
                    continue;
                }
                const int n = j * g->w + i, v = (n & 1) ? src[n >> 1] >> 4 : src[n >> 1] & 15;
                if (v) {
                    row[px] = v == 15 ? c : ui_mix(row[px], c, v * 17);
                }
            }
        }
        x4 += aa_adv4(f, g, *s);
    }
}
#define gfx_text_width(s, k) aa_w((s), (k))

#if BOARD_ROUND
// the text at the panel point px (the left end) and the logical row y (its middle stays); the panel scale ps
static void rtxt(int px, int y, const char *s, int scale, int ps, uint16_t c)
{
    float k = rv_k();
    int py = (int)(((float)y + 3.5f * (float)scale) * k) - 7 * ps / 2;
    // (the AA glyphs reach about 2 scale px over the dot line and 4 scale px under it)
    if (py - 2 * ps - 2 >= s_by1 || py + 11 * ps + 4 <= s_by0) {
        return;
    }
    aa_text(px, py, s, ps, c);
}
static int rps(int scale)
{
    return scale == 1 ? 2 : (int)((float)scale * rv_k() + 0.5f);
}
// a text that keeps its left end (the text on the panel is wider than in the logical square)
static void TXT(int x, int y, const char *s, int scale, uint16_t c)
{
    rtxt(rv(x), y, s, scale, rps(scale), c);
}
// a text that keeps its right end (rx: logical)
static void TXT_RT(int rx, int y, const char *s, int scale, uint16_t c)
{
    int ps = rps(scale);
    rtxt(rv(rx) - gfx_text_width(s, ps), y, s, scale, ps, c);
}
// a text that keeps its middle (cx: logical)
static void TXT_MID(int cx, int y, const char *s, int scale, uint16_t c)
{
    int ps = rps(scale);
    rtxt((int)((float)cx * rv_k()) - gfx_text_width(s, ps) / 2, y, s, scale, ps, c);
}
#elif BOARD_UI_TABLET
static void TXT(int x, int y, const char *s, int scale, uint16_t c)
{
    if (!in_band(y - 2 * scale - 2, 13 * scale + 6)) {  // (the AA glyphs are taller than the dot line)
        return;
    }
    int ps = scale == 1 ? 2 : scale == 3 ? 4 : scale * 3 / 2;  // (a whole scale)
    int px = (int)lroundf(1.5f * (float)x + 0.75f * (float)gfx_text_width(s, scale) -
                          0.5f * (float)gfx_text_width(s, ps));
    int py = (int)lroundf(1.5f * (float)y + 5.25f * (float)scale - 3.5f * (float)ps);
    int wp = gfx_text_width(s, ps);
    if (px + wp > gfx_w) {
        px = gfx_w - wp;  // (the bigger text stays in the frame)
    }
    if (px < 0) {
        px = 0;
    }
    aa_text(px, py, s, ps, c);
}
#else
static void TXT(int x, int y, const char *s, int scale, uint16_t c)
{
    // (the AA glyphs reach about 2 scale px over the dot line and 4 scale px under it)
    if (y - 2 * scale - 2 >= s_by1 || y + 11 * scale + 4 <= s_by0) {
        return;
    }
    aa_text(x, y, s, scale, c);
}
#endif

#if !BOARD_ROUND
// a text that ends at rx
static void TXT_RT(int rx, int y, const char *s, int scale, uint16_t c)
{
    TXT(rx - gfx_text_width(s, scale), y, s, scale, c);
}
#endif

static void TXT_C(int y, const char *s, int scale, uint16_t c)
{
#if BOARD_ROUND
    TXT_MID(SW / 2, y, s, scale, c);
#elif BOARD_UI_TABLET
    TXT((SW - gfx_text_width(s, scale)) / 2, y, s, scale, c);
#else
    TXT((gfx_w - gfx_text_width(s, scale)) / 2, y, s, scale, c);
#endif
}

static void TXT_IN(int x, int w, int y, const char *s, int scale, uint16_t c)
{
#if BOARD_ROUND
    TXT_MID(x + w / 2, y, s, scale, c);
    return;
#endif
    TXT(x + (w - gfx_text_width(s, scale)) / 2, y, s, scale, c);
}

static void text_fit(int x, int w, int y, const char *s, int scale, uint16_t c)
{
#if BOARD_ROUND
    while (scale > 1 && gfx_text_width(s, rps(scale)) > (int)((float)w * rv_k())) {
        scale--;
    }
    TXT_IN(x, w, y, s, scale, c);
    return;
#endif
    while (scale > 1 && gfx_text_width(s, scale) > w) {
        scale--;
    }
    TXT_IN(x, w, y, s, scale, c);
}

static void button(rc_t r, const char *label, const char *sub, uint16_t accent, bool on)
{
    if (!in_band(r.y, r.h)) {
        return;
    }
    R(r.x + 2, r.y + 2, r.w - 4, r.h - 4, on ? C_PANEL2 : C_PANEL);
    R(r.x + 2, r.y + 2, r.w - 4, 3, accent);
    FRAME(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 1, C_LINE);
    int scale = 2;
    if (gfx_text_width(label, 2) > r.w - 10 || r.h < 30) {
        scale = 1;
    }
    int gap = BOARD_ROUND ? 6 : 3;  // (the round view: the text on the panel is taller)
    int th = 7 * scale + (sub ? 7 + gap : 0);
    int ty = r.y + (r.h - th) / 2 + 1;
    text_fit(r.x + 2, r.w - 4, ty, label, scale, C_WHITE);
    if (sub) {
        text_fit(r.x + 2, r.w - 4, ty + 7 * scale + gap, sub, 1, C_DIM);
    }
}

static void top_bar(const char *title, uint16_t accent)
{
    R(0, 0, SW, TOP_H, C_PANEL);
    R(0, TOP_H - 2, SW, 2, accent);
    rc_t b = BACK_RC;
    R(b.x + 3, b.y + 3, b.w - 6, b.h - 8, C_PANEL2);
    TXT_IN(b.x, b.w, b.y + (b.h - 16) / 2 + 1, "<", 2, C_WHITE);
    if (title && !BOARD_ROUND) {
        TXT(b.x + b.w + 6, (TOP_H - 14) / 2, title, 2, accent);
    }
}

static void top_btn(rc_t r, const char *label, uint16_t accent)
{
    if (!in_band(r.y, r.h)) {
        return;
    }
    R(r.x + 3, r.y + 3, r.w - 6, r.h - 8, C_PANEL2);
    R(r.x + 3, r.y + r.h - 7, r.w - 6, 2, accent);
    text_fit(r.x + 3, r.w - 6, r.y + (r.h - 18) / 2 + 1, label, 2, C_WHITE);
}

// 20 small cells: the last answers (green / red), for the next unlock
static void hist_cells(int x, int y, int cw, const hist_t *h)
{
    for (int i = 0; i < 20; i++) {
        uint16_t c = C_DARK;
        if (i < h->n) {
            int k = (h->i - h->n + i + 40) % 20;
            c = h->h[k] ? C_GREEN : C_RED;
        }
        R(x + i * (cw + 1), y, cw, 8, c);
    }
}

static void hearts(int x, int y, int lives)
{
    for (int i = 0; i < LIVES; i++) {
        uint16_t c = i < lives ? C_RED : C_DARK;
        int hx = x + i * 16;
        CIRC(hx + 3, y + 3, 3, c);
        CIRC(hx + 9, y + 3, 3, c);
        for (int j = 0; j < 7; j++) {
            R(hx + j, y + 4 + j, 13 - 2 * j, 1, c);
        }
    }
}

static void draw_parts(void)
{
    for (int i = 0; i < N_PART; i++) {
        const part_t *p = &s_part[i];
        if (p->life <= 0 || !in_band((int)p->y, 3)) {
            continue;
        }
        int x = (int)p->x, y = (int)p->y;
        if (x < 0 || y < 0 || x >= SW - 3 || y >= SH - 3) {
            continue;
        }
        R(x, y, p->life > 0.3f ? 3 : 2, p->life > 0.3f ? 3 : 2, p->c);
    }
}

static void draw_banner(void)
{
    if (s_now >= s_banner_until) {
        return;
    }
    rc_t s = STAFF_RC;
    int w = s.w - 40, h = 44, x = s.x + 20, y = s.y + 8;
    if (!in_band(y, h)) {
        return;
    }
    R(x, y, w, h, C_PANEL);
    FRAME(x, y, w, h, 2, s_banner_col);
    text_fit(x + 4, w - 8, y + 6, s_banner, 2, s_banner_col);
    text_fit(x + 4, w - 8, y + 28, s_banner2, 1, C_WHITE);
}

// ---------------------------------------------------------------- the staff

// The clefs: paths through points (in staff spaces, from the anchor: the G line of the
// treble clef, the F line of the bass clef), made into dots once (Catmull-Rom).
typedef struct {
    float x, y;
} pt_t;

static const pt_t TREBLE_PTS[] = {
        {0.15f, 0.05f},  {0.38f, -0.30f},  {0.05f, -0.62f},  {-0.50f, -0.42f}, {-0.72f, 0.15f},
        {-0.42f, 0.82f}, {0.25f, 1.05f},   {0.85f, 0.70f},   {1.00f, 0.00f},   {0.72f, -0.60f},
        {0.20f, -1.10f}, {-0.25f, -1.70f}, {-0.35f, -2.50f}, {-0.10f, -3.25f}, {0.30f, -3.80f},
        {0.55f, -3.55f}, {0.45f, -2.95f},  {0.10f, -2.30f},  {-0.02f, -1.40f}, {0.05f, -0.30f},
        {0.15f, 0.90f},  {0.22f, 1.90f},   {0.10f, 2.40f},   {-0.25f, 2.60f},  {-0.55f, 2.35f},
};
static const pt_t BASS_PTS[] = {
        {0.10f, 0.00f}, {0.25f, -0.60f}, {0.85f, -0.85f}, {1.45f, -0.55f}, {1.65f, 0.10f},
        {1.40f, 0.85f}, {0.85f, 1.50f},  {0.15f, 2.10f},  {-0.25f, 2.35f},
};

#define CLEF_MAX 700
typedef struct {
    int n;
    int8_t d[CLEF_MAX][2];
} clefdots_t;

static clefdots_t s_clef[2];  // at the size of the staff
static clefdots_t s_iclef;    // the treble clef of the menu icon
static bool s_clef_ok;

static void build_path(const pt_t *p, int n, float s, clefdots_t *out)
{
    out->n = 0;
    int lx = 9999, ly = 9999;
    for (int i = 0; i < n - 1; i++) {
        pt_t p0 = p[i ? i - 1 : 0], p1 = p[i], p2 = p[i + 1], p3 = p[i + 2 < n ? i + 2 : n - 1];
        float len = hypotf(p2.x - p1.x, p2.y - p1.y) * s;
        int steps = (int)(len / 0.6f) + 1;
        for (int j = 0; j <= steps; j++) {
            float t = (float)j / (float)steps, t2 = t * t, t3 = t2 * t;
            float x = 0.5f * (2 * p1.x + (-p0.x + p2.x) * t + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * t2 +
                              (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * t3);
            float y = 0.5f * (2 * p1.y + (-p0.y + p2.y) * t + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * t2 +
                              (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * t3);
            int ix = (int)lroundf(x * s), iy = (int)lroundf(y * s);
            if ((ix != lx || iy != ly) && out->n < CLEF_MAX) {
                out->d[out->n][0] = (int8_t)ix;
                out->d[out->n][1] = (int8_t)iy;
                out->n++;
                lx = ix;
                ly = iy;
            }
        }
    }
}

static void clefs_build(void)
{
    if (s_clef_ok) {
        return;
    }
    build_path(TREBLE_PTS, (int)(sizeof(TREBLE_PTS) / sizeof(TREBLE_PTS[0])), (float)SP, &s_clef[TREBLE]);
    build_path(BASS_PTS, (int)(sizeof(BASS_PTS) / sizeof(BASS_PTS[0])), (float)SP, &s_clef[BASS]);
    build_path(TREBLE_PTS, (int)(sizeof(TREBLE_PTS) / sizeof(TREBLE_PTS[0])), 8.0f, &s_iclef);
    s_clef_ok = true;
}

static void draw_dots(const clefdots_t *c, int ax, int ay, int pen, uint16_t col)
{
    if (!in_band(ay - 5 * SP, 9 * SP)) {
        return;
    }
    for (int i = 0; i < c->n; i++) {
        int y = ay + c->d[i][1];
#if BOARD_UI_TABLET || BOARD_ROUND
        if (!in_band(y - pen, 2 * pen)) {
            continue;
        }
#else
        if (y + pen <= s_by0 || y - pen >= s_by1) {
            continue;
        }
#endif
        R(ax + c->d[i][0] - pen / 2, y - pen / 2, pen, pen, col);
    }
}

static int s_sy0 = Y0;  // the bottom line of the staff now

static int ypos(int pos)
{
    return s_sy0 - pos * STEP;
}

static void draw_clef(int clef, uint16_t col)
{
    int ax = CLEF_X, pen = STEP >= 7 ? 3 : 3;
    if (clef == TREBLE) {
        draw_dots(&s_clef[TREBLE], ax, ypos(2), pen, col);
        CIRC(ax + (int)(-0.42f * SP), ypos(2) + (int)(2.30f * SP), (int)(0.24f * SP), col);
    } else {
        int ay = ypos(6);
        draw_dots(&s_clef[BASS], ax - SP / 2, ay, pen, col);
        CIRC(ax - SP / 2 + (int)(0.15f * SP), ay + (int)(0.1f * SP), (int)(0.32f * SP), col);
        CIRC(ax - SP / 2 + (int)(2.05f * SP), ay - SP / 2, (int)(0.15f * SP) + 1, col);
        CIRC(ax - SP / 2 + (int)(2.05f * SP), ay + SP / 2, (int)(0.15f * SP) + 1, col);
    }
}

static void draw_staff_lines(int x, int w)
{
    for (int l = 0; l < 5; l++) {
        R(x, ypos(2 * l) - 1, w, 2, C_INK);
    }
}

static void draw_ledgers(int cx, int pos, int hw, uint16_t col)
{
    for (int p = -2; p >= pos; p -= 2) {
        R(cx - hw, ypos(p) - 1, 2 * hw + 1, 2, col);
    }
    for (int p = 10; p <= pos; p += 2) {
        R(cx - hw, ypos(p) - 1, 2 * hw + 1, 2, col);
    }
}

static void draw_sharp(int x, int yc, uint16_t col)
{
    int h1 = (int)(1.25f * SP), w = (int)(0.75f * SP);
    R(x + w / 4, yc - h1, 2, 2 * h1 - 2, col);
    R(x + 3 * w / 4, yc - h1 - 2, 2, 2 * h1 - 2, col);
    for (int i = 0; i <= w; i++) {
        int dy = (w / 2 - i) / 3;
        R(x + i, yc - SP / 2 + dy - 1, 1, 3, col);
        R(x + i, yc + SP / 2 + dy - 2, 1, 3, col);
    }
}

static void draw_flat(int x, int yc, uint16_t col, uint16_t bg)
{
    int stem = (int)(1.7f * SP);
    ELL(x + (int)(0.50f * SP), yc + (int)(0.12f * SP), (int)(0.46f * SP), (int)(0.40f * SP), col);
    ELL(x + (int)(0.50f * SP), yc + (int)(0.10f * SP), (int)(0.20f * SP), (int)(0.20f * SP), bg);
    R(x, yc - stem, 3, stem + (int)(0.52f * SP), col);
}

static void draw_acc(int acc, int x, int yc, uint16_t col, uint16_t bg)
{
    if (acc > 0) {
        draw_sharp(x, yc, col);
    } else if (acc < 0) {
        draw_flat(x, yc, col, bg);
    }
}

static int key_width(int key)
{
    int n = key < 0 ? -key : key;
    return n * (int)(0.85f * SP);
}

static void draw_keysig(int clef, int key, uint16_t col, uint16_t bg)
{
    static const int8_t SHP[7] = {8, 5, 9, 6, 3, 7, 4}, FLP[7] = {4, 7, 3, 6, 2, 5, 1};
    int n = key < 0 ? -key : key, off = clef == BASS ? -2 : 0;
    for (int i = 0; i < n && i < 7; i++) {
        int pos = (key > 0 ? SHP[i] : FLP[i]) + off;
        draw_acc(key > 0 ? 1 : -1, KSIG_X + i * (int)(0.85f * SP), ypos(pos), col, bg);
    }
}

// a whole note (hollow): the reading drills
static void draw_whole(int x, int pos, int acc, uint16_t col, uint16_t bg)
{
    int y = ypos(pos), rx = STEP * 16 / 10, ry = STEP * 108 / 100;
    draw_ledgers(x, pos, rx + 5, col == C_INK ? C_INK : col);
    ELL(x, y, rx, ry, col);
    ELL(x, y, rx * 42 / 100, ry * 58 / 100, bg);
    if (acc != ACC_NONE) {
        draw_acc(acc, x - rx - (int)(0.95f * SP), y, col, bg);
    }
}

// a quarter note (filled, with a stem): the reading run
static void draw_quarter(int x, int pos, int acc, uint16_t col, uint16_t bg)
{
    int y = ypos(pos), rx = STEP * 14 / 10, ry = STEP * 100 / 100;
    draw_ledgers(x, pos, rx + 4, col);
    ELL(x, y, rx, ry, col);
    int st = (int)(3.4f * SP);
    if (pos < 4) {
        R(x + rx - 2, y - st, 2, st, col);
    } else {
        R(x - rx, y, 2, st, col);
    }
    if (acc != ACC_NONE) {
        draw_acc(acc, x - rx - (int)(0.95f * SP), y, col, bg);
    }
}

static void paper(rc_t r)
{
    R(r.x, r.y, r.w, r.h, C_PAPER);
    R(r.x, r.y + r.h - 2, r.w, 2, C_PAPER2);
}

// the staff panel: paper, lines, clef, key signature
static void staff_base(int clef, int key)
{
    rc_t s = STAFF_RC;
    if (!in_band(s.y, s.h)) {
        return;
    }
    paper(s);
    draw_staff_lines(s.x + 4, s.w - 8);
    draw_clef(clef, C_INK);
    if (key) {
        draw_keysig(clef, key, C_INK, C_PAPER);
    }
}

// ---------------------------------------------------------------- the piano and the names

static uint16_t mark_col(int m, bool black)
{
    switch (m) {
    case MK_OK:
        return rgb(70, 200, 90);
    case MK_BAD:
        return rgb(230, 70, 70);
    case MK_SHOW:
        return black ? rgb(40, 150, 70) : rgb(140, 230, 150);
    case MK_LIT:
        return C_AMBER;
    case MK_OFF:
        return black ? rgb(60, 60, 66) : rgb(150, 150, 150);
    default:
        return black ? C_KEY_B : C_KEY_W;
    }
}

static void draw_piano(bool labels)
{
    rc_t k = KEYS_RC;
    if (!in_band(k.y, k.h)) {
        return;
    }
    R(k.x, k.y, k.w, k.h, rgb(60, 50, 44));
    for (int i = 0; i < 7; i++) {
        rc_t w = white_rc(i);
        R(w.x + 1, w.y, w.w - 2, w.h - 3, mark_col(s_mark[WS[i]], false));
        R(w.x + 1, w.y + w.h - 8, w.w - 2, 5, rgb(200, 200, 192));
        if (labels) {
            char t[2] = {LETTERS[i], 0};
            TXT_IN(w.x, w.w, w.y + w.h - 26, t, 2, s_mark[WS[i]] ? C_INK : rgb(150, 150, 160));
        }
    }
    for (int j = 0; j < 5; j++) {
        rc_t b = black_rc(j);
        R(b.x, b.y, b.w, b.h, mark_col(s_mark[BLACK_PC[j]], true));
        R(b.x + 3, b.y + b.h - 6, b.w - 6, 3, rgb(70, 70, 80));
    }
    if (s_press && s_aim >= 0 && s_aim < 12) {
        // the key under the finger: an amber frame (the lift answers it)
        bool black = false;
        for (int j = 0; j < 5; j++) {
            if (BLACK_PC[j] == s_aim) {
                rc_t b = black_rc(j);
                FRAME(b.x - 2, b.y, b.w + 4, b.h + 2, 3, C_AMBER);
                black = true;
            }
        }
        for (int i = 0; i < 7 && !black; i++) {
            if (WS[i] == s_aim) {
                rc_t w = white_rc(i);
                FRAME(w.x + 1, w.y, w.w - 2, w.h - 3, 3, C_AMBER);
            }
        }
    }
}

// the name of the key under the finger, beside the finger (the 3.49: at the left of the keys; the 320 frame: above
// the keys), so a person sees the answer before the lift
static void draw_aim(void)
{
    if (!s_press) {
        return;
    }
    char t[4] = {0};
    if (s_aim >= 0 && s_aim < 12) {
        snprintf(t, sizeof(t), "%s", PC_NAME[s_aim]);
    } else if (s_aim >= 100) {
        t[0] = LETTERS[s_aim - 100];
        if (s_latch) {
            t[1] = s_latch > 0 ? '#' : 'b';
        }
    } else if (s_aim == AK_FLAT || s_aim == AK_SHARP) {
        t[0] = s_aim == AK_SHARP ? '#' : 'b';
    } else {
        return;
    }
    rc_t k = KEYS_RC;
    const int bw = 46, bh = 36;
#if BOARD_BAR_SCREEN
    int bx = k.x - bw - 6, by = s_aim_y - bh / 2 - 20;
    if (by < TOP_H + 2) {
        by = TOP_H + 2;
    }
    if (by > SH - bh - 2) {
        by = SH - bh - 2;
    }
#else
    int bx = s_aim_x - bw / 2, by = k.y - bh - 4;
    if (bx < k.x + 2) {
        bx = k.x + 2;
    }
    if (bx > k.x + k.w - bw - 2) {
        bx = k.x + k.w - bw - 2;
    }
#endif
    if (!in_band(by, bh)) {
        return;
    }
    R(bx, by, bw, bh, C_AMBER);
    FRAME(bx, by, bw, bh, 2, C_INK);
    TXT_IN(bx, bw, by + (bh - 21) / 2, t, 3, C_INK);
}

static void draw_names(void)
{
    rc_t k = KEYS_RC;
    if (!in_band(k.y, k.h)) {
        return;
    }
    R(k.x, k.y, k.w, k.h, C_BG);
    for (int s = 0; s < 2; s++) {
        rc_t r = nb_acc(s);
        bool on = s_latch == (s ? 1 : -1);
        R(r.x + 2, r.y + 2, r.w - 4, r.h - 4, on ? C_AMBER : C_PANEL);
        bool aim = s_press && s_aim == (s ? AK_SHARP : AK_FLAT);
        FRAME(r.x + 2, r.y + 2, r.w - 4, r.h - 4, aim ? 3 : 1, aim ? C_WHITE : C_LINE);
        TXT_IN(r.x, r.w, r.y + (r.h - 21) / 2, s ? "#" : "b", 3, on ? C_INK : C_WHITE);
    }
    for (int i = 0; i < 7; i++) {
        rc_t r = nb_letter(i);
        // the mark: the letter of a marked pitch class (natural)
        int m = s_mark[WS[i]];
        for (int a = -1; a <= 1 && !m; a += 2) {
            m = s_mark[(WS[i] + a + 12) % 12] == MK_SHOW ? MK_SHOW : m;
        }
        R(r.x + 2, r.y + 2, r.w - 4, r.h - 4, m ? mark_col(m, false) : C_PANEL2);
        FRAME(r.x + 2, r.y + 2, r.w - 4, r.h - 4, s_press && s_aim == 100 + i ? 3 : 1,
              s_press && s_aim == 100 + i ? C_AMBER : C_LINE);
        char t[2] = {LETTERS[i], 0};
        TXT_IN(r.x, r.w, r.y + (r.h - 21) / 2, t, 3, m ? C_INK : C_WHITE);
    }
}

static void draw_input(void)
{
    if (s_input) {
        draw_names();
    } else {
        draw_piano(true);
    }
    draw_aim();
}

// ---------------------------------------------------------------- the pages

static void render_title(int64_t now)
{
    FILL(C_BG);
#if BOARD_BAR_SCREEN
    TXT(12, 8, "SIGHT", 5, C_AMBER);
    TXT(14, 50, "READ MUSIC - TRAIN YOUR EAR", 1, C_DIM);
    char t[32];
    snprintf(t, sizeof(t), "NOTES %d/%d", s_ln, N_LSEQ);
    TXT(14, 66, t, 1, C_WHITE);
    // a staff with a clef and a small tune
    rc_t d = RC(196, 4, 262, 94);
    if (in_band(d.y, d.h)) {
        R(d.x, d.y, d.w, d.h, C_PAPER);
        int keep = s_sy0;
        s_sy0 = d.y + 70;
        for (int l = 0; l < 5; l++) {
            R(d.x + 4, ypos(2 * l) - 1, d.w - 8, 2, C_INK);
        }
        int ax = d.x + 16;
        draw_dots(&s_clef[TREBLE], ax, ypos(2), 3, C_INK);
        CIRC(ax + (int)(-0.42f * SP), ypos(2) + (int)(2.30f * SP), (int)(0.24f * SP), C_INK);
        static const int8_t TUNE_POS[6] = {2, 4, 6, 5, 7, 4};
        int hop = (int)((now / 300) % 6);
        for (int i = 0; i < 6; i++) {
            int x = d.x + 70 + i * 32;
            draw_quarter(x, TUNE_POS[i], ACC_NONE, i == hop ? C_INK_HI : C_INK, C_PAPER);
        }
        s_sy0 = keep;
    }
#elif BOARD_ROUND
    TXT_C(26, "SIGHT", 4, C_AMBER);
    TXT_C(62, "READ MUSIC - TRAIN YOUR EAR", 1, C_DIM);
#else
    TXT(10, 8, "SIGHT", 4, C_AMBER);
    TXT(12, 42, "READ MUSIC", 1, C_DIM);
    TXT(12, 54, "TRAIN YOUR EAR", 1, C_DIM);
    char t[32];
    snprintf(t, sizeof(t), "NOTES %d/%d", s_ln, N_LSEQ);
    TXT(12, 70, t, 1, C_WHITE);
#endif
    button(TOGGLE_RC, s_input ? "KEYS: NAMES" : "KEYS: PIANO", "TAP TO CHANGE", C_CYAN, false);
    static const char *const NM[N_MODES] = {"LEARN", "SPEED", "EAR", "SING", "TUNER", "CHART"};
    char sub[N_MODES][20];
    snprintf(sub[0], 20, "%d / %d NOTES", s_ln, N_LSEQ);
    snprintf(sub[1], 20, "BEST %lu", (unsigned long)s_best_spd);
    snprintf(sub[2], 20, "3 PARTS");
    if (s_best_sing) {
        snprintf(sub[3], 20, "BEST %.1f S", (double)s_best_sing / 1000.0);
    } else {
        snprintf(sub[3], 20, "USE THE MIC");
    }
    snprintf(sub[4], 20, "PITCH NOW");
    snprintf(sub[5], 20, "ALL NOTES");
    for (int i = 0; i < N_MODES; i++) {
        uint16_t a = i == 0 ? C_GREEN : i == 1 ? C_AMBER : i == 2 ? C_CYAN : i == 3 ? C_VIOLET : C_DIM;
        button(title_btn(i), NM[i], sub[i], a, false);
    }
}

static void render_learn(int64_t now)
{
    FILL(C_BG);
    top_bar("LEARN", C_GREEN);
    char t[32];
#if BOARD_BAR_SCREEN
    snprintf(t, sizeof(t), "%d/%d", s_ln, N_LSEQ);
    TXT(170, 8 + TOP_DY, t, 2, C_WHITE);
    hist_cells(250, 11 + TOP_DY, 4, &s_lh);
    snprintf(t, sizeof(t), "STREAK %d", s_streak);
    TXT(360 - 6, 11 + TOP_DY, t, 1, C_DIM);
    if (!s_intro) {
        top_btn(TB1_RC, "HEAR", C_CYAN);
    }
#else
    hist_cells(HDR_X, 6 + TOP_DY, 3, &s_lh);
#if BOARD_ROUND
    snprintf(t, sizeof(t), "%d/%d", s_ln, N_LSEQ);  // (the bar of the round view is short)
#else
    snprintf(t, sizeof(t), "%d/%d  STREAK %d", s_ln, N_LSEQ, s_streak);
#endif
    TXT(HDR_X, 18 + TOP_DY, t, 1, C_DIM);
    if (!s_intro) {
        top_btn(TB1_RC, "HEAR", C_CYAN);
    }
#endif
    rc_t s = STAFF_RC;
    staff_base(s_q.clef, s_q.key);
    if (in_band(s.y, s.h)) {
        uint16_t col = C_INK;
        if (s_intro) {
            col = C_INK_HI;
        } else if (s_fb == 1) {
            col = C_INK_OK;
        } else if (s_fb == 2) {
            col = C_INK_BAD;
        }
        int pos = nq_pos(&s_q);
        int nx = NOTE_X + (s_q.key ? key_width(s_q.key) / 2 : 0);
        draw_whole(nx, pos, s_q.acc, col, C_PAPER);
        char nm[6];
        nq_name(&s_q, nm);
        if (s_intro) {
            TXT_RT(s.x + s.w - 4, s.y + 6, nm, 3, C_INK_HI);
            const char *head = LSEQ[s_intro_i].key ? "NEW KEY" : "NEW NOTE";
            TXT_RT(s.x + s.w - 4, s.y + 33, head, 1, C_INK);  // (under the taller AA name)
            if (LSEQ[s_intro_i].key) {
                snprintf(t, sizeof(t), "%s MAJOR", key_name(s_q.key));
                TXT_RT(s.x + s.w - 4, s.y + 45, t, 1, C_INK);
            }
            if ((now / 400) & 1) {
                TXT_RT(s.x + s.w - 4, s.y + s.h - 14, "TAP", 1, C_INK);
            }
        } else if (s_fb) {
            TXT_RT(s.x + s.w - 4, s.y + 6, nm, 3, s_fb == 1 ? C_INK_OK : C_INK_BAD);
            if (s_fb == 2) {
                snprintf(t, sizeof(t), "NOT %s", PC_NAME[s_fb_pc]);
                TXT_RT(s.x + s.w - 4, s.y + 30, t, 1, C_INK_BAD);
            }
        }
    }
    draw_input();
    draw_parts();
    draw_banner();
}

static void render_speed(int64_t now)
{
    FILL(C_BG);
    const slevel_t *L = slv();
    top_bar("SPEED", C_AMBER);
    char t[32];
#if BOARD_BAR_SCREEN
    snprintf(t, sizeof(t), "LV %d", s_lv + 1);
    TXT(176, 8 + TOP_DY, t, 2, C_WHITE);
    snprintf(t, sizeof(t), "%lu", (unsigned long)s_score);
    TXT(270, 8 + TOP_DY, t, 2, C_AMBER);
    if (s_combo > 1) {
        snprintf(t, sizeof(t), "x%d", s_combo);
        TXT(370, 11 + TOP_DY, t, 1, C_GREEN);
    }
    hearts(588, 9 + TOP_DY, s_lives);
#elif BOARD_ROUND
    snprintf(t, sizeof(t), "L%d %lu", s_lv + 1, (unsigned long)s_score);
    TXT(HDR_X, 9 + TOP_DY, t, 2, C_WHITE);
    hearts(212, 10 + TOP_DY, s_lives);
#else
    snprintf(t, sizeof(t), "LV %d  %lu", s_lv + 1, (unsigned long)s_score);
    TXT(150, 9 + TOP_DY, t, 2, C_WHITE);
    hearts(268, 10 + TOP_DY, s_lives);
#endif
    rc_t s = STAFF_RC;
    staff_base(L->clef, s_sp_key);
    if (in_band(s.y, s.h)) {
        // the play line
        R(PLAY_X - 2, s.y + 4, 4, s.h - 10, rgb(255, 200, 120));
        R(PLAY_X - 1, s.y + 4, 2, s.h - 10, C_INK_HI);
        int f = speed_front();
        for (int i = 0; i < N_SN; i++) {
            const sn_t *n = &s_sn[i];
            if (!n->on || n->x > s.x + s.w - 6) {
                continue;
            }
            uint16_t col = n->st == 1 ? C_INK_OK : n->st == 2 ? C_INK_BAD : (i == f ? C_INK : rgb(70, 64, 70));
            int y = ypos(nq_pos(&n->q));
            int dy = n->st ? -(int)(n->t * 40) : 0;
            int keep = s_sy0;
            s_sy0 += dy;
            draw_quarter((int)n->x, nq_pos(&n->q), n->q.acc, col, C_PAPER);
            s_sy0 = keep;
            if (i == f && in_band(s.y + s.h - 10, 8)) {
                // a small arrow under the note to answer now
                for (int k = 0; k < 6; k++) {
                    R((int)n->x - k, s.y + s.h - 12 + k, 2 * k + 1, 1, C_INK_HI);
                }
            }
            (void)y;
        }
    }
    draw_input();
    draw_parts();
    draw_banner();
    (void)now;
}

static void render_earmenu(int64_t now)
{
    FILL(C_BG);
    top_bar("EAR", C_CYAN);
#if BOARD_BAR_SCREEN
    TXT(14, 50, "LISTEN AND NAME WHAT YOU HEAR. A NEW ANSWER OPENS", 1, C_DIM);
    TXT(14, 62, "WHEN 18 OF THE LAST 20 ARE CORRECT.", 1, C_DIM);
#elif BOARD_ROUND
    TXT_C(89, "NAME WHAT YOU HEAR.", 1, C_DIM);
#else
    TXT(10, 42, "LISTEN AND NAME WHAT YOU HEAR.", 1, C_DIM);
    TXT(10, 56, "A NEW ANSWER OPENS AT 18/20.", 1, C_DIM);
#endif
    static const char *const SUBS[3] = {"WHICH INTERVAL?", "WHICH CHORD?", "WHICH NOTE?"};
    for (int i = 0; i < 3; i++) {
        char sub[32];
        snprintf(sub, sizeof(sub), "%d/%d  %s", s_en[i], ETOTAL[i], SUBS[i]);
        button(ear_menu_btn(i), ENAME[i], sub, C_CYAN, false);
    }
    (void)now;
}

static void render_ear(int64_t now)
{
    FILL(C_BG);
    top_bar(ENAME[s_esub], C_CYAN);
    top_btn(TB1_RC, "AGAIN", C_CYAN);
#if BOARD_BAR_SCREEN
    hist_cells(250, 11 + TOP_DY, 4, &s_eh);
#elif BOARD_ROUND
    hist_cells(HDR_X + 4, 12 + TOP_DY, 2, &s_eh);
#else
    hist_cells(182, 12 + TOP_DY, 1, &s_eh);
#endif
    rc_t r = s_esub == 2 ? STAFF_RC : EAR_INFO_RC;
    if (in_band(r.y, r.h)) {
        R(r.x, r.y, r.w, r.h, rgb(16, 22, 38));
        FRAME(r.x, r.y, r.w, r.h, 1, C_LINE);

        static const char *const Q[3] = {"WHICH INTERVAL?", "WHICH CHORD?", "WHICH NOTE AFTER THE CHORD?"};
        char t[48];
        int y = r.y + 8;
        text_fit(r.x + 6, r.w - 12, y, Q[s_esub], 2, C_WHITE);
        y += BOARD_BAR_SCREEN ? 22 : 18;
        if (s_esub == 0) {
            static const char *const D[3] = {"UP", "DOWN", "TOGETHER"};
            snprintf(t, sizeof(t), "%s", D[s_edir]);
            TXT_IN(r.x, r.w, y, t, 1, C_DIM);
        }
        if (s_efb) {
            const char *nm = s_esub == 0   ? INAME[IORDER[s_eans] - 1]
                             : s_esub == 1 ? CHORDS[s_eans].name
                                           : PC_NAME[NORDER[s_eans]];
            int ay = r.y + r.h - 30;
#if !BOARD_BAR_SCREEN
            if (s_esub != 2) {
                ay = r.y + 42;
            }
#endif
            if (s_efb == 1) {
                snprintf(t, sizeof(t), "YES: %s", nm);
                text_fit(r.x + 6, r.w - 12, ay, t, 2, C_GREEN);
            } else {
                snprintf(t, sizeof(t), "IT WAS %s", nm);
                text_fit(r.x + 6, r.w - 12, ay, t, 2, C_RED);
            }
        } else {
            snprintf(t, sizeof(t), "STREAK %d", s_estreak);
#if BOARD_BAR_SCREEN
            TXT_IN(r.x, r.w, r.y + r.h - 22, t, 1, C_DIM);
#else
            if (s_esub == 2) {
                TXT_IN(r.x, r.w, r.y + r.h - 16, t, 1, C_DIM);
            }
#endif
        }
    }
    if (s_esub == 2) {
        // the notes not in use are dark
        uint8_t keep[12];
        memcpy(keep, s_mark, sizeof(keep));
        for (int pc = 0; pc < 12; pc++) {
            if (ear_note_index(pc) < 0 && !s_mark[pc]) {
                s_mark[pc] = MK_OFF;
            }
        }
        draw_input();
        memcpy(s_mark, keep, sizeof(keep));
    } else {
        rc_t g = EAR_GRID_RC;
        if (in_band(g.y, g.h)) {
            R(g.x, g.y, g.w, g.h, C_BG);
        }
        for (int i = 0; i < s_nopt; i++) {
            int a = s_opt[i];
            const char *nm = s_esub == 0 ? INAME[IORDER[a] - 1] : CHORDS[a].name;
            rc_t b = ear_btn(i, s_nopt);
            bool on = false;
            uint16_t acc = C_CYAN;
            if (s_efb && a == s_eans) {
                acc = C_GREEN, on = true;
            } else if (s_efb == 2 && a == s_efb_pick) {
                acc = C_RED, on = true;
            }
            button(b, nm, a == s_enew ? "NEW" : NULL, acc, on || (s_press && s_aim_btn == i));
            if (s_efb && (a == s_eans || (s_efb == 2 && a == s_efb_pick)) && in_band(b.y, b.h)) {
                FRAME(b.x + 2, b.y + 2, b.w - 4, b.h - 4, 2, acc);
            }
        }
    }
    draw_parts();
    draw_banner();
    (void)now;
}

// the continuous staff step of a fractional MIDI note (for the trace)
static float midi_to_pos(float m, int clef)
{
    static const float DIA[12] = {0, 0.5f, 1, 1.5f, 2, 3, 3.5f, 4, 4.5f, 5, 5.5f, 6};
    int mi = (int)floorf(m);
    float fr = m - (float)mi;
    float d0 = (float)(7 * (mi / 12 - 1)) + DIA[mi % 12];
    int m1 = mi + 1;
    float d1 = (float)(7 * (m1 / 12 - 1)) + DIA[m1 % 12];
    float d = d0 + (d1 - d0) * fr;
    return d - (clef == BASS ? 18.0f : 30.0f);
}

static void draw_gauge(rc_t g, bool ok, float cents)
{
    if (!in_band(g.y, g.h + 14)) {
        return;
    }
    R(g.x, g.y, g.w, g.h, rgb(16, 22, 38));
    FRAME(g.x, g.y, g.w, g.h, 1, C_LINE);
    int cx = g.x + g.w / 2;
    // the in-tune zone and the ticks
    int zw = g.w * SING_CENTS / 100;
    R(cx - zw / 2, g.y + 2, zw, g.h - 4, rgb(24, 60, 36));
    for (int c = -50; c <= 50; c += 10) {
        int x = cx + c * (g.w - 8) / 100;
        R(x, g.y + 2, 1, c % 50 == 0 ? g.h - 4 : 6, C_DIM);
    }
    if (ok) {
        if (cents > 50) {
            cents = 50;
        }
        if (cents < -50) {
            cents = -50;
        }
        int x = cx + (int)(cents * (float)(g.w - 8) / 100.0f);
        uint16_t col = fabsf(cents) <= 10 ? C_GREEN : fabsf(cents) <= SING_CENTS ? C_AMBER : C_RED;
        R(x - 2, g.y - 3, 5, g.h + 6, col);
    }
    TXT(g.x, g.y + g.h + 4, "FLAT", 1, C_DIM);
    TXT_RT(g.x + g.w, g.y + g.h + 4, "SHARP", 1, C_DIM);
}

// the pitch trace on the staff: x0 .. x1, the newest at the right
static void draw_trace(int x0, int x1, int clef)
{
    int n = N_TRACE;
    for (int k = 0; k < n; k++) {
        float m = s_trace[(s_trace_i - 1 - k + 2 * N_TRACE) % N_TRACE];
        if (m <= 0) {
            continue;
        }
        int x = x1 - k * (x1 - x0) / n;
        float p = midi_to_pos(m, clef);
        if (p < -7 || p > 15) {
            continue;
        }
        int y = s_sy0 - (int)(p * (float)STEP);
        uint16_t c = k == 0 ? C_INK_HI : rgb(110, 120, 200);
        R(x - 2, y - 2, k == 0 ? 6 : 4, k == 0 ? 6 : 4, c);
    }
}

static void render_sing(int64_t now, bool tuner)
{
    FILL(C_BG);
    char t[40];
    if (tuner) {
        top_bar("TUNER", C_VIOLET);
    } else {
        top_bar("SING", C_VIOLET);
        snprintf(t, sizeof(t), "%d/%d", s_si + 1 > SING_N ? SING_N : s_si + 1, SING_N);
#if BOARD_BAR_SCREEN
        TXT(150, 8 + TOP_DY, t, 2, C_WHITE);
#elif BOARD_ROUND
        TXT(HDR_X, 10 + TOP_DY, t, 1, C_WHITE);
#endif
        static const char *const RG[3] = {"LOW", "MID", "HIGH"};
        top_btn(TB2_RC, RG[s_srange], C_VIOLET);
        top_btn(TB1_RC, s_any_oct ? "ANY OCT" : "SAME OCT", C_VIOLET);
    }
    rc_t s = STAFF_RC;
    int clef = tuner ? (s_pitch_ok && s_pitch_midi < 60 ? BASS : TREBLE) : s_sq.clef;
    staff_base(clef, 0);
    if (in_band(s.y, s.h)) {
        if (!tuner) {
            uint16_t col = s_sok_until ? C_INK_OK : C_INK;
            draw_whole(SING_X, nq_pos(&s_sq), s_sq.acc, col, C_PAPER);
            // the hold ring: a bar under the note
            int bw = (int)(s_shold * 60.0f / SING_HOLD);
            if (bw > 60) {
                bw = 60;
            }
            R(SING_X - 30, s.y + s.h - 12, 60, 5, C_PAPER2);
            R(SING_X - 30, s.y + s.h - 12, bw, 5, C_INK_OK);
        }
        if (tuner && s_pitch_ok) {
            int mi = (int)lroundf(s_pitch_midi);
            static const int8_t LET[12] = {0, 0, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};
            static const int8_t ACC[12] = {ACC_NONE, 1,        ACC_NONE, -1,       ACC_NONE, ACC_NONE,
                                           1,        ACC_NONE, -1,       ACC_NONE, -1,       ACC_NONE};
            nq_t q = {(uint8_t)clef, (int8_t)(LET[mi % 12] + 7 * (mi / 12 - 1)), ACC[mi % 12], 0};
            int p = nq_pos(&q);
            if (p >= -6 && p <= 14) {
                draw_whole(SING_X + 20, p, q.acc, C_INK_HI, C_PAPER);
            }
        }
        draw_trace(tuner ? SING_X + 60 : SING_X + 40, s.x + s.w - 10, clef);
        if (!s_mic_ok) {
            TXT_IN(s.x, s.w, s.y + s.h / 2 - 7, "NO MICROPHONE", 2, C_INK_BAD);
        }
    }
    // the right part (3.49) or the lower part (CoreS3)
    rc_t k = KEYS_RC;
    if (in_band(k.y, k.h)) {
        R(k.x, k.y, k.w, k.h, C_BG);
        if (s_pitch_ok) {
            int mi = (int)lroundf(s_pitch_midi);
            snprintf(t, sizeof(t), "%s%d", PC_NAME[mi % 12], mi / 12 - 1);
        } else {
            snprintf(t, sizeof(t), "--");
        }
        float cents = 0;
        if (s_pitch_ok) {
            cents = tuner ? (s_pitch_midi - roundf(s_pitch_midi)) * 100.0f : sing_cents();
        }
#if BOARD_BAR_SCREEN
        TXT(k.x + 12, SING_NAME_Y, t, 4, s_pitch_ok ? C_WHITE : C_DARK);
        if (s_pitch_ok) {
            char c2[24];
            if (tuner) {
                snprintf(c2, sizeof(c2), "%.1f HZ", (double)(440.0f * powf(2.0f, (s_pitch_midi - 69.0f) / 12.0f)));
            } else {
                snprintf(c2, sizeof(c2), "%+d CENTS", (int)lroundf(cents));
            }
            TXT(k.x + 140, SING_NAME_Y + 8, c2, 2, C_DIM);
        }
#elif BOARD_ROUND
        // the name and the cents at the left of the gauge; the count in the bottom cap
        TXT(30, SING_NAME_Y, t, 3, s_pitch_ok ? C_WHITE : C_DARK);
        if (s_pitch_ok) {
            char c2[24];
            if (tuner) {
                snprintf(c2, sizeof(c2), "%.1f HZ", (double)(440.0f * powf(2.0f, (s_pitch_midi - 69.0f) / 12.0f)));
            } else {
                snprintf(c2, sizeof(c2), "%+d C", (int)lroundf(cents));
            }
            TXT(tuner ? 124 : 30, tuner ? SING_NAME_Y + 6 : SING_NAME_Y + 28, c2, 1, C_DIM);
        }
#else
        TXT(k.x + 8, SING_NAME_Y, t, 3, s_pitch_ok ? C_WHITE : C_DARK);
        if (s_pitch_ok) {
            char c2[24];
            if (tuner) {
                snprintf(c2, sizeof(c2), "%.1f HZ", (double)(440.0f * powf(2.0f, (s_pitch_midi - 69.0f) / 12.0f)));
            } else {
                snprintf(c2, sizeof(c2), "%+d C", (int)lroundf(cents));
            }
            TXT(k.x + 8, SING_NAME_Y + 28, c2, 1, C_DIM);
        }
        if (!tuner) {
            char c3[28];
            snprintf(c3, sizeof(c3), "NOTE %d/%d", s_si + 1 > SING_N ? SING_N : s_si + 1, SING_N);
            TXT(k.x + 8, k.y + k.h - 16, c3, 1, C_WHITE);
        }
#endif
        rc_t g = GAUGE_RC;
#if BOARD_BAR_SCREEN
        if (tuner) {
            g = RC(320, 100, 310, 30);
        }
#elif BOARD_ROUND
        if (tuner) {
            g = RC(60, 226, 200, 28);
        }
#else
        if (tuner) {
            g = RC(104, 170, 210 + MDX, 30);
        }
#endif
        draw_gauge(g, s_pitch_ok, cents);
        if (!tuner) {
            button(HEAR_RC, "HEAR", NULL, C_CYAN, false);
            button(SKIP_RC, "SKIP", NULL, C_DIM, false);
        }
        // the level of the microphone
        int lv = (int)(s_level * 600.0f);
        if (lv > k.w - 8) {
            lv = k.w - 8;
        }
        R(k.x + 4 + (BOARD_ROUND ? 36 : 0), k.y + k.h - (BOARD_ROUND ? 16 : 4), BOARD_ROUND ? lv * 3 / 4 : lv, 2,
          C_DARK);
    }
    draw_parts();
    (void)now;
}

static void render_chart(int64_t now)
{
    FILL(C_BG);
    top_bar(BOARD_BAR_SCREEN ? "CHART" : NULL, C_DIM);
    top_btn(TB3_RC, s_cclef == BASS ? "BASS" : "TREBLE", C_AMBER);
    top_btn(TB2_RC, s_cpage > 0 ? "LOWER" : "-", C_DIM);
    top_btn(TB1_RC, s_cpage < chart_pages() - 1 ? "HIGHER" : "-", C_DIM);
    rc_t s = STAFF_RC;
    staff_base(s_cclef, 0);
    if (in_band(s.y, s.h)) {
        int n = chart_count();
        for (int k = 0; k < CH_PER; k++) {
            int i = s_cpage * CH_PER + k;
            if (i >= n) {
                break;
            }
            nq_t q = chart_note(i);
            bool lit = i == s_csel || (s_csel < 0 && s_mark[nq_pc(&q)] == MK_LIT);
            draw_whole(chart_x(k), nq_pos(&q), ACC_NONE, lit ? C_INK_HI : C_INK, C_PAPER);
        }
        if (s_csel >= 0) {
            nq_t q = chart_note(s_csel);
            char nm[6];
            nq_name(&q, nm);
            int p = nq_pos(&q), cx = chart_x(s_csel - s_cpage * CH_PER);
            int ty = p <= 4 ? ypos(p) - 2 * SP - 14 : ypos(p) + SP + 2;
            if (ty < s.y + 2) {
                ty = s.y + 2;
            }
            if (ty > s.y + s.h - 16) {
                ty = s.y + s.h - 16;
            }
            int tx = cx - gfx_text_width(nm, 2) / 2;
            if (tx < s.x + 2) {
                tx = s.x + 2;
            }
            if (tx > s.x + s.w - 2 - gfx_text_width(nm, 2)) {
                tx = s.x + s.w - 2 - gfx_text_width(nm, 2);
            }
            R(tx - 2, ty - 2, gfx_text_width(nm, 2) + 4, 18, C_PAPER);
            TXT(tx, ty, nm, 2, C_INK_HI);
        } else {
            TXT_RT(s.x + s.w - 4, s.y + 4, "TAP A NOTE", 1, C_INK);
        }
    }
    draw_piano(true);
    (void)now;
}

static void render_result(int64_t now)
{
    FILL(C_BG);
    char t[40];
    bool speed = s_result_of == ST_SPEED;
#if BOARD_BAR_SCREEN
    TXT_C(10, speed ? "SPEED OVER" : "SING DONE", 3, speed ? C_AMBER : C_VIOLET);
    if (speed) {
        snprintf(t, sizeof(t), "SCORE %lu   LEVEL %d", (unsigned long)s_score, s_lv + 1);
    } else {
        snprintf(t, sizeof(t), "TIME %.1f S   SKIPS %d", (double)s_stotal / 1000.0, s_sskips);
    }
    TXT_C(46, t, 2, C_WHITE);
    if (s_new_best && ((now / 300) & 1)) {
        TXT_C(72, "NEW BEST!", 2, C_GREEN);
    }
    if (speed) {
        snprintf(t, sizeof(t), "BEST %lu", (unsigned long)s_best_spd);
    } else {
        snprintf(t, sizeof(t), "BEST %.1f S", (double)s_best_sing / 1000.0);
    }
    TXT_C(96, t, 1, C_DIM);
#else
    TXT_C(RS_Y + 20, speed ? "SPEED OVER" : "SING DONE", 3, speed ? C_AMBER : C_VIOLET);
    if (speed) {
        snprintf(t, sizeof(t), "SCORE %lu", (unsigned long)s_score);
    } else {
        snprintf(t, sizeof(t), "TIME %.1f S", (double)s_stotal / 1000.0);
    }
    TXT_C(RS_Y + 64, t, 3, C_WHITE);
    if (speed) {
        snprintf(t, sizeof(t), "LEVEL %d", s_lv + 1);
    } else {
        snprintf(t, sizeof(t), "SKIPS %d", s_sskips);
    }
    TXT_C(RS_Y + 98, t, 2, C_WHITE);
    if (s_new_best && ((now / 300) & 1)) {
        TXT_C(RS_Y + 124, "NEW BEST!", 2, C_GREEN);
    }
    if (speed) {
        snprintf(t, sizeof(t), "BEST %lu", (unsigned long)s_best_spd);
    } else {
        snprintf(t, sizeof(t), "BEST %.1f S", (double)s_best_sing / 1000.0);
    }
    TXT_C(RS_Y + 150, t, 1, C_DIM);
#endif
    button(R_AGAIN_RC, "AGAIN", NULL, C_GREEN, false);
    button(R_MENU_RC, "MODES", NULL, C_DIM, false);
}

static void sight_render(int64_t now)
{
    font_capture();
    clefs_build();
    for (int y0 = 0; y0 < gfx_h; y0 += BAND) {
        s_by0 = y0;
        s_by1 = y0 + BAND < gfx_h ? y0 + BAND : gfx_h;
        switch (s_state) {
        case ST_TITLE:
            render_title(now);
            break;
        case ST_LEARN:
            render_learn(now);
            break;
        case ST_SPEED:
            render_speed(now);
            break;
        case ST_EARMENU:
            render_earmenu(now);
            break;
        case ST_EAR:
            render_ear(now);
            break;
        case ST_SING:
            render_sing(now, false);
            break;
        case ST_TUNER:
            render_sing(now, true);
            break;
        case ST_CHART:
            render_chart(now);
            break;
        case ST_RESULT:
            render_result(now);
            break;
        }
    }
    s_by0 = 0;
    s_by1 = 1 << 30;
}

#include "tficon/tficon.h"
// the menu icon: two notes on a staff; each one hops to its next line in time with the beat
static void sight_icon_note(float nx, float ny, float u, uint16_t c)
{
    tfi_drop_disc(nx, ny, 6.5f * u, 1.5f * u, 70);
    tfi_disc(nx, ny, 6.5f * u, tfi_light(c, 70), c, 256);
    tfd_rect((int)(nx + 4.2f * u), (int)(ny - 26 * u), (int)(2.6f * u) > 1 ? (int)(2.6f * u) : 1, (int)(26 * u), c,
             256);
}
static void sight_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0xff6fa8), tfi_hex(0xa0105a));
    float u = s / 100.0f, ph = tfi_ph(2);
    // the paper with the five lines
    tfi_rbox(x + (int)(13 * u), y + (int)(20 * u), (int)(74 * u), (int)(60 * u), (int)(7 * u), rgb(255, 255, 255),
             tfi_hex(0xf3e6ee), 256);
    int lh = (int)(1.8f * u) > 1 ? (int)(1.8f * u) : 1;
    for (int k = 0; k < 5; k++) {
        tfd_rect(x + (int)(18 * u), y + (int)((33 + k * 9) * u), (int)(64 * u), lh, tfi_hex(0x7a6a80), 220);
    }
    // the two notes hop: one on the beat, one on the off beat
    for (int k = 0; k < 2; k++) {
        float q = ph * 2 + k * 0.5f;
        q -= (float)(int)q;
        int step = (int)(ph * 2 + k * 0.5f) % 2;
        float hop = q < 0.25f ? fsin(q / 0.25f * 3.14159f) : 0;
        float ny = y + (k ? (step ? 55.5f : 46.5f) : (step ? 60 : 69)) * u - hop * 7 * u;
        sight_icon_note(x + (k ? 61 : 37) * u, ny, u, k ? tfi_hex(0xe0187a) : tfi_hex(0x5b2bd8));
    }
    tfi_end();
}

const app_t app_sight = {
        .name = "SIGHT",
        .landscape = true,
        .needs = 0,
        .init = sight_init,
        .enter = sight_enter,
        .leave = sight_leave,
        .input = sight_input,
        .update = sight_update,
        .render = sight_render,
        .draw_icon = sight_icon,
        .awake = sight_awake,
        .round = APP_ROUND_HI,
};
