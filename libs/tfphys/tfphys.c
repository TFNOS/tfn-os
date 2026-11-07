// libs/tfphys: 2D rigid-body physics (see tfphys.h).// The solver follows Box2D v3 by Erin Catto (MIT licence): the soft step (sub-steps, soft contacts, a relax pass, the
// restitution at the end), the speculative contacts, the polygon clipper and the joint math. The broad phase (a sort
// and sweep on x), the islands (the sleep), the breaks, the particles and the events are tfphys code.
// All the math is float: the board has a float unit and no double unit (no double constant, no double function).
#include "tfphys.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif

#ifndef TP_PROF
#define TP_PROF(n) ((void)0)  // (a speed test defines it: a time mark at the start of each part of a step)
#endif

#define PI_F      3.14159265f
#define SLOT_BITS 12
#define SLOT_MASK 0xFFF
#define HASH_N    1024  // the buckets of the particle grid (a power of 2)
#define BRK_CAP   32
#define CELL_MAX  16  // the corners of a cell of a break, at most (before the clean-up)

typedef tp_vec_t vec;
typedef tp_rot_t rot_t;
typedef tp_xf_t xf_t;

// ---------------------------------------------------------------- the math
static inline vec V(float x, float y)
{
    vec r;
    r.x = x;
    r.y = y;
    return r;
}
static inline vec vadd(vec a, vec b)
{
    return V(a.x + b.x, a.y + b.y);
}
static inline vec vsub(vec a, vec b)
{
    return V(a.x - b.x, a.y - b.y);
}
static inline vec vneg(vec a)
{
    return V(-a.x, -a.y);
}
static inline vec vscale(float s, vec a)
{
    return V(s * a.x, s * a.y);
}
static inline vec vmadd(vec a, float s, vec b)
{
    return V(a.x + s * b.x, a.y + s * b.y);
}
static inline float vdot(vec a, vec b)
{
    return a.x * b.x + a.y * b.y;
}
static inline float vcross(vec a, vec b)
{
    return a.x * b.y - a.y * b.x;
}
static inline vec vcrosssv(float s, vec a)
{
    return V(-s * a.y, s * a.x);
}  // s x a
static inline vec vrperp(vec a)
{
    return V(a.y, -a.x);
}
static inline vec vlperp(vec a)
{
    return V(-a.y, a.x);
}
static inline float vlen2(vec a)
{
    return a.x * a.x + a.y * a.y;
}
static inline float vlen(vec a)
{
    return sqrtf(a.x * a.x + a.y * a.y);
}
static inline vec vlerp(vec a, vec b, float t)
{
    return V(a.x + t * (b.x - a.x), a.y + t * (b.y - a.y));
}
static inline vec vnorm(vec a)
{
    float l = vlen(a);
    if (l < 1e-12f) {
        return V(0, 0);
    }
    return vscale(1.0f / l, a);
}
static inline float fab(float x)
{
    return x < 0 ? -x : x;
}
static inline float fmn(float a, float b)
{
    return a < b ? a : b;
}
static inline float fmx(float a, float b)
{
    return a > b ? a : b;
}
static inline float fcl(float x, float a, float b)
{
    return x < a ? a : x > b ? b : x;
}
static inline int ifloor(float v)
{
    int i = (int)v;
    return v < (float)i ? i - 1 : i;
}

static inline rot_t R(float a)
{
    rot_t q;
    q.c = cosf(a);
    q.s = sinf(a);
    return q;
}
static const rot_t RID = {1.0f, 0.0f};
static inline vec rot(rot_t q, vec v)
{
    return V(q.c * v.x - q.s * v.y, q.s * v.x + q.c * v.y);
}
static inline vec irot(rot_t q, vec v)
{
    return V(q.c * v.x + q.s * v.y, -q.s * v.x + q.c * v.y);
}
static inline rot_t rmul(rot_t a, rot_t b)
{
    rot_t r;
    r.c = a.c * b.c - a.s * b.s;
    r.s = a.s * b.c + a.c * b.s;
    return r;
}
static inline rot_t rimul(rot_t a, rot_t b)
{
    rot_t r;
    r.c = a.c * b.c + a.s * b.s;
    r.s = a.c * b.s - a.s * b.c;
    return r;
}
static inline rot_t rnorm(rot_t q)
{
    float m = sqrtf(q.c * q.c + q.s * q.s), im = m > 0 ? 1.0f / m : 0.0f;
    q.c *= im;
    q.s *= im;
    return q;
}
// a rotation turned by a small angle (the first order step, then the length 1 again: near 1, one Newton step from 1
// for 1 / sqrt(m2) is enough and needs no root and no division)
static inline rot_t rinteg(rot_t q, float da)
{
    rot_t r;
    r.c = q.c - da * q.s;
    r.s = q.s + da * q.c;
    float m2 = r.c * r.c + r.s * r.s;
    if (m2 > 0.9f && m2 < 1.1f) {
        float im = 1.5f - 0.5f * m2;
        r.c *= im;
        r.s *= im;
        return r;
    }
    return rnorm(r);
}
static inline vec xf_apply(xf_t x, vec p)
{
    return vadd(rot(x.q, p), x.p);
}
static inline vec xf_inv(xf_t x, vec p)
{
    return irot(x.q, vsub(p, x.p));
}
static inline xf_t xf_invmul(xf_t a, xf_t b)
{
    xf_t r;
    r.q = rimul(a.q, b.q);
    r.p = irot(a.q, vsub(b.p, a.p));
    return r;
}
// a fast atan2 (the polynomial of Box2D v3, about 1e-4 rad): the joint angles
static float fatan2(float y, float x)
{
    float ax = fab(x), ay = fab(y), mx = fmx(ay, ax), mn = fmn(ay, ax);
    float a = mn / (mx + FLT_MIN);
    float s = a * a, c = s * a, q = s * s;
    float r = 0.024840285f * q + 0.18681418f, t = -0.094097948f * q - 0.33213072f;
    r = r * s + t;
    r = r * c + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0) {
        r = 3.14159274f - r;
    }
    if (y < 0) {
        r = -r;
    }
    return r;
}
static inline float relang(rot_t b, rot_t a)
{
    return fatan2(b.s * a.c - b.c * a.s, b.c * a.c + b.s * a.s);
}
static inline float unwind(float a)
{
    if (a < -PI_F) {
        return a + 2.0f * PI_F;
    }
    if (a > PI_F) {
        return a - 2.0f * PI_F;
    }
    return a;
}
static vec solve22(float a11, float a12, float a21, float a22, vec b)
{
    float det = a11 * a22 - a12 * a21;
    if (det != 0.0f) {
        det = 1.0f / det;
    }
    return V(det * (a22 * b.x - a12 * b.y), det * (a11 * b.y - a21 * b.x));
}
static tp_soft_t mksoft(float hz, float zeta, float h)
{
    tp_soft_t s;
    if (hz <= 0.0f) {
        s.bias = 0.0f;
        s.mass = 1.0f;
        s.imp = 0.0f;
        return s;
    }
    float om = 2.0f * PI_F * hz, a1 = 2.0f * zeta + h * om, a2 = h * om * a1, a3 = 1.0f / (1.0f + a2);
    s.bias = om / a1;
    s.mass = a2 * a3;
    s.imp = a3;
    return s;
}
static inline tp_id_t mkid(int slot, uint32_t gid)
{
    return (tp_id_t)(((gid & 0x7FFFFu) << SLOT_BITS) | (uint32_t)slot);
}
static uint32_t rnd(tp_world_t *w)
{
    uint32_t x = w->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w->rng = x;
    return x;
}
static float rndf(tp_world_t *w)
{
    return (float)(int32_t)(rnd(w) >> 8) * (1.0f / 16777216.0f);
}  // 0 .. 1

// ---------------------------------------------------------------- the internal arrays
typedef struct {
    vec v;
    float w, pad;
    vec dp;
    rot_t dq;
} st_t;  // a body in the solver (32 bytes)
// a point of a contact in the solver. The solver works in the frame of the normal n and the tangent t: the anchors
// are only their products with n and t (dn = dot(n, r), rn = cross(r, n), tn = cross(r, t)); the separation as the
// bodies turn by dq: dot(n, rot(dq, r)) = dq.c dn + dq.s rn.
// (ka, kb: the change of the turn speed of A and of B for a normal impulse of 1 = ii x rn)
// (total: the sum of the normal impulses after each relax; 0 = the point had no impulse in the step: no hit)
typedef struct {
    float sep, dna, dnb, rna, rnb, ka, kb, tna, tnb, nm, tm, ni, ti, rv, total;
} ccp_t;  // 60 bytes
typedef struct {
    int16_t a, b, c;   // the bodies, the contact
    uint8_t np, stat;  // the points; stat = a body with no inverse mass (the stiffer softness)
    vec n;
    float fr, e, rr, rm, rolli;  // friction, restitution, rolling (x radius), rolling mass and impulse
    float ni0;                   // the sum of the impulses of the warm start (0 = a new touch)
    float ima, iia, imb, iib;
    ccp_t p[2];
} cc_t;
typedef struct {
    float x0, x1, y0, y1;
    int16_t b;
    uint8_t act, dyn;
    uint16_t group, cat, mask, pad;
} sap_t;
typedef struct {
    int16_t i;
    uint8_t dyn, pad;
    float ld, ad, tw;
    vec gv;
} mv_t;
typedef struct {
    int16_t body, next;
} gent_t;
typedef struct {
    int16_t x, o;
    uint32_t ogid;
    float dv, excess;
    vec p, dir;
} brk_t;

struct tp_in {
    st_t *st;  // [body_cap]
    cc_t *cc;  // [contact_cap]
    int ncc;
    sap_t *sap;  // [body_cap] the bodies sorted on the lower x of their boxes
    int nsap;
    mv_t *mv;  // [body_cap] the bodies that move in this step
    int nmv;
    int16_t *jl;  // [joint_cap] the joints in the solver
    int njl;
    int16_t *stack, *isl_head;  // [body_cap]
    int nisl;
    int16_t *bfree, *jfree, *cfree;  // the free slots
    int nbfree, njfree, ncfree;
    int16_t *grid;  // [HASH_N]
    gent_t *gent;
    int ngent, gent_cap;
    float icell;
    brk_t *brk;
    int nbrk;
    float h, inv_h;
    tp_soft_t soft_c, soft_s, soft_j;
    uint32_t seed;
    bool dirty;  // a body went away in this step (no island sleeps)
};

// the default materials: density, friction, restitution, toughness, rolling, burst; sound, pattern, pieces, crumbs
static const tp_mat_t MAT0[7] = {
        {2.5f, 0.4f, 0.05f, 2.5f, 0.0f, 0.9f, TP_SND_GLASS, TP_BREAK_SHARDS, 7, 10, TP_PART_SHARD},
        {0.7f, 0.6f, 0.1f, 7.0f, 0.0f, 0.25f, TP_SND_WOOD, TP_BREAK_SPLINTER, 3, 4, TP_PART_CRUMB},
        {7.8f, 0.3f, 0.1f, 0.0f, 0.0f, 0.2f, TP_SND_METAL, TP_BREAK_CHUNKS, 3, 0, TP_PART_SPARK},
        {0.5f, 0.7f, 0.05f, 3.0f, 0.0f, 0.5f, TP_SND_CHIP, TP_BREAK_CHUNKS, 3, 6, TP_PART_CRUMB},
        {1.1f, 0.9f, 0.5f, 0.0f, 0.04f, 0.2f, TP_SND_GUMMY, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB},
        {2.4f, 0.7f, 0.05f, 0.0f, 0.0f, 0.4f, TP_SND_STONE, TP_BREAK_CHUNKS, 4, 6, TP_PART_CRUMB},
        {1.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.2f, TP_SND_WATER, TP_BREAK_CHUNKS, 2, 0, TP_PART_WATER},
};
// the particle kinds: gravity, drag, restitution, friction, life, life_var, radius
static const tp_pkind_t PK0[TP_PART_KINDS] = {
        {1.0f, 0.05f, 0.1f, 0.04f, 4.0f, 1.5f, 0.012f},   // water
        {1.0f, 0.1f, 0.3f, 0.5f, 2.5f, 1.0f, 0.008f},     // crumb
        {0.25f, 2.0f, 0.5f, 0.2f, 0.45f, 0.25f, 0.004f},  // spark
        {1.0f, 0.1f, 0.25f, 0.35f, 1.6f, 0.8f, 0.006f},   // shard
};

// ---------------------------------------------------------------- the memory
enum {
    L_WORLD,
    L_IN,
    L_BODY,
    L_JOINT,
    L_CONTACT,
    L_PART,
    L_HIT,
    L_EV,
    L_PIECE,
    L_ST,
    L_CC,
    L_SAP,
    L_MV,
    L_JL,
    L_STACK,
    L_ISLH,
    L_BFREE,
    L_JFREE,
    L_CFREE,
    L_GRID,
    L_GENT,
    L_BRK,
    L_N
};

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void def_fix(const tp_world_def_t *in, tp_world_def_t *d)
{
    *d = *in;
    d->max_bodies = clampi(d->max_bodies, 2, 4096);
    d->max_joints = clampi(d->max_joints, 1, 4096);
    d->max_contacts = clampi(d->max_contacts, 4, 16383);
    d->max_parts = clampi(d->max_parts, 0, 1 << 20);
    d->max_hits = clampi(d->max_hits, 1, 4096);
    d->max_events = clampi(d->max_events, 8, 4096);
    d->substeps = clampi(d->substeps, 1, 8);
    if (!(d->dt > 0.0f) || d->dt > 0.1f) {
        d->dt = 1.0f / 60.0f;
    }
    if (!(d->bounds.hi.x > d->bounds.lo.x) || !(d->bounds.hi.y > d->bounds.lo.y)) {
        d->bounds.lo = V(-50, -20);
        d->bounds.hi = V(50, 60);
    }
}

static size_t layout(const tp_world_def_t *d, size_t *off)
{
    size_t sz[L_N];
    int nb = d->max_bodies;
    sz[L_WORLD] = sizeof(tp_world_t);
    sz[L_IN] = sizeof(struct tp_in);
    sz[L_BODY] = sizeof(tp_body_t) * (size_t)nb;
    sz[L_JOINT] = sizeof(tp_joint_t) * (size_t)d->max_joints;
    sz[L_CONTACT] = sizeof(tp_contact_t) * (size_t)d->max_contacts;
    sz[L_PART] = sizeof(tp_part_t) * (size_t)d->max_parts;
    sz[L_HIT] = sizeof(tp_hit_t) * (size_t)d->max_hits;
    sz[L_EV] = sizeof(tp_event_t) * (size_t)d->max_events;
    sz[L_PIECE] = sizeof(tp_id_t) * 64;
    sz[L_ST] = sizeof(st_t) * (size_t)nb;
    sz[L_CC] = sizeof(cc_t) * (size_t)d->max_contacts;
    sz[L_SAP] = sizeof(sap_t) * (size_t)nb;
    sz[L_MV] = sizeof(mv_t) * (size_t)nb;
    sz[L_JL] = sizeof(int16_t) * (size_t)d->max_joints;
    sz[L_STACK] = sizeof(int16_t) * (size_t)nb;
    sz[L_ISLH] = sizeof(int16_t) * (size_t)nb;
    sz[L_BFREE] = sizeof(int16_t) * (size_t)nb;
    sz[L_JFREE] = sizeof(int16_t) * (size_t)d->max_joints;
    sz[L_CFREE] = sizeof(int16_t) * (size_t)d->max_contacts;
    sz[L_GRID] = sizeof(int16_t) * HASH_N;
    sz[L_GENT] = sizeof(gent_t) * (size_t)(nb * 8 < 512 ? 512 : nb * 8);
    sz[L_BRK] = sizeof(brk_t) * BRK_CAP;
    size_t o = 0;
    for (int i = 0; i < L_N; i++) {
        off[i] = o;
        o += (sz[i] + 15) & ~(size_t)15;
    }
    return o;
}

size_t tp_mem_size(const tp_world_def_t *dd)
{
    tp_world_def_t d;
    size_t off[L_N];
    def_fix(dd, &d);
    return layout(&d, off);
}

tp_world_def_t tp_world_defaults(void)
{
    tp_world_def_t d;
    memset(&d, 0, sizeof d);
    d.gravity = V(0.0f, -9.8f);
    d.dt = 1.0f / 60.0f;
    d.substeps = 3;
    d.bounds.lo = V(-50, -20);
    d.bounds.hi = V(50, 60);
    d.max_bodies = 256;
    d.max_joints = 64;
    d.max_contacts = 512;
    d.max_parts = 512;
    d.max_hits = 64;
    d.max_events = 128;
    d.seed = 1;
    return d;
}

// the pools empty again (tp_init, tp_clear); the generations of the slots stay (old ids stay not valid)
static void pools_reset(tp_world_t *w)
{
    struct tp_in *in = w->in;
    for (int i = 0; i < w->body_cap; i++) {
        tp_body_t *b = &w->body[i];
        uint32_t g = b->gid + 1;
        memset(b, 0, sizeof *b);
        b->gid = g > 0x7FFFFu ? 1 : g;
        in->bfree[i] = (int16_t)(w->body_cap - 1 - i);
    }
    for (int i = 0; i < w->joint_cap; i++) {
        tp_joint_t *j = &w->joint[i];
        uint32_t g = j->gid + 1;
        memset(j, 0, sizeof *j);
        j->gid = g > 0x7FFFFu ? 1 : g;
        in->jfree[i] = (int16_t)(w->joint_cap - 1 - i);
    }
    for (int i = 0; i < w->contact_cap; i++) {
        memset(&w->contact[i], 0, sizeof w->contact[i]);
        w->contact[i].a = w->contact[i].b = -1;
        in->cfree[i] = (int16_t)(w->contact_cap - 1 - i);
    }
    in->nbfree = w->body_cap;
    in->njfree = w->joint_cap;
    in->ncfree = w->contact_cap;
    in->nsap = in->ncc = in->nmv = in->njl = in->nisl = in->ngent = in->nbrk = 0;
    w->body_hi = w->nbody = w->joint_hi = w->njoint = w->contact_hi = w->ncontact = 0;
    w->npart = w->nhit = w->nev = w->npiece = 0;
    w->rng = in->seed * 2654435761u ^ 0x9E3779B9u;
    if (!w->rng) {
        w->rng = 1;
    }
}

tp_world_t *tp_init(const tp_world_def_t *dd)
{
    tp_world_def_t d;
    size_t off[L_N];
    def_fix(dd, &d);
    size_t total = layout(&d, off);
    char *m = (char *)d.mem;
    bool own = false;
    if (!m) {
#ifdef ESP_PLATFORM
        m = (char *)heap_caps_malloc(total, MALLOC_CAP_SPIRAM);
#else
        m = (char *)malloc(total);
#endif
        if (!m) {
            return NULL;
        }
        own = true;
    }
    memset(m, 0, total);
    tp_world_t *w = (tp_world_t *)(void *)(m + off[L_WORLD]);
    struct tp_in *in = (struct tp_in *)(void *)(m + off[L_IN]);
    w->in = in;
    w->mem = m;
    w->own_mem = own;
    w->body = (tp_body_t *)(void *)(m + off[L_BODY]);
    w->joint = (tp_joint_t *)(void *)(m + off[L_JOINT]);
    w->contact = (tp_contact_t *)(void *)(m + off[L_CONTACT]);
    w->part = (tp_part_t *)(void *)(m + off[L_PART]);
    w->hit = (tp_hit_t *)(void *)(m + off[L_HIT]);
    w->ev = (tp_event_t *)(void *)(m + off[L_EV]);
    w->piece = (tp_id_t *)(void *)(m + off[L_PIECE]);
    in->st = (st_t *)(void *)(m + off[L_ST]);
    in->cc = (cc_t *)(void *)(m + off[L_CC]);
    in->sap = (sap_t *)(void *)(m + off[L_SAP]);
    in->mv = (mv_t *)(void *)(m + off[L_MV]);
    in->jl = (int16_t *)(void *)(m + off[L_JL]);
    in->stack = (int16_t *)(void *)(m + off[L_STACK]);
    in->isl_head = (int16_t *)(void *)(m + off[L_ISLH]);
    in->bfree = (int16_t *)(void *)(m + off[L_BFREE]);
    in->jfree = (int16_t *)(void *)(m + off[L_JFREE]);
    in->cfree = (int16_t *)(void *)(m + off[L_CFREE]);
    in->grid = (int16_t *)(void *)(m + off[L_GRID]);
    in->gent = (gent_t *)(void *)(m + off[L_GENT]);
    in->gent_cap = d.max_bodies * 8 < 512 ? 512 : d.max_bodies * 8;
    in->brk = (brk_t *)(void *)(m + off[L_BRK]);
    w->body_cap = d.max_bodies;
    w->joint_cap = d.max_joints;
    w->contact_cap = d.max_contacts;
    w->part_cap = d.max_parts;
    w->hit_cap = d.max_hits;
    w->ev_cap = d.max_events;
    w->piece_cap = 64;
    w->gravity = d.gravity;
    w->dt = d.dt;
    w->substeps = d.substeps;
    w->bounds = d.bounds;
    w->slop = 0.005f;
    w->spec = 0.02f;
    w->contact_hz = 30.0f;
    w->contact_zeta = 10.0f;
    w->joint_hz = 60.0f;
    w->joint_zeta = 5.0f;
    w->push_max = 3.0f;
    w->bounce_min = 1.0f;
    w->sleep_v = 0.05f;
    w->sleep_time = 0.5f;
    w->v_max = 60.0f;
    w->hit_min = 0.4f;
    w->max_breaks = 4;
    w->fade_area = 0.006f;
    w->crumb_area = 0.0006f;
    memcpy(w->mat, MAT0, sizeof MAT0);
    for (int i = 7; i < TP_MAT_MAX; i++) {
        w->mat[i] = MAT0[TP_MAT_WOOD];
    }
    memcpy(w->pkind, PK0, sizeof PK0);
    // the grid of the particles: a cell of 0.5 m, more when the world is big
    float cell = d.cell > 0.0f ? d.cell : 0.5f;
    float area = (d.bounds.hi.x - d.bounds.lo.x) * (d.bounds.hi.y - d.bounds.lo.y);
    if (d.cell <= 0.0f && area / (cell * cell) > 16.0f * HASH_N) {
        cell = sqrtf(area / (16.0f * HASH_N));
    }
    in->icell = 1.0f / cell;
    in->seed = d.seed ? d.seed : 1;
    for (int i = 0; i < w->body_cap; i++) {
        w->body[i].gid = 0;
    }
    for (int i = 0; i < w->joint_cap; i++) {
        w->joint[i].gid = 0;
    }
    pools_reset(w);
    return w;
}

void tp_free(tp_world_t *w)
{
    if (!w || !w->own_mem) {
        return;
    }
#ifdef ESP_PLATFORM
    heap_caps_free(w->mem);
#else
    free(w->mem);
#endif
}

void tp_clear(tp_world_t *w)
{
    if (w) {
        pools_reset(w);
    }
}

// ---------------------------------------------------------------- the lists of a body (contacts, joints)
// An edge is (the index * 2 + the side): side 0 = the body a of the contact or the joint, side 1 = the body b.
static void clist_add(tp_world_t *w, int ci)
{
    tp_contact_t *c = &w->contact[ci];
    for (int s = 0; s < 2; s++) {
        tp_body_t *b = &w->body[s ? c->b : c->a];
        int head = b->chead;
        c->prev[s] = -1;
        c->next[s] = (int16_t)head;
        if (head >= 0) {
            w->contact[head >> 1].prev[head & 1] = (int16_t)(ci * 2 + s);
        }
        b->chead = (int16_t)(ci * 2 + s);
    }
}
static void clist_del(tp_world_t *w, int ci)
{
    tp_contact_t *c = &w->contact[ci];
    for (int s = 0; s < 2; s++) {
        tp_body_t *b = &w->body[s ? c->b : c->a];
        int p = c->prev[s], n = c->next[s];
        if (p >= 0) {
            w->contact[p >> 1].next[p & 1] = (int16_t)n;
        } else {
            b->chead = (int16_t)n;
        }
        if (n >= 0) {
            w->contact[n >> 1].prev[n & 1] = (int16_t)p;
        }
    }
}
static void jlist_add(tp_world_t *w, int ji)
{
    tp_joint_t *j = &w->joint[ji];
    for (int s = 0; s < 2; s++) {
        tp_body_t *b = &w->body[s ? j->b : j->a];
        int head = b->jhead;
        j->prev[s] = -1;
        j->next[s] = (int16_t)head;
        if (head >= 0) {
            w->joint[head >> 1].prev[head & 1] = (int16_t)(ji * 2 + s);
        }
        b->jhead = (int16_t)(ji * 2 + s);
    }
}
static void jlist_del(tp_world_t *w, int ji)
{
    tp_joint_t *j = &w->joint[ji];
    for (int s = 0; s < 2; s++) {
        tp_body_t *b = &w->body[s ? j->b : j->a];
        int p = j->prev[s], n = j->next[s];
        if (p >= 0) {
            w->joint[p >> 1].next[p & 1] = (int16_t)n;
        } else {
            b->jhead = (int16_t)n;
        }
        if (n >= 0) {
            w->joint[n >> 1].prev[n & 1] = (int16_t)p;
        }
    }
}

static int contact_find(const tp_world_t *w, int a, int b)
{
    int x = w->body[a].kind == TP_STATIC ? b : a, o = x == a ? b : a;
    for (int e = w->body[x].chead; e >= 0;) {
        const tp_contact_t *c = &w->contact[e >> 1];
        int s = e & 1;
        if ((s ? c->a : c->b) == o) {
            return e >> 1;
        }
        e = c->next[s];
    }
    return -1;
}
static int contact_new(tp_world_t *w, int a, int b)
{
    struct tp_in *in = w->in;
    if (in->ncfree <= 0) {
        return -1;
    }
    const tp_body_t *A = &w->body[a], *B = &w->body[b];
    // the order: a polygon before a circle, else the lower slot first
    if ((A->shape == TP_CIRCLE && B->shape == TP_POLY) || (A->shape == B->shape && a > b)) {
        int t = a;
        a = b;
        b = t;
    }
    int ci = in->cfree[--in->ncfree];
    tp_contact_t *c = &w->contact[ci];
    memset(c, 0, sizeof *c);
    c->a = (int16_t)a;
    c->b = (int16_t)b;
    // the materials of the pair, one time (a change of a material later is for the contacts that begin after it)
    const tp_body_t *A2 = &w->body[a], *B2 = &w->body[b];
    const tp_mat_t *ma = &w->mat[A2->mat], *mb = &w->mat[B2->mat];
    c->fr = sqrtf(ma->friction * mb->friction);
    c->e = fmx(ma->restitution, mb->restitution);
    c->rr = fmx(A2->shape == TP_CIRCLE ? ma->rolling * A2->radius : 0.0f,
                B2->shape == TP_CIRCLE ? mb->rolling * B2->radius : 0.0f);
    clist_add(w, ci);
    if (ci >= w->contact_hi) {
        w->contact_hi = ci + 1;
    }
    w->ncontact++;
    return ci;
}
static void contact_free(tp_world_t *w, int ci)
{
    clist_del(w, ci);
    tp_contact_t *c = &w->contact[ci];
    c->a = c->b = -1;
    c->np = 0;
    w->in->cfree[w->in->ncfree++] = (int16_t)ci;
    w->ncontact--;
    while (w->contact_hi > 0 && w->contact[w->contact_hi - 1].a < 0) {
        w->contact_hi--;
    }
}

static void wake(tp_world_t *w, int s)
{
    tp_body_t *b = &w->body[s];
    if (b->kind == TP_DYNAMIC && (b->flags & TP_ALIVE) && !(b->flags & TP_AWAKE)) {
        b->flags |= TP_AWAKE;
        b->sleep_t = 0.0f;
    }
}

static void add_event(tp_world_t *w, const tp_event_t *e)
{
    if (w->nev < w->ev_cap) {
        w->ev[w->nev++] = *e;
    }
}

static void joint_free(tp_world_t *w, int ji)
{
    struct tp_in *in = w->in;
    tp_joint_t *j = &w->joint[ji];
    jlist_del(w, ji);
    j->flags = 0;
    j->gid = j->gid + 1 > 0x7FFFFu ? 1 : j->gid + 1;
    in->jfree[in->njfree++] = (int16_t)ji;
    w->njoint--;
    while (w->joint_hi > 0 && !(w->joint[w->joint_hi - 1].flags & TPJ_ALIVE)) {
        w->joint_hi--;
    }
}
static void joint_break(tp_world_t *w, int ji, int why, float value)
{
    tp_joint_t *j = &w->joint[ji];
    tp_event_t e;
    memset(&e, 0, sizeof e);
    e.kind = TP_EV_JOINT;
    e.why = (uint8_t)why;
    e.id = mkid(ji, j->gid);
    e.a = (w->body[j->a].flags & TP_ALIVE) && !(w->body[j->a].flags & TP_GONE) ? mkid(j->a, w->body[j->a].gid)
                                                                               : TP_NONE;
    e.b = (w->body[j->b].flags & TP_ALIVE) && !(w->body[j->b].flags & TP_GONE) ? mkid(j->b, w->body[j->b].gid)
                                                                               : TP_NONE;
    e.user = j->user;
    e.p = xf_apply(w->body[j->a].xf, j->la);
    e.value = value;
    add_event(w, &e);
    wake(w, j->a);
    wake(w, j->b);
    joint_free(w, ji);
}

// the body goes: its contacts and joints go too (jev: an event for each joint), the bodies round it wake up
static void body_kill(tp_world_t *w, int s, bool jev)
{
    struct tp_in *in = w->in;
    tp_body_t *b = &w->body[s];
    b->flags |= TP_GONE;
    while (b->chead >= 0) {
        int ci = b->chead >> 1;
        tp_contact_t *c = &w->contact[ci];
        wake(w, c->a == s ? c->b : c->a);
        contact_free(w, ci);
    }
    while (b->jhead >= 0) {
        int ji = b->jhead >> 1;
        if (jev) {
            joint_break(w, ji, TP_WHY_BODY, 0.0f);
        } else {
            wake(w, w->joint[ji].a == s ? w->joint[ji].b : w->joint[ji].a);
            joint_free(w, ji);
        }
    }
    for (int i = 0; i < in->nsap; i++) {
        if (in->sap[i].b == s) {
            memmove(&in->sap[i], &in->sap[i + 1], sizeof(sap_t) * (size_t)(in->nsap - i - 1));
            in->nsap--;
            break;
        }
    }
    b->flags = 0;
    b->gid = b->gid + 1 > 0x7FFFFu ? 1 : b->gid + 1;
    in->bfree[in->nbfree++] = (int16_t)s;
    w->nbody--;
    while (w->body_hi > 0 && !(w->body[w->body_hi - 1].flags & TP_ALIVE)) {
        w->body_hi--;
    }
    in->dirty = true;
}
static void body_gone(tp_world_t *w, int s, int why)
{
    tp_body_t *b = &w->body[s];
    tp_event_t e;
    memset(&e, 0, sizeof e);
    e.kind = TP_EV_GONE;
    e.why = (uint8_t)why;
    e.mat = b->mat;
    e.snd = w->mat[b->mat].sound;
    e.id = mkid(s, b->gid);
    e.a = e.b = TP_NONE;
    e.user = b->user;
    e.p = b->xf.p;
    add_event(w, &e);
    body_kill(w, s, true);
}

// ---------------------------------------------------------------- the shapes
// the area, the centroid and the inertia per unit density (about the centroid) of a convex polygon
static void poly_mass(const vec *v, int n, float *area, vec *cen, float *iu)
{
    vec r = v[0], c = V(0, 0);
    float A = 0.0f, I = 0.0f;
    for (int i = 1; i + 1 < n; i++) {
        vec e1 = vsub(v[i], r), e2 = vsub(v[i + 1], r);
        float D = vcross(e1, e2), ta = 0.5f * D;
        A += ta;
        c = vmadd(c, ta * (1.0f / 3.0f), vadd(e1, e2));
        float ix = e1.x * e1.x + e2.x * e1.x + e2.x * e2.x, iy = e1.y * e1.y + e2.y * e1.y + e2.y * e2.y;
        I += (0.25f / 3.0f) * D * (ix + iy);
    }
    if (A > 0.0f) {
        c = vscale(1.0f / A, c);
    }
    *area = A;
    if (cen) {
        *cen = vadd(r, c);
    }
    if (iu) {
        *iu = I - A * vdot(c, c);
    }
}
// the distance of p out of the convex polygon (the largest of the edges; <= 0 = inside)
static float poly_out(const vec *v, int n, vec p)
{
    float m = -FLT_MAX;
    for (int i = 0; i < n; i++) {
        vec a = v[i], e = vsub(v[i + 1 < n ? i + 1 : 0], a);
        float l = vlen(e);
        if (l < 1e-9f) {
            continue;
        }
        float s = -vcross(e, vsub(p, a)) / l;
        if (s > m) {
            m = s;
        }
    }
    return m;
}
// remove the corners that are too near the next one or that are on a straight line; then at most TP_MAX_VERTS
static int poly_clean(vec *v, int n, float tol)
{
    bool again = true;
    while (again && n >= 3) {
        again = false;
        for (int i = 0; i < n && n >= 3; i++) {
            vec a = v[(i + n - 1) % n], b = v[i], c = v[(i + 1) % n];
            float ac = vlen(vsub(c, a));
            bool close_ = vlen2(vsub(b, c)) < tol * tol;
            bool line = ac > 1e-9f && fab(vcross(vsub(b, a), vsub(c, a))) / ac < 0.1f * tol;
            if (close_ || line || vcross(vsub(b, a), vsub(c, b)) <= 0.0f) {
                for (int k = i; k + 1 < n; k++) {
                    v[k] = v[k + 1];
                }
                n--;
                again = true;
                break;
            }
        }
    }
    while (n > TP_MAX_VERTS) {
        int best = 0;
        float bm = FLT_MAX;
        for (int i = 0; i < n; i++) {
            vec a = v[(i + n - 1) % n], b = v[i], c = v[(i + 1) % n];
            float ar = fab(vcross(vsub(b, a), vsub(c, a)));
            if (ar < bm) {
                bm = ar;
                best = i;
            }
        }
        for (int k = best; k + 1 < n; k++) {
            v[k] = v[k + 1];
        }
        n--;
    }
    return n;
}
// the convex hull of the points (counter-clockwise; 0 = not a good polygon)
static int hull(const vec *in, int n, vec *out, float weld)
{
    if (n < 3 || n > TP_MAX_VERTS) {
        return 0;
    }
    vec p[TP_MAX_VERTS];
    int m = 0;
    for (int i = 0; i < n; i++) {
        if (!(in[i].x == in[i].x) || !(in[i].y == in[i].y)) {
            return 0;
        }
        bool dup = false;
        for (int j = 0; j < m; j++) {
            dup = dup || vlen2(vsub(in[i], p[j])) < weld * weld;
        }
        if (!dup) {
            p[m++] = in[i];
        }
    }
    if (m < 3) {
        return 0;
    }
    for (int i = 1; i < m; i++) {
        for (int j = i; j > 0 && (p[j].x < p[j - 1].x || (p[j].x == p[j - 1].x && p[j].y < p[j - 1].y)); j--) {
            vec t = p[j];
            p[j] = p[j - 1];
            p[j - 1] = t;
        }
    }
    vec h[2 * TP_MAX_VERTS + 2];
    int k = 0;
    for (int i = 0; i < m; i++) {
        while (k >= 2 && vcross(vsub(h[k - 1], h[k - 2]), vsub(p[i], h[k - 2])) <= 0.0f) {
            k--;
        }
        h[k++] = p[i];
    }
    for (int i = m - 2, t = k + 1; i >= 0; i--) {
        while (k >= t && vcross(vsub(h[k - 1], h[k - 2]), vsub(p[i], h[k - 2])) <= 0.0f) {
            k--;
        }
        h[k++] = p[i];
    }
    k--;
    k = poly_clean(h, k, weld);
    if (k < 3) {
        return 0;
    }
    for (int i = 0; i < k; i++) {
        out[i] = h[i];
    }
    return k;
}

static void body_box(tp_world_t *w, tp_body_t *b)
{
    vec lo, hi;
    if (b->shape == TP_CIRCLE) {
        lo = V(b->xf.p.x - b->radius, b->xf.p.y - b->radius);
        hi = V(b->xf.p.x + b->radius, b->xf.p.y + b->radius);
    } else {
        lo = hi = xf_apply(b->xf, b->vl[0]);
        for (int i = 1; i < b->n; i++) {
            vec p = xf_apply(b->xf, b->vl[i]);
            lo.x = fmn(lo.x, p.x);
            lo.y = fmn(lo.y, p.y);
            hi.x = fmx(hi.x, p.x);
            hi.y = fmx(hi.y, p.y);
        }
    }
    float m = w->spec;
    if (b->kind != TP_STATIC && (b->flags & TP_AWAKE)) {
        // the move of this step (a fast body gets a long box: the speculative contact finds the wall)
        float dt = w->dt;
        m += fmn(fab(b->w) * dt, 2.0f) * b->extent;
        vec d = vscale(dt, b->v);
        if (d.x < 0) {
            lo.x += d.x;
        } else {
            hi.x += d.x;
        }
        if (d.y < 0) {
            lo.y += d.y;
        } else {
            hi.y += d.y;
        }
    }
    b->box.lo = V(lo.x - m, lo.y - m);
    b->box.hi = V(hi.x + m, hi.y + m);
}

// a new body: pv = the corners round the centre of mass (convex, counter-clockwise), pos = the centre of mass
static int body_make(tp_world_t *w, const tp_body_def_t *d, int shape, const vec *pv, int n, float radius, vec pos,
                     rot_t q)
{
    struct tp_in *in = w->in;
    int mat = d->mat < 0 || d->mat >= TP_MAT_MAX ? 0 : d->mat;
    const tp_mat_t *m = &w->mat[mat];
    float area, iu;
    if (d->kind < TP_STATIC || d->kind > TP_DYNAMIC) {
        return -1;
    }
    if (shape == TP_CIRCLE) {
        if (!(radius > 0.0f)) {
            return -1;
        }
        area = PI_F * radius * radius;
        iu = 0.5f * area * radius * radius;
    } else {
        if (n < 3 || n > TP_MAX_VERTS) {
            return -1;
        }
        poly_mass(pv, n, &area, NULL, &iu);
    }
    if (!(area > 1e-7f) || !(pos.x == pos.x) || !(pos.y == pos.y)) {
        return -1;
    }
    if (in->nbfree <= 0) {
        return -1;
    }
    int s = in->bfree[--in->nbfree];
    if (s >= w->body_hi) {
        w->body_hi = s + 1;
    }
    w->nbody++;
    tp_body_t *b = &w->body[s];
    uint32_t gid = b->gid ? b->gid : 1;
    memset(b, 0, sizeof *b);
    b->gid = gid;
    b->kind = (uint8_t)d->kind;
    b->shape = (uint8_t)shape;
    b->mat = (uint8_t)mat;
    b->xf.p = pos;
    b->xf.q = q;
    if (d->kind != TP_STATIC) {
        b->v = d->vel;
        b->w = d->fixed_rot ? 0.0f : d->spin;
    }
    b->area = area;
    b->mass = m->density * area;
    b->inertia = m->density * iu;
    if (d->kind == TP_DYNAMIC && b->mass > 0.0f) {
        b->im = 1.0f / b->mass;
        b->ii = d->fixed_rot || b->inertia <= 0.0f ? 0.0f : 1.0f / b->inertia;
    }
    b->radius = shape == TP_CIRCLE ? radius : 0.0f;
    if (shape == TP_POLY) {
        b->n = (uint8_t)n;
        for (int i = 0; i < n; i++) {
            b->vl[i] = pv[i];
            b->nl[i] = vnorm(vrperp(vsub(pv[i + 1 < n ? i + 1 : 0], pv[i])));
            b->extent = fmx(b->extent, vlen(pv[i]));
        }
    } else {
        b->extent = radius;
    }
    b->toughness = d->toughness < 0.0f ? m->toughness : d->toughness;
    if (d->life > 0.0f) {
        b->life = b->life0 = d->life;
    }
    b->ldamp = d->ldamp;
    b->adamp = d->adamp;
    b->gscale = d->gscale;
    b->group = d->group;
    b->cat = d->cat;
    b->mask = d->mask;
    b->spill = (int16_t)clampi(d->spill, 0, 30000);
    b->user = d->user;
    b->parent = TP_NONE;
    b->chead = b->jhead = -1;
    b->isl = b->inext = -1;
    b->flags = TP_ALIVE | (d->fixed_rot ? TP_FIXROT : 0) | (d->no_sleep ? TP_NOSLEEP : 0);
    if (d->kind == TP_DYNAMIC && !d->sleeping) {
        b->flags |= TP_AWAKE;
    }
    if (d->kind == TP_KINEMATIC && (b->v.x != 0.0f || b->v.y != 0.0f || b->w != 0.0f)) {
        b->flags |= TP_AWAKE;
    }
    body_box(w, b);
    sap_t *e = &in->sap[in->nsap++];
    memset(e, 0, sizeof *e);
    e->b = (int16_t)s;
    e->x0 = b->box.lo.x;
    e->x1 = b->box.hi.x;
    e->y0 = b->box.lo.y;
    e->y1 = b->box.hi.y;
    return s;
}

tp_body_def_t tp_body_def(int kind, float x, float y, int mat)
{
    tp_body_def_t d;
    memset(&d, 0, sizeof d);
    d.kind = kind;
    d.pos = V(x, y);
    d.mat = mat;
    d.toughness = -1.0f;
    d.gscale = 1.0f;
    d.cat = 1;
    d.mask = 0xFFFF;
    return d;
}

tp_id_t tp_add_box(tp_world_t *w, const tp_body_def_t *d, float hw, float hh)
{
    if (!(hw > 0.0f) || !(hh > 0.0f)) {
        return TP_NONE;
    }
    vec p[4] = {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
    int s = body_make(w, d, TP_POLY, p, 4, 0.0f, d->pos, R(d->angle));
    if (s < 0) {
        return TP_NONE;
    }
    w->body[s].flags |= TP_BOXF;
    return mkid(s, w->body[s].gid);
}

tp_id_t tp_add_circle(tp_world_t *w, const tp_body_def_t *d, float r)
{
    int s = body_make(w, d, TP_CIRCLE, NULL, 0, r, d->pos, R(d->angle));
    return s < 0 ? TP_NONE : mkid(s, w->body[s].gid);
}

tp_id_t tp_add_poly(tp_world_t *w, const tp_body_def_t *d, const tp_vec_t *pts, int n)
{
    vec h[TP_MAX_VERTS];
    int m = hull(pts, n, h, w->slop);
    if (m < 3) {
        return TP_NONE;
    }
    float area;
    vec c;
    poly_mass(h, m, &area, &c, NULL);
    if (!(area > 1e-7f)) {
        return TP_NONE;
    }
    for (int i = 0; i < m; i++) {
        h[i] = vsub(h[i], c);
    }
    rot_t q = R(d->angle);
    int s = body_make(w, d, TP_POLY, h, m, 0.0f, vadd(d->pos, rot(q, c)), q);
    return s < 0 ? TP_NONE : mkid(s, w->body[s].gid);
}

tp_body_t *tp_body(tp_world_t *w, tp_id_t id)
{
    if (!w || id < 0) {
        return NULL;
    }
    int s = id & SLOT_MASK;
    if (s >= w->body_hi) {
        return NULL;
    }
    tp_body_t *b = &w->body[s];
    if (!(b->flags & TP_ALIVE) || (b->flags & TP_GONE) || mkid(s, b->gid) != id) {
        return NULL;
    }
    return b;
}
tp_id_t tp_body_id(const tp_world_t *w, int slot)
{
    if (slot < 0 || slot >= w->body_hi || !(w->body[slot].flags & TP_ALIVE)) {
        return TP_NONE;
    }
    return mkid(slot, w->body[slot].gid);
}
void tp_remove(tp_world_t *w, tp_id_t id)
{
    if (tp_body(w, id)) {
        body_kill(w, id & SLOT_MASK, false);
    }
}
void tp_wake(tp_world_t *w, tp_id_t id)
{
    if (tp_body(w, id)) {
        wake(w, id & SLOT_MASK);
    }
}
static void kin_flag(tp_body_t *b)
{
    if (b->v.x != 0.0f || b->v.y != 0.0f || b->w != 0.0f) {
        b->flags |= TP_AWAKE;
    } else {
        b->flags &= (uint8_t)~TP_AWAKE;
    }
}
void tp_set_vel(tp_world_t *w, tp_id_t id, tp_vec_t v, float spin)
{
    tp_body_t *b = tp_body(w, id);
    if (!b || b->kind == TP_STATIC) {
        return;
    }
    b->v = v;
    b->w = (b->flags & TP_FIXROT) ? 0.0f : spin;
    if (b->kind == TP_KINEMATIC) {
        kin_flag(b);
    } else {
        wake(w, id & SLOT_MASK);
    }
}
void tp_impulse(tp_world_t *w, tp_id_t id, tp_vec_t imp, tp_vec_t point)
{
    tp_body_t *b = tp_body(w, id);
    if (!b || b->kind != TP_DYNAMIC) {
        return;
    }
    b->v = vmadd(b->v, b->im, imp);
    b->w += b->ii * vcross(vsub(point, b->xf.p), imp);
    wake(w, id & SLOT_MASK);
}
void tp_force(tp_world_t *w, tp_id_t id, tp_vec_t f, tp_vec_t point)
{
    tp_body_t *b = tp_body(w, id);
    if (!b || b->kind != TP_DYNAMIC) {
        return;
    }
    b->force = vadd(b->force, f);
    b->torque += vcross(vsub(point, b->xf.p), f);
    wake(w, id & SLOT_MASK);
}
void tp_set_place(tp_world_t *w, tp_id_t id, tp_vec_t pos, float angle)
{
    tp_body_t *b = tp_body(w, id);
    if (!b) {
        return;
    }
    int s = id & SLOT_MASK;
    b->xf.p = pos;
    b->xf.q = R(angle);
    body_box(w, b);
    wake(w, s);
    for (int e = b->chead; e >= 0;) {
        tp_contact_t *c = &w->contact[e >> 1];
        int sd = e & 1;
        wake(w, sd ? c->a : c->b);
        e = c->next[sd];
    }
}
void tp_move_to(tp_world_t *w, tp_id_t id, tp_vec_t pos, float angle)
{
    tp_body_t *b = tp_body(w, id);
    if (!b || b->kind != TP_KINEMATIC) {
        return;
    }
    float idt = 1.0f / w->dt;
    b->v = vscale(idt, vsub(pos, b->xf.p));
    b->w = (b->flags & TP_FIXROT) ? 0.0f : unwind(angle - atan2f(b->xf.q.s, b->xf.q.c)) * idt;
    kin_flag(b);
}
float tp_angle(const tp_body_t *b)
{
    return atan2f(b->xf.q.s, b->xf.q.c);
}
int tp_verts(const tp_body_t *b, tp_vec_t *out)
{
    if (b->shape != TP_POLY) {
        return 0;
    }
    for (int i = 0; i < b->n; i++) {
        out[i] = xf_apply(b->xf, b->vl[i]);
    }
    return b->n;
}
tp_vec_t tp_world_point(const tp_body_t *b, tp_vec_t local)
{
    return xf_apply(b->xf, local);
}
tp_vec_t tp_local_point(const tp_body_t *b, tp_vec_t world)
{
    return xf_inv(b->xf, world);
}
tp_vec_t tp_point_vel(const tp_body_t *b, tp_vec_t world)
{
    return vadd(b->v, vcrosssv(b->w, vsub(world, b->xf.p)));
}

// ---------------------------------------------------------------- the joints
static tp_joint_def_t jdef(int kind, tp_id_t a, tp_id_t b, vec pa, vec pb)
{
    tp_joint_def_t d;
    memset(&d, 0, sizeof d);
    d.kind = kind;
    d.a = a;
    d.b = b;
    d.pa = pa;
    d.pb = pb;
    d.zeta = 1.0f;
    return d;
}
tp_joint_def_t tp_revolute_def(tp_id_t a, tp_id_t b, tp_vec_t anchor)
{
    return jdef(TP_JOINT_REVOLUTE, a, b, anchor, anchor);
}
tp_joint_def_t tp_weld_def(tp_id_t a, tp_id_t b, tp_vec_t anchor)
{
    return jdef(TP_JOINT_WELD, a, b, anchor, anchor);
}
tp_joint_def_t tp_distance_def(tp_id_t a, tp_id_t b, tp_vec_t pa, tp_vec_t pb)
{
    return jdef(TP_JOINT_DISTANCE, a, b, pa, pb);
}
tp_joint_def_t tp_rope_def(tp_id_t a, tp_id_t b, tp_vec_t pa, tp_vec_t pb, float len)
{
    tp_joint_def_t d = jdef(TP_JOINT_DISTANCE, a, b, pa, pb);
    d.minlen = 0.0f;
    d.maxlen = len;
    return d;
}

tp_id_t tp_add_joint(tp_world_t *w, const tp_joint_def_t *d)
{
    struct tp_in *in = w->in;
    tp_body_t *A = tp_body(w, d->a), *B = tp_body(w, d->b);
    if (!A || !B || A == B || in->njfree <= 0) {
        return TP_NONE;
    }
    if (d->kind < TP_JOINT_REVOLUTE || d->kind > TP_JOINT_WELD) {
        return TP_NONE;
    }
    if (A->kind != TP_DYNAMIC && B->kind != TP_DYNAMIC) {
        return TP_NONE;
    }
    int a = d->a & SLOT_MASK, b = d->b & SLOT_MASK;
    int ji = in->jfree[--in->njfree];
    tp_joint_t *j = &w->joint[ji];
    uint32_t gid = j->gid ? j->gid : 1;
    memset(j, 0, sizeof *j);
    j->gid = gid;
    j->kind = (uint8_t)d->kind;
    j->a = (int16_t)a;
    j->b = (int16_t)b;
    j->flags = TPJ_ALIVE | (d->collide ? TPJ_COLLIDE : 0);
    j->user = d->user;
    j->la = xf_inv(A->xf, d->pa);
    j->lb = xf_inv(B->xf, d->kind == TP_JOINT_DISTANCE ? d->pb : d->pa);
    j->ref = relang(B->xf.q, A->xf.q);
    if (d->kind == TP_JOINT_DISTANCE) {
        float L = vlen(vsub(d->pb, d->pa));
        j->len = L;
        if (d->maxlen > d->minlen) {
            j->minlen = fmx(0.0f, d->minlen);
            j->maxlen = d->maxlen;
        } else {
            j->minlen = j->maxlen = L;
        }
        if (d->hz > 0.0f) {
            j->flags |= TPJ_SPRING;
        }
    }
    if (d->kind == TP_JOINT_REVOLUTE) {
        if (d->limit) {
            j->flags |= TPJ_LIMIT;
            j->lower = fmn(d->lower, d->upper);
            j->upper = fmx(d->lower, d->upper);
        }
        if (d->motor) {
            j->flags |= TPJ_MOTOR;
        }
        j->mspeed = d->mspeed;
        j->mtorque = d->mtorque;
        j->friction = fmx(0.0f, d->friction);
    }
    j->hz = d->hz;
    j->zeta = d->zeta;
    j->break_force = d->break_force;
    j->break_torque = d->break_torque;
    jlist_add(w, ji);
    if (ji >= w->joint_hi) {
        w->joint_hi = ji + 1;
    }
    w->njoint++;
    wake(w, a);
    wake(w, b);
    if (!d->collide) {
        int ci = contact_find(w, a, b);
        if (ci >= 0) {
            contact_free(w, ci);
        }
    }
    return mkid(ji, j->gid);
}
tp_joint_t *tp_joint(tp_world_t *w, tp_id_t id)
{
    if (!w || id < 0) {
        return NULL;
    }
    int s = id & SLOT_MASK;
    if (s >= w->joint_hi) {
        return NULL;
    }
    tp_joint_t *j = &w->joint[s];
    if (!(j->flags & TPJ_ALIVE) || mkid(s, j->gid) != id) {
        return NULL;
    }
    return j;
}
void tp_joint_remove(tp_world_t *w, tp_id_t id)
{
    tp_joint_t *j = tp_joint(w, id);
    if (!j) {
        return;
    }
    wake(w, j->a);
    wake(w, j->b);
    joint_free(w, id & SLOT_MASK);
}
void tp_motor(tp_world_t *w, tp_id_t id, float speed, float torque)
{
    tp_joint_t *j = tp_joint(w, id);
    if (!j || j->kind != TP_JOINT_REVOLUTE) {
        return;
    }
    j->flags |= TPJ_MOTOR;
    j->mspeed = speed;
    j->mtorque = torque;
    wake(w, j->a);
    wake(w, j->b);
}
void tp_joint_anchors(const tp_world_t *w, const tp_joint_t *j, tp_vec_t *pa, tp_vec_t *pb)
{
    *pa = xf_apply(w->body[j->a].xf, j->la);
    *pb = xf_apply(w->body[j->b].xf, j->lb);
}

// ---------------------------------------------------------------- the shape tests (the narrow phase)
typedef struct {
    vec ra;
    float sep;
    uint16_t id;
} mpt_t;
typedef struct {
    vec n;
    int np;
    mpt_t p[2];
} mf_t;  // n from A to B; ra from the centre of A (world directions)
#define MKID(a, b) ((uint16_t)((((a) & 0x7F) << 8) | ((b) & 0xFF)))

static void col_circles(const tp_body_t *A, const tp_body_t *B, float maxd, mf_t *m)
{
    vec d = vsub(B->xf.p, A->xf.p);
    float l = vlen(d), sep = l - A->radius - B->radius;
    m->np = 0;
    if (sep > maxd) {
        return;
    }
    vec n = l > 1e-9f ? vscale(1.0f / l, d) : V(0, 1);
    m->n = n;
    m->np = 1;
    m->p[0].ra = vlerp(vscale(A->radius, n), vmadd(d, -B->radius, n), 0.5f);
    m->p[0].sep = sep;
    m->p[0].id = 0;
}

static void col_poly_circle(const tp_world_t *w, const tp_body_t *A, const tp_body_t *B, float maxd, mf_t *m)
{
    vec c = xf_inv(A->xf, B->xf.p);
    float rb = B->radius, s = -FLT_MAX;
    int ni = 0, n = A->n;
    m->np = 0;
    // a fast circle (it moves more than half its radius in this step, relative to A): the normal of the face that
    // it hits in the step (the nearest face now can be a wrong face: then the circle goes into a small polygon)
    vec mv = irot(A->xf.q, vscale(w->dt, vsub(tp_point_vel(B, B->xf.p), tp_point_vel(A, B->xf.p))));
    if (vlen2(mv) > 0.25f * rb * rb) {
        float tin = 0.0f, tout = 1.0f, sin_ = -FLT_MAX;
        int ein = -1;
        bool miss = false;
        for (int i = 0; i < n && !miss; i++) {
            float s0 = vdot(A->nl[i], vsub(c, A->vl[i])) - rb, dn = vdot(A->nl[i], mv);
            if (dn == 0.0f) {
                miss = s0 > 0.0f;
                continue;
            }
            float tt = -s0 / dn;
            if (dn < 0.0f) {
                if (tt > tin) {
                    tin = tt;
                    ein = i;
                    sin_ = s0;
                }
            } else if (tt < tout) {
                tout = tt;
            }
            miss = tin > tout;
        }
        if (!miss && ein >= 0 && tin > 0.0f && tin <= 1.0f && sin_ <= maxd) {
            vec nl = A->nl[ein];
            vec ca = vmadd(c, -vdot(vsub(c, A->vl[ein]), nl), nl), cb = vmadd(c, -rb, nl);
            m->n = rot(A->xf.q, nl);
            m->np = 1;
            m->p[0].ra = rot(A->xf.q, vlerp(ca, cb, 0.5f));
            m->p[0].sep = sin_;
            m->p[0].id = (uint16_t)(0x4000 | ein);
            return;
        }
    }
    for (int i = 0; i < n; i++) {
        float si = vdot(A->nl[i], vsub(c, A->vl[i]));
        if (si > s) {
            s = si;
            ni = i;
        }
    }
    if (s - rb > maxd) {
        return;
    }
    vec v1 = A->vl[ni], v2 = A->vl[ni + 1 < n ? ni + 1 : 0];
    float u1 = vdot(vsub(c, v1), vsub(v2, v1)), u2 = vdot(vsub(c, v2), vsub(v1, v2));
    vec nl, ca, cb;
    float sep;
    if ((u1 < 0.0f || u2 < 0.0f) && s > FLT_EPSILON) {
        vec v = u1 < 0.0f ? v1 : v2, d = vsub(c, v);
        float l = vlen(d);
        if (l < 1e-9f) {
            return;
        }
        nl = vscale(1.0f / l, d);
        sep = l - rb;
        if (sep > maxd) {
            return;
        }
        ca = v;
        cb = vmadd(c, -rb, nl);
    } else {
        nl = A->nl[ni];
        sep = s - rb;
        ca = vmadd(c, -vdot(vsub(c, v1), nl), nl);
        cb = vmadd(c, -rb, nl);
    }
    m->n = rot(A->xf.q, nl);
    m->np = 1;
    m->p[0].ra = rot(A->xf.q, vlerp(ca, cb, 0.5f));
    m->p[0].sep = sep;
    m->p[0].id = 0;
}

// the largest separation of poly 2 from poly 1 along the edge normals of poly 1
static float max_sep(const vec *n1, const vec *v1, int c1, const vec *v2, int c2, int *edge)
{
    int best = 0;
    float ms = -FLT_MAX;
    for (int i = 0; i < c1; i++) {
        vec n = n1[i], v = v1[i];
        float si = FLT_MAX;
        for (int j = 0; j < c2; j++) {
            float s = n.x * (v2[j].x - v.x) + n.y * (v2[j].y - v.y);
            if (s < si) {
                si = s;
            }
        }
        if (si > ms) {
            ms = si;
            best = i;
        }
    }
    *edge = best;
    return ms;
}

typedef struct {
    vec c1, c2;
    float f1, f2, d2;
} segd_t;
// the nearest points of two segments (Box2D b2SegmentDistance)
static segd_t seg_dist(vec p1, vec q1, vec p2, vec q2)
{
    segd_t r;
    vec d1 = vsub(q1, p1), d2 = vsub(q2, p2), rr = vsub(p1, p2);
    float dd1 = vdot(d1, d1), dd2 = vdot(d2, d2), rd1 = vdot(rr, d1), rd2 = vdot(rr, d2);
    const float eps = FLT_EPSILON * FLT_EPSILON;
    r.f1 = r.f2 = 0.0f;
    if (dd1 < eps || dd2 < eps) {
        if (dd1 >= eps) {
            r.f1 = fcl(-rd1 / dd1, 0.0f, 1.0f);
        } else if (dd2 >= eps) {
            r.f2 = fcl(rd2 / dd2, 0.0f, 1.0f);
        }
    } else {
        float d12 = vdot(d1, d2), den = dd1 * dd2 - d12 * d12, f1 = 0.0f;
        if (den != 0.0f) {
            f1 = fcl((d12 * rd2 - rd1 * dd2) / den, 0.0f, 1.0f);
        }
        float f2 = (d12 * f1 + rd2) / dd2;
        if (f2 < 0.0f) {
            f2 = 0.0f;
            f1 = fcl(-rd1 / dd1, 0.0f, 1.0f);
        } else if (f2 > 1.0f) {
            f2 = 1.0f;
            f1 = fcl((d12 - rd1) / dd1, 0.0f, 1.0f);
        }
        r.f1 = f1;
        r.f2 = f2;
    }
    r.c1 = vmadd(p1, r.f1, d1);
    r.c2 = vmadd(p2, r.f2, d2);
    r.d2 = vlen2(vsub(r.c1, r.c2));
    return r;
}

static void col_polys(const tp_world_t *w, const tp_body_t *A, const tp_body_t *B, float maxd, mf_t *m)
{
    xf_t xf = xf_invmul(A->xf, B->xf);
    vec vb[TP_MAX_VERTS], nb[TP_MAX_VERTS];
    int na = A->n, nn = B->n, ea = 0, eb = 0;
    m->np = 0;
    for (int i = 0; i < nn; i++) {
        vb[i] = xf_apply(xf, B->vl[i]);
        nb[i] = rot(xf.q, B->nl[i]);
    }
    float sa, sb;
    if (A->flags & B->flags & TP_BOXF) {
        // two boxes: the separation along each edge normal from the offset of the centres and the half sizes (the
        // same values as max_sep, with no 4 x 4 products); the edges in the order of max_sep (the first largest)
        float hwa = A->vl[2].x, hha = A->vl[2].y, hwb = B->vl[2].x, hhb = B->vl[2].y;
        vec c = xf.p, u = V(xf.q.c, xf.q.s), v = V(-xf.q.s, xf.q.c);
        float rbx = hwb * fab(u.x) + hhb * fab(v.x), rby = hwb * fab(u.y) + hhb * fab(v.y);
        float s4[4] = {-c.y - rby - hha, c.x - rbx - hwa, c.y - rby - hha, -c.x - rbx - hwa};
        sa = s4[0];
        for (int i = 1; i < 4; i++) {
            if (s4[i] > sa) {
                sa = s4[i];
                ea = i;
            }
        }
        if (sa > maxd) {
            return;
        }
        float rau = hwa * fab(u.x) + hha * fab(u.y), rav = hwa * fab(v.x) + hha * fab(v.y), cu = vdot(c, u),
              cv = vdot(c, v);
        float t4[4] = {cv - hhb - rav, -cu - hwb - rau, -cv - hhb - rav, cu - hwb - rau};
        sb = t4[0];
        for (int i = 1; i < 4; i++) {
            if (t4[i] > sb) {
                sb = t4[i];
                eb = i;
            }
        }
        if (sb > maxd) {
            return;
        }
    } else {
        sa = max_sep(A->nl, A->vl, na, vb, nn, &ea);
        if (sa > maxd) {
            return;
        }
        sb = max_sep(nb, vb, nn, A->vl, na, &eb);
        if (sb > maxd) {
            return;
        }
    }
    bool flip;
    if (sb > sa + 0.1f * w->slop) {
        float md = FLT_MAX;
        flip = true;
        ea = 0;
        for (int i = 0; i < na; i++) {
            float d = vdot(nb[eb], A->nl[i]);
            if (d < md) {
                md = d;
                ea = i;
            }
        }
    } else {
        float md = FLT_MAX;
        flip = false;
        eb = 0;
        for (int i = 0; i < nn; i++) {
            float d = vdot(A->nl[ea], nb[i]);
            if (d < md) {
                md = d;
                eb = i;
            }
        }
    }
    // the reference polygon (1) and the incident polygon (2), all in the frame of A
    const vec *v1 = flip ? vb : A->vl, *n1 = flip ? nb : A->nl, *v2 = flip ? A->vl : vb;
    int c1 = flip ? nn : na, c2 = flip ? na : nn;
    int i11 = flip ? eb : ea, i12 = i11 + 1 < c1 ? i11 + 1 : 0;
    int i21 = flip ? ea : eb, i22 = i21 + 1 < c2 ? i21 + 1 : 0;
    vec v11 = v1[i11], v12 = v1[i12], v21 = v2[i21], v22 = v2[i22];
    if ((flip ? sb : sa) > 0.1f * w->slop) {
        // apart: a corner to a corner gives one point with the true normal
        segd_t r = seg_dist(v11, v12, v21, v22);
        if ((r.f1 == 0.0f || r.f1 == 1.0f) && (r.f2 == 0.0f || r.f2 == 1.0f)) {
            float d = sqrtf(r.d2);
            if (d > maxd || d < 1e-9f) {
                return;
            }
            vec nrm = vscale(1.0f / d, vsub(r.c2, r.c1));
            int iv1 = r.f1 == 0.0f ? i11 : i12, iv2 = r.f2 == 0.0f ? i21 : i22;
            m->n = rot(A->xf.q, flip ? vneg(nrm) : nrm);
            m->np = 1;
            m->p[0].ra = rot(A->xf.q, vlerp(r.c1, r.c2, 0.5f));
            m->p[0].sep = d;
            m->p[0].id = (uint16_t)(MKID(iv1, iv2) | (flip ? 0x8000 : 0));
            return;
        }
    }
    // the incident edge, cut to the side planes of the reference edge
    vec nrm = n1[i11], tan = vcrosssv(1.0f, nrm);
    float upper1 = vdot(vsub(v12, v11), tan);
    float upper2 = vdot(vsub(v21, v11), tan), lower2 = vdot(vsub(v22, v11), tan), dd = upper2 - lower2;
    vec vlo = v22, vup = v21;
    if (lower2 < 0.0f && dd > FLT_EPSILON) {
        vlo = vlerp(v22, v21, (0.0f - lower2) / dd);
    }
    if (upper2 > upper1 && dd > FLT_EPSILON) {
        vup = vlerp(v22, v21, (upper1 - lower2) / dd);
    }
    float slo = vdot(vsub(vlo, v11), nrm), sup = vdot(vsub(vup, v11), nrm);
    vlo = vmadd(vlo, -0.5f * slo, nrm);
    vup = vmadd(vup, -0.5f * sup, nrm);
    m->n = rot(A->xf.q, flip ? vneg(nrm) : nrm);
    int k = 0;
    if (!flip) {
        if (slo <= maxd) {
            m->p[k].ra = rot(A->xf.q, vlo);
            m->p[k].sep = slo;
            m->p[k++].id = MKID(i11, i22);
        }
        if (sup <= maxd) {
            m->p[k].ra = rot(A->xf.q, vup);
            m->p[k].sep = sup;
            m->p[k++].id = MKID(i12, i21);
        }
    } else {
        if (sup <= maxd) {
            m->p[k].ra = rot(A->xf.q, vup);
            m->p[k].sep = sup;
            m->p[k++].id = (uint16_t)(MKID(i21, i12) | 0x8000);
        }
        if (slo <= maxd) {
            m->p[k].ra = rot(A->xf.q, vlo);
            m->p[k].sep = slo;
            m->p[k++].id = (uint16_t)(MKID(i22, i11) | 0x8000);
        }
    }
    m->np = k;
}

static void contact_update(tp_world_t *w, tp_contact_t *c)
{
    const tp_body_t *A = &w->body[c->a], *B = &w->body[c->b];
    // the speculative distance: the base, and the move of the two bodies toward each other in this step
    float maxd = w->spec + w->dt * (vlen(vsub(B->v, A->v)) + fab(A->w) * A->extent + fab(B->w) * B->extent);
    if (maxd > 2.5f) {
        maxd = 2.5f;
    }
    mf_t m;
    if (A->shape == TP_POLY) {
        if (B->shape == TP_POLY) {
            col_polys(w, A, B, maxd, &m);
        } else {
            col_poly_circle(w, A, B, maxd, &m);
        }
    } else {
        col_circles(A, B, maxd, &m);
    }
    w->stat.narrow++;
    tp_cpoint_t old[2] = {c->pt[0], c->pt[1]};
    int onp = c->np;
    vec dab = vsub(A->xf.p, B->xf.p);
    for (int i = 0; i < m.np; i++) {
        tp_cpoint_t *p = &c->pt[i];
        p->ra = m.p[i].ra;
        p->rb = vadd(m.p[i].ra, dab);
        p->sep = m.p[i].sep;
        p->id = m.p[i].id;
        p->ni = p->ti = p->hit = 0.0f;
        for (int k = 0; k < onp; k++) {
            if (old[k].id == p->id) {
                p->ni = old[k].ni;
                p->ti = old[k].ti;
                break;
            }
        }
    }
    if (m.np == 0) {
        c->rolli = 0.0f;
    } else {
        c->n = m.n;
    }
    c->np = (uint8_t)m.np;
}

// ---------------------------------------------------------------- a step: the broad phase
static bool joint_link(const tp_world_t *w, int a, int b)
{
    for (int e = w->body[a].jhead; e >= 0;) {
        const tp_joint_t *j = &w->joint[e >> 1];
        int s = e & 1;
        if ((s ? j->a : j->b) == b && !(j->flags & TPJ_COLLIDE)) {
            return true;
        }
        e = j->next[s];
    }
    return false;
}

static void broad_phase(tp_world_t *w)
{
    struct tp_in *in = w->in;
    sap_t *sp = in->sap;
    int n = in->nsap, pairs = 0;
    uint32_t now = w->stepno;
    for (int i = 0; i < n; i++) {
        sap_t *e = &sp[i];
        tp_body_t *b = &w->body[e->b];
        bool mv = b->kind != TP_STATIC && (b->flags & TP_AWAKE);
        if (mv) {
            body_box(w, b);
        }
        e->x0 = b->box.lo.x;
        e->x1 = b->box.hi.x;
        e->y0 = b->box.lo.y;
        e->y1 = b->box.hi.y;
        e->act = mv;
        e->dyn = b->kind == TP_DYNAMIC;
        e->group = b->group;
        e->cat = b->cat;
        e->mask = b->mask;
    }
    // the sort on the lower x (the order of the last step is nearly right: an insertion sort)
    for (int i = 1; i < n; i++) {
        sap_t t = sp[i];
        int j = i - 1;
        while (j >= 0 && sp[j].x0 > t.x0) {
            sp[j + 1] = sp[j];
            j--;
        }
        sp[j + 1] = t;
    }
    // the sweep
    for (int i = 0; i < n; i++) {
        const sap_t *e = &sp[i];
        float x1 = e->x1;
        for (int j = i + 1; j < n && sp[j].x0 <= x1; j++) {
            const sap_t *f = &sp[j];
            if (f->y0 > e->y1 || f->y1 < e->y0) {
                continue;
            }
            if (!(e->act | f->act) || !(e->dyn | f->dyn)) {
                continue;
            }
            if (e->group && e->group == f->group) {
                continue;
            }
            if (!(e->cat & f->mask) || !(f->cat & e->mask)) {
                continue;
            }
            int a = e->b, b = f->b;
            if (w->body[a].jhead >= 0 && w->body[b].jhead >= 0 && joint_link(w, a, b)) {
                continue;
            }
            pairs++;
            int ci = contact_find(w, a, b);
            if (ci < 0) {
                ci = contact_new(w, a, b);
            }
            if (ci >= 0) {
                w->contact[ci].stamp = now;
            }
        }
    }
    w->stat.pairs = pairs;
}

static void narrow_phase(tp_world_t *w)
{
    uint32_t now = w->stepno;
    for (int ci = 0; ci < w->contact_hi; ci++) {
        tp_contact_t *c = &w->contact[ci];
        if (c->a < 0) {
            continue;
        }
        const tp_body_t *A = &w->body[c->a], *B = &w->body[c->b];
        bool aa = A->kind != TP_STATIC && (A->flags & TP_AWAKE), ba = B->kind != TP_STATIC && (B->flags & TP_AWAKE);
        if (!aa && !ba) {
            continue;  // both still: the contact stays (a sleeping stack)
        }
        if (c->stamp != now) {  // the boxes do not touch now
            contact_free(w, ci);
            continue;
        }
        contact_update(w, c);
    }
}

// ---------------------------------------------------------------- a step: the islands (they sleep as one)
static void islands(tp_world_t *w)
{
    struct tp_in *in = w->in;
    uint32_t now = w->stepno;
    // a kinematic body that moves wakes the bodies that touch it, and they cannot sleep
    for (int s = 0; s < w->body_hi; s++) {
        tp_body_t *k = &w->body[s];
        if (!(k->flags & TP_ALIVE) || k->kind != TP_KINEMATIC || !(k->flags & TP_AWAKE)) {
            continue;
        }
        for (int e = k->chead; e >= 0;) {
            tp_contact_t *c = &w->contact[e >> 1];
            int sd = e & 1, o = sd ? c->a : c->b;
            e = c->next[sd];
            if (!c->np) {
                continue;
            }
            wake(w, o);
            w->body[o].mark = now;
        }
        for (int e = k->jhead; e >= 0;) {
            tp_joint_t *j = &w->joint[e >> 1];
            int sd = e & 1, o = sd ? j->a : j->b;
            e = j->next[sd];
            wake(w, o);
            w->body[o].mark = now;
        }
    }
    for (int s = 0; s < w->body_hi; s++) {
        w->body[s].isl = -1;
    }
    int nisl = 0;
    for (int s = 0; s < w->body_hi; s++) {
        tp_body_t *b = &w->body[s];
        if (!(b->flags & TP_ALIVE) || b->kind != TP_DYNAMIC || !(b->flags & TP_AWAKE) || b->isl >= 0) {
            continue;
        }
        int k = nisl++, head = -1, sp = 0;
        in->stack[sp++] = (int16_t)s;
        b->isl = (int16_t)k;
        while (sp > 0) {
            int x = in->stack[--sp];
            tp_body_t *X = &w->body[x];
            X->inext = (int16_t)head;
            head = x;
            X->flags |= TP_AWAKE;  // (a sleeping body that an awake body touches)
            for (int e = X->chead; e >= 0;) {
                tp_contact_t *c = &w->contact[e >> 1];
                int sd = e & 1, o = sd ? c->a : c->b;
                e = c->next[sd];
                if (!c->np) {
                    continue;
                }
                tp_body_t *O = &w->body[o];
                if (O->kind != TP_DYNAMIC || O->isl >= 0) {
                    continue;
                }
                O->isl = (int16_t)k;
                in->stack[sp++] = (int16_t)o;
            }
            for (int e = X->jhead; e >= 0;) {
                tp_joint_t *j = &w->joint[e >> 1];
                int sd = e & 1, o = sd ? j->a : j->b;
                e = j->next[sd];
                tp_body_t *O = &w->body[o];
                if (O->kind != TP_DYNAMIC || O->isl >= 0) {
                    continue;
                }
                O->isl = (int16_t)k;
                in->stack[sp++] = (int16_t)o;
            }
        }
        in->isl_head[k] = (int16_t)head;
    }
    in->nisl = nisl;
    w->stat.islands = nisl;
}

// ---------------------------------------------------------------- a step: the solver (the soft step)
static void joint_prepare(tp_world_t *w, tp_joint_t *j, float h)
{
    const tp_body_t *A = &w->body[j->a], *B = &w->body[j->b];
    j->ra = rot(A->xf.q, j->la);
    j->rb = rot(B->xf.q, j->lb);
    j->dc = vsub(B->xf.p, A->xf.p);
    j->da = unwind(relang(B->xf.q, A->xf.q) - j->ref);
    if (j->kind == TP_JOINT_DISTANCE) {
        vec ax = vnorm(vadd(vsub(j->rb, j->ra), j->dc));
        float ca = vcross(j->ra, ax), cb = vcross(j->rb, ax);
        float k = A->im + B->im + A->ii * ca * ca + B->ii * cb * cb;
        j->axial = k > 0.0f ? 1.0f / k : 0.0f;
    } else {
        float k = A->ii + B->ii;
        j->axial = k > 0.0f ? 1.0f / k : 0.0f;
    }
    j->soft = mksoft(j->hz, j->zeta, h);
    j->fmax = j->tmax = 0.0f;
}

static void joint_warm(tp_world_t *w, tp_joint_t *j)
{
    st_t *sa = &w->in->st[j->a], *sb = &w->in->st[j->b];
    const tp_body_t *A = &w->body[j->a], *B = &w->body[j->b];
    float ima = A->im, iia = A->ii, imb = B->im, iib = B->ii;
    vec rA = rot(sa->dq, j->ra), rB = rot(sb->dq, j->rb);
    if (j->kind == TP_JOINT_DISTANCE) {
        vec ax = vnorm(vadd(vadd(vsub(sb->dp, sa->dp), j->dc), vsub(rB, rA)));
        vec P = vscale(j->imp + j->lo_i - j->up_i, ax);
        sa->v = vmadd(sa->v, -ima, P);
        sa->w -= iia * vcross(rA, P);
        sb->v = vmadd(sb->v, imb, P);
        sb->w += iib * vcross(rB, P);
        return;
    }
    float axi = j->kind == TP_JOINT_WELD ? j->ang : j->mot + j->lo_i - j->up_i;
    sa->v = vmadd(sa->v, -ima, j->lin);
    sa->w -= iia * (vcross(rA, j->lin) + axi);
    sb->v = vmadd(sb->v, imb, j->lin);
    sb->w += iib * (vcross(rB, j->lin) + axi);
}

// The state of joint_solve: the states of the two bodies, their masses, the anchors and the speeds (the solve changes
// the speeds; joint_solve writes them back at the end)
typedef struct {
    st_t *sa, *sb;
    float ima, iia, imb, iib, inv_h;
    tp_soft_t sj;
    vec rA, rB;
    vec vA, vB;
    float wA, wB;
} jsolve_t;

// joint_solve: a distance joint (a rod, a spring, a rope)
static void joint_solve_dist(jsolve_t *c, tp_joint_t *j, bool bias)
{
    st_t *sa = c->sa, *sb = c->sb;
    const float ima = c->ima, iia = c->iia, imb = c->imb, iib = c->iib, inv_h = c->inv_h;
    const tp_soft_t sj = c->sj;
    const vec rA = c->rA, rB = c->rB;
    vec vA = c->vA, vB = c->vB;
    float wA = c->wA, wB = c->wB;
    vec d = vadd(vadd(vsub(sb->dp, sa->dp), j->dc), vsub(rB, rA));
    float len = vlen(d);
    vec ax = len > 1e-9f ? vscale(1.0f / len, d) : V(1, 0);
    bool rope = j->minlen < j->maxlen;
    if (!rope || (j->flags & TPJ_SPRING)) {
        float C = len - j->len, b = 0.0f, ms = 1.0f, is = 0.0f;
        if (j->flags & TPJ_SPRING) {
            b = j->soft.bias * C;
            ms = j->soft.mass;
            is = j->soft.imp;
        } else if (bias) {
            b = sj.bias * C;
            ms = sj.mass;
            is = sj.imp;
        }
        vec vr = vsub(vadd(vB, vcrosssv(wB, rB)), vadd(vA, vcrosssv(wA, rA)));
        float imp = -ms * j->axial * (vdot(ax, vr) + b) - is * j->imp;
        j->imp += imp;
        vec P = vscale(imp, ax);
        vA = vmadd(vA, -ima, P);
        wA -= iia * vcross(rA, P);
        vB = vmadd(vB, imb, P);
        wB += iib * vcross(rB, P);
    }
    if (rope) {
        if (j->minlen > 0.0f) {
            float C = len - j->minlen, b = 0.0f, ms = 1.0f, is = 0.0f;
            if (C > 0.0f) {
                b = C * inv_h;
            } else if (bias) {
                b = sj.bias * C;
                ms = sj.mass;
                is = sj.imp;
            }
            vec vr = vsub(vadd(vB, vcrosssv(wB, rB)), vadd(vA, vcrosssv(wA, rA)));
            float imp = -ms * j->axial * (vdot(ax, vr) + b) - is * j->lo_i;
            float ni = fmx(0.0f, j->lo_i + imp);
            imp = ni - j->lo_i;
            j->lo_i = ni;
            vec P = vscale(imp, ax);
            vA = vmadd(vA, -ima, P);
            wA -= iia * vcross(rA, P);
            vB = vmadd(vB, imb, P);
            wB += iib * vcross(rB, P);
        }
        float C = j->maxlen - len, b = 0.0f, ms = 1.0f, is = 0.0f;
        if (C > 0.0f) {
            b = C * inv_h;
        } else if (bias) {
            b = sj.bias * C;
            ms = sj.mass;
            is = sj.imp;
        }
        vec vr = vsub(vadd(vA, vcrosssv(wA, rA)), vadd(vB, vcrosssv(wB, rB)));
        float imp = -ms * j->axial * (vdot(ax, vr) + b) - is * j->up_i;
        float ni = fmx(0.0f, j->up_i + imp);
        imp = ni - j->up_i;
        j->up_i = ni;
        vec P = vscale(-imp, ax);
        vA = vmadd(vA, -ima, P);
        wA -= iia * vcross(rA, P);
        vB = vmadd(vB, imb, P);
        wB += iib * vcross(rB, P);
    }
    c->vA = vA;
    c->vB = vB;
    c->wA = wA;
    c->wB = wB;
}

// joint_solve: the angle of a weld, or the motor, the friction and the limits of a revolute joint
static void joint_solve_angle(jsolve_t *c, tp_joint_t *j, const struct tp_in *in, bool bias, bool weld, bool soft,
                              tp_soft_t sf)
{
    st_t *sa = c->sa, *sb = c->sb;
    const float iia = c->iia, iib = c->iib, inv_h = c->inv_h;
    const tp_soft_t sj = c->sj;
    float wA = c->wA, wB = c->wB;
    if (weld) {
        float b = 0.0f, ms = 1.0f, is = 0.0f;
        if (bias || soft) {
            b = sf.bias * (relang(sb->dq, sa->dq) + j->da);
            ms = sf.mass;
            is = sf.imp;
        }
        float imp = -j->axial * ms * (wB - wA + b) - is * j->ang;
        j->ang += imp;
        wA -= iia * imp;
        wB += iib * imp;
    } else if (iia + iib > 0.0f) {
        bool motor = (j->flags & TPJ_MOTOR) != 0;
        if (motor || j->friction > 0.0f) {
            // the motor, or the friction of the pin (a motor with no speed and a small torque)
            float sp = motor ? j->mspeed : 0.0f, tq = motor ? j->mtorque : j->friction;
            float imp = -j->axial * (wB - wA - sp), old = j->mot, mx = in->h * tq;
            j->mot = fcl(old + imp, -mx, mx);
            imp = j->mot - old;
            wA -= iia * imp;
            wB += iib * imp;
        }
        if (j->flags & TPJ_LIMIT) {
            float ang = unwind(j->da + relang(sb->dq, sa->dq));
            {
                float C = ang - j->lower, b = 0.0f, ms = 1.0f, is = 0.0f;
                if (C > 0.0f) {
                    b = C * inv_h;
                } else if (bias) {
                    b = sj.bias * C;
                    ms = sj.mass;
                    is = sj.imp;
                }
                float imp = -j->axial * ms * (wB - wA + b) - is * j->lo_i;
                float ni = fmx(j->lo_i + imp, 0.0f);
                imp = ni - j->lo_i;
                j->lo_i = ni;
                wA -= iia * imp;
                wB += iib * imp;
            }
            {
                float C = j->upper - ang, b = 0.0f, ms = 1.0f, is = 0.0f;
                if (C > 0.0f) {
                    b = C * inv_h;
                } else if (bias) {
                    b = sj.bias * C;
                    ms = sj.mass;
                    is = sj.imp;
                }
                float imp = -j->axial * ms * (wA - wB + b) - is * j->up_i;
                float ni = fmx(j->up_i + imp, 0.0f);
                imp = ni - j->up_i;
                j->up_i = ni;
                wA += iia * imp;
                wB -= iib * imp;
            }
        }
    }
    c->wA = wA;
    c->wB = wB;
}

static void joint_solve(tp_world_t *w, tp_joint_t *j, bool bias)
{
    struct tp_in *in = w->in;
    st_t *sa = &in->st[j->a], *sb = &in->st[j->b];
    const tp_body_t *A = &w->body[j->a], *B = &w->body[j->b];
    jsolve_t c;
    c.sa = sa;
    c.sb = sb;
    c.ima = A->im;
    c.iia = A->ii;
    c.imb = B->im;
    c.iib = B->ii;
    c.vA = sa->v;
    c.vB = sb->v;
    c.wA = sa->w;
    c.wB = sb->w;
    c.inv_h = in->inv_h;
    c.sj = in->soft_j;
    c.rA = rot(sa->dq, j->ra);
    c.rB = rot(sb->dq, j->rb);
    if (j->kind == TP_JOINT_DISTANCE) {
        joint_solve_dist(&c, j, bias);
    } else {
        bool weld = j->kind == TP_JOINT_WELD, soft = weld && j->hz > 0.0f;
        tp_soft_t sf = soft ? j->soft : c.sj;
        joint_solve_angle(&c, j, in, bias, weld, soft, sf);
        const float ima = c.ima, iia = c.iia, imb = c.imb, iib = c.iib;
        const vec rA = c.rA, rB = c.rB;
        vec vA = c.vA, vB = c.vB;
        float wA = c.wA, wB = c.wB;
        // the point: the anchors stay together
        vec cd = vsub(vadd(vB, vcrosssv(wB, rB)), vadd(vA, vcrosssv(wA, rA)));
        vec bv = V(0, 0);
        float ms = 1.0f, is = 0.0f;
        if (bias || soft) {
            vec sep = vadd(vadd(vsub(sb->dp, sa->dp), vsub(rB, rA)), j->dc);
            bv = vscale(sf.bias, sep);
            ms = sf.mass;
            is = sf.imp;
        }
        float k11 = ima + imb + rA.y * rA.y * iia + rB.y * rB.y * iib;
        float k12 = -rA.y * rA.x * iia - rB.y * rB.x * iib;
        float k22 = ima + imb + rA.x * rA.x * iia + rB.x * rB.x * iib;
        vec bb = solve22(k11, k12, k12, k22, vadd(cd, bv));
        vec imp = V(-ms * bb.x - is * j->lin.x, -ms * bb.y - is * j->lin.y);
        j->lin = vadd(j->lin, imp);
        vA = vmadd(vA, -ima, imp);
        wA -= iia * vcross(rA, imp);
        vB = vmadd(vB, imb, imp);
        wB += iib * vcross(rB, imp);
        c.vA = vA;
        c.vB = vB;
        c.wA = wA;
        c.wB = wB;
    }
    sa->v = c.vA;
    sa->w = c.wA;
    sb->v = c.vB;
    sb->w = c.wB;
}

// the warm start: the impulses of the last sub-step again, for all the contacts before the solve (the weights and
// the supports of a stack must come on together); the sums of the points: one change of each body
static void contacts_warm(tp_world_t *w)
{
    struct tp_in *in = w->in;
    for (int k = 0; k < in->ncc; k++) {
        cc_t *q = &in->cc[k];
        st_t *sa = &in->st[q->a], *sb = &in->st[q->b];
        float pn = 0.0f, pt = 0.0f, la = q->rolli, lb = q->rolli;
        for (int i = 0; i < q->np; i++) {
            const ccp_t *p = &q->p[i];
            pn += p->ni;
            pt += p->ti;
            la += p->ni * p->rna + p->ti * p->tna;
            lb += p->ni * p->rnb + p->ti * p->tnb;
        }
        float nx = q->n.x, ny = q->n.y, px = pn * nx + pt * ny, py = pn * ny - pt * nx;  // P = pn n + pt t
        sa->v.x -= q->ima * px;
        sa->v.y -= q->ima * py;
        sa->w -= q->iia * la;
        sb->v.x += q->imb * px;
        sb->v.y += q->imb * py;
        sb->w += q->iib * lb;
    }
}

#if defined(__GNUC__)
#define TP_ALWAYS_INLINE static inline __attribute__((always_inline))
#else
#define TP_ALWAYS_INLINE static inline
#endif

// one pass of the contacts (bias: the solve with the soft push out; no bias: the relax). The normal loop changes
// only the relative normal speed (one number), the friction loop only the relative tangent speed; the sums of the
// impulses go to the bodies at the end. Two copies (solve, relax): no test of bias in the loops.
TP_ALWAYS_INLINE void contacts_pass(tp_world_t *w, const bool bias)
{
    struct tp_in *in = w->in;
    const float inv_h = in->inv_h, push = w->push_max;
    for (int k = 0; k < in->ncc; k++) {
        cc_t *q = &in->cc[k];
        st_t *sa = &in->st[q->a], *sb = &in->st[q->b];
        float wA = sa->w, wB = sb->w, pn = 0.0f, pt = 0.0f;
        // (each loop reads only what it needs: few values stay in the registers; the board has 16 float registers)
        {
            const float nx = q->n.x, ny = q->n.y, imab = q->ima + q->imb;
            float vrn = (sb->v.x - sa->v.x) * nx + (sb->v.y - sa->v.y) * ny;
            const float cA = sa->dq.c, sA = sa->dq.s, cB = sb->dq.c, sB = sb->dq.s;
            const float dpn = (sb->dp.x - sa->dp.x) * nx + (sb->dp.y - sa->dp.y) * ny;
            const tp_soft_t *sf = q->stat ? &in->soft_s : &in->soft_c;
            for (int i = 0; i < q->np; i++) {
                ccp_t *p = &q->p[i];
                // the distance now: the anchors move with the bodies (the normal stays)
                float s = dpn + cB * p->dnb + sB * p->rnb - cA * p->dna - sA * p->rna + p->sep;
                float vn = vrn + wB * p->rnb - wA * p->rna, old = p->ni, imp;
                if (s > 0.0f) {
                    imp = -p->nm * (vn + s * inv_h);  // speculative: the bodies can come this near
                } else if (bias) {
                    imp = -p->nm * sf->mass * (vn + fmx(sf->bias * s, -push)) - sf->imp * old;
                } else {
                    imp = -p->nm * vn;
                }
                float ni = fmx(old + imp, 0.0f);
                imp = ni - old;
                p->ni = ni;
                if (!bias) {
                    p->total += ni;
                }
                vrn += imab * imp;
                wA -= p->ka * imp;
                wB += p->kb * imp;
                pn += imp;
            }
        }
        {
            // (the normal impulses do not change the tangent speed: it comes from the states in memory)
            const float nx = q->n.x, ny = q->n.y, imab = q->ima + q->imb, iia = q->iia, iib = q->iib, fr = q->fr;
            float vrt = (sb->v.x - sa->v.x) * ny - (sb->v.y - sa->v.y) * nx;
            for (int i = 0; i < q->np; i++) {
                ccp_t *p = &q->p[i];
                float old = p->ti, mf = fr * p->ni;
                if (mf == 0.0f && old == 0.0f) {
                    continue;  // (no normal impulse: no friction)
                }
                float vt = vrt + wB * p->tnb - wA * p->tna;
                float ti = fcl(old - p->tm * vt, -mf, mf), imp = ti - old;
                p->ti = ti;
                vrt += imab * imp;
                wA -= iia * p->tna * imp;
                wB += iib * p->tnb * imp;
                pt += imp;
            }
        }
        if (q->rr > 0.0f) {
            float tot = 0.0f;
            for (int i = 0; i < q->np; i++) {
                tot += q->p[i].ni;
            }
            float dl = -q->rm * (wB - wA), l = q->rolli, ml = q->rr * tot;
            q->rolli = fcl(l + dl, -ml, ml);
            dl = q->rolli - l;
            wA -= q->iia * dl;
            wB += q->iib * dl;
        }
        // the sums of the impulses to the bodies: P = pn n + pt t, t = (ny, -nx) (a static or a kinematic body has
        // no inverse mass: its speed stays exactly)
        const float nx = q->n.x, ny = q->n.y;
        float px = pn * nx + pt * ny, py = pn * ny - pt * nx;
        sa->v.x -= q->ima * px;
        sa->v.y -= q->ima * py;
        sa->w = wA;
        sb->v.x += q->imb * px;
        sb->v.y += q->imb * py;
        sb->w = wB;
    }
}
static void contacts_solve(tp_world_t *w)
{
    contacts_pass(w, true);
}
static void contacts_relax(tp_world_t *w)
{
    contacts_pass(w, false);
}

static void contacts_restitution(tp_world_t *w)
{
    struct tp_in *in = w->in;
    float thr = w->bounce_min;
    for (int k = 0; k < in->ncc; k++) {
        cc_t *q = &in->cc[k];
        if (q->e == 0.0f) {
            continue;
        }
        st_t *sa = &in->st[q->a], *sb = &in->st[q->b];
        float nx = q->n.x, ny = q->n.y;
        float vAn = sa->v.x * nx + sa->v.y * ny, vBn = sb->v.x * nx + sb->v.y * ny, wA = sa->w, wB = sb->w, dA = 0.0f,
              dB = 0.0f;
        for (int i = 0; i < q->np; i++) {
            ccp_t *p = &q->p[i];
            if (p->rv > -thr || p->total == 0.0f) {
                continue;  // no hit: a speculative point, or too slow
            }
            float vn = vBn - vAn + wB * p->rnb - wA * p->rna;
            float imp = -p->nm * (vn + q->e * p->rv);
            float ni = fmx(p->ni + imp, 0.0f);
            imp = ni - p->ni;
            p->ni = ni;
            p->total += imp;
            vAn -= q->ima * imp;
            dA -= q->ima * imp;
            wA -= p->ka * imp;
            vBn += q->imb * imp;
            dB += q->imb * imp;
            wB += p->kb * imp;
        }
        sa->v.x += dA * nx;
        sa->v.y += dA * ny;
        sa->w = wA;
        sb->v.x += dB * nx;
        sb->v.y += dB * ny;
        sb->w = wB;
    }
}

// solve: the list and the states of the bodies that move (the other states stay at rest)
static void solve_bodies(tp_world_t *w, struct tp_in *in, float h)
{
    in->nmv = 0;
    int awake = 0, sleeping = 0;
    for (int s = 0; s < w->body_hi; s++) {
        st_t *st = &in->st[s];
        tp_body_t *b = &w->body[s];
        st->v = V(0, 0);
        st->w = 0.0f;
        st->dp = V(0, 0);
        st->dq = RID;
        if (!(b->flags & TP_ALIVE) || b->kind == TP_STATIC) {
            continue;
        }
        if (!(b->flags & TP_AWAKE)) {
            sleeping += b->kind == TP_DYNAMIC;
            continue;
        }
        mv_t *m = &in->mv[in->nmv++];
        m->i = (int16_t)s;
        m->dyn = b->kind == TP_DYNAMIC;
        st->v = b->v;
        st->w = b->w;
        if (m->dyn) {
            awake++;
            m->ld = b->ldamp > 0.0f ? 1.0f / (1.0f + h * b->ldamp) : 1.0f;
            m->ad = b->adamp > 0.0f ? 1.0f / (1.0f + h * b->adamp) : 1.0f;
            m->gv = vscale(h, vadd(vscale(b->gscale, w->gravity), vscale(b->im, b->force)));
            m->tw = h * b->ii * b->torque;
        }
    }
    w->stat.awake = awake;
    w->stat.sleeping = sleeping;
}

// solve: the list of the joints with an awake body, prepared for the sub-steps
static void solve_joints(tp_world_t *w, struct tp_in *in, float h)
{
    in->njl = 0;
    for (int i = 0; i < w->joint_hi; i++) {
        tp_joint_t *j = &w->joint[i];
        if (!(j->flags & TPJ_ALIVE)) {
            continue;
        }
        const tp_body_t *A = &w->body[j->a], *B = &w->body[j->b];
        bool aa = A->kind == TP_DYNAMIC && (A->flags & TP_AWAKE), ba = B->kind == TP_DYNAMIC && (B->flags & TP_AWAKE);
        if (!aa && !ba) {
            continue;
        }
        in->jl[in->njl++] = (int16_t)i;
        joint_prepare(w, j, h);
    }
    w->stat.joints = in->njl;
}

// solve: the constraints of the contacts with an awake body (the masses and the speeds of the points)
static void solve_contacts(tp_world_t *w, struct tp_in *in)
{
    in->ncc = 0;
    int points = 0;
    for (int ci = 0; ci < w->contact_hi; ci++) {
        tp_contact_t *c = &w->contact[ci];
        if (c->a < 0 || !c->np) {
            continue;
        }
        const tp_body_t *A = &w->body[c->a], *B = &w->body[c->b];
        bool aa = A->kind == TP_DYNAMIC && (A->flags & TP_AWAKE), ba = B->kind == TP_DYNAMIC && (B->flags & TP_AWAKE);
        if (!aa && !ba) {
            continue;
        }
        cc_t *q = &in->cc[in->ncc++];
        q->a = c->a;
        q->b = c->b;
        q->c = (int16_t)ci;
        q->np = c->np;
        q->ima = A->im;
        q->iia = A->ii;
        q->imb = B->im;
        q->iib = B->ii;
        q->stat = A->im == 0.0f || B->im == 0.0f;
        q->n = c->n;
        q->fr = c->fr;
        q->e = c->e;
        q->rr = c->rr;
        q->rm = q->rr > 0.0f && q->iia + q->iib > 0.0f ? 1.0f / (q->iia + q->iib) : 0.0f;
        q->rolli = c->rolli;
        q->ni0 = 0.0f;
        vec n = q->n, t = vrperp(n);
        const st_t *sa = &in->st[q->a], *sb = &in->st[q->b];
        for (int i = 0; i < q->np; i++) {
            ccp_t *p = &q->p[i];
            const tp_cpoint_t *cp = &c->pt[i];
            vec ra = cp->ra, rb = cp->rb;
            p->dna = vdot(n, ra);
            p->dnb = vdot(n, rb);
            p->rna = vcross(ra, n);
            p->rnb = vcross(rb, n);
            p->tna = vcross(ra, t);
            p->tnb = vcross(rb, t);
            p->ka = q->iia * p->rna;
            p->kb = q->iib * p->rnb;
            p->sep = cp->sep - (p->dnb - p->dna);
            p->ni = cp->ni;
            q->ni0 += cp->ni;
            p->ti = cp->ti;
            p->total = 0.0f;
            // the normal and the tangent masses with one division (1 / (kn kt))
            float kn = q->ima + q->imb + q->iia * p->rna * p->rna + q->iib * p->rnb * p->rnb;
            float kt = q->ima + q->imb + q->iia * p->tna * p->tna + q->iib * p->tnb * p->tnb;
            float kk = kn * kt, ik = kk > 0.0f ? 1.0f / kk : 0.0f;
            p->nm = kt * ik;
            p->tm = kn * ik;
            vec dv = vsub(vadd(sb->v, vcrosssv(sb->w, rb)), vadd(sa->v, vcrosssv(sa->w, ra)));
            p->rv = vdot(n, dv);
        }
        points += q->np;
    }
    w->stat.contacts = in->ncc;
    w->stat.points = points;
}

// solve: the S sub-steps (the speeds, the warm start, the solve, the move, the relax)
static void solve_substeps(tp_world_t *w, struct tp_in *in, int S, float h, float inv_h)
{
    float vmax = w->v_max, vmax2 = vmax * vmax, wmax = 0.25f * PI_F * inv_h;
    for (int sub = 0; sub < S; sub++) {
        for (int k = 0; k < in->nmv; k++) {
            const mv_t *m = &in->mv[k];
            if (!m->dyn) {
                continue;
            }
            st_t *st = &in->st[m->i];
            vec v = vadd(vscale(m->ld, st->v), m->gv);
            float wv = m->ad * st->w + m->tw, v2 = vlen2(v);
            if (v2 > vmax2) {
                v = vscale(vmax / sqrtf(v2), v);
            }
            st->v = v;
            st->w = fcl(wv, -wmax, wmax);
        }
        for (int k = 0; k < in->njl; k++) {
            joint_warm(w, &w->joint[in->jl[k]]);
        }
        contacts_warm(w);
        for (int k = 0; k < in->njl; k++) {
            joint_solve(w, &w->joint[in->jl[k]], true);
        }
        contacts_solve(w);
        for (int k = 0; k < in->nmv; k++) {
            st_t *st = &in->st[in->mv[k].i];
            st->dq = rinteg(st->dq, h * st->w);
            st->dp = vmadd(st->dp, h, st->v);
        }
        for (int k = 0; k < in->njl; k++) {
            joint_solve(w, &w->joint[in->jl[k]], false);
        }
        contacts_relax(w);
        for (int k = 0; k < in->njl; k++) {
            tp_joint_t *j = &w->joint[in->jl[k]];
            float f = j->kind == TP_JOINT_DISTANCE ? fab(j->imp + j->lo_i - j->up_i) : vlen(j->lin);
            float tq = j->kind == TP_JOINT_WELD       ? fab(j->ang)
                       : j->kind == TP_JOINT_REVOLUTE ? fab(j->lo_i - j->up_i)
                                                      : 0.0f;
            j->fmax = fmx(j->fmax, f);
            j->tmax = fmx(j->tmax, tq);
        }
    }
}

// solve: the impulses back to the contacts (the warm start of the next step)
static void solve_store(tp_world_t *w, const struct tp_in *in, int S)
{
    float Sf = (float)S;
    for (int k = 0; k < in->ncc; k++) {
        const cc_t *q = &in->cc[k];
        tp_contact_t *c = &w->contact[q->c];
        for (int i = 0; i < q->np; i++) {
            c->pt[i].hit = fmx(0.0f, q->p[i].total - Sf * c->pt[i].ni);  // (c->pt[i].ni: the warm start)
            c->pt[i].ni = q->p[i].ni;
            c->pt[i].ti = q->p[i].ti;
        }
        c->rolli = q->rolli;
    }
}

// solve: the new places and speeds of the bodies, and their sleep times
static void solve_places(tp_world_t *w, const struct tp_in *in, float dt)
{
    for (int k = 0; k < in->nmv; k++) {
        const mv_t *m = &in->mv[k];
        tp_body_t *b = &w->body[m->i];
        const st_t *st = &in->st[m->i];
        b->v = st->v;
        b->w = st->w;
        b->xf.p = vadd(b->xf.p, st->dp);
        b->xf.q = rinteg(rmul(st->dq, b->xf.q), 0.0f);  // (the length 1 again: near 1 with no root)
        if (m->dyn) {
            // the sleep speed: max(|v| + |w| extent, 0.5 / dt (|dp| + |dq.s| extent)) > sleep_v, with no root
            float t1 = w->sleep_v - fab(st->w) * b->extent, t2 = w->sleep_v * 2.0f * dt - fab(st->dq.s) * b->extent;
            bool fast = t1 < 0.0f || vlen2(st->v) > t1 * t1 || t2 < 0.0f || vlen2(st->dp) > t2 * t2;
            if ((b->flags & TP_NOSLEEP) || fast) {
                b->sleep_t = 0.0f;
            } else {
                b->sleep_t += dt;
            }
        }
        b->force = V(0, 0);
        b->torque = 0.0f;
    }
}

static void solve(tp_world_t *w)
{
    struct tp_in *in = w->in;
    int S = w->substeps;
    float dt = w->dt, h = dt / (float)S, inv_h = (float)S / dt;
    float chz = fmn(w->contact_hz, 0.25f * inv_h);
    in->h = h;
    in->inv_h = inv_h;
    in->soft_c = mksoft(chz, w->contact_zeta, h);
    in->soft_s = mksoft(2.0f * chz, w->contact_zeta, h);
    in->soft_j = mksoft(w->joint_hz, w->joint_zeta, h);
    // the bodies that move: their state (the other states stay at rest)
    solve_bodies(w, in, h);
    // the joints
    solve_joints(w, in, h);
    // the contacts (the constraints)
    solve_contacts(w, in);
    // the sub-steps
    solve_substeps(w, in, S, h, inv_h);
    contacts_restitution(w);
    // the impulses back to the contacts (the warm start of the next step)
    solve_store(w, in, S);
    // the new places
    solve_places(w, in, dt);
}

// ---------------------------------------------------------------- the particles
tp_part_t *tp_emit(tp_world_t *w, int kind, tp_vec_t p, tp_vec_t v)
{
    if (kind < 0 || kind >= TP_PART_KINDS || w->npart >= w->part_cap) {
        return NULL;
    }
    const tp_pkind_t *k = &w->pkind[kind];
    tp_part_t *q = &w->part[w->npart++];
    q->p = p;
    q->v = v;
    q->ign = TP_NONE;
    q->kind = (uint8_t)kind;
    q->touch = 0;
    q->mat = 255;
    q->pad = 0;
    q->life = q->life0 = fmx(0.05f, k->life + k->life_var * (2.0f * rndf(w) - 1.0f));
    return q;
}

// a random point in a convex polygon (a triangle of the fan, by its area; then a point in it)
static vec rand_in_poly(tp_world_t *w, const vec *p, int n)
{
    float a[CELL_MAX], A = 0.0f;
    for (int i = 1; i + 1 < n; i++) {
        a[i] = 0.5f * vcross(vsub(p[i], p[0]), vsub(p[i + 1], p[0]));
        A += a[i];
    }
    float r = rndf(w) * A;
    int t = 1;
    while (t < n - 2 && r > a[t]) {
        r -= a[t];
        t++;
    }
    float u = rndf(w), v = rndf(w);
    if (u + v > 1.0f) {
        u = 1.0f - u;
        v = 1.0f - v;
    }
    return vadd(p[0], vadd(vscale(u, vsub(p[t], p[0])), vscale(v, vsub(p[t + 1], p[0]))));
}

int tp_spill(tp_world_t *w, tp_id_t id, int n, int kind)
{
    tp_body_t *b = tp_body(w, id);
    if (!b) {
        return 0;
    }
    int made = 0;
    for (int i = 0; i < n; i++) {
        vec lp;
        if (b->shape == TP_POLY) {
            lp = rand_in_poly(w, b->vl, b->n);
        } else {
            float a = 2.0f * PI_F * rndf(w), r = b->radius * sqrtf(rndf(w));
            lp = V(r * cosf(a), r * sinf(a));
        }
        vec p = xf_apply(b->xf, lp);
        vec v = vadd(tp_point_vel(b, p), V(0.6f * (rndf(w) - 0.5f), 0.6f * (rndf(w) - 0.5f)));
        tp_part_t *q = tp_emit(w, kind, p, v);
        if (!q) {
            break;
        }
        q->mat = b->mat;
        made++;
    }
    return made;
}

static inline int hcell(int ix, int iy)
{
    return (int)((((uint32_t)ix * 73856093u) ^ ((uint32_t)iy * 19349663u)) & (HASH_N - 1));
}

static void grid_build(tp_world_t *w)
{
    struct tp_in *in = w->in;
    float ic = in->icell;
    int bx0 = ifloor(w->bounds.lo.x * ic) - 1, bx1 = ifloor(w->bounds.hi.x * ic) + 1;
    int by0 = ifloor(w->bounds.lo.y * ic) - 1, by1 = ifloor(w->bounds.hi.y * ic) + 1;
    for (int k = 0; k < HASH_N; k++) {
        in->grid[k] = -1;
    }
    in->ngent = 0;
    // the moving bodies first, the static bodies last: a new entry goes to the head of its list, so a particle
    // finds the static bodies (the floor) first, also among many pieces
    for (int pass = 0; pass < 2; pass++) {
        for (int s = 0; s < w->body_hi; s++) {
            const tp_body_t *b = &w->body[s];
            if (!(b->flags & TP_ALIVE) || (b->kind == TP_STATIC) != (pass == 1)) {
                continue;
            }
            int x0 = ifloor(b->box.lo.x * ic), x1 = ifloor(b->box.hi.x * ic);
            int y0 = ifloor(b->box.lo.y * ic), y1 = ifloor(b->box.hi.y * ic);
            if (x0 < bx0) {
                x0 = bx0;
            }
            if (x1 > bx1) {
                x1 = bx1;
            }
            if (y0 < by0) {
                y0 = by0;
            }
            if (y1 > by1) {
                y1 = by1;
            }
            for (int iy = y0; iy <= y1; iy++) {
                for (int ix = x0; ix <= x1; ix++) {
                    if (in->ngent >= in->gent_cap) {
                        return;
                    }
                    int h = hcell(ix, iy);
                    in->gent[in->ngent].body = (int16_t)s;
                    in->gent[in->ngent].next = in->grid[h];
                    in->grid[h] = (int16_t)in->ngent++;
                }
            }
        }
    }
}

// the first point of the segment p0 -> p1 on the shape grown by r (false = no hit); *in = p0 is in it already
// (then *t = 0, *n = the way out, *depth = the distance to the way out)
static bool seg_shape(const tp_body_t *b, vec p0, vec p1, float r, float *t, vec *n, bool *in, float *depth)
{
    if (b->shape == TP_CIRCLE) {
        vec m = vsub(p0, b->xf.p);
        float R = b->radius + r, cc = vdot(m, m) - R * R;
        if (cc <= 0.0f) {
            float l = vlen(m);
            *in = true;
            *t = 0.0f;
            *n = l > 1e-9f ? vscale(1.0f / l, m) : V(0, 1);
            *depth = R - l;
            return true;
        }
        vec d = vsub(p1, p0);
        float a = vdot(d, d), bq = vdot(m, d);
        if (a < 1e-12f || bq >= 0.0f) {
            return false;
        }
        float disc = bq * bq - a * cc;
        if (disc < 0.0f) {
            return false;
        }
        float tt = (-bq - sqrtf(disc)) / a;
        if (tt > 1.0f) {
            return false;
        }
        *in = false;
        *t = fmx(tt, 0.0f);
        *n = vnorm(vmadd(m, *t, d));
        return true;
    }
    vec a = xf_inv(b->xf, p0), e = xf_inv(b->xf, p1);
    float tin = 0.0f, tout = 1.0f, smax = -FLT_MAX;
    int ein = -1, emax = 0;
    for (int i = 0; i < b->n; i++) {
        vec v = b->vl[i], nl = b->nl[i];
        float s0 = nl.x * (a.x - v.x) + nl.y * (a.y - v.y) - r, s1 = nl.x * (e.x - v.x) + nl.y * (e.y - v.y) - r;
        if (s0 > 0.0f && s1 > 0.0f) {
            return false;  // all of the segment is out of this edge: no hit
        }
        if (s0 > smax) {
            smax = s0;
            emax = i;
        }
        if (s0 > 0.0f) {  // it comes in through this edge
            float tt = s0 / (s0 - s1);
            if (tt > tin) {
                tin = tt;
                ein = i;
            }
        } else if (s1 > 0.0f) {  // it goes out through this edge
            float tt = s0 / (s0 - s1);
            if (tt < tout) {
                tout = tt;
            }
        }
    }
    if (smax <= 0.0f) {
        *in = true;
        *t = 0.0f;
        *n = rot(b->xf.q, b->nl[emax]);
        *depth = -smax;
        return true;
    }
    if (ein < 0 || tin > tout) {
        return false;
    }
    *in = false;
    *t = tin;
    *n = rot(b->xf.q, b->nl[ein]);
    return true;
}

static void parts_step(tp_world_t *w)
{
    struct tp_in *in = w->in;
    float dt = w->dt, ic = in->icell;
    int tests = 0;
    for (int k = 0; k < TP_PART_KINDS; k++) {
        w->splash[k] = 0;
        w->splash_v[k] = 0.0f;
    }
    w->stat.parts = w->npart;
    if (w->npart == 0) {
        return;
    }
    grid_build(w);
    float damp[TP_PART_KINDS];
    for (int k = 0; k < TP_PART_KINDS; k++) {
        damp[k] = 1.0f / (1.0f + dt * w->pkind[k].drag);
    }
    tp_box_t B = w->bounds;
    for (int i = 0; i < w->npart;) {
        tp_part_t *p = &w->part[i];
        const tp_pkind_t *k = &w->pkind[p->kind];
        p->life -= dt;
        if (p->life <= 0.0f || p->p.x < B.lo.x || p->p.x > B.hi.x || p->p.y < B.lo.y || p->p.y > B.hi.y) {
            w->part[i] = w->part[--w->npart];
            continue;
        }
        p->v = vscale(damp[p->kind], vmadd(p->v, dt * k->gravity, w->gravity));
        vec p0 = p->p, p1 = vmadd(p0, dt, p->v);
        float r = k->radius;
        vec lo = V(fmn(p0.x, p1.x) - r, fmn(p0.y, p1.y) - r), hi = V(fmx(p0.x, p1.x) + r, fmx(p0.y, p1.y) + r);
        int cand[32], nc = 0;
        tp_id_t ign = p->life0 - p->life < 0.3f ? p->ign : TP_NONE;
        int x0 = ifloor(lo.x * ic), x1 = ifloor(hi.x * ic), y0 = ifloor(lo.y * ic), y1 = ifloor(hi.y * ic);
        if (x1 - x0 > 3) {
            x1 = x0 + 3;
        }
        if (y1 - y0 > 3) {
            y1 = y0 + 3;
        }
        for (int iy = y0; iy <= y1; iy++) {
            for (int ix = x0; ix <= x1; ix++) {
                for (int e = in->grid[hcell(ix, iy)]; e >= 0; e = in->gent[e].next) {
                    int s = in->gent[e].body;
                    const tp_body_t *b = &w->body[s];
                    if (b->box.lo.x > hi.x || b->box.hi.x < lo.x || b->box.lo.y > hi.y || b->box.hi.y < lo.y) {
                        continue;
                    }
                    if (ign != TP_NONE && b->parent == ign) {
                        continue;
                    }
                    bool dup = false;
                    for (int q = 0; q < nc; q++) {
                        dup = dup || cand[q] == s;
                    }
                    if (!dup && nc < 32) {
                        cand[nc++] = s;
                    }
                }
            }
        }
        float bt = 2.0f, bdepth = 0.0f;
        vec bn = V(0, 0);
        int bs = -1;
        bool bin = false;
        for (int q = 0; q < nc; q++) {
            const tp_body_t *b = &w->body[cand[q]];
            float t, depth = 0.0f;
            vec n;
            bool inside;
            tests++;
            if (!seg_shape(b, p0, p1, r, &t, &n, &inside, &depth)) {
                continue;
            }
            if (t < bt || (inside && bin && depth > bdepth)) {
                bt = t;
                bn = n;
                bs = cand[q];
                bin = inside;
                bdepth = depth;
            }
        }
        p->touch = 0;
        bool dead = false;
        if (bs < 0) {
            p->p = p1;
        } else {
            const tp_body_t *b = &w->body[bs];
            vec hp = bin ? vmadd(p0, bdepth + 0.0005f, bn) : vmadd(vlerp(p0, p1, bt), 0.0005f, bn);
            vec vb = tp_point_vel(b, hp), vr = vsub(p->v, vb);
            float vn = vdot(vr, bn);
            if (vn < 0.0f) {
                if (-vn > 1.0f) {
                    w->splash[p->kind]++;
                    if (-vn > w->splash_v[p->kind]) {
                        w->splash_v[p->kind] = -vn;
                        w->splash_p[p->kind] = hp;
                    }
                }
                vec vt = vmadd(vr, -vn, bn);
                float e = -vn > 0.3f ? k->restitution : 0.0f;
                vr = vadd(vscale(-e * vn, bn), vscale(1.0f - k->friction, vt));
                p->v = vadd(vr, vb);
            }
            p->p = hp;
            p->touch = 1;
            // a push out of one body can put the particle into another (a piece that falls on a drop on the floor):
            // one more check of the other bodies near it, a push out of that one (no speed into it); a particle that
            // two bodies squeeze (the two ways out go against each other) goes away
            for (int q = 0; q < nc; q++) {
                if (cand[q] == bs) {
                    continue;
                }
                const tp_body_t *b2 = &w->body[cand[q]];
                float t2, d2 = 0.0f;
                vec n2;
                bool in2;
                if (!seg_shape(b2, hp, hp, r, &t2, &n2, &in2, &d2) || !in2) {
                    continue;
                }
                if (vdot(n2, bn) < -0.3f) {
                    dead = true;
                    break;
                }
                p->p = vmadd(hp, d2 + 0.0005f, n2);
                vec vr2 = vsub(p->v, tp_point_vel(b2, p->p));
                float vn2 = vdot(vr2, n2);
                if (vn2 < 0.0f) {
                    p->v = vmadd(p->v, -vn2, n2);
                }
                break;
            }
        }
        if (dead) {
            w->part[i] = w->part[--w->npart];
            continue;
        }
        i++;
    }
    w->stat.part_tests = tests;
}

// ---------------------------------------------------------------- the breaks
// keep the side dot(x, nn) <= off of the polygon p (n corners) in out; returns the new count
static int clip_half(const vec *p, int n, vec nn, float off, vec *out)
{
    int m = 0;
    for (int i = 0; i < n && m < CELL_MAX; i++) {
        vec a = p[i], b = p[i + 1 < n ? i + 1 : 0];
        float da = vdot(a, nn) - off, db = vdot(b, nn) - off;
        if (da <= 0.0f) {
            out[m++] = a;
        }
        if (((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f)) && m < CELL_MAX) {
            out[m++] = vlerp(a, b, da / (da - db));
        }
    }
    return m;
}

typedef struct {
    vec v[CELL_MAX];
    int n;
    float area;
    vec c;
} cell_t;

// do_break: the polygon of the shape in its own frame (a circle: 8 corners); returns the count of corners
static int brk_poly(const tp_body_t *X, vec *poly)
{
    int n;
    if (X->shape == TP_CIRCLE) {
        n = 8;
        for (int i = 0; i < 8; i++) {
            float a = (float)i * (2.0f * PI_F / 8.0f);
            poly[i] = V(X->radius * cosf(a), X->radius * sinf(a));
        }
    } else {
        n = X->n;
        memcpy(poly, X->vl, sizeof(vec) * (size_t)n);
    }
    return n;
}

// do_break: the seeds of the pieces (each piece is the part of the shape nearest to its seed); returns their count
static int brk_seeds(tp_world_t *w, const tp_mat_t *m, const vec *poly, int n, int k, vec Pin, vec *seed)
{
    int ns = 0;
    if (m->pattern == TP_BREAK_SPLINTER) {
        float sxx = 0.0f, syy = 0.0f, sxy = 0.0f;
        for (int i = 0; i < n; i++) {
            sxx += poly[i].x * poly[i].x;
            syy += poly[i].y * poly[i].y;
            sxy += poly[i].x * poly[i].y;
        }
        float th = 0.5f * fatan2(2.0f * sxy, sxx - syy);
        vec u = V(cosf(th), sinf(th)), v = vlperp(u);
        float u0 = FLT_MAX, u1 = -FLT_MAX, v0 = FLT_MAX, v1 = -FLT_MAX;
        for (int i = 0; i < n; i++) {
            u0 = fmn(u0, vdot(poly[i], u));
            u1 = fmx(u1, vdot(poly[i], u));
            v0 = fmn(v0, vdot(poly[i], v));
            v1 = fmx(v1, vdot(poly[i], v));
        }
        float L = u1 - u0, W = v1 - v0, s = (0.16f + 0.10f * rndf(w)) * L;
        vec off = vscale(W * 0.6f * (rndf(w) - 0.5f), v);
        seed[ns++] = vadd(vmadd(Pin, -s, u), off);
        seed[ns++] = vsub(vmadd(Pin, s, u), off);
        if (k >= 3 && rndf(w) < 0.6f) {
            seed[ns++] =
                    vadd(vmadd(Pin, (rndf(w) - 0.5f) * 0.1f * L, u), vscale((rndf(w) < 0.5f ? -0.3f : 0.3f) * W, v));
        }
    } else {
        bool sh = m->pattern == TP_BREAK_SHARDS;
        if (sh) {
            seed[ns++] = Pin;
        }
        for (int tries = 0; ns < k && tries < 4 * k; tries++) {
            vec q = rand_in_poly(w, poly, n);
            float u = rndf(w), f = sh ? 0.12f + 0.88f * u * u : 0.4f + 0.6f * u;
            vec sd = vlerp(Pin, q, f);
            bool dup = false;
            for (int j = 0; j < ns; j++) {
                dup = dup || vlen2(vsub(sd, seed[j])) < 1e-6f;
            }
            if (!dup) {
                seed[ns++] = sd;
            }
        }
    }
    return ns;
}

// do_break: the cells (the shape cut by the half planes to the other seeds: each is convex); returns their count
static int brk_cells(const vec *poly, int n, const vec *seed, int ns, cell_t *cell)
{
    int nc = 0;
    for (int i = 0; i < ns; i++) {
        cell_t *ce = &cell[nc];
        vec buf[CELL_MAX];
        ce->n = n;
        memcpy(ce->v, poly, sizeof(vec) * (size_t)n);
        for (int j = 0; j < ns && ce->n >= 3; j++) {
            if (j == i) {
                continue;
            }
            vec nn = vsub(seed[j], seed[i]);
            float off = 0.5f * (vlen2(seed[j]) - vlen2(seed[i]));
            ce->n = clip_half(ce->v, ce->n, nn, off, buf);
            memcpy(ce->v, buf, sizeof(vec) * (size_t)ce->n);
        }
        if (ce->n < 3) {
            continue;
        }
        ce->n = poly_clean(ce->v, ce->n, 0.002f);
        if (ce->n < 3) {
            continue;
        }
        poly_mass(ce->v, ce->n, &ce->area, &ce->c, NULL);
        if (ce->area > 0.0f) {
            nc++;
        }
    }
    return nc;
}

// do_break: the burst speed of each piece in bv (out from the point of the hit); returns the mean of the big pieces
// (no change of the momentum: the pieces get bv less the mean)
static vec brk_burst(tp_world_t *w, const tp_mat_t *m, const tp_body_t *X, const cell_t *cell, int nc, vec Pin,
                     float dv, vec *bv)
{
    float bs = m->burst * fcl(0.5f + 0.25f * dv / fmx(X->toughness, 0.1f), 0.75f, 1.5f);
    vec mean = V(0, 0);
    float M = 0.0f;
    for (int i = 0; i < nc; i++) {
        vec d = vsub(cell[i].c, Pin);
        float l = vlen(d), a = 2.0f * PI_F * rndf(w);
        vec u = l > 1e-6f ? vscale(1.0f / l, d) : V(cosf(a), sinf(a));
        bv[i] = vscale(bs * (0.6f + 0.4f * rndf(w)), u);
        if (cell[i].area >= w->crumb_area) {
            mean = vmadd(mean, cell[i].area, bv[i]);
            M += cell[i].area;
        }
    }
    if (M > 0.0f) {
        mean = vscale(1.0f / M, mean);
    }
    return mean;
}

// do_break: the new bodies of the pieces (slot: the body of each cell, or -1: crumbs); returns the count of new
// bodies. *xp is the body that breaks: a new body can move the array of the bodies
static int brk_pieces(tp_world_t *w, int xs, tp_body_t **xp, const tp_mat_t *m, const cell_t *cell, int nc, vec vX,
                      float wX, const vec *bv, vec mean, const tp_body_t *O, tp_id_t xid, int *slot)
{
    tp_body_t *X = *xp;
    int made = 0;
    float spin = m->pattern == TP_BREAK_SHARDS ? 3.0f : 1.2f;
    bool crowd = w->nbody > (w->body_cap * 3) / 4;
    for (int i = 0; i < nc; i++) {
        const cell_t *ce = &cell[i];
        vec cw = xf_apply(X->xf, ce->c);
        vec vel = vadd(vadd(vX, vcrosssv(wX, rot(X->xf.q, ce->c))), vsub(bv[i], mean));
        slot[i] = -1;
        // a piece whose middle is in the other body (a fast hitter went into the body in this step): crumbs
        bool inside = false;
        if (O) {
            if (O->shape == TP_CIRCLE) {
                inside = vlen2(vsub(cw, O->xf.p)) < O->radius * O->radius;
            } else {
                inside = poly_out(O->vl, O->n, xf_inv(O->xf, cw)) < 0.0f;
            }
        }
        if (ce->area < w->crumb_area || inside) {
            int np = ce->area > 0.5f * w->crumb_area ? 2 : 1;
            for (int q = 0; q < np; q++) {
                tp_part_t *pp =
                        tp_emit(w, m->crumb_kind, cw, vadd(vel, V(0.4f * (rndf(w) - 0.5f), 0.4f * (rndf(w) - 0.5f))));
                if (pp) {
                    pp->mat = X->mat;
                    pp->ign = xid;
                }
            }
            continue;
        }
        vec lv[TP_MAX_VERTS];
        for (int q = 0; q < ce->n; q++) {
            lv[q] = vsub(ce->v[q], ce->c);
        }
        tp_body_def_t d = tp_body_def(TP_DYNAMIC, cw.x, cw.y, X->mat);
        bool tiny = ce->area < w->fade_area || ce->area < 0.12f * X->area;
        d.vel = vel;
        d.spin = wX + (rndf(w) - 0.5f) * 2.0f * spin;
        d.toughness = !tiny && X->gen + 1 < 2 ? X->toughness * 1.3f : 0.0f;
        d.ldamp = X->ldamp;
        d.adamp = X->adamp;
        d.gscale = X->kind == TP_STATIC ? 1.0f : X->gscale;
        d.group = X->group;
        d.cat = X->cat;
        d.mask = X->mask;
        d.user = X->user;
        d.no_sleep = (X->flags & TP_NOSLEEP) != 0;
        if (tiny) {
            d.life = 1.2f + 2.0f * rndf(w);
        } else if (crowd) {
            d.life = 5.0f + 4.0f * rndf(w);
        }
        int s = body_make(w, &d, TP_POLY, lv, ce->n, 0.0f, cw, X->xf.q);
        if (s < 0) {
            continue;
        }
        X = &w->body[xs];
        tp_body_t *pb = &w->body[s];
        pb->gen = (uint8_t)(X->gen + 1);
        pb->parent = xid;
        slot[i] = s;
        if (w->npiece < w->piece_cap) {
            w->piece[w->npiece++] = mkid(s, pb->gid);
        }
        made++;
    }
    *xp = X;
    return made;
}

// do_break: the joints of the body go to the piece that holds the anchor (a mount keeps its end of a pane)
static void brk_joints(tp_world_t *w, const tp_body_t *X, const cell_t *cell, int nc, const int *slot)
{
    int je[16], nj = 0;
    for (int e = X->jhead; e >= 0 && nj < 16;) {
        je[nj++] = e;
        e = w->joint[e >> 1].next[e & 1];
    }
    for (int q = 0; q < nj; q++) {
        int ji = je[q] >> 1, sd = je[q] & 1;
        tp_joint_t *j = &w->joint[ji];
        vec la = sd ? j->lb : j->la;
        int best = -1;
        float bo = 0.03f;
        for (int i = 0; i < nc; i++) {
            if (slot[i] < 0) {
                continue;
            }
            float o = poly_out(cell[i].v, cell[i].n, la);
            if (o < bo) {
                bo = o;
                best = i;
            }
        }
        if (best < 0) {
            continue;  // (body_kill breaks it, with an event)
        }
        jlist_del(w, ji);
        if (sd) {
            j->b = (int16_t)slot[best];
            j->lb = vsub(la, cell[best].c);
        } else {
            j->a = (int16_t)slot[best];
            j->la = vsub(la, cell[best].c);
        }
        j->lin = V(0, 0);
        j->ang = j->mot = j->lo_i = j->up_i = j->imp = 0.0f;
        j->age = 0;
        jlist_add(w, ji);
    }
}

// do_break: the water of a tank, and the crumbs at the point of the hit
static void brk_spill(tp_world_t *w, const tp_body_t *X, const tp_mat_t *m, const vec *poly, int n, vec vX, float wX,
                      tp_id_t xid, vec pw, vec dir)
{
    for (int q = 0; q < X->spill; q++) {
        vec lp = rand_in_poly(w, poly, n), p = xf_apply(X->xf, lp);
        vec v = vadd(vadd(vX, vcrosssv(wX, rot(X->xf.q, lp))), V(0.8f * (rndf(w) - 0.5f), 0.8f * (rndf(w) - 0.5f)));
        tp_part_t *pp = tp_emit(w, TP_PART_WATER, p, v);
        if (!pp) {
            break;
        }
        pp->mat = TP_MAT_WATER;
        pp->ign = xid;
    }
    for (int q = 0; q < m->crumbs; q++) {
        float a = 2.0f * PI_F * rndf(w), sp = 0.6f + 1.9f * rndf(w);
        vec v = vadd(vX, vadd(vscale(sp, V(cosf(a), sinf(a))), vscale(-0.8f * sp, dir)));
        tp_part_t *pp = tp_emit(w, m->crumb_kind, pw, v);
        if (!pp) {
            break;
        }
        pp->mat = X->mat;
        pp->ign = xid;
    }
}

static bool do_break(tp_world_t *w, int xs, vec pw, vec dir, float dv, int os, uint32_t ogid, float excess)
{
    tp_body_t *X = &w->body[xs];
    const tp_mat_t *m = &w->mat[X->mat];
    vec poly[TP_MAX_VERTS];
    int n = brk_poly(X, poly);
    int k = m->pieces < 2 ? 2 : m->pieces > 8 ? 8 : m->pieces;
    if (k > w->body_cap - w->nbody) {
        k = w->body_cap - w->nbody;
    }
    if (k < 2) {
        return false;
    }
    // the point of the hit, moved into the shape
    vec P = xf_inv(X->xf, pw), Pin = vscale(0.7f, P);
    for (int it = 0; it < 8 && poly_out(poly, n, Pin) > -0.002f; it++) {
        Pin = vscale(0.6f, Pin);
    }
    // the seeds of the pieces (each piece is the part of the shape nearest to its seed)
    vec seed[8];
    int ns = brk_seeds(w, m, poly, n, k, Pin, seed);
    // the cells: the shape cut by the half planes to the other seeds (each is convex)
    cell_t cell[8];
    int nc = brk_cells(poly, n, seed, ns, cell);
    if (nc == 0) {
        return false;
    }
    // the speed of the pieces: the speed of the body, less the part of the hit that goes on with the other body
    vec vX = X->v;
    float wX = X->w;
    tp_body_t *O =
            os >= 0 && (w->body[os].flags & TP_ALIVE) && !(w->body[os].flags & TP_GONE) && w->body[os].gid == ogid
                    ? &w->body[os]
                    : NULL;
    if (X->kind == TP_STATIC) {
        vX = vscale(0.5f * fmn(dv, X->toughness), dir);
        wX = 0.0f;
    }
    if (O && O->kind == TP_DYNAMIC && excess > 0.0f) {
        float J = 0.8f * excess;
        O->v = vmadd(O->v, J * O->im, dir);  // the other body goes on through
        if (X->kind != TP_STATIC) {
            vX = vmadd(vX, -J / X->mass, dir);
        }
    }
    // the burst: out from the point of the hit (no change of the momentum)
    vec bv[8];
    vec mean = brk_burst(w, m, X, cell, nc, Pin, dv, bv);
    // the new bodies
    tp_id_t xid = mkid(xs, X->gid);
    int first = w->npiece, slot[8];
    int made = brk_pieces(w, xs, &X, m, cell, nc, vX, wX, bv, mean, O, xid, slot);
    // the joints of the body go to the piece that holds the anchor (a mount keeps its end of a pane)
    brk_joints(w, X, cell, nc, slot);
    // the water of a tank, and the crumbs at the point of the hit
    brk_spill(w, X, m, poly, n, vX, wX, xid, pw, dir);
    tp_event_t e;
    memset(&e, 0, sizeof e);
    e.kind = TP_EV_BREAK;
    e.mat = X->mat;
    e.snd = m->sound;
    e.id = xid;
    e.a = e.b = TP_NONE;
    e.user = X->user;
    e.p = pw;
    e.value = dv;
    e.first = (int16_t)first;
    e.count = (int16_t)(w->npiece - first);
    add_event(w, &e);
    body_kill(w, xs, true);
    return made > 0 || nc > 0;
}

bool tp_break(tp_world_t *w, tp_id_t id, tp_vec_t p, tp_vec_t dir)
{
    tp_body_t *b = tp_body(w, id);
    if (!b) {
        return false;
    }
    if (dir.x == 0.0f && dir.y == 0.0f) {
        dir = vsub(b->xf.p, p);
    }
    dir = vnorm(dir);
    if (dir.x == 0.0f && dir.y == 0.0f) {
        dir = V(0, -1);
    }
    float t = b->toughness > 0.0f ? b->toughness : 3.0f;
    return do_break(w, id & SLOT_MASK, p, dir, 2.0f * t, -1, 0, 0.0f);
}

static void add_hit(tp_world_t *w, const tp_hit_t *h)
{
    if (w->nhit < w->hit_cap) {
        w->hit[w->nhit++] = *h;
        return;
    }
    int wi = 0;
    for (int i = 1; i < w->nhit; i++) {
        if (w->hit[i].impulse < w->hit[wi].impulse) {
            wi = i;
        }
    }
    if (h->impulse > w->hit[wi].impulse) {
        w->hit[wi] = *h;
    }
}

static void brk_add(struct tp_in *in, int x, int o, uint32_t ogid, float dv, float excess, vec p, vec dir)
{
    for (int i = 0; i < in->nbrk; i++) {
        brk_t *r = &in->brk[i];
        if (r->x != x) {
            continue;
        }
        if (dv > r->dv) {
            r->o = (int16_t)o;
            r->ogid = ogid;
            r->dv = dv;
            r->excess = excess;
            r->p = p;
            r->dir = dir;
        }
        return;
    }
    if (in->nbrk >= BRK_CAP) {
        return;
    }
    brk_t *r = &in->brk[in->nbrk++];
    r->x = (int16_t)x;
    r->o = (int16_t)o;
    r->ogid = ogid;
    r->dv = dv;
    r->excess = excess;
    r->p = p;
    r->dir = dir;
}

// after the solver: the joints that broke, the hit events, the bodies that break
static void after_solve(tp_world_t *w)
{
    struct tp_in *in = w->in;
    float inv_h = in->inv_h;
    for (int k = 0; k < in->njl; k++) {
        int ji = in->jl[k];
        tp_joint_t *j = &w->joint[ji];
        if (!(j->flags & TPJ_ALIVE)) {
            continue;
        }
        j->force = j->fmax * inv_h;
        j->torque = j->tmax * inv_h;
        if (j->age < 255) {
            j->age++;
        }
        if (j->age <= TP_JOINT_SETTLE) {
            continue;  // (the first steps: the load comes on, a short peak)
        }
        if (j->break_force > 0.0f && j->force > j->break_force) {
            joint_break(w, ji, TP_WHY_FORCE, j->force);
        } else if (j->break_torque > 0.0f && j->torque > j->break_torque) {
            joint_break(w, ji, TP_WHY_TORQUE, j->torque);
        }
    }
    in->nbrk = 0;
    for (int k = 0; k < in->ncc; k++) {
        const cc_t *q = &in->cc[k];
        const tp_contact_t *c = &w->contact[q->c];
        const tp_body_t *A = &w->body[q->a], *B = &w->body[q->b];
        float hit = 0.0f, appr = 0.0f, wsum = 0.0f;
        for (int i = 0; i < q->np; i++) {
            hit += c->pt[i].hit;
            if (q->p[i].total > 0.0f && -q->p[i].rv > appr) {
                appr = -q->p[i].rv;
            }
        }
        if (hit <= 1e-6f) {
            continue;  // (a body at rest: no hit, no point)
        }
        vec pw = V(0, 0);
        for (int i = 0; i < q->np; i++) {
            // the point: the middle of the two points of the bodies (they were one point at the start of the step)
            float wt = c->pt[i].hit + 1e-6f;
            vec pa = vadd(A->xf.p, c->pt[i].ra), pb = vadd(B->xf.p, c->pt[i].rb);
            pw = vmadd(pw, wt, vlerp(pa, pb, 0.5f));
            wsum += wt;
        }
        pw = vscale(1.0f / wsum, pw);
        if (appr >= w->hit_min) {
            tp_hit_t h;
            h.a = mkid(q->a, A->gid);
            h.b = mkid(q->b, B->gid);
            h.p = pw;
            h.n = q->n;
            h.impulse = hit;
            h.speed = appr;
            h.mat_a = A->mat;
            h.mat_b = B->mat;
            h.snd_a = w->mat[A->mat].sound;
            h.snd_b = w->mat[B->mat].sound;
            h.begin = q->ni0 == 0.0f;
            add_hit(w, &h);
        }
        for (int sd = 0; sd < 2; sd++) {
            const tp_body_t *X = sd ? B : A, *O = sd ? A : B;
            if (X->toughness <= 0.0f || X->life0 > 0.0f || X->gen >= 2 || !(X->mass > 0.0f)) {
                continue;
            }
            float dv = hit / X->mass;
            if (dv <= X->toughness) {
                continue;
            }
            brk_add(in, sd ? q->b : q->a, sd ? q->a : q->b, O->gid, dv, hit - X->toughness * X->mass, pw,
                    sd ? q->n : vneg(q->n));
        }
    }
    // the breaks: the strongest first (then the lower slot)
    for (int i = 1; i < in->nbrk; i++) {
        for (int j = i; j > 0; j--) {
            brk_t *a = &in->brk[j - 1], *b = &in->brk[j];
            if (b->dv > a->dv || (b->dv == a->dv && b->x < a->x)) {
                brk_t t = *a;
                *a = *b;
                *b = t;
            } else {
                break;
            }
        }
    }
    int nb = 0;
    for (int i = 0; i < in->nbrk && nb < w->max_breaks; i++) {
        brk_t *r = &in->brk[i];
        tp_body_t *X = &w->body[r->x];
        if (!(X->flags & TP_ALIVE) || (X->flags & TP_GONE)) {
            continue;
        }
        if (do_break(w, r->x, r->p, r->dir, r->dv, r->o, r->ogid, r->excess)) {
            nb++;
        }
    }
    w->stat.breaks = nb;
}

static void sleep_islands(tp_world_t *w)
{
    struct tp_in *in = w->in;
    uint32_t now = w->stepno;
    if (in->dirty) {
        return;  // (a body went away: the islands are not right now)
    }
    for (int k = 0; k < in->nisl; k++) {
        float mt = FLT_MAX;
        for (int s = in->isl_head[k]; s >= 0; s = w->body[s].inext) {
            const tp_body_t *b = &w->body[s];
            float t = b->mark == now ? 0.0f : b->sleep_t;
            if (t < mt) {
                mt = t;
            }
        }
        if (mt < w->sleep_time) {
            continue;
        }
        for (int s = in->isl_head[k]; s >= 0; s = w->body[s].inext) {
            tp_body_t *b = &w->body[s];
            b->flags &= (uint8_t)~TP_AWAKE;
            b->v = V(0, 0);
            b->w = 0.0f;
            b->force = V(0, 0);
            b->torque = 0.0f;
        }
    }
}

static void life_bounds(tp_world_t *w)
{
    float dt = w->dt;
    tp_box_t B = w->bounds;
    for (int s = 0; s < w->body_hi; s++) {
        tp_body_t *b = &w->body[s];
        if (!(b->flags & TP_ALIVE)) {
            continue;
        }
        if (b->life0 > 0.0f) {
            b->life -= dt;
            if (b->life <= 0.0f) {
                body_gone(w, s, TP_GONE_FADED);
                continue;
            }
        }
        if (b->kind != TP_STATIC &&
            (b->box.hi.x < B.lo.x || b->box.lo.x > B.hi.x || b->box.hi.y < B.lo.y || b->box.lo.y > B.hi.y)) {
            body_gone(w, s, TP_GONE_OUT);
        }
    }
}

void tp_step(tp_world_t *w)
{
    struct tp_in *in = w->in;
    w->nhit = w->nev = w->npiece = 0;
    w->stepno++;
    in->dirty = false;
    memset(&w->stat, 0, sizeof w->stat);
    TP_PROF(0);
    broad_phase(w);
    TP_PROF(1);
    narrow_phase(w);
    TP_PROF(2);
    islands(w);
    TP_PROF(3);
    solve(w);
    TP_PROF(4);
    after_solve(w);
    sleep_islands(w);
    TP_PROF(5);
    parts_step(w);
    TP_PROF(6);
    life_bounds(w);
    TP_PROF(7);
}

// ---------------------------------------------------------------- the queries
bool tp_ray(tp_world_t *w, tp_vec_t p0, tp_vec_t p1, uint16_t mask, tp_ray_t *out)
{
    float bt = 2.0f;
    vec lo = V(fmn(p0.x, p1.x), fmn(p0.y, p1.y)), hi = V(fmx(p0.x, p1.x), fmx(p0.y, p1.y));
    out->id = TP_NONE;
    out->t = 1.0f;
    out->p = p1;
    out->n = V(0, 0);
    for (int s = 0; s < w->body_hi; s++) {
        const tp_body_t *b = &w->body[s];
        if (!(b->flags & TP_ALIVE) || !(b->cat & mask)) {
            continue;
        }
        if (b->box.lo.x > hi.x || b->box.hi.x < lo.x || b->box.lo.y > hi.y || b->box.hi.y < lo.y) {
            continue;
        }
        float t, depth;
        vec n;
        bool inside;
        if (!seg_shape(b, p0, p1, 0.0f, &t, &n, &inside, &depth) || inside) {
            continue;
        }
        if (t < bt) {
            bt = t;
            out->id = mkid(s, b->gid);
            out->t = t;
            out->n = n;
            out->p = vlerp(p0, p1, t);
        }
    }
    return out->id != TP_NONE;
}

tp_id_t tp_point(tp_world_t *w, tp_vec_t p, float r, uint16_t mask)
{
    int best = -1, bstat = 1;
    float bo = FLT_MAX;
    for (int s = 0; s < w->body_hi; s++) {
        const tp_body_t *b = &w->body[s];
        if (!(b->flags & TP_ALIVE) || !(b->cat & mask)) {
            continue;
        }
        if (p.x < b->box.lo.x - r || p.x > b->box.hi.x + r || p.y < b->box.lo.y - r || p.y > b->box.hi.y + r) {
            continue;
        }
        float o;
        if (b->shape == TP_CIRCLE) {
            o = vlen(vsub(p, b->xf.p)) - b->radius;
        } else {
            vec lp = xf_inv(b->xf, p);
            o = -FLT_MAX;
            for (int i = 0; i < b->n; i++) {
                o = fmx(o, vdot(b->nl[i], vsub(lp, b->vl[i])));
            }
        }
        if (o > r) {
            continue;
        }
        int st = b->kind == TP_STATIC;
        if (best < 0 || st < bstat || (st == bstat && o < bo)) {
            best = s;
            bstat = st;
            bo = o;
        }
    }
    return best < 0 ? TP_NONE : mkid(best, w->body[best].gid);
}

int tp_query_box(tp_world_t *w, tp_box_t box, uint16_t mask, tp_id_t *out, int max)
{
    int n = 0;
    for (int s = 0; s < w->body_hi && n < max; s++) {
        const tp_body_t *b = &w->body[s];
        if (!(b->flags & TP_ALIVE) || !(b->cat & mask)) {
            continue;
        }
        if (b->box.lo.x > box.hi.x || b->box.hi.x < box.lo.x || b->box.lo.y > box.hi.y || b->box.hi.y < box.lo.y) {
            continue;
        }
        out[n++] = mkid(s, b->gid);
    }
    return n;
}
