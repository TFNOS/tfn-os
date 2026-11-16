// THE TURNING WOOD: the state of a game and the rules (the tables of tw_game.inc, the same as the design: the solver of
// the design proves them). Included by app_wood.c.
#include "tw_game.inc"

typedef struct {
    uint8_t pos, season;  // the place; the season of the wood (0 SP, 1 SU, 2 AU, 3 WI)
    uint64_t bits;        // the objects in the bag (bit = item) and the facts (bit TW_NITEM + fact)
    uint64_t done;        // the actions that were done (bit = action)
    uint32_t seen;        // the places that the player knows (the map)
} tw_state_t;

static tw_state_t T;
// (a 64-bit shift by a variable needs a libgcc function on the board that the ABI of an app file does not have)
static inline bool bit64(uint64_t v, int i)
{
    const uint32_t w = i < 32 ? (uint32_t)v : (uint32_t)(v >> 32);
    return (w >> (i & 31)) & 1;
}
static inline uint64_t one64(int i)
{
    return i < 32 ? (uint64_t)(1u << i) : (uint64_t)(1u << (i - 32)) << 32;
}
static inline bool has_item(int i)
{
    return bit64(T.bits, i);
}
static inline bool has_fact(int f)
{
    return bit64(T.bits, TW_BIT_FACT(f));
}
static inline bool is_done(int a)
{
    return bit64(T.done, a);
}
static inline bool season_ok(uint8_t smask)
{
    return (smask >> T.season) & 1;
}
static inline bool need_ok(uint64_t need)
{
    return (T.bits & need) == need;
}

static void tw_new(void)
{
    memset(&T, 0, sizeof T);
    T.pos = TW_START;
    T.season = TW_START_SEASON;
    T.bits = TW_START_BITS;
    T.seen = 1u << TW_START;
}

// a path from the place now to `to`: -1 = no such path; 0 = closed (why: the reason); 1 = open
static int path_state(int to, const char **why)
{
    for (int i = 0; i < TW_NPATH; i++) {
        const tw_path_t *p = &TW_PATHS[i];
        if (!((p->a == T.pos && p->b == to) || (p->b == T.pos && p->a == to))) {
            continue;
        }
        if (need_ok(p->need) && season_ok(p->smask)) {
            return 1;
        }
        if (why) {
            *why = p->why[0][0] ? p->why[s_lang] : NULL;
        }
        return 0;
    }
    return -1;
}

// can action a act now (all that it needs is here, and the season)? It ignores the place.
static bool act_ready(int a)
{
    const tw_act_t *A = &TW_ACTS[a];
    return !is_done(a) && need_ok(A->need) && season_ok(A->smask);
}
static void act_apply(int a)
{
    const tw_act_t *A = &TW_ACTS[a];
    T.bits |= A->gives;
    T.bits &= ~A->takes;
    T.done |= one64(a);
}

// the season stones: can the Wheel turn to season s?
static bool season_open(int s)
{
    return TW_SEASON_FACT[s] < 0 || has_fact(TW_SEASON_FACT[s]);
}

// the places that the player can walk to now from here (the map): a breadth search through the open paths
static uint32_t reachable_now(void)
{
    uint32_t got = 1u << T.pos, front = got;
    const uint8_t keep = T.pos;
    while (front) {
        uint32_t next = 0;
        for (int p = 0; p < TW_NPLACE; p++) {
            if (!(front >> p & 1)) {
                continue;
            }
            T.pos = (uint8_t)p;
            for (int q = 0; q < TW_NPLACE; q++) {
                if (!(got >> q & 1) && path_state(q, NULL) == 1) {
                    next |= 1u << q;
                }
            }
        }
        got |= next;
        front = next;
    }
    T.pos = keep;
    return got;
}

// the next goal for a hint: the first action of the best way through that is not done
static int next_goal(void)
{
    for (int i = 0; i < TW_NORDER; i++) {
        if (!is_done(TW_ORDER[i])) {
            return TW_ORDER[i];
        }
    }
    return -1;
}

// ---------------------------------------------------------------- the save
static void tw_save(void)
{
    store_set(TW_KEY "pos", T.pos);
    store_set(TW_KEY "se", T.season);
    store_set(TW_KEY "b0", (int32_t)(uint32_t)T.bits);
    store_set(TW_KEY "b1", (int32_t)(uint32_t)(T.bits >> 32));
    store_set(TW_KEY "d0", (int32_t)(uint32_t)T.done);
    store_set(TW_KEY "d1", (int32_t)(uint32_t)(T.done >> 32));
    store_set(TW_KEY "seen", (int32_t)T.seen);
    store_set(TW_KEY "has", 2);  // (2: a save of design 2; 1 was the old game)
}
static bool tw_load(void)
{
    if (store_get(TW_KEY "has", 0) != 2) {
        return false;
    }
    tw_new();
    T.pos = (uint8_t)(store_get(TW_KEY "pos", TW_START) % TW_NPLACE);
    T.season = (uint8_t)(store_get(TW_KEY "se", TW_START_SEASON) & 3);
    T.bits = (uint64_t)(uint32_t)store_get(TW_KEY "b0", 0) | (uint64_t)(uint32_t)store_get(TW_KEY "b1", 0) << 32;
    T.done = (uint64_t)(uint32_t)store_get(TW_KEY "d0", 0) | (uint64_t)(uint32_t)store_get(TW_KEY "d1", 0) << 32;
    T.seen = (uint32_t)store_get(TW_KEY "seen", 1) | 1u << T.pos;
    if (!T.bits) {
        T.bits = TW_START_BITS;
    }
    return T.pos != TW_GOAL || !is_done(A_YEAR);
}
