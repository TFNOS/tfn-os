// MINESWEEPER: the core (included by app_mines.c). Helpers, the eight gardens (a season, the colours, the size of the
// board, the count of the mines, the time for the stars), the texts.

// the frame (enter() takes gfx_w x gfx_h): 640 x 172 on the 3.49, 320 x 240 on the CoreS3, the AMOLED, the Watcher
static int SW = 640, SH = 172, CX = 320;
#define NARROW (SW < 400)
#define RND    gfx_round()  // the round frame (466 x 466 on the 1.75C, 412 on the Watcher)
#if BOARD_UI_TABLET
#define BIG (SW >= 600 && SH >= 340)  // the Tab5 (640 x 360): more rows of tiles, the HUD at the left
#else
#define BIG 0
#endif

#ifdef ESP_PLATFORM
static void *ms_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
}
#else
static void *ms_alloc(size_t n)
{
    return malloc(n);
}
#endif
static int64_t ms_us(void)
{
    return esp_timer_get_time();
}
static inline int clampi(int v, int a, int b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}
static inline float lerpf(float a, float b, float t)
{
    return a + (b - a) * t;
}
static inline uint32_t hash2(int a, int b)
{
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static uint32_t s_rng = 77;
static inline uint32_t rnd32(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static inline float rndf(void)
{
    return (rnd32() & 0xFFFFFF) / 16777216.0f;
}
static inline int rndi(int a, int b)
{
    return a + (int)(rnd32() % (uint32_t)(b - a + 1));
}
static inline uint16_t rgbc(int r, int g, int b)
{
    return rgb((uint8_t)clampi(r, 0, 255), (uint8_t)clampi(g, 0, 255), (uint8_t)clampi(b, 0, 255));
}
static inline uint16_t rgb3(const uint8_t *c)
{
    return rgb(c[0], c[1], c[2]);
}
static inline uint16_t rgb3k(const uint8_t *c, int k)
{
    return rgbc(c[0] * k / 100, c[1] * k / 100, c[2] * k / 100);
}
static inline void unrgb(uint16_t p, int *r, int *g, int *b)
{
    uint16_t c = (uint16_t)((p >> 8) | (p << 8));
    *r = ((c >> 11) & 31) << 3;
    *g = ((c >> 5) & 63) << 2;
    *b = (c & 31) << 3;
}
static inline uint16_t mix565(uint16_t a, uint16_t b, int k)
{
    uint32_t x = (uint16_t)((a >> 8) | (a << 8)), y = (uint16_t)((b >> 8) | (b << 8));
    x = (x | (x << 16)) & 0x07E0F81Fu;
    y = (y | (y << 16)) & 0x07E0F81Fu;
    uint32_t r = ((x * (uint32_t)(32 - k) + y * (uint32_t)k) >> 5) & 0x07E0F81Fu;
    uint16_t o = (uint16_t)(r | (r >> 16));
    return (uint16_t)((o >> 8) | (o << 8));
}
static inline uint16_t add565(uint16_t a, int r, int g, int b)
{
    int ar, ag, ab;
    unrgb(a, &ar, &ag, &ab);
    return rgbc(ar + r, ag + g, ab + b);
}
static char s_err[80];

// ================================================================ the gardens
// The colours of a garden: the sky (top, bottom), the hills (far, near), the tile (main, light, dark, the front face),
// the open earth (main, dark, light), two colours of the small things (flowers, shells, leaves ...), the frame.
enum {
    AMB_PETAL,
    AMB_SUN,
    AMB_SEA,
    AMB_LEAF,
    AMB_SNOW,
    AMB_SAND,
    AMB_FIREFLY,
    AMB_EMBER
};
typedef struct {
    const char *name[2];
    uint8_t sky0[3], sky1[3], hill0[3], hill1[3];
    uint8_t top[3], topl[3], topd[3], face[3];
    uint8_t dirt[3], dirtd[3], dirtl[3];
    uint8_t deco0[3], deco1[3], frame[3];
    uint8_t amb, bright;  // the moving things in the air; bright numbers (on dark earth)
    uint8_t cols, rows, mines;
    uint16_t par;  // the time for 2 stars (s); 3 stars at 60% of it
} garden_t;
#define NGARD 8
static const garden_t GARD[NGARD] = {
        {{"SPRING MEADOW", "אחו האביב"},
         {104, 184, 255},
         {206, 236, 255},
         {128, 196, 128},
         {84, 160, 72},
         {96, 188, 72},
         {146, 220, 96},
         {58, 136, 52},
         {122, 82, 46},
         {196, 156, 104},
         {156, 116, 74},
         {226, 192, 140},
         {255, 120, 168},
         {255, 236, 110},
         {150, 100, 58},
         AMB_PETAL,
         0,
         10,
         5,
         7,
         45},
        {{"SUNFLOWER HILL", "גבעת החמניות"},
         {255, 186, 112},
         {255, 232, 178},
         {206, 170, 86},
         {150, 158, 58},
         {150, 186, 60},
         {192, 220, 92},
         {100, 138, 40},
         {132, 92, 42},
         {204, 162, 102},
         {164, 122, 70},
         {232, 196, 140},
         {255, 208, 40},
         {110, 70, 30},
         {146, 96, 50},
         AMB_SUN,
         0,
         12,
         6,
         11,
         75},
        {{"SEASHELL BEACH", "חוף הצדפים"},
         {86, 196, 255},
         {196, 240, 255},
         {60, 160, 210},
         {236, 212, 150},
         {238, 214, 152},
         {255, 236, 186},
         {206, 176, 118},
         {186, 146, 88},
         {204, 172, 124},
         {170, 140, 96},
         {228, 204, 156},
         {255, 164, 170},
         {255, 138, 76},
         {120, 160, 190},
         AMB_SEA,
         0,
         14,
         6,
         14,
         105},
        {{"AUTUMN WOODS", "יער הסתיו"},
         {238, 166, 118},
         {255, 222, 172},
         {196, 112, 62},
         {146, 92, 52},
         {150, 150, 62},
         {192, 182, 84},
         {108, 108, 42},
         {112, 72, 40},
         {170, 120, 78},
         {134, 92, 58},
         {200, 154, 106},
         {232, 110, 40},
         {204, 58, 40},
         {120, 74, 40},
         AMB_LEAF,
         0,
         15,
         7,
         17,
         135},
        {{"SNOW VILLAGE", "כפר השלג"},
         {150, 178, 230},
         {222, 230, 250},
         {226, 236, 255},
         {198, 214, 242},
         {238, 244, 255},
         {255, 255, 255},
         {184, 200, 234},
         {140, 162, 206},
         {196, 186, 180},
         {162, 152, 148},
         {222, 214, 208},
         {164, 220, 255},
         {110, 160, 226},
         {120, 130, 160},
         AMB_SNOW,
         0,
         16,
         7,
         20,
         165},
        {{"DESERT RUINS", "חורבות המדבר"},
         {255, 188, 108},
         {255, 230, 168},
         {228, 168, 92},
         {208, 140, 70},
         {220, 160, 90},
         {246, 192, 122},
         {178, 120, 62},
         {158, 100, 52},
         {236, 220, 186},
         {204, 186, 150},
         {252, 240, 212},
         {92, 160, 82},
         {242, 236, 220},
         {170, 110, 60},
         AMB_SAND,
         0,
         17,
         7,
         22,
         195},
        {{"NIGHT GARDEN", "גן הלילה"},
         {10, 14, 48},
         {40, 42, 100},
         {22, 40, 64},
         {16, 50, 42},
         {40, 92, 70},
         {62, 132, 92},
         {24, 60, 48},
         {38, 38, 52},
         {62, 56, 74},
         {42, 38, 52},
         {88, 82, 98},
         {120, 240, 255},
         {255, 140, 240},
         {70, 60, 90},
         AMB_FIREFLY,
         1,
         18,
         7,
         24,
         225},
        {{"FIRE MOUNTAIN", "הר האש"},
         {58, 20, 30},
         {150, 50, 40},
         {40, 20, 26},
         {70, 32, 26},
         {72, 62, 68},
         {104, 92, 98},
         {46, 40, 46},
         {40, 30, 30},
         {92, 82, 80},
         {62, 56, 56},
         {124, 112, 106},
         {255, 120, 30},
         {255, 204, 60},
         {90, 50, 40},
         AMB_EMBER,
         1,
         19,
         7,
         27,
         255},
};

// the boards of the narrow frames (columns, rows, mines): at most 9 x 9 tiles, about the mine density of the 3.49
static const uint8_t NAR[NGARD][3] = {{7, 6, 6},  {8, 7, 9},  {9, 7, 10}, {9, 8, 12},
                                      {9, 8, 13}, {9, 9, 15}, {9, 9, 16}, {9, 9, 17}};
#if BOARD_UI_TABLET
// the boards of the Tab5 (columns, rows, mines): up to 20 x 14 tiles of 24 px (4 mm), a little less dense than the 3.49
// (a big board that needs no guess is rarer)
static const uint8_t T5B[NGARD][3] = {{12, 8, 12},  {14, 9, 17},  {15, 10, 22}, {16, 11, 26},
                                      {17, 12, 32}, {18, 13, 38}, {19, 13, 42}, {20, 14, 50}};
static int g_cols(int g)
{
    return BIG ? T5B[g][0] : NARROW ? NAR[g][0] : GARD[g].cols;
}
static int g_rows(int g)
{
    return BIG ? T5B[g][1] : NARROW ? NAR[g][1] : GARD[g].rows;
}
static int g_mines(int g)
{
    return BIG ? T5B[g][2] : NARROW ? NAR[g][2] : GARD[g].mines;
}
static float g_par(int g)
{
    return BIG      ? GARD[g].par * (float)(T5B[g][0] * T5B[g][1]) / (GARD[g].cols * GARD[g].rows)
           : NARROW ? GARD[g].par * (float)(NAR[g][0] * NAR[g][1]) / (GARD[g].cols * GARD[g].rows)
                    : GARD[g].par;
}
#else
// the round frame: a round board of n x n tiles (only the tiles in the circle; see board_clear()), the mine density of
// the narrow boards
static const uint8_t RNDN[NGARD] = {10, 10, 11, 11, 12, 12, 12, 12};
// (at most 62 % of the width of the circle: the HUD and the buttons stay round the board)
static int rnd_n(int g)
{
    int m = SW * 62 / 100 / 24;
    return RNDN[g] < m ? RNDN[g] : m;
}
static int g_cols(int g)
{
    return RND ? rnd_n(g) : NARROW ? NAR[g][0] : GARD[g].cols;
}
static int g_rows(int g)
{
    return RND ? rnd_n(g) : NARROW ? NAR[g][1] : GARD[g].rows;
}
static int g_mines(int g)
{
    return NARROW ? NAR[g][2] : GARD[g].mines;
}
// the time for 2 stars: less on a smaller board
static float g_par(int g)
{
    return NARROW ? GARD[g].par * (float)(NAR[g][0] * NAR[g][1]) / (GARD[g].cols * GARD[g].rows) : GARD[g].par;
}
#endif

// ================================================================ the texts
static int s_lang = 1;
enum {
    T_TITLE,
    T_PLAY,
    T_GARDEN,
    T_CLEAR,
    T_BOOM,
    T_AGAIN,
    T_NEXT,
    T_ALL,
    T_HINT,
    T_BEST,
    T_PHEW,
    T_SAFE,
    T_COINS,
    T_LOCKED,
    T_N
};
static const char *const TXT[T_N][2] = {
        {"MINESWEEPER", "שולה המוקשים"},
        {"TAP TO PLAY", "געו כדי לשחק"},
        {"GARDEN", "גן"},
        {"GARDEN CLEAR!", "הגן נקי!"},
        {"BOOM!", "בום!"},
        {"TAP TO TRY AGAIN", "געו כדי לנסות שוב"},
        {"TAP FOR THE NEXT GARDEN", "געו לגן הבא"},
        {"ALL EIGHT GARDENS!", "כל שמונת הגנים!"},
        {"TAP A TILE TO AIM - THEN DIG OR FLAG", "געו באריח כדי לכוון - ואז חפרו או סמנו"},
        {"BEST", "שיא"},
        {"PHEW!", "פיו!"},
        {"SAFE HERE!", "בטוח כאן!"},
        {"COINS", "מטבעות"},
        {"LOCKED", "נעול"},
};
static const char *TX(int i)
{
    return TXT[i][s_lang ? 1 : 0];
}
