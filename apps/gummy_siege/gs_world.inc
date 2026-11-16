// GUMMY SIEGE: the world (included by app_gummy.c). The physics of tfphys, the things of a level, the bears and their
// powers, the enemies and their damage, the points, the turns of a level and the targets of the camera. The world
// moves in steps of 1/60 s (game_tick): the same shots give the same result (the level check of the PC uses it).

// ================================================================ the bears
enum {
    BK_RED,
    BK_GREEN,
    BK_YELLOW,
    BK_ORANGE,
    BK_CLEAR,
    BK_N
};
static int bear_of(char c)
{
    switch (c) {
    case 'R':
        return BK_RED;
    case 'G':
        return BK_GREEN;
    case 'Y':
        return BK_YELLOW;
    case 'O':
        return BK_ORANGE;
    case 'C':
        return BK_CLEAR;
    default:
        return -1;
    }
}
// the materials of the world (tp_init fills 0..6 with the defaults; the game sets all 16)
enum {
    M_JAR,
    M_TABLE,
    M_CAN,
    M_CHIP,
    M_GUMMY,
    M_HEAVY,
    M_WATER,
    M_PRETZEL,
    M_CRACKER,
    M_CUP,
    M_BOUNCY,
    M_ENEMY,
    M_GIFT,
    M_BALLOON,
    M_STICKY,
    M_LIGHT
};
static const struct {
    float r;                     // the radius (m)
    uint8_t mat;                 // the material (its density gives the weight)
    float vmax;                  // the speed at a full pull (m/s)
    uint8_t c[3], lt[3], dk[3];  // the colour, the light colour, the dark colour (the look page)
} BEAR[BK_N] = {
        {0.27f, M_HEAVY, 15.0f, {255, 45, 85}, {255, 143, 166}, {158, 11, 44}},       // RED: heavy; SLAM
        {0.25f, M_GUMMY, 15.5f, {69, 214, 74}, {182, 255, 138}, {29, 122, 38}},       // GREEN: three of them
        {0.23f, M_LIGHT, 17.0f, {255, 210, 26}, {255, 243, 168}, {176, 124, 0}},      // YELLOW: fast; a DASH
        {0.26f, M_BOUNCY, 15.0f, {255, 140, 26}, {255, 208, 138}, {168, 69, 10}},     // ORANGE: bounces; a HOP
        {0.25f, M_STICKY, 15.0f, {244, 236, 224}, {255, 255, 255}, {185, 169, 140}},  // CLEAR: sticky; a YANK
};
#define SPLIT_R  0.18f
#define SL_X     0.0f   // the slingshot: the post on the table at x = 0
#define SL_Y     1.02f  // the pouch at rest
#define PULL_MAX 0.95f  // the pouch goes at most this far back (m)
#define PULL_MIN 0.16f  // a smaller pull is no shot (the finger goes back to the start: the shot stops)

// ================================================================ the state of a level
enum {
    PH_INTRO,
    PH_HOP,
    PH_AIM,
    PH_FLY,
    PH_WATCH,
    PH_END
};
#define THING_MAX  96
#define USER_BEAR  1000  // the user value of a bear body: USER_BEAR + its part (0..2)
#define USER_TABLE 2000
typedef struct {
    tp_id_t id;    // the body (TP_NONE: none, or it went away)
    float dmg;     // an enemy: the damage so far
    bool out;      // an enemy: knocked out; a balloon: popped
    float x0, y0;  // the place at the start
    float lift;    // a balloon: the force up (N)
    float hit_t;   // an enemy: the time of the last hit (the look: it blinks)
    bool broke;    // a jar: it broke (its pieces break again with no more salsa and no more points)
} trun_t;
static trun_t s_tr[THING_MAX];
static tp_world_t *W;
static int s_li = 0;  // the level now
static const level_t *L;
static int s_phase;
static int s_tick;                  // the steps in this phase
static uint32_t s_ticks;            // the steps since the start of the level
static uint8_t s_queue[NBEAR_MAX];  // the bears that wait (the first is the next one)
static int s_nqueue;
static int s_bear_kind = -1;  // the bear in the sling or in the air
static tp_id_t s_bear[3];     // the bodies of the bear in the air (3 after a split)
static int s_nbody;           // the bodies of the bear (0 = none)
static bool s_power_used, s_power_armed;
static int s_power_tick;           // the step of the power
static int s_dash_left;            // the steps of the DASH with no gravity
static tp_id_t s_weld = TP_NONE;   // the joint of the CLEAR bear (it sticks)
static tp_id_t s_stuck = TP_NONE;  // the body that the CLEAR bear holds
static int s_rest_ticks;           // the steps that the bear is still
static int s_calm_ticks;           // the steps that the world is calm
static int s_enemies;              // the enemies that are not out
static uint32_t s_score;
static int s_shots;
static bool s_won;
static int s_stars_now;
static float s_aim_a = 0.6f, s_aim_p;  // the aim: the angle (rad) and the pull 0..1 (the planner and the finger)
static bool s_aiming;                  // a finger pulls now
// the last path (the dots of the shot before: a help for the next aim)
#define PATH_N 48
static float s_path[2][PATH_N][2];
static int s_npath[2];
static int s_pathi;
// the zones of cheese dip (from the things of the level)
typedef struct {
    float x0, x1, y1;
} dip_t;
static dip_t s_dip[4];
static int s_ndip;
// the events of a step for the looks and the sounds (fx.inc reads them)
static float s_shake;                          // the screen shake (px), it goes down
static float s_slow_t;                         // the time of the slow motion that is left (s of the game)
static bool s_slow_done;                       // one slow motion for each shot
static float s_focus_x, s_focus_y, s_focus_t;  // the point of a big moment (the camera goes near it)
static float s_brk_x, s_brk_y;                 // the last thing that broke in this shot (the camera holds on it)
static int s_brk_n;                            // the things that broke in this shot (0: the camera holds on the bear)

static void fx_score(float x, float y, int pts, int kind);
static void fx_break(const tp_event_t *e);
static void fx_ko(int ti, float x, float y);
static void fx_hit(const tp_hit_t *h, float strength);
static void fx_power(int kind, float x, float y);
static void fx_splat(float x, float y);
static void fx_launch(void);
static void fx_bear_pop(float x, float y, int kind);
static void fx_balloon(int ti, float x, float y);
static void fx_dip(float x, float y);
static void fx_stick(float x, float y);

static inline bool user_bear(int32_t u)
{
    return u >= USER_BEAR && u < USER_BEAR + 3;
}
static inline bool user_thing(int32_t u)
{
    return u >= 0 && u < THING_MAX && L && u < L->n;
}
static inline const thing_t *thing_of(int ti)
{
    return &s_thing[L->first + ti];
}

// ================================================================ the world
static void mat_set(int i, float dens, float fr, float rest, float tough, float roll, float burst, int snd, int pat,
                    int pieces, int crumbs, int ck)
{
    tp_mat_t *m = &W->mat[i];
    m->density = dens;
    m->friction = fr;
    m->restitution = rest;
    m->toughness = tough;
    m->rolling = roll;
    m->burst = burst;
    m->sound = (uint8_t)snd;
    m->pattern = (uint8_t)pat;
    m->pieces = (uint8_t)pieces;
    m->crumbs = (uint8_t)crumbs;
    m->crumb_kind = (uint8_t)ck;
}
static bool world_make(void)
{
    if (W) {
        return true;
    }
    tp_world_def_t d = tp_world_defaults();
    d.bounds.lo = (tp_vec_t){-30, -14};
    d.bounds.hi = (tp_vec_t){90, 60};
    d.max_bodies = 200;
    d.max_joints = 40;
    d.max_contacts = 480;
    d.max_parts = 420;
    d.max_hits = 64;
    d.max_events = 96;
    d.seed = 1867;
    W = tp_init(&d);
    if (!W) {
        return false;
    }
    //            density fric  rest  tough roll  burst sound          pattern            pieces crumbs kind
    mat_set(M_JAR, 1.5f, 0.4f, 0.10f, 3.2f, 0, 0.8f, TP_SND_GLASS, TP_BREAK_SHARDS, 6, 8, TP_PART_SHARD);
    mat_set(M_TABLE, 1.0f, 0.75f, 0.05f, 0, 0, 0.2f, TP_SND_WOOD, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB);
    mat_set(M_CAN, 2.6f, 0.4f, 0.10f, 0, 0, 0.2f, TP_SND_METAL, TP_BREAK_CHUNKS, 2, 0, TP_PART_SPARK);
    mat_set(M_CHIP, 0.7f, 0.75f, 0.05f, 3.6f, 0, 0.6f, TP_SND_CHIP, TP_BREAK_CHUNKS, 3, 8, TP_PART_CRUMB);
    mat_set(M_GUMMY, 1.3f, 0.9f, 0.35f, 0, 0.05f, 0.2f, TP_SND_GUMMY, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB);
    mat_set(M_HEAVY, 3.2f, 0.9f, 0.25f, 0, 0.05f, 0.2f, TP_SND_GUMMY, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB);
    mat_set(M_WATER, 1.0f, 0.1f, 0.0f, 0, 0, 0.2f, TP_SND_WATER, TP_BREAK_CHUNKS, 2, 0, TP_PART_WATER);
    mat_set(M_PRETZEL, 0.9f, 0.6f, 0.08f, 5.0f, 0, 0.35f, TP_SND_WOOD, TP_BREAK_SPLINTER, 3, 5, TP_PART_CRUMB);
    mat_set(M_CRACKER, 0.6f, 0.7f, 0.05f, 3.8f, 0, 0.5f, TP_SND_CHIP, TP_BREAK_CHUNKS, 4, 8, TP_PART_CRUMB);
    mat_set(M_CUP, 0.35f, 0.6f, 0.1f, 0, 0, 0.3f, TP_SND_WOOD, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB);
    mat_set(M_BOUNCY, 1.3f, 0.5f, 0.82f, 0, 0.02f, 0.2f, TP_SND_GUMMY, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB);
    mat_set(M_ENEMY, 0.8f, 0.8f, 0.10f, 0, 0, 0.3f, TP_SND_CHIP, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB);
    mat_set(M_GIFT, 0.45f, 0.6f, 0.08f, 4.2f, 0, 0.4f, TP_SND_WOOD, TP_BREAK_CHUNKS, 3, 4, TP_PART_CRUMB);
    mat_set(M_BALLOON, 0.12f, 0.5f, 0.5f, 0, 0, 0.2f, TP_SND_GUMMY, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB);
    mat_set(M_STICKY, 1.3f, 1.0f, 0.0f, 0, 0.2f, 0.2f, TP_SND_GUMMY, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB);
    mat_set(M_LIGHT, 1.0f, 0.9f, 0.35f, 0, 0.05f, 0.2f, TP_SND_GUMMY, TP_BREAK_CHUNKS, 2, 0, TP_PART_CRUMB);
    // the particles: crumbs live a little longer (the look), the water drops are salsa
    W->pkind[TP_PART_CRUMB].life = 2.2f;
    W->pkind[TP_PART_WATER].life = 2.6f;
    W->pkind[TP_PART_WATER].restitution = 0.0f;
    return true;
}
static void world_free(void)
{
    if (W) {
        tp_free(W);
    }
    W = NULL;
}

// the local shape of a thing (counter-clockwise; the origin is the middle of the shape); returns the corner count
static int thing_shape(const thing_t *T, tp_vec_t *v)
{
    float s, h;
    switch (T->kind) {
    case OK_CHIP:
    case OK_BIGCHIP:
    case OK_KNIGHT:
    case OK_CAPTAIN:
    case OK_KING:
        if (T->kind == OK_CHIP) {
            s = CHIP_S;
            h = CHIP_H;
        } else if (T->kind == OK_BIGCHIP) {
            s = CHIP_S * 1.45f;
            h = CHIP_H * 1.45f;
        } else {
            s = ENEMY[T->kind - OK_KNIGHT].s;
            h = ENEMY[T->kind - OK_KNIGHT].h;
        }
        v[0] = (tp_vec_t){-s / 2, -h / 3};
        v[1] = (tp_vec_t){s / 2, -h / 3};
        v[2] = (tp_vec_t){0, 2 * h / 3};
        return 3;
    case OK_CUP:
        v[0] = (tp_vec_t){-0.17f, -0.25f};
        v[1] = (tp_vec_t){0.17f, -0.25f};
        v[2] = (tp_vec_t){0.22f, 0.25f};
        v[3] = (tp_vec_t){-0.22f, 0.25f};
        return 4;
    default: {
        float hw, hh;
        if (T->kind == OK_STICK) {
            hw = (T->w > 0.1f ? T->w : 1.6f) / 2;
            hh = 0.045f;
        } else if (T->kind == OK_JAR) {
            hw = 0.20f;
            hh = 0.25f;
        } else if (T->kind == OK_CAN) {
            hw = 0.22f;
            hh = 0.31f;
        } else {
            hw = (T->w > 0.05f ? T->w : 0.5f) / 2;
            hh = (T->h > 0.05f ? T->h : T->w > 0.05f ? T->w : 0.5f) / 2;
        }
        v[0] = (tp_vec_t){-hw, -hh};
        v[1] = (tp_vec_t){hw, -hh};
        v[2] = (tp_vec_t){hw, hh};
        v[3] = (tp_vec_t){-hw, hh};
        return 4;
    }
    }
}
static int thing_mat(int k)
{
    switch (k) {
    case OK_CHIP:
    case OK_BIGCHIP:
        return M_CHIP;
    case OK_STICK:
        return M_PRETZEL;
    case OK_CRACKER:
        return M_CRACKER;
    case OK_JAR:
        return M_JAR;
    case OK_CUP:
        return M_CUP;
    case OK_CAN:
        return M_CAN;
    case OK_GIFT:
        return M_GIFT;
    case OK_BALLOON:
        return M_BALLOON;
    case OK_BLOCK:
        return M_TABLE;
    default:
        return M_ENEMY;
    }
}
// the points of a break of a thing
static int break_pts(int k)
{
    switch (k) {
    case OK_CHIP:
        return 500;
    case OK_BIGCHIP:
        return 900;
    case OK_STICK:
        return 300;
    case OK_CRACKER:
        return 400;
    case OK_JAR:
        return 3000;
    case OK_GIFT:
        return 700;
    default:
        return 200;
    }
}

static void level_build(int li)
{
    s_li = clampi(li, 0, s_nlev - 1);
    L = &s_lev[s_li];
    tp_clear(W);
    memset(s_tr, 0, sizeof s_tr);
    s_ndip = 0;
    s_enemies = 0;
    // the table (and its end: a counter with a floor far down)
    {
        float x1 = L->edge > 1.0f ? L->edge : 80.0f;
        tp_body_def_t d = tp_body_def(TP_STATIC, (x1 - 25.0f) * 0.5f, -1.0f, M_TABLE);
        d.user = USER_TABLE;
        tp_add_box(W, &d, (x1 + 25.0f) * 0.5f, 1.0f);
    }
    int n = L->n < THING_MAX ? L->n : THING_MAX;
    for (int i = 0; i < n; i++) {
        const thing_t *T = thing_of(i);
        trun_t *R = &s_tr[i];
        R->id = TP_NONE;
        R->x0 = T->x;
        R->y0 = T->y;
        if (T->kind == OK_DIP) {
            if (s_ndip < 4) {
                s_dip[s_ndip].x0 = T->x;
                s_dip[s_ndip].x1 = T->x + T->w;
                s_dip[s_ndip].y1 = T->h;
                s_ndip++;
            }
            // the bowl: two low walls (the dip itself is a zone that slows a body)
            for (int sd = 0; sd < 2; sd++) {
                tp_body_def_t d = tp_body_def(TP_STATIC, sd ? T->x + T->w - 0.03f : T->x + 0.03f, T->h * 0.5f, M_TABLE);
                d.user = USER_TABLE;
                tp_add_box(W, &d, 0.03f, T->h * 0.5f);
            }
            continue;
        }
        if (T->kind == OK_BLOCK) {
            tp_body_def_t d = tp_body_def(TP_STATIC, T->x + T->w * 0.5f, T->y + T->h * 0.5f, M_TABLE);
            d.user = i;
            R->id = tp_add_box(W, &d, T->w * 0.5f, T->h * 0.5f);
            continue;
        }
        if (T->kind == OK_BALLOON) {
            tp_body_def_t d = tp_body_def(TP_DYNAMIC, T->x, T->y, M_BALLOON);
            d.user = i;
            d.ldamp = 1.6f;
            d.adamp = 3.0f;
            d.sleeping = true;
            R->id = tp_add_circle(W, &d, 0.28f);
            tp_body_t *b = tp_body(W, R->id);
            // the lift: the force up of the file (N), 1.2 N when it gives none (the weight of the balloon is in it)
            float lift = T->w > 0.01f ? T->w : 1.2f;
            R->lift = lift;
            if (b && b->mass > 0) {
                b->gscale = -(lift / (b->mass * 9.8f));
            }
            continue;
        }
        tp_vec_t v[TP_MAX_VERTS];
        int nv = thing_shape(T, v);
        tp_body_def_t d = tp_body_def(TP_DYNAMIC, T->x, T->y, thing_mat(T->kind));
        d.angle = T->a;
        d.user = i;
        d.sleeping = true;
        if (T->kind == OK_JAR) {
            d.spill = 46;
        }
        if (ok_enemy(T->kind)) {
            d.toughness = 0;
            s_enemies++;
        }
        R->id = tp_add_poly(W, &d, v, nv);
    }
    // the ropes of the balloons (to a thing, or to a point of the table) and of the "rope" lines
    for (int i = 0; i < n; i++) {
        const thing_t *T = thing_of(i);
        if (T->kind != OK_BALLOON || s_tr[i].id == TP_NONE) {
            continue;
        }
        tp_vec_t pa = {T->x, T->y - 0.30f}, pb = {T->tx, T->ty};
        tp_id_t other = TP_NONE;
        if (T->tie >= 0 && T->tie < n && s_tr[T->tie].id != TP_NONE) {
            other = s_tr[T->tie].id;
        } else {
            other = tp_body_id(W, 0);  // (the table: the first body)
        }
        float dx = pb.x - pa.x, dy = pb.y - pa.y;
        tp_joint_def_t j = tp_rope_def(s_tr[i].id, other, pa, pb, sqrtf(dx * dx + dy * dy) + 0.002f);
        j.user = i;
        tp_add_joint(W, &j);
    }
    // the steps of the start: the queue of bears
    s_nqueue = L->nbear;
    memcpy(s_queue, L->bears, (size_t)L->nbear);
    s_bear_kind = -1;
    s_nbody = 0;
    s_weld = s_stuck = TP_NONE;
    s_score = 0;
    s_shots = 0;
    s_won = false;
    s_stars_now = 0;
    s_phase = PH_INTRO;
    s_tick = 0;
    s_ticks = 0;
    s_npath[0] = s_npath[1] = 0;
    s_shake = 0;
    s_slow_t = 0;
    s_focus_t = 0;
    s_aiming = false;
    s_aim_p = 0;
}

// ================================================================ the aim and the shot
// the place of the pouch for a pull p (0..1) at the angle a (the shot goes in the direction a)
static void pouch_at(float a, float p, float *x, float *y)
{
    *x = SL_X - cosf(a) * p * PULL_MAX;
    *y = SL_Y - sinf(a) * p * PULL_MAX;
    if (*y < 0.32f) {
        *y = 0.32f;
    }
}
static void shot_speed(int kind, float a, float p, float *vx, float *vy)
{
    float v = BEAR[kind].vmax * p;
    *vx = cosf(a) * v;
    *vy = sinf(a) * v;
}
static tp_id_t bear_body(int kind, float x, float y, float vx, float vy, float r, int part)
{
    tp_body_def_t d = tp_body_def(TP_DYNAMIC, x, y, BEAR[kind].mat);
    d.vel = (tp_vec_t){vx, vy};
    d.spin = -2.0f;
    d.user = USER_BEAR + part;
    d.group = 7;  // (the three parts of a split do not hit each other)
    d.no_sleep = true;
    d.adamp = 0.4f;
    return tp_add_circle(W, &d, r);
}
static void path_start(void)
{
    s_pathi ^= 1;
    s_npath[s_pathi] = 0;
}
static bool shot_fire(float a, float p)
{
    if (s_phase != PH_AIM || s_bear_kind < 0 || p < PULL_MIN) {
        return false;
    }
    p = clampf(p, 0, 1);
    float x, y, vx, vy;
    pouch_at(a, p, &x, &y);
    shot_speed(s_bear_kind, a, p, &vx, &vy);
    s_bear[0] = bear_body(s_bear_kind, x, y, vx, vy, BEAR[s_bear_kind].r, 0);
    s_nbody = s_bear[0] == TP_NONE ? 0 : 1;
    s_power_used = s_power_armed = false;
    s_power_tick = -1;
    s_dash_left = 0;
    s_weld = s_stuck = TP_NONE;
    s_rest_ticks = 0;
    s_slow_done = false;
    s_aim_a = a;
    s_aim_p = p;
    s_aiming = false;
    s_shots++;
    s_phase = PH_FLY;
    s_tick = 0;
    path_start();
    fx_launch();
    return true;
}
static tp_body_t *bear_main(void)
{
    for (int i = 0; i < s_nbody; i++) {
        tp_body_t *b = tp_body(W, s_bear[i]);
        if (b) {
            return b;
        }
    }
    return NULL;
}
// the power of the bear in the air (the second tap)
static void power_now(void)
{
    tp_body_t *b = bear_main();
    if (!b) {
        return;
    }
    float vx = b->v.x, vy = b->v.y, sp = sqrtf(vx * vx + vy * vy);
    s_power_used = true;
    s_power_tick = s_tick;
    switch (s_bear_kind) {
    case BK_RED: {  // SLAM: a dive down
        tp_set_vel(W, s_bear[0], (tp_vec_t){vx * 0.3f, minf(vy, 0) - 15.0f}, 9.0f);
        break;
    }
    case BK_GREEN: {  // three bears in a fan
        float x = b->xf.p.x, y = b->xf.p.y;
        tp_remove(W, s_bear[0]);
        float a0 = atan2f(vy, vx), v = sp * 1.04f;
        static const float DA[3] = {0.0f, 0.2f, -0.2f};
        s_nbody = 0;
        for (int k = 0; k < 3; k++) {
            float a = a0 + DA[k], nx = -sinf(a0) * (k == 0 ? 0 : k == 1 ? 0.26f : -0.26f);
            float ny = cosf(a0) * (k == 0 ? 0 : k == 1 ? 0.26f : -0.26f);
            tp_id_t id = bear_body(BK_GREEN, x + nx, y + ny, cosf(a) * v, sinf(a) * v, SPLIT_R, k);
            if (id != TP_NONE) {
                s_bear[s_nbody++] = id;
            }
        }
        break;
    }
    case BK_YELLOW: {  // DASH: twice as fast, in a straight line for a moment
        float v = maxf(sp * 2.1f, 19.0f);
        if (sp > 0.1f) {
            tp_set_vel(W, s_bear[0], (tp_vec_t){vx / sp * v, vy / sp * v}, 0);
        }
        b = tp_body(W, s_bear[0]);
        if (b) {
            b->gscale = 0;
        }
        s_dash_left = 22;
        break;
    }
    case BK_ORANGE: {  // HOP: a bounce in the air
        tp_set_vel(W, s_bear[0], (tp_vec_t){vx * 0.95f, maxf(vy, 0) + 7.5f}, 6.0f);
        break;
    }
    case BK_CLEAR: {  // YANK: the jelly snaps back to the sling, with what it holds
        // (the thing can break after the bear sticks to it: the weld then holds a piece; take the body from the weld)
        const tp_joint_t *J = s_weld != TP_NONE ? tp_joint(W, s_weld) : NULL;
        if (J) {
            s_stuck = J->a == s_bear[0] ? J->b : J->a;
        }
        tp_body_t *s = J ? tp_body(W, s_stuck) : NULL;
        if (!s) {
            s_power_armed = true;
            s_power_used = false;
            return;
        }
        float m = b->mass + s->mass;
        tp_impulse(W, s_bear[0], (tp_vec_t){-6.5f * m, 3.2f * m}, b->xf.p);
        break;
    }
    default:
        break;
    }
    b = bear_main();
    if (b) {
        fx_power(s_bear_kind, b->xf.p.x, b->xf.p.y);
    }
}
static void power_use(void)
{
    if (s_phase != PH_FLY || s_power_used || s_power_armed || !s_nbody) {
        return;
    }
    power_now();
}

// ================================================================ the damage of the enemies
static void enemy_out(int ti)
{
    trun_t *R = &s_tr[ti];
    if (R->out) {
        return;
    }
    R->out = true;
    s_enemies--;
    tp_body_t *b = tp_body(W, R->id);
    float x = b ? b->xf.p.x : R->x0, y = b ? b->xf.p.y : R->y0;
    if (b) {
        tp_remove(W, R->id);
    }
    R->id = TP_NONE;
    if (s_phase == PH_END) {
        return;  // (after the end: no points, no looks)
    }
    int pts = ENEMY[thing_of(ti)->kind - OK_KNIGHT].pts;
    s_score += (uint32_t)pts;
    fx_ko(ti, x, y);
    fx_score(x, y + 0.5f, pts, 1);
    // a big moment: the slow motion (once for each shot) and the camera near it
    s_focus_x = x;
    s_focus_y = y;
    s_focus_t = 1.2f;
    if (!s_slow_done && s_phase >= PH_FLY) {
        s_slow_t = 0.55f;
        s_slow_done = true;
    }
}
static void enemy_hurt(int ti, float dmg, float t)
{
    trun_t *R = &s_tr[ti];
    if (R->out || dmg <= 0) {
        return;
    }
    R->dmg += dmg;
    R->hit_t = t;
    if (R->dmg >= ENEMY[thing_of(ti)->kind - OK_KNIGHT].hp) {
        enemy_out(ti);
    }
}
#define DMG_MIN 1.5f  // a hit under this change of speed (m/s) does no damage
static void hits_step(void)
{
    float now = (float)s_ticks * STEP_DT;
    for (int i = 0; i < W->nhit; i++) {
        const tp_hit_t *h = &W->hit[i];
        tp_body_t *A = tp_body(W, h->a), *B = tp_body(W, h->b);
        float strength = 0;
        for (int sd = 0; sd < 2; sd++) {
            tp_body_t *X = sd ? B : A, *O = sd ? A : B;
            if (!X || !(X->mass > 0)) {
                continue;
            }
            float dv = h->impulse / X->mass;
            if (dv > strength) {
                strength = dv;
            }
            int32_t u = X->user;
            if (!user_thing(u)) {
                continue;
            }
            const thing_t *T = thing_of(u);
            if (ok_enemy(T->kind)) {
                float k = O && user_bear(O->user) ? 1.5f : 1.0f;
                enemy_hurt(u, (dv - DMG_MIN) * k, now);
            } else if (T->kind == OK_BALLOON && !s_tr[u].out && h->speed > 1.6f) {
                // a balloon pops at a hit of the bear or of a thing that is not on its rope
                int other = O ? O->user : -1;
                if (!(user_thing(other) && (other == T->tie))) {
                    trun_t *R = &s_tr[u];
                    R->out = true;
                    fx_balloon(u, X->xf.p.x, X->xf.p.y);
                    tp_remove(W, R->id);
                    R->id = TP_NONE;
                    if (s_phase != PH_END) {
                        s_score += 1000;
                        fx_score(X->xf.p.x, X->xf.p.y, 1000, 0);
                    }
                    if (X == A) {
                        A = NULL;
                    } else {
                        B = NULL;
                    }
                }
            }
        }
        fx_hit(h, strength);
        // the CLEAR bear sticks to the first thing that it hits (not the table)
        if (s_bear_kind == BK_CLEAR && s_weld == TP_NONE && s_nbody && h->begin) {
            tp_body_t *bb = tp_body(W, s_bear[0]);
            if (bb && (A == bb || B == bb)) {
                tp_body_t *O = A == bb ? B : A;
                tp_id_t oid = A == bb ? h->b : h->a;
                if (O && O->kind == TP_DYNAMIC && !user_bear(O->user)) {
                    tp_joint_def_t j = tp_weld_def(s_bear[0], oid, h->p);
                    j.break_force = 90.0f;
                    j.hz = 6.0f;
                    j.zeta = 0.7f;
                    s_weld = tp_add_joint(W, &j);
                    if (s_weld != TP_NONE) {
                        s_stuck = oid;
                        fx_stick(h->p.x, h->p.y);
                        if (s_power_armed) {
                            s_power_armed = false;
                            power_now();
                        }
                    }
                }
            }
        }
    }
}
static void events_step(void)
{
    for (int i = 0; i < W->nev; i++) {
        const tp_event_t *e = &W->ev[i];
        if (e->kind == TP_EV_BREAK && user_thing(e->user)) {
            const thing_t *T = thing_of(e->user);
            bool jar_again = T->kind == OK_JAR && s_tr[e->user].broke;
            if (T->kind == OK_JAR) {
                s_tr[e->user].broke = true;
            }
            if (s_phase == PH_END || jar_again) {
                fx_break(e);
                continue;
            }
            int pts = break_pts(T->kind);
            s_score += (uint32_t)pts;
            fx_break(e);
            s_brk_x = e->p.x;
            s_brk_y = e->p.y;
            s_brk_n++;
            fx_score(e->p.x, e->p.y + 0.2f, pts, T->kind == OK_JAR ? 2 : 0);
            if (T->kind == OK_JAR) {
                // the salsa: the enemies near it get the hot splash
                fx_splat(e->p.x, e->p.y);
                for (int k = 0; k < L->n && k < THING_MAX; k++) {
                    const thing_t *E = thing_of(k);
                    tp_body_t *b = ok_enemy(E->kind) && !s_tr[k].out ? tp_body(W, s_tr[k].id) : NULL;
                    if (!b) {
                        continue;
                    }
                    float dx = b->xf.p.x - e->p.x, dy = b->xf.p.y - e->p.y, d = sqrtf(dx * dx + dy * dy);
                    if (d < 1.5f) {
                        enemy_hurt(k, 3.0f * (1.0f - d / 1.5f), (float)s_ticks * STEP_DT);
                    }
                }
                s_focus_x = e->p.x;
                s_focus_y = e->p.y;
                s_focus_t = 1.0f;
                if (!s_slow_done) {
                    s_slow_t = 0.45f;
                    s_slow_done = true;
                }
            }
        } else if (e->kind == TP_EV_GONE && e->why == TP_GONE_OUT) {
            if (user_thing(e->user)) {
                const thing_t *T = thing_of(e->user);
                if (ok_enemy(T->kind)) {
                    s_tr[e->user].id = TP_NONE;
                    enemy_out(e->user);
                } else if (s_phase != PH_END) {
                    s_score += 200;
                    fx_score(e->p.x, maxf(e->p.y, -0.5f) + 0.6f, 200, 0);
                }
            }
        }
    }
}
// the enemies that fall off the end of the table, and the cheese dip
static void zones_step(void)
{
    for (int k = 0; k < L->n && k < THING_MAX; k++) {
        if (!ok_enemy(thing_of(k)->kind) || s_tr[k].out) {
            continue;
        }
        tp_body_t *b = tp_body(W, s_tr[k].id);
        if (!b) {
            s_tr[k].id = TP_NONE;
            enemy_out(k);
            continue;
        }
        if (b->xf.p.y < -0.9f) {
            enemy_out(k);
        }
    }
    if (!s_ndip) {
        return;
    }
    for (int i = 0; i < W->body_hi; i++) {
        tp_body_t *b = &W->body[i];
        if (!(b->flags & TP_ALIVE) || b->kind != TP_DYNAMIC) {
            continue;
        }
        bool in = false;
        for (int d = 0; d < s_ndip; d++) {
            if (b->xf.p.x > s_dip[d].x0 && b->xf.p.x < s_dip[d].x1 && b->xf.p.y < s_dip[d].y1 + 0.05f) {
                in = true;
            }
        }
        if (user_bear(b->user)) {
            float want = in ? 7.0f : 0.0f;
            if (in && b->ldamp < 1.0f) {
                fx_dip(b->xf.p.x, b->xf.p.y);
            }
            b->ldamp = want;
            b->adamp = in ? 6.0f : 0.4f;
        } else if (in) {
            b->ldamp = 5.0f;
            b->adamp = 5.0f;
        } else if (b->ldamp == 5.0f) {
            b->ldamp = 0;  // (the things have no damping)
            b->adamp = 0;
        }
    }
}

// ================================================================ the turns
static bool world_calm(float vmax)
{
    for (int i = 0; i < W->body_hi; i++) {
        const tp_body_t *b = &W->body[i];
        if (!(b->flags & TP_ALIVE) || b->kind != TP_DYNAMIC || !(b->flags & TP_AWAKE)) {
            continue;
        }
        if (b->user >= 0 && b->user < THING_MAX && L && b->user < L->n && thing_of(b->user)->kind == OK_BALLOON) {
            continue;
        }
        if (b->life0 > 0) {
            continue;  // (a small piece that fades)
        }
        if (b->v.x * b->v.x + b->v.y * b->v.y > vmax * vmax || absf(b->w) > 1.0f) {
            return false;
        }
    }
    return true;
}
static void bear_clear(void)
{
    for (int i = 0; i < s_nbody; i++) {
        tp_body_t *b = tp_body(W, s_bear[i]);
        if (b) {
            fx_bear_pop(b->xf.p.x, b->xf.p.y, s_bear_kind);
            tp_remove(W, s_bear[i]);
        }
    }
    s_nbody = 0;
    s_weld = s_stuck = TP_NONE;
}
static int stars_for(uint32_t score)
{
    if (!s_won) {
        return 0;
    }
    return score >= L->s3 ? 3 : score >= L->s2 ? 2 : 1;
}
static void level_end(bool won)
{
    s_won = won;
    if (won) {
        s_score += (uint32_t)s_nqueue * 10000;
    }
    s_stars_now = stars_for(s_score);
    s_phase = PH_END;
    s_tick = 0;
}
#define INTRO_TICKS 330  // (the camera tour of the intro: all, the fort, the bears, all; a tap skips it)
#define HOP_TICKS   40
static void game_tick(void)
{
    // the dash ends; the bear part that goes out of the world
    if (s_dash_left > 0 && --s_dash_left == 0) {
        tp_body_t *b = tp_body(W, s_bear[0]);
        if (b) {
            b->gscale = 1.0f;
        }
    }
    tp_step(W);
    s_ticks++;
    s_tick++;
    hits_step();
    events_step();
    zones_step();
    if (s_weld != TP_NONE && !tp_joint(W, s_weld)) {
        s_weld = TP_NONE;
        s_stuck = TP_NONE;
    }
    // the path of the bear (the dots of the last shot)
    if (s_phase == PH_FLY && (s_tick % 3) == 0) {
        tp_body_t *b = bear_main();
        if (b && s_npath[s_pathi] < PATH_N) {
            s_path[s_pathi][s_npath[s_pathi]][0] = b->xf.p.x;
            s_path[s_pathi][s_npath[s_pathi]][1] = b->xf.p.y;
            s_npath[s_pathi]++;
        }
    }
    switch (s_phase) {
    case PH_INTRO:
        if (s_tick >= INTRO_TICKS) {
            s_phase = PH_HOP;
            s_tick = 0;
        }
        break;
    case PH_HOP:
        if (!s_enemies) {
            bear_clear();
            level_end(true);
            break;
        }
        if (s_tick == 1) {
            s_bear_kind = s_nqueue ? s_queue[0] : -1;
            if (s_nqueue) {
                memmove(s_queue, s_queue + 1, (size_t)(s_nqueue - 1));
                s_nqueue--;
            }
        }
        if (s_tick >= HOP_TICKS) {
            s_phase = PH_AIM;
            s_tick = 0;
        }
        break;
    case PH_AIM:
        if (!s_enemies) {
            bear_clear();
            level_end(true);
        }
        break;
    case PH_FLY: {
        // the bear ends its turn when all its parts are still or gone (or after 9 s)
        bool any = false, moving = false;
        for (int i = 0; i < s_nbody; i++) {
            tp_body_t *b = tp_body(W, s_bear[i]);
            if (!b) {
                continue;
            }
            any = true;
            if (b->v.x * b->v.x + b->v.y * b->v.y > 0.35f * 0.35f) {
                moving = true;
            }
        }
        s_rest_ticks = moving ? 0 : s_rest_ticks + 1;
        if (!any || s_rest_ticks > 24 || s_tick > 540) {
            s_phase = PH_WATCH;
            s_tick = 0;
            s_calm_ticks = 0;
        }
        break;
    }
    case PH_WATCH:
        s_calm_ticks = world_calm(0.25f) ? s_calm_ticks + 1 : 0;
        if (s_calm_ticks > 24 || s_tick > (s_enemies ? 420 : 200)) {
            if (!s_enemies) {
                bear_clear();
                level_end(true);
            } else if (!s_nqueue) {
                bear_clear();
                level_end(false);
            } else {
                bear_clear();
                s_phase = PH_HOP;
                s_tick = 0;
            }
        }
        break;
    default:
        break;
    }
}
