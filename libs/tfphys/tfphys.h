#pragma once
// tfphys: 2D rigid-body physics for the store games (GLASSHOUSE, the slingshot game).
// The method is the "soft step" of Box2D v3 (Erin Catto): sequential impulses with warm start, sub-steps of one fixed
// step, soft contacts that keep stacks stable, speculative contacts (a fast body does not go through a wall) and sleep.
// Plain C99. The units are metres, kilograms, seconds and radians. The y axis goes UP: the game turns it for the screen.
// The module of main/apps.json: "uses": ["tfphys"]. The module is in the firmware of each board kind: the functions
// below are ABI names (ABI rev 22), and the structs of this file are a part of the ABI (the app reads the world, the
// bodies and the events directly): a change of their layout needs a new ABI version. An app includes this header only
// ("../../libs/tfphys/tfphys.h" in the repo, "tfphys/tfphys.h" with the SDK package) and has no copy of the module.
// The memory that tp_init gets is in the set of the app: the unload of an app file frees it when the app did not call
// tp_free.
//
// THE STEPS OF A GAME
// 1. Make the world one time (in enter()):
//        tp_world_def_t d = tp_world_defaults();      (then change d.bounds, d.max_bodies, d.seed ... if necessary)
//        tp_world_t *w = tp_init(&d);                 (NULL = no memory)
//    tp_init gets all the memory one time (PSRAM on the board, or the block d.mem). A step gets no memory.
// 2. Make the bodies:
//        tp_body_def_t b = tp_body_def(TP_DYNAMIC, x, y, TP_MAT_GLASS);
//        tp_id_t pane = tp_add_box(w, &b, 0.4f, 0.02f);      (half sizes; also tp_add_circle, tp_add_poly)
//    Each add gives the id of the body (TP_NONE = no free place). The position of a body is its centre of mass.
//    Join two bodies with a joint: tp_add_joint(w, &j), with j from tp_weld_def(), tp_revolute_def(),
//    tp_distance_def() or tp_rope_def(). A joint with j.break_force > 0 breaks at that force (N).
// 3. Each frame: call tp_step(w) one time for each 1/60 s of game time. Keep the rest of the time for the next frame
//    (an accumulator). Do not give tp_step a frame time: the step is fixed, so the same inputs give the same result.
// 4. After each step, read the events (the next step clears them):
//        w->hit[0 .. w->nhit)   the hits (for the sound): the two bodies, the point, the impulse, the speed, the
//                               materials and their sound classes;
//        w->ev[0 .. w->nev)     a body broke (its new pieces: w->piece[first .. first + count)), a joint broke, a
//                               body went away (out of the bounds, or a small piece that faded).
// 5. Draw: each body i < w->body_hi with (w->body[i].flags & TP_ALIVE): its place (xf), its shape (shape, n, vl[],
//    radius), its material (mat), its fade (life / life0 when life0 > 0) and its state (TP_AWAKE). tp_verts() gives
//    the corners in the world. The particles: w->part[0 .. w->npart). tfphys_draw.h has a debug view (tfdraw).
// 6. At the end (in leave()): tp_free(w).
//
// THE BREAKS, THE TANKS, THE PARTICLES
// A body with toughness > 0 breaks at a hard hit: glass makes 4-8 sharp pieces round the point of the hit, timber
// 2-3 long pieces, a chip or a stone a few thick pieces (tp_mat_t.pattern). The pieces get the speed of the body; a
// small piece fades out (life), a very small one becomes particles (crumbs). A hitter that breaks a body goes on
// through it. The joints of the body go to the piece that holds the anchor (a pane keeps its mount). A tank: give
// d.spill = the count of its water drops; they spill out when it breaks (or call tp_spill). The particles (water,
// crumbs, sparks, shards) fall and bounce on the bodies, with no forces between them; w->splash[] counts their hits.
// The same inputs and the same d.seed give the same result, to the bit (on one kind of board).
// The cost: a body that sleeps costs almost nothing; 120 awake bodies with 300 contacts, 20 joints and 400 particles
// need about 3.5-6 ms a step on the board with 3 sub-steps (NOTES.md). A new joint does not break in its first
// TP_JOINT_SETTLE steps (the load comes on: a short peak).
//
// The game reads the structs of this header. It writes only the fields that the comments mark "(game)", or uses the
// functions. The ids have a generation: tp_body() gives NULL for the id of a body that went away (a broken pane).
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TP_MAX_VERTS    8   // the corners of a polygon, at most
#define TP_MAT_MAX      16  // the materials of a world
#define TP_PART_KINDS   4   // the particle kinds
#define TP_JOINT_SETTLE 12  // the steps after its start in which a joint cannot break (the load comes on)
#define TP_NONE         (-1)

typedef int32_t tp_id_t;  // the id of a body or a joint (the slot and its generation); TP_NONE = none
typedef struct {
    float x, y;
} tp_vec_t;
typedef struct {
    float c, s;
} tp_rot_t;  // a rotation: the cosine and the sine of the angle
typedef struct {
    tp_vec_t p;
    tp_rot_t q;
} tp_xf_t;  // a place: the position and the rotation
typedef struct {
    tp_vec_t lo, hi;
} tp_box_t;  // an axis-aligned box (the AABB)

enum {
    TP_STATIC,
    TP_KINEMATIC,
    TP_DYNAMIC
};  // the kinds of body
enum {
    TP_CIRCLE,
    TP_POLY
};  // the kinds of shape
// the sound classes of the materials (for the game: which sample a hit plays)
enum {
    TP_SND_GLASS,
    TP_SND_WOOD,
    TP_SND_METAL,
    TP_SND_CHIP,
    TP_SND_GUMMY,
    TP_SND_STONE,
    TP_SND_WATER,
    TP_SND_N
};
// the default materials (tp_init fills w->mat[0 .. 6]; the game can change them and add more, to TP_MAT_MAX)
enum {
    TP_MAT_GLASS,
    TP_MAT_WOOD,
    TP_MAT_METAL,
    TP_MAT_CHIP,
    TP_MAT_GUMMY,
    TP_MAT_STONE,
    TP_MAT_WATER
};
// how a breakable body breaks: SHARDS = many sharp pieces round the hit (glass); SPLINTER = 2-3 long pieces across
// the long axis (timber, a pretzel stick); CHUNKS = a few thick pieces (a chip, a stone)
enum {
    TP_BREAK_SHARDS,
    TP_BREAK_SPLINTER,
    TP_BREAK_CHUNKS
};
// the particle kinds (w->pkind[] has their values)
enum {
    TP_PART_WATER,
    TP_PART_CRUMB,
    TP_PART_SPARK,
    TP_PART_SHARD
};

typedef struct {
    float density;       // kg per square metre (a 2D world: a body has an area, not a volume)
    float friction;      // 0 = ice .. 1 = rubber (two materials: the root of the product)
    float restitution;   // the bounce 0 .. 1 (two materials: the larger value)
    float toughness;     // m/s: a hit whose impulse is more than toughness x the mass of the body breaks it
                         // (the change of speed that the hit gives a free body); 0 = it never breaks
    float rolling;       // the rolling resistance of a circle (0 = it rolls on and on)
    float burst;         // m/s: the pieces of a break fly apart at about this speed
    uint8_t sound;       // TP_SND_*
    uint8_t pattern;     // TP_BREAK_*
    uint8_t pieces;      // the pieces of a break, 2 .. 8
    uint8_t crumbs;      // the particles at the point of the break (the effect)
    uint8_t crumb_kind;  // TP_PART_* of those particles
} tp_mat_t;

typedef struct {
    float gravity;                // the part of the world gravity (1 = all, 0.3 = sparks)
    float drag;                   // 1/s: the air slows the particle
    float restitution, friction;  // for a hit on a body
    float life, life_var;         // the life (s) = life +- life_var
    float radius;                 // for the hits (m)
} tp_pkind_t;

typedef struct {
    tp_vec_t p, v;      // the position and the speed
    float life, life0;  // the time left (s) and the full life (the fade: life / life0)
    tp_id_t ign;        // the id of a body that broke: in its first 0.3 s the particle goes through the pieces
                        // of that body (the water of a tank comes out of it); TP_NONE = none
    uint8_t kind;       // TP_PART_*
    uint8_t touch;      // 1 = it touched a body in the last step
    uint8_t mat;        // the material of the body that made it (TP_MAT_*), or 255
    uint8_t pad;
} tp_part_t;

// the flags of a body
#define TP_ALIVE   1
#define TP_AWAKE   2   // it moves: the solver works on it (a sleeping body costs almost nothing)
#define TP_FIXROT  4   // it does not turn
#define TP_NOSLEEP 8   // it never sleeps
#define TP_GONE    16  // (internal: it goes away at the end of this step)
#define TP_BOXF    32  // (internal: the shape is a box of tp_add_box: a fast shape test)

typedef struct {
    tp_xf_t xf;                   // the place: xf.p = the centre of mass, xf.q = the rotation
    tp_vec_t v;                   // the speed of the centre of mass (m/s)
    float w;                      // the turn speed (rad/s, + = counter-clockwise)
    tp_vec_t force;               // (game) a force for the next step (N); tp_force() adds to it; the step clears it
    float torque;                 // (game) the same for the torque (N m)
    float mass, im;               // the mass (kg) and its inverse (0 for a static or a kinematic body)
    float inertia, ii;            // the moment of inertia about the centre of mass, and its inverse
    float area;                   // m2
    float extent;                 // the largest distance from the centre of mass to a point of the shape
    float radius;                 // TP_CIRCLE: the radius
    float toughness;              // (game) m/s: see tp_mat_t; 0 = it never breaks
    float life, life0;            // (game) life > 0: the body goes away when it counts down to 0 (a piece fades)
    float sleep_t;                // the time with a low speed (s)
    float ldamp, adamp;           // (game) the damping of the speed and of the turn (1/s)
    float gscale;                 // (game) the part of the gravity (1 = all)
    tp_box_t box;                 // the box of the shape, with the margin of this step
    uint8_t kind, shape, n, mat;  // TP_STATIC..., TP_CIRCLE / TP_POLY, the corner count, the material
    uint8_t flags;                // TP_ALIVE, TP_AWAKE, TP_FIXROT, TP_NOSLEEP
    uint8_t gen;                  // 0 = a body of the game, 1 = a piece, 2 = a piece of a piece
    uint16_t group;               // (game) two bodies with the same group (not 0) do not collide
    uint16_t cat, mask;           // (game) a body collides with another when (cat & other mask) and (mask & other cat)
    int16_t spill;                // (game) the water drops in it: they spill when it breaks (a tank)
    int32_t user;                 // (game) a value of the game (the index of its object)
    tp_id_t parent;               // a piece: the id of the body that broke (that id is not valid now)
    uint32_t gid;                 // (internal) the generation of the slot
    int16_t chead, jhead;         // (internal) the lists of its contacts and joints
    int16_t isl, inext;           // (internal) the island (the sleep)
    uint32_t mark;                // (internal)
    tp_vec_t vl[TP_MAX_VERTS];    // TP_POLY: the corners round the centre of mass (local, counter-clockwise)
    tp_vec_t nl[TP_MAX_VERTS];  // TP_POLY: the outward normals of the edges (local; edge i goes from vl[i] to vl[i+1])
} tp_body_t;

typedef struct {
    int kind;                   // TP_STATIC, TP_KINEMATIC, TP_DYNAMIC
    tp_vec_t pos;               // tp_add_box / tp_add_circle: the centre; tp_add_poly: the origin of its points
    float angle;                // rad
    tp_vec_t vel;               // the start speed
    float spin;                 // the start turn speed
    int mat;                    // TP_MAT_* (or a material of the game)
    float toughness;            // < 0 = the value of the material; 0 = it never breaks
    float ldamp, adamp;         // the damping (1/s); 0 = none
    float gscale;               // 1 = all the gravity
    float life;                 // > 0: the body goes away after this time (s)
    uint16_t group, cat, mask;  // the collision filter (see tp_body_t)
    int spill;                  // water drops that spill when it breaks
    bool fixed_rot, no_sleep, sleeping;  // sleeping: it starts asleep (a level that waits for the first hit)
    int32_t user;
} tp_body_def_t;

typedef struct {
    float bias, mass, imp;
} tp_soft_t;  // (internal) a soft constraint: the bias rate and two scales

enum {
    TP_JOINT_REVOLUTE,
    TP_JOINT_DISTANCE,
    TP_JOINT_WELD
};
#define TPJ_ALIVE   1
#define TPJ_COLLIDE 2   // the two bodies of the joint collide (default: they do not)
#define TPJ_LIMIT   4   // a revolute joint: the angle stays from lower to upper
#define TPJ_MOTOR   8   // a revolute joint: the motor turns it
#define TPJ_SPRING  16  // a distance joint: a spring to len (between minlen and maxlen)

typedef struct {
    uint8_t kind, flags;        // TP_JOINT_*, TPJ_*
    int16_t a, b;               // the slots of the two bodies
    int16_t next[2], prev[2];   // (internal) the lists of the joints of a and of b
    uint32_t gid;               // (internal)
    int32_t user;               // (game)
    tp_vec_t la, lb;            // the anchors on a and on b (local, from each centre of mass)
    float ref;                  // the angle of b less the angle of a at the start (revolute, weld)
    float len, minlen, maxlen;  // a distance joint: its length; min < max = a rope (min 0) or a spring with limits
    float lower, upper;         // (game) a revolute joint: the limits of the angle (rad, from the start angle)
    float mspeed, mtorque;      // (game) a revolute joint: the motor speed (rad/s) and its largest torque (N m)
    float friction;             // (game) a revolute joint with no motor: the torque (N m) that resists its turn
    float hz, zeta;             // (game) the stiffness (Hz) and the damping ratio: a distance spring, a soft weld
                                // (0 Hz = rigid)
    float break_force;          // (game) N: the joint breaks above this force; 0 = never
    float break_torque;         // (game) N m: a weld or a revolute joint breaks above this torque; 0 = never
    float force, torque;        // the force and the torque of the last step (the largest of its sub-steps)
    // (internal: the solver)
    tp_vec_t lin, ra, rb, dc;
    float ang, mot, lo_i, up_i, imp, da, axial, fmax, tmax;
    tp_soft_t soft;
    uint8_t age;  // the steps since the joint was made (a new joint does not break: it settles)
} tp_joint_t;

typedef struct {
    int kind;              // TP_JOINT_*
    tp_id_t a, b;          // the two bodies (a static body is good for a mount)
    tp_vec_t pa, pb;       // the anchors in the world (revolute and weld: pa only; distance: pa on a, pb on b)
    float minlen, maxlen;  // distance: both 0 = rigid at the length now; a rope: min 0, max = its length
    bool limit;
    float lower, upper;  // revolute
    bool motor;
    float mspeed, mtorque;            // revolute
    float friction;                   // revolute: the friction torque (N m) of the pin (a chain comes to rest)
    float hz, zeta;                   // distance: a spring (hz > 0); weld: soft (hz > 0)
    float break_force, break_torque;  // a joint does not break in its first TP_JOINT_SETTLE steps
    bool collide;                     // the two bodies collide
    int32_t user;
} tp_joint_def_t;

typedef struct {
    tp_vec_t ra, rb;  // the point from the centre of mass of a and of b (world directions)
    float sep;        // the distance of the shapes at the point (< 0 = they overlap)
    float ni, ti;     // the normal and the tangent impulse of the last sub-step (N s)
    float hit;        // the impulse of the hit in the last step (N s; 0 = a body only rests on it)
    uint16_t id;      // (internal) the key of the feature (warm start)
} tp_cpoint_t;

typedef struct {
    int16_t a, b;              // the slots of the two bodies (the normal goes from a to b)
    int16_t next[2], prev[2];  // (internal) the lists of the contacts of a and of b
    uint8_t np;                // the points: 0 = the boxes touch, the shapes do not
    uint8_t flags;             // (internal)
    tp_vec_t n;                // the normal (world)
    float rolli;               // (internal) the rolling impulse
    float fr, e, rr;           // the friction, the restitution and the rolling of the pair (from its start)
    uint32_t stamp;            // (internal) the last step that the broad phase found the pair
    tp_cpoint_t pt[2];
} tp_contact_t;

typedef struct {
    tp_id_t a, b;          // the two bodies
    tp_vec_t p, n;         // the point and the normal (from a to b)
    float impulse;         // the normal impulse of the hit (N s; not the part that holds a body at rest)
    float speed;           // the speed at which the bodies came together (m/s)
    uint8_t mat_a, mat_b;  // the materials
    uint8_t snd_a, snd_b;  // their sound classes (TP_SND_*)
    bool begin;            // true: the bodies did not touch in the last step
} tp_hit_t;

enum {
    TP_EV_BREAK,
    TP_EV_JOINT,
    TP_EV_GONE
};
enum {
    TP_WHY_FORCE,
    TP_WHY_TORQUE,
    TP_WHY_BODY
};  // TP_EV_JOINT: why it broke (TP_WHY_BODY: a body went away)
enum {
    TP_GONE_OUT,
    TP_GONE_FADED
};  // TP_EV_GONE: why the body went away
typedef struct {
    uint8_t kind;          // TP_EV_*
    uint8_t why;           // TP_EV_JOINT: TP_WHY_*; TP_EV_GONE: TP_GONE_*
    uint8_t mat, snd;      // the material of the body and its sound class (TP_EV_BREAK, TP_EV_GONE)
    tp_id_t id;            // the body (TP_EV_BREAK, TP_EV_GONE) or the joint (TP_EV_JOINT): not valid now
    tp_id_t a, b;          // TP_EV_JOINT: its two bodies (TP_NONE when it went away)
    int32_t user;          // the user value of that body or joint
    tp_vec_t p;            // the point: the hit (BREAK), the anchor (JOINT), the last position (GONE)
    float value;           // BREAK: the hit (m/s, see toughness); JOINT: the force (N) or the torque (N m)
    int16_t first, count;  // TP_EV_BREAK: the new pieces are w->piece[first .. first + count)
} tp_event_t;

typedef struct {
    int awake, sleeping;    // the dynamic bodies
    int pairs;              // the pairs of the broad phase (their boxes touch)
    int narrow;             // the shape tests of the contacts
    int contacts, points;   // the contacts with points, and the points (the solver works on them)
    int joints;             // the joints in the solver
    int islands;            // the islands of awake bodies
    int parts, part_tests;  // the particles, and their tests against the shapes
    int breaks;             // the bodies that broke in the last step
} tp_stats_t;

typedef struct {
    tp_vec_t gravity;  // m/s2 (default 0, -9.8)
    float dt;          // the fixed step (default 1/60 s)
    int substeps;      // 1 .. 8 (default 3; 4 = the stiffest stacks, 1/5 more time; 2 = the fastest:
                       // a chain with a heavy weight stretches)
    tp_box_t bounds;   // a body that leaves this box goes away (an event)
    int max_bodies, max_joints, max_contacts, max_parts, max_hits, max_events;
    uint32_t seed;  // the start of the random numbers (the breaks, the spills)
    float cell;     // the cell size of the particle grid (m; 0 = 0.5 m, or more for a big world)
    void *mem;      // a block of tp_mem_size() bytes for all the memory, or NULL (tp_init gets it)
} tp_world_def_t;

typedef struct tp_world {
    // the settings: tp_init copies them; the game can change them between two steps
    tp_vec_t gravity;
    float dt;
    int substeps;
    tp_box_t bounds;
    float slop;                      // the length tolerance of the shapes (0.005 m: corners nearer than this are one)
    float spec;                      // the speculative distance (0.02 m) (more for a fast body: its move in a step)
    float contact_hz, contact_zeta;  // the stiffness and the damping of the contacts (30 Hz, 10)
    float joint_hz, joint_zeta;      // the joints (60 Hz, 5)
    float push_max;                  // the largest speed that pushes overlapped bodies apart (3 m/s)
    float bounce_min;                // a hit slower than this does not bounce (1 m/s)
    float sleep_v, sleep_time;       // a body sleeps after sleep_time s below sleep_v (0.05 m/s, 0.5 s)
    float v_max;                     // the speed limit (60 m/s)
    float hit_min;                   // a hit event needs this approach speed (0.4 m/s)
    int max_breaks;                  // the breaks in one step, at most (4)
    float fade_area;                 // a piece smaller than this (m2) fades out (0.006)
    float crumb_area;                // a piece smaller than this (m2) becomes particles (0.0006)
    tp_mat_t mat[TP_MAT_MAX];
    tp_pkind_t pkind[TP_PART_KINDS];
    // the state (read only)
    tp_body_t *body;  // the slots 0 .. body_hi - 1 (a free slot has no TP_ALIVE)
    int body_cap, body_hi, nbody;
    tp_joint_t *joint;  // the slots 0 .. joint_hi - 1 (TPJ_ALIVE)
    int joint_cap, joint_hi, njoint;
    tp_contact_t *contact;  // the slots 0 .. contact_hi - 1 (a live contact has a >= 0)
    int contact_cap, contact_hi, ncontact;
    tp_part_t *part;  // the live particles 0 .. npart - 1
    int part_cap, npart;
    tp_hit_t *hit;  // the hits of the last step
    int hit_cap, nhit;
    tp_event_t *ev;  // the events of the last step
    int ev_cap, nev;
    tp_id_t *piece;  // the new pieces of the last step (TP_EV_BREAK)
    int piece_cap, npiece;
    int splash[TP_PART_KINDS];      // the particles that hit a body faster than 1 m/s in the last step (a splash sound)
    float splash_v[TP_PART_KINDS];  // the fastest of those hits (m/s)
    tp_vec_t splash_p[TP_PART_KINDS];  // and its point
    tp_stats_t stat;                   // the counts of the last step
    uint32_t stepno;                   // the steps since tp_init
    uint32_t rng;                      // the random state (the seed)
    struct tp_in *in;                  // (internal) the solver arrays
    void *mem;
    bool own_mem;
} tp_world_t;

// ---------------------------------------------------------------- the world
tp_world_def_t tp_world_defaults(void);
size_t tp_mem_size(const tp_world_def_t *d);   // the bytes that tp_init needs for d
tp_world_t *tp_init(const tp_world_def_t *d);  // NULL = no memory (or a bad def)
void tp_free(tp_world_t *w);                   // frees the memory (only the memory that tp_init got)
void tp_clear(tp_world_t *w);                  // removes all bodies, joints and particles (a new level)
void tp_step(tp_world_t *w);                   // one fixed step of w->dt

// ---------------------------------------------------------------- the bodies
tp_body_def_t tp_body_def(int kind, float x, float y, int mat);
tp_id_t tp_add_box(tp_world_t *w, const tp_body_def_t *d, float hw, float hh);  // half width, half height
tp_id_t tp_add_circle(tp_world_t *w, const tp_body_def_t *d, float r);
// a convex polygon: the convex hull of the n points (3 .. 8, round d->pos, turned by d->angle); TP_NONE = a bad shape
tp_id_t tp_add_poly(tp_world_t *w, const tp_body_def_t *d, const tp_vec_t *pts, int n);
tp_body_t *tp_body(tp_world_t *w, tp_id_t id);      // NULL = the body went away
tp_id_t tp_body_id(const tp_world_t *w, int slot);  // the id of a live slot (TP_NONE for a free slot)
void tp_remove(tp_world_t *w, tp_id_t id);          // removes the body and its joints now (no event)
void tp_wake(tp_world_t *w, tp_id_t id);
void tp_set_vel(tp_world_t *w, tp_id_t id, tp_vec_t v, float spin);        // wakes the body
void tp_impulse(tp_world_t *w, tp_id_t id, tp_vec_t imp, tp_vec_t point);  // N s at a world point; wakes it
void tp_force(tp_world_t *w, tp_id_t id, tp_vec_t f, tp_vec_t point);      // N at a world point, for the next step
void tp_set_place(tp_world_t *w, tp_id_t id, tp_vec_t pos, float angle);   // a jump to a new place (the speed stays)
// a kinematic body (a crane, a platform): it moves to pos, angle in the next step (the speed for it; it pushes the
// dynamic bodies). Call it in each frame for a smooth path.
void tp_move_to(tp_world_t *w, tp_id_t id, tp_vec_t pos, float angle);
// break the body now at the world point p (dir: the direction of the hit, into the body; 0, 0 = from p to the
// centre); false = it cannot break (no material pattern, no place for the pieces)
bool tp_break(tp_world_t *w, tp_id_t id, tp_vec_t p, tp_vec_t dir);
float tp_angle(const tp_body_t *b);                           // the angle of the body (rad)
int tp_verts(const tp_body_t *b, tp_vec_t *out);              // the corners in the world (TP_POLY; returns n)
tp_vec_t tp_world_point(const tp_body_t *b, tp_vec_t local);  // a local point (from the centre) in the world
tp_vec_t tp_local_point(const tp_body_t *b, tp_vec_t world);
tp_vec_t tp_point_vel(const tp_body_t *b, tp_vec_t world);  // the speed of the body at a world point

// ---------------------------------------------------------------- the joints
tp_joint_def_t tp_revolute_def(tp_id_t a, tp_id_t b, tp_vec_t anchor);
tp_joint_def_t tp_weld_def(tp_id_t a, tp_id_t b, tp_vec_t anchor);
tp_joint_def_t tp_distance_def(tp_id_t a, tp_id_t b, tp_vec_t pa, tp_vec_t pb);         // rigid at the length now
tp_joint_def_t tp_rope_def(tp_id_t a, tp_id_t b, tp_vec_t pa, tp_vec_t pb, float len);  // slack below len
tp_id_t tp_add_joint(tp_world_t *w, const tp_joint_def_t *d);         // TP_NONE = no place or a bad def
tp_joint_t *tp_joint(tp_world_t *w, tp_id_t id);                      // NULL = the joint went away
void tp_joint_remove(tp_world_t *w, tp_id_t id);                      // (no event)
void tp_motor(tp_world_t *w, tp_id_t id, float speed, float torque);  // a revolute motor (speed rad/s); wakes it
void tp_joint_anchors(const tp_world_t *w, const tp_joint_t *j, tp_vec_t *pa, tp_vec_t *pb);  // in the world

// ---------------------------------------------------------------- the particles (no forces between them)
tp_part_t *tp_emit(tp_world_t *w, int kind, tp_vec_t p, tp_vec_t v);  // NULL = full
// n particles at random points in the shape of the body, with its speed (the water of a tank spills)
int tp_spill(tp_world_t *w, tp_id_t id, int n, int kind);

// ---------------------------------------------------------------- the queries
typedef struct {
    tp_id_t id;     // the body (TP_NONE = no hit)
    tp_vec_t p, n;  // the point and the normal of the surface
    float t;        // the fraction of the ray, 0 .. 1
} tp_ray_t;
// the first body on the line from p0 to p1 whose cat has a bit of mask (0xFFFF = all); false = none
bool tp_ray(tp_world_t *w, tp_vec_t p0, tp_vec_t p1, uint16_t mask, tp_ray_t *out);
// the body under a point (a finger), at most r from it: a dynamic or kinematic body first, then a static one
tp_id_t tp_point(tp_world_t *w, tp_vec_t p, float r, uint16_t mask);
// the bodies whose boxes touch the box: at most max ids into out; returns the count
int tp_query_box(tp_world_t *w, tp_box_t box, uint16_t mask, tp_id_t *out, int max);
