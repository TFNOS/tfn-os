#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

bool imu_ok(void);

// A task reads the sensor at 200 Hz (gyro + accelerometer filter).

// Direction of gravity in units of g.
// Physical frame: +x = right, +y = down (portrait layout), +z = into the screen.
void imu_gravity_phys(float *gx, float *gy, float *gz);
// Absolute gravity in the logical frame of the current orientation (+x right, +y down).
void imu_gravity_logical(float *gx, float *gy);

// Take the current board position as "level" (a virtual flat table). The Watcher (no sensor): the wheel position now.
void imu_set_level(void);

// Tilt relative to the level position, logical frame, in g (0 when level). The Watcher: the wheel is a left/right
// tilt, but an app with app_t.wheel (it reads the wheel itself) always gets 0.
void imu_tilt_logical(float *tx, float *ty);

// Motion of the board without gravity, in g: logical x/y of the current orientation,
// z = into the screen.
void imu_linear_logical(float *ax, float *ay, float *az);
// Shake level in g: the peak of the linear acceleration, it falls to a third in 0.25 s.
float imu_shake(void);
// Shake ticks: when on, the IMU task plays a short tick at the end of each stroke of a shake
// (the board stops hard and goes back), so down-up-down-up gives tick-tick-tick-tick.
// A harder stop gives a louder tick. main.c sets it to off before each app starts.
void imu_shake_ticks(bool on);

// A hit: a sharp jerk of the board. x, y in the logical frame of the current
// orientation, z = into the screen; g = the peak size.
typedef struct {
    float x, y, z, g;
    int64_t t_us;
} imu_hit_t;
bool imu_pop_hit(imu_hit_t *h);
void imu_hits_clear(void);

// A snap: the board moves fast and then stops at once (like the flick of a towel).
// Loose things in the board then continue in the direction of the move.
// x, y: unit direction of the move in the logical frame; v: the top speed in m/s.
typedef struct {
    float x, y, v;
    int64_t t_us;
} imu_snap_t;
bool imu_pop_snap(imu_snap_t *s);
void imu_snaps_clear(void);

// The dial: the turn of the board around the screen normal, from the gyro (for example
// the board flat on a table, turned like the dial of a safe). In radians, + = clockwise as
// the user looks at the screen; it counts from the start-up and does not wrap. The gyro
// bias is removed while the board is still. imu_turn_rate() is the rate now (rad/s).
float imu_dial(void);
// The turn of the board about each physical axis since the start-up (rad), the bias removed while still.
void imu_gyro_sum(float a[3]);

// Steps of a person who walks with the board in the hand (a pedometer): the count since
// the start-up. Use the change of the count.
uint32_t imu_steps(void);

// Pick the upright orientation for a family (portrait or landscape).
// Returns `current` if the board is too flat to decide.
orient_t imu_upright(bool landscape, orient_t current, float min_g);
// Pick the upright orientation of the four (portrait or landscape, the menu). The family
// changes only when the other axis is clearly stronger. Returns `current` if the board is
// too flat to decide.
orient_t imu_upright_any(orient_t current, float min_g);
