// GLASSHOUSE: the physics world of a level (libs/tfphys). It makes the bodies and the joints of a level, does the
// moves, keeps the glass panes of the cells, the fragile object, the water, the motors and the winches, and finds how
// much of the miniature is down. All the state that changes the physics is in G and in the memory block of the world.
// The world does not step before the first move: a new build of the level is the world at the first move, so the
// replay builds the level again and does the same moves at the same steps (the same result).
// Included by app_glass.c.

#define PXM      40.0f   // world px in one metre
#define FLOOR    139.0f  // the top of the stand (world px)
#define STAND_X0 12.0f   // the stand: its two ends (world px)
#define STAND_X1 628.0f
#define DT_STEP  (1.0f / 60.0f)
static inline float m_x(float px)
{
    return px / PXM;
}
static inline float m_y(float py)
{
    return (FLOOR - py) / PXM;
}
static inline float px_x(float mx)
{
    return mx * PXM;
}
static inline float px_y(float my)
{
    return FLOOR - my * PXM;
}

// ---------------------------------------------------------------- the fragile objects
enum {
    OB_ORCHID,
    OB_EGG,
    OB_BIRD,
    OB_SWAN,
    OB_VASE,
    OB_CANDLE,
    OB_GLOBE,
    OB_TEACUP,
    OB_FISH,
    OB_N
};
typedef struct {
    const char *key;
    const char *name[2];
    float w, h;    // the object (px)
    float sw, sh;  // its stand (px; 0 = none: it stands on the ground or on a part)
    float tough;   // m/s: a hit with more change of speed breaks it
    float dens;
    bool wet;                        // water spoils it
    const char *safe[2], *broke[2];  // the title of the end card
} objk_t;
static const objk_t OBK[OB_N] = {
        {"orchid",
         {"THE ORCHID", "הסחלב"},
         18,
         27,
         26,
         22,
         1.5f,
         1.2f,
         false,
         {"THE ORCHID IS SAFE", "הסחלב שלם"},
         {"THE ORCHID BROKE", "הסחלב נשבר"}},
        {"egg",
         {"THE EGG", "הביצה"},
         9,
         12,
         12,
         9,
         1.1f,
         1.2f,
         false,
         {"THE EGG IS SAFE", "הביצה שלמה"},
         {"THE EGG BROKE", "הביצה נשברה"}},
        {"bird",
         {"THE PORCELAIN BIRD", "ציפור החרסינה"},
         16,
         15,
         10,
         20,
         1.3f,
         1.4f,
         false,
         {"THE BIRD IS SAFE", "הציפור שלמה"},
         {"THE BIRD BROKE", "הציפור נשברה"}},
        {"swan",
         {"THE GLASS SWAN", "ברבור הזכוכית"},
         20,
         17,
         22,
         8,
         1.3f,
         1.8f,
         false,
         {"THE SWAN IS SAFE", "הברבור שלם"},
         {"THE SWAN BROKE", "הברבור נשבר"}},
        {"vase",
         {"THE CHINA VASE", "אגרטל החרסינה"},
         14,
         24,
         18,
         6,
         1.4f,
         1.6f,
         false,
         {"THE VASE IS SAFE", "האגרטל שלם"},
         {"THE VASE BROKE", "האגרטל נשבר"}},
        {"candle",
         {"THE CANDLE", "הנר"},
         10,
         22,
         14,
         6,
         1.6f,
         1.6f,
         true,
         {"THE CANDLE BURNS ON", "הנר דולק"},
         {"THE CANDLE WENT OUT", "הנר כבה"}},
        {"globe",
         {"THE SNOW GLOBE", "כדור השלג"},
         16,
         18,
         16,
         6,
         1.5f,
         2.0f,
         false,
         {"THE GLOBE IS SAFE", "כדור השלג שלם"},
         {"THE GLOBE BROKE", "כדור השלג נשבר"}},
        {"teacup",
         {"THE TEACUP", "ספל התה"},
         15,
         10,
         20,
         3,
         1.2f,
         1.4f,
         false,
         {"THE CUP IS SAFE", "הספל שלם"},
         {"THE CUP BROKE", "הספל נשבר"}},
        {"fish",
         {"THE GOLDFISH BOWL", "צנצנת דג הזהב"},
         20,
         17,
         22,
         12,
         1.4f,
         2.2f,
         false,
         {"THE BOWL IS SAFE", "הצנצנת שלמה"},
         {"THE BOWL BROKE", "הצנצנת נשברה"}},
};
static int obj_kind_of(const char *s)
{
    for (int i = 0; i < OB_N; i++) {
        if (!strcmp(OBK[i].key, s)) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- the state of a level
enum {
    PS_STAND,
    PS_DOWN,
    PS_BROKEN,
    PS_GONE
};
typedef struct {
    tp_id_t id;        // the body (TP_NONE: it broke or went away)
    uint8_t st;        // PS_*
    float x0, y0, a0;  // the place after the settle (px, screen rad)
    float h0;          // the height of its middle over the floor (px)
    float worth;       // its area (px2); 0 = it does not count
    uint8_t npane;
    uint8_t sub[NSUB];         // the glass panes: 0 whole, 1 cracked, 2 broken
    float crack_t[NSUB];       // the time of the crack (s of the level; -1 = none)
    float shatter_at[NSUB];    // a cracked pane breaks at this time (0 = only on a new hit)
    float cx[NSUB], cy[NSUB];  // the point of the crack (local px of the part)
    uint16_t cseed[NSUB];
    float water;  // a bucket: the water in it (kg); a tank: its drops left
} prt_t;
typedef struct {
    tp_id_t id;    // TP_NONE: cut, pulled or broken
    float snap_t;  // the time it went (s; 0 = it is there)
} jrt_t;
typedef struct {
    bool on;
    float left, acc;  // a pipe: the time left, the drops to emit
    float len;        // a winch: the length of its rope now (px)
} ert_t;
enum {
    OS_WHOLE,
    OS_BROKEN
};
typedef struct {
    tp_id_t id, stand;
    uint8_t st;
    float hurt;    // 0..1: the biggest hit so far / its toughness (the cracks on it)
    float risk_t;  // the last time that a hit or a near body put it at risk (s)
    float wet;     // the drops on it (a water object)
    float x, y;    // its place now (px), for the camera
} ort_t;
enum {
    LV_AIM,
    LV_ACT,
    LV_WON,
    LV_LOST,
    LV_STILL
};
typedef struct {
    uint32_t step;  // the physics steps since the start of the level (after the settle)
    uint32_t rng;   // the random numbers of the game (the shards)
    prt_t p[MAXP];
    jrt_t j[MAXJ];
    ert_t e[MAXE];
    ort_t o;
    uint8_t used[MAXH];  // the moves that are done
    int8_t queue[4];     // the moves that wait for the next step
    int nqueue;
    uint8_t moves_left, state;
    uint32_t first_step, last_move_step, quiet_steps, settle_step, end_step;
    uint32_t last_event;  // the last step with a hit, a break or a snap (the end of a won run)
    uint16_t moves_done;
    float destroy, goal_now;  // 0..1
    bool goal_met, any_move;
    int panes_broken;
    // the record of the moves (for the replay): the step after the first move and the move
    uint32_t rec_step[MAXH];
    int8_t rec_h[MAXH];
    int nrec;
    // the camera hints of the run (for the replay): the first break, the biggest hit
    uint32_t first_break, big_step;
    float big_v, big_x, big_y;
} gstate_t;
static gstate_t G;
static level_t *LV;  // the level now (PSRAM)
static tp_world_t *W;
static void *s_wmem;  // the memory block of the world (W is in it)
static size_t s_wsize;
static bool s_pre;              // the settle at the start: no effects, no damage
static uint8_t s_mat_of[MAXP];  // the material of each part in the world

// ---------------------------------------------------------------- the events for the picture and the sound
enum {
    FX_HIT,
    FX_SHATTER,
    FX_CRACK,
    FX_BREAK,
    FX_SNAP,
    FX_SPLASH,
    FX_TINKLE,
    FX_MOVE,
    FX_OBJ_HIT,
    FX_OBJ_BREAK,
    FX_CREAK,
    FX_MOTOR,
    FX_N
};
typedef struct {
    uint8_t kind, mat, extra;
    float x, y, v;
} fx_t;
#define FXQ 48
static fx_t s_fx[FXQ];
static int s_nfx;
static void fx_add(int kind, float x, float y, float v, int mat, int extra)
{
    if (s_pre) {
        return;
    }
    if (s_nfx >= FXQ) {
        // full: replace the weakest event of the same kind
        int wi = -1;
        float wv = v;
        for (int i = 0; i < FXQ; i++) {
            if (s_fx[i].kind == kind && s_fx[i].v < wv) {
                wv = s_fx[i].v;
                wi = i;
            }
        }
        if (wi < 0) {
            return;
        }
        s_fx[wi] = (fx_t){(uint8_t)kind, (uint8_t)mat, (uint8_t)extra, x, y, v};
        return;
    }
    s_fx[s_nfx++] = (fx_t){(uint8_t)kind, (uint8_t)mat, (uint8_t)extra, x, y, v};
}
static inline float g_rnd(void)
{
    G.rng ^= G.rng << 13;
    G.rng ^= G.rng >> 17;
    G.rng ^= G.rng << 5;
    return (float)(G.rng >> 8) / 16777216.0f;
}

// ---------------------------------------------------------------- the materials of a level
enum {
    MB_GLASS,
    MB_WOOD,
    MB_IRON,
    MB_STONE,
    MB_FRAME,
    MB_WATERTANK,
    MB_OBJECT
};
static int s_nmat;
static struct {
    uint8_t base;
    float dens, fric, tough;
} s_matk[TP_MAT_MAX];
// the material slot of the world for this kind of part (with its density and friction)
static int mat_slot(int base, float dens, float fric, float tough)
{
    for (int i = 7; i < s_nmat; i++) {
        if (s_matk[i].base == base && s_matk[i].dens == dens && s_matk[i].fric == fric && s_matk[i].tough == tough) {
            return i;
        }
    }
    if (s_nmat >= TP_MAT_MAX) {
        return base == MB_GLASS   ? TP_MAT_GLASS
               : base == MB_STONE ? TP_MAT_STONE
               : base == MB_WOOD  ? TP_MAT_WOOD
                                  : TP_MAT_METAL;
    }
    int i = s_nmat++;
    s_matk[i].base = (uint8_t)base;
    s_matk[i].dens = dens;
    s_matk[i].fric = fric;
    s_matk[i].tough = tough;
    tp_mat_t *m = &W->mat[i];
    switch (base) {
    case MB_GLASS:
        *m = W->mat[TP_MAT_GLASS];
        m->pieces = 5;
        m->crumbs = 8;
        break;
    case MB_WOOD:
        *m = W->mat[TP_MAT_WOOD];
        break;
    case MB_STONE:
        *m = W->mat[TP_MAT_STONE];
        m->pieces = 4;
        m->pattern = TP_BREAK_CHUNKS;
        break;
    case MB_FRAME:
        *m = W->mat[TP_MAT_METAL];
        m->toughness = 0;
        m->restitution = 0.05f;
        m->sound = TP_SND_GLASS;
        break;
    case MB_WATERTANK:
        *m = W->mat[TP_MAT_WOOD];
        m->pieces = 3;
        m->crumbs = 6;
        break;
    case MB_OBJECT:
        *m = W->mat[TP_MAT_GLASS];
        m->pieces = 6;
        m->crumbs = 10;
        m->toughness = 0;
        break;
    default:
        *m = W->mat[TP_MAT_METAL];
        m->restitution = 0.15f;
        break;
    }
    if (dens > 0) {
        m->density = dens;
    }
    if (fric >= 0) {
        m->friction = fric;
    }
    if (tough >= 0) {
        m->toughness = tough;
    }
    return i;
}
// the base material, density, friction and toughness of a kind of part
static void part_mat(const pdef_t *P, int *base, float *dens, float *fric, float *tough)
{
    float d = -1, f = -1, t = -1;
    int b = MB_WOOD;
    switch (P->kind) {
    case PK_CELL:
    case PK_ROOF:
        b = MB_FRAME;
        d = 0.55f;
        f = 0.5f;
        t = 0;
        break;
    case PK_POST:
    case PK_GIRDER:
        b = MB_IRON;
        d = 2.4f;
        f = 0.45f;
        t = 0;
        break;
    case PK_BEAM:
    case PK_PLANK:
    case PK_CRATE:
        b = MB_WOOD;
        d = 0.75f;
        f = 0.6f;
        t = 7;
        break;
    case PK_TANK:
        b = MB_WATERTANK;
        d = 1.5f;
        f = 0.6f;
        t = 5;
        break;
    case PK_BALL:
    case PK_WEIGHT:
        b = MB_IRON;
        d = 7.8f;
        f = 0.35f;
        t = 0;
        break;
    case PK_BELL:
        b = MB_IRON;
        d = 6.0f;
        f = 0.35f;
        t = 0;
        break;
    case PK_POT:
        b = MB_STONE;
        d = 1.8f;
        f = 0.7f;
        t = 2.6f;
        break;
    case PK_SACK:
        b = MB_WOOD;
        d = 1.4f;
        f = 0.9f;
        t = 0;
        break;
    case PK_BRICK:
    case PK_SLAB:
        b = MB_STONE;
        d = 2.0f;
        f = 0.75f;
        t = 6;
        break;
    case PK_STONE:
        b = MB_STONE;
        d = 2.4f;
        f = 0.8f;
        t = 0;
        break;
    case PK_PANE:
        b = MB_GLASS;
        d = 2.5f;
        f = 0.4f;
        t = 2.0f;
        break;
    case PK_BOTTLE:
        b = MB_GLASS;
        d = 1.6f;
        f = 0.4f;
        t = 1.8f;
        break;
    case PK_LENS:
        b = MB_GLASS;
        d = 2.5f;
        f = 0.4f;
        t = 2.4f;
        break;
    case PK_WHEEL:
        b = MB_WOOD;
        d = 0.9f;
        f = 0.6f;
        t = 0;
        break;
    case PK_BUCKET:
        b = MB_IRON;
        d = 0.9f;
        f = 0.5f;
        t = 0;
        break;
    case PK_LAMP:
        b = MB_GLASS;
        d = 1.4f;
        f = 0.4f;
        t = 1.6f;
        break;
    default:
        break;
    }
    if (P->dens > 0) {
        d = P->dens;
    }
    if (P->fric >= 0) {
        f = P->fric;
    }
    if (P->tough >= 0) {
        t = P->tough;
    }
    *base = b;
    *dens = d;
    *fric = f;
    *tough = t;
}

// ---------------------------------------------------------------- the places
static tp_body_t *part_body(int i)
{
    return i >= 0 && i < LV->np ? tp_body(W, G.p[i].id) : NULL;
}
// a body: its middle (px) and its screen angle (rad, + = clockwise)
static void body_px(const tp_body_t *b, float *x, float *y, float *a)
{
    *x = px_x(b->xf.p.x);
    *y = px_y(b->xf.p.y);
    if (a) {
        *a = -atan2f(b->xf.q.s, b->xf.q.c);
    }
}
// a local point of a body (px, y down, from its middle) to the world (px)
static inline void loc2w(const tp_body_t *b, float lx, float ly, float *wx, float *wy)
{
    float c = b->xf.q.c, s = b->xf.q.s;
    *wx = px_x(b->xf.p.x) + c * lx + s * ly;
    *wy = px_y(b->xf.p.y) - s * lx + c * ly;
}
static inline void w2loc(const tp_body_t *b, float wx, float wy, float *lx, float *ly)
{
    float c = b->xf.q.c, s = b->xf.q.s, dx = wx - px_x(b->xf.p.x), dy = wy - px_y(b->xf.p.y);
    *lx = c * dx - s * dy;
    *ly = s * dx + c * dy;
}
// the glass panes of a cell: the box of pane k in the local px of the part (the rim and the bars between them)
#define RIM  1.4f
#define MULL 0.9f
static void pane_box(const pdef_t *P, int k, float *x0, float *y0, float *x1, float *y1)
{
    int nx = P->nx, ny = P->ny, ix = k % nx, iy = k / nx;
    float iw = P->w - 2 * RIM, ih = P->h - 2 * RIM;
    float pw = (iw - (float)(nx - 1) * MULL) / (float)nx, ph = (ih - (float)(ny - 1) * MULL) / (float)ny;
    *x0 = -P->w / 2 + RIM + (float)ix * (pw + MULL);
    *y0 = -P->h / 2 + RIM + (float)iy * (ph + MULL);
    *x1 = *x0 + pw;
    *y1 = *y0 + ph;
}
static int pane_at(const pdef_t *P, float lx, float ly)
{
    int best = 0;
    float bd = 1e9f;
    for (int k = 0; k < P->nx * P->ny; k++) {
        float x0, y0, x1, y1;
        pane_box(P, k, &x0, &y0, &x1, &y1);
        float dx = lx < x0 ? x0 - lx : lx > x1 ? lx - x1 : 0, dy = ly < y0 ? y0 - ly : ly > y1 ? ly - y1 : 0;
        float d = dx * dx + dy * dy;
        if (d < bd) {
            bd = d;
            best = k;
        }
    }
    return best;
}
static bool is_cell(int i)
{
    return i >= 0 && i < LV->np && (LV->p[i].kind == PK_CELL || LV->p[i].kind == PK_ROOF);
}

// ---------------------------------------------------------------- the build of a level
static void w_free(void)
{
    if (s_wmem) {
        heap_caps_free(s_wmem);
    }
    s_wmem = NULL;
    W = NULL;
}
static tp_world_def_t w_def(void)
{
    tp_world_def_t d = tp_world_defaults();
    d.bounds.lo = (tp_vec_t){-6, -6};
    d.bounds.hi = (tp_vec_t){22, 12};
    // (the peaks of the 8 levels in the level check: 44 bodies, 255 contacts, 90 particles, 27 hits, 3 events)
    d.max_bodies = 180;
    d.max_joints = MAXJ + 8;
    d.max_contacts = 480;
    d.max_parts = 360;
    d.max_hits = 64;
    d.max_events = 96;
    d.substeps = 3;
    d.cell = 0.5f;
    return d;
}
static bool w_alloc(void)
{
    if (s_wmem) {
        return true;
    }
    tp_world_def_t d = w_def();
    s_wsize = tp_mem_size(&d);
    s_wmem = heap_caps_malloc(s_wsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return s_wmem != NULL;
}
// a pane breaks: its shards (gl_view.inc), its rope, the cracks of its neighbours
static void pane_shatter(int pi, int k, float push_x, float push_y);
static void shard_burst(int pi, int k, const tp_body_t *b, float x0, float y0, float x1, float y1, float wcx, float wcy,
                        float push_x, float push_y);
static void pane_crack(int pi, int k, float lx, float ly, float delay)
{
    prt_t *R = &G.p[pi];
    if (R->sub[k] != 0) {
        return;
    }
    R->sub[k] = 1;
    R->crack_t[k] = (float)G.step * DT_STEP;
    R->shatter_at[k] = delay > 0 ? R->crack_t[k] + delay : 0;
    R->cx[k] = lx;
    R->cy[k] = ly;
    R->cseed[k] = (uint16_t)(g_rnd() * 65535);
    tp_body_t *b = part_body(pi);
    if (b) {
        float wx, wy;
        loc2w(b, lx, ly, &wx, &wy);
        fx_add(FX_CRACK, wx, wy, 1, MB_GLASS, 0);
    }
}

static int add_part(int i)
{
    const pdef_t *P = &LV->p[i];
    int base;
    float dens, fric, tough;
    part_mat(P, &base, &dens, &fric, &tough);
    int ms = mat_slot(base, dens, fric, base == MB_FRAME || P->kind == PK_BALL ? 0 : tough);
    s_mat_of[i] = (uint8_t)ms;
    int kind = (P->flags & PF_STATIC) ? TP_STATIC : TP_DYNAMIC;
    tp_body_def_t d = tp_body_def(kind, m_x(P->x), m_y(P->y), ms);
    d.angle = -P->a;
    d.toughness = -1;
    d.fixed_rot = (P->flags & PF_FIXROT) != 0;
    d.spill = P->spill;
    d.user = i;
    d.ldamp = P->damp > 0 ? P->damp : 0.02f;
    d.adamp = P->damp > 0 ? P->damp : 0.05f;
    tp_id_t id = TP_NONE;
    if (P->shape == SH_CIRCLE) {
        id = tp_add_circle(W, &d, m_x(P->w / 2));
    } else if (P->shape == SH_POLY) {
        tp_vec_t pts[8];
        for (int q = 0; q < P->npt; q++) {
            pts[q] = (tp_vec_t){m_x(P->pt[2 * q]) - d.pos.x, m_y(P->pt[2 * q + 1]) - d.pos.y};
        }
        d.angle = 0;
        id = tp_add_poly(W, &d, pts, P->npt);
    } else {
        id = tp_add_box(W, &d, m_x(P->w / 2), m_x(P->h / 2));
    }
    G.p[i].id = id;
    return id == TP_NONE ? -1 : 0;
}
static tp_id_t body_of(int part)
{
    return part < 0 ? TP_NONE : G.p[part].id;
}
static tp_id_t s_ground;
static void add_joint(int k)
{
    const jdef_t *J = &LV->j[k];
    tp_id_t a = J->a < 0 ? s_ground : body_of(J->a), b = J->b < 0 ? s_ground : body_of(J->b);
    G.j[k].id = TP_NONE;
    if (a == TP_NONE || b == TP_NONE) {
        return;
    }
    tp_vec_t pa = {m_x(J->x1), m_y(J->y1)}, pb = {m_x(J->x2), m_y(J->y2)};
    tp_joint_def_t d;
    switch (J->kind) {
    case J_WELD:
        d = tp_weld_def(a, b, pa);
        break;
    case J_HINGE:
    case J_MOTOR:
        d = tp_revolute_def(a, b, pa);
        d.friction = J->fric > 0 ? J->fric : 0.02f;
        if (J->limit) {
            d.limit = true;
            d.lower = J->lo;
            d.upper = J->hi;
        }
        break;
    case J_ROPE: {
        float dx = J->x2 - J->x1, dy = J->y2 - J->y1, len = J->len > 0 ? J->len : sqrtf(dx * dx + dy * dy);
        d = tp_rope_def(a, b, pa, pb, m_x(len));
        break;
    }
    default:
        d = tp_distance_def(a, b, pa, pb);
        break;
    }
    d.break_force = J->brk;
    d.break_torque = J->brkt;
    d.user = k;
    G.j[k].id = tp_add_joint(W, &d);
}

// one game step before tp_step: the moves, the pipes, the winches, the buckets, the cracks that break now
static void w_do_move(int h);
static void pre_step(void)
{
    for (int q = 0; q < G.nqueue; q++) {
        w_do_move(G.queue[q]);
    }
    G.nqueue = 0;
    float now = (float)G.step * DT_STEP;
    // the pipes and the winches
    for (int i = 0; i < LV->ne; i++) {
        const edef_t *E = &LV->e[i];
        ert_t *R = &G.e[i];
        if (!R->on) {
            continue;
        }
        if (E->kind == 0) {
            R->left -= DT_STEP;
            if (R->left <= 0) {
                R->on = false;
                continue;
            }
            R->acc += E->rate * DT_STEP;
            float l = sqrtf(E->dx * E->dx + E->dy * E->dy) + 1e-6f;
            while (R->acc >= 1) {
                R->acc -= 1;
                float sp = 2.2f + 0.4f * g_rnd(), jx = (g_rnd() - 0.5f) * 0.3f;
                tp_vec_t v = {E->dx / l * sp + jx, -E->dy / l * sp + jx};
                tp_part_t *pp = tp_emit(W, TP_PART_WATER, (tp_vec_t){m_x(E->x + (g_rnd() - 0.5f) * 2), m_y(E->y)}, v);
                if (pp) {
                    pp->mat = TP_MAT_WATER;
                }
            }
        } else {
            tp_joint_t *j = tp_joint(W, G.j[E->rope].id);
            if (!j) {
                R->on = false;
                continue;
            }
            R->len -= E->speed * DT_STEP;
            if (R->len <= E->min) {
                R->len = E->min;
                R->on = false;
            }
            j->maxlen = m_x(R->len);  // (the rope is shorter: the solver pulls the load up)
            tp_wake(W, j->a >= 0 ? tp_body_id(W, j->a) : TP_NONE);
            tp_wake(W, j->b >= 0 ? tp_body_id(W, j->b) : TP_NONE);
        }
    }
    // the buckets: the drops that come into a bucket stay there (their weight pulls the bucket down)
    for (int i = 0; i < LV->np; i++) {
        if (LV->p[i].kind != PK_BUCKET) {
            continue;
        }
        tp_body_t *b = part_body(i);
        if (!b) {
            continue;
        }
        const pdef_t *P = &LV->p[i];
        float bx, by;
        body_px(b, &bx, &by, NULL);
        for (int q = 0; q < W->npart; q++) {
            tp_part_t *pp = &W->part[q];
            if (pp->kind != TP_PART_WATER || pp->life <= 0) {
                continue;
            }
            float lx, ly;
            w2loc(b, px_x(pp->p.x), px_y(pp->p.y), &lx, &ly);
            if (fabsf(lx) < P->w / 2 - 1.5f && ly > -P->h / 2 - 3 && ly < P->h / 2 - 1.5f) {
                pp->life = 0;
                G.p[i].water += 0.012f;
            }
        }
        if (G.p[i].water > 0) {
            tp_force(W, G.p[i].id, (tp_vec_t){0, -G.p[i].water * 9.8f}, b->xf.p);
            tp_wake(W, G.p[i].id);
        }
    }
    // the tanks with an open top pour when they lean
    for (int i = 0; i < LV->np; i++) {
        if (LV->p[i].kind != PK_TANK || G.p[i].water < 1) {
            continue;
        }
        tp_body_t *b = part_body(i);
        if (!b) {
            continue;
        }
        float ang = fabsf(atan2f(b->xf.q.s, b->xf.q.c));
        if (ang < 0.45f) {
            continue;
        }
        int n = ang > 1.0f ? 3 : 1;
        const pdef_t *P = &LV->p[i];
        float side = atan2f(b->xf.q.s, b->xf.q.c) > 0 ? -1.0f : 1.0f;  // (the lip that is lower)
        for (int q = 0; q < n && G.p[i].water >= 1; q++) {
            float wx, wy;
            loc2w(b, side * (P->w / 2 - 2) * (0.6f + 0.4f * g_rnd()), -P->h / 2 + 1, &wx, &wy);
            tp_vec_t v = b->v;
            v.x += side * cosf(ang) * 0.8f;
            v.y += 0.4f;
            tp_part_t *pp = tp_emit(W, TP_PART_WATER, (tp_vec_t){m_x(wx), m_y(wy)}, v);
            if (pp) {
                pp->mat = TP_MAT_WATER;
            }
            G.p[i].water -= 1;
        }
    }
    // the cracked panes that break now
    for (int i = 0; i < LV->np; i++) {
        if (!is_cell(i) || G.p[i].id == TP_NONE) {
            continue;
        }
        prt_t *R = &G.p[i];
        for (int k = 0; k < R->npane; k++) {
            if (R->sub[k] == 1 && R->shatter_at[k] > 0 && now >= R->shatter_at[k]) {
                pane_shatter(i, k, 0, 0);
            }
        }
    }
}

static void pane_shatter(int pi, int k, float push_x, float push_y)
{
    prt_t *R = &G.p[pi];
    const pdef_t *P = &LV->p[pi];
    if (R->sub[k] == 2) {
        return;
    }
    if (R->sub[k] == 0) {
        R->cx[k] = 0;
        R->cy[k] = 0;
        R->cseed[k] = (uint16_t)(g_rnd() * 65535);
        R->crack_t[k] = (float)G.step * DT_STEP;
    }
    R->sub[k] = 2;
    G.panes_broken++;
    if (!G.first_break) {
        G.first_break = G.step;
    }
    tp_body_t *b = part_body(pi);
    if (!b) {
        return;
    }
    float x0, y0, x1, y1;
    pane_box(P, k, &x0, &y0, &x1, &y1);
    float area = (x1 - x0) * (y1 - y0);
    // the point of the crack: the middle of the burst
    float cx = R->cx[k], cy = R->cy[k];
    if (cx < x0 || cx > x1 || cy < y0 || cy > y1) {
        cx = (x0 + x1) / 2;
        cy = (y0 + y1) / 2;
    }
    float wcx, wcy;
    loc2w(b, cx, cy, &wcx, &wcy);
    // the shards fall in front of the frame to the floor (pictures only: they do not push the bodies)
    if (!s_pre) {
        shard_burst(pi, k, b, x0, y0, x1, y1, wcx, wcy, push_x, push_y);
    }
    fx_add(FX_SHATTER, wcx, wcy, area, MB_GLASS, (uint8_t)pi);
    // a rope that is tied to this pane goes
    for (int j = 0; j < LV->nj; j++) {
        if (LV->j[j].kind != J_ROPE || LV->j[j].sub != k || LV->j[j].a != pi || G.j[j].id == TP_NONE) {
            continue;
        }
        tp_joint_remove(W, G.j[j].id);
        G.j[j].id = TP_NONE;
        G.j[j].snap_t = (float)G.step * DT_STEP;
        fx_add(FX_SNAP, wcx, wcy, 1, MB_WOOD, (uint8_t)j);
    }
    // the cracks spread: the neighbour panes of the cell crack, some break later
    for (int q = 0; q < R->npane; q++) {
        if (q == k || R->sub[q] != 0) {
            continue;
        }
        int dx = abs(q % P->nx - k % P->nx), dy = abs(q / P->nx - k / P->nx);
        if (dx + dy != 1 || g_rnd() > 0.55f) {
            continue;
        }
        float qx0, qy0, qx1, qy1;
        pane_box(P, q, &qx0, &qy0, &qx1, &qy1);
        float lx = dx ? (q % P->nx > k % P->nx ? qx0 + 0.5f : qx1 - 0.5f) : (qx0 + qx1) / 2;
        float ly = dy ? (q / P->nx > k / P->nx ? qy0 + 0.5f : qy1 - 0.5f) : (qy0 + qy1) / 2;
        pane_crack(pi, q, lx, ly, g_rnd() < 0.4f ? 0.4f + 0.8f * g_rnd() : 0);
    }
}

// a hit on the frame of a cell: the pane under the point cracks or breaks
#define GLASS_BREAK 1.6f
#define GLASS_CRACK 0.8f
static void cell_hit(int pi, float wx, float wy, float dv, float nx, float ny)
{
    prt_t *R = &G.p[pi];
    tp_body_t *b = part_body(pi);
    if (!b) {
        return;
    }
    float lx, ly;
    w2loc(b, wx, wy, &lx, &ly);
    int k = pane_at(&LV->p[pi], lx, ly);
    if (dv > GLASS_BREAK || (R->sub[k] == 1 && dv > GLASS_CRACK * 0.7f)) {
        if (R->sub[k] == 0) {
            R->cx[k] = lx;
            R->cy[k] = ly;
        }
        pane_shatter(pi, k, nx * 0.5f, ny * 0.5f);
        // a hard hit breaks the panes next to it too; a very hard hit breaks all the panes of the cell
        const pdef_t *P = &LV->p[pi];
        for (int q = 0; q < R->npane; q++) {
            if (R->sub[q] == 2) {
                continue;
            }
            int dx = abs(q % P->nx - k % P->nx), dy = abs(q / P->nx - k / P->nx);
            if ((dv > GLASS_BREAK * 2 && dx + dy == 1) || dv > GLASS_BREAK * 3.5f) {
                pane_shatter(pi, q, nx * 0.4f, ny * 0.4f);
            }
        }
    } else if (dv > GLASS_CRACK) {
        pane_crack(pi, k, lx, ly, (float)G.step > 0 && b->kind == TP_DYNAMIC ? 0.5f + g_rnd() : 0);
    }
}

// the object is hit: a crack, or it breaks (the level is lost)
static void obj_break(float wx, float wy);
static void obj_hit(float dv, float wx, float wy)
{
    const objk_t *K = &OBK[LV->obj];
    if (G.o.st != OS_WHOLE) {
        return;
    }
    float h = dv / K->tough;
    if (h > 0.25f) {
        G.o.risk_t = (float)G.step * DT_STEP;
    }
    if (h > G.o.hurt) {
        G.o.hurt = h > 0.99f ? 0.99f : h;
    }
    if (h >= 1) {
        obj_break(wx, wy);
    } else if (h > 0.35f) {
        fx_add(FX_OBJ_HIT, wx, wy, h, MB_GLASS, 0);
    }
}
static void obj_break(float wx, float wy)
{
    if (G.o.st != OS_WHOLE) {
        return;
    }
    G.o.st = OS_BROKEN;
    G.o.hurt = 1;
    fx_add(FX_OBJ_BREAK, wx, wy, 1, MB_GLASS, 0);
    tp_body_t *b = tp_body(W, G.o.id);
    if (b) {
        G.o.x = px_x(b->xf.p.x);
        G.o.y = px_y(b->xf.p.y);
        tp_break(W, G.o.id, (tp_vec_t){m_x(wx), m_y(wy)}, (tp_vec_t){0, 0});
    }
    G.o.id = TP_NONE;
}

// after tp_step: the joints that broke, the hits (the sound, the glass, the object), the breaks
static void post_step(void)
{
    // the joints that broke (a cell that loses a weld shakes: a pane can crack)
    for (int e = 0; e < W->nev; e++) {
        const tp_event_t *E = &W->ev[e];
        G.last_event = G.step;
        if (E->kind == TP_EV_JOINT) {
            int k = E->user;
            if (k >= 0 && k < LV->nj && G.j[k].id != TP_NONE) {
                G.j[k].id = TP_NONE;
                G.j[k].snap_t = (float)G.step * DT_STEP;
                if (E->why != TP_WHY_BODY) {
                    fx_add(FX_SNAP, px_x(E->p.x), px_y(E->p.y), E->value, LV->j[k].kind == J_ROPE ? MB_WOOD : MB_IRON,
                           (uint8_t)k);
                }
                for (int s = 0; s < 2; s++) {
                    int pi = s ? LV->j[k].b : LV->j[k].a;
                    if (is_cell(pi) && g_rnd() < 0.5f) {
                        int q = (int)(g_rnd() * G.p[pi].npane) % (G.p[pi].npane ? G.p[pi].npane : 1);
                        float x0, y0, x1, y1;
                        pane_box(&LV->p[pi], q, &x0, &y0, &x1, &y1);
                        pane_crack(pi, q, x0 + (x1 - x0) * g_rnd(), y0 + (y1 - y0) * g_rnd(), 0.6f + 1.2f * g_rnd());
                    }
                }
            }
        } else if (E->kind == TP_EV_BREAK) {
            int pi = E->user;
            if (!G.first_break) {
                G.first_break = G.step;
            }
            fx_add(FX_BREAK, px_x(E->p.x), px_y(E->p.y), E->value, E->snd, (uint8_t)(pi >= 0 ? pi : 255));
            if (pi >= 0 && pi < LV->np && E->id == G.p[pi].id) {
                G.p[pi].id = TP_NONE;
                G.p[pi].st = PS_BROKEN;
            }
        } else if (E->kind == TP_EV_GONE) {
            int pi = E->user;
            if (pi >= 0 && pi < LV->np && E->id == G.p[pi].id) {
                G.p[pi].id = TP_NONE;
                if (G.p[pi].st != PS_BROKEN) {
                    G.p[pi].st = PS_GONE;
                }
            }
            if (E->id == G.o.id && G.o.st == OS_WHOLE) {
                obj_break(px_x(E->p.x), px_y(E->p.y));
            }
        }
    }
    // the hits
    for (int h = 0; h < W->nhit; h++) {
        const tp_hit_t *H = &W->hit[h];
        tp_body_t *A = tp_body(W, H->a), *B = tp_body(W, H->b);
        float wx = px_x(H->p.x), wy = px_y(H->p.y);
        for (int s = 0; s < 2; s++) {
            tp_body_t *X = s ? B : A, *O = s ? A : B;
            tp_id_t xid = s ? H->b : H->a;
            if (!X) {
                continue;
            }
            // (a static cell feels the change of speed of the body that hits it)
            float m = X->kind == TP_DYNAMIC ? X->mass : O && O->kind == TP_DYNAMIC ? 2.0f * O->mass : 0;
            if (m <= 0) {
                continue;
            }
            float dv = H->impulse / m;
            if (xid == G.o.id) {
                obj_hit(dv, wx, wy);
                continue;
            }
            if (X->gen == 0 && is_cell(X->user)) {
                cell_hit(X->user, wx, wy, dv, s ? H->n.x : -H->n.x, s ? H->n.y : -H->n.y);
            }
        }
        // the sound of the hit: the stronger material of the two (glass, iron, wood, stone)
        int ma = A ? (int)H->snd_a : TP_SND_STONE, mb = B ? (int)H->snd_b : TP_SND_STONE;
        float j = H->impulse;
        if (H->speed > 0.7f && j > 0.02f) {
            fx_add(FX_HIT, wx, wy, j * (0.5f + 0.25f * H->speed),
                   ma == TP_SND_GLASS || mb == TP_SND_GLASS   ? TP_SND_GLASS
                   : ma == TP_SND_METAL || mb == TP_SND_METAL ? TP_SND_METAL
                   : ma == TP_SND_WOOD || mb == TP_SND_WOOD   ? TP_SND_WOOD
                                                              : TP_SND_STONE,
                   0);
        }
        if (j > G.big_v && G.any_move) {
            G.big_v = j;
            G.big_step = G.step;
            G.big_x = wx;
            G.big_y = wy;
        }
        if (j > 0.15f) {
            G.last_event = G.step;
        }
    }
    // the splashes of the water and the shards on the floor
    if (W->splash[TP_PART_WATER] > 2) {
        fx_add(FX_SPLASH, px_x(W->splash_p[TP_PART_WATER].x), px_y(W->splash_p[TP_PART_WATER].y),
               (float)W->splash[TP_PART_WATER], 0, 0);
    }
    if (W->splash[TP_PART_SHARD] > 0) {
        fx_add(FX_TINKLE, px_x(W->splash_p[TP_PART_SHARD].x), px_y(W->splash_p[TP_PART_SHARD].y),
               W->splash_v[TP_PART_SHARD], 0, 0);
    }
    // a water object: the drops on it spoil it
    if (OBK[LV->obj].wet && G.o.st == OS_WHOLE) {
        tp_body_t *b = tp_body(W, G.o.id);
        if (b) {
            for (int q = 0; q < W->npart; q++) {
                const tp_part_t *pp = &W->part[q];
                if (pp->kind != TP_PART_WATER) {
                    continue;
                }
                float lx, ly;
                w2loc(b, px_x(pp->p.x), px_y(pp->p.y), &lx, &ly);
                if (fabsf(lx) < OBK[LV->obj].w / 2 + 1 && fabsf(ly) < OBK[LV->obj].h / 2 + 1) {
                    G.o.wet += 1;
                }
            }
            if (G.o.wet > 4) {
                G.o.risk_t = (float)G.step * DT_STEP;
            }
            if (G.o.wet > 40) {
                float x, y;
                body_px(b, &x, &y, NULL);
                obj_break(x, y);
            }
        }
    }
}

// the state of each part (down, broken), the destruction, the goal; every 6 steps
static void w_measure(void)
{
    float all = 0, down = 0, gall = 0, gdown = 0;
    int gpanes = 0;
    for (int i = 0; i < LV->np; i++) {
        prt_t *R = &G.p[i];
        const pdef_t *P = &LV->p[i];
        if (R->st == PS_STAND && R->id != TP_NONE) {
            tp_body_t *b = tp_body(W, R->id);
            if (!b) {
                R->st = PS_GONE;
            } else if (b->kind == TP_DYNAMIC) {
                float x, y, a;
                body_px(b, &x, &y, &a);
                float da = fabsf(a - R->a0);
                while (da > PI_) {
                    da = fabsf(da - TAU_);
                }
                if (y - R->y0 > fmaxf(9, 0.35f * R->h0) || da > 0.5f || x < STAND_X0 || x > STAND_X1) {
                    R->st = PS_DOWN;
                }
            }
        }
        float f = R->st == PS_STAND ? 0 : 1;
        if (R->npane && f < 1) {
            int nb = 0;
            for (int k = 0; k < R->npane; k++) {
                nb += R->sub[k] == 2;
            }
            f = fmaxf(f, 0.85f * (float)nb / (float)R->npane);
        }
        if (R->npane) {
            for (int k = 0; k < R->npane; k++) {
                gpanes += (P->tags & (1u << LV->goal_tag)) && R->sub[k] == 2;
            }
        }
        if (P->kind == PK_PANE || P->kind == PK_BOTTLE || P->kind == PK_LENS || P->kind == PK_LAMP) {
            gpanes += (P->tags & (1u << LV->goal_tag)) && R->st == PS_BROKEN;
        }
        all += R->worth;
        down += R->worth * f;
        if (P->tags & (1u << LV->goal_tag)) {
            gall += R->worth;
            gdown += R->worth * f;
        }
    }
    G.destroy = all > 0 ? down / all : 0;
    if (LV->goal == GOAL_DOWN) {
        G.goal_now = gall > 0 ? gdown / gall / (LV->goal_v / 100.0f) : 0;
    } else {
        G.goal_now = (float)gpanes / fmaxf(1, LV->goal_v);
    }
    if (G.goal_now >= 1) {
        G.goal_now = 1;
        G.goal_met = true;
    }
}

// the build: the world, the parts, the object, the joints, then 1 s of settle (the joints get their load and the
// bodies sleep), then the start places
static bool w_build(level_t *L, uint32_t seed)
{
    LV = L;
    tp_world_def_t d = w_def();
    d.mem = s_wmem;
    d.seed = seed;
    W = tp_init(&d);
    if (!W) {
        return false;
    }
    memset(&G, 0, sizeof G);
    G.rng = seed * 2654435761u + 1;
    s_nmat = 7;
    s_nfx = 0;
    // the stand: a static slab under the floor
    tp_body_def_t gd = tp_body_def(TP_STATIC, m_x(320), m_y(FLOOR + 6), TP_MAT_STONE);
    gd.user = -1;
    s_ground = tp_add_box(W, &gd, m_x((STAND_X1 - STAND_X0) / 2), m_x(6));
    W->mat[TP_MAT_STONE].friction = 0.8f;
    for (int i = 0; i < L->np; i++) {
        add_part(i);
        prt_t *R = &G.p[i];
        const pdef_t *P = &L->p[i];
        R->st = PS_STAND;
        R->npane = (P->kind == PK_CELL || P->kind == PK_ROOF) ? (uint8_t)(P->nx * P->ny) : 0;
        R->worth = (P->flags & PF_NOCOUNT) || (P->flags & PF_STATIC)
                           ? 0
                           : P->w * P->h * (P->shape == SH_CIRCLE ? 0.785f : 1);
        R->water = P->kind == PK_TANK ? (float)P->spill : 0;
        for (int k = 0; k < NSUB; k++) {
            R->crack_t[k] = -1;
        }
    }
    // the object and its stand
    const objk_t *K = &OBK[L->obj];
    float oy = L->obj_y;
    G.o.stand = TP_NONE;
    if (K->sw > 0) {
        tp_body_def_t sd =
                tp_body_def(TP_DYNAMIC, m_x(L->obj_x), m_y(oy - K->sh / 2), mat_slot(MB_IRON, 1.6f, 0.8f, 0));
        sd.user = -3;
        G.o.stand = tp_add_box(W, &sd, m_x(K->sw / 2), m_x(K->sh / 2));
        oy -= K->sh;
    }
    tp_body_def_t od =
            tp_body_def(TP_DYNAMIC, m_x(L->obj_x), m_y(oy - K->h / 2), mat_slot(MB_OBJECT, K->dens, 0.8f, 0));
    od.user = -4;
    od.no_sleep = false;
    G.o.id = tp_add_box(W, &od, m_x(K->w / 2), m_x(K->h / 2));
    G.o.st = OS_WHOLE;
    G.o.x = L->obj_x;
    G.o.y = oy - K->h / 2;
    for (int k = 0; k < L->nj; k++) {
        add_joint(k);
    }
    for (int i = 0; i < L->ne; i++) {
        if (L->e[i].kind == 1) {
            const jdef_t *J = &L->j[(int)L->e[i].rope];
            float dx = J->x2 - J->x1, dy = J->y2 - J->y1;
            G.e[i].len = J->len > 0 ? J->len : sqrtf(dx * dx + dy * dy);
        }
    }
    G.moves_left = L->moves;
    // the settle (no effects, no damage)
    s_pre = true;
    for (int s = 0; s < 75; s++) {
        tp_step(W);
    }
    s_pre = false;
    G.step = 0;
    for (int i = 0; i < L->np; i++) {
        tp_body_t *b = part_body(i);
        prt_t *R = &G.p[i];
        if (!b) {
            R->st = PS_GONE;
            continue;
        }
        body_px(b, &R->x0, &R->y0, &R->a0);
        R->h0 = FLOOR - R->y0;
    }
    w_measure();
    G.destroy = 0;
    s_nfx = 0;
    return true;
}

// a move: queued for the next step (the replay does the same move in the same step)
static bool w_move_ok(int h)
{
    if (h < 0 || h >= LV->nh || G.used[h] || G.moves_left == 0) {
        return false;
    }
    if (G.state != LV_AIM && G.state != LV_ACT) {
        return false;
    }
    if (G.o.st != OS_WHOLE) {
        return false;
    }
    // the target must still be there
    const hdef_t *H = &LV->h[h];
    if (H->tkind == TK_JOINT && G.j[H->target].id == TP_NONE) {
        return false;
    }
    if (H->tkind == TK_PART && G.p[H->target].id == TP_NONE) {
        return false;
    }
    return true;
}
static void w_move(int h)
{
    if (!w_move_ok(h) || G.nqueue >= 4) {
        return;
    }
    G.used[h] = 1;
    G.moves_left--;
    G.moves_done++;
    G.queue[G.nqueue++] = (int8_t)h;
    if (!G.any_move) {
        G.first_step = G.step;
    }
    G.any_move = true;
    G.last_move_step = G.step;
    G.state = LV_ACT;
    G.quiet_steps = 0;
    if (G.nrec < MAXH) {
        G.rec_step[G.nrec] = G.step;
        G.rec_h[G.nrec] = (int8_t)h;
        G.nrec++;
    }
}
// the point of the handle of move h (world px), from its target when the level gives none
static void move_point(int h, float *x, float *y)
{
    const hdef_t *H = &LV->h[h];
    if (H->hx != 0 || H->hy != 0) {
        // (a point on a part moves with the part)
        if (H->tkind == TK_PART) {
            const pdef_t *P = &LV->p[H->target];
            tp_body_t *b = part_body(H->target);
            if (b) {
                float c = cosf(P->a), s = sinf(P->a), dx = H->hx - P->x, dy = H->hy - P->y;
                loc2w(b, c * dx + s * dy, -s * dx + c * dy, x, y);
                return;
            }
        }
        *x = H->hx;
        *y = H->hy;
        return;
    }
    if (H->tkind == TK_JOINT) {
        tp_joint_t *j = tp_joint(W, G.j[H->target].id);
        if (j) {
            tp_vec_t a, b;
            tp_joint_anchors(W, j, &a, &b);
            *x = px_x((a.x + b.x) / 2);
            *y = px_y((a.y + b.y) / 2);
            return;
        }
        *x = LV->j[H->target].x1;
        *y = LV->j[H->target].y1;
        return;
    }
    if (H->tkind == TK_PART) {
        tp_body_t *b = part_body(H->target);
        if (b) {
            body_px(b, x, y, NULL);
            return;
        }
        *x = LV->p[H->target].x;
        *y = LV->p[H->target].y;
        return;
    }
    const edef_t *E = &LV->e[H->target];
    // (a winch: its drum is at the fixed end of its rope)
    if (E->kind == 1) {
        const jdef_t *J = &LV->j[(int)E->rope];
        *x = J->b < 0 ? J->x2 : J->x1;
        *y = J->b < 0 ? J->y2 : J->y1;
        return;
    }
    *x = E->x;
    *y = E->y;
}
static void w_do_move(int h)
{
    const hdef_t *H = &LV->h[h];
    float hx, hy;
    move_point(h, &hx, &hy);
    switch (H->kind) {
    case MV_CUT:
    case MV_PULL:
        if (G.j[H->target].id != TP_NONE) {
            tp_joint_t *j = tp_joint(W, G.j[H->target].id);
            if (j) {
                tp_wake(W, tp_body_id(W, j->a));
                tp_wake(W, tp_body_id(W, j->b));
            }
            tp_joint_remove(W, G.j[H->target].id);
            G.j[H->target].id = TP_NONE;
            G.j[H->target].snap_t = (float)G.step * DT_STEP;
        }
        break;
    case MV_CRACK: {
        int pi = H->target;
        tp_body_t *b = part_body(pi);
        if (!b || !is_cell(pi)) {
            break;
        }
        float lx, ly;
        w2loc(b, hx, hy, &lx, &ly);
        int k = H->sub >= 0 ? H->sub : pane_at(&LV->p[pi], lx, ly);
        if (G.p[pi].sub[k] == 0) {
            pane_crack(pi, k, lx, ly, 0.9f);
        } else if (G.p[pi].sub[k] == 1) {
            G.p[pi].shatter_at[k] = (float)G.step * DT_STEP + 0.3f;
        }
        break;
    }
    case MV_KICK: {
        tp_body_t *b = part_body(H->target);
        if (!b) {
            break;
        }
        float l = sqrtf(H->dx * H->dx + H->dy * H->dy) + 1e-6f;
        tp_impulse(W, G.p[H->target].id, (tp_vec_t){H->dx / l * H->power * b->mass, -H->dy / l * H->power * b->mass},
                   (tp_vec_t){m_x(hx), m_y(hy)});
        break;
    }
    case MV_OPEN:
        G.e[H->target].on = true;
        G.e[H->target].left = LV->e[H->target].time;
        break;
    case MV_START:
        if (H->tkind == TK_JOINT) {
            const jdef_t *J = &LV->j[H->target];
            if (G.j[H->target].id != TP_NONE) {
                tp_motor(W, G.j[H->target].id, J->speed, J->torque);
            }
        } else {
            G.e[H->target].on = true;
        }
        break;
    default:
        break;
    }
    fx_add(FX_MOVE, hx, hy, 1, H->kind, (uint8_t)h);
}

// one physics step of the level
static void w_step(void)
{
    pre_step();
    tp_step(W);
    G.step++;
    post_step();
    if (G.step % 6 == 0) {
        w_measure();
    }
    // the end of the action: the moving bodies are slow (or asleep) for 0.8 s, and no pipe or winch works
    if (G.state == LV_ACT) {
        float vmax = 0;
        for (int i = 0; i < W->body_hi; i++) {
            const tp_body_t *b = &W->body[i];
            if (!(b->flags & TP_ALIVE) || !(b->flags & TP_AWAKE) || b->kind != TP_DYNAMIC || b->user == -2) {
                continue;
            }
            float v = b->v.x * b->v.x + b->v.y * b->v.y;
            if (v > vmax) {
                vmax = v;
            }
        }
        bool busy = false;
        for (int i = 0; i < LV->ne; i++) {
            busy |= G.e[i].on;
        }
        for (int i = 0; i < LV->np; i++) {
            for (int k = 0; k < G.p[i].npane; k++) {
                busy |= G.p[i].sub[k] == 1 && G.p[i].shatter_at[k] > 0;
            }
        }
        if (vmax < 0.12f * 0.12f && !busy) {
            G.quiet_steps++;
        } else {
            G.quiet_steps = 0;
        }
        bool timeout = G.step - G.last_move_step > 60 * 22;
        // (a won run ends when the goal is met and nothing hits for 2.5 s: a pendulum that swings on does not hold it)
        bool calm = G.goal_met && G.step - G.last_event > 150 && G.step - G.last_move_step > 90 && !busy;
        if (G.o.st != OS_WHOLE) {
            if (!G.end_step) {
                G.end_step = G.step + 50;
            }
            if (G.step >= G.end_step) {
                G.state = LV_LOST;
            }
        } else if (G.quiet_steps > 48 || timeout || calm) {
            G.settle_step = G.step;
            w_measure();
            if (G.goal_met) {
                G.state = LV_WON;
            } else if (G.moves_left == 0) {
                G.state = LV_STILL;
            } else {
                G.state = LV_AIM;
            }
        }
    }
    {
        tp_body_t *ob = tp_body(W, G.o.id);
        if (ob) {
            body_px(ob, &G.o.x, &G.o.y, NULL);
        }
    }
}
