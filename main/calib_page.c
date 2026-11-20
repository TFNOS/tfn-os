// SETUP > TOUCH TEST and SETUP > TILT CALIBRATE (for a board with a bad accelerometer or a touch
// panel that does not work well).
//
// TOUCH TEST: a red ring where the panel reports a finger (with no calibration), a green cross where the board
// uses it (with the calibration). CALIBRATE: tap the centre of 4 targets near the corners, then a check target in
// the middle. The fit is an affine map of the panel points (offset, scale, a turn, a mirror or swapped axes;
// input.c applies it). It is saved only when the fit and the check are good. RESET: no calibration.
//
// TILT CALIBRATE: put the board on each of its 6 sides (screen up, screen down, each long edge, each short end)
// and keep it still for 1 s: a click for each new side. After the 6 sides each axis gets an offset and a gain
// (a = (raw - offset) * gain; imu.c applies it) and the scale goes to AUTO. RESET: no calibration.
//
// THE THUMB: RESET erases the calibration only on a SECOND tap within 3 s (the first tap shows "TAP AGAIN");
// the buttons are 44 px high (48 on the 320 x 240 frame; the Watcher shows it at 1:1).
//
// Each touch and each capture is in the log ("calib: ...").

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "uikit.h"
#include "esp_log.h"
#include "audio.h"
#include "calib_page.h"
#include "gfx.h"
#include "imu.h"
#include "input.h"
#include "tfn/theme.h"
#include "set_round.h"

static const char *TAG = "calib";

// THE ROUND FRAME (gfx_round()): the title and the state on the top cap, the help, the sides and the buttons in the
// middle box, BACK on the bottom cap; the 5 targets inside the circle.
#if BOARD_ROUND
#define CB_RND gfx_round()
#else
#define CB_RND false
#endif

enum {
    K_TOUCH,
    K_TILT
};
static int s_kind;

// the layout (the 3.49: 640 x 172; the CoreS3: 320 x 240)
#define BACK_W 56
static int btn_w(void)
{
    return gfx_w > 400 ? 130 : 100;
}
static int btn_h(void)
{
    return gfx_h > 200 ? 48 : 44;
}
static void btn_top(int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    if (CB_RND) {
        // (TOUCH TEST: CALIBRATE at the left of RESET; TILT CALIBRATE: RESET in the middle)
        *w = RS(150);
        *h = RS(50);
        *x = s_kind == K_TILT ? gfx_w / 2 - *w / 2 : gfx_w / 2 - RS(6) - *w;
        *y = s_kind == K_TILT ? RS(330) : RS(318);
        return;
    }
#endif
    *w = btn_w();
    *h = btn_h();
    *x = gfx_w - *w - 6;
    *y = 6;
}
static void btn_bot(int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    if (CB_RND) {
        *w = RS(150);
        *h = RS(50);
        *x = gfx_w / 2 + RS(6);
        *y = RS(318);
        return;
    }
#endif
    *w = btn_w();
    *h = btn_h();
    *x = gfx_w - *w - 6;
    *y = gfx_h - *h - 6;
}
static void button(int x, int y, int w, int h, const char *t, uint16_t bg)
{
#if BOARD_ROUND
    if (CB_RND) {
        rr_btn(x, y, w, h, t, bg, THEME_CALIB_BTN_TEXT);
        return;
    }
#endif
    gfx_fill_rect(x, y, w, h, bg);
    gfx_frame_rect(x, y, w, h, 1, THEME_CALIB_BTN_EDGE);
    int sc = gfx_text_width(t, 2) <= w - 6 ? 2 : 1;  // (CALIBRATE on the CoreS3: small)
    gfx_text_center_in(x, w, y + h / 2 - (sc == 2 ? 7 : 4), t, sc, THEME_CALIB_BTN_TEXT);
}
// a result message in the middle band: big text if it fits, else small
static void msg_text(const char *s)
{
#if BOARD_ROUND
    if (CB_RND) {
        ui_rrect(RB_X, gfx_h / 2 - RS(24), RB_W, RS(48), RS(12), s_kind == K_TILT ? THEME_CALIB_OK_BG : 0);
        rr_line(&KF_TEXT, gfx_h / 2 + KF_TEXT.asc / 2, s, THEME_CALIB_TEXT);
        return;
    }
#endif
    int sc = gfx_text_width(s, 2) <= gfx_w - BACK_W - 48 ? 2 : 1;
    gfx_text_center_in(BACK_W, gfx_w - BACK_W, gfx_h / 2 - (sc == 2 ? 7 : 4), s, sc, THEME_CALIB_TEXT);
}
// RESET: the first tap arms it for 3 s ("TAP AGAIN"), the second tap resets
#define ARM_MS 3000
static int64_t s_arm_at = -100000;
static bool armed(int64_t now)
{
    return now - s_arm_at < ARM_MS;
}
static void reset_button(int x, int y, int w, int h, int64_t now)
{
    bool a = armed(now);
    button(x, y, w, h, a ? "TAP AGAIN" : "RESET", a ? THEME_CALIB_RESET_ARMED : THEME_CALIB_RESET);
}
// a tap on RESET: true when this tap resets (the second tap)
static bool reset_tap(int64_t now)
{
    if (armed(now)) {
        s_arm_at = -100000;
        return true;
    }
    s_arm_at = now;
    sfx_ui(SFX_TICK);
    return false;
}

static void back_button(void)
{
#if BOARD_ROUND
    if (CB_RND) {
        rr_back("< BACK", false, THEME_CALIB_BACK, THEME_CALIB_BACK, THEME_CALIB_BACK_TEXT, THEME_CALIB_BACK_TEXT);
        return;
    }
#endif
    gfx_fill_rect(0, 0, BACK_W - 6, gfx_h, THEME_CALIB_BACK);
    gfx_text_center_in(0, BACK_W - 6, gfx_h / 2 - 14, "<", 4, THEME_CALIB_BACK_TEXT);
}

// BACK of the round frame (the bottom cap)
static bool back_at(int x, int y)
{
#if BOARD_ROUND
    return rr_back_at(x, y);
#else
    (void)x;
    (void)y;
    return false;
#endif
}

// ---------------------------------------------------------------- the touch

#define N_TGT 5                         // 4 near the corners, then the check in the middle
static int s_step = -1;                 // -1: the test view; 0 .. 4: a target; 5: the result
static float s_mx[N_TGT], s_my[N_TGT];  // the measured raw points (physical)
static float s_sx, s_sy;
static int s_sn;
static int64_t s_down_at, s_step_at, s_res_at, s_log_at;
static bool s_was_down;
static char s_res[48];
static bool s_res_ok;

static void target_logical(int i, int *x, int *y)
{
#if BOARD_ROUND
    if (CB_RND) {
        // (the round frame: the 4 targets on a square inside the circle, the check in the middle)
        static const float RX[N_TGT] = {0.27f, 0.73f, 0.73f, 0.27f, 0.54f},
                           RY[N_TGT] = {0.27f, 0.27f, 0.73f, 0.73f, 0.5f};
        *x = (int)(RX[i] * gfx_w);
        *y = (int)(RY[i] * gfx_h);
        return;
    }
#endif
    static const float FX[N_TGT] = {0.20f, 0.88f, 0.88f, 0.20f, 0.54f}, FY[N_TGT] = {0.22f, 0.22f, 0.78f, 0.78f, 0.50f};
    *x = (int)(FX[i] * gfx_w);
    *y = (int)(FY[i] * gfx_h);
}

static void target_physical(int i, float *x, float *y)
{
    int lx, ly, px, py;
    target_logical(i, &lx, &ly);
    gfx_logical_to_touch(lx, ly, &px, &py);
    *x = (float)px;
    *y = (float)py;
}

// the 3 x 3 system A v = b (Cramer)
static bool solve3(const double A[3][3], const double b[3], double v[3])
{
    double d = A[0][0] * (A[1][1] * A[2][2] - A[1][2] * A[2][1]) - A[0][1] * (A[1][0] * A[2][2] - A[1][2] * A[2][0]) +
               A[0][2] * (A[1][0] * A[2][1] - A[1][1] * A[2][0]);
    if (fabs(d) < 1e-6) {
        return false;
    }
    for (int c = 0; c < 3; c++) {
        double M[3][3];
        memcpy(M, A, sizeof M);
        for (int r = 0; r < 3; r++) {
            M[r][c] = b[r];
        }
        v[c] = (M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) - M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0])) /
               d;
    }
    return true;
}

// the affine map (least squares) from the 4 measured points to the 4 targets; the check with target 5
static void touch_finish(void)
{
    double A[3][3] = {{0}}, bx[3] = {0}, by[3] = {0};
    for (int i = 0; i < 4; i++) {
        float ex, ey;
        target_physical(i, &ex, &ey);
        double r[3] = {s_mx[i], s_my[i], 1};
        for (int a = 0; a < 3; a++) {
            for (int b = 0; b < 3; b++) {
                A[a][b] += r[a] * r[b];
            }
            bx[a] += r[a] * ex;
            by[a] += r[a] * ey;
        }
    }
    double vx[3], vy[3];
    s_res_ok = false;
    if (!solve3(A, bx, vx) || !solve3(A, by, vy)) {
        snprintf(s_res, sizeof s_res, "NOT GOOD: TRY AGAIN");
    } else {
        float m[6] = {(float)vx[0], (float)vx[1], (float)vx[2], (float)vy[0], (float)vy[1], (float)vy[2]};
        // the error of the fit (the 4 targets) and of the check target
        float fit = 0;
        for (int i = 0; i < 4; i++) {
            float ex, ey;
            target_physical(i, &ex, &ey);
            float cx = m[0] * s_mx[i] + m[1] * s_my[i] + m[2], cy = m[3] * s_mx[i] + m[4] * s_my[i] + m[5];
            fit = fmaxf(fit, sqrtf((cx - ex) * (cx - ex) + (cy - ey) * (cy - ey)));
        }
        float ex, ey;
        target_physical(4, &ex, &ey);
        float cx = m[0] * s_mx[4] + m[1] * s_my[4] + m[2], cy = m[3] * s_mx[4] + m[4] * s_my[4] + m[5];
        float chk = sqrtf((cx - ex) * (cx - ex) + (cy - ey) * (cy - ey));
        ESP_LOGI(TAG,
                 "touch fit: x' = %.4f x %+.4f y %+.1f, y' = %.4f x %+.4f y %+.1f; the fit error %.1f px, the check "
                 "%.1f px",
                 (double)m[0], (double)m[1], (double)m[2], (double)m[3], (double)m[4], (double)m[5], (double)fit,
                 (double)chk);
        bool sane = fabsf(m[0]) + fabsf(m[1]) > 0.5f && fabsf(m[3]) + fabsf(m[4]) > 0.5f;
        if (sane && fit < 15 && chk < 25) {  // (a finger is about 5 to 10 px off; a wrong tap spreads in the fit)
            input_touch_cal_set(m);
            s_res_ok = true;
            snprintf(s_res, sizeof s_res, "SAVED. THE ERROR IS %d PX", (int)lroundf(fmaxf(fit, chk)));
            sfx_play(SFX_WIN);
        } else {
            snprintf(s_res, sizeof s_res, "NOT GOOD (%d PX): TRY AGAIN", (int)lroundf(fmaxf(fit, chk)));
            sfx_play(SFX_BUMP);
        }
    }
    s_step = N_TGT;
}

static void touch_update(int64_t now)
{
    int16_t rx[INPUT_MAX_POINTS], ry[INPUT_MAX_POINTS];
    int n = input_points_raw(rx, ry, INPUT_MAX_POINTS);
    if (s_step < 0) {
        // the test view: a log line each 200 ms while a finger is down
        if (n > 0 && now - s_log_at >= 200) {
            s_log_at = now;
            int16_t cx[INPUT_MAX_POINTS], cy[INPUT_MAX_POINTS];
            int nc = input_points(cx, cy, INPUT_MAX_POINTS);
            int lx, ly;
            gfx_touch_to_logical(rx[0], ry[0], &lx, &ly);
            ESP_LOGI(TAG, "touch %d: raw %d,%d (screen %d,%d), used %d,%d", n, rx[0], ry[0], lx, ly, nc ? cx[0] : -1,
                     nc ? cy[0] : -1);
        }
        return;
    }
    if (s_step >= N_TGT) {
        if (now - s_res_at > 2600) {
            s_step = -1;
        }
        return;
    }
    if (now - s_step_at > 30000) {  // no tap for 30 s: stop
        s_step = -1;
        return;
    }
    // a target: the mean raw point of one touch (not its first 60 ms: the edge of the finger comes first). A panel can
    // report no point for some polls while the finger stays down: the touch ends after 150 ms with no point.
    static int64_t s_last_pt;
    if (n > 0) {
        s_last_pt = now;
        if (!s_was_down) {
            s_was_down = true;
            s_down_at = now;
            s_sx = s_sy = 0;
            s_sn = 0;
        } else if (now - s_down_at >= 60) {
            s_sx += rx[0];
            s_sy += ry[0];
            s_sn++;
        }
    } else if (s_was_down && now - s_last_pt >= 150) {
        s_was_down = false;
        if (s_sn >= 2) {
            s_mx[s_step] = s_sx / s_sn;
            s_my[s_step] = s_sy / s_sn;
            float ex, ey;
            target_physical(s_step, &ex, &ey);
            ESP_LOGI(TAG, "touch target %d: raw %.0f,%.0f, the target %.0f,%.0f", s_step + 1, (double)s_mx[s_step],
                     (double)s_my[s_step], (double)ex, (double)ey);
            sfx_ui(SFX_TICK);
            s_step++;
            s_step_at = now;
            if (s_step == N_TGT) {
                touch_finish();
                s_res_at = now;
            }
        }
    }
}

static void cross(int x, int y, int r, uint16_t c)
{
    gfx_fill_rect(x - r, y - 1, 2 * r + 1, 3, c);
    gfx_fill_rect(x - 1, y - r, 3, 2 * r + 1, c);
}

static void touch_render(int64_t now)
{
    gfx_fill(THEME_CALIB_BG);
    if (s_step >= 0 && s_step < N_TGT) {
        int tx, ty;
        target_logical(s_step, &tx, &ty);
        bool pulse = (now / 300) & 1;
        gfx_ring(tx, ty, 16, 13, pulse ? rgb(255, 230, 90) : rgb(255, 160, 60));
        cross(tx, ty, 22, rgb(255, 255, 255));
        gfx_fill_circle(tx, ty, 3, rgb(255, 60, 60));
        char t[48];
        snprintf(t, sizeof t, "TAP THE CENTER OF THE TARGET (%d / %d)", s_step + 1, N_TGT);
#if BOARD_ROUND
        if (CB_RND) {
            snprintf(t, sizeof t, "TAP THE CENTER (%d / %d)", s_step + 1, N_TGT);
            rr_line(&KF_TEXT, ty < gfx_h / 2 ? RS(390) : RS(96), t, THEME_CALIB_TEXT);
            return;
        }
#endif
        int ty2 = ty < gfx_h / 2 ? gfx_h - 26 : 10;
        gfx_text_center_in(0, gfx_w, ty2, t, gfx_w > 400 ? 2 : 1, THEME_CALIB_TEXT);
        return;
    }
    // the test view: a grid, the buttons, the points
    for (int x = BACK_W; x < gfx_w; x += 40) {
        gfx_fill_rect(x, 0, 1, gfx_h, rgb(30, 34, 60));
    }
    for (int y = 0; y < gfx_h; y += 40) {
        gfx_fill_rect(BACK_W, y, gfx_w - BACK_W, 1, rgb(30, 34, 60));
    }
    back_button();
    int bx, by, bw, bh;
    btn_top(&bx, &by, &bw, &bh);
    button(bx, by, bw, bh, "CALIBRATE", THEME_CALIB_BTN_GO);
    btn_bot(&bx, &by, &bw, &bh);
    reset_button(bx, by, bw, bh, now);
    float m[6];
    bool on = input_touch_cal_get(m);
#if BOARD_ROUND
    if (CB_RND) {
        rr_head("TOUCH TEST", on ? "CALIBRATION: ON" : "CALIBRATION: OFF", THEME_CALIB_TEXT,
                on ? THEME_CALIB_OK : THEME_CALIB_TEXT_OFF);
        rr_line(&KF_TEXT, RS(170), "RED: THE PANEL READS THIS", THEME_CALIB_HELP);
        rr_line(&KF_TEXT, RS(200), "GREEN: THE BOARD USES THIS", THEME_CALIB_HELP);
        rr_line(&KF_TEXT, RS(230), "GREEN MUST BE UNDER YOUR FINGER", THEME_CALIB_HELP);
    } else
#endif
    {
        gfx_text(BACK_W + 8, 8, "TOUCH TEST", 2, THEME_CALIB_TEXT);
        gfx_text(BACK_W + 8, 28, on ? "CALIBRATION: ON" : "CALIBRATION: OFF", 1,
                 on ? THEME_CALIB_OK : THEME_CALIB_TEXT_OFF);
    }
    if (CB_RND) {
        // (the round frame: the help is on the head lines above)
    } else if (gfx_w > 400) {
        gfx_text(BACK_W + 8, gfx_h - 26, "RED: THE PANEL. GREEN: THE BOARD USES THIS.", 1, THEME_CALIB_HELP);
        gfx_text(BACK_W + 8, gfx_h - 14, "THE GREEN CROSS MUST BE UNDER YOUR FINGER.", 1, THEME_CALIB_HELP);
    } else {
        // (the CoreS3: below the title and the CALIBRATE button)
        gfx_text(BACK_W + 8, 62, "RED: THE PANEL READS THIS.", 1, THEME_CALIB_HELP);
        gfx_text(BACK_W + 8, 74, "GREEN: THE BOARD USES THIS.", 1, THEME_CALIB_HELP);
        gfx_text(BACK_W + 8, 86, "GREEN MUST BE UNDER YOUR FINGER.", 1, THEME_CALIB_HELP);
    }
    if (s_step >= N_TGT) {
        if (!CB_RND) {
            gfx_fill_rect(BACK_W + 20, gfx_h / 2 - 16, gfx_w - BACK_W - 40, 32,
                          s_res_ok ? THEME_CALIB_OK_BG : THEME_CALIB_FAULT_BG);
        }
#if BOARD_ROUND
        if (CB_RND) {
            ui_rrect(RB_X, gfx_h / 2 - RS(24), RB_W, RS(48), RS(12),
                     s_res_ok ? THEME_CALIB_OK_BG : THEME_CALIB_FAULT_BG);
            rr_line(&KF_TEXT, gfx_h / 2 + KF_TEXT.asc / 2, s_res, THEME_CALIB_TEXT);
        } else
#endif
        {
            msg_text(s_res);
        }
    }
    int16_t rx[INPUT_MAX_POINTS], ry[INPUT_MAX_POINTS], cx[INPUT_MAX_POINTS], cy[INPUT_MAX_POINTS];
    int n = input_points_raw(rx, ry, INPUT_MAX_POINTS), nc = input_points(cx, cy, INPUT_MAX_POINTS);
    for (int i = 0; i < n; i++) {
        int lx, ly;
        gfx_touch_to_logical(rx[i], ry[i], &lx, &ly);
        gfx_ring(lx, ly, 18, 15, rgb(255, 70, 70));
    }
    for (int i = 0; i < nc; i++) {
        int lx, ly;
        gfx_touch_to_logical(cx[i], cy[i], &lx, &ly);
        cross(lx, ly, 26, rgb(90, 255, 130));
        char t[24];
        snprintf(t, sizeof t, "%d,%d", lx, ly);
        gfx_text(lx + 8, ly + 8, t, 1, rgb(90, 255, 130));
    }
}

static bool touch_input(const input_event_t *ev, int64_t now)
{
    if (s_step >= 0 && s_step < N_TGT) {
        return false;  // the targets read the raw points
    }
    if (ev->kind != IN_TAP) {
        return false;
    }
    if (CB_RND ? back_at(ev->x, ev->y) : ev->x < BACK_W) {
        return true;
    }
    int bx, by, bw, bh;
    btn_top(&bx, &by, &bw, &bh);
    if (ui_hit(ev->x, ev->y, bx, by, bw, bh, 6)) {
        s_step = 0;
        s_step_at = now;
        s_was_down = false;
        sfx_ui(SFX_CLICK);
        ESP_LOGI(TAG, "touch calibration: start");
        return false;
    }
    btn_bot(&bx, &by, &bw, &bh);
    if (ui_hit(ev->x, ev->y, bx, by, bw, bh, 6)) {
        if (reset_tap(now)) {
            input_touch_cal_set(NULL);
            sfx_ui(SFX_BACK);
            ESP_LOGI(TAG, "touch calibration: reset");
        }
        return false;
    }
    s_arm_at = -100000;  // a tap on another place: RESET is not armed
    return false;
}

// ---------------------------------------------------------------- the tilt

static bool s_have[6];  // +X, -X, +Y, -Y, +Z, -Z
static float s_val[6];
static float s_still_t, s_acc[3], s_ref[3];
static int s_acc_n;
static bool s_taken;  // this still moment gave its side (a move first for the next one)
static char s_tmsg[64];
static int64_t s_tmsg_at, s_tprev;

static void tilt_open_reset(void)
{
    memset(s_have, 0, sizeof s_have);
    s_still_t = 0;
    s_acc_n = 0;
    s_taken = false;
    s_tmsg[0] = 0;
}

static void tilt_finish(void)
{
    float off[3], gain[3];
    for (int k = 0; k < 3; k++) {
        float p = s_val[2 * k], q = s_val[2 * k + 1];  // + side (about +1), - side (about -1)
        off[k] = (p + q) / 2;
        gain[k] = 2 / (p - q);
    }
    bool ok = true;
    for (int k = 0; k < 3; k++) {
        ok &= gain[k] > 0.6f && gain[k] < 1.6f && fabsf(off[k]) < 0.5f;
    }
    ESP_LOGI(TAG, "tilt fit: offsets %+.3f %+.3f %+.3f g, gains %.3f %.3f %.3f%s", (double)off[0], (double)off[1],
             (double)off[2], (double)gain[0], (double)gain[1], (double)gain[2], ok ? "" : " (NOT SANE)");
    if (ok) {
        imu_accel_cal_set(off, gain);
        imu_set_accel_scale(0);  // AUTO (the axes are right now: it stays about 1)
        snprintf(s_tmsg, sizeof s_tmsg, "SAVED");
        sfx_play(SFX_WIN);
    } else {
        snprintf(s_tmsg, sizeof s_tmsg, "NOT GOOD: DO IT AGAIN");
        sfx_play(SFX_BUMP);
        memset(s_have, 0, sizeof s_have);
    }
    s_tmsg_at = s_tprev;
}

static void tilt_update(int64_t now)
{
    float dt = s_tprev ? (now - s_tprev) / 1000.0f : 0;
    s_tprev = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    float a[3], w[3];
    imu_raw(a, w);
    float wm = sqrtf(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    float d = sqrtf((a[0] - s_ref[0]) * (a[0] - s_ref[0]) + (a[1] - s_ref[1]) * (a[1] - s_ref[1]) +
                    (a[2] - s_ref[2]) * (a[2] - s_ref[2]));
    if (wm < 0.08f && d < 0.04f) {
        s_still_t += dt;
        for (int k = 0; k < 3; k++) {
            s_acc[k] += a[k];
        }
        s_acc_n++;
    } else {
        memcpy(s_ref, a, sizeof s_ref);
        s_still_t = 0;
        s_acc[0] = s_acc[1] = s_acc[2] = 0;
        s_acc_n = 0;
        s_taken = false;
    }
    if (s_still_t >= 1.0f && !s_taken && s_acc_n > 5) {
        s_taken = true;
        float m[3] = {s_acc[0] / s_acc_n, s_acc[1] / s_acc_n, s_acc[2] / s_acc_n};
        float g = sqrtf(m[0] * m[0] + m[1] * m[1] + m[2] * m[2]);
        int k = 0;
        for (int i = 1; i < 3; i++) {
            if (fabsf(m[i]) > fabsf(m[k])) {
                k = i;
            }
        }
        if (fabsf(m[k]) > 0.85f * g) {
            int slot = 2 * k + (m[k] > 0 ? 0 : 1);
            bool fresh = !s_have[slot];
            s_have[slot] = true;
            s_val[slot] = m[k];
            ESP_LOGI(TAG, "tilt side %c%c: %+.3f g (all: %+.3f %+.3f %+.3f, size %.3f)", m[k] > 0 ? '+' : '-', 'X' + k,
                     (double)m[k], (double)m[0], (double)m[1], (double)m[2], (double)g);
            if (fresh) {
                sfx_ui(SFX_COIN);
            }
            int n = 0;
            for (int i = 0; i < 6; i++) {
                n += s_have[i];
            }
            if (n == 6) {
                tilt_finish();
            }
        } else {
            sfx_play(SFX_BUMP);  // not flat on a side
        }
    }
}

#if BOARD_ROUND
static void tilt_render_round(int64_t now);
#endif

static void tilt_render(int64_t now)
{
#if BOARD_ROUND
    if (CB_RND) {
        tilt_render_round(now);
        return;
    }
#endif
    gfx_fill(THEME_CALIB_BG);
    back_button();
    int bx, by, bw, bh;
    btn_top(&bx, &by, &bw, &bh);
    reset_button(bx, by, bw, bh, now);
    bool wide = gfx_w > 400;
    gfx_text(BACK_W + 8, 8, wide ? "TILT CALIBRATE" : "TILT CALIB.", 2, THEME_CALIB_TEXT);
    // (the text stays out of the RESET button: on the 3.49 at its left, on the CoreS3 below it; the lines break at a
    // space, and the sides go down when the text needs more lines)
    const int hy = wide ? 28 : 58, hw = (wide ? bx - 8 : gfx_w - 8) - (BACK_W + 8);
    const int hn = set_wrap(BACK_W + 8, hy, hw, 12,
                            wide ? "PUT THE BOARD ON EACH OF ITS 6 SIDES, STILL FOR 1 S. SCREEN UP, SCREEN DOWN, EACH "
                                   "LONG EDGE, EACH SHORT END. A CLICK FOR EACH SIDE."
                                 : "PUT THE BOARD ON EACH OF ITS 6 SIDES, STILL FOR 1 S (A CLICK FOR EACH SIDE).",
                            1, THEME_CALIB_TILT_HELP, false);
    static const char *const NAME[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    int cols = wide ? 6 : 3, gap = 6;
    int w = (gfx_w - BACK_W - 12 - (cols - 1) * gap) / cols, h = wide ? 40 : 44;
    int y0 = hy + 12 * hn + 4;
    if (y0 < (wide ? 56 : 86)) {
        y0 = wide ? 56 : 86;
    }
    for (int i = 0; i < 6; i++) {
        int x = BACK_W + 6 + (i % cols) * (w + gap), y = y0 + (i / cols) * (h + gap);
        gfx_fill_rect(x, y, w, h, s_have[i] ? THEME_CALIB_SIDE_DONE : THEME_CALIB_SIDE);
        gfx_frame_rect(x, y, w, h, 1, s_have[i] ? THEME_CALIB_SIDE_DONE_EDGE : THEME_CALIB_SIDE_EDGE);
        gfx_text(x + 4, y + 4, NAME[i], 2, THEME_CALIB_TEXT);
        char t[16];
        if (s_have[i]) {
            snprintf(t, sizeof t, "%+.3f G", (double)s_val[i]);
        } else {
            snprintf(t, sizeof t, "--");
        }
        gfx_text(x + 4, y + h - 12, t, 1, THEME_CALIB_SIDE_VALUE);
    }
    // now: the size of the raw reading, and with the calibration (if any)
    float a[3], w3[3], off[3], gain[3];
    imu_raw(a, w3);
    imu_accel_cal_get(off, gain);
    bool on = imu_accel_cal_on();
    float r = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), c = r;
    if (on) {
        float b[3];
        for (int k = 0; k < 3; k++) {
            b[k] = (a[k] - off[k]) * gain[k];
        }
        c = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    }
    char t[80];
    int ly = gfx_h - 16;
    snprintf(t, sizeof t, "NOW %.2f G%s%s", (double)r, on ? ", CALIBRATED " : "", "");
    if (on) {
        snprintf(t + strlen(t), sizeof t - strlen(t), "%.2f G", (double)c);
    }
    gfx_text(BACK_W + 8, ly, t, 1, THEME_CALIB_NOW);
    // the still bar
    int barw = wide ? 160 : 100;
    gfx_fill_rect(gfx_w - barw - 8, ly, barw, 8, THEME_CALIB_TRACK);
    gfx_fill_rect(gfx_w - barw - 8, ly, (int)(barw * fminf(1, s_still_t)), 8,
                  s_taken ? THEME_CALIB_BAR_DONE : THEME_CALIB_BAR);
    if (s_tmsg[0] && now - s_tmsg_at < 4000) {
        gfx_fill_rect(BACK_W + 20, gfx_h / 2 - 16, gfx_w - BACK_W - 40, 32, THEME_CALIB_OK_BG);
        msg_text(s_tmsg);
    }
}

#if BOARD_ROUND
// the round frame: the title on the top cap, the help, the 6 sides (3 x 2), the reading and RESET in the middle box
static void tilt_render_round(int64_t now)
{
    gfx_fill(THEME_CALIB_BG);
    rr_head("TILT CALIBRATE", "6 SIDES, STILL FOR 1 S EACH", THEME_CALIB_TEXT, THEME_CALIB_TILT_HELP);
    rr_line(&KF_LABEL, RS(126), "SCREEN UP, SCREEN DOWN, EACH LONG EDGE,", THEME_CALIB_TILT_HELP);
    rr_line(&KF_LABEL, RS(144), "EACH SHORT END. A CLICK FOR EACH SIDE", THEME_CALIB_TILT_HELP);
    static const char *const NAME[6] = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    const int g = RS(6), w = (RB_W - 2 * g) / 3, h = RS(60), y0 = RS(158);
    for (int i = 0; i < 6; i++) {
        int x = RB_X + (i % 3) * (w + g), y = y0 + (i / 3) * (h + g);
        ui_rbox(x, y, w, h, RS(10), s_have[i] ? THEME_CALIB_SIDE_DONE : THEME_CALIB_SIDE,
                s_have[i] ? THEME_CALIB_SIDE_DONE_EDGE : THEME_CALIB_SIDE_EDGE, 1);
        ui_text_c(&KF_BIG, x + w / 2, y + RS(30), NAME[i], THEME_CALIB_TEXT);
        char t[16];
        if (s_have[i]) {
            snprintf(t, sizeof t, "%+.3f G", (double)s_val[i]);
        } else {
            snprintf(t, sizeof t, "--");
        }
        ui_text_c(&KF_LABEL, x + w / 2, y + h - RS(10), t, THEME_CALIB_SIDE_VALUE);
    }
    float a[3], w3[3], off[3], gain[3];
    imu_raw(a, w3);
    imu_accel_cal_get(off, gain);
    bool on = imu_accel_cal_on();
    float r = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]), c = r;
    if (on) {
        float b[3];
        for (int k = 0; k < 3; k++) {
            b[k] = (a[k] - off[k]) * gain[k];
        }
        c = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
    }
    char t[80];
    snprintf(t, sizeof t, "NOW %.2f G", (double)r);
    if (on) {
        snprintf(t + strlen(t), sizeof t - strlen(t), ", CALIBRATED %.2f G", (double)c);
    }
    rr_line(&KF_TEXT, RS(304), t, THEME_CALIB_NOW);
    int barw = RS(200), bx = gfx_w / 2 - barw / 2;
    ui_rrect(bx, RS(314), barw, RS(8), RS(4), THEME_CALIB_TRACK);
    ui_rrect(bx, RS(314), (int)(barw * fminf(1, s_still_t)), RS(8), RS(4),
             s_taken ? THEME_CALIB_BAR_DONE : THEME_CALIB_BAR);
    int x, y, bw, bh;
    btn_top(&x, &y, &bw, &bh);
    reset_button(x, y, bw, bh, now);
    back_button();
    if (s_tmsg[0] && now - s_tmsg_at < 4000) {
        msg_text(s_tmsg);
    }
}
#endif

static bool tilt_input(const input_event_t *ev, int64_t now)
{
    if (ev->kind != IN_TAP) {
        return false;
    }
    if (CB_RND ? back_at(ev->x, ev->y) : ev->x < BACK_W) {
        return true;
    }
    int bx, by, bw, bh;
    btn_top(&bx, &by, &bw, &bh);
    if (ui_hit(ev->x, ev->y, bx, by, bw, bh, 6)) {
        if (reset_tap(now)) {
            imu_accel_cal_set(NULL, NULL);
            tilt_open_reset();
            sfx_ui(SFX_BACK);
            ESP_LOGI(TAG, "tilt calibration: reset");
        }
        return false;
    }
    s_arm_at = -100000;
    return false;
}

// ---------------------------------------------------------------- the page

void calib_touch_open(void)
{
    s_kind = K_TOUCH;
    s_step = -1;
    s_arm_at = -100000;
}

void calib_tilt_open(void)
{
    s_kind = K_TILT;
    s_tprev = 0;
    s_arm_at = -100000;
    tilt_open_reset();
}

void calib_update(int64_t now)
{
    if (s_kind == K_TOUCH) {
        touch_update(now);
    } else {
        tilt_update(now);
    }
}

bool calib_input(const input_event_t *ev, int64_t now)
{
    return s_kind == K_TOUCH ? touch_input(ev, now) : tilt_input(ev, now);
}

void calib_render(int64_t now)
{
    if (s_kind == K_TOUCH) {
        touch_render(now);
    } else {
        tilt_render(now);
    }
}
