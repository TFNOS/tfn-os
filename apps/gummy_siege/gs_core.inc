// GUMMY SIEGE: the core (included by app_gummy.c). The constants, small helpers, the kinds of the things of a fort,
// the levels (the text file levels.txt of the data pack: tools/levels.py makes it) and the saves.

// the frame: 640 x 172 on the 3.49, 320 x 240 on the other boards (the BOX; the Watcher shows it in its circle)
#define SW     gfx_w
#define SH     gfx_h
#define GS_BOX (!BOARD_BAR_SCREEN)
// the Tab5 (640 x 360): the BOX layout; the table lower (more room for the forts over it), the menus bigger
#if BOARD_UI_TABLET
#define GS_TAB5 1
#else
#define GS_TAB5 0
#endif
// THE ROUND LAYOUT: the round frame of the panel (app_t.round = APP_ROUND_HI: 466 x 466 on the AMOLED-1.75C, 412 x 412 on the
// Watcher). Only the circle of diameter SW shows. The sling stands at 0.30 SW, so the finger has the room of the
// lower left part of the circle for the pull. All sizes come from SW (GS_RS: the px of this frame against the px of
// the BOX, for the same size in mm).
#if BOARD_ROUND
#define GS_RND gfx_round()
#else
#define GS_RND 0
#endif
#define GS_RS   (SW / 345.0f)
#define RND_SLX 0.30f  // the sling on the screen in the round layout (a part of SW)
#define RND_XR  0.94f  // the right end of the view of the aim (a part of SW; the circle is wide at the table)
#define PPM     40.0f  // px for one metre at the zoom 1
// the lines of the frame (frame_geom): the horizon, the play line (the table: y = 0 of the world), the smallest zoom
static float s_hzy = 118, s_tsy = 148, s_zmin = 0.62f,
             s_vpy = 74;  // s_vpy: the vanishing line (over the far end of the table)
static void frame_geom(void)
{
    if (GS_RND) {
        // (the play line over the middle: the circle is wide there, and the room under it is for the pull)
        s_tsy = SH * 0.62f;
        s_hzy = s_tsy - 44 * GS_RS;
        s_vpy = s_tsy - 94 * GS_RS;
        s_zmin = 0.50f * GS_RS;
    } else if (GS_TAB5) {
        s_hzy = 196;
        s_tsy = 266;
        s_zmin = 0.75f;
        s_vpy = 128;
    } else if (SH >= 200) {
        s_hzy = 146;
        s_tsy = 190;
        s_zmin = 0.60f;
        s_vpy = 96;
    } else {
        s_hzy = 118;
        s_tsy = 148;
        s_zmin = 0.62f;
        s_vpy = 74;
    }
}
#define STEP_DT (1.0f / 60.0f)  // one physics step

// the data of the game is in the app file (gs_data.c: the levels, the pictures, the effects): no data pack
#ifdef ESP_PLATFORM
static void *gs_alloc(size_t n)
{
    return heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
}
#else
static void *gs_alloc(size_t n)
{
    return malloc(n);
}
#endif
static int64_t gs_us(void)
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
static inline float absf(float v)
{
    return v < 0 ? -v : v;
}
static inline float smooth01(float t)
{
    t = clampf(t, 0, 1);
    return t * t * (3 - 2 * t);
}
static inline float minf(float a, float b)
{
    return a < b ? a : b;
}
static inline float maxf(float a, float b)
{
    return a > b ? a : b;
}
static inline int mini(int a, int b)
{
    return a < b ? a : b;
}
static inline int maxi(int a, int b)
{
    return a > b ? a : b;
}
static inline uint32_t hash2(int a, int b)
{
    uint32_t h = (uint32_t)a * 374761393u + (uint32_t)b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}
// the random numbers of the looks (the effects): not the physics (the physics has its own seed)
static uint32_t s_rng = 2026;
static inline uint32_t rnd32(void)
{
    s_rng ^= s_rng << 13;
    s_rng ^= s_rng >> 17;
    s_rng ^= s_rng << 5;
    return s_rng;
}
static inline float rndf(void)
{
    return (float)(rnd32() & 0xFFFFFF) / 16777216.0f;
}
static inline float rnds(void)
{
    return rndf() * 2.0f - 1.0f;
}
static inline uint16_t rgbc(int r, int g, int b)
{
    return rgb((uint8_t)clampi(r, 0, 255), (uint8_t)clampi(g, 0, 255), (uint8_t)clampi(b, 0, 255));
}
static inline void unrgb(uint16_t p, int *r, int *g, int *b)
{
    uint16_t c = (uint16_t)((p >> 8) | (p << 8));
    *r = ((c >> 11) & 31) << 3;
    *g = ((c >> 5) & 63) << 2;
    *b = (c & 31) << 3;
}
static char s_err[96];

// ================================================================ the kinds of the things
// A thing of a level is one line of levels.txt. Each body kind has its own local shape (the origin is the middle of
// its shape), its material, its look and its points.
enum {
    OK_CHIP,
    OK_BIGCHIP,
    OK_STICK,
    OK_CRACKER,
    OK_JAR,
    OK_CUP,
    OK_CAN,
    OK_GIFT,  // the parts of a fort
    OK_KNIGHT,
    OK_CAPTAIN,
    OK_KING,     // the enemies
    OK_BALLOON,  // floats; a rope holds it
    OK_BLOCK,    // a static block (a shelf, a box)
    OK_DIP,      // a bowl of cheese dip (a zone)
    OK_N
};
static const char *const OK_NAME[OK_N] = {"chip", "bigchip", "stick",   "cracker", "jar",     "cup",   "can",
                                          "gift", "knight",  "captain", "king",    "balloon", "block", "dip"};
static inline bool ok_enemy(int k)
{
    return k >= OK_KNIGHT && k <= OK_KING;
}
// the chip triangle: the side of its base and its height (m)
#define CHIP_S 0.52f
#define CHIP_H 0.45f
// the enemies: a chip triangle with a helmet of cheese; HP = the damage that knocks one out (m/s of hits, see
// world.inc); the points
static const struct {
    float s, h, hp;
    int pts;
} ENEMY[3] = {
        {0.40f, 0.40f, 2.4f, 5000},   // a NACHO KNIGHT
        {0.56f, 0.56f, 4.6f, 8000},   // a CAPTAIN (a jalapeno plume)
        {0.84f, 0.84f, 8.0f, 15000},  // the KING (a crown of cheese)
};

typedef struct {
    uint8_t kind;   // OK_*
    uint8_t var;    // a variant: the colour of a gift or a balloon, the look of a block
    int16_t tie;    // a balloon: the thing that its rope holds (-1 = a point on the table: tx, ty)
    float x, y, a;  // the middle (m) and the angle (rad); a block: x, y = the low corner
    float w, h;     // the sizes (a stick: w = the length; a cracker, a gift, a block: w x h; a dip: w, h)
    float tx, ty;   // a balloon with tie -1: the point of the rope on the table
} thing_t;

#define NLEV_MAX  40
#define NBEAR_MAX 8
typedef struct {
    char name[2][48];    // English, Hebrew (UTF-8)
    uint8_t world, num;  // 0..2, 1..10
    uint8_t bears[NBEAR_MAX];
    uint8_t nbear;
    uint32_t s2, s3;  // the score for 2 and for 3 stars
    float edge;       // the end of the table (m); 0 = the table goes on
    int first, n;     // the things: s_thing[first .. first + n)
} level_t;
static level_t s_lev[NLEV_MAX];
static int s_nlev;
static thing_t *s_thing;
static int s_nthing;

// ================================================================ the level file
// levels.txt: one level is a block of lines:
//   L <world 1..3> <number> <English name>|<Hebrew name>
//   B <the bears: R G Y O C, in the order of the shots>
//   S <the score for 2 stars> <the score for 3 stars>
//   T <the end of the table in m, 0 = none>
//   <thing> <values ...>          (see tools/levels.py: x y angle in degrees, then the sizes)
//   E
// A line that starts with # is a note.
static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    return p;
}
static float rd_f(const char **p)
{
    char *e;
    float v = strtof(skip_ws(*p), &e);
    *p = e;
    return v;
}
static int bear_of(char c);
static bool levels_parse(const char *txt, size_t len)
{
    // two passes: count the things, then fill
    int nthing = 0;
    for (size_t i = 0; i < len; i++) {
        if ((i == 0 || txt[i - 1] == '\n') && txt[i] >= 'a' && txt[i] <= 'z') {
            nthing++;
        }
    }
    free(s_thing);
    s_thing = (thing_t *)gs_alloc(sizeof(thing_t) * (size_t)(nthing + 1));
    if (!s_thing) {
        snprintf(s_err, sizeof s_err, "NO MEMORY (LEVELS)");
        return false;
    }
    memset(s_thing, 0, sizeof(thing_t) * (size_t)(nthing + 1));
    s_nthing = 0;
    s_nlev = 0;
    level_t *L = NULL;
    const char *p = txt, *end = txt + len;
    char line[256];
    while (p < end) {
        const char *q = p;
        while (q < end && *q != '\n') {
            q++;
        }
        size_t n = (size_t)(q - p);
        if (n >= sizeof line) {
            n = sizeof line - 1;
        }
        memcpy(line, p, n);
        line[n] = 0;
        if (n && line[n - 1] == '\r') {
            line[--n] = 0;
        }
        p = q < end ? q + 1 : end;
        const char *s = skip_ws(line);
        if (!*s || *s == '#') {
            continue;
        }
        if (s[0] == 'L' && s[1] == ' ') {
            if (s_nlev >= NLEV_MAX) {
                break;
            }
            L = &s_lev[s_nlev++];
            memset(L, 0, sizeof *L);
            const char *t = s + 2;
            L->world = (uint8_t)clampi((int)rd_f(&t) - 1, 0, 2);
            L->num = (uint8_t)clampi((int)rd_f(&t), 1, 99);
            t = skip_ws(t);
            const char *bar = strchr(t, '|');
            size_t ne = bar ? (size_t)(bar - t) : strlen(t);
            if (ne > 47) {
                ne = 47;
            }
            memcpy(L->name[0], t, ne);
            L->name[0][ne] = 0;
            if (bar) {
                snprintf(L->name[1], sizeof L->name[1], "%s", bar + 1);
            } else {
                memcpy(L->name[1], L->name[0], sizeof L->name[0]);
            }
            L->first = s_nthing;
            L->s2 = 20000;
            L->s3 = 40000;
            continue;
        }
        if (!L) {
            continue;
        }
        if (s[0] == 'B' && s[1] == ' ') {
            for (const char *t = s + 2; *t && L->nbear < NBEAR_MAX; t++) {
                int b = bear_of(*t);
                if (b >= 0) {
                    L->bears[L->nbear++] = (uint8_t)b;
                }
            }
            continue;
        }
        if (s[0] == 'S' && s[1] == ' ') {
            const char *t = s + 2;
            L->s2 = (uint32_t)maxf(0, rd_f(&t));
            L->s3 = (uint32_t)maxf(0, rd_f(&t));
            continue;
        }
        if (s[0] == 'T' && s[1] == ' ') {
            const char *t = s + 2;
            L->edge = rd_f(&t);
            continue;
        }
        if (s[0] == 'E' && (s[1] == 0 || s[1] == ' ')) {
            L = NULL;
            continue;
        }
        if (*s < 'a' || *s > 'z' || s_nthing >= nthing) {
            continue;
        }
        // a thing: its name, then its numbers
        int k = -1;
        size_t wl = 0;
        while (s[wl] >= 'a' && s[wl] <= 'z') {
            wl++;
        }
        for (int i = 0; i < OK_N; i++) {
            if (strlen(OK_NAME[i]) == wl && !strncmp(s, OK_NAME[i], wl)) {
                k = i;
            }
        }
        if (k < 0) {
            continue;
        }
        thing_t *T = &s_thing[s_nthing];
        memset(T, 0, sizeof *T);
        T->kind = (uint8_t)k;
        T->tie = -1;
        const char *t = s + wl;
        float v[8] = {0};
        int nv = 0;
        while (nv < 8) {
            t = skip_ws(t);
            if (!*t) {
                break;
            }
            char *e;
            v[nv] = strtof(t, &e);
            if (e == t) {
                break;
            }
            t = e;
            nv++;
        }
        const float D2R = 0.017453293f;
        switch (k) {
        case OK_BLOCK:  // block x0 y0 x1 y1 var
            T->x = v[0];
            T->y = v[1];
            T->w = v[2] - v[0];
            T->h = v[3] - v[1];
            T->var = (uint8_t)v[4];
            if (T->w <= 0.01f || T->h <= 0.01f) {
                continue;
            }
            break;
        case OK_DIP:  // dip x0 x1 h
            T->x = v[0];
            T->w = v[1] - v[0];
            T->h = v[2] > 0.05f ? v[2] : 0.32f;
            T->y = 0;
            if (T->w <= 0.1f) {
                continue;
            }
            break;
        case OK_BALLOON:  // balloon x y var tie [ax ay]
            T->x = v[0];
            T->y = v[1];
            T->var = (uint8_t)v[2];
            T->tie = (int16_t)v[3];
            T->tx = v[4];
            T->ty = v[5];
            break;
        default:  // kind x y angle [w h var]
            T->x = v[0];
            T->y = v[1];
            T->a = v[2] * D2R;
            T->w = v[3];
            T->h = v[4];
            T->var = (uint8_t)v[5];
            break;
        }
        s_nthing++;
        L->n++;
    }
    for (int i = 0; i < s_nlev; i++) {
        if (!s_lev[i].nbear) {
            s_lev[i].bears[0] = 0;
            s_lev[i].nbear = 1;
        }
    }
    if (!s_nlev) {
        snprintf(s_err, sizeof s_err, "LEVELS: NO LEVEL IN THE FILE");
        return false;
    }
    return true;
}
static bool levels_load(void)
{
    return levels_parse((const char *)GS_LEVELS_TXT, GS_LEVELS_LEN);
}

// ================================================================ the saves
// The stars of each level: 2 bits (0 = not won, 1..3); 16 levels in each key. The best score of each level: the file
// /data/gummy.sav (40 x 4 bytes).
static uint8_t s_stars[NLEV_MAX];
static uint32_t s_best[NLEV_MAX];
static const char *const STAR_KEY[3] = {"gummy_st0", "gummy_st1", "gummy_st2"};
#define SAVE_FILE "/data/gummy.sav"
static void save_load(void)
{
    for (int k = 0; k < 3; k++) {
        uint32_t v = store_get(STAR_KEY[k], 0);
        for (int i = 0; i < 16 && k * 16 + i < NLEV_MAX; i++) {
            s_stars[k * 16 + i] = (uint8_t)((v >> (2 * i)) & 3);
        }
    }
    memset(s_best, 0, sizeof s_best);
    size_t n = 0;
    uint32_t b[NLEV_MAX];
    if (storage_ok() && storage_read(SAVE_FILE, b, sizeof b, &n) && n >= 4) {
        memcpy(s_best, b, n < sizeof b ? n : sizeof b);
    }
}
static void save_level(int li, int stars, uint32_t score)
{
    if (li < 0 || li >= NLEV_MAX) {
        return;
    }
    if (stars > s_stars[li]) {
        s_stars[li] = (uint8_t)stars;
        int k = li / 16;
        uint32_t v = 0;
        for (int i = 0; i < 16 && k * 16 + i < NLEV_MAX; i++) {
            v |= (uint32_t)(s_stars[k * 16 + i] & 3) << (2 * i);
        }
        store_set(STAR_KEY[k], v);
    }
    if (score > s_best[li]) {
        s_best[li] = score;
        if (storage_ok()) {
            storage_write(SAVE_FILE, s_best, sizeof s_best);
        }
    }
}
// a level is open when it is the first one, or when the level before it is won
static bool level_open(int li)
{
    return li <= 0 || (li < s_nlev && s_stars[li - 1] > 0);
}
static int stars_of_world(int w)
{
    int n = 0;
    for (int i = 0; i < s_nlev; i++) {
        if (s_lev[i].world == w) {
            n += s_stars[i];
        }
    }
    return n;
}
static int levels_of_world(int w, int *first)
{
    int n = 0;
    *first = -1;
    for (int i = 0; i < s_nlev; i++) {
        if (s_lev[i].world == w) {
            if (*first < 0) {
                *first = i;
            }
            n++;
        }
    }
    return n;
}
