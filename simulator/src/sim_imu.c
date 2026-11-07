// The simulator: the motion sensor (imu.h). The window gives the position of the board (pose: the orient_t that is
// upright for the user), a tilt (left/right and forward/back, -1..1, as the user sees the screen), a shake level, the
// dial angle and single events (a knock, a flick, a step). The gravity in the physical frame comes from these, with
// the same axes as main/imu.c: the board held upright in ORIENT_PORTRAIT has gravity on +y, in ORIENT_LAND_A on -x.
#include <math.h>
#include <string.h>
#include "imu.h"
#include "gfx.h"
#include "esp_timer.h"
#include "sim.h"

static volatile int s_pose = ORIENT_LAND_A;
static volatile float s_tx, s_ty;               // the tilt as the user sees the screen
static volatile float s_shake;                  // g
static volatile float s_dial, s_rate;           // rad, rad/s (+ = clockwise)
static int64_t s_dial_t;
static volatile uint32_t s_steps;
static float s_ascale = 1.0f;
static bool s_ascale_auto = true;
static float s_cal_off[3], s_cal_gain[3] = {1, 1, 1};
static bool s_cal_on;

#define N_HITS 8
static imu_hit_t s_hits[N_HITS];
static volatile int s_hit_r, s_hit_w;
static imu_snap_t s_snaps[N_HITS];
static volatile int s_snap_r, s_snap_w;

// the view frame (as the user sees the screen, x right, y down) to the physical frame, for a pose
static void view_to_phys(int pose, float vx, float vy, float *px, float *py)
{
    switch (pose) {
    case ORIENT_PORTRAIT:       *px = vx;  *py = vy;  break;
    case ORIENT_PORTRAIT_FLIP:  *px = -vx; *py = -vy; break;
    case ORIENT_LAND_A:         *px = -vy; *py = vx;  break;
    default:                    *px = vy;  *py = -vx; break;
    }
}
// the physical frame to the logical frame of the current orientation (main/imu.c phys_to_logical)
static void phys_to_logical(float px, float py, float *lx, float *ly)
{
    switch (gfx_orient()) {
    case ORIENT_PORTRAIT:       *lx = px;  *ly = py;  break;
    case ORIENT_PORTRAIT_FLIP:  *lx = -px; *ly = -py; break;
    case ORIENT_LAND_A:         *lx = py;  *ly = -px; break;
    default:                    *lx = -py; *ly = px;  break;
    }
}

bool imu_init(void) { return true; }
void imu_slow(bool slow) { (void)slow; }
bool imu_ok(void) { return true; }

// The board in the hand: the screen 45 degrees back from upright. The left/right tilt turns it up to 60 degrees, the
// forward/back tilt changes the 45 degrees by up to 45.
void imu_gravity_phys(float *gx, float *gy, float *gz)
{
    float roll = s_tx * 1.0472f, pitch = 0.7854f + s_ty * 0.7854f;
    float vx = sinf(roll), vy = cosf(roll) * sinf(pitch), vz = cosf(roll) * cosf(pitch);
    view_to_phys(s_pose, vx, vy, gx, gy);
    *gz = vz;
}

void imu_gravity_logical(float *gx, float *gy)
{
    float px, py, pz;
    imu_gravity_phys(&px, &py, &pz);
    phys_to_logical(px, py, gx, gy);
}

void imu_tilt_logical(float *tx, float *ty)
{
    float px, py;
    view_to_phys(s_pose, s_tx, s_ty, &px, &py);
    phys_to_logical(px, py, tx, ty);
}

void imu_set_level(void) {}
void imu_wheel_shift(int steps) { (void)steps; }
float imu_accel_scale(void) { return s_ascale; }
bool imu_accel_scale_auto(void) { return s_ascale_auto; }
void imu_set_accel_scale(float k)
{
    s_ascale_auto = k <= 0;
    if (k > 0) s_ascale = k < 0.7f ? 0.7f : k > 1.4f ? 1.4f : k;
}
float imu_raw_g(void) { return 1.0f; }
void imu_accel_cal_set(const float off[3], const float gain[3])
{
    s_cal_on = off && gain;
    for (int i = 0; i < 3; i++) {
        s_cal_off[i] = off ? off[i] : 0;
        s_cal_gain[i] = gain ? gain[i] : 1;
    }
}
bool imu_accel_cal_on(void) { return s_cal_on; }
void imu_accel_cal_get(float off[3], float gain[3])
{
    for (int i = 0; i < 3; i++) {
        off[i] = s_cal_off[i];
        gain[i] = s_cal_gain[i];
    }
}

// the shake: a movement of about 5 Hz in the plane of the screen, its size the shake level (g)
static void shake_phys(float a[3])
{
    double t = (double)esp_timer_get_time() / 1e6;
    float s = s_shake;
    float vx = s * (float)sin(t * 31.4), vy = s * (float)sin(t * 27.0 + 1.0);
    view_to_phys(s_pose, vx, vy, &a[0], &a[1]);
    a[2] = 0.3f * s * (float)sin(t * 37.7 + 2.0);
}

void imu_raw(float a[3], float w[3])
{
    float lin[3];
    shake_phys(lin);
    imu_gravity_phys(&a[0], &a[1], &a[2]);
    for (int i = 0; i < 3; i++) a[i] += lin[i];
    w[0] = w[1] = 0;
    w[2] = s_rate;
}

void imu_linear_logical(float *ax, float *ay, float *az)
{
    float a[3];
    shake_phys(a);
    phys_to_logical(a[0], a[1], ax, ay);
    *az = a[2];
}
float imu_shake(void) { return s_shake; }
void imu_shake_ticks(bool on) { (void)on; }
void imu_velocity_phys(float v[3])
{
    double t = (double)esp_timer_get_time() / 1e6;
    float s = s_shake * 0.3f;
    float vx = -s * (float)cos(t * 31.4), vy = -s * (float)cos(t * 27.0 + 1.0);
    view_to_phys(s_pose, vx, vy, &v[0], &v[1]);
    v[2] = 0;
}
void imu_linacc_phys(float a[3])
{
    shake_phys(a);
    for (int i = 0; i < 3; i++) a[i] *= 9.81f;
}

bool imu_pop_hit(imu_hit_t *h)
{
    if (s_hit_r == s_hit_w) return false;
    *h = s_hits[s_hit_r];
    s_hit_r = (s_hit_r + 1) % N_HITS;
    float lx, ly;
    phys_to_logical(h->x, h->y, &lx, &ly);
    h->x = lx;
    h->y = ly;
    return true;
}
void imu_hits_clear(void) { s_hit_r = s_hit_w; }

bool imu_pop_snap(imu_snap_t *s)
{
    if (s_snap_r == s_snap_w) return false;
    *s = s_snaps[s_snap_r];
    s_snap_r = (s_snap_r + 1) % N_HITS;
    float lx, ly;
    phys_to_logical(s->x, s->y, &lx, &ly);
    s->x = lx;
    s->y = ly;
    return true;
}
void imu_snaps_clear(void) { s_snap_r = s_snap_w; }

float imu_dial(void) { return s_dial; }
float imu_turn_rate(void)
{
    if (esp_timer_get_time() - s_dial_t > 200000) s_rate = 0;   // (the dial stopped)
    return s_rate;
}
void imu_gyro_sum(float a[3]) { a[0] = a[1] = 0; a[2] = s_dial; }
uint32_t imu_steps(void) { return s_steps; }

orient_t imu_upright(bool landscape, orient_t current, float min_g)
{
    orient_t def = landscape ? ORIENT_LAND_A : ORIENT_PORTRAIT;
    if (orient_is_landscape(current) != landscape) current = def;
    float px, py, pz;
    imu_gravity_phys(&px, &py, &pz);
    if (landscape) {
        if (px < -min_g) return ORIENT_LAND_A;
        if (px > min_g) return ORIENT_LAND_B;
    } else {
        if (py > min_g) return ORIENT_PORTRAIT;
        if (py < -min_g) return ORIENT_PORTRAIT_FLIP;
    }
    return current;
}

orient_t imu_upright_any(orient_t current, float min_g)
{
    float px, py, pz;
    imu_gravity_phys(&px, &py, &pz);
    bool land = orient_is_landscape(current);
    if (land && fabsf(py) > fabsf(px) + 0.25f && fabsf(py) > min_g) land = false;
    else if (!land && fabsf(px) > fabsf(py) + 0.25f && fabsf(px) > min_g) land = true;
    return imu_upright(land, current, min_g);
}

// ---------------------------------------------------------------- the window side
void sim_imu_pose(int orient) { if (orient >= 0 && orient <= 3) s_pose = orient; }
void sim_imu_tilt(float tx, float ty)
{
    s_tx = tx < -1 ? -1 : tx > 1 ? 1 : tx;
    s_ty = ty < -1 ? -1 : ty > 1 ? 1 : ty;
}
void sim_imu_shake(float g) { s_shake = g < 0 ? 0 : g > 4 ? 4 : g; }
void sim_imu_dial(float rad)
{
    int64_t now = esp_timer_get_time();
    float dt = (float)(now - s_dial_t) / 1e6f;
    if (dt > 0.001f) {
        float r = (rad - s_dial) / dt;
        s_rate = s_dial_t ? r : 0;
        s_dial_t = now;
    }
    s_dial = rad;
}

// 0 a knock on the screen, 1..4 a flick up, down, left, right (as the user sees the screen), 5 a step
void sim_imu_event(int kind)
{
    int64_t now = esp_timer_get_time();
    if (kind == 0) {
        float px, py;
        view_to_phys(s_pose, 0, 0, &px, &py);
        s_hits[s_hit_w] = (imu_hit_t){px, py, 1.0f, 2.6f, now};
        s_hit_w = (s_hit_w + 1) % N_HITS;
        if (s_hit_w == s_hit_r) s_hit_r = (s_hit_r + 1) % N_HITS;
    } else if (kind >= 1 && kind <= 4) {
        static const float D[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        float px, py;
        view_to_phys(s_pose, D[kind - 1][0], D[kind - 1][1], &px, &py);
        s_snaps[s_snap_w] = (imu_snap_t){px, py, 1.5f, now};
        s_snap_w = (s_snap_w + 1) % N_HITS;
        if (s_snap_w == s_snap_r) s_snap_r = (s_snap_r + 1) % N_HITS;
    } else if (kind == 5) {
        s_steps++;
    }
}
