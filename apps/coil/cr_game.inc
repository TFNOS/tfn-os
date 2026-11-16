// COIL RIDER: the rules of a run (included by app_coil.c): the controls (the tilt and the touch, through a filter), the
// steps of the physics, the pickups, the exit gate and the time. All the state of a run is in G (the PC search of the
// tracks copies G to try more than one way from the same point).

#define PICK_R 0.62f  // a part of the bike nearer than this takes a pickup (m)
#define GATE_R 0.80f  // the gate takes the bike (m, from its middle 0.9 m over the ground point)
#define TURN_S 0.30f  // the time of the turn of the rider (s)

// the tilt sensitivity: the dead zone and the full gas (degrees as the sine of the angle)
enum {
    SENS_LOW,
    SENS_MID,
    SENS_HIGH
};
static const float SENS_DEAD[3] = {0.0698f, 0.0523f, 0.0349f};  // 4, 3, 2 degrees
static const float SENS_FULL[3] = {0.3746f, 0.2588f, 0.1736f};  // 22, 15, 10 degrees

typedef struct {
    bike_t b;
    uint32_t got;  // the pickups that the bike took (one bit each)
    int ngot;
    bool done;          // the bike went through the open gate
    float t;            // the time of the run (s)
    float acc;          // the physics time that waits (s)
    float tilt_f;       // the tilt after the filter (the sine of the angle, + = to the right)
    float lean_in;      // the lean that the input asks for (-1, 0, +1)
    float turn_k;       // the turn of the rider 0..1 (1 = done)
    float hx_from;      // the head x at the start of the turn
    uint8_t last_pick;  // (the sound) the pickups taken in the last frame
    float max_land;     // (the camera and the sound) the hardest landing of the last frame
} game_t;
static game_t G;
static const cr_level_t *s_L;
static int s_sens = SENS_MID;
static bool s_tilt_on = true;

static void game_start(int lv)
{
    s_L = &CR_LEV[lv];
    terrain_load(s_L);
    memset(&G, 0, sizeof G);
    bike_place(&G.b, s_L->sx * (1.0f / CR_VK), s_L->sy * (1.0f / CR_VK), s_L->sdir);
    G.turn_k = 1;
    // (0.4 s: the springs settle on the ground; no input)
    for (int k = 0; k < 144; k++) {
        bike_step(&G.b, PDT);
    }
    G.b.vx = G.b.vy = G.b.w = 0;
    for (int i = 0; i < 2; i++) {
        G.b.wh[i].vx = G.b.wh[i].vy = G.b.wh[i].w = 0;
    }
    G.b.crash = false;
    G.b.land = 0;
}
static inline float gate_x(void)
{
    return s_L->ex * (1.0f / CR_VK);
}
static inline float gate_y(void)
{
    return s_L->ey * (1.0f / CR_VK);
}
static inline bool gate_open(void)
{
    return G.ngot >= s_L->np;
}

// the turn: the rider goes to the other end of the seat (the head moves in TURN_S)
static void game_turn(void)
{
    if (G.done || G.b.crash) {
        return;
    }
    G.hx_from = G.b.hx;
    G.b.dir = (int8_t)-G.b.dir;
    G.turn_k = 0;
}

// the gas from the tilt (the filter, the dead zone, the full angle): + = forward (the tilt toward the face of the bike)
static float tilt_gas(float tx, float dt)
{
    G.tilt_f += (tx - G.tilt_f) * clampf(dt * 11.0f, 0, 1);  // (a soft filter: about 0.09 s)
    float m = fabsf(G.tilt_f), d = SENS_DEAD[s_sens], f = SENS_FULL[s_sens];
    float u = clampf((m - d) * frcp(f - d), 0, 1);
    u = u * u * (3 - 2 * u) * 0.35f + u * 0.65f;  // (a soft start after the dead zone)
    float g = G.tilt_f > 0 ? u : -u;
    return g * G.b.dir;
}

// the steps of the physics for the time gdt; the gas and the lean are set
static void game_physics(float gdt, float gas, float lean, bool crouch)
{
    G.b.gas = clampf(gas, -1, 1);
    G.b.crouch = crouch;
    G.last_pick = 0;
    G.max_land = 0;
    G.acc += gdt;
    int n = 0;
    while (G.acc >= PDT && n < 24) {
        G.acc -= PDT;
        n++;
        // the lean follows the input softly (about 0.08 s)
        G.b.lean_f += (lean - G.b.lean_f) * (PDT * 12.0f);
        if (G.turn_k < 1) {
            G.turn_k += PDT * (1.0f / TURN_S);
            if (G.turn_k > 1) {
                G.turn_k = 1;
            }
            float k = G.turn_k * G.turn_k * (3 - 2 * G.turn_k);
            G.b.hx = lerpf(G.hx_from, HEAD_X * G.b.dir, k);
        }
        bike_step(&G.b, PDT);
        if (G.b.land > G.max_land) {
            G.max_land = G.b.land;
        }
        G.b.land = 0;
        if (G.b.crash) {
            break;
        }
        G.t += PDT;
        // the pickups and the gate
        float hx, hy;
        head_pos(&G.b, &hx, &hy);
        for (int k = 0; k < s_L->np; k++) {
            if (G.got & (1u << k)) {
                continue;
            }
            float px = s_L->pk[2 * k] * (1.0f / CR_VK), py = s_L->pk[2 * k + 1] * (1.0f / CR_VK);
            float r2 = PICK_R * PICK_R;
            bool hit = false;
            float qx[4] = {G.b.x, G.b.wh[0].x, G.b.wh[1].x, hx}, qy[4] = {G.b.y, G.b.wh[0].y, G.b.wh[1].y, hy};
            for (int j = 0; j < 4 && !hit; j++) {
                float dx = qx[j] - px, dy = qy[j] - py;
                if (dx * dx + dy * dy < r2) {
                    hit = true;
                }
            }
            if (hit) {
                G.got |= 1u << k;
                G.ngot++;
                G.last_pick++;
            }
        }
        if (gate_open()) {
            float gx = gate_x(), gy = gate_y() + 0.9f;
            float dx = G.b.x - gx, dy = G.b.y - gy;
            if (dx * dx + dy * dy < GATE_R * GATE_R * 2.2f) {
                G.done = true;
                break;
            }
        }
    }
    if (n == 24) {
        G.acc = 0;
    }
}
