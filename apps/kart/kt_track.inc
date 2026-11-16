// INK KARTS: the tracks (included by app_kart.c). Each track is a closed spline through a few points (a PC script checks the
// shapes: the smallest radius and the gaps). The spline becomes ROWS of 1 m
// (the middle of the road and its direction). The world (the road, the kerbs, the barriers, the start, the props) is
// built from the rows with the light baked in (kt_mesh.inc). THE INK: each row keeps, for each kart, the last ribbon
// of ink that the kart laid there (the place across the road, the width, the time).

#define TMAX   1000   // the most rows of a track (1 m each)
#define ROAD_W 6.0f   // the half width of the road
#define KERB_W 1.0f   // the width of a kerb
#define WALL_D 10.5f  // the barrier: its inner face from the middle of the road
#define NTRACK 3
#define NKART  6

typedef struct {
    float x, z, tx, tz;
} trow_t;             // the middle of the road, the unit direction of the road
static trow_t *s_tr;  // (PSRAM) the rows of the track now
static int s_ntr;
static int s_track = -1;  // the track that the world holds now (-1: none)

// ---------------------------------------------------------------- the looks of the tracks
typedef struct {
    const char *name[2];
    const float *pts;
    int npts;
    look_t lk;        // the light (kt_mesh.inc)
    v3 sun;           // the direction to the sun
    float sky[5][4];  // the sky by the height of the view ray: (e, r, g, b)
    float gnd[3][4];  // the ground by the distance: (m, r, g, b)
    float mott;       // the strength of the mottle of the ground (0..1)
    uint32_t road0, road1, kerb0, kerb1, wall0, wall1, line;
    uint32_t far0, far1, near0, near1;  // the skyline: the far layer (top, low), the near layer (top, low)
    int skyline;                        // 0 hills, 1 mesas, 2 a city
    int props;                          // 0 trees, 1 rocks and palms, 2 lamps and towers
    int sun_glow;                       // the sun on the sky (0..255)
} theme_t;

static const float PTS_MEADOW[] = {0,  -80, 60, -86, 102, -62, 112, -12, 88,   28, 48,  39,  36,  68,
                                   34, 99,  2,  124, -52, 108, -92, 72,  -104, 12, -84, -48, -46, -78};
static const float PTS_DUNES[] = {0,   -90, 70,  -96, 112, -70, 118,  -24, 92,   6,   54,  2,
                                  26,  22,  28,  58,  64,  74,  104,  84,  110,  118, 66,  134,
                                  -20, 124, -62, 92,  -72, 46,  -108, 14,  -114, -48, -70, -86};
static const float PTS_NEON[] = {0,  -72, 78,  -78, 104, -43, 95,   -6, 114, 30, 98,  82,  46,  91,
                                 11, 78,  -28, 94,  -82, 88,  -104, 42, -89, 2,  -99, -42, -62, -78};

static const theme_t THEMES[NTRACK] = {
        {{"MEADOW RING", "טבעת האחו"},
         PTS_MEADOW,
         14,
         {{1.00f, 0.90f, 0.70f},
          {0.45f, 0.66f, 1.00f},
          {0.70f, 0.80f, 0.45f},
          1.10f,
          0.42f,
          1.45f,
          {176, 214, 255},
          34,
          1.0f / 95,
          0.85f},
         {-0.45f, 0.70f, -0.55f},
         {{-0.02f, 214, 236, 255},
          {0.03f, 190, 224, 255},
          {0.09f, 140, 198, 255},
          {0.24f, 70, 150, 255},
          {0.55f, 30, 96, 238}},
         {{0, 92, 176, 64}, {40, 104, 170, 84}, {130, 150, 196, 200}},
         0.9f,
         0x40444F,
         0x4C505C,
         0xF03C3C,
         0xFAFAFA,
         0x2E7CF0,
         0xFAFAFA,
         0xFFFFFF,
         0x9CB8E6,
         0xB6D0F0,
         0x5C9A6A,
         0x86B486,
         0,
         0,
         230},
        {{"SUNSET DUNES", "דיונות השקיעה"},
         PTS_DUNES,
         18,
         {{1.00f, 0.72f, 0.46f},
          {0.62f, 0.52f, 0.86f},
          {0.95f, 0.66f, 0.42f},
          1.15f,
          0.46f,
          1.40f,
          {255, 190, 150},
          30,
          1.0f / 90,
          0.85f},
         {0.75f, 0.32f, 0.55f},
         {{-0.02f, 255, 196, 140},
          {0.03f, 255, 168, 120},
          {0.09f, 246, 128, 112},
          {0.22f, 156, 92, 150},
          {0.55f, 70, 52, 130}},
         {{0, 236, 180, 112}, {40, 226, 160, 104}, {130, 240, 170, 140}},
         0.8f,
         0x4E4246,
         0x5A4C4E,
         0xF0A020,
         0x3A2A3A,
         0xE04870,
         0xFFE0B0,
         0xFFFFFF,
         0xC88A9C,
         0xE0A8A8,
         0xA8584A,
         0xC87452,
         1,
         1,
         255},
        {{"NEON BAY", "מפרץ הניאון"},
         PTS_NEON,
         14,
         {{0.78f, 0.70f, 1.00f},
          {0.40f, 0.36f, 0.90f},
          {0.40f, 0.20f, 0.50f},
          0.85f,
          0.58f,
          1.55f,
          {70, 52, 120},
          24,
          1.0f / 80,
          0.9f},
         {0.30f, 0.55f, 0.75f},
         {{-0.02f, 96, 60, 140}, {0.03f, 70, 46, 120}, {0.09f, 40, 30, 96}, {0.24f, 20, 18, 66}, {0.55f, 8, 8, 40}},
         {{0, 34, 30, 70}, {40, 40, 32, 82}, {130, 70, 52, 120}},
         1.0f,
         0x3A3C58,
         0x464A6A,
         0xFF3CC8,
         0x30E0FF,
         0x2A2050,
         0xFF50D0,
         0xFFFFFF,
         0x3C2C6C,
         0x54407C,
         0x1C1838,
         0x2C2450,
         2,
         2,
         120},
};
static const theme_t *TH = &THEMES[0];

// ---------------------------------------------------------------- the spline and the rows
static void catmull(const float *p0, const float *p1, const float *p2, const float *p3, float t, float *o)
{
    float t2 = t * t, t3 = t2 * t;
    for (int k = 0; k < 2; k++) {
        o[k] = 0.5f * (2 * p1[k] + (-p0[k] + p2[k]) * t + (2 * p0[k] - 5 * p1[k] + 4 * p2[k] - p3[k]) * t2 +
                       (-p0[k] + 3 * p1[k] - 3 * p2[k] + p3[k]) * t3);
    }
}
// the rows of the track: 32 points on each part of the spline, then a new point each metre of the length
static void track_rows(const theme_t *th)
{
    int n = th->npts, nd = n * 32;
    static float dx_[TMAX * 2], dz_[TMAX * 2], L[TMAX * 2 + 1];
    if (nd > TMAX * 2) {
        nd = TMAX * 2;
    }
    for (int i = 0; i < n; i++) {
        const float *p0 = th->pts + 2 * ((i + n - 1) % n), *p1 = th->pts + 2 * i, *p2 = th->pts + 2 * ((i + 1) % n),
                    *p3 = th->pts + 2 * ((i + 2) % n);
        for (int k = 0; k < 32 && i * 32 + k < nd; k++) {
            float o[2];
            catmull(p0, p1, p2, p3, k / 32.0f, o);
            dx_[i * 32 + k] = o[0];
            dz_[i * 32 + k] = o[1];
        }
    }
    L[0] = 0;
    for (int i = 1; i <= nd; i++) {
        float ax = dx_[i - 1], az = dz_[i - 1], bx = dx_[i % nd], bz = dz_[i % nd];
        L[i] = L[i - 1] + fsqrt((bx - ax) * (bx - ax) + (bz - az) * (bz - az));
    }
    float total = L[nd];
    int nr = (int)total;
    if (nr > TMAX) {
        nr = TMAX;
    }
    if (nr < 16) {
        nr = 16;
    }
    int j = 0;
    for (int r = 0; r < nr; r++) {
        float s = r * total / nr;
        while (j < nd - 1 && L[j + 1] < s) {
            j++;
        }
        float ax = dx_[j], az = dz_[j], bx = dx_[(j + 1) % nd], bz = dz_[(j + 1) % nd];
        float t = (s - L[j]) / FMAX(1e-6f, L[j + 1] - L[j]);
        s_tr[r].x = ax + (bx - ax) * t;
        s_tr[r].z = az + (bz - az) * t;
    }
    s_ntr = nr;
    for (int r = 0; r < nr; r++) {
        const trow_t *a = &s_tr[(r + nr - 1) % nr], *b = &s_tr[(r + 1) % nr];
        float tx = b->x - a->x, tz = b->z - a->z, k = frsqrt(tx * tx + tz * tz + 1e-9f);
        s_tr[r].tx = tx * k;
        s_tr[r].tz = tz * k;
    }
}
static inline int rwrap(int i)
{
    i %= s_ntr;
    return i < 0 ? i + s_ntr : i;
}
static inline float swrap(float s)
{
    float n = (float)s_ntr;
    s -= n * (float)ifloor(s / n);
    return s;
}
// the point at (s, d): s along the road (rows), d across it (+ = the right of the way of the race)
static v3 tr_point(float s, float d)
{
    s = swrap(s);
    int i = (int)s;
    float t = s - i;
    const trow_t *a = &s_tr[i], *b = &s_tr[rwrap(i + 1)];
    float x = a->x + (b->x - a->x) * t, z = a->z + (b->z - a->z) * t;
    float tx = a->tx + (b->tx - a->tx) * t, tz = a->tz + (b->tz - a->tz) * t;
    float k = frsqrt(tx * tx + tz * tz + 1e-9f);
    return V3(x + tz * k * d, 0, z - tx * k * d);
}
// the heading of the road at s (yaw: the front of a kart is (sin yaw, cos yaw))
static float fatan2(float y, float x);
static float tr_yaw(float s)
{
    const trow_t *a = &s_tr[rwrap((int)swrap(s))];
    return fatan2(a->tx, a->tz);
}
// the row nearest to (x, z), a search round the row hint (a kart moves a few rows a step)
static int tr_near(float x, float z, int hint, int span)
{
    int best = hint;
    float bd = 1e30f;
    for (int k = -span; k <= span; k++) {
        int i = rwrap(hint + k);
        float dx = x - s_tr[i].x, dz = z - s_tr[i].z, d = dx * dx + dz * dz;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}
// the place of (x, z) on the track: s (rows, with the part of the row) and d (across)
static void tr_coords(float x, float z, int *hint, float *s, float *d)
{
    int i = tr_near(x, z, *hint, 14);
    *hint = i;
    const trow_t *r = &s_tr[i];
    float dx = x - r->x, dz = z - r->z;
    *s = i + (dx * r->tx + dz * r->tz);
    *d = dx * r->tz - dz * r->tx;
}

// ---------------------------------------------------------------- the world of a track
static uint32_t s_rs = 1;
static float rnd01(void)
{
    s_rs ^= s_rs << 13;
    s_rs ^= s_rs >> 17;
    s_rs ^= s_rs << 5;
    return (s_rs & 0xFFFFFF) / 16777216.0f;
}
// a quad of four corners with one normal and one colour (the corners in order round the face)
static void wquad(v3 a, v3 b, v3 c, v3 d, v3 n, const float *col)
{
    int i0 = mb_v(a, n, col, 1), i1 = mb_v(b, n, col, 1), i2 = mb_v(c, n, col, 1), i3 = mb_v(d, n, col, 1);
    mb_q(i0, i1, i2, i3);
}
// the road: strips of quads (a soft patch colour along it). fine: 2 m and two strips (near the eye); else 6 m and one
// strip (far: the level of detail of each chunk, at ROAD_LOD m of its middle)
#define ROAD_LOD 46.0f
static void build_road_lod(bool fine)
{
    float c0[3], c1[3], col[3];
    col3(c0, TH->road0);
    col3(c1, TH->road1);
    const v3 up = V3(0, 1, 0);
    int st = fine ? 2 : 6, nq = fine ? 3 : 2;
    s_wlod = fine ? LOD_FINE : LOD_COARSE;
    s_wlodd = ROAD_LOD;
    for (int i = 0; i < s_ntr; i += st) {
        int j = i + st >= s_ntr ? 0 : i + st;
        int base = s_bnv;
        for (int e = 0; e < 2; e++) {
            int r = e ? j : i;
            for (int q = 0; q < nq; q++) {
                float d = fine ? (q - 1) * ROAD_W : (q ? ROAD_W : -ROAD_W);
                int qq = fine ? q : q * 2;
                float k = 0.5f + 0.5f * fsin(r * 0.11f + qq * 1.7f) * fsin(r * 0.037f + qq);
                col_mix(col, c0, c1, k);
                mb_v(tr_point((float)r, d), up, col, 1);
            }
        }
        if (fine) {
            mb_q(base, base + 1, base + 4, base + 3);
            mb_q(base + 1, base + 2, base + 5, base + 4);
        } else {
            mb_q(base, base + 1, base + 3, base + 2);
        }
        if (s_bnv > BMAXV - 40 || s_bnt > BMAXT - 40) {
            world_add(CK_SOLID);
        }
    }
    world_add(CK_SOLID);
    // the kerbs: blocks of 2 m (far: 6 m) in two colours, a little higher than the road
    float k0[3], k1[3];
    col3(k0, TH->kerb0);
    col3(k1, TH->kerb1);
    for (int i = 0; i < s_ntr; i += st) {
        int j = i + st >= s_ntr ? 0 : i + st;
        const float *kc = ((i / st) & 1) ? k1 : k0;
        for (int side = -1; side <= 1; side += 2) {
            float d0 = side * ROAD_W, d1 = side * (ROAD_W + KERB_W);
            v3 a = tr_point((float)i, d0), b = tr_point((float)i, d1), c = tr_point((float)j, d1),
               d = tr_point((float)j, d0);
            a.y = d.y = 0.02f;
            b.y = c.y = 0.05f;
            wquad(a, b, c, d, V3(0, 1, 0), kc);
        }
        if (s_bnv > BMAXV - 40 || s_bnt > BMAXT - 40) {
            world_add(CK_SOLID);
        }
    }
    world_add(CK_SOLID);
    s_wlod = LOD_ALL;
    s_wlodd = 0;
}
static void build_road(void)
{
    build_road_lod(true);
    build_road_lod(false);
    float cw[3], cb[3];
    // the start line: a band of squares
    col3(cw, 0xFAFAFA);
    col3(cb, 0x202028);
    for (int row = 0; row < 2; row++) {
        for (int q = 0; q < 10; q++) {
            float s0 = row * 0.7f, s1 = s0 + 0.7f, d0 = -ROAD_W + q * (2 * ROAD_W / 10), d1 = d0 + 2 * ROAD_W / 10;
            v3 a = tr_point(s0, d0), b = tr_point(s0, d1), c = tr_point(s1, d1), d = tr_point(s1, d0);
            a.y = b.y = c.y = d.y = 0.025f;
            wquad(a, b, c, d, V3(0, 1, 0), ((row + q) & 1) ? cw : cb);
        }
    }
    // the grid boxes: a white line in front of each start place
    for (int g = 0; g < NKART; g++) {
        float s = -6.0f - (g / 2) * 6.0f + 1.2f, dd = (g & 1) ? 2.6f : -2.6f;
        v3 a = tr_point(s, dd - 1.0f), b = tr_point(s, dd + 1.0f), c = tr_point(s + 0.3f, dd + 1.0f),
           d = tr_point(s + 0.3f, dd - 1.0f);
        a.y = b.y = c.y = d.y = 0.025f;
        wquad(a, b, c, d, V3(0, 1, 0), cw);
    }
    world_add(CK_SOLID);
}
// the barriers: a low wall with a soft top on each side, bands of two colours each 4 m (far: 8 m, only the face)
static void build_walls_lod(bool fine)
{
    float w0[3], w1[3];
    col3(w0, TH->wall0);
    col3(w1, TH->wall1);
    int st = fine ? 4 : 8, np = fine ? 3 : 2;
    s_wlod = fine ? LOD_FINE : LOD_COARSE;
    s_wlodd = ROAD_LOD;
    for (int i = 0; i < s_ntr; i += st) {
        int j = i + st >= s_ntr ? 0 : i + st;
        const float *wc = ((i / st) & 1) ? w1 : w0;
        for (int side = -1; side <= 1; side += 2) {
            // the profile across the wall (d from the inner face, y) and its normal (across, up)
            static const float PR[3][4] = {{0, 0, -1, 0.15f}, {0, 0.62f, -1, 0.4f}, {0.42f, 0.82f, -0.2f, 1}};
            int id[2][3];
            for (int e = 0; e < 2; e++) {
                int r = e ? j : i;
                const trow_t *t = &s_tr[r];
                v3 rt = V3(t->tz, 0, -t->tx);  // the right of the road
                for (int k = 0; k < np; k++) {
                    v3 p = tr_point((float)r, side * (WALL_D + PR[k][0]));
                    p.y = PR[k][1];
                    // (the inner face looks to the road: -side x the right)
                    v3 n = vnorm(V3(-rt.x * side * absf(PR[k][2]), PR[k][3], -rt.z * side * absf(PR[k][2])));
                    id[e][k] = mb_v(p, n, wc, k == 0 ? 0.7f : 1);
                }
            }
            for (int k = 0; k + 1 < np; k++) {
                mb_q(id[0][k], id[0][k + 1], id[1][k + 1], id[1][k]);
            }
        }
        if (s_bnv > BMAXV - 60 || s_bnt > BMAXT - 60) {
            world_add(CK_SOLID);
        }
    }
    world_add(CK_SOLID);
    s_wlod = LOD_ALL;
    s_wlodd = 0;
}
static void build_walls(void)
{
    build_walls_lod(true);
    build_walls_lod(false);
}
// the start gantry: two posts and a beam over the line, with a band of squares
static void build_gantry(void)
{
    const trow_t *t = &s_tr[0];
    float yaw = fatan2(t->tx, t->tz);
    xf_t x;
    v3 at = tr_point(0.6f, 0);
    xf_make(&x, at, yaw, 0, 0, V3(1, 1, 1));
    mb_frame(&x);
    uint32_t pc = TH->skyline == 2 ? 0x2A2A44 : 0x30343E;
    prim_cbox(V3(-WALL_D + 0.3f, 3.2f, 0), V3(0.35f, 3.2f, 0.35f), 0.08f, pc);
    prim_cbox(V3(WALL_D - 0.3f, 3.2f, 0), V3(0.35f, 3.2f, 0.35f), 0.08f, pc);
    prim_cbox(V3(0, 6.6f, 0), V3(WALL_D, 0.55f, 0.4f), 0.1f, pc);
    // (the band: squares on the face toward the grid)
    float cw[3], cb[3];
    col3(cw, 0xFAFAFA);
    col3(cb, 0x18181E);
    for (int q = 0; q < 16; q++) {
        for (int r = 0; r < 2; r++) {
            float x0 = -WALL_D + 1 + q * ((2 * WALL_D - 2) / 16), x1 = x0 + (2 * WALL_D - 2) / 16,
                  y0 = 6.15f + r * 0.45f, y1 = y0 + 0.45f;
            wquad(V3(x0, y0, -0.42f), V3(x1, y0, -0.42f), V3(x1, y1, -0.42f), V3(x0, y1, -0.42f), V3(0, 0, -1),
                  ((q + r) & 1) ? cw : cb);
        }
    }
    // (the lamp boxes of the start lights)
    for (int k = 0; k < 3; k++) {
        prim_cbox(V3(-1.6f + k * 1.6f, 5.6f, -0.1f), V3(0.6f, 0.45f, 0.3f), 0.08f, 0x14141A);
    }
    mb_noframe();
    world_add(CK_SOLID);
}
// a tree: a trunk and a round crown of two or three balls (fine or coarse)
static void prop_tree(v3 p, float s, uint32_t c_top, uint32_t c_bot, bool fine)
{
    xf_t x;
    xf_make(&x, p, rnd01() * TAU_F, 0, 0, V3(s, s, s));
    mb_frame(&x);
    prim_cyl(V3(0, 0, 0), 0, 2.2f, 0.32f, 0.22f, fine ? 5 : 3, 0, 0x6A4A30, 0x7A5638, 0x6A4A30);
    s_bmat = MT_LEAF;
    if (fine) {
        prim_ell(V3(0, 3.4f, 0), V3(1.9f, 1.7f, 1.9f), 4, 8, c_top, c_bot);
        prim_ell(V3(0.7f, 4.6f, 0.3f), V3(1.2f, 1.1f, 1.2f), 3, 6, c_top, c_bot);
    } else {
        prim_ell(V3(0, 3.6f, 0), V3(2.0f, 2.0f, 2.0f), 3, 6, c_top, c_bot);
    }
    s_bmat = MT_MATTE;
    mb_noframe();
}
// a rock: a flat lump with a lighter top
static void prop_rock(v3 p, float s, bool fine)
{
    xf_t x;
    xf_make(&x, p, rnd01() * TAU_F, 0, 0, V3(s * (1 + rnd01() * 0.6f), s * (0.6f + rnd01() * 0.5f), s));
    mb_frame(&x);
    prim_ell(V3(0, 0.3f, 0), V3(1.6f, 1.3f, 1.4f), fine ? 4 : 3, fine ? 7 : 5, 0xD88A5A, 0x9A4E3A);
    mb_noframe();
}
// a cactus: a stem and two arms
static void prop_cactus(v3 p, float s, bool fine)
{
    xf_t x;
    xf_make(&x, p, rnd01() * TAU_F, 0, 0, V3(s, s, s));
    mb_frame(&x);
    int k = fine ? 6 : 4;
    prim_cyl(V3(0, 0, 0), 0, 3.2f, 0.32f, 0.3f, k, 2, 0x3E8A4A, 0x5AAA5A, 0x6ABA6A);
    if (fine) {
        v3 a1[3] = {V3(0, 1.4f, 0), V3(0.8f, 1.5f, 0), V3(0.85f, 2.4f, 0)};
        float r1[3] = {0.2f, 0.2f, 0.18f};
        prim_tube(a1, r1, 3, 5, 0x3E8A4A, 0x5AAA5A, true);
        v3 a2[3] = {V3(0, 1.9f, 0), V3(-0.7f, 2.0f, 0), V3(-0.75f, 2.7f, 0)};
        prim_tube(a2, r1, 3, 5, 0x3E8A4A, 0x5AAA5A, true);
    }
    mb_noframe();
}
// a palm: a bent trunk and fronds that sway
static void prop_palm(v3 p, float s)
{
    xf_t x;
    xf_make(&x, p, rnd01() * TAU_F, 0, 0, V3(s, s, s));
    mb_frame(&x);
    v3 tk[4] = {V3(0, 0, 0), V3(0.3f, 2.0f, 0), V3(0.9f, 4.0f, 0), V3(1.7f, 5.6f, 0)};
    float tr[4] = {0.3f, 0.26f, 0.22f, 0.2f};
    prim_tube(tk, tr, 4, 6, 0x8A6440, 0xA87E50, false);
    s_bsway = 0.6f;
    s_bmat = MT_LEAF;
    for (int f = 0; f < 6; f++) {
        float a = f * TAU_F / 6;
        v3 d = V3(fcos(a), 0, fsin(a));
        v3 fp[4];
        float hw[4] = {0.15f, 0.45f, 0.4f, 0.08f};
        for (int k = 0; k < 4; k++) {
            fp[k] = V3(1.7f + d.x * k * 0.9f, 5.6f + 0.25f * k - 0.22f * k * k, d.z * k * 0.9f);
        }
        prim_leaf(fp, hw, 4, V3(-d.z, 0, d.x), 0x3E9A3A, 0x6AC24A);
    }
    s_bsway = 0;
    s_bmat = MT_MATTE;
    mb_noframe();
}
// a lamp of the bay: a post with a glowing head (the glow itself is drawn each frame: s_lamp)
#define NLAMP 48
static v3 s_lamp[NLAMP];
static int s_nlamp;
static void prop_lamp(v3 p, float yaw)
{
    xf_t x;
    xf_make(&x, p, yaw, 0, 0, V3(1, 1, 1));
    mb_frame(&x);
    prim_cyl(V3(0, 0, 0), 0, 5.0f, 0.14f, 0.1f, 6, 0, 0x2C2C40, 0x3C3C54, 0x2C2C40);
    s_bmat = MT_GLOW;
    prim_ell(V3(0, 5.2f, 0), V3(0.38f, 0.3f, 0.38f), 3, 6, 0xFFF0FF, 0xFF80E0);
    s_bmat = MT_MATTE;
    mb_noframe();
}
// a tower of the bay: a tall box with bands of lit windows (the light colour is baked: MT_GLOW bands)
static void prop_tower(v3 p, float w, float h, uint32_t lit)
{
    xf_t x;
    xf_make(&x, p, rnd01() * TAU_F, 0, 0, V3(1, 1, 1));
    mb_frame(&x);
    prim_cbox(V3(0, h * 0.5f, 0), V3(w, h * 0.5f, w), 0.3f, 0x24203C);
    s_bmat = MT_GLOW;
    float c[3];
    col3(c, lit);
    for (int b = 1; b < (int)(h / 3.2f); b++) {
        float y = b * 3.2f;
        for (int f = 0; f < 4; f++) {
            float a = f * PI_F * 0.5f;
            v3 n = V3(fsin(a), 0, fcos(a)), r = V3(fcos(a), 0, -fsin(a));
            v3 o = vadd(V3(0, y, 0), vmul(n, w + 0.02f));
            v3 p0 = vadd(o, vmul(r, -w * 0.8f)), p1 = vadd(o, vmul(r, w * 0.8f));
            if ((b + f) % 3 == 0) {
                continue;
            }
            wquad(p0, p1, vadd(p1, V3(0, 0.9f, 0)), vadd(p0, V3(0, 0.9f, 0)), n, c);
        }
    }
    s_bmat = MT_MATTE;
    mb_noframe();
}
// the grandstand by the start straight: steps with the colours of the crowd, a roof
static void build_stand(void)
{
    const trow_t *t = &s_tr[rwrap(-14)];
    float yaw = fatan2(t->tx, t->tz);
    xf_t x;
    v3 at = tr_point(-14, -(WALL_D + 6.5f));
    xf_make(&x, at, yaw, 0, 0, V3(1, 1, 1));
    mb_frame(&x);
    for (int k = 0; k < 4; k++) {
        prim_box(V3(-k * 1.2f, 0.45f + k * 0.9f, 0), V3(0.6f, 0.45f + k * 0.9f, 11), 0xA8B0C0);
    }
    static const uint32_t CROWD[] = {0xFF5050, 0xFFD040, 0x50C0FF, 0xFF80D0, 0x70E070, 0xFFFFFF, 0xFF9030};
    for (int k = 0; k < 4; k++) {
        for (int q = 0; q < 11; q++) {
            float z = -10.0f + q * 2.0f + (k & 1) * 0.9f, y = 1.05f + k * 1.8f;
            prim_box(V3(-k * 1.2f, y, z), V3(0.25f, 0.3f, 0.3f), CROWD[(k * 5 + q * 3) % 7]);
        }
    }
    prim_cbox(V3(-2.0f, 8.4f, 0), V3(3.2f, 0.18f, 11.5f), 0.08f, TH->wall0);
    prim_cyl(V3(-4.6f, 0, -10.5f), 0, 8.4f, 0.16f, 0.16f, 6, 0, 0xC0C8D0, 0xC0C8D0, 0xC0C8D0);
    prim_cyl(V3(-4.6f, 0, 10.5f), 0, 8.4f, 0.16f, 0.16f, 6, 0, 0xC0C8D0, 0xC0C8D0, 0xC0C8D0);
    mb_noframe();
    world_add(CK_SOLID);
}
// the distance from (x, z) to the middle of the road (the nearest row; all rows)
static float tr_dist(float x, float z)
{
    float bd = 1e30f;
    for (int i = 0; i < s_ntr; i += 2) {
        float dx = x - s_tr[i].x, dz = z - s_tr[i].z, d = dx * dx + dz * dz;
        if (d < bd) {
            bd = d;
        }
    }
    return fsqrt(bd);
}
// the props of the track: each one at a random place beside the track (not on it, not near the start)
static void build_props(void)
{
    s_nlamp = 0;
    s_rs = 0x9E3779B9u ^ (uint32_t)(s_track * 7919 + 17);
    int nprop = TH->props == 2 ? 26 : 50;
    for (int k = 0; k < nprop; k++) {
        float s = rnd01() * s_ntr, side = rnd01() < 0.5f ? -1.0f : 1.0f,
              off = WALL_D + 3.0f + rnd01() * rnd01() * 34.0f;
        if (TH->props == 2) {
            off = WALL_D + 8.0f + rnd01() * 30.0f;
        }
        v3 p = tr_point(s, side * off);
        if (tr_dist(p.x, p.z) < WALL_D + 2.6f) {
            continue;
        }
        float sc = 0.8f + rnd01() * 0.6f;
        int nlod = TH->props == 2 ? 1 : 2;  // (a tower has one level)
        for (int lod = 0; lod < nlod; lod++) {
            s_wlod = nlod == 1 ? LOD_ALL : lod ? LOD_COARSE : LOD_FINE;
            uint32_t keep = s_rs;
            if (TH->props == 0) {
                static const uint32_t TOP[4] = {0x5CC04A, 0x46A848, 0x86C846, 0xF0A030};
                static const uint32_t BOT[4] = {0x2C7A30, 0x24683A, 0x4A8A2A, 0xB05A20};
                int c = k % 7 == 3 ? 3 : k % 3;
                prop_tree(p, sc, TOP[c], BOT[c], !lod);
            } else if (TH->props == 1) {
                if (k % 3 == 0) {
                    prop_rock(p, sc * 1.6f, !lod);
                } else if (k % 3 == 1) {
                    prop_cactus(p, sc, !lod);
                } else {
                    if (lod) {
                        prop_cactus(p, sc, false);
                    } else {
                        prop_palm(p, sc);
                    }
                }
            } else {
                static const uint32_t LIT[3] = {0xFF60D8, 0x40E8FF, 0xFFD050};
                prop_tower(p, 2.5f + rnd01() * 3, 10 + rnd01() * 26, LIT[k % 3]);
            }
            s_rs = keep;
            world_add(CK_SOLID);
        }
        rnd01();
    }
    s_wlod = LOD_ALL;
    // the lamps of the bay: both sides of the road, each 36 m
    if (TH->props == 2) {
        for (int i = 8; i + 8 < s_ntr && s_nlamp < NLAMP; i += 36) {
            float side = ((i / 36) & 1) ? 1.0f : -1.0f;
            v3 p = tr_point((float)i, side * (WALL_D + 1.2f));
            prop_lamp(p, 0);
            s_lamp[s_nlamp++] = V3(p.x, 5.2f, p.z);
            if ((i / 36) % 4 == 0) {
                world_add(CK_SOLID);
            }
        }
        world_add(CK_SOLID);
    }
}
static void karts_build(void);
// the world of track tr: the rows, the road, the walls, the start, the props
static void track_build(int tr)
{
    TH = &THEMES[tr];
    LK = TH->lk;
    L_SUN = vnorm(TH->sun);
    s_track = tr;
    karts_build();  // (the karts again: their wheels and their far models have the light baked in)
    track_rows(TH);
    s_wnv = s_wnt = s_nch = 0;
    s_wlod = LOD_ALL;
    mb_begin();
    build_road();
    build_walls();
    build_gantry();
    build_stand();
    build_props();
}

// ---------------------------------------------------------------- the ink
// For each row and each kart: the last ribbon of the kart there. d = across (x 0.1 m), w = 1 wide (a BLOT), t = the
// time of the race (x 0.1 s) + 1 (0: none)
typedef struct {
    int8_t d;
    uint8_t w;
    uint16_t t;
} ink_t;
static ink_t *s_ink;     // (PSRAM) s_ntr x NKART
#define INK_LIFE  50.0f  // the ink stays this long (s), the last 10 s it fades
#define INK_OWN   2.5f   // your own ink counts after this time (s)
#define INK_RIVAL 0.25f
#define INK_WET   7.0f  // the ink of a rival is wet (SLICK) until this age (s), then dry
#define INK_HW    0.5f  // the half width of a ribbon (m), a BLOT 3.5 x
static float s_rtime;   // the time of the race (s; 0 at GO)
static inline float ink_hw(const ink_t *e)
{
    return e->w ? INK_HW * 3.5f : INK_HW;
}
static void ink_clear(void)
{
    if (s_ink) {
        memset(s_ink, 0, sizeof(ink_t) * TMAX * NKART);
    }
}
static void ink_put(int row, int k, float d, bool wide)
{
    ink_t *e = &s_ink[rwrap(row) * NKART + k];
    e->d = (int8_t)clampi((int)(d * 10 + (d >= 0 ? 0.5f : -0.5f)), -127, 127);
    e->w = wide ? 1 : 0;
    int t = (int)(s_rtime * 10) + 1;
    e->t = (uint16_t)clampi(t, 1, 65535);
}
// the ink under the point (s, d) for kart k: -1 none, else the kart that laid it (the newest ribbon that covers the point); *age =
// its age (s)
static int ink_at(float s, float d, float *age)
{
    int row = rwrap((int)swrap(s));
    const ink_t *e = &s_ink[row * NKART];
    int best = -1, bt = 0;
    for (int j = 0; j < NKART; j++) {
        if (!e[j].t) {
            continue;
        }
        float a = s_rtime - (e[j].t - 1) * 0.1f;
        if (a > INK_LIFE || a < 0) {
            continue;
        }
        if (absf(d - e[j].d * 0.1f) > ink_hw(&e[j]) + 0.15f) {
            continue;
        }
        if (e[j].t > bt) {
            bt = e[j].t;
            best = j;
        }
    }
    if (best >= 0 && age) {
        *age = s_rtime - (bt - 1) * 0.1f;
    }
    return best;
}
