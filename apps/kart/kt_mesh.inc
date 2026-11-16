// INK KARTS: the meshes (included by app_kart.c; a copy of apps/cubby/cb_mesh.inc, the light of each track in LK). A builder makes the shapes (rounded boxes, cylinders, ellipsoids,
// tubes, the rounded terraces of the island) with a smooth normal at each corner. The island goes into the WORLD
// (the light is baked into each corner at the start); the robot, the cubes and the things go into MODELS (the light is
// made in each frame, with the shine and the rim light of the view). Each frame: the corners go to the screen (the
// haze of the distance too), the triangles go into a draw list (the back faces out, a cut at the near plane), and
// each core draws the list in its band.

// ---------------------------------------------------------------- the light
// the direction to the sun (from the south-west, behind the start view at the left) and the colours of the light
static v3 L_SUN;
// the light of the track (kt_track.inc sets LK for each track: the day, the sunset, the dusk)
typedef struct {
    float sun[3], sky[3], gnd[3];   // the colours of the sun light, of the sky light (from above), of the ground light
    float sun_k, amb_k, sat;        // the strength of the sun, of the ambient light, the colour of the grade
    float haze[3];                  // the haze colour of the far distance (the sky at the horizon), 0..255
    float haze0, haze_d, haze_max;  // the haze starts at haze0 m, grows over haze_d m, up to haze_max
} look_t;
static look_t LK;
#define SUN_R     LK.sun[0]
#define SUN_G     LK.sun[1]
#define SUN_B     LK.sun[2]
#define SKY_R     LK.sky[0]
#define SKY_G     LK.sky[1]
#define SKY_B     LK.sky[2]
#define GND_R     LK.gnd[0]
#define GND_G     LK.gnd[1]
#define GND_B     LK.gnd[2]
#define SUN_K     LK.sun_k
#define AMB_K     LK.amb_k
#define GRADE_SAT LK.sat
#define HAZE_R    LK.haze[0]
#define HAZE_G    LK.haze[1]
#define HAZE_B    LK.haze[2]

enum {
    MT_MATTE,
    MT_PLASTIC,
    MT_METAL,
    MT_GLASS,
    MT_GLOW,
    MT_LEAF
};

// the light of a corner: albedo a (0..1 a channel), normal n (unit), ao (0..1). v = the unit vector to the eye (or
// NULL: no shine, no rim). Out: 0..255 a channel
static void light_rgb(const float *a, v3 n, float ao, int mat, const v3 *v, float *o)
{
    if (mat == MT_GLOW) {
        o[0] = a[0] * 255;
        o[1] = a[1] * 255;
        o[2] = a[2] * 255;
        return;
    }
    float nl = vdot(n, L_SUN);
    float wrap = mat == MT_LEAF ? 0.6f : 0.32f;
    float d = (nl + wrap) * (mat == MT_LEAF ? 0.625f : 0.757576f);
    if (d < 0) {
        d = 0;
    }
    d *= d * 0.35f + 0.65f;
    float h = 0.5f + 0.5f * n.y;
    float sr = SUN_R * d * SUN_K + (SKY_R * h + GND_R * (1 - h)) * AMB_K;
    float sg = SUN_G * d * SUN_K + (SKY_G * h + GND_G * (1 - h)) * AMB_K;
    float sb = SUN_B * d * SUN_K + (SKY_B * h + GND_B * (1 - h)) * AMB_K;
    float r = a[0] * sr * ao, g = a[1] * sg * ao, b = a[2] * sb * ao;
    if (v) {
        float nv = vdot(n, *v);
        if (nv < 0) {
            nv = 0;
        }
        // the rim: the edge of a shape against the sky gets a warm light
        float rim = 1 - nv;
        rim = rim * rim * rim * 0.42f;
        r += rim * 1.0f;
        g += rim * 0.86f;
        b += rim * 0.70f;
        if (mat == MT_PLASTIC || mat == MT_METAL || mat == MT_GLASS) {
            v3 hv = vnorm(vadd(L_SUN, *v));
            float s = vdot(n, hv);
            if (s > 0) {
                s *= s;
                s *= s;
                s *= s;  // ^8
                if (mat != MT_PLASTIC) {
                    s *= s;  // ^16
                }
                if (mat == MT_GLASS) {
                    s *= s;
                    s *= 1.6f;
                }  // ^32
                else if (mat == MT_METAL) {
                    s *= 0.75f;
                } else {
                    s *= 0.45f;
                }
                r += s;
                g += s * 0.96f;
                b += s * 0.88f;
            }
            if (mat == MT_METAL) {
                // the reflection of the sky: the faces that look up are bluer
                float up = n.y > 0 ? n.y : 0;
                r += up * 0.05f;
                g += up * 0.09f;
                b += up * 0.16f;
            }
        }
    }
    // the grade: a little more colour, and a soft top so the light keeps its hue
    float l = r * 0.3f + g * 0.55f + b * 0.15f;
    r = l + (r - l) * GRADE_SAT;
    g = l + (g - l) * GRADE_SAT;
    b = l + (b - l) * GRADE_SAT;
    r = r < 0 ? 0 : r * frcp(1 + r * 0.18f) * 1.18f;
    g = g < 0 ? 0 : g * frcp(1 + g * 0.18f) * 1.18f;
    b = b < 0 ? 0 : b * frcp(1 + b * 0.18f) * 1.18f;
    o[0] = clampf(r * 255, 0, 247);
    o[1] = clampf(g * 255, 0, 251);
    o[2] = clampf(b * 255, 0, 247);
}

// the light of a corner from its dot products (the vectors are in the frame of the part, once a part): nl = n.L,
// ny = n.up, nv = n.eye, nh = n.half. The same light as light_rgb, with a table for the tone curve
static uint8_t TONE[384];
static void tone_init(void)
{
    for (int i = 0; i < 384; i++) {
        float v = i / 128.0f, t = v * frcp(1 + v * 0.18f) * 1.18f * 255;
        TONE[i] = (uint8_t)(t > 255 ? 255 : t);
    }
}
static inline void light_fast(const float *a, float nl, float ny, float nv, float nh, float ao, int mat, float *o)
{
    if (mat == MT_GLOW) {
        o[0] = a[0] * 255;
        o[1] = a[1] * 255;
        o[2] = a[2] * 255;
        return;
    }
    float d = mat == MT_LEAF ? (nl + 0.6f) * 0.625f : (nl + 0.32f) * 0.757576f;
    if (d < 0) {
        d = 0;
    }
    d *= d * 0.35f + 0.65f;
    float h = 0.5f + 0.5f * ny;
    float r = a[0] * ao * (SUN_R * d * SUN_K + (SKY_R * h + GND_R * (1 - h)) * AMB_K);
    float g = a[1] * ao * (SUN_G * d * SUN_K + (SKY_G * h + GND_G * (1 - h)) * AMB_K);
    float b = a[2] * ao * (SUN_B * d * SUN_K + (SKY_B * h + GND_B * (1 - h)) * AMB_K);
    if (nv < 0) {
        nv = 0;
    }
    // (a soft fill light from the eye: the hero is never dull, also with his back to the sun)
    float fill = nv * 0.34f * ao;
    r += a[0] * fill;
    g += a[1] * fill * 0.97f;
    b += a[2] * fill * 0.92f;
    float rim = 1 - nv;
    rim = rim * rim * rim * 0.42f;
    r += rim;
    g += rim * 0.86f;
    b += rim * 0.70f;
    if ((mat == MT_PLASTIC || mat == MT_METAL || mat == MT_GLASS) && nh > 0) {
        float s = nh * nh;
        s *= s;
        s *= s;
        if (mat != MT_PLASTIC) {
            s *= s;
        }
        if (mat == MT_GLASS) {
            s *= s;
            s *= 1.6f;
        } else if (mat == MT_METAL) {
            s *= 0.75f;
        } else {
            s *= 0.45f;
        }
        r += s;
        g += s * 0.96f;
        b += s * 0.88f;
    }
    if (mat == MT_METAL && ny > 0) {
        r += ny * 0.05f;
        g += ny * 0.09f;
        b += ny * 0.16f;
    }
    float l = r * 0.3f + g * 0.55f + b * 0.15f;
    r = l + (r - l) * GRADE_SAT;
    g = l + (g - l) * GRADE_SAT;
    b = l + (b - l) * GRADE_SAT;
    o[0] = TONE[clampi((int)(r * 128), 0, 383)];
    o[1] = TONE[clampi((int)(g * 128), 0, 383)];
    o[2] = TONE[clampi((int)(b * 128), 0, 383)];
}
// a small hash noise: 0..1 for a point of the grid
static float hash2(float x, float z)
{
    uint32_t h = (uint32_t)(int32_t)(x * 37.0f) * 73856093u ^ (uint32_t)(int32_t)(z * 37.0f) * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return (h & 0xFFFF) / 65535.0f;
}

// ---------------------------------------------------------------- the builder
typedef struct {
    float x, y, z, nx, ny, nz, r, g, b, ao, sway;
    uint8_t mat;
} bv_t;
typedef struct {
    uint16_t a, b, c;
} tri_t;
#define BMAXV 3000
#define BMAXT 4800
static bv_t *s_bv;  // the builder (PSRAM): the shape now
static tri_t *s_bt;
static int s_bnv, s_bnt;
static xf_t s_bx;       // the frame of the shapes that are added
static bool s_bx_flip;  // the frame mirrors (a negative scale): the triangles turn round
static float s_bsway;   // the sway of the corners that are added (palm leaves)
static int s_bmat;

static void mb_begin(void)
{
    s_bnv = s_bnt = 0;
    xf_ident(&s_bx);
    s_bx_flip = false;
    s_bsway = 0;
    s_bmat = MT_MATTE;
}
static void mb_frame(const xf_t *x)
{
    s_bx = *x;
    const float *r = x->r;
    float det = r[0] * (r[4] * r[8] - r[5] * r[7]) - r[1] * (r[3] * r[8] - r[5] * r[6]) +
                r[2] * (r[3] * r[7] - r[4] * r[6]);
    s_bx_flip = det < 0;
}
static void mb_noframe(void)
{
    xf_ident(&s_bx);
    s_bx_flip = false;
}
// a corner (in the frame now), its normal and its colour (0..1 a channel); the index of the corner
static int mb_v(v3 p, v3 n, const float *col, float ao)
{
    if (s_bnv >= BMAXV) {
        return BMAXV - 1;
    }
    bv_t *v = &s_bv[s_bnv];
    v3 q = xf_pt(&s_bx, p);
    // (the normal: the inverse scale of the frame, then unit length)
    const float *r = s_bx.r;
    float sx2 = r[0] * r[0] + r[3] * r[3] + r[6] * r[6], sy2 = r[1] * r[1] + r[4] * r[4] + r[7] * r[7],
          sz2 = r[2] * r[2] + r[5] * r[5] + r[8] * r[8];
    v3 m = xf_dir(&s_bx,
                  V3(n.x / (sx2 > 1e-9f ? sx2 : 1), n.y / (sy2 > 1e-9f ? sy2 : 1), n.z / (sz2 > 1e-9f ? sz2 : 1)));
    m = vnorm(m);
    v->x = q.x;
    v->y = q.y;
    v->z = q.z;
    v->nx = m.x;
    v->ny = m.y;
    v->nz = m.z;
    v->r = col[0];
    v->g = col[1];
    v->b = col[2];
    v->ao = ao;
    v->sway = s_bsway;
    v->mat = (uint8_t)s_bmat;
    return s_bnv++;
}
// a triangle. Its front is the side of the normals of its corners: the order of the corners comes from them, so
// a shape cannot face the wrong way (the rasterizer: a front face has (b - a) x (c - a) against the normal)
static void mb_t(int a, int b, int c)
{
    if (s_bnt >= BMAXT) {
        return;
    }
    const bv_t *A = &s_bv[a], *B = &s_bv[b], *Cc = &s_bv[c];
    v3 e1 = V3(B->x - A->x, B->y - A->y, B->z - A->z), e2 = V3(Cc->x - A->x, Cc->y - A->y, Cc->z - A->z);
    v3 n = V3(A->nx + B->nx + Cc->nx, A->ny + B->ny + Cc->ny, A->nz + B->nz + Cc->nz);
    if (vdot(vcross(e1, e2), n) > 0) {
        int q = b;
        b = c;
        c = q;
    }
    s_bt[s_bnt++] = (tri_t){(uint16_t)a, (uint16_t)b, (uint16_t)c};
}
static void mb_q(int a, int b, int c, int d)
{
    mb_t(a, b, c);
    mb_t(a, c, d);
}

static float *col3(float *o, uint32_t hex)
{
    o[0] = ((hex >> 16) & 255) / 255.0f;
    o[1] = ((hex >> 8) & 255) / 255.0f;
    o[2] = (hex & 255) / 255.0f;
    return o;
}
static void col_mix(float *o, const float *a, const float *b, float t)
{
    for (int i = 0; i < 3; i++) {
        o[i] = a[i] + (b[i] - a[i]) * t;
    }
}

// a rounded box: half size h, the radius rad of the edges, seg steps in each rounded edge; one colour
static void prim_rbox(v3 c, v3 h, float rad, int seg, uint32_t hex)
{
    float col[3];
    col3(col, hex);
    // the coordinates along one axis of a face: -h, the steps of the rounding, the flat part, the steps, +h
    for (int f = 0; f < 6; f++) {
        int ax = f >> 1;  // the axis of the normal of the face
        float sgn = (f & 1) ? 1.0f : -1.0f;
        int u = (ax + 1) % 3, w = (ax + 2) % 3;
        float hs[3] = {h.x, h.y, h.z};
        float cu[12], cw[12];
        int nu = 0, nw = 0;
        for (int k = 0; k <= seg; k++) {
            float t = (float)k / seg;
            float a = (1 - fcos(t * PI_F * 0.5f));
            cu[nu++] = -hs[u] + rad * a;
        }
        for (int k = seg; k >= 0; k--) {
            float t = (float)k / seg;
            float a = (1 - fcos(t * PI_F * 0.5f));
            cu[nu++] = hs[u] - rad * a;
        }
        for (int k = 0; k <= seg; k++) {
            float t = (float)k / seg;
            float a = (1 - fcos(t * PI_F * 0.5f));
            cw[nw++] = -hs[w] + rad * a;
        }
        for (int k = seg; k >= 0; k--) {
            float t = (float)k / seg;
            float a = (1 - fcos(t * PI_F * 0.5f));
            cw[nw++] = hs[w] - rad * a;
        }
        int base = s_bnv;
        for (int i = 0; i < nu; i++) {
            for (int j = 0; j < nw; j++) {
                float p[3];
                p[ax] = sgn * hs[ax];
                p[u] = cu[i];
                p[w] = cw[j];
                float in[3];
                for (int k = 0; k < 3; k++) {
                    float lim = hs[k] - rad;
                    in[k] = clampf(p[k], -lim, lim);
                }
                v3 d = V3(p[0] - in[0], p[1] - in[1], p[2] - in[2]);
                v3 n = vnorm(d);
                v3 q = vadd(V3(in[0], in[1], in[2]), vmul(n, rad));
                mb_v(vadd(c, q), n, col, 1);
            }
        }
        for (int i = 0; i + 1 < nu; i++) {
            for (int j = 0; j + 1 < nw; j++) {
                int a = base + i * nw + j, b = a + 1, d = a + nw, e = d + 1;
                if (sgn > 0) {
                    mb_q(a, b, e, d);
                } else {
                    mb_q(a, d, e, b);
                }
            }
        }
    }
}
// a box with flat faces (12 triangles)
static void prim_box(v3 c, v3 h, uint32_t hex)
{
    float col[3];
    col3(col, hex);
    static const int8_t F[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (int f = 0; f < 6; f++) {
        v3 n = V3(F[f][0], F[f][1], F[f][2]);
        v3 u = f < 2 ? V3(0, 1, 0) : f < 4 ? V3(0, 0, 1) : V3(1, 0, 0);
        v3 w = vcross(n, u);
        v3 cc = vadd(c, V3(n.x * h.x, n.y * h.y, n.z * h.z));
        v3 uu = V3(u.x * h.x, u.y * h.y, u.z * h.z), ww = V3(w.x * h.x, w.y * h.y, w.z * h.z);
        int a = mb_v(vsub(vsub(cc, uu), ww), n, col, 1), b = mb_v(vsub(vadd(cc, uu), ww), n, col, 1);
        int d = mb_v(vadd(vadd(cc, uu), ww), n, col, 1), e = mb_v(vadd(vsub(cc, uu), ww), n, col, 1);
        mb_q(a, b, d, e);
    }
}
// a chamfered box (44 triangles): half size h, the bevel b. The corners have soft normals (k toward the corner), so
// the light makes the box look rounded and soft (a toy look) for a fraction of the triangles of prim_rbox
static void prim_cbox(v3 c, v3 h, float b, uint32_t hex)
{
    float col[3];
    col3(col, hex);
    const float k = 0.42f;
    int id[8][3];
    for (int q = 0; q < 8; q++) {
        float sx = (q & 1) ? 1.0f : -1.0f, sy = (q & 2) ? 1.0f : -1.0f, sz = (q & 4) ? 1.0f : -1.0f;
        id[q][0] = mb_v(V3(c.x + sx * h.x, c.y + sy * (h.y - b), c.z + sz * (h.z - b)), vnorm(V3(sx, sy * k, sz * k)),
                        col, 1);
        id[q][1] = mb_v(V3(c.x + sx * (h.x - b), c.y + sy * h.y, c.z + sz * (h.z - b)), vnorm(V3(sx * k, sy, sz * k)),
                        col, 1);
        id[q][2] = mb_v(V3(c.x + sx * (h.x - b), c.y + sy * (h.y - b), c.z + sz * h.z), vnorm(V3(sx * k, sy * k, sz)),
                        col, 1);
    }
    // the 6 faces: the corners of each side (axis a, sign s), the vertex of that face at each corner
    for (int a = 0; a < 3; a++) {
        for (int s = 0; s < 2; s++) {
            int q4[4], n = 0;
            for (int q = 0; q < 8; q++) {
                if (((q >> a) & 1) == s) {
                    q4[n++] = q;
                }
            }
            // (the order round the face: 0, 1, 3, 2 of the 4 corners in the order of their bits)
            mb_q(id[q4[0]][a], id[q4[1]][a], id[q4[3]][a], id[q4[2]][a]);
        }
    }
    // the 12 bevels of the edges: between the faces a and b along the third axis
    for (int a = 0; a < 3; a++) {
        for (int b2 = a + 1; b2 < 3; b2++) {
            int third = 3 - a - b2;
            for (int sa = 0; sa < 2; sa++) {
                for (int sb = 0; sb < 2; sb++) {
                    int q0 = (sa << a) | (sb << b2), q1 = q0 | (1 << third);
                    mb_q(id[q0][a], id[q1][a], id[q1][b2], id[q0][b2]);
                }
            }
        }
    }
    // the 8 corners
    for (int q = 0; q < 8; q++) {
        mb_t(id[q][0], id[q][1], id[q][2]);
    }
}
// a flat disc (a fan) at c, its normal n, radius r, seg sides; the colour of the middle and of the rim
static void prim_disc(v3 c, v3 n, float r, int seg, uint32_t hm, uint32_t hr)
{
    float cm[3], cr[3];
    col3(cm, hm);
    col3(cr, hr);
    v3 u = absf(n.y) < 0.9f ? vnorm(vcross(n, V3(0, 1, 0))) : vnorm(vcross(n, V3(1, 0, 0)));
    v3 w = vcross(n, u);
    int m = mb_v(c, n, cm, 1), b0 = s_bnv;
    for (int i = 0; i < seg; i++) {
        float a = TAU_F * i / seg;
        mb_v(vadd(c, vadd(vmul(u, fcos(a) * r), vmul(w, fsin(a) * r))), n, cr, 1);
    }
    for (int i = 0; i < seg; i++) {
        mb_t(m, b0 + i, b0 + (i + 1) % seg);
    }
}
// a cylinder along y from y0 to y1 at (c.x, c.z): radius r0 at the bottom, r1 at the top, n sides; caps: 1 bottom,
// 2 top. Colours: side bottom, side top, cap
static void prim_cyl(v3 c, float y0, float y1, float r0, float r1, int n, int caps, uint32_t hb, uint32_t ht,
                     uint32_t hc)
{
    float cb[3], ct[3], cc[3];
    col3(cb, hb);
    col3(ct, ht);
    col3(cc, hc);
    float slope = (r0 - r1) / (y1 - y0);
    int base = s_bnv;
    for (int i = 0; i <= n; i++) {
        float a = TAU_F * i / n, ca = fcos(a), sa = fsin(a);
        v3 nn = vnorm(V3(ca, slope, sa));
        mb_v(V3(c.x + ca * r0, c.y + y0, c.z + sa * r0), nn, cb, 1);
        mb_v(V3(c.x + ca * r1, c.y + y1, c.z + sa * r1), nn, ct, 1);
    }
    for (int i = 0; i < n; i++) {
        int a = base + i * 2;
        mb_q(a, a + 2, a + 3, a + 1);
    }
    for (int cap = 0; cap < 2; cap++) {
        if (!(caps & (1 << cap))) {
            continue;
        }
        float y = cap ? y1 : y0, r = cap ? r1 : r0;
        v3 nn = V3(0, cap ? 1 : -1, 0);
        int m = mb_v(V3(c.x, c.y + y, c.z), nn, cc, 1);
        int b0 = s_bnv;
        for (int i = 0; i <= n; i++) {
            float a = TAU_F * i / n;
            mb_v(V3(c.x + fcos(a) * r, c.y + y, c.z + fsin(a) * r), nn, cc, 1);
        }
        for (int i = 0; i < n; i++) {
            if (cap) {
                mb_t(m, b0 + i, b0 + i + 1);
            } else {
                mb_t(m, b0 + i + 1, b0 + i);
            }
        }
    }
}
// an ellipsoid: centre c, radii r; rings x segs; colour top and bottom (a soft gradient)
static void prim_ell(v3 c, v3 r, int rings, int segs, uint32_t htop, uint32_t hbot)
{
    float ct[3], cb[3], col[3];
    col3(ct, htop);
    col3(cb, hbot);
    int base = s_bnv;
    for (int i = 0; i <= rings; i++) {
        float th = PI_F * i / rings, sy = fcos(th), sr = fsin(th);
        col_mix(col, ct, cb, (float)i / rings);
        for (int j = 0; j <= segs; j++) {
            float ph = TAU_F * j / segs;
            v3 u = V3(sr * fcos(ph), sy, sr * fsin(ph));
            v3 n = vnorm(V3(u.x / r.x, u.y / r.y, u.z / r.z));
            mb_v(V3(c.x + u.x * r.x, c.y + u.y * r.y, c.z + u.z * r.z), n, col, 1);
        }
    }
    for (int i = 0; i < rings; i++) {
        for (int j = 0; j < segs; j++) {
            int a = base + i * (segs + 1) + j, b = a + 1, d = a + segs + 1, e = d + 1;
            mb_q(a, d, e, b);
        }
    }
}
// a tube along points p[0..n-1] with the radius rr[i]; k sides; colours a gradient from the first to the last point
static void prim_tube(const v3 *p, const float *rr, int n, int k, uint32_t h0, uint32_t h1, bool caps)
{
    float c0[3], c1[3], col[3];
    col3(c0, h0);
    col3(c1, h1);
    int base = s_bnv;
    v3 prev_side = V3(1, 0, 0);
    for (int i = 0; i < n; i++) {
        v3 t = vnorm(vsub(p[i + 1 < n ? i + 1 : i], p[i > 0 ? i - 1 : i]));
        v3 s = vcross(t, prev_side);
        if (vlen(s) < 0.1f) {
            s = vcross(t, V3(0, 1, 0));
        }
        if (vlen(s) < 0.1f) {
            s = vcross(t, V3(0, 0, 1));
        }
        s = vnorm(s);
        v3 u = vnorm(vcross(s, t));
        prev_side = u;
        col_mix(col, c0, c1, n > 1 ? (float)i / (n - 1) : 0);
        for (int j = 0; j <= k; j++) {
            float a = TAU_F * j / k;
            v3 d = vadd(vmul(s, fcos(a)), vmul(u, fsin(a)));
            mb_v(vadd(p[i], vmul(d, rr[i])), d, col, 1);
        }
    }
    for (int i = 0; i + 1 < n; i++) {
        for (int j = 0; j < k; j++) {
            int a = base + i * (k + 1) + j, b = a + 1, d = a + k + 1, e = d + 1;
            mb_q(a, b, e, d);
        }
    }
    if (caps) {
        for (int e = 0; e < 2; e++) {
            int i = e ? n - 1 : 0;
            v3 t = vnorm(vsub(p[e ? n - 1 : 1], p[e ? n - 2 : 0]));
            if (!e) {
                t = vmul(t, -1);
            }
            col_mix(col, c0, c1, (float)e);
            int m = mb_v(p[i], t, col, 1);
            int r0 = base + i * (k + 1);
            for (int j = 0; j < k; j++) {
                if (e) {
                    mb_t(m, r0 + j, r0 + j + 1);
                } else {
                    mb_t(m, r0 + j + 1, r0 + j);
                }
            }
        }
    }
}
// a two-sided leaf (a palm frond): a strip along the points p[] with the half width hw[] across the side vector s
static void prim_leaf(const v3 *p, const float *hw, int n, v3 s, uint32_t h0, uint32_t h1)
{
    float c0[3], c1[3], col[3];
    col3(c0, h0);
    col3(c1, h1);
    for (int side = 0; side < 2; side++) {
        int base = s_bnv;
        for (int i = 0; i < n; i++) {
            v3 t = vnorm(vsub(p[i + 1 < n ? i + 1 : i], p[i > 0 ? i - 1 : i]));
            v3 nn = vnorm(vcross(s, t));
            if (side) {
                nn = vmul(nn, -1);
            }
            // (a fold: the two halves of the leaf bend down a little)
            col_mix(col, c0, c1, n > 1 ? (float)i / (n - 1) : 0);
            v3 lift = vmul(nn, side ? -0.01f : 0.01f);
            mb_v(vadd(vadd(p[i], vmul(s, -hw[i])), lift), nn, col, 1);
            mb_v(vadd(vadd(p[i], vmul(s, hw[i])), lift), nn, col, 1);
        }
        for (int i = 0; i + 1 < n; i++) {
            int a = base + i * 2;
            mb_q(a, a + 2, a + 3, a + 1);
        }
    }
}

// ---------------------------------------------------------------- the world (the light baked in)
// a corner of the world (10 bytes: less PSRAM traffic in each frame): the place in steps of 1/WQ unit from the origin
// of its chunk, the colour with the light baked in (0..255; the foam: r = the strength), the sway (the palm leaves and
// the bushes: x 200; the shelf of shallow water: the part of the sea colour x 255)
#define WQ 512.0f
typedef struct {
    int16_t x, y, z;
    uint8_t r, g, b, sway;
} wv_t;
// a chunk: its corners and triangles, the middle and the radius, the kind; the level of detail (LOD_*) and the middle
// of its prop (the distance for the level)
typedef struct {
    int v0, nv, t0, nt;
    v3 c;
    float rad;
    uint8_t kind, lod;
    v3 anc;
    float lodd;
} chunk_t;
enum {
    LOD_FINE,
    LOD_COARSE,
    LOD_ALL
};
#define LOD_D 20.0f           // (the fine props nearer than this to the eye, the coarse ones farther)
static int s_blod;            // (the build: 0 the fine props, 1 the coarse props)
static int s_wlod = LOD_ALL;  // (the build: the level of the chunks that world_add makes now)
static float s_wlodd;         // (the build: > 0 = each chunk changes its level at this distance of its own middle)
enum {
    CK_SOLID,
    CK_FOAM,
    CK_SHELF
};
#define WMAXV 24000
#define WMAXT 32000
#define WMAXC 800
// the cells of the world: WCN x WCN cells of WCELL units from (-WCELL0, -WCELL0) (the sea round the island too)
#define WCELL  20.0f
#define WCELL0 150.0f
#define WCN    15
static wv_t *s_wv;
static tri_t *s_wt;
static chunk_t s_ch[WMAXC];
static int s_wnv, s_wnt, s_nch;
static uint16_t s_wmap[BMAXV];  // (world_add: a corner of the builder -> its corner in the chunk now)
static uint8_t s_wcell[BMAXT];  // (world_add: the cell of each triangle of the builder)

// the shape of the builder becomes chunks of the world (kind: CK_*): one chunk for each cell of WCELL x WCELL units
// that has triangles (a triangle goes to the cell of its middle; a corner of two cells goes into both chunks), so the
// parts out of the view are not processed
static void world_add(int kind)
{
    if (s_bnv == 0) {
        mb_begin();
        return;
    }
    v3 alo = V3(1e9f, 1e9f, 1e9f), ahi = V3(-1e9f, -1e9f, -1e9f);
    for (int i = 0; i < s_bnv; i++) {
        const bv_t *b = &s_bv[i];
        alo = V3(FMIN(alo.x, b->x), FMIN(alo.y, b->y), FMIN(alo.z, b->z));
        ahi = V3(FMAX(ahi.x, b->x), FMAX(ahi.y, b->y), FMAX(ahi.z, b->z));
    }
    v3 anc = vmul(vadd(alo, ahi), 0.5f);
#ifndef ESP_PLATFORM
    if (getenv("CB_WSTAT")) {
        printf("world_add kind %d: %d corners %d triangles\n", kind, s_bnv, s_bnt);
    }
#endif
    static int used[WCN * WCN];
    memset(used, 0, sizeof used);
    for (int i = 0; i < s_bnt; i++) {
        const bv_t *a = &s_bv[s_bt[i].a], *b = &s_bv[s_bt[i].b], *c = &s_bv[s_bt[i].c];
        float mx = (a->x + b->x + c->x) * (1.0f / 3), mz = (a->z + b->z + c->z) * (1.0f / 3);
        int gx = clampi(ifloor((mx + WCELL0) * (1.0f / WCELL)), 0, WCN - 1),
            gz = clampi(ifloor((mz + WCELL0) * (1.0f / WCELL)), 0, WCN - 1);
        s_wcell[i] = (uint8_t)(gx * WCN + gz);
        used[s_wcell[i]]++;
    }
    for (int q = 0; q < WCN * WCN; q++) {
        if (!used[q]) {
            continue;
        }
        if (s_nch >= WMAXC) {
            break;
        }
        memset(s_wmap, 0xFF, sizeof(uint16_t) * s_bnv);
        int nv = 0;
        v3 lo = V3(1e9f, 1e9f, 1e9f), hi = V3(-1e9f, -1e9f, -1e9f);
        for (int i = 0; i < s_bnt; i++) {
            if (s_wcell[i] != q) {
                continue;
            }
            const uint16_t id[3] = {s_bt[i].a, s_bt[i].b, s_bt[i].c};
            for (int k = 0; k < 3; k++) {
                if (s_wmap[id[k]] != 0xFFFF) {
                    continue;
                }
                s_wmap[id[k]] = (uint16_t)nv++;
                const bv_t *b = &s_bv[id[k]];
                lo = V3(FMIN(lo.x, b->x), FMIN(lo.y, b->y), FMIN(lo.z, b->z));
                hi = V3(FMAX(hi.x, b->x), FMAX(hi.y, b->y), FMAX(hi.z, b->z));
            }
        }
        if (s_wnv + nv > WMAXV || s_wnt + used[q] > WMAXT) {
            break;
        }
        chunk_t *ch = &s_ch[s_nch++];
        ch->v0 = s_wnv;
        ch->nv = nv;
        ch->t0 = s_wnt;
        ch->nt = used[q];
        ch->kind = (uint8_t)kind;
        ch->lod = (uint8_t)s_wlod;
        ch->anc = anc;
        ch->c = vmul(vadd(lo, hi), 0.5f);
        ch->rad = vlen(vsub(hi, lo)) * 0.5f + 0.2f;
        ch->lodd = s_wlodd;
        if (s_wlodd > 0) {
            ch->anc = ch->c;
        }
        for (int i = 0; i < s_bnv; i++) {
            if (s_wmap[i] == 0xFFFF) {
                continue;
            }
            const bv_t *b = &s_bv[i];
            wv_t *w = &s_wv[s_wnv + s_wmap[i]];
            w->x = (int16_t)clampi((int)((b->x - ch->c.x) * WQ + (b->x >= ch->c.x ? 0.5f : -0.5f)), -32767, 32767);
            w->y = (int16_t)clampi((int)((b->y - ch->c.y) * WQ + (b->y >= ch->c.y ? 0.5f : -0.5f)), -32767, 32767);
            w->z = (int16_t)clampi((int)((b->z - ch->c.z) * WQ + (b->z >= ch->c.z ? 0.5f : -0.5f)), -32767, 32767);
            float o[3];
            if (kind == CK_SOLID) {
                light_rgb(&b->r, V3(b->nx, b->ny, b->nz), b->ao, b->mat, NULL, o);
            } else {
                o[0] = b->r * 255;
                o[1] = b->g * 255;
                o[2] = b->b * 255;
            }
            w->r = (uint8_t)clampi((int)(o[0] + 0.5f), 0, 255);
            w->g = (uint8_t)clampi((int)(o[1] + 0.5f), 0, 255);
            w->b = (uint8_t)clampi((int)(o[2] + 0.5f), 0, 255);
            w->sway = (uint8_t)clampi((int)(b->sway * (kind == CK_SHELF ? 255 : 200) + 0.5f), 0, 255);
        }
        int nt = 0;
        for (int i = 0; i < s_bnt; i++) {
            if (s_wcell[i] != q) {
                continue;
            }
            s_wt[s_wnt + nt++] = (tri_t){s_wmap[s_bt[i].a], s_wmap[s_bt[i].b], s_wmap[s_bt[i].c]};
        }
        s_wnv += nv;
        s_wnt += nt;
    }
    mb_begin();
}

// ---------------------------------------------------------------- the models (the light in each frame)
// (a model that does not turn has its light baked at the start: lit = true; r, g, b are then 0..255. The parts of the
// robot turn: their light is made in each frame from the albedo)
// a corner of a model (20 bytes): the place, the normal x 127, the albedo x 255 (a model with the light baked: the colour
// 0..255), the ambient light x 255, the material
typedef struct {
    float x, y, z;
    int8_t nx, ny, nz;
    uint8_t r, g, b, ao, mat;
} mv_t;
typedef struct {
    int v0, nv, t0, nt;
    float rad;
    bool lit;
} model_t;
#define MMAXV 9000
#define MMAXT 14000
static mv_t *s_mv;
static tri_t *s_mt;
static int s_mnv, s_mnt;
// the shape of the builder becomes a model
static void model_add(model_t *m)
{
    m->v0 = s_mnv;
    m->nv = 0;
    m->t0 = s_mnt;
    m->nt = 0;
    m->rad = 0;
    m->lit = false;
    if (s_mnv + s_bnv > MMAXV || s_mnt + s_bnt > MMAXT) {
        mb_begin();
        return;
    }
    for (int i = 0; i < s_bnv; i++) {
        const bv_t *b = &s_bv[i];
        mv_t *v = &s_mv[s_mnv + i];
        v->x = b->x;
        v->y = b->y;
        v->z = b->z;
        v->nx = (int8_t)clampi((int)(b->nx * 127 + (b->nx >= 0 ? 0.5f : -0.5f)), -127, 127);
        v->ny = (int8_t)clampi((int)(b->ny * 127 + (b->ny >= 0 ? 0.5f : -0.5f)), -127, 127);
        v->nz = (int8_t)clampi((int)(b->nz * 127 + (b->nz >= 0 ? 0.5f : -0.5f)), -127, 127);
        v->r = (uint8_t)clampi((int)(b->r * 255 + 0.5f), 0, 255);
        v->g = (uint8_t)clampi((int)(b->g * 255 + 0.5f), 0, 255);
        v->b = (uint8_t)clampi((int)(b->b * 255 + 0.5f), 0, 255);
        v->ao = (uint8_t)clampi((int)(b->ao * 255 + 0.5f), 0, 255);
        v->mat = b->mat;
        float r = fsqrt(b->x * b->x + b->y * b->y + b->z * b->z);
        if (r > m->rad) {
            m->rad = r;
        }
    }
    memcpy(&s_mt[s_mnt], s_bt, sizeof(tri_t) * s_bnt);
    m->nv = s_bnv;
    m->nt = s_bnt;
    s_mnv += s_bnv;
    s_mnt += s_bnt;
    mb_begin();
}
// the light of a model that does not turn: baked now (no shine and no rim, they need the eye)
static void model_bake(model_t *m)
{
    for (int i = 0; i < m->nv; i++) {
        mv_t *v = &s_mv[m->v0 + i];
        float a[3] = {v->r * (1.0f / 255), v->g * (1.0f / 255), v->b * (1.0f / 255)}, o[3];
        light_rgb(a, vnorm(V3(v->nx, v->ny, v->nz)), v->ao * (1.0f / 255), v->mat, NULL, o);
        v->r = (uint8_t)clampi((int)(o[0] + 0.5f), 0, 255);
        v->g = (uint8_t)clampi((int)(o[1] + 0.5f), 0, 255);
        v->b = (uint8_t)clampi((int)(o[2] + 0.5f), 0, 255);
    }
    m->lit = true;
}

// ---------------------------------------------------------------- the frame: the corners on the screen, the list
// a triangle of a frame (8 bytes: less PSRAM traffic): the 3 corners (14 bits each; the kind in the top bits of a, the
// top bit of the colour in b), the tiles that its box touches (tx0, tx1: 3 bits each, ty0, ty1: 4 bits each) and the
// low 2 bits of the colour
typedef struct {
    uint16_t a, b, c, tr;
} dt_t;
#define DT_I(v)    ((v) & 0x3FFF)
#define DT_KIND(d) ((d)->a >> 14)
#define DT_COL(d)  (((d)->tr >> 14) | (((d)->b >> 15) << 2))
#define DT_TX0(d)  ((d)->tr & 7)
#define DT_TX1(d)  (((d)->tr >> 3) & 7)
#define DT_TY0(d)  (((d)->tr >> 6) & 15)
#define DT_TY1(d)  (((d)->tr >> 10) & 15)
_Static_assert(TCOLS_MAX <= 8 && TROWS_MAX <= 16, "the tile range of a triangle does not fit");
enum {
    DK_SOLID,
    DK_SOLID_NM,
    DK_ADD,
    DK_DARK
};
#define PMAXV 12000
#define DMAXT 16000
_Static_assert(PMAXV <= 16384, "a corner index has 14 bits");
// THE TWO SETS (the pipeline of app_cubby.c): the cores make the list of a frame in the build set (s_pv, s_dt, s_bin,
// s_bin_at, s_glb) while the tiles draw the set of the frame before (s_pvd, s_dtd, s_bind, s_bin_atd, s_gld)
static pv_t *s_pv, *s_pvd, *s_pvm[2];
static dt_t *s_dt, *s_dtd, *s_dtm[2];
// the two lanes of the frame list: lane 0 (core 0: the robot, the things) and lane 1 (core 1: the island) fill their
// own halves of the corners and of the triangles at the same time
typedef struct {
    int pn, pv_end, dn, dt_end, dropped;
} lane_t;
static lane_t s_lane[2];
#define PV_HALF (PMAXV / 2)
#define DT_HALF (DMAXT / 2)
static void lanes_reset(void)
{
    s_lane[0] = (lane_t){0, PV_HALF, 0, DT_HALF, 0};
    s_lane[1] = (lane_t){PV_HALF, PMAXV, DT_HALF, DMAXT, 0};
}
// the colours of the light triangles (DK_ADD: col is an index)
// (INK KARTS: white, gold, then the 6 inks of the racers; the colour index has 3 bits in dt_t)
static const uint8_t ADD_COL[8][3] = {{255, 255, 255}, {255, 214, 120}, {40, 225, 255}, {255, 60, 205},
                                      {255, 222, 30},  {120, 255, 70},  {255, 130, 30}, {170, 100, 255}};
enum {
    AC_WHITE,
    AC_GOLD,
    AC_INK0
};

// the haze of the distance on a colour (o: 0..255 a channel)
static inline void haze(float dist, float *o)
{
    float k = (dist - LK.haze0) * LK.haze_d;
    if (k <= 0) {
        return;
    }
    if (k > LK.haze_max) {
        k = LK.haze_max;
    }
    o[0] += (HAZE_R - o[0]) * k;
    o[1] += (HAZE_G - o[1]) * k;
    o[2] += (HAZE_B - o[2]) * k;
}
// a corner in camera space with its colour: the index in the frame list
static inline int pv_add_l(lane_t *L, v3 c, float r, float g, float b)
{
    if (L->pn >= L->pv_end) {
        return -1;
    }
    pv_t *p = &s_pv[L->pn];
    p->r = (uint8_t)(r < 1 ? 1 : r > 247 ? 247 : r);
    p->g = (uint8_t)(g < 1 ? 1 : g > 251 ? 251 : g);
    p->b = (uint8_t)(b < 1 ? 1 : b > 247 ? 247 : b);
    if (c.z >= NEARZ) {
        float iz = frcp(c.z);
        p->x = C.cx + C.fx * c.x * iz;
        p->y = C.cy - C.f * c.y * iz;
        p->z = ZK * iz;
        p->f = 0;
    } else {
        p->x = c.x;
        p->y = c.y;
        p->z = c.z;
        p->f = 1;
    }
    return L->pn++;
}
#define pv_add_cam(c, r, g, b) pv_add_l(&s_lane[0], c, r, g, b)
// the camera space point of a corner of the frame list
static inline v3 pv_cam(const pv_t *p)
{
    if (p->f) {
        return V3(p->x, p->y, p->z);
    }
    float cz = ZK * frcp(p->z);
    return V3((p->x - C.cx) * cz * frcp(C.fx), (C.cy - p->y) * cz * frcp(C.f), cz);
}
// a triangle of the frame list (a, b, c: indices of s_pv). kind DK_*; back faces of solids go out
static void dl_tri_l(lane_t *L, int a, int b, int c, int kind, int col)
{
    if (a < 0 || b < 0 || c < 0) {
        return;
    }
    const pv_t *A = &s_pv[a], *B = &s_pv[b], *Cc = &s_pv[c];
    int nin = !A->f + !B->f + !Cc->f;
    if (nin == 0) {
        return;
    }
    if (nin < 3) {
        // the cut at the near plane: at most 4 corners, then a fan
        const pv_t *in[3] = {A, B, Cc};
        int ids[4], n = 0;
        int src[3] = {a, b, c};
        for (int i = 0; i < 3; i++) {
            const pv_t *u = in[i], *v = in[(i + 1) % 3];
            bool ui = !u->f, vi = !v->f;
            if (ui) {
                ids[n++] = src[i];
            }
            if (ui != vi) {
                v3 cu = pv_cam(u), cv = pv_cam(v);
                float t = (NEARZ - cu.z) * frcp(cv.z - cu.z);
                v3 q = V3(cu.x + (cv.x - cu.x) * t, cu.y + (cv.y - cu.y) * t, NEARZ);
                int k = pv_add_l(L, q, u->r + (v->r - u->r) * t, u->g + (v->g - u->g) * t, u->b + (v->b - u->b) * t);
                if (k < 0) {
                    return;
                }
                ids[n++] = k;
            }
        }
        for (int i = 1; i + 1 < n; i++) {
            dl_tri_l(L, ids[0], ids[i], ids[i + 1], kind, col);
        }
        return;
    }
    if (kind <= DK_SOLID_NM) {
        float area = (B->x - A->x) * (Cc->y - A->y) - (Cc->x - A->x) * (B->y - A->y);
        if (area >= 0) {
            return;  // (the screen y goes down: a front face has a negative area)
        }
    }
    float ymin = FMIN(A->y, FMIN(B->y, Cc->y)), ymax = FMAX(A->y, FMAX(B->y, Cc->y));
    float xmin = FMIN(A->x, FMIN(B->x, Cc->x)), xmax = FMAX(A->x, FMAX(B->x, Cc->x));
    // (a triangle with no pixel centre: it falls between the rows or the columns)
    if (iceil(ymin - 0.5f) >= iceil(ymax - 0.5f) || iceil(xmin - 0.5f) >= iceil(xmax - 0.5f)) {
        return;
    }
    if (ymax < 0 || ymin > VH || xmax < 0 || xmin > VW) {
        return;
    }
    if (L->dn >= L->dt_end) {
        L->dropped++;
        return;
    }
    dt_t *d = &s_dt[L->dn++];
    // (the tiles of the box with a margin of 1 px)
    int ty0 = clampi((int)(ymin - 1), 0, VH - 1) / TLH, ty1 = clampi((int)(ymax + 1), 0, VH - 1) / TLH;
    int tx0 = clampi((int)(xmin - 1), 0, VW - 1) / TLW, tx1 = clampi((int)(xmax + 1), 0, VW - 1) / TLW;
    d->a = (uint16_t)(a | kind << 14);
    d->b = (uint16_t)(b | (col >> 2) << 15);
    d->c = (uint16_t)c;
    d->tr = (uint16_t)(tx0 | tx1 << 3 | ty0 << 6 | ty1 << 10 | (col & 3) << 14);
}

// the colour of the sea at a distance (app_cubby.c)
static void sea_colour(float dist, float *o);
// the time of the world (seconds) for the sway of the leaves and the foam
static float s_wtime;
// a chunk of the world into the list
#define dl_tri(a, b, c, k, col) dl_tri_l(&s_lane[0], a, b, c, k, col)
static void dl_chunk(lane_t *L, const chunk_t *ch)
{
    v3 cc = xf_pt(&C.w2c, ch->c);
    if (!cam_sees(cc, ch->rad)) {
        return;
    }
    int base = L->pn;
    if (L->pn + ch->nv > L->pv_end - 64) {
        L->dropped += ch->nt;
        return;
    }
    int core = L == &s_lane[1];
    uint32_t c0 = cy_now();
    // (the corners: the camera frame of the chunk with the scale of the steps, so a corner is 3 whole numbers to floats
    // and one transform)
    xf_t m;
    const float iq = 1.0f / WQ;
    for (int k = 0; k < 9; k++) {
        m.r[k] = C.w2c.r[k] * iq;
    }
    m.t = cc;
    for (int i = 0; i < ch->nv; i++) {
        const wv_t *w = &s_wv[ch->v0 + i];
        v3 p = V3(w->x, w->y, w->z);
        if (w->sway && ch->kind == CK_SOLID) {
            float sw = w->sway * (WQ / 200.0f);
            float ph = (ch->c.x + w->x * iq) * 0.7f + (ch->c.z + w->z * iq) * 0.5f;
            p.x += fsin(s_wtime * 1.3f + ph) * sw * 0.10f;
            p.z += fsin(s_wtime * 1.1f + ph * 1.3f) * sw * 0.07f;
            p.y += fsin(s_wtime * 2.1f + ph) * sw * 0.03f;
        }
        v3 c = xf_pt(&m, p);
        float o[3] = {w->r, w->g, w->b};
        if (ch->kind == CK_SOLID) {
            haze(c.z, o);
        } else if (ch->kind == CK_FOAM) {
            // (the foam: r = the strength; it comes and goes with a wave along the shore)
            float k =
                    0.55f + 0.45f * fsin(s_wtime * 1.6f + (ch->c.x + w->x * iq) * 0.9f + (ch->c.z + w->z * iq) * 0.7f);
            o[0] = w->r * k;
        } else {
            // (the shelf of shallow water: the outer corners take the colour of the sea at that distance)
            float t = w->sway * (1.0f / 255);
            float sea[3];
            sea_colour(c.z, sea);
            for (int k = 0; k < 3; k++) {
                o[k] = o[k] + (sea[k] - o[k]) * t;
            }
        }
        pv_add_l(L, c, o[0], o[1], o[2]);
    }
    uint32_t c1 = cy_now();
    s_cy[core][CY_CCORN] += c1 - c0;
    s_cn[core][CN_CCORN] += (uint32_t)ch->nv;
    int kind = ch->kind == CK_FOAM ? DK_ADD : DK_SOLID;
    for (int i = 0; i < ch->nt; i++) {
        const tri_t *t = &s_wt[ch->t0 + i];
        dl_tri_l(L, base + t->a, base + t->b, base + t->c, kind, AC_WHITE);
    }
    s_cy[core][CY_CTRI] += cy_now() - c1;
    s_cn[core][CN_CTRI] += (uint32_t)ch->nt;
}
// THE LIGHT CACHE OF THE ROBOT: each part of the robot (slot) keeps the light of its corners; in each frame half of the
// parts make it again (the light of a part changes slowly: one frame of delay does not show)
#define LC_MAX 2400
#define NSLOT  80
typedef struct {
    const model_t *m;
    int off;
} lslot_t;
static lslot_t s_lslot[NSLOT];
static uint8_t s_lc[LC_MAX * 3];
static int s_lc_used;
static uint32_t s_lframe;
// a model into the list: the frame x (model space to the world), a tint (1 = none), a flash (0..1: to white), the slot
// of the light cache (-1: none)
static void dl_model_s(const model_t *m, const xf_t *x, float tint, float flash, int slot)
{
    if (!m->nv) {
        return;
    }
    v3 wc = x->t;
    float sc = fsqrt(x->r[0] * x->r[0] + x->r[3] * x->r[3] + x->r[6] * x->r[6]);
    float sc2 = fsqrt(x->r[1] * x->r[1] + x->r[4] * x->r[4] + x->r[7] * x->r[7]);
    if (sc2 > sc) {
        sc = sc2;
    }
    v3 cc = xf_pt(&C.w2c, wc);
    if (!cam_sees(cc, m->rad * sc * 1.5f + 0.1f)) {
        return;
    }
    lane_t *L0 = &s_lane[0];
    if (L0->pn + m->nv > L0->pv_end - 64) {
        L0->dropped += m->nt;
        return;
    }
    int base = L0->pn;
    const float *r = x->r;
    float sx2 = r[0] * r[0] + r[3] * r[3] + r[6] * r[6], sy2 = r[1] * r[1] + r[4] * r[4] + r[7] * r[7],
          sz2 = r[2] * r[2] + r[5] * r[5] + r[8] * r[8];
    // (the light in the frame of the part: the sun, the up, the eye and the half vector turned back by the turn of the
    // part, once; then 4 dot products for each corner)
    v3 Ll = V3(0, 0, 0), Ul = Ll, El = Ll, Hl = Ll;
    uint8_t *lc = NULL;
    bool relight = !m->lit;
    if (!m->lit && slot >= 0 && slot < NSLOT) {
        lslot_t *S = &s_lslot[slot];
        if (S->m != m) {
            S->m = NULL;
            if (s_lc_used + m->nv <= LC_MAX) {
                S->m = m;
                S->off = s_lc_used;
                s_lc_used += m->nv;
            }
        } else {
            relight = ((s_lframe + (uint32_t)slot) & 1) == 0;
        }
        if (S->m == m) {
            lc = s_lc + S->off * 3;
        }
    }
    if (relight) {
        // (the normals of the corners are x 127: the vectors take 1 / 127)
        v3 e = vnorm(vsub(C.eye, wc)), hv = vnorm(vadd(L_SUN, e));
        float i0 = frsqrt(sx2) * (1.0f / 127), i1 = frsqrt(sy2) * (1.0f / 127), i2 = frsqrt(sz2) * (1.0f / 127);
        v3 c0 = V3(r[0] * i0, r[3] * i0, r[6] * i0), c1 = V3(r[1] * i1, r[4] * i1, r[7] * i1),
           c2 = V3(r[2] * i2, r[5] * i2, r[8] * i2);
        Ll = V3(vdot(c0, L_SUN), vdot(c1, L_SUN), vdot(c2, L_SUN));
        Ul = V3(c0.y, c1.y, c2.y);
        El = V3(vdot(c0, e), vdot(c1, e), vdot(c2, e));
        Hl = V3(vdot(c0, hv), vdot(c1, hv), vdot(c2, hv));
    }
    // (the model to the camera in one frame; the haze only when the part is far)
    xf_t m2c;
    xf_mul(&m2c, &C.w2c, x);
    bool far = cc.z > 16.0f;
    uint32_t c0 = cy_now();
    for (int i = 0; i < m->nv; i++) {
        const mv_t *v = &s_mv[m->v0 + i];
        v3 c = xf_pt(&m2c, V3(v->x, v->y, v->z));
        float o[3];
        if (m->lit) {
            o[0] = v->r;
            o[1] = v->g;
            o[2] = v->b;
        } else if (!relight) {
            const uint8_t *q = lc + i * 3;
            o[0] = q[0];
            o[1] = q[1];
            o[2] = q[2];
        } else {
            v3 n = V3(v->nx, v->ny, v->nz);
            const float a[3] = {v->r * (1.0f / 255), v->g * (1.0f / 255), v->b * (1.0f / 255)};
            light_fast(a, vdot(n, Ll), vdot(n, Ul), vdot(n, El), vdot(n, Hl), v->ao * (1.0f / 255), v->mat, o);
            if (lc) {
                uint8_t *q = lc + i * 3;
                q[0] = (uint8_t)o[0];
                q[1] = (uint8_t)o[1];
                q[2] = (uint8_t)o[2];
            }
        }
        if (tint != 1.0f) {
            o[0] *= tint;
            o[1] *= tint;
            o[2] *= tint;
        }
        if (flash > 0) {
            o[0] += (247 - o[0]) * flash;
            o[1] += (251 - o[1]) * flash;
            o[2] += (247 - o[2]) * flash;
        }
        if (far) {
            haze(c.z, o);
        }
        pv_add_cam(c, o[0], o[1], o[2]);
    }
    uint32_t c1 = cy_now();
    s_cy[0][CY_MCORN] += c1 - c0;
    s_cn[0][CN_MCORN] += (uint32_t)m->nv;
    for (int i = 0; i < m->nt; i++) {
        const tri_t *t = &s_mt[m->t0 + i];
        dl_tri(base + t->a, base + t->b, base + t->c, DK_SOLID, 0);
    }
    s_cy[0][CY_MTRI] += cy_now() - c1;
    s_cn[0][CN_MTRI] += (uint32_t)m->nt;
}
#define dl_model(m, x, tint, flash) dl_model_s(m, x, tint, flash, -1)

// the bins: the triangles of the list for each tile (a stable sort by tile: the order of the list stays)
#define BINMAX 40000
static uint16_t *s_bin, *s_bind, *s_binm[2];  // (PSRAM) the triangle numbers, tile by tile
static int s_bin_atm[2][NTILE_MAX + 1];
static int *s_bin_at = s_bin_atm[0], *s_bin_atd = s_bin_atm[0];
static int s_bin_lost;
static void bins_build(void)
{
    static int cnt[NTILE_MAX];
    memset(cnt, 0, sizeof cnt);
    for (int l = 0; l < 2; l++) {
        for (int i = l ? DT_HALF : 0; i < s_lane[l].dn; i++) {
            const dt_t *d = &s_dt[i];
            int ty0 = DT_TY0(d), ty1 = DT_TY1(d), tx0 = DT_TX0(d), tx1 = DT_TX1(d);
            for (int ty = ty0; ty <= ty1; ty++) {
                for (int tx = tx0; tx <= tx1; tx++) {
                    cnt[ty * TCOLS + tx]++;
                }
            }
        }
    }
    int a = 0;
    for (int k = 0; k < NTILE; k++) {
        s_bin_at[k] = a;
        a += cnt[k];
        cnt[k] = s_bin_at[k];
    }
    s_bin_at[NTILE] = a;
    s_bin_lost = a > BINMAX ? a - BINMAX : 0;
    for (int l = 0; l < 2; l++) {
        for (int i = l ? DT_HALF : 0; i < s_lane[l].dn; i++) {
            const dt_t *d = &s_dt[i];
            int ty0 = DT_TY0(d), ty1 = DT_TY1(d), tx0 = DT_TX0(d), tx1 = DT_TX1(d);
            for (int ty = ty0; ty <= ty1; ty++) {
                for (int tx = tx0; tx <= tx1; tx++) {
                    int k = ty * TCOLS + tx;
                    if (cnt[k] < BINMAX) {
                        s_bin[cnt[k]] = (uint16_t)i;
                    }
                    cnt[k]++;
                }
            }
        }
    }
}
// the triangles of the bin of tile k, the kinds kind_lo..kind_hi
static void dl_draw(tile_t *t, int k, int kind_lo, int kind_hi)
{
    int a = s_bin_atd[k], b = s_bin_atd[k + 1];
    if (b > BINMAX) {
        b = BINMAX;
    }
    for (int q = a; q < b; q++) {
        const dt_t *d = &s_dtd[s_bind[q]];
        int kind = DT_KIND(d);
        if (kind < kind_lo || kind > kind_hi) {
            continue;
        }
        const pv_t *pa = &s_pvd[DT_I(d->a)], *pb = &s_pvd[DT_I(d->b)], *pc = &s_pvd[DT_I(d->c)];
        switch (kind) {
        case DK_SOLID:
            tri_rgb(t, pa, pb, pc, true);
            break;
        case DK_SOLID_NM:
            tri_rgb(t, pa, pb, pc, false);
            break;
        case DK_ADD: {
            int col = DT_COL(d);
            tri_add(t, pa, pb, pc, ADD_COL[col][0], ADD_COL[col][1], ADD_COL[col][2]);
        } break;
        default:
            tri_dark(t, pa, pb, pc);
            break;
        }
    }
}

// a soft shadow on the ground: a disc at (x, y, z) with the radii rx, rz and the strength k (0..1), turned by yaw
static void dl_shadow(v3 at, float rx, float rz, float yaw, float k)
{
    if (k <= 0.01f) {
        return;
    }
    v3 cc = xf_pt(&C.w2c, at);
    if (!cam_sees(cc, FMAX(rx, rz) + 0.2f)) {
        return;
    }
    if (s_lane[0].pn + 40 > s_lane[0].pv_end) {
        return;
    }
    const int N = 10;
    float cy = fcos(yaw), sy = fsin(yaw);
    int m = pv_add_cam(cc, 256 * k, 0, 0);
    int b0 = s_lane[0].pn;
    for (int i = 0; i <= N; i++) {
        float a = TAU_F * i / N, lx = fcos(a) * rx, lz = fsin(a) * rz;
        v3 p = V3(at.x + lx * cy + lz * sy, at.y, at.z - lx * sy + lz * cy);
        pv_add_cam(xf_pt(&C.w2c, p), 0, 0, 0);
    }
    // (an inner ring at 55 %: the shadow is soft at the edge)
    int b1 = s_lane[0].pn;
    for (int i = 0; i <= N; i++) {
        float a = TAU_F * i / N, lx = fcos(a) * rx * 0.55f, lz = fsin(a) * rz * 0.55f;
        v3 p = V3(at.x + lx * cy + lz * sy, at.y, at.z - lx * sy + lz * cy);
        pv_add_cam(xf_pt(&C.w2c, p), 256 * k * 0.85f, 0, 0);
    }
    for (int i = 0; i < N; i++) {
        dl_tri(m, b1 + i, b1 + i + 1, DK_DARK, 0);
        dl_tri(b1 + i, b0 + i, b0 + i + 1, DK_DARK, 0);
        dl_tri(b1 + i, b0 + i + 1, b1 + i + 1, DK_DARK, 0);
    }
}
// a soft glow in the world (the light of a thing): its depth for the z test
typedef struct {
    float x, y, r, z;
    uint8_t col;
    int a;
} glow_t;
#define GMAX 96
typedef struct {
    glow_t g[GMAX];
    int n;
} glset_t;
static glset_t s_gls[2];
static glset_t *s_glb = &s_gls[0], *s_gld = &s_gls[0];  // (the build set, the draw set)
static void dl_glow(v3 at, float rad, int col, int a)
{
    if (s_glb->n >= GMAX) {
        return;
    }
    float sx, sy, z;
    if (!world_proj(at, &sx, &sy, &z)) {
        return;
    }
    float r = C.f * rad * frcp(z);
    if (sx + r / KX < 0 || sx - r / KX > VW || sy + r < 0 || sy - r > VH) {
        return;
    }
    glow_t *g = &s_glb->g[s_glb->n++];
    g->x = sx;
    g->y = sy;
    g->r = r;
    g->z = ZK * frcp(z);
    g->col = (uint8_t)col;
    g->a = a;
}
