// MORSE (UTIL): a telegraph key and a Morse trainer, on the landscape screen (640 x 172 on the 3.49, 320 x 240
// on the CoreS3).
// The task: the app task of TFN OS calls the hooks of app_morse; the audio task calls the audio tick
// (morse_tick, each 5 ms) while the app is open.
// The memory: the file statics; no heap. The link (tfn/link.h) keeps its own buffers.
//
// - LEARN: the Koch method. The board plays a letter and you tap the letter. It starts with
//   2 letters and adds one when 18 of the last 20 answers are correct.
// - COPY: the board plays letters, words and numbers, and you type them. Levels, 3 lives.
// - SEND: the board shows a letter or a word, and you key it on the telegraph key. The board
//   learns your speed, shows what it hears, and shows your timing against the ideal timing.
// - LINK: two boards on the same WiFi channel. Each board sends its key live (ESP-NOW,
//   link.c); the other board beeps it in another tone and writes the letters.
// - CHART: all the codes; tap one to hear it.
//
// Sound: the key tone and the tone of the other board are sustain voices that an audio tick
// starts and stops (5 ms steps); the played letters and words use the tune player
// (audio_tune), so their timing is exact at any frame rate. The decoder uses the time stamps
// of the touch events, not the frame time.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/mfont.h"
#include "tfn/uikit.h"
#include "tfn/link.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/tunes.h"
#include "tfn/wifi.h"

// ---------------------------------------------------------------- layout

typedef struct {
    int16_t x, y, w, h;
} rc_t;

#define RC(x, y, w, h) ((rc_t){(int16_t)(x), (int16_t)(y), (int16_t)(w), (int16_t)(h)})

#if BOARD_BAR_SCREEN
#define SW      640
#define SH      172
#define BACK_RC RC(0, 0, 64, 40)
#define KEY_RC  RC(400, 0, 240, 172)  // the telegraph key: the right thumb
#define TOP_H   40
// title
#define T_MINUS_RC RC(406, 6, 50, 44)
#define T_PLUS_RC  RC(584, 6, 50, 44)
// LEARN
#define L_REPLAY_RC RC(66, 0, 128, 40)
#define L_GRID_RC   RC(196, 0, 444, 172)
#define L_FB_RC     RC(0, 42, 194, 130)
#define L_PAGES_RC  RC(196, 0, 60, 172)  // the page tabs (a column), when the letters need two pages
// COPY
#define C_REPLAY_RC RC(66, 0, 96, 40)
#define C_PAGE_RC   RC(164, 128, 64, 44)
#define C_KB_RC     RC(230, 0, 410, 172)  // 10 x 3 keys
#define C_ANS_Y     48
// SEND
#define S_LISTEN_RC RC(298, 0, 100, 40)
#define PANEL_W     398  // the text panel at the left of the key
// CHART
#define CH_GRID_RC RC(0, 42, 640, 130)
#define CH_COLS    7  // a page of 7 x 3 cells (91 x 43 px)
#define CH_ROWS    3
#define CH_PREV_RC RC(442, 0, 96, 40)
#define CH_NEXT_RC RC(542, 0, 96, 40)
// result
#define R_AGAIN_RC RC(150, 118, 160, 46)
#define R_MENU_RC  RC(330, 118, 160, 46)
#define CHAT_COLS  32
#define CHAT_SHOW  3
#else
#if BOARD_UI_TABLET
#define MDX 107  // the Tab5: the logical frame is 427 px wide (x 1.5 = 640)
#else
#define MDX 0
#endif
#define SW         (320 + MDX)
#define SH         240
#define KEY_RC     RC(0, 144, SW, 96)  // the telegraph key: the lower part
#define TOP_H      42
#define T_MINUS_RC RC(48 + MDX / 2, 46, 50, 44)
#define T_PLUS_RC  RC(222 + MDX / 2, 46, 50, 44)
#if BOARD_ROUND
// (the round panel: see THE ROUND LAYOUT below; these are replaced there)
#define BACK_RC     RC(54, 36, 50, 44)
#define L_REPLAY_RC RC(204, 36, 62, 44)
#define L_GRID_RC   RC(36, 134, 248, 124)
#define L_PAGES_X   108
#define C_REPLAY_RC RC(204, 36, 62, 44)
#define S_LISTEN_RC RC(204, 36, 62, 44)
#define CH_GRID_RC  RC(32, 88, 256, 172)
#define CH_PREV_RC  RC(150, 36, 50, 44)
#define CH_NEXT_RC  RC(206, 36, 58, 44)
#else
#define BACK_RC     RC(0, 0, 60, 42)
#define L_REPLAY_RC RC(220 + MDX, 0, 100, 42)
#define L_GRID_RC   RC(0, 88, SW, 152)
#define L_PAGES_X   (214 + MDX)
#define C_REPLAY_RC RC(220 + MDX, 0, 100, 42)
#define S_LISTEN_RC RC(220 + MDX, 0, 100, 42)
#define CH_GRID_RC  RC(0, 44, SW, 196)
#define CH_PREV_RC  RC(130 + MDX, 0, 88, 42)
#define CH_NEXT_RC  RC(222 + MDX, 0, 98, 42)
#endif
#define L_FB_RC    RC(0, 42, SW, 46)
#define FB_X       (BOARD_ROUND ? 30 : 12)
#define HDR2_X     (BOARD_ROUND ? 96 : 142)   // COPY / SEND: the hearts and the level in the top bar
#define SCORE_R    (BOARD_ROUND ? 186 : 216)  // ... the right end of the score     // the feedback text of LEARN
#define HIST_X     (BOARD_ROUND ? 96 : 66)    // the 20 answers of LEARN, in the top bar
#define C_KB_RC    RC(0, 64, SW, 176)         // 8 x 4 keys of 40 x 44
#define C_ANS_Y    42
#define PANEL_W    SW
#define CH_COLS    4  // a page of 4 x 4 cells (80 x 49 px)
#define CH_ROWS    4
#define R_AGAIN_RC RC(18, 182, 136 + MDX / 2, 48)
#define R_MENU_RC  RC(166 + MDX / 2, 182, 136 + MDX / 2, 48)
#define CHAT_COLS  (25 + MDX / 12)
#define CHAT_SHOW  4
#endif

#if BOARD_ROUND
// THE ROUND LAYOUT (the 1.75C 466 x 466, the Watcher 412 x 412: APP_ROUND_HI): the code draws a logical square of
// 320 x 320; all its parts are in the circle of radius 160 round (160, 160). The top cap is the bar (back, the
// score, AGAIN), the middle band has the text and the keys, the bottom cap the telegraph key. The drawing functions
// (THE ROUND VIEW) put the square on the panel at gfx_w / 320.
#undef SH
#undef KEY_RC
#undef TOP_H
#undef T_MINUS_RC
#undef T_PLUS_RC
#undef L_FB_RC
#undef FB_X
#undef HDR2_X
#undef SCORE_R
#undef HIST_X
#undef C_KB_RC
#undef C_ANS_Y
#undef R_AGAIN_RC
#undef R_MENU_RC
#undef CHAT_COLS
#define SH         320
#define KEY_RC     RC(0, 198, SW, 122)  // the telegraph key: the bottom cap
#define TOP_H      84
#define T_MINUS_RC RC(64, 84, 50, 44)
#define T_PLUS_RC  RC(206, 84, 50, 44)
#define L_FB_RC    RC(20, 86, 280, 44)
#define FB_X       40
#define FB_T       52  // the hint text right of the big letter
#define HDR2_X     110
#define SCORE_R    198
#define HIST_X     110
#define C_KB_RC    RC(0, 112, SW, 170)  // 8, 8, 8 and 6 keys: each row follows the circle
#define C_ANS_Y    84
#define R_AGAIN_RC RC(46, 222, 110, 48)
#define R_MENU_RC  RC(164, 222, 110, 48)
#define CHAT_COLS  22
#define HDR_Y      42  // the hearts and the score in the bar
#define HIST_Y     42  // the 20 answers of LEARN in the bar
#define FBL_X      40  // the feedback of LEARN: the letter, its code, the wrong answer
#define FBC_X      76
#define FBN_X      196
#define C_AX       24  // the answer of COPY
#define C_AW       272
#define S_X        24  // the left of the text of SEND and LINK
#define S_TY       92  // SEND: the word, the live code, the timing, the speed
#define S_LY       136
#define S_TYY      156
#define S_WY       176
#define LK_Y       34   // LINK: the state in the bar
#define LK_LY      176  // LINK: the live codes
#define RS_Y       40   // the result page: down from the 240 frame
#define TT_Y1      34   // the title: MORSE, the ticker, the speed
#define TT_TICK    70
#define TT_Y2      30
#define BN_X       10  // the banner
#define BN_W       300
#define BN_Y       120
#define CARD_X     44  // the card of a new letter
#define CARD_Y     88
#define CARD_W     232
#define CARD_H     190
// the half width of the circle of the square at the row y
static int rchord(int y)
{
    int dy = y - 160;
    return dy * dy >= 160 * 160 ? 0 : (int)sqrtf((float)(160 * 160 - dy * dy));
}
static float rv_k(void)
{
    return (float)gfx_w / SW;
}
#else
#define HDR_Y   8
#define FB_T    36
#define HIST_Y  8
#define FBL_X   12
#define FBC_X   48
#define FBN_X   170
#define C_AX    6
#define C_AW    (308 + MDX)
#define S_X     8
#define S_TY    40
#define S_LY    86
#define S_TYY   108
#define S_WY    126
#define LK_Y    0
#define LK_LY   120
#define RS_Y    0
#define TT_Y1   10
#define TT_TICK 40
#define TT_Y2   0
#define BN_X    10
#define BN_W    (300 + MDX)
#define BN_Y    92
#define CARD_X  8
#define CARD_Y  40
#define CARD_W  (SW - 16)
#define CARD_H  196
#endif

static bool in_rc(rc_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

static rc_t title_btn(int i)
{
#if BOARD_BAR_SCREEN
    return RC(6 + i * 127, 104, 121, 62);
#elif BOARD_ROUND
    if (i < 4) {
        return RC(26 + (i % 2) * 138, 136 + (i / 2) * 50, 130, 46);
    }
    return RC(70, 240, 180, 46);
#else
    if (i < 4) {
        return RC(8 + (i % 2) * (156 + MDX / 2), 92 + (i / 2) * 50, 148 + MDX / 2, 48);
    }
    return RC(BOARD_ROUND ? 40 : 8, 192, BOARD_ROUND ? 240 : 304 + MDX, 46);
#endif
}

// ---------------------------------------------------------------- colours

#define C_BG      rgb(12, 16, 28)
#define C_PANEL   rgb(24, 30, 50)
#define C_PANEL2  rgb(36, 44, 72)
#define C_LINE    rgb(64, 76, 116)
#define C_AMBER   rgb(255, 184, 64)
#define C_AMBER_D rgb(120, 84, 30)
#define C_CYAN    rgb(90, 220, 255)
#define C_CYAN_D  rgb(30, 80, 100)
#define C_GREEN   rgb(100, 232, 120)
#define C_RED     rgb(255, 90, 90)
#define C_WHITE   rgb(235, 240, 255)
#define C_DIM     rgb(130, 140, 172)
#define C_DARK    rgb(70, 78, 104)

// ---------------------------------------------------------------- the code

typedef struct {
    char c;
    const char *code;
} mcode_t;

static const mcode_t CODES[] = {
        {'A', ".-"},     {'B', "-..."},   {'C', "-.-."},   {'D', "-.."},   {'E', "."},     {'F', "..-."},
        {'G', "--."},    {'H', "...."},   {'I', ".."},     {'J', ".---"},  {'K', "-.-"},   {'L', ".-.."},
        {'M', "--"},     {'N', "-."},     {'O', "---"},    {'P', ".--."},  {'Q', "--.-"},  {'R', ".-."},
        {'S', "..."},    {'T', "-"},      {'U', "..-"},    {'V', "...-"},  {'W', ".--"},   {'X', "-..-"},
        {'Y', "-.--"},   {'Z', "--.."},   {'0', "-----"},  {'1', ".----"}, {'2', "..---"}, {'3', "...--"},
        {'4', "....-"},  {'5', "....."},  {'6', "-...."},  {'7', "--..."}, {'8', "---.."}, {'9', "----."},
        {'.', ".-.-.-"}, {',', "--..--"}, {'?', "..--.."}, {'/', "-..-."}, {'=', "-...-"},
};
#define N_CODES ((int)(sizeof(CODES) / sizeof(CODES[0])))

// The Koch order (LCWO): the letters of LEARN come in this order.
static const char KOCH[] = "KMURESNAPTLWI.JZ=FOY,VG5/Q92H38B?47C1D60X";
#define N_KOCH ((int)sizeof(KOCH) - 1)

static const char *code_of(char c)
{
    for (int i = 0; i < N_CODES; i++) {
        if (CODES[i].c == c) {
            return CODES[i].code;
        }
    }
    return NULL;
}

static char char_of(const char *code)
{
    for (int i = 0; i < N_CODES; i++) {
        if (strcmp(CODES[i].code, code) == 0) {
            return CODES[i].c;
        }
    }
    return 0;
}

static bool is_prefix(const char *p, const char *s)
{
    while (*p) {
        if (*p++ != *s++) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------- words

static const char *const W2[] = {
        "AN", "AS", "AT", "BE", "BY", "DO", "GO", "HE", "HI", "IF", "IN", "IS", "IT", "ME",
        "MY", "NO", "OF", "OK", "ON", "OR", "SO", "TO", "UP", "US", "WE", "CQ", "DE",
};
static const char *const W3[] = {
        "AND", "ARE", "BIG", "BOX", "CAT", "DOG", "EAT", "FUN", "GET", "HAT", "HOT", "JOY",
        "KEY", "MAP", "NEW", "NOW", "OLD", "ONE", "RED", "RUN", "SEA", "SKY", "SUN", "TEN",
        "TOP", "TWO", "WAY", "YES", "ZOO", "SOS", "FOX", "OWL", "ICE", "JAM",
};
static const char *const W4[] = {
        "BIRD", "BLUE", "CAKE", "CODE", "DARK", "FAST", "FISH", "GAME", "GOLD", "HAND", "HELP",
        "HOME", "JUMP", "KING", "LAMP", "MOON", "NAME", "NOTE", "PLAY", "RAIN", "ROAD", "SHIP",
        "SNOW", "STAR", "TIME", "TREE", "WAVE", "WIND", "WOLF", "ZERO", "BEEP", "KNOB", "QUIZ",
};
static const char *const W5[] = {
        "ABOUT", "BEACH", "BRAVO", "CANDY", "CLOUD", "DANCE", "EAGLE", "FLAME", "GHOST", "HAPPY",
        "HOUSE", "LASER", "LEMON", "MAGIC", "MUSIC", "NIGHT", "OCEAN", "PIANO", "RADIO", "ROBOT",
        "SMILE", "STORM", "TIGER", "TRAIN", "WATER", "ZEBRA", "MORSE", "QUEEN", "JOLLY", "PIXEL",
};
static const char *const HAM[] = {
        "CQ CQ", "DE", "73", "QTH", "RST 599", "TNX",    "QSL", "QRZ?", "SOS",    "NAME", "WX",
        "FB OM", "GM", "GN", "HW?", "TU",      "UR 5NN", "K2X", "W1AW", "N0CALL", "VE3",  "73 GL",
};
static const char *const WMIX[] = {
        "HELLO",  "WORLD",  "SIGNAL", "ARCADE", "BUTTON", "GARDEN", "PLANET", "ROCKET", "SILVER",
        "WINTER", "CASTLE", "DRAGON", "JUNGLE", "MARKET", "PUZZLE", "TOMATO", "YELLOW", "WIZARD",
};
#define NW(a) ((int)(sizeof(a) / sizeof(a[0])))

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
    ST_COPY,
    ST_SEND,
    ST_LINK,
    ST_CHART,
    ST_RESULT
} mstate_t;

static mstate_t s_state;
static int64_t s_now, s_prev;
static int s_cwpm = 18;   // the character speed (setting, NVS "mor_wpm")
static int s_koch_n = 2;  // letters that LEARN uses (NVS "mor_koch")
static uint32_t s_best_copy, s_best_send;
static int64_t s_shake_until;
static float s_shake_px;

// banner (a level, a new letter...)
static char s_banner[28], s_banner2[40];
static int64_t s_banner_until;
static uint16_t s_banner_col;

static void banner(const char *a, const char *b, uint16_t col, int ms)
{
    snprintf(s_banner, sizeof(s_banner), "%s", a);
    snprintf(s_banner2, sizeof(s_banner2), "%s", b ? b : "");
    s_banner_col = col;
    s_banner_until = s_now + ms;
}

// particles
#define N_PART 48
typedef struct {
    float x, y, vx, vy, life;
    uint16_t c;
} part_t;
static part_t s_part[N_PART];

static void sparks(int x, int y, int n, uint16_t c)
{
    for (int k = 0; k < n; k++) {
        int best = 0;
        for (int i = 0; i < N_PART; i++) {
            if (s_part[i].life <= 0) {
                best = i;
                break;
            }
            if (s_part[i].life < s_part[best].life) {
                best = i;
            }
        }
        float a = frand(0, 6.2831853f), v = frand(40, 170);
        s_part[best] = (part_t){(float)x, (float)y, fcos(a) * v, fsin(a) * v - 40, frand(0.4f, 0.9f), c};
    }
}

// ---------------------------------------------------------------- sound

#define PLAY_NOTE    76  // E5, 659 Hz: the tone of this board
#define PLAY_MAX     160
#define TONE_OWN_HZ  659.3f
#define TONE_PART_HZ 554.4f  // C#5: the tone of the other board
#define GRP_OWN      61
#define GRP_PART     62

static tune_note_t s_play[2][PLAY_MAX];
static int s_play_sel;
static int64_t s_play_end;  // the sound of the last playback ends at this time

// Play text as Morse with the tune player. cwpm: the character speed; ewpm: the
// effective speed (Farnsworth: longer gaps between the letters and the words).
// Returns the length in ms.
static int play_text(const char *txt, int cwpm, int ewpm)
{
    tune_note_t *buf = s_play[s_play_sel];
    s_play_sel ^= 1;
    if (cwpm < 5) {
        cwpm = 5;
    }
    float dot = 1200.0f / (float)cwpm, lgap = 3.0f * dot, wgap = 7.0f * dot;
    if (ewpm > 0 && ewpm < cwpm) {
        float ta = (60.0f * cwpm - 37.2f * ewpm) / (float)(cwpm * ewpm) * 1000.0f;
        lgap = 3.0f * ta / 19.0f;
        wgap = 7.0f * ta / 19.0f;
    }
    float t = 80;  // a short lead-in
    int n = 0;
    for (const char *p = txt; *p && n < PLAY_MAX; p++) {
        if (*p == ' ') {
            t += wgap - lgap;  // the letter gap is already in t
            continue;
        }
        const char *code = code_of(*p);
        if (!code) {
            continue;
        }
        for (const char *e = code; *e && n < PLAY_MAX; e++) {
            float d = *e == '-' ? 3.0f * dot : dot;
            if (t + d > 60000.0f) {
                break;
            }
            buf[n++] = (tune_note_t){(uint16_t)t, (uint16_t)d, PLAY_NOTE, 0, W_SINE | TUNE_HOLD, 70};
            t += d + dot;
        }
        t += lgap - dot;
    }
    if (n) {
        audio_tune(buf, n);
    }
    s_play_end = s_now + (int64_t)t;
    return (int)t;
}

// The key tones: the audio tick starts a sustain voice when a key goes down and lets it go
// when the key goes up. [0] = this board, [1] = the other board (from the link ring).
static volatile bool s_own_down;
static volatile bool s_tick_flush;
static bool s_tone_on[2];
static int s_tone_blocks[2];

// the key edges of the other board, at local times (written by the app, read by the tick
// and by the app's decoder)
#define PRING 256
typedef struct {
    int64_t at;
    bool down;
} pedge_t;
static pedge_t s_pring[PRING];
static uint32_t s_phead;       // written by the app (atomic)
static uint32_t s_ptail_tick;  // the audio task
static uint32_t s_ptail_app;   // the app
static bool s_part_tone;       // the audio task: the tone of the other board now

static void pring_push(int64_t at, bool down)
{
    uint32_t h = s_phead;
    s_pring[h % PRING] = (pedge_t){at, down};
    __atomic_store_n(&s_phead, h + 1, __ATOMIC_RELEASE);
}

static void tone_voice(int k)
{
    audio_voice_t v = {
            .delay = 0,
            .dur = AUDIO_SAMPLE_RATE * 8,
            .f0 = k ? TONE_PART_HZ : TONE_OWN_HZ,
            .f1 = k ? TONE_PART_HZ : TONE_OWN_HZ,
            .wave = W_SINE,
            .vol = k ? 34 : 40,
            .attack_ms = 4,
            .shape = AUDIO_SUSTAIN,
            .group = (uint8_t)(GRP_OWN + k),
    };
    audio_voice(&v);
}

static void morse_tick(void)
{
    int64_t now = esp_timer_get_time() / 1000;
    uint32_t h = __atomic_load_n(&s_phead, __ATOMIC_ACQUIRE);
    if (s_tick_flush) {
        s_ptail_tick = h;
        s_part_tone = false;
        s_tick_flush = false;
    }
    if (h - s_ptail_tick > PRING) {
        s_ptail_tick = h - PRING;
    }
    while (s_ptail_tick != h && s_pring[s_ptail_tick % PRING].at <= now) {
        s_part_tone = s_pring[s_ptail_tick % PRING].down;
        s_ptail_tick++;
    }
    bool want[2] = {s_own_down, s_part_tone};
    for (int k = 0; k < 2; k++) {
        if (want[k] && !s_tone_on[k]) {
            tone_voice(k);
            s_tone_on[k] = true;
            s_tone_blocks[k] = 0;
        } else if (!want[k] && s_tone_on[k]) {
            audio_release_group((uint8_t)(GRP_OWN + k), 6);
            s_tone_on[k] = false;
        } else if (s_tone_on[k] && ++s_tone_blocks[k] > 1500) {
            // a key held for 7.5 s: a new voice before the old one ends
            audio_release_group((uint8_t)(GRP_OWN + k), 6);
            tone_voice(k);
            s_tone_blocks[k] = 0;
        }
    }
}

// one last tick: the tones fade, then no more ticks
static void stop_tick(void)
{
    audio_release_group(GRP_OWN, 6);
    audio_release_group(GRP_PART, 6);
    s_tone_on[0] = s_tone_on[1] = false;
    s_part_tone = false;
    audio_set_tick(NULL);
}

// ---------------------------------------------------------------- the decoder

#define HIST   8
#define EL_MAX 7
#define LETTER_GAP                                                                                                     \
    2.2f    // dots: a longer gap ends the letter (the ideal gap is 3 dots,
            // between the elements 1 dot; a short limit helps after a speed-up)

typedef struct {
    float thr;  // ms: an element longer than this is a dash
    bool down;
    int64_t down_at, up_at;
    char el[EL_MAX + 1];            // the elements of the letter now ('.' and '-')
    float on[EL_MAX], off[EL_MAX];  // their lengths and the gaps before them (ms)
    int nel;
    bool word_open;  // a letter came after the last space
    float hist[HIST];
    int nhist, hist_i;
    char out[12];  // decoded characters that the app did not take yet
    int nout;
    bool assisted;  // the last letter was accepted with a new dot/dash limit
    // the last letter, for the timing strip
    float last_on[EL_MAX], last_off[EL_MAX];
    int last_n;
    char last_code[EL_MAX + 1];
    float last_rhythm;  // 0..1: how near the dash/dot ratio is to 3
} dec_t;

static void dec_reset(dec_t *d, int wpm)
{
    memset(d, 0, sizeof(*d));
    d->thr = 2.0f * 1200.0f / (float)(wpm > 4 ? wpm : 5);
    d->last_rhythm = -1;
}

// A new word: no letter and no output, but the learned timing (the limit, the lengths)
// stays.
static void dec_clear(dec_t *d)
{
    d->down = false;
    d->nel = 0;
    d->el[0] = 0;
    d->nout = 0;
    d->word_open = false;
    d->last_n = 0;
    d->last_rhythm = -1;
}

static float dec_dot(const dec_t *d)
{
    return d->thr / 1.732f;  // the limit is the geometric mean of a dot and a dash
}

static int dec_wpm(const dec_t *d)
{
    return (int)(1200.0f / dec_dot(d) + 0.5f);
}

static void dec_push(dec_t *d, char c)
{
    if (d->nout < (int)sizeof(d->out)) {
        d->out[d->nout++] = c;
    }
}

static char dec_pop(dec_t *d)
{
    if (!d->nout) {
        return 0;
    }
    char c = d->out[0];
    d->nout--;
    memmove(d->out, d->out + 1, (size_t)d->nout);
    return c;
}

// The dot/dash limit from the last element lengths: the best split into two groups (in
// log lengths). A clear split (ratio 1.8 or more) gives the limit; else it stays.
static void dec_learn(dec_t *d, float dur)
{
    d->hist[d->hist_i] = dur;
    d->hist_i = (d->hist_i + 1) % HIST;
    if (d->nhist < HIST) {
        d->nhist++;
    }
    int n = d->nhist;
    if (n < 3) {
        return;
    }
    float v[HIST];
    memcpy(v, d->hist, (size_t)n * sizeof(float));
    for (int i = 1; i < n; i++) {
        float x = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > x) {
            v[j + 1] = v[j];
            j--;
        }
        v[j + 1] = x;
    }
    float lv[HIST];
    for (int i = 0; i < n; i++) {
        lv[i] = logf(v[i] > 1 ? v[i] : 1);
    }
    float best = 1e30f, lo_m = 0, hi_m = 0;
    for (int k = 1; k < n; k++) {
        float a = 0, b = 0;
        for (int i = 0; i < k; i++) {
            a += lv[i];
        }
        for (int i = k; i < n; i++) {
            b += lv[i];
        }
        a /= (float)k;
        b /= (float)(n - k);
        float sse = 0;
        for (int i = 0; i < k; i++) {
            sse += (lv[i] - a) * (lv[i] - a);
        }
        for (int i = k; i < n; i++) {
            sse += (lv[i] - b) * (lv[i] - b);
        }
        if (sse < best) {
            best = sse;
            lo_m = expf(a);
            hi_m = expf(b);
        }
    }
    if (lo_m > 0 && hi_m / lo_m > 1.8f) {
        float t = sqrtf(lo_m * hi_m);
        d->thr = t < 50 ? 50 : t > 900 ? 900 : t;
    }
}

static void dec_key(dec_t *d, bool down, int64_t t)
{
    if (down) {
        if (d->down) {
            return;
        }
        d->down = true;
        d->down_at = t;
        return;
    }
    if (!d->down) {
        return;
    }
    d->down = false;
    float dur = (float)(t - d->down_at);
    if (dur < 12) {
        return;  // a bounce: no element
    }
    if (d->nel < EL_MAX) {
        d->on[d->nel] = dur;
        d->off[d->nel] = d->nel ? (float)(d->down_at - d->up_at) : 0;
        d->nel++;
    }
    d->up_at = t;
    dec_learn(d, dur);
    // the elements of this letter again, with the new limit
    for (int i = 0; i < d->nel; i++) {
        d->el[i] = d->on[i] > d->thr ? '-' : '.';
    }
    d->el[d->nel] = 0;
}

static float rhythm_of(const float *on, const char *code, int n)
{
    float ds = 0, dd = 0;
    int nd = 0, na = 0;
    for (int i = 0; i < n; i++) {
        if (code[i] == '-') {
            dd += on[i];
            na++;
        } else {
            ds += on[i];
            nd++;
        }
    }
    if (!nd || !na) {
        return -1;  // only dots or only dashes: no ratio
    }
    float r = (dd / (float)na) / (ds / (float)nd);
    float q = 1.0f - fabsf(r - 3.0f) / 3.0f;
    return q < 0 ? 0 : q > 1 ? 1 : q;
}

// The end of a letter. With an expected code (SEND), a letter of the correct length whose
// dots are all shorter than its dashes counts as correct, and the limit moves to fit it:
// the rhythm is correct, only the speed is different.
static void dec_finish(dec_t *d, const char *expect)
{
    char c = char_of(d->el);
    d->assisted = false;
    if (expect && strcmp(d->el, expect) != 0 && (int)strlen(expect) == d->nel && strchr(expect, '.') &&
        strchr(expect, '-')) {
        float maxdot = 0, mindash = 1e9f;
        for (int i = 0; i < d->nel; i++) {
            if (expect[i] == '.') {
                if (d->on[i] > maxdot) {
                    maxdot = d->on[i];
                }
            } else if (d->on[i] < mindash) {
                mindash = d->on[i];
            }
        }
        if (maxdot * 1.3f < mindash) {
            d->thr = sqrtf(maxdot * mindash);
            strcpy(d->el, expect);
            c = char_of(d->el);
            d->assisted = true;
        }
    }
    d->last_n = d->nel;
    memcpy(d->last_on, d->on, sizeof(d->on));
    memcpy(d->last_off, d->off, sizeof(d->off));
    strcpy(d->last_code, d->el);
    d->last_rhythm = rhythm_of(d->on, d->el, d->nel);
    dec_push(d, c ? c : '*');
    d->nel = 0;
    d->el[0] = 0;
    d->word_open = true;
}

// The time after the last element tells the end of a letter (more than 2.2 dots) and the end
// of a word (more than 6 dots). With an expected code, a correct letter ends sooner and an
// unfinished correct start waits longer.
static void dec_poll(dec_t *d, int64_t now, const char *expect)
{
    if (d->down) {
        return;
    }
    float dot = dec_dot(d);
    float gap = (float)(now - d->up_at);
    if (d->nel > 0) {
        bool end;
        if (expect && strcmp(d->el, expect) == 0) {
            end = gap > 2.0f * dot;
        } else if (expect && is_prefix(d->el, expect)) {
            end = gap > 4.5f * dot;
        } else {
            end = gap > LETTER_GAP * dot;
        }
        if (end) {
            dec_finish(d, expect);
        }
    } else if (d->word_open && gap > 6.0f * dot) {
        d->word_open = false;
        dec_push(d, ' ');
    }
}

// 0..1: the part of the letter gap that is gone (for the "the letter ends" bar)
static float dec_gap_frac(const dec_t *d, int64_t now)
{
    if (d->down || d->nel == 0) {
        return 0;
    }
    float f = (float)(now - d->up_at) / (LETTER_GAP * dec_dot(d));
    return f < 0 ? 0 : f > 1 ? 1 : f;
}

static dec_t s_me, s_part_dec;

// ---------------------------------------------------------------- LEARN

static char s_koch_set[N_KOCH + 1];  // the letters now, in A..Z 0..9 order for the grid
static char s_learn_cur;
static char s_learn_last[2];
static int64_t s_learn_next;  // play a new letter at this time (0 = wait)
static bool s_learn_wait;     // a letter was played: wait for the answer
static uint8_t s_hist20[20];
static int s_hist_n, s_hist_i;
static int s_learn_fb;  // 0 = none, 1 = correct, 2 = wrong
static char s_fb_ans;
static int64_t s_fb_at;
static int s_learn_score, s_learn_streak;
static bool s_fb_replayed;
static bool s_intro;  // the card of a new letter
static char s_intro_c;
static int s_intro_plays;
static int64_t s_intro_next;

static int cmp_char(const void *a, const void *b)
{
    char x = *(const char *)a, y = *(const char *)b;
    // letters first, then digits, then signs
    int rx = (x >= 'A' && x <= 'Z') ? 0 : (x >= '0' && x <= '9') ? 1 : 2;
    int ry = (y >= 'A' && y <= 'Z') ? 0 : (y >= '0' && y <= '9') ? 1 : 2;
    if (rx != ry) {
        return rx - ry;
    }
    return x - y;
}

static void learn_build_set(void)
{
    memcpy(s_koch_set, KOCH, (size_t)s_koch_n);
    s_koch_set[s_koch_n] = 0;
    qsort(s_koch_set, (size_t)s_koch_n, 1, cmp_char);
}

static int learn_correct20(void)
{
    int c = 0;
    for (int i = 0; i < s_hist_n; i++) {
        c += s_hist20[i];
    }
    return c;
}

static void learn_intro(char c)
{
    s_intro = true;
    s_intro_c = c;
    s_intro_plays = 0;
    s_intro_next = s_now + 300;
    s_learn_wait = false;
    s_learn_next = 0;
}

static void learn_start(void)
{
    learn_build_set();
    s_hist_n = s_hist_i = 0;
    s_learn_fb = 0;
    s_learn_score = s_learn_streak = 0;
    s_learn_last[0] = s_learn_last[1] = 0;
    s_learn_wait = false;
    s_intro = false;
    int64_t t0 = s_now + tune_ms(TUNE_MORSE) + 300;
    // the newest letter first gets its card
    learn_intro(KOCH[s_koch_n - 1]);
    s_intro_next = t0;
}

static void learn_play_new(void)
{
    // 40% the newest letter, else any letter; not the same letter 3 times
    char c;
    for (int tries = 0; tries < 8; tries++) {
        c = (rnd(100) < 40) ? KOCH[s_koch_n - 1] : s_koch_set[rnd((uint32_t)s_koch_n)];
        if (!(c == s_learn_last[0] && c == s_learn_last[1])) {
            break;
        }
    }
    s_learn_last[1] = s_learn_last[0];
    s_learn_last[0] = c;
    s_learn_cur = c;
    char t[2] = {c, 0};
    play_text(t, s_cwpm, 0);
    s_learn_wait = true;
    s_learn_next = 0;
}

static void learn_answer(char c)
{
    if (!s_learn_wait || s_learn_fb) {
        return;
    }
    bool ok = c == s_learn_cur;
    s_hist20[s_hist_i] = ok ? 1 : 0;
    s_hist_i = (s_hist_i + 1) % 20;
    if (s_hist_n < 20) {
        s_hist_n++;
    }
    s_learn_fb = ok ? 1 : 2;
    s_fb_ans = c;
    s_fb_at = s_now;
    s_learn_wait = false;
    if (ok) {
        s_learn_score += 1;
        s_learn_streak++;
        sfx_tone(1320, 1760, 70, W_SINE, 40);
        s_learn_next = s_now + 650;
    } else {
        s_learn_streak = 0;
        s_fb_replayed = false;
        sfx_tone(220, 160, 160, W_SQUARE, 18);
        s_shake_until = s_now + 250;
        // the correct letter again, after the buzz
        s_learn_next = s_now + 1900;
    }
    // a new letter at 18 of the last 20
    if (s_hist_n >= 20 && learn_correct20() >= 18 && s_koch_n < N_KOCH) {
        s_koch_n++;
        store_set("mor_koch", (uint32_t)s_koch_n);
        learn_build_set();
        s_hist_n = s_hist_i = 0;
        char t[24];
        snprintf(t, sizeof(t), "NEW LETTER: %c", KOCH[s_koch_n - 1]);
        banner(t, "18 OF 20 CORRECT - WELL DONE", C_GREEN, 2200);
        sfx_play(SFX_LEVEL_UP);
        sparks(SW / 2, SH / 2, 30, C_GREEN);
        s_learn_next = 0;
        s_intro_c = KOCH[s_koch_n - 1];
        s_intro = true;
        s_intro_plays = 0;
        s_intro_next = s_now + 1600;
    }
}

static void learn_update(void)
{
    if (s_intro) {
        // play the letter of the card 3 times, then wait for a tap (or go on after 2 s)
        if (s_intro_next && s_now >= s_intro_next) {
            if (s_intro_plays < 3) {
                char t[4] = {s_intro_c, 0, 0, 0};
                if (s_koch_n <= 2) {
                    t[0] = KOCH[0];  // the first card: K and M
                    t[1] = ' ';
                    t[2] = KOCH[1];
                }
                int ms = play_text(t, s_cwpm, 0);
                s_intro_plays++;
                s_intro_next = s_now + ms + 450;
            } else {
                s_intro_next = 0;
            }
        }
        return;
    }
    if (s_learn_fb && s_now - s_fb_at > (s_learn_fb == 1 ? 600 : 1800)) {
        s_learn_fb = 0;
    }
    if (s_learn_fb == 2 && !s_fb_replayed && s_now - s_fb_at > 450) {
        char t[2] = {s_learn_cur, 0};
        play_text(t, s_cwpm, 0);
        s_fb_replayed = true;
    }
    if (s_learn_next && s_now >= s_learn_next) {
        learn_play_new();
    }
}

// the grid of the letters of LEARN (for the thumb): at most 3 rows of 7 on a page (21 letters: cells of 54 x 57
// px on the 3.49, 45 x 50 px on the 320 frame); more letters go to a second page, with two page tabs
#define L_PER 21
static int s_lpage;  // the page of the letters now
#define CH_PAGES ((N_CODES + CH_COLS * CH_ROWS - 1) / (CH_COLS * CH_ROWS))
static int s_chpage;  // the page of CHART

static int learn_pages(void)
{
    return (s_koch_n + L_PER - 1) / L_PER;
}

static int learn_on_page(void)
{
    int n = s_koch_n - s_lpage * L_PER;
    return n > L_PER ? L_PER : n;
}

static void learn_grid(int *cols, int *rows)
{
    int n = learn_pages() > 1 ? L_PER : s_koch_n;
    int r = n <= 4 ? 1 : n <= 12 ? 2 : 3;
    *rows = r;
    *cols = (n + r - 1) / r;
}

static rc_t learn_area(void)
{
    rc_t g = L_GRID_RC;
#if BOARD_BAR_SCREEN
    if (learn_pages() > 1) {
        rc_t t = L_PAGES_RC;
        g.x = (int16_t)(t.x + t.w + 2);
        g.w = (int16_t)(SW - g.x);
    }
#endif
    return g;
}

// the page tab p (two pages)
static rc_t learn_tab(int p)
{
#if BOARD_BAR_SCREEN
    rc_t t = L_PAGES_RC;
    return RC(t.x, t.y + p * t.h / 2, t.w, t.h / 2 - 2);
#elif BOARD_ROUND
    return RC(L_PAGES_X + p * 54, 262, 50, 36);  // (the bottom cap)
#else
    return RC(L_PAGES_X + p * 53, 44, 51, 42);
#endif
}

// the cell of letter i of the set (s_koch_set); a letter of the other page has no cell (w 0)
static rc_t learn_cell(int i)
{
    int k = i - s_lpage * L_PER;
    if (k < 0 || k >= learn_on_page()) {
        return RC(-100, -100, 0, 0);
    }
    int cols, rows;
    learn_grid(&cols, &rows);
    rc_t g = learn_area();
    int r = k / cols, c = k % cols;
    int x0 = g.x + c * g.w / cols, x1 = g.x + (c + 1) * g.w / cols;
    int y0 = g.y + r * g.h / rows, y1 = g.y + (r + 1) * g.h / rows;
    return RC(x0, y0, x1 - x0, y1 - y0);
}

static int learn_at(int x, int y)
{
    for (int i = 0; i < s_koch_n; i++) {
        if (in_rc(learn_cell(i), x, y)) {
            return i;
        }
    }
    return -1;
}

// THE PRESS (for the thumb): the answer is the letter where the finger comes off the glass, not where it first
// touches. While the finger is down, its letter lights and shows big in the place of the "?", so a person can slide to
// the right letter, or slide off the letters to give no answer.
static bool s_press;
static int s_aim = -1;  // the letter under the finger now (an index of s_koch_set), -1 = none

// ---------------------------------------------------------------- COPY and SEND (levels)

#define COPY_ITEMS 10
#define SEND_ITEMS 8
#define LIVES      3

typedef struct {
    uint8_t kind;   // 0 letters, 1..4 words of 2..5, 5 numbers, 6 ham words, 7 long
    uint8_t ewpm;   // COPY: the effective speed
    uint8_t cplus;  // COPY: more character speed
    bool hint;      // SEND: the codes show under the letters
} level_t;

static const level_t CLEVELS[] = {
        {0, 8, 0, 0},  {1, 9, 0, 0},  {2, 10, 0, 0}, {3, 11, 0, 0}, {5, 12, 0, 0},
        {4, 13, 0, 0}, {6, 14, 0, 0}, {7, 15, 2, 0}, {7, 17, 4, 0}, {7, 19, 6, 0},
};
static const level_t SLEVELS[] = {
        {8, 0, 0, 1}, {0, 0, 0, 1}, {0, 0, 0, 0}, {1, 0, 0, 1}, {2, 0, 0, 0},
        {3, 0, 0, 0}, {5, 0, 0, 0}, {4, 0, 0, 0}, {6, 0, 0, 0}, {7, 0, 0, 0},
};
#define N_CLEVELS ((int)(sizeof(CLEVELS) / sizeof(CLEVELS[0])))
#define N_SLEVELS ((int)(sizeof(SLEVELS) / sizeof(SLEVELS[0])))

static int s_level, s_item, s_lives;
static uint32_t s_score;
static bool s_new_best;
static char s_target[12];
static char s_typed[12];
static int s_ntyped;
static int64_t s_play_at;  // COPY: play the item at this time
static bool s_replayed;
static int s_fb;  // 0 none, 1 correct, 2 wrong
static int64_t s_fb_until;
static bool s_digits;         // COPY: the keyboard shows 0..9 and signs
static mstate_t s_result_of;  // the mode of the result screen
static int s_pos;             // SEND: the letters done
static bool s_hint_on;        // SEND: the codes show (by level or a tap)
static char s_wrong_sent[EL_MAX + 1], s_wrong_need[EL_MAX + 1];
static char s_wrong_c, s_need_c;
static float s_rhythm_sum;
static int s_rhythm_n;
static int s_assists;

static void make_item(uint8_t kind, char *out, int len)
{
    switch (kind) {
    case 8: {  // easy letters for the first SEND level
        static const char EASY[] = "ETIANMSO";
        out[0] = EASY[rnd(8)];
        out[1] = 0;
        break;
    }
    case 0:
        out[0] = (char)('A' + rnd(26));
        out[1] = 0;
        break;
    case 1:
        snprintf(out, (size_t)len, "%s", W2[rnd(NW(W2))]);
        break;
    case 2:
        snprintf(out, (size_t)len, "%s", W3[rnd(NW(W3))]);
        break;
    case 3:
        snprintf(out, (size_t)len, "%s", W4[rnd(NW(W4))]);
        break;
    case 4:
        snprintf(out, (size_t)len, "%s", W5[rnd(NW(W5))]);
        break;
    case 5: {
        int n = 2 + (int)rnd(2);
        for (int i = 0; i < n; i++) {
            out[i] = (char)('0' + rnd(10));
        }
        out[n] = 0;
        break;
    }
    case 6:
        snprintf(out, (size_t)len, "%s", HAM[rnd(NW(HAM))]);
        break;
    default: {
        uint32_t r = rnd(3);
        const char *w = r == 0 ? WMIX[rnd(NW(WMIX))] : r == 1 ? W5[rnd(NW(W5))] : HAM[rnd(NW(HAM))];
        snprintf(out, (size_t)len, "%s", w);
        break;
    }
    }
}

static const level_t *clevel(void)
{
    return &CLEVELS[s_level < N_CLEVELS ? s_level : N_CLEVELS - 1];
}

static const level_t *slevel(void)
{
    return &SLEVELS[s_level < N_SLEVELS ? s_level : N_SLEVELS - 1];
}

static int copy_cwpm(void)
{
    const level_t *l = clevel();
    int extra = s_level >= N_CLEVELS ? s_level - N_CLEVELS + 1 : 0;
    int c = (s_cwpm > l->ewpm ? s_cwpm : l->ewpm) + l->cplus + extra;
    return c > 35 ? 35 : c;
}

static int copy_ewpm(void)
{
    int extra = s_level >= N_CLEVELS ? s_level - N_CLEVELS + 1 : 0;
    int e = clevel()->ewpm + extra;
    int c = copy_cwpm();
    return e > c ? c : e;
}

static void copy_next_item(int delay)
{
    char prev[12];
    snprintf(prev, sizeof(prev), "%s", s_target);
    for (int i = 0; i < 5; i++) {
        make_item(clevel()->kind, s_target, sizeof(s_target));
        if (strcmp(prev, s_target) != 0) {
            break;
        }
    }
    s_ntyped = 0;
    s_typed[0] = 0;
    s_replayed = false;
    s_fb = 0;
    s_digits = clevel()->kind == 5;
    s_play_at = s_now + delay;
}

static void run_start(mstate_t mode)
{
    s_state = mode;
    s_level = 0;
    s_item = 0;
    s_lives = LIVES;
    s_score = 0;
    s_new_best = false;
    s_target[0] = 0;
    s_fb = 0;
    s_rhythm_sum = 0;
    s_rhythm_n = 0;
    s_assists = 0;
    int t0 = tune_ms(TUNE_MORSE) + 300;
    if (mode == ST_COPY) {
        copy_next_item(t0);
        banner("LEVEL 1", "LISTEN, THEN TYPE WHAT YOU HEAR", C_AMBER, 1800);
    } else {
        make_item(slevel()->kind, s_target, sizeof(s_target));
        s_pos = 0;
        s_hint_on = slevel()->hint;
        dec_reset(&s_me, s_cwpm);
        banner("LEVEL 1", "KEY THE LETTERS ON THE BRASS KEY", C_AMBER, 1800);
    }
}

static void run_over(void)
{
    s_result_of = s_state;
    uint32_t *best = s_state == ST_COPY ? &s_best_copy : &s_best_send;
    s_new_best = s_score > *best;
    if (s_new_best) {
        *best = s_score;
        store_set(s_state == ST_COPY ? "mor_copy" : "mor_send", s_score);
    }
    s_state = ST_RESULT;
    s_own_down = false;
    sfx_play(s_new_best ? SFX_WIN : SFX_GAME_OVER);
}

static void level_done(void)
{
    s_level++;
    s_item = 0;
    s_lives = LIVES;  // a clear level gives all the lives back
    char t[20];
    snprintf(t, sizeof(t), "LEVEL %d", s_level + 1);
    const char *b = "FASTER NOW";
    if (s_state == ST_COPY) {
        static const char *const NAMES[] = {"LETTERS",        "2-LETTER WORDS", "3-LETTER WORDS", "4-LETTER WORDS",
                                            "5-LETTER WORDS", "NUMBERS",        "RADIO TALK",     "LONG WORDS"};
        uint8_t k = clevel()->kind;
        b = k < 8 ? NAMES[k] : "FASTER NOW";
    } else {
        b = slevel()->hint ? "THE CODES SHOW" : "NO CODES - TAP THE WORD FOR HELP";
    }
    banner(t, b, C_GREEN, 2000);
    sfx_play(SFX_LEVEL_UP);
    sparks(SW / 2, SH / 3, 24, C_AMBER);
}

static void copy_check(void)
{
    bool ok = strcmp(s_typed, s_target) == 0;
    s_fb = ok ? 1 : 2;
    int len = (int)strlen(s_target);
    if (ok) {
        uint32_t pts = (uint32_t)(len * (5 + s_level * 2));
        if (s_replayed) {
            pts /= 2;
        }
        s_score += pts;
        sfx_tone(1320, 1760, 80, W_SINE, 40);
        sparks(SW / 2, C_ANS_Y + 20, 14, C_GREEN);
        s_fb_until = s_now + 800;
    } else {
        s_lives--;
        sfx_tone(220, 160, 180, W_SQUARE, 18);
        s_shake_until = s_now + 300;
        s_fb_until = s_now + 2400;
        // the answer again, slowly
        play_text(s_target, copy_cwpm(), copy_ewpm() - 2 > 5 ? copy_ewpm() - 2 : 5);
    }
}

static void copy_after_fb(void)
{
    if (s_lives <= 0) {
        run_over();
        return;
    }
    if (++s_item >= COPY_ITEMS) {
        level_done();
        copy_next_item(1900);
    } else {
        copy_next_item(350);
    }
}

static void copy_type(char c)
{
    if (s_fb || s_play_at) {
        return;
    }
    int len = (int)strlen(s_target);
    if (s_ntyped >= len) {
        return;
    }
    // the space in "CQ CQ" comes by itself
    s_typed[s_ntyped++] = c;
    while (s_ntyped < len && s_target[s_ntyped] == ' ') {
        s_typed[s_ntyped++] = ' ';
    }
    s_typed[s_ntyped] = 0;
    sfx_play(SFX_CLICK);
    // (for the thumb: no check at the last letter; ENTER checks the word)
}

static void copy_enter(void)
{
    if (s_fb || s_play_at) {
        return;
    }
    if (s_ntyped >= (int)strlen(s_target) && s_ntyped > 0) {
        copy_check();
    } else {
        sfx_tone(330, 260, 60, W_SQUARE, 14);  // the word is not complete
    }
}

static void copy_del(void)
{
    if (s_fb || s_ntyped == 0) {
        return;
    }
    s_ntyped--;
    while (s_ntyped > 0 && s_typed[s_ntyped - 1] == ' ') {
        s_ntyped--;
    }
    s_typed[s_ntyped] = 0;
}

static void copy_update(void)
{
    if (s_play_at && s_now >= s_play_at) {
        s_play_at = 0;
        play_text(s_target, copy_cwpm(), copy_ewpm());
    }
    if (s_fb && s_now >= s_fb_until) {
        s_fb = 0;
        copy_after_fb();
    }
}

// the keyboard of COPY (for the thumb): an ENTER key checks the word (DEL is far from it); the 3.49: 10 x 3 keys of
// 41 x 57 px (ABC / 123 at the left); the 320 frame: 8 x 4 keys of 40 x 44 px, the fourth row DEL, CLEAR, 123, Y, Z
// and ENTER (the Watcher: 6 keys of 42 px in the circle)
#define K_DEL   '\b'
#define K_ENTER '\r'
#define K_CLR   1
#define K_PAGE  2
#if BOARD_BAR_SCREEN
#define KB_COLS 10
#define KB_ROWS 3
static const char KB_ABC[30] = {'A', 'B', 'C', 'D', 'E', 'F',   'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N',     'O',
                                'P', 'Q', 'R', 'S', 'T', K_DEL, 'U', 'V', 'W', 'X', 'Y', 'Z', 0,   K_ENTER, K_ENTER};
static const char KB_123[30] = {'0', '1', '2', '3', '4', '5',   '6', '7', '8', '9', '.', ',', '?', '/',     '=',
                                0,   0,   0,   0,   0,   K_DEL, 0,   0,   0,   0,   0,   0,   0,   K_ENTER, K_ENTER};
#else
#define KB_COLS 8
#define KB_ROWS 4
#if BOARD_ROUND
static const char KB_ABC[32] = {'A', 'B', 'C',   'D',   'E',    'F', 'G', 'H',     'I', 'J', 'K',
                                'L', 'M', 'N',   'O',   'P',    'Q', 'R', 'S',     'T', 'U', 'V',
                                'W', 'X', K_DEL, K_CLR, K_PAGE, 'Y', 'Z', K_ENTER, 0,   0};
static const char KB_123[32] = {'0', '1', '2',   '3',   '4',    '5', '6', '7',     '8', '9', '.',
                                ',', '?', '/',   '=',   0,      0,   0,   0,       0,   0,   0,
                                0,   0,   K_DEL, K_CLR, K_PAGE, 0,   0,   K_ENTER, 0,   0};
#else
static const char KB_ABC[32] = {'A', 'B', 'C',   'D',   'E',    'F', 'G', 'H',     'I',     'J',    'K',
                                'L', 'M', 'N',   'O',   'P',    'Q', 'R', 'S',     'T',     'U',    'V',
                                'W', 'X', K_DEL, K_CLR, K_PAGE, 'Y', 'Z', K_ENTER, K_ENTER, K_ENTER};
static const char KB_123[32] = {'0', '1', '2',   '3',   '4',    '5', '6', '7',     '8',     '9',    '.',
                                ',', '?', '/',   '=',   0,      0,   0,   0,       0,       0,      0,
                                0,   0,   K_DEL, K_CLR, K_PAGE, 0,   0,   K_ENTER, K_ENTER, K_ENTER};
#endif
#endif
#define KB_N (KB_COLS * KB_ROWS)

static rc_t kb_cell(int i)
{
    rc_t g = C_KB_RC;
    int r = i / KB_COLS, c = i % KB_COLS;
#if BOARD_ROUND
    {
        // each row follows the circle: 8 keys, the last row 6 keys
        int n = r == KB_ROWS - 1 ? 6 : KB_COLS;
        if (c >= n) {
            return RC(-100, -100, 0, 0);
        }
        int y0 = g.y + r * g.h / KB_ROWS, y1 = g.y + (r + 1) * g.h / KB_ROWS;
        int a = rchord(y0), b = rchord(y1), half = (a < b ? a : b) - 6;
        int x0 = 160 - half + c * 2 * half / n, x1 = 160 - half + (c + 1) * 2 * half / n;
        return RC(x0, y0, x1 - x0, y1 - y0);
    }
#endif
    int x0 = g.x + c * g.w / KB_COLS, x1 = g.x + (c + 1) * g.w / KB_COLS;
    int y0 = g.y + r * g.h / KB_ROWS, y1 = g.y + (r + 1) * g.h / KB_ROWS;
    return RC(x0, y0, x1 - x0, y1 - y0);
}

static char kb_char(int i)
{
    return s_digits ? KB_123[i] : KB_ABC[i];
}

static const char *send_expect(void)
{
    if (s_state != ST_SEND || s_fb == 2) {
        return NULL;
    }
    while (s_target[s_pos] == ' ') {
        s_pos++;
    }
    char c = s_target[s_pos];
    return c ? code_of(c) : NULL;
}

static void send_next_item(void)
{
    char prev[12];
    snprintf(prev, sizeof(prev), "%s", s_target);
    for (int i = 0; i < 5; i++) {
        make_item(slevel()->kind, s_target, sizeof(s_target));
        if (strcmp(prev, s_target) != 0) {
            break;
        }
    }
    s_pos = 0;
    s_hint_on = slevel()->hint;
    s_fb = 0;
    s_me.nout = 0;
}

static void send_char(char c)
{
    if (s_fb) {
        return;
    }
    if (c == ' ') {
        return;
    }
    while (s_target[s_pos] == ' ') {
        s_pos++;
    }
    char need = s_target[s_pos];
    if (!need) {
        return;
    }
    if (s_me.last_rhythm >= 0) {
        s_rhythm_sum += s_me.last_rhythm;
        s_rhythm_n++;
    }
    if (s_me.assisted) {
        s_assists++;
    }
    if (c == need) {
        s_pos++;
        sfx_tone(1568, 2093, 50, W_SINE, 30);
        while (s_target[s_pos] == ' ') {
            s_pos++;
        }
        if (!s_target[s_pos]) {
            int len = (int)strlen(s_target);
            float rh = s_rhythm_n ? s_rhythm_sum / (float)s_rhythm_n : 0.5f;
            // more points for a faster key (12 WPM = x1) and for a good rhythm
            float sp = (float)dec_wpm(&s_me) / 12.0f;
            sp = sp < 0.5f ? 0.5f : sp > 2.5f ? 2.5f : sp;
            uint32_t pts = (uint32_t)((float)(len * (5 + s_level * 2)) * sp) + (uint32_t)(rh * 5.0f * (float)len);
            if (s_hint_on && !slevel()->hint) {
                pts = pts * 2 / 3;
            }
            s_score += pts;
            s_fb = 1;
            s_fb_until = s_now + 900;
            sparks(PANEL_W / 2, 60, 18, C_GREEN);
        }
    } else {
        s_fb = 2;
        s_fb_until = s_now + 2200;
        s_lives--;
        s_wrong_c = c;
        s_need_c = need;
        snprintf(s_wrong_sent, sizeof(s_wrong_sent), "%s", s_me.last_code);
        snprintf(s_wrong_need, sizeof(s_wrong_need), "%s", code_of(need) ? code_of(need) : "");
        sfx_tone(220, 160, 180, W_SQUARE, 18);
        s_shake_until = s_now + 300;
    }
}

static void send_update(void)
{
    dec_poll(&s_me, s_now, send_expect());
    char c;
    while ((c = dec_pop(&s_me)) != 0) {
        send_char(c);
    }
    if (s_fb && s_now >= s_fb_until) {
        int fb = s_fb;
        s_fb = 0;
        dec_clear(&s_me);
        if (s_lives <= 0) {
            run_over();
            return;
        }
        if (fb == 2) {
            s_pos = 0;         // the same item again
            s_hint_on = true;  // with its codes
        } else if (++s_item >= SEND_ITEMS) {
            level_done();
            send_next_item();
        } else {
            send_next_item();
        }
    }
}

// ---------------------------------------------------------------- LINK

#define APP_ID    'M'
#define K_HELLO   1
#define K_KEYS    2
#define EDGE_KEEP 8
#define PLAY_DELAY                                                                                                     \
    250     // ms: the other board's key plays this late, so that a lost
            // packet comes again (in the next packet) before its time
#ifndef CHAT_LINES
#define CHAT_LINES 8
#endif

typedef struct {
    uint8_t down;
    uint32_t t;
} edge_t;

static edge_t s_my_edges[EDGE_KEEP];
static uint32_t s_my_nedges;  // all edges since the start (the newest has seq = this)
static uint32_t s_link_base;  // the edges before LINK opened are not sent
static int64_t s_my_last_edge, s_last_send, s_last_hello;
static bool s_link_ok;

typedef struct {
    bool present;
    uint8_t mac[6];
    char name[17];
    int wpm;
    int8_t rssi;
    int64_t last_rx;
    uint32_t last_seq;
    bool have_seq;
    int64_t offset;  // local ms - the partner's ms
    bool have_offset;
    int64_t last_offset_fix;
    bool down;  // the last edge that we scheduled
    int64_t last_edge_at;
    uint32_t last_t;  // the partner's time of the last edge
    bool have_last;
} partner_t;
static partner_t s_pt;

typedef struct {
    char txt[CHAT_COLS + 1];
    uint8_t who;  // 0 = this board, 1 = the other board
} chat_t;
static chat_t s_chat[CHAT_LINES];
static int s_nchat;
static int s_chat_cur[2] = {-1, -1};  // the line that each board writes now
static int64_t s_chat_last[2];

static void put32(uint8_t *b, uint32_t v)
{
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

// Each board writes on its own line; when the two boards key at the same time, the two
// lines grow side by side. A new line starts when the line is full or after 4 s.
static void chat_add(int who, char c)
{
#ifdef MORSE_TEST_HOOK
    MORSE_TEST_HOOK(who, c);
#endif
    int i = s_chat_cur[who];
    bool fresh = i < 0 || (int)strlen(s_chat[i].txt) >= CHAT_COLS || s_now - s_chat_last[who] > 4000;
    if (c == ' ' && (fresh || !s_chat[i].txt[0])) {
        return;  // no space at the start of a line
    }
    if (fresh) {
        if (s_nchat == CHAT_LINES) {
            memmove(s_chat, s_chat + 1, sizeof(chat_t) * (CHAT_LINES - 1));
            s_nchat--;
            for (int k = 0; k < 2; k++) {
                if (s_chat_cur[k] >= 0) {
                    s_chat_cur[k]--;
                }
            }
        }
        i = s_nchat++;
        s_chat[i].txt[0] = 0;
        s_chat[i].who = (uint8_t)who;
        s_chat_cur[who] = i;
    }
    int n = (int)strlen(s_chat[i].txt);
    if (n < CHAT_COLS) {
        s_chat[i].txt[n] = c;
        s_chat[i].txt[n + 1] = 0;
    }
    s_chat_last[who] = s_now;
}

static void link_send_keys(void)
{
    uint8_t b[7 + EDGE_KEEP * 5];
    uint32_t n = s_my_nedges - s_link_base;
    if (n > EDGE_KEEP) {
        n = EDGE_KEEP;
    }
    b[0] = (uint8_t)s_my_nedges;
    b[1] = (uint8_t)(s_my_nedges >> 8);
    put32(b + 2, (uint32_t)s_now);
    b[6] = (uint8_t)n;
    for (uint32_t i = 0; i < n; i++) {
        const edge_t *e = &s_my_edges[(s_my_nedges - n + i) % EDGE_KEEP];
        b[7 + 5 * i] = e->down;
        put32(b + 8 + 5 * i, e->t);
    }
    link_send(APP_ID, K_KEYS, b, (int)(7 + 5 * n));
    s_last_send = s_now;
}

static void link_send_hello(void)
{
    uint8_t b[21];
    memset(b, 0, sizeof(b));
    put32(b, (uint32_t)s_now);
    b[4] = (uint8_t)dec_wpm(&s_me);
    const char *nm = wifi_hotspot_ssid();
    snprintf((char *)b + 5, 16, "%s", nm ? nm : "TFN");
    link_send(APP_ID, K_HELLO, b, sizeof(b));
    s_last_hello = s_now;
}

static void my_edge(bool down, int64_t t)
{
    s_my_edges[s_my_nedges % EDGE_KEEP] = (edge_t){(uint8_t)down, (uint32_t)t};
    s_my_nedges++;
    s_my_last_edge = s_now;
    if (s_state == ST_LINK && s_link_ok) {
        link_send_keys();
    }
}

static void partner_offset(int64_t rx, uint32_t sent)
{
    int64_t sample = rx - (int64_t)sent;
    if (!s_pt.have_offset || sample < s_pt.offset - 2000 || sample > s_pt.offset + 2000) {
        s_pt.offset = sample;  // the first packet, or the other board started again
        s_pt.have_offset = true;
        s_pt.last_offset_fix = rx;
        return;
    }
    if (sample < s_pt.offset) {
        s_pt.offset = sample;
    }
    // the clocks drift a little: let the offset grow 1 ms each second
    if (rx - s_pt.last_offset_fix > 1000) {
        s_pt.offset++;
        s_pt.last_offset_fix = rx;
    }
}

// t: the partner's time of the edge. The edge plays at its time on our clock, PLAY_DELAY
// late. If packets come late, the time from the edge before stays the same (the rhythm
// stays correct, only later); after a pause the delay becomes PLAY_DELAY again.
static void partner_edge(bool down, uint32_t t)
{
    int64_t at = (int64_t)t + s_pt.offset + PLAY_DELAY;
    if (s_pt.have_last) {
        // keep the time from the last edge (a long pause counts as 1.5 s, so that a delay
        // that grew can become short again)
        uint32_t gap = t - s_pt.last_t;
        int64_t rel = s_pt.last_edge_at + (int64_t)(gap < 1500 ? gap : 1500);
        if (at < rel) {
            at = rel;
        }
    }
    if (at < s_now) {
        at = s_now;
    }
    if (at < s_pt.last_edge_at) {
        at = s_pt.last_edge_at;
    }
    s_pt.last_t = t;
    s_pt.have_last = true;
    if (down == s_pt.down) {
        // an edge was lost: put the opposite edge just before this one
        pring_push(at > 1 ? at - 1 : at, !down);
    }
    pring_push(at, down);
    s_pt.down = down;
    s_pt.last_edge_at = at;
}

// no packets while the partner's key is down: the key goes up now
static void partner_force_up(void)
{
    int64_t at = s_now < s_pt.last_edge_at ? s_pt.last_edge_at : s_now;
    pring_push(at, false);
    s_pt.down = false;
    s_pt.last_edge_at = at;
    s_pt.have_last = false;
}

static bool same_mac(const uint8_t *a, const uint8_t *b)
{
    return memcmp(a, b, 6) == 0;
}

static void link_poll(void)
{
    link_msg_t m;
    int guard = 0;
    while (guard++ < 32 && link_recv(&m)) {
        if (m.app != APP_ID) {
            continue;
        }
        // one partner: the first one that comes, or a new one after 5 s of silence
        if (s_pt.present && !same_mac(m.mac, s_pt.mac) && s_now - s_pt.last_rx < 5000) {
            continue;
        }
        if (!s_pt.present && s_pt.have_seq && same_mac(m.mac, s_pt.mac)) {
            // the same board again after a silence: keep its edges and its clock
            s_pt.present = true;
            banner("THE BOARD IS BACK", "", C_CYAN, 1200);
        } else if (!s_pt.present || !same_mac(m.mac, s_pt.mac)) {
            memset(&s_pt, 0, sizeof(s_pt));
            memcpy(s_pt.mac, m.mac, 6);
            snprintf(s_pt.name, sizeof(s_pt.name), "%02X%02X%02X", m.mac[3], m.mac[4], m.mac[5]);
            s_pt.present = true;
            s_tick_flush = true;
            s_ptail_app = s_phead;
            dec_reset(&s_part_dec, s_cwpm);
            sfx_tone(880, 1320, 120, W_SINE, 30);
            banner("A BOARD IS THERE", "YOUR KEY PLAYS ON BOTH BOARDS", C_CYAN, 1800);
        }
        s_pt.last_rx = s_now;
        s_pt.rssi = m.rssi;
        if (m.kind == K_HELLO && m.len >= 21) {
            partner_offset(m.rx_ms, get32(m.data));
            s_pt.wpm = m.data[4];
            char nm[17];
            memcpy(nm, m.data + 5, 16);
            nm[16] = 0;
            if (nm[0]) {
                snprintf(s_pt.name, sizeof(s_pt.name), "%s", nm);
            }
        } else if (m.kind == K_KEYS && m.len >= 7) {
            uint32_t seq = (uint32_t)m.data[0] | ((uint32_t)m.data[1] << 8);
            uint32_t sent = get32(m.data + 2);
            partner_offset(m.rx_ms, sent);
            int n = m.data[6];
            if (n > EDGE_KEEP || 7 + 5 * n > m.len) {
                continue;
            }
            for (int i = 0; i < n; i++) {
                uint32_t es = (seq - (uint32_t)(n - 1 - i)) & 0xFFFF;
                if (s_pt.have_seq && (int16_t)(es - s_pt.last_seq) <= 0) {
                    continue;
                }
                bool down = m.data[7 + 5 * i] != 0;
                uint32_t t = get32(m.data + 8 + 5 * i);
                s_pt.last_seq = es;
                s_pt.have_seq = true;
                if ((int32_t)(sent - t) > 4000) {
                    continue;  // an old edge: not now
                }
                partner_edge(down, t);
            }
        }
    }
}

static void link_update(void)
{
    if (s_link_ok) {
        link_poll();
        if (s_now - s_last_hello > 1000) {
            link_send_hello();
        }
        // repeat the last edges for a short time (a lost packet must not keep a tone on)
        bool recent = s_own_down || s_now - s_my_last_edge < 1500;
        if (recent && s_my_nedges > s_link_base && s_now - s_last_send > 80) {
            link_send_keys();
        }
        if (s_pt.present && s_now - s_pt.last_rx > 4000) {
            s_pt.present = false;
            banner("THE OTHER BOARD IS GONE", "", C_DIM, 1500);
        }
        // no packets for a time while its key is down: let the key go up
        if (s_pt.down && s_now - s_pt.last_rx > 800 && s_now > s_pt.last_edge_at + 600) {
            partner_force_up();
        }
    }
    // the edges of the other board that are due go to its decoder
    uint32_t h = s_phead;
    if (h - s_ptail_app > PRING) {
        s_ptail_app = h - PRING;
    }
    while (s_ptail_app != h && s_pring[s_ptail_app % PRING].at <= s_now) {
        const pedge_t *e = &s_pring[s_ptail_app % PRING];
        dec_key(&s_part_dec, e->down, e->at);
        s_ptail_app++;
    }
    dec_poll(&s_part_dec, s_now, NULL);
    char c;
    while ((c = dec_pop(&s_part_dec)) != 0) {
        chat_add(1, c);
    }
    dec_poll(&s_me, s_now, NULL);
    while ((c = dec_pop(&s_me)) != 0) {
        chat_add(0, c);
    }
}

static void link_open(void)
{
    s_link_ok = link_start();
    s_link_base = s_my_nedges;
    memset(&s_pt, 0, sizeof(s_pt));
    s_tick_flush = true;
    s_ptail_app = s_phead;
    dec_reset(&s_me, s_cwpm);
    dec_reset(&s_part_dec, s_cwpm);
    s_nchat = 0;
    s_chat_cur[0] = s_chat_cur[1] = -1;
    s_last_hello = 0;
    if (!s_link_ok) {
        banner("NO RADIO", "TURN THE WIFI ON IN SETUP", C_RED, 2500);
    }
}

// ---------------------------------------------------------------- the key (touch)

static bool s_key_touch;  // the finger came down on the key

static void key_down(int64_t t)
{
    if (s_own_down) {
        return;
    }
    s_own_down = true;
    s_key_touch = true;
    dec_key(&s_me, true, t);
    my_edge(true, t);
}

static void key_up(int64_t t)
{
    if (!s_own_down) {
        return;
    }
    s_own_down = false;
    s_key_touch = false;
    dec_key(&s_me, false, t);
    my_edge(false, t);
}

// ---------------------------------------------------------------- app

// a run that stops early (BACK, PWR) keeps its best score
static void save_best(void)
{
    if (s_state == ST_COPY && s_score > s_best_copy) {
        s_best_copy = s_score;
        store_set("mor_copy", s_score);
    } else if (s_state == ST_SEND && s_score > s_best_send) {
        s_best_send = s_score;
        store_set("mor_send", s_score);
    }
}

static void go_title(void)
{
    save_best();
    if (s_state == ST_LINK) {
        link_stop();
    }
    s_link_ok = false;
    key_up(s_now);
    s_tick_flush = true;
    s_state = ST_TITLE;
    audio_tune_stop();
}

// The settings panel (sysui.h): THE TITLE. In LEARN, COPY, SEND and LINK it is a hold of 1 s (PI_ACT2: the run, or
// the line with the friend, ends); on the chart and on the result it is one tap (PI_ACT). It does what BACK does
// (go_title: the best score stays, LINK stops the radio as the leave of the app does).
enum {
    MI_TITLE_RUN,
    MI_TITLE
};
static const pitem_t MO_ITEMS[] = {
        {PI_ACT2, MI_TITLE_RUN, 0, 0, {"THE TITLE", "מסך הפתיחה"}, NULL, NULL},
        {PI_ACT, MI_TITLE, 0, 0, {"THE TITLE", "מסך הפתיחה"}, NULL, NULL},
};
static bool mo_item_shown(int id)
{
    const bool run = s_state == ST_LEARN || s_state == ST_COPY || s_state == ST_SEND || s_state == ST_LINK;
    if (id == MI_TITLE_RUN) {
        return run;
    }
    return s_state == ST_CHART || s_state == ST_RESULT;
}
static void mo_item_set(int id, int v)
{
    (void)id;
    (void)v;
    go_title();
    s_press = false;
    s_aim = -1;
}
static const pitems_t MO_PANEL = {
        MO_ITEMS, sizeof MO_ITEMS / sizeof MO_ITEMS[0], mo_item_shown, NULL, mo_item_set, NULL, NULL};

static void morse_init(void)
{
    s_cwpm = (int)store_get("mor_wpm", 18);
    if (s_cwpm < 10 || s_cwpm > 30) {
        s_cwpm = 18;
    }
    s_koch_n = (int)store_get("mor_koch", 2);
    if (s_koch_n < 2 || s_koch_n > N_KOCH) {
        s_koch_n = 2;
    }
    s_best_copy = store_get("mor_copy", 0);
    s_best_send = store_get("mor_send", 0);
}

static void morse_enter(int64_t now)
{
    s_now = s_prev = now;
    s_state = ST_TITLE;
    s_own_down = false;
    s_key_touch = false;
    s_tick_flush = true;
    s_banner_until = 0;
    memset(s_part, 0, sizeof(s_part));
    s_target[0] = 0;
    dec_reset(&s_me, s_cwpm);
    audio_set_tick(morse_tick);
    sysui_panel_items(&MO_PANEL);
}

static void morse_leave(void)
{
    save_best();
    if (s_state == ST_LINK) {
        link_stop();
    }
    s_link_ok = false;
    s_own_down = false;
    s_tick_flush = true;
    audio_tune_stop();
    audio_set_tick(stop_tick);
}

static void start_mode(mstate_t m)
{
    sfx_play(SFX_CLICK);
    if (m != ST_CHART) {
        tune_play(TUNE_MORSE);  // a new game; the chart is not a game
    }
    s_own_down = false;
    s_press = false;
    s_aim = -1;
    s_lpage = 0;
    s_chpage = 0;
    if (m == ST_LEARN) {
        s_state = ST_LEARN;
        learn_start();
    } else if (m == ST_COPY || m == ST_SEND) {
        run_start(m);
    } else if (m == ST_LINK) {
        s_state = ST_LINK;
        link_open();
    } else {
        s_state = ST_CHART;
    }
}

// LEARN: the press shows the letter, the lift answers (THE PRESS); true when the event is used
static bool learn_press(const input_event_t *ev, int x, int y)
{
    if (ev->kind == IN_DOWN) {
        rc_t g = learn_area();
        s_press = x >= g.x - 10 && x < g.x + g.w + 10 && y >= g.y - 10 && y < g.y + g.h + 10;
        s_aim = s_press ? learn_at(x, y) : -1;
        if (s_press) {
            return true;
        }
    } else if (ev->kind == IN_MOVE && s_press) {
        s_aim = learn_at(x, y);
        return true;
    } else if (ev->kind == IN_UP && s_press) {
        s_press = false;
        s_aim = -1;
        int i = learn_at(x, y);
        if (i >= 0) {
            learn_answer(s_koch_set[i]);
        }
        return true;
    }
    return false;
}

// the telegraph key: a press that starts on the key, with the event times; true when the event is used
static bool key_input(const input_event_t *ev, int x, int y)
{
    if (ev->kind == IN_DOWN && in_rc(KEY_RC, x, y)) {
        key_down(ev->t_ms);
        return true;
    }
    if (ev->kind == IN_UP && s_key_touch) {
        key_up(ev->t_ms);
        return true;
    }
    if (ev->kind == IN_TAP && in_rc(KEY_RC, x, y)) {
        return true;
    }
    return false;
}

static void tap_title(int x, int y)
{
    if (in_rc(T_MINUS_RC, x, y) || in_rc(T_PLUS_RC, x, y)) {
        int d = in_rc(T_MINUS_RC, x, y) ? -2 : 2;
        int w = s_cwpm + d;
        if (w >= 10 && w <= 30) {
            s_cwpm = w;
            store_set("mor_wpm", (uint32_t)s_cwpm);
            play_text("PARIS", s_cwpm, 0);
        }
        sfx_play(SFX_CLICK);
        return;
    }
    for (int i = 0; i < 5; i++) {
        if (in_rc(title_btn(i), x, y)) {
            static const mstate_t M[5] = {ST_LEARN, ST_COPY, ST_SEND, ST_LINK, ST_CHART};
            start_mode(M[i]);
            return;
        }
    }
}

static void tap_learn(int x, int y)
{
    if (s_intro) {
        if (in_rc(L_REPLAY_RC, x, y)) {
            char t[2] = {s_intro_c, 0};
            play_text(t, s_cwpm, 0);
            return;
        }
        // a tap closes the card after the letter played
        if (s_intro_plays >= 1) {
            s_intro = false;
            s_learn_next = s_now + 350;
            sfx_play(SFX_CLICK);
        }
        return;
    }
    if (in_rc(L_REPLAY_RC, x, y)) {
        if (s_learn_wait) {
            char t[2] = {s_learn_cur, 0};
            play_text(t, s_cwpm, 0);
        }
        return;
    }
    for (int p = 0; p < learn_pages() && learn_pages() > 1; p++) {
        if (in_rc(learn_tab(p), x, y) && p != s_lpage) {
            s_lpage = p;
            sfx_play(SFX_TICK);
            return;
        }
    }
    // (the letters answer at the lift: learn_press)
}

// a key of the keyboard of COPY
static void copy_key(char c)
{
    if (c == K_DEL) {
        copy_del();
    } else if (c == K_ENTER) {
        copy_enter();
    } else if (c == K_PAGE) {
        s_digits = !s_digits;
        sfx_play(SFX_CLICK);
    } else if (c == K_CLR) {
        if (!s_fb) {  // no change while the answer shows
            s_ntyped = 0;
            s_typed[0] = 0;
        }
    } else if (c) {
        copy_type(c);
    }
}

static void tap_copy(int x, int y)
{
    if (in_rc(C_REPLAY_RC, x, y)) {
        if (!s_fb && !s_play_at && s_target[0]) {
            s_replayed = true;
            play_text(s_target, copy_cwpm(), copy_ewpm());
        }
        return;
    }
#if BOARD_BAR_SCREEN
    if (in_rc(C_PAGE_RC, x, y)) {
        s_digits = !s_digits;
        sfx_play(SFX_CLICK);
        return;
    }
#endif
    for (int i = 0; i < KB_N; i++) {
        if (in_rc(kb_cell(i), x, y)) {
            copy_key(kb_char(i));
            return;
        }
    }
}

static void tap_send(int x, int y)
{
    if (in_rc(S_LISTEN_RC, x, y)) {
        if (s_level >= 2) {
            s_hint_on = true;  // the help costs a little
        }
        play_text(s_target + 0, s_cwpm, 0);
        return;
    }
    // a tap on the word shows its codes
    if (!in_rc(KEY_RC, x, y) && x < PANEL_W && y > TOP_H && y < TOP_H + 70) {
        s_hint_on = true;
    }
}

static void tap_chart(int x, int y)
{
    if (in_rc(CH_PREV_RC, x, y) || in_rc(CH_NEXT_RC, x, y)) {
        int pg = s_chpage + (in_rc(CH_NEXT_RC, x, y) ? 1 : -1);
        if (pg >= 0 && pg < CH_PAGES) {
            s_chpage = pg;
            sfx_play(SFX_TICK);
        }
        return;
    }
    rc_t g = CH_GRID_RC;
    if (in_rc(g, x, y)) {
        int c = (x - g.x) * CH_COLS / g.w, r = (y - g.y) * CH_ROWS / g.h;
        int i = s_chpage * CH_COLS * CH_ROWS + r * CH_COLS + c;
        if (i >= 0 && i < N_CODES) {
            char t[2] = {CODES[i].c, 0};
            play_text(t, s_cwpm, 0);
        }
    }
}

static void morse_input(const input_event_t *ev)
{
#if BOARD_UI_TABLET
    int x = ev->x * 2 / 3, y = ev->y * 2 / 3;  // (the Tab5 view: the touch in the logical frame)
#elif BOARD_ROUND
    int x = (int)((float)ev->x / rv_k()), y = (int)((float)ev->y / rv_k());  // (the round view: the square)
#else
    int x = ev->x, y = ev->y;
#endif
    bool key_mode = s_state == ST_SEND || s_state == ST_LINK;

    if (s_state == ST_LEARN && !s_intro && learn_press(ev, x, y)) {
        return;
    }
    if (key_mode && key_input(ev, x, y)) {
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }

    if (s_state != ST_TITLE && s_state != ST_RESULT && in_rc(BACK_RC, x, y)) {
        sfx_play(SFX_BACK);
        go_title();
        return;
    }
    switch (s_state) {
    case ST_TITLE:
        tap_title(x, y);
        break;
    case ST_LEARN:
        tap_learn(x, y);
        break;
    case ST_COPY:
        tap_copy(x, y);
        break;
    case ST_SEND:
        tap_send(x, y);
        break;
    case ST_CHART:
        tap_chart(x, y);
        break;
    case ST_RESULT:
        if (in_rc(R_AGAIN_RC, x, y)) {
            start_mode(s_result_of);
        } else if (in_rc(R_MENU_RC, x, y)) {
            sfx_play(SFX_BACK);
            go_title();
        }
        break;
    default:
        break;
    }
}

static void morse_update(int64_t now)
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
    case ST_COPY:
        copy_update();
        break;
    case ST_SEND:
        send_update();
        break;
    case ST_LINK:
        link_update();
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
    s_shake_px = s_now < s_shake_until ? (float)(((s_now / 30) & 1) ? 3 : -3) : 0;
}

static bool morse_awake(void)
{
    // the other board can send while this one is not touched
    return (s_state == ST_LINK && s_pt.present) || s_now < s_play_end;
}

// ---------------------------------------------------------------- drawing

// The frame buffer is in PSRAM, and the slow part of the drawing is a cache miss (64 bytes
// from PSRAM and back). A page that fills the screen and then paints panels over it touches
// each line two or three times, and the lines leave the 64 KB cache between the passes. So
// morse_render() draws the page in horizontal bands that fit in the cache: the page code
// runs once for each band, and these local functions clip all the drawing to the band. Thus
// each line of the frame comes from PSRAM one time. The functions copy the math of gfx.c, so
// the pixels are the same.

// 15 KB bands: the LCD task (core 1) reads the other frame through the same cache at the
// same time, so a band must use only a part of the cache.
#ifndef BAND  // (a test can set it)
#if BOARD_BAR_SCREEN
#define BAND 12  // rows: 12 x 1280 bytes = 15 KB
#else
#define BAND 24  // rows: 24 x 640 bytes = 15 KB
#endif
#endif

static int s_by0 = 0, s_by1 = 1 << 30;  // the band now: rows s_by0 .. s_by1 - 1

// true if rows y .. y + h - 1 touch the band (to skip the work of items in other bands)
#if BOARD_UI_TABLET
// THE TAB5 VIEW: the code draws a logical frame of 427 x 240 (the 320 x 240 layout, wider: see SW and
// MDX). The functions below put it on the 640 x 360 frame of the Tab5 at 1.5 times, the same on the two axes, so a
// circle stays round. The text has a whole scale (1 -> 2, 2 -> 3, 3 -> 4) and keeps its middle point.
// morse_input() takes the touch back to the logical frame.
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
// text has a whole scale (1 -> 2; others: the scale x k) and keeps its middle point.
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

// as gfx_fill_ellipse, only the rows of the band
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
        int dx = f > 0 ? (int)(rx * sqrtf(f) + 0.5f) : 0;
        gfx_fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, c);
    }
}
#endif

static void CIRC(int cx, int cy, int r, uint16_t c)
{
    ELL(cx, cy, r, r, c);
}

static int isqrt_i(int64_t v)
{
    if (v <= 0) {
        return 0;
    }
    int r = (int)sqrtf((float)v);
    while ((int64_t)r * r > v) {
        r--;
    }
    while ((int64_t)(r + 1) * (r + 1) <= v) {
        r++;
    }
    return r;
}

// as gfx_ring, only the rows of the band
#if BOARD_ROUND
static void RING(int cx, int cy, int r_out, int r_in, uint16_t c)
{
    if (r_out < 0) {
        return;
    }
    int y0 = rv(cy - r_out) - 1, y1 = rv(cy + r_out + 1) + 1;
    if (y0 < s_by0) {
        y0 = s_by0;
    }
    if (y1 > s_by1) {
        y1 = s_by1;
    }
    for (int py = y0; py < y1; py++) {
        int xa, xb, ia, ib;
        if (!rv_span(cx, cy, r_out, r_out, py, &xa, &xb)) {
            continue;
        }
        if (r_in > 0 && rv_span(cx, cy, r_in - 1, r_in - 1, py, &ia, &ib)) {
            gfx_fill_rect(xa, py, ia - xa, 1, c);
            gfx_fill_rect(ib, py, xb - ib, 1, c);
        } else {
            gfx_fill_rect(xa, py, xb - xa, 1, c);
        }
    }
}
#elif BOARD_UI_TABLET
static void RING(int cx, int cy, int r_out, int r_in, uint16_t c)
{
    if (r_out < 0) {
        return;
    }
    int y0 = (int)floorf(1.5f * (float)(cy - r_out)), y1 = (int)ceilf(1.5f * (float)(cy + r_out + 1));
    if (y0 < s_by0) {
        y0 = s_by0;
    }
    if (y1 > s_by1) {
        y1 = s_by1;
    }
    for (int py = y0; py < y1; py++) {
        int xa, xb, ia, ib;
        if (!tv_span(cx, cy, r_out, r_out, py, &xa, &xb)) {
            continue;
        }
        if (r_in > 0 && tv_span(cx, cy, r_in - 1, r_in - 1, py, &ia, &ib)) {
            gfx_fill_rect(xa, py, ia - xa, 1, c);
            gfx_fill_rect(ib, py, xb - ib, 1, c);
        } else {
            gfx_fill_rect(xa, py, xb - xa, 1, c);
        }
    }
}
#else
static void RING(int cx, int cy, int r_out, int r_in, uint16_t c)
{
    for (int dy = -r_out; dy <= r_out; dy++) {
        int y = cy + dy;
        if (y < s_by0 || y >= s_by1 || y < 0 || y >= gfx_h) {
            continue;
        }
        int xo = isqrt_i((int64_t)r_out * r_out - (int64_t)dy * dy);
        if (dy > -r_in && dy < r_in) {
            int xi = isqrt_i((int64_t)r_in * r_in - (int64_t)dy * dy);
            gfx_fill_rect(cx - xo, y, xo - xi, 1, c);
            gfx_fill_rect(cx + xi + 1, y, xo - xi, 1, c);
        } else {
            gfx_fill_rect(cx - xo, y, 2 * xo + 1, 1, c);
        }
    }
}
#endif

// The 5 x 7 font of gfx.c, read back from the frame buffer once (gfx.h has no function that
// gives the glyph bits). Each row: bit 4 = the left column.
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

// as gfx_text: runs of set bits as one rectangle; only the glyph rows in the band
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
    if (gfx_text_width(label, 2) > r.w - 10 || r.h < 34) {
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

// a dot or a dash: u = the height in px
static int code_width(const char *code, int u)
{
    int w = 0;
    if (!code) {
        return 0;
    }
    for (const char *e = code; *e; e++) {
        w += (*e == '-' ? 3 * u : u) + u;
    }
    return w ? w - u : 0;
}

static void pill(int x, int y, int w, int u, uint16_t c)
{
    if (u >= 5) {
        int r = u / 2;
        CIRC(x + r, y + r, r, c);
        CIRC(x + w - 1 - r, y + r, r, c);
        R(x + r, y, w - 2 * r, u, c);
    } else {
        R(x, y, w, u, c);
    }
}

static int draw_code(int x, int y, const char *code, int u, uint16_t c)
{
    if (!code) {
        return x;
    }
    for (const char *e = code; *e; e++) {
        int w = *e == '-' ? 3 * u : u;
        pill(x, y, w, u, c);
        x += w + u;
    }
    return x;
}

static void draw_code_center(int cx, int y, const char *code, int u, uint16_t c)
{
    draw_code(cx - code_width(code, u) / 2, y, code, u, c);
}

static int s_bar_w = PANEL_W;  // the width of the top bar (the 3.49 LEARN and COPY: the left panel only)

static void top_bar(const char *title, uint16_t accent)
{
    R(0, 0, s_bar_w, TOP_H, C_PANEL);
    R(0, TOP_H - 2, s_bar_w, 2, accent);
    rc_t b = BACK_RC;
    R(b.x + 3, b.y + 3, b.w - 6, b.h - 8, C_PANEL2);
    TXT_IN(b.x, b.w, b.y + (b.h - 16) / 2 + 1, "<", 2, C_WHITE);
    if (title && !BOARD_ROUND) {
        TXT(b.x + b.w + 6, (TOP_H - 14) / 2, title, 2, accent);
    }
}

static void hearts(int x, int y)
{
    for (int i = 0; i < LIVES; i++) {
        uint16_t c = i < s_lives ? C_RED : C_DARK;
        int hx = x + i * 16;
        CIRC(hx + 3, y + 3, 3, c);
        CIRC(hx + 9, y + 3, 3, c);
        for (int j = 0; j < 7; j++) {
            R(hx + j, y + 4 + j, 13 - 2 * j, 1, c);
        }
    }
}

// the brass telegraph key, fitted in r (designed for 240 x 172)
static void draw_key(rc_t r, bool down, int64_t now)
{
    if (!in_band(r.y, r.h)) {
        return;
    }
    R(r.x, r.y, r.w, r.h, rgb(18, 22, 36));
    FRAME(r.x, r.y, r.w, r.h, 1, rgb(40, 48, 74));
    float sx = (float)r.w / 240.0f, sy = (float)r.h / 172.0f;
    float s = sx < sy ? sx : sy;
    int ox = r.x + (int)((float)r.w - 240.0f * s) / 2, oy = r.y + (int)((float)r.h - 172.0f * s) / 2;
#define KX(v) (ox + (int)((v) * s))
#define KY(v) (oy + (int)((v) * s))
#define KS(v) ((int)((v) * s) > 0 ? (int)((v) * s) : 1)
    // the wooden base
    R(KX(18), KY(118), KS(204), KS(34), rgb(96, 58, 32));
    R(KX(18), KY(118), KS(204), KS(5), rgb(140, 92, 54));
    R(KX(18), KY(146), KS(204), KS(6), rgb(64, 38, 22));
    // the pivot posts and the spring post
    R(KX(40), KY(92), KS(18), KS(28), rgb(176, 140, 64));
    R(KX(110), KY(100), KS(10), KS(20), rgb(150, 150, 160));
    // the contact under the knob
    R(KX(168), KY(110), KS(16), KS(9), rgb(200, 200, 210));
    // the lever: it goes down at the knob end
    int drop = down ? 12 : 0;
    for (int i = 0; i <= 10; i++) {
        float f = (float)i / 10.0f;
        int lx = KX(44 + f * 136), ly = KY(88 + f * drop);
        R(lx, ly, KS(15), KS(9), rgb(214, 172, 80));
        R(lx, ly, KS(15), KS(2), rgb(250, 220, 140));
    }
    // the knob
    ELL(KX(184), KY(76 + drop), KS(30), KS(15), rgb(20, 20, 24));
    ELL(KX(176), KY(70 + drop), KS(10), KS(4), rgb(90, 90, 100));
    // a spark at the contact
    if (down) {
        int sx0 = KX(176), sy0 = KY(106);
        for (int i = 0; i < 4; i++) {
            int a = (int)((now / 40 + i * 37) % 360);
            R(sx0 + (int)(fcos((float)a * 0.01745f) * 10 * s), sy0 + (int)(fsin((float)a * 0.01745f) * 5 * s), 2, 2,
              C_AMBER);
        }
    }
    // the lamp
    int lr = KS(13);
    CIRC(KX(40), KY(40), lr + 2, rgb(40, 40, 50));
    CIRC(KX(40), KY(40), lr, down ? C_AMBER : C_AMBER_D);
    if (down) {
        RING(KX(40), KY(40), lr + 7, lr + 4, rgb(255, 140, 40));
    }
#undef KX
#undef KY
#undef KS
#if BOARD_BAR_SCREEN
    TXT_IN(r.x, r.w, r.y + r.h - 12, down ? "BEEEP" : "PRESS HERE", 1, C_DIM);
#elif BOARD_ROUND
    TXT_IN(r.x, r.w, r.y + 4, down ? "BEEEP" : "PRESS HERE", 1, C_DIM);  // (the cap is narrow: one line)
#else
    TXT(r.x + 6, r.y + 10, down ? "BEEEP" : "PRESS HERE", 1, C_DIM);
    TXT(r.x + 6, r.y + 60, "SHORT = DOT", 1, C_DIM);
    TXT(r.x + 6, r.y + 74, "LONG = DASH", 1, C_DIM);
#endif
}

static void draw_parts(void)
{
    for (int i = 0; i < N_PART; i++) {
        const part_t *p = &s_part[i];
        if (p->life <= 0 || !in_band((int)p->y, 3)) {
            continue;
        }
        int sz = p->life > 0.5f ? 3 : 2;
        int x = (int)p->x, y = (int)p->y;
        if (x < 0 || y < 0 || x >= SW - 3 || y >= SH - 3) {
            continue;
        }
        R(x, y, sz, sz, p->c);
    }
}

static void draw_banner(void)
{
    if (s_now >= s_banner_until) {
        return;
    }
#if BOARD_BAR_SCREEN
    int w = 380, h = 52, x = (SW - w) / 2, y = 60;
#else
    int w = BN_W, h = 54, x = BN_X, y = BN_Y;
#endif
    R(x, y, w, h, rgb(10, 12, 22));
    FRAME(x, y, w, h, 2, s_banner_col);
    text_fit(x + 4, w - 8, y + 8, s_banner, 3, s_banner_col);
    if (s_banner2[0]) {
        text_fit(x + 4, w - 8, y + 36, s_banner2, 1, C_WHITE);
    }
}

// the elements of the letter now, and a bar for the time to the end of the letter
static void draw_live(const dec_t *d, int x, int y, int w, int u, uint16_t c)
{
    int cx = x;
    for (int i = 0; i < d->nel; i++) {
        int ew = d->el[i] == '-' ? 3 * u : u;
        if (cx + ew > x + w) {
            break;
        }
        pill(cx, y, ew, u, c);
        cx += ew + u;
    }
    if (d->down) {
        // the element that is down now grows with the time
        float len = (float)(s_now - d->down_at) / dec_dot(d) * (float)u;
        int lw = (int)len < 3 * u ? (int)len : 3 * u;
        if (lw < 2) {
            lw = 2;
        }
        if (cx + lw <= x + w) {
            pill(cx, y, lw, u, len > d->thr / dec_dot(d) * (float)u ? C_WHITE : c);
        }
    }
    float f = dec_gap_frac(d, s_now);
    if (f > 0 && f < 1) {
        int bw = 40;
        R(x + w - bw, y + u / 2 - 1, bw, 3, C_DARK);
        R(x + w - bw, y + u / 2 - 1, (int)((float)bw * f), 3, c);
    }
}

// the timing of the last letter (bars) above the ideal timing at the same speed
static void draw_timing(const dec_t *d, int x, int y, int w)
{
    if (d->last_n <= 0) {
        return;
    }
    float dot = dec_dot(d);
    float real = 0, ideal = 0;
    for (int i = 0; i < d->last_n; i++) {
        real += d->last_on[i] + (i ? d->last_off[i] : 0);
        ideal += (d->last_code[i] == '-' ? 3 : 1) * dot + (i ? dot : 0);
    }
    float tot = real > ideal ? real : ideal;
    if (tot <= 0) {
        return;
    }
    float k = (float)(w - 4) / tot;
    float t = 0;
    for (int i = 0; i < d->last_n; i++) {
        if (i) {
            t += d->last_off[i];
        }
        int bx = x + (int)(t * k), bw = (int)(d->last_on[i] * k);
        R(bx, y, bw > 1 ? bw : 1, 5, C_AMBER);
        t += d->last_on[i];
    }
    t = 0;
    for (int i = 0; i < d->last_n; i++) {
        if (i) {
            t += dot;
        }
        float on = (d->last_code[i] == '-' ? 3 : 1) * dot;
        int bx = x + (int)(t * k), bw = (int)(on * k);
        R(bx, y + 8, bw > 1 ? bw : 1, 3, C_DIM);
        t += on;
    }
}

static void render_title(int64_t now)
{
    FILL(C_BG);
    // a ticker of dots and dashes behind the name
    static const char TICK[] = "-- --- .-. ... .   -.-. --- -.. .   ";
    int u = 4, xx = -(int)((now / 12) % 240);
    for (int rep = 0; rep < 8 && xx < SW; rep++) {
        for (const char *e = TICK; *e && xx < SW; e++) {
            if (*e == ' ') {
                xx += 2 * u;
                continue;
            }
            int w = *e == '-' ? 3 * u : u;
            if (xx + w > 0) {
                pill(xx, BOARD_BAR_SCREEN ? 50 : TT_TICK, w, u, C_AMBER_D);
            }
            xx += w + u;
        }
    }
#if BOARD_BAR_SCREEN
    TXT(16 + 3, 10 + 3, "MORSE", 5, rgb(60, 40, 10));
    TXT(16, 10, "MORSE", 5, C_AMBER);
    TXT(190, 14, "TELEGRAPH KEY AND TRAINER", 1, C_WHITE);
    TXT(190, 28, "LEARN BY EAR, THEN SEND", 1, C_DIM);
    TXT(16, 64, "TWO BOARDS: LINK SENDS YOUR KEY TO THE OTHER BOARD (SAME WIFI)", 1, C_DIM);
    TXT(16, 80, "TAP A MODE. THE SPEED IS THE SOUND OF THE LETTERS.", 1, C_DIM);
    // the speed
    button(T_MINUS_RC, "-", NULL, C_AMBER, false);
    button(T_PLUS_RC, "+", NULL, C_AMBER, false);
    char sp[16];
    snprintf(sp, sizeof(sp), "%d WPM", s_cwpm);
    TXT_IN(T_MINUS_RC.x + T_MINUS_RC.w, T_PLUS_RC.x - T_MINUS_RC.x - T_MINUS_RC.w, 14, sp, 2, C_WHITE);
    TXT_IN(T_MINUS_RC.x + T_MINUS_RC.w, T_PLUS_RC.x - T_MINUS_RC.x - T_MINUS_RC.w, 34, "SPEED", 1, C_DIM);
#else
    TXT_C(TT_Y1 + 3, "MORSE", 4, rgb(60, 40, 10));
    TXT_C(TT_Y1, "MORSE", 4, C_AMBER);
    button(T_MINUS_RC, "-", NULL, C_AMBER, false);
    button(T_PLUS_RC, "+", NULL, C_AMBER, false);
    char sp[16];
    snprintf(sp, sizeof(sp), "%d WPM", s_cwpm);
    TXT_IN(T_MINUS_RC.x + T_MINUS_RC.w, T_PLUS_RC.x - T_MINUS_RC.x - T_MINUS_RC.w, 60 + TT_Y2, sp, 2, C_WHITE);
    TXT_IN(T_MINUS_RC.x + T_MINUS_RC.w, T_PLUS_RC.x - T_MINUS_RC.x - T_MINUS_RC.w, 80 + TT_Y2, "SPEED", 1, C_DIM);
#endif
    char sub[5][24];
    snprintf(sub[0], sizeof(sub[0]), BOARD_ROUND ? "%d/%d LETTERS" : "%d OF %d LETTERS", s_koch_n, N_KOCH);
    snprintf(sub[1], sizeof(sub[1]), "BEST %lu", (unsigned long)s_best_copy);
    snprintf(sub[2], sizeof(sub[2]), "BEST %lu", (unsigned long)s_best_send);
    snprintf(sub[3], sizeof(sub[3]), "TWO BOARDS");
    snprintf(sub[4], sizeof(sub[4]), "ALL THE CODES");
    static const char *const LBL[5] = {"LEARN", "COPY", "SEND", "LINK", "CHART"};
    static const uint8_t HUE[5][3] = {
            {100, 232, 120}, {90, 220, 255}, {255, 184, 64}, {200, 120, 255}, {170, 180, 210}};
    for (int i = 0; i < 5; i++) {
        button(title_btn(i), LBL[i], sub[i], rgb(HUE[i][0], HUE[i][1], HUE[i][2]), false);
    }
}

// the grid of the letters of LEARN
static void draw_learn_grid(int64_t now)
{
    for (int i = 0; i < s_koch_n; i++) {
        rc_t r = learn_cell(i);
        if (r.w <= 0 || !in_band(r.y, r.h)) {
            continue;
        }
        char c = s_koch_set[i];
        bool newest = c == KOCH[s_koch_n - 1];
        uint16_t bg = C_PANEL;
        if (s_learn_fb == 1 && c == s_learn_cur) {
            bg = rgb(30, 90, 50);
        }
        if (s_learn_fb == 2 && c == s_fb_ans) {
            bg = rgb(110, 30, 30);
        }
        if (s_learn_fb == 2 && c == s_learn_cur && ((now / 150) & 1)) {
            bg = rgb(30, 90, 50);
        }
        bool aim = s_press && s_aim == i;
        if (aim) {
            bg = C_AMBER_D;
        }
        R(r.x + 2, r.y + 2, r.w - 4, r.h - 4, bg);
        FRAME(r.x + 2, r.y + 2, r.w - 4, r.h - 4, aim ? 3 : 1, aim ? C_AMBER : newest ? C_GREEN : C_LINE);
        char L[2] = {c, 0};
        int sc = r.h >= 40 && r.w >= 40 ? 3 : 2;
        TXT_IN(r.x, r.w, r.y + (r.h - 7 * sc) / 2, L, sc, C_WHITE);
    }
}

// the card of a new letter
static void draw_learn_card(int64_t now)
{
#if BOARD_BAR_SCREEN
    int x = 8, y = 38, w = SW - 16, h = 130;
#else
    int x = CARD_X, y = CARD_Y, w = CARD_W, h = CARD_H;
#endif
    R(x, y, w, h, rgb(14, 22, 20));
    FRAME(x, y, w, h, 2, C_GREEN);
    // the first card has the first two letters, a later card the new letter
    int n = s_koch_n <= 2 ? 2 : 1;
    char cs[2] = {n == 2 ? KOCH[0] : s_intro_c, KOCH[1]};
    bool blink = (now / 400) & 1;
#if BOARD_BAR_SCREEN
    TXT_IN(x, w, y + 10, n == 2 ? "THE FIRST TWO LETTERS" : "A NEW LETTER", 2, C_WHITE);
    for (int i = 0; i < n; i++) {
        int cx = n == 2 ? x + w / 4 + i * w / 2 : x + w / 2;
        char L[2] = {cs[i], 0};
        const char *code = code_of(cs[i]);
        int u = 10;
        int bw = 50 + 10 + code_width(code, u);
        int bx = cx - bw / 2;
        TXT(bx, y + 38, L, 7, C_GREEN);
        draw_code(bx + 60, y + 56, code, u, C_AMBER);
    }
    TXT_IN(x, w, y + 100, "LISTEN TO THE SOUND", 1, C_DIM);
    TXT_IN(x, w, y + 112, s_intro_plays >= 1 ? "TAP TO START" : "", 1, blink ? C_WHITE : C_DIM);
#else
    text_fit(x, w, y + 10, n == 2 ? "THE FIRST TWO LETTERS" : "A NEW LETTER", 2, C_WHITE);
    for (int i = 0; i < n; i++) {
        int ry = n == 2 ? y + 40 + i * 60 : y + 60;
        char L[2] = {cs[i], 0};
        const char *code = code_of(cs[i]);
        int u = 9;
        int bw = 48 + 12 + code_width(code, u);
        int bx = x + (w - bw) / 2;
        TXT(bx, ry, L, 7, C_GREEN);
        draw_code(bx + 60, ry + 20, code, u, C_AMBER);
    }
    TXT_IN(x, w, y + 160, "LISTEN TO THE SOUND", 1, C_DIM);
    TXT_IN(x, w, y + 174, s_intro_plays >= 1 ? "TAP TO START" : "", 2, blink ? C_WHITE : C_DIM);
#endif
    (void)now;
}

static void render_learn(int64_t now)
{
    FILL(C_BG);
    int sh = (int)s_shake_px;
    char t[32];
    snprintf(t, sizeof(t), "LEARN %d", s_koch_n);
    s_bar_w = BOARD_BAR_SCREEN ? L_FB_RC.w : PANEL_W;
    top_bar(NULL, C_GREEN);  // (the 3.49: AGAIN is in the place of the title; the 320 frame: the 20 answers)
    s_bar_w = PANEL_W;
    button(L_REPLAY_RC, "AGAIN", NULL, C_GREEN, false);
    rc_t fb = L_FB_RC;
    R(fb.x, fb.y, fb.w, fb.h, C_PANEL);
    int correct = learn_correct20();
#if BOARD_BAR_SCREEN
    // the feedback panel: the last letter big, its code, and the progress
    if (s_learn_fb) {
        uint16_t c = s_learn_fb == 1 ? C_GREEN : C_RED;
        char L[2] = {s_learn_cur, 0};
        TXT_IN(fb.x + sh, fb.w, fb.y + 8, L, 6, c);
        draw_code_center(fb.x + fb.w / 2 + sh, fb.y + 58, code_of(s_learn_cur), 7, c);
        if (s_learn_fb == 2) {
            snprintf(t, sizeof(t), "NOT %c", s_fb_ans);
            TXT_IN(fb.x, fb.w, fb.y + 74, t, 2, C_RED);
        }
    } else if (s_press && s_aim >= 0) {
        // the letter under the finger: the lift answers it
        char L[2] = {s_koch_set[s_aim], 0};
        TXT_IN(fb.x, fb.w, fb.y + 20, L, 6, C_WHITE);
        TXT_IN(fb.x, fb.w, fb.y + 68, "LIFT TO ANSWER", 1, C_AMBER);
    } else if (s_learn_wait) {
        TXT_IN(fb.x, fb.w, fb.y + 20, "?", 6, C_AMBER);
        TXT_IN(fb.x, fb.w, fb.y + 68, "TAP THE LETTER", 1, C_DIM);
    } else {
        TXT_IN(fb.x, fb.w, fb.y + 34, "LISTEN", 2, C_DIM);
    }
    // the last 20 answers
    for (int i = 0; i < 20; i++) {
        int bx = fb.x + 8 + i * 9;
        uint16_t c = i < s_hist_n ? (s_hist20[(s_hist_i - s_hist_n + i + 40) % 20] ? C_GREEN : C_RED) : C_DARK;
        R(bx, fb.y + fb.h - 30, 7, 7, c);
    }
    snprintf(t, sizeof(t), "%d/20  NEED 18", correct);
    TXT(fb.x + 8, fb.y + fb.h - 18, t, 1, C_DIM);
    // all 41: the part that is open
    R(fb.x + 8, fb.y + fb.h - 6, fb.w - 16, 3, C_DARK);
    R(fb.x + 8, fb.y + fb.h - 6, (fb.w - 16) * s_koch_n / N_KOCH, 3, C_GREEN);
#else
    if (s_learn_fb) {
        uint16_t c = s_learn_fb == 1 ? C_GREEN : C_RED;
        char L[2] = {s_learn_cur, 0};
        TXT(FBL_X + sh, fb.y + 4, L, 4, c);
        draw_code(FBC_X + sh, fb.y + 11, code_of(s_learn_cur), 6, c);
        if (s_learn_fb == 2) {
            snprintf(t, sizeof(t), "NOT %c", s_fb_ans);
            TXT(FBN_X, fb.y + 4, t, 1, C_RED);
        }
    } else if (s_press && s_aim >= 0) {
        char L[2] = {s_koch_set[s_aim], 0};
        TXT(FB_X, fb.y + 8, L, 4, C_WHITE);
        TXT(FB_X + FB_T, fb.y + 10, "LIFT TO", 1, C_AMBER);
        TXT(FB_X + FB_T, fb.y + 22, "ANSWER", 1, C_AMBER);
    } else if (s_learn_wait) {
        TXT(FB_X, fb.y + 8, "?", 4, C_AMBER);
        TXT(FB_X + FB_T, fb.y + 10, "TAP THE LETTER", 1, C_DIM);
        TXT(FB_X + FB_T, fb.y + 22, "THAT YOU HEAR", 1, C_DIM);
    } else {
        TXT(FB_X, fb.y + 14, "LISTEN...", 2, C_DIM);
    }
    // the last 20 answers, in the top bar
    for (int i = 0; i < 20; i++) {
        uint16_t c = i < s_hist_n ? (s_hist20[(s_hist_i - s_hist_n + i + 40) % 20] ? C_GREEN : C_RED) : C_DARK;
        R(HIST_X + (i % 10) * 9, HIST_Y + (i / 10) * 9, 7, 6, c);
    }
    snprintf(t, sizeof(t), "%d/20 - %d", correct, s_koch_n);
    TXT(HIST_X, HIST_Y + 20, t, 1, C_DIM);
    // the page tabs
    for (int p = 0; p < learn_pages() && learn_pages() > 1; p++) {
        rc_t r = learn_tab(p);
        int a = p * L_PER, b = p * L_PER + L_PER - 1;
        if (b >= s_koch_n) {
            b = s_koch_n - 1;
        }
        char lab[4] = {s_koch_set[a], '-', s_koch_set[b], 0};
        button(r, lab, NULL, C_GREEN, p == s_lpage);
    }
#endif
#if BOARD_BAR_SCREEN
    // the page tabs
    for (int p = 0; p < learn_pages() && learn_pages() > 1; p++) {
        rc_t r = learn_tab(p);
        int a = p * L_PER, b = p * L_PER + L_PER - 1;
        if (b >= s_koch_n) {
            b = s_koch_n - 1;
        }
        char lab[4] = {s_koch_set[a], '-', s_koch_set[b], 0};
        button(r, lab, NULL, C_GREEN, p == s_lpage);
    }
#endif
    draw_learn_grid(now);
    if (s_intro) {
        draw_learn_card(now);
    }
    draw_parts();
    draw_banner();
}

static void render_copy(int64_t now)
{
    FILL(C_BG);
    char t[40];
    snprintf(t, sizeof(t), "COPY %d", s_level + 1);
    int sh = (int)s_shake_px;
    int len = (int)strlen(s_target);
#if BOARD_BAR_SCREEN
    s_bar_w = C_KB_RC.x - 2;
    top_bar(NULL, C_CYAN);
    s_bar_w = PANEL_W;
    button(C_REPLAY_RC, "AGAIN", NULL, C_CYAN, false);
    R(0, TOP_H, 230, SH - TOP_H, C_PANEL);
    TXT(8, TOP_H + 6, t, 1, C_CYAN);
    hearts(8, TOP_H + 18);
    snprintf(t, sizeof(t), "%lu", (unsigned long)s_score);
    TXT(226 - gfx_text_width(t, 2), TOP_H + 8, t, 2, C_WHITE);
    int ax = 8, aw = 214, ay = C_ANS_Y + 12;
#else
    top_bar("COPY", C_CYAN);
    button(C_REPLAY_RC, "AGAIN", NULL, C_CYAN, false);
    hearts(HDR2_X, HDR_Y);
    snprintf(t, sizeof(t), "%lu", (unsigned long)s_score);
    TXT(SCORE_R - gfx_text_width(t, 1), HDR_Y, t, 1, C_WHITE);
    snprintf(t, sizeof(t), "L%d  %d/%d", s_level + 1, s_item + 1, COPY_ITEMS);
    TXT(HDR2_X, HDR_Y + 16, t, 1, C_DIM);
    int ax = C_AX, aw = C_AW, ay = C_ANS_Y + 3;
    R(0, C_ANS_Y, SW, C_KB_RC.y - C_ANS_Y, C_PANEL);
#endif
    // the answer: the typed letters, then lines for the rest
    int sc = BOARD_BAR_SCREEN ? 3 : 2;
    while (sc > 1 && len * 6 * sc + 4 > aw) {
        sc--;
    }
    int cw = 6 * sc + 2;
    int x0 = ax + (aw - len * cw) / 2 + sh;
    for (int i = 0; i < len; i++) {
        int cx = x0 + i * cw;
        if (s_target[i] == ' ') {
            continue;
        }
        uint16_t c = C_WHITE;
        if (s_fb == 1) {
            c = C_GREEN;
        }
        if (s_fb == 2) {
            c = s_typed[i] == s_target[i] ? C_GREEN : C_RED;
        }
        if (i < s_ntyped) {
            char L[2] = {s_typed[i], 0};
            TXT(cx, ay, L, sc, c);
        }
        R(cx, ay + 7 * sc + 3, 6 * sc - 1, 2, i == s_ntyped && !s_fb ? C_CYAN : C_DARK);
    }
    if (s_fb == 2) {
#if BOARD_BAR_SCREEN
        TXT_IN(0, 230, ay + 32, s_target, 2, C_GREEN);
#endif
    }
    bool sounding = s_now < s_play_end;
#if BOARD_BAR_SCREEN
    const char *st = s_play_at ? "GET READY" : (sounding ? "LISTEN..." : "TYPE IT");
    TXT(8, 122, st, 1, sounding ? C_AMBER : C_DIM);
    snprintf(t, sizeof(t), "WORD %d/%d", s_item + 1, COPY_ITEMS);
    TXT(8, 138, t, 1, C_DIM);
    snprintf(t, sizeof(t), "%d WPM", copy_cwpm());
    TXT(8, 152, t, 1, C_DIM);
    button(C_PAGE_RC, s_digits ? "ABC" : "123", NULL, C_CYAN, false);
#else
    if (sounding) {
        CIRC(SCORE_R - 10, HDR_Y + 20, 3, C_AMBER);
    }
#endif
    (void)now;
    // the keyboard
    bool full = s_ntyped >= len && len > 0;
    for (int i = 0; i < KB_N; i++) {
        char c = kb_char(i);
        rc_t r = kb_cell(i);
        if (r.w <= 0 || !in_band(r.y, r.h)) {
            continue;
        }
        if (!c) {
            R(r.x + 2, r.y + 2, r.w - 4, r.h - 4, rgb(16, 20, 34));
            continue;
        }
        if (c == K_ENTER) {
            if (i > 0 && kb_char(i - 1) == K_ENTER && i % KB_COLS) {
                continue;  // (one key over its cells)
            }
            int w = r.w;
            for (int j = i + 1; j < KB_N && j % KB_COLS && kb_char(j) == K_ENTER; j++) {
                w += kb_cell(j).w;
            }
            R(r.x + 2, r.y + 2, w - 4, r.h - 4, full && !s_fb ? rgb(30, 110, 60) : C_PANEL2);
            FRAME(r.x + 2, r.y + 2, w - 4, r.h - 4, 1, full ? C_GREEN : C_LINE);
            TXT_IN(r.x, w, r.y + (r.h - 14) / 2, w >= 70 ? "ENTER" : "OK", 2, full ? C_WHITE : C_DIM);
            continue;
        }
        bool sp = c == K_DEL || c == K_CLR || c == K_PAGE;
        R(r.x + 2, r.y + 2, r.w - 4, r.h - 4, sp ? C_PANEL2 : C_PANEL);
        FRAME(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 1, C_LINE);
        if (sp) {
            const char *nm = c == K_DEL ? "DEL" : c == K_CLR ? "CLR" : (s_digits ? "ABC" : "123");
            TXT_IN(r.x, r.w, r.y + (r.h - 7) / 2, nm, 1, C_WHITE);
        } else {
            char L[2] = {c, 0};
            int s2 = r.h >= 44 ? 3 : 2;
            TXT_IN(r.x, r.w, r.y + (r.h - 7 * s2) / 2, L, s2, C_WHITE);
        }
    }
#if !BOARD_BAR_SCREEN
    if (s_fb == 2 && in_band(C_KB_RC.y, 30)) {
        // the right word, over the first row of keys (no key acts while the answer shows)
        R(0, C_KB_RC.y, SW, 30, C_PANEL);
        TXT_IN(0, SW, C_KB_RC.y + 8, s_target, 2, C_GREEN);
    }
#endif
    draw_parts();
    draw_banner();
}

static void render_send(int64_t now)
{
    FILL(C_BG);
    char t[48];
    snprintf(t, sizeof(t), "SEND %d", s_level + 1);
    top_bar(BOARD_BAR_SCREEN ? t : "SEND", C_AMBER);
    button(S_LISTEN_RC, "LISTEN", NULL, C_AMBER, false);
#if BOARD_BAR_SCREEN
    hearts(170, 12);
    snprintf(t, sizeof(t), "%lu", (unsigned long)s_score);
    TXT(290 - gfx_text_width(t, 2), 10, t, 2, C_WHITE);
#else
    hearts(HDR2_X, HDR_Y);
    snprintf(t, sizeof(t), "%lu", (unsigned long)s_score);
    TXT(SCORE_R - gfx_text_width(t, 1), HDR_Y, t, 1, C_WHITE);
    snprintf(t, sizeof(t), "L%d  %d/%d", s_level + 1, s_item + 1, SEND_ITEMS);
    TXT(HDR2_X, HDR_Y + 16, t, 1, C_DIM);
#endif
    int sh = (int)s_shake_px;
    // the word: done letters green, the letter now amber
    int len = (int)strlen(s_target);
#if BOARD_BAR_SCREEN
    int ty = 44, sc = 4, u = 4;
#else
    int ty = S_TY, sc = 3, u = 3;
#endif
    int cell[12], tot = 0;
    for (int i = 0; i < len; i++) {
        const char *code = code_of(s_target[i]);
        int w = 6 * sc + 6;
        if (s_hint_on && code && code_width(code, u) + 8 > w) {
            w = code_width(code, u) + 8;
        }
        if (s_target[i] == ' ') {
            w = 6 * sc;
        }
        cell[i] = w;
        tot += w;
    }
    int x = (PANEL_W - tot) / 2 + sh;
    for (int i = 0; i < len; i++) {
        char L[2] = {s_target[i], 0};
        uint16_t c = i < s_pos ? C_GREEN : (i == s_pos ? C_AMBER : C_WHITE);
        if (s_fb == 2 && i == s_pos) {
            c = C_RED;
        }
        TXT_IN(x, cell[i], ty, L, sc, c);
        const char *code = code_of(s_target[i]);
        if (s_hint_on && code) {
            draw_code_center(x + cell[i] / 2, ty + 7 * sc + 6, code, u, i < s_pos ? C_GREEN : C_AMBER_D);
        }
        if (i == s_pos && !s_fb && ((now / 300) & 1)) {
            R(x + 4, ty - 5, cell[i] - 8, 2, C_AMBER);
        }
        x += cell[i];
    }
#if BOARD_BAR_SCREEN
    int ly = 110, tyy = 134;
#else
    int ly = S_LY, tyy = S_TYY;
#endif
    if (s_fb == 2) {
        snprintf(t, sizeof(t), "YOU SENT %c", s_wrong_c);
        TXT(S_X, ly, t, 2, C_RED);
        draw_code(S_X + gfx_text_width(t, 2) + 10, ly + 3, s_wrong_sent, 5, C_RED);
        snprintf(t, sizeof(t), "NEED %c", s_need_c);
        TXT(S_X, ly + 22, t, 2, C_GREEN);
        draw_code(S_X + gfx_text_width(t, 2) + 10, ly + 25, s_wrong_need, 5, C_GREEN);
    } else {
        draw_live(&s_me, S_X, ly, PANEL_W - 2 * S_X, BOARD_BAR_SCREEN ? 8 : 7, C_AMBER);
        draw_timing(&s_me, S_X, tyy, PANEL_W / 2);
        if (s_me.last_n) {
            TXT(PANEL_W / 2 + 14, tyy, "YOU / IDEAL", 1, C_DIM);
        }
        snprintf(t, sizeof(t), "%d WPM", dec_wpm(&s_me));
#if BOARD_BAR_SCREEN
        TXT(PANEL_W - 8 - gfx_text_width(t, 1), 160, t, 1, C_DIM);
        if (s_me.last_rhythm >= 0) {
            snprintf(t, sizeof(t), "RHYTHM %d%%", (int)(s_me.last_rhythm * 100));
            TXT(8, 160, t, 1, s_me.last_rhythm > 0.7f ? C_GREEN : C_AMBER);
        }
#else
        TXT(PANEL_W - S_X - gfx_text_width(t, 1), S_WY, t, 1, C_DIM);
        if (s_me.last_rhythm >= 0) {
            snprintf(t, sizeof(t), "RHYTHM %d%%", (int)(s_me.last_rhythm * 100));
            TXT(S_X, S_WY, t, 1, s_me.last_rhythm > 0.7f ? C_GREEN : C_AMBER);
        }
#endif
    }
    draw_key(KEY_RC, s_own_down, now);
    draw_parts();
    draw_banner();
}

static void render_link(int64_t now)
{
    FILL(C_BG);
    char t[48];
    top_bar("LINK", rgb(200, 120, 255));
    // the state of the radio and the other board
    int sx = BOARD_ROUND ? BACK_RC.x + BACK_RC.w + 8 : BACK_RC.w + 70;
    if (!s_link_ok) {
        TXT(sx, (TOP_H - 7) / 2, "NO RADIO: WIFI OFF", 1, C_RED);
    } else if (s_pt.present) {
        snprintf(t, sizeof(t), "WITH %s", s_pt.name);
        TXT(sx, LK_Y + 6, t, 1, C_CYAN);
        int bars = s_pt.rssi > -55 ? 4 : s_pt.rssi > -67 ? 3 : s_pt.rssi > -78 ? 2 : 1;
        for (int i = 0; i < 4; i++) {
            R(sx + i * 5, LK_Y + 26 - i * 3, 3, 4 + i * 3, i < bars ? C_CYAN : C_DARK);
        }
        snprintf(t, sizeof(t), "CH %d", link_channel());
        TXT(sx + 26, LK_Y + 20, t, 1, C_DIM);
    } else {
        int dots = (int)((now / 300) % 4);
        snprintf(t, sizeof(t), "LOOKING%.*s", dots, "...");
        TXT(sx, LK_Y + 6, t, 1, C_AMBER);
        snprintf(t, sizeof(t), BOARD_ROUND ? "CH %d - SAME WIFI" : "CH %d - SAME WIFI ON BOTH", link_channel());
        TXT(sx, LK_Y + 20, t, 1, C_DIM);
    }
    // the chat
#if BOARD_BAR_SCREEN
    int cy = TOP_H + 6, lh = 20;
#else
    int cy = TOP_H + 4, lh = 20;
#endif
    int first = s_nchat > CHAT_SHOW ? s_nchat - CHAT_SHOW : 0;
    if (s_nchat == 0) {
        TXT(S_X, cy + 4, s_pt.present ? "KEY A MESSAGE. THE OTHER BOARD" : "KEY ON THE BRASS KEY. TEXT", 1, C_DIM);
        TXT(S_X, cy + 18, s_pt.present ? "HEARS IT AND WRITES IT HERE." : "SHOWS HERE AS YOU SEND.", 1, C_DIM);
    }
    for (int i = first; i < s_nchat; i++) {
        const chat_t *l = &s_chat[i];
        uint16_t c = l->who ? C_CYAN : C_AMBER;
        R(S_X - 6, cy + 2, 3, 12, c);
        TXT(S_X + 2, cy, l->txt, 2, c);
        cy += lh;
    }
    // the letters that come now
#if BOARD_BAR_SCREEN
    int ly = 130;
#else
    int ly = LK_LY;
#endif
    draw_live(&s_me, S_X, ly, PANEL_W / 2 - 8 - S_X, 7, C_AMBER);
    draw_live(&s_part_dec, PANEL_W / 2 + 8, ly, PANEL_W / 2 - 16, 7, C_CYAN);
#if BOARD_BAR_SCREEN
    snprintf(t, sizeof(t), "YOU %d WPM", dec_wpm(&s_me));
    TXT(8, 150, t, 1, C_DIM);
    if (s_pt.present) {
        snprintf(t, sizeof(t), "OTHER %d WPM", dec_wpm(&s_part_dec));
        TXT(PANEL_W / 2 + 8, 150, t, 1, C_DIM);
    }
#endif
    // the lamp of the other board
    if (s_part_dec.down) {
        CIRC(PANEL_W - 8 - S_X, ly + 3, 6, C_CYAN);
    }
    draw_key(KEY_RC, s_own_down, now);
    draw_parts();
    draw_banner();
}

static void render_chart(int64_t now)
{
    FILL(C_BG);
    top_bar(BOARD_BAR_SCREEN ? "CHART" : NULL, rgb(170, 180, 210));
#if BOARD_BAR_SCREEN
    TXT(BACK_RC.w + 100, (TOP_H - 7) / 2, "TAP A CODE TO HEAR IT", 1, C_DIM);
#endif
    button(CH_PREV_RC, s_chpage > 0 ? "<" : "", NULL, C_DIM, false);
    button(CH_NEXT_RC, s_chpage < CH_PAGES - 1 ? ">" : "", NULL, C_DIM, false);
    char pt[16];
    snprintf(pt, sizeof pt, "%d/%d", s_chpage + 1, CH_PAGES);
#if BOARD_BAR_SCREEN
    TXT(CH_PREV_RC.x - 6 - gfx_text_width(pt, 2), (TOP_H - 14) / 2, pt, 2, C_DIM);
#elif !BOARD_ROUND
    TXT(BACK_RC.x + BACK_RC.w + 8, (TOP_H - 14) / 2, pt, 2, C_DIM);
#else
    TXT_C(CH_GRID_RC.y + CH_GRID_RC.h + 10, pt, 2, C_DIM);
#endif
    rc_t g = CH_GRID_RC;
    for (int k = 0; k < CH_COLS * CH_ROWS; k++) {
        int i = s_chpage * CH_COLS * CH_ROWS + k;
        if (i >= N_CODES) {
            break;
        }
        int r = k / CH_COLS, c = k % CH_COLS;
        int x0 = g.x + c * g.w / CH_COLS, y0 = g.y + r * g.h / CH_ROWS;
        int w = g.w / CH_COLS, h = g.h / CH_ROWS;
        if (!in_band(y0, h)) {
            continue;
        }
        R(x0 + 1, y0 + 1, w - 2, h - 2, (r + c) & 1 ? C_PANEL : rgb(20, 26, 44));
        char L[2] = {CODES[i].c, 0};
        TXT(x0 + 4, y0 + (h - 14) / 2, L, 2, C_WHITE);
        // the dot size that fits the cell: a dash is 3 units, a gap 1 unit
        int units = code_width(CODES[i].code, 1);
        int u = (w - 26) / (units > 0 ? units : 1);
        if (u > 5) {
            u = 5;
        }
        if (u < 2) {
            u = 2;
        }
        draw_code(x0 + 20, y0 + (h - u) / 2, CODES[i].code, u, C_AMBER);
    }
    (void)now;
}

static void render_result(int64_t now)
{
    FILL(C_BG);
    char t[40];
    bool copy = s_result_of == ST_COPY;
#if BOARD_BAR_SCREEN
    TXT_C(10, copy ? "COPY OVER" : "SEND OVER", 3, copy ? C_CYAN : C_AMBER);
    snprintf(t, sizeof(t), "SCORE %lu   LEVEL %d", (unsigned long)s_score, s_level + 1);
    TXT_C(44, t, 2, C_WHITE);
    if (s_new_best && ((now / 300) & 1)) {
        TXT_C(68, "NEW BEST!", 2, C_GREEN);
    }
    snprintf(t, sizeof(t), "BEST %lu", (unsigned long)(copy ? s_best_copy : s_best_send));
    TXT_C(90, t, 1, C_DIM);
    if (!copy && s_rhythm_n) {
        snprintf(t, sizeof(t), "RHYTHM %d%%", (int)(s_rhythm_sum * 100.0f / (float)s_rhythm_n));
        TXT_C(102, t, 1, C_DIM);
    }
#else
    TXT_C(RS_Y + 20, copy ? "COPY OVER" : "SEND OVER", 3, copy ? C_CYAN : C_AMBER);
    snprintf(t, sizeof(t), "SCORE %lu", (unsigned long)s_score);
    TXT_C(RS_Y + 64, t, 3, C_WHITE);
    snprintf(t, sizeof(t), "LEVEL %d", s_level + 1);
    TXT_C(RS_Y + 96, t, 2, C_WHITE);
    if (s_new_best && ((now / 300) & 1)) {
        TXT_C(RS_Y + 122, "NEW BEST!", 2, C_GREEN);
    }
    snprintf(t, sizeof(t), "BEST %lu", (unsigned long)(copy ? s_best_copy : s_best_send));
    TXT_C(RS_Y + 146, t, 1, C_DIM);
    if (!copy && s_rhythm_n) {
        snprintf(t, sizeof(t), "RHYTHM %d%%", (int)(s_rhythm_sum * 100.0f / (float)s_rhythm_n));
        TXT_C(RS_Y + 160, t, 1, C_DIM);
    }
#endif
    button(R_AGAIN_RC, "AGAIN", NULL, C_GREEN, false);
    button(R_MENU_RC, "MODES", NULL, C_DIM, false);
}

static void morse_render(int64_t now)
{
    font_capture();
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
        case ST_COPY:
            render_copy(now);
            break;
        case ST_SEND:
            render_send(now);
            break;
        case ST_LINK:
            render_link(now);
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
// the menu icon: a telegraph key sends "dot dash dot"; each sign lights in the line above when the key is down
static void morse_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0x2f80ff), tfi_hex(0x0b1d66));
    float u = s / 100.0f, ph = tfi_ph(2);
    // the signs: dot, dash, dot, dash (start, end in the loop)
    static const float SG[4][2] = {{0.06f, 0.16f}, {0.26f, 0.52f}, {0.62f, 0.72f}, {0.80f, 0.98f}};
    static const float SX[4][2] = {{16, 25}, {31, 53}, {59, 68}, {74, 86}};
    float down = 0;
    for (int k = 0; k < 4; k++) {
        bool on = ph >= SG[k][0] && ph < SG[k][1];
        float done = ph >= SG[k][0] ? 1.0f : 0.25f;
        if (on) {
            down = 1;
        }
        int sx0 = x + (int)(SX[k][0] * u), sw = (int)((SX[k][1] - SX[k][0]) * u);
        if (on) {
            tfd_glow(sx0 + sw / 2.0f, y + 26 * u, 14 * u, tfi_hex(0xffd36b), 200);
        }
        tfi_rbox(sx0, y + (int)(22 * u), sw, (int)(9 * u), (int)(4.5f * u),
                 tfd_mix(tfi_hex(0x31407a), tfi_hex(0xfff3c4), (int)(256 * done)),
                 tfd_mix(tfi_hex(0x25305e), tfi_hex(0xffb020), (int)(256 * done)), 256);
    }
    // the base, the lever (it turns down), the knob
    tfi_rbox(x + (int)(14 * u), y + (int)(70 * u), (int)(72 * u), (int)(14 * u), (int)(4 * u), tfi_hex(0x9a5a2e),
             tfi_hex(0x5e3216), 256);
    float pvx = x + 74 * u, pvy = y + 62 * u, e = 0.12f - 0.18f * down;
    tfi_disc(pvx, pvy + 3 * u, 6 * u, tfi_hex(0xffe08a), tfi_hex(0xb07a20), 256);
    float kx = pvx - fcos(e) * 46 * u, ky = pvy - fsin(e) * 46 * u;
    tfi_line(pvx, pvy + 2 * u, kx, ky + 2 * u, 6 * u, false, 0, 70);
    tfi_line(pvx, pvy, kx, ky, 6 * u, true, tfi_hex(0xffcf5a), 256);
    tfi_line(pvx, pvy - 1.4f * u, kx, ky - 1.4f * u, 1.8f * u, false, tfi_hex(0xfff3c4), 220);
    tfi_drop_disc(kx, ky, 9.5f * u, 3 * u, 80);
    tfi_disc(kx, ky, 9.5f * u, tfi_hex(0x5a6070), tfi_hex(0x15171d), 256);
    tfi_spark(kx - 3 * u, ky - 4 * u, 2.6f * u, 150);
    tfi_end();
}

const app_t app_morse = {
        .name = "MORSE",
        .landscape = true,
        .needs = 0,
        .init = morse_init,
        .enter = morse_enter,
        .leave = morse_leave,
        .input = morse_input,
        .update = morse_update,
        .render = morse_render,
        .draw_icon = morse_icon,
        .awake = morse_awake,
        .round = APP_ROUND_HI,
};
