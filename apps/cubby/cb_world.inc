// CUBBY: the island SUNDROP ISLE (included by app_cubby.c; round 3: a big island with puzzles that need ideas). The
// look is a set of rounded terraces and props; the
// physics uses a grid of cells (1 unit, the size of a cube): the height of the ground in each cell, the stack of
// cubes on it, the round colliders (the lamp room, the palm trunks, the trees). x goes to the east, z to the north,
// y up.
//
// THE SIX SUNDROPS (each one needs its own idea; the jumps: a hop = a step of 1, a hop + the double jump = 2, the big
// jump + the double jump = 3, never 4):
// 1 THE CRATES (the beach): a stack of crates 4 high. A TIN cube is a step: the cube + the big and the double jump.
// 2 THE OLD ORCHARD (the north-west): high stone walls; the gate opens only while its PLATE is down. The robot on
//   the plate opens it, but the gate closes before he gets there: a cube on the plate holds it open. Inside: the tree
//   house is 6 high: a SPRING cube (4.6) + the double jump.
// 3 THE VAULT (the east): the door opens only while BOTH plates are down. Near it: balloons and springs, no cans. A
//   balloon cube is too light for a plate; a spring cube is heavy: one spring cube on each plate.
// 4 THE SEA STACK (the north): a rock in the sea, too far for a jump from the bluff. A balloon cube that the robot
//   drops in the air stays there: a step in the air half way.
// 5 THE NEEDLE (the middle): a spire 10 over the meadow. A balloon cube lifts the robot 5; from the ground that is
//   not enough (by half a unit): the balloon cube on a TIN cube starts 1 higher.
// 6 THE LIGHTHOUSE (the headland in the north-east, over a wide channel): the DRAWBRIDGE is down only while its plate
//   is down (a cube on it); a SPRING cube that the robot carries over, on the roof of the house of the keeper, and the
//   double jump at the top of the spring: the gallery.
// 30 sparks: on the ways, on the tops, and some in the air (a double jump or a lift gets them).

#define GX      64
#define START_X 33.5f  // the start: on the beach, the face to the north
#define START_Z 7.5f
#define GZ      60
#define WATER   (-9)
#define STK     8
static int8_t s_H[GX][GZ];          // the ground of each cell (units); WATER = the sea
static uint8_t s_stk[GX][GZ][STK];  // the cubes on a cell (CB_* kind), from the bottom
static uint8_t s_nstk[GX][GZ];
static uint8_t s_cf[GX][GZ];  // the flags of a cell (CF_*)
static int8_t s_mc[GX][GZ];   // the machine of a cell + 1 (0: none)
enum {
    CF_NOPLACE = 1,  // no cube on this cell (the crates, the tower, a gate)
    CF_NOSAFE = 2,   // not a safe place for a fall into the sea (the sea stack: the robot comes back on the bluff)
    CF_PLATE = 4     // a plate (s_plate)
};

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
    LK_CLIFF,
    LK_STONE,
    LK_STEP,
    LK_NEEDLE,
    LK_WOOD,
    LK_GOLD
};
// (round 3: soft natural colours; the robot, the cubes, the piles and the goals keep the strong colours: they are the
// things of the player)
static const slab_look_t LOOKS[] = {
        // top, top2, lip, wall high, wall low, band
        [LK_SAND] = {0xE6D3A3, 0xDAC690, 0xF0E2BE, 0xC8A47C, 0x96785E, 0xB49272},
        [LK_GRASS] = {0x74B45A, 0x8EC066, 0xA8CE7E, 0xB2906A, 0x7C6656, 0x9C7E62},
        [LK_CLIFF] = {0x7EAC5E, 0x94BA6A, 0xA6C27E, 0xA89884, 0x6E6660, 0x8C8072},
        [LK_STONE] = {0xB6B0A2, 0xAAA496, 0xCAC4B6, 0xA69E90, 0x7A726A, 0x928A7C},
        [LK_STEP] = {0xD8B48A, 0xCCA67E, 0xE6C8A0, 0xB88C68, 0x8A6A56, 0xA07C60},
        [LK_NEEDLE] = {0x9CAAB2, 0x8E9CA6, 0xB2BEC6, 0x9098A8, 0x606676, 0x788092},
        [LK_WOOD] = {0xC89E6C, 0xB88E60, 0xD8B080, 0x8C6648, 0x604634, 0x76563E},
        [LK_GOLD] = {0xE8CC88, 0xDCBE78, 0xF4DEA4, 0xC8A060, 0x987448, 0xB08A54},
};
static const slab_t SLABS[] = {
        // the coast: the beach, the two meadows, the headland (the landing of the bridge, its step, its top)
        {6, 3, 58, 15, 4.0f, SEA_Y - 0.15f, 0, LK_SAND},   // 0 the south beach
        {8, 12, 40, 46, 5.0f, -0.6f, 2, LK_GRASS},         // 1 the west meadow
        {38, 12, 57, 37, 4.0f, -0.6f, 2, LK_GRASS},        // 2 the east meadow
        {46, 47.5f, 61, 58, 3.0f, -0.6f, 6, LK_CLIFF},     // 3 the top of the headland
        {47, 44, 53, 48, 1.0f, -0.6f, 2, LK_SAND},         // 4 the landing of the bridge
        {52.5f, 44, 58.5f, 48, 1.0f, -0.6f, 4, LK_CLIFF},  // 5 the step of the headland
        // the steps from the beach up to the meadow
        {26, 10, 31, 12.5f, 0.7f, -0.2f, 1, LK_STEP},
        {40, 10, 45, 12.5f, 0.7f, -0.2f, 1, LK_STEP},
        // the orchard: the walls (the gate in the south wall at x 15..16), the tree house
        {9, 30, 15, 31, 0.25f, 1.5f, 13, LK_STONE},
        {17, 30, 23, 31, 0.25f, 1.5f, 13, LK_STONE},
        {9, 44, 23, 45, 0.25f, 1.5f, 13, LK_STONE},
        {9, 30, 10, 45, 0.25f, 1.5f, 13, LK_STONE},
        {22, 30, 23, 45, 0.25f, 1.5f, 13, LK_STONE},
        {18, 40, 20, 42, 0.3f, 1.5f, 8, LK_WOOD},
        // the vault: the walls (the door in the west wall at z 24..25), the pedestal
        {47, 20, 48, 24, 0.25f, 1.5f, 13, LK_STONE},
        {47, 26, 48, 29, 0.25f, 1.5f, 13, LK_STONE},
        {54, 20, 55, 29, 0.25f, 1.5f, 13, LK_STONE},
        {47, 20, 55, 21, 0.25f, 1.5f, 13, LK_STONE},
        {47, 28, 55, 29, 0.25f, 1.5f, 13, LK_STONE},
        {51, 24, 53, 26, 0.3f, 1.5f, 3, LK_GOLD},
        // the needle; the north bluff and its step; the sea stack
        {34, 26, 36, 28, 0.5f, 1.5f, 12, LK_NEEDLE},
        {24, 37, 35, 47.5f, 2.0f, -0.6f, 6, LK_CLIFF},
        {25, 34.5f, 31, 37.5f, 0.8f, 1.5f, 4, LK_CLIFF},
        {29, 54, 31, 56, 0.7f, SEA_Y - 1.0f, 7, LK_NEEDLE},
};
#define NSLAB (int)(sizeof SLABS / sizeof SLABS[0])
// (the coast terraces: the shelf of shallow water and the foam round them)
static const uint8_t COAST[] = {0, 1, 2, 3, 4, 5, 21, 23};
#define NCOAST (int)(sizeof COAST)

// the lighthouse (goal 6): a 2 x 2 column of cells on the headland, the gallery at its top, the lamp room in the middle
#define LH_X   54.0f
#define LH_Z   53.0f
#define LH_Y0  6.0f
#define LH_TOP 15
// the house of the keeper: 2 x 2 cells west of the tower, the flat roof at 9
#define KH_I 50
#define KH_J 52
#define KH_Y 9
// the crates on the beach (goal 1): 2 x 2 cells, 4 high
#define CR_I 12
#define CR_J 6
#define CR_Y 4
// the round colliders: centre, radius, bottom, top
typedef struct {
    float x, z, r, y0, y1;
} rcol_t;
#define RMAX 24
static rcol_t s_rc[RMAX];
static int s_nrc;

// the junk piles: the kind of cube that they make
typedef struct {
    float x, z;
    uint8_t kind;
    float y;
    float pulse;
} pile_t;
#define NPILE 9
static pile_t s_pile[NPILE] = {
        {19.5f, 6.5f, CB_TIN, 0, 0},      // the beach (THE CRATES)
        {19.5f, 24.5f, CB_TIN, 0, 0},     // before the orchard (its plate)
        {12.5f, 33.5f, CB_SPRING, 0, 0},  // in the orchard (the tree house)
        {39.5f, 19.5f, CB_SPRING, 0, 0},  // west of the vault
        {39.5f, 29.5f, CB_FLOAT, 0, 0},   // between the vault and the needle
        {32.5f, 44.5f, CB_FLOAT, 0, 0},   // on the bluff (the sea stack)
        {30.5f, 24.5f, CB_TIN, 0, 0},     // near the needle
        {43.5f, 33.5f, CB_TIN, 0, 0},     // near the plate of the bridge
        {52.5f, 33.5f, CB_SPRING, 0, 0},  // near the bridge (the lighthouse)
};

// the machines: a gate, a door, a drawbridge. Each one is open only while all of its plates are down (a cube on the
// plate or a robot on it; a balloon cube is too light). The cells of a machine have its height (h, from hc closed to
// ho open); the bridge is water until it is all the way down
enum {
    MK_GATE,
    MK_DOOR,
    MK_BRIDGE
};
typedef struct {
    uint8_t i, j;
    bool on;     // down now
    float push;  // the look: 0 up .. 1 down
} plate_t;
#define NPLATE 4
static plate_t s_plate[NPLATE] = {{15, 26}, {43, 21}, {43, 28}, {46, 33}};
typedef struct {
    uint8_t kind, i0, j0, i1, j1;
    int8_t ho, hc;  // the height open, closed
    uint8_t np, pl[2];
    float speed;  // units a second
    float h;      // the height now
    bool open;    // all of its plates are down
} mech_t;
#define NMECH 3
static mech_t s_mech[NMECH] = {
        {MK_GATE, 15, 30, 16, 30, 2, 13, 1, {0, 0}, 9.0f, 13, false},    // the gate of the orchard
        {MK_DOOR, 47, 24, 47, 25, 2, 13, 2, {1, 2}, 6.0f, 13, false},    // the door of the vault
        {MK_BRIDGE, 48, 37, 49, 43, 2, -2, 1, {3, 0}, 2.2f, -2, false},  // the drawbridge
};

// the six SUNDROPS (the goals), their names, and the sparks (the small finds)
typedef struct {
    v3 p;
    bool got;
    float t;
} drop_t;
#define NDROP 6
static drop_t s_drop[NDROP];
static const float DROP_XYZ[NDROP][3] = {
        {13.0f, CR_Y + 0.75f, 7.0f},      // THE CRATES
        {19.0f, 8.75f, 41.0f},            // THE OLD ORCHARD (the tree house)
        {52.0f, 3.75f, 25.0f},            // THE VAULT (the pedestal)
        {30.0f, 7.75f, 55.0f},            // THE SEA STACK
        {35.0f, 12.75f, 27.0f},           // THE NEEDLE
        {54.0f, LH_TOP + 0.75f, 51.95f},  // THE LIGHTHOUSE (the gallery)
};
static const char *const DROP_NAME[NDROP] = {"THE CRATES",    "THE OLD ORCHARD", "THE VAULT",
                                             "THE SEA STACK", "THE NEEDLE",      "THE LIGHTHOUSE"};
#define NSPARK 30
static drop_t s_spark[NSPARK];
static const float SPARK_XYZ[NSPARK][3] = {
        // the beach and the steps
        {16.0f, 0.6f, 5.0f},
        {23.0f, 0.6f, 9.5f},
        {28.0f, 0.6f, 5.0f},
        {37.0f, 0.6f, 6.0f},
        {44.0f, 0.6f, 8.0f},
        {51.0f, 0.6f, 6.0f},
        {28.5f, 1.6f, 11.2f},
        // the meadows
        {20.0f, 2.6f, 18.0f},
        {26.0f, 2.6f, 22.0f},
        {44.0f, 2.6f, 16.0f},
        {51.0f, 2.6f, 17.0f},
        // the orchard (one in the arc of the spring)
        {12.0f, 2.6f, 38.0f},
        {16.0f, 2.6f, 36.0f},
        {20.5f, 2.6f, 33.0f},
        {14.5f, 5.2f, 41.5f},
        // the vault (one over the plates)
        {49.5f, 2.6f, 22.5f},
        {49.5f, 2.6f, 26.5f},
        {43.5f, 3.6f, 24.5f},
        // the needle (in the air: a lift gets them)
        {33.2f, 6.6f, 26.5f},
        {34.6f, 9.6f, 25.3f},
        // the bluff and the way to the sea stack (one over the sea)
        {27.5f, 4.6f, 36.0f},
        {29.0f, 6.6f, 42.0f},
        {33.0f, 6.6f, 45.5f},
        {30.0f, 7.4f, 51.0f},
        // the bridge and the headland
        {48.9f, 2.6f, 40.5f},
        {50.0f, 2.6f, 46.0f},
        {55.5f, 4.6f, 46.0f},
        {48.5f, 6.6f, 53.5f},
        {58.0f, 6.6f, 54.0f},
        {51.0f, KH_Y + 0.6f, 53.0f},
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
static void cells_set(int i0, int j0, int i1, int j1, int h, uint8_t flags)
{
    for (int i = i0; i <= i1; i++) {
        for (int j = j0; j <= j1; j++) {
            if (h != -100) {
                s_H[i][j] = (int8_t)h;
            }
            s_cf[i][j] |= flags;
        }
    }
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
            s_cf[i][j] = 0;
            s_mc[i][j] = 0;
        }
    }
    cells_set(CR_I, CR_J, CR_I + 1, CR_J + 1, CR_Y, CF_NOPLACE);  // the crates
    cells_set(53, 52, 54, 53, LH_TOP, CF_NOPLACE);                // the lighthouse column
    cells_set(KH_I, KH_J, KH_I + 1, KH_J + 1, KH_Y, 0);           // the house of the keeper
    cells_set(29, 54, 30, 55, -100, CF_NOSAFE);                   // the sea stack
    for (int m = 0; m < NMECH; m++) {
        mech_t *q = &s_mech[m];
        cells_set(q->i0, q->j0, q->i1, q->j1, q->kind == MK_BRIDGE ? WATER : q->ho, CF_NOPLACE);
        for (int i = q->i0; i <= q->i1; i++) {
            for (int j = q->j0; j <= q->j1; j++) {
                s_mc[i][j] = (int8_t)(m + 1);
            }
        }
        q->h = q->hc;
        q->open = false;
    }
    for (int p = 0; p < NPLATE; p++) {
        s_cf[s_plate[p].i][s_plate[p].j] |= CF_PLATE;
        s_plate[p].on = false;
        s_plate[p].push = 0;
    }
    s_nrc = 0;
    s_rc[s_nrc++] = (rcol_t){LH_X, LH_Z, 0.62f, LH_TOP - 0.1f, LH_TOP + 1.6f};  // the lamp room
    s_rc[s_nrc++] = (rcol_t){LH_X, LH_Z, 1.18f, LH_Y0 + 0.5f, LH_TOP - 0.05f};  // the tower
}
static inline bool cell_in(int i, int j)
{
    return i >= 0 && j >= 0 && i < GX && j < GZ;
}
// the top of a cell (the ground + the stack, or the machine); the sea: very low
static inline float cell_top(int i, int j)
{
    if (!cell_in(i, j)) {
        return -20;
    }
    if (s_mc[i][j]) {
        const mech_t *q = &s_mech[s_mc[i][j] - 1];
        if (q->kind == MK_BRIDGE) {
            return q->h > q->ho - 0.05f ? q->ho : -20;
        }
        return q->h;
    }
    if (s_H[i][j] == WATER) {
        return -20;
    }
    return s_H[i][j] + s_nstk[i][j];
}
static inline bool cell_water(int i, int j)
{
    if (!cell_in(i, j)) {
        return true;
    }
    if (s_mc[i][j]) {
        const mech_t *q = &s_mech[s_mc[i][j] - 1];
        return q->kind == MK_BRIDGE && q->h <= q->ho - 0.05f;
    }
    return s_H[i][j] == WATER;
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
    prim_tube(pts, rr, s_blod ? 3 : 4, s_blod ? 4 : 5, 0xA88660, 0xC4A27A, false);
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
        prim_leaf(lp, hw, nk, side, 0x4C9A50, 0x9CC468);
    }
    s_bmat = MT_PLASTIC;
    s_bsway = 0.25f;
    if (!s_blod) {
        prim_ell(V3(top.x + 0.12f, top.y - 0.12f, top.z), V3(0.15f, 0.15f, 0.15f), 2, 5, 0x7A5A40, 0x5A3E2E);
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
    prim_ell(V3(x, y + 0.28f * s, z), V3(0.5f * s, 0.38f * s, 0.45f * s), s_blod ? 2 : 3, s_blod ? 4 : 5, 0x96C672,
             0x467E4A);
    if (!s_blod) {
        prim_ell(V3(x + 0.35f * s, y + 0.2f * s, z + 0.15f * s), V3(0.32f * s, 0.26f * s, 0.3f * s), 2, 5, 0xA6CE7C,
                 0x4C8650);
    }
    s_bsway = 0;
    s_bmat = MT_MATTE;
}
// a fruit tree of the orchard: a trunk and a round crown with a few fruits (a round collider for the trunk)
static void prop_fruit_tree(float x, float y, float z, float s, uint32_t fruit)
{
    v3 pts[3] = {V3(x, y, z), V3(x + 0.05f, y + 1.0f * s, z), V3(x - 0.05f, y + 1.7f * s, z + 0.05f)};
    float rr[3] = {0.16f * s, 0.12f * s, 0.1f * s};
    prim_tube(pts, rr, 3, s_blod ? 4 : 5, 0x7A5A40, 0x94704E, false);
    s_bsway = 0.1f;
    s_bmat = MT_LEAF;
    prim_ell(V3(x, y + 2.2f * s, z), V3(0.95f * s, 0.75f * s, 0.95f * s), s_blod ? 2 : 3, s_blod ? 5 : 7, 0x8CC06A,
             0x3E7646);
    s_bsway = 0;
    s_bmat = MT_PLASTIC;
    if (!s_blod) {
        for (int k = 0; k < 5; k++) {
            float a = TAU_F * k / 5 + x, r = 0.82f * s;
            prim_ell(V3(x + fcos(a) * r, y + (2.0f + 0.35f * fsin(a * 3)) * s, z + fsin(a) * r),
                     V3(0.11f, 0.11f, 0.11f), 2, 5, fruit, fruit);
        }
    }
    s_bmat = MT_MATTE;
    if (!s_blod && s_nrc < RMAX) {
        s_rc[s_nrc++] = (rcol_t){x, z, 0.2f * s, y - 0.5f, y + 1.6f * s};
    }
}
// a flower on the grass: a flat star of petals and a middle
static void prop_flower(float x, float y, float z, uint32_t petal, float s, float rot)
{
    float cp[3], cm[3];
    col3(cp, petal);
    col3(cm, 0xF2D25A);
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
// a round stone
static void prop_stone(float x, float y, float z, float s, uint32_t c0, uint32_t c1)
{
    prim_ell(V3(x, y + 0.12f * s, z), V3(0.45f * s, 0.3f * s, 0.38f * s), s_blod ? 2 : 3, s_blod ? 4 : 5, c0, c1);
}
// the lighthouse on the headland: stripes, the gallery, the lamp room, the dome
static void prop_lighthouse(void)
{
    const float y0 = LH_Y0, y1 = LH_TOP;
    int bands = 6;
    for (int b = 0; b < bands; b++) {
        float ya = y0 + (y1 - 0.25f - y0) * b / bands, yb = y0 + (y1 - 0.25f - y0) * (b + 1) / bands;
        float ra = 1.25f - 0.32f * (ya - y0) / (y1 - y0), rb = 1.25f - 0.32f * (yb - y0) / (y1 - y0);
        uint32_t c = (b & 1) ? 0xD2574E : 0xF2EEE6;
        uint32_t c2 = (b & 1) ? 0xB4443E : 0xDCD6D0;
        s_bmat = MT_PLASTIC;
        prim_cyl(V3(LH_X, 0, LH_Z), ya, yb, ra, rb, s_blod ? 8 : 10, 0, c2, c, c);
    }
    // the gallery: a flat disc at the top with a dark rim
    s_bmat = MT_METAL;
    prim_cyl(V3(LH_X, 0, LH_Z), y1 - 0.25f, y1, 1.45f, 1.45f, 12, 2, 0x34586A, 0x4A7488, 0xB4BEC8);
    // the railing posts
    for (int i = 0; i < 8; i += s_blod ? 2 : 1) {
        float a = TAU_F * i / 8;
        prim_cyl(V3(LH_X + fcos(a) * 1.36f, 0, LH_Z + fsin(a) * 1.36f), y1, y1 + 0.42f, 0.04f, 0.04f, 3, 0, 0x34586A,
                 0x56808E, 0);
    }
    v3 rp[15];
    float rr[21];
    for (int i = 0; i <= 10; i++) {
        float a = TAU_F * i / 10;
        rp[i] = V3(LH_X + fcos(a) * 1.36f, y1 + 0.42f, LH_Z + fsin(a) * 1.36f);
        rr[i] = 0.045f;
    }
    prim_tube(rp, rr, 11, 3, 0x56808E, 0x56808E, false);
    // the lamp room and the dome
    s_bmat = MT_GLOW;
    prim_cyl(V3(LH_X, 0, LH_Z), y1, y1 + 1.0f, 0.58f, 0.58f, 8, 0, 0xFFE9A0, 0xFFF6CF, 0);
    s_bmat = MT_PLASTIC;
    prim_cyl(V3(LH_X, 0, LH_Z), y1 + 1.0f, y1 + 1.12f, 0.66f, 0.66f, 8, 0, 0x34586A, 0x34586A, 0);
    prim_ell(V3(LH_X, y1 + 1.12f, LH_Z), V3(0.62f, 0.5f, 0.62f), s_blod ? 2 : 3, 8, 0xD2574E, 0xB4443E);
    prim_cyl(V3(LH_X, 0, LH_Z), y1 + 1.55f, y1 + 1.9f, 0.05f, 0.02f, 4, 0, 0x34586A, 0x34586A, 0);
    s_bmat = MT_MATTE;
}
// a house of 2 x 2 cells on the ground y0 with a flat roof at y0 + h (the robot can stand on it), its door to the
// south
static void prop_house(float x0, float z0, float y0, float h, uint32_t wall, uint32_t roof)
{
    s_bmat = MT_MATTE;
    prim_cbox(V3(x0 + 1.0f, y0 + (h - 0.1f) * 0.5f, z0 + 1.0f), V3(0.96f, (h - 0.1f) * 0.5f, 0.96f), 0.08f, wall);
    prim_box(V3(x0 + 1.0f, y0 + 0.62f, z0 - 0.0f), V3(0.32f, 0.55f, 0.05f), 0x4E6A86);
    prim_box(V3(x0 + 0.35f, y0 + h * 0.6f, z0 + 2.0f), V3(0.25f, 0.2f, 0.05f), 0xF4E8C4);
    prim_box(V3(x0 + 1.65f, y0 + h * 0.6f, z0 + 2.0f), V3(0.25f, 0.2f, 0.05f), 0xF4E8C4);
    s_bmat = MT_PLASTIC;
    prim_cbox(V3(x0 + 1.0f, y0 + h - 0.1f, z0 + 1.0f), V3(1.12f, 0.1f, 1.12f), 0.05f, roof);
    s_bmat = MT_MATTE;
}
// the crates on the beach: two big crates of 2 x 2 x 2 with dark planks round them
static void prop_crates(void)
{
    for (int k = 0; k < 2; k++) {
        float y = k * 2.0f;
        s_bmat = MT_MATTE;
        prim_cbox(V3(CR_I + 1.0f, y + 1.0f, CR_J + 1.0f), V3(0.98f, 0.98f, 0.98f), 0.06f, k ? 0xC8A06C : 0xBC9462);
        for (int s = 0; s < 2; s++) {
            prim_box(V3(CR_I + 1.0f, y + 0.25f + s * 1.5f, CR_J + 1.0f), V3(1.0f, 0.09f, 1.0f), 0x8A6644);
        }
        prim_box(V3(CR_I + 1.0f, y + 1.0f, CR_J - 0.005f), V3(0.09f, 0.98f, 0.03f), 0x8A6644);
    }
}
// a dock of planks into the sea at the south-east
static void prop_dock(void)
{
    s_bmat = MT_MATTE;
    for (int k = 0; k < 7; k++) {
        float x = 47.2f + k * 0.42f;
        prim_box(V3(x, -0.06f, 1.6f), V3(0.18f, 0.06f, 1.4f), (k & 1) ? 0xB08660 : 0xC09470);
    }
    for (int k = 0; k < 2; k++) {
        for (int s = 0; s < 2; s++) {
            prim_cyl(V3(47.1f + 2.7f * k, 0, 0.4f + 2.3f * s), SEA_Y - 0.3f, 0.25f, 0.1f, 0.1f, 6, 2, 0x7A5A40,
                     0x98724E, 0xB08660);
        }
    }
}
// the posts of the gate and the door: two stone columns with a lamp for each plate (the lamps are glows in the frame)
static void prop_gateposts(float x0, float z0, float x1, float z1, float y0, float y1)
{
    s_bmat = MT_MATTE;
    prim_cbox(V3(x0, (y0 + y1) * 0.5f, z0), V3(0.22f, (y1 - y0) * 0.5f, 0.22f), 0.06f, 0x9A9284);
    prim_cbox(V3(x1, (y0 + y1) * 0.5f, z1), V3(0.22f, (y1 - y0) * 0.5f, 0.22f), 0.06f, 0x9A9284);
}

// the foam at the waterline round a terrace (an added light)
static void foam_ring(const slab_t *s)
{
    float cw[3] = {0.7f, 0, 0}, cz[3] = {0, 0, 0};
    const int N = (s->x1 - s->x0) + (s->z1 - s->z0) > 30 ? 48 : 24;  // (the points round it: by its size)
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
    int b0 = s_bnv - (N + 1) * 2;
    for (int i = 0; i < N; i++) {
        int a = b0 + i * 2;
        mb_q(a, a + 1, a + 3, a + 2);
    }
}
// the shelf of shallow water round a terrace: sand under clear water near the shore, the sea colour at the outside
static void shelf_ring(const slab_t *s, float width)
{
    float ci[3] = {0.52f, 0.80f, 0.74f}, co[3] = {0.52f, 0.80f, 0.74f};
    const int N = (s->x1 - s->x0) + (s->z1 - s->z0) > 30 ? 48 : 24;
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
    // the shelf of shallow water round the coast
    for (int k = 0; k < NCOAST; k++) {
        const slab_t *s = &SLABS[COAST[k]];
        shelf_ring(s, COAST[k] == 0 ? 2.6f : 1.8f);
        world_add(CK_SHELF);
    }
    for (int k = 0; k < NSLAB; k++) {
        const slab_t *s = &SLABS[k];
        bool coast = false;
        for (int c = 0; c < NCOAST; c++) {
            coast |= COAST[c] == k;
        }
        if (coast) {
            // (a big terrace: one version, in chunks)
            prim_slab(s->x0, s->z0, s->x1, s->z1, s->rc, s->y0, s->y1, &LOOKS[s->look]);
            world_add(CK_SOLID);
            continue;
        }
        // (a wall, a rock, a step: a near and a far version)
        for (int lod = 0; lod < 2; lod++) {
            s_slab_coarse = lod;
            s_wlod = lod ? LOD_WCOARSE : LOD_WFINE;
            prim_slab(s->x0, s->z0, s->x1, s->z1, s->rc, s->y0, s->y1, &LOOKS[s->look]);
            world_add(CK_SOLID);
        }
        s_slab_coarse = false;
        s_wlod = LOD_ALL;
    }
    for (int k = 0; k < NCOAST; k++) {
        foam_ring(&SLABS[COAST[k]]);
        world_add(CK_FOAM);
    }
    // (the props: each one alone in a fine and a coarse version; the order of a frame takes one of them by its distance)
    for (int lod = 0; lod < 2; lod++) {
        s_blod = lod;
        s_wlod = lod ? LOD_COARSE : LOD_FINE;
        prop_lighthouse();
        world_add(CK_SOLID);
        static const float PALM[9][7] = {
                {8.6f, 0, 9.0f, 0.25f, -0.15f, 3.2f, 0.4f},  {22.0f, 0, 4.2f, -0.2f, -0.3f, 3.6f, 1.7f},
                {36.5f, 0, 4.4f, 0.3f, -0.2f, 3.4f, 2.9f},   {54.0f, 0, 9.5f, 0.3f, 0.1f, 3.0f, 0.9f},
                {44.6f, 0, 4.0f, -0.25f, -0.2f, 3.3f, 2.2f}, {25.0f, 2, 16.0f, -0.2f, 0.25f, 3.0f, 1.1f},
                {34.0f, 6, 40.0f, 0.2f, 0.25f, 3.2f, 0.6f},  {57.5f, 6, 56.0f, 0.25f, 0.2f, 3.0f, 1.4f},
                {55.0f, 2, 34.5f, 0.3f, -0.1f, 3.1f, 2.6f}};
        for (int k = 0; k < 9; k++) {
            const float *p = PALM[k];
            prop_palm(p[0], p[1], p[2], p[3], p[4], p[5], p[6]);
            world_add(CK_SOLID);
        }
        static const float TREE[4][4] = {
                {13.0f, 2, 40.5f, 1.0f}, {16.5f, 2, 34.5f, 0.9f}, {19.5f, 2, 36.5f, 1.05f}, {12.5f, 2, 37.0f, 0.85f}};
        static const uint32_t FRUIT[4] = {0xE8644C, 0xF0B43C, 0xE8644C, 0xF0B43C};
        for (int k = 0; k < 4; k++) {
            prop_fruit_tree(TREE[k][0], TREE[k][1], TREE[k][2], TREE[k][3], FRUIT[k]);
            world_add(CK_SOLID);
        }
        static const float BUSH[12][4] = {{12.6f, 2, 17.4f, 1.0f}, {20.2f, 2, 14.6f, 1.2f}, {32.4f, 2, 20.0f, 0.9f},
                                          {24.8f, 2, 28.2f, 0.8f}, {42.0f, 2, 15.0f, 1.0f}, {56.0f, 2, 24.0f, 0.9f},
                                          {26.0f, 6, 44.8f, 1.1f}, {36.8f, 2, 33.5f, 0.8f}, {14.4f, 2, 21.6f, 1.0f},
                                          {50.8f, 6, 56.4f, 1.0f}, {59.0f, 6, 50.5f, 0.9f}, {11.0f, 2, 26.5f, 0.9f}};
        for (int k = 0; k < 12; k++) {
            prop_bush(BUSH[k][0], BUSH[k][1], BUSH[k][2], BUSH[k][3]);
            world_add(CK_SOLID);
        }
        static const float STONE[8][4] = {{14.0f, 0, 3.6f, 1.0f},  {31.6f, 0, 3.2f, 0.8f},  {56.2f, 0, 7.2f, 1.2f},
                                          {7.8f, 0, 6.0f, 0.9f},   {38.5f, 2, 24.5f, 1.1f}, {45.5f, 2, 30.0f, 0.8f},
                                          {28.0f, 6, 39.5f, 1.0f}, {60.0f, 6, 52.0f, 1.2f}};
        static const uint32_t STC[2][2] = {{0xC8BCA8, 0x9A8E80}, {0xB8B0A4, 0x8A8478}};
        for (int k = 0; k < 8; k++) {
            prop_stone(STONE[k][0], STONE[k][1], STONE[k][2], STONE[k][3], STC[k & 1][0], STC[k & 1][1]);
            world_add(CK_SOLID);
        }
    }
    s_blod = 0;
    s_wlod = LOD_FINE;
    static const uint32_t PET[4] = {0xE88AAE, 0xF4F0EA, 0xB8A0E0, 0x8AB4E8};  // (no gold: the sparks are gold)
    for (int i = 0; i < 44; i++) {
        float x = 10.0f + hash2(i * 3.1f, 1.7f) * 46.0f, z = 13.0f + hash2(2.3f, i * 1.9f) * 32.0f;
        int ci = (int)x, cj = (int)z;
        if (!cell_in(ci, cj) || s_H[ci][cj] != 2 || s_cf[ci][cj] || s_mc[ci][cj]) {
            continue;
        }
        bool near_pile = false;
        for (int k = 0; k < NPILE; k++) {
            near_pile |= absf(x - s_pile[k].x) < 1.2f && absf(z - s_pile[k].z) < 1.2f;
        }
        if (near_pile) {
            continue;
        }
        prop_flower(x, 2.0f, z, PET[i & 3], 0.9f + hash2(i, 7) * 0.5f, hash2(i, 3) * 6);
        world_add(CK_SOLID);
    }
    s_wlod = LOD_ALL;
    prop_house(KH_I, KH_J, LH_Y0, KH_Y - LH_Y0, 0xE8E2D6, 0x5A86A0);
    world_add(CK_SOLID);
    prop_crates();
    world_add(CK_SOLID);
    prop_dock();
    world_add(CK_SOLID);
    prop_gateposts(14.8f, 30.5f, 17.2f, 30.5f, 2.0f, 13.6f);
    world_add(CK_SOLID);
    prop_gateposts(47.5f, 23.8f, 47.5f, 26.2f, 2.0f, 13.6f);
    world_add(CK_SOLID);
    prop_gateposts(47.8f, 36.8f, 50.2f, 36.8f, 2.0f, 4.6f);
    world_add(CK_SOLID);
}
