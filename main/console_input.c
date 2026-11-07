// The console commands of the input: the touch, the keys and the keyboard, the motion sensor, the gamepad (the table: console.c).
#include "console_priv.h"

static const char *TAG = "console";

// "pad"
void con_pad(char *cmd)
{
    char *a = strtok(NULL, " \t\r\n");
    bt_pad_start();
    if (a && !strcmp(a, "forget")) {
        bt_pad_forget();
        ESP_LOGI(TAG, "gamepad: all pairings forgotten");
    }
    ESP_LOGI(TAG, "gamepad: %s, state %d", bt_pad_name(), (int)bt_pad_state());
}

// "padtest"
void con_padtest(char *cmd)
{
    // a known pattern for a test on the computer: each button, each hat
    // direction, then the sticks from one end to the other (about 9 s)
    bt_pad_start();
    pad_report_t r = {.hat = PAD_HAT_CENTER};
    for (int b = 0; b < 16; b++) {
        r.buttons = (uint16_t)(1u << b);
        bt_pad_send(&r);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    r.buttons = 0;
    for (int h = 0; h <= 8; h++) {
        r.hat = (uint8_t)h;
        bt_pad_send(&r);
        vTaskDelay(pdMS_TO_TICKS(250));
    }
    for (int v = -127; v <= 127; v += 16) {
        r.x = r.y = r.z = r.rz = (int8_t)v;
        bt_pad_send(&r);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    pad_report_t zero = {.hat = PAD_HAT_CENTER};
    bt_pad_send(&zero);
    ESP_LOGI(TAG, "padtest done: state %d, %lu reports", (int)bt_pad_state(), (unsigned long)bt_pad_sent());
}

// "padcal"
void con_padcal(char *cmd)
{
    // GAMEPAD: start the calibration (the app must be open)
    extern volatile int g_pad_cal_req;
    g_pad_cal_req = 1;
    ESP_LOGI(TAG, "padcal: asked");
}

// "padlog"
void con_padlog(char *cmd)
{
    // GAMEPAD: the live tilt, speed and direction, 10 lines a second, for N s
    extern volatile int64_t g_pad_log_until;
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 10;
    g_pad_log_until = esp_timer_get_time() / 1000 + (int64_t)secs * 1000;
    ESP_LOGI(TAG, "padlog %d s", secs);
}

// "finger", "fmove"
void con_finger(char *cmd)
{
    // tests: a virtual finger at logical x y: "finger x y ms" (down for ms), "fmove x y" (the finger
    // that is down goes there), "finger up"
    static int64_t until;
    char *a = strtok(NULL, " \t\r\n"), *b = strtok(NULL, " \t\r\n"), *c = strtok(NULL, " \t\r\n");
    int64_t t = esp_timer_get_time() / 1000;
    if (a && !strcmp(a, "up")) {
        until = 0;
        input_virtual_finger(0, 0, 0);
    } else if (a && b) {
        int px, py;
        gfx_logical_to_touch(atoi(a), atoi(b), &px, &py);
        if (cmd[1] == 'i') {
            until = t + (c ? atoi(c) : 100);
        }
        input_virtual_finger(px, py, until);
    }
}

// "key"
void con_key(char *cmd)
{
    // tests: the BOOT key of the 3.49 down for ms ("key 80"; main.c: a press for an app with app_t.key, else a
    // hold of 700 ms opens CHAT)
    extern int64_t g_vkey_until_ms;
    char *a = strtok(NULL, " \t\r\n");
    g_vkey_until_ms = esp_timer_get_time() / 1000 + (a ? atoi(a) : 80);
    ESP_LOGI(TAG, "key down for %d ms", a ? atoi(a) : 80);
}

// "finger2", "fmove2"
void con_finger2(char *cmd)
{
    // tests: a second virtual finger, as "finger" (a test of two thumbs)
    static int64_t until2;
    char *a = strtok(NULL, " \t\r\n"), *b = strtok(NULL, " \t\r\n"), *c = strtok(NULL, " \t\r\n");
    int64_t t = esp_timer_get_time() / 1000;
    if (a && !strcmp(a, "up")) {
        until2 = 0;
        input_virtual_finger2(0, 0, 0);
    } else if (a && b) {
        int px, py;
        gfx_logical_to_touch(atoi(a), atoi(b), &px, &py);
        if (cmd[1] == 'i') {
            until2 = t + (c ? atoi(c) : 100);
        }
        input_virtual_finger2(px, py, until2);
    }
}

// "kbd": tests: keys as from a keyboard: "kbd text{UP}{ENTER}" ({NAME}, ^x = Ctrl+x)
void con_kbd(char *cmd)
{
    static const struct {
        const char *n;
        int k;
    } KN[] = {{"UP", INK_UP},   {"DOWN", INK_DOWN}, {"LEFT", INK_LEFT}, {"RIGHT", INK_RIGHT}, {"ENTER", INK_ENTER},
              {"ESC", INK_ESC}, {"BKSP", INK_BKSP}, {"TAB", INK_TAB},   {"DEL", INK_DEL},     {"HOME", INK_HOME},
              {"END", INK_END}, {"PGUP", INK_PGUP}, {"PGDN", INK_PGDN}, {"SPACE", ' '}};
    const char *p = strtok(NULL, "");
    int n = 0;
    while (p && *p) {
        int key = 0, mods = 0;
        if (*p == '^' && p[1]) {
            mods = INM_CTRL;
            p++;
        }
        if (*p == '{') {
            const char *e = strchr(p, '}');
            if (!e) {
                break;
            }
            for (size_t i = 0; i < sizeof KN / sizeof KN[0]; i++) {
                if ((size_t)(e - p - 1) == strlen(KN[i].n) && !strncmp(p + 1, KN[i].n, (size_t)(e - p - 1))) {
                    key = KN[i].k;
                }
            }
            p = e + 1;
        } else {
            key = (unsigned char)*p++;
            if (key == '\r' || key == '\n') {
                continue;
            }
        }
        if (key) {
            input_key_push(key, mods);
            n++;
        }
    }
    ESP_LOGI(TAG, "kbd: %d keys", n);
}

// "imu"
void con_imu(char *cmd)
{
    // the gravity direction, for a check of the IMU axes: "imu 10" = 5 lines a second for 10 s
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 0;
    if (secs > 60) {
        secs = 60;
    }
    for (int i = 0; i <= secs * 5; i++) {
        float px, py, pz, lx, ly;
        imu_gravity_phys(&px, &py, &pz);
        imu_gravity_logical(&lx, &ly);
        ESP_LOGI(TAG,
                 "imu %s: gravity physical %+.2f %+.2f %+.2f, screen now (x right, y down) %+.2f %+.2f, shake "
                 "%.2f, dial %+.0f deg, steps %lu",
                 imu_ok() ? "on" : "none", px, py, pz, lx, ly, imu_shake(), imu_dial() * 57.29578f,
                 (unsigned long)imu_steps());
        if (secs) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

// "imuraw"
void con_imuraw(char *cmd)
{
    // the IMU in motion, for a check of the axes and the filter: "imuraw 20" = 10 lines a second for 20 s: the
    // sensor (the QMI8658 axes, before the calibration), the gyro (rad/s), the gravity (physical), the game tilt
    // (logical, from the level), the screen turn
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 0;
    if (secs > 60) {
        secs = 60;
    }
    float off[3], gain[3];
    imu_accel_cal_get(off, gain);
    ESP_LOGI(TAG, "imuraw: calibration %s, off %+.3f %+.3f %+.3f, gain %.3f %.3f %.3f",
             imu_accel_cal_on() ? "on" : "off", off[0], off[1], off[2], gain[0], gain[1], gain[2]);
    for (int i = 0; i <= secs * 10; i++) {
        float ra[3], rw[3], g[3], tx, ty;
        imu_raw(ra, rw);
        imu_gravity_phys(&g[0], &g[1], &g[2]);
        imu_tilt_logical(&tx, &ty);
        ESP_LOGI(TAG, "imuraw a %+.2f %+.2f %+.2f w %+.2f %+.2f %+.2f g %+.2f %+.2f %+.2f tilt %+.2f %+.2f o %d", ra[0],
                 ra[1], ra[2], rw[0], rw[1], rw[2], g[0], g[1], g[2], tx, ty, (int)gfx_orient());
        if (secs) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

// "orient"
void con_orient(char *cmd)
{
    // tests of the IMU axes: "orient 0..3" = this screen turn (orient_t) and no auto turn until a restart (the
    // setting in flash does not change); "orient" = show the turn now
    char *a = strtok(NULL, " \t\r\n");
    if (a) {
        g_settings.auto_rotate = false;
        gfx_set_orient((orient_t)(atoi(a) & 3));
    }
    ESP_LOGI(TAG, "orient %d, auto turn %s", (int)gfx_orient(), g_settings.auto_rotate ? "on" : "off");
}

// "touchw"
void con_touchw(char *cmd)
{
    // the touch points with the weight and the area of the chip report, for N seconds (the 3.49 driver)
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 10;
    g_touch_log_until_ms = esp_timer_get_time() / 1000 + (secs > 60 ? 60 : secs) * 1000;
    ESP_LOGI(TAG, "touchw: %d s", secs);
}

// "tpdiag"
void con_tpdiag(char *cmd)
{
    // the AMOLED touch chip: the good and the failed reads each second for N s
    char *a = strtok(NULL, " \t\r\n");
    board_tp_diag(a ? atoi(a) : 10);
}

// "tpreg", "tpset"
void con_tpreg(char *cmd)
{
    // one register of the AMOLED touch chip: read (tpreg R) or write (tpset R V); hex or decimal
    const bool set = cmd[2] == 's';
    char *a = strtok(NULL, " \t\r\n"), *b = strtok(NULL, " \t\r\n");
    if (a) {
        const int reg = (int)strtol(a, NULL, 0), val = set && b ? (int)strtol(b, NULL, 0) : -1;
        const int r = board_tp_reg(reg, val);
        ESP_LOGI(TAG, "tp reg 0x%02X %s: %d (0x%02X)", reg, val >= 0 ? "write" : "read", r, r & 0xFF);
    }
}

// "touchread"
void con_touchread(char *cmd)
{
    // the length of a touch read (the 3.49: 32 = up to 5 points, 14 = 2 points); with no number: the length now
    char *a = strtok(NULL, " \t\r\n");
    if (a) {
        board_touch_read_len(atoi(a));
    }
    ESP_LOGI(TAG, "touchread: %d", board_touch_read_len_get());
}

// "touch2"
void con_touch2(char *cmd)
{
    // each touch report for N seconds (the 3.49 driver): the count, and x, y, the id and the event of each point
    // ("t2 <ms> <n> x,y,id,ev ...")
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 10;
    g_touch2_until_ms = esp_timer_get_time() / 1000 + (secs > 120 ? 120 : secs) * 1000;
    ESP_LOGI(TAG, "touch2: %d s", secs);
}

// "touchraw"
void con_touchraw(char *cmd)
{
    // the raw touch for N seconds (a sample each 5 ms): how often the panel loses a finger that stays down (the
    // gaps), and how far the point moves under a still finger (the jitter); a line each second
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 10;
    if (secs > 60) {
        secs = 60;
    }
    int64_t t0 = esp_timer_get_time() / 1000, last_pt = 0, gap_start = 0;
    int on = 0, all = 0, gaps = 0, gmax = 0, g20 = 0, g50 = 0, g100 = 0, sx = -1, sy = -1, spread = 0, downs = 0;
    bool was = false;
    while (esp_timer_get_time() / 1000 - t0 < secs * 1000) {
        int16_t x[INPUT_MAX_POINTS], y[INPUT_MAX_POINTS];
        int n = input_points_raw(x, y, INPUT_MAX_POINTS);
        int64_t t = esp_timer_get_time() / 1000;
        all++;
        if (n > 0) {
            on++;
            if (!was && gap_start && t - gap_start < 300) {
                int g = (int)(t - gap_start);  // the finger came back soon: a gap (or a quick second tap)
                gaps++;
                if (g > gmax) {
                    gmax = g;
                }
                if (g >= 100) {
                    g100++;
                } else if (g >= 50) {
                    g50++;
                } else if (g >= 20) {
                    g20++;
                }
            } else if (!was) {
                downs++;
                sx = x[0];
                sy = y[0];
            }
            int d = abs(x[0] - sx) > abs(y[0] - sy) ? abs(x[0] - sx) : abs(y[0] - sy);
            if (sx >= 0 && d > spread) {
                spread = d;
            }
            last_pt = t;
            was = true;
        } else if (was) {
            was = false;
            gap_start = last_pt;
        }
        if (all % 200 == 0) {
            ESP_LOGI(TAG,
                     "touchraw: finger %d%% of the time, %d touches, %d gaps (%d of 20-49 ms, %d of 50-99, %d of "
                     "100+; the longest %d ms), the point moved %d px",
                     on * 100 / all, downs, gaps, g20, g50, g100, gmax, spread);
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    ESP_LOGI(TAG, "touchraw: end: %d touches, %d gaps (the longest %d ms), the point moved %d px", downs, gaps, gmax,
             spread);
}

// "touches"
void con_touches(char *cmd)
{
    // the touch points for N seconds (10 lines a second), for a check of the multi-touch
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 5;
    if (secs > 60) {
        secs = 60;
    }
    int most = 0;
    for (int i = 0; i < secs * 10; i++) {
        int16_t x[INPUT_MAX_POINTS], y[INPUT_MAX_POINTS];
        int n = input_points(x, y, INPUT_MAX_POINTS);
        if (n > most) {
            most = n;
        }
        if (n) {
            ESP_LOGI(TAG, "touches %d: (%d,%d) (%d,%d) (%d,%d)", n, x[0], y[0], n > 1 ? x[1] : -1, n > 1 ? y[1] : -1,
                     n > 2 ? x[2] : -1, n > 2 ? y[2] : -1);
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "touches: %d points at most", most);
}

// "tap", "ltap"
void con_tap(char *cmd)
{
    // a tap at physical x y, or (ltap) at logical x y in the orientation now (tests from the PC)
    bool logical = cmd[0] == 'l';
    char *a = strtok(NULL, " \t\r\n"), *b = strtok(NULL, " \t\r\n");
    static char pa[12], pb[12];
    if (a && b && logical) {
        int lx = atoi(a), ly = atoi(b), px, py;
        gfx_logical_to_touch(lx, ly, &px, &py);
        snprintf(pa, sizeof(pa), "%d", px);
        snprintf(pb, sizeof(pb), "%d", py);
        a = pa;
        b = pb;
    }
    if (a && b) {
        int64_t t = esp_timer_get_time() / 1000;
        input_event_t d = {.kind = IN_DOWN, .x = (int16_t)atoi(a), .y = (int16_t)atoi(b), .t_ms = t};
        input_event_t u = d, tp = d;
        u.kind = IN_UP;
        tp.kind = IN_TAP;
        u.t_ms = tp.t_ms = t + 50;
        input_inject(&d);
        input_inject(&u);
        input_inject(&tp);
    }
}

#if BOARD_WHEEL
// "wheel"
void con_wheel(char *cmd)
{
    // the wheel: the raw count and the steps, for N seconds (a line when they change)
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 10, last = 1 << 30;
    if (secs > 60) {
        secs = 60;
    }
    for (int i = 0; i < secs * 20; i++) {
        int raw = board_wheel_raw();
        if (raw != last) {
            ESP_LOGI(TAG, "wheel raw %d, steps %d, button %d", raw, board_wheel_pos(), (int)board_pwr_key_down());
            last = raw;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGI(TAG, "wheel: end");
}
#endif
