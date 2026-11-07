// TFN OS SDK example 2: TILT BALL, a ball that rolls with the tilt of the board.
//
// Tilt the board to roll the ball. Roll it into the glowing gem. The ball bounces off the walls. A hard hit shakes the
// camera. The camera also moves a little with the ball.
//
// What this example shows:
//   - the motion sensor: imu_ok(), imu_set_level() and imu_tilt_logical() (visual.md section 6, behave.md section 3)
//   - THE ZERO RULE of the project: the position of the board at the start is the zero (imu_set_level() in enter()),
//     and the app takes the zero again when the settings panel goes away (the resume function of the panel items)
//   - small angles that do something: 1 degree is about 0.017 g and the ball moves at once (a dead zone of 0.3 degree)
//   - the feel of a game: a soft camera and a shake on a hard hit
//   - the settings panel items: a choice (SENSITIVITY, with a save key) and an action (ZERO), see behave.md section 7
//   - the battery rule: an idle ball does not draw busy frames (behave.md section 9)
//   - the layouts of the 3.49 bar and of the 320 x 240 frame, and a render() that paints the whole frame
//
// The Watcher has no motion sensor: .needs = APP_NEEDS_IMU hides the app there.
//
// Build (from tfn-os/):
//   python sdk/tools/mk_app.py sdk/examples/tilt/app_tiltball.c --sym app_tiltball --name "TILT BALL" --kinds lcd349,cores3 --nosign
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"

// ---------------------------------------------------------------- the arena of each board kind
// The walls are boxes (x, y, w, h) in frame px. The outer wall is the border of the frame.
typedef struct { int x, y, w, h; } box_t;
typedef struct { int x, y; } pt_t;

#if BOARD_BAR_SCREEN
#define SW 640                              // the frame
#define SH 172
static const box_t WALLS[] = {{140, 8, 14, 84}, {290, 80, 14, 84}, {440, 8, 14, 84}, {520, 86, 80, 12}};
static const pt_t GEMS[] = {{70, 86}, {220, 40}, {220, 130}, {370, 40}, {370, 130}, {500, 40}, {580, 130}, {590, 40}};
#define START_X 40
#define START_Y 130
#define CHIP_X  14
#define CHIP_Y  12
#else
#define SW 320
#define SH 240
static const box_t WALLS[] = {{8, 72, 192, 12}, {112, 156, 200, 12}};
static const pt_t GEMS[] = {{60, 40}, {260, 40}, {260, 120}, {60, 120}, {60, 200}, {260, 200}, {160, 40}};
#define START_X 160
#define START_Y 120
#define CHIP_X  14
#define CHIP_Y  14
#endif
#define N_WALLS ((int)(sizeof WALLS / sizeof WALLS[0]))
#define N_GEMS  ((int)(sizeof GEMS / sizeof GEMS[0]))

#define BORDER      8                       // the thickness of the outer wall
#define BALL_R      11
#define GEM_R       12
#define REST        0.45f                   // the bounce: the part of the speed that stays after a hit
#define HIT_SPEED   120.0f                  // a hit above this speed (px/s) makes a sound and a shake
#define DEAD_ZONE   0.005f                  // g: a tilt of about 0.3 degree gives nothing (the noise of the sensor)
#define V_MAX       650.0f
#define FRICTION    1.4f                    // 1/s
#define SUB_DT      0.005f                  // the physics step: a fast ball must not jump over a wall

// the settings panel: the sensitivity (the value is saved by the panel with this key) and the action ZERO
enum { HI_SENS, HI_ZERO };
static const char *const SENS_VALS[] = {"LOW", "נמוך", "NORMAL", "רגיל", "HIGH", "גבוה"};
static const pitem_t ITEMS[] = {
    {PI_PICK, HI_SENS, 3, 1, {"SENSITIVITY", "רגישות"}, SENS_VALS, "tiltball_sens"},
    {PI_ACT, HI_ZERO, 0, 0, {"ZERO", "אפס"}, NULL, NULL},
};
static const float GAIN[3] = {4500.0f, 7000.0f, 10000.0f};      // px/s2 for a tilt of 1 g

// ---------------------------------------------------------------- the state
static float s_x, s_y, s_vx, s_vy;
static float s_cam_x, s_cam_y;              // the soft camera, px
static float s_shake;                       // the amplitude of the shake, px
static int s_sens;
static int s_gem, s_score;
static int64_t s_last, s_hit_ms, s_calm_since;
static float s_pulse;                       // the time for the gem glow
static pt_t s_trail[10];
static int s_trail_n;
static int64_t s_spark_ms;                  // the time of the last gem pickup
static pt_t s_spark;

static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static float dead(float v)                  // remove the dead zone, keep the sign
{
    float a = v < 0 ? -v : v;
    a = a > DEAD_ZONE ? a - DEAD_ZONE : 0;
    return v < 0 ? -a : a;
}

// ---------------------------------------------------------------- the physics
// The ball against a box. Returns the speed of the hit (px/s), 0 when there is no hit.
static float hit_box(const box_t *b)
{
    float nx = clampf(s_x, (float)b->x, (float)(b->x + b->w));
    float ny = clampf(s_y, (float)b->y, (float)(b->y + b->h));
    float dx = s_x - nx, dy = s_y - ny, d2 = dx * dx + dy * dy;
    if (d2 >= (float)(BALL_R * BALL_R)) return 0;
    float ux, uy, pen;
    if (d2 > 1e-6f) {
        float d = sqrtf(d2);
        ux = dx / d;
        uy = dy / d;
        pen = BALL_R - d;
    } else {                                // the centre is inside the box: go out by the nearest side
        float l = s_x - b->x, r = b->x + b->w - s_x, t = s_y - b->y, bt = b->y + b->h - s_y;
        float m = fminf(fminf(l, r), fminf(t, bt));
        ux = uy = 0;
        if (m == l) ux = -1; else if (m == r) ux = 1; else if (m == t) uy = -1; else uy = 1;
        pen = m + BALL_R;
    }
    s_x += ux * pen;
    s_y += uy * pen;
    float vn = s_vx * ux + s_vy * uy;
    if (vn >= 0) return 0;
    s_vx -= (1 + REST) * vn * ux;
    s_vy -= (1 + REST) * vn * uy;
    return -vn;
}

static float hit_arena(void)                // the outer wall
{
    float hit = 0;
    if (s_x < BORDER + BALL_R)      { s_x = BORDER + BALL_R;      if (s_vx < 0) { hit = fmaxf(hit, -s_vx); s_vx = -s_vx * REST; } }
    if (s_x > SW - BORDER - BALL_R) { s_x = SW - BORDER - BALL_R; if (s_vx > 0) { hit = fmaxf(hit, s_vx);  s_vx = -s_vx * REST; } }
    if (s_y < BORDER + BALL_R)      { s_y = BORDER + BALL_R;      if (s_vy < 0) { hit = fmaxf(hit, -s_vy); s_vy = -s_vy * REST; } }
    if (s_y > SH - BORDER - BALL_R) { s_y = SH - BORDER - BALL_R; if (s_vy > 0) { hit = fmaxf(hit, s_vy);  s_vy = -s_vy * REST; } }
    return hit;
}

static void next_gem(void)
{
    // a random gem that is not the last one and not too near the ball
    for (int tries = 0; tries < 20; tries++) {
        int k = (int)(esp_timer_get_time() / 997 + tries * 7 + s_score * 3) % N_GEMS;
        float dx = GEMS[k].x - s_x, dy = GEMS[k].y - s_y;
        if (k != s_gem && dx * dx + dy * dy > 90.0f * 90.0f) { s_gem = k; return; }
    }
    s_gem = (s_gem + 1) % N_GEMS;
}

// ---------------------------------------------------------------- the app functions
static void item_set(int id, int v)
{
    if (id == HI_SENS) s_sens = v;          // (the panel wrote the save key already)
    if (id == HI_ZERO) imu_set_level();     // ZERO: the position now is the new zero
}
static void item_resume(void) { imu_set_level(); }
static const pitems_t PANEL = {ITEMS, 2, NULL, NULL, item_set, NULL, item_resume};

static void enter(int64_t now)
{
    s_sens = (int)store_get("tiltball_sens", 1);
    if (s_sens < 0 || s_sens > 2) s_sens = 1;
    imu_set_level();                        // THE ZERO RULE: the board position at the start is the level
    s_x = START_X;
    s_y = START_Y;
    s_vx = s_vy = 0;
    s_cam_x = s_cam_y = 0;
    s_shake = 0;
    s_score = 0;
    s_gem = -1;
    next_gem();
    s_last = now;
    s_hit_ms = s_spark_ms = -10000;
    s_calm_since = now;
    s_trail_n = 0;
    sysui_panel_items(&PANEL);              // the options go ONLY into the panel
}

static void input(const input_event_t *ev)
{
    (void)ev;                               // the app has no touch controls: the tilt is the control
}

static void update(int64_t now)
{
    float dt = (float)(now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.05f) dt = 0.05f;             // a slow frame must not make a jump
    if (dt <= 0) return;
    s_pulse += dt;

    float tx = 0, ty = 0;
    if (imu_ok()) imu_tilt_logical(&tx, &ty);   // g from the zero; + x = to the right, + y = down on the screen
    float ax = dead(tx) * GAIN[s_sens], ay = dead(ty) * GAIN[s_sens];

    int steps = (int)ceilf(dt / SUB_DT);
    float h = dt / (float)steps, hit = 0;
    for (int i = 0; i < steps; i++) {
        s_vx += ax * h;
        s_vy += ay * h;
        float f = expf(-FRICTION * h);
        s_vx *= f;
        s_vy *= f;
        float sp = sqrtf(s_vx * s_vx + s_vy * s_vy);
        if (sp > V_MAX) { s_vx *= V_MAX / sp; s_vy *= V_MAX / sp; }
        s_x += s_vx * h;
        s_y += s_vy * h;
        hit = fmaxf(hit, hit_arena());
        for (int k = 0; k < N_WALLS; k++) hit = fmaxf(hit, hit_box(&WALLS[k]));
    }
    if (ax == 0 && ay == 0 && s_vx * s_vx + s_vy * s_vy < 64.0f) s_vx = s_vy = 0;     // it stops at last

    if (hit > HIT_SPEED && now - s_hit_ms > 100) {
        s_hit_ms = now;
        s_shake = clampf(hit / 70.0f, 1.5f, 9.0f);
        sfx_tone(190, 90, 55, W_TRI, (uint8_t)clampf(15.0f + hit / 14.0f, 15, 60));
    }
    s_shake *= expf(-9.0f * dt);            // the shake dies in about 0.3 s
    if (s_shake < 0.05f) s_shake = 0;

    // the soft camera: it leans a little toward the ball
    float k = 1.0f - expf(-5.0f * dt);
    s_cam_x += ((s_x - SW / 2) * 0.03f - s_cam_x) * k;
    s_cam_y += ((s_y - SH / 2) * 0.03f - s_cam_y) * k;

    float gx = GEMS[s_gem].x - s_x, gy = GEMS[s_gem].y - s_y;
    if (gx * gx + gy * gy < (float)((BALL_R + GEM_R - 4) * (BALL_R + GEM_R - 4))) {
        s_score++;
        s_spark = GEMS[s_gem];
        s_spark_ms = now;
        sfx_play(SFX_COIN);
        next_gem();
    }

    if (s_trail_n < 10) s_trail[s_trail_n++] = (pt_t){(int)s_x, (int)s_y};
    else { for (int i = 0; i < 9; i++) s_trail[i] = s_trail[i + 1]; s_trail[9] = (pt_t){(int)s_x, (int)s_y}; }

    // The battery rule: when the ball lies still for 3 s, wait between the frames. A new tilt wakes the app in 100 ms.
    bool calm = s_vx == 0 && s_vy == 0 && ax == 0 && ay == 0 && s_shake == 0;
    if (!calm) s_calm_since = now;
#ifdef ESP_PLATFORM
    else if (now - s_calm_since > 3000) vTaskDelay(pdMS_TO_TICKS(100));
#endif
}

// ---------------------------------------------------------------- drawing
static void render(int64_t now)
{
    // The camera: the whole arena moves by (ox, oy). The frame is painted first, so the border of the frame never
    // shows an old picture when the arena moves.
    float t = (float)now / 1000.0f;
    int ox = (int)(-s_cam_x + s_shake * fsin(t * 83.0f)), oy = (int)(-s_cam_y + s_shake * fsin(t * 71.0f + 1.0f));
    gfx_fill(rgb(8, 8, 18));
    gfx_fill_rect(ox, oy, SW, SH, rgb(92, 98, 140));                          // the outer wall
    gfx_fill_rect(ox + BORDER, oy + BORDER, SW - 2 * BORDER, SH - 2 * BORDER, rgb(30, 34, 58));   // the floor
    for (int x = BORDER + 40; x < SW - BORDER; x += 40)                       // a grid: it shows the motion
        gfx_fill_rect(ox + x, oy + BORDER, 1, SH - 2 * BORDER, rgb(38, 43, 72));
    for (int y = BORDER + 40; y < SH - BORDER; y += 40)
        gfx_fill_rect(ox + BORDER, oy + y, SW - 2 * BORDER, 1, rgb(38, 43, 72));

    for (int k = 0; k < N_WALLS; k++) {                                       // the walls, with a shadow
        const box_t *b = &WALLS[k];
        int sh = b->y + b->h + 4 > SH - BORDER ? SH - BORDER - b->y - 4 : b->h;       // the shadow stays on the floor
        gfx_darken_rect(ox + b->x + 3, oy + b->y + 4, b->w, sh, 1);
        gfx_fill_rect(ox + b->x, oy + b->y, b->w, b->h, rgb(92, 98, 140));
        gfx_fill_rect(ox + b->x, oy + b->y, b->w, 2, rgb(130, 138, 190));      // a light top edge
    }

    // the gem: a glow that breathes, and a diamond core
    int gx = ox + GEMS[s_gem].x, gy = oy + GEMS[s_gem].y;
    float br = 0.5f + 0.5f * fsin(s_pulse * 4.0f);
    ui_circle(gx, gy, GEM_R + 4 + (int)(5 * br), rgb(60, 220, 255), 50);
    ui_circle(gx, gy, GEM_R - 2, rgb(80, 230, 255), 255);
    ui_circle(gx - 3, gy - 3, 3, rgb(255, 255, 255), 200);
    if (now - s_spark_ms < 350) {                                             // a ring after a pickup
        float f = (float)(now - s_spark_ms) / 350.0f;
        ui_ring(ox + s_spark.x, oy + s_spark.y, GEM_R + (int)(f * 26), 3, rgb(200, 250, 255), (int)(255 * (1 - f)));
    }

    for (int i = 0; i < s_trail_n; i++)                                       // the trail of the ball
        ui_circle(ox + s_trail[i].x, oy + s_trail[i].y, 3 + i * BALL_R / 12, rgb(255, 190, 80), 12 + i * 5);

    int bx = ox + (int)s_x, by = oy + (int)s_y;                               // the ball
    ui_circle(bx + 3, by + 5, BALL_R, rgb(0, 0, 10), 120);
    ui_circle(bx, by, BALL_R, rgb(255, 176, 60), 255);
    ui_circle(bx - 4, by - 4, 4, rgb(255, 250, 220), 170);

    // the score chip: it does not move with the camera
    char b[16];
    snprintf(b, sizeof b, "GEMS %d", s_score);
    int w = mf_width(&MF_LABEL, b, 1) + 20;
    ui_rrect(CHIP_X, CHIP_Y, w, 26, 8, rgb(12, 12, 30));
    mf_text(&MF_LABEL, CHIP_X + 10, CHIP_Y + 19, b, rgb(255, 255, 255), 255, 1);
    if (!imu_ok()) mf_text_c(&MF_N20, SW / 2, SH / 2 + 8, "NO MOTION SENSOR", rgb(255, 120, 120), 255, 1);
}

// The menu icon: the ball rolls to and fro (a period of 2 s), the gem breathes. Only gfx_* shapes; the whole box.
static void icon(int x, int y, int s)
{
    float t = icon_time();
    gfx_fill_rect(x, y, s, s, rgb(92, 98, 140));                              // the wall
    gfx_fill_rect(x + s / 14, y + s / 14, s - s / 7, s - s / 7, rgb(30, 34, 58));    // the floor
    gfx_fill_rect(x + s / 14, y + s * 3 / 4, s - s / 7, s / 10, rgb(92, 98, 140));   // a shelf
    int gr = s / 9 + (int)(2 * (0.5f + 0.5f * fsin(t * 6.2832f * 1.0f)));
    gfx_fill_circle(x + s * 3 / 4, y + s * 36 / 100, gr + s / 20, rgb(40, 110, 140));
    gfx_fill_circle(x + s * 3 / 4, y + s * 36 / 100, gr, rgb(80, 230, 255));
    float p = fsin(t * 3.14159f);                                             // -1..1, a period of 2 s
    int r = s / 7;
    int bx = x + s / 2 + (int)(p * s * 0.28f), by = y + s * 3 / 4 - s / 10 - r;
    gfx_fill_circle(bx + s / 28, by + s / 20, r, rgb(12, 12, 28));            // the shadow
    gfx_fill_circle(bx, by, r, rgb(255, 176, 60));
    gfx_fill_circle(bx - r / 3, by - r / 3, r / 3, rgb(255, 250, 220));
}

const app_t app_tiltball = {
    .name = "TILT BALL",
    .landscape = true,                      // 640 x 172 on the 3.49, 320 x 240 on the other boards
    .enter = enter,
    .input = input,                         // never NULL, also when the app has no touch controls
    .update = update,
    .render = render,
    .draw_icon = icon,
    .round_fit = true,
    .needs = APP_NEEDS_IMU,                 // the app has no use without the motion sensor
};
