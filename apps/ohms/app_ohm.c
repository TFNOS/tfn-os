// OHMS (UTIL): resistor codes, on the landscape screen (640 x 172 on the 3.49, 320 x 240 on the CoreS3).
// The task: the app task of TFN OS calls the hooks of app_ohm.
// The memory: the file statics; no heap. The pins of METER belong to board_ohm_* from enter to leave.
//
// - READ: tap a band, then a colour (or drag up and down on a band). The value, the tolerance,
//   the range and the E-series show, with a warning for a band that cannot be there. FLIP reads
//   the part from the other end. 4, 5 or 6 bands.
// - FIND: type a value; the bands, the SMD codes and the nearest standard value show.
// - SMD: type the code of a chip resistor (3 digits, 4 digits, R codes, EIA-96).
// - LEARN: 16 levels, from the colours to full resistors and SMD codes. The next level opens
//   when 18 of the last 20 answers are correct (NVS "ohm_lvl").
// - CALC: the resistor for LEDs, and a voltage divider from standard values.
// - CHART: the colour table, the E6/E12/E24 values and the EIA-96 codes.
// - METER (only a board with free pins: the CoreS3 Port B): measure a resistor against a
//   known reference resistor (board_ohm_*). CAL uses a part with the value of the reference.
//
// Text: '~' draws an omega (ohm) and '`' draws a plus-minus sign.
//
// The Tab5 (640 x 360): the layout of the 320 x 240 frame 1.5 times bigger on the two axes (OX, OY: 480 x 360), with
// bigger text (OS), and at the right a rail of the 6 modes (160 px: a tap goes to that mode). The title screen uses the
// whole width. The Tab5 has no free pins: no METER.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_random.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/settings.h"
#include "tfn/tunes.h"
#include "tfn/mfont.h"
#include "tfn/uikit.h"

// THE ANTI-ALIASED TEXT (the 5 x 7 dot font is rough and hard to read on these screens): the Bungee faces of the
// firmware in place of the dot font. A dot scale k (7 * k px high, 6 * k px a letter) takes the face of about the same
// width (MF_TINY, MF_N16, MF_N24, MF_N34); the capitals keep the middle of the dot line. The big faces have capitals
// only: a small letter shows as its capital. The omega '~' and the plus-minus '`' stay the 5 x 7 glyphs of the app
// (6 * k px wide). These functions take panel points (the round view, ohm_rv.inc, calls them after its mapping).
static const char *const G_OHM[7] = {" ### ", "#   #", "#   #", "#   #", " # # ", " # # ", "## ##"};
static const char *const G_PM[7] = {"  #  ", "  #  ", "#####", "  #  ", "  #  ", "     ", "#####"};
static const mfont_t *ohm_face(int k)
{
    return k <= 1 ? &MF_TINY : k == 2 ? &MF_N16 : k == 3 ? &MF_N24 : &MF_N34;
}
static int ohm_cp(int ch, const mfont_t *f)
{
    return (f != &MF_TINY && ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
}
// the advance of a letter in quarters of a px (a space: 0.3 of the capital height at least; the two glyphs: 6 * k)
static int ohm_adv4(const mfont_t *f, int ch, int k)
{
    if (ch == '~' || ch == '`') {
        return 24 * k;
    }
    const mglyph_t *g = mf_glyph(f, ohm_cp(ch, f));
    if (!g) {
        return 0;
    }
    const int sp = f->asc * 6 / 5;
    return ch == ' ' && g->adv4 < sp ? sp : g->adv4;
}
static int ohm_aa_w(const char *s, int k)
{
    const mfont_t *f = ohm_face(k);
    int w4 = 0;
    for (; *s; s++) {
        w4 += ohm_adv4(f, (uint8_t)*s, k);
    }
    return (w4 + 2) / 4;
}
// the text from x (the left end) in the dot line from y (its top) of scale k
static void ohm_aa_text(int x, int y, const char *s, int k, uint16_t c)
{
    const mfont_t *f = ohm_face(k);
    const int base = y + (7 * k + f->asc) / 2;
    int x4 = x * 4;
    for (; *s; s++) {
        const int ch = (uint8_t)*s, gx0 = (x4 + 2) / 4;
        if (ch == '~' || ch == '`') {
            const char *const *g = ch == '~' ? G_OHM : G_PM;
            for (int r = 0; r < 7; r++) {
                for (int i = 0; i < 5; i++) {
                    if (g[r][i] == '#') {
                        gfx_fill_rect(gx0 + i * k, y + r * k, k, k, c);
                    }
                }
            }
            x4 += 24 * k;
            continue;
        }
        const mglyph_t *g = mf_glyph(f, ohm_cp(ch, f));
        if (!g) {
            continue;
        }
        const int gx = gx0 + g->ox, gy = base + g->oy;
        const uint8_t *src = f->a4 + g->off;
        for (int j = 0; j < g->h; j++) {
            const int py = gy + j;
            if (py < 0 || py >= gfx_h) {
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
        x4 += ohm_adv4(f, ch, k);
    }
}
#include "ohm_rv.inc"

#if BOARD_UI_TABLET
#define HAS_METER 0
#else
#define HAS_METER (!BOARD_BAR_SCREEN)
#endif

// the Tab5: the 320 x 240 layout grows 1.5 times on the two axes (the same scale: no stretch) to 480 x 360; the text
// 2 or 1.5 times. RAIL_X .. 640: the rail of the modes.
#if BOARD_UI_TABLET
#define OX(v)      ((v) * 3 / 2)
#define OY(v)      ((v) * 3 / 2)
#define RAIL_X     480
#define OS(v)      ((v) == 1 ? 2 : (v) * 3 / 2)
#define CALC_ROW_H 54  // (CALC: lower rows, so that the 3 result lines have room)
#define CALC_LH    18
#else
#define OX(v)      (v)
#define OY(v)      (v)
#define OS(v)      (v)
#define CALC_ROW_H 41
#define CALC_LH    11
#endif

// ---------------------------------------------------------------- layout

typedef struct {
    int16_t x, y, w, h;
} rc_t;

#define RC(x, y, w, h) ((rc_t){(int16_t)(x), (int16_t)(y), (int16_t)(w), (int16_t)(h)})

#if BOARD_BAR_SCREEN
#define SW      640
#define SH      172
#define TOP_H   40
#define BACK_W  64
#define BACK_RC RC(0, 0, BACK_W, TOP_H)
#define TX      6  // the left edge of the text
// READ
#define RD_X      8
#define RD_W      364
#define RD_CY     80
#define RD_H      46
#define RD_HIT    RC(0, 42, 380, 78)
#define RD_INFO_X 386
#define RD_INFO_Y 46
#define RD_INFO_W 250
// FIND
#define FD_KP     RC(334, 42, 306, 130)  // 6 x 3 keys of 51 x 43
#define FD_LEFT_W 330
#define FD_STD    RC(0, 104, 330, 16)
// SMD
#define SM_KP   RC(300, 42, 340, 130)  // 5 x 3 keys of 68 x 43 (two pages)
#define SM_CHIP RC(14, 48, 150, 58)
#define RB_DY   40  // SMD: the bands under the value
#define RB_H    18
// LEARN
#define LN_Q    RC(0, 42, 298, 130)
#define LN_A    RC(300, 42, 340, 130)
#define CONF_RC RC(392, 72, 224, 46)  // the confirm button (CLEAR, LEVELS): far from the top bar
#else
#define SW        OX(320)
#define SH        OY(240)
#define TOP_H     OY(44)
#define BACK_W    OX(60)
// (the round panels: the round view, ohm_rv.inc, takes this layout into the circle)
#define BACK_RC   RC(0, 0, BACK_W, TOP_H)
#define TX        OX(6)
#define FD_KP     RC(0, OY(114), OX(320), OY(126))  // 6 x 3 keys of 53 x 42
#define SM_KP     RC(0, OY(114), OX(320), OY(126))  // 5 x 3 keys of 64 x 42 (two pages)
#define LN_A      RC(0, OY(126), OX(320), OY(114))
#define PAL_Y     OY(156)
#define RD_X      OX(4)
#define RD_W      OX(312)
#define RD_CY     OY(68)
#define RD_H      OY(34)
#define RD_HIT    RC(0, OY(46), OX(320), OY(52))
#define RD_INFO_X TX
#define RD_INFO_Y OY(100)
#define RD_INFO_W (SW - 2 * TX)
#define FD_LEFT_W OX(320)
#define FD_STD    RC(0, OY(88), OX(320), OY(14))
#define SM_CHIP   RC(TX, OY(48), OX(104), OY(40))
#define RB_DY     OY(26)
#define RB_H      OY(14)
#define LN_Q      RC(0, OY(46), OX(320), OY(78))
#define CONF_RC   RC(OX(50), OY(112), OX(220), OY(48))  // the confirm button (CLEAR, LEVELS): far from the top bar
#endif

static bool in_rc(rc_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// button i of n at the right end of the top bar (6 px apart; the Watcher: in the circle)
static rc_t bar_btn(int i, int n)
{
#if BOARD_BAR_SCREEN
    return RC(SW - (n - i) * 106 + 6, 0, 100, TOP_H);
#else
    return RC(SW - (n - i) * OX(74) + OX(6), 0, OX(68), TOP_H);
#endif
}

// cell i of a grid of cols x rows in r
static rc_t grid(rc_t r, int cols, int rows, int i)
{
    int c = i % cols, w = r.w / cols, h = r.h / rows;
    return RC(r.x + c * w, r.y + (i / cols) * h, w, h);
}

// ---------------------------------------------------------------- colours of the screen

#define C_BG     rgb(14, 16, 24)
#define C_PANEL  rgb(28, 32, 46)
#define C_PANEL2 rgb(46, 52, 76)
#define C_LINE   rgb(72, 80, 108)
#define C_WHITE  rgb(236, 240, 250)
#define C_DIM    rgb(140, 148, 172)
#define C_DARK   rgb(74, 80, 102)
#define C_COPPER rgb(236, 150, 72)
#define C_GREEN  rgb(100, 230, 120)
#define C_RED    rgb(255, 92, 92)
#define C_AMBER  rgb(255, 196, 72)
#define C_CYAN   rgb(96, 210, 255)

// ---------------------------------------------------------------- the colour code

enum {
    K_BLACK,
    K_BROWN,
    K_RED,
    K_ORANGE,
    K_YELLOW,
    K_GREEN,
    K_BLUE,
    K_VIOLET,
    K_GREY,
    K_WHITE,
    K_GOLD,
    K_SILVER,
    K_NONE,
    N_KOL
};

static const uint8_t KRGB[N_KOL][3] = {
        {26, 26, 28},   {124, 72, 34},   {222, 38, 38},   {255, 134, 22}, {250, 220, 32},  {38, 168, 72}, {42, 98, 232},
        {152, 72, 212}, {136, 136, 142}, {246, 246, 246}, {212, 170, 58}, {194, 196, 206}, {0, 0, 0},
};
static const char *const KNAME[N_KOL] = {
        "BLACK",  "BROWN", "RED",   "ORANGE", "YELLOW", "GREEN", "BLUE",
        "VIOLET", "GREY",  "WHITE", "GOLD",   "SILVER", "NONE",
};
// tolerance in 0.01 % (IEC 60062), -1 = not a tolerance colour
static const int16_t KTOL[N_KOL] = {-1, 100, 200, 5, 2, 50, 25, 10, 1, -1, 500, 1000, 2000};
// temperature coefficient in ppm/K, -1 = none
static const int16_t KTCR[N_KOL] = {250, 100, 50, 15, 25, 20, 10, 5, 1, -1, -1, -1, -1};
static const char *const KMUL[N_KOL] = {
        "X1", "X10", "X100", "X1K", "X10K", "X100K", "X1M", "X10M", "X100M", "X1G", "X0.1", "X0.01", "",
};

static uint16_t kcol(int k)
{
    return rgb(KRGB[k][0], KRGB[k][1], KRGB[k][2]);
}

// dark text on these colours
static bool k_light(int k)
{
    return k == K_ORANGE || k == K_YELLOW || k == K_GREY || k == K_WHITE || k == K_GOLD || k == K_SILVER ||
           k == K_GREEN;
}

enum {
    RO_DIG,
    RO_MUL,
    RO_TOL,
    RO_TCR
};

static int role_of(int i, int n)
{
    int nd = n == 4 ? 2 : 3;
    if (i < nd) {
        return RO_DIG;
    }
    if (i == nd) {
        return RO_MUL;
    }
    if (i == nd + 1) {
        return RO_TOL;
    }
    return RO_TCR;
}

static bool k_valid(int role, int k)
{
    switch (role) {
    case RO_DIG:
        return k <= K_WHITE;
    case RO_MUL:
        return k <= K_SILVER;
    case RO_TOL:
        return KTOL[k] >= 0;
    default:
        return KTCR[k] >= 0;
    }
}

static int mul_e(int k)
{
    return k <= K_WHITE ? k : k == K_GOLD ? -1 : -2;
}

static uint8_t mul_k(int e)
{
    return (uint8_t)(e >= 0 ? e : e == -1 ? K_GOLD : K_SILVER);
}

// ---------------------------------------------------------------- E-series

static const uint8_t E6[6] = {10, 15, 22, 33, 47, 68};
static const uint8_t E12[12] = {10, 12, 15, 18, 22, 27, 33, 39, 47, 56, 68, 82};
static const uint8_t E24[24] = {
        10, 11, 12, 13, 15, 16, 18, 20, 22, 24, 27, 30, 33, 36, 39, 43, 47, 51, 56, 62, 68, 75, 82, 91,
};
// E96 in the order of the EIA-96 codes (01 = 100 ... 96 = 976); E48 = each second value
static const uint16_t E96[96] = {
        100, 102, 105, 107, 110, 113, 115, 118, 121, 124, 127, 130, 133, 137, 140, 143, 147, 150, 154, 158,
        162, 165, 169, 174, 178, 182, 187, 191, 196, 200, 205, 210, 215, 221, 226, 232, 237, 243, 249, 255,
        261, 267, 274, 280, 287, 294, 301, 309, 316, 324, 332, 340, 348, 357, 365, 374, 383, 392, 402, 412,
        422, 432, 442, 453, 464, 475, 487, 499, 511, 523, 536, 549, 562, 576, 590, 604, 619, 634, 649, 665,
        681, 698, 715, 732, 750, 768, 787, 806, 825, 845, 866, 887, 909, 931, 953, 976,
};

enum {
    SER_E6,
    SER_E12,
    SER_E24,
    SER_E48,
    SER_E96,
    N_SER
};
static const char *const SER_NAME[N_SER] = {"E6", "E12", "E24", "E48", "E96"};

static int ser_n(int s)
{
    static const uint8_t N[N_SER] = {6, 12, 24, 48, 96};
    return N[s];
}

// value i of a series as 3 digits (100 .. 976)
static int ser_m3(int s, int i)
{
    switch (s) {
    case SER_E6:
        return E6[i] * 10;
    case SER_E12:
        return E12[i] * 10;
    case SER_E24:
        return E24[i] * 10;
    case SER_E48:
        return E96[i * 2];
    default:
        return E96[i];
    }
}

static float p10(int e)
{
    float v = 1;
    if (e >= 0) {
        while (e--) {
            v *= 10;
        }
    } else {
        while (e++) {
            v /= 10;
        }
    }
    return v;
}

// v = sig * 10^e with sd digits in sig; returns true if that is exact
static bool sig_exp(float v, int sd, int *sig, int *e)
{
    int lim = sd == 2 ? 100 : 1000;
    int ex = (int)floorf(log10f(v)) - (sd - 1);
    int s = (int)lroundf(v / p10(ex));
    if (s >= lim) {
        s = (s + 5) / 10;
        ex++;
    }
    if (s < lim / 10) {
        s *= 10;
        ex--;
    }
    *sig = s;
    *e = ex;
    return fabsf(s * p10(ex) - v) <= v * 2e-4f;
}

// the series that has v (the smallest set), or -1
static int series_of(float v)
{
    if (v <= 0) {
        return -1;
    }
    int m, e;
    sig_exp(v, 3, &m, &e);
    for (int s = 0; s < N_SER; s++) {
        for (int i = 0; i < ser_n(s); i++) {
            if (ser_m3(s, i) == m) {
                return s;
            }
        }
    }
    return -1;
}

// the nearest value of series s (dir > 0: the smallest value at or above v)
static float ser_nearest(float v, int s, int dir)
{
    int m, e;
    sig_exp(v, 3, &m, &e);
    float best = v, bd = 1e30f;
    for (int k = -1; k <= 1; k++) {
        for (int i = 0; i < ser_n(s); i++) {
            float c = (float)ser_m3(s, i) * p10(e + k);
            float d;
            if (dir > 0) {
                if (c < v * 0.9999f) {
                    continue;
                }
                d = c / v;
            } else {
                d = fabsf(logf(c / v));
            }
            if (d < bd) {
                bd = d;
                best = c;
            }
        }
    }
    return best;
}

// The digit and multiplier bands of a value (n = 4: 2 digits, else 3).
// Returns 0 = out of range, 1 = exact, 2 = rounded.
static int encode(float v, int n, uint8_t *b)
{
    int sd = n == 4 ? 2 : 3, sig, e;
    if (v <= 0) {
        return 0;
    }
    bool ex = sig_exp(v, sd, &sig, &e);
    if (e < -2 || e > 9) {
        return 0;
    }
    if (sd == 2) {
        b[0] = (uint8_t)(sig / 10);
        b[1] = (uint8_t)(sig % 10);
        b[2] = mul_k(e);
    } else {
        b[0] = (uint8_t)(sig / 100);
        b[1] = (uint8_t)(sig / 10 % 10);
        b[2] = (uint8_t)(sig % 10);
        b[3] = mul_k(e);
    }
    return ex ? 1 : 2;
}

// all n bands for a value: digits, multiplier, tolerance (n = 4 or 5)
static int make_bands(float v, int n, int tolk, uint8_t *b)
{
    int r = encode(v, n, b);
    b[n - 1] = (uint8_t)tolk;
    return r;
}

// ---------------------------------------------------------------- numbers to text

static void trim0(char *t)
{
    if (!strchr(t, '.')) {
        return;
    }
    int n = (int)strlen(t);
    while (n > 0 && t[n - 1] == '0') {
        t[--n] = 0;
    }
    if (n > 0 && t[n - 1] == '.') {
        t[--n] = 0;
    }
}

// 4700 -> "4.7K~" (unit = false: no omega)
static void fmt_ohm(float v, char *o, int n, bool unit)
{
    const char *u = unit ? "~" : "";
    if (v < 0) {
        snprintf(o, n, "?");
        return;
    }
    if (v == 0) {
        snprintf(o, n, "0%s", u);
        return;
    }
    static const char *const SUF[4] = {"", "K", "M", "G"};
    int k = 0;
    float m = v;
    while (m >= 999.5f && k < 3) {
        m /= 1000;
        k++;
    }
    int d = m >= 99.95f ? 0 : m >= 9.995f ? 1 : m >= 0.9995f ? 2 : 3;
    char t[24];
    snprintf(t, sizeof(t), "%.*f", d, (double)m * (1.0 + 1e-6));
    trim0(t);
    strncat(t, SUF[k], 2);
    strncat(t, u, 2);
    int l = (int)strlen(t);
    if (l > n - 1) {
        l = n - 1;
    }
    memcpy(o, t, (size_t)l);
    o[l] = 0;
}

// tolerance in 0.01 % -> "0.25%"
static void fmt_tol(int t, char *o, int n)
{
    char b[16];
    snprintf(b, sizeof(b), "%d.%02d", t / 100, t % 100);
    trim0(b);
    int l = (int)strlen(b);
    if (l > n - 2) {
        l = n - 2;
    }
    memcpy(o, b, (size_t)l);
    o[l] = '%';
    o[l + 1] = 0;
}

// ---------------------------------------------------------------- SMD codes

static const char EIA_LET[] = "ZYXABCDEF";  // 10^-3 .. 10^5

// the codes of a value ("" = no code of that kind)
static void smd_codes(float v, char *c3, char *c4, char *ce)
{
    c3[0] = c4[0] = ce[0] = 0;
    if (v <= 0) {
        if (v == 0) {
            strcpy(c3, "000");
            strcpy(c4, "0000");
        }
        return;
    }
    int sig, e;
    if (sig_exp(v, 2, &sig, &e)) {
        if (e >= 0 && e <= 9) {
            snprintf(c3, 24, "%d%d", sig, e);
        } else if (e == -1) {
            snprintf(c3, 24, "%dR%d", sig / 10, sig % 10);
        } else if (e == -2) {
            snprintf(c3, 24, "R%d", sig);
        }
    }
    if (sig_exp(v, 3, &sig, &e)) {
        if (e >= 0 && e <= 9) {
            snprintf(c4, 24, "%d%d", sig, e);
        } else if (e == -1) {
            snprintf(c4, 24, "%dR%d", sig / 10, sig % 10);
        } else if (e == -2) {
            snprintf(c4, 24, "%dR%02d", sig / 100, sig % 100);
        } else if (e == -3) {
            snprintf(c4, 24, "R%d", sig);
        }
        if (e >= -3 && e <= 5) {
            for (int i = 0; i < 96; i++) {
                if (E96[i] == sig) {
                    snprintf(ce, 24, "%02d%c", i + 1, EIA_LET[e + 3]);
                    break;
                }
            }
        }
    }
}

static int eia_mult(char c)
{
    switch (c) {
    case 'Z':
        return -3;
    case 'Y':
    case 'R':
        return -2;
    case 'X':
    case 'S':
        return -1;
    case 'A':
        return 0;
    case 'B':
    case 'H':
        return 1;
    case 'C':
        return 2;
    case 'D':
        return 3;
    case 'E':
        return 4;
    case 'F':
        return 5;
    default:
        return 99;
    }
}

// An SMD code to a value (-1 = not a code). *kind = a short text; *alt = the EIA-96 reading of
// a code such as "47R" (-1 = none).
static float smd_decode(const char *s, const char **kind, float *alt)
{
    int n = (int)strlen(s), nd = 0, nr = 0, rpos = -1;
    *alt = -1;
    for (int i = 0; i < n; i++) {
        if (s[i] >= '0' && s[i] <= '9') {
            nd++;
        } else if (s[i] == 'R') {
            nr++;
            rpos = i;
        }
    }
    if (n == 0) {
        *kind = "TYPE THE CODE ON THE CHIP";
        return -1;
    }
    if (nd == n) {
        bool zero = true;
        for (int i = 0; i < n; i++) {
            zero = zero && s[i] == '0';
        }
        if (zero) {
            *kind = "A ZERO OHM LINK (A JUMPER)";
            return 0;
        }
        if (n == 3) {
            *kind = "3 DIGITS: 2 DIGITS, THEN THE ZEROS";
            return (float)((s[0] - '0') * 10 + (s[1] - '0')) * p10(s[2] - '0');
        }
        if (n == 4) {
            *kind = "4 DIGITS (1%): 3 DIGITS, THEN THE ZEROS";
            return (float)((s[0] - '0') * 100 + (s[1] - '0') * 10 + (s[2] - '0')) * p10(s[3] - '0');
        }
        *kind = "A CODE HAS 3 OR 4 CHARACTERS";
        return -1;
    }
    if (nr == 1 && nd == n - 1 && n >= 2) {
        // R is the decimal point
        char t[8];
        int k = 0;
        if (rpos == 0) {
            t[k++] = '0';
        }
        for (int i = 0; i < n && k < 7; i++) {
            t[k++] = s[i] == 'R' ? '.' : s[i];
        }
        t[k] = 0;
        if (n == 3 && rpos == 2) {
            int idx = (s[0] - '0') * 10 + (s[1] - '0');
            if (idx >= 1 && idx <= 96) {
                *alt = (float)E96[idx - 1] * p10(-2);
            }
        }
        *kind = "R IS THE DECIMAL POINT";
        return (float)atof(t);
    }
    if (n == 3 && nd == 2 && s[0] <= '9' && s[1] <= '9' && eia_mult(s[2]) != 99) {
        int idx = (s[0] - '0') * 10 + (s[1] - '0');
        if (idx < 1 || idx > 96) {
            *kind = "EIA-96 INDEX MUST BE 01 TO 96";
            return -1;
        }
        *kind = "EIA-96: AN E96 INDEX AND A LETTER";
        return (float)E96[idx - 1] * p10(eia_mult(s[2]));
    }
    *kind = n < 3 ? "MORE CHARACTERS..." : "NOT A KNOWN CODE";
    return -1;
}

// ---------------------------------------------------------------- reading the bands

typedef struct {
    bool ok;
    float v;
    int tol, tcr;
    const char *err, *warn;
} reading_t;

static reading_t reading(const uint8_t *b, int n)
{
    reading_t r = {.tol = -1, .tcr = -1};
    int nd = n == 4 ? 2 : 3, sig = 0;
    if (b[0] == K_GOLD || b[0] == K_SILVER) {
        r.err = "GOLD AND SILVER ARE AT THE RIGHT END: TAP FLIP";
        return r;
    }
    for (int i = 0; i < nd; i++) {
        if (b[i] > K_WHITE) {
            r.err = b[i] == K_NONE ? "A DIGIT BAND CANNOT BE EMPTY" : "GOLD AND SILVER ARE NEVER DIGITS";
            return r;
        }
        sig = sig * 10 + b[i];
    }
    if (b[nd] == K_NONE) {
        r.err = "THE MULTIPLIER BAND IS EMPTY";
        return r;
    }
    if (KTOL[b[nd + 1]] < 0) {
        r.err = "BLACK AND WHITE ARE NOT TOLERANCE COLOURS";
        return r;
    }
    if (n == 6 && KTCR[b[5]] < 0) {
        r.err = "THIS COLOUR IS NOT A TEMPERATURE CODE";
        return r;
    }
    r.ok = true;
    r.v = (float)sig * p10(mul_e(b[nd]));
    r.tol = KTOL[b[nd + 1]];
    if (n == 6) {
        r.tcr = KTCR[b[5]];
    }
    if (b[0] == K_BLACK) {
        r.warn = "BAND 1 IS NEVER BLACK ON A REAL PART";
    }
    return r;
}

// ---------------------------------------------------------------- state

typedef enum {
    ST_TITLE,
    ST_READ,
    ST_FIND,
    ST_SMD,
    ST_LEARN,
    ST_CALC,
    ST_CHART,
    ST_METER
} ostate_t;

static ostate_t s_state;
static int64_t s_now;

static char s_banner[48];
static int64_t s_banner_until;
static uint16_t s_banner_col;

static void banner(const char *t, uint16_t col, int ms)
{
    snprintf(s_banner, sizeof(s_banner), "%s", t);
    s_banner_col = col;
    s_banner_until = s_now + ms;
}

static uint32_t rnd(uint32_t n)
{
    return n ? esp_random() % n : 0;
}

// a soft tone for a new value: the pitch follows the value (a log scale)
static void value_tone(float v)
{
    if (v <= 0) {
        return;
    }
    float l = log10f(v);
    if (l < -1) {
        l = -1;
    }
    if (l > 10) {
        l = 10;
    }
    uint16_t f = (uint16_t)(330.0f * powf(2.0f, l * 0.25f));
    sfx_tone(f, f, 50, W_SINE, 28);
}

// READ
static uint8_t s_rb[6];
static int s_rn = 4, s_rsel;
static int s_drag_band = -1, s_drag_y0, s_drag_done;

// FIND
static char s_fv[8];
static int s_fn = 4, s_ftol_i, s_fser = SER_E12;
static const uint8_t FTOL[] = {K_GOLD, K_SILVER, K_BROWN, K_RED, K_GREEN, K_BLUE, K_VIOLET, K_NONE};
#define N_FTOL ((int)sizeof(FTOL))

// SMD
static char s_sc[6];

// CALC
static int s_calc_page;  // 0 = LED, 1 = DIVIDER
static int s_led_vs = 3, s_led_t, s_led_ma = 7, s_led_n = 1;
static int s_vin = 50, s_vout = 33, s_dser = 1;  // tenths of a volt; 0 E12, 1 E24, 2 E96
static float s_r1, s_r2, s_dvo;
static int s_hold_row = -1, s_hold_dir, s_hold_n;
static int64_t s_hold_next;

static const float LED_VS[] = {3.0f, 3.3f, 3.7f, 5.0f, 6.0f, 9.0f, 12.0f, 15.0f, 24.0f};
#define N_LED_VS ((int)(sizeof(LED_VS) / sizeof(LED_VS[0])))
static const struct {
    const char *name;
    float vf;
} LEDS[] = {
        {"RED", 2.0f}, {"YELLOW", 2.1f}, {"GREEN", 2.2f}, {"BLUE", 3.2f}, {"WHITE", 3.2f}, {"IR", 1.3f}, {"UV", 3.4f},
};
#define N_LEDS ((int)(sizeof(LEDS) / sizeof(LEDS[0])))
static const uint8_t LED_MA[] = {1, 2, 3, 5, 8, 10, 15, 20, 25, 30};
#define N_LED_MA ((int)sizeof(LED_MA))

// CHART
static int s_chart_page;

// METER
static const float REFS[3] = {1000.0f, 10000.0f, 100000.0f};
static int s_ref = 1, s_vt = 3300;
static bool s_meter_ok;
static int s_mv, s_m_state, s_m_n, s_m_i, s_stable_n;
static float s_m_buf[5], s_rx;
static bool s_stable;
static int64_t s_m_next;
enum {
    M_NONE,
    M_OPEN,
    M_SHORT,
    M_OK
};

// title
static int s_demo;
static int64_t s_demo_at;

// ---------------------------------------------------------------- drawing

static void R(int x, int y, int w, int h, uint16_t c)
{
    gfx_fill_rect(x, y, w, h, c);
}

#if BOARD_ROUND
// the text (with the omega and the plus-minus) from the panel point px, py (the top of its dot line); a smaller face
// when it is wider than avail px (its middle row stays)
static void ptext(int px, int py, const char *s, int ps, uint16_t c, int avail)
{
    int k = ps;
    while (k > 1 && ohm_aa_w(s, k) > avail) {
        k--;
    }
    ohm_aa_text(px, py + 7 * (ps - k) / 2, s, k, c);
}
// the text at the frame point x, y: its left end at the place of x in the row of the text
static void TXT(int x, int y, const char *s, int sc, uint16_t c)
{
    int ps = rvw_ps(sc), py = rvw_ty(y, sc), px = rvw_xp((float)x, py + 7 * ps / 2);
    ptext(px, py, s, ps, c, rvw_xp(SW, py + 7 * ps / 2) - px);
}
#else
static void TXT(int x, int y, const char *s, int sc, uint16_t c)
{
    ohm_aa_text(x, y, s, sc, c);
}
#endif

static int TW(const char *s, int sc)
{
#if BOARD_ROUND
    return gfx_text_width(s, sc);  // (rvw_text_width: the width of the AA faces in the frame)
#else
    return ohm_aa_w(s, sc);
#endif
}

static void TXT_IN(int x, int w, int y, const char *s, int sc, uint16_t c)
{
#if BOARD_ROUND
    // (the round view: the middle of the box in the row of the text)
    int ps = rvw_ps(sc), py = rvw_ty(y, sc), mid = py + 7 * ps / 2;
    int a = rvw_xp((float)x, mid), b = rvw_xp((float)(x + w), mid), tw = ohm_aa_w(s, ps);
    if (tw > b - a) {
        ptext(a, py, s, ps, c, b - a);  // (too wide for the box: a smaller face from its left end)
    } else {
        ptext((a + b - tw) / 2, py, s, ps, c, tw);
    }
    return;
#endif
    TXT(x + (w - TW(s, sc)) / 2, y, s, sc, c);
}

static void text_fit(int x, int w, int y, const char *s, int sc, uint16_t c)
{
    while (sc > 1 && TW(s, sc) > w) {
        sc--;
    }
    TXT_IN(x, w, y, s, sc, c);
}

// text in lines of at most w px (breaks at spaces, by the width of the text); returns the y under the last line
static int wrap(int x, int y, int w, const char *s, int sc, uint16_t c, int lh)
{
    char line[100];
    while (*s) {
        while (*s == ' ') {
            s++;
        }
        if (!*s) {
            break;
        }
        int lw = w;
#if BOARD_ROUND
        lw = (int)((float)w * rvw_sx(rvw_y(y + 3)));  // (the round view: the line is as long as its row lets it be)
#endif
        int n = (int)strlen(s), cut = 0;
        if (n > (int)sizeof(line) - 1) {
            n = (int)sizeof(line) - 1;
        }
        // the longest part that ends at a space (or at the end) and fits; one long word alone: all of it
        for (int k = 1; k <= n; k++) {
            if (k < n && s[k] != ' ') {
                continue;
            }
            memcpy(line, s, (size_t)k);
            line[k] = 0;
            if (TW(line, sc) > lw && cut) {
                break;
            }
            cut = k;
        }
        memcpy(line, s, (size_t)cut);
        line[cut] = 0;
        TXT(x, y, line, sc, c);
        y += lh;
        s += cut;
    }
    return y;
}

static void button(rc_t r, const char *label, const char *sub, uint16_t accent, bool on)
{
    R(r.x + 2, r.y + 2, r.w - 4, r.h - 4, on ? C_PANEL2 : C_PANEL);
    R(r.x + 2, r.y + 2, r.w - 4, 3, accent);
    gfx_frame_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 1, on ? accent : C_LINE);
#if BOARD_UI_TABLET
    // (the Tab5: the largest text that fits the button; text_fit makes a long label smaller)
    int sc = 4;
    while (sc > 2 && (TW(label, sc) > r.w - 12 || 7 * sc + (sub ? 20 : 0) > r.h - 10)) {
        sc--;
    }
    int th = 7 * sc + (sub ? 20 : 0);
    int ty = r.y + (r.h - th) / 2 + 1;
    text_fit(r.x + 2, r.w - 4, ty, label, sc, C_WHITE);
    if (sub) {
        text_fit(r.x + 2, r.w - 4, ty + 7 * sc + 6, sub, 2, C_DIM);
    }
#else
    int sc = 2;
    if (TW(label, 2) > r.w - 10 || r.h < 30 || (sub && r.h < 44)) {
        sc = 1;
    }
    int th = 7 * sc + (sub ? 10 : 0);
    int ty = r.y + (r.h - th) / 2 + 1;
    text_fit(r.x + 2, r.w - 4, ty, label, sc, C_WHITE);
    if (sub) {
        text_fit(r.x + 2, r.w - 4, ty + 7 * sc + 3, sub, 1, C_DIM);
    }
#endif
}

static void top_bar(const char *title, uint16_t acc)
{
    R(0, 0, SW, TOP_H, C_PANEL);
    R(0, TOP_H - 2, SW, 2, acc);
    button(BACK_RC, "BACK", NULL, C_DIM, false);
    if (title) {
        TXT(BACK_RC.x + BACK_RC.w + OX(8), (TOP_H - 7 * OS(2)) / 2, title, OS(2), acc);
    }
}

// THE CONFIRM (for the thumb): CLEAR (READ) and LEVELS (LEARN) lose work on one tap of a small bar button. The
// first tap shows a big YES button far from the top bar for 2.5 s; any other tap closes it.
enum {
    CONF_NONE,
    CONF_CLEAR,
    CONF_LEVEL
};
static int s_conf;
static int64_t s_conf_until;
#define CAL_HOLD_MS 1000
static int64_t s_cal_t0;  // METER: the time a hold of CAL started (0 = none)

static void conf_open(int kind)
{
    s_conf = kind;
    s_conf_until = s_now + 2500;
    sfx_play(SFX_TICK);
}

static void draw_conf(void)
{
    if (!s_conf || s_now >= s_conf_until) {
        return;
    }
    rc_t c = CONF_RC;
    R(c.x - OX(6), c.y - OY(20), c.w + OX(12), c.h + OY(26), C_BG);
    gfx_frame_rect(c.x - OX(6), c.y - OY(20), c.w + OX(12), c.h + OY(26), 1, C_AMBER);
    text_fit(c.x, c.w, c.y - OY(15), s_conf == CONF_CLEAR ? "CLEAR ALL THE BANDS?" : "GO TO THE NEXT LEVEL?", OS(1),
             C_AMBER);
    button(c, s_conf == CONF_CLEAR ? "YES, CLEAR" : "YES, NEXT", NULL, C_AMBER, true);
}

// the place of band i of n along the body (0..1)
static float band_pos(int i, int n)
{
    static const float P4[4] = {0.24f, 0.37f, 0.50f, 0.79f};
    static const float P5[5] = {0.21f, 0.32f, 0.43f, 0.54f, 0.80f};
    static const float P6[6] = {0.20f, 0.30f, 0.40f, 0.50f, 0.67f, 0.82f};
    switch (n) {
    case 4:
        return P4[i];
    case 5:
        return P5[i];
    case 6:
        return P6[i];
    default:
        return 0.5f;
    }
}

static int band_cx(int x, int w, int i, int n)
{
    int bx = x + w * 12 / 100, bw = w * 76 / 100;
    return bx + (int)((float)bw * band_pos(i, n));
}

// A resistor from x to x + w (with its leads), the middle at cy, the body h px high.
// n = 1 .. 6 bands (1 = a zero ohm link); sel = the band with a mark (-1 = none).
static void draw_res(int x, int cy, int w, int h, const uint8_t *b, int n, int sel)
{
    int bx = x + w * 12 / 100, bw = w * 76 / 100;
    int lt = h / 10;
    if (lt < 2) {
        lt = 2;
    }
    R(x, cy - lt / 2, w, lt, rgb(178, 180, 188));
    bool film = n >= 5;
    uint16_t body = film ? rgb(104, 160, 214) : rgb(224, 198, 150);
    uint16_t dark = film ? rgb(72, 118, 170) : rgb(184, 154, 108);
    uint16_t lite = film ? rgb(152, 198, 238) : rgb(246, 228, 192);
    int capw = bw / 7, hm = h * 74 / 100;
    gfx_fill_ellipse(bx + capw, cy, capw, h / 2, body);
    gfx_fill_ellipse(bx + bw - capw, cy, capw, h / 2, body);
    R(bx + capw, cy - hm / 2, bw - 2 * capw, hm, body);
    R(bx + capw, cy + hm / 2 - hm / 6, bw - 2 * capw, hm / 6, dark);
    R(bx + capw, cy - hm / 2 + hm / 8, bw - 2 * capw, hm / 10 > 0 ? hm / 10 : 1, lite);
    int bwid = bw * (n == 6 ? 5 : 6) / 100;
    if (bwid < 2) {
        bwid = 2;
    }
    for (int i = 0; i < n; i++) {
        int cx = bx + (int)((float)bw * band_pos(i, n));
        bool cap = cx < bx + 2 * capw || cx > bx + bw - 2 * capw;
        int bh = cap ? h - h / 8 : hm;
        int x0 = cx - bwid / 2, y0 = cy - bh / 2;
        if (b[i] != K_NONE) {
            R(x0, y0, bwid, bh, kcol(b[i]));
            if (b[i] == K_GOLD || b[i] == K_SILVER) {
                R(x0, y0 + bh / 5, bwid, bh / 6 > 0 ? bh / 6 : 1,
                  b[i] == K_GOLD ? rgb(255, 228, 140) : rgb(244, 246, 252));
            }
        }
        if (i == sel) {
            gfx_frame_rect(x0 - 3, y0 - 3, bwid + 6, bh + 6, 2, C_WHITE);
            gfx_spike(cx - 6, cy + h / 2 + 3, 12, 7, C_WHITE);
        }
    }
}

// a colour swatch with a label; ok = false: the colour cannot be in this band
static void swatch(rc_t r, int k, const char *lab, bool ok, bool on)
{
    int x = r.x + 2, y = r.y + 2, w = r.w - 4, h = r.h - 4;
    if (k == K_NONE) {
        R(x, y, w, h, C_PANEL);
        for (int i = 0; i < w; i += 6) {
            R(x + i, y, 3, 1, C_DIM);
            R(x + i, y + h - 1, 3, 1, C_DIM);
        }
    } else {
        R(x, y, w, h, kcol(k));
        if (k == K_GOLD || k == K_SILVER) {
            R(x, y + h / 5, w, h / 7 + 1, k == K_GOLD ? rgb(255, 228, 140) : rgb(244, 246, 252));
        }
    }
    if (!ok) {
        gfx_darken_rect(x, y, w, h, 2);
    }
    gfx_frame_rect(x, y, w, h, 1, on ? C_WHITE : C_LINE);
    if (on) {
        gfx_frame_rect(x - 2, y - 2, w + 4, h + 4, 2, C_WHITE);
    }
    if (lab && ok) {
        uint16_t tc = (k != K_NONE && k_light(k)) ? rgb(20, 20, 24) : C_WHITE;
        int sc = (TW(lab, OS(2)) <= w - 4 && h >= OY(30)) ? OS(2) : OS(1);
        text_fit(x, w, y + (h - 7 * sc) / 2, lab, sc, tc);
    }
}

static void draw_chip(rc_t r, const char *code)
{
    int cw = r.w / 8;
    R(r.x, r.y, r.w, r.h, rgb(24, 24, 28));
    R(r.x, r.y, cw, r.h, rgb(188, 190, 198));
    R(r.x + r.w - cw, r.y, cw, r.h, rgb(188, 190, 198));
    R(r.x + cw, r.y, r.w - 2 * cw, 2, rgb(64, 64, 72));
    R(r.x + cw, r.y + r.h - 3, r.w - 2 * cw, 3, rgb(10, 10, 12));
    int sc = OS(4);
    while (sc > 1 && (TW(code, sc) > r.w - 2 * cw - 8 || 7 * sc > r.h - 8)) {
        sc--;
    }
    TXT_IN(r.x, r.w, r.y + (r.h - 7 * sc) / 2, code, sc, rgb(230, 230, 236));
}

// ---------------------------------------------------------------- READ

static const uint8_t RD_DEF[3][6] = {
        {K_YELLOW, K_VIOLET, K_RED, K_GOLD, K_NONE, K_NONE},
        {K_BROWN, K_BLACK, K_BLACK, K_RED, K_BROWN, K_NONE},
        {K_BROWN, K_BLACK, K_BLACK, K_RED, K_BROWN, K_BROWN},
};

static void read_defaults(void)
{
    memcpy(s_rb, RD_DEF[s_rn - 4], 6);
    s_rsel = 0;
}

// change the band count and keep the value if possible
static void read_set_n(int n)
{
    reading_t r = reading(s_rb, s_rn);
    int tolk = s_rb[s_rn == 4 ? 3 : 4];
    s_rn = n;
    read_defaults();
    if (r.ok) {
        uint8_t b[6];
        memcpy(b, s_rb, 6);
        if (encode(r.v, n, b) > 0) {
            memcpy(s_rb, b, 6);
            if (KTOL[tolk] >= 0) {
                s_rb[n == 4 ? 3 : 4] = (uint8_t)tolk;
            }
        }
    }
    store_set("ohm_nb", (uint32_t)n);
}

static void read_changed(void)
{
    reading_t r = reading(s_rb, s_rn);
    if (r.ok) {
        value_tone(r.v);
    }
}

static int next_valid(int k, int role, int dir)
{
    for (int i = 0; i < N_KOL; i++) {
        k = (k + dir + N_KOL) % N_KOL;
        if (k_valid(role, k)) {
            return k;
        }
    }
    return k;
}

static rc_t pal_rc(int i)
{
#if BOARD_BAR_SCREEN
    return RC(1 + i * 49, 121, 49, 51);
#else
    if (i < 7) {
        return RC(OX(2) + i * OX(45), PAL_Y, OX(45), OY(42));
    }
    return RC(OX(24) + (i - 7) * OX(45), PAL_Y + OY(42), OX(45), OY(42));
#endif
}

static const char *pal_label(int role, int k, char *buf, int n)
{
    switch (role) {
    case RO_DIG:
        snprintf(buf, n, "%d", k);
        break;
    case RO_MUL:
        snprintf(buf, n, "%s", KMUL[k]);
        break;
    case RO_TOL:
        fmt_tol(KTOL[k], buf, n);
        break;
    default:
        snprintf(buf, n, "%d", KTCR[k]);
        break;
    }
    return buf;
}

static int band_at(int x)
{
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < s_rn; i++) {
        int d = abs(band_cx(RD_X, RD_W, i, s_rn) - x);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static void render_read(void)
{
    top_bar(BOARD_BAR_SCREEN ? "READ" : NULL, C_COPPER);
    char t[16];
    snprintf(t, sizeof(t), "%d BANDS", s_rn);
    button(bar_btn(0, 3), t, NULL, C_COPPER, false);
    button(bar_btn(1, 3), "FLIP", NULL, C_COPPER, false);
    button(bar_btn(2, 3), "CLEAR", NULL, C_COPPER, false);
    draw_res(RD_X, RD_CY, RD_W, RD_H, s_rb, s_rn, s_rsel);

    // the palette for the marked band
    int role = role_of(s_rsel, s_rn);
    for (int k = 0; k < N_KOL; k++) {
        char lab[12];
        swatch(pal_rc(k), k, pal_label(role, k, lab, sizeof(lab)), k_valid(role, k), s_rb[s_rsel] == k);
    }

    // the result
    int x0 = RD_INFO_X, y = RD_INFO_Y, w = RD_INFO_W;
    reading_t r = reading(s_rb, s_rn);
    static const char *const ROLE[4] = {"DIGIT", "MULTIPLIER", "TOLERANCE", "TEMP. COEFFICIENT"};
    char line[80];
    if (!r.ok) {
        TXT(x0, y, "?", OS(3), C_RED);
        snprintf(line, sizeof(line), "BAND %d: %s", s_rsel + 1, ROLE[role]);
        TXT(x0 + OX(30), y + OY(7), line, OS(1), C_DIM);
        wrap(x0, y + (BOARD_BAR_SCREEN ? 30 : OY(26)), w, r.err, OS(1), C_RED, BOARD_BAR_SCREEN ? 11 : OY(10));
        return;
    }
    char v[20], tol[12], lo[16], hi[16];
    fmt_ohm(r.v, v, sizeof(v), true);
    TXT(x0, y, v, OS(3), C_WHITE);
    fmt_tol(r.tol, tol, sizeof(tol));
    snprintf(line, sizeof(line), "`%s", tol);
    TXT(x0 + TW(v, OS(3)) + OX(10), y + OY(7), line, OS(2), C_COPPER);
    fmt_ohm(r.v * (1 - (float)r.tol / 10000.0f), lo, sizeof(lo), true);
    fmt_ohm(r.v * (1 + (float)r.tol / 10000.0f), hi, sizeof(hi), true);
    snprintf(line, sizeof(line), "%s TO %s", lo, hi);
#if !BOARD_BAR_SCREEN
    // the 320 frame: three lines (the swatches are bigger): the range (+ the temperature), then the standard, a hint
    // or a warning
    {
        if (r.tcr >= 0) {
            char tc[24];
            snprintf(tc, sizeof(tc), "  %d PPM/K", r.tcr);
            strncat(line, tc, sizeof(line) - strlen(line) - 1);
        }
        TXT(x0, y + OY(24), line, OS(1), C_DIM);
        int s = series_of(r.v);
        int yy = y + OY(36);
        {
            uint8_t fl[6];
            for (int i = 0; i < s_rn; i++) {
                fl[i] = s_rb[s_rn - 1 - i];
            }
            reading_t f = reading(fl, s_rn);
            if (f.ok && !f.warn && series_of(f.v) >= 0 && (s < 0 || r.warn)) {
                char fv[20];
                fmt_ohm(f.v, fv, sizeof(fv), true);
                snprintf(line, sizeof(line), "FROM THE OTHER END: %s (TAP FLIP)", fv);
                TXT(x0, yy, line, OS(1), C_AMBER);
            } else if (r.warn) {
                wrap(x0, yy, w, r.warn, OS(1), C_AMBER, OY(10));
            } else {
                if (s >= 0) {
                    snprintf(line, sizeof(line), "A STANDARD %s VALUE", SER_NAME[s]);
                } else {
                    snprintf(line, sizeof(line), "NOT A STANDARD VALUE");
                }
                TXT(x0, yy, line, OS(1), s >= 0 ? C_GREEN : C_AMBER);
            }
        }
        return;
    }
#endif
    TXT(x0, y + 26, line, 1, C_DIM);
    int s = series_of(r.v);
    if (s >= 0) {
        snprintf(line, sizeof(line), "A STANDARD %s VALUE", SER_NAME[s]);
    } else {
        snprintf(line, sizeof(line), "NOT A STANDARD VALUE");
    }
    TXT(x0, y + 38, line, 1, s >= 0 ? C_GREEN : C_AMBER);
    int yy = y + 50;
    if (r.tcr >= 0) {
        snprintf(line, sizeof(line), "%d PPM/K", r.tcr);
        TXT(x0, yy, line, 1, C_DIM);
        yy += 12;
    }
    // a reading from the other end that makes more sense
    uint8_t fl[6];
    for (int i = 0; i < s_rn; i++) {
        fl[i] = s_rb[s_rn - 1 - i];
    }
    reading_t f = reading(fl, s_rn);
    if (f.ok && !f.warn && series_of(f.v) >= 0 && (s < 0 || r.warn)) {
        char fv[20];
        fmt_ohm(f.v, fv, sizeof(fv), true);
        snprintf(line, sizeof(line), "FROM THE OTHER END: %s (TAP FLIP)", fv);
        wrap(x0, yy, w, line, 1, C_AMBER, 11);
    } else if (r.warn) {
        wrap(x0, yy, w, r.warn, 1, C_AMBER, 11);
    } else {
        snprintf(line, sizeof(line), "BAND %d: %s", s_rsel + 1, ROLE[role]);
        TXT(x0, yy, line, 1, C_DARK);
    }
}

static void read_input(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
    if (ev->kind == IN_DOWN && in_rc(RD_HIT, x, y)) {
        s_drag_band = band_at(x);
        s_rsel = s_drag_band;
        s_drag_y0 = y;
        s_drag_done = 0;
        return;
    }
    if (ev->kind == IN_MOVE && s_drag_band >= 0) {
        // drag up = the next colour, drag down = the colour before
        int steps = (s_drag_y0 - y) / OY(14);
        int role = role_of(s_drag_band, s_rn);
        bool changed = false;
        while (s_drag_done < steps) {
            s_rb[s_drag_band] = (uint8_t)next_valid(s_rb[s_drag_band], role, 1);
            s_drag_done++;
            changed = true;
        }
        while (s_drag_done > steps) {
            s_rb[s_drag_band] = (uint8_t)next_valid(s_rb[s_drag_band], role, -1);
            s_drag_done--;
            changed = true;
        }
        if (changed) {
            read_changed();
        }
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    if (in_rc(bar_btn(0, 3), x, y)) {
        read_set_n(s_rn == 6 ? 4 : s_rn + 1);
        sfx_play(SFX_CLICK);
        return;
    }
    if (in_rc(bar_btn(1, 3), x, y)) {
        for (int i = 0; i < s_rn / 2; i++) {
            uint8_t t = s_rb[i];
            s_rb[i] = s_rb[s_rn - 1 - i];
            s_rb[s_rn - 1 - i] = t;
        }
        s_rsel = s_rn - 1 - s_rsel;
        sfx_play(SFX_SWITCH);
        read_changed();
        return;
    }
    if (in_rc(bar_btn(2, 3), x, y)) {
        conf_open(CONF_CLEAR);  // (the YES button clears)
        return;
    }
    int role = role_of(s_rsel, s_rn);
    for (int k = 0; k < N_KOL; k++) {
        if (in_rc(pal_rc(k), x, y)) {
            if (!k_valid(role, k)) {
                sfx_play(SFX_BUMP);
                return;
            }
            s_rb[s_rsel] = (uint8_t)k;
            read_changed();
            if (s_rsel < s_rn - 1) {
                s_rsel++;
            }
            return;
        }
    }
}

// ---------------------------------------------------------------- FIND

static const char *const FD_KEYS[18] = {
        "7", "8", "9", "K", "M", "DEL", "4", "5", "6", "0", ".", "CLR", "1", "2", "3", "USE", "USE", "USE",
};
#define FD_N    18
#define FD_COLS 6

static float parse_val(const char *t);

// the nearest standard value of the typed value (0: none, or it is standard already)
static float find_use(void)
{
    float v = parse_val(s_fv);
    uint8_t b[6];
    if (v <= 0 || make_bands(v, s_fn, FTOL[s_ftol_i], b) <= 0) {
        return 0;
    }
    int nd = s_fn == 4 ? 2 : 3, sig = 0;
    for (int i = 0; i < nd; i++) {
        sig = sig * 10 + b[i];
    }
    float shown = (float)sig * p10(mul_e(b[nd]));
    int ser = series_of(shown);
    if (ser >= 0 && ser <= s_fser) {
        return 0;
    }
    return ser_nearest(shown, s_fser, 0);
}

// "4K7", "4.7K", "470" -> ohms; -1 = no value
static float parse_val(const char *s)
{
    char t[12];
    int n = 0;
    float mul = 1;
    bool dot = false, let = false;
    for (const char *p = s; *p && n < 10; p++) {
        char c = *p;
        if (c >= '0' && c <= '9') {
            t[n++] = c;
        } else if (c == '.') {
            if (dot || let) {
                return -1;
            }
            dot = true;
            t[n++] = '.';
        } else if (c == 'K' || c == 'M') {
            if (let) {
                return -1;
            }
            let = true;
            mul = c == 'K' ? 1e3f : 1e6f;
            if (p[1]) {  // 4K7: the letter is the decimal point
                if (dot) {
                    return -1;
                }
                dot = true;
                t[n++] = '.';
            }
        } else {
            return -1;
        }
    }
    t[n] = 0;
    if (n == 0 || (n == 1 && t[0] == '.')) {
        return -1;
    }
    if (t[0] == '.' && let && n == 1) {
        return -1;
    }
    return (float)atof(t) * mul;
}

static void find_key(int i)
{
    int n = (int)strlen(s_fv);
    const char *k = FD_KEYS[i];
    if (strcmp(k, "DEL") == 0) {
        if (n > 0) {
            s_fv[n - 1] = 0;
        }
        sfx_play(SFX_CLICK);
        return;
    }
    if (strcmp(k, "CLR") == 0) {
        s_fv[0] = 0;
        sfx_play(SFX_CLICK);
        return;
    }
    if (n >= 6) {
        sfx_play(SFX_BUMP);
        return;
    }
    char t[8];
    snprintf(t, sizeof(t), "%s%s", s_fv, k);
    float v = parse_val(t);
    // a K or M as the first key means 1K or 1M
    if (v < 0 && n == 0 && (k[0] == 'K' || k[0] == 'M')) {
        snprintf(t, sizeof(t), "1%s", k);
        v = parse_val(t);
    }
    if (v < 0 && strcmp(k, ".") == 0 && n == 0) {
        snprintf(t, sizeof(t), "0.");
        v = 0;
    }
    if (v < 0) {
        sfx_play(SFX_BUMP);
        return;
    }
    snprintf(s_fv, sizeof(s_fv), "%s", t);
    sfx_play(SFX_CLICK);
    float pv = parse_val(s_fv);
    if (pv > 0) {
        value_tone(pv);
    }
}

static void render_find(void)
{
    top_bar(BOARD_BAR_SCREEN ? "FIND" : NULL, C_CYAN);
    char t[24];
    fmt_tol(KTOL[FTOL[s_ftol_i]], t, sizeof(t));
    button(bar_btn(0, 3), t, NULL, C_CYAN, false);
    snprintf(t, sizeof(t), "%d BANDS", s_fn);
    button(bar_btn(1, 3), t, NULL, C_CYAN, false);
    button(bar_btn(2, 3), SER_NAME[s_fser == 0 ? SER_E12 : s_fser], NULL, C_CYAN, false);
    float use = find_use();
    for (int i = 0; i < FD_N; i++) {
        rc_t c = grid(FD_KP, FD_COLS, 3, i);
        if (!strcmp(FD_KEYS[i], "USE")) {
            if (strcmp(FD_KEYS[i - 1], "USE")) {
                char u[24] = "STANDARD", nv[16];
                if (use > 0) {
                    fmt_ohm(use, nv, sizeof(nv), false);
                    snprintf(u, sizeof(u), "USE %s", nv);
                }
                c.w = (int16_t)(c.w * 3);
                button(c, u, NULL, use > 0 ? C_AMBER : C_DARK, false);
            }
            continue;
        }
        bool fn = FD_KEYS[i][0] >= 'A';
        button(c, FD_KEYS[i], NULL, fn ? C_CYAN : C_DARK, false);
    }
#if BOARD_BAR_SCREEN
    int ty = 46, tsc = 3, rcy = 84, rh = 30, ly = 106;
#else
    int ty = OY(48), tsc = OS(2), rcy = OY(72), rh = OY(20), ly = OY(87);
#endif
    float v = parse_val(s_fv);
    char line[80], vs[20];
    if (s_fv[0]) {
        snprintf(line, sizeof(line), "%s~", s_fv);
        TXT(TX, ty, line, tsc, C_WHITE);
    } else {
        TXT(TX, ty, "_", tsc, C_DIM);
        TXT(TX + 6 * tsc + OX(8), ty + (tsc - 1) * 3, "TYPE A VALUE: 4K7 = 4.7K", OS(1), C_DIM);
    }
    uint8_t b[6] = {K_NONE, K_NONE, K_NONE, K_NONE, K_NONE, K_NONE};
    if (v < 0) {
        draw_res(TX - 2, rcy, FD_LEFT_W - 2 * TX + 4, rh, b, s_fn, -1);
        return;
    }
    if (v == 0) {
        b[0] = K_BLACK;
        draw_res(TX - 2, rcy, FD_LEFT_W - 2 * TX + 4, rh, b, 1, -1);
        TXT(TX, ly, "A ZERO OHM LINK: ONE BLACK BAND", OS(1), C_GREEN);
        TXT(TX, ly + OY(12), "SMD CODE: 0 OR 000", OS(1), C_DIM);
        return;
    }
    int code = make_bands(v, s_fn, FTOL[s_ftol_i], b);
    if (code == 0) {
        draw_res(TX - 2, rcy, FD_LEFT_W - 2 * TX + 4, rh, b, s_fn, -1);
        TXT(TX, ly, s_fn == 4 ? "OUT OF RANGE: 0.1~ TO 99G~" : "OUT OF RANGE: 1~ TO 999G~", OS(1), C_RED);
        return;
    }
    draw_res(TX - 2, rcy, FD_LEFT_W - 2 * TX + 4, rh, b, s_fn, -1);
    // the value that the bands show
    int nd = s_fn == 4 ? 2 : 3, sig = 0;
    for (int i = 0; i < nd; i++) {
        sig = sig * 10 + b[i];
    }
    float shown = (float)sig * p10(mul_e(b[nd]));
    fmt_ohm(shown, vs, sizeof(vs), true);
#if BOARD_BAR_SCREEN
    snprintf(line, sizeof(line), "= %s", vs);
    TXT(FD_LEFT_W - 6 - TW(line, 2), 50, line, 2, C_CYAN);
#else
    snprintf(line, sizeof(line), "= %s", vs);
    TXT(FD_LEFT_W - TX - TW(line, OS(1)), OY(52), line, OS(1), C_CYAN);
#endif
    int ser = series_of(shown);
    int want = s_fser;
    if (ser >= 0 && ser <= want) {
        snprintf(line, sizeof(line), "IN THE %s SERIES", SER_NAME[want]);
        TXT(TX, ly, line, OS(1), C_GREEN);
    } else {
        char nv[20];
        fmt_ohm(ser_nearest(shown, want, 0), nv, sizeof(nv), true);
        snprintf(line, sizeof(line), "NEAREST %s: %s  (KEY USE)", SER_NAME[want], nv);
        TXT(TX, ly, line, OS(1), C_AMBER);
    }
    int y2 = ly + (BOARD_BAR_SCREEN ? 13 : OY(12));
    if (code == 2) {
        snprintf(line, sizeof(line), "%d BANDS SHOW ONLY %d DIGITS: USE %d BANDS", s_fn, nd, s_fn == 4 ? 5 : 4);
        if (s_fn != 4) {
            snprintf(line, sizeof(line), "5 BANDS SHOW ONLY 3 DIGITS");
        }
        TXT(TX, y2, line, OS(1), C_AMBER);
        y2 += 13;
        if (!BOARD_BAR_SCREEN) {
            return;  // (the 320 frame: no room for the SMD line)
        }
    }
    char c3[24], c4[24], ce[24];
    smd_codes(shown, c3, c4, ce);
    snprintf(line, sizeof(line), "SMD  %s  %s  %s", c3[0] ? c3 : "-", c4[0] ? c4 : "-", ce[0] ? ce : "-");
    TXT(TX, y2, line, OS(1), C_WHITE);
#if BOARD_BAR_SCREEN
    y2 += 13;
    if (y2 < SH - 10) {
        line[0] = 0;
        for (int i = 0; i < s_fn; i++) {
            if (i) {
                strncat(line, " ", sizeof(line) - strlen(line) - 1);
            }
            strncat(line, KNAME[b[i]], sizeof(line) - strlen(line) - 1);
        }
        text_fit(0, FD_LEFT_W, y2, line, 1, C_DIM);
    }
#endif
}

static void find_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (in_rc(bar_btn(0, 3), x, y)) {
        s_ftol_i = (s_ftol_i + 1) % N_FTOL;
        sfx_play(SFX_CLICK);
        return;
    }
    if (in_rc(bar_btn(1, 3), x, y)) {
        s_fn = s_fn == 4 ? 5 : 4;
        sfx_play(SFX_CLICK);
        return;
    }
    if (in_rc(bar_btn(2, 3), x, y)) {
        s_fser = s_fser == SER_E12 ? SER_E24 : s_fser == SER_E24 ? SER_E96 : s_fser == SER_E96 ? SER_E6 : SER_E12;
        sfx_play(SFX_CLICK);
        return;
    }
    for (int i = 0; i < FD_N; i++) {
        if (!in_rc(grid(FD_KP, FD_COLS, 3, i), x, y)) {
            continue;
        }
        if (strcmp(FD_KEYS[i], "USE")) {
            find_key(i);
            return;
        }
        // USE: the nearest standard value
        float nv = find_use();
        if (nv > 0) {
            char t[20];
            fmt_ohm(nv, t, sizeof(t), false);
            if (strlen(t) < sizeof(s_fv)) {
                snprintf(s_fv, sizeof(s_fv), "%s", t);
                value_tone(nv);
            }
        } else {
            sfx_play(SFX_BUMP);
        }
        return;
    }
}

// ---------------------------------------------------------------- SMD

// two pages of 5 x 3 keys (for the thumb: keys of 64 x 42 px on the 320 frame); "ABC" / "123" change the page
static const char *const SM_PAGES[2][15] = {
        {"7", "8", "9", "R", "DEL", "4", "5", "6", "0", "CLR", "1", "2", "3", "ABC", "ABC"},
        {"A", "B", "C", "D", "DEL", "E", "F", "H", "R", "CLR", "S", "X", "Y", "Z", "123"},
};
static int s_spage;  // the page of the SMD keys

static void render_smd(void)
{
    top_bar("SMD", C_GREEN);
    for (int i = 0; i < 15; i++) {
        rc_t c = grid(SM_KP, 5, 3, i);
        const char *k = SM_PAGES[s_spage][i];
        if (i == 14 && s_spage == 0) {
            continue;
        }
        if (i == 13 && s_spage == 0) {
            c.w = (int16_t)(c.w * 2);
        }
        bool fn = strlen(k) > 1;
        bool letter = !fn && k[0] >= 'A';
        button(c, k, NULL, fn ? C_GREEN : letter ? C_DARK : C_LINE, false);
    }
    draw_chip(SM_CHIP, s_sc[0] ? s_sc : "?");
    const char *kind;
    float alt;
    float v = smd_decode(s_sc, &kind, &alt);
    char vs[20], line[64];
#if BOARD_BAR_SCREEN
    int vx = 176, vy = 50, ky = 114, vw = 120;
#else
    int vx = SM_CHIP.x + SM_CHIP.w + OX(10), vy = OY(48), ky = OY(92), vw = SW - TX - vx;
#endif
    if (v >= 0) {
        fmt_ohm(v, vs, sizeof(vs), true);
        text_fit(vx, vw, vy, vs, OS(3), C_WHITE);
        uint8_t b[6] = {K_BLACK, K_NONE, K_NONE, K_NONE, K_NONE, K_NONE};
        if (v == 0) {
            draw_res(vx, vy + RB_DY, vw, RB_H, b, 1, -1);
        } else {
            int n = (strlen(s_sc) == 4 || kind[0] == 'E') ? 5 : 4;
            if (make_bands(v, n, n == 5 ? K_BROWN : K_GOLD, b) > 0) {
                draw_res(vx, vy + RB_DY, vw, RB_H, b, n, -1);
            }
        }
    } else {
        text_fit(vx, vw, vy + OY(6), "-", OS(3), C_DIM);
    }
    int y = wrap(TX, ky, BOARD_BAR_SCREEN ? SM_KP.x - 10 : SW - 2 * TX, kind, OS(1), v >= 0 ? C_GREEN : C_DIM,
                 BOARD_BAR_SCREEN ? 11 : OY(10));
    if (alt >= 0) {
        fmt_ohm(alt, vs, sizeof(vs), true);
        snprintf(line, sizeof(line), "AS EIA-96 IT IS %s", vs);
        TXT(TX, y, line, OS(1), C_AMBER);
    } else if (v > 0) {
        int n = (int)strlen(s_sc);
        TXT(TX, y, kind[0] == 'E' || n == 4 ? "USUALLY 1%" : "USUALLY 5%", OS(1), C_DIM);
    }
}

static void smd_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    for (int i = 0; i < 15; i++) {
        if (!in_rc(grid(SM_KP, 5, 3, i), ev->x, ev->y)) {
            continue;
        }
        const char *k = SM_PAGES[s_spage][i];
        if (i == 14 && s_spage == 0) {
            k = "ABC";
        }
        int n = (int)strlen(s_sc);
        if (!strcmp(k, "ABC") || !strcmp(k, "123")) {
            s_spage ^= 1;
        } else if (!strcmp(k, "DEL")) {
            if (n) {
                s_sc[n - 1] = 0;
            }
        } else if (!strcmp(k, "CLR")) {
            s_sc[0] = 0;
        } else if (n < 4) {
            s_sc[n] = k[0];
            s_sc[n + 1] = 0;
            const char *kind;
            float alt, v = smd_decode(s_sc, &kind, &alt);
            if (v > 0) {
                value_tone(v);
            } else {
                sfx_play(SFX_CLICK);
            }
            return;
        } else {
            sfx_play(SFX_BUMP);
            return;
        }
        sfx_play(SFX_CLICK);
        return;
    }
}

// ---------------------------------------------------------------- LEARN

enum {
    Q_COL2DIG,
    Q_DIG2COL,
    Q_MULT,
    Q_TOL,
    Q_READ4,
    Q_READ5,
    Q_PICK,
    Q_SMD3,
    Q_SMD4,
    Q_EIA,
    Q_MIX
};
enum {
    A_DIGIT,
    A_COLOUR,
    A_CHOICE,
    A_PICK
};
enum {
    SH_SWATCH,
    SH_DIGIT,
    SH_RES,
    SH_VALUE,
    SH_SMD
};

typedef struct {
    uint8_t kind, arg;
    const char *name;
    const char *text;
} level_t;

static const level_t LEVELS[] = {
        {Q_COL2DIG, 3, "COLOURS 0 TO 2",
         "EACH COLOUR IS A DIGIT. BLACK = 0, BROWN = 1, RED = 2. TAP THE DIGIT OF THE COLOUR."},
        {Q_COL2DIG, 5, "COLOURS 0 TO 4",
         "NEW: ORANGE = 3, YELLOW = 4. AFTER RED THE RAINBOW STARTS: RED, ORANGE, YELLOW..."},
        {Q_COL2DIG, 7, "COLOURS 0 TO 6", "NEW: GREEN = 5, BLUE = 6. THE RAINBOW CONTINUES: YELLOW, GREEN, BLUE..."},
        {Q_COL2DIG, 10, "ALL 10 COLOURS",
         "NEW: VIOLET = 7, GREY = 8, WHITE = 9. A HELP TO REMEMBER: BB ROY OF GREAT BRITAIN HAS A VERY GOOD WIFE."},
        {Q_DIG2COL, 10, "DIGIT TO COLOUR", "NOW THE OTHER WAY: A DIGIT SHOWS. TAP ITS COLOUR."},
        {Q_MULT, 0, "THE MULTIPLIER",
         "THE BAND AFTER THE DIGITS IS THE MULTIPLIER: ITS DIGIT IS THE NUMBER OF ZEROS. RED = 2 ZEROS = X100."},
        {Q_MULT, 1, "GOLD AND SILVER",
         "AS A MULTIPLIER, GOLD = X0.1 AND SILVER = X0.01. THEY MAKE VALUES BELOW 10 OHMS."},
        {Q_TOL, 0, "TOLERANCE", "THE LAST BAND IS THE TOLERANCE: GOLD 5%, SILVER 10%, NO BAND 20%, BROWN 1%, RED 2%."},
        {Q_READ4, 0, "READ 4 BANDS",
         "4 BANDS: DIGIT, DIGIT, MULTIPLIER, TOLERANCE. YELLOW VIOLET RED = 47 X100 = 4.7K. START AT THE END "
         "AWAY FROM THE GOLD BAND."},
        {Q_READ4, 1, "SMALL AND LARGE", "NOW ALL THE E24 VALUES, FROM 1 OHM TO 10M. A GOLD MULTIPLIER = X0.1."},
        {Q_READ5, 0, "READ 5 BANDS",
         "5 BANDS (1% PARTS): THREE DIGITS, THE MULTIPLIER, THEN THE TOLERANCE. BROWN BLACK BLACK RED = 100 X100 = "
         "10K."},
        {Q_PICK, 0, "FIND THE PART", "A VALUE SHOWS. TAP THE RESISTOR THAT HAS THIS VALUE."},
        {Q_SMD3, 0, "SMD: 3 DIGITS",
         "CHIP RESISTORS HAVE A NUMBER CODE. 472 = 47 AND 2 ZEROS = 4.7K. R IS THE DECIMAL POINT: 4R7 = 4.7 OHMS."},
        {Q_SMD4, 0, "SMD: 4 DIGITS", "1% CHIPS HAVE 4 DIGITS: 4701 = 470 AND 1 ZERO = 4.7K. 10R0 = 10.0 OHMS."},
        {Q_EIA, 0, "SMD: EIA-96",
         "SMALL 1% CHIPS: 2 DIGITS AND A LETTER. THE DIGITS ARE A PLACE IN THE E96 LIST (01 = 100, SEE CHART). "
         "THE LETTER IS THE MULTIPLIER: X=0.1 A=1 B=10 C=100 D=1K E=10K."},
        {Q_MIX, 0, "MASTER", "ALL THE KINDS OF QUESTIONS. MAKE A LONG SERIES OF CORRECT ANSWERS!"},
};
#define N_LEVELS ((int)(sizeof(LEVELS) / sizeof(LEVELS[0])))

typedef struct {
    uint8_t kind, ans, show;
    int8_t correct;
    uint8_t n_dig;  // A_DIGIT: the digits 0 .. n_dig - 1
    uint8_t bands[6], nb;
    int8_t hl;  // the marked band
    uint8_t swatch;
    char big[16];  // the digit, the value or the SMD code
    const char *prompt;
    char ch[4][16];          // A_CHOICE texts
    uint8_t chb[4][6], chn;  // A_PICK resistors
} quest_t;

static quest_t s_q;
static int s_lvl, s_lvl_max;  // the level now, the highest open level (NVS "ohm_lvl")
static uint8_t s_hist[20];
static int s_hist_n, s_hist_i;
static int s_fb, s_fb_pick;  // 0 none, 1 correct, 2 wrong
static int64_t s_fb_at, s_fb_until;
static bool s_intro, s_level_up;
static int s_last_key = -1;
static int s_streak, s_best;  // MASTER (NVS "ohm_best")

static int correct20(void)
{
    int c = 0;
    for (int i = 0; i < s_hist_n; i++) {
        c += s_hist[i];
    }
    return c;
}

static int pick(int n)
{
    int k = (int)rnd((uint32_t)n);
    for (int t = 0; t < 6 && k == s_last_key && n > 1; t++) {
        k = (int)rnd((uint32_t)n);
    }
    s_last_key = k;
    return k;
}

static bool near_same(float a, float b)
{
    return fabsf(a - b) <= fabsf(b) * 1e-3f;
}

// four value texts: v and three typical mistakes; sd = the digits of the code
static void value_choices(float v, int sd, int bands_n)
{
    float cand[10];
    int nc = 0, sig, e;
    sig_exp(v, sd, &sig, &e);
    cand[nc++] = v * 10;
    cand[nc++] = v / 10;
    if (sd == 2 && sig % 10 && sig / 10 != sig % 10) {
        cand[nc++] = (float)((sig % 10) * 10 + sig / 10) * p10(e);
    }
    if (sd == 3 && sig % 10 && sig / 100 != sig % 10) {
        cand[nc++] = (float)((sig % 10) * 100 + sig / 10 % 10 * 10 + sig / 100) * p10(e);
    }
    cand[nc++] = v * 100;
    cand[nc++] = v / 100;
    cand[nc++] = ser_nearest(v * 1.3f, SER_E12, 0);
    cand[nc++] = ser_nearest(v / 1.3f, SER_E12, 0);
    cand[nc++] = v * 1000;
    float got[4];
    int ng = 0;
    got[ng++] = v;
    for (int t = 0; t < 60 && ng < 4; t++) {
        float c = cand[rnd((uint32_t)nc)];
        if (t >= 40) {
            c = ser_nearest(v * p10((int)rnd(5) - 2) * (0.6f + 0.1f * (float)rnd(9)), SER_E24, 0);
        }
        if (c < 0.1f || c > 9.9e9f) {
            continue;
        }
        uint8_t b[6];
        if (bands_n && encode(c, bands_n, b) != 1) {
            continue;
        }
        bool dup = false;
        for (int i = 0; i < ng; i++) {
            dup = dup || near_same(c, got[i]);
        }
        if (!dup) {
            got[ng++] = c;
        }
    }
    while (ng < 4) {
        got[ng] = got[ng - 1] * 10;
        ng++;
    }
    int at = (int)rnd(4);
    float tmp = got[0];
    got[0] = got[at];
    got[at] = tmp;
    s_q.correct = (int8_t)at;
    for (int i = 0; i < 4; i++) {
        fmt_ohm(got[i], s_q.ch[i], sizeof(s_q.ch[i]), true);
        if (s_q.ans == A_PICK) {
            make_bands(got[i], 4, K_GOLD, s_q.chb[i]);
        }
    }
}

// four texts from a list: the correct one and three others
static void text_choices(const char *const *list, int n, int correct)
{
    int got[4], ng = 0;
    got[ng++] = correct;
    for (int t = 0; t < 50 && ng < 4; t++) {
        int c = (int)rnd((uint32_t)n);
        bool dup = false;
        for (int i = 0; i < ng; i++) {
            dup = dup || got[i] == c || strcmp(list[got[i]], list[c]) == 0;
        }
        if (!dup) {
            got[ng++] = c;
        }
    }
    while (ng < 4) {
        got[ng] = got[0];
        ng++;
    }
    int at = (int)rnd(4);
    int tmp = got[0];
    got[0] = got[at];
    got[at] = tmp;
    s_q.correct = (int8_t)at;
    for (int i = 0; i < 4; i++) {
        snprintf(s_q.ch[i], sizeof(s_q.ch[i]), "%s", list[got[i]]);
    }
}

static void new_question(void)
{
    int kind = LEVELS[s_lvl].kind, arg = LEVELS[s_lvl].arg;
    if (kind == Q_MIX) {
        int l = 4 + (int)rnd(11);
        kind = LEVELS[l].kind;
        arg = LEVELS[l].arg;
    }
    memset(&s_q, 0, sizeof(s_q));
    s_q.kind = (uint8_t)kind;
    s_q.hl = -1;
    switch (kind) {
    case Q_COL2DIG: {
        int k = pick(arg);
        s_q.show = SH_SWATCH;
        s_q.swatch = (uint8_t)k;
        s_q.ans = A_DIGIT;
        s_q.n_dig = (uint8_t)arg;
        s_q.correct = (int8_t)k;
        s_q.prompt = "WHICH DIGIT IS THIS COLOUR?";
        break;
    }
    case Q_DIG2COL: {
        int d = pick(10);
        s_q.show = SH_DIGIT;
        snprintf(s_q.big, sizeof(s_q.big), "%d", d);
        s_q.ans = A_COLOUR;
        s_q.correct = (int8_t)d;
        s_q.prompt = "WHICH COLOUR IS THIS DIGIT?";
        break;
    }
    case Q_MULT: {
        int k = pick(arg ? 9 : 7);
        if (arg && k >= 7) {
            k = k == 7 ? K_GOLD : K_SILVER;
        }
        s_q.show = SH_RES;
        s_q.nb = 4;
        s_q.bands[0] = (uint8_t)(1 + rnd(9));
        s_q.bands[1] = (uint8_t)rnd(10);
        s_q.bands[2] = (uint8_t)k;
        s_q.bands[3] = K_GOLD;
        s_q.hl = 2;
        s_q.ans = A_CHOICE;
        s_q.prompt = "THE MARKED BAND MULTIPLIES BY...";
        static const char *const ML[9] = {"X1", "X10", "X100", "X1K", "X10K", "X100K", "X1M", "X0.1", "X0.01"};
        int ci = k <= K_BLUE ? k : k == K_GOLD ? 7 : 8;
        text_choices(ML, arg ? 9 : 7, ci);
        break;
    }
    case Q_TOL: {
        static const uint8_t TK[5] = {K_BROWN, K_RED, K_GOLD, K_SILVER, K_NONE};
        static const char *const TL[5] = {"1%", "2%", "5%", "10%", "20%"};
        int i = pick(5);
        s_q.show = SH_RES;
        s_q.nb = 4;
        make_bands(ser_nearest((float)(10 + rnd(90)) * p10((int)rnd(4)), SER_E12, 0), 4, TK[i], s_q.bands);
        s_q.hl = 3;
        s_q.ans = A_CHOICE;
        s_q.prompt = "THE TOLERANCE OF THIS PART IS...";
        text_choices(TL, 5, i);
        break;
    }
    case Q_READ4:
    case Q_PICK: {
        int s = (kind == Q_READ4 && arg) ? SER_E24 : SER_E12;
        int e = (kind == Q_READ4 && arg) ? (int)rnd(7) - 1 : (int)rnd(5);
        float v = (float)ser_m3(s, (int)rnd((uint32_t)ser_n(s))) * p10(e - 1);
        static const uint8_t T4[3] = {K_GOLD, K_SILVER, K_BROWN};
        if (kind == Q_PICK) {
            s_q.show = SH_VALUE;
            fmt_ohm(v, s_q.big, sizeof(s_q.big), true);
            s_q.ans = A_PICK;
            s_q.prompt = "TAP THE RESISTOR WITH THIS VALUE";
        } else {
            s_q.show = SH_RES;
            s_q.nb = 4;
            make_bands(v, 4, T4[rnd(3)], s_q.bands);
            s_q.ans = A_CHOICE;
            s_q.prompt = "READ THE VALUE";
        }
        value_choices(v, 2, kind == Q_PICK ? 4 : 0);
        break;
    }
    case Q_READ5: {
        int e = (int)rnd(5) - 1;
        float v = (float)E96[rnd(96)] * p10(e);
        s_q.show = SH_RES;
        s_q.nb = 5;
        make_bands(v, 5, rnd(2) ? K_BROWN : K_RED, s_q.bands);
        s_q.ans = A_CHOICE;
        s_q.prompt = "READ THE VALUE (5 BANDS)";
        value_choices(v, 3, 0);
        break;
    }
    case Q_SMD3:
    case Q_SMD4:
    case Q_EIA: {
        float v = 4700;
        char c3[24], c4[24], ce[24];
        for (int t = 0; t < 20; t++) {
            if (kind == Q_SMD3) {
                v = (float)E24[rnd(24)] * p10((int)rnd(8) - 1);
            } else {
                v = (float)E96[rnd(96)] * p10((int)rnd(kind == Q_EIA ? 7 : 6) - 2);
            }
            smd_codes(v, c3, c4, ce);
            const char *c = kind == Q_SMD3 ? c3 : kind == Q_SMD4 ? c4 : ce;
            if (c[0]) {
                snprintf(s_q.big, sizeof(s_q.big), "%s", c);
                break;
            }
        }
        if (!s_q.big[0]) {
            v = 4700;
            snprintf(s_q.big, sizeof(s_q.big), kind == Q_SMD3 ? "472" : kind == Q_SMD4 ? "4701" : "68B");
            if (kind == Q_EIA) {
                v = 4990;
            }
        }
        s_q.show = SH_SMD;
        s_q.ans = A_CHOICE;
        s_q.prompt = "THE VALUE OF THIS CHIP IS...";
        value_choices(v, kind == Q_SMD3 ? 2 : 3, 0);
        break;
    }
    default:
        break;
    }
}

static void learn_start(void)
{
    s_lvl = s_lvl_max;
    s_hist_n = s_hist_i = 0;
    s_fb = 0;
    s_intro = true;
    s_level_up = false;
    s_streak = 0;
    new_question();
}

static void learn_answer(int a)
{
    if (s_fb) {
        return;
    }
    bool ok = a == s_q.correct;
    s_fb = ok ? 1 : 2;
    s_fb_pick = a;
    s_fb_at = s_now;
    s_fb_until = s_now + (ok ? 650 : 2200);
    s_hist[s_hist_i] = ok ? 1 : 0;
    s_hist_i = (s_hist_i + 1) % 20;
    if (s_hist_n < 20) {
        s_hist_n++;
    }
    sfx_play(ok ? SFX_COIN : SFX_BUMP);
    if (s_lvl == N_LEVELS - 1) {
        s_streak = ok ? s_streak + 1 : 0;
        if (s_streak > s_best) {
            s_best = s_streak;
            store_set("ohm_best", (uint32_t)s_best);
        }
    }
    if (ok && s_lvl == s_lvl_max && s_lvl_max < N_LEVELS - 1 && s_hist_n >= 20 && correct20() >= 18) {
        s_lvl_max++;
        store_set("ohm_lvl", (uint32_t)s_lvl_max);
        s_level_up = true;
    }
}

static void learn_update(void)
{
    if (!s_fb || s_now < s_fb_until) {
        return;
    }
    s_fb = 0;
    if (s_level_up) {
        s_level_up = false;
        s_lvl = s_lvl_max;
        s_hist_n = s_hist_i = 0;
        s_intro = true;
        sfx_play(SFX_LEVEL_UP);
        banner("A NEW LEVEL IS OPEN", C_GREEN, 1500);
    }
    new_question();
}

static int ans_count(int *cols, int *rows)
{
    switch (s_q.ans) {
    case A_DIGIT:
    case A_COLOUR:
        *cols = 5;
        *rows = 2;
        return 10;
    default:
        *cols = 2;
        *rows = 2;
        return 4;
    }
}

static void render_learn(void)
{
    R(0, 0, SW, TOP_H, C_PANEL);
    R(0, TOP_H - 2, SW, 2, C_GREEN);
    button(BACK_RC, "BACK", NULL, C_DIM, false);
    char t[32];
#if BOARD_BAR_SCREEN
    snprintf(t, sizeof(t), "LEARN %d", s_lvl + 1);
    TXT(BACK_W + 8, 13, t, 2, C_GREEN);
    int hx = 200, hs = 12;
#else
    snprintf(t, sizeof(t), "L%d", s_lvl + 1);
    TXT(BACK_RC.x + BACK_RC.w + OX(6), (TOP_H - 7 * OS(2)) / 2, t, OS(2), C_GREEN);
    int hx = BACK_RC.x + OX(104), hs = OX(7);
#endif
    // the last 20 answers (the newest at the right)
    for (int i = 0; i < 20; i++) {
        int k = i - (20 - s_hist_n);
        uint16_t c = C_DARK;
        if (k >= 0) {
            c = s_hist[(s_hist_i - s_hist_n + k + 40) % 20] ? C_GREEN : C_RED;
        }
        R(hx + i * hs, OY(12), hs - 3, OY(10), c);
    }
    snprintf(t, sizeof(t), "%d OPEN", s_lvl_max + 1);
    button(bar_btn(0, 1), "LEVELS", t, C_GREEN, false);

    rc_t q = LN_Q;
    R(q.x, q.y, q.w, q.h, C_BG);
    if (s_intro) {
        R(4, TOP_H + 4, SW - 8, SH - TOP_H - 8, C_PANEL);
        gfx_frame_rect(4, TOP_H + 4, SW - 8, SH - TOP_H - 8, 1, C_GREEN);
        snprintf(t, sizeof(t), "LEVEL %d: %s", s_lvl + 1, LEVELS[s_lvl].name);
        TXT(OX(14), TOP_H + OY(12), t, OS(2), C_GREEN);
        int y = wrap(OX(14), TOP_H + OY(34), SW - OX(28), LEVELS[s_lvl].text, OS(1), C_WHITE, OY(12));
        if (s_lvl == N_LEVELS - 1) {
            snprintf(t, sizeof(t), "BEST SERIES: %d", s_best);
            TXT(OX(14), y + OY(4), t, OS(1), C_AMBER);
        } else {
#if BOARD_UI_TABLET
            wrap(OX(14), y + OY(4), SW - OX(28), "18 OF THE LAST 20 CORRECT OPENS THE NEXT LEVEL.", OS(1), C_DIM,
                 OY(12));
#else
            TXT(OX(14), y + OY(4), "18 OF THE LAST 20 CORRECT OPENS THE NEXT LEVEL.", OS(1), C_DIM);
#endif
        }
        TXT_IN(0, SW, SH - OY(20), "TAP TO START", OS(1), C_GREEN);
        return;
    }
    // the question
#if BOARD_BAR_SCREEN
    int py = q.y + q.h - 14;
#else
    int py = q.y + q.h - OY(12);
#endif
    switch (s_q.show) {
    case SH_SWATCH: {
        int w = q.w / 2, h = q.h - OY(34);
        rc_t r = RC(q.x + (q.w - w) / 2, q.y + OY(6), w, h);
        R(r.x - OY(6), r.y - OY(6), r.w + OY(12), r.h + OY(12), rgb(196, 176, 140));  // a light card: black shows
        swatch(r, s_q.swatch, NULL, true, false);
        break;
    }
    case SH_DIGIT:
        TXT_IN(q.x, q.w, q.y + OY(8), s_q.big, BOARD_BAR_SCREEN ? 12 : OS(8), C_WHITE);
        break;
    case SH_RES:
        draw_res(q.x + OX(6), q.y + q.h / 2 - OY(8), q.w - OX(12), BOARD_BAR_SCREEN ? 50 : OY(40), s_q.bands, s_q.nb,
                 s_q.hl);
        break;
    case SH_VALUE:
        text_fit(q.x, q.w, q.y + OY(22), s_q.big, BOARD_BAR_SCREEN ? 5 : OS(4), C_WHITE);
        break;
    case SH_SMD: {
        int w = q.w * 2 / 3, h = q.h - OY(36);
        draw_chip(RC(q.x + (q.w - w) / 2, q.y + OY(8), w, h), s_q.big);
        break;
    }
    default:
        break;
    }
    if (s_q.prompt) {
        text_fit(q.x, q.w, py, s_q.prompt, OS(1), C_DIM);
    }
    if (s_q.kind == Q_COL2DIG && s_fb) {
        text_fit(q.x, q.w, py - OY(12), KNAME[s_q.swatch], OS(1), C_WHITE);
    }

    // the answers
    int cols, rows, n = ans_count(&cols, &rows);
    for (int i = 0; i < n; i++) {
        rc_t c = grid(LN_A, cols, rows, i);
        bool good = s_fb && i == s_q.correct;
        bool bad = s_fb == 2 && i == s_fb_pick;
        switch (s_q.ans) {
        case A_DIGIT: {
            char d[4];
            snprintf(d, sizeof(d), "%d", i);
            bool on = i < s_q.n_dig;
            button(c, d, NULL, on ? C_GREEN : C_DARK, false);
            if (!on) {
                gfx_darken_rect(c.x + 2, c.y + 2, c.w - 4, c.h - 4, 1);
            }
            break;
        }
        case A_COLOUR:
            swatch(c, i, NULL, true, false);
            break;
        case A_CHOICE:
            button(c, s_q.ch[i], NULL, C_GREEN, false);
            break;
        default:
            R(c.x + 2, c.y + 2, c.w - 4, c.h - 4, C_PANEL);
            draw_res(c.x + OX(6), c.y + c.h / 2, c.w - OX(12), c.h / 2, s_q.chb[i], 4, -1);
            if (s_fb) {
                text_fit(c.x, c.w, c.y + c.h - OY(11), s_q.ch[i], OS(1), C_DIM);
            }
            break;
        }
        if (good) {
            gfx_frame_rect(c.x + 1, c.y + 1, c.w - 2, c.h - 2, 3, C_GREEN);
        }
        if (bad) {
            gfx_frame_rect(c.x + 1, c.y + 1, c.w - 2, c.h - 2, 3, C_RED);
        }
    }
}

// LEVELS (after the confirm): go through the open levels (it resets the streak and the history)
static void learn_next_level(void)
{
    s_lvl = s_lvl >= s_lvl_max ? 0 : s_lvl + 1;
    s_hist_n = s_hist_i = 0;
    s_fb = 0;
    s_level_up = false;
    s_intro = true;
    s_streak = 0;
    new_question();
    sfx_play(SFX_CLICK);
}

static void learn_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    int x = ev->x, y = ev->y;
    if (in_rc(bar_btn(0, 1), x, y)) {
        conf_open(CONF_LEVEL);  // (the YES button goes on: learn_next_level)
        return;
    }
    if (s_intro) {
        s_intro = false;
        sfx_play(SFX_CLICK);
        return;
    }
    if (s_fb) {
        // a tap ends a long feedback (after a short time to see it)
        if (s_fb == 2 && s_now - s_fb_at > 500) {
            s_fb_until = s_now;
        }
        return;
    }
    int cols, rows, n = ans_count(&cols, &rows);
    for (int i = 0; i < n; i++) {
        if (in_rc(grid(LN_A, cols, rows, i), x, y)) {
            if (s_q.ans == A_DIGIT && i >= s_q.n_dig) {
                return;
            }
            learn_answer(i);
            return;
        }
    }
}

// ---------------------------------------------------------------- CALC

// the rows of CALC (for the thumb): the 3.49 has 2 x 2 cells (a name, then - value + with buttons of 46 x 44);
// the 320 frame has rows of 41 px
static rc_t calc_row(int i)
{
#if BOARD_BAR_SCREEN
    return RC((i % 2) * 166, 42 + (i / 2) * 66, 164, 64);
#else
    return RC(0, OY(44) + i * CALC_ROW_H, OX(320), CALC_ROW_H);
#endif
}

static rc_t calc_btn(int i, int dir)
{
    rc_t r = calc_row(i);
#if BOARD_BAR_SCREEN
    return dir < 0 ? RC(r.x + 2, r.y + 18, 46, 44) : RC(r.x + r.w - 48, r.y + 18, 46, 44);
#else
    return dir < 0 ? RC(OX(110), r.y, OX(56), r.h) : RC(OX(262), r.y, OX(56), r.h);
#endif
}

static int calc_rows(void)
{
    return s_calc_page == 0 ? 4 : 3;
}

static void div_solve(void)
{
    float vin = (float)s_vin / 10.0f, vout = (float)s_vout / 10.0f;
    int s = s_dser == 0 ? SER_E12 : s_dser == 1 ? SER_E24 : SER_E96;
    float best = 1e9f;
    for (int dec = 3; dec <= 4; dec++) {
        for (int i = 0; i < ser_n(s); i++) {
            float r2 = (float)ser_m3(s, i) * p10(dec - 2);
            float r1 = ser_nearest(r2 * (vin / vout - 1), s, 0);
            float vo = vin * r2 / (r1 + r2);
            float err = fabsf(vo - vout);
            if (err < best - 1e-6f) {
                best = err;
                s_r1 = r1;
                s_r2 = r2;
                s_dvo = vo;
            }
        }
    }
}

static void calc_step(int row, int dir)
{
    int big = s_hold_n > 10 ? 10 : 1;
    if (s_calc_page == 0) {
        switch (row) {
        case 0:
            s_led_vs = (s_led_vs + dir + N_LED_VS) % N_LED_VS;
            break;
        case 1:
            s_led_t = (s_led_t + dir + N_LEDS) % N_LEDS;
            break;
        case 2:
            s_led_ma = (s_led_ma + dir + N_LED_MA) % N_LED_MA;
            break;
        default:
            s_led_n += dir;
            if (s_led_n < 1) {
                s_led_n = 1;
            }
            if (s_led_n > 8) {
                s_led_n = 8;
            }
            break;
        }
    } else {
        switch (row) {
        case 0:
            s_vin += dir * big;
            if (s_vin < 5) {
                s_vin = 5;
            }
            if (s_vin > 300) {
                s_vin = 300;
            }
            if (s_vout > s_vin - 1) {
                s_vout = s_vin - 1;
            }
            break;
        case 1:
            s_vout += dir * big;
            if (s_vout < 1) {
                s_vout = 1;
            }
            if (s_vout > s_vin - 1) {
                s_vout = s_vin - 1;
            }
            break;
        default:
            s_dser = (s_dser + dir + 3) % 3;
            break;
        }
        div_solve();
    }
    sfx_play(SFX_TICK);
}

static void calc_press(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        for (int i = 0; i < calc_rows(); i++) {
            for (int d = -1; d <= 1; d += 2) {
                if (in_rc(calc_btn(i, d), ev->x, ev->y)) {
                    s_hold_n = 0;  // (the step comes at the lift, or the repeat after 450 ms)
                    s_hold_row = i;
                    s_hold_dir = d;
                    s_hold_next = s_now + 450;
                    return;
                }
            }
        }
    } else if (ev->kind == IN_MOVE && s_hold_row >= 0) {
        if (!in_rc(calc_btn(s_hold_row, s_hold_dir), ev->x, ev->y)) {
            s_hold_row = -1;
        }
    }
}

static void calc_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    for (int p = 0; p < 2; p++) {
        if (in_rc(bar_btn(p, 2), ev->x, ev->y) && s_calc_page != p) {
            s_calc_page = p;
            s_hold_row = -1;
            div_solve();
            sfx_play(SFX_CLICK);
            return;
        }
    }
}

static const char *watt_part(float p)
{
    if (p < 0.0625f) {
        return "1/8 W";
    }
    if (p < 0.125f) {
        return "1/4 W";
    }
    if (p < 0.25f) {
        return "1/2 W";
    }
    if (p < 0.5f) {
        return "1 W";
    }
    if (p < 1.0f) {
        return "2 W";
    }
    return "5 W OR MORE";
}

static void fmt_power(float p, char *o, int n)
{
    if (p < 1.0f) {
        snprintf(o, n, "%d MW", (int)lroundf(p * 1000));
    } else {
        snprintf(o, n, "%.1f W", (double)p);
    }
}

static void render_calc(void)
{
    top_bar("CALC", C_AMBER);
    button(bar_btn(0, 2), "LED", NULL, C_AMBER, s_calc_page == 0);
    button(bar_btn(1, 2), "DIVIDER", NULL, C_AMBER, s_calc_page == 1);
    char names[4][16], vals[4][16];
    int nr = calc_rows();
    if (s_calc_page == 0) {
        snprintf(names[0], 16, "SUPPLY");
        snprintf(vals[0], 16, "%.1f V", (double)LED_VS[s_led_vs]);
        snprintf(names[1], 16, "LED");
        snprintf(vals[1], 16, "%s", LEDS[s_led_t].name);
        snprintf(names[2], 16, "CURRENT");
        snprintf(vals[2], 16, "%d MA", LED_MA[s_led_ma]);
#if BOARD_UI_TABLET
        snprintf(names[3], 16, "LEDS (SERIES)");  // (the Tab5: fits at text size 2)
#else
        snprintf(names[3], 16, "LEDS IN SERIES");
#endif
        snprintf(vals[3], 16, "%d", s_led_n);
    } else {
        snprintf(names[0], 16, "VIN");
        snprintf(vals[0], 16, "%.1f V", (double)s_vin / 10.0);
        snprintf(names[1], 16, "VOUT");
        snprintf(vals[1], 16, "%.1f V", (double)s_vout / 10.0);
        snprintf(names[2], 16, "SERIES");
        snprintf(vals[2], 16, "%s", s_dser == 0 ? "E12" : s_dser == 1 ? "E24" : "E96");
    }
    for (int i = 0; i < nr; i++) {
        rc_t r = calc_row(i);
        R(r.x, r.y + r.h - 1, r.w, 1, C_PANEL);
        rc_t bm = calc_btn(i, -1), bp = calc_btn(i, 1);
#if BOARD_BAR_SCREEN
        text_fit(r.x + 4, r.w - 8, r.y + 5, names[i], 1, C_DIM);
#else
        text_fit(TX - OX(4), bm.x - TX, r.y + (r.h - 7 * OS(1)) / 2, names[i], OS(1), C_DIM);
#endif
        button(bm, "-", NULL, C_AMBER, s_hold_row == i && s_hold_dir < 0);
        button(bp, "+", NULL, C_AMBER, s_hold_row == i && s_hold_dir > 0);
        text_fit(bm.x + bm.w + 2, bp.x - bm.x - bm.w - 4, bm.y + (bm.h - 7 * OS(2)) / 2, vals[i], OS(2), C_WHITE);
    }
#if BOARD_BAR_SCREEN
    int x0 = 340, y = 46, w = SW - 344, lh = 14;
#else
    int x0 = TX, y = calc_row(nr - 1).y + calc_row(nr - 1).h + OY(2), w = SW - 2 * TX, lh = CALC_LH;
#endif
    char line[64], a[20], b[20], p[16];
    if (s_calc_page == 0) {
        float vs = LED_VS[s_led_vs], drop = LEDS[s_led_t].vf * (float)s_led_n;
        float i = (float)LED_MA[s_led_ma] / 1000.0f;
        if (vs - drop < 0.2f) {
            wrap(x0, y, w, "THE SUPPLY IS TOO LOW FOR THESE LEDS. USE FEWER LEDS IN SERIES OR A HIGHER SUPPLY.", OS(1),
                 C_RED, lh);
            return;
        }
        float rx = (vs - drop) / i;
        float rs = ser_nearest(rx, SER_E24, 1);
        float ia = (vs - drop) / rs, pw = ia * ia * rs;
        fmt_ohm(rx, a, sizeof(a), true);
        fmt_ohm(rs, b, sizeof(b), true);
        uint8_t bands[6];
        make_bands(rs, 4, K_GOLD, bands);
#if BOARD_BAR_SCREEN
        snprintf(line, sizeof(line), "USE %s", b);
        TXT(x0, y, line, 3, C_WHITE);
        snprintf(line, sizeof(line), "EXACT %s, THE NEXT E24 VALUE ABOVE", a);
        TXT(x0, y + 26, line, 1, C_DIM);
        draw_res(x0, y + 50, 150, 18, bands, 4, -1);
        y += 66;
#else
        snprintf(line, sizeof(line), "USE %s (EXACT %s)", b, a);
        TXT(x0, y, line, OS(1), C_WHITE);
        draw_res(SW - OX(110), y + OY(3), OX(106), OY(14), bands, 4, -1);
        y += lh;
#endif
        fmt_power(pw, p, sizeof(p));
        snprintf(line, sizeof(line), "CURRENT %.1f MA, LED DROP %.1f V", (double)(ia * 1000), (double)drop);
        TXT(x0, y, line, OS(1), C_WHITE);
        snprintf(line, sizeof(line), "POWER %s: A %s PART", p, watt_part(pw));
        TXT(x0, y + lh, line, OS(1), C_AMBER);
#if BOARD_BAR_SCREEN
        TXT(x0, y + 2 * lh, "R = (SUPPLY - LED DROP) / CURRENT", 1, C_DARK);
#endif
    } else {
        int ser = s_dser == 2 ? 5 : 4;
        uint8_t b1[6], b2[6];
        make_bands(s_r1, ser, ser == 5 ? K_BROWN : K_GOLD, b1);
        make_bands(s_r2, ser, ser == 5 ? K_BROWN : K_GOLD, b2);
        fmt_ohm(s_r1, a, sizeof(a), true);
        fmt_ohm(s_r2, b, sizeof(b), true);
        float vout = (float)s_vout / 10.0f;
        float err = (s_dvo - vout) / vout * 100.0f;
#if BOARD_BAR_SCREEN
        snprintf(line, sizeof(line), "R1 (TOP) %s", a);
        TXT(x0, y, line, 2, C_WHITE);
        draw_res(x0 + 190, y + 7, 110, 16, b1, ser, -1);
        snprintf(line, sizeof(line), "R2 (LOW) %s", b);
        TXT(x0, y + 24, line, 2, C_WHITE);
        draw_res(x0 + 190, y + 31, 110, 16, b2, ser, -1);
        y += 52;
#else
        snprintf(line, sizeof(line), "R1 (TOP) %s", a);
        TXT(x0, y, line, OS(1), C_WHITE);
        draw_res(SW - OX(110), y + OY(3), OX(106), OY(14), b1, ser, -1);
        snprintf(line, sizeof(line), "R2 (LOW) %s", b);
        TXT(x0, y + lh + OY(6), line, OS(1), C_WHITE);
        draw_res(SW - OX(110), y + lh + OY(9), OX(106), OY(14), b2, ser, -1);
        y += 2 * lh + OY(10);
#endif
        snprintf(line, sizeof(line), "VOUT %.2f V (%+.1f%%)", (double)s_dvo, (double)err);
        TXT(x0, y, line, OS(1), fabsf(err) < 1.0f ? C_GREEN : C_AMBER);
        float cur = (float)s_vin / 10.0f / (s_r1 + s_r2) * 1000.0f;
        snprintf(line, sizeof(line), "CURRENT %.2f MA", (double)cur);
        TXT(x0, y + lh, line, OS(1), C_DIM);
#if BOARD_BAR_SCREEN
        TXT(x0, y + 2 * lh, "VOUT = VIN X R2 / (R1 + R2)", 1, C_DARK);
#endif
    }
}

// ---------------------------------------------------------------- CHART

#if !BOARD_BAR_SCREEN
// one row of the E-series page of CHART (the 320 frame): the series name, then the values of one half of E24
static void chart_e_row(const char *name, int half, int row)
{
    char t[24];
    int y = OY(60) + half * OY(74) + row * OY(22);
    TXT(OX(2), y + OY(4), name, OS(1), C_COPPER);
    for (int j = 0; j < 12; j++) {
        int i = half * 12 + j;
        int step = row == 0 ? 1 : row == 1 ? 2 : 4;
        if (i % step) {
            continue;
        }
        snprintf(t, sizeof(t), "%d", E24[i]);
        TXT(OX(28) + j * OX(24), y, t, OS(2), row == 0 ? (i % 2 ? C_DIM : C_WHITE) : row == 1 ? C_CYAN : C_GREEN);
    }
}
#endif

static void render_chart(void)
{
    top_bar(BOARD_BAR_SCREEN ? "CHART" : NULL, C_DIM);
    static const char *const TAB[3] = {"COLOURS", "E6-E24", "EIA-96"};
    for (int i = 0; i < 3; i++) {
        button(bar_btn(i, 3), TAB[i], NULL, C_COPPER, s_chart_page == i);
    }
    char t[24];
    if (s_chart_page == 0) {
#if BOARD_BAR_SCREEN
        // two blocks: 0..6 and 7..12
        for (int blk = 0; blk < 2; blk++) {
            int bx = blk * 322, y0 = 44;
            TXT(bx + 46, y0, "COLOUR", 1, C_DIM);
            TXT(bx + 110, y0, "DIGIT", 1, C_DIM);
            TXT(bx + 152, y0, "MULT", 1, C_DIM);
            TXT(bx + 206, y0, "TOL", 1, C_DIM);
            TXT(bx + 256, y0, "PPM/K", 1, C_DIM);
            int k0 = blk ? 7 : 0, k1 = blk ? N_KOL : 7;
            for (int k = k0; k < k1; k++) {
                int y = 55 + (k - k0) * 16;
                swatch(RC(bx, y - 2, 42, 16), k, NULL, true, false);
                TXT(bx + 46, y + 3, KNAME[k], 1, C_WHITE);
#else
        {
            int bx = 0, y0 = OY(46);
            TXT(bx + OX(34), y0, "COLOUR", OS(1), C_DIM);
#if BOARD_UI_TABLET
            TXT(bx + OX(88), y0, "DIG", OS(1), C_DIM);  // (over the digits: not against COLOUR)
#else
            TXT(bx + OX(82), y0, "DIG", OS(1), C_DIM);
#endif
            TXT(bx + OX(108), y0, "MULT", OS(1), C_DIM);
            TXT(bx + OX(172), y0, "TOL", OS(1), C_DIM);
            TXT(bx + OX(226), y0, "PPM/K", OS(1), C_DIM);
            for (int k = 0; k < N_KOL; k++) {
                int y = OY(57) + k * OY(14);
                swatch(RC(bx, y - OY(2), OX(30), OY(14)), k, NULL, true, false);
                TXT(bx + OX(34), y + OY(2), KNAME[k], OS(1), C_WHITE);
#endif
                if (k <= K_WHITE) {
                    snprintf(t, sizeof(t), "%d", k);
#if BOARD_BAR_SCREEN
                    TXT(bx + 120, y + 3, t, 1, C_WHITE);
#else
                    TXT(bx + OX(88), y + OY(2), t, OS(1), C_WHITE);
#endif
                }
#if BOARD_BAR_SCREEN
                int yy = y + 3, xm = bx + 152, xt = bx + 206, xc = bx + 256;
#else
                int yy = y + OY(2), xm = bx + OX(108), xt = bx + OX(172), xc = bx + OX(226);
#endif
                if (k != K_NONE) {
                    TXT(xm, yy, KMUL[k], OS(1), C_WHITE);
                }
                if (KTOL[k] >= 0) {
                    fmt_tol(KTOL[k], t, sizeof(t));
                    TXT(xt, yy, t, OS(1), C_AMBER);
                }
                if (KTCR[k] >= 0) {
                    snprintf(t, sizeof(t), "%d", KTCR[k]);
                    TXT(xc, yy, t, OS(1), C_DIM);
                }
            }
        }
    } else if (s_chart_page == 1) {
        static const char *const SN[3] = {"E24", "E12", "E6"};
#if BOARD_BAR_SCREEN
        static const char *const ST[3] = {"5%", "10%", "20%"};
        TXT(4, 46, "THE SAME DIGITS IN EACH DECADE: 4.7, 47, 470, 4.7K, 47K ...", 1, C_DIM);
        for (int row = 0; row < 3; row++) {
            int y = 64 + row * 26;
            TXT(4, y + 2, SN[row], 1, C_COPPER);
            TXT(4, y + 12, ST[row], 1, C_DARK);
            for (int i = 0; i < 24; i++) {
                int step = row == 0 ? 1 : row == 1 ? 2 : 4;
                if (i % step) {
                    continue;
                }
                snprintf(t, sizeof(t), "%d", E24[i]);
                TXT(42 + i * 25, y, t, 2, row == 0 ? (i % 2 ? C_DIM : C_WHITE) : row == 1 ? C_CYAN : C_GREEN);
            }
        }
        TXT(4, 140, "E48 (2%) AND E96 (1%): 3 DIGITS, SEE EIA-96.", 1, C_DIM);
        TXT(4, 154, "A PART IN A SMALL SERIES IS ALSO IN ALL THE LARGER SERIES.", 1, C_DARK);
#else
        TXT(OX(4), OY(46), "THE SAME DIGITS IN EACH DECADE", OS(1), C_DIM);
        for (int half = 0; half < 2; half++) {
            for (int row = 0; row < 3; row++) {
                chart_e_row(SN[row], half, row);
            }
        }
        TXT(OX(4), OY(204), "E24 5%, E12 10%, E6 20%.", OS(1), C_DIM);
        TXT(OX(4), OY(218), "E48 (2%) AND E96 (1%): SEE EIA-96.", OS(1), C_DIM);
#endif
    } else {
#if BOARD_BAR_SCREEN
        TXT(4, 44, "EIA-96 CODE: INDEX + LETTER. Z=0.001 Y,R=0.01 X,S=0.1 A=1 B,H=10 C=100 D=1K E=10K F=100K", 1,
            C_DIM);
        int cols = 12, cw = 53, ch = 15, y0 = 57;
#else
#if BOARD_UI_TABLET
        TXT(6, 72, "Z=.001 Y,R=.01 X,S=.1 A=1 B,H=10", 2, C_DIM);
        TXT(6, 90, "C=100 D=1K E=10K F=100K. 01C = 10K", 2, C_DIM);
        int cols = 6, cw = 80, ch = 15, y0 = 108;  // (6 x 16 codes in the 480 x 360 page)
#else
        TXT(4, 47, "Z=.001 Y,R=.01 X,S=.1 A=1 B,H=10", 1, C_DIM);
        TXT(4, 57, "C=100 D=1K E=10K F=100K. 01C = 10K", 1, C_DIM);
        int cols = 8, cw = 40, ch = 14, y0 = 68;
#endif
#endif
        int rows = 96 / cols;
        for (int i = 0; i < 96; i++) {
            int c = i / rows, r = i % rows;
            int x = c * cw + OX(2), y = y0 + r * ch;
            snprintf(t, sizeof(t), "%02d", i + 1);
            TXT(x, y, t, OS(1), C_COPPER);
            snprintf(t, sizeof(t), "%d", E96[i]);
            TXT(x + OX(20), y, t, OS(1), C_WHITE);
        }
    }
}

static void chart_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    for (int i = 0; i < 3; i++) {
        if (in_rc(bar_btn(i, 3), ev->x, ev->y)) {
            s_chart_page = i;
            sfx_play(SFX_CLICK);
        }
    }
}

// ---------------------------------------------------------------- METER

static void meter_reset(void)
{
    s_m_n = s_m_i = s_stable_n = 0;
    s_rx = -1;
    s_stable = false;
    s_m_state = M_NONE;
}

static float median5(const float *a, int n)
{
    float t[5];
    memcpy(t, a, sizeof(float) * (size_t)n);
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && t[j - 1] > t[j]; j--) {
            float x = t[j];
            t[j] = t[j - 1];
            t[j - 1] = x;
        }
    }
    return t[n / 2];
}

static void meter_update(void)
{
    if (!s_meter_ok || s_now < s_m_next) {
        return;
    }
    s_m_next = s_now + 100;
    int mv;
    if (!board_ohm_read(&mv)) {
        s_m_state = M_NONE;
        return;
    }
    s_mv = mv;
    float vt = (float)s_vt, hi = vt * 0.965f;
    if (hi > 3080) {
        hi = 3080;
    }
    if ((float)mv >= hi) {
        if (s_m_state != M_OPEN) {
            meter_reset();
        }
        s_m_state = M_OPEN;
        return;
    }
    if ((float)mv < vt * 0.02f) {
        if (s_m_state != M_SHORT) {
            meter_reset();
        }
        s_m_state = M_SHORT;
        return;
    }
    float rx = REFS[s_ref] * (float)mv / (vt - (float)mv);
    s_m_buf[s_m_i] = rx;
    s_m_i = (s_m_i + 1) % 5;
    if (s_m_n < 5) {
        s_m_n++;
    }
    float med = median5(s_m_buf, s_m_n);
    if (s_rx <= 0 || fabsf(med - s_rx) > s_rx * 0.03f) {
        s_rx = med;
        s_stable_n = 0;
        s_stable = false;
    } else {
        s_rx += 0.3f * (med - s_rx);
        if (++s_stable_n == 8) {
            s_stable = true;
            sfx_tone(1568, 1568, 70, W_SINE, 30);
        }
    }
    s_m_state = M_OK;
}

static void meter_cal(void)
{
    float vt = (float)s_vt;
    if (s_m_state == M_OK && (float)s_mv > vt * 0.35f && (float)s_mv < vt * 0.65f) {
        int v = 2 * s_mv;
        if (v < 2900) {
            v = 2900;
        }
        if (v > 3500) {
            v = 3500;
        }
        s_vt = v;
        store_set("ohm_vt", (uint32_t)s_vt);
        char t[40];
        snprintf(t, sizeof(t), "DONE: THE DRIVE IS %.2f V", (double)s_vt / 1000.0);
        banner(t, C_GREEN, 2000);
        meter_reset();
    } else {
        banner("PUT IN A PART WITH THE REF VALUE", C_AMBER, 2500);
    }
}

static void render_meter(void)
{
    top_bar("METER", C_CYAN);
    char t[48], v[20];
    fmt_ohm(REFS[s_ref], v, sizeof(v), false);
    snprintf(t, sizeof(t), "REF %s", v);
    button(bar_btn(0, 2), t, NULL, C_CYAN, false);
    {
        // CAL: the fill of the hold
        rc_t c = bar_btn(1, 2);
        button(c, "CAL", s_cal_t0 ? NULL : "HOLD", C_CYAN, false);
        if (s_cal_t0) {
            int f = (int)((s_now - s_cal_t0) * (c.w - 8) / CAL_HOLD_MS);
            if (f > c.w - 8) {
                f = c.w - 8;
            }
            R(c.x + 4, c.y + c.h - 9, f, 5, C_AMBER);
        }
    }
    // the wiring: G9 -[REF]- G8 -[PART]- GND
    int y = TOP_H + 14;
    uint8_t rb[6], none[6] = {K_NONE, K_NONE, K_NONE, K_NONE, K_NONE, K_NONE};
    make_bands(REFS[s_ref], 4, K_GOLD, rb);
    TXT(4, y - 3, "G9", 1, C_WHITE);
    draw_res(18, y, 96, 14, rb, 4, -1);
    TXT(118, y - 3, "G8", 1, C_WHITE);
    draw_res(132, y, 96, 14, none, 4, -1);
    TXT(232, y - 3, "GND", 1, C_WHITE);
    TXT(42, y + 12, "REF", 1, C_DIM);
    TXT(160, y + 12, "PART", 1, C_DIM);
#if BOARD_BAR_SCREEN
    int vx = 270, vy = 40;
#else
    int vx = TX, vy = 100;
    TXT(TX, y + 24, "PORT B: G9, G8 AND GND (THE BLACK PORT)", 1, C_DARK);
#endif
    if (!s_meter_ok) {
        wrap(vx, vy, SW - vx - 4, "THIS BOARD HAS NO FREE PINS FOR THE METER.", 1, C_RED, 12);
        return;
    }
    switch (s_m_state) {
    case M_OPEN:
        TXT(vx, vy, "OPEN", 4, C_DIM);
        wrap(vx, vy + 36, SW - vx - 4, "NO PART, OR THE PART IS MORE THAN 15 X THE REF: USE A LARGER REF.", 1, C_DIM,
             12);
        break;
    case M_SHORT:
        TXT(vx, vy, "SHORT", 4, C_AMBER);
        wrap(vx, vy + 36, SW - vx - 4, "A SHORT, OR THE PART IS LESS THAN 1/50 OF THE REF: USE A SMALLER REF.", 1,
             C_AMBER, 12);
        break;
    case M_OK: {
        fmt_ohm(s_rx, v, sizeof(v), true);
        TXT(vx, vy, v, 4, s_stable ? C_WHITE : C_DIM);
        float nv = ser_nearest(s_rx, SER_E24, 0);
        char n[20];
        fmt_ohm(nv, n, sizeof(n), true);
        snprintf(t, sizeof(t), "NEAREST E24: %s (%+.1f%%)", n, (double)((s_rx - nv) / nv * 100.0f));
        TXT(vx, vy + 34, t, 1, C_CYAN);
        uint8_t b[6];
        if (make_bands(nv, 4, K_GOLD, b) > 0) {
            draw_res(vx, vy + 58, 150, 18, b, 4, -1);
        }
        float ratio = s_rx / REFS[s_ref];
        const char *hint = NULL;
        if (ratio > 8 && s_ref < 2) {
            hint = "A LARGER REF GIVES A BETTER READING";
        } else if (ratio < 0.12f && s_ref > 0) {
            hint = "A SMALLER REF GIVES A BETTER READING";
        }
        if (hint) {
            TXT(vx, vy + 74, hint, 1, C_AMBER);
        } else {
            TXT(vx, vy + 74, s_stable ? "STEADY" : "WAIT...", 1, s_stable ? C_GREEN : C_DIM);
        }
        break;
    }
    default:
        TXT(vx, vy, "---", 4, C_DIM);
        TXT(vx, vy + 36, "NO READING", 1, C_DIM);
        break;
    }
    snprintf(t, sizeof(t), "JOINT %d MV, DRIVE %d MV", s_mv, s_vt);
    TXT(vx, SH - 12, t, 1, C_DARK);
}

// CAL (for the thumb): a wrong part in the clips gives a wrong calibration, so CAL needs a hold of 1 s (the button
// fills); a tap only says so
static void meter_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN && in_rc(bar_btn(1, 2), ev->x, ev->y)) {
        s_cal_t0 = s_now > 0 ? s_now : 1;
        return;
    }
    if (ev->kind == IN_MOVE && s_cal_t0 && !in_rc(bar_btn(1, 2), ev->x, ev->y)) {
        s_cal_t0 = 0;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    if (in_rc(bar_btn(0, 2), ev->x, ev->y)) {
        s_ref = (s_ref + 1) % 3;
        store_set("ohm_ref", (uint32_t)s_ref);
        meter_reset();
        sfx_play(SFX_CLICK);
    } else if (in_rc(bar_btn(1, 2), ev->x, ev->y)) {
        banner("HOLD CAL FOR 1 S", C_AMBER, 1500);
    }
}

static void meter_hold(void)
{
    if (s_cal_t0 && s_now - s_cal_t0 >= CAL_HOLD_MS) {
        s_cal_t0 = 0;
        meter_cal();
    }
}

// ---------------------------------------------------------------- title

#define N_MODES (HAS_METER ? 7 : 6)

static rc_t title_btn(int i)
{
#if BOARD_BAR_SCREEN
    return RC(4 + i * 105, 104, 104, 66);
#elif BOARD_UI_TABLET
    return RC(4 + (i % 3) * 211, 72 + (i / 3) * 144, 209, 140);  // (6 modes: 3 x 2 big buttons)
#else
    return RC(4 + (i % 2) * 158, 40 + (i / 2) * 50, 154, 48);
#endif
}

static const float DEMO[] = {4700, 10000, 220, 1000, 100000, 330, 2200, 47000, 1.5f, 680000};
#define N_DEMO ((int)(sizeof(DEMO) / sizeof(DEMO[0])))

static void render_title(void)
{
    uint8_t b[6];
    char v[20], sub[24];
    make_bands(DEMO[s_demo], 4, K_GOLD, b);
    fmt_ohm(DEMO[s_demo], v, sizeof(v), true);
#if BOARD_BAR_SCREEN
    TXT(16 + 3, 12 + 3, "OHMS", 5, rgb(70, 40, 16));
    TXT(16, 12, "OHMS", 5, C_COPPER);
    TXT(150, 14, "RESISTOR COLOUR CODES", 1, C_WHITE);
    TXT(150, 28, "AND SMD CODES", 1, C_DIM);
    draw_res(320, 34, 230, 30, b, 4, -1);
    TXT(560, 27, v, 2, C_WHITE);
    TXT(16, 64, "READ: COLOURS TO VALUE. FIND: VALUE TO COLOURS.", 1, C_DIM);
    TXT(16, 78, "CALC: THE RESISTOR FOR AN LED, A VOLTAGE DIVIDER.", 1, C_DIM);
#else
#if BOARD_UI_TABLET
    TXT(14, 12, "OHMS", 6, C_COPPER);
    draw_res(200, 36, 280, 34, b, 4, -1);
    TXT(496, 26, v, 3, C_WHITE);
#else
    TXT(10, 6, "OHMS", 4, C_COPPER);
    draw_res(132, 8, 110, 14, b, 4, -1);
    TXT(132, 26, v, 1, C_WHITE);
#endif
#endif
    static const char *const LBL[7] = {"READ", "FIND", "SMD", "LEARN", "CALC", "CHART", "METER"};
    static const uint8_t HUE[7][3] = {
            {236, 150, 72}, {96, 210, 255},  {100, 230, 120}, {100, 230, 120},
            {255, 196, 72}, {170, 180, 210}, {96, 210, 255},
    };
    for (int i = 0; i < N_MODES; i++) {
        switch (i) {
        case 0:
            snprintf(sub, sizeof(sub), "BANDS TO VALUE");
            break;
        case 1:
            snprintf(sub, sizeof(sub), "VALUE TO BANDS");
            break;
        case 2:
            snprintf(sub, sizeof(sub), "CHIP CODES");
            break;
        case 3:
            snprintf(sub, sizeof(sub), "LEVEL %d OF %d", s_lvl_max + 1, N_LEVELS);
            break;
        case 4:
            snprintf(sub, sizeof(sub), "LED, DIVIDER");
            break;
        case 5:
            snprintf(sub, sizeof(sub), "ALL THE TABLES");
            break;
        default:
            snprintf(sub, sizeof(sub), "MEASURE A PART");
            break;
        }
        button(title_btn(i), LBL[i], sub, rgb(HUE[i][0], HUE[i][1], HUE[i][2]), false);
    }
}

static void start_mode(ostate_t m)
{
    s_state = m;
    s_hold_row = -1;
    s_drag_band = -1;
    s_conf = CONF_NONE;
    s_cal_t0 = 0;
    s_spage = 0;
    if (m == ST_LEARN) {
        learn_start();
    }
    if (m == ST_CALC) {
        div_solve();
    }
    if (m == ST_METER) {
        s_meter_ok = board_ohm_start();
        meter_reset();
        s_m_next = 0;
    }
    sfx_play(SFX_CLICK);
}

static void go_title(void)
{
    if (s_state == ST_METER) {
        board_ohm_stop();
    }
    s_meter_ok = false;
    s_state = ST_TITLE;
    s_hold_row = -1;
    s_drag_band = -1;
    sfx_play(SFX_BACK);
}

#if BOARD_UI_TABLET
static const char *const MODE_LBL[7] = {"READ", "FIND", "SMD", "LEARN", "CALC", "CHART", "METER"};
static const ostate_t MODE_ST[7] = {ST_READ, ST_FIND, ST_SMD, ST_LEARN, ST_CALC, ST_CHART, ST_METER};
static const uint8_t MODE_HUE[7][3] = {
        {236, 150, 72}, {96, 210, 255},  {100, 230, 120}, {100, 230, 120},
        {255, 196, 72}, {170, 180, 210}, {96, 210, 255},
};
// the Tab5: the rail of the modes at the right of the 480 px page (the mode now is lit)
static rc_t rail_btn(int i)
{
    int h = 360 / N_MODES;
    return RC(RAIL_X, i * h, 640 - RAIL_X, h);
}
static void draw_rail(void)
{
    R(RAIL_X, 0, 640 - RAIL_X, 360, C_BG);
    R(RAIL_X, 0, 2, 360, C_LINE);
    for (int i = 0; i < N_MODES; i++) {
        const uint8_t *c = MODE_HUE[i];
        button(rail_btn(i), MODE_LBL[i], NULL, rgb(c[0], c[1], c[2]), s_state == MODE_ST[i]);
    }
}
#endif

// ---------------------------------------------------------------- app

static void ohm_enter(int64_t now)
{
    s_now = now;
    s_state = ST_TITLE;
    s_banner_until = 0;
    s_hold_row = -1;
    s_drag_band = -1;
    s_meter_ok = false;
    s_lvl_max = (int)store_get("ohm_lvl", 0);
    if (s_lvl_max < 0 || s_lvl_max >= N_LEVELS) {
        s_lvl_max = 0;
    }
    s_best = (int)store_get("ohm_best", 0);
    s_ref = (int)(store_get("ohm_ref", 1) % 3);
    s_vt = (int)store_get("ohm_vt", 3300);
    if (s_vt < 2900 || s_vt > 3500) {
        s_vt = 3300;
    }
    int nb = (int)store_get("ohm_nb", 4);
    if (s_rb[0] == 0 && s_rb[1] == 0 && s_rb[2] == 0) {
        s_rn = nb >= 4 && nb <= 6 ? nb : 4;
        read_defaults();
    }
    s_demo_at = now;
    tune_play(TUNE_OHM);
}

static void ohm_leave(void)
{
    board_ohm_stop();
    s_meter_ok = false;
    s_hold_row = -1;
}

static void ohm_input_v(const input_event_t *ev);
static void ohm_input(const input_event_t *ev)
{
#if BOARD_ROUND
    input_event_t e = *ev;  // (the round view: the touch back to the 320 x 240 frame)
    int x = e.x, y = e.y;
    rvw_pt(&x, &y);
    if (y >= SH + 4 && (ev->kind == IN_DOWN || ev->kind == IN_TAP)) {
        if (ev->kind == IN_TAP && s_state != ST_TITLE) {
            x = BACK_RC.x + BACK_RC.w / 2;  // (the BACK key of the bottom cap: a tap on BACK)
            y = BACK_RC.y + BACK_RC.h / 2;
        } else {
            return;
        }
    }
    e.x = (int16_t)(x < 0 ? 0 : x < SW ? x : SW - 1);
    e.y = (int16_t)(y < 0 ? 0 : y < SH ? y : SH - 1);
    ohm_input_v(&e);
#else
    ohm_input_v(ev);
#endif
}
static void ohm_input_v(const input_event_t *ev)
{
    if (ev->kind == IN_UP) {
        // CALC: a short press steps at the lift (the mean point is near the last point), when the finger is still on
        // the button and the hold repeat did not start
        if (s_state == ST_CALC && s_hold_row >= 0 && s_hold_n == 0 &&
            in_rc(calc_btn(s_hold_row, s_hold_dir), ev->x, ev->y)) {
            calc_step(s_hold_row, s_hold_dir);
        }
        s_hold_row = -1;
        s_drag_band = -1;
        s_cal_t0 = 0;
    }
    if (s_conf && s_now < s_conf_until) {
        // the confirm is up: YES does the action, any other tap closes it
        if (ev->kind == IN_TAP) {
            if (in_rc(CONF_RC, ev->x, ev->y)) {
                if (s_conf == CONF_CLEAR && s_state == ST_READ) {
                    read_defaults();
                    sfx_play(SFX_CLICK);
                } else if (s_conf == CONF_LEVEL && s_state == ST_LEARN) {
                    learn_next_level();
                }
            }
            s_conf = CONF_NONE;
        }
        if (ev->kind != IN_UP) {
            return;
        }
    }
    s_conf = CONF_NONE;
#if BOARD_UI_TABLET
    if (s_state != ST_TITLE && ev->x >= RAIL_X) {
        // the rail of the modes: a tap opens that mode (the page does not get the touches of the rail)
        if (ev->kind != IN_TAP) {
            return;
        }
        for (int i = 0; i < N_MODES; i++) {
            if (in_rc(rail_btn(i), ev->x, ev->y) && s_state != MODE_ST[i]) {
                start_mode(MODE_ST[i]);
            }
        }
        return;
    }
#endif
    if (ev->kind == IN_TAP && s_state != ST_TITLE && in_rc(BACK_RC, ev->x, ev->y)) {
        go_title();
        return;
    }
    switch (s_state) {
    case ST_TITLE:
        if (ev->kind != IN_TAP) {
            return;
        }
        for (int i = 0; i < N_MODES; i++) {
            if (in_rc(title_btn(i), ev->x, ev->y)) {
                static const ostate_t M[7] = {ST_READ, ST_FIND, ST_SMD, ST_LEARN, ST_CALC, ST_CHART, ST_METER};
                start_mode(M[i]);
                return;
            }
        }
        break;
    case ST_READ:
        read_input(ev);
        break;
    case ST_FIND:
        find_input(ev);
        break;
    case ST_SMD:
        smd_input(ev);
        break;
    case ST_LEARN:
        learn_input(ev);
        break;
    case ST_CALC:
        calc_press(ev);
        calc_input(ev);
        break;
    case ST_CHART:
        chart_input(ev);
        break;
    case ST_METER:
        meter_input(ev);
        break;
    }
}

static void ohm_update(int64_t now)
{
    s_now = now;
    if (s_state == ST_TITLE && now - s_demo_at > 1600) {
        s_demo_at = now;
        s_demo = (s_demo + 1) % N_DEMO;
    }
    if (s_state == ST_CALC && s_hold_row >= 0 && now >= s_hold_next) {
        s_hold_n++;
        calc_step(s_hold_row, s_hold_dir);
        s_hold_next = now + (s_hold_n > 10 ? 60 : 110);
    }
    if (s_state == ST_LEARN) {
        learn_update();
    }
    if (s_state == ST_METER) {
        meter_update();
        meter_hold();
    }
    if (s_conf && now >= s_conf_until) {
        s_conf = CONF_NONE;
    }
}

static void ohm_render_page(void);

static void ohm_render(int64_t now)
{
    (void)now;
    gfx_fill(C_BG);
    ohm_render_page();
    draw_conf();
#if BOARD_ROUND
    // the round view: the name of the mode in the top cap, a BACK key in the bottom cap
    static const char *const NAME[] = {"OHMS", "READ", "FIND", "SMD", "LEARN", "CALC", "CHART", "METER"};
    if (s_state != ST_TITLE) {
        TXT_IN(0, SW, -24, NAME[s_state], 1, C_COPPER);
        R(96, SH + 4, 128, 28, C_PANEL);
        TXT_IN(96, 128, SH + 14, "BACK", 1, C_WHITE);
    }
#endif
}

static void ohm_render_page(void)
{
    switch (s_state) {
    case ST_TITLE:
        render_title();
        break;
    case ST_READ:
        render_read();
        break;
    case ST_FIND:
        render_find();
        break;
    case ST_SMD:
        render_smd();
        break;
    case ST_LEARN:
        render_learn();
        break;
    case ST_CALC:
        render_calc();
        break;
    case ST_CHART:
        render_chart();
        break;
    case ST_METER:
        render_meter();
        break;
    }
#if BOARD_UI_TABLET
    if (s_state != ST_TITLE) {
        draw_rail();
    }
#endif
    if (s_now < s_banner_until) {
        int w = TW(s_banner, OS(2)) + OX(24);
        if (w > SW - 8) {
            w = SW - 8;
        }
        int x = (SW - w) / 2, y = SH / 2 - OY(16);
        R(x, y, w, OY(32), C_PANEL2);
        gfx_frame_rect(x, y, w, OY(32), 2, s_banner_col);
        text_fit(x, w, y + (OY(32) - 7 * OS(2)) / 2, s_banner, OS(2), s_banner_col);
    }
}

#include "tficon/tficon.h"
// the menu icon: a resistor with its colour bands; the current flows through it (light points on the wires)
static void ohm_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_tile(x, y, s, tfi_hex(0x2ee8c8), tfi_hex(0x00695c));
    float u = s / 100.0f, ph = tfi_ph(2), cy = y + 60 * u;
    // the wires and the current
    tfd_rect(x + (int)(10 * u), (int)(cy - 1.7f * u), (int)(80 * u), (int)(3.4f * u) > 1 ? (int)(3.4f * u) : 1,
             tfi_hex(0xe6ecf2), 256);
    for (int k = 0; k < 3; k++) {
        float q = ph + k / 3.0f;
        q -= (float)(int)q;
        float px = x + (10 + q * 80) * u;
        if (px < x + 24 * u || px > x + 76 * u) {
            tfd_glow(px, cy, 6 * u, tfi_hex(0xfff59d), 230);
        }
    }
    // the body
    tfi_drop(x + (int)(24 * u), (int)(cy - 11 * u), (int)(52 * u), (int)(22 * u), (int)(10 * u), (int)(2.5f * u), 70);
    tfi_rbox(x + (int)(24 * u), (int)(cy - 11 * u), (int)(52 * u), (int)(22 * u), (int)(10 * u), tfi_hex(0xffe8b8),
             tfi_hex(0xd8a860), 256);
    static const uint32_t BAND[4] = {0xffd400, 0x8a2be2, 0xe52020, 0xd4a020};
    for (int k = 0; k < 4; k++) {
        int bx = x + (int)((33 + k * 9 + (k == 3 ? 5 : 0)) * u);
        tfd_rect(bx, (int)(cy - 10 * u), (int)(4.4f * u), (int)(20 * u), tfi_hex(BAND[k]), 256);
    }
    tfd_rect(x + (int)(30 * u), (int)(cy - 8 * u), (int)(40 * u), (int)(3 * u) > 1 ? (int)(3 * u) : 1,
             rgb(255, 255, 255), 90);
    // the omega sign above it
    float ox = x + 50 * u, oy = y + 30 * u;
    tfi_arc(ox, oy, 10 * u, 3.6f * u, 2.35f, 7.07f, rgb(255, 255, 255), 256);
    tfi_line(ox - 12 * u, oy + 9 * u, ox - 6 * u, oy + 9 * u, 3.6f * u, true, rgb(255, 255, 255), 256);
    tfi_line(ox + 6 * u, oy + 9 * u, ox + 12 * u, oy + 9 * u, 3.6f * u, true, rgb(255, 255, 255), 256);
    tfi_end();
}

static bool ohm_awake(void)
{
    return s_state == ST_METER;
}

const app_t app_ohm = {
        .name = "OHMS",
        .landscape = true,
        .round = APP_ROUND_HI,  // (a round panel: the round view, ohm_rv.inc)
        .needs = 0,
        .enter = ohm_enter,
        .leave = ohm_leave,
        .input = ohm_input,
        .update = ohm_update,
        .render = ohm_render,
        .draw_icon = ohm_icon,
        .awake = ohm_awake,
};
