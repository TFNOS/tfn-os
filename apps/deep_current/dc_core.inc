// DEEP CURRENT: the core (included by app_deep.c). The helpers, the traits (the parts of a body), the species of the
// sea, the four zones and the texts in English and Hebrew.

#define SW 640  // the layout of the 3.49 (landscape)
#define SH 172
#define WW 2048.0f  // the width of a zone (the world wraps round in x)

#ifdef ESP_PLATFORM
__attribute__((unused)) static void *dc_alloc(size_t n)
{
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}
#else
__attribute__((unused)) static void *dc_alloc(size_t n)
{
    return calloc(1, n);
}
#endif
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
static inline float smooth01(float t)
{
    t = clampf(t, 0, 1);
    return t * t * (3 - 2 * t);
}
static inline float ease_out(float t)
{
    t = clampf(t, 0, 1);
    return 1 - (1 - t) * (1 - t);
}
static inline float sqr(float v)
{
    return v * v;
}
// the shortest x step from a to b in the world that wraps
static inline float wrapd(float d)
{
    while (d > WW * 0.5f) {
        d -= WW;
    }
    while (d < -WW * 0.5f) {
        d += WW;
    }
    return d;
}
static inline float wrapx(float x)
{
    while (x >= WW) {
        x -= WW;
    }
    while (x < 0) {
        x += WW;
    }
    return x;
}
static inline float angd(float d)
{
    while (d > 3.14159265f) {
        d -= 6.2831853f;
    }
    while (d < -3.14159265f) {
        d += 6.2831853f;
    }
    return d;
}
static uint32_t s_rng = 0x1234567u;
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
static inline float rnds(void)
{
    return rndf() * 2 - 1;
}
static inline int rndi(int a, int b)
{
    return a + (int)(rnd32() % (uint32_t)(b - a + 1));
}
static inline uint32_t hash2(int a, int b)
{
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
static float vnoise1(float x, int seed)
{
    int i = (int)floorf(x);
    float f = x - i, u = f * f * (3 - 2 * f);
    float a = (hash2(i, seed) & 0xFFFF) / 65535.0f, b = (hash2(i + 1, seed) & 0xFFFF) / 65535.0f;
    return lerpf(a, b, u) * 2 - 1;
}
static float fbm1(float x, int seed)
{
    return 0.5f * vnoise1(x, seed) + 0.25f * vnoise1(x * 2.03f + 17.3f, seed) +
           0.125f * vnoise1(x * 4.1f + 34.6f, seed);
}

// ================================================================ the traits: the parts of a body
// Each prey carries the light of one trait. The creature that swallows it can grow that part at its next growth.
enum {
    T_NONE,
    T_TAIL,
    T_FIN,
    T_JAWS,
    T_SPINE,
    T_SHELL,
    T_STING,
    T_LURE,
    T_INK,
    T_JET,
    T_GLASS,
    T_N
};
typedef struct {
    const char *name[2];
    const char *does[2];
    uint8_t c[3];  // the colour of its light
    uint8_t head;  // 1: an organ of the head (it is never bitten off)
    uint8_t maxn;  // the most of it on one body (0: no limit)
} trait_t;
static const trait_t TRAIT[T_N] = {
        {{"FOOD", "מזון"}, {"", ""}, {214, 226, 240}, 0, 0},
        {{"TAIL", "זנב"}, {"FAST, WIDE TURNS", "מהיר, פניות רחבות"}, {79, 216, 255}, 0, 0},
        {{"FINS", "סנפירים"}, {"SHARP TURNS", "פניות חדות"}, {109, 255, 216}, 0, 0},
        {{"JAWS", "לסתות"}, {"BIGGER BITES", "נגיסות גדולות"}, {244, 248, 255}, 1, 3},
        {{"SPINES", "קוצים"}, {"HUNTERS LET GO", "טורפים מרפים"}, {183, 124, 255}, 0, 0},
        {{"SHELL", "שריון"}, {"STOPS BITES, HEAVY", "עוצר נשיכות, כבד"}, {255, 196, 90}, 0, 0},
        {{"STINGERS", "עוקצים"}, {"STUN ON TOUCH", "משתקים במגע"}, {255, 95, 200}, 0, 0},
        {{"LURE", "פיתיון"}, {"LIGHT THAT DRAWS PREY", "אור שמושך טרף"}, {157, 255, 106}, 1, 1},
        {{"INK", "דיו"}, {"BOOT: INK CLOUD", "BOOT: ענן דיו"}, {255, 75, 110}, 0, 2},
        {{"JET", "סילון"}, {"STRONG DASH", "זינוק חזק"}, {255, 148, 64}, 0, 0},
        {{"GLASS SKIN", "עור שקוף"}, {"PREY SEE YOU LATE", "הטרף מבחין מאוחר"}, {200, 224, 255}, 0, 0},
};
// the traits that a new player has; the other four unlock for the next dives (see unlock_check)
#define UNL_BASE ((1u << T_TAIL) | (1u << T_FIN) | (1u << T_JAWS) | (1u << T_SPINE) | (1u << T_SHELL) | (1u << T_STING))

// ================================================================ the species
enum {
    B_SCHOOL,
    B_DRIFT,
    B_FLEE,
    B_DART,
    B_SLOW,
    B_HAZARD,
    B_PRED,
    B_LURK
};
enum {
    SP_SPRAT,
    SP_LANTERN,
    SP_EEL,
    SP_URCHIN,
    SP_JELLY,
    SP_ANGEL,
    SP_OSTRA,
    SP_SQUID,
    SP_GLASSQ,
    SP_FANG,
    SP_COMB,
    SP_ALARVA,
    SP_VAMP,
    SP_ISOPOD,
    SP_BIGJELLY,
    SP_MACKEREL,
    SP_DRAGON,
    SP_ANGLER,
    SP_GULPER,
    SP_N
};
typedef struct {
    const char *name[2];
    uint8_t trait, beh;
    float sz;     // the size: the radius of the body (world px); the creature swallows it
                  // when its mouth is this big
    float len;    // the length (a hunter: the size that the creature must pass)
    float speed;  // px/s
    float food;
    float sense;           // it sees the creature at this distance (px)
    float hp;              // the bites that it takes (a big thing)
    uint8_t c1[3], c2[3];  // the body, the light
} spec_t;
static const spec_t SPEC[SP_N] = {
        [SP_SPRAT] = {{"SPRAT", "שפרוט"}, T_FIN, B_SCHOOL, 2.2f, 8, 70, 1.0f, 60, 1, {150, 190, 215}, {230, 250, 255}},
        [SP_LANTERN] =
                {{"LANTERNFISH", "דג פנס"}, T_FIN, B_SCHOOL, 2.5f, 9, 64, 1.0f, 56, 1, {70, 100, 130}, {120, 220, 255}},
        [SP_EEL] = {{"EEL LARVA", "זחל צלופח"},
                    T_TAIL,
                    B_FLEE,
                    3.0f,
                    18,
                    34,
                    2.0f,
                    40,
                    1,
                    {170, 230, 255},
                    {120, 230, 255}},
        [SP_URCHIN] = {{"URCHIN LARVA", "זחל קיפוד"},
                       T_SPINE,
                       B_DRIFT,
                       2.0f,
                       6,
                       8,
                       1.0f,
                       0,
                       1,
                       {200, 170, 255},
                       {183, 124, 255}},
        [SP_JELLY] =
                {{"JELLY", "מדוזה קטנה"}, T_STING, B_DRIFT, 2.8f, 7, 10, 1.5f, 0, 1, {255, 150, 220}, {255, 95, 200}},
        [SP_ANGEL] =
                {{"SEA ANGEL", "מלאך ים"}, T_FIN, B_FLEE, 2.4f, 7, 46, 1.0f, 46, 1, {255, 190, 140}, {109, 255, 216}},
        [SP_OSTRA] = {{"SEED SHRIMP", "סרטן זרע"},
                      T_SHELL,
                      B_SLOW,
                      2.4f,
                      6,
                      18,
                      1.0f,
                      30,
                      1,
                      {255, 210, 140},
                      {90, 170, 255}},
        [SP_SQUID] = {{"SQUID", "דיונון"}, T_JET, B_DART, 3.6f, 14, 60, 2.0f, 60, 1, {255, 170, 120}, {255, 148, 64}},
        [SP_GLASSQ] = {{"GLASS SQUID", "דיונון זכוכית"},
                       T_GLASS,
                       B_FLEE,
                       3.4f,
                       13,
                       40,
                       2.0f,
                       44,
                       1,
                       {200, 224, 255},
                       {200, 224, 255}},
        [SP_FANG] = {{"FANGTOOTH", "דג ניבים"},
                     T_JAWS,
                     B_FLEE,
                     4.0f,
                     14,
                     48,
                     2.5f,
                     40,
                     1,
                     {120, 110, 130},
                     {244, 248, 255}},
        [SP_COMB] = {{"COMB JELLY", "מסרקנית"},
                     T_STING,
                     B_DRIFT,
                     3.8f,
                     10,
                     12,
                     2.0f,
                     0,
                     1,
                     {170, 220, 255},
                     {255, 95, 200}},
        [SP_ALARVA] = {{"ANGLER LARVA", "זחל חכאי"},
                       T_LURE,
                       B_FLEE,
                       3.2f,
                       9,
                       30,
                       2.0f,
                       34,
                       1,
                       {130, 120, 110},
                       {157, 255, 106}},
        [SP_VAMP] = {{"VAMPIRE SQUID", "דיונון ערפד"},
                     T_INK,
                     B_DART,
                     5.0f,
                     16,
                     56,
                     3.0f,
                     70,
                     1,
                     {150, 40, 60},
                     {255, 75, 110}},
        [SP_ISOPOD] =
                {{"ISOPOD", "שווה רגליים"}, T_SHELL, B_SLOW, 6.0f, 16, 14, 3.5f, 0, 2, {200, 180, 150}, {255, 196, 90}},
        [SP_BIGJELLY] =
                {{"JELLYFISH", "המדוזה"}, T_STING, B_HAZARD, 14.0f, 40, 6, 6.0f, 0, 3, {255, 150, 220}, {255, 95, 200}},
        [SP_MACKEREL] =
                {{"MACKEREL", "המקרל"}, T_JAWS, B_PRED, 9.0f, 40, 104, 6.0f, 150, 3, {160, 190, 210}, {240, 250, 255}},
        [SP_DRAGON] =
                {{"DRAGONFISH", "דג הדרקון"}, T_JAWS, B_PRED, 10.0f, 58, 96, 8.0f, 150, 3, {40, 36, 52}, {255, 60, 70}},
        [SP_ANGLER] = {{"ANGLERFISH", "דג החכאי"},
                       T_LURE,
                       B_LURK,
                       13.0f,
                       48,
                       150,
                       9.0f,
                       80,
                       4,
                       {48, 40, 40},
                       {157, 255, 106}},
        [SP_GULPER] = {{"GULPER EEL", "הצלופח הבולען"},
                       T_JAWS,
                       B_PRED,
                       14.0f,
                       96,
                       84,
                       12.0f,
                       170,
                       4,
                       {34, 30, 44},
                       {255, 120, 200}},
};
static inline bool is_hunter(int sp)
{
    return SPEC[sp].beh == B_PRED || SPEC[sp].beh == B_LURK;
}

// ================================================================ the zones
enum {
    FAR_WHALE = 1,
    FAR_FLOOR = 2,
    FAR_SPARKS = 4,
    FAR_VENTS = 8,
    FAR_SURFACE = 16
};
typedef struct {
    uint8_t sp, n;
} spawn_t;
typedef struct {
    const char *name[2];
    int m0, m1;                   // the depth (m) at the top and at the bottom
    float h;                      // the height (world px)
    uint8_t w0[3], w1[3], w2[3];  // the water at the top, the middle and the bottom of the zone
    float rays;                   // the light rays from above (0 = none)
    float dark;                   // 0 = lit water, 1 = only the light of the life
    float flow;                   // the current (px/s, + = to the right)
    uint8_t far;                  // FAR_*
    uint8_t goal;                 // the growth count (in all) that opens the deep current
    uint8_t music;
    spawn_t sp[10];
} zone_t;
#define NZONE 4
static const zone_t ZONE[NZONE] = {
        {{"THE SHALLOWS", "המים הרדודים"},
         0,
         200,
         360,
         {44, 140, 170},
         {18, 86, 120},
         {9, 50, 84},
         1.0f,
         0.0f,
         6,
         FAR_WHALE | FAR_SURFACE,
         3,
         1,
         {{SP_SPRAT, 70},
          {SP_EEL, 4},
          {SP_URCHIN, 6},
          {SP_JELLY, 4},
          {SP_ANGEL, 6},
          {SP_BIGJELLY, 1},
          {SP_MACKEREL, 1}}},
        {{"THE TWILIGHT", "אזור הדמדומים"},
         200,
         1000,
         420,
         {12, 64, 94},
         {7, 38, 64},
         {3, 16, 34},
         0.55f,
         0.25f,
         -8,
         FAR_WHALE | FAR_FLOOR,
         6,
         2,
         {{SP_LANTERN, 80},
          {SP_EEL, 3},
          {SP_OSTRA, 7},
          {SP_SQUID, 4},
          {SP_GLASSQ, 3},
          {SP_FANG, 3},
          {SP_JELLY, 2},
          {SP_BIGJELLY, 1},
          {SP_DRAGON, 1}}},
        {{"THE MIDNIGHT", "אזור חצות"},
         1000,
         4000,
         470,
         {7, 24, 42},
         {3, 13, 27},
         {1, 6, 14},
         0,
         0.72f,
         5,
         FAR_FLOOR | FAR_SPARKS,
         9,
         3,
         {{SP_LANTERN, 50},
          {SP_COMB, 5},
          {SP_ALARVA, 4},
          {SP_VAMP, 2},
          {SP_OSTRA, 6},
          {SP_FANG, 4},
          {SP_EEL, 2},
          {SP_ANGLER, 1},
          {SP_GULPER, 1}}},
        {{"THE ABYSS", "התהום"},
         4000,
         6000,
         500,
         {6, 15, 24},
         {3, 9, 16},
         {5, 7, 9},
         0,
         0.85f,
         -4,
         FAR_FLOOR | FAR_SPARKS | FAR_VENTS,
         12,
         4,
         {{SP_ISOPOD, 4},
          {SP_COMB, 3},
          {SP_VAMP, 3},
          {SP_ALARVA, 3},
          {SP_OSTRA, 5},
          {SP_URCHIN, 3},
          {SP_FANG, 3},
          {SP_ANGLER, 2},
          {SP_GULPER, 1}}},
};
#define END_M 6000  // the deep at the end of the run

// ================================================================ the texts
static int s_lang = 1;  // 0 English, 1 Hebrew (the default)
enum {
    X_TITLE,
    X_TAP,
    X_BEST,
    X_DEPTH,
    X_M,
    X_GROWTH,
    X_OPEN,
    X_CURRENT,
    X_NEW,
    X_WILD,
    X_EATEN,
    X_SIZE,
    X_TIME,
    X_NEWBEST,
    X_AGAIN,
    X_DEEP,
    X_CAUGHT,
    X_UNLOCK,
    X_H_TWI,
    X_H_MID,
    X_H_ABY,
    X_H_EAT,
    X_RIVAL,
    X_DIVES,
    X_CHOOSE,
    X_LOCK,
    X_N
};
static const char *const TXT[X_N][2] = {
        {"DEEP CURRENT", "זרם עמוק"},
        {"TAP TO DIVE", "געו כדי לצלול"},
        {"BEST", "שיא"},
        {"DEPTH", "עומק"},
        {"M", "מ׳"},
        {"GROWTH", "גדילה"},
        {"THE CURRENT IS OPEN", "הזרם נפתח"},
        {"THE CURRENT", "הזרם"},
        {"NEW", "חדש"},
        {"WILD", "מוטציה"},
        {"EATEN", "נאכלו"},
        {"SIZE", "גודל"},
        {"TIME", "זמן"},
        {"NEW BEST", "שיא חדש"},
        {"TAP TO DIVE AGAIN", "געו כדי לצלול שוב"},
        {"YOU REACHED THE DEEP", "הגעתם אל המעמקים"},
        {"CAUGHT BY", "נטרפתם:"},
        {"UNLOCKED", "נפתח"},
        {"REACH THE TWILIGHT", "הגיעו לאזור הדמדומים"},
        {"REACH THE MIDNIGHT", "הגיעו לאזור חצות"},
        {"REACH THE ABYSS", "הגיעו לתהום"},
        {"EAT 40 IN ONE DIVE", "אכלו 40 בצלילה אחת"},
        {"THE RIVAL", "היריב"},
        {"DIVES", "צלילות"},
        {"CHOOSE", "בחרו"},
        {"LOCKED", "נעול"},
};
static const char *TX(int i)
{
    return TXT[i][s_lang ? 1 : 0];
}
static const char *TNAME(int t)
{
    return TRAIT[t].name[s_lang ? 1 : 0];
}
// a number with commas: 12,345
static void fmt_k(char *b, int n, int v)
{
    if (v < 1000) {
        snprintf(b, (size_t)n, "%d", v);
    } else if (v < 1000000) {
        snprintf(b, (size_t)n, "%d,%03d", v / 1000, v % 1000);
    } else {
        snprintf(b, (size_t)n, "%d,%03d,%03d", v / 1000000, v / 1000 % 1000, v % 1000);
    }
}
