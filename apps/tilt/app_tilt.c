// TILT (GAMES): a labyrinth in a long tube on the portrait screen (172 x 640, or 240 x 320 on the CoreS3: a
// wider maze and a shorter view).
// The task: the app task of TFN OS calls the hooks of app_tilt.
// The memory: the file statics (the maze, the ball, the level); no heap.
//
// Tilt the board to roll the ball down the maze to the green end.
// Holes send the ball back to the last checkpoint. Each level has a longer maze.
// The board position at the start of a level counts as flat (a virtual table),
// so hold the board as you like. A long touch sets the current position as level again.

#include <math.h>
#include <stdio.h>
#include <string.h>
#include "esp_random.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/tunes.h"
#include "tfn/gfx.h"
#include "tfn/mfont.h"
#include "tfn/imu.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#if BOARD_ROUND
#include "tfn/kfont.h"
#include "tfn/uikit.h"
#endif

#if BOARD_BAR_SCREEN
#define SCR_W      172
#define SCR_H      640
#define COLS       7
#define CP_EVERY   12  // checkpoint every 12 rows
#define MAX_SPEED  420.0f
#define CAM_BALL_Y 260  // the camera keeps the ball this far from the top
#define ROWS_K     1    // the length of a level: rows_for_level() * ROWS_K / ROWS_D
#define ROWS_D     1
#elif BOARD_UI_TABLET
// Tab5 (360 x 640): the CoreS3 maze (9 columns) at 1.5 times the size: the cells, the
// walls, the ball, the holes and the speeds. The view shows about 17 rows (CoreS3: 13).
#define SCR_W      360
#define SCR_H      640
#define COLS       9
#define CP_EVERY   10
#define MAX_SPEED  510.0f
#define CAM_BALL_Y 210
#define ROWS_K     7
#define ROWS_D     9
#define CELL       36
#define MAZE_TOP   60
#define WALL       6
#define BALL_R     9
#define HOLE_R     12
#define ACCEL      1650.0f
#define GOAL_R     15  // the goal: the outer and the inner circle
#define GOAL_R2    10
#define HUD_H      46  // the HUD band at the top, and its text scale
#define HUD_S      3
#define HUD_TY     12
#elif BOARD_ROUND
// THE ROUND LAYOUT: the round frame of the panel (app_t.round = APP_ROUND_HI: D = 466 on the AMOLED-1.75C,
// 412 on the Watcher) is a round wooden tray. The maze of the CoreS3 (9 columns), RF = D / 345 times bigger (the same
// sizes in mm), goes down through the middle of the tray; the camera keeps the ball at 0.38 D, where the circle is
// wide. The level and the time are on the top arc; the texts use the smooth fonts.
#define RF         (gfx_w / 345.0f)
#define SCR_W      gfx_w
#define SCR_H      gfx_h
#define COLS       9
#define CP_EVERY   10
#define MAX_SPEED  (340.0f * RF)
#define CAM_BALL_Y (gfx_h * 38 / 100)
#define ROWS_K     7
#define ROWS_D     9
#define CELL       ((int)(24 * RF))
#define MAZE_TOP   (gfx_h * 17 / 100)
#define WALL       ((int)(4 * RF + 0.5f))
#define BALL_R     ((int)(6 * RF + 0.5f))
#define HOLE_R     ((int)(8 * RF + 0.5f))
#define ACCEL      (1100.0f * RF)
#define GOAL_R     ((int)(10 * RF))
#define GOAL_R2    ((int)(7 * RF))
#define HUD_H      (gfx_h * 15 / 100)
#define HUD_S      2
#define HUD_TY     8
#else
// CoreS3: 9 columns of the same size. A level has about the same number of cells as on
// the 3.49 (fewer, wider rows) and the same number of checkpoints. The view is half as
// tall, so the top speed is lower: the player sees the way ahead in time.
#define SCR_W      240
#define SCR_H      320
#define COLS       9
#define CP_EVERY   10
#define MAX_SPEED  340.0f
#define CAM_BALL_Y 110
#define ROWS_K     7
#define ROWS_D     9
#endif
#ifndef CELL
#define CELL     24
#define MAZE_TOP 40  // maze top in world pixels (room for the HUD)
#define WALL     4
#define BALL_R   6
#define HOLE_R   8
#define ACCEL    1100.0f  // px/s^2 per g of tilt
#define GOAL_R   10
#define GOAL_R2  7
#define HUD_H    30
#define HUD_S    2
#define HUD_TY   8
#endif
#define MX        ((SCR_W - COLS * CELL) / 2)  // maze left edge on the screen
#define MAX_ROWS  150
#define MAX_HOLES 64

#define W_EAST  1
#define W_SOUTH 2

typedef enum {
    ST_TITLE,
    ST_PLAY,
    ST_FALL,
    ST_WIN
} tilt_state_t;

static uint8_t s_walls[MAX_ROWS][COLS];
static int s_rows;
static struct {
    int16_t x, y;
} s_hole[MAX_HOLES];
static int s_nholes;
static int s_goal_c;

static tilt_state_t s_state;
static int s_level = 1;
static uint32_t s_best_level;
static float s_bx, s_by, s_vx, s_vy;  // ball, maze coordinates
static float s_rx, s_ry;              // respawn point
static int s_next_cp;                 // row of the next checkpoint
static float s_cam;
static int64_t s_last, s_t_start, s_t_end, s_fall_at, s_last_bump;
static float s_fall_x, s_fall_y;
static uint16_t s_text, s_accent, s_wall, s_wall_hi, s_shadow;

// ---------------------------------------------------------------- maze

static int rnd(int n)
{
    return (int)(esp_random() % (uint32_t)n);
}

static void make_maze(int rows)
{
    static uint16_t stack[MAX_ROWS * COLS];
    static uint8_t seen[MAX_ROWS][COLS];
    s_rows = rows;
    memset(seen, 0, sizeof(seen));
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < COLS; c++) {
            s_walls[r][c] = W_EAST | W_SOUTH;
        }
    }
    // depth-first carve
    int sp = 0;
    int start = rnd(COLS);
    stack[sp++] = (uint16_t)start;
    seen[0][start] = 1;
    while (sp > 0) {
        int cell = stack[sp - 1];
        int r = cell / COLS, c = cell % COLS;
        int opts[4], n = 0;
        if (r > 0 && !seen[r - 1][c]) {
            opts[n++] = 0;
        }
        if (r < rows - 1 && !seen[r + 1][c]) {
            opts[n++] = 1;
        }
        if (c > 0 && !seen[r][c - 1]) {
            opts[n++] = 2;
        }
        if (c < COLS - 1 && !seen[r][c + 1]) {
            opts[n++] = 3;
        }
        if (n == 0) {
            sp--;
            continue;
        }
        int nr = r, nc = c;
        switch (opts[rnd(n)]) {
        case 0:
            nr = r - 1;
            s_walls[nr][c] &= ~W_SOUTH;
            break;
        case 1:
            nr = r + 1;
            s_walls[r][c] &= ~W_SOUTH;
            break;
        case 2:
            nc = c - 1;
            s_walls[r][nc] &= ~W_EAST;
            break;
        default:
            nc = c + 1;
            s_walls[r][c] &= ~W_EAST;
            break;
        }
        seen[nr][nc] = 1;
        stack[sp++] = (uint16_t)(nr * COLS + nc);
    }
    // open some more walls: loops make the tube less of a puzzle and more of a ride
    for (int i = 0; i < rows * COLS / 8; i++) {
        int r = rnd(rows - 1), c = rnd(COLS - 1);
        s_walls[r][c] &= (uint8_t)~(rnd(2) ? W_EAST : W_SOUTH);
    }

    // holes in dead ends (never in the first rows, the last row or on a checkpoint row)
    s_nholes = 0;
    int want = 3 + s_level * 3;
    for (int tries = 0; tries < 2000 && s_nholes < want && s_nholes < MAX_HOLES; tries++) {
        int r = 2 + rnd(rows - 3), c = rnd(COLS);
        if (r % CP_EVERY == 0) {
            continue;
        }
        int walls = 0;
        if (s_walls[r][c] & W_EAST) {
            walls++;
        }
        if (s_walls[r][c] & W_SOUTH) {
            walls++;
        }
        if (c == 0 || (s_walls[r][c - 1] & W_EAST)) {
            walls++;
        }
        if (s_walls[r - 1][c] & W_SOUTH) {
            walls++;
        }
        if (walls < 3) {
            continue;
        }
        int16_t hx = (int16_t)(c * CELL + CELL / 2), hy = (int16_t)(r * CELL + CELL / 2);
        bool dup = false;
        for (int i = 0; i < s_nholes; i++) {
            dup |= (s_hole[i].x == hx && s_hole[i].y == hy);
        }
        if (!dup) {
            s_hole[s_nholes].x = hx;
            s_hole[s_nholes].y = hy;
            s_nholes++;
        }
    }
    s_goal_c = rnd(COLS);
}

static int rows_for_level(int level)
{
    int r = (30 + level * 15) * ROWS_K / ROWS_D;
    return r > MAX_ROWS ? MAX_ROWS : r;
}

// ---------------------------------------------------------------- physics

static float s_impact;

static void push_out(float rx, float ry, float rw, float rh)
{
    float qx = s_bx < rx ? rx : (s_bx > rx + rw ? rx + rw : s_bx);
    float qy = s_by < ry ? ry : (s_by > ry + rh ? ry + rh : s_by);
    float dx = s_bx - qx, dy = s_by - qy;
    float d2 = dx * dx + dy * dy;
    if (d2 >= BALL_R * BALL_R) {
        return;
    }
    float nx, ny, d;
    if (d2 < 1e-6f) {
        // centre inside the wall: push out on the short way
        float l = s_bx - rx, r = rx + rw - s_bx, t = s_by - ry, b = ry + rh - s_by;
        float m = fminf(fminf(l, r), fminf(t, b));
        nx = (m == l) ? -1 : (m == r) ? 1 : 0;
        ny = (nx != 0) ? 0 : ((m == t) ? -1 : 1);
        d = 0;
    } else {
        d = sqrtf(d2);
        nx = dx / d;
        ny = dy / d;
    }
    s_bx += nx * (BALL_R - d);
    s_by += ny * (BALL_R - d);
    float vn = s_vx * nx + s_vy * ny;
    if (vn < 0) {
        s_vx -= 1.4f * vn * nx;  // 0.4 bounce
        s_vy -= 1.4f * vn * ny;
        if (-vn > s_impact) {
            s_impact = -vn;
        }
    }
}

static void collide(void)
{
    int cr = (int)(s_by / CELL), cc = (int)(s_bx / CELL);
    for (int r = cr - 1; r <= cr + 1; r++) {
        if (r < 0 || r >= s_rows) {
            continue;
        }
        for (int c = cc - 1; c <= cc + 1; c++) {
            if (c < 0 || c >= COLS) {
                continue;
            }
            if (s_walls[r][c] & W_EAST) {
                push_out((c + 1) * CELL - WALL / 2.0f, r * CELL - WALL / 2.0f, WALL, CELL + WALL);
            }
            if (s_walls[r][c] & W_SOUTH) {
                push_out(c * CELL - WALL / 2.0f, (r + 1) * CELL - WALL / 2.0f, CELL + WALL, WALL);
            }
        }
    }
    // outer left and top walls
    push_out(-WALL / 2.0f - 20, -40, 20 + WALL, s_rows * CELL + 80);
    push_out(-40, -WALL / 2.0f - 20, COLS * CELL + 80, 20 + WALL);
}

static void start_level(int64_t now)
{
    make_maze(rows_for_level(s_level));
    s_bx = s_rx = CELL / 2.0f + CELL * (COLS / 2);
    s_by = s_ry = CELL / 2.0f;
    s_vx = s_vy = 0;
    s_next_cp = CP_EVERY;
    imu_set_level();
    s_t_start = now;
    s_state = ST_PLAY;
    s_cam = 0;
}

static void physics(float dt, int64_t now)
{
    // Just after the start tap the hand still moves: keep the ball still and
    // take the position as level again.
    if (now - s_t_start < 400) {
        imu_set_level();
        return;
    }
    float gx, gy;
    imu_tilt_logical(&gx, &gy);
    // small dead zone, so the ball can rest
    if (fabsf(gx) < 0.015f) {
        gx = 0;
    }
    if (fabsf(gy) < 0.015f) {
        gy = 0;
    }

    s_vx += gx * ACCEL * dt;
    s_vy += gy * ACCEL * dt;
    float damp = 1.0f - 1.1f * dt;
    s_vx *= damp;
    s_vy *= damp;
    float sp = sqrtf(s_vx * s_vx + s_vy * s_vy);
    if (sp > MAX_SPEED) {
        s_vx *= MAX_SPEED / sp;
        s_vy *= MAX_SPEED / sp;
    }

    int n = (int)ceilf(fmaxf(fabsf(s_vx), fabsf(s_vy)) * dt / 2.0f);
    if (n < 1) {
        n = 1;
    }
    if (n > 16) {
        n = 16;
    }
    s_impact = 0;
    for (int i = 0; i < n; i++) {
        s_bx += s_vx * dt / n;
        s_by += s_vy * dt / n;
        collide();
    }
    if (s_impact > 70 && now - s_last_bump > 70) {
        s_last_bump = now;
        int vol = (int)(s_impact / 3);
        sfx_tone(170, 90, 35, W_TRI, (uint8_t)(vol > 95 ? 95 : vol));
    }

    // checkpoint
    int row = (int)(s_by / CELL);
    if (row >= s_next_cp && s_next_cp < s_rows) {
        s_rx = ((int)(s_bx / CELL)) * CELL + CELL / 2.0f;
        s_ry = s_next_cp * CELL + CELL / 2.0f;
        s_next_cp += CP_EVERY;
        sfx_play(SFX_CHECKPOINT);
    }
    // holes
    for (int i = 0; i < s_nholes; i++) {
        float dx = s_bx - s_hole[i].x, dy = s_by - s_hole[i].y;
        if (dx * dx + dy * dy < (HOLE_R - 2) * (HOLE_R - 2)) {
            s_state = ST_FALL;
            s_fall_at = now;
            s_fall_x = s_hole[i].x;
            s_fall_y = s_hole[i].y;
            sfx_play(SFX_HOLE);
            return;
        }
    }
    // goal
    float gdx = s_bx - (s_goal_c * CELL + CELL / 2.0f), gdy = s_by - ((s_rows - 1) * CELL + CELL / 2.0f);
    if (gdx * gdx + gdy * gdy < (GOAL_R - 2) * (GOAL_R - 2)) {
        s_state = ST_WIN;
        s_t_end = now;
        sfx_play(SFX_WIN);
        if ((uint32_t)s_level > s_best_level) {
            s_best_level = (uint32_t)s_level;
            store_set("tilt_lvl", s_best_level);
        }
    }
}

// ---------------------------------------------------------------- app

static void tilt_init(void)
{
    s_best_level = store_get("tilt_lvl", 0);
    s_text = rgb(255, 250, 235);
    s_accent = rgb(255, 215, 40);
    s_wall = rgb(112, 72, 40);
    s_wall_hi = rgb(160, 112, 66);
    s_shadow = rgb(140, 100, 60);
    s_level = 1;
    make_maze(rows_for_level(1));
    s_bx = CELL / 2.0f + CELL * (COLS / 2);
    s_by = CELL / 2.0f;
    s_state = ST_TITLE;
}

// the settings panel: RESTART (a hold of 1 s, only in a game: the level is lost)
static const pitem_t TLT_ITEMS[] = {
        {PI_ACT2, 0, 0, 0, {"RESTART", "מחדש"}, NULL, NULL},
};
static bool tlt_item_shown(int id)
{
    (void)id;
    return s_state == ST_PLAY;
}
static void tlt_item_set(int id, int v)
{
    (void)id;
    (void)v;
    start_level(s_last);
}
// the panel went away: the board can be in another position now: take it as level
static void tlt_panel_resume(void)
{
    if (s_state == ST_PLAY) {
        imu_set_level();
        s_vx = s_vy = 0;
    }
}
static const pitems_t TLT_PANEL = {TLT_ITEMS, 1, tlt_item_shown, NULL, tlt_item_set, NULL, tlt_panel_resume};

static void tilt_enter(int64_t now)
{
    s_last = now;
    sysui_panel_items(&TLT_PANEL);
    if (s_state == ST_PLAY || s_state == ST_FALL) {
        // the board can be in another position now: take it as level
        imu_set_level();
        s_vx = s_vy = 0;
    }
}

// a word in a Bungee face in the middle (big text and numbers look rough in the dot font); top = the top of
// the capitals
static void tilt_word(const mfont_t *f, int top, const char *t, uint16_t c)
{
    mf_text_oc(f, gfx_w / 2, top + f->asc, t, c, rgb(10, 10, 24), 2, 1);
}

// a line of small text (Rubik) in the middle
static void tilt_line(int top, const char *t, uint16_t c)
{
    mf_text_oc(&MF_LABEL, gfx_w / 2, top + MF_LABEL.asc, t, c, rgb(10, 10, 24), 1, 0);
}

// The two start buttons of the title (only with a cleared level): 0 = LEVEL 1, 1 = the next level after the best
// (so the player does not lose the longer levels at each open of the app).
static void start_button(int i, int *x, int *y, int *w, int *h)
{
#if BOARD_ROUND
    // (the round layout: two buttons side by side in the wide middle of the tray; 0.12 D = 5.3 mm on the 1.75C)
    *y = SCR_H * 61 / 100;
    *h = SCR_H * 12 / 100;
    *w = SCR_W * 31 / 100;
    *x = i ? SCR_W / 2 + SCR_W / 100 : SCR_W / 2 - SCR_W / 100 - *w;
    return;
#endif
#if BOARD_BAR_SCREEN
    *y = 362;
    *h = 56;
#elif BOARD_UI_TABLET
    *y = 430;
    *h = 50;
#else
    *y = 252;
    *h = 48;
#endif
    *w = SCR_W / 2 - 12;
    *x = i ? SCR_W / 2 + 4 : 8;
}

static void draw_start_buttons(int64_t now)
{
#if BOARD_ROUND
    {
        int x, y, w, h;
        if (s_best_level == 0) {
            start_button(0, &x, &y, &w, &h);
            if (((now / 450) & 1) == 0) {
                ui_text_c(&KF_BIG, SCR_W / 2, y + h / 2 + 10, "TAP TO START", s_accent);
            }
            return;
        }
        for (int i = 0; i < 2; i++) {
            start_button(i, &x, &y, &w, &h);
            ui_rbox(x, y, w, h, h / 3, i ? rgb(70, 52, 8) : rgb(40, 30, 20), i ? s_accent : s_wall_hi, 2);
            char one[20];
            snprintf(one, sizeof(one), "LEVEL %lu", i ? (unsigned long)s_best_level + 1 : 1ul);
            ui_text_c(ui_text_w(&KF_BIG, one) <= w - 10 ? &KF_BIG : &KF_TEXT, x + w / 2, y + h / 2 + 9, one,
                      i ? s_accent : s_text);
        }
        return;
    }
#endif
    if (s_best_level == 0) {
        int x, y, w, h;
        start_button(0, &x, &y, &w, &h);
        if (((now / 450) & 1) == 0) {
            tilt_line(y + h / 2 - 7, "TAP TO START", s_accent);
        }
        return;
    }
    for (int i = 0; i < 2; i++) {
        int x, y, w, h;
        start_button(i, &x, &y, &w, &h);
        gfx_fill_rect(x, y, w, h, i ? rgb(70, 52, 8) : rgb(40, 30, 20));
        gfx_frame_rect(x, y, w, h, 2, i ? s_accent : s_wall_hi);
        char num[12];
        snprintf(num, sizeof(num), "%lu", i ? (unsigned long)s_best_level + 1 : 1ul);
        char one[20];
        snprintf(one, sizeof(one), "LEVEL %s", num);
        const mfont_t *bf = &MF_N16;
        if (mf_width(bf, one, 1) <= w - 8) {
            mf_text_oc(bf, x + w / 2, y + h / 2 - 8 + bf->asc, one, s_text, rgb(10, 10, 24), 1, 1);
        } else {
            mf_text_oc(bf, x + w / 2, y + h / 2 - 18 + bf->asc, "LEVEL", s_text, rgb(10, 10, 24), 1, 1);
            mf_text_oc(bf, x + w / 2, y + h / 2 + 1 + bf->asc, num, i ? s_accent : s_text, rgb(10, 10, 24), 1, 1);
        }
    }
}

static void tilt_input(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_TAP:
        if (s_state == ST_TITLE) {
            // LEVEL 1 only on its own button; any other tap goes on from the next level
            int x, y, w, h;
            start_button(0, &x, &y, &w, &h);
            bool first = s_best_level == 0 || (ev->x < x + w + 4 && ev->y >= y - 8 && ev->y < y + h + 8);
            s_level = first ? 1 : (int)s_best_level + 1;
            start_level(ev->t_ms);
            tune_play(TUNE_TILT);
        } else if (s_state == ST_WIN && ev->t_ms - s_t_end > 600) {
            s_level++;
            start_level(ev->t_ms);
            sfx_play(SFX_CLICK);
        }
        break;
    case IN_LONG:
        if (s_state == ST_PLAY) {
            imu_set_level();
            sfx_play(SFX_CLICK);
        }
        break;
    default:
        break;
    }
}

static void tilt_update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.04f) {
        dt = 0.04f;
    }

    if (s_state == ST_PLAY) {
        physics(dt, now);
    } else if (s_state == ST_FALL && now - s_fall_at > 500) {
        s_bx = s_rx;
        s_by = s_ry;
        s_vx = s_vy = 0;
        s_state = ST_PLAY;
    }

    // the camera keeps the ball in the upper part: the way ahead is down
    float want = MAZE_TOP + s_by - CAM_BALL_Y;
    float maxc = MAZE_TOP + s_rows * CELL + 20 - SCR_H;
    if (want > maxc) {
        want = maxc;
    }
    if (want < 0) {
        want = 0;
    }
    s_cam += (want - s_cam) * (dt * 6 > 1 ? 1 : dt * 6);
}

// world y (maze coordinates) to screen y
static inline int sy(float y)
{
    return (int)(MAZE_TOP + y - s_cam);
}

static void draw_maze(int64_t now)
{
    // wooden floor with grain
    for (int y = 0; y < SCR_H; y += 6) {
        uint32_t k = (uint32_t)((y + (int)s_cam) / 6);
        k = (k * 2654435761u) >> 28;  // 0..15
        gfx_fill_rect(0, y, SCR_W, 6, rgb((uint8_t)(184 + k), (uint8_t)(140 + k), (uint8_t)(88 + k / 2)));
    }
    int r0 = (int)((s_cam - MAZE_TOP) / CELL) - 1, r1 = r0 + SCR_H / CELL + 3;
    if (r0 < 0) {
        r0 = 0;
    }
    if (r1 > s_rows) {
        r1 = s_rows;
    }

    // checkpoint lines
    for (int r = CP_EVERY; r < s_rows; r += CP_EVERY) {
        if (r < r0 || r > r1) {
            continue;
        }
        int y = sy((float)r * CELL);
        uint16_t c = (r < s_next_cp) ? rgb(120, 200, 90) : s_accent;
        for (int x = MX; x < MX + COLS * CELL; x += 8) {
            gfx_fill_rect(x, y - 1, 4, 2, c);
        }
    }
    // holes and the goal
    for (int i = 0; i < s_nholes; i++) {
        int y = sy(s_hole[i].y);
        if (y < -12 || y > SCR_H + 12) {
            continue;
        }
        gfx_fill_circle(MX + s_hole[i].x, y, HOLE_R + 1, rgb(90, 60, 30));
        gfx_fill_circle(MX + s_hole[i].x, y, HOLE_R - 1, rgb(10, 6, 4));
    }
    int gx = MX + s_goal_c * CELL + CELL / 2, gy = sy((s_rows - 1) * CELL + CELL / 2.0f);
    gfx_fill_circle(gx, gy, GOAL_R, rgb(30, 120, 50));
    gfx_fill_circle(gx, gy, GOAL_R2, rgb(60, 220, 90));

    // walls: shadow first, then the wall
    for (int pass = 0; pass < 2; pass++) {
        int o = pass ? 0 : 2;
        uint16_t c = pass ? s_wall : s_shadow;
        for (int r = r0; r < r1; r++) {
            int y = sy((float)r * CELL);
            for (int c2 = 0; c2 < COLS; c2++) {
                int x = MX + c2 * CELL;
                if (s_walls[r][c2] & W_EAST) {
                    gfx_fill_rect(x + CELL - WALL / 2 + o, y - WALL / 2 + o, WALL, CELL + WALL, c);
                }
                if (s_walls[r][c2] & W_SOUTH) {
                    gfx_fill_rect(x - WALL / 2 + o, y + CELL - WALL / 2 + o, CELL + WALL, WALL, c);
                }
            }
        }
        int top = sy(0);
        gfx_fill_rect(MX - WALL / 2 + o, top - WALL / 2 + o, WALL, s_rows * CELL + WALL, c);
        gfx_fill_rect(MX - WALL / 2 + o, top - WALL / 2 + o, COLS * CELL + WALL, WALL, c);
    }
    // light top edges
    for (int r = r0; r < r1; r++) {
        int y = sy((float)r * CELL);
        for (int c2 = 0; c2 < COLS; c2++) {
            if (s_walls[r][c2] & W_SOUTH) {
                gfx_fill_rect(MX + c2 * CELL - WALL / 2, y + CELL - WALL / 2, CELL + WALL, 1, s_wall_hi);
            }
        }
    }
}

static void draw_ball(int64_t now)
{
    float x = s_bx, y = s_by;
    int r = BALL_R;
    if (s_state == ST_FALL) {
        float t = (now - s_fall_at) / 500.0f;
        if (t > 1) {
            t = 1;
        }
        x += (s_fall_x - x) * t;
        y += (s_fall_y - y) * t;
        r = (int)(BALL_R * (1 - t)) + 1;
    }
    int bx = MX + (int)x, by = sy(y);
    if (s_state != ST_FALL) {
        gfx_fill_circle(bx + 2, by + 3, r, rgb(110, 76, 44));
    }
    gfx_fill_circle(bx, by, r, rgb(150, 156, 172));
    gfx_fill_circle(bx - 1, by - 1, r - 2 > 0 ? r - 2 : 0, rgb(200, 205, 220));
    gfx_fill_circle(bx - 2, by - 2, r / 3, rgb(255, 255, 255));
}

#if BOARD_ROUND
// the rim of the round tray: dark wood with a light inner edge, and a soft shadow on the floor under it
static void draw_tray(void)
{
    int cx = SCR_W / 2, cy = SCR_H / 2, r = SCR_W / 2, t = (int)(12 * RF);
    ui_ring(cx, cy, r - t - 4, 8, rgb(0, 0, 0), 60);
    ui_ring(cx, cy, r - t / 2, t, s_wall, 255);
    ui_ring(cx, cy, r - t + 1, 2, s_wall_hi, 255);
    ui_ring(cx, cy, r - 2, 2, rgb(70, 44, 24), 255);
}
#endif

static void draw_hud(int64_t now)
{
    char buf[24];
#if BOARD_ROUND
    // the round layout: the level at the upper left of the top arc, the time at the upper right
    gfx_darken_rect(0, 0, SCR_W, HUD_H, 1);
    snprintf(buf, sizeof(buf), "LV %d", s_level);
    ui_text_c(&KF_BIG, SCR_W * 34 / 100, HUD_H - SCR_H * 3 / 100, buf, s_text);
    int64_t tr = (s_state == ST_WIN ? s_t_end : now) - s_t_start;
    snprintf(buf, sizeof(buf), "%d.%d", (int)(tr / 1000), (int)(tr / 100 % 10));
    ui_text_c(&KF_BIG, SCR_W * 66 / 100, HUD_H - SCR_H * 3 / 100, buf, s_text);
    return;
#endif
    gfx_darken_rect(0, 0, SCR_W, HUD_H, 1);
    // (the Bungee faces: the old scale 3 = 21 px, scale 2 = 14 px)
    const mfont_t *hf = HUD_S >= 3 ? &MF_N20 : &MF_N16;
    snprintf(buf, sizeof(buf), "LV %d", s_level);
    mf_text_o(hf, 6, HUD_TY + hf->asc, buf, s_text, rgb(10, 10, 24), 1, 1);
    int64_t t = (s_state == ST_WIN ? s_t_end : now) - s_t_start;
    snprintf(buf, sizeof(buf), "%d.%d", (int)(t / 1000), (int)(t / 100 % 10));
    mf_text_o(hf, SCR_W - 6 - mf_width(hf, buf, 1), HUD_TY + hf->asc, buf, s_text, rgb(10, 10, 24), 1, 1);
}

static void tilt_render(int64_t now)
{
    draw_maze(now);
    if (s_state != ST_TITLE) {
        draw_ball(now);
        draw_hud(now);
    }
#if BOARD_ROUND
    char buf[24];
    int D = SCR_W, cx = D / 2;
    if (s_state == ST_TITLE) {
        gfx_darken_rect(0, D * 8 / 100, SCR_W, D * 72 / 100, 2);
        ui_text_c(&KF_TITLE, cx, D * 20 / 100, "TILT", s_accent);
        ui_text_c(&KF_TEXT, cx, D * 27 / 100, "TILT TO ROLL THE BALL", s_text);
        ui_text_c(&KF_TEXT, cx, D * 32 / 100, "TO THE GREEN END", s_text);
        ui_text_c(&KF_TEXT, cx, D * 38 / 100, "AVOID THE HOLES", rgb(255, 140, 120));
        ui_text_c(&KF_LABEL, cx, D * 44 / 100, "HOLD THE BOARD AS YOU LIKE:", s_text);
        ui_text_c(&KF_LABEL, cx, D * 48 / 100, "THAT IS FLAT", s_text);
        snprintf(buf, sizeof(buf), "BEST LEVEL %lu", (unsigned long)s_best_level);
        ui_text_c(&KF_TEXT, cx, D * 56 / 100, buf, s_accent);
        draw_start_buttons(now);
        if (!imu_ok()) {
            ui_text_c(&KF_TEXT, cx, D * 85 / 100, "NO IMU FOUND", rgb(255, 80, 80));
        }
    } else if (s_state == ST_WIN) {
        gfx_darken_rect(0, D * 33 / 100, SCR_W, D * 32 / 100, 2);
        ui_text_c(&KF_TITLE, cx, D * 44 / 100, "CLEAR!", rgb(90, 230, 110));
        int64_t t = s_t_end - s_t_start;
        snprintf(buf, sizeof(buf), "TIME %d.%d", (int)(t / 1000), (int)(t / 100 % 10));
        ui_text_c(&KF_BIG, cx, D * 52 / 100, buf, s_text);
        if (now - s_t_end > 600 && ((now / 450) & 1) == 0) {
            ui_text_c(&KF_TEXT, cx, D * 60 / 100, "TAP: NEXT LEVEL", s_accent);
        }
    }
    draw_tray();  // (over the dark bands of the texts: the rim stays whole)
#elif BOARD_BAR_SCREEN
    char buf[24];
    if (s_state == ST_TITLE) {
        // (at most 14 letters of size 2 in a line: the 172 px width)
        gfx_darken_rect(0, 90, SCR_W, 340, 2);
        tilt_word(&MF_N34, 110, "TILT", s_accent);
        tilt_line(170, "TILT TO ROLL", s_text);
        tilt_line(190, "THE BALL TO", s_text);
        tilt_line(210, "THE GREEN END", s_text);
        tilt_line(238, "MISS THE HOLES", rgb(255, 140, 120));
        tilt_line(266, "HOLD THE BOARD", s_text);
        tilt_line(286, "AS YOU LIKE:", s_text);
        tilt_line(306, "THAT IS FLAT", s_text);
        snprintf(buf, sizeof(buf), "BEST LEVEL %lu", (unsigned long)s_best_level);
        tilt_line(334, buf, s_accent);
        draw_start_buttons(now);
        if (!imu_ok()) {
            tilt_line(60, "NO IMU FOUND", rgb(255, 80, 80));
        }
    } else if (s_state == ST_WIN) {
        gfx_darken_rect(0, 200, SCR_W, 150, 2);
        tilt_word(&MF_N28, 215, "CLEAR!", rgb(90, 230, 110));
        int64_t t = s_t_end - s_t_start;
        snprintf(buf, sizeof(buf), "TIME %d.%d", (int)(t / 1000), (int)(t / 100 % 10));
        tilt_line(265, buf, s_text);
        if (now - s_t_end > 600 && ((now / 450) & 1) == 0) {
            tilt_line(305, "TAP: NEXT LEVEL", s_accent);
        }
    }
#elif BOARD_UI_TABLET
    // Tab5: the CoreS3 texts, 1.5 times bigger and lower
    char buf[24];
    if (s_state == ST_TITLE) {
        gfx_darken_rect(0, 60, SCR_W, 420, 2);
        tilt_word(&MF_N34, 80, "TILT", s_accent);
        tilt_word(&MF_N20, 162, "TILT THE BOARD", s_text);
        tilt_word(&MF_N20, 192, "TO ROLL THE BALL", s_text);
        tilt_word(&MF_N20, 222, "TO THE GREEN END", s_text);
        tilt_word(&MF_N20, 264, "AVOID THE HOLES", rgb(255, 140, 120));
        tilt_word(&MF_N20, 310, "HOLD THE BOARD", s_text);
        tilt_word(&MF_N20, 340, "AS YOU LIKE:", s_text);
        tilt_word(&MF_N20, 370, "THAT IS FLAT", s_text);
        snprintf(buf, sizeof(buf), "BEST LEVEL %lu", (unsigned long)s_best_level);
        tilt_line(404, buf, s_accent);
        draw_start_buttons(now);
        if (!imu_ok()) {
            tilt_word(&MF_N20, 20, "NO IMU FOUND", rgb(255, 80, 80));
        }
    } else if (s_state == ST_WIN) {
        gfx_darken_rect(0, 200, SCR_W, 180, 2);
        tilt_word(&MF_N34, 218, "CLEAR!", rgb(90, 230, 110));
        int64_t t = s_t_end - s_t_start;
        snprintf(buf, sizeof(buf), "TIME %d.%d", (int)(t / 1000), (int)(t / 100 % 10));
        tilt_word(&MF_N20, 286, buf, s_text);
        if (now - s_t_end > 600 && ((now / 450) & 1) == 0) {
            tilt_word(&MF_N20, 334, "TAP: NEXT LEVEL", s_accent);
        }
    }
#else
    char buf[24];
    if (s_state == ST_TITLE) {
        gfx_darken_rect(0, 28, SCR_W, 280, 2);
        tilt_word(&MF_N34, 36, "TILT", s_accent);
        tilt_line(86, "TILT THE BOARD", s_text);
        tilt_line(104, "TO ROLL THE BALL", s_text);
        tilt_line(122, "TO THE GREEN END", s_text);
        tilt_line(146, "AVOID THE HOLES", rgb(255, 140, 120));
        tilt_line(170, "HOLD THE BOARD", s_text);
        tilt_line(188, "AS YOU LIKE:", s_text);
        tilt_line(206, "THAT IS FLAT", s_text);
        snprintf(buf, sizeof(buf), "BEST LEVEL %lu", (unsigned long)s_best_level);
        tilt_line(228, buf, s_accent);
        draw_start_buttons(now);
        if (!imu_ok()) {
            tilt_line(8, "NO IMU FOUND", rgb(255, 80, 80));
        }
    } else if (s_state == ST_WIN) {
        gfx_darken_rect(0, 104, SCR_W, 120, 2);
        tilt_word(&MF_N28, 116, "CLEAR!", rgb(90, 230, 110));
        int64_t t = s_t_end - s_t_start;
        snprintf(buf, sizeof(buf), "TIME %d.%d", (int)(t / 1000), (int)(t / 100 % 10));
        tilt_line(160, buf, s_text);
        if (now - s_t_end > 600 && ((now / 450) & 1) == 0) {
            tilt_line(192, "TAP: NEXT LEVEL", s_accent);
        }
    }
#endif
}

// ---------------------------------------------------------------- the menu icon (smooth shapes of tfdraw)
#include "tfdraw/tfdraw.h"

// the place of the icon ball on its way through the maze (board units, the middle is 0, 0) at the part f (0..1)
static void tilt_icon_path(float f, float *px, float *py)
{
    static const float P[6][2] = {{-27, -26}, {27, -26}, {27, 0}, {-27, 0}, {-27, 26}, {22, 26}};
    static const float LEN[5] = {54, 26, 54, 26, 49};
    float d = f * 209;
    for (int i = 0; i < 5; i++) {
        if (d <= LEN[i] || i == 4) {
            float k = fminf(d / LEN[i], 1.0f);
            *px = P[i][0] + (P[i + 1][0] - P[i][0]) * k;
            *py = P[i][1] + (P[i + 1][1] - P[i][1]) * k;
            return;
        }
        d -= LEN[i];
    }
}
// THE ICON: the wooden maze; the steel ball rolls along the way past two holes to the green end, drops in, and
// comes again at the start. A loop of 4 s.
static void tilt_icon(int x, int y, int s)
{
    float t = icon_time(), u = s / 100.0f, p = fmodf(t, 4.0f);
    float f = p < 3.3f ? p / 3.3f : 1.0f;
    f = f * f * (3 - 2 * f) * 0.6f + f * 0.4f;  // (slow at the start and at the end)
    float bxw, byw;
    tilt_icon_path(f, &bxw, &byw);
    float cx = x + 50 * u, cy = y + 50 * u;
#define TLT_P(bx_, by_, ox, oy) float ox = cx + (bx_) * u, oy = cy + (by_) * u
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
    // the board (flat boxes: cheap): the table, a dark frame, the wood with its grain
    tfd_rect(x, y, s, s, rgb(40, 30, 26), 256);
    tfd_rrect(x + (int)(4 * u), y + (int)(4 * u), (int)(92 * u), (int)(92 * u), (int)(4 * u), rgb(122, 78, 42), 256);
    tfd_rect(x + (int)(8 * u), y + (int)(8 * u), (int)(84 * u), (int)(84 * u), rgb(204, 160, 102), 256);
    for (int k = 0; k < 5; k++) {
        tfd_rect(x + (int)(8 * u), y + (int)((16 + k * 17) * u), (int)(84 * u), (int)(0.9f * u + 0.5f),
                 rgb(184, 138, 84), 256);
    }
    // the walls: a dark side and a light top
    static const float TI_WALL[2][4] = {{-42, -13.5f, 14, -13.5f}, {-14, 13.5f, 42, 13.5f}};
    for (int k = 0; k < 2; k++) {
        float wx0 = cx + TI_WALL[k][0] * u, wy = cy + TI_WALL[k][1] * u, len = (TI_WALL[k][2] - TI_WALL[k][0]) * u;
        tfd_rect((int)(wx0 + 0.8f * u), (int)(wy - 1.5f * u), (int)len, (int)(7 * u), rgb(92, 56, 28), 256);
        tfd_rect((int)wx0, (int)(wy - 3 * u), (int)len, (int)(6 * u), rgb(150, 100, 56), 256);
        tfd_rect((int)wx0, (int)(wy - 3 * u), (int)len, (int)(1.6f * u + 0.5f), rgb(230, 190, 136), 256);
    }
    // the holes (the ball goes past them) and the green end
    for (int k = 0; k < 2; k++) {
        TLT_P(k ? 4 : -8, k ? 37.5f : -37.5f, hx, hy);
        tfd_disc(hx, hy, 5.4f * u, rgb(232, 196, 140), 256);
        tfd_disc(hx, hy, 4.7f * u, rgb(20, 12, 8), 256);
        tfd_disc(hx - 0.7f * u, hy - 0.8f * u, 3.4f * u, rgb(52, 32, 20), 256);
    }
    TLT_P(27, 26, ex, ey);
    tfd_disc(ex, ey, 8 * u, rgb(30, 120, 50), 256);
    tfd_disc(ex, ey, 6.5f * u, rgb(70, 220, 90), 256);
    tfd_disc(ex - 1.6f * u, ey - 1.6f * u, 2.6f * u, rgb(200, 255, 200), 200);
    // the ball: its shadow, the steel and the light; it gets small at the end (it drops into the green end)
    float sc = p < 3.3f ? 1 : p < 3.6f ? 1 - (p - 3.3f) / 0.3f * 0.6f : 0;
    if (p > 3.85f) {
        sc = (p - 3.85f) / 0.15f;
    }
    if (p > 3.3f && p < 3.6f) {
        bxw += (27 - bxw) * (p - 3.3f) / 0.3f;
    }
    TLT_P(bxw, byw, bx, by);
    float br = 5.6f * u * sc;
    if (br > 0.5f) {
        tfd_disc(bx + 2 * u, by + 2.4f * u, br, rgb(0, 0, 0), 90);
        tfd_disc(bx, by, br, rgb(70, 78, 92), 256);
        tfd_disc(bx - br * 0.12f, by - br * 0.12f, br * 0.82f, rgb(170, 178, 196), 256);
        tfd_disc(bx - br * 0.36f, by - br * 0.38f, br * 0.34f, rgb(255, 255, 255), 240);
    }
#undef TLT_P
    tfd_noclip();
}

const app_t app_tilt = {
        .name = "TILT",
        .landscape = false,
        .round = APP_ROUND_HI,  // the round frame of the panel: a round tray
        .needs = APP_NEEDS_IMU,
        .init = tilt_init,
        .enter = tilt_enter,
        .input = tilt_input,
        .update = tilt_update,
        .render = tilt_render,
        .draw_icon = tilt_icon,
};
