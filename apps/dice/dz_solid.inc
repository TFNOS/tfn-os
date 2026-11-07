// DICE: the solids (the true shape of each die kind), the quaternions, the label masks (the numbers and the pips) and
// the renderer of one die.
// The task: included by app_dice.c; the render hook of the app task draws the dice.
// The memory: the solids s_sol and the masks are file statics, made at init.
//
// A die is a convex mesh (the circumradius is 1). The renderer turns it (a quaternion), adds the tilt of the camera,
// and draws each face that looks at the camera: first the dark outline of the whole die, then each face as a full
// polygon in the bevel colour, then the face again a little smaller with cut corners (a soft edge). The light comes
// from the top left. The number of a face is an alpha mask (made from the Bungee face MF_N34) that the renderer maps
// onto the face with an affine map, so the number turns and shrinks with its face.

typedef struct {
    float x, y, z;
} v3;
typedef struct {
    float w, x, y, z;
} quat;

static inline v3 v3m(float x, float y, float z)
{
    v3 r = {x, y, z};
    return r;
}
static inline v3 v3add(v3 a, v3 b)
{
    return v3m(a.x + b.x, a.y + b.y, a.z + b.z);
}
static inline v3 v3sub(v3 a, v3 b)
{
    return v3m(a.x - b.x, a.y - b.y, a.z - b.z);
}
static inline v3 v3mul(v3 a, float k)
{
    return v3m(a.x * k, a.y * k, a.z * k);
}
static inline float v3dot(v3 a, v3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static inline v3 v3cross(v3 a, v3 b)
{
    return v3m(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline float v3len(v3 a)
{
    return sqrtf(v3dot(a, a));
}
static inline v3 v3unit(v3 a)
{
    float l = v3len(a);
    return l > 1e-9f ? v3mul(a, 1.0f / l) : v3m(0, 0, 1);
}

static const quat Q_ONE = {1, 0, 0, 0};
static inline quat qmul(quat a, quat b)
{
    quat r = {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z, a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
              a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x, a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
    return r;
}
static inline quat qconj(quat a)
{
    quat r = {a.w, -a.x, -a.y, -a.z};
    return r;
}
static inline quat qunit(quat a)
{
    float l = sqrtf(a.w * a.w + a.x * a.x + a.y * a.y + a.z * a.z);
    if (l < 1e-9f) {
        return Q_ONE;
    }
    quat r = {a.w / l, a.x / l, a.y / l, a.z / l};
    return r;
}
// a turn of ang radians about the unit axis
static inline quat qaxis(v3 axis, float ang)
{
    float s = fsin(ang * 0.5f), c = fcos(ang * 0.5f);
    quat r = {c, axis.x * s, axis.y * s, axis.z * s};
    return qunit(r);  // (the table of fsin is not exact: keep the length 1)
}
// the normalized mix of two turns (t 0..1), on the short path
static inline quat qnlerp(quat a, quat b, float t)
{
    if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0) {
        b.w = -b.w;
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
    }
    quat r = {a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    return qunit(r);
}
// the 3 x 3 matrix of a unit quaternion (rows)
static inline void qmat(quat q, float m[9])
{
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z, xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    m[0] = 1 - 2 * (yy + zz);
    m[1] = 2 * (xy - wz);
    m[2] = 2 * (xz + wy);
    m[3] = 2 * (xy + wz);
    m[4] = 1 - 2 * (xx + zz);
    m[5] = 2 * (yz - wx);
    m[6] = 2 * (xz - wy);
    m[7] = 2 * (yz + wx);
    m[8] = 1 - 2 * (xx + yy);
}
static inline v3 mrot(const float m[9], v3 v)
{
    return v3m(m[0] * v.x + m[1] * v.y + m[2] * v.z, m[3] * v.x + m[4] * v.y + m[5] * v.z,
               m[6] * v.x + m[7] * v.y + m[8] * v.z);
}
// the quaternion of a matrix (rows)
static quat qfrom(const float m[9])
{
    float tr = m[0] + m[4] + m[8];
    quat q;
    if (tr > 0) {
        float s = sqrtf(tr + 1.0f) * 2;
        q.w = 0.25f * s;
        q.x = (m[7] - m[5]) / s;
        q.y = (m[2] - m[6]) / s;
        q.z = (m[3] - m[1]) / s;
    } else if (m[0] > m[4] && m[0] > m[8]) {
        float s = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2;
        q.w = (m[7] - m[5]) / s;
        q.x = 0.25f * s;
        q.y = (m[1] + m[3]) / s;
        q.z = (m[2] + m[6]) / s;
    } else if (m[4] > m[8]) {
        float s = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2;
        q.w = (m[2] - m[6]) / s;
        q.x = (m[1] + m[3]) / s;
        q.y = 0.25f * s;
        q.z = (m[5] + m[7]) / s;
    } else {
        float s = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2;
        q.w = (m[3] - m[1]) / s;
        q.x = (m[2] + m[6]) / s;
        q.y = (m[5] + m[7]) / s;
        q.z = 0.25f * s;
    }
    return qunit(q);
}

// ---------------------------------------------------------------- the meshes
enum {
    SO_D4,
    SO_D6,
    SO_D8,
    SO_D10,
    SO_D12,
    SO_D20,
    SO_N
};
#define MAXV  20
#define MAXF  20
#define MAXFV 5
typedef struct {
    int nv, nf;
    v3 v[MAXV];
    uint8_t fn[MAXF];               // the corner count of each face
    uint8_t fv[MAXF][MAXFV];        // the corners (counter-clockwise when you look at the face from outside)
    v3 n[MAXF], c[MAXF], up[MAXF];  // the outward normal, the middle, the up of the label (in the face plane)
    uint8_t val[MAXF];              // the number of each face (1..nf); opposite faces give nf + 1
    float lab;                      // the label height in model units for the cap height of MF_N34 (35 px)
} solid_t;
static solid_t s_sol[SO_N];
static bool s_sol_ok;

// the faces of a convex solid from its corners (a small hull: each plane through 3 corners with all corners on one
// side is a face; the corners of a face are all corners in its plane)
static void sol_hull(solid_t *s)
{
    int nf = 0;
    for (int i = 0; i < s->nv; i++) {
        for (int j = i + 1; j < s->nv; j++) {
            for (int l = j + 1; l < s->nv && nf < MAXF; l++) {
                v3 n = v3cross(v3sub(s->v[j], s->v[i]), v3sub(s->v[l], s->v[i]));
                if (v3len(n) < 1e-5f) {
                    continue;
                }
                n = v3unit(n);
                float d = v3dot(n, s->v[i]);
                if (d < 0) {
                    n = v3mul(n, -1);
                    d = -d;
                }
                bool out = false;
                for (int m = 0; m < s->nv && !out; m++) {
                    out = v3dot(n, s->v[m]) > d + 1e-4f;
                }
                if (out) {
                    continue;
                }
                bool seen = false;
                for (int f = 0; f < nf && !seen; f++) {
                    seen = v3dot(n, s->n[f]) > 0.9999f;
                }
                if (seen) {
                    continue;
                }
                s->n[nf++] = n;
            }
        }
    }
    s->nf = nf;
    for (int f = 0; f < nf; f++) {
        v3 n = s->n[f];
        float best = -9;
        for (int i = 0; i < s->nv; i++) {
            if (v3dot(s->v[i], n) > best) {
                best = v3dot(s->v[i], n);
            }
        }
        int k = 0;
        for (int i = 0; i < s->nv && k < MAXFV; i++) {
            if (v3dot(s->v[i], n) > best - 1e-3f) {
                s->fv[f][k++] = (uint8_t)i;
            }
        }
        s->fn[f] = (uint8_t)k;
        // the corners in order round the normal
        v3 c = v3m(0, 0, 0);
        for (int i = 0; i < k; i++) {
            c = v3add(c, s->v[s->fv[f][i]]);
        }
        c = v3mul(c, 1.0f / (float)k);
        v3 e1 = v3unit(v3sub(s->v[s->fv[f][0]], c)), e2 = v3cross(n, e1);
        float ang[MAXFV];
        for (int i = 0; i < k; i++) {
            v3 d = v3sub(s->v[s->fv[f][i]], c);
            ang[i] = atan2f(v3dot(d, e2), v3dot(d, e1));
        }
        for (int i = 1; i < k; i++) {
            for (int j = i; j > 0 && ang[j - 1] > ang[j]; j--) {
                float t = ang[j];
                ang[j] = ang[j - 1];
                ang[j - 1] = t;
                uint8_t u = s->fv[f][j];
                s->fv[f][j] = s->fv[f][j - 1];
                s->fv[f][j - 1] = u;
            }
        }
    }
}
// the middles, the normals (Newell), the label directions and the numbers
static void sol_finish(solid_t *s, bool edge_up)
{
    for (int f = 0; f < s->nf; f++) {
        int k = s->fn[f];
        v3 c = v3m(0, 0, 0), n = v3m(0, 0, 0);
        for (int i = 0; i < k; i++) {
            v3 a = s->v[s->fv[f][i]], b = s->v[s->fv[f][(i + 1) % k]];
            c = v3add(c, a);
            n = v3add(n, v3m((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y)));
        }
        c = v3mul(c, 1.0f / (float)k);
        n = v3unit(n);
        if (v3dot(n, c) < 0) {
            // (the corners went round the wrong way: turn the order)
            n = v3mul(n, -1);
            for (int i = 0; i < k / 2; i++) {
                uint8_t t = s->fv[f][i];
                s->fv[f][i] = s->fv[f][k - 1 - i];
                s->fv[f][k - 1 - i] = t;
            }
        }
        s->n[f] = n;
        s->c[f] = c;
        // the up of the label: to the first corner (a tip of a triangle, a kite or a pentagon), or to the middle of
        // the first edge (the cube: the pips stay square to the edges)
        v3 to = edge_up ? v3mul(v3add(s->v[s->fv[f][0]], s->v[s->fv[f][1]]), 0.5f) : s->v[s->fv[f][0]];
        v3 u = v3sub(to, c);
        u = v3sub(u, v3mul(n, v3dot(u, n)));
        s->up[f] = v3unit(u);
        s->val[f] = 0;
    }
    // the numbers: 1, 2, 3 ... and nf + 1 - that number on the opposite face (no opposite face: the next number)
    int next = 1;
    for (int f = 0; f < s->nf; f++) {
        if (s->val[f]) {
            continue;
        }
        s->val[f] = (uint8_t)next;
        int g = -1;
        for (int h = 0; h < s->nf; h++) {
            if (h != f && !s->val[h] && v3dot(s->n[h], s->n[f]) < -0.999f) {
                g = h;
            }
        }
        if (g >= 0) {
            s->val[g] = (uint8_t)(s->nf + 1 - next);
        }
        next++;
        if (g < 0) {
            continue;
        }
    }
    // (the d4 has no opposite faces: the loop above gives 1, 2, 3, 4; for the others the first half of the numbers
    // goes up and the second half comes from the opposite faces)
    for (int f = 0; f < s->nf; f++) {
        if (s->val[f] > s->nf) {
            s->val[f] = (uint8_t)s->nf;
        }
    }
}
static void sol_norm(solid_t *s)
{
    float r = 0;
    for (int i = 0; i < s->nv; i++) {
        if (v3len(s->v[i]) > r) {
            r = v3len(s->v[i]);
        }
    }
    for (int i = 0; i < s->nv; i++) {
        s->v[i] = v3mul(s->v[i], 1.0f / r);
    }
}

static void sol_build(void)
{
    if (s_sol_ok) {
        return;
    }
    const float P = 1.6180340f, IP = 0.6180340f;
    solid_t *s;
    // d4: the tetrahedron (it rests on a face; the number at the top corner is the result)
    s = &s_sol[SO_D4];
    {
        static const float V[] = {1, 1, 1, 1, -1, -1, -1, 1, -1, -1, -1, 1};
        s->nv = 4;
        for (int i = 0; i < 4; i++) {
            s->v[i] = v3m(V[3 * i], V[3 * i + 1], V[3 * i + 2]);
        }
        sol_norm(s);
        sol_hull(s);
        sol_finish(s, false);
        s->lab = 0.30f;
    }
    // d6: the cube
    s = &s_sol[SO_D6];
    {
        s->nv = 8;
        for (int i = 0; i < 8; i++) {
            s->v[i] = v3m(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
        }
        sol_norm(s);
        sol_hull(s);
        sol_finish(s, true);
        s->lab = 0.50f;
    }
    // d8: the octahedron
    s = &s_sol[SO_D8];
    {
        static const float V[] = {1, 0, 0, -1, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 1, 0, 0, -1};
        s->nv = 6;
        for (int i = 0; i < 6; i++) {
            s->v[i] = v3m(V[3 * i], V[3 * i + 1], V[3 * i + 2]);
        }
        sol_hull(s);
        sol_finish(s, false);
        s->lab = 0.42f;
    }
    // d10: the pentagonal trapezohedron (10 kites). The rings are at +-z1: then each kite is flat.
    s = &s_sol[SO_D10];
    {
        const float H = 1.15f, c36 = 0.809017f, z1 = H * (1 - c36) / (1 + c36);
        s->nv = 12;
        s->v[0] = v3m(0, 0, H);
        s->v[1] = v3m(0, 0, -H);
        for (int i = 0; i < 5; i++) {
            float a = 1.2566371f * (float)i;
            s->v[2 + i] = v3m(fcos(a), fsin(a), z1);
            s->v[7 + i] = v3m(fcos(a + 0.6283185f), fsin(a + 0.6283185f), -z1);
        }
        sol_norm(s);
        s->nf = 10;
        for (int i = 0; i < 5; i++) {
            int j = (i + 1) % 5;
            uint8_t *t = s->fv[i], *b = s->fv[5 + i];
            t[0] = 0;
            t[1] = (uint8_t)(2 + i);
            t[2] = (uint8_t)(7 + i);
            t[3] = (uint8_t)(2 + j);
            b[0] = 1;
            b[1] = (uint8_t)(7 + j);
            b[2] = (uint8_t)(2 + j);
            b[3] = (uint8_t)(7 + i);
            s->fn[i] = s->fn[5 + i] = 4;
        }
        sol_finish(s, false);
        // (the label sits a little toward the wide end of the kite)
        for (int f = 0; f < 10; f++) {
            s->c[f] = v3sub(s->c[f], v3mul(s->up[f], 0.06f));
        }
        s->lab = 0.32f;
    }
    // d12: the dodecahedron
    s = &s_sol[SO_D12];
    {
        int k = 0;
        for (int i = 0; i < 8; i++) {
            s->v[k++] = v3m(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
        }
        for (int i = 0; i < 4; i++) {
            float a = i & 1 ? IP : -IP, b = i & 2 ? P : -P;
            s->v[k++] = v3m(0, a, b);
            s->v[k++] = v3m(a, b, 0);
            s->v[k++] = v3m(b, 0, a);
        }
        s->nv = 20;
        sol_norm(s);
        sol_hull(s);
        sol_finish(s, false);
        s->lab = 0.36f;
    }
    // d20: the icosahedron
    s = &s_sol[SO_D20];
    {
        int k = 0;
        for (int i = 0; i < 4; i++) {
            float a = i & 1 ? 1 : -1, b = i & 2 ? P : -P;
            s->v[k++] = v3m(0, a, b);
            s->v[k++] = v3m(a, b, 0);
            s->v[k++] = v3m(b, 0, a);
        }
        s->nv = 12;
        sol_norm(s);
        sol_hull(s);
        sol_finish(s, false);
        s->lab = 0.29f;
    }
    s_sol_ok = true;
}

// the turn that puts face f on top (its normal to the camera, its label up on the screen), then yaw about the view axis
static quat sol_rest(const solid_t *s, int f, float yaw)
{
    v3 n = s->n[f], u = s->up[f], r = v3cross(u, n);
    float m[9] = {r.x, r.y, r.z, u.x, u.y, u.z, n.x, n.y, n.z};
    return qmul(qaxis(v3m(0, 0, 1), yaw), qfrom(m));
}
// the face of a value
static int face_with(int so, int val)
{
    const solid_t *s = &s_sol[so];
    for (int f = 0; f < s->nf; f++) {
        if (s->val[f] == val) {
            return f;
        }
    }
    return 0;
}
static quat s_view;  // the tilt of the camera (app_dice.c init)
// the rest turn of a die with the value val on top. The d4: its corner val points to the camera (the eye looks at the
// tilted table a little from the south: else one face of the pyramid is edge-on); each other die: the face val.
static quat rest_of(int so, int val, float yaw)
{
    const solid_t *s = &s_sol[so];
    if (so == SO_D4) {
        v3 n = v3unit(s->v[(val - 1) & 3]), o = s->v[val & 3];
        v3 u = v3mul(v3unit(v3sub(o, v3mul(n, v3dot(o, n)))), -1);
        v3 r = v3cross(u, n);
        float m[9] = {r.x, r.y, r.z, u.x, u.y, u.z, n.x, n.y, n.z};
        return qmul(qconj(s_view), qmul(qaxis(v3m(0, 0, 1), yaw), qfrom(m)));
    }
    return sol_rest(s, face_with(so, val), yaw);
}

// ---------------------------------------------------------------- the label masks
// One mask for each label: 8-bit alpha, LW x LH px; the cap height of the numbers is 35 px (MF_N34) in the middle.
#define LW 64
#define LH 48
enum {
    LB_NUM = 0,
    LB_TENS = 20,
    LB_ZERO = 30,
    LB_PIP = 31,
    LB_N = 37
};  // 1..20, 00..90, 0, the pips 1..6
static uint8_t s_mask[LB_N][LH * LW];
static uint8_t s_mask1[LB_N][(LH / 2) * (LW / 2)], s_mask2[LB_N][(LH / 4) * (LW / 4)];  // half and quarter size
static bool s_mask_ok;

// a mask at half size (a box of 2 x 2 px for each px)
static void mask_half(const uint8_t *src, int w, int h, uint8_t *dst)
{
    for (int y = 0; y < h / 2; y++) {
        for (int x = 0; x < w / 2; x++) {
            const uint8_t *p = src + (2 * y) * w + 2 * x;
            dst[y * (w / 2) + x] = (uint8_t)((p[0] + p[1] + p[w] + p[w + 1] + 2) >> 2);
        }
    }
}

// a text of MF_N34 into a mask (w x h), its caps in the middle; bar: a 6 or a 9 alone gets a bar under it (a face)
static void mask_text(uint8_t *m, int w, int h, const char *str, bool bar)
{
    memset(m, 0, (size_t)(w * h));
    const mfont_t *f = &MF_N34;
    int tw = mf_width(f, str, 0);
    float fx = (float)((w - tw) / 2);
    int base = h / 2 + f->asc / 2;
    for (const char *p = str; *p; p++) {
        const mglyph_t *g = mf_glyph(f, (uint8_t)*p);
        if (!g) {
            continue;
        }
        int gx = (int)(fx + 0.5f) + g->ox, gy = base + g->oy;
        const uint8_t *src = f->a4 + g->off;
        for (int j = 0; j < g->h; j++) {
            int y = gy + j;
            if (y < 0 || y >= h) {
                continue;
            }
            for (int k = 0; k < g->w; k++) {
                int x = gx + k;
                if (x < 0 || x >= w) {
                    continue;
                }
                int idx = j * g->w + k, v = (idx & 1) ? src[idx >> 1] >> 4 : src[idx >> 1] & 15;
                if (v * 17 > m[y * w + x]) {
                    m[y * w + x] = (uint8_t)(v * 17);
                }
            }
        }
        fx += g->adv4 / 4.0f;
    }
    if (bar && (str[0] == '6' || str[0] == '9') && !str[1]) {
        int x0 = (w - tw) / 2 + 3, x1 = (w + tw) / 2 - 3, y0 = base + 3;
        for (int y = y0; y < y0 + 4 && y < h; y++) {
            for (int x = x0; x < x1; x++) {
                m[y * w + x] = 255;
            }
        }
    }
}
// the pips of a d6 face (n = 1..6) into a mask: a 3 x 3 grid in the square of 44 px in the middle
static void mask_pips(uint8_t *m, int n)
{
    memset(m, 0, LW * LH);
    static const int8_t PIPS[6][6][2] = {
            {{0, 0}},
            {{-1, -1}, {1, 1}},
            {{-1, -1}, {0, 0}, {1, 1}},
            {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}},
            {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}},
            {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}},
    };
    const float d = 13.5f, pr = n == 1 ? 8.0f : 5.6f;
    for (int i = 0; i < n; i++) {
        float cx = LW / 2.0f + PIPS[n - 1][i][0] * d, cy = LH / 2.0f + PIPS[n - 1][i][1] * d;
        for (int y = (int)(cy - pr - 2); y <= (int)(cy + pr + 2); y++) {
            for (int x = (int)(cx - pr - 2); x <= (int)(cx + pr + 2); x++) {
                if (x < 0 || y < 0 || x >= LW || y >= LH) {
                    continue;
                }
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy, cov = pr + 0.5f - sqrtf(dx * dx + dy * dy);
                if (cov <= 0) {
                    continue;
                }
                int a = cov >= 1 ? 255 : (int)(cov * 255);
                if (a > m[y * LW + x]) {
                    m[y * LW + x] = (uint8_t)a;
                }
            }
        }
    }
}
static void mask_build(void)
{
    if (s_mask_ok) {
        return;
    }
    char b[8];
    for (int i = 0; i < 20; i++) {
        snprintf(b, sizeof b, "%d", i + 1);
        mask_text(s_mask[LB_NUM + i], LW, LH, b, true);
    }
    for (int i = 0; i < 10; i++) {
        snprintf(b, sizeof b, "%d0", i);
        mask_text(s_mask[LB_TENS + i], LW, LH, b, true);
    }
    mask_text(s_mask[LB_ZERO], LW, LH, "0", true);
    for (int i = 0; i < 6; i++) {
        mask_pips(s_mask[LB_PIP + i], i + 1);
    }
    for (int i = 0; i < LB_N; i++) {
        mask_half(s_mask[i], LW, LH, s_mask1[i]);
        mask_half(s_mask1[i], LW / 2, LH / 2, s_mask2[i]);
    }
    s_mask_ok = true;
}

// Draw a mask (w x h) with an affine map: o = the screen point of the middle of the mask, a = the screen step of one
// mask px to the right, b = of one mask px down. Bilinear samples; ink colour with alpha al (0..256).
static void mask_draw(const uint8_t *m, int w, int h, float ox, float oy, float ax, float ay, float bx, float by,
                      uint16_t ink, int al)
{
    float det = ax * by - ay * bx;
    if (det > -1e-4f && det < 1e-4f) {
        return;
    }
    float hw = w * 0.5f, hh = h * 0.5f;
    float x0 = ox, x1 = ox, y0 = oy, y1 = oy;
    for (int k = 0; k < 4; k++) {
        float sx = (k & 1 ? hw : -hw), sy = (k & 2 ? hh : -hh);
        float px = ox + ax * sx + bx * sy, py = oy + ay * sx + by * sy;
        if (px < x0) {
            x0 = px;
        }
        if (px > x1) {
            x1 = px;
        }
        if (py < y0) {
            y0 = py;
        }
        if (py > y1) {
            y1 = py;
        }
    }
    int ix0 = (int)x0 - 1, ix1 = (int)x1 + 1, iy0 = (int)y0 - 1, iy1 = (int)y1 + 1;
    if (ix0 < tfd_cx0) {
        ix0 = tfd_cx0;
    }
    if (ix0 < 0) {
        ix0 = 0;
    }
    if (iy0 < tfd_cy0) {
        iy0 = tfd_cy0;
    }
    if (iy0 < 0) {
        iy0 = 0;
    }
    if (ix1 > tfd_cx1 - 1) {
        ix1 = tfd_cx1 - 1;
    }
    if (ix1 > tfd_w - 1) {
        ix1 = tfd_w - 1;
    }
    if (iy1 > tfd_cy1 - 1) {
        iy1 = tfd_cy1 - 1;
    }
    if (iy1 > tfd_h - 1) {
        iy1 = tfd_h - 1;
    }
    // (the mask point of a screen point p: u = (b x (p - o)) ... the inverse of the 2 x 2 map)
    float iu_x = by / det, iu_y = -bx / det, iv_x = -ay / det, iv_y = ax / det;
    for (int y = iy0; y <= iy1; y++) {
        float dy = y + 0.5f - oy, dx = ix0 + 0.5f - ox;
        float u = dx * iu_x + dy * iu_y + hw - 0.5f, v = dx * iv_x + dy * iv_y + hh - 0.5f;
        uint16_t *row = &tfd_fb[y * tfd_w];
        for (int x = ix0; x <= ix1; x++, u += iu_x, v += iv_x) {
            if (u < 0 || v < 0 || u >= w - 1 || v >= h - 1) {
                continue;
            }
            int iu = (int)u, iv = (int)v;
            float fu = u - iu, fv = v - iv;
            const uint8_t *p = m + iv * w + iu;
            if (!(p[0] | p[1] | p[w] | p[w + 1])) {
                continue;
            }
            float s = (p[0] * (1 - fu) + p[1] * fu) * (1 - fv) + (p[w] * (1 - fu) + p[w + 1] * fu) * fv;
            int a = (int)(s * al) >> 8;
            if (a <= 0) {
                continue;
            }
            row[x] = a >= 256 ? ink : tfd_mix(row[x], ink, a);
        }
    }
}

// ---------------------------------------------------------------- the renderer of one die
// (s_view, the tilt of the camera, is over rest_of: a view from a little to the south and to the east, so that two
// sides of a die show)
static const v3 LIGHT = {-0.42f, 0.58f, 0.70f};

static inline uint16_t col_scale(const uint8_t c[3], float k, float spec)
{
    return tfd_rgb((int)(c[0] * k + 255 * spec), (int)(c[1] * k + 255 * spec), (int)(c[2] * k + 255 * spec));
}

// a 2D convex hull of n points (x, y pairs) into out; returns the count (the monotone chain)
static int hull2(const float *p, int n, float *out)
{
    int idx[MAXV], k = 0;
    for (int i = 0; i < n; i++) {
        idx[i] = i;
    }
    for (int i = 1; i < n; i++) {
        for (int j = i; j > 0 && (p[2 * idx[j - 1]] > p[2 * idx[j]] ||
                                  (p[2 * idx[j - 1]] == p[2 * idx[j]] && p[2 * idx[j - 1] + 1] > p[2 * idx[j] + 1]));
             j--) {
            int t = idx[j];
            idx[j] = idx[j - 1];
            idx[j - 1] = t;
        }
    }
    int h[2 * MAXV + 2];
#define CR(o, a, b)                                                                                                    \
    ((p[2 * (a)] - p[2 * (o)]) * (p[2 * (b) + 1] - p[2 * (o) + 1]) -                                                   \
     (p[2 * (a) + 1] - p[2 * (o) + 1]) * (p[2 * (b)] - p[2 * (o)]))
    for (int i = 0; i < n; i++) {
        while (k >= 2 && CR(h[k - 2], h[k - 1], idx[i]) <= 0) {
            k--;
        }
        h[k++] = idx[i];
    }
    for (int i = n - 2, t = k + 1; i >= 0; i--) {
        while (k >= t && CR(h[k - 2], h[k - 1], idx[i]) <= 0) {
            k--;
        }
        h[k++] = idx[i];
    }
#undef CR
    k--;
    for (int i = 0; i < k; i++) {
        out[2 * i] = p[2 * h[i]];
        out[2 * i + 1] = p[2 * h[i] + 1];
    }
    return k;
}

typedef struct {
    uint8_t so;          // SO_*
    uint8_t lset;        // the labels: LS_*
    const uint8_t *col;  // the base colour (r, g, b)
    uint16_t ink;        // the colour of the numbers
    float tint;          // 0..1: a grey and dark tint (the sad effect of a natural 1)
} look_t;
enum {
    LS_NUM,
    LS_PIP,
    LS_TENS,
    LS_ONES
};

static int label_of(int lset, int val)
{
    if (lset == LS_PIP) {
        return LB_PIP + val - 1;
    }
    if (lset == LS_TENS) {
        return LB_TENS + (val - 1);  // faces 1..10: 00, 10 .. 90
    }
    if (lset == LS_ONES) {
        return val == 1 ? LB_ZERO : LB_NUM + val - 2;  // faces 1..10: 0, 1 .. 9
    }
    return LB_NUM + val - 1;
}

// one label on a face: c = its middle, u = its up, n = the normal of the face (model), sc = model units for one mask
// px, li = the mask, al = the alpha
static void label_put(const look_t *lk, const uint8_t *base, const float *m, float cx, float cy, float r, v3 cm, v3 um,
                      v3 nm, float sc, int li, int al)
{
    v3 c = mrot(m, cm), u = mrot(m, um), rt = mrot(m, v3cross(um, nm));
    float kc = r / (1.0f - 0.16f * c.z);
    float ox = cx + c.x * kc, oy = cy - c.y * kc;
    float ax = rt.x * kc * sc, ay = -rt.y * kc * sc, bx = -u.x * kc * sc, by = u.y * kc * sc;
    // (a small label: a smaller copy of the mask, so that the samples do not skip px)
    int mw = LW, mh = LH;
    const uint8_t *mk = s_mask[li];
    float step = sqrtf(fabsf(ax * by - ay * bx));
    if (step < 0.42f) {
        mk = s_mask2[li];
        mw = LW / 4;
        mh = LH / 4;
        ax *= 4;
        ay *= 4;
        bx *= 4;
        by *= 4;
    } else if (step < 0.80f) {
        mk = s_mask1[li];
        mw = LW / 2;
        mh = LH / 2;
        ax *= 2;
        ay *= 2;
        bx *= 2;
        by *= 2;
    }
    if (r > 26 && al > 128) {
        // (a dark edge under the number: it looks cut into the face)
        float o = r > 60 ? 1.2f : 0.8f;
        mask_draw(mk, mw, mh, ox + o, oy + o, ax, ay, bx, by, col_scale(base, 0.25f, 0), al * 3 / 5);
    }
    mask_draw(mk, mw, mh, ox, oy, ax, ay, bx, by, lk->ink, al);
}

// Draw one die: the middle (cx, cy) on the screen, the radius r px, the turn q (model to world).
static void die_draw(const look_t *lk, float cx, float cy, float r, quat q)
{
    const solid_t *s = &s_sol[lk->so];
    float m[9];
    qmat(qmul(s_view, q), m);
    float P[2 * MAXV];
    for (int i = 0; i < s->nv; i++) {
        v3 p = mrot(m, s->v[i]);
        float k = r / (1.0f - 0.16f * p.z);  // a little perspective: the near side is bigger
        P[2 * i] = cx + p.x * k;
        P[2 * i + 1] = cy - p.y * k;
    }
    uint8_t base[3] = {lk->col[0], lk->col[1], lk->col[2]};
    if (lk->tint > 0) {
        int g = (base[0] * 3 + base[1] * 5 + base[2] * 2) / 10;
        for (int i = 0; i < 3; i++) {
            base[i] = (uint8_t)(base[i] + ((g * 0.55f) - base[i]) * lk->tint);
        }
    }
    // the outline of the whole die (the seams between the faces show it as thin dark edges)
    float hp[2 * MAXV + 4];
    int nh = hull2(P, s->nv, hp);
    tfd_poly(hp, nh, col_scale(base, 0.22f, 0), 256);
    v3 L = v3unit(LIGHT), Hh = v3unit(v3add(L, v3m(0, 0, 1)));
    for (int f = 0; f < s->nf; f++) {
        int k = s->fn[f];
        // a face that looks at the camera: its corners go round counter-clockwise on the screen (y up)
        float area = 0;
        for (int i = 0; i < k; i++) {
            int a = s->fv[f][i], b = s->fv[f][(i + 1) % k];
            area += P[2 * a] * (-P[2 * b + 1]) - P[2 * b] * (-P[2 * a + 1]);
        }
        if (area <= 0.5f) {
            continue;
        }
        v3 n = mrot(m, s->n[f]);
        float d = v3dot(n, L), sp = v3dot(n, Hh);
        float lit = d > 0 ? 0.52f + 0.46f * d : 0.52f + 0.22f * d;
        if (lit < 0.34f) {
            lit = 0.34f;
        }
        sp = sp > 0.90f ? (sp - 0.90f) * 10.0f : 0;
        sp = sp * sp * 0.14f;
        float pts[2 * MAXFV], cxf = 0, cyf = 0;
        for (int i = 0; i < k; i++) {
            pts[2 * i] = P[2 * s->fv[f][i]];
            pts[2 * i + 1] = P[2 * s->fv[f][i] + 1];
            cxf += pts[2 * i];
            cyf += pts[2 * i + 1];
        }
        cxf /= k;
        cyf /= k;
        // the bevel: the full face a little darker, then the face smaller with cut corners
        tfd_poly(pts, k, col_scale(base, lit * 0.80f, sp * 0.4f), 256);
        float in[4 * MAXFV], sh = r > 30 ? 0.08f : 0.05f;
        for (int i = 0; i < k; i++) {
            pts[2 * i] += (cxf - pts[2 * i]) * sh;
            pts[2 * i + 1] += (cyf - pts[2 * i + 1]) * sh;
        }
        for (int i = 0; i < k; i++) {
            int a = (i + k - 1) % k, b = (i + 1) % k;
            const float cut = 0.14f;
            in[4 * i] = pts[2 * i] + (pts[2 * a] - pts[2 * i]) * cut;
            in[4 * i + 1] = pts[2 * i + 1] + (pts[2 * a + 1] - pts[2 * i + 1]) * cut;
            in[4 * i + 2] = pts[2 * i] + (pts[2 * b] - pts[2 * i]) * cut;
            in[4 * i + 3] = pts[2 * i + 1] + (pts[2 * b + 1] - pts[2 * i + 1]) * cut;
        }
        tfd_poly(in, 2 * k, col_scale(base, lit, sp), 256);
        // the labels: an affine map of the mask onto the face
        if (lk->so == SO_D4) {
            // (the d4: a number at each corner of the face, its top to that corner; the corner at the top of the
            // die has the same number on each face that shows)
            if (n.z < 0.06f) {
                continue;
            }
            int al = n.z > 0.30f ? 256 : (int)((n.z - 0.06f) / 0.24f * 256);
            for (int i = 0; i < k; i++) {
                v3 vc = s->v[s->fv[f][i]], c = v3add(s->c[f], v3mul(v3sub(vc, s->c[f]), 0.56f));
                v3 u = v3unit(v3sub(vc, s->c[f]));
                label_put(lk, base, m, cx, cy, r, c, u, s->n[f], s->lab / 35.0f, label_of(lk->lset, s->fv[f][i] + 1),
                          al);
            }
            continue;
        }
        if (n.z < 0.22f) {
            continue;
        }
        int al = n.z > 0.55f ? 256 : (int)((n.z - 0.22f) / 0.33f * 256);
        float sc = lk->lset == LS_PIP ? (0.577f * 2 * 0.80f) / 44.0f : s->lab / 35.0f;
        label_put(lk, base, m, cx, cy, r, s->c[f], s->up[f], s->n[f], sc, label_of(lk->lset, s->val[f]), al);
    }
}
