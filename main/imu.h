#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/imu.h.
#include "tfn/imu.h"

#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

// QMI8658 on I2C0. Call imu_init() after board_init().
bool imu_init(void);
// The screen is off (main.c): 25 readings a second and the gyro off (the motion wake needs only the gravity).
void imu_slow(bool slow);

// A task reads the sensor at 200 Hz (gyro + accelerometer filter).

// The Watcher: move the level wheel position by these steps (the settings panel reads the wheel: its turns must not
// tilt the app). Nothing on the other boards.
void imu_wheel_shift(int steps);

// The scale of the accelerometer: a sensor can read the gravity a little high or low (one 3.49
// reads 1.26 g). AUTO: the scale is measured while the board is still (1 g / the size of the reading).
// MANUAL: a fixed scale from SETUP > TILT SCALE (store key "imu_scale": 0 = AUTO, else the scale x 1000).
float imu_accel_scale(void);  // the scale now in use
bool imu_accel_scale_auto(void);
void imu_set_accel_scale(float k);  // k <= 0: AUTO; else MANUAL (0.70 .. 1.40); it is saved
float imu_raw_g(void);              // the size of the raw reading (g, filtered): about 1 at rest

// The calibration of the accelerometer axes (SETUP > TILT CALIBRATE): a = (raw - off) * gain for
// each axis, before the scale. off = NULL: no calibration. It is saved.
void imu_accel_cal_set(const float off[3], const float gain[3]);
bool imu_accel_cal_on(void);
void imu_accel_cal_get(float off[3], float gain[3]);
// The last reading of the sensor: the accelerometer (g) with no calibration and no scale, the gyro (rad/s)
void imu_raw(float a[3], float w[3]);

// The speed of the board in m/s, physical frame (GAMEPAD RETRO MOVE). The linear acceleration
// against a slow gravity filter, integrated with a leak (0.5 s); at rest it goes to 0 fast.
void imu_velocity_phys(float v[3]);
// The linear acceleration (gravity removed) in the physical frame, m/s2, filtered about 20 ms.
void imu_linacc_phys(float a[3]);

float imu_turn_rate(void);
