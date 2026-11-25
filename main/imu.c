// QMI8658 6-axis IMU (3.49): accelerometer +-8 g, gyroscope +-512 dps.
// CoreS3: BMI270 6-axis IMU at 0x69 (the CoreS3-SE has none): +-8 g, +-500 dps, 200 Hz.
// read_raw() turns the BMI270 axes into the axes of the QMI8658 on the 3.49 (see
// BMI_AXIS), so the rest of this file is the same for the two boards.
// A complementary filter turns the gravity vector with the gyro rates, then
// pulls it slowly toward the accelerometer reading. The result reacts fast
// and has little noise.
//
// Axes on this board (found from measurements):
//   sensor Z = screen normal (board flat, screen up: accel Z = +1 g)
//   screen x (right, portrait) = -sensor Y
//   screen y (down, portrait)  = -sensor X
//
// A task reads the sensor at 200 Hz. It updates the gravity estimate, the
// linear acceleration (the motion of the board without gravity) and a shake level.
//
// Tilt games use a "virtual level": the board position at the start of a game
// counts as flat. A rotation turns that reference gravity onto the screen normal,
// and the same rotation turns the current gravity. The in-plane part of the
// result is the tilt.
//
// The SenseCAP Watcher has no motion sensor: its wheel is a left/right tilt (read_raw() below). There the level is
// the wheel position at the last imu_set_level(), and an app that reads the wheel itself (app_t.wheel) gets no tilt
// (imu_tilt_logical()), so a step of the wheel does not move it twice.

#include <math.h>
#include <string.h>
#include "power.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "board.h"
#include "imu.h"
#include "audio.h"
#include "gfx.h"
#include "settings.h"
#if BOARD_IMU_WHEEL
#include "app.h"  // (app_current(): the app that reads the wheel itself gets no tilt from it)
#endif

static const char *TAG = "imu";

#define QMI_ADDR       BOARD_IMU_ADDR
#define REG_WHOAMI     0x00
#define REG_CTRL1      0x02
#define REG_CTRL2      0x03  // accel: range [6:4], ODR [3:0]
#define REG_CTRL3      0x04  // gyro:  range [6:4], ODR [3:0]
#define REG_CTRL5      0x06  // low-pass filters
#define REG_CTRL7      0x08  // sensor enable
#define REG_AX_L       0x35
#define REG_RST_RESULT 0x4D
#define REG_RESET      0x60

#define ACC_LSB_PER_G   4096.0f  // +-8 g (hard shakes go above 4 g)
#define RATE_HZ         200
#define GYR_LSB_PER_DPS 64.0f  // +-512 dps
#define DEG2RAD         0.017453293f

static i2c_master_dev_handle_t s_dev;
static bool s_ok;
static float s_f[3] = {0, 0, 1};  // filtered specific force, sensor frame, g
static int64_t s_last_us;

// linear acceleration (physical frame, g) and the shake level (g, peak hold)
static float s_lin[3];
static float s_shake;

// hits: sharp jerks of the board (for the drum machine and the snap game)
#define HIT_G      1.3f    // start of a hit
#define HIT_WIN    8       // samples to find the peak (40 ms)
#define HIT_GAP_US 150000  // no new hit this soon after one
#define N_HITS     8
static imu_hit_t s_hits[N_HITS];
static volatile int s_hit_w, s_hit_r;
static int s_hit_win;
static float s_hit_peak[3], s_hit_g;
static int64_t s_hit_last;

// snaps: the speed comes from the linear acceleration (with a leak, so that the
// drift goes away). A hard stop against a fast move is a snap.
#define SNAP_V      0.30f  // m/s: the move must be at least this fast
#define SNAP_STOP_G 1.2f   // g against the move: the stop
#define SNAP_GAP_US 300000
#define N_SNAPS     4
static float s_vel[2], s_vpeak, s_vdir[2];
static imu_snap_t s_snaps[N_SNAPS];
static volatile int s_snap_w, s_snap_r;
static int64_t s_snap_last;

// the dial: the turn of the board around the screen normal (gyro z, physical frame),
// with a slow estimate of the gyro bias while the board is still (SAFE)
static float s_dial, s_turn_rate, s_bias_z;
// the turn about each physical axis since the start-up (SKY), with the bias removed while still
static float s_gsum[3], s_gbias[3];

// steps: the vertical part of the linear acceleration (up = +), a rise then a fall (EGG RUN)
#define STEP_UP_G   0.12f
#define STEP_DOWN_G -0.08f
#define STEP_WIN_US 500000
#define STEP_GAP_US 300000
static float s_vert;
static int64_t s_step_up_at, s_step_last;
static volatile uint32_t s_steps;

// shake ticks: the end of a stroke is a peak of the linear acceleration (the board stops
// and turns back). A new tick needs the level to fall below TICK_OFF_G first, and the
// direction must turn back (or TICK_SAME_US must go by), so one stop gives one tick.
#define TICK_ON_G    0.9f
#define TICK_OFF_G   0.45f
#define TICK_WIN     4  // samples to find the peak (20 ms)
#define TICK_GAP_US  40000
#define TICK_SAME_US 300000
static volatile bool s_tick_on;
static bool s_tick_armed = true;
static int s_tick_win;
static float s_tick_peak, s_tick_dir[3], s_tick_last_dir[3];
static int64_t s_tick_last;

// The speed of the board (the RETRO MOVE mode of the GAMEPAD), in the sensor frame. A second
// gravity filter, only for the speed: it turns with the gyro (with its own bias, learned at
// rest, and a second-order step, so a fast flick of the wrist leaves no error) and corrects
// to the accelerometer only when the board is almost free of motion (else it takes a part
// of each move and gives it back after the move as a false speed the other way). The speed is
// the linear acceleration against it, integrated with a leak (VEL_TAU); at rest (the raw
// acceleration steady near 1 g for VEL_REST_MS) the speed goes to 0 fast.
#define VEL_TAU     0.5f
#define VEL_GTAU    0.5f  // (s) the correction toward the accelerometer while nothing pushes the board
#define VEL_REST_G  0.10f
#define VEL_REST_MS 200
static float s_vg[3] = {0, 0, 1}, s_vbias[3], s_valp[3] = {0, 0, 1}, s_vel3[3];
static float s_lin3[3];  // the linear acceleration (g, sensor frame), a little filtered
static float s_vrest_pk, s_vrest_ms;

// reference ("level") rotation, physical frame
static float s_rot[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};

static void imu_task(void *arg);

// the accelerometer scale (imu.h): AUTO learns it while the board is still
#define ASCALE_MIN 0.70f
#define ASCALE_MAX 1.40f
static float s_ascale = 1.0f, s_raw_g = 1.0f;
static bool s_ascale_auto = true;
static float s_still_ref, s_still_sum;
static int s_still_n;

// the calibration of the axes (imu.h): a = (raw - off) * gain; the last raw reading for SETUP
static float s_aoff[3], s_again[3] = {1, 1, 1}, s_rawa[3], s_raww[3];
static bool s_acal;
static const char *const IA_O[3] = {"ia_ox", "ia_oy", "ia_oz"}, *const IA_G[3] = {"ia_gx", "ia_gy", "ia_gz"};

static void acal_load(void)
{
    if (store_get("ia_on", 0) != 1) {
        return;
    }
    float o[3], g[3];
    for (int k = 0; k < 3; k++) {
        o[k] = (float)(int32_t)store_get(IA_O[k], 0) / 10000.0f;
        g[k] = (float)(int32_t)store_get(IA_G[k], 10000) / 10000.0f;
        if (g[k] < 0.5f || g[k] > 1.8f || fabsf(o[k]) > 0.6f) {
            return;  // not a sane calibration
        }
    }
    memcpy(s_aoff, o, sizeof o);
    memcpy(s_again, g, sizeof g);
    s_acal = true;
}

void imu_accel_cal_set(const float off[3], const float gain[3])
{
    if (!off || !gain) {
        s_acal = false;
        store_set("ia_on", 0);
        return;
    }
    for (int k = 0; k < 3; k++) {
        s_aoff[k] = off[k];
        s_again[k] = gain[k];
        store_set(IA_O[k], (uint32_t)(int32_t)lroundf(off[k] * 10000));
        store_set(IA_G[k], (uint32_t)(int32_t)lroundf(gain[k] * 10000));
    }
    s_acal = true;
    store_set("ia_on", 1);
}

bool imu_accel_cal_on(void)
{
    return s_acal;
}

void imu_accel_cal_get(float off[3], float gain[3])
{
    memcpy(off, s_aoff, sizeof s_aoff);
    memcpy(gain, s_again, sizeof s_again);
}

void imu_raw(float a[3], float w[3])
{
    memcpy(a, s_rawa, sizeof s_rawa);
    memcpy(w, s_raww, sizeof s_raww);
}

// the raw reading: kept for SETUP, then the calibration of the axes
static void acal_apply(float a[3], const float w[3])
{
    memcpy(s_rawa, a, sizeof s_rawa);
    memcpy(s_raww, w, sizeof s_raww);
#if !BOARD_IMU_WHEEL
    if (s_acal) {
        for (int k = 0; k < 3; k++) {
            a[k] = (a[k] - s_aoff[k]) * s_again[k];
        }
    }
#endif
}

float imu_accel_scale(void)
{
    return s_ascale;
}
bool imu_accel_scale_auto(void)
{
    return s_ascale_auto;
}
float imu_raw_g(void)
{
    return s_raw_g;
}

void imu_set_accel_scale(float k)
{
    if (k <= 0) {
        s_ascale_auto = true;  // AUTO: the next still moment measures it again
        s_still_n = 0;
        store_set("imu_scale", 0);
        return;
    }
    if (k < ASCALE_MIN) {
        k = ASCALE_MIN;
    }
    if (k > ASCALE_MAX) {
        k = ASCALE_MAX;
    }
    s_ascale_auto = false;
    s_ascale = k;
    store_set("imu_scale", (uint32_t)lroundf(k * 1000));
}

// the raw reading to g: learn the scale (AUTO, the board still for 0.5 s), then apply it
static void ascale_apply(float a[3], const float w[3])
{
#if BOARD_IMU_WHEEL
    (void)a;
    (void)w;
    return;  // the virtual sensor is exact
#else
    float m = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    s_raw_g += 0.05f * (m - s_raw_g);
    if (s_ascale_auto) {
        float wm = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
        if (wm < 0.06f && fabsf(m - s_still_ref) < 0.015f && m > 1 / ASCALE_MAX && m < 1 / ASCALE_MIN) {
            s_still_sum += m;
            if (++s_still_n >= RATE_HZ / 2) {
                float k = (float)s_still_n / s_still_sum;  // 1 g / the mean size
                s_ascale += 0.3f * (k - s_ascale);
                if (s_ascale < ASCALE_MIN) {
                    s_ascale = ASCALE_MIN;
                }
                if (s_ascale > ASCALE_MAX) {
                    s_ascale = ASCALE_MAX;
                }
                s_still_n = 0;
                s_still_sum = 0;
            }
        } else {
            s_still_ref = m;
            s_still_n = 0;
            s_still_sum = 0;
        }
    }
    for (int i = 0; i < 3; i++) {
        a[i] *= s_ascale;
    }
#endif
}

static __attribute__((unused)) esp_err_t wr(uint8_t reg, uint8_t val)
{
    uint8_t b[2] = {reg, val};
    return i2c_master_transmit(s_dev, b, 2, 50);
}

static __attribute__((unused)) esp_err_t rd(uint8_t reg, uint8_t *buf, size_t n)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, n, 50);
}

#if BOARD_IMU_BMI270
// ---------------------------------------------------------------- BMI270 (CoreS3; the Tab5 too, at 0x68)

#define BMI_ADDR            BOARD_IMU_ADDR
#define BMI_CHIP_ID         0x00  // 0x24
#define BMI_ACC_X_LSB       0x0C  // accel x, y, z, then gyro x, y, z (12 bytes)
#define BMI_INTERNAL_STATUS 0x21  // bits 0..3: 1 = the configuration file is OK
#define BMI_ACC_CONF        0x40
#define BMI_ACC_RANGE       0x41
#define BMI_GYR_CONF        0x42
#define BMI_GYR_RANGE       0x43
#define BMI_INIT_CTRL       0x59
#define BMI_INIT_ADDR_0     0x5B
#define BMI_INIT_DATA       0x5E
#define BMI_PWR_CONF        0x7C
#define BMI_PWR_CTRL        0x7D
#define BMI_CMD             0x7E
#define BMI_ACC_LSB_PER_G   4096.0f  // +-8 g
#define BMI_GYR_LSB_PER_DPS 65.536f  // +-500 dps
#define BMI_CHUNK           64       // bytes of the configuration file in one I2C write

extern const uint8_t bmi270_config_file[8192];

// The BMI270 axes in the physical frame of gfx.h (x right, y down in portrait, z into the
// screen): {sensor axis 0..2, sign} for physical x, y and z. Then read_raw() gives the axes of
// the QMI8658 of the 3.49 (sensor 0 = -physical y, sensor 1 = -physical x, sensor 2 = -physical z).
// (the map of each board: BOARD_IMU_AXIS in boards/<kind>/board_caps.h)
static const int8_t BMI_AXIS[3][2] = BOARD_IMU_AXIS;

static bool read_raw(float acc[3], float gyr[3])
{
    uint8_t b[12];
    if (rd(BMI_ACC_X_LSB, b, sizeof(b)) != ESP_OK) {
        return false;
    }
    float a[3], w[3], pa[3], pw[3];
    for (int i = 0; i < 3; i++) {
        a[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8)) / BMI_ACC_LSB_PER_G;
        w[i] = (int16_t)(b[6 + 2 * i] | (b[6 + 2 * i + 1] << 8)) / BMI_GYR_LSB_PER_DPS * DEG2RAD;
    }
    for (int i = 0; i < 3; i++) {
        pa[i] = BMI_AXIS[i][1] * a[BMI_AXIS[i][0]];
        pw[i] = BMI_AXIS[i][1] * w[BMI_AXIS[i][0]];
    }
    acc[0] = -pa[1], acc[1] = -pa[0], acc[2] = -pa[2];
    gyr[0] = -pw[1], gyr[1] = -pw[0], gyr[2] = -pw[2];
    return true;
}

// Start the BMI270: a soft reset, then its configuration file, then 200 Hz output.
static bool sensor_start(void)
{
    i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = BMI_ADDR,
            .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(board_i2c0(), &dc, &s_dev) != ESP_OK) {
        return false;
    }
    uint8_t v = 0;
    rd(BMI_CHIP_ID, &v, 1);  // the first read after power-on can fail
    if (rd(BMI_CHIP_ID, &v, 1) != ESP_OK || v != 0x24) {
        ESP_LOGI(TAG, "no BMI270 (CoreS3-SE): no IMU");
        i2c_master_bus_rm_device(s_dev);
        s_dev = NULL;
        return false;
    }
    wr(BMI_CMD, 0xB6);  // soft reset
    vTaskDelay(pdMS_TO_TICKS(5));
    wr(BMI_PWR_CONF, 0x00);  // no power save (necessary for the upload)
    vTaskDelay(pdMS_TO_TICKS(2));
    wr(BMI_INIT_CTRL, 0x00);
    uint8_t buf[1 + BMI_CHUNK];
    for (int i = 0; i < (int)sizeof(bmi270_config_file); i += BMI_CHUNK) {
        uint8_t addr[3] = {BMI_INIT_ADDR_0, (uint8_t)((i / 2) & 0x0F), (uint8_t)((i / 2) >> 4)};
        buf[0] = BMI_INIT_DATA;
        memcpy(buf + 1, bmi270_config_file + i, BMI_CHUNK);
        if (i2c_master_transmit(s_dev, addr, sizeof(addr), 50) != ESP_OK ||
            i2c_master_transmit(s_dev, buf, sizeof(buf), 50) != ESP_OK) {
            ESP_LOGE(TAG, "BMI270 configuration upload failed at %d", i);
            return false;
        }
    }
    wr(BMI_INIT_CTRL, 0x01);
    for (int i = 0; i < 30; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        if (rd(BMI_INTERNAL_STATUS, &v, 1) == ESP_OK && (v & 0x0F) == 0x01) {
            break;
        }
    }
    if ((v & 0x0F) != 0x01) {
        ESP_LOGE(TAG, "BMI270 configuration not accepted (status 0x%02X)", v);
        return false;
    }
    wr(BMI_ACC_CONF, 0xA9);   // performance mode, normal filter, 200 Hz
    wr(BMI_ACC_RANGE, 0x02);  // +-8 g
    wr(BMI_GYR_CONF, 0xE9);   // performance mode, normal filter, 200 Hz
    wr(BMI_GYR_RANGE, 0x02);  // +-500 dps
    wr(BMI_PWR_CTRL, 0x0E);   // accel, gyro and temperature on
    vTaskDelay(pdMS_TO_TICKS(50));
    ESP_LOGI(TAG, "BMI270 ok");
    return true;
}

#elif BOARD_IMU_WHEEL
// ---------------------------------------------------------------- the wheel as a tilt (Watcher)

// No motion sensor: the wheel position is a left/right tilt of the board (the frame of the
// screen as the user sees it; the Watcher does not turn its frames). WHEEL_DEG for each step, at
// most WHEEL_MAX_DEG. The values are in the axes of the QMI8658 of the 3.49 (flat, screen up:
// z = +1 g; sensor x = -physical y, sensor y = -physical x; a landscape frame has logical
// x = physical y). imu_step() takes the gravity from it with no filter.
// The level (0 degrees) is the wheel position at the last imu_set_level(), not the position at the start-up: the
// position counts all the steps (also the steps in the menu), and from a far position one side of the tilt stayed at
// WHEEL_MAX_DEG (no change for many steps).
#define WHEEL_DEG     1.5f
#define WHEEL_MAX_DEG 24.0f

static volatile int s_wheel_org;  // the level wheel position (imu_set_level() sets it; the IMU task reads it)

// the reading of the virtual sensor at this wheel position
static void wheel_acc(int pos, float acc[3])
{
    float deg = (float)(pos - s_wheel_org) * WHEEL_DEG;
    if (deg > WHEEL_MAX_DEG) {
        deg = WHEEL_MAX_DEG;
    }
    if (deg < -WHEEL_MAX_DEG) {
        deg = -WHEEL_MAX_DEG;
    }
    float s = sinf(deg * DEG2RAD), c = cosf(deg * DEG2RAD);
    // the "up" vector leans to the left when the right side goes down (+ = clockwise = right down)
    bool land = orient_is_landscape(gfx_orient());
    acc[0] = land ? s : 0.0f;
    acc[1] = land ? 0.0f : s;
    acc[2] = c;
}

static bool read_raw(float acc[3], float gyr[3])
{
    wheel_acc(board_wheel_pos(), acc);
    gyr[0] = gyr[1] = gyr[2] = 0.0f;
    return true;
}

static bool sensor_start(void)
{
    ESP_LOGI(TAG, "no motion sensor: the wheel is a left/right tilt");
    return true;
}

#elif BOARD_IMU_QMI8658
// ---------------------------------------------------------------- QMI8658 (3.49)

static bool read_raw(float acc[3], float gyr[3])
{
    uint8_t b[12];
    if (rd(REG_AX_L, b, sizeof(b)) != ESP_OK) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        acc[i] = (int16_t)(b[2 * i] | (b[2 * i + 1] << 8)) / ACC_LSB_PER_G;
        gyr[i] = (int16_t)(b[6 + 2 * i] | (b[6 + 2 * i + 1] << 8)) / GYR_LSB_PER_DPS * DEG2RAD;
    }
#if BOARD_IMU_TURN_Y
    // The QMI8658 of the AMOLED is turned 180 degrees about the physical y axis, compared with the
    // 3.49 (face up: z +1 on the 3.49, -1 here; portrait checked on the board). Then physical x
    // and z change sign: the sensor axes 1 and 2 (the gyro too: a turn, not a mirror).
    acc[1] = -acc[1];
    acc[2] = -acc[2];
    gyr[1] = -gyr[1];
    gyr[2] = -gyr[2];
#endif
    return true;
}

static bool sensor_start(void)
{
    i2c_device_config_t dc = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = QMI_ADDR,
            .scl_speed_hz = 400000,
    };
    if (i2c_master_bus_add_device(board_i2c0(), &dc, &s_dev) != ESP_OK) {
        return false;
    }
    wr(REG_RESET, 0xB0);
    uint8_t v = 0;
    for (int i = 0; i < 20; i++) {
        vTaskDelay(pdMS_TO_TICKS(5));
        if (rd(REG_RST_RESULT, &v, 1) == ESP_OK && v == 0x80) {
            break;
        }
    }
    if (rd(REG_WHOAMI, &v, 1) != ESP_OK || v != 0x05) {
        ESP_LOGE(TAG, "QMI8658 not found (id 0x%02X)", v);
        return false;
    }
    wr(REG_CTRL1, 0x40);             // address auto increment, little endian
    wr(REG_CTRL2, (2 << 4) | 0x05);  // +-8 g, about 235 Hz
    wr(REG_CTRL3, (5 << 4) | 0x05);  // +-512 dps, about 235 Hz
    wr(REG_CTRL5, 0x11);             // low-pass filter on for both
    wr(REG_CTRL7, 0x03);             // accel + gyro on
    return true;
}
#else
// No motion sensor on this board (all BOARD_IMU_* are 0).
static bool read_raw(float acc[3], float gyr[3])
{
    (void)acc;
    (void)gyr;
    return false;
}

static bool sensor_start(void)
{
    return false;
}
#endif

bool imu_init(void)
{
    if (!sensor_start()) {
        return false;
    }
#if !BOARD_IMU_WHEEL
    uint32_t sv = store_get("imu_scale", 0);
    if (sv >= 700 && sv <= 1400) {
        s_ascale_auto = false;
        s_ascale = (float)sv / 1000.0f;
    }
    acal_load();
#endif

    // The first samples after the start are 0 or not stable. Use the first two
    // samples in a row that are near 1 g and near each other.
    float a[3] = {0, 0, 1}, g[3], prev[3] = {0, 0, 0}, rprev[3] = {0, 0, 0};
    (void)rprev;
    for (int i = 0; i < 60; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!read_raw(a, g)) {
            continue;
        }
        acal_apply(a, g);
        float m = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
#if !BOARD_IMU_WHEEL
        // AUTO: two still raw readings in a row give the first scale (the board is still at the start)
        s_raw_g = m;
        if (s_ascale_auto && m > 1 / ASCALE_MAX && m < 1 / ASCALE_MIN) {
            float dx0 = a[0] - rprev[0], dy0 = a[1] - rprev[1], dz0 = a[2] - rprev[2];
            if (dx0 * dx0 + dy0 * dy0 + dz0 * dz0 < 0.0004f) {
                s_ascale = 1.0f / m;
            }
        }
        memcpy(rprev, a, sizeof(rprev));
        for (int k = 0; k < 3; k++) {
            a[k] *= s_ascale;
        }
        m *= s_ascale;
#endif
        float dx = a[0] - prev[0], dy = a[1] - prev[1], dz = a[2] - prev[2];
        bool stable = dx * dx + dy * dy + dz * dz < 0.01f;
        prev[0] = a[0];
        prev[1] = a[1];
        prev[2] = a[2];
        if (m > 0.8f && m < 1.2f && stable) {
            s_f[0] = a[0];
            s_f[1] = a[1];
            s_f[2] = a[2];
            break;
        }
    }
    memcpy(s_vg, s_f, sizeof(s_vg));
    memcpy(s_valp, s_f, sizeof(s_valp));
    s_last_us = esp_timer_get_time();
    s_ok = true;
    xTaskCreatePinnedToCore(imu_task, "imu", 3072, NULL, 4, NULL, 1);
    ESP_LOGI(TAG, "IMU ok, accel = %.2f %.2f %.2f g (QMI8658 axes), scale %.3f (%s), raw %.3f g, axes calibration %s",
             a[0], a[1], a[2], (double)s_ascale, s_ascale_auto ? "auto" : "manual", (double)s_raw_g,
             s_acal ? "on" : "off");
    return true;
}

bool imu_ok(void)
{
    return s_ok;
}

// A short wooden tick and a click of noise; a harder stop is louder.
static void play_tick(float g)
{
    float k = (g - TICK_ON_G) / 2.5f;
    if (k > 1) {
        k = 1;
    }
    int vol = 30 + (int)(k * 45);
    audio_voice_t t = {
            .dur = 16 * AUDIO_SAMPLE_RATE / 1000,
            .f0 = 1500,
            .f1 = 800,
            .wave = W_TRI,
            .vol = (uint8_t)vol,
            .attack_ms = 0,
            .shape = AUDIO_PERC,
    };
    audio_play(&t);
    audio_voice_t n = {
            .dur = 7 * AUDIO_SAMPLE_RATE / 1000,
            .f0 = 9000,
            .f1 = 6000,
            .wave = W_NOISE,
            .vol = (uint8_t)(vol * 2 / 3),
            .attack_ms = 0,
            .shape = AUDIO_PERC,
            .hp = 160,
    };
    audio_play(&n);
}

static void shake_tick_step(float px, float py, float pz, float m, int64_t now)
{
    if (!s_tick_on) {
        s_tick_win = 0;
        s_tick_armed = true;
        return;
    }
    if (s_tick_win > 0) {
        if (m > s_tick_peak) {
            s_tick_peak = m;
            s_tick_dir[0] = px / m;
            s_tick_dir[1] = py / m;
            s_tick_dir[2] = pz / m;
        }
        // the peak: the level falls again, or the window ends
        if (--s_tick_win == 0 || m < s_tick_peak * 0.8f) {
            s_tick_win = 0;
            play_tick(s_tick_peak);
            s_tick_last = now;
            memcpy(s_tick_last_dir, s_tick_dir, sizeof(s_tick_dir));
        }
        return;
    }
    if (!s_tick_armed) {
        if (m < TICK_OFF_G) {
            s_tick_armed = true;
        }
        return;
    }
    if (m > TICK_ON_G && now - s_tick_last > TICK_GAP_US) {
        float d = (px * s_tick_last_dir[0] + py * s_tick_last_dir[1] + pz * s_tick_last_dir[2]) / m;
        if (d < -0.2f || now - s_tick_last > TICK_SAME_US) {
            s_tick_armed = false;
            s_tick_win = TICK_WIN;
            s_tick_peak = m;
            s_tick_dir[0] = px / m;
            s_tick_dir[1] = py / m;
            s_tick_dir[2] = pz / m;
        }
    }
}

void imu_shake_ticks(bool on)
{
    s_tick_on = on;
}

// one step of the speed (see VEL_TAU): a = the accelerometer (g), w = the gyro (rad/s), both
// in the sensor frame; mag = |a|
static void vel_step(const float a[3], const float w[3], float mag, float dt)
{
    float *g = s_vg;
    float wc[3] = {w[0] - s_vbias[0], w[1] - s_vbias[1], w[2] - s_vbias[2]};
    // turn g by -wc*dt (second-order Rodrigues form)
    float px = -wc[0] * dt, py = -wc[1] * dt, pz = -wc[2] * dt;
    float th2 = px * px + py * py + pz * pz;
    float ka = 1.0f - th2 / 6.0f, kb = 0.5f - th2 / 24.0f;
    float c1x = py * g[2] - pz * g[1], c1y = pz * g[0] - px * g[2], c1z = px * g[1] - py * g[0];
    float c2x = py * c1z - pz * c1y, c2y = pz * c1x - px * c1z, c2z = px * c1y - py * c1x;
    g[0] += ka * c1x + kb * c2x;
    g[1] += ka * c1y + kb * c2y;
    g[2] += ka * c1z + kb * c2z;
    // the correction toward the accelerometer whenever nothing pushes the board (the main filter,
    // which corrects each 0.3 s, sees no linear acceleration): slow tilts of the hand and the gyro
    // bias go away in about VEL_GTAU; in a push or a stop it waits, and the move stays whole
    float wn = sqrtf(wc[0] * wc[0] + wc[1] * wc[1] + wc[2] * wc[2]);
    float lm = sqrtf(s_lin[0] * s_lin[0] + s_lin[1] * s_lin[1] + s_lin[2] * s_lin[2]);
    if (lm < 0.035f && wn < 1.5f) {
        float k2 = dt / VEL_GTAU;
        for (int i = 0; i < 3; i++) {
            g[i] += k2 * (a[i] - g[i]);
        }
    }
    float p[3] = {a[0] - g[0], a[1] - g[1], a[2] - g[2]};
    // rest: the raw acceleration stays steady near 1 g (this test does not use g, so it also
    // works while the bias is still wrong); then the bias of this filter is learned (1 s)
    float da[3];
    for (int i = 0; i < 3; i++) {
        s_valp[i] += (a[i] - s_valp[i]) * dt / 0.1f;
        da[i] = a[i] - s_valp[i];
    }
    float dm = sqrtf(da[0] * da[0] + da[1] * da[1] + da[2] * da[2]);
    s_vrest_pk *= 1.0f - dt / 0.1f;
    if (dm > s_vrest_pk) {
        s_vrest_pk = dm;
    }
    if (s_vrest_pk < VEL_REST_G && fabsf(mag - 1.0f) < VEL_REST_G && wn < 0.3f) {
        s_vrest_ms += dt * 1000;
    } else {
        s_vrest_ms = 0;
    }
    if (s_vrest_ms >= VEL_REST_MS) {
        for (int i = 0; i < 3; i++) {
            s_vbias[i] += (w[i] - s_vbias[i]) * dt / 1.0f;
        }
    }
    float vleak = 1.0f - dt / (s_vrest_ms >= VEL_REST_MS ? 0.06f : VEL_TAU);
    if (vleak < 0) {
        vleak = 0;
    }
    for (int i = 0; i < 3; i++) {
        s_vel3[i] = s_vel3[i] * vleak + p[i] * 9.81f * dt;
    }
    for (int i = 0; i < 3; i++) {
        s_lin3[i] += (p[i] - s_lin3[i]) * (dt / 0.02f > 1 ? 1 : dt / 0.02f);
    }
}

void imu_linacc_phys(float a[3])
{
    // the same map as the velocity; m/s2
    a[0] = -s_lin3[1] * 9.81f;
    a[1] = -s_lin3[0] * 9.81f;
    a[2] = -s_lin3[2] * 9.81f;
}

void imu_velocity_phys(float v[3])
{
    // the sensor axes to the physical frame (the map of the linear acceleration)
    v[0] = -s_vel3[1];
    v[1] = -s_vel3[0];
    v[2] = -s_vel3[2];
}

static volatile bool s_slow_req;  // imu_slow()
static bool s_slow;               // the task: 25 Hz, the gyro numbers not used

// The sensor itself does not change: a change of its mode (the gyro off and on again) gives bad readings for more
// than 0.1 s, a false shake that wakes the screen at each screen off and on. Only the rate changes.
void imu_slow(bool slow)
{
    s_slow_req = slow;
}

// imu_step: the dial and the turn of the gyro (the bias follows slowly while the board is still)
static void dial_step(const float w[3], float m, float dt, bool nogyro)
{
    // dial: gyro z in the physical frame (the same axis map as the force), + = clockwise
    // for the user; the bias follows slowly while the board is still
    if (nogyro) {
        s_turn_rate = 0;  // (no gyro now: the dial and the bias stay)
    } else {
        float wz = -w[2];
        if (m < 0.03f && fabsf(wz - s_bias_z) < 0.02f) {
            s_bias_z += (wz - s_bias_z) * (dt / 5.0f);
        }
        s_turn_rate = wz - s_bias_z;
        s_dial += s_turn_rate * dt;
        float wp[3] = {-w[1], -w[0], -w[2]};
        for (int i = 0; i < 3; i++) {
            if (m < 0.03f && fabsf(wp[i] - s_gbias[i]) < 0.02f) {
                s_gbias[i] += (wp[i] - s_gbias[i]) * (dt / 5.0f);
            }
            s_gsum[i] += (wp[i] - s_gbias[i]) * dt;
        }
    }
}

// imu_step: the step count of a walk
static void walk_step(const float *f, float px, float py, float pz, int64_t now)
{
    // steps: the vertical acceleration (up = +) rises above STEP_UP_G, then falls below
    // STEP_DOWN_G within STEP_WIN_US
    float gx = f[1], gy = f[0], gz = f[2];  // gravity, physical frame (as imu_gravity_phys)
    float gn = sqrtf(gx * gx + gy * gy + gz * gz);
    if (gn > 0.5f) {
        float v = -(px * gx + py * gy + pz * gz) / gn;
        s_vert += 0.3f * (v - s_vert);
        if (s_vert > STEP_UP_G) {
            s_step_up_at = now;
        } else if (s_vert < STEP_DOWN_G && s_step_up_at && now - s_step_up_at < STEP_WIN_US &&
                   now - s_step_last > STEP_GAP_US) {
            s_steps++;
            s_step_last = now;
            s_step_up_at = 0;
        }
    }
}

// imu_step: the snap of the board (a fast move, then a hard stop)
static void snap_step(float px, float py, float dt, int64_t now)
{
    // snap: the top speed and its direction, then a hard stop against it
    float leak = 1.0f - dt / 0.3f;
    s_vel[0] = s_vel[0] * leak + px * 9.81f * dt;
    s_vel[1] = s_vel[1] * leak + py * 9.81f * dt;
    float sp = sqrtf(s_vel[0] * s_vel[0] + s_vel[1] * s_vel[1]);
    s_vpeak *= 1.0f - dt / 0.15f;
    if (sp > s_vpeak) {
        s_vpeak = sp;
        s_vdir[0] = s_vel[0] / sp;
        s_vdir[1] = s_vel[1] / sp;
    }
    float along = px * s_vdir[0] + py * s_vdir[1];
    if (s_vpeak > SNAP_V && along < -SNAP_STOP_G && now - s_snap_last > SNAP_GAP_US) {
        int w = s_snap_w;
        s_snaps[w] = (imu_snap_t){s_vdir[0], s_vdir[1], s_vpeak, now};
        s_snap_w = (w + 1) % N_SNAPS;
        if (s_snap_w == s_snap_r) {
            s_snap_r = (s_snap_r + 1) % N_SNAPS;
        }
        s_snap_last = now;
        s_vpeak = 0;
        s_vel[0] = s_vel[1] = 0;
    }
}

// imu_step: the hit of the board (a rising edge above HIT_G, then the peak)
static void hit_step(float px, float py, float pz, float m, int64_t now)
{
    // hit: a rising edge above HIT_G, then the peak of the next 40 ms
    if (s_hit_win > 0) {
        if (m > s_hit_g) {
            s_hit_g = m;
            s_hit_peak[0] = px;
            s_hit_peak[1] = py;
            s_hit_peak[2] = pz;
        }
        if (--s_hit_win == 0) {
            int w = s_hit_w;
            s_hits[w] = (imu_hit_t){s_hit_peak[0], s_hit_peak[1], s_hit_peak[2], s_hit_g, now};
            s_hit_w = (w + 1) % N_HITS;
            if (s_hit_w == s_hit_r) {
                s_hit_r = (s_hit_r + 1) % N_HITS;  // full: drop the oldest
            }
        }
    } else if (m > HIT_G && now - s_hit_last > HIT_GAP_US) {
        s_hit_last = now;
        s_hit_win = HIT_WIN;
        s_hit_g = m;
        s_hit_peak[0] = px;
        s_hit_peak[1] = py;
        s_hit_peak[2] = pz;
    }
}

static void imu_step(void)
{
    float a[3], w[3];
    if (!read_raw(a, w)) {
        return;
    }
    acal_apply(a, w);
    ascale_apply(a, w);
    // the screen off: a reading far below 1 g is a fault of the sensor (a still board cannot read it), not a shake
    if (s_slow && a[0] * a[0] + a[1] * a[1] + a[2] * a[2] < 0.09f) {
        s_last_us = esp_timer_get_time();
        return;
    }
    bool nogyro = s_slow;
    if (nogyro) {
        w[0] = w[1] = w[2] = 0;
    }
    int64_t now = esp_timer_get_time();
    float dt = (now - s_last_us) / 1e6f;
    s_last_us = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }

    float *f = s_f;
#if CONFIG_TFN_IMU_USE_GYRO
    // A vector fixed in the world turns as df/dt = -w x f in the sensor frame.
    float c0 = w[1] * f[2] - w[2] * f[1];
    float c1 = w[2] * f[0] - w[0] * f[2];
    float c2 = w[0] * f[1] - w[1] * f[0];
    f[0] -= dt * c0;
    f[1] -= dt * c1;
    f[2] -= dt * c2;
    float k = 3.0f * dt;  // correction toward the accelerometer, about 0.3 s
#else
    float k = 15.0f * dt;  // plain low-pass filter
#endif
    // Trust the accelerometer less while the board shakes.
    float mag = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (fabsf(mag - 1.0f) > 0.25f) {
        k *= 0.2f;
    }
    if (k > 1.0f) {
        k = 1.0f;
    }
#if BOARD_IMU_WHEEL
    k = 1.0f;  // the virtual tilt has no noise: no filter
#endif
    for (int i = 0; i < 3; i++) {
        f[i] += k * (a[i] - f[i]);
    }

    // linear acceleration = reading - gravity part; map sensor -> physical axes
    float l0 = a[0] - f[0], l1 = a[1] - f[1], l2 = a[2] - f[2];
    float px = -l1, py = -l0, pz = -l2;
    s_lin[0] += 0.5f * (px - s_lin[0]);
    s_lin[1] += 0.5f * (py - s_lin[1]);
    s_lin[2] += 0.5f * (pz - s_lin[2]);
    float m = sqrtf(px * px + py * py + pz * pz);
    s_shake *= expf(-dt / 0.25f);  // falls to a third in 0.25 s
    if (m > s_shake) {
        s_shake = m;
    }

    dial_step(w, m, dt, nogyro);

    walk_step(f, px, py, pz, now);

    snap_step(px, py, dt, now);

    shake_tick_step(px, py, pz, m, now);
    vel_step(a, w, mag, dt);

    hit_step(px, py, pz, m, now);

#if CONFIG_TFN_IMU_DEBUG
    static int64_t last_log;
    if (now - last_log > 500000) {
        last_log = now;
        float gx, gy, gz, tx, ty;
        imu_gravity_phys(&gx, &gy, &gz);
        imu_tilt_logical(&tx, &ty);
        ESP_LOGI(TAG, "acc %.2f %.2f %.2f  grav(phys) %.2f %.2f %.2f  tilt(logical) %.2f %.2f", a[0], a[1], a[2], gx,
                 gy, gz, tx, ty);
    }
#endif
}

static void imu_task(void *arg)
{
    TickType_t wake = xTaskGetTickCount(), per = pdMS_TO_TICKS(1000 / RATE_HZ);
#if BOARD_IMU_WHEEL
    if (per < 1) {
        per = 1;  // the virtual sensor does not wait for I2C as a real one does
    }
#endif
    for (;;) {
        bool slow = s_slow_req;
        if (slow != s_slow) {
            s_slow = slow;
            wake = xTaskGetTickCount();
        }
        vTaskDelayUntil(&wake, s_slow ? pdMS_TO_TICKS(power_x() & PWX_IMU ? 1000 : 40) : per);
        imu_step();
    }
}

void imu_linear_logical(float *ax, float *ay, float *az)
{
    float px = s_lin[0], py = s_lin[1];
    switch (gfx_orient()) {
    case ORIENT_PORTRAIT:
        *ax = px;
        *ay = py;
        break;
    case ORIENT_PORTRAIT_FLIP:
        *ax = -px;
        *ay = -py;
        break;
    case ORIENT_LAND_A:
        *ax = py;
        *ay = -px;
        break;
    case ORIENT_LAND_B:
        *ax = -py;
        *ay = px;
        break;
    }
    *az = s_lin[2];
}

float imu_shake(void)
{
    return s_shake;
}

static void phys_to_logical(float px, float py, float *lx, float *ly);

bool imu_pop_hit(imu_hit_t *h)
{
    if (s_hit_r == s_hit_w) {
        return false;
    }
    *h = s_hits[s_hit_r];
    s_hit_r = (s_hit_r + 1) % N_HITS;
    float lx, ly;
    phys_to_logical(h->x, h->y, &lx, &ly);
    h->x = lx;
    h->y = ly;
    return true;
}

void imu_hits_clear(void)
{
    s_hit_r = s_hit_w;
}

bool imu_pop_snap(imu_snap_t *s)
{
    if (s_snap_r == s_snap_w) {
        return false;
    }
    *s = s_snaps[s_snap_r];
    s_snap_r = (s_snap_r + 1) % N_SNAPS;
    float lx, ly;
    phys_to_logical(s->x, s->y, &lx, &ly);
    s->x = lx;
    s->y = ly;
    return true;
}

void imu_snaps_clear(void)
{
    s_snap_r = s_snap_w;
}

float imu_dial(void)
{
    return s_dial;
}

void imu_gyro_sum(float a[3])
{
    a[0] = s_gsum[0];
    a[1] = s_gsum[1];
    a[2] = s_gsum[2];
}

float imu_turn_rate(void)
{
    return s_turn_rate;
}

uint32_t imu_steps(void)
{
    return s_steps;
}

void imu_gravity_phys(float *gx, float *gy, float *gz)
{
#if BOARD_IMU_WHEEL
    // the virtual sensor has no filter: read the wheel now (no wait for the IMU task, so a new level is true at once)
    float f[3];
    wheel_acc(board_wheel_pos(), f);
#else
    const float *f = s_f;
#endif
    // gravity points opposite to the specific force at rest
    float g0 = -f[0], g1 = -f[1], g2 = -f[2];
    *gx = -g1;
    *gy = -g0;
    *gz = -g2;  // +z = into the screen (x right, y down, z in: right-handed)
}

static void phys_to_logical(float px, float py, float *lx, float *ly)
{
    switch (gfx_orient()) {
    case ORIENT_PORTRAIT:
        *lx = px;
        *ly = py;
        break;
    case ORIENT_PORTRAIT_FLIP:
        *lx = -px;
        *ly = -py;
        break;
    case ORIENT_LAND_A:
        *lx = py;
        *ly = -px;
        break;
    default:
        *lx = -py;
        *ly = px;
        break;
    }
}

void imu_gravity_logical(float *gx, float *gy)
{
    float px, py, pz;
    imu_gravity_phys(&px, &py, &pz);
    phys_to_logical(px, py, gx, gy);
}

void imu_wheel_shift(int steps)
{
#if BOARD_IMU_WHEEL
    s_wheel_org += steps;
#else
    (void)steps;
#endif
}

void imu_set_level(void)
{
#if BOARD_IMU_WHEEL
    s_wheel_org = board_wheel_pos();  // the wheel position now is level (the gravity below is flat: no turn)
#endif
    // rotation that turns the current gravity direction onto +z (board flat, screen up)
    float a[3];
    imu_gravity_phys(&a[0], &a[1], &a[2]);
    float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (n < 0.3f) {
        return;
    }
    a[0] /= n;
    a[1] /= n;
    a[2] /= n;
    // v = a x z, c = a . z
    float vx = a[1], vy = -a[0], c = a[2];
    if (c < -0.95f) {
        // screen faces the floor: use a half turn around x
        float r[3][3] = {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
        for (int i = 0; i < 3; i++) {
            for (int j = 0; j < 3; j++) {
                s_rot[i][j] = r[i][j];
            }
        }
        return;
    }
    // Rodrigues: R = I + [v]x + [v]x^2 / (1 + c), with vz = 0
    float k = 1.0f / (1.0f + c);
    s_rot[0][0] = 1 - k * vy * vy;
    s_rot[0][1] = k * vx * vy;
    s_rot[0][2] = vy;
    s_rot[1][0] = k * vx * vy;
    s_rot[1][1] = 1 - k * vx * vx;
    s_rot[1][2] = -vx;
    s_rot[2][0] = -vy;
    s_rot[2][1] = vx;
    s_rot[2][2] = 1 - k * (vx * vx + vy * vy);
}

void imu_tilt_logical(float *tx, float *ty)
{
#if BOARD_IMU_WHEEL
    // An app that reads the wheel itself (app_t.wheel: the menu, WRONG ROOM, TEAM BUILDING, JUNGLE RUN, ...) gets no
    // tilt from the wheel: else each step moves it twice, and the tilt stays after the wheel stops (the hero walks on).
    // (The gravity stays: main.c sees the turns as activity.)
    const app_t *app = app_current();
    if (app && app->wheel) {
        *tx = *ty = 0;
        return;
    }
#endif
    float g[3], r[3];
    imu_gravity_phys(&g[0], &g[1], &g[2]);
    for (int i = 0; i < 3; i++) {
        r[i] = s_rot[i][0] * g[0] + s_rot[i][1] * g[1] + s_rot[i][2] * g[2];
    }
    phys_to_logical(r[0], r[1], tx, ty);
}

orient_t imu_upright_any(orient_t current, float min_g)
{
    if (!s_ok) {
        return current;
    }
    float px, py, pz;
    imu_gravity_phys(&px, &py, &pz);
    bool land = orient_is_landscape(current);
    // physical x is the short side of the portrait frame: a landscape board has gravity on x
    if (land && fabsf(py) > fabsf(px) + 0.25f && fabsf(py) > min_g) {
        land = false;
    } else if (!land && fabsf(px) > fabsf(py) + 0.25f && fabsf(px) > min_g) {
        land = true;
    }
    return imu_upright(land, current, min_g);
}

orient_t imu_upright(bool landscape, orient_t current, float min_g)
{
    orient_t def = landscape ? ORIENT_LAND_A : ORIENT_PORTRAIT;
    if (orient_is_landscape(current) != landscape) {
        current = def;
    }
    if (!s_ok) {
        return current;
    }
    float px, py, pz;
    imu_gravity_phys(&px, &py, &pz);
    if (landscape) {
        if (px < -min_g) {
            return ORIENT_LAND_A;
        }
        if (px > min_g) {
            return ORIENT_LAND_B;
        }
    } else {
        if (py > min_g) {
            return ORIENT_PORTRAIT;
        }
        if (py < -min_g) {
            return ORIENT_PORTRAIT_FLIP;
        }
    }
    return current;
}
