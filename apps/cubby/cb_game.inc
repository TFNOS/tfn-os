// CUBBY: the play (included by app_cubby.c). The thumbs, the physics of the robot (a fixed step of 1/60 s of game
// time; the picture blends between two steps), the cubes (scoop, press, place, pick up, the spring, the balloon cube),
// the jump (a tap = a hop; a hold = the big jump, and the time goes slow at its top), the SUNDROPS and the sparks,
// the camera, the particles and the sounds.
//
// THE FORWARD JUMP (the way of THE MODEL, so the robot can jump forward and not only in place): a stick that loses its thumb for up to STICK_KEEP
// frames keeps its value and takes its point back near its base (a panel can lose a point when the second thumb comes
// down; a stick that starts again at the thumb has a zero value and the robot stops). The jump keeps the run speed,
// and the stick gives its speed in its direction at once (no turn on the spot first). In the air the stick steers in
// each direction (the speed over the ground goes toward the stick); the facing turns as fast as on the ground. A jump
// comes also COYOTE s after an edge, and from a press JBUF s before the landing. On the ground the treads stay as they
// were (a turn first; a light push only turns the robot: the aim of a cube).

#define G_ACC    22.0f
#define V_HOP    7.7f   // a hop: up 1.35 units (onto a step of 1)
#define V_BOOST  7.76f  // the big jump: the push at 0.18 s of a held jump (up 2.4 units in all)
#define V_SPRING 14.2f  // the spring cube: up 4.6 units
#define V_DJ                                                                                                           \
    7.7f    // the double jump: a second press in the air, a hop more (at the top of a hop: up 2.5 in
            // all, onto a step of 2; at the top of the big jump: up 3.5, onto a step of 3; never 4)
#define RUN_MAX    3.6f
#define R_BOX      0.34f
#define CLIMB      0.30f
#define HOLD_BIG   0.16f  // a held JUMP after this time = the big jump
#define SLOW_REAL  1.7f   // the slow time at the top of a big jump (real seconds)
#define SLOW_SCALE 0.22f  // the speed of the game time in the slow time
#define ACC_AIR    12.0f  // the change of the speed over the ground in the air (units/s each s)
#define COYOTE     0.10f  // a jump after the robot drives off an edge (s)
#define JBUF       0.12f  // a press of JUMP waits this long for the ground (s)
#define STEP_DT    (1.0f / 60.0f)

enum {
    A_NONE,
    A_SCOOP,
    A_PLACE,
    A_PICK,
    A_NO,
    A_CHEER,
    A_SPLASH,
    A_BOOT
};
static const float A_LEN[] = {0, 1.15f, 0.5f, 0.42f, 0.55f, 2.2f, 1.4f, 2.0f};

typedef struct {
    v3 p, v, p0;      // the place (the ground under the treads), the speed, the place of the last step
    float yaw, yaw0;  // the facing, the facing of the last step
    float spin;       // the turn speed (rad/s)
    float speed;      // the speed along the facing (in the air: the part of avx, avz along the facing)
    float avx, avz;   // in the air: the speed over the ground (the stick steers it)
    float jbuf;       // a press of JUMP that waits (s)
    bool ground;
    float air_t;   // the time since the ground
    int on_float;  // the balloon cube under the treads (-1 none)
    v3 safe;       // the last safe place (a respawn after the sea)
    float safe_yaw;
    int act;
    float act_t;
    int carry;
    float jump_t;  // the time since the jump (-1: no jump now)
    bool jump_held;
    bool big;  // this jump is a big jump (the slow time at its top)
    bool slow_used;
    bool dj_used;  // the double jump of this time in the air is used
    float land_v;
    float idle_t;
    float bonk_t;
    int act_cell_i, act_cell_j, act_kind, act_float;
    v3 act_from;
} player_t;
static player_t P;
static rig_t R;

// the balloon cubes: free things (not in a stack). A balloon cube on the ground goes up with the robot on it (a lift);
// a balloon cube that the robot drops in the air stays there (it sinks slowly under the robot)
typedef struct {
    bool on;
    int i, j;
    float y, base, t;
    bool hung;
    float pop;
} flo_t;
#define NFLO 3
static flo_t s_flo[NFLO];
static int s_flo_next;
// a cube that falls to the top of a stack
typedef struct {
    bool on;
    int i, j, kind;
    float y, vy;
} fall_t;
#define NFALL 4
static fall_t s_fall[NFALL];
// the cube that the robot throws to a cell (round 3: an animation of the throw): an arc from over the head
// to the cell, one turn in the air; it lands in the stack (or as a balloon cube) at the end
typedef struct {
    bool on;
    int kind, i, j;
    v3 a, b;    // from, to (the middle of the cube)
    float yaw;  // the facing of the robot at the throw
    float t, dur, top;
} toss_t;
static toss_t s_toss;
#define TOSS_AT 0.14f         // (the time in A_PLACE when the cube leaves the hands)
static float s_coil[GX][GZ];  // the spring on the top of a cell: 1 = free, less = pressed
static float s_pop[GX][GZ];   // the time since the top cube of the cell came (its pop)

// the particles
enum {
    PT_DUST,
    PT_BIT,
    PT_STEAM,
    PT_STAR,
    PT_SPLASH,
    PT_CONF
};
typedef struct {
    v3 p, v;
    float life, max, size;
    uint8_t kind, r, g, b;
} part_t;
#define NPART 72
static part_t s_part[NPART];
static int s_part_next;

static int s_ndrops, s_nsparks;
static float s_t;                  // the game time (seconds; slow in the slow time)
static float s_rt;                 // the real time (seconds)
static float s_acc;                // the time not yet stepped
static float s_slow, s_slow_left;  // the slow time: 0..1 (the blend), the real seconds left
static float s_shake;
static float s_win_t;        // the time since the last SUNDROP (-1: not yet)
static int s_drop_name;      // the SUNDROP of the name on the screen (-1: none)
static float s_drop_name_t;  // the real time since that SUNDROP
static float s_mech_move;    // a machine moves now: 0..1 (near)
static float s_hint_t;       // the real time of play (the control hints fade)

// the friend on a second board (cb_net.inc): the events of the world that my robot makes, the friend robot on a
// balloon cube, its push
enum {
    NE_STACK_ADD = 1,
    NE_STACK_POP,
    NE_FLOAT_NEW,
    NE_FLOAT_OFF,
    NE_DROP_GOT,
    NE_SPARK_GOT,
    NE_RESTART
};
static void net_event(int type, int a, int b, int c, float y);
static bool net_friend_on_float(int k);
static void life_reset(void);
static void life_step(float dt);
static bool net_friend_on_cell(int i, int j, float t);
static bool net_friend_in_mech(const mech_t *q);
static void net_push(void);
// the music (app_cubby.c): a jingle, then the island loop again
static void music_jingle(int j, int ms);

// ---------------------------------------------------------------- the sounds
enum {
    FX_HOP,
    FX_BIG,
    FX_LAND,
    FX_CRUNCH,
    FX_CLUNK,
    FX_PICK,
    FX_SPRING,
    FX_DROP,
    FX_SPARK,
    FX_SPLASH,
    FX_BEEP,
    FX_NO,
    FX_SLOW,
    FX_POP,
    FX_STEAM,
    FX_N
};
static const char *const FX_NAMES[FX_N] = {"hop",   "big",    "land", "crunch", "clunk", "pick", "spring", "sundrop",
                                           "spark", "splash", "beep", "no",     "slow",  "pop",  "steam"};
static bool s_sound = true;
static void fx(int id, int vol, float rate)
{
    if (!s_sound) {
        return;
    }
    float r = rate * (1 - 0.35f * s_slow);
    tfsfx_play(id, vol, r < 0.25f ? 0.25f : r);
}

// ---------------------------------------------------------------- small helpers
static inline v3 fwd_of(float yaw)
{
    return V3(fsin(yaw), 0, fcos(yaw));
}
static inline float approach(float v, float to, float d)
{
    return v < to ? (v + d > to ? to : v + d) : (v - d < to ? to : v - d);
}
// the robot leaves the ground: the speed over the ground in the air starts as the run speed along the facing
static void air_from_run(void)
{
    v3 f = fwd_of(P.yaw);
    P.avx = f.x * P.speed;
    P.avz = f.z * P.speed;
    P.air_t = 0;
}
// a fast atan2 (radians): the error is about 0.005 rad
static float fatan2(float y, float x)
{
    float ax = absf(x), ay = absf(y);
    if (ax < 1e-9f && ay < 1e-9f) {
        return 0;
    }
    float a = ax > ay ? ay / ax : ax / ay, s = a * a;
    float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
    if (ay > ax) {
        r = 1.57079637f - r;
    }
    if (x < 0) {
        r = PI_F - r;
    }
    if (y < 0) {
        r = -r;
    }
    return r;
}
static void part_add(v3 p, v3 v, float life, float size, int kind, uint32_t hex)
{
    part_t *q = &s_part[s_part_next];
    s_part_next = (s_part_next + 1) % NPART;
    q->p = p;
    q->v = v;
    q->life = q->max = life;
    q->size = size;
    q->kind = (uint8_t)kind;
    q->r = (uint8_t)(hex >> 16);
    q->g = (uint8_t)(hex >> 8);
    q->b = (uint8_t)hex;
}
static float frand(void)
{
    return (esp_random() & 0xFFFF) / 65535.0f;
}
static float frs(void)
{
    return frand() * 2 - 1;
}
static void dust_burst(v3 at, int n, float spd)
{
    for (int i = 0; i < n; i++) {
        float a = TAU_F * i / n + frand();
        part_add(V3(at.x + fcos(a) * 0.3f, at.y + 0.05f, at.z + fsin(a) * 0.3f),
                 V3(fcos(a) * spd, 0.4f + frand() * 0.5f, fsin(a) * spd), 0.5f + frand() * 0.3f,
                 0.10f + frand() * 0.06f, PT_DUST, 0xFFF2D8);
    }
}

// ---------------------------------------------------------------- the world queries
static int float_at(int i, int j)
{
    for (int k = 0; k < NFLO; k++) {
        if (s_flo[k].on && s_flo[k].i == i && s_flo[k].j == j) {
            return k;
        }
    }
    return -1;
}
// the highest top under the box at (x, z) that is not higher than ylim; *fl = the balloon cube of it (or -1)
static float floor_at(float x, float z, float ylim, int *fl)
{
    float best = -20;
    int bf = -1;
    int i0 = ifloor(x - R_BOX), i1 = ifloor(x + R_BOX), j0 = ifloor(z - R_BOX), j1 = ifloor(z + R_BOX);
    for (int i = i0; i <= i1; i++) {
        for (int j = j0; j <= j1; j++) {
            float t = cell_top(i, j);
            if (t <= ylim && t > best) {
                best = t;
                bf = -1;
            }
        }
    }
    for (int k = 0; k < NFLO; k++) {
        const flo_t *f = &s_flo[k];
        if (!f->on) {
            continue;
        }
        if (f->i + 1 <= x - R_BOX || f->i >= x + R_BOX || f->j + 1 <= z - R_BOX || f->j >= z + R_BOX) {
            continue;
        }
        float t = f->y + 1;
        if (t <= ylim + 0.02f && t > best) {
            best = t;
            bf = k;
        }
    }
    // the gallery of the lighthouse (a disc, not only the 2 x 2 cells)
    float dx = x - LH_X, dz = z - LH_Z;
    if (dx * dx + dz * dz < 1.5f * 1.5f && LH_TOP <= ylim && LH_TOP > best) {
        best = LH_TOP;
        bf = -1;
    }
    if (fl) {
        *fl = bf;
    }
    return best;
}
// the box at (x, z) with its feet at y hits a wall (a cell, a balloon cube or a round thing higher than a step).
// ground: the robot is on the ground, so the sea is a wall too
static bool box_hits(float x, float z, float y, bool ground)
{
    int i0 = ifloor(x - R_BOX), i1 = ifloor(x + R_BOX), j0 = ifloor(z - R_BOX), j1 = ifloor(z + R_BOX);
    for (int i = i0; i <= i1; i++) {
        for (int j = j0; j <= j1; j++) {
            if (cell_water(i, j)) {
                if (ground && y < 0.6f) {
                    // (only the part of the sea where the beach is not under the box: the corner of the box over the sea)
                    float cx = clampf(x, (float)i, i + 1.0f), cz = clampf(z, (float)j, j + 1.0f);
                    if (absf(cx - x) < R_BOX - 0.12f && absf(cz - z) < R_BOX - 0.12f) {
                        return true;
                    }
                }
                continue;
            }
            if (cell_top(i, j) > y + CLIMB) {
                // the gallery disc is not a wall above the top of the tower
                return true;
            }
        }
    }
    for (int k = 0; k < NFLO; k++) {
        const flo_t *f = &s_flo[k];
        if (!f->on) {
            continue;
        }
        if (f->i + 1 <= x - R_BOX || f->i >= x + R_BOX || f->j + 1 <= z - R_BOX || f->j >= z + R_BOX) {
            continue;
        }
        if (f->y < y + 1.0f && f->y + 1 > y + CLIMB) {
            return true;
        }
    }
    for (int k = 0; k < s_nrc; k++) {
        const rcol_t *c = &s_rc[k];
        if (y + 1.1f < c->y0 || y > c->y1) {
            continue;
        }
        float dx = x - c->x, dz = z - c->z, rr = c->r + R_BOX * 0.9f;
        if (dx * dx + dz * dz < rr * rr) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- the targets of the hands
// the cell in front of the robot (for a cube): the first cell along the grid axis nearest to the facing that the box
// of the robot does not touch
static void front_cell(int *ci, int *cj)
{
    v3 f = fwd_of(P.yaw);
    int i = ifloor(P.p.x), j = ifloor(P.p.z);
    if (absf(f.x) >= absf(f.z)) {
        i = f.x > 0 ? ifloor(P.p.x + R_BOX - 0.02f) + 1 : ifloor(P.p.x - R_BOX + 0.02f) - 1;
    } else {
        j = f.z > 0 ? ifloor(P.p.z + R_BOX - 0.02f) + 1 : ifloor(P.p.z - R_BOX + 0.02f) - 1;
    }
    *ci = i;
    *cj = j;
}
// the box of the robot covers the cell (i, j)
static bool box_on_cell(float x, float z, int i, int j)
{
    return !(i + 1 <= x - R_BOX + 0.02f || i >= x + R_BOX - 0.02f || j + 1 <= z - R_BOX + 0.02f ||
             j >= z + R_BOX - 0.02f);
}
// a cube can go on the cell (i, j) in front: the top there (out)
static bool place_ok(int i, int j, float *top)
{
    if (!cell_in(i, j) || cell_water(i, j)) {
        return false;
    }
    if (float_at(i, j) >= 0) {
        return false;
    }
    if (s_nstk[i][j] >= STK) {
        return false;
    }
    float t = cell_top(i, j);
    if (t > P.p.y + 1.05f || t < P.p.y - 3.05f) {
        return false;
    }
    if (box_on_cell(P.p.x, P.p.z, i, j) && t + 1 > P.p.y + CLIMB) {
        return false;
    }
    if (s_cf[i][j] & CF_NOPLACE) {
        return false;  // (not on the crates, the tower, a gate)
    }
    *top = t;
    return true;
}
// the pile in front that the arms can reach (-1 none)
static int pile_near(void)
{
    v3 f = fwd_of(P.yaw);
    for (int k = 0; k < NPILE; k++) {
        float dx = s_pile[k].x - P.p.x, dz = s_pile[k].z - P.p.z;
        float d = fsqrt(dx * dx + dz * dz);
        if (d > 1.45f || absf(s_pile[k].y - P.p.y) > 0.6f) {
            continue;
        }
        if (d > 0.3f && (dx * f.x + dz * f.z) / d < 0.25f) {
            continue;
        }
        return k;
    }
    return -1;
}
// a cube in front that the arms can take back: the cell, or a balloon cube (*fl)
static bool pick_ok(int *ci, int *cj, int *fl)
{
    int i, j;
    front_cell(&i, &j);
    *ci = i;
    *cj = j;
    *fl = -1;
    int k = float_at(i, j);
    if (k >= 0) {
        const flo_t *f = &s_flo[k];
        if (absf(f->y - f->base) > 0.05f || f->hung) {
            return false;
        }
        if (f->y + 1 > P.p.y + 1.1f || f->y + 1 < P.p.y - 1.1f) {
            return false;
        }
        *fl = k;
        return true;
    }
    if (!cell_in(i, j) || !s_nstk[i][j]) {
        return false;
    }
    float t = cell_top(i, j);
    return t <= P.p.y + 1.1f && t >= P.p.y - 1.1f;
}
// what the ACT button does now (for its icon): 0 nothing, 1 scoop, 2 place, 3 pick up
static int act_kind(void)
{
    if (P.act != A_NONE) {
        return 0;
    }
    if (!P.ground) {
        return P.carry ? 2 : 0;
    }
    if (P.carry) {
        int i, j;
        float t;
        front_cell(&i, &j);
        return place_ok(i, j, &t) ? 2 : 0;
    }
    if (pile_near() >= 0) {
        return 1;
    }
    int i, j, fl;
    return pick_ok(&i, &j, &fl) ? 3 : 0;
}

// ---------------------------------------------------------------- the cubes
static void float_new(int i, int j, float y, bool hung)
{
    int k = -1;
    for (int q = 0; q < NFLO; q++) {
        if (!s_flo[q].on) {
            k = q;
            break;
        }
    }
    if (k < 0) {
        // (all three are out: the oldest one floats away)
        k = s_flo_next;
        s_flo_next = (s_flo_next + 1) % NFLO;
        v3 c = V3(s_flo[k].i + 0.5f, s_flo[k].y + 0.5f, s_flo[k].j + 0.5f);
        for (int n = 0; n < 10; n++) {
            part_add(c, V3(frs() * 2, frand() * 2, frs() * 2), 0.6f, 0.08f, PT_CONF, 0x58E2FF);
        }
        fx(FX_POP, 80, 1.0f);
        if (P.on_float == k) {
            P.on_float = -1;
        }
    }
    flo_t *f = &s_flo[k];
    f->on = true;
    f->i = i;
    f->j = j;
    f->y = y;
    f->base = y;
    f->t = 0;
    f->hung = hung;
    f->pop = 0;
    net_event(NE_FLOAT_NEW, i, j, k | (hung ? 16 : 0), y);
}
static void stack_add(int i, int j, int kind)
{
    if (!cell_in(i, j) || s_nstk[i][j] >= STK) {
        return;
    }
    s_stk[i][j][s_nstk[i][j]++] = (uint8_t)kind;
    s_pop[i][j] = 0;
    net_event(NE_STACK_ADD, i, j, kind, 0);
    if (kind == CB_SPRING) {
        s_coil[i][j] = 1;
    }
    v3 c = V3(i + 0.5f, cell_top(i, j) - 1, j + 0.5f);
    dust_burst(c, 8, 1.4f);
    fx(FX_CLUNK, 85, 0.9f + frand() * 0.2f);
    s_shake = FMAX(s_shake, 0.12f);
}
// a cube leaves the hands: to the cell (i, j) from the height y (it falls to the top of the stack)
static void cube_drop(int kind, int i, int j, float y)
{
    if (kind == CB_FLOAT) {
        return;
    }
    for (int k = 0; k < NFALL; k++) {
        if (!s_fall[k].on) {
            s_fall[k] = (fall_t){true, i, j, kind, y, 0};
            return;
        }
    }
    stack_add(i, j, kind);
}

// ---------------------------------------------------------------- the actions
static void act_start(int a)
{
    P.act = a;
    P.act_t = 0;
}
static void do_act(void)
{
    if (P.act != A_NONE && P.act != A_NO) {
        return;
    }
    if (!P.ground) {
        if (!P.carry) {
            return;
        }
        // in the air: the cube goes under the treads (a balloon cube stays in the air; the others fall to the stack)
        int i = ifloor(P.p.x), j = ifloor(P.p.z);
        if (!cell_in(i, j) || float_at(i, j) >= 0) {
            fx(FX_NO, 60, 1);
            return;
        }
        float y = P.p.y - 1.02f;
        float top = cell_top(i, j);
        if (P.carry == CB_FLOAT) {
            if (y < top) {
                y = top;
            }
            if (cell_water(i, j) && y < SEA_Y - 0.3f) {
                y = SEA_Y - 0.3f;
            }
            float_new(i, j, y, true);
            fx(FX_CLUNK, 70, 1.4f);
            for (int n = 0; n < 8; n++) {
                part_add(V3(i + 0.5f, y + 0.5f, j + 0.5f), V3(frs() * 1.5f, frs(), frs() * 1.5f), 0.5f, 0.07f, PT_STAR,
                         0xC8FFFF);
            }
        } else {
            if (cell_water(i, j)) {
                fx(FX_SPLASH, 60, 1.3f);
            } else {
                cube_drop(P.carry, i, j, y);
            }
        }
        P.carry = 0;
        R.carry = 0;
        // (the slow time ends soon after the drop)
        if (s_slow_left > 0.35f) {
            s_slow_left = 0.35f;
        }
        return;
    }
    if (P.carry) {
        int i, j;
        float t;
        front_cell(&i, &j);
        if (!place_ok(i, j, &t)) {
            act_start(A_NO);
            fx(FX_NO, 55, 1);
            return;
        }
        P.act_cell_i = i;
        P.act_cell_j = j;
        P.act_kind = P.carry;
        act_start(A_PLACE);
        return;
    }
    int k = pile_near();
    if (k >= 0) {
        P.act_kind = s_pile[k].kind;
        P.act_from = V3(s_pile[k].x, s_pile[k].y + 0.2f, s_pile[k].z);
        s_pile[k].pulse = 1;
        act_start(A_SCOOP);
        fx(FX_BEEP, 60, 1.2f);
        return;
    }
    int i, j, fl;
    if (pick_ok(&i, &j, &fl)) {
        P.act_cell_i = i;
        P.act_cell_j = j;
        P.act_float = fl;
        act_start(A_PICK);
        return;
    }
    act_start(A_NO);
    fx(FX_NO, 50, 1);
}
// the timeline of an action (game time dt)
static void act_step(float dt)
{
    if (P.act == A_NONE) {
        return;
    }
    float t0 = P.act_t;
    P.act_t += dt;
    float t = P.act_t;
#define CROSS(x) (t0 < (x) && t >= (x))
    switch (P.act) {
    case A_SCOOP:
        if (t < 0.55f && ((int)(t0 * 30) != (int)(t * 30))) {
            // the bits of junk fly into the open hatch
            v3 f = fwd_of(P.yaw);
            v3 to = V3(P.p.x + f.x * 0.36f, P.p.y + 0.55f, P.p.z + f.z * 0.36f);
            v3 fr = vadd(P.act_from, V3(frs() * 0.25f, frand() * 0.2f, frs() * 0.25f));
            static const uint32_t BITC[3][3] = {
                    {0xFF4E50, 0xFFD23C, 0x3E9EFF}, {0xFF79C2, 0xFFFFFF, 0xC8D2E2}, {0x58E2FF, 0xFFA8D4, 0xFFF0A0}};
            int kk = P.act_kind - 1;
            part_add(fr, vmul(vsub(to, fr), 1.0f / 0.28f), 0.28f, 0.07f, PT_BIT, BITC[kk][esp_random() % 3]);
        }
        if (CROSS(0.3f)) {
            fx(FX_CRUNCH, 90, 1.0f);
        }
        if (CROSS(0.62f)) {
            fx(FX_CRUNCH, 80, 0.8f);
            s_shake = FMAX(s_shake, 0.1f);
        }
        if (CROSS(0.78f)) {
            fx(FX_STEAM, 60, 1.0f);
            for (int n = 0; n < 6; n++) {
                part_add(V3(P.p.x + frs() * 0.2f, P.p.y + 1.0f, P.p.z + frs() * 0.2f),
                         V3(frs() * 0.3f, 1.2f + frand(), frs() * 0.3f), 0.8f, 0.14f, PT_STEAM, 0xFFFFFF);
            }
        }
        if (CROSS(0.95f)) {
            P.carry = P.act_kind;
            R.carry_s = 0.25f;
            fx(FX_PICK, 80, 1.2f);
        }
        break;
    case A_PLACE:
        if (CROSS(TOSS_AT)) {
            // the throw: the cube leaves the hands over the head and flies to its cell
            int i = P.act_cell_i, j = P.act_cell_j;
            float top;
            if (place_ok(i, j, &top) && !s_toss.on) {
                v3 f = fwd_of(P.yaw);
                s_toss = (toss_t){true,
                                  P.act_kind,
                                  i,
                                  j,
                                  V3(P.p.x + f.x * 0.1f, P.p.y + 1.55f, P.p.z + f.z * 0.1f),
                                  V3(i + 0.5f, top + 0.5f, j + 0.5f),
                                  P.yaw,
                                  0,
                                  0,
                                  top};
                s_toss.dur = 0.26f + 0.04f * clampf(P.p.y + 1.0f - top, 0, 3);
                P.carry = 0;
                fx(FX_STEAM, 45, 1.7f);
            } else {
                fx(FX_NO, 55, 1);
            }
        }
        break;
    case A_PICK:
        if (CROSS(0.22f)) {
            int i = P.act_cell_i, j = P.act_cell_j;
            if (P.act_float >= 0 && s_flo[P.act_float].on) {
                P.carry = CB_FLOAT;
                s_flo[P.act_float].on = false;
                net_event(NE_FLOAT_OFF, 0, 0, P.act_float, 0);
            } else if (cell_in(i, j) && s_nstk[i][j]) {
                P.carry = s_stk[i][j][--s_nstk[i][j]];
                s_pop[i][j] = 1;
                net_event(NE_STACK_POP, i, j, 0, 0);
            }
            if (P.carry) {
                R.carry_s = 0.5f;
                fx(FX_PICK, 75, 1.0f);
            }
        }
        break;
    case A_CHEER:
        if (CROSS(0.25f)) {
            P.v.y = 5.5f;
            P.ground = false;
            P.big = false;
            air_from_run();
        }
        break;
    case A_SPLASH:
        if (CROSS(1.1f)) {
            P.p = P.safe;
            P.p0 = P.p;
            P.yaw = P.yaw0 = P.safe_yaw;
            P.v = V3(0, 0, 0);
            P.ground = true;
            P.on_float = -1;
            R.flash = 1;
            fx(FX_BEEP, 70, 1.0f);
        }
        break;
    default:
        break;
    }
#undef CROSS
    if (P.act_t >= A_LEN[P.act]) {
        P.act = A_NONE;
    }
}

// ---------------------------------------------------------------- the thumbs
// the layout of the buttons (logical px of the frame)
#if CB_WIDE
#define JX         552
#define JY         (112 + CB_DY)
#define JR         40
#define AX         452
#define AY         (124 + CB_DY)
#define AR         34
#define STICK_R    38.0f
#define STICK_ZONE 300
#elif CB_ROUND
// (the round: JUMP and ACT on the lower right of the circle, the stick in the left part; sizes from the frame)
#define JX         (OW * 76 / 100)
#define JY         (OH * 69 / 100)
#define JR         (OW * 40 / 466)
#define AX         (OW * 59 / 100)
#define AY         (OH * 82 / 100)
#define AR         (OW * 32 / 466)
#define STICK_R    (OW * 44.0f / 466)
#define STICK_ZONE (OW * 45 / 100)
#else
#define JX         278
#define JY         198
#define JR         30
#define AX         212
#define AY         212
#define AR         25
#define STICK_R    34.0f
#define STICK_ZONE 140
#endif
enum {
    TR_NONE,
    TR_STICK,
    TR_JUMP,
    TR_ACT,
    TR_CAM
};
typedef struct {
    bool on;
    int role, miss;
    float x, y, x0, y0, bx, by;
    bool seen;
} touch_t;
#define NTOUCH     4
#define STICK_KEEP 4  // the frames that a stick keeps its value with no point
static touch_t s_tc[NTOUCH];
static float s_stick_x, s_stick_y;  // -1..1 (y + = up on the screen = away from the camera)
static bool s_jump_down, s_jump_edge, s_act_edge;
static float s_cam_drag;  // the turn of the camera by a finger in this frame (rad)
static float s_cam_hold;  // real seconds: the camera does not follow the robot after a drag

static int touch_role(float x, float y)
{
    float dj = (x - JX) * (x - JX) + (y - JY) * (y - JY), da = (x - AX) * (x - AX) + (y - AY) * (y - AY);
    if (dj < (JR + 16) * (JR + 16) && dj <= da) {
        return TR_JUMP;
    }
    if (da < (AR + 16) * (AR + 16)) {
        return TR_ACT;
    }
    if (x < STICK_ZONE) {
        return TR_STICK;
    }
    return TR_CAM;
}
static void thumbs_read(void)
{
    int16_t px[INPUT_MAX_POINTS], py[INPUT_MAX_POINTS];
    int n = input_points(px, py, INPUT_MAX_POINTS);
    for (int k = 0; k < NTOUCH; k++) {
        s_tc[k].seen = false;
    }
    for (int q = 0; q < n; q++) {
        int x, y;
        gfx_touch_to_logical(px[q], py[q], &x, &y);
        // (the nearest track of the last frame takes the point; a stick that lost its point takes it back near its
        // base, but not a new press of a button or of the camera side; a new finger starts a new track)
        int best = -1;
        float bd = 120.0f * 120.0f;
        for (int k = 0; k < NTOUCH; k++) {
            const touch_t *t = &s_tc[k];
            if (!t->on || t->seen) {
                continue;
            }
            float d = (t->x - x) * (t->x - x) + (t->y - y) * (t->y - y);
            if (t->role == TR_STICK && t->miss) {
                if (d > 40.0f * 40.0f && touch_role((float)x, (float)y) != TR_STICK) {
                    continue;
                }
                float db = (t->bx - x) * (t->bx - x) + (t->by - y) * (t->by - y);
                if (db < 150.0f * 150.0f && db < d) {
                    d = db;
                }
            }
            if (d < bd) {
                bd = d;
                best = k;
            }
        }
        if (best < 0) {
            for (int k = 0; k < NTOUCH; k++) {
                if (!s_tc[k].on) {
                    best = k;
                    break;
                }
            }
            if (best < 0) {
                continue;
            }
            touch_t *t = &s_tc[best];
            t->on = true;
            t->miss = 0;
            t->role = touch_role((float)x, (float)y);
            t->x0 = t->x = t->bx = (float)x;
            t->y0 = t->y = t->by = (float)y;
            if (t->role == TR_JUMP) {
                s_jump_edge = true;
            }
            if (t->role == TR_ACT) {
                s_act_edge = true;
            }
        }
        touch_t *t = &s_tc[best];
        if (t->role == TR_CAM) {
            s_cam_drag += ((float)x - t->x) * 0.0085f;  // (a slide to the right turns the view to the right)
        }
        t->x = (float)x;
        t->y = (float)y;
        t->seen = true;
        t->miss = 0;
    }
    s_stick_x = s_stick_y = 0;
    s_jump_down = false;
    for (int k = 0; k < NTOUCH; k++) {
        touch_t *t = &s_tc[k];
        if (!t->on) {
            continue;
        }
        if (!t->seen) {
            // (a stick keeps its last value for a few frames; the other roles end at once)
            if (t->role != TR_STICK || ++t->miss > STICK_KEEP) {
                t->on = false;
                continue;
            }
        }
        if (t->role == TR_STICK) {
            float dx = t->x - t->bx, dy = t->y - t->by, d = fsqrt(dx * dx + dy * dy);
            // (the base follows a thumb that goes far: no dead stick at the edge)
            if (d > STICK_R * 1.5f) {
                t->bx = t->x - dx / d * STICK_R * 1.5f;
                t->by = t->y - dy / d * STICK_R * 1.5f;
                dx = t->x - t->bx;
                dy = t->y - t->by;
                d = STICK_R * 1.5f;
            }
            float m = d / STICK_R;
            if (m < 0.12f) {
                m = 0;
            } else {
                m = (m - 0.12f) / 0.88f;
            }
            if (m > 1) {
                m = 1;
            }
            if (d > 0.01f) {
                s_stick_x = dx / d * m;
                s_stick_y = -dy / d * m;
            }
        } else if (t->role == TR_JUMP) {
            s_jump_down = true;
        } else if (t->role == TR_CAM) {
            s_cam_hold = 1.6f;
        }
    }
}

// ---------------------------------------------------------------- the physics step (game time)
static float s_cam_yaw;
// the jump (a hop; held: the big jump). It keeps the run speed; the stick (want: the direction from the view of the
// camera, its length mag) gives at once its speed in its direction (no turn on the spot first)
static void jump_start(v3 want, float mag)
{
    P.ground = false;
    P.v = V3(0, V_HOP, 0);
    P.jump_t = 0;
    P.jump_held = s_jump_down;
    P.jbuf = 0;
    P.big = false;
    P.slow_used = false;
    P.dj_used = false;
    P.on_float = -1;
    air_from_run();
    if (mag > 0.05f) {
        float ux = want.x / mag, uz = want.z / mag, along = P.avx * ux + P.avz * uz, to = RUN_MAX * mag;
        if (along < to) {
            P.avx += ux * (to - along);
            P.avz += uz * (to - along);
        }
    }
    R.squash = 1.18f;
    fx(FX_HOP, 70, 1.0f);
}
// the double jump: a new push up from the air, the stick sets the direction again; a puff under the treads
static void double_jump(v3 want, float mag)
{
    P.v.y = V_DJ;
    P.dj_used = true;
    P.jbuf = 0;
    P.jump_t = -1;
    if (mag > 0.3f) {
        float to = RUN_MAX * mag, ux = want.x / mag, uz = want.z / mag;
        P.avx += (ux * to - P.avx) * 0.75f;
        P.avz += (uz * to - P.avz) * 0.75f;
    }
    R.squash = 1.26f;
    for (int n = 0; n < 8; n++) {
        float a = TAU_F * n / 8;
        part_add(V3(P.p.x + fcos(a) * 0.25f, P.p.y + 0.05f, P.p.z + fsin(a) * 0.25f),
                 V3(fcos(a) * 1.8f, -0.6f, fsin(a) * 1.8f), 0.45f, 0.11f, PT_STEAM, 0xFFFFFF);
    }
    fx(FX_HOP, 75, 1.32f);
    fx(FX_STEAM, 40, 1.5f);
}
// the robot is in a cube or a balloon cube (round 3: the cubes that stuck to the robot: a cube that fell,
// or a balloon cube that came down, into a cell that the box of the robot covers; then each move hits it): up onto
// it when it is low, else out to the nearest free place
static void unstick(void)
{
    if (P.act == A_SPLASH || !box_hits(P.p.x, P.p.z, P.p.y, P.ground)) {
        return;
    }
    float top = floor_at(P.p.x, P.p.z, P.p.y + 1.2f, NULL);
    if (top > P.p.y && !box_hits(P.p.x, P.p.z, top, P.ground)) {
        P.p.y = top;
        ESP_LOGI(TAG, "CUBBY: the robot was in a cube: up onto it (%.1f %.1f %.1f)", (double)P.p.x, (double)P.p.y,
                 (double)P.p.z);
        return;
    }
    for (int r = 1; r <= 12; r++) {
        for (int a = 0; a < 16; a++) {
            float x = P.p.x + fcos(a * (TAU_F / 16)) * r * 0.1f, z = P.p.z + fsin(a * (TAU_F / 16)) * r * 0.1f;
            float y = floor_at(x, z, P.p.y + CLIMB, NULL);
            if (y > P.p.y - 1.5f && !box_hits(x, z, y, P.ground)) {
                ESP_LOGI(TAG, "CUBBY: the robot was in a cube: out to (%.1f %.1f)", (double)x, (double)z);
                P.p = V3(x, y, z);
                P.p0 = P.p;
                return;
            }
        }
    }
}
// (a part of player_step: inline, so the machine code is the same; the size check of tests/style)
static inline __attribute__((always_inline)) void player_pick(void)
{
    // the SUNDROPS and the sparks
    v3 mid = V3(P.p.x, P.p.y + 0.6f, P.p.z);
    for (int k = 0; k < NDROP; k++) {
        drop_t *d = &s_drop[k];
        if (d->got) {
            continue;
        }
        if (vlen(vsub(d->p, mid)) < 0.85f) {
            d->got = true;
            d->t = 0;
            s_ndrops++;
            net_event(NE_DROP_GOT, 0, 0, k, 0);
            fx(FX_DROP, 100, 1.0f);
            for (int n = 0; n < 24; n++) {
                part_add(d->p, V3(frs() * 3, frand() * 3, frs() * 3), 1.2f, 0.09f, PT_CONF,
                         n & 1 ? 0xFFD23A : 0xFFFFFF);
            }
            if (P.ground) {
                act_start(A_CHEER);
            } else {
                P.act = A_NONE;
            }
            R.flash = 0.8f;
            s_drop_name = k;
            s_drop_name_t = 0;
            if (s_ndrops == NDROP) {
                s_win_t = 0;
            }
            music_jingle(s_ndrops == NDROP ? CBM_J_WIN : CBM_J_SUNDROP,
                         s_ndrops == NDROP ? CB_J_WIN_MS : CB_J_SUNDROP_MS);
        }
    }
    for (int k = 0; k < NSPARK; k++) {
        drop_t *d = &s_spark[k];
        if (d->got) {
            continue;
        }
        if (vlen(vsub(d->p, mid)) < 0.7f) {
            d->got = true;
            s_nsparks++;
            net_event(NE_SPARK_GOT, 0, 0, k, 0);
            fx(FX_SPARK, 80, 1.0f + 0.03f * s_nsparks);
            for (int n = 0; n < 8; n++) {
                part_add(d->p, V3(frs() * 1.5f, frand() * 1.5f, frs() * 1.5f), 0.5f, 0.06f, PT_STAR, 0xFFD23A);
            }
        }
    }
}

static void player_step(float dt)
{
    unstick();
    P.p0 = P.p;
    P.yaw0 = P.yaw;
    bool busy = P.act == A_SCOOP || P.act == A_PLACE || P.act == A_PICK || P.act == A_SPLASH || P.act == A_BOOT ||
                (P.act == A_CHEER && P.ground);
    // the wanted direction: the thumb, from the view of the camera
    float sx = busy ? 0 : s_stick_x, sy = busy ? 0 : s_stick_y;
    float mag = fsqrt(sx * sx + sy * sy);
    if (mag > 1) {
        mag = 1;
    }
    v3 cf = fwd_of(s_cam_yaw), cr = V3(cf.z, 0, -cf.x);
    v3 want = V3(cf.x * sy + cr.x * sx, 0, cf.z * sy + cr.z * sx);
    // the jump: a press waits JBUF for the ground; the coyote time after an edge
    if (P.jbuf > 0) {
        P.jbuf -= dt;
    }
    if (P.jbuf > 0 && P.act == A_NONE && (P.ground || (P.air_t < COYOTE && P.jump_t < 0 && P.v.y <= 0))) {
        jump_start(want, mag);
    }
    // the double jump: a press in the air (not just over the ground: there the press waits for the landing)
    if (P.jbuf > 0 && !P.ground && !P.dj_used && P.act == A_NONE && P.air_t > 0.06f) {
        float fy = floor_at(P.p.x, P.p.z, P.p.y + 0.02f, NULL);
        if (P.v.y > 0 || P.p.y - fy > 0.55f) {
            double_jump(want, mag);
        }
    }
    float want_speed = 0;
    if (mag > 0.05f) {
        float wy = fatan2(want.x, want.z);
        float d = wrap_pi(wy - P.yaw);
        // the treads turn the robot (fast, but not at once); a big turn first, then the drive (in the air the facing
        // turns as fast)
        float turn = clampf(d * 9.0f, -11.0f, 11.0f);
        P.spin = approach(P.spin, turn, 60.0f * dt);
        // (a light push only turns the robot on the spot: the aim of a cube)
        float drive = clampf((mag - 0.3f) / 0.7f, 0, 1);
        want_speed = RUN_MAX * drive * clampf(1.2f - absf(d) * 0.9f, 0, 1);
    } else {
        P.spin = approach(P.spin, 0, 50.0f * dt);
    }
    P.yaw = wrap_pi(P.yaw + P.spin * dt);
    float spd0 = P.speed;
    v3 f = fwd_of(P.yaw);
    float vx, vz;
    if (P.ground) {
        P.speed = approach(P.speed, want_speed, 13.0f * dt);
        vx = f.x * P.speed;
        vz = f.z * P.speed;
    } else {
        // in the air: the speed over the ground goes toward the stick, in each direction (a light push moves too)
        float wx = 0, wz = 0;
        if (mag > 0.05f) {
            wx = want.x * RUN_MAX;
            wz = want.z * RUN_MAX;
        }
        float acc = s_slow > 0.3f ? 16.0f : ACC_AIR;
        float dvx = wx - P.avx, dvz = wz - P.avz, dl = fsqrt(dvx * dvx + dvz * dvz), st = acc * dt;
        if (dl > st) {
            dvx *= st / dl;
            dvz *= st / dl;
        }
        P.avx += dvx;
        P.avz += dvz;
        // (the part along the facing: the treads, the camera, the robot on the board of the friend)
        P.speed = P.avx * f.x + P.avz * f.z;
        vx = P.avx + P.v.x;
        vz = P.avz + P.v.z;
        P.v.x *= (1 - dt * 0.6f);
        P.v.z *= (1 - dt * 0.6f);
    }
    // the body leans with the speed change and the turn
    float dv = (P.speed - spd0) / dt;
    R.body_pitch += (clampf(-dv * 0.012f, -0.16f, 0.16f) - R.body_pitch) * clampf(dt * 10, 0, 1);
    // the move, with a slide along the walls
    float nx = P.p.x + vx * dt, nz = P.p.z + vz * dt;
    bool hit = false;
    if (!box_hits(nx, nz, P.p.y, P.ground)) {
        P.p.x = nx;
        P.p.z = nz;
    } else {
        hit = true;
        if (!box_hits(nx, P.p.z, P.p.y, P.ground)) {
            P.p.x = nx;
        } else if (!box_hits(P.p.x, nz, P.p.y, P.ground)) {
            P.p.z = nz;
        } else if (!P.ground) {
            P.v.x = P.v.z = 0;
        }
    }
    if (hit && P.ground && P.speed > 1.5f && P.bonk_t <= 0) {
        P.bonk_t = 0.5f;
        R.squash = 0.86f;
        fx(FX_LAND, 40, 1.6f);
    }
    if (hit && P.ground) {
        P.speed *= 0.9f;
    }
    P.bonk_t -= dt;
    net_push();
    // up and down
    int fl = -1;
    if (P.ground) {
        float fy = floor_at(P.p.x, P.p.z, P.p.y + CLIMB, &fl);
        if (fl >= 0) {
            P.p.y = fy;
            P.on_float = fl;
        } else if (fy < P.p.y - 0.05f) {
            P.ground = false;
            air_from_run();
            P.v.y = 0;
            P.big = false;
            P.jump_t = -1;
            P.on_float = -1;
        } else {
            P.p.y = approach(P.p.y, fy, 4.0f * dt);
            P.on_float = -1;
        }
    }
    if (!P.ground) {
        P.air_t += dt;
        float vy0 = P.v.y;
        P.v.y -= G_ACC * dt;
        float ny = P.p.y + P.v.y * dt;
        // the head under a balloon cube
        if (P.v.y > 0) {
            for (int k = 0; k < NFLO; k++) {
                const flo_t *q = &s_flo[k];
                if (!q->on || q->i + 1 <= P.p.x - R_BOX || q->i >= P.p.x + R_BOX || q->j + 1 <= P.p.z - R_BOX ||
                    q->j >= P.p.z + R_BOX) {
                    continue;
                }
                if (P.p.y + 1.15f <= q->y && ny + 1.15f > q->y) {
                    ny = q->y - 1.15f;
                    P.v.y = 0;
                    fx(FX_LAND, 40, 1.8f);
                }
            }
        }
        float fy = floor_at(P.p.x, P.p.z, FMAX(P.p.y, ny) + 0.02f, &fl);
        if (P.v.y <= 0 && ny <= fy) {
            // the landing
            P.land_v = -vy0;
            P.p.y = fy;
            P.ground = true;
            P.dj_used = false;
            P.on_float = fl;
            // (the treads go on with the part of the speed over the ground along the facing; on a balloon cube they
            // stop: it is soft, and a robot that rolls on falls off its one cell)
            P.speed = fl >= 0 ? 0 : clampf(P.avx * f.x + P.avz * f.z, 0, RUN_MAX);
            P.avx = P.avz = 0;
            P.v.x = P.v.z = 0;
            P.jump_t = -1;
            float k = clampf(P.land_v / 14.0f, 0, 1);
            R.squash = 1 - 0.12f - 0.22f * k;
            if (P.land_v > 4) {
                fx(FX_LAND, (int)(40 + 50 * k), 1.0f - 0.2f * k);
                dust_burst(P.p, 6 + (int)(10 * k), 1.0f + k * 1.5f);
            }
            if (P.land_v > 12) {
                s_shake = FMAX(s_shake, 0.25f);
            }
            s_slow_left = 0;
            // a spring cube under the treads: the launch
            int ci = ifloor(P.p.x), cj = ifloor(P.p.z);
            if (fl < 0 && cell_in(ci, cj) && s_nstk[ci][cj] && s_stk[ci][cj][s_nstk[ci][cj] - 1] == CB_SPRING &&
                absf(cell_top(ci, cj) - P.p.y) < 0.05f) {
                s_coil[ci][cj] = 0.35f;
                P.ground = false;
                P.v.y = V_SPRING;
                v3 ff = fwd_of(P.yaw);
                P.v.x = ff.x * 2.4f;
                P.v.z = ff.z * 2.4f;
                air_from_run();
                P.big = true;
                P.slow_used = false;
                P.dj_used = false;
                P.jump_t = 1;
                R.squash = 1.35f;
                fx(FX_SPRING, 95, 1.0f);
            }
        } else {
            P.p.y = ny;
        }
        // the slow time at the top of a big jump
        if (P.big && !P.slow_used && P.v.y < 2.6f) {
            P.slow_used = true;
            s_slow_left = SLOW_REAL;
            fx(FX_SLOW, 75, 0.7f);
            midi_bg_duck(40, (int)(SLOW_REAL * 1000));
        }
        if (P.p.y < SEA_Y - 0.2f && P.act != A_SPLASH) {
            act_start(A_SPLASH);
            fx(FX_SPLASH, 95, 1.0f);
            music_jingle(CBM_J_SPLASH, CB_J_SPLASH_MS);
            for (int n = 0; n < 16; n++) {
                part_add(V3(P.p.x + frs() * 0.4f, SEA_Y + 0.1f, P.p.z + frs() * 0.4f),
                         V3(frs() * 1.6f, 2.5f + frand() * 2.5f, frs() * 1.6f), 0.9f, 0.09f, PT_SPLASH, 0xE8FFFF);
            }
            P.carry = 0;
        }
        if (P.act == A_SPLASH) {
            P.p.y = FMAX(P.p.y, SEA_Y - 1.2f);
            P.v.y = FMAX(P.v.y, -1.0f);
        }
    }
    // the jump
    if (P.jump_t >= 0) {
        P.jump_t += dt;
    }
    if (P.jump_t >= HOLD_BIG && P.jump_t - dt < HOLD_BIG && P.jump_held && !P.big && !P.ground && P.v.y > 0) {
        P.big = true;
        P.slow_used = false;
        P.v.y = V_BOOST;
        R.squash = 1.3f;
        fx(FX_BIG, 85, 1.0f);
        for (int n = 0; n < 8; n++) {
            part_add(V3(P.p.x + frs() * 0.3f, P.p.y, P.p.z + frs() * 0.3f), V3(frs() * 0.8f, -1.5f, frs() * 0.8f), 0.4f,
                     0.09f, PT_STAR, 0xFFE07A);
        }
    }
    // the safe place: on the ground of the island (not on a balloon cube)
    if (P.ground && P.on_float < 0 && P.act != A_SPLASH) {
        int i = ifloor(P.p.x), j = ifloor(P.p.z);
        if (cell_in(i, j) && !cell_water(i, j) && !(s_cf[i][j] & CF_NOSAFE) && !s_mc[i][j]) {
            P.safe = P.p;
            P.safe_yaw = P.yaw;
        }
    }
    player_pick();
}
// the robot box (feet at y) is on the cell (i, j) with the top t
static bool robot_on(float x, float y, float z, int i, int j, float t)
{
    return absf(y - t) < 0.12f && !(i + 1 <= x - R_BOX || i >= x + R_BOX || j + 1 <= z - R_BOX || j >= z + R_BOX);
}
// the robot box at (x, z) is in the cells of a machine
static bool robot_in_mech(const mech_t *q, float x, float z)
{
    return !(q->i1 + 1 <= x - R_BOX || q->i0 >= x + R_BOX || q->j1 + 1 <= z - R_BOX || q->j0 >= z + R_BOX);
}
// the plates (a cube, my robot or the friend robot on it; a balloon cube is too light) and the machines
static void mechs_step(float dt)
{
    for (int p = 0; p < NPLATE; p++) {
        plate_t *pl = &s_plate[p];
        int i = pl->i, j = pl->j;
        float t = s_H[i][j];
        bool on =
                s_nstk[i][j] > 0 || (P.ground && robot_on(P.p.x, P.p.y, P.p.z, i, j, t)) || net_friend_on_cell(i, j, t);
        if (on != pl->on) {
            fx(on ? FX_CLUNK : FX_BEEP, on ? 70 : 40, on ? 1.5f : 0.8f);
            if (on) {
                dust_burst(V3(i + 0.5f, t, j + 0.5f), 5, 0.8f);
            }
        }
        pl->on = on;
        pl->push = approach(pl->push, on ? 1.0f : 0.0f, dt * 8);
    }
    s_mech_move = 0;
    for (int m = 0; m < NMECH; m++) {
        mech_t *q = &s_mech[m];
        bool open = true;
        for (int k = 0; k < q->np; k++) {
            open &= s_plate[q->pl[k]].on;
        }
        q->open = open;
        float to = open ? q->ho : q->hc;
        // (a gate or a door does not close on a robot in it: it waits)
        if (!open && q->kind != MK_BRIDGE && q->h < to && (robot_in_mech(q, P.p.x, P.p.z) || net_friend_in_mech(q))) {
            continue;
        }
        float h0 = q->h;
        q->h = approach(q->h, to, q->speed * dt);
        if (q->h != h0) {
            float cx = (q->i0 + q->i1 + 1) * 0.5f, cz = (q->j0 + q->j1 + 1) * 0.5f;
            float d = fsqrt((cx - P.p.x) * (cx - P.p.x) + (cz - P.p.z) * (cz - P.p.z));
            s_mech_move = FMAX(s_mech_move, clampf(1 - d / 22.0f, 0, 1));
            if (q->h == to) {
                fx(FX_CLUNK, (int)(40 + 50 * clampf(1 - d / 22.0f, 0, 1)), 0.6f);
                s_shake = FMAX(s_shake, d < 8 ? 0.15f : 0);
            }
        }
    }
}
// the balloon cubes, the falling cubes, the springs (game time)
static void things_step(float dt)
{
    for (int k = 0; k < NFLO; k++) {
        flo_t *f = &s_flo[k];
        if (!f->on) {
            continue;
        }
        f->pop += dt;
        bool me = P.ground && P.on_float == k, stood = me || net_friend_on_float(k);
        if (f->hung) {
            if (stood) {
                float bottom = cell_top(f->i, f->j);
                if (cell_water(f->i, f->j)) {
                    bottom = SEA_Y - 0.3f;
                }
                f->y -= 0.45f * dt;
                if (f->y <= bottom) {
                    f->y = bottom;
                    f->hung = false;
                    f->base = bottom;
                }
            }
        } else {
            if (stood) {
                f->t = 0;
                f->y = approach(f->y, f->base + 5.0f, 1.6f * dt);
            } else {
                f->t += dt;
                bool under = !(f->i + 1 <= P.p.x - R_BOX || f->i >= P.p.x + R_BOX || f->j + 1 <= P.p.z - R_BOX ||
                               f->j >= P.p.z + R_BOX) &&
                             P.p.y + 1.15f <= f->y + 0.05f;
                if (f->t > 2.5f) {
                    float ny = approach(f->y, f->base, 0.9f * dt);
                    // (not down into the robot under it: it waits over the head)
                    f->y = under && ny < P.p.y + 1.15f ? FMAX(f->y, P.p.y + 1.15f) : ny;
                }
            }
        }
        if (me) {
            P.p.y = f->y + 1;
        }
    }
    for (int k = 0; k < NFALL; k++) {
        fall_t *q = &s_fall[k];
        if (!q->on) {
            continue;
        }
        q->vy -= G_ACC * dt;
        q->y += q->vy * dt;
        float top = cell_top(q->i, q->j);
        if (q->y <= top) {
            q->on = false;
            stack_add(q->i, q->j, q->kind);
        }
    }
    for (int i = 0; i < GX; i++) {
        for (int j = 0; j < GZ; j++) {
            if (s_coil[i][j] < 1) {
                s_coil[i][j] = approach(s_coil[i][j], 1, dt * 3.5f);
            }
            if (s_pop[i][j] < 1) {
                s_pop[i][j] += dt * 3;
            }
        }
    }
    mechs_step(dt);
    life_step(dt);
    if (s_toss.on) {
        s_toss.t += dt;
        if (s_toss.t >= s_toss.dur) {
            toss_t *q = &s_toss;
            q->on = false;
            int i = q->i, j = q->j;
            if (q->kind == CB_FLOAT) {
                if (!cell_water(i, j) && float_at(i, j) < 0) {
                    float_new(i, j, cell_top(i, j), false);
                    fx(FX_CLUNK, 70, 1.4f);
                }
            } else if (cell_in(i, j) && !cell_water(i, j)) {
                stack_add(i, j, q->kind);
            }
        }
    }
    for (int k = 0; k < NPILE; k++) {
        pile_t *pl = &s_pile[k];
        if (pl->pulse > 0) {
            pl->pulse = approach(pl->pulse, 0, dt * 0.7f);
        }
    }
    for (int k = 0; k < NPART; k++) {
        part_t *q = &s_part[k];
        if (q->life <= 0) {
            continue;
        }
        q->life -= dt;
        float g = q->kind == PT_STEAM ? -0.5f : q->kind == PT_DUST ? 2.0f : q->kind == PT_BIT ? 0 : 9.0f;
        if (q->kind == PT_CONF) {
            g = 4.0f;
        }
        q->v.y -= g * dt;
        float drag = q->kind == PT_DUST || q->kind == PT_STEAM ? 2.5f : 0.4f;
        q->v = vmul(q->v, 1 - drag * dt);
        q->p = vadd(q->p, vmul(q->v, dt));
    }
}

// ---------------------------------------------------------------- the rig: the moves of the parts (real dt + game dt)
static float s_blink_t, s_look_t, s_look_yaw, s_look_pitch;
// the blink: about each 3 s, and two blinks sometimes (0 .. 1; a part of rig_step, inline)
static inline __attribute__((always_inline)) float rig_blink(float dt)
{
    s_blink_t -= dt;
    float blink = 0;
    if (s_blink_t < 0.12f && s_blink_t > 0) {
        blink = 1 - absf(s_blink_t - 0.06f) / 0.06f;
    }
    if (s_blink_t <= 0) {
        s_blink_t = 2.0f + frand() * 3.0f;
    }
    return blink;
}

static void rig_step(float dt)
{
    float t = s_t;
    // the springs of the squash and the lean
    static float sq_v, roll_v;
    sq_v += ((1 - R.squash) * 320 - sq_v * 15) * dt;
    R.squash += sq_v * dt;
    if (P.jump_held && P.ground && P.act == A_NONE) {
        R.squash = approach(R.squash, 0.9f, dt * 2);
    }
    float roll_to = clampf(-P.spin * P.speed * 0.018f, -0.18f, 0.18f);
    roll_v += ((roll_to - R.roll) * 140 - roll_v * 14) * dt;
    R.roll += roll_v * dt;
    R.pitch = 0;
    R.bob = fsin(t * 23) * 0.006f * (P.speed / RUN_MAX) + fsin(t * 2.2f) * 0.004f;
    // the treads: the left one goes faster in a turn to the right
    float vl = P.speed + P.spin * TX, vr = P.speed - P.spin * TX;
    R.tread_l -= vl * dt;
    R.tread_r -= vr * dt;
    // the head looks at the nearest thing of interest in front (a pile, a SUNDROP, the cell of the cube), else ahead
    v3 f = fwd_of(P.yaw);
    v3 head = V3(P.p.x, P.p.y + 1.2f, P.p.z);
    float best = 5.5f;
    v3 look = vadd(head, vadd(vmul(f, 3), V3(0, -0.6f, 0)));
    for (int k = 0; k < NDROP; k++) {
        if (s_drop[k].got) {
            continue;
        }
        v3 d = vsub(s_drop[k].p, head);
        float l = vlen(d);
        if (l < best + 2 && vdot(d, f) > l * 0.2f) {
            best = l - 2;
            look = s_drop[k].p;
        }
    }
    for (int k = 0; k < NPILE; k++) {
        v3 pp = V3(s_pile[k].x, s_pile[k].y + 0.3f, s_pile[k].z);
        v3 d = vsub(pp, head);
        float l = vlen(d);
        if (!P.carry && l < best && vdot(d, f) > l * 0.3f) {
            best = l;
            look = pp;
        }
    }
    if (P.carry && P.ground) {
        int i, j;
        float top;
        front_cell(&i, &j);
        if (place_ok(i, j, &top)) {
            look = V3(i + 0.5f, top + 0.5f, j + 0.5f);
        }
    }
    s_look_t -= dt;
    if (P.idle_t > 3.5f && s_look_t <= 0) {
        s_look_t = 1.2f + frand() * 1.8f;
        s_look_yaw = frs() * 1.1f;
        s_look_pitch = frs() * 0.25f + 0.1f;
    }
    v3 d = vsub(look, head);
    float ly = wrap_pi(fatan2(d.x, d.z) - P.yaw), lp = -fatan2(d.y, fsqrt(d.x * d.x + d.z * d.z));
    if (P.idle_t > 3.5f) {
        ly = s_look_yaw;
        lp = -s_look_pitch;
    }
    ly = clampf(ly, -1.2f, 1.2f);
    lp = clampf(lp, -0.6f, 0.7f);
    float hy = ly, hp = lp, hr = 0, nk = 1.0f, er = 0, bl = 0, br = 0, bu = 0, ir = 1.0f;
    float al = 0.25f, ar = 0.25f, cl = 0.2f, hat = 0;
    // the arms hold the cube up over the head
    if (P.carry) {
        al = ar = 2.9f;
        cl = 0.75f;
    }
    if (!P.ground) {
        // in the air: the arms go up a little, the eyes open wide; at the top of a big jump the arms spread out
        al = ar = P.carry ? 2.9f : 0.9f + s_slow * 0.9f;
        bu = 0.03f + s_slow * 0.02f;
        ir = 1.15f + s_slow * 0.25f;
        nk = 1.15f + s_slow * 0.3f;
        hp += (P.v.y < -2 ? 0.35f : -0.1f);
    }
    if (P.jump_held && P.ground) {
        nk = 0.75f;
        br = bl = -0.12f;
    }
    float at = P.act_t;
    switch (P.act) {
    case A_SCOOP: {
        float k1 = smooth01(at / 0.25f) * (1 - smooth01((at - 0.45f) / 0.2f));
        al = ar = 0.25f + 1.0f * k1;
        cl = 0.2f + 0.8f * k1;
        hat = smooth01(at / 0.2f) * (1 - smooth01((at - 0.5f) / 0.12f));
        hp = 0.45f * k1;
        hy = 0;
        if (at > 0.55f && at < 0.95f) {
            // the press: three squashes and a shake
            float q = (at - 0.55f) / 0.4f;
            R.squash = 1 - 0.16f * absf(fsin(q * PI_F * 3));
            hr = fsin(at * 60) * 0.06f;
            bl = br = -0.15f;
            er = 0.12f;
        }
        if (at > 0.95f) {
            al = ar = 1.45f + 1.45f * smooth01((at - 0.95f) / 0.2f);
            cl = 0.75f;
            ir = 1.25f;
            bu = 0.02f;
        }
        break;
    }
    case A_PLACE: {
        // the throw: the arms go back over the head (the wind-up), swing forward at TOSS_AT, then come down
        float wind = smooth01(at / TOSS_AT), sw = smooth01((at - TOSS_AT) / 0.1f), dn = smooth01((at - 0.3f) / 0.2f);
        al = ar = at < TOSS_AT ? 2.9f + 0.35f * wind : 3.25f - 2.0f * sw - 1.0f * dn;
        cl = at < TOSS_AT ? 0.75f : 0.2f;
        hp = at < TOSS_AT ? -0.15f : 0.35f;
        hy = 0;
        break;
    }
    case A_PICK: {
        float k1 = smooth01(at / 0.2f);
        al = ar = 0.4f + 1.05f * k1 + (at > 0.22f ? 1.45f * smooth01((at - 0.22f) / 0.2f) : 0);
        cl = 0.8f - 0.2f * k1;
        hp = 0.3f;
        hy = 0;
        break;
    }
    case A_NO:
        hy = fsin(at * 22) * 0.35f * (1 - at / 0.55f);
        bl = br = 0.18f;
        er = 0.15f;
        break;
    case A_CHEER: {
        float k1 = smooth01(at / 0.2f) * (1 - smooth01((at - 1.8f) / 0.3f));
        al = ar = 0.3f + 2.4f * k1 + fsin(at * 16) * 0.25f * k1;
        cl = 1;
        bu = 0.04f * k1;
        bl = br = 0.2f * k1;
        ir = 1 + 0.45f * k1;
        nk = 1 + 0.5f * k1;
        hp = -0.3f * k1;
        hy = 0;
        er = -0.12f * k1;
        if (at > 0.25f && at < 1.3f) {
            P.yaw += dt * TAU_F / 1.05f;  // one turn in the air
        }
        break;
    }
    case A_SPLASH:
        er = 0.25f;
        bl = br = 0.25f;
        ir = 0.7f;
        al = ar = 2.6f + fsin(at * 18) * 0.3f;
        break;
    case A_BOOT: {
        // asleep, then the eyes come on (a flicker), the head comes up, a look left and right
        float on = at < 0.6f ? 0 : at < 0.75f ? ((int)(at * 40) & 1) : 1;
        ir = on * 1.1f;
        nk = 0.6f + 0.5f * smooth01((at - 0.7f) / 0.4f);
        hp = 0.5f - 0.6f * smooth01((at - 0.7f) / 0.4f);
        hy = at > 1.1f ? fsin((at - 1.1f) * 7) * 0.6f * (1 - smooth01((at - 1.6f) / 0.3f)) : 0;
        bu = 0.03f * smooth01((at - 0.8f) / 0.2f);
        break;
    }
    default:
        break;
    }
    float blink = rig_blink(dt);
    float k = clampf(dt * 12, 0, 1), kf = clampf(dt * 20, 0, 1);
    R.head_yaw += (hy - R.head_yaw) * k;
    R.head_pitch += (hp - R.head_pitch) * k;
    R.head_roll += (hr - R.head_roll) * kf;
    R.neck += (nk - R.neck) * k;
    R.eye_roll += (er - R.eye_roll) * k;
    R.brow_l += (bl - R.brow_l) * k;
    R.brow_r += (br - R.brow_r) * k;
    R.brow_up += (bu - R.brow_up) * k;
    R.iris += (ir - R.iris) * kf;
    R.blink = P.act == A_BOOT && at < 0.6f ? 1 : blink;
    R.arm_l += (al - R.arm_l) * kf;
    R.arm_r += (ar - R.arm_r) * kf;
    R.claw += (cl - R.claw) * kf;
    R.hatch += (hat - R.hatch) * kf;
    R.carry = P.carry;
    if (R.carry_s < 0.82f) {
        R.carry_s = approach(R.carry_s, 0.82f, dt * 4);
    }
    R.gauge = P.carry ? P.carry : (P.act == A_SCOOP && at > 0.3f ? P.act_kind : 0);
    R.flash = approach(R.flash, 0, dt * 2.5f);
    R.tint = 1;
}

// ---------------------------------------------------------------- the camera
static v3 s_cam_at, s_cam_eye;
static float s_cam_f;
static bool s_cam_init;
static float s_cam_clear = 1;  // the part of the distance of the eye that the ground leaves free
static float s_tilt_x, s_tilt_y;
static void camera_step(float rdt, bool title)
{
    float at_y = P.p.y + 0.85f;
    v3 f = fwd_of(P.yaw);
#if CB_WIDE
    float dist = 5.2f, pitch = 0.22f + CB_DY * 0.0002f, fpx = 300;
#elif CB_ROUND
    float dist = 5.2f, pitch = 0.30f, fpx = OW * 0.80f;  // (the round: about the width of view of the CoreS3)
#else
    float dist = 4.9f, pitch = 0.30f, fpx = 265;
#endif
    v3 at = V3(P.p.x + f.x * P.speed * 0.25f, at_y, P.p.z + f.z * P.speed * 0.25f);
    // the camera follows the back of the robot when it drives (not when a finger turned it a moment ago)
    s_cam_hold -= rdt;
    s_cam_yaw = wrap_pi(s_cam_yaw + s_cam_drag);
    s_cam_drag = 0;
    if (s_cam_hold <= 0 && P.speed > 0.6f && P.act == A_NONE) {
        float d = wrap_pi(P.yaw - s_cam_yaw);
        s_cam_yaw = wrap_pi(s_cam_yaw + d * clampf(rdt * 0.9f * (P.speed / RUN_MAX), 0, 1));
    }
    float yaw = s_cam_yaw + s_tilt_x * 0.35f;
    pitch += s_tilt_y * 0.18f;
    // high places: a little more from above; the slow time: closer
    float lowest = P.p.y - floor_at(P.p.x, P.p.z, P.p.y + 0.01f, NULL);
    pitch += clampf(lowest * 0.04f, 0, 0.25f);
    dist *= 1 - 0.16f * s_slow;
    if (P.act == A_CHEER) {
        // the face: the camera goes round to the front of the robot
        float k = smooth01(P.act_t / 0.5f) * (1 - smooth01((P.act_t - 1.8f) / 0.4f));
        yaw = s_cam_yaw + wrap_pi(P.yaw + PI_F - s_cam_yaw) * k;
        dist *= 1 - 0.45f * k;
        pitch -= 0.18f * k;
        at.y += 0.25f * k;
    }
    if (title) {
        // the title: a slow flight round the island
        // (round 3: over the beach and the crates, the island behind in the haze: a big island costs much)
        float a = s_rt * 0.11f;
        at = V3(22 + fsin(a) * 4, 2.0f, 10.5f);
        yaw = fsin(a * 0.8f) * 0.75f;
        dist = 15;
        pitch = 0.30f;
        fpx *= 1.05f;
    }
    v3 dir = V3(fsin(yaw) * fcos(pitch), -fsin(pitch), fcos(yaw) * fcos(pitch));
    v3 eye = vsub(at, vmul(dir, dist));
    // (the camera stays over the ground and the cubes that are under it)
    int fl;
    float g = floor_at(eye.x, eye.z, 99, &fl);
    if (eye.y < g + 0.8f) {
        eye.y = g + 0.8f;
    }
    if (eye.y < SEA_Y + 0.6f) {
        eye.y = SEA_Y + 0.6f;
    }
    // (the eye stays out of the terraces and the stacks: it comes nearer along the ray from the robot when the ground
    // is over the ray; fast in, slowly out again)
    if (!title) {
        float clear = 1;
        for (int k = 1; k <= 12; k++) {
            v3 q = vlerp(at, eye, k / 12.0f);
            if (cell_top(ifloor(q.x), ifloor(q.z)) > q.y - 0.35f) {
                clear = (k - 1) / 12.0f;
                break;
            }
        }
        if (clear < 0.22f) {
            clear = 0.22f;
        }
        s_cam_clear += (clear - s_cam_clear) * clampf(rdt * (clear < s_cam_clear ? 14.0f : 1.6f), 0, 1);
        eye = vadd(at, vmul(vsub(eye, at), s_cam_clear));
    } else {
        s_cam_clear = 1;
    }
    if (!s_cam_init) {
        s_cam_at = at;
        s_cam_eye = eye;
        s_cam_f = fpx;
        s_cam_init = true;
    }
    float ka = clampf(rdt * (title ? 2 : 7), 0, 1), ke = clampf(rdt * (title                 ? 2
                                                                       : s_cam_clear < 0.98f ? 10
                                                                                             : 4.5f),
                                                                0, 1);
    s_cam_at = vlerp(s_cam_at, at, ka);
    s_cam_eye = vlerp(s_cam_eye, eye, ke);
    s_cam_f += (fpx - s_cam_f) * ka;
    v3 sh = V3(0, 0, 0);
    if (s_shake > 0) {
        sh = V3(frs() * s_shake * 0.25f, frs() * s_shake * 0.25f, frs() * s_shake * 0.25f);
        s_shake = approach(s_shake, 0, rdt * 1.2f);
    }
    cam_set(vadd(s_cam_eye, sh), vadd(s_cam_at, vmul(sh, 0.5f)), s_cam_f);
}

// ---------------------------------------------------------------- the start of a play
static void game_reset(void)
{
    world_grid();
    memset(&P, 0, sizeof P);
    P.p = V3(START_X, 0, START_Z);
    P.yaw = 0;
    P.p0 = P.p;
    P.yaw0 = P.yaw;
    P.ground = true;
    P.on_float = -1;
    P.jump_t = -1;
    P.safe = P.p;
    P.safe_yaw = P.yaw;
    memset(&R, 0, sizeof R);
    R.squash = 1;
    R.neck = 1;
    R.iris = 1;
    R.carry_s = 0.82f;
    R.tint = 1;
    R.arm_l = R.arm_r = 0.25f;
    memset(s_flo, 0, sizeof s_flo);
    memset(s_fall, 0, sizeof s_fall);
    memset(&s_toss, 0, sizeof s_toss);
    life_reset();
    memset(s_part, 0, sizeof s_part);
    for (int i = 0; i < GX; i++) {
        for (int j = 0; j < GZ; j++) {
            s_coil[i][j] = 1;
            s_pop[i][j] = 1;
        }
    }
    for (int k = 0; k < NDROP; k++) {
        s_drop[k] = (drop_t){V3(DROP_XYZ[k][0], DROP_XYZ[k][1], DROP_XYZ[k][2]), false, 0};
    }
    s_drop_name = -1;
    for (int k = 0; k < NSPARK; k++) {
        s_spark[k] = (drop_t){V3(SPARK_XYZ[k][0], SPARK_XYZ[k][1], SPARK_XYZ[k][2]), false, 0};
    }
    for (int k = 0; k < NPILE; k++) {
        int i = (int)s_pile[k].x, j = (int)s_pile[k].z;
        s_pile[k].y = cell_in(i, j) ? s_H[i][j] : 0;
        s_pile[k].pulse = 0;
    }
    s_ndrops = s_nsparks = 0;
    s_slow = s_slow_left = 0;
    s_shake = 0;
    s_win_t = -1;
    s_cam_yaw = P.yaw;
    s_cam_init = false;
    s_acc = 0;
    s_blink_t = 1.5f;
}

// ---------------------------------------------------------------- the update of a frame (real dt)
static void play_update(float rdt, bool title)
{
    if (!title) {
        thumbs_read();
    } else {
        s_jump_edge = s_act_edge = false;
        s_stick_x = s_stick_y = 0;
        s_jump_down = false;
    }
    // the tilt looks round a little (the zero is the hold of the board at the start)
    float tx = 0, ty = 0;
    if (s_tilt_on && imu_ok()) {
        imu_tilt_logical(&tx, &ty);
    }
    s_tilt_x += (clampf(tx * 2.2f, -1, 1) - s_tilt_x) * clampf(rdt * 6, 0, 1);
    s_tilt_y += (clampf(ty * 2.2f, -1, 1) - s_tilt_y) * clampf(rdt * 6, 0, 1);
    if (absf(s_stick_x) + absf(s_stick_y) > 0.1f || s_jump_down || s_act_edge) {
        P.idle_t = 0;
    } else {
        P.idle_t += rdt;
    }
    // the slow time
    if (s_slow_left > 0) {
        s_slow_left -= rdt;
    }
    s_slow = approach(s_slow, s_slow_left > 0 ? 1.0f : 0.0f, rdt * (s_slow_left > 0 ? 7.0f : 4.0f));
    float scale = 1 - (1 - SLOW_SCALE) * s_slow;
    float gdt = rdt * scale;
    // the buttons (a press of JUMP waits in P.jbuf: the jump comes in the steps, jump_start)
    if (s_jump_edge && !title) {
        P.jbuf = JBUF;
    }
    if (!s_jump_down) {
        P.jump_held = false;
    }
    if (s_act_edge && !title) {
        do_act();
    }
    s_jump_edge = s_act_edge = false;
    // the fixed steps of the game time
    s_acc += gdt;
    if (s_acc > 0.1f) {
        s_acc = 0.1f;
    }
    while (s_acc >= STEP_DT) {
        s_acc -= STEP_DT;
        s_t += STEP_DT;
        things_step(STEP_DT);
        if (!title) {
            player_step(STEP_DT);
        }
        act_step(STEP_DT);
    }
    rig_step(gdt);
    if (s_win_t >= 0) {
        s_win_t += rdt;
    }
    for (int k = 0; k < NDROP; k++) {
        if (s_drop[k].got) {
            s_drop[k].t += rdt;
        }
    }
    if (s_drop_name >= 0) {
        s_drop_name_t += rdt;
    }
    s_wtime = s_t;
    // the motor and the treads (cb_motor.inc): the pitch and the volume with the speed of the treads; in the air the
    // treads turn free (a higher pitch, softer, no cleats); the slow time lowers the pitch
    if (s_sound && !title && P.act != A_SPLASH) {
        float sp = absf(P.speed) + absf(P.spin) * 0.3f;
        float k = clampf(sp / RUN_MAX, 0, 1.2f);
        float rate = (0.8f + 0.95f * k + (P.ground ? 0 : 0.3f)) * (1 - 0.35f * s_slow);
        s_mot_rate = rate;
        s_mot_vol = k < 0.04f ? 0 : (P.ground ? 14 + 22 * k : 9 + 8 * k);
        s_mot_cps = P.ground ? sp * 6.5f : 0;
    } else {
        s_mot_vol = 0;
        s_mot_cps = 0;
    }
    motor_frame(esp_timer_get_time());
    // the rumble of a gate, a door or the bridge that moves (louder near)
    audio_drone(1, 140, s_sound && !title ? (int)(s_mech_move * 16) : 0, W_NOISE);
}
