// tf3d: the 3D core of the games on the CUBBY engine (CUBBY, THE MODEL, INK KARTS, AFTER HOURS 3D, NIGHT TAPES):
// the vector math, the transforms, the camera and a smooth rasterizer in tiles. One copy of the code: each game
// includes it in ONE source file (its <x>_r3d.inc) after its settings, so each game keeps its own machine code.
//
// The rasterizer: triangles with a z-buffer of 16 bits (the value is 1/z, linear on the screen) and a colour in full
// RGB for each corner (Gouraud). The colour of a pixel gets a fine ordered dither of less than one step of RGB565, so
// the gradients are smooth and the dither shows no pattern.
// THE TILES (the speed): the PSRAM of the board moves only about 43 MB/s, so a frame and a z-buffer there cost about
// 1 MB of traffic a frame. The screen is drawn in tiles of TLW x TLH px in internal RAM (the colour, the z, the edge
// marks: about 4 KB for each core); a done tile goes to the frame in one copy, so each pixel goes to PSRAM once. The
// two cores share the tiles. THE ROWS: the values of a triangle (the z and the colour) are whole numbers that step from
// pixel to pixel and from row to row: a row costs a few additions (small triangles are only 2-4 px high). The
// rasterizer marks the first and the last pixel of each row of a triangle; a pass in the tile softens those pixels
// where the colour jumps (the edges): an anti-alias for the price of the edges only.
// No sqrtf and no float division in the paths of each frame: in an app file both are calls of the library (about
// 0.3 us each on the board); frsqrt() and frcp() are a few multiplications.
//
// THE SETTINGS that the game defines before the include:
//   OW, VW, VH       the width of the frame, the width and the height of the 3D (render px); KX = OW / VW
//   KX, KY           the render px is KX x KY px of the frame (KY: default 1; a game with a KY gets the focal
//                    length of the rows C.f = fpx / KY)
//   KXF              a radius in the px of the rows, in the px of the columns (default (float)KX / KY)
//   TLW, TLH         the size of a tile (px)
//   TF3D_ROUND       1: the round frame: circle_make() and the rows of a triangle stop at the circle (default 0)
//   TF3D_CAM_BASIS   1: cam_set_basis(), a camera from a frame of vectors and a centre (the shots of a pack)
//   TF3D_FAR         the far cut of cam_sees() (default 160)
//   TF3D_ADD_MIX(p, cr, cg, cb, k)   the mix of tri_add: it declares the int r, g, b of the pixel p (RGB565) with
//                    the colour cr, cg, cb at the strength k (0..256); the default adds the light
//   TF3D_HOST_CY     1: the game gives its own cy_now() for a PC build (the board always reads the cycle counter)
// THE MEMORY: static data of about 1 KB (the profile s_cy and s_cn, the camera C, the dither table DQ_PS; the round
// frame: 4 bytes a row of the 3D for the circle). The tiles, the frame and the lists are the memory of the game. The
// app needs <math.h>, <string.h>, <stdint.h>, <stdbool.h> and tfn/gfx.h (fsin, fcos) first.
#ifdef KY
#define TF3D_KY 1  // (the game has a KY: the camera takes the focal length of the rows from it)
#else
#define TF3D_KY 0
#define KY      1
#endif
#ifndef KXF
#define KXF ((float)KX / KY)
#endif
#ifndef TF3D_ROUND
#define TF3D_ROUND 0
#endif
#ifndef TF3D_CAM_BASIS
#define TF3D_CAM_BASIS 0
#endif
#ifndef TF3D_FAR
#define TF3D_FAR 160
#endif
#ifndef TF3D_HOST_CY
#define TF3D_HOST_CY 0
#endif
#define TCOLS ((VW + TLW - 1) / TLW)
#define TROWS ((VH + TLH - 1) / TLH)
#define NTILE (TCOLS * TROWS)
#define NEARZ 0.10f
#define ZK    (65000.0f * NEARZ)  // the z-buffer value = ZK / z (bigger = nearer; 0 = nothing there)

// the profile of the work (CPU cycles, summed for each core; the log each 5 s): the cycle counter costs 1 instruction.
// TF3D_PROF 1: also the cycles of the setup and of the rows of each solid triangle (a cost of its own in each setup)
#define TF3D_PROF 0
#if defined(ESP_PLATFORM) && defined(__XTENSA__)
static inline uint32_t cy_now(void)
{
    uint32_t c;
    __asm__ volatile("rsr.ccount %0" : "=a"(c));
    return c;
}
#elif !TF3D_HOST_CY
static inline uint32_t cy_now(void)
{
    return 0;
}
#endif
enum {
    CY_CLEAR,
    CY_SOLID,
    CY_BG,
    CY_DARK,
    CY_ADD,
    CY_GLOW,
    CY_EDGE,
    CY_OUT,
    CY_MCORN,
    CY_MTRI,
    CY_CCORN,
    CY_CTRI,
    CY_SETUP,
    CY_ROWS,
    CY_N
};
enum {
    CN_PX,
    CN_SETUP,
    CN_ROWS,
    CN_MCORN,
    CN_MTRI,
    CN_CCORN,
    CN_CTRI,
    CN_WR,
    CN_N
};
static uint64_t s_cy[2][CY_N];
static uint32_t s_cn[2][CN_N];

typedef struct {
    float x, y, z;
} v3;
// 1 / sqrt(x) and 1 / x with no call of the library (the error is less than 0.0005 %)
static inline float frsqrt(float x)
{
    union {
        float f;
        uint32_t i;
    } u = {x};
    u.i = 0x5f375a86u - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return y;
}
static inline float frcp(float x)
{
    float s = 1;
    if (x < 0) {
        x = -x;
        s = -1;
    }
    union {
        float f;
        uint32_t i;
    } u = {x};
    u.i = 0x7EF311C3u - u.i;
    float y = u.f;
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    return y * s;
}
static inline float fsqrt(float x)
{
    return x > 1e-12f ? x * frsqrt(x) : 0;
}
static inline v3 V3(float x, float y, float z)
{
    v3 r = {x, y, z};
    return r;
}
static inline v3 vadd(v3 a, v3 b)
{
    return V3(a.x + b.x, a.y + b.y, a.z + b.z);
}
static inline v3 vsub(v3 a, v3 b)
{
    return V3(a.x - b.x, a.y - b.y, a.z - b.z);
}
static inline v3 vmul(v3 a, float s)
{
    return V3(a.x * s, a.y * s, a.z * s);
}
static inline float vdot(v3 a, v3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static inline v3 vcross(v3 a, v3 b)
{
    return V3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float vlen(v3 a)
{
    return fsqrt(vdot(a, a));
}
static inline v3 vnorm(v3 a)
{
    float d = vdot(a, a);
    return d > 1e-18f ? vmul(a, frsqrt(d)) : V3(0, 1, 0);
}
static inline v3 vlerp(v3 a, v3 b, float t)
{
    return V3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
}
static inline float clampf(float v, float a, float b)
{
    return v < a ? a : v > b ? b : v;
}
static inline int clampi(int v, int a, int b)
{
    return v < a ? a : v > b ? b : v;
}
// (inline: with no fast math, ceilf and floorf are calls of the library on the board)
static inline int iceil(float v)
{
    int i = (int)v;
    return v > (float)i ? i + 1 : i;
}
static inline int ifloor(float v)
{
    int i = (int)v;
    return v < (float)i ? i - 1 : i;
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
static inline float absf(float v)
{
    return v < 0 ? -v : v;
}
#define FMIN(a, b) ((a) < (b) ? (a) : (b))
#define FMAX(a, b) ((a) > (b) ? (a) : (b))
#define PI_F       3.14159265f
#define TAU_F      6.2831853f
// an angle in (-pi, pi]
static inline float wrap_pi(float a)
{
    while (a > PI_F) {
        a -= TAU_F;
    }
    while (a <= -PI_F) {
        a += TAU_F;
    }
    return a;
}

// a frame: p' = R p + t (R by rows)
typedef struct {
    float r[9];
    v3 t;
} xf_t;
static inline v3 xf_pt(const xf_t *x, v3 p)
{
    return V3(x->r[0] * p.x + x->r[1] * p.y + x->r[2] * p.z + x->t.x,
              x->r[3] * p.x + x->r[4] * p.y + x->r[5] * p.z + x->t.y,
              x->r[6] * p.x + x->r[7] * p.y + x->r[8] * p.z + x->t.z);
}
static inline v3 xf_dir(const xf_t *x, v3 p)
{
    return V3(x->r[0] * p.x + x->r[1] * p.y + x->r[2] * p.z, x->r[3] * p.x + x->r[4] * p.y + x->r[5] * p.z,
              x->r[6] * p.x + x->r[7] * p.y + x->r[8] * p.z);
}
static void m3_mul(float *o, const float *a, const float *b)
{
    float r[9];
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r[i * 3 + j] = a[i * 3] * b[j] + a[i * 3 + 1] * b[3 + j] + a[i * 3 + 2] * b[6 + j];
        }
    }
    memcpy(o, r, sizeof r);
}
// o = a after b (first b, then a)
static void xf_mul(xf_t *o, const xf_t *a, const xf_t *b)
{
    xf_t r;
    m3_mul(r.r, a->r, b->r);
    r.t = vadd(xf_dir(a, b->t), a->t);
    *o = r;
}
static void xf_ident(xf_t *x)
{
    static const float I[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    memcpy(x->r, I, sizeof I);
    x->t = V3(0, 0, 0);
}
// the turns (radians): about x (the nose goes down for a > 0 when z is the front), y (the front turns toward +x), z
static void rot_x(float *r, float a)
{
    float c = fcos(a), s = fsin(a);
    float m[9] = {1, 0, 0, 0, c, -s, 0, s, c};
    memcpy(r, m, sizeof m);
}
static void rot_y(float *r, float a)
{
    float c = fcos(a), s = fsin(a);
    float m[9] = {c, 0, s, 0, 1, 0, -s, 0, c};
    memcpy(r, m, sizeof m);
}
static void rot_z(float *r, float a)
{
    float c = fcos(a), s = fsin(a);
    float m[9] = {c, -s, 0, s, c, 0, 0, 0, 1};
    memcpy(r, m, sizeof m);
}
// a frame from a turn y (yaw), then x (pitch), then z (roll), a scale and a place
static void xf_make(xf_t *x, v3 at, float yaw, float pitch, float roll, v3 scale)
{
    float a[9], b[9], c[9], t[9];
    rot_y(a, yaw);
    rot_x(b, pitch);
    rot_z(c, roll);
    m3_mul(t, b, c);
    m3_mul(x->r, a, t);
    for (int i = 0; i < 3; i++) {
        x->r[i * 3] *= scale.x;
        x->r[i * 3 + 1] *= scale.y;
        x->r[i * 3 + 2] *= scale.z;
    }
    x->t = at;
}

// ---------------------------------------------------------------- the camera
typedef struct {
    v3 eye, R, U, F;
    float f, fx, cx, cy;
    xf_t w2c;
    float fx_edge, fy_edge, kx, ky;
} cam3_t;
static cam3_t C;
#if TF3D_CAM_BASIS
// the camera of a shot: its place, its frame (right, up, forward: unit vectors), the focal length (px) and the centre
// of the picture on the screen (a wide shot moves left and right, the tilt view up and down: cx and cy change)
static void cam_set_basis(v3 eye, v3 R, v3 U, v3 F, float fpx, float cx, float cy)
{
    C.eye = eye;
    C.R = R;
    C.U = U;
    C.F = F;
    C.f = fpx;
    C.fx = fpx * (1.0f / KX);
    C.cx = cx;
    C.cy = cy;
    float r[9] = {R.x, R.y, R.z, U.x, U.y, U.z, F.x, F.y, F.z};
    memcpy(C.w2c.r, r, sizeof r);
    C.w2c.t = V3(-vdot(R, eye), -vdot(U, eye), -vdot(F, eye));
    // (the edges of the view for the culling: the larger side of the centre)
    float hw = FMAX(cx, VW - cx) + 4;
    C.fx_edge = hw / C.fx;
    C.fy_edge = (FMAX(cy, VH - cy) + 4) / fpx;
    C.kx = fsqrt(1 + C.fx_edge * C.fx_edge);
    C.ky = fsqrt(1 + C.fy_edge * C.fy_edge);
}
#endif
static void cam_set(v3 eye, v3 at, float fpx)
{
    C.eye = eye;
    C.F = vnorm(vsub(at, eye));
    C.R = vnorm(vcross(V3(0, 1, 0), C.F));
    C.U = vcross(C.F, C.R);
#if TF3D_KY
    C.f = fpx * (1.0f / KY);
#else
    C.f = fpx;
#endif
    C.fx = fpx * (1.0f / KX);
    C.cx = VW * 0.5f;
    C.cy = VH * 0.5f;
    float r[9] = {C.R.x, C.R.y, C.R.z, C.U.x, C.U.y, C.U.z, C.F.x, C.F.y, C.F.z};
    memcpy(C.w2c.r, r, sizeof r);
    C.w2c.t = V3(-vdot(C.R, eye), -vdot(C.U, eye), -vdot(C.F, eye));
    C.fx_edge = (VW * 0.5f + 4) / C.fx;  // the half width of the view at z = 1 (with a small margin)
#if TF3D_KY
    C.fy_edge = (VH * 0.5f + 4) / C.f;
#else
    C.fy_edge = (VH * 0.5f + 4) / fpx;
#endif
    C.kx = fsqrt(1 + C.fx_edge * C.fx_edge);
    C.ky = fsqrt(1 + C.fy_edge * C.fy_edge);
}
// a sphere (camera space centre c, radius r) is in the view
static inline bool cam_sees(v3 c, float r)
{
    if (c.z < NEARZ - r) {
        return false;
    }
    if (c.z > TF3D_FAR + r) {
        return false;  // (the far cut)
    }
    if (c.x - C.fx_edge * c.z > r * C.kx) {
        return false;
    }
    if (-c.x - C.fx_edge * c.z > r * C.kx) {
        return false;
    }
    if (c.y - C.fy_edge * c.z > r * C.ky) {
        return false;
    }
    if (-c.y - C.fy_edge * c.z > r * C.ky) {
        return false;
    }
    return true;
}
static inline bool world_proj(v3 p, float *sx, float *sy, float *z)
{
    v3 c = xf_pt(&C.w2c, p);
    *z = c.z;
    if (c.z < NEARZ) {
        return false;
    }
    float iz = frcp(c.z);
    *sx = C.cx + C.fx * c.x * iz;
    *sy = C.cy - C.f * c.y * iz;
    return true;
}

// ---------------------------------------------------------------- the tiles
// a tile: TLW x TLH px in one buffer of 32-bit words (row stride TLW): the colour (RGB565 in the order of the CPU) in
// the low half of a word and the z in the high half, so a row of a triangle walks one pointer. Then the edge marks (a
// bit for each px of a row) and the place on the screen [x0, x1) x [y0, y1). The buffer starts on 32 bytes: the column
// of the dither of a px comes from its address.
typedef struct {
    uint32_t *p;
    uint32_t e[TLH][2];
    int x0, y0, x1, y1, core;
} tile_t;
#if TF3D_ROUND
// THE CIRCLE (the round): the px of each row of the 3D that show, [s_rcl[y], s_rcr[y]) (render px, 1 px of margin):
// the solid rows, the sky and the sea and the OUT copy stop there (circle_make() at enter)
static int16_t s_rcl[VH], s_rcr[VH];
static void circle_make(void)
{
    const float c = OW * 0.5f;
    for (int y = 0; y < VH; y++) {
        // (the wider of the two output rows of the render row)
        float dy = (y + 0.5f) * KY - c, a = fabsf(dy) - KY * 0.5f;
        if (a < 0) {
            a = 0;
        }
        float h2 = (c + 1.5f) * (c + 1.5f) - a * a;
        if (h2 <= 0) {
            s_rcl[y] = (int16_t)VW;
            s_rcr[y] = 0;
            continue;
        }
        float hw = sqrtf(h2);
        int l = (int)((c - hw) / KX), r = (int)((c + hw) / KX + 0.999f);
        s_rcl[y] = (int16_t)(l < 0 ? 0 : l);
        s_rcr[y] = (int16_t)(r > VW ? VW : r);
    }
}
#endif
static int s_ntri_drawn;
// an 8 x 8 ordered dither, 0..255 (less than one step of RGB565 when it is scaled: << 3 for red and blue, << 2 green)
static const uint8_t DITH[8][8] = {{2, 130, 34, 162, 10, 138, 42, 170},  {194, 66, 226, 98, 202, 74, 234, 106},
                                   {50, 178, 18, 146, 58, 186, 26, 154}, {242, 114, 210, 82, 250, 122, 218, 90},
                                   {14, 142, 46, 174, 6, 134, 38, 166},  {206, 78, 238, 110, 198, 70, 230, 102},
                                   {62, 190, 30, 158, 54, 182, 22, 150}, {254, 126, 222, 94, 246, 118, 214, 86}};
// THE COLOUR LANES: red and blue step together in one 32-bit value (two lanes of 16 bits), green alone. A lane holds
// LANE0 + the value x 256 (red and blue 0..247, green 0..251): a small step out of the range keeps the lanes apart.
// The dither of a px goes into the lanes with one add each: DQ[y & 7].rb[x & 7] and .g[x & 7] (the dither less LANE0)
#define LANE0 1024
typedef struct {
    uint32_t rb[8], g[8];
} dq_t;
static dq_t DQ_PS[8];  // (the table when there is no internal RAM for it)
static dq_t *DQ = DQ_PS;
static void *s_dq_mem;
static void dq_init(void)
{
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int q = DITH[y][x];
            uint32_t lr = (uint32_t)((q << 3) - LANE0), lg = (uint32_t)((q << 2) - LANE0);
            DQ[y].rb[x] = (lr << 16) + lr;
            DQ[y].g[x] = lg;
        }
    }
}
// the RGB565 colour of the lanes after the dither
static inline uint16_t lanes_565(uint32_t rb, uint32_t g)
{
    return (uint16_t)((rb >> 27) << 11 | ((g >> 10) & 63) << 5 | ((rb >> 11) & 31));
}

// ---------------------------------------------------------------- the rasterizer
// a corner of the frame (16 bytes): in front of the near plane x, y = the screen point (px), z = the z-buffer value;
// behind it (f = 1) x, y, z = the point in camera space (only the cut at the near plane uses it). r, g, b = the
// colour (1..247 red and blue, 1..251 green), or r = the strength of a light (0..255)
typedef struct {
    float x, y, z;
    uint8_t r, g, b, f;
} pv_t;

// the setup of a triangle in a tile: the rows [y0, y1), the edges in 16.16 fixed point at row y0 (the long edge a-c,
// the short edge a-b, then b-c from row ym), the values at the corner (xo + 0.5, y0 + 0.5) of the rows (x 256: z, r,
// g, b) and their steps in x and y. THE EDGES: each edge starts at the first row of its top corner and steps in whole
// numbers, so two triangles on one edge get the same px of each row (no hole and no double px). A triangle with a corner
// far off the screen (flt) takes the edges in floats.
typedef struct {
    int y0, y1, ym, xo;
    bool long_left, flt;
    int32_t xl, dxl, xs, dxs, xs2, dxs2;
    float xa, ya, xb, yb, sac, sab, sbc;
    int32_t A[4], AX[4], AY[4];
} rs_t;
#define EDGE_MAX 8000.0f
// an edge from the top corner (tx, ty) with the slope s (px a row): its x (16.16) at the centre of row y, by whole steps
// from the first row of the edge (the same number for each triangle on the edge)
static inline int32_t edge_at(float tx, float ty, float s, int y, int32_t *dx)
{
    int yt = iceil(ty - 0.5f);
    int32_t x0 = (int32_t)((tx + (yt + 0.5f - ty) * s) * 65536.0f);
    int32_t d = (int32_t)(s * 65536.0f);
    *dx = d;
    return x0 + (y - yt) * d;
}
// 1 / x with two Newton steps (relative error about 2e-4) and no branch: the sign goes into the first guess
static inline float frcp2(float x)
{
    union {
        float f;
        uint32_t i;
    } u = {x};
    uint32_t sg = u.i & 0x80000000u;
    u.i = (0x7EF311C3u - (u.i & 0x7FFFFFFFu)) | sg;
    float y = u.f;
    y = y * (2.0f - x * y);
    y = y * (2.0f - x * y);
    return y;
}
// the steps (x 256) of one value of the corners across the triangle, and its value at the corner of the first row;
// false for a sliver: its steps do not fit the whole numbers of a tile (it shows no px)
static inline bool rs_attr(rs_t *S, int k, float va, float db, float dc, float x1, float y1_, float x2, float y2,
                           float id, float fx, float fy)
{
    float gx = (db * y2 - dc * y1_) * id, gy = (dc * x1 - db * x2) * id;
    if (absf(gx) + absf(gy) > 2.3e7f) {
        return false;
    }
    float v = va * 256.0f + fx * gx + fy * gy;
    if (absf(v) > 1.6e9f) {
        return false;
    }
    S->A[k] = (int32_t)v;
    S->AX[k] = (int32_t)gx;
    S->AY[k] = (int32_t)gy;
    return true;
}
static bool rs_setup(rs_t *S, const pv_t *v0, const pv_t *v1, const pv_t *v2, const tile_t *t, int nattr)
{
    const pv_t *a = v0, *b = v1, *c = v2, *q;
    if (b->y < a->y) {
        q = a;
        a = b;
        b = q;
    }
    if (c->y < a->y) {
        q = a;
        a = c;
        c = q;
    }
    if (c->y < b->y) {
        q = b;
        b = c;
        c = q;
    }
    int ya = iceil(a->y - 0.5f), yc = iceil(c->y - 0.5f);
    if (ya < t->y0) {
        ya = t->y0;
    }
    if (yc > t->y1) {
        yc = t->y1;
    }
    if (ya >= yc) {
        return false;
    }
    float x1 = b->x - a->x, y1_ = b->y - a->y, x2 = c->x - a->x, y2 = c->y - a->y;
    float det = x1 * y2 - x2 * y1_;
    if (absf(det) < 1e-3f) {
        return false;
    }
    float id = frcp2(det) * 256.0f;  // (x 256: the values step in whole numbers x 256)
    float fx = t->x0 + 0.5f - a->x, fy = ya + 0.5f - a->y;
    if (!rs_attr(S, 0, a->z, b->z - a->z, c->z - a->z, x1, y1_, x2, y2, id, fx, fy)) {
        return false;
    }
    // (the colour, or the strength of a light: the differences of the bytes)
    if (!rs_attr(S, 1, a->r, (float)((int)b->r - a->r), (float)((int)c->r - a->r), x1, y1_, x2, y2, id, fx, fy)) {
        return false;
    }
    if (nattr > 2) {
        if (!rs_attr(S, 2, a->g, (float)((int)b->g - a->g), (float)((int)c->g - a->g), x1, y1_, x2, y2, id, fx, fy)) {
            return false;
        }
        if (!rs_attr(S, 3, a->b, (float)((int)b->b - a->b), (float)((int)c->b - a->b), x1, y1_, x2, y2, id, fx, fy)) {
            return false;
        }
    }
    // the edges: the slope of each edge = (bottom x - top x) / (bottom y - top y), the same numbers for each triangle on
    // it; a short edge only when it has rows in this tile
    S->ym = iceil(b->y - 0.5f);
    bool ab = ya<S->ym, bc = yc> S->ym;
    S->sac = x2 * frcp2(y2);
    S->sab = ab && y1_ > 0 ? x1 * frcp2(y1_) : 0;
    S->sbc = bc && c->y > b->y ? (c->x - b->x) * frcp2(c->y - b->y) : 0;
    S->long_left = det > 0;
    S->y0 = ya;
    S->y1 = yc;
    S->xo = t->x0;
    float big = FMAX(FMAX(absf(a->x), absf(b->x)), absf(c->x));
    big = FMAX(big, FMAX(absf(S->sac), FMAX(absf(S->sab), absf(S->sbc))));
    S->flt = big > EDGE_MAX;
    if (S->flt) {
        S->xa = a->x;
        S->ya = a->y;
        S->xb = b->x;
        S->yb = b->y;
        return true;
    }
    // (a short edge only when it has rows in this tile: a value far past its end does not fit the whole numbers)
    S->xl = edge_at(a->x, a->y, S->sac, ya, &S->dxl);
    S->xs = S->dxs = S->xs2 = S->dxs2 = 0;
    if (ab) {
        S->xs = edge_at(a->x, a->y, S->sab, ya, &S->dxs);
    }
    if (bc) {
        S->xs2 = edge_at(b->x, b->y, S->sbc, ya > S->ym ? ya : S->ym, &S->dxs2);
    }
    return true;
}
// the px of row y of a triangle in [lo, hi): [*ix0, *ix1); then the edges step to the next row
static inline bool rs_row(rs_t *S, int y, int lo, int hi, int *ix0, int *ix1)
{
    int i0, i1;
    if (S->flt) {
        float yy = y + 0.5f;
        float xl = S->xa + (yy - S->ya) * S->sac;
        float xs = yy < S->yb ? S->xa + (yy - S->ya) * S->sab : S->xb + (yy - S->yb) * S->sbc;
        if (xl > xs) {
            float q = xl;
            xl = xs;
            xs = q;
        }
        i0 = iceil(xl - 0.5f);
        i1 = iceil(xs - 0.5f);
    } else {
        int32_t xs;
        if (y < S->ym) {
            xs = S->xs;
            S->xs += S->dxs;
        } else {
            xs = S->xs2;
            S->xs2 += S->dxs2;
        }
        int32_t xl = S->xl;
        S->xl += S->dxl;
        if (S->long_left) {
            i0 = (xl + 0x7FFF) >> 16;
            i1 = (xs + 0x7FFF) >> 16;
        } else {
            i0 = (xs + 0x7FFF) >> 16;
            i1 = (xl + 0x7FFF) >> 16;
        }
    }
    if (i0 < lo) {
        i0 = lo;
    }
    if (i1 > hi) {
        i1 = hi;
    }
    *ix0 = i0;
    *ix1 = i1;
    return i0 < i1;
}
// the first px of row y of a tile (a pointer to its colour: [0] the colour, [1] the z, 2 for each px)
static inline uint16_t *tile_px(const tile_t *t, int x, int y)
{
    return (uint16_t *)(t->p + (y - t->y0) * TLW + (x - t->x0));
}
// the steps of a solid triangle along a row (the z, the red-blue lanes, the green lane) and the dither of the row
typedef struct {
    int32_t zx;
    uint32_t drb, dg;
    const dq_t *dq;
} spank_t;
// the px of one row of a solid triangle: n px from h (the px words), the z and the colour lanes at the first px.
// (Not inline: a loop with a call keeps the compiler from a hardware loop round the loop here)
__attribute__((noinline)) static void span_rgb(uint16_t *h, int n, int32_t z, uint32_t RB, uint32_t G, const spank_t *K)
{
    if (n <= 0) {
        return;
    }
    const int32_t zx = K->zx;
    const uint32_t drb = K->drb, dg = K->dg;
    const dq_t *dq = K->dq;
#if defined(ESP_PLATFORM) && defined(__XTENSA__)
    // (the loop by hand: 22 instructions for a px in front, 7 for a px behind; the compiler made 27 and 11)
    uint32_t t0, t1, t2, t3;
    __asm__ volatile("loop %[n], 2f\n"
                     "l16ui %[t0], %[h], 2\n"
                     "srli %[t1], %[z], 8\n"
                     "bgeu %[t0], %[t1], 1f\n"
                     "s16i %[t1], %[h], 2\n"
                     "extui %[t0], %[h], 2, 3\n"
                     "addx4 %[t0], %[t0], %[dq]\n"
                     "l32i %[t1], %[t0], 0\n"
                     "l32i %[t2], %[t0], 32\n"
                     "add %[t1], %[t1], %[rb]\n"
                     "add %[t2], %[t2], %[g]\n"
                     "extui %[t3], %[t1], 27, 5\n"
                     "slli %[t3], %[t3], 11\n"
                     "extui %[t0], %[t2], 10, 6\n"
                     "slli %[t0], %[t0], 5\n"
                     "or %[t3], %[t3], %[t0]\n"
                     "extui %[t0], %[t1], 11, 5\n"
                     "or %[t3], %[t3], %[t0]\n"
                     "s16i %[t3], %[h], 0\n"
                     "1:\n"
                     "add %[z], %[z], %[zx]\n"
                     "add %[rb], %[rb], %[drb]\n"
                     "add %[g], %[g], %[dg]\n"
                     "addi %[h], %[h], 4\n"
                     "2:\n"
                     : [h] "+a"(h), [z] "+a"(z), [rb] "+a"(RB), [g] "+a"(G), [t0] "=&a"(t0), [t1] "=&a"(t1),
                       [t2] "=&a"(t2), [t3] "=&a"(t3)
                     : [n] "a"(n), [zx] "a"(zx), [drb] "a"(drb), [dg] "a"(dg), [dq] "a"(dq)
                     : "memory");
#else
    for (; n > 0; n--, h += 2, z += zx, RB += drb, G += dg) {
        uint32_t zz = (uint32_t)z >> 8;
        if (zz > h[1]) {
            h[1] = (uint16_t)zz;
            uint32_t k = ((uintptr_t)h >> 2) & 7;
            h[0] = lanes_565(RB + dq->rb[k], G + dq->g[k]);
            s_cn[0][CN_WR]++;
        }
    }
#endif
}
#define CR_TOP (247 * 256)
#define CG_TOP (251 * 256)
// a solid triangle in a tile: the z-buffer, the colour of the corners, the edge marks
static void tri_rgb(tile_t *t, const pv_t *v0, const pv_t *v1, const pv_t *v2, bool marks)
{
    rs_t S;
#if TF3D_PROF
    uint32_t q0 = cy_now();
#endif
    bool ok = rs_setup(&S, v0, v1, v2, t, 4);
#if TF3D_PROF
    uint32_t q1 = cy_now();
    s_cy[t->core][CY_SETUP] += q1 - q0;
#endif
    if (!ok) {
        return;
    }
    s_ntri_drawn++;
    // the values at the corner (xo + 0.5, y + 0.5) of each row: the z, and the colour lanes packed (a value out of the
    // triangle there can be out of its lane: the sums are exact modulo 2^32, and each px of a row is in the triangle)
    int32_t zr = S.A[0];
    uint32_t RBr = ((uint32_t)(S.A[1] + LANE0) << 16) + (uint32_t)(S.A[3] + LANE0), Gr = (uint32_t)(S.A[2] + LANE0);
    const uint32_t rby = ((uint32_t)S.AY[1] << 16) + (uint32_t)S.AY[3], gy = (uint32_t)S.AY[2];
    const int32_t zy = S.AY[0];
    spank_t K = {S.AX[0], ((uint32_t)S.AX[1] << 16) + (uint32_t)S.AX[3], (uint32_t)S.AX[2], NULL};
    const int lo = t->x0, hi = t->x1, xo = S.xo;
    // the edges in whole numbers (S.flt: the float path, for a triangle far off the screen)
    int32_t xl = S.xl, dxl = S.dxl;
    bool lower = S.y0 >= S.ym;
    int32_t xs = lower ? S.xs2 : S.xs, dxs = lower ? S.dxs2 : S.dxs;
    uint32_t nrows = 0, npx = 0;
    for (int y = S.y0; y < S.y1; y++, zr += zy, RBr += rby, Gr += gy) {
        int x0, x1;
        if (S.flt) {
            if (!rs_row(&S, y, lo, hi, &x0, &x1)) {
                continue;
            }
        } else {
            if (y == S.ym) {
                xs = S.xs2;
                dxs = S.dxs2;
            }
            int32_t ea = xl, eb = xs;
            xl += dxl;
            xs += dxs;
            if (!S.long_left) {
                int32_t q = ea;
                ea = eb;
                eb = q;
            }
            x0 = (ea + 0x7FFF) >> 16;
            x1 = (eb + 0x7FFF) >> 16;
            if (x0 < lo) {
                x0 = lo;
            }
            if (x1 > hi) {
                x1 = hi;
            }
            if (x0 >= x1) {
                continue;
            }
        }
#if TF3D_ROUND
        // (the round: the px out of the circle do not show)
        if (x0 < s_rcl[y]) {
            x0 = s_rcl[y];
        }
        if (x1 > s_rcr[y]) {
            x1 = s_rcr[y];
        }
        if (x0 >= x1) {
            continue;
        }
#endif
        int off = x0 - xo, n = x1 - x0;
        nrows++;
        npx += (uint32_t)n;
        int32_t z = zr + off * K.zx;
        K.dq = &DQ[y & 7];
        uint16_t *h = tile_px(t, x0, y);
        span_rgb(h, n, z, RBr + (uint32_t)off * K.drb, Gr + (uint32_t)off * K.dg, &K);
        if (marks) {
            // (the first and the last pixel are this triangle when the z there is its own)
            uint32_t *e = t->e[y - t->y0];
            int b0 = x0 - lo, b1 = x1 - 1 - lo;
            if (h[1] == (uint16_t)((uint32_t)z >> 8)) {
                e[b0 >> 5] |= 1u << (b0 & 31);
            }
            if (h[2 * (n - 1) + 1] == (uint16_t)((uint32_t)(z + (n - 1) * K.zx) >> 8)) {
                e[b1 >> 5] |= 1u << (b1 & 31);
            }
        }
    }
    uint32_t *cn = s_cn[t->core];
    cn[CN_SETUP]++;
    cn[CN_ROWS] += nrows;
    cn[CN_PX] += npx;
#if TF3D_PROF
    s_cy[t->core][CY_ROWS] += cy_now() - q1;
#endif
}
// a triangle that adds light, with the z test and no z write: the colour (0..255 a channel) x the strength of the
// corners (.r, 0..255)
static void tri_add(tile_t *t, const pv_t *v0, const pv_t *v1, const pv_t *v2, int cr, int cg, int cb)
{
    rs_t S;
    if (!rs_setup(&S, v0, v1, v2, t, 2)) {
        return;
    }
    int32_t rz = S.A[0], rl = S.A[1];
    const int32_t zx = S.AX[0], lx = S.AX[1];
    for (int y = S.y0; y < S.y1; y++, rz += S.AY[0], rl += S.AY[1]) {
        int x0, x1;
        if (!rs_row(&S, y, t->x0, t->x1, &x0, &x1)) {
            continue;
        }
        int off = x0 - S.xo;
        int32_t z = rz + off * zx, l = rl + off * lx;
        uint16_t *h = tile_px(t, x0, y);
        for (int n = x1 - x0; n > 0; n--, h += 2, z += zx, l += lx) {
            if (((uint32_t)z >> 8) < h[1]) {
                continue;
            }
            int k = l >> 8;
            if (k <= 0) {
                continue;
            }
            if (k > 256) {
                k = 256;
            }
            uint32_t p = h[0];
#ifdef TF3D_ADD_MIX
            TF3D_ADD_MIX(p, cr, cg, cb, k);
#else
            int r = (int)(p >> 11) + ((cr * k) >> 11), g = (int)((p >> 5) & 63) + ((cg * k) >> 10),
                b = (int)(p & 31) + ((cb * k) >> 11);
#endif
            if (r > 31) {
                r = 31;
            }
            if (g > 63) {
                g = 63;
            }
            if (b > 31) {
                b = 31;
            }
            h[0] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}
// a triangle that darkens (a soft shadow), with the z test: the pixel x (256 - k) / 256, k = .r of the corners
// (0..255); the shadow takes a cool tint (the sky light stays in a shadow)
static void tri_dark(tile_t *t, const pv_t *v0, const pv_t *v1, const pv_t *v2)
{
    rs_t S;
    if (!rs_setup(&S, v0, v1, v2, t, 2)) {
        return;
    }
    int32_t rz = S.A[0], rl = S.A[1];
    const int32_t zx = S.AX[0], lx = S.AX[1];
    for (int y = S.y0; y < S.y1; y++, rz += S.AY[0], rl += S.AY[1]) {
        int x0, x1;
        if (!rs_row(&S, y, t->x0, t->x1, &x0, &x1)) {
            continue;
        }
        int off = x0 - S.xo;
        int32_t z = rz + off * zx, l = rl + off * lx;
        uint16_t *h = tile_px(t, x0, y);
        for (int n = x1 - x0; n > 0; n--, h += 2, z += zx, l += lx) {
            if (((uint32_t)z >> 8) + 24 < h[1]) {
                continue;
            }
            int k = l >> 8;
            if (k <= 0) {
                continue;
            }
            if (k > 256) {
                k = 256;
            }
            int kr = 256 - k, kb = 256 - (k >> 1);
            uint32_t p = h[0];
            int r = (int)((p >> 11) * kr) >> 8, g = (int)(((p >> 5) & 63) * kr) >> 8, b = (int)((p & 31) * kb) >> 8;
            h[0] = (uint16_t)((r << 11) | (g << 5) | b);
        }
    }
}

// a soft glow that adds light (a disc of radius r px, the strongest in the middle), with one depth for the z test
// (zv 0: no test; zv < 0: only the pixels that nothing covers: the sun and the clouds behind all)
static void glow_add(tile_t *t, float cx, float cy, float r, float zv, int cr, int cg, int cb, int a)
{
    if (r < 0.8f || a <= 0) {
        return;
    }
    float rx = r * (1.0f / KXF);
    if (cx + rx < t->x0 || cx - rx >= t->x1 || cy + r < t->y0 || cy - r >= t->y1) {
        return;
    }
    int ya = (int)(cy - r), yb = (int)(cy + r) + 1;
    if (ya < t->y0) {
        ya = t->y0;
    }
    if (yb > t->y1) {
        yb = t->y1;
    }
    float ir2 = frcp(r * r);
    bool sky = zv < 0;
    uint16_t zt = sky ? 0 : (uint16_t)clampf(zv, 0, 65535);
    if (a > 256) {
        a = 256;
    }
    // (q = 1 - d^2 / r^2 of a px in 16.16, by its first and second steps along the row; the light = a q^2)
    int32_t ddq = (int32_t)(-2.0f * KXF * KXF * ir2 * 65536.0f);
    for (int y = ya; y < yb; y++) {
        float dy = y + 0.5f - cy, q0 = 1 - dy * dy * ir2;
        if (q0 <= 0) {
            continue;
        }
        float hw = fsqrt(q0) * rx;
        int xa = (int)(cx - hw), xb = (int)(cx + hw) + 1;
        if (xa < t->x0) {
            xa = t->x0;
        }
        if (xb > t->x1) {
            xb = t->x1;
        }
        if (xa >= xb) {
            continue;
        }
        float dx0 = (xa + 0.5f - cx) * KXF;
        int32_t q = (int32_t)((q0 - dx0 * dx0 * ir2) * 65536.0f),
                dq = (int32_t)(-(2 * dx0 * KXF + KXF * KXF) * ir2 * 65536.0f);
        uint16_t *h = tile_px(t, xa, y);
        for (int x = xa; x < xb; x++, h += 2, q += dq, dq += ddq) {
            if (q <= 0) {
                continue;
            }
            if (sky ? h[1] != 0 : (zt && h[1] > zt)) {
                continue;
            }
            int qq = q >> 8;
            if (qq > 256) {
                qq = 256;
            }
            int k = (a * qq * qq) >> 16;
            if (k <= 0) {
                continue;
            }
            uint32_t p = h[0];
            int rr = (int)(p >> 11) + ((cr * k) >> 11), gg = (int)((p >> 5) & 63) + ((cg * k) >> 10),
                bb = (int)(p & 31) + ((cb * k) >> 11);
            if (rr > 31) {
                rr = 31;
            }
            if (gg > 63) {
                gg = 63;
            }
            if (bb > 31) {
                bb = 31;
            }
            h[0] = (uint16_t)((rr << 11) | (gg << 5) | bb);
        }
    }
}

// ---------------------------------------------------------------- the soft edges (in the tile)
// the colour step between two px (red and blue count twice)
static inline int jump565(uint32_t p, uint32_t q)
{
    int dr = (int)(p >> 11) - (int)(q >> 11), dg = (int)((p >> 5) & 63) - (int)((q >> 5) & 63),
        db = (int)(p & 31) - (int)(q & 31);
    return (dr < 0 ? -dr : dr) * 2 + (dg < 0 ? -dg : dg) + (db < 0 ? -db : db) * 2;
}
// Each marked pixel with a colour jump to its left or right neighbour (a mark is the first or the last px of a row of a
// triangle: the step across the edge is to the side) becomes (4 c + left + right + up + down) / 8. (The pixels on the
// border of the tile stay: their neighbours are in the next tile.)
static void edges_soften(tile_t *t)
{
    int w = t->x1 - t->x0, h = t->y1 - t->y0;
    uint64_t inner = (w >= 64 ? ~(uint64_t)0 : (((uint64_t)1 << w) - 1)) & ~(uint64_t)1 & ~((uint64_t)1 << (w - 1));
    for (int r = 1; r < h - 1; r++) {
        uint64_t bits = (((uint64_t)t->e[r][1] << 32) | t->e[r][0]) & inner;
        uint32_t *row = t->p + r * TLW;
        while (bits) {
            int x = (uint32_t)bits ? __builtin_ctz((uint32_t)bits)
                                   : 32 + __builtin_ctz((uint32_t)(bits >> 32));  // (no __ctzdi2 on RISC-V)
            bits &= bits - 1;
            uint32_t p = row[x] & 0xFFFF, l = row[x - 1] & 0xFFFF, rr = row[x + 1] & 0xFFFF;
            if (jump565(p, l) < 10 && jump565(p, rr) < 10) {
                continue;
            }
            uint32_t u = row[x - TLW] & 0xFFFF, d = row[x + TLW] & 0xFFFF;
            int sr = (int)(p >> 11) * 4 + (int)(l >> 11) + (int)(rr >> 11) + (int)(u >> 11) + (int)(d >> 11);
            int sg = (int)((p >> 5) & 63) * 4 + (int)((l >> 5) & 63) + (int)((rr >> 5) & 63) + (int)((u >> 5) & 63) +
                     (int)((d >> 5) & 63);
            int sb = (int)(p & 31) * 4 + (int)(l & 31) + (int)(rr & 31) + (int)(u & 31) + (int)(d & 31);
            ((uint16_t *)&row[x])[0] = (uint16_t)(((sr + 4) >> 3) << 11 | ((sg + 4) >> 3) << 5 | ((sb + 4) >> 3));
        }
    }
}
