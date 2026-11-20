// SKY (UTIL): learn the night sky, on the landscape screen (640 x 172 on the 3.49, 320 x 240 on the CoreS3).
// The task: the app task of TFN OS calls the hooks of app_sky.
// The memory: the file statics; no heap. The stars and the lines are constant tables (sky_data.c).
//
// - The board is a window into the sky: hold it up and turn around, and the view follows the
//   board (the gravity gives the altitude and the roll; the gyro gives the turn around the
//   vertical, see imu_gyro_sum() below). The board has no compass: the view starts to the
//   south, and the N button sets the north. Drag to turn the view (with no IMU, the drag is
//   the only control).
// - LEARN: the constellations one by one, from the easiest (Orion, the Great Bear ...). A card
//   shows the figure, the main stars and a short fact; then a practice. When 9 of the last 10
//   answers are correct, the next constellation opens (40 in all).
// - NAME: the view goes to a constellation; tap its name. Levels: with the lines, turned,
//   only the stars, then all 88 constellations. 3 lives.
// - STARS: name the bright stars, or tap the star that the board names.
// - FIND: "find Orion": turn the board until the figure is in the circle. An arrow and a beep
//   that goes faster as you come near guide you.
// - EXPLORE: the sky now, with the names, the planets, the Moon and the Sun. Tap a star for its
//   name, magnitude and distance; move the time by hours.
// - SETUP: the place (city presets or latitude and longitude), the time zone, and the date and
//   the hour when the board has no clock (with WiFi the clock comes from SNTP).
//
// The sky: a perspective (gnomonic) projection of the stars (to magnitude 4.6, sky_data.c),
// the constellation lines, a Milky Way band from a model of the galaxy, the horizon, the
// twilight colour from the Sun altitude, and an approximate model of the planets and the Moon.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "esp_random.h"
#include "esp_timer.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/mfont.h"
#include "tfn/uikit.h"
#include "tfn/input.h"
#include "tfn/settings.h"
#include "sky_data.h"
#include "tfn/tunes.h"
#include "tfn/wifi.h"

#if BOARD_UI_TABLET || BOARD_ROUND
// The Tab5: the 640 x 360 frame (0.17 mm a px); the round panel (APP_ROUND_HI, 466 or 412 px). The text of this app
// is 1.5 times there (the scale 1 is 2 times): these names go to the copies below in this file. TS() gives the
// scale on the screen.
#define TS(s) ((s) == 1 ? 2 : (s) * 3 / 2)
#define TAP_R 34  // the tap zone of a star or a body (px)
#else
#define TS(s) (s)
#define TAP_R 22
#endif

// THE ANTI-ALIASED TEXT (the 5 x 7 dot font is rough and hard to read on these screens): the Bungee faces of the
// firmware in
// place of the 5 x 7 dot font. A dot scale k (7 * k px high, 6 * k px a letter) takes the face of about the same width
// (MF_TINY, MF_N16, MF_N24, MF_N34); the capitals keep the middle of the dot line. The big faces have capitals only: a
// small letter shows as its capital. The Tab5 and the round panels use the scale TS() (1.5 times).
static const mfont_t *sky_face(int k)
{
    return k <= 1 ? &MF_TINY : k == 2 ? &MF_N16 : k == 3 ? &MF_N24 : &MF_N34;
}
static int sky_cp(int ch, const mfont_t *f)
{
    return (f != &MF_TINY && ch >= 'a' && ch <= 'z') ? ch - 32 : ch;
}
// the advance of a glyph in quarters of a px (a space: 0.3 of the capital height at least)
static int sky_adv4(const mfont_t *f, const mglyph_t *g, int ch)
{
    const int sp = f->asc * 6 / 5;
    return ch == ' ' && g->adv4 < sp ? sp : g->adv4;
}
static int sky_text_w(const char *s, int scale)
{
    const mfont_t *f = sky_face(TS(scale));
    int w4 = 0;
    for (; *s; s++) {
        const mglyph_t *g = mf_glyph(f, sky_cp((uint8_t)*s, f));
        w4 += g ? sky_adv4(f, g, *s) : 0;
    }
    return (w4 + 2) / 4;
}
// the text from x (the left end) in the dot line from y (its top)
static void sky_text(int x, int y, const char *s, int scale, uint16_t c)
{
    const int k = TS(scale);
    const mfont_t *f = sky_face(k);
    const int base = y + (7 * k + f->asc) / 2;
    int x4 = x * 4;
    for (; *s; s++) {
        const mglyph_t *g = mf_glyph(f, sky_cp((uint8_t)*s, f));
        if (!g) {
            continue;
        }
        const int gx = (x4 + 2) / 4 + g->ox, gy = base + g->oy;
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
        x4 += sky_adv4(f, g, *s);
    }
}
static void sky_text_c(int y, const char *s, int scale, uint16_t c)
{
    sky_text((gfx_w - sky_text_w(s, scale)) / 2, y, s, scale, c);
}
static void sky_text_ci(int x, int w, int y, const char *s, int scale, uint16_t c)
{
    sky_text(x + (w - sky_text_w(s, scale)) / 2, y, s, scale, c);
}
#define gfx_text           sky_text
#define gfx_text_width     sky_text_w
#define gfx_text_center    sky_text_c
#define gfx_text_center_in sky_text_ci

#if defined(ESP_PLATFORM) && __has_include("esp_sntp.h")
#include "esp_sntp.h"
#define SKY_SNTP 1
#endif

// The turn of the board about each physical axis from the start-up (rad, the gyro bias
// removed; tfn/imu.h). A weak reference keeps this file working when the function is not
// there (the drag then turns the view).
#if defined(ESP_PLATFORM)
void imu_gyro_sum(float a[3]) __attribute__((weak));
#define HAVE_GYRO_SUM() (imu_gyro_sum != NULL)
static void gyro_sum(float a[3])
{
    imu_gyro_sum(a);
}
#elif defined(SKY_TEST_GYRO)
void imu_gyro_sum(float a[3]);
#define HAVE_GYRO_SUM() 1
static void gyro_sum(float a[3])
{
    imu_gyro_sum(a);
}
#else
#define HAVE_GYRO_SUM() 0  // the host fuzz: no gyro, the drag turns the view
static void gyro_sum(float a[3])
{
    a[0] = a[1] = a[2] = 0;
}
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define PI_F 3.14159265f
#define DEG  0.017453293f

// ---------------------------------------------------------------- layout

typedef struct {
    int16_t x, y, w, h;
} rc_t;

#define RC(x, y, w, h) ((rc_t){(int16_t)(x), (int16_t)(y), (int16_t)(w), (int16_t)(h)})

#if BOARD_BAR_SCREEN
#define SW           640
#define SH           172
#define BACK_RC      RC(0, 0, 64, 40)
#define Q_VIEW_RC    RC(0, 0, 400, 172)  // the quiz view at the left, the answers at the right
#define Q_PANEL_X    400
#define CARD_VIEW_RC RC(0, 0, 400, 172)
#define CARD_PREV_RC RC(404, 126, 48, 44)
#define CARD_NEXT_RC RC(456, 126, 48, 44)
#define CARD_GO_RC   RC(508, 126, 130, 44)
#define R_AGAIN_RC   RC(150, 118, 160, 46)
#define R_MENU_RC    RC(330, 118, 160, 46)
#define TOOL_W       52
#define TOOL_H       44
#elif BOARD_UI_TABLET
// the Tab5: the layout of the 320 x 240 frame, 2 times in x and 1.5 times in y
#define SW           640
#define SH           360
#define BACK_RC      RC(0, 0, 112, 64)
#define TOOLS_R      636
#define Q_VIEW_RC    RC(0, 0, 640, 212)  // the quiz view at the top, the answers below
#define Q_PANEL_X    0
#define CARD_VIEW_RC RC(0, 0, 640, 198)
#define CARD_PREV_RC RC(4, 292, 100, 64)
#define CARD_NEXT_RC RC(112, 292, 100, 64)
#define CARD_GO_RC   RC(220, 292, 416, 64)
#define R_AGAIN_RC   RC(36, 272, 272, 72)
#define R_MENU_RC    RC(332, 272, 272, 72)
#define TOOL_W       92
#define TOOL_H       64
#elif BOARD_ROUND
// THE ROUND PANEL (the 1.75C 466 x 466, the Watcher 412 x 412: APP_ROUND_HI): the sky fills the circle. BACK is
// on the left arc, N on the top cap, + and - on the right arc, the time keys of EXPLORE on the bottom arc; the
// answers and the cards in the lower band. RU(): a value of the 466 layout on this panel.
#define RU(v)        ((v) * BOARD_ROUND_HI / 466)
#define SW           BOARD_ROUND_HI
#define SH           BOARD_ROUND_HI
#define BACK_RC      RC(RU(20), RU(204), RU(60), RU(58))
#define TOOLS_R      RU(446)
#define Q_VIEW_RC    RC(0, 0, SW, RU(276))  // the quiz view at the top, the answers below
#define Q_PANEL_X    0
#define CARD_VIEW_RC RC(0, 0, SW, RU(268))
#define CARD_PREV_RC RC(RU(106), RU(380), RU(58), RU(50))
#define CARD_NEXT_RC RC(RU(170), RU(380), RU(58), RU(50))
#define CARD_GO_RC   RC(RU(234), RU(380), RU(126), RU(50))
#define R_AGAIN_RC   RC(RU(70), RU(300), RU(160), RU(60))
#define R_MENU_RC    RC(RU(236), RU(300), RU(160), RU(60))
#define TOOL_W       RU(60)
#define TOOL_H       RU(56)
#else
#define SW           320
#define SH           240
#define BACK_RC      RC(0, 0, 60, 44)
#define TOOLS_R      318
#define Q_VIEW_RC    RC(0, 0, 320, 142)  // the quiz view at the top, the answers below
#define Q_PANEL_X    0
#define CARD_VIEW_RC RC(0, 0, 320, 132)
#define CARD_PREV_RC RC(2, 194, 52, 44)
#define CARD_NEXT_RC RC(58, 194, 52, 44)
#define CARD_GO_RC   RC(114, 194, 204, 44)
#define R_AGAIN_RC   RC(18, 182, 136, 48)
#define R_MENU_RC    RC(166, 182, 136, 48)
#define TOOL_W       48
#define TOOL_H       44
#endif

#if BOARD_ROUND
// the half width of the circle of the panel at the row y
static int rchord(int y)
{
    int r = SW / 2, dy = y - SH / 2;
    return dy * dy >= r * r ? 0 : (int)sqrtf((float)(r * r - dy * dy));
}

// a row of n boxes from y to y + h that follows the circle (inset px from the edge)
static rc_t row_rc(int n, int i, int y, int h, int inset)
{
    int a = rchord(y), b = rchord(y + h), half = (a < b ? a : b) - inset, gap = RU(8);
    int w = (2 * half - gap * (n - 1)) / n;
    return RC(SW / 2 - half + i * (w + gap), y, w, h);
}
#endif

static bool in_rc(rc_t r, int x, int y)
{
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

// the 6 mode buttons of the title
static rc_t title_btn(int i)
{
#if BOARD_BAR_SCREEN
    return RC(4 + i * 106, 104, 102, 64);
#elif BOARD_UI_TABLET
    return RC(12 + (i % 2) * 312, 134 + (i / 2) * 74, 304, 70);
#elif BOARD_ROUND
    return row_rc(2, i % 2, RU(132) + (i / 2) * RU(84), RU(78), RU(10));
#else
    return RC(6 + (i % 2) * 156, 88 + (i / 2) * 50, 152, 48);
#endif
}

// the 4 answer buttons of a quiz
static rc_t ans_rc(int i)
{
#if BOARD_BAR_SCREEN
    return RC(402 + (i % 2) * 119, 44 + (i / 2) * 64, 117, 62);
#elif BOARD_UI_TABLET
    return RC(4 + (i % 2) * 318, 216 + (i / 2) * 72, 314, 68);
#elif BOARD_ROUND
    return row_rc(2, i % 2, RU(286) + (i / 2) * RU(58), RU(54), RU(10));
#else
    return RC(2 + (i % 2) * 161, 144 + (i / 2) * 50, 157, 46);  // (4 px apart)
#endif
}

// the tools of the live view: + and - at the top right (index 0 = the right end, 6 px apart); 2 = NORTH, at the left
// between BACK and the zoom tools, away from both (it acts on a hold of 1 s)
static rc_t tool_rc(int i)
{
#if BOARD_ROUND
    // + and - on the right arc, N on the top cap
    if (i == 2) {
        return RC(SW / 2 - TOOL_W / 2, RU(28), TOOL_W, TOOL_H);
    }
    return RC(TOOLS_R - TOOL_W, SH / 2 + (i ? RU(6) : -RU(6) - TOOL_H), TOOL_W, TOOL_H);
#endif
#if BOARD_BAR_SCREEN
    const int r = SW - 2;
#else
    const int r = TOOLS_R;
#endif
#if BOARD_UI_TABLET
    if (i == 2) {
        return RC(252, 0, TOOL_W + 4, TOOL_H);
    }
#else
    if (i == 2) {
        return RC(BOARD_BAR_SCREEN ? 230 : (BOARD_ROUND ? 120 : 126), 0, TOOL_W + 4,
                  TOOL_H);  // (gaps of 30 px or more)
    }
#endif
    return RC(r - (i + 1) * TOOL_W - i * 6, 0, TOOL_W, TOOL_H);
}

// the time and toggle buttons of EXPLORE: along the bottom
static rc_t ex_btn(int i)
{
#if BOARD_BAR_SCREEN
    static const int16_t X[5] = {0, 56, 112, 476, 558};
    static const int16_t W[5] = {54, 54, 54, 80, 82};
#elif BOARD_ROUND
    // (two rows in the bottom cap: the time keys, then LINES and NAMES)
    return i < 3 ? row_rc(3, i, RU(342), RU(50), RU(14)) : row_rc(2, i - 3, RU(398), RU(46), RU(12));
#elif BOARD_UI_TABLET
    static const int16_t X[5] = {0, 100, 200, 316, 480};
    static const int16_t W[5] = {96, 96, 96, 160, 160};
#else
    static const int16_t X[5] = {0, 50, 100, 158, 240};
    static const int16_t W[5] = {48, 48, 48, 80, 80};
#endif
#if !BOARD_ROUND
    return RC(X[i], SH - TOOL_H, W[i], TOOL_H);
#endif
}

// the 6 rows of SETUP: 2 columns of 3
static rc_t set_cell(int i)
{
#if BOARD_BAR_SCREEN
    return RC((i % 2) * 320, 40 + (i / 2) * 44, 318, 44);
#elif BOARD_UI_TABLET
    return RC((i % 2) * 320, 66 + (i / 2) * 97, 318, 97);
#elif BOARD_ROUND
    // one column of 6 rows that follow the circle; the rows next to BACK start right of it
    rc_t r = row_rc(1, 0, RU(100) + i * RU(52), RU(48), RU(12));
    rc_t b = BACK_RC;
    if (r.y < b.y + b.h && r.y + r.h > b.y && r.x < b.x + b.w + RU(6)) {
        int x1 = r.x + r.w;
        r.x = (int16_t)(b.x + b.w + RU(6));
        r.w = (int16_t)(x1 - r.x);
    }
    return r;
#else
    return RC((i % 2) * 160, 44 + (i / 2) * 65, 158, 65);
#endif
}

static rc_t set_btn(int i, int plus)
{
    rc_t c = set_cell(i);
#if BOARD_BAR_SCREEN
    return RC(c.x + c.w - (plus ? 50 : 102), c.y + 1, 48, 42);
#elif BOARD_UI_TABLET
    return RC(c.x + (plus ? 164 : 8), c.y + 30, 144, 64);
#elif BOARD_ROUND
    return RC(c.x + c.w - (plus ? RU(54) : RU(110)), c.y + 1, RU(52), c.h - 2);
#else
    return RC(c.x + (plus ? 82 : 4), c.y + 19, 72, 44);
#endif
}

// ---------------------------------------------------------------- colours

#define C_BG     rgb(8, 10, 24)
#define C_PANEL  rgb(18, 24, 46)
#define C_PANEL2 rgb(30, 40, 72)
#define C_LINE   rgb(56, 70, 116)
#define C_GOLD   rgb(255, 196, 80)
#define C_CYAN   rgb(110, 200, 255)
#define C_GREEN  rgb(100, 232, 130)
#define C_RED    rgb(255, 92, 92)
#define C_WHITE  rgb(236, 240, 255)
#define C_DIM    rgb(130, 142, 180)
#define C_VIOLET rgb(190, 140, 255)

// ---------------------------------------------------------------- the learn path

typedef struct {
    const char *abbr;
    const char *fact;
} learn_t;

// from the easiest to see (at about 32 N) to the hardest
static const learn_t LEARN[] = {
        {"Ori", "The hunter. Find the three belt stars in a straight line."},
        {"UMa", "The Great Bear. Its seven bright stars make the Big Dipper."},
        {"Cas", "A W of five stars: the queen on her throne, near the pole."},
        {"Cyg", "The swan, also the Northern Cross. Deneb is its tail."},
        {"Leo", "The lion. A backward question mark makes its head and mane."},
        {"Sco", "The scorpion. Red Antares is its heart; the tail curls south."},
        {"Lyr", "The lyre. Vega, the fifth brightest star of the night, is here."},
        {"Gem", "The twins: Castor and Pollux, two bright stars side by side."},
        {"Tau", "The bull. Red Aldebaran is its eye; the Pleiades sit on its back."},
        {"CMa", "The big dog. Sirius, the brightest star of the night, is here."},
        {"UMi", "The Little Dipper. Polaris, the North Star, ends its handle."},
        {"Boo", "The herdsman. Follow the arc of the Dipper handle to Arcturus."},
        {"Aql", "The eagle. Altair is one corner of the Summer Triangle."},
        {"Sgr", "The archer: a teapot of stars. The centre of our galaxy is near."},
        {"Peg", "The winged horse. Four stars make the Great Square."},
        {"And", "The princess. The Andromeda Galaxy, 2.5 million light-years away."},
        {"Per", "The hero. Algol, the demon star, gets dim every 2.9 days."},
        {"Aur", "The charioteer. Yellow Capella is a star like the Sun, but bigger."},
        {"Vir", "The maiden. Blue-white Spica is near the path of the Sun."},
        {"CrB", "The northern crown: a small half circle of stars."},
        {"Her", "The strong man. Four stars make the Keystone in his body."},
        {"Dra", "The dragon winds between the two dippers around the pole."},
        {"Cep", "The king: a house shape near Cassiopeia and the pole."},
        {"CMi", "The little dog. Procyon is one corner of the Winter Triangle."},
        {"Cnc", "The crab: faint, with the Beehive star cluster in its middle."},
        {"Ari", "The ram: three stars make a short bent line."},
        {"Psc", "The fishes: a long V of faint stars below Pegasus."},
        {"Aqr", "The water bearer: a big faint figure of the autumn sky."},
        {"Cap", "The sea goat: a wide triangle of faint stars."},
        {"Lib", "The scales, between Virgo and Scorpius."},
        {"Oph", "The serpent bearer: a big house shape above Scorpius."},
        {"Del", "The dolphin: a small kite with a tail, near Altair."},
        {"Sge", "The arrow: one of the smallest constellations, near Altair."},
        {"Lep", "The hare sits at the feet of Orion."},
        {"Eri", "The river: it flows from Orion far to the south."},
        {"Crv", "The crow: four stars make a small sail, south of Virgo."},
        {"Hya", "The water snake, the largest constellation. Alphard is its heart."},
        {"PsA", "The southern fish. Bright Fomalhaut is alone in the autumn sky."},
        {"Cet", "The whale. Mira, a red giant, changes its brightness."},
        {"Tri", "The triangle: three stars between Andromeda and Aries."},
};
#define N_LEARN ((int)(sizeof(LEARN) / sizeof(LEARN[0])))

// the bright stars of STARS, from the best known
static const char *const FAMOUS[] = {
        "Sirius",    "Vega",      "Arcturus",   "Betelgeuse", "Rigel",   "Polaris",    "Capella",  "Aldebaran",
        "Antares",   "Altair",    "Deneb",      "Procyon",    "Spica",   "Pollux",     "Castor",   "Regulus",
        "Fomalhaut", "Bellatrix", "Canopus",    "Alnilam",    "Alnitak", "Mintaka",    "Dubhe",    "Merak",
        "Mizar",     "Alkaid",    "Alioth",     "Kochab",     "Algol",   "Mirfak",     "Schedar",  "Caph",
        "Hamal",     "Alpheratz", "Markab",     "Scheat",     "Enif",    "Rasalhague", "Denebola", "Algieba",
        "Elnath",    "Alhena",    "Mirzam",     "Adhara",     "Wezen",   "Shaula",     "Nunki",    "Alphard",
        "Albireo",   "Sadr",      "Menkalinan", "Diphda",     "Alcyone", "Thuban",     "Achernar", "Saiph",
};
#define N_FAMOUS ((int)(sizeof(FAMOUS) / sizeof(FAMOUS[0])))

// city presets: latitude, longitude (degrees, + = north / east), the time zone (hours)
typedef struct {
    const char *name;
    float lat, lon, tz;
} city_t;

static const city_t CITIES[] = {
        {"TEL AVIV", 32.08f, 34.78f, 3},  {"JERUSALEM", 31.77f, 35.21f, 3}, {"HAIFA", 32.79f, 34.99f, 3},
        {"EILAT", 29.56f, 34.95f, 3},     {"LONDON", 51.51f, -0.13f, 1},    {"NEW YORK", 40.71f, -74.0f, -4},
        {"SYDNEY", -33.87f, 151.21f, 10}, {"TOKYO", 35.68f, 139.69f, 9},
};
#define N_CITIES ((int)(sizeof(CITIES) / sizeof(CITIES[0])))

// ---------------------------------------------------------------- state

typedef enum {
    ST_TITLE,
    ST_CARD,  // LEARN: the card of a constellation
    ST_QUIZ,  // LEARN practice, NAME, STARS
    ST_FIND,
    ST_EXPLORE,
    ST_SETUP,
    ST_RESULT,
} sstate_t;

typedef enum {
    M_LEARN,
    M_NAME,
    M_STARS,
    M_FIND
} smode_t;
typedef enum {
    Q_CON,
    Q_STAR,
    Q_TAP
} qkind_t;

static sstate_t s_state;
static smode_t s_mode;
static int64_t s_now, s_prev;

// saved
static int s_learned;       // constellations open in LEARN (1 .. N_LEARN)
static uint32_t s_best[4];  // the best scores of LEARN (not used), NAME, STARS, FIND
static int s_city;          // the preset, or N_CITIES = custom
static float s_lat = 32.08f, s_lon = 34.78f, s_tz = 3;
static uint32_t s_manual_t;  // UTC seconds when the board has no clock
static int64_t s_manual_at;  // esp_timer seconds of s_manual_t

// the unit vectors of the stars and the line points (equatorial, J2000)
static float s_sv[SKY_N_STARS][3];
static float s_pv[SKY_N_PTS][3];
static int16_t s_learn_con[N_LEARN];  // LEARN -> SKY_CONS index
static int16_t s_famous[N_FAMOUS];    // FAMOUS -> SKY_STARS index (-1 = not there)
static int s_n_famous;
static bool s_data_ok;

// ---------------------------------------------------------------- vectors

static void v_set(float *v, float x, float y, float z)
{
    v[0] = x;
    v[1] = y;
    v[2] = z;
}

static float v_dot(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void v_cross(const float *a, const float *b, float *o)
{
    float x = a[1] * b[2] - a[2] * b[1], y = a[2] * b[0] - a[0] * b[2], z = a[0] * b[1] - a[1] * b[0];
    v_set(o, x, y, z);
}

static float v_norm(float *v)
{
    float m = sqrtf(v_dot(v, v));
    if (m > 1e-9f) {
        v[0] /= m;
        v[1] /= m;
        v[2] /= m;
    }
    return m;
}

static void v_radec(float ra, float dec, float *v)
{
    float cd = cosf(dec);
    v_set(v, cd * cosf(ra), cd * sinf(ra), sinf(dec));
}

// turn v about the unit axis k by the angle a (Rodrigues)
static void v_turn(float *v, const float *k, float a)
{
    float c = cosf(a), s = sinf(a), kv[3];
    v_cross(k, v, kv);
    float d = v_dot(k, v) * (1 - c);
    for (int i = 0; i < 3; i++) {
        v[i] = v[i] * c + kv[i] * s + k[i] * d;
    }
}

static float clampf(float v, float a, float b)
{
    return v < a ? a : (v > b ? b : v);
}

static float frand(float a, float b)
{
    return a + (b - a) * (float)(esp_random() & 0xFFFF) / 65535.0f;
}

static int irand(int n)
{
    return n > 0 ? (int)(esp_random() % (uint32_t)n) : 0;
}

// ---------------------------------------------------------------- time and place

static int64_t now_s(void)
{
    return esp_timer_get_time() / 1000000;
}

static bool clock_ok(void)
{
    return time(NULL) > 1735689600;  // after the start of 2025: the clock is set (SNTP)
}

// the civil date of a day number (days since 1970-01-01)
static void civil(int64_t days, int *y, int *m, int *d)
{
    days += 719468;
    int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    int64_t doe = days - era * 146097;
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int64_t mp = (5 * doy + 2) / 153;
    *d = (int)(doy - (153 * mp + 2) / 5 + 1);
    *m = (int)(mp < 10 ? mp + 3 : mp - 9);
    *y = (int)(yoe + era * 400 + (*m <= 2));
}

static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

// the default when the board has no clock: the build date at 22:00 local time
static uint32_t default_manual_t(void)
{
    static const char *const MON = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *b = __DATE__;  // "Oct  2 2026"
    int m = 10, d = 1, y = 2026;
    for (int i = 0; i < 12; i++) {
        if (!strncmp(b, MON + 3 * i, 3)) {
            m = i + 1;
        }
    }
    d = atoi(b + 4);
    y = atoi(b + 7);
    if (y < 2025 || y > 2100) {
        y = 2026;
    }
    if (d < 1 || d > 31) {
        d = 1;
    }
    int64_t t = days_from_civil(y, m, d) * 86400 + 22 * 3600 - (int64_t)(s_tz * 3600);
    return (uint32_t)t;
}

static float s_scrub_h;  // EXPLORE: hours from now

// UTC seconds of the sky now
static double sky_time(void)
{
    double t;
    if (clock_ok()) {
        t = (double)time(NULL);
    } else {
        t = (double)s_manual_t + (double)(now_s() - s_manual_at);
    }
    if (s_state == ST_EXPLORE) {
        t += s_scrub_h * 3600.0;
    }
    return t;
}

// the local sidereal time (rad) of the UTC time t
static float lst_rad(double t)
{
    double jd = t / 86400.0 + 2440587.5;
    double gmst = 280.46061837 + 360.98564736629 * (jd - 2451545.0);
    double l = fmod(gmst + s_lon, 360.0);
    if (l < 0) {
        l += 360.0;
    }
    return (float)(l * (PI_F / 180.0));
}

// ---------------------------------------------------------------- the Sun, the Moon and the planets

typedef struct {
    const char *name;
    float a, e, i, l, w, o;         // the elements at J2000 (AU, deg)
    float da, de, di, dl, dw, dob;  // their change in a century
    uint8_t r, g, b;
    float mag;
} orbit_t;

// approximate Keplerian elements (JPL, Standish: 1800 to 2050)
static const orbit_t ORBITS[6] = {
        {"Mercury", 0.38709927f, 0.20563593f, 7.00497902f, 252.25032350f, 77.45779628f, 48.33076593f, 0.00000037f,
         0.00001906f, -0.00594749f, 149472.67411175f, 0.16047689f, -0.12534081f, 200, 190, 180, 0.0f},
        {"Venus", 0.72333566f, 0.00677672f, 3.39467605f, 181.97909950f, 131.60246718f, 76.67984255f, 0.00000390f,
         -0.00004107f, -0.00078890f, 58517.81538729f, 0.00268329f, -0.27769418f, 255, 250, 220, -4.2f},
        {"Earth", 1.00000261f, 0.01671123f, -0.00001531f, 100.46457166f, 102.93768193f, 0.0f, 0.00000562f, -0.00004392f,
         -0.01294668f, 35999.37244981f, 0.32327364f, 0.0f, 0, 0, 0, 0},
        {"Mars", 1.52371034f, 0.09339410f, 1.84969142f, -4.55343205f, -23.94362959f, 49.55953891f, 0.00001847f,
         0.00007882f, -0.00813131f, 19140.30268499f, 0.44441088f, -0.29257343f, 255, 120, 80, 0.5f},
        {"Jupiter", 5.20288700f, 0.04838624f, 1.30439695f, 34.39644051f, 14.72847983f, 100.47390909f, -0.00011607f,
         -0.00013253f, -0.00183714f, 3034.74612775f, 0.21252668f, 0.20469106f, 255, 236, 200, -2.2f},
        {"Saturn", 9.53667594f, 0.05386179f, 2.48599187f, 49.95424423f, 92.59887831f, 113.66242448f, -0.00125060f,
         -0.00050991f, 0.00193609f, 1222.49362201f, -0.41897216f, -0.28867794f, 240, 210, 140, 0.7f},
};

// the body list of EXPLORE: 0 = the Sun, 1 = the Moon, 2.. = the planets (not the Earth)
#define N_BODY 7
typedef struct {
    const char *name;
    float v[3];  // equatorial unit vector
    float mag;
    uint8_t r, g, b;
} body_t;
static body_t s_body[N_BODY];
static float s_moon_lit;  // the lit part of the Moon, 0..1
static double s_body_t = -1;

static void helio(const orbit_t *o, float T, double *x, double *y, double *z)
{
    double a = o->a + o->da * T, e = o->e + o->de * T;
    double i = (o->i + o->di * T) * (M_PI / 180), L = o->l + o->dl * T;
    double wb = o->w + o->dw * T, om = o->o + o->dob * T;
    double w = (wb - om) * (M_PI / 180), O = om * (M_PI / 180);
    double M = fmod(L - wb, 360.0);
    if (M > 180) {
        M -= 360;
    }
    if (M < -180) {
        M += 360;
    }
    M *= M_PI / 180;
    double E = M + e * sin(M);
    for (int k = 0; k < 6; k++) {
        E -= (E - e * sin(E) - M) / (1 - e * cos(E));
    }
    double xp = a * (cos(E) - e), yp = a * sqrt(1 - e * e) * sin(E);
    double cw = cos(w), sw = sin(w), cO = cos(O), sO = sin(O), ci = cos(i), si = sin(i);
    *x = (cw * cO - sw * sO * ci) * xp + (-sw * cO - cw * sO * ci) * yp;
    *y = (cw * sO + sw * cO * ci) * xp + (-sw * sO + cw * cO * ci) * yp;
    *z = (sw * si) * xp + (cw * si) * yp;
}

// ecliptic (x, y, z) to an equatorial unit vector
static void ecl_to_eq(double x, double y, double z, float *v)
{
    const double eps = 23.43928 * (M_PI / 180);
    v_set(v, (float)x, (float)(y * cos(eps) - z * sin(eps)), (float)(y * sin(eps) + z * cos(eps)));
    v_norm(v);
}

static void bodies_update(double t)
{
    if (s_body_t >= 0 && fabs(t - s_body_t) < 20) {
        return;
    }
    s_body_t = t;
    float T = (float)((t / 86400.0 + 2440587.5 - 2451545.0) / 36525.0);
    double ex, ey, ez;
    helio(&ORBITS[2], T, &ex, &ey, &ez);
    // the Sun
    s_body[0] = (body_t){"Sun", {0, 0, 0}, -26.7f, 255, 236, 140};
    ecl_to_eq(-ex, -ey, -ez, s_body[0].v);
    // the Moon (a short series, about 0.3 degrees)
    double Tm = T;
    double lam = 218.32 + 481267.881 * Tm + 6.29 * sin((135.0 + 477198.87 * Tm) * M_PI / 180) -
                 1.27 * sin((259.3 - 413335.36 * Tm) * M_PI / 180) + 0.66 * sin((235.7 + 890534.22 * Tm) * M_PI / 180) +
                 0.21 * sin((269.9 + 954397.74 * Tm) * M_PI / 180) - 0.19 * sin((357.5 + 35999.05 * Tm) * M_PI / 180) -
                 0.11 * sin((186.5 + 966404.03 * Tm) * M_PI / 180);
    double bet = 5.13 * sin((93.3 + 483202.02 * Tm) * M_PI / 180) + 0.28 * sin((228.2 + 960400.89 * Tm) * M_PI / 180) -
                 0.28 * sin((318.3 + 6003.15 * Tm) * M_PI / 180) - 0.17 * sin((217.6 - 407332.21 * Tm) * M_PI / 180);
    lam *= M_PI / 180;
    bet *= M_PI / 180;
    s_body[1] = (body_t){"Moon", {0, 0, 0}, -12.0f, 230, 230, 215};
    ecl_to_eq(cos(bet) * cos(lam), cos(bet) * sin(lam), sin(bet), s_body[1].v);
    float el = v_dot(s_body[1].v, s_body[0].v);  // cos of the angle Sun - Moon
    s_moon_lit = (1 - el) * 0.5f;
    // the planets
    int k = 2;
    for (int p = 0; p < 6; p++) {
        if (p == 2) {
            continue;
        }
        double x, y, z;
        helio(&ORBITS[p], T, &x, &y, &z);
        s_body[k] = (body_t){ORBITS[p].name, {0, 0, 0}, ORBITS[p].mag, ORBITS[p].r, ORBITS[p].g, ORBITS[p].b};
        ecl_to_eq(x - ex, y - ey, z - ez, s_body[k].v);
        k++;
    }
}

// ---------------------------------------------------------------- the camera

typedef struct {
    float r[3], u[3], f[3];  // equatorial unit vectors: screen right, screen up, forward
    float F;                 // px per unit of tan
    float cx, cy;            // the screen centre of the view
    rc_t vr;                 // the view rectangle
    bool live;               // the real sky: a horizon, the twilight
    float up[3];             // live: the local vertical (equatorial)
    float north[3];          // live: the north point of the horizon (equatorial)
} cam_t;

static cam_t s_cam;
static float s_fov = 80;  // live: the horizontal field of view (degrees)

// project an equatorial unit vector; false if it is behind
static bool project(const cam_t *c, const float *v, float *sx, float *sy)
{
    float z = v_dot(v, c->f);
    if (z < 0.02f) {
        return false;
    }
    *sx = c->cx + c->F * v_dot(v, c->r) / z;
    *sy = c->cy - c->F * v_dot(v, c->u) / z;
    return true;
}

// a camera on a sky point (atlas: north up, then turned by rot), the radius fits in the view
static void cam_atlas(cam_t *c, rc_t vr, const float *target, float rot, float radius_deg)
{
    static const float POLE[3] = {0, 0, 1};
    memset(c, 0, sizeof(*c));
    c->vr = vr;
    c->cx = vr.x + vr.w * 0.5f;
    c->cy = vr.y + vr.h * 0.5f;
    memcpy(c->f, target, sizeof(c->f));
    float r0[3], u0[3];
    v_cross(c->f, POLE, r0);
    if (v_norm(r0) < 1e-4f) {
        v_set(r0, 0, -1, 0);
    }
    v_cross(r0, c->f, u0);
    float cr = cosf(rot), sr = sinf(rot);
    for (int i = 0; i < 3; i++) {
        c->r[i] = cr * r0[i] + sr * u0[i];
        c->u[i] = -sr * r0[i] + cr * u0[i];
    }
    float half = (vr.w < vr.h ? vr.w : vr.h) * 0.5f;
    float rad = clampf(radius_deg, 4, 62) * DEG;
    c->F = 0.86f * half / tanf(rad);
}

// the live view: the board orientation (or the drag) and the time
static float s_href[3] = {1, 0, 0};  // phys: the world north (a horizontal vector)
static bool s_href_ok;
static float s_gprev[3];  // phys: the gravity of the last frame
static float s_gsum_prev[3];
static float s_gsign = 1;  // +1, or -1 if the gyro turns the other way
static float s_sign_score;
static float s_man_az = PI_F, s_man_alt = 30 * DEG;  // no IMU: the view direction
static bool s_use_imu;

static bool imu_live(void)
{
    return (app_board_caps() & APP_NEEDS_IMU) && imu_ok();
}

// the logical right and down directions of the screen in the physical frame
static void screen_axes(float *lx, float *ly)
{
    switch (gfx_orient()) {
    case ORIENT_PORTRAIT:
        v_set(lx, 1, 0, 0);
        v_set(ly, 0, 1, 0);
        break;
    case ORIENT_PORTRAIT_FLIP:
        v_set(lx, -1, 0, 0);
        v_set(ly, 0, -1, 0);
        break;
    case ORIENT_LAND_A:
        v_set(lx, 0, 1, 0);
        v_set(ly, -1, 0, 0);
        break;
    default:
        v_set(lx, 0, -1, 0);
        v_set(ly, 1, 0, 0);
        break;
    }
}

// the horizontal direction that the board looks to (phys): from the back of the board; when
// the board looks up (or down), the screen top tilts toward the user (or away), so the
// heading also uses the screen up with that sign
static void board_heading(const float *U, float *h)
{
    float lx[3], ly[3];
    screen_axes(lx, ly);
    float f[3] = {0, 0, 1};
    float s = f[0] * U[0] + f[1] * U[1] + f[2] * U[2] > 0 ? 1.0f : -1.0f;
    for (int i = 0; i < 3; i++) {
        h[i] = f[i] + s * ly[i];  // forward -/+ screen up
    }
    float k = v_dot(h, U);
    for (int i = 0; i < 3; i++) {
        h[i] -= k * U[i];
    }
    if (v_norm(h) < 1e-3f) {
        v_cross(lx, U, h);
        v_norm(h);
    }
}

static void gravity_up(float *U)
{
    float g[3];
    imu_gravity_phys(&g[0], &g[1], &g[2]);
    v_set(U, -g[0], -g[1], -g[2]);
    if (v_norm(U) < 0.3f) {
        v_set(U, 0, -1, 0);
    }
}

// set the north: the board looks to the north (az = 0) or to the south (az = pi)
static void heading_set(float az)
{
    float U[3], h[3];
    gravity_up(U);
    board_heading(U, h);
    v_turn(h, U, az);  // h has the azimuth az: the north is h turned back by az
    memcpy(s_href, h, sizeof(s_href));
    s_href_ok = true;
    float a[3];
    gyro_sum(a);
    memcpy(s_gsum_prev, a, sizeof(a));
    float g[3];
    imu_gravity_phys(&g[0], &g[1], &g[2]);
    memcpy(s_gprev, g, sizeof(g));
}

// turn the view by a drag of dx px (the sky moves with the finger)
static void drag_turn(float dx, float dy)
{
    float k = (s_fov * DEG) / (float)SW;
    if (s_use_imu && HAVE_GYRO_SUM()) {
        float U[3];
        gravity_up(U);
        v_turn(s_href, U, -dx * k);
    } else if (s_use_imu) {
        s_man_az -= dx * k;  // no gyro: the drag gives the azimuth
    } else {
        s_man_az -= dx * k;
        s_man_alt = clampf(s_man_alt + dy * k, -10 * DEG, 89 * DEG);
    }
}

static void cam_live(cam_t *c, rc_t vr)
{
    memset(c, 0, sizeof(*c));
    c->vr = vr;
    c->live = true;
    c->cx = vr.x + vr.w * 0.5f;
    c->cy = vr.y + vr.h * 0.5f;
    c->F = (vr.w * 0.5f) / tanf(s_fov * 0.5f * DEG);
    // the ENU axes -> equatorial (the transpose of the equatorial -> ENU matrix)
    double t = sky_time();
    float th = lst_rad(t), ph = s_lat * DEG;
    float ct = cosf(th), st = sinf(th), cp = cosf(ph), sp = sinf(ph);
    float rowE[3] = {-st, ct, 0}, rowN[3] = {-sp * ct, -sp * st, cp}, rowU[3] = {cp * ct, cp * st, sp};
    memcpy(c->up, rowU, sizeof(c->up));
    memcpy(c->north, rowN, sizeof(c->north));
    float qe[3][3];  // the screen r, u, f in ENU
    if (s_use_imu) {
        float U[3], lx[3], ly[3];
        gravity_up(U);
        screen_axes(lx, ly);
        if (HAVE_GYRO_SUM() && s_href_ok) {
            float a[3], d[3], g[3];
            gyro_sum(a);
            for (int i = 0; i < 3; i++) {
                d[i] = (a[i] - s_gsum_prev[i]) * s_gsign;
            }
            memcpy(s_gsum_prev, a, sizeof(a));
            float ang = sqrtf(v_dot(d, d));
            if (ang > 1e-5f && ang < 1.0f) {
                // a fixed world vector turns by -d in the board frame
                float k[3] = {-d[0] / ang, -d[1] / ang, -d[2] / ang};
                v_turn(s_href, k, ang);
                // the gyro sign: the gravity must turn the same way
                imu_gravity_phys(&g[0], &g[1], &g[2]);
                float pg[3], dg[3];
                v_cross(d, s_gprev, pg);  // the predicted change is -d x g
                for (int i = 0; i < 3; i++) {
                    dg[i] = g[i] - s_gprev[i];
                }
                float c1 = -v_dot(pg, dg);
                if (ang > 0.01f) {
                    s_sign_score = clampf(s_sign_score * 0.98f + c1 * 50, -2, 2);
                }
                if (s_sign_score < -1) {
                    s_gsign = -s_gsign;
                    s_sign_score = 0;
                }
                memcpy(s_gprev, g, sizeof(g));
            } else {
                imu_gravity_phys(&s_gprev[0], &s_gprev[1], &s_gprev[2]);
            }
        }
        if (!HAVE_GYRO_SUM()) {
            heading_set(s_man_az);  // no gyro: the azimuth of the drag
        }
        if (!s_href_ok) {
            heading_set(PI_F);  // the view starts to the south
        }
        // the north: the reference, flat on the horizon
        float N[3], E[3];
        memcpy(N, s_href, sizeof(N));
        float k = v_dot(N, U);
        for (int i = 0; i < 3; i++) {
            N[i] -= k * U[i];
        }
        if (v_norm(N) < 0.05f) {
            heading_set(PI_F);
            memcpy(N, s_href, sizeof(N));
        }
        memcpy(s_href, N, sizeof(N));
        v_cross(N, U, E);
        float sr[3], su[3], sf[3] = {0, 0, 1};
        memcpy(sr, lx, sizeof(sr));
        v_set(su, -ly[0], -ly[1], -ly[2]);
        const float *ax[3] = {sr, su, sf};
        for (int j = 0; j < 3; j++) {
            qe[j][0] = v_dot(ax[j], E);
            qe[j][1] = v_dot(ax[j], N);
            qe[j][2] = v_dot(ax[j], U);
        }
    } else {
        float ca = cosf(s_man_alt), sa = sinf(s_man_alt), cz = cosf(s_man_az), sz = sinf(s_man_az);
        v_set(qe[2], ca * sz, ca * cz, sa);
        v_set(qe[0], cz, -sz, 0);
        v_cross(qe[0], qe[2], qe[1]);
    }
    float *out[3] = {c->r, c->u, c->f};
    for (int j = 0; j < 3; j++) {
        for (int i = 0; i < 3; i++) {
            out[j][i] = qe[j][0] * rowE[i] + qe[j][1] * rowN[i] + qe[j][2] * rowU[i];
        }
    }
}

// the altitude (sin) of a direction in the live sky
static float alt_of(const cam_t *c, const float *v)
{
    return v_dot(v, c->up);
}

// ---------------------------------------------------------------- the sky background

// the galaxy: the north galactic pole and the galactic centre (J2000)
static float s_ngp[3], s_gc[3], s_gy[3];
static float s_sun_alt;  // live: the altitude of the Sun (degrees)
static float s_day;      // 0 = night .. 1 = day (live)

static float milky(const float *d)
{
    float b = asinf(clampf(v_dot(d, s_ngp), -1, 1));
    float l = atan2f(v_dot(d, s_gy), v_dot(d, s_gc));
    float w = 0.12f + 0.07f * expf(-l * l / 0.8f);
    float core = expf(-(b * b) / (w * w));
    float bright = 0.30f + 0.70f * expf(-(l * l) / 1.3f);
    float clump = 0.78f + 0.22f * sinf(l * 7.0f + 1.3f) * sinf(l * 3.1f + 0.4f);
    if (l > 0.3f && l < 1.45f) {
        float db = b - 0.03f;
        core *= 1 - 0.6f * expf(-(db * db) / 0.0012f);  // the dark rift of Cygnus
    }
    float bulge = 0.8f * expf(-(l * l + 4 * b * b) / 0.05f);
    return core * bright * clump + bulge;
}

// The speed: the board has no hardware for expf, asinf, atan2f ..., so the sky background
// uses tables, not the models, for each grid point:
// - a cube map of the Milky Way (6 faces of CUBE x CUBE, made once from milky());
// - the horizon glow and the fade of the Milky Way by the altitude (ALT_N steps of sin(alt)).
#define CUBE  48
#define ALT_N 256
static uint8_t s_cube[6][CUBE][CUBE];
static float s_glow_t[ALT_N + 1], s_fade_t[ALT_N + 1];
static bool s_tables_ok;

static inline float rsq(float x)
{
    union {
        float f;
        uint32_t i;
    } u = {x};
    u.i = 0x5f3759dfu - (u.i >> 1);
    float y = u.f;
    y *= 1.5f - 0.5f * x * y * y;
    y *= 1.5f - 0.5f * x * y * y;
    return y;
}

static void tables_make(void)
{
    if (s_tables_ok) {
        return;
    }
    for (int f = 0; f < 6; f++) {
        int a = f >> 1;
        float sg = (f & 1) ? -1.0f : 1.0f;
        for (int j = 0; j < CUBE; j++) {
            for (int i = 0; i < CUBE; i++) {
                float d[3];
                d[a] = sg;
                d[(a + 1) % 3] = (i + 0.5f) * (2.0f / CUBE) - 1;
                d[(a + 2) % 3] = (j + 0.5f) * (2.0f / CUBE) - 1;
                v_norm(d);
                float m = milky(d) * 140.0f;
                s_cube[f][j][i] = (uint8_t)(m > 255 ? 255 : m);
            }
        }
    }
    for (int k = 0; k <= ALT_N; k++) {
        float alt = k * (2.0f / ALT_N) - 1;  // sin of the altitude
        s_glow_t[k] = alt > 0 ? expf(-alt * 7) : 1;
        s_fade_t[k] = alt > 0 ? 1 - expf(-alt * 12) : 0;
    }
    s_tables_ok = true;
}

// the Milky Way brightness of a direction (any length), from the cube map
static float milky_fast(const float *d)
{
    float ax = fabsf(d[0]), ay = fabsf(d[1]), az = fabsf(d[2]);
    int a = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
    float m = a == 0 ? ax : (a == 1 ? ay : az);
    if (m < 1e-6f) {
        return 0;
    }
    int f = a * 2 + (d[a] < 0);
    float k = 1.0f / m;
    float fu = (d[(a + 1) % 3] * k + 1) * (CUBE * 0.5f) - 0.5f;
    float fv = (d[(a + 2) % 3] * k + 1) * (CUBE * 0.5f) - 0.5f;
    int i0 = (int)(fu + 1) - 1, j0 = (int)(fv + 1) - 1;  // floor (fu, fv >= -0.5)
    float tu = fu - i0, tv = fv - j0;
    int i1 = i0 + 1, j1 = j0 + 1;
    if (i0 < 0) {
        i0 = 0;
    }
    if (j0 < 0) {
        j0 = 0;
    }
    if (i1 > CUBE - 1) {
        i1 = CUBE - 1;
    }
    if (j1 > CUBE - 1) {
        j1 = CUBE - 1;
    }
    if (i0 > CUBE - 1) {
        i0 = CUBE - 1;
    }
    if (j0 > CUBE - 1) {
        j0 = CUBE - 1;
    }
    const uint8_t (*c)[CUBE] = s_cube[f];
    float top = c[j0][i0] + (c[j0][i1] - c[j0][i0]) * tu;
    float bot = c[j1][i0] + (c[j1][i1] - c[j1][i0]) * tu;
    return (top + (bot - top) * tv) * (1.0f / 140.0f);
}

static float alt_table(const float *t, float alt)
{
    float x = (clampf(alt, -1, 1) + 1) * (ALT_N * 0.5f);
    int k = (int)x;
    if (k >= ALT_N) {
        return t[ALT_N];
    }
    return t[k] + (t[k + 1] - t[k]) * (x - k);
}

// the colour of the sky in a direction d of length len (0..255 x 16 for each channel)
static void sky_colour(const cam_t *c, const float *d, float inv_len, int *rr, int *gg, int *bb)
{
    float r = 5, g = 7, b = 20;
    float alt = c->live ? alt_of(c, d) * inv_len : 0.5f;
    float mw = milky_fast(d) * (1 - s_day);
    if (c->live) {
        float glow = alt_table(s_glow_t, alt);
        r += 26 * glow;
        g += 22 * glow;
        b += 30 * glow;
        mw *= alt_table(s_fade_t, alt);
        if (s_sun_alt > -18) {
            float tw = clampf((s_sun_alt + 18) / 12, 0, 1);
            r += tw * 12;
            g += tw * 26;
            b += tw * 62;
            if (s_sun_alt > -6) {
                float day = clampf((s_sun_alt + 6) / 12, 0, 1);
                r += day * 42;
                g += day * 70;
                b += day * 92;
            }
        }
    }
    r += mw * 52;
    g += mw * 56;
    b += mw * 74;
    if (c->live && alt < 0.012f) {
        // the ground: dark, a little lit by the twilight
        float k = clampf((0.012f - alt) / 0.024f, 0, 1);
        float gr = 16 + 20 * s_day, gg2 = 14 + 22 * s_day, gb = 12 + 14 * s_day;
        r += (gr - r) * k;
        g += (gg2 - g) * k;
        b += (gb - b) * k;
    }
    *rr = (int)(clampf(r, 0, 255) * 16);
    *gg = (int)(clampf(g, 0, 255) * 16);
    *bb = (int)(clampf(b, 0, 255) * 16);
}

static inline uint16_t pack565(int r, int g, int b)
{
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    return (uint16_t)((v >> 8) | (v << 8));
}

static inline void unpack565(uint16_t c, int *r, int *g, int *b)
{
    uint16_t v = (uint16_t)((c >> 8) | (c << 8));
    *r = (v >> 8) & 0xF8;
    *g = (v >> 3) & 0xFC;
    *b = (v << 3) & 0xF8;
}

#define GRID   8
#define GX_MAX (SW / GRID + 2)
#define GY_MAX (SH / GRID + 2)
static int16_t s_gr[GY_MAX][GX_MAX], s_gg[GY_MAX][GX_MAX], s_gb[GY_MAX][GX_MAX];

// the sky of the view: a colour for each grid point, then a bilinear fill of the pixels
static void draw_sky_bg(const cam_t *c)
{
    rc_t vr = c->vr;
    int nx = vr.w / GRID + 2, ny = vr.h / GRID + 2;
    if (nx > GX_MAX) {
        nx = GX_MAX;
    }
    if (ny > GY_MAX) {
        ny = GY_MAX;
    }
    for (int j = 0; j < ny; j++) {
        for (int i = 0; i < nx; i++) {
            float px = (vr.x + i * GRID - c->cx) / c->F, py = (c->cy - (vr.y + j * GRID)) / c->F;
            float d[3];
            for (int k = 0; k < 3; k++) {
                d[k] = c->f[k] + px * c->r[k] + py * c->u[k];
            }
            int r, g, b;
            sky_colour(c, d, rsq(v_dot(d, d)), &r, &g, &b);
            s_gr[j][i] = (int16_t)r;
            s_gg[j][i] = (int16_t)g;
            s_gb[j][i] = (int16_t)b;
        }
    }
    // the colour of each grid column steps down the rows (x 256): no multiply or divide for
    // each cell, and a run of one 565 colour is a plain fill
    static int32_t ar[GX_MAX], ag[GX_MAX], ab[GX_MAX], sr[GX_MAX], sg[GX_MAX], sb[GX_MAX];
    for (int y = 0; y < vr.h; y++) {
        int j = y / GRID;
        if (j + 1 >= ny) {
            j = ny - 2;
        }
        if (y % GRID == 0) {
            for (int i = 0; i < nx; i++) {
                ar[i] = s_gr[j][i] * 16;
                ag[i] = s_gg[j][i] * 16;
                ab[i] = s_gb[j][i] * 16;
                sr[i] = (s_gr[j + 1][i] - s_gr[j][i]) * 16 / GRID;
                sg[i] = (s_gg[j + 1][i] - s_gg[j][i]) * 16 / GRID;
                sb[i] = (s_gb[j + 1][i] - s_gb[j][i]) * 16 / GRID;
            }
        }
        uint16_t *row = g_fb + (vr.y + y) * gfx_w + vr.x;
        for (int i = 0; i + 1 < nx; i++) {
            int x0 = i * GRID;
            if (x0 >= vr.w) {
                break;
            }
            int n = GRID;
            if (x0 + n > vr.w) {
                n = vr.w - x0;
            }
            int r = ar[i], g = ag[i], b = ab[i];
            int dr = (ar[i + 1] - r) / GRID, dg = (ag[i + 1] - g) / GRID, db = (ab[i + 1] - b) / GRID;
            uint16_t c0 = pack565(r >> 8, g >> 8, b >> 8);
            uint16_t c1 = pack565((r + dr * (n - 1)) >> 8, (g + dg * (n - 1)) >> 8, (b + db * (n - 1)) >> 8);
            uint16_t *q = row + x0;
            if (c0 == c1) {
                for (int k = 0; k < n; k++) {
                    q[k] = c0;
                }
                continue;
            }
            for (int k = 0; k < n; k++) {
                q[k] = pack565(r >> 8, g >> 8, b >> 8);
                r += dr;
                g += dg;
                b += db;
            }
        }
        for (int i = 0; i < nx; i++) {
            ar[i] += sr[i];
            ag[i] += sg[i];
            ab[i] += sb[i];
        }
    }
}

// ---------------------------------------------------------------- stars, lines and labels

static rc_t s_clip;  // the drawing of the sky stays in this rectangle

static void put_max(int x, int y, int r, int g, int b)
{
    if (x < s_clip.x || y < s_clip.y || x >= s_clip.x + s_clip.w || y >= s_clip.y + s_clip.h) {
        return;
    }
    uint16_t *p = g_fb + y * gfx_w + x;
    int pr, pg, pb;
    unpack565(*p, &pr, &pg, &pb);
    if (r > pr) {
        pr = r;
    }
    if (g > pg) {
        pg = g;
    }
    if (b > pb) {
        pb = b;
    }
    *p = pack565(pr > 255 ? 255 : pr, pg > 255 ? 255 : pg, pb > 255 ? 255 : pb);
}

static void put(int x, int y, uint16_t c)
{
    if (x < s_clip.x || y < s_clip.y || x >= s_clip.x + s_clip.w || y >= s_clip.y + s_clip.h) {
        return;
    }
    g_fb[y * gfx_w + x] = c;
}

static void line(int x0, int y0, int x1, int y1, uint16_t c)
{
    // the long lines are clipped to a big box first (the projection can give far points)
    if ((x0 < -4000 && x1 < -4000) || (x0 > 4000 && x1 > 4000) || (y0 < -4000 && y1 < -4000) ||
        (y0 > 4000 && y1 > 4000)) {
        return;
    }
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, n = 0;
    while (n++ < 3000) {
        put(x0, y0, c);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

// the colour of a star from B-V
static void star_rgb(uint8_t bv, int *r, int *g, int *b)
{
    static const int16_t BV[7] = {0, 40, 70, 100, 130, 170, 220};  // (B-V + 0.4) x 100
    static const uint8_t RGB[7][3] = {{150, 176, 255}, {196, 212, 255}, {236, 240, 255}, {255, 246, 234},
                                      {255, 228, 186}, {255, 204, 146}, {255, 172, 110}};
    int i = 0;
    while (i < 5 && bv > BV[i + 1]) {
        i++;
    }
    int t = (bv - BV[i]) * 256 / (BV[i + 1] - BV[i]);
    if (t < 0) {
        t = 0;
    }
    if (t > 256) {
        t = 256;
    }
    *r = RGB[i][0] + ((RGB[i + 1][0] - RGB[i][0]) * t >> 8);
    *g = RGB[i][1] + ((RGB[i + 1][1] - RGB[i][1]) * t >> 8);
    *b = RGB[i][2] + ((RGB[i + 1][2] - RGB[i][2]) * t >> 8);
}

// a star (or a planet) at (x, y): the size and the brightness from the magnitude
static void draw_star(int x, int y, float mag, int r, int g, int b, float k)
{
    float lum = clampf((5.2f - mag) / 4.2f, 0.12f, 1.0f) * k;
    int R = (int)(r * lum), G = (int)(g * lum), B = (int)(b * lum);
    put_max(x, y, R, G, B);
    if (mag < 2.6f) {
        float h = mag < 1.4f ? 0.85f : 0.5f;
        put_max(x - 1, y, (int)(R * h), (int)(G * h), (int)(B * h));
        put_max(x + 1, y, (int)(R * h), (int)(G * h), (int)(B * h));
        put_max(x, y - 1, (int)(R * h), (int)(G * h), (int)(B * h));
        put_max(x, y + 1, (int)(R * h), (int)(G * h), (int)(B * h));
    }
    if (mag < 1.4f) {
        float h = 0.45f;
        put_max(x - 1, y - 1, (int)(R * h), (int)(G * h), (int)(B * h));
        put_max(x + 1, y - 1, (int)(R * h), (int)(G * h), (int)(B * h));
        put_max(x - 1, y + 1, (int)(R * h), (int)(G * h), (int)(B * h));
        put_max(x + 1, y + 1, (int)(R * h), (int)(G * h), (int)(B * h));
    }
    if (mag < 0.4f) {
        float h = 0.3f;
        put_max(x - 2, y, (int)(R * h), (int)(G * h), (int)(B * h));
        put_max(x + 2, y, (int)(R * h), (int)(G * h), (int)(B * h));
        put_max(x, y - 2, (int)(R * h), (int)(G * h), (int)(B * h));
        put_max(x, y + 2, (int)(R * h), (int)(G * h), (int)(B * h));
    }
}

static int16_t s_sx[SKY_N_STARS], s_sy[SKY_N_STARS];  // the stars on the screen (or -32768)

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static void draw_stars(const cam_t *c, float mag_max, float fade)
{
    uint32_t tw = (uint32_t)(s_now / 110);
    for (int i = 0; i < SKY_N_STARS; i++) {
        s_sx[i] = -32768;
    }
    for (int i = 0; i < SKY_N_STARS; i++) {
        const sky_star_t *st = &SKY_STARS[i];
        float mag = st->mag10 / 10.0f;
        if (mag > mag_max) {
            break;  // brightest first
        }
        float x, y;
        if (!project(c, s_sv[i], &x, &y)) {
            continue;
        }
        if (x < c->vr.x - 2 || y < c->vr.y - 2 || x > c->vr.x + c->vr.w + 2 || y > c->vr.y + c->vr.h + 2) {
            continue;
        }
        float k = fade;
        if (c->live) {
            float alt = alt_of(c, s_sv[i]);
            if (alt < 0) {
                continue;
            }
            // the twinkle: stronger near the horizon
            float amp = 0.10f + 0.30f * (1 - clampf(alt * 3, 0, 1));
            k *= 1 - amp * (float)(hash32((uint32_t)i * 2654435761U + tw) & 255) / 255.0f;
            k *= 1 - 0.75f * s_day;
        }
        int r, g, b;
        star_rgb(st->bv, &r, &g, &b);
        int ix = (int)lroundf(x), iy = (int)lroundf(y);
        draw_star(ix, iy, mag, r, g, b, k);
        s_sx[i] = (int16_t)ix;
        s_sy[i] = (int16_t)iy;
    }
}

// one constellation line from point a to point b (clipped at the near plane and the horizon)
static void seg(const cam_t *c, const float *a, const float *b, uint16_t col, int thick)
{
    float p[3], q[3];
    memcpy(p, a, sizeof(p));
    memcpy(q, b, sizeof(q));
    if (c->live) {
        float ha = alt_of(c, p), hb = alt_of(c, q);
        if (ha < 0 && hb < 0) {
            return;
        }
        if (ha < 0 || hb < 0) {
            float t = ha / (ha - hb);
            float m[3];
            for (int i = 0; i < 3; i++) {
                m[i] = p[i] + (q[i] - p[i]) * t;
            }
            if (ha < 0) {
                memcpy(p, m, sizeof(m));
            } else {
                memcpy(q, m, sizeof(m));
            }
        }
    }
    float za = v_dot(p, c->f), zb = v_dot(q, c->f);
    const float ZN = 0.05f;
    if (za < ZN && zb < ZN) {
        return;
    }
    if (za < ZN || zb < ZN) {
        float t = (ZN - za) / (zb - za);
        float m[3];
        for (int i = 0; i < 3; i++) {
            m[i] = p[i] + (q[i] - p[i]) * t;
        }
        if (za < ZN) {
            memcpy(p, m, sizeof(m));
        } else {
            memcpy(q, m, sizeof(m));
        }
    }
    float x0, y0, x1, y1;
    if (!project(c, p, &x0, &y0) || !project(c, q, &x1, &y1)) {
        return;
    }
    // clip to the drawing area first (Liang-Barsky): a point near the near plane projects
    // far off the screen, and the pixel steps there cost time for nothing
    float xmin = s_clip.x, ymin = s_clip.y, xmax = s_clip.x + s_clip.w - 1, ymax = s_clip.y + s_clip.h - 1;
    float dx = x1 - x0, dy = y1 - y0, t0 = 0, t1 = 1;
    float pp[4] = {-dx, dx, -dy, dy}, qq[4] = {x0 - xmin, xmax - x0, y0 - ymin, ymax - y0};
    for (int i = 0; i < 4; i++) {
        if (pp[i] == 0) {
            if (qq[i] < 0) {
                return;
            }
            continue;
        }
        float t = qq[i] / pp[i];
        if (pp[i] < 0) {
            if (t > t1) {
                return;
            }
            if (t > t0) {
                t0 = t;
            }
        } else {
            if (t < t0) {
                return;
            }
            if (t < t1) {
                t1 = t;
            }
        }
    }
    float ax = x0 + t0 * dx, ay = y0 + t0 * dy, bx = x0 + t1 * dx, by = y0 + t1 * dy;
    line((int)ax, (int)ay, (int)bx, (int)by, col);
    if (thick > 1) {
        line((int)ax + 1, (int)ay, (int)bx + 1, (int)by, col);
    }
}

// the lines of one constellation (con), or of all (con < 0)
static void draw_lines(const cam_t *c, int con, uint16_t col, int thick)
{
    int p0 = 0, p1 = SKY_N_POLYS;
    if (con >= 0) {
        p0 = SKY_CONS[con].poly0;
        p1 = p0 + SKY_CONS[con].npoly;
    }
    for (int p = p0; p < p1; p++) {
        const sky_poly_t *pl = &SKY_POLYS[p];
        for (int k = 0; k + 1 < pl->n; k++) {
            seg(c, s_pv[pl->pt0 + k], s_pv[pl->pt0 + k + 1], col, thick);
        }
    }
}

static float s_cv[SKY_N_CONS][3];  // the centres of the constellations (sky_init)

static void con_centre(int con, float *v)
{
    memcpy(v, s_cv[con], sizeof(s_cv[con]));
}

static void text_sh(int x, int y, const char *s, int scale, uint16_t c)
{
    gfx_text(x + 1, y + 1, s, scale, rgb(0, 0, 0));
    gfx_text(x, y, s, scale, c);
}

// The places that the labels of this frame use (and the buttons): a new label that covers
// one of them is not drawn.
#define N_LAB 48
static rc_t s_lab[N_LAB];
static int s_nlab;

static void label_block(rc_t r)
{
    if (s_nlab < N_LAB) {
        s_lab[s_nlab++] = r;
    }
}

static void label_reset(bool live)
{
    s_nlab = 0;
    label_block(BACK_RC);
    if (live) {
        for (int i = 0; i < 3; i++) {
            label_block(tool_rc(i));
        }
        if (s_state == ST_EXPLORE) {
            for (int i = 0; i < 5; i++) {
                label_block(ex_btn(i));
            }
        }
    }
}

static bool label_free(rc_t r)
{
    for (int i = 0; i < s_nlab; i++) {
        const rc_t *o = &s_lab[i];
        if (r.x < o->x + o->w && o->x < r.x + r.w && r.y < o->y + o->h && o->y < r.y + r.h) {
            return false;
        }
    }
    return true;
}

// a label next to a sky point, if it is in the view and the place is free
static void label_at(const cam_t *c, const float *v, const char *s, uint16_t col, int dx, int dy, bool centre)
{
    float x, y;
    if (!project(c, v, &x, &y)) {
        return;
    }
    if (c->live && alt_of(c, v) < 0) {
        return;
    }
    int w = gfx_text_width(s, 1);
    int ix = (int)x + dx - (centre ? w / 2 : 0), iy = (int)y + dy;
    const int th = 7 * TS(1);  // (the height of the text on the screen)
    if (ix < c->vr.x || iy < c->vr.y || ix + w > c->vr.x + c->vr.w || iy + th > c->vr.y + c->vr.h) {
        return;
    }
    rc_t r = RC(ix - 1, iy - 1, w + 2, th + 3);
    if (!label_free(r)) {
        // a second place: below the point
        iy = (int)y + 5;
        r = RC(ix - 1, iy - 1, w + 2, th + 3);
        if (iy + th > c->vr.y + c->vr.h || !label_free(r)) {
            return;
        }
    }
    label_block(r);
    text_sh(ix, iy, s, 1, col);
}

// the name of a star: the proper name, or the Bayer letter and the constellation
static void star_name(int i, char *out, int len)
{
    const sky_star_t *st = &SKY_STARS[i];
    if (st->name != 255) {
        snprintf(out, len, "%s", SKY_NAMES[st->name]);
    } else if (st->greek && st->con != 255) {
        snprintf(out, len, "%s %s", SKY_GREEK[st->greek], SKY_CONS[st->con].gen);
    } else if (st->con != 255) {
        snprintf(out, len, "A star in %s", SKY_CONS[st->con].name);
    } else {
        snprintf(out, len, "A star");
    }
}

// the Sun, the Moon and the planets (live)
static int16_t s_bx[N_BODY], s_by[N_BODY];

// the Sun: a disc of 10 rings with a soft edge
static void draw_sun(int ix, int iy)
{
    for (int r = 9; r >= 0; r--) {
        int k = r > 5 ? 90 - (r - 5) * 18 : 255;
        for (int dy = -r; dy <= r; dy++) {
            int w = (int)sqrtf((float)(r * r - dy * dy));
            for (int dx = -w; dx <= w; dx++) {
                put_max(ix + dx, iy + dy, k, k * 9 / 10, k * 5 / 10);
            }
        }
    }
}

// the Moon: the lit side looks to the Sun
static void draw_moon(const cam_t *c, int ix, int iy)
{
    float sx = v_dot(s_body[0].v, c->r), sy = v_dot(s_body[0].v, c->u), sz = v_dot(s_body[0].v, c->f);
    const int R = 6;
    for (int dy = -R; dy <= R; dy++) {
        for (int dx = -R; dx <= R; dx++) {
            float nx = dx / (float)R, ny = -dy / (float)R, q = 1 - nx * nx - ny * ny;
            if (q < 0) {
                continue;
            }
            float nz = -sqrtf(q);  // the side that looks to the viewer
            bool lit = nx * sx + ny * sy + nz * sz > 0;
            int v = lit ? 236 : 46;
            put(ix + dx, iy + dy, pack565(v, v, lit ? v - 16 : v + 8));
        }
    }
}

static void draw_bodies(const cam_t *c, bool labels)
{
    for (int i = 0; i < N_BODY; i++) {
        s_bx[i] = -32768;
        body_t *b = &s_body[i];
        float x, y;
        if (!project(c, b->v, &x, &y) || alt_of(c, b->v) < -0.01f) {
            continue;
        }
        int ix = (int)x, iy = (int)y;
        if (ix < c->vr.x - 8 || iy < c->vr.y - 8 || ix > c->vr.x + c->vr.w + 8 || iy > c->vr.y + c->vr.h + 8) {
            continue;
        }
        s_bx[i] = (int16_t)ix;
        s_by[i] = (int16_t)iy;
        if (i == 0) {
            draw_sun(ix, iy);
        } else if (i == 1) {
            draw_moon(c, ix, iy);
        } else {
            draw_star(ix, iy, b->mag < 0 ? b->mag : 0.6f, b->r, b->g, b->b, 1);
            put_max(ix, iy, b->r, b->g, b->b);
        }
        if (labels) {
            label_at(c, b->v, b->name, i < 2 ? C_GOLD : rgb(255, 214, 160), 9, -3, false);
        }
    }
}

// the horizon: the compass points and a tick each 15 degrees
static void draw_compass(const cam_t *c)
{
    static const char *const P[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    float east[3];
    v_cross(c->north, c->up, east);
    static float CS[24][2];
    if (CS[1][0] == 0) {
        for (int k = 0; k < 24; k++) {
            CS[k][0] = cosf(k * 15 * DEG);
            CS[k][1] = sinf(k * 15 * DEG);
        }
    }
    for (int a = 0; a < 360; a += 15) {
        float ca = CS[a / 15][0], sa = CS[a / 15][1], v[3];
        for (int i = 0; i < 3; i++) {
            v[i] = ca * c->north[i] + sa * east[i];
        }
        float x, y;
        if (!project(c, v, &x, &y)) {
            continue;
        }
        int ix = (int)x, iy = (int)y;
        if (a % 45 == 0) {
            const char *s = P[a / 45];
            int w = gfx_text_width(s, a % 90 == 0 ? 2 : 1);
            if (ix - w / 2 > c->vr.x && ix + w / 2 < c->vr.x + c->vr.w && iy + 4 < c->vr.y + c->vr.h - 14 &&
                iy > c->vr.y) {
                gfx_text(ix - w / 2, iy + 4, s, a % 90 == 0 ? 2 : 1, a == 0 ? C_RED : C_GOLD);
            }
            for (int k = 0; k < 4; k++) {
                put(ix, iy - k, C_GOLD);
            }
        } else {
            put(ix, iy, C_DIM);
            put(ix, iy - 1, C_DIM);
        }
    }
}

// ---------------------------------------------------------------- UI helpers

static void panel(rc_t r, uint16_t c)
{
    gfx_fill_rect(r.x, r.y, r.w, r.h, c);
}

static void text_fit(int x, int w, int y, const char *s, int scale, uint16_t c)
{
    while (scale > 1 && gfx_text_width(s, scale) > w) {
        scale--;
    }
    gfx_text_center_in(x, w, y, s, scale, c);
}

static void button(rc_t r, const char *label, const char *sub, uint16_t accent, bool on)
{
    gfx_fill_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, on ? C_PANEL2 : C_PANEL);
    gfx_fill_rect(r.x + 2, r.y + 2, r.w - 4, 3, accent);
    gfx_frame_rect(r.x + 2, r.y + 2, r.w - 4, r.h - 4, 1, C_LINE);
    int scale = 2;
    if (gfx_text_width(label, 2) > r.w - 10 || r.h < 34 * TS(2) / 2) {
        scale = 1;
    }
    int th = 7 * TS(scale) + (sub ? 7 * TS(1) + 3 : 0);
    int ty = r.y + (r.h - th) / 2 + 1;
    text_fit(r.x + 2, r.w - 4, ty, label, scale, C_WHITE);
    if (sub) {
        text_fit(r.x + 2, r.w - 4, ty + 7 * TS(scale) + 3, sub, 1, C_DIM);
    }
}

static void back_btn(void)
{
    rc_t b = BACK_RC;
    gfx_fill_rect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, C_PANEL2);
    gfx_frame_rect(b.x + 2, b.y + 2, b.w - 4, b.h - 4, 1, C_LINE);
    gfx_text_center_in(b.x, b.w, b.y + (b.h - 7 * TS(2)) / 2 + 1, "<", 2, C_WHITE);
}

// text in a box: words wrap at the width; returns the y after the text
static int wrap(int x, int y, int w, const char *s, int scale, uint16_t c, int max_lines)
{
    int cols = w / (6 * TS(scale));
    char ln[64];
    int lines = 0;
    while (*s && lines < max_lines) {
        while (*s == ' ') {
            s++;
        }
        int n = (int)strlen(s);
        if (n > cols) {
            n = cols;
            while (n > 0 && s[n] != ' ') {
                n--;
            }
            if (n == 0) {
                n = cols;
            }
        }
        if (n > (int)sizeof(ln) - 1) {
            n = (int)sizeof(ln) - 1;
        }
        memcpy(ln, s, (size_t)n);
        ln[n] = 0;
        gfx_text(x, y, ln, scale, c);
        y += 9 * TS(scale);
        s += n;
        lines++;
    }
    return y;
}

// ---------------------------------------------------------------- the quiz

typedef struct {
    qkind_t kind;
    int target;  // a constellation (SKY_CONS) or a star (SKY_STARS)
    int choice[4];
    int correct;
    float rot;           // the turn of the view
    float off_x, off_y;  // Q_TAP: the view centre moves from the target (rad)
    bool lines;
    int64_t t0;
    int answered;  // -1 = not yet, else the slot (4 = a tap, 5 = the time ran out)
    bool ok;
    int64_t fb_until;
    int tap_star;  // Q_TAP: the star that the player tapped
} quiz_t;

static quiz_t s_q;
static int s_level, s_item, s_lives;
static uint32_t s_score;
static bool s_new_best;
static uint8_t s_hist[10];  // LEARN: the last 10 answers (1 = correct)
static int s_hist_n, s_since_unlock;
static int s_card;       // LEARN: the card on the screen (index in LEARN)
static bool s_card_new;  // the card of a constellation that just opened
static int64_t s_banner_until;
static char s_banner[40];

#define ITEMS_PER_LEVEL 10
#define Q_TIME_MS       12000

static void banner(const char *t)
{
    snprintf(s_banner, sizeof(s_banner), "%s", t);
    s_banner_until = s_now + 1600;
}

static bool in_list(const int *a, int n, int v)
{
    for (int i = 0; i < n; i++) {
        if (a[i] == v) {
            return true;
        }
    }
    return false;
}

// the pool of constellations of NAME (SKY_CONS indexes)
static int name_pool(int *out)
{
    int n = 0;
    if (s_level <= 3) {
        int m = s_learned > 8 ? s_learned : 8;
        for (int i = 0; i < m && i < N_LEARN; i++) {
            if (s_learn_con[i] >= 0) {
                out[n++] = s_learn_con[i];
            }
        }
    } else {
        for (int i = 0; i < SKY_N_CONS; i++) {
            out[n++] = i;
        }
    }
    return n;
}

// the pool of STARS (SKY_STARS indexes)
static int star_pool(int *out)
{
    int m = s_level == 1 ? 10 : (s_level == 2 ? 20 : (s_level == 3 ? 34 : s_n_famous));
    if (m > s_n_famous) {
        m = s_n_famous;
    }
    int n = 0;
    for (int i = 0; i < m; i++) {
        out[n++] = s_famous[i];
    }
    return n;
}

static void fill_choices(const int *pool, int n)
{
    s_q.correct = irand(4);
    s_q.choice[s_q.correct] = s_q.target;
    for (int k = 0; k < 4; k++) {
        if (k == s_q.correct) {
            continue;
        }
        int c, tries = 0;
        do {
            c = pool[irand(n)];
            tries++;
        } while ((c == s_q.target || in_list(s_q.choice, k, c)) && tries < 200);
        if (tries >= 200) {
            // a small pool: any other constellation or star
            do {
                c = s_mode == M_STARS ? s_famous[irand(s_n_famous)] : irand(SKY_N_CONS);
            } while (c == s_q.target || in_list(s_q.choice, k, c));
        }
        s_q.choice[k] = c;
    }
}

static void next_question(void)
{
    memset(&s_q, 0, sizeof(s_q));
    s_q.answered = -1;
    s_q.tap_star = -1;
    s_q.t0 = s_now;
    static int pool[SKY_N_CONS > N_FAMOUS ? SKY_N_CONS : N_FAMOUS];
    if (s_mode == M_LEARN) {
        int n = 0;
        for (int i = 0; i < s_learned; i++) {
            pool[n++] = s_learn_con[i];
        }
        int newest = s_learn_con[s_learned - 1];
        s_q.kind = Q_CON;
        s_q.target = (irand(100) < 40 || n == 1) ? newest : pool[irand(n)];
        s_q.lines = true;
        s_q.rot = frand(-0.35f, 0.35f);
        // with less than 4 open, the other names come from the next ones on the path
        int m = n;
        for (int i = s_learned; m < 4 && i < N_LEARN; i++) {
            pool[m++] = s_learn_con[i];
        }
        fill_choices(pool, m);
    } else if (s_mode == M_NAME) {
        int n = name_pool(pool);
        s_q.kind = Q_CON;
        s_q.target = pool[irand(n)];
        s_q.lines = s_level != 3 && s_level < 5;
        s_q.rot = s_level == 1 ? frand(-0.3f, 0.3f) : frand(-PI_F, PI_F);
        fill_choices(pool, n);
    } else {
        int n = star_pool(pool);
        s_q.target = pool[irand(n)];
        s_q.kind = s_level == 1 ? Q_STAR : (irand(2) ? Q_TAP : Q_STAR);
        s_q.lines = true;
        s_q.rot = frand(-0.4f, 0.4f);
        if (s_q.kind == Q_TAP) {
            s_q.off_x = frand(-0.18f, 0.18f);
            s_q.off_y = frand(-0.12f, 0.12f);
        } else {
            fill_choices(pool, n);
        }
    }
}

// the camera of the question
static void quiz_cam(cam_t *c)
{
    if (s_q.kind == Q_CON) {
        float v[3];
        con_centre(s_q.target, v);
        cam_atlas(c, Q_VIEW_RC, v, s_q.rot, SKY_CONS[s_q.target].radius + 3.0f);
    } else {
        float v[3];
        memcpy(v, s_sv[s_q.target], sizeof(v));
        if (s_q.kind == Q_TAP) {
            // the centre moves off the target, so that the target is not in the middle
            cam_t tmp;
            cam_atlas(&tmp, Q_VIEW_RC, v, 0, 30);
            for (int i = 0; i < 3; i++) {
                v[i] += s_q.off_x * tmp.r[i] + s_q.off_y * tmp.u[i];
            }
            v_norm(v);
        }
        cam_atlas(c, Q_VIEW_RC, v, s_q.rot, s_q.kind == Q_TAP ? 30 : 22);
    }
}

static void save_best(void)
{
    static const char *const KEY[4] = {NULL, "sky_bname", "sky_bstar", "sky_bfind"};
    if (s_mode != M_LEARN && s_score > s_best[s_mode]) {
        s_best[s_mode] = s_score;
        store_set(KEY[s_mode], s_score);
        s_new_best = true;
    }
}

static void run_over(void)
{
    save_best();
    s_state = ST_RESULT;
    sfx_play(SFX_GAME_OVER);
}

static void learn_unlock_check(void)
{
    int ok = 0;
    for (int i = 0; i < s_hist_n && i < 10; i++) {
        ok += s_hist[i];
    }
    if (s_since_unlock >= 10 && s_hist_n >= 10 && ok >= 9 && s_learned < N_LEARN) {
        s_learned++;
        store_set("sky_learn", (uint32_t)s_learned);
        s_card = s_learned - 1;
        s_card_new = true;
        s_hist_n = 0;
        s_since_unlock = 0;
        s_state = ST_CARD;
        sfx_play(SFX_LEVEL_UP);
    }
}

static void answer(int slot)
{
    if (s_q.answered >= 0) {
        return;
    }
    s_q.answered = slot;
    if (slot < 4) {
        s_q.ok = slot == s_q.correct;
    } else if (slot == 4) {
        s_q.ok = s_q.tap_star == s_q.target;
    } else {
        s_q.ok = false;
    }
    s_q.fb_until = s_now + (s_q.ok ? 800 : 1500);
    int ms = (int)(s_now - s_q.t0);
    if (s_q.ok) {
        sfx_tone(1318, 1760, 120, W_SINE, 40);
        int bonus = (Q_TIME_MS - ms) / 100;
        if (bonus < 0) {
            bonus = 0;
        }
        if (s_mode != M_LEARN) {
            s_score += (uint32_t)((100 + bonus) * s_level);
        } else {
            s_score += 10;
        }
    } else {
        sfx_tone(330, 220, 260, W_TRI, 50);
    }
    if (s_mode == M_LEARN) {
        memmove(s_hist + 1, s_hist, sizeof(s_hist) - 1);
        s_hist[0] = s_q.ok ? 1 : 0;
        if (s_hist_n < 10) {
            s_hist_n++;
        }
        s_since_unlock++;
    }
}

static void after_feedback(void)
{
    if (s_mode == M_LEARN) {
        learn_unlock_check();
        if (s_state == ST_QUIZ) {
            next_question();
        }
        return;
    }
    if (!s_q.ok) {
        s_lives--;
        if (s_lives <= 0) {
            run_over();
            return;
        }
    }
    s_item++;
    if (s_item >= ITEMS_PER_LEVEL) {
        s_level++;
        s_item = 0;
        char t[24];
        snprintf(t, sizeof(t), "LEVEL %d", s_level);
        banner(t);
        sfx_play(SFX_LEVEL_UP);
        save_best();
        if ((s_mode == M_NAME && s_level > 6) || (s_mode == M_STARS && s_level > 5)) {
            run_over();  // all the levels are done
            return;
        }
    }
    next_question();
}

static void quiz_update(void)
{
    if (s_q.answered >= 0) {
        if (s_now >= s_q.fb_until) {
            after_feedback();
        }
        return;
    }
    if (s_mode != M_LEARN && s_level >= 2 && s_now - s_q.t0 > Q_TIME_MS) {
        answer(5);
    }
}

// Q_TAP: the star nearest to the tap (bright stars only)
static int star_at(int x, int y, float mag_max, int radius)
{
    int best = -1, bd = radius * radius + 1;
    for (int i = 0; i < SKY_N_STARS; i++) {
        if (SKY_STARS[i].mag10 > mag_max * 10) {
            break;
        }
        if (s_sx[i] == -32768) {
            continue;
        }
        int dx = s_sx[i] - x, dy = s_sy[i] - y, d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

// ---------------------------------------------------------------- FIND

#define FIND_TARGETS 6
static int s_find_list[FIND_TARGETS];
static int s_find_n, s_find_i;
static int64_t s_find_t0, s_find_hold, s_find_done_at, s_beep_at;
static float s_find_ang;  // the angle to the target now (rad)

static void find_pick(void)
{
    cam_t c;
    cam_live(&c, RC(0, 0, SW, SH));
    int cand[N_LEARN + 16], n = 0;
    int m = s_learned > 8 ? s_learned : 8;
    for (int i = 0; i < m && i < N_LEARN; i++) {
        float v[3];
        int k = s_learn_con[i];
        if (k < 0) {
            continue;
        }
        con_centre(k, v);
        if (alt_of(&c, v) > sinf(12 * DEG)) {
            cand[n++] = k;
        }
    }
    if (n < 3) {
        for (int i = 0; i < N_LEARN && n < N_LEARN + 16; i++) {
            float v[3];
            int k = s_learn_con[i];
            if (k < 0 || in_list(cand, n, k)) {
                continue;
            }
            con_centre(k, v);
            if (alt_of(&c, v) > sinf(12 * DEG)) {
                cand[n++] = k;
            }
        }
    }
    s_find_n = 0;
    while (s_find_n < FIND_TARGETS && n > 0) {
        int j = irand(n);
        s_find_list[s_find_n++] = cand[j];
        cand[j] = cand[--n];
    }
    if (s_find_n == 0) {
        s_find_list[0] = s_learn_con[0];  // nothing is up: Orion (it can be below)
        s_find_n = 1;
    }
    s_find_i = 0;
    s_find_t0 = s_now;
    s_find_hold = 0;
    s_find_done_at = 0;
}

static float find_radius(void)
{
    return (SW < SH ? SW : SH) * 0.24f;
}

static void find_update(void)
{
    if (s_find_done_at) {
        if (s_now - s_find_done_at > 1400) {
            s_find_i++;
            s_find_done_at = 0;
            s_find_hold = 0;
            s_find_t0 = s_now;
            if (s_find_i >= s_find_n) {
                s_level = 1;
                run_over();
            }
        }
        return;
    }
    int con = s_find_list[s_find_i];
    float v[3], x, y;
    con_centre(con, v);
    s_find_ang = acosf(clampf(v_dot(v, s_cam.f), -1, 1));
    bool in = false;
    if (project(&s_cam, v, &x, &y)) {
        float dx = x - s_cam.cx, dy = y - s_cam.cy;
        in = dx * dx + dy * dy < find_radius() * find_radius();
    }
    if (in) {
        if (!s_find_hold) {
            s_find_hold = s_now;
        }
        if (s_now - s_find_hold > 600) {
            s_find_done_at = s_now;
            int sec = (int)((s_now - s_find_t0) / 1000);
            int pts = 300 - 8 * sec;
            s_score += (uint32_t)(pts < 50 ? 50 : pts);
            sfx_play(SFX_WIN);
        }
    } else {
        s_find_hold = 0;
    }
    // the hot and cold beep: faster and higher near the target
    float k = clampf(s_find_ang / PI_F, 0, 1);
    int gap = 150 + (int)(1100 * k);
    if (s_now - s_beep_at > gap) {
        s_beep_at = s_now;
        sfx_tone((uint16_t)(1400 - 800 * k), (uint16_t)(1400 - 800 * k), 40, W_SINE, 24);
    }
}

// ---------------------------------------------------------------- EXPLORE

static bool s_ex_lines = true, s_ex_names = true;
static int s_info_star = -1, s_info_body = -1;
static int64_t s_info_until;

// ---------------------------------------------------------------- touch

static bool s_drag;
static int s_dx0, s_dy0, s_lastx, s_lasty;
static bool s_qpress;    // QUIZ: a finger is down on the answers
static int s_qaim = -1;  // ... the answer under it

static int ans_at(int x, int y)
{
    for (int i = 0; i < 4; i++) {
        if (in_rc(ans_rc(i), x, y)) {
            return i;
        }
    }
    return -1;
}
#define NORTH_HOLD_MS 1000
static int64_t s_north_t0;  // a hold of N started at this time (0 = none)
static bool s_north_done;   // (the hold of this touch set north: its tap says nothing)
static float s_pinch0, s_fov0;
static bool s_pinch;

static void live_tools_input(int x, int y)
{
    if (in_rc(tool_rc(0), x, y)) {
        s_fov = clampf(s_fov * 0.8f, 20, BOARD_BAR_SCREEN ? 120 : 100);  // + = zoom in
        sfx_play(SFX_CLICK);
    } else if (in_rc(tool_rc(1), x, y)) {
        s_fov = clampf(s_fov * 1.25f, 20, BOARD_BAR_SCREEN ? 120 : 100);
        sfx_play(SFX_CLICK);
    } else if (in_rc(tool_rc(2), x, y) && !s_north_done) {
        banner("HOLD N FOR 1 S");
    }
}

// NORTH (for the thumb): N sets the north again and the heading is lost, so N acts only after a hold of 1 s (a
// ring fills)
static void north_hold(void)
{
    if (s_north_t0 && s_now - s_north_t0 >= NORTH_HOLD_MS) {
        s_north_t0 = 0;
        s_north_done = true;
        if (s_use_imu && HAVE_GYRO_SUM()) {
            heading_set(0);  // the board looks to the north now
        } else {
            s_man_az = 0;
        }
        banner("NORTH IS SET");
        sfx_play(SFX_SWITCH);
    }
}

static bool on_tool(int x, int y)
{
    return in_rc(tool_rc(0), x, y) || in_rc(tool_rc(1), x, y) || in_rc(tool_rc(2), x, y) || in_rc(BACK_RC, x, y);
}

static void go_title(void)
{
    if (s_state == ST_QUIZ || s_state == ST_FIND) {
        save_best();
    }
    s_state = ST_TITLE;
    audio_tune_stop();
}

static void start_mode(int i)
{
    sfx_play(SFX_CLICK);
    s_qpress = false;
    s_north_t0 = 0;
    s_score = 0;
    s_level = 1;
    s_item = 0;
    s_lives = 3;
    s_new_best = false;
    s_banner_until = 0;
    switch (i) {
    case 0:
        s_mode = M_LEARN;
        s_card = s_learned - 1;
        s_card_new = false;
        s_state = ST_CARD;
        tune_play(TUNE_SKY);
        break;
    case 1:
        s_mode = M_NAME;
        s_state = ST_QUIZ;
        next_question();
        tune_play(TUNE_SKY);
        break;
    case 2:
        s_mode = M_STARS;
        s_state = ST_QUIZ;
        next_question();
        tune_play(TUNE_SKY);
        break;
    case 3:
        s_mode = M_FIND;
        s_state = ST_FIND;
        s_href_ok = false;
        find_pick();
        tune_play(TUNE_SKY);
        banner(s_use_imu ? "HOLD THE BOARD UP" : "DRAG TO TURN");
        break;
    case 4:
        s_state = ST_EXPLORE;
        s_href_ok = false;
        s_scrub_h = 0;
        s_info_star = s_info_body = -1;
        banner(s_use_imu ? "VIEW SOUTH: HOLD N FOR NORTH" : "DRAG TO LOOK AROUND");
        break;
    default:
        s_state = ST_SETUP;
        break;
    }
}

static void save_place(void)
{
    store_set("sky_city", (uint32_t)s_city);
    store_set("sky_lat", (uint32_t)(int32_t)lroundf((s_lat + 90) * 100));
    store_set("sky_lon", (uint32_t)(int32_t)lroundf((s_lon + 180) * 100));
    store_set("sky_tz", (uint32_t)(int32_t)lroundf((s_tz + 14) * 4));
    store_set("sky_tm", s_manual_t);
}

static void setup_input(int x, int y)
{
    for (int i = 0; i < 6; i++) {
        for (int p = 0; p < 2; p++) {
            if (!in_rc(set_btn(i, p), x, y)) {
                continue;
            }
            int d = p ? 1 : -1;
            sfx_play(SFX_TICK);
            // the manual clock: from the time now
            s_manual_t += (uint32_t)(now_s() - s_manual_at);
            s_manual_at = now_s();
            switch (i) {
            case 0:
                s_city = (s_city + d + N_CITIES + 1) % (N_CITIES + 1);
                if (s_city < N_CITIES) {
                    s_lat = CITIES[s_city].lat;
                    s_lon = CITIES[s_city].lon;
                    s_tz = CITIES[s_city].tz;
                }
                break;
            case 1:
                s_lat = clampf(s_lat + d, -89, 89);
                s_city = N_CITIES;
                break;
            case 2:
                s_lon += d;
                if (s_lon > 180) {
                    s_lon -= 360;
                }
                if (s_lon < -180) {
                    s_lon += 360;
                }
                s_city = N_CITIES;
                break;
            case 3:
                s_tz = clampf(s_tz + d, -12, 14);
                break;
            case 4:
                s_manual_t += (uint32_t)(d * 86400);
                break;
            default:
                s_manual_t += (uint32_t)(d * 3600);
                break;
            }
            s_body_t = -1;
            save_place();
            return;
        }
    }
}

// the live view: drag to turn (not on a button), and the hold of N
static void live_drag(const input_event_t *ev, int x, int y)
{
    if (ev->kind == IN_DOWN) {
        s_drag = !on_tool(x, y);
        s_dx0 = s_lastx = x;
        s_dy0 = s_lasty = y;
        s_north_done = false;
        s_north_t0 = in_rc(tool_rc(2), x, y) ? (s_now > 0 ? s_now : 1) : 0;
    } else if (ev->kind == IN_MOVE && s_drag && !s_pinch) {
        drag_turn((float)(x - s_lastx), (float)(y - s_lasty));
        s_lastx = x;
        s_lasty = y;
    } else if (ev->kind == IN_MOVE && s_north_t0 && !in_rc(tool_rc(2), x, y)) {
        s_north_t0 = 0;
    } else if (ev->kind == IN_UP) {
        s_drag = false;
        s_north_t0 = 0;
    }
}

// QUIZ (for the thumb): an answer is scored where the finger comes off the glass; while the finger is down, the
// answer under it lights, so a person can slide to another one, or off the answers to give none. True when the event
// is used.
static bool quiz_press(const input_event_t *ev, int x, int y)
{
    if (ev->kind == IN_DOWN) {
        s_qpress = s_q.answered < 0 && y >= ans_rc(0).y - 10;
        s_qaim = s_qpress ? ans_at(x, y) : -1;
        if (s_qpress) {
            return true;
        }
    } else if (ev->kind == IN_MOVE && s_qpress) {
        s_qaim = ans_at(x, y);
        return true;
    } else if (ev->kind == IN_UP && s_qpress) {
        int a = ans_at(x, y);
        s_qpress = false;
        s_qaim = -1;
        if (a >= 0 && s_q.answered < 0) {
            answer(a);
        }
        return true;
    }
    return false;
}

// EXPLORE: the tools, the 5 buttons, and a tap on the sky (the nearest body or star)
static void tap_explore(int x, int y)
{
    live_tools_input(x, y);
    if (on_tool(x, y)) {
        return;
    }
    for (int i = 0; i < 5; i++) {
        if (!in_rc(ex_btn(i), x, y)) {
            continue;
        }
        sfx_play(SFX_TICK);
        if (i == 0) {
            s_scrub_h -= 1;
        } else if (i == 1) {
            s_scrub_h = 0;
        } else if (i == 2) {
            s_scrub_h += 1;
        } else if (i == 3) {
            s_ex_lines = !s_ex_lines;
        } else {
            s_ex_names = !s_ex_names;
        }
        return;
    }
    // a tap on the sky: the nearest body or star
    if (abs(x - s_dx0) > 10 || abs(y - s_dy0) > 10) {
        return;
    }
    s_info_body = -1;
    for (int i = 0; i < N_BODY; i++) {
        if (s_bx[i] == -32768) {
            continue;
        }
        if (abs(s_bx[i] - x) < TAP_R && abs(s_by[i] - y) < TAP_R) {
            s_info_body = i;
        }
    }
    s_info_star = s_info_body >= 0 ? -1 : star_at(x, y, 4.6f, TAP_R);
    s_info_until = s_now + 6000;
    if (s_info_star >= 0 || s_info_body >= 0) {
        sfx_play(SFX_STAR);
    }
}

static void sky_input(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
    bool live = s_state == ST_FIND || s_state == ST_EXPLORE;

    if (live) {
        live_drag(ev, x, y);
    }
    if (s_state == ST_QUIZ && s_q.kind != Q_TAP && quiz_press(ev, x, y)) {
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }

    if (s_state != ST_TITLE && in_rc(BACK_RC, x, y)) {
        sfx_play(SFX_BACK);
        go_title();
        return;
    }
    switch (s_state) {
    case ST_TITLE:
        for (int i = 0; i < 6; i++) {
            if (in_rc(title_btn(i), x, y)) {
                start_mode(i);
                return;
            }
        }
        break;
    case ST_CARD:
        if (in_rc(CARD_PREV_RC, x, y) && s_card > 0) {
            s_card--;
            s_card_new = false;
            sfx_play(SFX_TICK);
        } else if (in_rc(CARD_NEXT_RC, x, y) && s_card < s_learned - 1) {
            s_card++;
            s_card_new = false;
            sfx_play(SFX_TICK);
        } else if (in_rc(CARD_GO_RC, x, y)) {
            sfx_play(SFX_CLICK);
            s_state = ST_QUIZ;
            next_question();
        }
        break;
    case ST_QUIZ:
        if (s_q.answered >= 0) {
            break;
        }
        if (s_q.kind == Q_TAP) {
            if (in_rc(Q_VIEW_RC, x, y)) {
#if BOARD_UI_TABLET || BOARD_ROUND
                int rad = 44;  // (7.5 mm; the round panel 4.2 mm)
#else
                int rad = BOARD_BAR_SCREEN ? 26 : 30;
#endif
                int t = s_q.target, dx = s_sx[t] - x, dy = s_sy[t] - y;
                if (s_sx[t] != -32768 && dx * dx + dy * dy <= rad * rad) {
                    s_q.tap_star = t;
                } else {
                    s_q.tap_star = star_at(x, y, 4.0f, rad);
                }
                answer(4);
            }
        }
        break;  // (the 4 answers act at the lift: quiz_press)
    case ST_FIND:
        live_tools_input(x, y);
        break;
    case ST_EXPLORE:
        tap_explore(x, y);
        break;
    case ST_SETUP:
        setup_input(x, y);
        break;
    case ST_RESULT:
        if (in_rc(R_AGAIN_RC, x, y)) {
            start_mode(s_mode == M_NAME ? 1 : (s_mode == M_STARS ? 2 : 3));
        } else if (in_rc(R_MENU_RC, x, y)) {
            sfx_play(SFX_BACK);
            go_title();
        }
        break;
    }
}

// two fingers in the live view: a pinch zooms
static void pinch_update(void)
{
    int16_t px[2], py[2];
    int n = input_points(px, py, 2);
    if (n >= 2) {
        int x0, y0, x1, y1;
        gfx_touch_to_logical(px[0], py[0], &x0, &y0);
        gfx_touch_to_logical(px[1], py[1], &x1, &y1);
        float d = sqrtf((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
        if (d < 10) {
            d = 10;
        }
        if (!s_pinch) {
            s_pinch = true;
            s_pinch0 = d;
            s_fov0 = s_fov;
        } else {
            s_fov = clampf(s_fov0 * s_pinch0 / d, 20, BOARD_BAR_SCREEN ? 120 : 100);
        }
    } else {
        s_pinch = false;
    }
}

// ---------------------------------------------------------------- the app

static void sntp_start(void)
{
#ifdef SKY_SNTP
    if (!esp_sntp_enabled() && wifi_state() == WIFI_CONNECTED) {
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
    }
#endif
}

static void sky_init(void)
{
    // the vectors of the stars, the line points and the galaxy
    for (int i = 0; i < SKY_N_STARS; i++) {
        v_radec(SKY_RA_DEG(SKY_STARS[i].ra) * DEG, SKY_DEC_DEG(SKY_STARS[i].dec) * DEG, s_sv[i]);
    }
    for (int i = 0; i < SKY_N_PTS; i++) {
        v_radec(SKY_RA_DEG(SKY_PTS[i].ra) * DEG, SKY_DEC_DEG(SKY_PTS[i].dec) * DEG, s_pv[i]);
    }
    for (int k = 0; k < SKY_N_CONS; k++) {
        v_radec(SKY_RA_DEG(SKY_CONS[k].ra) * DEG, SKY_DEC_DEG(SKY_CONS[k].dec) * DEG, s_cv[k]);
    }
    v_radec(192.85948f * DEG, 27.12825f * DEG, s_ngp);
    v_radec(266.40500f * DEG, -28.93617f * DEG, s_gc);
    v_cross(s_ngp, s_gc, s_gy);
    v_norm(s_gy);
    for (int i = 0; i < N_LEARN; i++) {
        s_learn_con[i] = -1;
        for (int k = 0; k < SKY_N_CONS; k++) {
            if (!strcmp(SKY_CONS[k].abbr, LEARN[i].abbr)) {
                s_learn_con[i] = (int16_t)k;
            }
        }
    }
    s_n_famous = 0;
    for (int f = 0; f < N_FAMOUS; f++) {
        for (int i = 0; i < SKY_N_STARS; i++) {
            if (SKY_STARS[i].name != 255 && !strcmp(SKY_NAMES[SKY_STARS[i].name], FAMOUS[f])) {
                s_famous[s_n_famous++] = (int16_t)i;
                break;
            }
        }
    }
    s_data_ok = s_learn_con[0] >= 0 && s_n_famous >= 8;
    // the saved progress and place
    s_learned = (int)store_get("sky_learn", 1);
    if (s_learned < 1 || s_learned > N_LEARN) {
        s_learned = 1;
    }
    s_best[1] = store_get("sky_bname", 0);
    s_best[2] = store_get("sky_bstar", 0);
    s_best[3] = store_get("sky_bfind", 0);
    s_city = (int)store_get("sky_city", 0);
    if (s_city < 0 || s_city > N_CITIES) {
        s_city = 0;
    }
    if (s_city < N_CITIES) {
        s_lat = CITIES[s_city].lat;
        s_lon = CITIES[s_city].lon;
        s_tz = CITIES[s_city].tz;
    } else {
        s_lat = clampf((int32_t)store_get("sky_lat", 12208) / 100.0f - 90, -89, 89);
        s_lon = clampf((int32_t)store_get("sky_lon", 21478) / 100.0f - 180, -180, 180);
        s_tz = clampf((int32_t)store_get("sky_tz", 68) / 4.0f - 14, -12, 14);
    }
    s_manual_t = store_get("sky_tm", 0);
    if (s_manual_t < 1735689600u) {
        s_manual_t = default_manual_t();
    }
    s_manual_at = now_s();
}

static void sky_enter(int64_t now)
{
    tables_make();
    s_now = s_prev = now;
    s_state = ST_TITLE;
    s_use_imu = imu_live();
    s_man_az = PI_F;
    s_man_alt = 30 * DEG;
    s_href_ok = false;
    s_drag = s_pinch = false;
    s_body_t = -1;
    s_banner_until = 0;
    sntp_start();
}

static void sky_leave(void)
{
    if (s_state == ST_QUIZ || s_state == ST_FIND) {
        save_best();
    }
    audio_tune_stop();
}

static void sky_update(int64_t now)
{
    s_now = now;
    s_prev = now;
    north_hold();
    if ((now / 1000) % 5 == 0) {
        sntp_start();
    }
    s_use_imu = imu_live();
    switch (s_state) {
    case ST_QUIZ:
        quiz_update();
        break;
    case ST_FIND:
        pinch_update();
        cam_live(&s_cam, RC(0, 0, SW, SH));
        find_update();
        break;
    case ST_EXPLORE:
        pinch_update();
        cam_live(&s_cam, RC(0, 0, SW, SH));
        if (s_now > s_info_until) {
            s_info_star = s_info_body = -1;
        }
        break;
    default:
        break;
    }
}

static bool sky_awake(void)
{
    // the board is held up and moves, with no touch: keep the screen on in the live views
    return s_state == ST_FIND || s_state == ST_EXPLORE;
}

// ---------------------------------------------------------------- drawing

// the live sky: the background, the lines, the stars, the planets, the horizon
static void render_live_sky(const cam_t *c, int highlight, bool all_lines, bool names)
{
    double t = sky_time();
    bodies_update(t);
    s_sun_alt = asinf(clampf(alt_of(c, s_body[0].v), -1, 1)) / DEG;
    s_day = clampf((s_sun_alt + 10) / 14, 0, 1);
    s_clip = c->vr;
    label_reset(true);
    draw_sky_bg(c);
    if (all_lines) {
        draw_lines(c, -1, rgb(40, 66, 112), 1);
    }
    if (highlight >= 0) {
        draw_lines(c, highlight, C_GOLD, 2);
    }
    draw_stars(c, 4.6f, 1);
    draw_bodies(c, names);
    draw_compass(c);
    if (names) {
        for (int k = 0; k < SKY_N_CONS; k++) {
            if (SKY_CONS[k].rank > 2 && s_fov > 70) {
                continue;
            }
            float v[3];
            con_centre(k, v);
            label_at(c, v, SKY_CONS[k].name, rgb(110, 150, 220), 0, 0, true);
        }
        for (int i = 0; i < SKY_N_STARS && SKY_STARS[i].mag10 <= 15; i++) {
            if (SKY_STARS[i].name == 255 || s_sx[i] == -32768) {
                continue;
            }
            label_at(c, s_sv[i], SKY_NAMES[SKY_STARS[i].name], rgb(210, 214, 230), 5, -9, false);
        }
    }
}

static void live_tools(void)
{
    back_btn();
    button(tool_rc(0), "+", NULL, C_CYAN, false);
    button(tool_rc(1), "-", NULL, C_CYAN, false);
    rc_t n = tool_rc(2);
    button(n, "N", NULL, C_RED, false);
    if (s_north_t0) {
        // the ring of the hold
        float f = (float)(s_now - s_north_t0) / NORTH_HOLD_MS;
        if (f > 1) {
            f = 1;
        }
        int cx = n.x + n.w / 2, cy = n.y + n.h / 2, r = n.h / 2 - 3;
        for (int k = 0; k < (int)(f * 48); k++) {
            float a = (float)k / 48.0f * 6.2831853f - 1.5707963f;
            gfx_fill_rect(cx + (int)(cosf(a) * r) - 1, cy + (int)(sinf(a) * r) - 1, 3, 3, C_GOLD);
        }
    }
}

static void draw_banner(void)
{
    if (s_now >= s_banner_until) {
        return;
    }
    int w = gfx_text_width(s_banner, 2) + 24;
    if (w > SW - 20) {
        w = SW - 20;
    }
    int x = (SW - w) / 2, y = SH / 2 - 16;
    gfx_fill_rect(x, y, w, 7 * TS(2) + 16, C_PANEL);
    gfx_frame_rect(x, y, w, 7 * TS(2) + 16, 1, C_GOLD);
    text_fit(x, w, y + 8, s_banner, 2, C_GOLD);
}

// the title: a slow sky behind the name and the buttons
static void render_title(int64_t now)
{
    cam_t c;
    float v[3];
    float a = (float)(now % 240000) / 240000.0f * 2 * PI_F;
    v_radec(a, 20 * DEG, v);
    cam_atlas(&c, RC(0, 0, SW, SH), v, 0.3f * sinf(a), BOARD_BAR_SCREEN ? 16 : 40);
    s_clip = c.vr;
    s_day = 0;
    draw_sky_bg(&c);
    draw_lines(&c, -1, rgb(36, 56, 100), 1);
    draw_stars(&c, 4.6f, 1);
#if BOARD_BAR_SCREEN
    text_sh(18, 10, "SKY", 5, C_GOLD);
    text_sh(124, 14, "LEARN THE NIGHT SKY", 1, C_WHITE);
    text_sh(124, 28, "HOLD THE BOARD UP: IT IS A WINDOW TO THE STARS", 1, C_DIM);
    char pl[48];
    snprintf(pl, sizeof(pl), "%s  %.1f%c %.1f%c", s_city < N_CITIES ? CITIES[s_city].name : "CUSTOM",
             (double)fabsf(s_lat), s_lat >= 0 ? 'N' : 'S', (double)fabsf(s_lon), s_lon >= 0 ? 'E' : 'W');
    text_sh(124, 42, pl, 1, C_CYAN);
#elif BOARD_ROUND
    text_sh((SW - gfx_text_width("SKY", 4)) / 2, RU(26), "SKY", 4, C_GOLD);
    gfx_text_center(RU(80), "LEARN THE NIGHT SKY", 1, C_WHITE);
    gfx_text_center(RU(102), "A WINDOW TO THE STARS", 1, C_DIM);
#elif BOARD_UI_TABLET
    text_sh((SW - gfx_text_width("SKY", 4)) / 2, 10, "SKY", 4, C_GOLD);
    gfx_text_center(66, "LEARN THE NIGHT SKY", 1, C_WHITE);
    gfx_text_center(88, "HOLD THE BOARD UP: A WINDOW TO THE STARS", 1, C_DIM);
    char pl[48];
    snprintf(pl, sizeof(pl), "%s  %.1f%c %.1f%c", s_city < N_CITIES ? CITIES[s_city].name : "CUSTOM",
             (double)fabsf(s_lat), s_lat >= 0 ? 'N' : 'S', (double)fabsf(s_lon), s_lon >= 0 ? 'E' : 'W');
    gfx_text_center(108, pl, 1, C_CYAN);
#else
    text_sh((SW - gfx_text_width("SKY", 4)) / 2, 10, "SKY", 4, C_GOLD);
    gfx_text_center(46, "LEARN THE NIGHT SKY", 1, C_WHITE);
    gfx_text_center(60, "HOLD THE BOARD UP: A WINDOW", 1, C_DIM);
    gfx_text_center(72, "TO THE STARS", 1, C_DIM);
#endif
    char sub[6][24];
    snprintf(sub[0], sizeof(sub[0]), "%d OF %d", s_learned, N_LEARN);
    snprintf(sub[1], sizeof(sub[1]), "BEST %lu", (unsigned long)s_best[1]);
    snprintf(sub[2], sizeof(sub[2]), "BEST %lu", (unsigned long)s_best[2]);
    snprintf(sub[3], sizeof(sub[3]), "BEST %lu", (unsigned long)s_best[3]);
    snprintf(sub[4], sizeof(sub[4]), "THE SKY NOW");
    snprintf(sub[5], sizeof(sub[5]), "PLACE, TIME");
    static const char *const LBL[6] = {"LEARN", "NAME", "STARS", "FIND", "EXPLORE", "SETUP"};
    static const uint8_t HUE[6][3] = {{100, 232, 130}, {110, 200, 255}, {255, 196, 80},
                                      {255, 120, 120}, {190, 140, 255}, {170, 180, 210}};
    for (int i = 0; i < 6; i++) {
        button(title_btn(i), LBL[i], sub[i], rgb(HUE[i][0], HUE[i][1], HUE[i][2]), false);
    }
}

// the main stars of a constellation (named, bright), for the card
static int con_stars(int con, int *out, int max)
{
    int n = 0;
    for (int i = 0; i < SKY_N_STARS && n < max; i++) {
        if (SKY_STARS[i].con == con && SKY_STARS[i].name != 255 && SKY_STARS[i].mag10 <= 32) {
            out[n++] = i;
        }
    }
    return n;
}

static void render_card(int64_t now)
{
    int con = s_learn_con[s_card];
    if (con < 0) {
        con = 0;
    }
    cam_t c;
    float v[3];
    con_centre(con, v);
    cam_atlas(&c, CARD_VIEW_RC, v, 0.05f * sinf((float)now / 2000.0f), SKY_CONS[con].radius + 4.0f);
    s_clip = c.vr;
    label_reset(false);
    s_day = 0;
    draw_sky_bg(&c);
    draw_lines(&c, -1, rgb(34, 50, 90), 1);
    // the lines light up one after the other
    draw_lines(&c, con, C_GOLD, 2);
    draw_stars(&c, 4.6f, 1);
    int st[6];
    int n = con_stars(con, st, 6);
    for (int k = 0; k < n; k++) {
        label_at(&c, s_sv[st[k]], SKY_NAMES[SKY_STARS[st[k]].name], C_WHITE, 5, -9, false);
    }
    back_btn();
    char t[48];
    rc_t p;
#if BOARD_BAR_SCREEN
    p = RC(400, 0, 240, 172);
    panel(p, C_BG);
    gfx_fill_rect(400, 0, 3, 172, C_GOLD);
    snprintf(t, sizeof(t), "%s%d OF %d", s_card_new ? "NEW!  " : "", s_card + 1, N_LEARN);
    gfx_text(410, 6, t, 1, s_card_new ? C_GREEN : C_DIM);
    text_fit(406, 230, 18, SKY_CONS[con].name, 2, C_GOLD);
    int y = wrap(410, 38, 224, LEARN[s_card].fact, 1, C_WHITE, 4);
    char ks[64] = "";
    for (int k = 0; k < n && k < 3; k++) {
        if (k) {
            strncat(ks, ", ", sizeof(ks) - strlen(ks) - 1);
        }
        strncat(ks, SKY_NAMES[SKY_STARS[st[k]].name], sizeof(ks) - strlen(ks) - 1);
    }
    if (n) {
        wrap(410, y + 4, 224, ks, 1, C_CYAN, 2);
    }
#elif BOARD_ROUND
    p = RC(0, RU(268), SW, SH - RU(268));
    panel(p, C_BG);
    gfx_fill_rect(0, RU(268), SW, 3, C_GOLD);
    gfx_text_center(RU(278), SKY_CONS[con].name, 2, C_GOLD);
    snprintf(t, sizeof(t), "%s%d OF %d", s_card_new ? "NEW!  " : "", s_card + 1, N_LEARN);
    gfx_text_center(RU(306), t, 1, s_card_new ? C_GREEN : C_DIM);
    wrap(RU(52), RU(326), RU(362), LEARN[s_card].fact, 1, C_WHITE, 2);
#elif BOARD_UI_TABLET
    p = RC(0, 198, 640, 162);
    panel(p, C_BG);
    gfx_fill_rect(0, 198, 640, 3, C_GOLD);
    snprintf(t, sizeof(t), "%s%d OF %d", s_card_new ? "NEW!  " : "", s_card + 1, N_LEARN);
    gfx_text(SW - 10 - gfx_text_width(t, 1), 210, t, 1, s_card_new ? C_GREEN : C_DIM);
    gfx_text(10, 208, SKY_CONS[con].name, 2, C_GOLD);
    int y = wrap(10, 236, 620, LEARN[s_card].fact, 1, C_WHITE, 2);
    char ks[64] = "";
    for (int k = 0; k < n && k < 3; k++) {
        if (k) {
            strncat(ks, ", ", sizeof(ks) - strlen(ks) - 1);
        }
        strncat(ks, SKY_NAMES[SKY_STARS[st[k]].name], sizeof(ks) - strlen(ks) - 1);
    }
    if (n) {
        wrap(10, y, 620, ks, 1, C_CYAN, 1);
    }
#else
    p = RC(0, 132, 320, 108);
    panel(p, C_BG);
    gfx_fill_rect(0, 132, 320, 2, C_GOLD);
    snprintf(t, sizeof(t), "%s%d OF %d", s_card_new ? "NEW!  " : "", s_card + 1, N_LEARN);
    gfx_text(SW - 6 - gfx_text_width(t, 1), 140, t, 1, s_card_new ? C_GREEN : C_DIM);
    gfx_text(6, 138, SKY_CONS[con].name, 2, C_GOLD);
    int y = wrap(6, 158, 308, LEARN[s_card].fact, 1, C_WHITE, 2);
    char ks[64] = "";
    for (int k = 0; k < n && k < 3; k++) {
        if (k) {
            strncat(ks, ", ", sizeof(ks) - strlen(ks) - 1);
        }
        strncat(ks, SKY_NAMES[SKY_STARS[st[k]].name], sizeof(ks) - strlen(ks) - 1);
    }
    if (n) {
        wrap(6, y, 308, ks, 1, C_CYAN, 1);
    }
#endif
    button(CARD_PREV_RC, "<", NULL, C_DIM, s_card > 0);
    button(CARD_NEXT_RC, ">", NULL, C_DIM, s_card < s_learned - 1);
    button(CARD_GO_RC, "PRACTICE", NULL, C_GREEN, false);
}

static void hearts(int x, int y)
{
#if BOARD_UI_TABLET || BOARD_ROUND
    for (int i = 0; i < 3; i++) {
        gfx_fill_circle(x + i * 22 + 8, y + 6, 7, i < s_lives ? C_RED : C_LINE);
    }
#else
    for (int i = 0; i < 3; i++) {
        gfx_fill_circle(x + i * 14 + 5, y + 4, 4, i < s_lives ? C_RED : C_LINE);
    }
#endif
}

static void render_quiz(int64_t now)
{
    cam_t c;
    quiz_cam(&c);
    s_clip = c.vr;
    label_reset(false);
    s_day = 0;
    draw_sky_bg(&c);
    bool done = s_q.answered >= 0;
    if (s_q.kind == Q_CON) {
        if (s_q.lines || done) {
            draw_lines(&c, s_q.target, done ? (s_q.ok ? C_GREEN : C_GOLD) : rgb(120, 170, 240), 2);
        }
        draw_stars(&c, 4.6f, 1);
        if (done) {
            float v[3];
            con_centre(s_q.target, v);
            label_at(&c, v, SKY_CONS[s_q.target].name, C_WHITE, 0, 14, true);
        }
    } else {
        draw_lines(&c, -1, rgb(34, 52, 92), 1);
        draw_stars(&c, 4.6f, 1);
        float x, y;
        bool show = s_q.kind == Q_STAR || done;
        if (show && project(&c, s_sv[s_q.target], &x, &y)) {
            uint16_t rc = done ? (s_q.ok ? C_GREEN : C_GOLD) : C_CYAN;
            gfx_ring((int)x, (int)y, 11, 9, rc);
            if (done) {
                text_sh((int)x + 14, (int)y - 4, SKY_NAMES[SKY_STARS[s_q.target].name], 1, C_WHITE);
            }
        }
        if (done && s_q.kind == Q_TAP && !s_q.ok && s_q.tap_star >= 0 && s_sx[s_q.tap_star] != -32768) {
            gfx_ring(s_sx[s_q.tap_star], s_sy[s_q.tap_star], 9, 7, C_RED);
        }
    }
    back_btn();
    // the question and the score
    char q[48], sc[40];
    if (s_q.kind == Q_CON) {
        snprintf(q, sizeof(q), "WHICH CONSTELLATION?");
    } else if (s_q.kind == Q_STAR) {
        snprintf(q, sizeof(q), "WHICH STAR IS IN THE RING?");
    } else {
        snprintf(q, sizeof(q), "TAP %s", SKY_NAMES[SKY_STARS[s_q.target].name]);
    }
    if (s_mode == M_LEARN) {
        int ok = 0;
        for (int i = 0; i < s_hist_n; i++) {
            ok += s_hist[i];
        }
        snprintf(sc, sizeof(sc), "%d OPEN  %d/10", s_learned, ok);
    } else {
        snprintf(sc, sizeof(sc), "L%d  %d/%d  %lu", s_level, s_item + 1, ITEMS_PER_LEVEL, (unsigned long)s_score);
    }
#if BOARD_BAR_SCREEN
    panel(RC(400, 0, 240, 172), C_BG);
    gfx_fill_rect(400, 0, 3, 172, C_CYAN);
    text_fit(404, 234, 6, q, 1, C_WHITE);
    gfx_text(408, 22, sc, 1, C_DIM);
    if (s_mode != M_LEARN) {
        hearts(590, 22);
    }
    if (s_mode == M_LEARN) {
        // the bar of the last 10 answers
        for (int i = 0; i < 10; i++) {
            uint16_t col = i < s_hist_n ? (s_hist[i] ? C_GREEN : C_RED) : C_LINE;
            gfx_fill_rect(560 + i * 7, 24, 5, 6, col);
        }
    }
#elif BOARD_ROUND
    // the question and the score in the top cap; the answers in the lower band
    gfx_fill_rect(0, 0, SW, RU(108), C_BG);
    text_fit(RU(70), RU(326), RU(46), q, 1, C_WHITE);
    if (s_mode != M_LEARN) {
        gfx_text(SW / 2 - RU(110), RU(76), sc, 1, C_DIM);
        hearts(SW / 2 + RU(60), RU(72));
    } else {
        gfx_text_center(RU(76), sc, 1, C_DIM);
    }
    panel(RC(0, Q_VIEW_RC.h, SW, SH - Q_VIEW_RC.h), C_BG);
#elif BOARD_UI_TABLET
    gfx_fill_rect(BACK_RC.x + BACK_RC.w, 0, SW - BACK_RC.x - BACK_RC.w, 40, C_BG);
    text_fit(BACK_RC.x + BACK_RC.w + 4, 420, 4, q, 1, C_WHITE);
    gfx_text(BACK_RC.x + BACK_RC.w + 8, 22, sc, 1, C_DIM);
    if (s_mode != M_LEARN) {
        hearts(560, 18);
    }
    panel(RC(0, 212, 640, 148), C_BG);
#else
    gfx_fill_rect(BACK_RC.x + BACK_RC.w, 0, 286 - BACK_RC.x - BACK_RC.w + (BOARD_ROUND ? 0 : 34), 26, C_BG);
    text_fit(BACK_RC.x + BACK_RC.w + 2, BOARD_ROUND ? 190 : 256, 3, q, 1, C_WHITE);
    gfx_text(BACK_RC.x + BACK_RC.w + 4, 15, sc, 1, C_DIM);
    if (s_mode != M_LEARN) {
        hearts(BOARD_ROUND ? 236 : 274, 14);
    }
    panel(RC(0, 142, 320, 98), C_BG);
#endif
    if (s_mode != M_LEARN && s_level >= 2 && !done) {
        // the time bar
        int left = Q_TIME_MS - (int)(now - s_q.t0);
        if (left < 0) {
            left = 0;
        }
#if BOARD_BAR_SCREEN
        gfx_fill_rect(404, 36, 232 * left / Q_TIME_MS, 3, left < 3000 ? C_RED : C_GOLD);
#elif BOARD_ROUND
        gfx_fill_rect(RU(40), Q_VIEW_RC.h, (SW - RU(80)) * left / Q_TIME_MS, 3, left < 3000 ? C_RED : C_GOLD);
#elif BOARD_UI_TABLET
        gfx_fill_rect(0, 212, 640 * left / Q_TIME_MS, 3, left < 3000 ? C_RED : C_GOLD);
#else
        gfx_fill_rect(0, 148, 320 * left / Q_TIME_MS, 2, left < 3000 ? C_RED : C_GOLD);
#endif
    }
    if (s_q.kind == Q_TAP) {
#if BOARD_BAR_SCREEN
        wrap(410, 60, 224, "TAP THE STAR IN THE VIEW AT THE LEFT. BRIGHT STARS ONLY.", 1, C_DIM, 4);
#elif BOARD_ROUND
        gfx_text_center(RU(320), "TAP THE STAR IN THE VIEW ABOVE", 1, C_DIM);
#elif BOARD_UI_TABLET
        gfx_text_center(270, "TAP THE STAR IN THE VIEW ABOVE", 1, C_DIM);
#else
        gfx_text_center(170, "TAP THE STAR IN THE VIEW ABOVE", 1, C_DIM);
#endif
    } else {
        for (int i = 0; i < 4; i++) {
            const char *nm =
                    s_q.kind == Q_CON ? SKY_CONS[s_q.choice[i]].name : SKY_NAMES[SKY_STARS[s_q.choice[i]].name];
            uint16_t acc = C_LINE;
            bool on = false;
            if (done && i == s_q.correct) {
                acc = C_GREEN;
                on = true;
            } else if (done && i == s_q.answered) {
                acc = C_RED;
            } else if (!done && s_qpress && i == s_qaim) {
                acc = C_GOLD;  // the answer under the finger (the lift scores it)
                on = true;
            }
            button(ans_rc(i), nm, NULL, acc, on);
        }
    }
    draw_banner();
}

// the arrow at the edge of the view that points to the target
static void edge_arrow(const cam_t *c, const float *v)
{
    float x = v_dot(v, c->r), y = v_dot(v, c->u), z = v_dot(v, c->f);
    float ang = atan2f(-y, x);  // screen direction (y down)
    if (z > 0) {
        float sx = c->cx + c->F * x / z, sy = c->cy - c->F * y / z;
        if (sx > 20 && sx < SW - 20 && sy > 40 && sy < SH - 20) {
            return;  // on the screen
        }
    }
    float dx = cosf(ang), dy = sinf(ang);
#if BOARD_ROUND
    float k = SW * 0.5f - RU(40);  // (the round panel: on a circle inside the edge)
#else
    float hw = SW * 0.5f - 22, hh = SH * 0.5f - 24;
    float k = fminf(hw / fmaxf(fabsf(dx), 1e-3f), hh / fmaxf(fabsf(dy), 1e-3f));
#endif
    int ax = (int)(c->cx + dx * k), ay = (int)(c->cy + dy * k);
    uint16_t col = ((s_now / 250) & 1) ? C_GOLD : C_WHITE;
    for (int t = 0; t < 14; t++) {
        float w = (14 - t) * 0.6f;
        float bx = ax - dx * t, by = ay - dy * t;
        for (int s = -(int)w; s <= (int)w; s++) {
            int px = (int)(bx - dy * s), py = (int)(by + dx * s);
            if (px >= 0 && py >= 0 && px < SW && py < SH) {
                g_fb[py * gfx_w + px] = col;
            }
        }
    }
}

static void render_find(int64_t now)
{
    int con = s_find_list[s_find_i < s_find_n ? s_find_i : 0];
    render_live_sky(&s_cam, s_find_done_at ? con : -1, false, false);
    // the circle in the middle
    int r = (int)find_radius();
    uint16_t cc = s_find_hold ? C_GREEN : rgb(120, 140, 190);
    gfx_ring((int)s_cam.cx, (int)s_cam.cy, r, r - 1, cc);
    if (s_find_hold && !s_find_done_at) {
        int k = (int)((now - s_find_hold) * r / 600);
        if (k > r) {
            k = r;
        }
        gfx_ring((int)s_cam.cx, (int)s_cam.cy, k, k > 2 ? k - 2 : 0, C_GREEN);
    }
    float v[3];
    con_centre(con, v);
    if (!s_find_done_at) {
        edge_arrow(&s_cam, v);
    }
    live_tools();
    char t[48];
    snprintf(t, sizeof(t), s_find_done_at ? "FOUND %s!" : "FIND %s", SKY_CONS[con].name);
#if BOARD_UI_TABLET
    int y = 72, x = 12, ly = 20;  // (the lines: 20 px apart)
#elif BOARD_ROUND
    int y = RU(96), x = RU(96), ly = 20;
#else
    int y = BOARD_BAR_SCREEN ? 8 : 40;
    int x = BOARD_BAR_SCREEN ? 72 : 8, ly = 12;
#endif
    text_sh(x, y, t, 2, s_find_done_at ? C_GREEN : C_GOLD);
    snprintf(t, sizeof(t), "%d/%d  %lu  %ds", s_find_i + 1, s_find_n, (unsigned long)s_score,
             (int)((now - s_find_t0) / 1000));
    text_sh(x, y + 6 + ly, t, 1, C_WHITE);
    if (s_cam.live && alt_of(&s_cam, v) < 0) {
        text_sh(x, y + 6 + 2 * ly, "IT IS BELOW THE HORIZON NOW", 1, C_RED);
    }
    if (s_day > 0.5f) {
        text_sh(x, y + 6 + 3 * ly, BOARD_ROUND ? "THE SUN IS UP" : "THE SUN IS UP: THESE STARS ARE BEHIND THE DAYLIGHT",
                1, C_DIM);
    }
    draw_banner();
}

static void render_explore(int64_t now)
{
    render_live_sky(&s_cam, -1, s_ex_lines, s_ex_names);
    gfx_ring((int)s_cam.cx, (int)s_cam.cy, 4, 3, rgb(150, 160, 200));
    live_tools();
    static const char *const L[5] = {"-1H", "NOW", "+1H", "LINES", "NAMES"};
    for (int i = 0; i < 5; i++) {
        button(ex_btn(i), L[i], NULL, i >= 3 ? C_VIOLET : C_CYAN, (i == 3 && s_ex_lines) || (i == 4 && s_ex_names));
    }
    // the date and the time
    double t = sky_time() + s_tz * 3600.0;
    int64_t days = (int64_t)floor(t / 86400.0);
    int sec = (int)(t - (double)days * 86400.0);
    int y, m, d;
    civil(days, &y, &m, &d);
    char s[48];
    snprintf(s, sizeof(s), "%02d.%02d.%04d %02d:%02d%s", d, m, y, sec / 3600, (sec / 60) % 60,
             clock_ok() ? "" : " (SET)");
#if BOARD_UI_TABLET
    int x0 = 12, y0 = 72, ly = 20;
#elif BOARD_ROUND
    int x0 = RU(110), y0 = RU(98), ly = 20;
#else
    int x0 = BOARD_BAR_SCREEN ? 72 : (BOARD_ROUND ? 40 : 4), y0 = BOARD_BAR_SCREEN ? 6 : 48, ly = 12;
#endif
    text_sh(x0, y0, s, 1, s_scrub_h != 0 ? C_GOLD : C_WHITE);
    // the direction of the view: the azimuth and the altitude
    float east[3];
    v_cross(s_cam.north, s_cam.up, east);
    float fa = asinf(clampf(alt_of(&s_cam, s_cam.f), -1, 1)) / DEG;
    float fz = atan2f(v_dot(s_cam.f, east), v_dot(s_cam.f, s_cam.north)) / DEG;
    if (fz < 0) {
        fz += 360;
    }
    snprintf(s, sizeof(s), "AZ %03d  ALT %+03d", (int)fz, (int)fa);
    text_sh(x0, y0 + ly, s, 1, C_DIM);
    // the information card of a tapped star or body
    if (s_info_star >= 0 || s_info_body >= 0) {
        char l1[48], l2[48], l3[48];
        if (s_info_body >= 0) {
            const body_t *b = &s_body[s_info_body];
            snprintf(l1, sizeof(l1), "%s", b->name);
            if (s_info_body == 1) {
                snprintf(l2, sizeof(l2), "%d%% LIT", (int)(s_moon_lit * 100));
            } else if (s_info_body == 0) {
                snprintf(l2, sizeof(l2), "OUR STAR");
            } else {
                snprintf(l2, sizeof(l2), "A PLANET, MAG %.1f", (double)b->mag);
            }
            float alt = asinf(clampf(alt_of(&s_cam, b->v), -1, 1)) / DEG;
            snprintf(l3, sizeof(l3), "ALTITUDE %d DEG", (int)alt);
        } else {
            const sky_star_t *st = &SKY_STARS[s_info_star];
            star_name(s_info_star, l1, sizeof(l1));
            snprintf(l2, sizeof(l2), "MAG %.1f  %s%s", (double)(st->mag10 / 10.0f), st->con != 255 ? "IN " : "",
                     st->con != 255 ? SKY_CONS[st->con].name : "");
            static const char *const COL[] = {"BLUE", "BLUE-WHITE", "WHITE", "YELLOW-WHITE", "YELLOW", "ORANGE", "RED"};
            const char *cl = "";
            switch (st->spec) {
            case 'O':
                cl = COL[0];
                break;
            case 'B':
                cl = COL[1];
                break;
            case 'A':
                cl = COL[2];
                break;
            case 'F':
                cl = COL[3];
                break;
            case 'G':
                cl = COL[4];
                break;
            case 'K':
                cl = COL[5];
                break;
            case 'M':
                cl = COL[6];
                break;
            default:
                break;
            }
            if (st->ly) {
                snprintf(l3, sizeof(l3), "%u LIGHT-YEARS  %s", (unsigned)st->ly, cl);
            } else {
                snprintf(l3, sizeof(l3), "%s", cl);
            }
        }
#if BOARD_BAR_SCREEN
        rc_t p = RC(SW - 250, 40, 248, 46);
#elif BOARD_UI_TABLET
        rc_t p = RC(8, 212, 624, 78);
#elif BOARD_ROUND
        rc_t p = RC(RU(64), RU(262), RU(338), RU(74));
#else
        rc_t p = RC(4, 150, 312, 46);
#endif
        gfx_fill_rect(p.x, p.y, p.w, p.h, C_PANEL);
        gfx_frame_rect(p.x, p.y, p.w, p.h, 1, C_GOLD);
#if BOARD_UI_TABLET || BOARD_ROUND
        text_fit(p.x + 6, p.w - 12, p.y + 6, l1, 2, C_GOLD);
        gfx_text(p.x + 10, p.y + 36, l2, 1, C_WHITE);
        gfx_text(p.x + 10, p.y + 56, l3, 1, C_DIM);
#else
        text_fit(p.x + 4, p.w - 8, p.y + 4, l1, 2, C_GOLD);
        gfx_text(p.x + 6, p.y + 22, l2, 1, C_WHITE);
        gfx_text(p.x + 6, p.y + 33, l3, 1, C_DIM);
#endif
    }
    draw_banner();
}

static void render_setup(int64_t now)
{
    (void)now;
    gfx_fill(C_BG);
    back_btn();
#if BOARD_UI_TABLET
    gfx_text(BACK_RC.x + BACK_RC.w + 12, 10, "PLACE AND TIME", 2, C_GOLD);
#elif BOARD_ROUND
    text_fit(RU(93), RU(280), RU(42), "PLACE AND TIME", 2, C_GOLD);
#else
    gfx_text(BOARD_BAR_SCREEN ? 76 : BACK_RC.x + BACK_RC.w + 8, 12, "PLACE AND TIME", 2, C_GOLD);
#endif
    double t = (clock_ok() ? (double)time(NULL) : (double)s_manual_t + (double)(now_s() - s_manual_at)) + s_tz * 3600.0;
    int64_t days = (int64_t)floor(t / 86400.0);
    int sec = (int)(t - (double)days * 86400.0);
    int y, m, d;
    civil(days, &y, &m, &d);
    char v[6][24];
    snprintf(v[0], sizeof(v[0]), "%s", s_city < N_CITIES ? CITIES[s_city].name : "CUSTOM");
    snprintf(v[1], sizeof(v[1]), "%.1f %c", (double)fabsf(s_lat), s_lat >= 0 ? 'N' : 'S');
    snprintf(v[2], sizeof(v[2]), "%.1f %c", (double)fabsf(s_lon), s_lon >= 0 ? 'E' : 'W');
    snprintf(v[3], sizeof(v[3]), "UTC%+d", (int)s_tz);
    snprintf(v[4], sizeof(v[4]), "%02d.%02d.%04d", d, m, y);
    snprintf(v[5], sizeof(v[5]), "%02d:%02d", sec / 3600, (sec / 60) % 60);
#if BOARD_BAR_SCREEN
    static const char *const L[6] = {"PLACE", "LATITUDE", "LONGITUDE", "TIME ZONE", "DATE", "HOUR"};
#else
    static const char *const L[6] = {"PLACE", "LAT", "LON", "ZONE", "DATE", "HOUR"};  // (the value on the same line)
#endif
    for (int i = 0; i < 6; i++) {
        rc_t c = set_cell(i);
        bool locked = i >= 4 && clock_ok();
        gfx_fill_rect(c.x + 2, c.y, c.w - 4, c.h, C_PANEL);
#if BOARD_BAR_SCREEN
        gfx_text(c.x + 8, c.y + 6, L[i], 1, C_DIM);
        gfx_text(c.x + 8, c.y + 20, v[i], 2, locked ? C_DIM : C_WHITE);
#elif BOARD_UI_TABLET
        gfx_text(c.x + 12, c.y + 9, L[i], 1, C_DIM);
        text_fit(c.x + 80, c.w - 88, c.y + 4, v[i], 2, locked ? C_DIM : C_WHITE);
#elif BOARD_ROUND
        gfx_text(c.x + 10, c.y + (c.h - 14) / 2, L[i], 1, C_DIM);
        text_fit(c.x + RU(66), c.w - RU(66) - RU(114), c.y + (c.h - 21) / 2, v[i], 2, locked ? C_DIM : C_WHITE);
#else
        gfx_text(c.x + 8, c.y + 5, L[i], 1, C_DIM);
        text_fit(c.x + 40, c.w - 44, c.y + 2, v[i], 2, locked ? C_DIM : C_WHITE);
#endif
        if (!locked) {
            button(set_btn(i, 0), "-", NULL, C_CYAN, false);
            button(set_btn(i, 1), "+", NULL, C_CYAN, false);
        }
    }
    const char *note = clock_ok() ? "THE CLOCK COMES FROM THE WIFI" : "NO CLOCK: SET THE DATE AND THE HOUR";
#if BOARD_BAR_SCREEN
    gfx_text(330, 14, note, 1, clock_ok() ? C_GREEN : C_GOLD);
#elif BOARD_UI_TABLET
    gfx_text(BACK_RC.x + BACK_RC.w + 12, 40, note, 1, clock_ok() ? C_GREEN : C_GOLD);
#elif BOARD_ROUND
    gfx_text_center(RU(76), clock_ok() ? "THE CLOCK COMES FROM WIFI" : "SET THE DATE AND THE HOUR", 1,
                    clock_ok() ? C_GREEN : C_GOLD);
    (void)note;
#else
    gfx_text(BACK_RC.x + BACK_RC.w + 8, 30, note, 1, clock_ok() ? C_GREEN : C_GOLD);
#endif
}

static void render_result(int64_t now)
{
    (void)now;
    cam_t c;
    float v[3];
    v_radec(1.5f, 10 * DEG, v);
    cam_atlas(&c, RC(0, 0, SW, SH), v, 0, 30);
    s_clip = c.vr;
    s_day = 0;
    draw_sky_bg(&c);
    draw_stars(&c, 4.0f, 0.6f);
    static const char *const NM[4] = {"LEARN", "NAME", "STARS", "FIND"};
    char t[40];
    snprintf(t, sizeof(t), "%s: %lu", NM[s_mode], (unsigned long)s_score);
#if BOARD_UI_TABLET
    const int ry0 = 60, ry1 = 120, ry2 = 165;
#elif BOARD_ROUND
    const int ry0 = RU(120), ry1 = RU(180), ry2 = RU(225);
#else
    const int ry0 = BOARD_BAR_SCREEN ? 14 : 40, ry1 = BOARD_BAR_SCREEN ? 46 : 80, ry2 = BOARD_BAR_SCREEN ? 72 : 110;
#endif
    gfx_text_center(ry0, t, 3, C_GOLD);
    if (s_mode != M_FIND) {
        snprintf(t, sizeof(t), "LEVEL %d", s_level);
        gfx_text_center(ry1, t, 2, C_WHITE);
    }
    snprintf(t, sizeof(t), s_new_best ? "NEW BEST!" : "BEST %lu", (unsigned long)s_best[s_mode]);
    gfx_text_center(ry2, t, 2, s_new_best ? C_GREEN : C_DIM);
    button(R_AGAIN_RC, "AGAIN", NULL, C_GREEN, false);
    button(R_MENU_RC, "MODES", NULL, C_DIM, false);
}

static void sky_render(int64_t now)
{
    if (!s_data_ok) {
        gfx_fill(C_BG);
        gfx_text_center(SH / 2, "NO STAR DATA", 2, C_RED);
        return;
    }
    switch (s_state) {
    case ST_TITLE:
        render_title(now);
        break;
    case ST_CARD:
        render_card(now);
        break;
    case ST_QUIZ:
        render_quiz(now);
        break;
    case ST_FIND:
        render_find(now);
        break;
    case ST_EXPLORE:
        render_explore(now);
        break;
    case ST_SETUP:
        render_setup(now);
        break;
    case ST_RESULT:
        render_result(now);
        break;
    }
    s_clip = RC(0, 0, SW, SH);
}

#include "tficon/tficon.h"
// the menu icon: the night sky: a moon, a constellation whose stars twinkle, and a shooting star
static void sky_icon_star(float sx, float sy, float r, int a)
{
    int t = (int)(r * 0.34f) > 1 ? (int)(r * 0.34f + 0.5f) : 1;
    tfd_rect((int)(sx - r), (int)(sy - t / 2.0f), (int)(2 * r), t, rgb(255, 255, 255), a);
    tfd_rect((int)(sx - t / 2.0f), (int)(sy - r), t, (int)(2 * r), rgb(255, 255, 255), a);
    tfd_disc(sx, sy, r * 0.32f, rgb(255, 255, 255), 256);
}
static void sky_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_box_t b = tfi_tile(x, y, s, tfi_hex(0x4a3fc0), tfi_hex(0x080a30));
    float u = s / 100.0f, ph = tfi_ph(2);
    // the moon: a disc, a disc of the sky over it
    tfd_glow(x + 70 * u, y + 30 * u, 17 * u, tfi_hex(0xfff3c4), 90);
    tfi_disc(x + 70 * u, y + 30 * u, 12 * u, tfi_hex(0xfff8e0), tfi_hex(0xffd56b), 256);
    tfi_disc(x + 76 * u, y + 25 * u, 10.5f * u, tfi_hex(0x3e36ae), tfi_hex(0x2f2a92), 256);
    // the constellation
    static const float ST[5][2] = {{22, 70}, {34, 52}, {50, 60}, {44, 80}, {62, 76}};
    for (int k = 0; k < 4; k++) {
        int j = k == 3 ? 2 : k + 1;
        tfi_line(x + ST[k][0] * u, y + ST[k][1] * u, x + ST[j][0] * u, y + ST[j][1] * u, 1.3f * u, false,
                 tfi_hex(0x9fb4ff), 120);
    }
    for (int k = 0; k < 5; k++) {
        float tw = 0.5f + 0.5f * fsin(ph * 6.2831853f * (k & 1 ? 1 : 2) + k * 1.7f);
        sky_icon_star(x + ST[k][0] * u, y + ST[k][1] * u, (2.6f + 2.6f * tw) * u, (int)(130 + 120 * tw));
    }
    // the shooting star: 0.2 .. 0.55 of the loop, from the upper left down to the right
    float q = (ph - 0.2f) / 0.35f;
    if (q > 0 && q < 1) {
        tfd_clip(b.x, b.y, b.x + b.w, b.y + b.h);
        float hx = x + (14 + q * 50) * u, hy = y + (14 + q * 26) * u;
        int a = (int)(255 * fsin(q * 3.14159f));
        float tr[6] = {hx - 20 * u, hy - 10.4f * u, hx + 0.6f * u, hy - 1.2f * u, hx - 0.6f * u, hy + 1.2f * u};
        tfi_convex(tr, 3, tfi_hex(0xbfe0ff), a * 2 / 3);
        tfd_disc(hx, hy, 2.4f * u, rgb(255, 255, 255), a);
        tfd_clip(x, y, x + s, y + s);
    }
    tfi_end();
}

const app_t app_sky = {
        .name = "SKY",
        .landscape = true,
        .needs = 0,
        .init = sky_init,
        .enter = sky_enter,
        .leave = sky_leave,
        .input = sky_input,
        .update = sky_update,
        .render = sky_render,
        .draw_icon = sky_icon,
        .round = APP_ROUND_HI,
        .awake = sky_awake,
};
