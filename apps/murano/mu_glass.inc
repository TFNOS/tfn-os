// MURANO: the glass. A gather of hot glass on the end of the pipe is NS slices along the axis. Each slice has a length
// (dx), the volume of its glass wall (V), the volume of its air (A), a heat (T) and a bend (b1, b2: the sag, in the frame
// of the glass, which turns with the pipe). The radii come from them: r^2 = (V + A) / dx, ri^2 = A / dx (the factor pi
// is left out). The inputs: the turn, the blow, the swing (the tilt), the jacks (a pinch), the glory hole (the heat).
// The model and its numbers are the same as the page model (engine.js). Included by app_murano.c.

#define NS   44   // the slices of the glass
#define PMAX 300  // the shape along the axis: 1 px per entry

typedef struct {
    int n;
    float L, a0;
    float r[PMAX], ri[PMAX], v[PMAX], T[PMAX];
} prof_t;

// ---------------------------------------------------------------- the colours of the glass
typedef struct {
    float k[3];
    float body[3];
    bool milk;
    const char *en, *he;
} tint_t;
enum {
    TN_CRIS,
    TN_AMBRA,
    TN_SMER,
    TN_COB,
    TN_RUBY,
    TN_MILK,
    TN_N
};
static const tint_t TINTS[TN_N] = {
        {{0.92f, 0.97f, 0.94f}, {196, 222, 210}, false, "CRISTALLO", "קריסטל"},
        {{0.97f, 0.72f, 0.32f}, {214, 140, 52}, false, "AMBER", "ענבר"},
        {{0.42f, 0.86f, 0.5f}, {60, 160, 90}, false, "EMERALD", "אזמרגד"},
        {{0.3f, 0.42f, 0.98f}, {52, 82, 196}, false, "COBALT", "קובלט"},
        {{0.98f, 0.3f, 0.42f}, {190, 40, 66}, false, "GOLD RUBY", "אודם זהב"},
        {{0.96f, 0.95f, 0.92f}, {232, 228, 216}, true, "MILK GLASS", "זכוכית חלב"},
};

// a shape from points [a, r] (smooth steps between them); wall = the thickness of the wall (0: solid)
static void prof_from(prof_t *P, const float (*pts)[2], int np, float wall)
{
    float L = pts[np - 1][0];
    P->L = L;
    P->a0 = 0;
    P->n = (int)ceilf(L) + 1;
    if (P->n > PMAX) {
        P->n = PMAX;
    }
    for (int a = 0; a < P->n; a++) {
        int k = 0;
        while (k < np - 2 && pts[k + 1][0] < a) {
            k++;
        }
        float a0 = pts[k][0], r0 = pts[k][1], a1 = pts[k + 1][0], r1 = pts[k + 1][1];
        float t = clampf((a - a0) / (a1 - a0 > 0 ? a1 - a0 : 1), 0, 1), s = t * t * (3 - 2 * t);
        P->r[a] = r0 + (r1 - r0) * s;
        P->ri[a] = fmaxf(0, P->r[a] - wall - (a > L - 5 ? (a - (L - 5)) * 1.2f : 0));
        P->v[a] = 0;
        P->T[a] = 0;
    }
}

// The glass at one pixel. a = the distance along the axis, d = the distance from the axis (+ = down on the pipe, right
// when the piece stands). bg = the room behind the pixel (for the cold glass). False outside the glass.
typedef c3 (*bg_fn)(int X, int Y);
// tables of the glass (each 0.5 px of the path in the glass, 0 .. 160 px): the glow, and the colour that the glass
// lets through (k ^ (1 + t / 9)) for each tint
#define TT 321
static float s_glow_t[TT];
static float s_pass_t[TN_N][3][TT];
static bool s_tables;
static void glass_tables(void)
{
    if (s_tables) {
        return;
    }
    for (int i = 0; i < TT; i++) {
        float t = i * 0.5f;
        s_glow_t[i] = 0.18f + 0.82f * (1 - expf(-t / 13));
        for (int n = 0; n < TN_N; n++) {
            for (int c = 0; c < 3; c++) {
                s_pass_t[n][c][i] = powf(TINTS[n].k[c], 1 + t / 9);
            }
        }
    }
    s_tables = true;
}
static bool glass_px(const prof_t *P, float a, float d, bg_fn bg, const tint_t *tn, int X, int Y, c3 *out)
{
    int ia = (int)a;
    if (a < 0 || ia < (int)P->a0 || ia >= P->n - 1) {
        return false;
    }
    float f = a - ia;
    float r = P->r[ia] + (P->r[ia + 1] - P->r[ia]) * f;
    if (r < 0.6f) {
        return false;
    }
    float v = P->v[ia] + (P->v[ia + 1] - P->v[ia]) * f;
    float e = d - v, ae = fabsf(e);
    if (ae > r) {
        return false;
    }
    float ri = P->ri[ia] + (P->ri[ia + 1] - P->ri[ia]) * f;
    float T = P->T[ia];
    float u = e / r, nz = sqrtf(fmaxf(0, 1 - u * u));
    float tin = ae < ri ? 2 * sqrtf(ri * ri - e * e) : 0;
    float t = fmaxf(0, 2 * r * nz - tin);
    bool edge = ae > r - 1.05f || (P->a0 > 0 && a - P->a0 < 1.1f);
    float g = sstep(0.12f, 0.85f, T);
    c3 c;
    if (g < 0.999f) {
        // the cold glass: the room through it (bent a little), dark where the path in the glass is long
        if (tn->milk) {
            float lv = 0.45f + 0.4f * nz - (ae < ri ? 0.12f : 0);
            c = crgb(tn->body[0] * lv, tn->body[1] * lv, tn->body[2] * lv);
        } else {
            c3 b = bg(X + (int)(u * 2.2f + (u > 0 ? 0.5f : -0.5f)), Y - (int)(u * nz * 4 + (u > 0 ? 0.5f : -0.5f)));
            float p = fminf(1, t / 26);
            int ti = (int)(t * 2), tn_i = (int)(tn - TINTS);
            if (ti > TT - 1) {
                ti = TT - 1;
            }
            c = crgb(b.r * s_pass_t[tn_i][0][ti] + tn->body[0] * 0.16f * p,
                     b.g * s_pass_t[tn_i][1][ti] + tn->body[1] * 0.16f * p,
                     b.b * s_pass_t[tn_i][2][ti] + tn->body[2] * 0.16f * p);
            if (tin > 0 && fabsf(ae - ri) < 0.9f) {
                c = cmix(c, crgb(tn->body[0], tn->body[1], tn->body[2]), 0.35f);
            }
        }
    } else {
        c = crgb(0, 0, 0);
    }
    // highlights: the window above, the fire below, one glint
    float span = fmaxf(1, P->L - P->a0), am = (a - P->a0) / span;
    float ends = sstep(1, 7, a - P->a0) * sstep(0, 9, P->L - a);
    bool hl = u < -0.47f && u > -0.63f && ends > 0.5f;
    bool glint = u < -0.5f && u > -0.6f && am > 0.18f && am < 0.42f;
    bool fire = u > 0.56f && u < 0.68f && ends > 0.5f;
    if (g < 0.999f) {
        if (glint) {
            c = cmix(c, crgb(250, 252, 248), 0.92f * (1 - g));
        } else if (hl) {
            c = cmix(c, crgb(228, 234, 230), 0.62f * (1 - g));
        } else if (fire) {
            c = cmix(c, CR(HOT, 7), 0.4f * (1 - g));
        }
        if (edge) {
            c = cmix(c,
                     tn->milk ? crgb(60, 58, 54) : crgb(tn->body[0] * 0.25f, tn->body[1] * 0.25f, tn->body[2] * 0.25f),
                     0.7f * (1 - g));
        }
    }
    if (g > 0.001f) {
        // the hot glass glows; a long path in the glass glows more, so the bubble shows darker
        int ti = (int)(t * 2);
        float em = s_glow_t[ti < TT ? ti : TT - 1] * (0.42f + 0.58f * T);
        c3 h = DITH(HOT, em * (edge ? 0.75f : 1) + (hl ? 0.08f : 0), X, Y);
        c = g >= 0.999f ? h : cmix(c, h, g);
    }
    *out = c;
    return true;
}

// ---------------------------------------------------------------- the shelf of finished work (in the world)
static int s_snap_x0, s_snap_y0;
static uint16_t *s_snap;  // a copy of the wall behind a piece (40 x 40)
static c3 snap_bg(int X, int Y)
{
    return unpk(s_snap[clampi(Y - s_snap_y0, 0, 39) * 40 + clampi(X - s_snap_x0, 0, 39)]);
}
static void paint_shelf(void)
{
    static const int16_t planks[2] = {74, 120};
    for (int k = 0; k < 2; k++) {
        int py = planks[k];
        for (int x = 650; x < 814; x++) {
            wset(x, py, CR(WOOD, 7));
            wset(x, py + 1, CR(WOOD, 5));
            wset(x, py + 2, CR(WOOD, 4));
            wset(x, py + 3, CR(WOOD, 2));
            wset(x, py + 4, cmix(wget(x, py + 4), crgb(0, 0, 0), 0.5f));
        }
        static const int16_t br[2] = {658, 806};
        for (int b = 0; b < 2; b++) {
            for (int q = 0; q < 7; q++) {
                wset(br[b], py + 4 + q, CR(STEEL, 3));
                for (int j = 0; j < 7 - q; j++) {
                    wset(br[b] - j, py + 4 + q, CR(STEEL, 2));
                }
            }
        }
    }
    typedef struct {
        int16_t cx, base;
        uint8_t tint, np;
        float pts[7][2];
    } piece_t;
    static const piece_t PC[] = {
            {668, 74, TN_COB, 7, {{0, 5}, {3, 4}, {8, 6}, {16, 10}, {24, 9}, {29, 6}, {30, 6}}},
            {690, 74, TN_RUBY, 7, {{0, 7}, {6, 6}, {11, 4}, {12, 1.3f}, {21, 1.3f}, {22, 6}, {23, 6}}},
            {712, 74, TN_SMER, 6, {{0, 2.5f}, {7, 2.5f}, {11, 6}, {15, 8}, {32, 8}, {34, 7}}},
            {734, 74, TN_AMBRA, 5, {{0, 4}, {2, 6}, {8, 9}, {14, 8}, {17, 6}}},
            {758, 74, TN_CRIS, 6, {{0, 3}, {6, 3}, {10, 8}, {18, 11}, {26, 10}, {31, 8}}},
            {786, 74, TN_MILK, 5, {{0, 6}, {5, 5}, {12, 8}, {21, 9}, {27, 7}}},
            {670, 120, TN_CRIS, 4, {{0, 11}, {8, 10}, {13, 7}, {14, 7}}},
            {700, 120, TN_AMBRA, 5, {{0, 3}, {9, 3}, {14, 7}, {24, 8}, {28, 6}}},
            {730, 120, TN_COB, 6, {{0, 3}, {3, 7}, {10, 9}, {17, 7}, {20, 3}, {21, 0}}},
            {764, 120, TN_RUBY, 6, {{0, 6}, {4, 5}, {13, 9}, {22, 9}, {26, 6}, {27, 6}}},
            {792, 120, TN_SMER, 4, {{0, 13}, {7, 12}, {11, 9}, {12, 8}}},
    };
    static prof_t P;
    glass_tables();
    s_snap = heap_caps_malloc(40 * 40 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_snap) {
        return;
    }
    for (int k = 0; k < (int)(sizeof PC / sizeof PC[0]); k++) {
        const piece_t *p = &PC[k];
        prof_from(&P, p->pts, p->np, 1.6f);
        int top = p->base - (int)lroundf(P.L);
        s_snap_x0 = p->cx - 20;
        s_snap_y0 = top - 2;
        for (int y = 0; y < 40; y++) {
            for (int x = 0; x < 40; x++) {
                s_snap[y * 40 + x] = pk(wget(s_snap_x0 + x, s_snap_y0 + y));
            }
        }
        for (int y = top; y < p->base; y++) {
            for (int x = p->cx - 16; x <= p->cx + 16; x++) {
                c3 c;
                if (glass_px(&P, y - top + 0.5f, (float)(x - p->cx), snap_bg, &TINTS[p->tint], x, y, &c)) {
                    wset(x, y, c);
                }
            }
        }
    }
    heap_caps_free(s_snap);
    s_snap = NULL;
}

// ---------------------------------------------------------------- the hand of the gaffer (round shapes, pixel shading)
#define HAND_W  92
#define HAND_H  96
#define HAND_OX 30
#define HAND_OY 30
static uint16_t *s_hand[3];  // 3 frames: the fingers move a little as the hand rolls the pipe
static uint8_t *s_hand_a[3];
static float capsule(float px, float py, float ax, float ay, float bx, float by, float r)
{
    float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay;
    float t = clampf((wx * vx + wy * vy) / (vx * vx + vy * vy), 0, 1);
    return len2(wx - vx * t, wy - vy * t) - r;
}
static float s_fing[4][5];
static float s_hsh;
static void hand_parts(float x, float y, float *df, float *skin, float *sleeve)
{
    float m = 1e9f;
    for (int i = 0; i < 4; i++) {
        df[i] = capsule(x, y, s_fing[i][0], s_fing[i][1], s_fing[i][2], s_fing[i][3], s_fing[i][4]);
        if (df[i] < m) {
            m = df[i];
        }
    }
    float bx = (x - 1.5f - s_hsh * 0.5f) / 11.5f, by = (y + 9.5f) / 6;
    float back = len2(bx, by) * 6 - 6;
    float thumb = capsule(x, y, -3 + s_hsh, -9.5f, -14 + s_hsh, -7, 2.6f);
    float arm = capsule(x, y, 9, -9, 22, 15, 6.4f);
    *sleeve = capsule(x, y, 21, 17, 36, 52, 10.5f);
    *skin = fminf(fminf(m, back), fminf(thumb, arm));
}
static float hand_skin(float x, float y)
{
    float df[4], sk, sl;
    hand_parts(x, y, df, &sk, &sl);
    return sk;
}
static float hand_sleeve(float x, float y)
{
    float df[4], sk, sl;
    hand_parts(x, y, df, &sk, &sl);
    return sl;
}
static void make_hand(int k, float phase)
{
    const float SC = 1.35f;
    s_hsh = phase;
    const float F[4][5] = {{-8, -6.5f, -9, 5.4f, 2.6f},
                           {-3, -7.5f, -3.6f, 6, 2.75f},
                           {2, -7.5f, 1.6f, 5.6f, 2.65f},
                           {6.6f, -6.5f, 6.4f, 4.4f, 2.35f}};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 5; j++) {
            s_fing[i][j] = F[i][j];
        }
        s_fing[i][0] += phase;
        s_fing[i][2] += phase;
    }
    uint16_t *S = s_hand[k];
    uint8_t *A = s_hand_a[k];
    for (int y = 0; y < HAND_H; y++) {
        for (int x = 0; x < HAND_W; x++) {
            float lx = (x - HAND_OX) / SC, ly = (y - HAND_OY) / SC, df[4], sk, sl;
            hand_parts(lx, ly, df, &sk, &sl);
            bool on = false;
            c3 c = {0, 0, 0};
            if (sl < 0) {
                float gx = hand_sleeve(lx + 0.5f, ly) - hand_sleeve(lx - 0.5f, ly),
                      gy = hand_sleeve(lx, ly + 0.5f) - hand_sleeve(lx, ly - 0.5f);
                float v = 0.56f - gx * 0.5f - gy * 0.25f;
                if (sl > -0.9f) {
                    v = 0.1f;
                }
                if (fabsf(capsule(lx, ly, 14, 24, 30, 20, 0)) < 0.55f ||
                    fabsf(capsule(lx, ly, 20, 34, 35, 30, 0)) < 0.5f) {
                    v -= 0.3f;
                }
                if (ly < 21 && sl < -0.9f) {
                    v += 0.16f;
                }
                c = FLAT(LINEN, v);
                on = true;
            }
            if (sk < 0 && (sl >= 0 || ly < 17)) {
                float gx = hand_skin(lx + 0.5f, ly) - hand_skin(lx - 0.5f, ly),
                      gy = hand_skin(lx, ly + 0.5f) - hand_skin(lx, ly - 0.5f);
                float v = 0.58f - gx * 0.45f - gy * 0.3f;  // the light comes from the furnace on the left
                float d0 = 1e9f, d1 = 1e9f;
                for (int i = 0; i < 4; i++) {
                    if (df[i] < d0) {
                        d1 = d0;
                        d0 = df[i];
                    } else if (df[i] < d1) {
                        d1 = df[i];
                    }
                }
                if (d0 < 0 && d1 < 0.6f && d1 - d0 < 1.2f) {
                    v = 0.14f;  // the gap between two fingers
                }
                for (int i = 0; i < 4; i++) {
                    if (len2(lx - s_fing[i][2], ly - s_fing[i][3] + 1.5f) < 1.25f) {
                        v = 0.86f;  // the nails
                    }
                    if (len2(lx - s_fing[i][0] + 0.3f, ly - s_fing[i][1] - 0.7f) < 1.1f) {
                        v += 0.15f;  // the knuckles
                    }
                }
                if (fabsf(capsule(lx, ly, 9, -4, 15, 8, 0)) < 0.45f) {
                    v -= 0.18f;
                }
                if (sk > -0.8f) {
                    v = 0.05f;
                }
                c = FLAT(SKIN, v);
                on = true;
            }
            A[y * HAND_W + x] = on;
            S[y * HAND_W + x] = on ? pk(c) : 0;
        }
    }
}

// ---------------------------------------------------------------- the orders (one new skill on each day)
#define NDAYS      5
#define DAY_MARKET 6  // the free days after day 5: random orders
typedef struct {
    const char *skill[2], *what[2], *who[2];
    uint8_t tint;
    bool aut, tilt, jacks;
    uint8_t np;
    float pts[10][2];
} order_t;
static const order_t ORDERS[NDAYS] = {
        {{"BLOW", "נשיפה"},
         {"A LANTERN GLOBE", "כדור לפנס"},
         {"THE LAMPLIGHTER", "מדליק הפנסים"},
         TN_CRIS,
         true,
         false,
         false,
         9,
         {{0, 8}, {5, 13}, {10, 22}, {18, 28}, {32, 30}, {46, 28}, {54, 22}, {59, 13}, {61, 0}}},
        {{"TURN", "סיבוב"},
         {"A ROUND FLASK", "בקבוקון עגול"},
         {"THE APOTHECARY", "הרוקח"},
         TN_AMBRA,
         false,
         false,
         false,
         9,
         {{0, 9}, {8, 12}, {14, 22}, {24, 31}, {38, 34}, {52, 32}, {62, 24}, {68, 13}, {70, 0}}},
        {{"SWING", "הטיה"},
         {"A TALL BOTTLE", "בקבוק גבוה"},
         {"THE WINE SELLER", "מוכר היין"},
         TN_SMER,
         false,
         true,
         false,
         9,
         {{0, 9}, {14, 11}, {26, 18}, {40, 24}, {60, 26}, {84, 25}, {98, 21}, {104, 12}, {106, 0}}},
        {{"PINCH", "צביטה"},
         {"A VASE WITH A NECK", "אגרטל עם צוואר"},
         {"SIGNORA BELLINI", "סניורה בליני"},
         TN_COB,
         false,
         true,
         true,
         10,
         {{0, 10}, {6, 10}, {12, 12}, {20, 21}, {32, 32}, {46, 37}, {60, 37}, {72, 31}, {82, 19}, {86, 0}}},
        {{"ALL", "הכול"},
         {"A BOWL FOR THE DOGE", "קערה לדוג'ה"},
         {"THE DOGE", "הדוג'ה"},
         TN_RUBY,
         false,
         true,
         true,
         8,
         {{0, 22}, {6, 31}, {14, 38}, {24, 41}, {33, 38}, {40, 29}, {44, 15}, {45, 0}}},
};
// the market: the buyers and the things of a random order (a base shape of a day, a little changed)
static const char *const MK_WHO[][2] = {{"A SAILOR", "מלח"},         {"A NUN", "נזירה"},    {"A MERCHANT", "סוחר"},
                                        {"A COUNTESS", "רוזנת"},     {"A PAINTER", "צייר"}, {"THE CANTOR", "החזן"},
                                        {"A GONDOLIER", "גונדולייר"}};
static const char *const MK_WHAT[NDAYS][2] = {
        {"A GLOBE", "כדור"}, {"A FLASK", "בקבוקון"}, {"A BOTTLE", "בקבוק"}, {"A VASE", "אגרטל"}, {"A BOWL", "קערה"}};

typedef struct {
    int day;                                              // 1..5, or DAY_MARKET
    const char *skill[2], *what[2], *who[2], *shortw[2];  // (shortw: the short name, for a small screen)
    uint8_t tint;
    bool aut, tilt, jacks;
    prof_t P;
} cur_order_t;

// ---------------------------------------------------------------- the model of the glass
enum {
    PH_WORK,
    PH_REVEAL,
    PH_BURST,
    PH_DROP,
    PH_CRACK
};
enum {
    M_NONE,
    M_BURST,
    M_DROP,
    M_CRACK,
    M_SAG,
    M_STIFF,
    M_FLAT,
    M_BIG,
    M_JACKS_LATER
};
typedef struct {
    float a, d, va, vd, life;
    bool spark;
} part_t;
#define NPART 96
typedef struct {
    float flick;  // a push on the turn (rad/s)
    bool blow, reheat, crack;
    float tilt;  // -1 .. 1: + = the end of the pipe down (longer)
    bool jack;
    float jack_a;  // the place of the jacks along the axis (px)
} gin_t;
typedef struct {
    cur_order_t O;
    float dx[NS], V[NS], A[NS], T[NS], b1[NS], b2[NS];
    float theta, omega, P, tilt, slide, t, phase_t, shake, score, final, reveal_t, T0, cut_a, flick_t;
    bool jack_on, auto_roll, no_cool;
    float jack_a, jack_gap;
    int phase, msg, stars;
    float msg_t;
    prof_t prof;
    part_t parts[NPART];
    int nparts;
    gin_t last;
} glass_t;

static uint32_t s_rng = 12345;
static float rnd(void)
{
    s_rng = s_rng * 1664525u + 1013904223u;
    return (s_rng >> 8) / 16777216.0f;
}

static void order_make(cur_order_t *O, int day)
{
    O->day = day;
    if (day >= 1 && day <= NDAYS) {
        const order_t *o = &ORDERS[day - 1];
        for (int l = 0; l < 2; l++) {
            O->skill[l] = o->skill[l];
            O->what[l] = o->what[l];
            O->who[l] = o->who[l];
            O->shortw[l] = MK_WHAT[day - 1][l];
        }
        O->tint = o->tint;
        O->aut = o->aut;
        O->tilt = o->tilt;
        O->jacks = o->jacks;
        prof_from(&O->P, o->pts, o->np, 0);
        return;
    }
    // a market order: a base shape of a day, a little longer or shorter, wider or narrower; any colour
    int b = (int)(rnd() * NDAYS) % NDAYS;
    const order_t *o = &ORDERS[b];
    float sl = 0.9f + rnd() * 0.2f, sr = 0.9f + rnd() * 0.18f;
    float pts[10][2];
    for (int i = 0; i < o->np; i++) {
        pts[i][0] = o->pts[i][0] * sl;
        pts[i][1] = o->pts[i][1] * sr;
    }
    int w = (int)(rnd() * 7) % 7;
    O->skill[0] = "MARKET";
    O->skill[1] = "שוק";
    O->what[0] = MK_WHAT[b][0];
    O->what[1] = MK_WHAT[b][1];
    O->shortw[0] = MK_WHAT[b][0];
    O->shortw[1] = MK_WHAT[b][1];
    O->who[0] = MK_WHO[w][0];
    O->who[1] = MK_WHO[w][1];
    O->tint = (uint8_t)((int)(rnd() * TN_N) % TN_N);
    O->aut = false;
    O->tilt = true;
    O->jacks = true;
    prof_from(&O->P, (const float (*)[2])pts, o->np, 0);
}

#define R_CAP 44.5f  // (the widest glass: it fits the glory hole, HOLE_R 46)
static inline void radii(const glass_t *G, int i, float *r, float *ri)
{
    *r = sqrtf((G->V[i] + G->A[i]) / G->dx[i]);
    *ri = sqrtf(G->A[i] / G->dx[i]);
}

static void say(glass_t *G, int m, float s)
{
    if (G->msg != m) {
        G->msg = m;
        G->msg_t = s;
    } else if (G->msg_t < 0.3f) {
        G->msg_t = 0.3f;
    }
}

static void glass_build(glass_t *G);
static void glass_gather(glass_t *G, int day)
{
    memset(G, 0, sizeof *G);
    order_make(&G->O, day);
    const float d0 = 1.1f, L0 = NS * d0;
    for (int i = 0; i < NS; i++) {
        float a = (i + 0.5f) * d0, s = a / L0;
        float r = 22 * sqrtf(fmaxf(0.03f, 1 - powf(s, 5))) * (0.45f + 0.55f * sstep(0, 17, a));
        G->dx[i] = d0;
        G->A[i] = i < 2 ? 1.6f * 1.6f * d0 : 0;
        G->V[i] = r * r * d0 - G->A[i];
        G->T[i] = 0.74f + 0.26f * sstep(0, 16, a);
    }
    G->jack_a = 20;
    G->jack_gap = 46;
    G->phase = PH_WORK;
    glass_build(G);
}

typedef struct {
    float L, rmax, amax, tmin, tavg;
} meas_t;
static meas_t measure(const glass_t *G)
{
    meas_t m = {G->prof.L, 0, 0, 1, 0};
    for (int a = 0; a < G->prof.n; a++) {
        if (G->prof.r[a] > m.rmax) {
            m.rmax = G->prof.r[a];
            m.amax = (float)a;
        }
    }
    for (int i = 0; i < NS; i++) {
        m.tmin = fminf(m.tmin, G->T[i]);
        m.tavg += G->T[i];
    }
    m.tavg /= NS;
    return m;
}
static float r_at(const glass_t *G, float a)
{
    return G->prof.r[clampi((int)lroundf(a), 0, G->prof.n - 1)];
}

static void glass_fail(glass_t *G, int kind)
{
    G->phase = kind;
    G->phase_t = 0;
    G->P = 0;
    G->jack_on = false;
    G->shake = kind == PH_BURST ? 1 : 0.5f;
    const prof_t *p = &G->prof;
    if (kind == PH_BURST) {
        say(G, M_BURST, 3);
        for (int k = 0; k < 70 && G->nparts < NPART; k++) {
            float aa = rnd() * p->L, side = rnd() < 0.5f ? -1.0f : 1.0f, rr = p->r[clampi((int)aa, 0, p->n - 1)];
            part_t *q = &G->parts[G->nparts++];
            q->a = aa;
            q->d = side * rr;
            q->va = (rnd() - 0.5f) * 60;
            q->vd = side * (30 + rnd() * 90);
            q->life = 1.4f + rnd();
            q->spark = false;
        }
    } else if (kind == PH_DROP) {
        say(G, M_DROP, 3);
    } else {
        say(G, M_CRACK, 3);
    }
}

static void glass_physics(glass_t *G, float h)
{
    float f[NS], r[NS], ri[NS], a[NS], acc = 0;
    float *dx = G->dx, *V = G->V, *A = G->A, *T = G->T;
    for (int i = 0; i < NS; i++) {
        float s = sstep(0.3f, 1, T[i]);
        f[i] = s * s;
        radii(G, i, &r[i], &ri[i]);
        a[i] = acc + dx[i] / 2;
        acc += dx[i];
    }
    bool in_hole = G->slide > 0.02f;
    float ang = in_hole ? 0 : G->tilt * 0.62f, gax = fsin(ang), gp = fcos(ang);
    // stretch or squash along the axis (gravity along the pipe); the mass beyond each slice pulls it
    float mb = 0;
    for (int i = NS - 1; i >= 0; i--) {
        mb += V[i];
        float area = V[i] / dx[i];
        float eps = gax * f[i] * mb / (area + 6) * 0.0075f;
        dx[i] = clampf(dx[i] * (1 + eps * h), 0.35f, 9);
        if (gax > 0.2f && area < 2.2f && f[i] > 0.3f && i < NS - 3) {
            glass_fail(G, PH_DROP);
            return;
        }
    }
    // blow: the air goes into the bubble; a hot, thin wall gives way first
    if (G->P > 0.02f && !in_hole) {
        for (int i = 0; i < NS; i++) {
            if (A[i] > 0.75f * dx[i]) {
                float w = r[i] - ri[i];
                float nri = ri[i] + fminf(9, G->P * 6.5f * f[i] * (1 + fminf(ri[i], 26) / 10) / (1 + w / 20)) * h;
                float dA = nri * nri * dx[i] - A[i];
                A[i] += dA;
                dx[i] *= 1 + 0.2f * dA / (A[i] + V[i]);
                if (w < 0.85f && G->P > 0.25f) {
                    glass_fail(G, PH_BURST);
                    return;
                }
                if (T[i] < 0.2f && G->P > 0.4f) {
                    glass_fail(G, PH_CRACK);
                    return;
                }
            } else {
                // the front of the bubble goes into the solid glass; the bottom stays thick
                float rest = 0;
                for (int j = i; j < NS; j++) {
                    rest += dx[j];
                }
                if (rest > 9) {
                    A[i] += G->P * 40 * fmaxf(f[i], 0.25f) * dx[i] * h;
                }
                break;
            }
        }
    }
    // the jacks make a neck where they touch, but only while the glass turns
    if (G->jack_on && !in_hole) {
        G->jack_gap = fmaxf(4, G->jack_gap - 24 * h);
        float roll = clampf((fabsf(G->omega) - 0.8f) / 2.2f, 0, 1);
        for (int i = 0; i < NS; i++) {
            float q = (a[i] - G->jack_a) / 3, w = expf(-q * q);
            if (w < 0.02f || r[i] <= G->jack_gap) {
                continue;
            }
            if (T[i] < 0.2f) {
                glass_fail(G, PH_CRACK);
                return;
            }
            float k = fminf(1, 3.4f * f[i] * w * (0.15f + 0.85f * roll) * h);
            float rn = r[i] - (r[i] - G->jack_gap) * k;
            dx[i] = clampf(dx[i] * (r[i] / rn) * (r[i] / rn), 0.35f, 9);
            if (roll < 0.3f) {
                G->b1[i] += fcos(G->theta) * 0.004f * w * f[i];
            }
        }
    } else {
        G->jack_gap = fminf(52, G->jack_gap + 60 * h);
    }
    // the glass sags to the floor; the turn of the pipe takes the sag round and round, so it does not grow
    float m1 = 0, m0 = 0, ct = fcos(G->theta), st = fsin(G->theta);
    float keepf = clampf((fabsf(G->omega) - 1) / 3, 0, 1);
    for (int i = NS - 1; i >= 0; i--) {
        float q0, q1;
        radii(G, i, &q0, &q1);
        float I = q0 * q0 * q0 * q0 - q1 * q1 * q1 * q1 + 4000;
        float M = m1 - a[i] * m0;
        float db = fminf(0.009f, gp * M / I * f[i] * 0.0026f) * h;
        G->b1[i] += db * ct;
        G->b2[i] += db * st;
        float keep = expf(-h * f[i] * 1.3f * keepf);
        G->b1[i] = clampf(G->b1[i] * keep, -0.012f, 0.012f);
        G->b2[i] = clampf(G->b2[i] * keep, -0.012f, 0.012f);
        m1 += V[i] * a[i];
        m0 += V[i];
    }
    // surface tension: the shape goes smooth while it is hot
    for (int i = 1; i < NS - 1; i++) {
        float q0, q1, q2, z;
        radii(G, i - 1, &q0, &z);
        radii(G, i, &q1, &z);
        radii(G, i + 1, &q2, &z);
        float rn = q1 + (q0 + q2 - 2 * q1) * clampf(0.9f * f[i] * h, 0, 0.3f);
        if (rn > 0.5f) {
            dx[i] = clampf((V[i] + A[i]) / (rn * rn), 0.35f, 9);
        }
        float k = clampf(2 * f[i] * h, 0, 0.3f);
        G->b1[i] += (G->b1[i - 1] + G->b1[i + 1] - 2 * G->b1[i]) * k;
        G->b2[i] += (G->b2[i - 1] + G->b2[i + 1] - 2 * G->b2[i]) * k;
    }
    // the heat: a thin wall cools first; the steel pipe takes heat; the glory hole heats it again
    for (int i = 0; i < NS; i++) {
        float q0, q1;
        radii(G, i, &q0, &q1);
        float w = q0 - q1;
        if (G->slide > 0.9f) {
            T[i] += (1.02f - T[i]) * 1.5f * h;
        } else if (!G->no_cool) {
            T[i] -= 0.022f * (0.6f + 4 / (w + 1)) * (1 + 0.6f * i / NS) * h + (i < 8 ? 0.006f * (8 - i) / 8 * h : 0);
        }
        T[i] = clampf(T[i], 0, 1);
    }
    // the glass always fits the glory hole (else the ball can get too big, and nothing the player does can compensate):
    // no slice gets wider than R_CAP; a blow or a squash past it makes the glass longer (the volume
    // stays). The widest order is a market bowl (41 x 1.08 = 44.3 px).
    for (int i = 0; i < NS; i++) {
        const float dmin = (V[i] + A[i]) / (R_CAP * R_CAP);
        if (dx[i] < dmin) {
            dx[i] = dmin;
        }
    }
}

static void glass_build(glass_t *G)
{
    float ac[NS], rr[NS], rri[NS], vv[NS];
    float acc = 0, p1 = 0, p2 = 0, o1 = 0, o2 = 0, c = fcos(G->theta), s = fsin(G->theta);
    for (int i = 0; i < NS; i++) {
        float q0, q1;
        radii(G, i, &q0, &q1);
        p1 += G->b1[i] * G->dx[i];
        p2 += G->b2[i] * G->dx[i];
        o1 += p1 * G->dx[i];
        o2 += p2 * G->dx[i];
        ac[i] = acc + G->dx[i] / 2;
        acc += G->dx[i];
        rr[i] = q0;
        rri[i] = q1;
        vv[i] = o1 * c + o2 * s;
    }
    prof_t *P = &G->prof;
    float L = fminf(acc, PMAX - 2);
    P->n = (int)ceilf(L) + 1;
    P->L = L;
    P->a0 = G->phase == PH_REVEAL ? G->cut_a : 0;
    int k = 0;
    for (int a = 0; a < P->n; a++) {
        while (k < NS - 2 && ac[k + 1] < a) {
            k++;
        }
        float t = (a - ac[k]) / (ac[k + 1] - ac[k]);
        if (a < ac[0]) {
            t = 0;
        }
        t = clampf(t, 0, 1);
        P->r[a] = rr[k] + (rr[k + 1] - rr[k]) * t;
        P->ri[a] = rri[k] + (rri[k + 1] - rri[k]) * t;
        P->v[a] = vv[k] + (vv[k + 1] - vv[k]) * t;
        P->T[a] = G->T[k + (t > 0.5f ? 1 : 0) < NS ? k + (t > 0.5f ? 1 : 0) : NS - 1];
        if (a > acc - 1.2f) {
            P->r[a] *= 0.6f;
        }
        if (a < 8 && P->r[a] > 4 && G->phase != PH_REVEAL) {
            float q = (a + 0.5f) / 8;
            P->r[a] = fmaxf(4, P->r[a] * sqrtf(1 - (1 - q) * (1 - q)));
            P->ri[a] = fminf(P->ri[a], P->r[a] - 2);
        }
    }
    // the match with the order: the shared area of the two side views over the area of both
    const prof_t *O = &G->O.P;
    float inter = 0, uni = 0;
    int m = P->n > O->n ? P->n : O->n;
    for (int a = 0; a < m; a++) {
        float r1 = a < P->n ? P->r[a] : 0, v1 = a < P->n ? P->v[a] : 0, r2 = a < O->n ? O->r[a] : 0;
        float lo = fmaxf(v1 - r1, -r2), hi = fminf(v1 + r1, r2), ov = fmaxf(0, hi - lo);
        inter += ov;
        uni += 2 * r1 + 2 * r2 - ov;
    }
    G->score = uni > 0 ? inter / uni : 0;
}

static void glass_finish(glass_t *G)
{
    const prof_t *P0 = &G->prof;
    int cut = 4;
    float rc = 99;
    for (int a = 4; a < fmaxf(6, P0->L * 0.3f) && a < P0->n; a++) {
        if (P0->r[a] < rc) {
            rc = P0->r[a];
            cut = a;
        }
    }
    G->cut_a = (float)cut;
    G->phase = PH_REVEAL;
    G->reveal_t = 0;
    G->P = 0;
    G->jack_on = false;
    float s = 0;
    for (int i = 0; i < NS; i++) {
        s += G->T[i];
    }
    G->T0 = fmaxf(0.5f, s / NS);
    glass_build(G);
    G->final = G->score;
    G->stars = G->score >= 0.86f ? 3 : G->score >= 0.74f ? 2 : G->score >= 0.6f ? 1 : 0;
    for (int k = 0; k < 24 && G->nparts < NPART; k++) {
        part_t *q = &G->parts[G->nparts++];
        q->a = 0;
        q->d = (rnd() - 0.5f) * 6;
        q->va = -20 - rnd() * 40;
        q->vd = (rnd() - 0.5f) * 80 - 30;
        q->life = 0.5f + rnd() * 0.4f;
        q->spark = true;
    }
}

// one step of the game time (dt in s)
static void glass_step(glass_t *G, float dt, const gin_t *in)
{
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    G->last = *in;
    if (in->flick != 0) {
        G->flick_t = 0.45f;
    }
    G->flick_t = fmaxf(0, G->flick_t - dt);
    G->t += dt;
    G->phase_t += dt;
    G->msg_t -= dt;
    G->shake = fmaxf(0, G->shake - dt * 1.6f);
    const cur_order_t *o = &G->O;
    if (G->phase == PH_WORK) {
        G->omega += in->flick;
        if (o->aut || G->auto_roll) {
            G->omega += (5 - G->omega) * fminf(1, dt * 3);
        }
        G->omega *= expf(-dt * 0.55f);
        G->omega = clampf(G->omega, -12, 12);
        G->theta += G->omega * dt;
        float tg = o->tilt ? clampf(in->tilt, -1, 1) : 0;
        G->tilt += (tg - G->tilt) * fminf(1, dt * 5);
        meas_t mm = measure(G);
        bool want = in->reheat;
        if (want && G->slide < 0.05f && mm.rmax > HOLE_R - 1) {
            want = false;
            say(G, M_BIG, 1.5f);
        }
        G->slide = clampf(G->slide + (want ? 1 : -1) * dt * 1.8f, 0, 1);
        G->P += ((in->blow ? 1.0f : 0.0f) - G->P) * fminf(1, dt * (in->blow ? 5 : 3));
        G->jack_on = in->jack && o->jacks;
        if (G->jack_on) {
            G->jack_a = clampf(in->jack_a, 0, mm.L);
        }
        if (in->jack && !o->jacks) {
            say(G, M_JACKS_LATER, 1);
        }
        for (int k = 0; k < 4 && G->phase == PH_WORK; k++) {
            glass_physics(G, dt / 4);
        }
        glass_build(G);
        if (G->phase != PH_WORK) {
            return;
        }
        meas_t m2 = measure(G);
        if (G->jack_on && fabsf(G->omega) < 1.2f) {
            say(G, M_FLAT, 0.5f);
        } else if (m2.tmin < 0.36f && G->slide < 0.5f) {
            say(G, M_STIFF, 0.5f);
        } else if (!o->aut && fabsf(G->omega) < 0.9f && m2.tavg > 0.45f && G->t > 2) {
            say(G, M_SAG, 0.5f);
        }
        if (in->crack) {
            glass_finish(G);
        }
    } else if (G->phase == PH_REVEAL) {
        G->reveal_t += dt;
        G->theta += 0.9f * dt;
        float k = clampf((G->reveal_t - 0.6f) / 2.2f, 0, 1);
        float Tn = G->reveal_t < 3.2f ? 0.3f + (G->T0 - 0.3f) * (1 - k * k * (3 - 2 * k)) : 0;
        for (int i = 0; i < NS; i++) {
            G->T[i] = Tn;
        }
        glass_build(G);
    }
    int w = 0;
    for (int i = 0; i < G->nparts; i++) {
        part_t *p = &G->parts[i];
        p->a += p->va * dt;
        p->d += p->vd * dt;
        p->vd += 160 * dt;
        p->life -= dt;
        if (p->life > 0) {
            G->parts[w++] = *p;
        }
    }
    G->nparts = w;
}

// ---------------------------------------------------------------- the demo gaffer: it plays an order as a skilled hand
typedef struct {
    bool heat;
    float t, still, last;
    int sw;
} bot_t;
static void bot_input(glass_t *G, bot_t *st, float dt, gin_t *in)
{
    memset(in, 0, sizeof *in);
    st->t += dt;
    if (G->phase != PH_WORK) {
        return;
    }
    if (fabsf(G->omega) < 4.2f) {
        in->flick = 2.5f;
    }
    meas_t m = measure(G);
    const cur_order_t *o = &G->O;
    const prof_t *O = &o->P;
    if (st->heat) {
        in->reheat = true;
        if (m.tmin > 0.9f && G->slide > 0.97f) {
            st->heat = false;
        }
        return;
    }
    if (m.tmin < 0.4f && m.rmax < HOLE_R - 1) {
        st->heat = true;
        in->reheat = true;
        return;
    }
    float ob = 0, on = 99;
    int oa = 0, na = -1;
    for (int a = 0; a < O->n; a++) {
        if (O->r[a] > ob) {
            ob = O->r[a];
            oa = a;
        }
    }
    for (int a = 4; a < oa; a++) {
        if (O->r[a] < on) {
            on = O->r[a];
            na = a;
        }
    }
    if (na >= 0 && on > ob * 0.5f) {
        na = -1;  // no real neck in this order
    }
    float len_t = O->L * (o->jacks && na > 0 ? 0.86f : 1);
    if (o->tilt && m.tavg > 0.5f) {
        if (m.L < len_t * 0.9f) {
            st->sw = 1;
        } else if (m.L > len_t * 1.08f) {
            st->sw = -1;
        } else if ((st->sw > 0 && m.L > len_t * 0.97f) || (st->sw < 0 && m.L < len_t * 1.03f)) {
            st->sw = 0;
        }
        in->tilt = (float)st->sw;
    }
    if (m.rmax < ob * 0.96f && m.tavg > 0.5f) {
        in->blow = true;
    }
    if (o->jacks && na > 0 && m.rmax > ob * 0.75f && in->tilt == 0) {
        float neck_a = na * m.L / len_t;
        if (r_at(G, neck_a) > on + 1) {
            in->jack = true;
            in->jack_a = neck_a;
            in->blow = false;
        }
    }
    if (G->score > st->last + 0.003f) {
        st->last = G->score;
        st->still = 0;
    } else {
        st->still += dt;
    }
    if (G->score > 0.9f || (G->score > 0.62f && st->still > 4) || st->t > 80) {
        in->crack = true;
    }
}
