// CUBBY: the island SUNDROP COVE (included by app_cubby.c). The look is a set of rounded terraces and props; the
// physics uses a grid of cells (1 unit, the size of a cube): the height of the ground in each cell, the stack of
// cubes on it, the round colliders (the lamp room, the palm trunks). x goes to the east, z to the north, y up.

#define GX    26
#define GZ    22
#define WATER (-9)
#define STK   8
static int8_t s_H[GX][GZ];          // the ground of each cell (units); WATER = the sea
static uint8_t s_stk[GX][GZ][STK];  // the cubes on a cell (CB_* kind), from the bottom
static uint8_t s_nstk[GX][GZ];

enum {
    CB_TIN = 1,
    CB_SPRING,
    CB_FLOAT
};

// the terraces: the rounded rectangle, the corner radius, the bottom, the top, the look
typedef struct {
    float x0, z0, x1, z1, rc, y0, y1;
    uint8_t look;
} slab_t;
enum {
    LK_SAND,
    LK_GRASS,
    LK_SHELF,
    LK_PILLAR,
    LK_STEP,
    LK_HILL
};
static const slab_look_t LOOKS[] = {
        [LK_SAND] = {0xFFD98C, 0xFFC86E, 0xFFEDB8, 0xFFA45A, 0xD8685A, 0xFF8C5A},
        [LK_GRASS] = {0x3EDC48, 0x9CF03C, 0xB4FA6A, 0xFF7058, 0x8A40C8, 0xFF4E84},
        [LK_SHELF] = {0xFFB45A, 0xFF9C46, 0xFFD890, 0xFF7E48, 0xC0407A, 0xFFA060},
        [LK_PILLAR] = {0x7AE44A, 0xB4F454, 0xC8FC78, 0xA070FF, 0x5030C8, 0xC88AFF},
        [LK_STEP] = {0xFFA898, 0xFF9488, 0xFFCCB8, 0xFF7480, 0xA048B8, 0xFF8C9C},
        [LK_HILL] = {0x36D84E, 0x86EA3E, 0xA8F466, 0xFF8460, 0x8C44CC, 0xFF6086},
};
static const slab_t SLABS[] = {
        {1.0f, 1.0f, 25.0f, 9.0f, 2.6f, SEA_Y - 0.15f, 0.0f, LK_SAND},  // the beach
        {3.0f, 8.0f, 22.0f, 20.0f, 2.2f, -0.6f, 2.0f, LK_GRASS},        // the meadow
        {13.0f, 14.0f, 21.0f, 19.0f, 1.4f, 1.5f, 3.0f, LK_HILL},        // the hill of the lighthouse
        {15.0f, 2.0f, 18.0f, 5.0f, 0.7f, -0.2f, 3.0f, LK_SHELF},        // goal 1: the rock shelf
        {9.0f, 12.0f, 11.0f, 14.0f, 0.6f, 1.6f, 7.0f, LK_PILLAR},       // goal 2: the violet pillar
        {3.0f, 6.0f, 6.0f, 8.0f, 0.7f, -0.2f, 1.0f, LK_STEP},           // the step of the west
};
#define NSLAB (int)(sizeof SLABS / sizeof SLABS[0])

// the lighthouse (goal 3): a 2 x 2 column of cells, the gallery at its top, the lamp room in the middle
#define LH_X   18.0f
#define LH_Z   17.0f
#define LH_TOP 10
// the round colliders: centre, radius, bottom, top
typedef struct {
    float x, z, r, y0, y1;
} rcol_t;
#define RMAX 12
static rcol_t s_rc[RMAX];
static int s_nrc;

// the junk piles: the kind of cube that they make
typedef struct {
    float x, z;
    uint8_t kind;
    float y;
    float pulse;
} pile_t;
static pile_t s_pile[3] = {
        {12.5f, 3.5f, CB_TIN, 0, 0},
        {6.5f, 11.5f, CB_SPRING, 0, 0},
        {21.5f, 4.5f, CB_FLOAT, 0, 0},
};
// the three SUNDROPS (the goals) and the sparks (the small finds)
typedef struct {
    v3 p;
    bool got;
    float t;
} drop_t;
static drop_t s_drop[3];
#define NSPARK 14
static drop_t s_spark[NSPARK];
static const float SPARK_XYZ[NSPARK][3] = {
        {4.5f, 1.6f, 7.0f},   {5.5f, 2.6f, 9.0f},   {8.5f, 0.6f, 4.0f},   {10.0f, 0.6f, 4.0f}, {11.5f, 0.6f, 5.5f},
        {15.6f, 3.6f, 2.6f},  {19.0f, 0.6f, 6.0f},  {23.0f, 0.6f, 3.0f},  {8.0f, 4.6f, 13.0f}, {8.6f, 6.4f, 13.0f},
        {14.5f, 3.6f, 15.5f}, {15.0f, 4.6f, 10.0f}, {19.5f, 2.6f, 10.5f}, {7.0f, 2.6f, 17.0f},
};

// the height of a point of the look: the rounded rectangle of a terrace holds the point
static bool slab_has(const slab_t *s, float x, float z)
{
    if (x < s->x0 || x > s->x1 || z < s->z0 || z > s->z1) {
        return false;
    }
    float rc = s->rc;
    float cx = clampf(x, s->x0 + rc, s->x1 - rc), cz = clampf(z, s->z0 + rc, s->z1 - rc);
    float dx = x - cx, dz = z - cz;
    return dx * dx + dz * dz <= rc * rc;
}
static void world_grid(void)
{
    for (int i = 0; i < GX; i++) {
        for (int j = 0; j < GZ; j++) {
            int h = WATER;
            for (int k = 0; k < NSLAB; k++) {
                if (slab_has(&SLABS[k], i + 0.5f, j + 0.5f) && SLABS[k].y1 > h) {
                    h = (int)SLABS[k].y1;
                }
            }
            s_H[i][j] = (int8_t)h;
            s_nstk[i][j] = 0;
        }
    }
    // the hut on the meadow: a block of 2 over the meadow
    for (int i = 14; i <= 15; i++) {
        for (int j = 9; j <= 10; j++) {
            s_H[i][j] = 4;
        }
    }
    // the lighthouse column
    for (int i = 17; i <= 18; i++) {
        for (int j = 16; j <= 17; j++) {
            s_H[i][j] = LH_TOP;
        }
    }
    s_nrc = 0;
    s_rc[s_nrc++] = (rcol_t){LH_X, LH_Z, 0.62f, LH_TOP - 0.1f, LH_TOP + 1.6f};  // the lamp room
    s_rc[s_nrc++] = (rcol_t){LH_X, LH_Z, 1.18f, 2.5f, LH_TOP - 0.05f};          // the tower
}
static inline bool cell_in(int i, int j)
{
    return i >= 0 && j >= 0 && i < GX && j < GZ;
}
// the top of a cell (the ground + the stack); the sea: very low
static inline float cell_top(int i, int j)
{
    if (!cell_in(i, j)) {
        return -20;
    }
    if (s_H[i][j] == WATER) {
        return -20;
    }
    return s_H[i][j] + s_nstk[i][j];
}
static inline bool cell_water(int i, int j)
{
    return !cell_in(i, j) || s_H[i][j] == WATER;
}

// ---------------------------------------------------------------- the props of the look
// a palm tree at (x, z) on the ground y, the lean (dx, dz) of its top
static void prop_palm(float x, float y, float z, float lx, float lz, float hgt, float seed)
{
    v3 pts[4];
    float rr[4];
    for (int i = 0; i < 4; i++) {
        float t = i / 3.0f;
        float bend = t * t;
        pts[i] = V3(x + lx * bend * hgt, y + t * hgt, z + lz * bend * hgt);
        rr[i] = 0.17f - 0.06f * t;
    }
    prim_tube(pts, rr, s_blod ? 3 : 4, s_blod ? 4 : 5, 0xB98457, 0xD7A774, false);
    // the rings of the trunk
    v3 top = pts[3];
    s_bsway = 0.6f;
    s_bmat = MT_LEAF;
    for (int l = 0; l < 5; l++) {
        float a = TAU_F * l / 5 + seed;
        v3 dir = V3(fcos(a), 0, fsin(a));
        v3 side = V3(-dir.z, 0, dir.x);
        v3 lp[4];
        float hw[4];
        int nk = s_blod ? 3 : 4;
        for (int k = 0; k < nk; k++) {
            float t = k / (float)(nk - 1);
            float len = 1.55f + 0.25f * fsin(seed * 3 + l);
            lp[k] = V3(top.x + dir.x * t * len, top.y + 0.25f * t - 0.95f * t * t * t, top.z + dir.z * t * len);
            hw[k] = 0.30f * fsin(PI_F * (0.15f + 0.85f * t)) + 0.02f;
        }
        prim_leaf(lp, hw, nk, side, 0x2FC85E, 0xA8EE52);
    }
    s_bmat = MT_PLASTIC;
    s_bsway = 0.25f;
    if (!s_blod) {
        prim_ell(V3(top.x + 0.12f, top.y - 0.12f, top.z), V3(0.15f, 0.15f, 0.15f), 2, 5, 0x8A5A3A, 0x5A3A2A);
    }
    s_bmat = MT_MATTE;
    s_bsway = 0;
    if (!s_blod && s_nrc < RMAX) {
        s_rc[s_nrc++] = (rcol_t){x, z, 0.22f, y - 0.5f, y + hgt};
    }
}
// a bush: a few round blobs
static void prop_bush(float x, float y, float z, float s)
{
    s_bsway = 0.12f;
    s_bmat = MT_LEAF;
    prim_ell(V3(x, y + 0.28f * s, z), V3(0.5f * s, 0.38f * s, 0.45f * s), s_blod ? 2 : 3, s_blod ? 4 : 5, 0x8CEB5E,
             0x2E9E4E);
    if (!s_blod) {
        prim_ell(V3(x + 0.35f * s, y + 0.2f * s, z + 0.15f * s), V3(0.32f * s, 0.26f * s, 0.3f * s), 2, 5, 0xA4F068,
                 0x34A250);
    }
    s_bsway = 0;
    s_bmat = MT_MATTE;
}
// a flower on the grass: a flat star of petals and a middle
static void prop_flower(float x, float y, float z, uint32_t petal, float s, float rot)
{
    float cp[3], cm[3];
    col3(cp, petal);
    col3(cm, 0xFFE24A);
    y += 0.03f;
    int m = mb_v(V3(x, y + 0.02f, z), V3(0, 1, 0), cm, 1);
    int b0 = s_bnv;
    for (int i = 0; i < 10; i++) {
        float a = rot + TAU_F * i / 10;
        float r = (i & 1) ? 0.09f * s : 0.24f * s;
        mb_v(V3(x + fcos(a) * r, y + ((i & 1) ? 0.0f : 0.04f), z + fsin(a) * r), V3(0, 1, 0), (i & 1) ? cm : cp, 1);
    }
    for (int i = 0; i < 10; i++) {
        mb_t(m, b0 + i, b0 + (i + 1) % 10);
    }
}
// a round beach stone
static void prop_stone(float x, float y, float z, float s, uint32_t c0, uint32_t c1)
{
    prim_ell(V3(x, y + 0.12f * s, z), V3(0.45f * s, 0.3f * s, 0.38f * s), s_blod ? 2 : 3, s_blod ? 4 : 5, c0, c1);
}
// the lighthouse on the hill: stripes, the gallery, the lamp room, the dome
static void prop_lighthouse(void)
{
    const float y0 = 3.0f, y1 = LH_TOP;
    int bands = 7;
    for (int b = 0; b < bands; b++) {
        float ya = y0 + (y1 - 0.25f - y0) * b / bands, yb = y0 + (y1 - 0.25f - y0) * (b + 1) / bands;
        float ra = 1.25f - 0.32f * (ya - y0) / (y1 - y0), rb = 1.25f - 0.32f * (yb - y0) / (y1 - y0);
        uint32_t c = (b & 1) ? 0xFF4A5C : 0xFAF7F2;
        uint32_t c2 = (b & 1) ? 0xE8344C : 0xE9E4F4;
        s_bmat = MT_PLASTIC;
        prim_cyl(V3(LH_X, 0, LH_Z), ya, yb, ra, rb, s_blod ? 8 : 10, 0, c2, c, c);
    }
    // the gallery: a flat disc at the top with a dark rim
    s_bmat = MT_METAL;
    prim_cyl(V3(LH_X, 0, LH_Z), y1 - 0.25f, y1, 1.45f, 1.45f, 12, 2, 0x2B6C88, 0x3E8AA8, 0xB9C6D6);
    // the railing posts
    for (int i = 0; i < 8; i += s_blod ? 2 : 1) {
        float a = TAU_F * i / 8;
        prim_cyl(V3(LH_X + fcos(a) * 1.36f, 0, LH_Z + fsin(a) * 1.36f), y1, y1 + 0.42f, 0.04f, 0.04f, 3, 0, 0x2B6C88,
                 0x4FA0C0, 0);
    }
    v3 rp[15];
    float rr[21];
    for (int i = 0; i <= 10; i++) {
        float a = TAU_F * i / 10;
        rp[i] = V3(LH_X + fcos(a) * 1.36f, y1 + 0.42f, LH_Z + fsin(a) * 1.36f);
        rr[i] = 0.045f;
    }
    prim_tube(rp, rr, 11, 3, 0x4FA0C0, 0x4FA0C0, false);
    // the lamp room and the dome
    s_bmat = MT_GLOW;
    prim_cyl(V3(LH_X, 0, LH_Z), y1, y1 + 1.0f, 0.58f, 0.58f, 8, 0, 0xFFE9A0, 0xFFF6CF, 0);
    s_bmat = MT_PLASTIC;
    prim_cyl(V3(LH_X, 0, LH_Z), y1 + 1.0f, y1 + 1.12f, 0.66f, 0.66f, 8, 0, 0x2B6C88, 0x2B6C88, 0);
    prim_ell(V3(LH_X, y1 + 1.12f, LH_Z), V3(0.62f, 0.5f, 0.62f), s_blod ? 2 : 3, 8, 0xFF5A6A, 0xD83A50);
    prim_cyl(V3(LH_X, 0, LH_Z), y1 + 1.55f, y1 + 1.9f, 0.05f, 0.02f, 4, 0, 0x2B6C88, 0x2B6C88, 0);
    s_bmat = MT_MATTE;
}
// a small beach hut, 2 x 2 cells on the ground y0, with a flat roof at y0 + 2 (the robot can stand on it)
static void prop_hut(float x0, float z0, float y0)
{
    s_bmat = MT_MATTE;
    prim_cbox(V3(x0 + 1.0f, y0 + 0.9f, z0 + 1.0f), V3(0.96f, 0.9f, 0.96f), 0.08f, 0x5FC8E8);
    prim_box(V3(x0 + 1.0f, y0 + 0.62f, z0 - 0.0f), V3(0.32f, 0.55f, 0.05f), 0x2E6E9E);
    prim_box(V3(x0 + 0.35f, y0 + 1.15f, z0 + 2.0f), V3(0.25f, 0.2f, 0.05f), 0xFFF4C8);
    prim_box(V3(x0 + 1.65f, y0 + 1.15f, z0 + 2.0f), V3(0.25f, 0.2f, 0.05f), 0xFFF4C8);
    s_bmat = MT_PLASTIC;
    prim_cbox(V3(x0 + 1.0f, y0 + 1.9f, z0 + 1.0f), V3(1.12f, 0.1f, 1.12f), 0.05f, 0xFF7A4A);
    // the stripes of the awning over the door
    for (int k = 0; k < 4; k++) {
        prim_box(V3(x0 + 0.55f + k * 0.3f, y0 + 1.42f, z0 - 0.2f), V3(0.15f, 0.04f, 0.22f),
                 (k & 1) ? 0xFFFFFF : 0xFF4A5C);
    }
    s_bmat = MT_MATTE;
}
// a dock of planks into the sea at the east
static void prop_dock(void)
{
    s_bmat = MT_MATTE;
    for (int k = 0; k < 6; k++) {
        float z = 3.2f + k * 0.42f;
        prim_box(V3(24.8f, -0.06f, z), V3(1.3f, 0.06f, 0.18f), (k & 1) ? 0xC88A5A : 0xDB9C68);
    }
    for (int k = 0; k < 2; k++) {
        for (int s = 0; s < 2; s++) {
            prim_cyl(V3(23.7f + 2.2f * k, 0, 3.1f + 2.3f * s), SEA_Y - 0.3f, 0.25f, 0.1f, 0.1f, 6, 2, 0x8A5A3A,
                     0xA8724A, 0xC88A5A);
        }
    }
}

// the foam at the waterline round a terrace (an added light)
static void foam_ring(const slab_t *s)
{
    float cw[3] = {0.85f, 0, 0}, cz[3] = {0, 0, 0};
    const int N = 48;
    int b0 = s_bnv;
    for (int i = 0; i <= N; i++) {
        float a = TAU_F * i / N;
        float cx = (s->x0 + s->x1) * 0.5f, czz = (s->z0 + s->z1) * 0.5f;
        float hx = (s->x1 - s->x0) * 0.5f, hz = (s->z1 - s->z0) * 0.5f;
        // (a point of the outline: from the middle along the angle to the rounded rectangle)
        float dx = fcos(a), dz = fsin(a);
        float lo = 0, hi = hx + hz;
        for (int it = 0; it < 18; it++) {
            float m = (lo + hi) * 0.5f;
            if (slab_has(s, cx + dx * m, czz + dz * m)) {
                lo = m;
            } else {
                hi = m;
            }
        }
        float px = cx + dx * lo, pz = czz + dz * lo;
        float ox = dx, oz = dz;
        mb_v(V3(px - ox * 0.02f, SEA_Y + 0.03f, pz - oz * 0.02f), V3(0, 1, 0), cw, 1);
        mb_v(V3(px + ox * 0.5f, SEA_Y + 0.02f, pz + oz * 0.5f), V3(0, 1, 0), cz, 1);
    }
    (void)cz;
    for (int i = 0; i < N; i++) {
        int a = b0 + i * 2;
        mb_q(a, a + 1, a + 3, a + 2);
    }
}
// the shelf of shallow water round a terrace: sand under clear water near the shore, the sea colour at the outside
static void shelf_ring(const slab_t *s, float width)
{
    float ci[3] = {0.50f, 0.93f, 0.86f}, co[3] = {0.50f, 0.93f, 0.86f};
    const int N = 48;
    int b0 = s_bnv;
    float cx = (s->x0 + s->x1) * 0.5f, czz = (s->z0 + s->z1) * 0.5f;
    float hx = (s->x1 - s->x0) * 0.5f, hz = (s->z1 - s->z0) * 0.5f;
    for (int i = 0; i <= N; i++) {
        float a = TAU_F * i / N;
        float dx = fcos(a), dz = fsin(a);
        float lo = 0, hi = hx + hz;
        for (int it = 0; it < 18; it++) {
            float m = (lo + hi) * 0.5f;
            if (slab_has(s, cx + dx * m, czz + dz * m)) {
                lo = m;
            } else {
                hi = m;
            }
        }
        float px = cx + dx * lo, pz = czz + dz * lo;
        s_bsway = 0.05f;  // (the inner corners: a little of the sea colour)
        mb_v(V3(px, SEA_Y - 0.01f, pz), V3(0, 1, 0), ci, 1);
        s_bsway = 1.0f;  // (the outer corners: all of the sea colour)
        mb_v(V3(px + dx * width, SEA_Y - 0.01f, pz + dz * width), V3(0, 1, 0), co, 1);
    }
    s_bsway = 0;
    for (int i = 0; i < N; i++) {
        int a = b0 + i * 2;
        mb_q(a, a + 2, a + 3, a + 1);
    }
}

// all of the island into the world chunks
static void world_build(void)
{
    s_wnv = s_wnt = s_nch = 0;
    world_grid();
    mb_begin();
    // the shelf of shallow water round the beach and the meadow
    shelf_ring(&SLABS[0], 2.6f);
    world_add(CK_SHELF);
    shelf_ring(&SLABS[1], 2.0f);
    world_add(CK_SHELF);
    for (int k = 0; k < NSLAB; k++) {
        const slab_t *s = &SLABS[k];
        prim_slab(s->x0, s->z0, s->x1, s->z1, s->rc, s->y0, s->y1, &LOOKS[s->look]);
        world_add(CK_SOLID);
    }
    foam_ring(&SLABS[0]);
    world_add(CK_FOAM);
    foam_ring(&SLABS[1]);
    world_add(CK_FOAM);
    // (the props: each one alone in a fine and a coarse version; the order of a frame takes one of them by its distance)
    for (int lod = 0; lod < 2; lod++) {
        s_blod = lod;
        s_wlod = lod ? LOD_COARSE : LOD_FINE;
        prop_lighthouse();
        world_add(CK_SOLID);
        static const float PALM[4][7] = {{2.6f, 0, 3.2f, 0.25f, -0.15f, 3.2f, 0.4f},
                                         {9.2f, 0, 2.0f, -0.2f, -0.3f, 3.6f, 1.7f},
                                         {22.8f, 0, 7.6f, 0.3f, 0.1f, 3.4f, 2.9f},
                                         {4.6f, 2, 17.6f, -0.2f, 0.25f, 3.0f, 0.9f}};
        for (int k = 0; k < 4; k++) {
            const float *p = PALM[k];
            prop_palm(p[0], p[1], p[2], p[3], p[4], p[5], p[6]);
            world_add(CK_SOLID);
        }
        static const float BUSH[5][4] = {{12.6f, 2, 9.4f, 1.0f},
                                         {20.2f, 2, 12.6f, 1.2f},
                                         {4.4f, 2, 13.0f, 0.9f},
                                         {15.8f, 3, 18.2f, 0.8f},
                                         {19.0f, 0, 7.8f, 0.8f}};
        for (int k = 0; k < 5; k++) {
            prop_bush(BUSH[k][0], BUSH[k][1], BUSH[k][2], BUSH[k][3]);
            world_add(CK_SOLID);
        }
        static const float STONE[4][4] = {
                {13.8f, 0, 1.6f, 1.0f}, {19.6f, 0, 2.0f, 0.8f}, {24.2f, 0, 7.2f, 1.2f}, {1.8f, 0, 6.0f, 0.9f}};
        static const uint32_t STC[4][2] = {
                {0xFFB9A0, 0xD98A8A}, {0xF8C8B8, 0xC88AA0}, {0xFFB9A0, 0xC07A90}, {0xF8C8B8, 0xC88AA0}};
        for (int k = 0; k < 4; k++) {
            prop_stone(STONE[k][0], STONE[k][1], STONE[k][2], STONE[k][3], STC[k][0], STC[k][1]);
            world_add(CK_SOLID);
        }
    }
    s_blod = 0;
    s_wlod = LOD_FINE;
    static const uint32_t PET[4] = {0xFF6FA8, 0xFFFFFF, 0xFFD23A, 0xFF8A5A};
    for (int i = 0; i < 26; i++) {
        float x = 4.2f + hash2(i * 3.1f, 1.7f) * 16.5f, z = 8.8f + hash2(2.3f, i * 1.9f) * 10.0f;
        int ci = (int)x, cj = (int)z;
        if (!cell_in(ci, cj) || s_H[ci][cj] != 2) {
            continue;
        }
        if (absf(x - 6.5f) < 1.2f && absf(z - 11.5f) < 1.2f) {
            continue;
        }
        prop_flower(x, 2.0f, z, PET[i & 3], 0.9f + hash2(i, 7) * 0.5f, hash2(i, 3) * 6);
        world_add(CK_SOLID);
    }
    s_wlod = LOD_ALL;
    prop_hut(14.0f, 9.0f, 2.0f);
    prop_dock();
    world_add(CK_SOLID);
}
