// SEA BATTLE: the pencil-and-paper sea battle on two boards
// (friends; duel.c: an invite, a join, the messages in order) or against the computer. A grid of 10 x 10, a fleet of
// five: CARRIER 5, BATTLESHIP 4, CRUISER 3, SUBMARINE 3, DESTROYER 2. First the fleet goes on the sea (SHUFFLE for a new
// place, a ship can turn), then a shot each in turn: MISS, HIT, or SUNK (the other side then sees the whole ship). All
// five sunk: the end. Each board keeps its own fleet and answers the shots at it.
// The computer: it shoots at random (on a chess pattern: a ship of 2 or more always has a square of each colour) until
// a hit; then it looks round the hit and goes on along the line.
// The screens: the 3.49 both grids (YOUR FLEET at the left, ENEMY WATERS at the right) and the panel between them; the
// CoreS3 both grids under the panel; the round boards (the 1.75C, the Watcher) one grid at a time (yours when the other
// side shoots) in a porthole on the sea, with touch buttons (the 1.75C has no wheel).
// THE THUMB: a square of the small grids is about 2 mm, a thumb is about 10 mm, and a thumb lands 2-3 mm from
// the point that the person wants. So no tap fires at once. A shot has three steps:
//   1. a tap on the enemy grid opens THE LOUPE: the squares round the tap, big (3.49: 3 x 5 of 54 px = 7.2 mm; the
//      CoreS3 frame: 3 x 3 of 64 px = 7.1 mm on the AMOLED, 8.1 mm on the CoreS3; Watcher: 3 x 3 of 80 px = 7.2 mm);
//   2. a tap on a big square AIMS there (a yellow frame, and its name on FIRE). A drag on the loupe moves it (the sea
//      follows the finger). A tap or a drag on the small grid moves it there. The Watcher wheel moves the aim;
//   3. FIRE (the Watcher: a push of the wheel) fires at the aim. FIRE is 5 mm or more from all the zones of the aim.
// The touch: the app acts when the finger comes off the glass, at the mean point of the touch (the first point is often
// at the edge of the finger). A touch that starts just after a tap changed the screen does nothing (a bounce or a double
// tap: THE GUARD). The placing: a tap picks the nearest ship (a frame; its name on TURN), and TURN turns it (the Watcher:
// a tap or the wheel picks a ship, SHUFFLE or READY, and a push does it). The buttons are 6.5 mm or more, with 2 mm or
// more between the tap zones of two buttons.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/duel.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "tfn/friends.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/mfont.h"
#include "miniz.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "jingle/jingle.h"
// the opening of SEA BATTLE: a bugle call of the navy in G (the notes of the bugle: G C E G), a
// low ship horn under it, and two far guns
static const tune_note_t SB_OPEN[] = {
        JN(0, 120, J_G4, W_PULSE, 22),
        JN(140, 120, J_G4, W_PULSE, 22),
        JN(280, 240, J_C5, W_PULSE, 24),
        JN(540, 120, J_E5, W_PULSE, 22),
        JN(680, 120, J_C5, W_PULSE, 22),
        JN(820, 600, J_G5, JHOLD(W_PULSE), 24),
        JN(0, 700, J_G2, JHOLD(W_SAW), 14),
        JN(820, 600, J_C3, JHOLD(W_TRI), 40),
        JTOM(300, J_D3),
        JTOM(820, J_G2),
        JCRASH(820),
};

#define N     10
#define NSHIP 5
static const int SHIP_LEN[NSHIP] = {5, 4, 3, 3, 2};
static const char *const SHIP_NAME[NSHIP] = {"CARRIER", "BATTLESHIP", "CRUISER", "SUBMARINE", "DESTROYER"};
enum {
    G_TITLE,
    G_WAIT,
    G_PLACE,
    G_PLAY,
    G_OVER
};
enum {
    M_READY = 1,
    M_SHOT,
    M_RES
};
enum {
    R_MISS,
    R_HIT,
    R_SUNK
};
enum {
    FOCUS_SHUFFLE = NSHIP,
    FOCUS_READY,
    NFOCUS
};  // the Watcher placing: the wheel picks a ship, SHUFFLE or READY
typedef struct {
    int8_t x, y, dir, len;
    uint8_t hits;
} ship_t;  // dir 0: along x, 1: along y
typedef struct {
    ship_t ship[NSHIP];
    int8_t at[N][N];     // the ship in each cell (-1: sea)
    uint8_t shot[N][N];  // the shots at this sea: 0 none, 1 miss, 2 hit
} sea_t;
static struct {
    int g;
    float gt;
    bool vs_cpu;
    sea_t me, foe;  // foe: what we know of the other sea (its ships: only the sunk ones)
    int foe_sunk, me_sunk;
    int8_t foe_kind[NSHIP];  // the kind of each sunk enemy ship (the order of foe.ship: the order they sank)
    bool my_turn, my_ready, foe_ready, waiting;  // waiting: a shot is out, the answer not yet here
    int sx, sy;                                  // the last shot (an animation at it)
    float shot_t;                                // the time since the last shot (any side)
    bool shot_mine;
    int shot_res;
    char msg[48];
    float msg_t;
    int won;  // 1 won, 0 lost, -1 the friend left
    // the computer
    float cpu_t;
    int hunt[N * N], nhunt;
    int wins, losses;
    int cur;  // the Watcher: the cursor of the wheel (y * N + x)
    // the loupe
    bool lp_on, lp_hold;  // open; hold: it shows the shot that went out (no input)
    int lp_x, lp_y;       // the first square that it shows (the top left)
    int aim, hover;       // the aim (y * N + x; -1: none); hover: the square under a finger on the loupe
    // the placing: the picked ship (-1: none; the Watcher also FOCUS_SHUFFLE, FOCUS_READY); a shake when it cannot turn
    int pick;
    bool pick_ok;
    float pick_t;
} B;
static int64_t s_last;

// ---------------------------------------------------------------- the rules
static int clampi(int v, int a, int b)
{
    return v < a ? a : v > b ? b : v;
}
static bool fits(const sea_t *s, int x, int y, int dir, int len, int skip)
{
    for (int k = 0; k < len; k++) {
        const int cx = x + (dir ? 0 : k), cy = y + (dir ? k : 0);
        if (cx < 0 || cy < 0 || cx >= N || cy >= N) {
            return false;
        }
        if (s->at[cy][cx] >= 0 && s->at[cy][cx] != skip) {
            return false;
        }
    }
    return true;
}
static void put_ship(sea_t *s, int i)
{
    const ship_t *p = &s->ship[i];
    for (int k = 0; k < p->len; k++) {
        s->at[p->y + (p->dir ? k : 0)][p->x + (p->dir ? 0 : k)] = (int8_t)i;
    }
}
static void clear_sea(sea_t *s)
{
    memset(s, 0, sizeof *s);
    memset(s->at, -1, sizeof s->at);
}
static uint32_t rnd(uint32_t n)
{
    return esp_random() % n;
}
static void shuffle(sea_t *s)
{
    clear_sea(s);
    for (int i = 0; i < NSHIP; i++) {
        for (int tries = 0; tries < 500; tries++) {
            const int dir = (int)rnd(2), len = SHIP_LEN[i], x = (int)rnd(dir ? N : N - len + 1),
                      y = (int)rnd(dir ? N - len + 1 : N);
            if (fits(s, x, y, dir, len, -1)) {
                s->ship[i] = (ship_t){(int8_t)x, (int8_t)y, (int8_t)dir, (int8_t)len, 0};
                put_ship(s, i);
                break;
            }
        }
    }
}
// turn ship i about its first cell (if it fits)
static bool turn_ship(sea_t *s, int i)
{
    ship_t *p = &s->ship[i];
    if (!fits(s, p->x, p->y, !p->dir, p->len, i)) {
        return false;
    }
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            if (s->at[y][x] == i) {
                s->at[y][x] = -1;
            }
        }
    }
    p->dir = (int8_t)!p->dir;
    put_ship(s, i);
    return true;
}
// a shot at my sea: the answer
static int take_shot(int x, int y, int *ship)
{
    *ship = -1;
    if (B.me.shot[y][x]) {
        return B.me.shot[y][x] == 2 ? R_HIT : R_MISS;
    }
    const int i = B.me.at[y][x];
    if (i < 0) {
        B.me.shot[y][x] = 1;
        return R_MISS;
    }
    B.me.shot[y][x] = 2;
    B.me.ship[i].hits++;
    *ship = i;
    if (B.me.ship[i].hits >= B.me.ship[i].len) {
        B.me_sunk++;
        return R_SUNK;
    }
    return R_HIT;
}

// ---------------------------------------------------------------- the sounds
static const tune_note_t TUNE_SB_WIN[] = {{0, 140, 67, 0, W_PULSE, 36},
                                          {150, 140, 72, 0, W_PULSE, 36},
                                          {300, 140, 76, 0, W_PULSE, 36},
                                          {450, 600, 79, 0, W_PULSE | TUNE_HOLD, 38},
                                          {0, 1000, 43, 0, W_TRI | TUNE_HOLD, 50}};
static const tune_note_t TUNE_SB_LOSE[] = {{0, 240, 64, 0, W_SQUARE, 32},
                                           {250, 240, 60, 0, W_SQUARE, 32},
                                           {500, 700, 55, -1, W_SQUARE | TUNE_HOLD, 32},
                                           {0, 1100, 36, 0, W_TRI | TUNE_HOLD, 50}};
static const tune_note_t TUNE_SB_SUNK[] = {{0, 260, 43, 0, W_SQUARE, 40}, {280, 600, 38, 0, W_SQUARE | TUNE_HOLD, 40}};
#define TN(t) ((int)(sizeof(t) / sizeof((t)[0])))
static void say(const char *s)
{
    snprintf(B.msg, sizeof B.msg, "%s", s);
    B.msg_t = 2.5f;
}
static void result_sound(int r)
{
    if (r == R_MISS) {
        sfx_tone(2200, 900, 220, W_NOISE, 30);
    } else if (r == R_HIT) {
        sfx_tone(220, 60, 420, W_NOISE, 60);
        sfx_tone(90, 40, 300, W_SQUARE, 30);
    } else {
        audio_tune(TUNE_SB_SUNK, TN(TUNE_SB_SUNK));
    }
}
// "no": a square that was shot, FIRE with no aim, a ship that cannot turn
static void no_sound(void)
{
    sfx_tone(160, 110, 80, W_SQUARE, 24);
}

// ---------------------------------------------------------------- the match
static void lp_close(void)
{
    B.lp_on = B.lp_hold = false;
    B.aim = B.hover = -1;
}
static void end_match(int won)
{
    B.g = G_OVER;
    B.gt = 0;
    B.won = won;
    lp_close();
    if (won == 1) {
        audio_tune(TUNE_SB_WIN, TN(TUNE_SB_WIN));
    } else if (won == 0) {
        audio_tune(TUNE_SB_LOSE, TN(TUNE_SB_LOSE));
    }
    if (B.vs_cpu && won >= 0) {
        if (won) {
            B.wins++;
        } else {
            B.losses++;
        }
        store_set("sb_w", (uint32_t)B.wins);
        store_set("sb_l", (uint32_t)B.losses);
    }
    // (a match of boards: the line stays on until the end screen closes: the last answer must go out)
}
static void start_place(bool cpu)
{
    B.vs_cpu = cpu;
    clear_sea(&B.foe);
    shuffle(&B.me);
    B.foe_sunk = B.me_sunk = 0;
    B.my_ready = B.foe_ready = B.waiting = false;
    B.shot_t = 99;
    B.msg_t = 0;
    B.g = G_PLACE;
    B.gt = 0;
    B.cur = 44;
    lp_close();
    B.pick = -1;
    B.pick_t = 0;
    sfx_play(SFX_CLICK);
}
static void start_play(void)
{
    B.g = G_PLAY;
    B.gt = 0;
    B.pick = -1;
    lp_close();
    B.my_turn = B.vs_cpu ? true : duel_host();
    say(B.my_turn ? "YOU SHOOT FIRST" : "THE ENEMY SHOOTS FIRST");
    if (B.vs_cpu) {
        // the computer's fleet (its own sea, here) and its hunt list (a chess pattern, at random)
        B.nhunt = 0;
        for (int i = 0; i < N * N; i++) {
            if (((i % N) + (i / N)) % 2 == 0) {
                B.hunt[B.nhunt++] = i;
            }
        }
        for (int i = B.nhunt - 1; i > 0; i--) {
            const int j = (int)rnd((uint32_t)i + 1), t = B.hunt[i];
            B.hunt[i] = B.hunt[j];
            B.hunt[j] = t;
        }
    }
}
static sea_t s_cpu;  // the computer's sea (vs the computer)
// my shot at the enemy sea (false: no shot went out)
static bool fire(int x, int y)
{
    if (!B.my_turn || B.waiting || B.foe.shot[y][x]) {
        return false;
    }
    if (!B.vs_cpu) {
        const uint8_t m[3] = {M_SHOT, (uint8_t)x, (uint8_t)y};
        if (!duel_send(m, 3)) {
            return false;
        }
        B.waiting = true;
    }
    B.sx = x;
    B.sy = y;
    B.shot_t = 0;
    B.shot_mine = true;
    sfx_tone(1800, 500, 380, W_SQUARE, 22);  // (the whistle of the shell)
    if (B.vs_cpu) {
        const int i = s_cpu.at[y][x];
        int r = R_MISS;
        if (i >= 0) {
            s_cpu.ship[i].hits++;
            r = s_cpu.ship[i].hits >= s_cpu.ship[i].len ? R_SUNK : R_HIT;
        }
        B.foe.shot[y][x] = r == R_MISS ? 1 : 2;
        if (r == R_SUNK) {
            B.foe.ship[B.foe_sunk] = s_cpu.ship[i];
            B.foe_kind[B.foe_sunk] = (int8_t)i;
            B.foe_sunk++;
            char b[48];
            snprintf(b, sizeof b, "YOU SANK THE %s", SHIP_NAME[i]);
            say(b);
        }
        B.shot_res = r;
        result_sound(r);
        B.my_turn = false;
        B.cpu_t = 0;
        if (B.foe_sunk == NSHIP) {
            end_match(1);
        }
    }
    return true;
}
// the computer shoots: around a hit that is not sunk yet, else the hunt list
static void cpu_fire(void)
{
    int pick = -1;
    // a hit cell of a ship not sunk: its neighbours (along a line of hits first)
    for (int y = 0; y < N && pick < 0; y++) {
        for (int x = 0; x < N && pick < 0; x++) {
            if (B.me.shot[y][x] != 2) {
                continue;
            }
            const int i = B.me.at[y][x];
            if (B.me.ship[i].hits >= B.me.ship[i].len) {
                continue;
            }
            static const int8_t D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (int d = 0; d < 4 && pick < 0; d++) {
                // along a line: past the hits in this way
                int cx = x, cy = y;
                const bool line = (x + D[d][0] >= 0 && x + D[d][0] < N && y + D[d][1] >= 0 && y + D[d][1] < N &&
                                   B.me.shot[y + D[d][1]][x + D[d][0]] == 2) ||
                                  (x - D[d][0] >= 0 && x - D[d][0] < N && y - D[d][1] >= 0 && y - D[d][1] < N &&
                                   B.me.shot[y - D[d][1]][x - D[d][0]] == 2);
                if (!line) {
                    continue;
                }
                while (cx >= 0 && cy >= 0 && cx < N && cy < N && B.me.shot[cy][cx] == 2) {
                    cx += D[d][0];
                    cy += D[d][1];
                }
                if (cx >= 0 && cy >= 0 && cx < N && cy < N && !B.me.shot[cy][cx]) {
                    pick = cy * N + cx;
                }
            }
            for (int d = 0; d < 4 && pick < 0; d++) {
                const int cx = x + D[d][0], cy = y + D[d][1];
                if (cx >= 0 && cy >= 0 && cx < N && cy < N && !B.me.shot[cy][cx]) {
                    pick = cy * N + cx;
                }
            }
        }
    }
    while (pick < 0 && B.nhunt > 0) {
        const int c = B.hunt[--B.nhunt];
        if (!B.me.shot[c / N][c % N]) {
            pick = c;
        }
    }
    for (int c = 0; pick < 0 && c < N * N; c++) {
        if (!B.me.shot[c / N][c % N]) {
            pick = c;
        }
    }
    if (pick < 0) {
        return;
    }
    int ship;
    const int r = take_shot(pick % N, pick / N, &ship);
    B.sx = pick % N;
    B.sy = pick / N;
    B.shot_t = 0;
    B.shot_mine = false;
    B.shot_res = r;
    result_sound(r);
    if (r == R_SUNK) {
        char b[48];
        snprintf(b, sizeof b, "YOUR %s IS SUNK", SHIP_NAME[ship]);
        say(b);
    }
    B.my_turn = true;
    if (B.me_sunk == NSHIP) {
        end_match(0);
    }
}
// the messages of the friend
static void net_step(void)
{
    uint8_t m[DUEL_MSG_MAX];
    int n;
    while (duel_recv(m, &n)) {
        if (n < 1) {
            continue;
        }
        if (m[0] == M_READY) {
            B.foe_ready = true;
            if (B.my_ready && B.g == G_PLACE) {
                start_play();
            }
        } else if (m[0] == M_SHOT && n >= 3 && m[1] < N && m[2] < N && B.g == G_PLAY) {
            int ship;
            const int r = take_shot(m[1], m[2], &ship);
            B.sx = m[1];
            B.sy = m[2];
            B.shot_t = 0;
            B.shot_mine = false;
            B.shot_res = r;
            result_sound(r);
            uint8_t a[9] = {M_RES,
                            m[1],
                            m[2],
                            (uint8_t)r,
                            (uint8_t)(ship < 0 ? 255 : ship),
                            0,
                            0,
                            0,
                            (uint8_t)(B.me_sunk == NSHIP)};
            if (r == R_SUNK) {
                const ship_t *p = &B.me.ship[ship];
                a[5] = (uint8_t)p->x;
                a[6] = (uint8_t)p->y;
                a[7] = (uint8_t)p->dir;
                char b[48];
                snprintf(b, sizeof b, "YOUR %s IS SUNK", SHIP_NAME[ship]);
                say(b);
            }
            duel_send(a, 9);
            B.my_turn = true;
            if (B.me_sunk == NSHIP) {
                end_match(0);
            }
        } else if (m[0] == M_RES && n >= 9 && B.g == G_PLAY && B.waiting) {
            const int x = m[1], y = m[2], r = m[3];
            if (x >= N || y >= N) {
                continue;
            }
            B.waiting = false;
            B.foe.shot[y][x] = r == R_MISS ? 1 : 2;
            B.shot_res = r;
            result_sound(r);
            if (B.shot_t > 0.35f) {
                B.shot_t = 0.35f;  // (the splash or the blast starts now, in the right colour)
            }
            if (r == R_SUNK && m[4] < NSHIP && B.foe_sunk < NSHIP) {
                B.foe.ship[B.foe_sunk] = (ship_t){(int8_t)m[5], (int8_t)m[6], (int8_t)m[7], (int8_t)SHIP_LEN[m[4]], 0};
                B.foe_kind[B.foe_sunk] = (int8_t)m[4];
                B.foe_sunk++;
                char b[48];
                snprintf(b, sizeof b, "YOU SANK THE %s", SHIP_NAME[m[4]]);
                say(b);
            }
            B.my_turn = false;
            if (m[8]) {
                end_match(1);
            }
        }
    }
}

// ---------------------------------------------------------------- the layout
// The sizes are in mm of the glass. One px: 3.49 0.134 mm, CoreS3 0.127 mm, AMOLED 0.111 mm (it shows the CoreS3 frame
// x 1.4, so it sets the sizes of that frame), Watcher 0.089 mm.
typedef struct {
    int x, y, c, my;
} grid_t;  // the top left and the cell size of a grid on the screen; my: the marks are
           // my px lower (the loupe: under the name of the square)
typedef struct {
    int x, y, w, h;
} rect_t;
// THE LOUPE: LP_ROWS x LP_COLS big squares of LP_C px at (LP_X, LP_Y). Its tap zone LPZ_* goes 4 px round it, and to the
// edges of the screen where no other target is (such a tap hits the edge square). LP_TAG: the face of the name of each
// big square ("C4": the row C, the column 4; about 1.5 mm), LP_INK the height of its capitals; LP_MY: the marks of the
// loupe are that much lower (under the name). LBL_F: the face of the row letters and the column numbers of the enemy
// grid (1 mm or more). FIRE_R: the FIRE button (x, y, w, h), 5 mm or more from all the zones of the aim. SLOP: a touch
// that moves more is a drag (2.5 mm). GUARD_R: the radius of the guard round the tap that changed the screen (6 mm).
#if BOARD_BAR_SCREEN
enum {
    LP_COLS = 5,
    LP_ROWS = 3,
    LP_C = 54,
    LP_X = 165,
    LP_Y = 5
};  // (the middle: FIRE is at the left)
enum {
    LPZ_X0 = LP_X - 4,
    LPZ_X1 = LP_X + LP_COLS * LP_C + 4,
    LPZ_Y0 = 0,
    LPZ_Y1 = 172
};
#define FIRE_R 8, 5, 110, 162
#define FIRE_Z 0, 0, 120, 172  // (its tap zone: to the left, top and bottom edges)
#define LP_TAG MF_LABEL
#define LBL_F  MF_SMALL
enum {
    LP_INK = 11,
    LP_MY = 7,
    SLOP = 19,
    GUARD_R = 45
};
#elif BOARD_ROUND
// THE ROUND LAYOUT (APP_ROUND_HI: the 466 frame of the AMOLED-1.75C, the 412 frame of the Watcher; all
// sizes from gfx_w). The circle is a porthole on the sea. The aim screen, from the top: the small map (SM_C squares),
// the loupe (3 x 3 of LP_C: 74 px = 7 mm on the 1.75C), the line of the aim, FIRE (FIRE_GAP under the zone of the
// loupe: 56 px = 5.3 mm). The 1.75C has no wheel: FIRE, TURN, SHUFFLE and READY are touch buttons (the Watcher keeps
// its wheel too). GC: the squares of the grid of the play (a tap opens the loupe there), PC: the squares of the fleet
// in the placing.
enum {
    LP_COLS = 3,
    LP_ROWS = 3
};
#define SM_C     (gfx_w / 66)
#define SM_Y     (gfx_w * 5 / 100)
#define LP_C     (gfx_w * 16 / 100)
#define LP_X     ((gfx_w - LP_COLS * LP_C) / 2)
#define LP_Y     (SM_Y + N * SM_C + gfx_w / 40)
#define LPZ_X0   (LP_X - 4)
#define LPZ_X1   (LP_X + LP_COLS * LP_C + 4)
#define LPZ_Y0   (LP_Y - 4)
#define LPZ_Y1   (LP_Y + LP_ROWS * LP_C + 4)
#define FIRE_GAP (gfx_w * 12 / 100)
#define GC       (gfx_w * 13 / 200)
#define PC       (gfx_w * 55 / 1000)
#define LP_TAG   MF_N24
#define LBL_F    MF_N16
enum {
    LP_INK = 18,
    LP_MY = 10,
    SLOP = 28,
    GUARD_R = 67
};
#elif BOARD_UI_TABLET
// the Tab5 (640 x 360, 0.17 mm a px): the small grid at the left, the loupe at the right (4 x 3 of 64 px = 10.9 mm),
// FIRE under the loupe (28 px = 4.8 mm under the zone of the loupe)
enum {
    LP_COLS = 4,
    LP_ROWS = 3,
    LP_C = 64,
    LP_X = 340,
    LP_Y = 6
};
enum {
    LPZ_X0 = LP_X - 4,
    LPZ_X1 = 640,
    LPZ_Y0 = 0,
    LPZ_Y1 = LP_Y + LP_ROWS * LP_C + 4
};
#define FIRE_R 340, 234, 256, 120
#define FIRE_Z 336, 230, 640, 360  // (its tap zone: to the right and bottom edges)
#define LP_TAG MF_N16
#define LBL_F  MF_LABEL
enum {
    LP_INK = 12,
    LP_MY = 7,
    SLOP = 15,
    GUARD_R = 35
};
#else
enum {
    LP_COLS = 3,
    LP_ROWS = 3,
    LP_C = 64,
    LP_X = 124,
    LP_Y = 24
};  // (the right: FIRE is at the left)
enum {
    LPZ_X0 = LP_X - 4,
    LPZ_X1 = 320,
    LPZ_Y0 = 0,
    LPZ_Y1 = 240
};
#define FIRE_R 6, 160, 66, 76
#define FIRE_Z 0, 158, 74, 240  // (its tap zone: to the left and bottom edges)
#define LP_TAG MF_N16
#define LBL_F  MF_LABEL
enum {
    LP_INK = 12,
    LP_MY = 7,
    SLOP = 22,
    GUARD_R = 54
};
#endif
enum {
    BTN_PAD = 2,
    GUARD_MS = 250,
    GUARD_NEAR_MS = 500
};
static void grids(grid_t *mine, grid_t *foe, bool *show_mine, bool *show_foe)
{
#if BOARD_BAR_SCREEN
    *mine = (grid_t){8, 21, 15, 0};
    *foe = (grid_t){482, 21, 15, 0};
    *show_mine = true;
    *show_foe = !(B.g == G_PLACE && !B.my_ready);  // (the placing: its buttons are there)
#elif BOARD_ROUND
    *mine = *foe = (grid_t){(gfx_w - N * GC) / 2, (gfx_h - N * GC) / 2, GC, 0};
    if (B.g == G_PLACE && !B.my_ready) {
        *mine = (grid_t){(gfx_w - N * PC) / 2, (gfx_h - N * PC) / 2, PC,
                         0};  // (SHUFFLE and READY above it, TURN under it)
    }
    // (the enemy sea: my turn, my shell in the air, its splash; the end that I won)
    const bool foe_view = (B.g == G_PLAY && (B.my_turn || B.waiting || (B.shot_mine && B.shot_t < 1.2f))) ||
                          (B.g == G_OVER && B.won == 1);
    *show_foe = foe_view && B.g != G_PLACE;
    *show_mine = !*show_foe;
#elif BOARD_UI_TABLET
    // (the Tab5: two grids of 28 px squares, about 4.8 mm, side by side under the panel)
    *mine = (grid_t){24, 78, 28, 0};
    *foe = (grid_t){336, 78, 28, 0};
    *show_mine = true;
    *show_foe = !(B.g == G_PLACE && !B.my_ready);  // (the placing: its buttons are there)
#else
    *mine = (grid_t){6, 98, 14, 0};
    *foe = (grid_t){174, 98, 14, 0};
    *show_mine = true;
    *show_foe = !(B.g == G_PLACE && !B.my_ready);  // (the placing: its buttons are there)
#endif
}
// the small enemy grid while the loupe is open: on the 3.49 the grid itself; on the CoreS3 and the Watcher a small copy
static grid_t lp_small(void)
{
#if BOARD_BAR_SCREEN
    return (grid_t){482, 21, 15, 0};
#elif BOARD_ROUND
    return (grid_t){(gfx_w - N * SM_C) / 2, SM_Y, SM_C, 0};
#elif BOARD_UI_TABLET
    return (grid_t){24, 78, 28, 0};
#else
    return (grid_t){6, 6, 7, 0};
#endif
}
// the sea of the loupe: a grid of big cells, placed so that its first square is at (LP_X, LP_Y)
static grid_t lp_zoom(void)
{
    return (grid_t){LP_X - B.lp_x * LP_C, LP_Y - B.lp_y * LP_C, LP_C, LP_MY};
}
// the cell of the grid nearest to (x, y)
static int grid_cell(const grid_t *g, int x, int y)
{
    return clampi((y - g->y + g->c) / g->c - 1, 0, N - 1) * N + clampi((x - g->x + g->c) / g->c - 1, 0, N - 1);
}
// the zone of the first tap (the loupe is shut; no other target is on the screen then): the enemy grid and the space
// round it (the 3.49: from x 440, the end of the zone of the loupe; the CoreS3: from x 147, between the two grids; the
// Watcher: all the circle); the square (the nearest), or -1
static int foe_cell_at(const grid_t *g, int x, int y)
{
#if BOARD_BAR_SCREEN
    if (x < LPZ_X1) {
        return -1;
    }
#elif BOARD_UI_TABLET
    if (x < 320) {
        return -1;
    }
#elif !BOARD_ROUND
    if (x < 147) {
        return -1;
    }
#endif
    return grid_cell(g, x, y);
}
// the zone of the small grid while the loupe is open (a tap or a drag moves the loupe there): the square, or -1
static int small_cell_at(int x, int y)
{
#if BOARD_ROUND
    // (the round map: its squares and two of them round it, above the zone of the loupe)
    const grid_t s = lp_small();
    if (x < s.x - 2 * s.c || x >= s.x + (N + 2) * s.c || y < s.y - 2 * s.c || y >= LPZ_Y0) {
        return -1;
    }
    return grid_cell(&s, x, y);
#else
    const grid_t s = lp_small();
#if BOARD_BAR_SCREEN
    if (x < s.x - s.c) {
        return -1;
    }
#elif BOARD_UI_TABLET
    if (x >= s.x + N * s.c + s.c / 2) {
        return -1;  // (the Tab5: the grid and half a square; the loupe zone is at x 336)
    }
#else
    if (x >= s.x + (N + 1) * s.c || y >= s.y + (N + 1) * s.c) {
        return -1;
    }
#endif
    return grid_cell(&s, x, y);
#endif
}
// a tap on the loupe (its zone): the square (y * N + x), or -1
static bool in_loupe(int x, int y)
{
    return x >= LPZ_X0 && x < LPZ_X1 && y >= LPZ_Y0 && y < LPZ_Y1;
}
static int lp_cell_at(int x, int y)
{
    if (!in_loupe(x, y)) {
        return -1;
    }
    return (B.lp_y + clampi((y - LP_Y + LP_C) / LP_C - 1, 0, LP_ROWS - 1)) * N + B.lp_x +
           clampi((x - LP_X + LP_C) / LP_C - 1, 0, LP_COLS - 1);
}
// the zone of the fleet in the placing (a tap there picks the nearest ship): the grid and half a square round it (on
// the 3.49 and the CoreS3 also to the edges of the screen)
static bool in_fleet(const grid_t *g, int x, int y)
{
    const int p = g->c / 2, e = g->x + N * g->c + p;
#if BOARD_BAR_SCREEN
    (void)y;
    return x < e;
#elif BOARD_ROUND
    return x >= g->x - p && y >= g->y - p && x < e && y < g->y + N * g->c + p;
#else
    return x < e && y >= g->y - p;
#endif
}
static void lp_center(int c)
{
    B.lp_x = clampi(c % N - LP_COLS / 2, 0, N - LP_COLS);
    B.lp_y = clampi(c / N - LP_ROWS / 2, 0, N - LP_ROWS);
}
// THE GUARD: after a tap changed the screen (the loupe opened, a button), a touch that starts less than GUARD_MS later
// (a bounce: when the contact breaks for more than 40 ms, input.c gives IN_UP and a new IN_DOWN; a quick double tap),
// or less than GUARD_NEAR_MS later and less than GUARD_R px from that tap (a slow double tap), does nothing.
static int64_t s_g_ms;
static int s_gx, s_gy;
static void guard_set(int x, int y, int64_t ms)
{
    s_g_ms = ms;
    s_gx = x;
    s_gy = y;
}
static bool guarded(int x, int y, int64_t ms)
{
    const int64_t d = ms - s_g_ms;
    if (d < GUARD_MS) {
        return true;
    }
    return d < GUARD_NEAR_MS && (x - s_gx) * (x - s_gx) + (y - s_gy) * (y - s_gy) < GUARD_R * GUARD_R;
}
// open the loupe round the square c (no aim yet)
static void lp_open(int c)
{
    B.lp_on = true;
    B.lp_hold = false;
    B.aim = B.hover = -1;
    B.cur = c;
    lp_center(c);
    sfx_play(SFX_CLICK);
}
// the Watcher: the loupe shows the aim
static void lp_follow(void)
{
    const int x = B.aim % N, y = B.aim / N;
    if (x < B.lp_x) {
        B.lp_x = x;
    } else if (x >= B.lp_x + LP_COLS) {
        B.lp_x = x - LP_COLS + 1;
    }
    if (y < B.lp_y) {
        B.lp_y = y;
    } else if (y >= B.lp_y + LP_ROWS) {
        B.lp_y = y - LP_ROWS + 1;
    }
}
// the placing: the ship NEAREST to the tap (the distance from the tap to the box of the ship), up to 1.5 cells; -1: none
static int ship_near(const grid_t *g, int px, int py)
{
    int best = -1, bd = 0, bc = 0;
    for (int i = 0; i < NSHIP; i++) {
        const ship_t *p = &B.me.ship[i];
        const int x0 = g->x + p->x * g->c, y0 = g->y + p->y * g->c;  // the box: x0..x1, y0..y1 (the last px)
        const int x1 = x0 + (p->dir ? 1 : p->len) * g->c - 1, y1 = y0 + (p->dir ? p->len : 1) * g->c - 1;
        const int dx = px < x0 ? x0 - px : px > x1 ? px - x1 : 0, dy = py < y0 ? y0 - py : py > y1 ? py - y1 : 0;
        if (dx > 2 * g->c || dy > 2 * g->c) {
            continue;  // (far: skip it; the numbers stay small)
        }
        const int d = 4 * (dx * dx + dy * dy);  // (2 x the distance, squared)
        if (d > 9 * g->c * g->c) {
            continue;
        }
        const int ex = 2 * px - x0 - x1, ey = 2 * py - y0 - y1, dc = ex * ex + ey * ey;  // (a tie: the nearer middle)
        if (best < 0 || d < bd || (d == bd && dc < bc)) {
            best = i;
            bd = d;
            bc = dc;
        }
    }
    return best;
}

// ---------------------------------------------------------------- the drawing
static uint16_t C_TXT, C_DIM, C_YEL, C_BG;
// THE 3D PICTURES: Blender renders of the five ships and of the sea, made
// small by tools/mk_art.py into sea_art.c (P8 + 16 steps of alpha, deflated). art_load() inflates them one time; each
// cell size gets its own copy of the ships (a box filter), kept in a small cache. The sea is a tile that wraps and
// moves slowly. The functions obey the clip box s_cl_* (the loupe), not the clip of gfx.
extern const uint8_t SEA_ART[];
extern const uint32_t SEA_ART_LEN;
typedef struct {
    int w, h;
    const uint16_t *pal;
    uint8_t *idx, *a;
} spic_t;
typedef struct {
    int16_t w, h;
    uint16_t *c;
    uint8_t *a;
} sspr_t;
static spic_t s_ship_src[NSHIP][2], s_sea_src;
static uint16_t s_spal[2][256];
static bool s_art_ok;
#define SCACHE 4
static struct {
    int c;
    uint32_t used;
    sspr_t p[NSHIP][2];
} s_sc[SCACHE];
static uint32_t s_sc_t;
static int s_cl_x0, s_cl_y0, s_cl_x1 = 4096, s_cl_y1 = 4096;
static void *sea_alloc(size_t n)
{
#ifdef ESP_PLATFORM
    void *q = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    return q ? q : malloc(n);
#else
    return malloc(n);
#endif
}
static inline uint16_t s_mix(uint16_t d, uint16_t c, int a)  // a: 0 = d .. 256 = c (the byte order of the frame)
{
    if (a <= 0) {
        return d;
    }
    if (a >= 256) {
        return c;
    }
    uint32_t dd = (uint16_t)(d >> 8 | d << 8), ss = (uint16_t)(c >> 8 | c << 8), a5 = (uint32_t)(a + 4) >> 3;
    dd = (dd | (dd << 16)) & 0x07E0F81F;
    ss = (ss | (ss << 16)) & 0x07E0F81F;
    uint32_t r = (dd + (((ss - dd) * a5) >> 5)) & 0x07E0F81F;
    r = (r | (r >> 16)) & 0xFFFF;
    return (uint16_t)(r >> 8 | r << 8);
}
static void art_load(void)
{
    if (s_art_ok) {
        return;
    }
    const uint8_t *b = SEA_ART;
    const size_t n = SEA_ART_LEN;
    if (n < 5 || memcmp(b, "SEA1", 4)) {
        return;
    }
    const int cnt = b[4];
    size_t at = 5;
    tinfl_decompressor *d = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
    uint8_t pal8[512];
    bool ok = d != NULL;
    for (int k = 0; ok && k < 2; k++) {
        size_t zl = b[at] | b[at + 1] << 8, in = zl, out = 512;
        at += 2;
        tinfl_init(d);
        ok = at + zl <= n &&
             tinfl_decompress(d, b + at, &in, pal8, pal8, &out,
                              TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) ==
                     TINFL_STATUS_DONE &&
             out == 512;
        for (int i = 0; ok && i < 256; i++) {
            s_spal[k][i] = (uint16_t)(pal8[2 * i + 1] << 8 | pal8[2 * i]);  // (as the frame keeps it)
        }
        at += zl;
    }
    for (int k = 0; ok && k < cnt; k++) {
        if (at + 10 > n) {
            ok = false;
            break;
        }
        const int ship = b[at], dir = b[at + 1] & 1, w = b[at + 2] | b[at + 3] << 8, h = b[at + 4] | b[at + 5] << 8;
        const size_t zl =
                (size_t)b[at + 6] | (size_t)b[at + 7] << 8 | (size_t)b[at + 8] << 16 | (size_t)b[at + 9] << 24;
        at += 10;
        const bool sea = ship == 255;
        if (!sea && ship >= NSHIP) {
            at += zl;
            continue;
        }
        size_t out = (size_t)w * h * (sea ? 1 : 2), in = zl, got = out;
        uint8_t *dst = (uint8_t *)sea_alloc(out);
        if (!dst || at + zl > n) {
            free(dst);
            ok = false;
            break;
        }
        tinfl_init(d);
        if (tinfl_decompress(d, b + at, &in, dst, dst, &got,
                             TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) !=
                    TINFL_STATUS_DONE ||
            got != out) {
            free(dst);
            ok = false;
            break;
        }
        if (sea) {
            s_sea_src = (spic_t){w, h, s_spal[1], dst, NULL};
        } else {
            s_ship_src[ship][dir] = (spic_t){w, h, s_spal[0], dst, dst + w * h};
        }
        at += zl;
    }
    free(d);
    if (ok && (!s_sea_src.idx || s_sea_src.w != 128 || s_sea_src.h != 128)) {
        ok = false;
    }
    for (int i = 0; ok && i < NSHIP; i++) {
        if (!s_ship_src[i][0].idx || !s_ship_src[i][1].idx) {
            ok = false;
        }
    }
    s_art_ok = ok;
}
// a picture of the data at w x h: each pixel is the mean of its box of the picture (the colours weighted by alpha)
static void spr_make(const spic_t *s, int W, int H, sspr_t *o)
{
    if (W < 1) {
        W = 1;
    }
    if (H < 1) {
        H = 1;
    }
    o->c = (uint16_t *)sea_alloc((size_t)W * H * 3);
    if (!o->c) {
        o->w = o->h = 0;
        return;
    }
    o->a = (uint8_t *)(o->c + W * H);
    o->w = (int16_t)W;
    o->h = (int16_t)H;
    for (int y = 0; y < H; y++) {
        int y0 = y * s->h / H, y1 = (y + 1) * s->h / H;
        if (y1 <= y0) {
            y1 = y0 + 1;
        }
        for (int x = 0; x < W; x++) {
            int x0 = x * s->w / W, x1 = (x + 1) * s->w / W;
            if (x1 <= x0) {
                x1 = x0 + 1;
            }
            int r = 0, g = 0, bl = 0, at = 0, cnt = 0;
            for (int sy = y0; sy < y1; sy++) {
                for (int sx = x0; sx < x1; sx++) {
                    const int i = sy * s->w + sx, a = s->a[i];
                    const uint16_t c = (uint16_t)(s->pal[s->idx[i]] >> 8 | s->pal[s->idx[i]] << 8);
                    r += (c >> 11) * a;
                    g += ((c >> 5) & 63) * a;
                    bl += (c & 31) * a;
                    at += a;
                    cnt++;
                }
            }
            const int k = y * W + x;
            o->a[k] = (uint8_t)(at / cnt);
            const uint16_t c = at ? (uint16_t)((r / at) << 11 | (g / at) << 5 | (bl / at)) : 0;
            o->c[k] = (uint16_t)(c >> 8 | c << 8);
        }
    }
}
// the ships for the cell size c (from the cache, or made now in the slot that waited the longest)
static const sspr_t *ships_at(int c, int ship, int dir)
{
    int slot = 0;
    for (int i = 0; i < SCACHE; i++) {
        if (s_sc[i].c == c && s_sc[i].p[0][0].c) {
            s_sc[i].used = ++s_sc_t;
            return &s_sc[i].p[ship][dir];
        }
        if (s_sc[i].used < s_sc[slot].used) {
            slot = i;
        }
    }
    for (int i = 0; i < NSHIP; i++) {
        for (int d = 0; d < 2; d++) {
            free(s_sc[slot].p[i][d].c);
            s_sc[slot].p[i][d].c = NULL;
            const int L = SHIP_LEN[i];
            spr_make(&s_ship_src[i][d], d ? c : L * c, d ? L * c : c, &s_sc[slot].p[i][d]);
        }
    }
    s_sc[slot].c = c;
    s_sc[slot].used = ++s_sc_t;
    return &s_sc[slot].p[ship][dir];
}
static void draw_sea(const grid_t *g, float t)
{
    const int gx1 = g->x + N * g->c + 1, gy1 = g->y + N * g->c + 1;
    const int x0 = g->x > s_cl_x0 ? (g->x > 0 ? g->x : 0) : (s_cl_x0 > 0 ? s_cl_x0 : 0);
    const int y0 = g->y > s_cl_y0 ? (g->y > 0 ? g->y : 0) : (s_cl_y0 > 0 ? s_cl_y0 : 0);
    int x1 = gx1 < s_cl_x1 ? gx1 : s_cl_x1, y1 = gy1 < s_cl_y1 ? gy1 : s_cl_y1;
    if (x1 > gfx_w) {
        x1 = gfx_w;
    }
    if (y1 > gfx_h) {
        y1 = gfx_h;
    }
    const uint16_t line = rgb(10, 34, 70);
    if (!s_art_ok) {
        for (int y = 0; y < N; y++) {
            for (int x = 0; x < N; x++) {
                const float w = sinf(t * 1.3f + x * 0.9f + y * 0.6f);
                gfx_fill_rect(g->x + x * g->c, g->y + y * g->c, g->c, g->c,
                              rgb(18, (uint8_t)(58 + 6 * w), (uint8_t)(112 + 10 * w)));
            }
        }
    } else {
        // the tile at 1 px for 1 px, moved slowly (the waves drift); the cells: the same water everywhere
        const int ox = (int)(t * 5.0f), oy = (int)(t * 3.0f);
        const uint16_t *pal = s_sea_src.pal;
        for (int y = y0; y < y1; y++) {
            const uint8_t *row = s_sea_src.idx + ((y + oy) & 127) * 128;
            uint16_t *d = g_fb + y * gfx_w;
            for (int x = x0; x < x1; x++) {
                d[x] = pal[row[(x + ox) & 127]];
            }
        }
    }
    // the lines of the grid (a soft line over the water)
    for (int k = 0; k <= N; k++) {
        const int lx = g->x + k * g->c, ly = g->y + k * g->c;
        if (lx >= x0 && lx < x1) {
            for (int y = y0; y < y1; y++) {
                g_fb[y * gfx_w + lx] = s_mix(g_fb[y * gfx_w + lx], line, 170);
            }
        }
        if (ly >= y0 && ly < y1) {
            uint16_t *d = g_fb + ly * gfx_w;
            for (int x = x0; x < x1; x++) {
                d[x] = s_mix(d[x], line, 170);
            }
        }
    }
}
#if BOARD_ROUND
// the half width of the circle at the row y (0: out of the circle)
static int rd_half(int y)
{
    const float r = gfx_w * 0.5f, d = y + 0.5f - gfx_h * 0.5f, q = r * r - d * d;
    return q > 0 ? (int)sqrtf(q) : 0;
}
// THE PORTHOLE: all the circle is deep sea (the tile of the sea, darker, slower), with a brass rim and its rivets; the
// grids, the loupe and the buttons go over it
static void rd_ocean(float t)
{
    static uint16_t deep[256];
    static const uint16_t *made;
    const int R = gfx_w / 2;
    if (s_art_ok && made != s_sea_src.pal) {
        for (int i = 0; i < 256; i++) {
            deep[i] = s_mix(s_sea_src.pal[i], rgb(2, 10, 26), 150);
        }
        made = s_sea_src.pal;
    }
    const int ox = (int)(t * 3.0f), oy = (int)(t * 2.0f);
    for (int y = 0; y < gfx_h; y++) {
        const int h = rd_half(y);
        if (!h) {
            continue;
        }
        uint16_t *d = g_fb + y * gfx_w;
        if (!s_art_ok) {
            for (int x = R - h; x < R + h; x++) {
                d[x] = rgb(8, 30, 58);
            }
            continue;
        }
        const uint8_t *row = s_sea_src.idx + ((y + oy) & 127) * 128;
        for (int x = R - h; x < R + h; x++) {
            d[x] = deep[row[(x + ox) & 127]];
        }
    }
    ui_ring(R, R, R - 4, 8, rgb(150, 116, 56), 255);
    ui_ring(R, R, R - 9, 2, rgb(70, 50, 24), 255);
    ui_ring(R, R, R - 1, 2, rgb(220, 186, 110), 160);
    for (int k = 0; k < 12; k++) {
        const float a = k * 0.5235988f;
        ui_circle(R + (int)lroundf((R - 4) * fcos(a)), R + (int)lroundf((R - 4) * fsin(a)), 2, rgb(236, 206, 130), 255);
    }
}
#endif
// a ship of the kind 'kind' (0 CARRIER .. 4 DESTROYER) at its squares; sunk: dark and burnt
static void draw_ship(const grid_t *g, const ship_t *p, bool sunk, int kind)
{
    const int c = g->c, x0 = g->x + p->x * c, y0 = g->y + p->y * c;
    if (!s_art_ok || kind < 0 || kind >= NSHIP) {
        const int w = p->dir ? c : p->len * c, h = p->dir ? p->len * c : c, m = c / 7 + 1;
        gfx_fill_rect(x0 + m, y0 + m, w - 2 * m, h - 2 * m, sunk ? rgb(70, 64, 60) : rgb(150, 156, 166));
        return;
    }
    const sspr_t *sp = ships_at(c, kind, p->dir ? 1 : 0);
    if (!sp->c) {
        return;
    }
    const uint16_t burnt = rgb(44, 34, 28);
    for (int j = 0; j < sp->h; j++) {
        const int y = y0 + j;
        if (y < s_cl_y0 || y >= s_cl_y1 || y < 0 || y >= gfx_h) {
            continue;
        }
        uint16_t *d = g_fb + y * gfx_w;
        const uint16_t *col = sp->c + j * sp->w;
        const uint8_t *al = sp->a + j * sp->w;
        for (int i = 0; i < sp->w; i++) {
            const int x = x0 + i, a = al[i];
            if (!a || x < s_cl_x0 || x >= s_cl_x1 || x < 0 || x >= gfx_w) {
                continue;
            }
            const uint16_t cc = sunk ? s_mix(col[i], burnt, 110) : col[i];
            d[x] = a == 255 ? cc : s_mix(d[x], cc, a + (a >> 7));
        }
    }
}
static void draw_marks(const grid_t *g, const sea_t *s, float t)
{
    const int r = g->my ? g->c * 3 / 10 : g->c / 3;  // (the loupe: a little smaller, under the name of the square)
    for (int y = 0; y < N; y++) {
        for (int x = 0; x < N; x++) {
            const int cx = g->x + x * g->c + g->c / 2, cy = g->y + y * g->c + g->c / 2 + g->my;
            if (s->shot[y][x] == 1) {
                gfx_ring(cx, cy, r, r - 2 - g->c / 24, rgb(220, 232, 246));
            } else if (s->shot[y][x] == 2) {
                // a fire: red with a yellow heart that flickers
                const int f = (int)(t * 8 + x * 3 + y) % 3;
                gfx_fill_circle(cx, cy, r + 1, rgb(214, 46, 26));
                gfx_fill_circle(cx, cy - (f == 1), g->c / 6 + (f == 2), rgb(255, 210, 70));
            }
        }
    }
}
// the shell that falls on the last shot, then a splash or a blast (my shot with no answer yet: the shell waits)
static void draw_shot(const grid_t *g, float t)
{
    const bool wait = B.waiting && B.shot_mine;
    if (B.shot_t > 1.0f && !wait) {
        return;
    }
    const int cx = g->x + B.sx * g->c + g->c / 2, cy = g->y + B.sy * g->c + g->c / 2 + g->my;
    if (wait && B.shot_t >= 0.35f) {
        const int r = (int)(g->c * (0.45f + 0.08f * sinf(t * 14)));
        gfx_ring(cx, cy, r + 2, r, rgb(255, 255, 255));
    } else if (B.shot_t < 0.35f) {
        const int r = (int)(g->c * (1.4f - B.shot_t * 3));
        gfx_ring(cx, cy, r + 2, r, rgb(255, 255, 255));
    } else {
        const float k = (B.shot_t - 0.35f) / 0.65f;
        const int r = (int)(g->c * (0.4f + k * 1.2f));
        gfx_ring(cx, cy, r + 2, r, B.shot_res == R_MISS ? rgb(200, 230, 255) : rgb(255, 160, 40));
    }
}
static void draw_grid(const grid_t *g, bool mine, float t)
{
    draw_sea(g, t);
    if (mine) {
        for (int i = 0; i < NSHIP; i++) {
            grid_t s = *g;
            if (i == B.pick && B.pick_t > 0 && !B.pick_ok) {
                s.x += (int)(sinf(B.pick_t * 60.0f) * 3.0f);  // (the shake: it cannot turn)
            }
            draw_ship(&s, &B.me.ship[i], B.me.ship[i].hits >= B.me.ship[i].len, i);
        }
    } else {
        for (int i = 0; i < B.foe_sunk; i++) {
            draw_ship(g, &B.foe.ship[i], true, B.foe_kind[i]);
        }
        if (B.g == G_OVER && B.vs_cpu) {
            for (int i = 0; i < NSHIP; i++) {
                if (s_cpu.ship[i].hits < s_cpu.ship[i].len) {
                    draw_ship(g, &s_cpu.ship[i], false, i);
                }
            }
        }
    }
    draw_marks(g, mine ? &B.me : &B.foe, t);
    if ((mine && !B.shot_mine) || (!mine && B.shot_mine)) {
        draw_shot(g, t);
    }
    // the placing: a frame on the picked ship (yellow; red when it cannot turn)
    if (mine && B.g == G_PLACE && B.pick >= 0 && B.pick < NSHIP) {
        const ship_t *p = &B.me.ship[B.pick];
        const bool bad = B.pick_t > 0 && !B.pick_ok;
        gfx_frame_rect(g->x + p->x * g->c - 2, g->y + p->y * g->c - 2, (p->dir ? 1 : p->len) * g->c + 5,
                       (p->dir ? p->len : 1) * g->c + 5, 2, bad ? rgb(240, 80, 60) : C_YEL);
    }
}
// the names of the grids; round the enemy grid the row letters (A-J) and the column numbers (1-10)
#if BOARD_BAR_SCREEN
#define NAME_Y   8
#define NUM_Y(g) ((g)->y - 3)
#elif BOARD_ROUND
#define NUM_Y(g) ((g)->y + N * (g)->c + 15)
#elif BOARD_UI_TABLET
#define NAME_Y   60
#define NUM_Y(g) ((g)->y - 3)
#else
#define NAME_Y   81
#define NUM_Y(g) ((g)->y - 3)
#endif
// the name of a square: the row letter and the column number ("C4", "J10"); y < 0: the number only
static void sq_name(char *s, int x, int y)
{
    if (y >= 0) {
        *s++ = (char)('A' + y);
    }
    if (x == 9) {
        *s++ = '1';
        *s++ = '0';
    } else {
        *s++ = (char)('1' + x);
    }
    *s = 0;
}
static void grid_names(const grid_t *gm, const grid_t *gf, bool sm, bool sf)
{
#if BOARD_ROUND
    {
        // (the name under the grid: the numbers of the enemy grid are there first)
        const grid_t *g = sm ? gm : gf;
        mf_text_o(&MF_TINY, gfx_w / 2 - mf_width(&MF_TINY, sf && !sm ? "ENEMY WATERS" : "YOUR FLEET", 1) / 2,
                  g->y + N * g->c + (sf ? 36 : 16), sf && !sm ? "ENEMY WATERS" : "YOUR FLEET", C_DIM, rgb(0, 10, 24), 1,
                  1);
    }
#else
    if (sm) {
        mf_text(&MF_TINY, gm->x, NAME_Y, "YOUR FLEET", C_DIM, 255, 1);
    }
    if (sf) {
        mf_text(&MF_TINY, gf->x, NAME_Y, "ENEMY WATERS", C_DIM, 255, 1);
    }
#endif
    if (!sf) {
        return;
    }
    const int ink = mf_glyph(&LBL_F, 'A')->h;
    // (a narrow column: "9" and "10" move apart, so that 3 px stay between them)
    const int gap = gf->c - (mf_width(&LBL_F, "9", 1) + mf_width(&LBL_F, "10", 1) + 1) / 2,
              need = gap < 3 ? 3 - gap : 0, g9 = need / 2, g10 = need - g9;
    for (int k = 0; k < N; k++) {
        char s[4] = {(char)('A' + k), 0};
        mf_text_c(&LBL_F, gf->x - mf_width(&LBL_F, "J", 1) / 2 - 3, gf->y + k * gf->c + (gf->c + ink) / 2, s, C_DIM,
                  255, 1);
        sq_name(s, k, -1);
        mf_text_c(&LBL_F,
                  gf->x + k * gf->c + gf->c / 2 + 1 +
                          (k == 8   ? -g9
                           : k == 9 ? g10
                                    : 0),
                  NUM_Y(gf), s, C_DIM, 255, 1);
    }
}
// THE LOUPE: the sea of the region, big (the same draw functions, in a clip box), the name of each square, a cross on
// each square that was not shot (aim here), a white frame on the square under a finger, a yellow frame on the aim
static void sq_frame(int c, int t, uint16_t col)
{
    const int i = c % N - B.lp_x, j = c / N - B.lp_y;
    if (c < 0 || i < 0 || j < 0 || i >= LP_COLS || j >= LP_ROWS) {
        return;
    }
    gfx_frame_rect(LP_X + i * LP_C - 1, LP_Y + j * LP_C - 1, LP_C + 3, LP_C + 3, t,
                   col);  // (on the lines: clear of the name)
}
static void draw_loupe(float t)
{
    const int w = LP_COLS * LP_C, h = LP_ROWS * LP_C;
    const grid_t z = lp_zoom();
    gfx_fill_rect(LP_X - 5, LP_Y - 5, w + 11, h + 11, C_BG);
    gfx_clip_box(LP_X, LP_Y, LP_X + w + 1, LP_Y + h + 1);
    s_cl_x0 = LP_X;
    s_cl_y0 = LP_Y;
    s_cl_x1 = LP_X + w + 1;
    s_cl_y1 = LP_Y + h + 1;
    draw_grid(&z, false, t);
    s_cl_x0 = s_cl_y0 = 0;
    s_cl_x1 = s_cl_y1 = 4096;
    gfx_clip_box_off();
    for (int j = 0; j < LP_ROWS; j++) {
        for (int i = 0; i < LP_COLS; i++) {
            const int x = B.lp_x + i, y = B.lp_y + j, px = LP_X + i * LP_C, py = LP_Y + j * LP_C;
            if (!B.foe.shot[y][x] && !(B.lp_hold && x == B.sx && y == B.sy)) {
                const int cx = px + LP_C / 2, cy = py + LP_C / 2 + LP_MY, r = LP_C / 5;
                const uint16_t k = rgb(96, 150, 206);
                gfx_fill_rect(cx - r, cy, r - 3, 1, k);
                gfx_fill_rect(cx + 4, cy, r - 3, 1, k);
                gfx_fill_rect(cx, cy - r, 1, r - 3, k);
                gfx_fill_rect(cx, cy + 4, 1, r - 3, k);
            }
            char tag[4];
            sq_name(tag, x, y);
            mf_text_o(&LP_TAG, px + 5, py + 5 + LP_INK, tag, y * N + x == B.aim ? C_YEL : C_DIM, C_BG, 0, 1);
        }
    }
    if (B.hover != B.aim) {
        sq_frame(B.hover, 2, rgb(236, 242, 250));
    }
    sq_frame(B.aim, 3, C_YEL);
    gfx_frame_rect(LP_X - 3, LP_Y - 3, w + 7, h + 7, 2, rgb(120, 150, 180));
}
// the buttons (a tap up to BTN_PAD px out of a button still hits it; 2 mm or more between two of these zones)
static rect_t s_btn[6];
static int s_nbtn;
static int s_btn_id[6];
static uint8_t s_inv[3][6];
static int s_ninv;
enum {
    BT_CPU,
    BT_FRIEND,
    BT_SHUFFLE,
    BT_READY,
    BT_CANCEL,
    BT_TURN,
    BT_FIRE,
    BT_JOIN0
};  // (BT_JOIN0 + i: join the invite i)
static void add_zone(int id, int x, int y, int w, int h)
{
    if (s_nbtn == 6) {
        return;
    }
    s_btn[s_nbtn] = (rect_t){x, y, w, h};
    s_btn_id[s_nbtn++] = id;
}
// the biggest face (MF_N20, MF_N16, MF_LABEL, MF_SMALL) for the words s in a button of w px
static const mfont_t *fit_face(const char *s, int w)
{
    static const mfont_t *const F[] = {&MF_N20, &MF_N16, &MF_LABEL, &MF_SMALL};
    for (int k = 0; k < 3; k++) {
        if (mf_width(F[k], s, 1) <= w - 8) {
            return F[k];
        }
    }
    return F[3];
}
static void box(int x, int y, int w, int h, bool main, bool lit)
{
#if BOARD_ROUND
    // (the round: a pill, its ends round)
    ui_rbox(x, y, w, h, h / 2,
            lit    ? rgb(40, 84, 128)
            : main ? rgb(22, 60, 96)
                   : rgb(12, 26, 40),
            lit    ? rgb(255, 236, 140)
            : main ? rgb(250, 210, 80)
                   : rgb(150, 170, 190),
            lit ? 3 : 2);
    return;
#endif
    gfx_fill_rect(x, y, w, h, lit ? rgb(40, 84, 128) : main ? rgb(22, 60, 96) : rgb(16, 28, 40));
    gfx_frame_rect(x, y, w, h, lit ? 3 : 2, lit ? rgb(255, 236, 140) : main ? rgb(250, 210, 80) : rgb(150, 170, 190));
}
static void button(int id, int x, int y, int w, int h, const char *s, bool main, const mfont_t *f)
{
    add_zone(id, x, y, w, h);
    box(x, y, w, h, main, false);
    if (!f) {
        f = fit_face(s, w);
    }
    mf_text_c(f, x + w / 2, y + (h + mf_glyph(f, 'A')->h) / 2, s, main ? rgb(250, 216, 96) : C_TXT, 255, 1);
}
static int button_at(int x, int y)
{
#if !BOARD_ROUND
    {
        const int z[4] = {FIRE_Z};
        for (int i = 0; i < s_nbtn; i++) {
            if (s_btn_id[i] == BT_FIRE && x >= z[0] && x < z[2] && y >= z[1] && y < z[3]) {
                return BT_FIRE;
            }
        }
    }
#endif
    for (int i = 0; i < s_nbtn; i++) {
        if (x >= s_btn[i].x - BTN_PAD && x < s_btn[i].x + s_btn[i].w + BTN_PAD && y >= s_btn[i].y - BTN_PAD &&
            y < s_btn[i].y + s_btn[i].h + BTN_PAD) {
            return s_btn_id[i];
        }
    }
    return -1;
}
// the panel: a box and its lines (the 3.49: between the grids; the CoreS3: on top; the Watcher: above the grid)
#if BOARD_BAR_SCREEN
enum {
    PL_X = 320,
    PL_Y1 = 40,
    PL_Y2 = 64
};
#elif BOARD_ROUND
// (the round: the title lines over the two big buttons; in a game the lines in the top segment of the circle)
#define PL_X  (gfx_w / 2)
#define PL_Y1 (B.g <= G_WAIT ? gfx_w * 20 / 100 : gfx_w * 107 / 1000)
#define PL_Y2 (PL_Y1 + gfx_w * 5 / 100)
#elif BOARD_UI_TABLET
enum {
    PL_X = 320,
    PL_Y1 = 24,
    PL_Y2 = 46
};
#else
enum {
    PL_X = 160,
    PL_Y1 = 24,
    PL_Y2 = 44
};
#endif
#if BOARD_ROUND
// the biggest face of the list whose words fit the circle at the top of the line (base y)
static const mfont_t *rd_fit(const mfont_t *const *f, int n, const char *s, int y)
{
    for (int i = 0; i < n - 1; i++) {
        if (mf_width(f[i], s, 1) <= 2 * rd_half(y - f[i]->asc) - 16) {
            return f[i];
        }
    }
    return f[n - 1];
}
#endif
static void panel_lines(const char *a, const char *b)
{
#if BOARD_ROUND
    static const mfont_t *const FA[] = {&MF_N28, &MF_N20, &MF_N16, &MF_LABEL}, *const FB[] = {&MF_LABEL, &MF_SMALL};
    const int na = B.g <= G_WAIT ? 4 : 3;
    if (a) {
        mf_text_o(rd_fit(FA + 4 - na, na, a, PL_Y1), PL_X - mf_width(rd_fit(FA + 4 - na, na, a, PL_Y1), a, 1) / 2,
                  PL_Y1, a, C_YEL, rgb(0, 10, 24), 2, 1);
    }
    if (b) {
        mf_text_o(rd_fit(FB, 2, b, PL_Y2), PL_X - mf_width(rd_fit(FB, 2, b, PL_Y2), b, 1) / 2, PL_Y2, b, C_TXT,
                  rgb(0, 10, 24), 1, 1);
    }
#else
    if (a) {
        mf_text_c(&MF_N20, PL_X, PL_Y1, a, C_YEL, 255, 1);
    }
    if (b) {
        mf_text_c(&MF_LABEL, PL_X, PL_Y2, b, C_TXT, 255, 1);
    }
#endif
}
// the buttons of the title (A: COMPUTER, B: INVITE A FRIEND; with an invite A: JOIN, B: COMPUTER), the wait and the
// placing (x, y, w, h)
#if BOARD_BAR_SCREEN
#define TB_A       166, 84, 140, 64
#define TB_B       334, 84, 140, 64
#define TB_CANCEL  250, 84, 140, 64
#define PB_TURN    230, 88, 140, 72
#define PB_SHUFFLE 394, 88, 112, 72
#define PB_READY   530, 88, 106, 72
#elif BOARD_ROUND
// (the round: two big buttons in the middle of the circle; the placing: SHUFFLE and READY above the fleet, TURN under it)
#define TB_W       (gfx_w * 55 / 100)
#define TB_H       (gfx_w * 17 / 100)
#define TB_A       (gfx_w - TB_W) / 2, gfx_w * 32 / 100, TB_W, TB_H
#define TB_B       (gfx_w - TB_W) / 2, gfx_w * 54 / 100, TB_W, TB_H
#define TB_CANCEL  (gfx_w - TB_W) / 2, gfx_w * 40 / 100, TB_W, TB_H
#define PB_W       (gfx_w * 27 / 100)
#define PB_H       (gfx_w * 11 / 100)
#define PB_SHUFFLE gfx_w / 2 - 4 - PB_W, (gfx_h - N * PC) / 2 - PB_H - gfx_w * 3 / 100, PB_W, PB_H
#define PB_READY   gfx_w / 2 + 4, (gfx_h - N * PC) / 2 - PB_H - gfx_w * 3 / 100, PB_W, PB_H
#define PB_TURN (gfx_w - gfx_w * 40 / 100) / 2, (gfx_h + N * PC) / 2 + PC / 2 + gfx_w * 2 / 100, gfx_w * 40 / 100, PB_H
#elif BOARD_UI_TABLET
#define TB_A       44, 150, 240, 90
#define TB_B       356, 150, 240, 90
#define TB_CANCEL  200, 150, 240, 90
#define PB_READY   336, 78, 280, 80
#define PB_SHUFFLE 336, 174, 280, 80
#define PB_TURN    336, 270, 280, 84
#else
#define TB_A       6, 56, 139, 64
#define TB_B       175, 56, 139, 64
#define TB_CANCEL  90, 56, 140, 64
#define PB_READY   214, 1, 102, 59
#define PB_SHUFFLE 214, 94, 102, 59
#define PB_TURN    214, 179, 102, 59
#endif
static void title_buttons(void)
{
    const rect_t a = {TB_A}, b = {TB_B};
    const mfont_t *f = fit_face("INVITE A FRIEND", b.w);  // (one face for the two buttons)
    if (s_ninv) {
        const char *nm = friends_name_of(s_inv[0]);
        char s[24];
        snprintf(s, sizeof s, "JOIN %s", friends_short_name(nm));
        button(BT_JOIN0, a.x, a.y, a.w, a.h, s, true, mf_width(f, s, 1) <= a.w - 8 ? f : NULL);
        button(BT_CPU, b.x, b.y, b.w, b.h, "COMPUTER", false, f);
    } else {
        button(BT_CPU, a.x, a.y, a.w, a.h, "COMPUTER", true, f);
        button(BT_FRIEND, b.x, b.y, b.w, b.h, "INVITE A FRIEND", false, f);
    }
}
#if !BOARD_ROUND
// TURN: the picked ship turns (its name under the word)
static void turn_button(void)
{
    const rect_t r = {PB_TURN};
    add_zone(BT_TURN, r.x, r.y, r.w, r.h);
    box(r.x, r.y, r.w, r.h, false, B.pick >= 0);
    const bool on = B.pick >= 0 && B.pick < NSHIP;
    mf_text_c(&MF_N20, r.x + r.w / 2, r.y + r.h / 2 - 2, "TURN", on ? C_TXT : C_DIM, 255, 1);
    const char *nm = on ? SHIP_NAME[B.pick] : "TAP A SHIP";
    mf_text_c(mf_width(&MF_LABEL, nm, 1) <= r.w - 8 ? &MF_LABEL : &MF_SMALL, r.x + r.w / 2, r.y + r.h / 2 + 19, nm,
              on ? C_YEL : C_DIM, 255, 1);
}
// FIRE: the aim in big letters (white: the square under a finger on the loupe), the answer of the shot; with no aim a
// hint. (x, y of the lines in the box)
#if BOARD_BAR_SCREEN
#define FIRE_F1 MF_N24
#define FIRE_F2 MF_N34
enum {
    FY1 = 36,
    FY2 = 98,
    FY3 = 140,
    FYH = 86
};
#elif BOARD_UI_TABLET
#define FIRE_F1 MF_N24
#define FIRE_F2 MF_N34
enum {
    FY1 = 32,
    FY2 = 76,
    FY3 = 106,
    FYH = 62
};
#else
#define FIRE_F1 MF_N20
#define FIRE_F2 MF_N28
enum {
    FY1 = 21,
    FY2 = 49,
    FY3 = 68,
    FYH = 40
};
#endif
static void draw_fire(void)
{
    const rect_t r = {FIRE_R};
    add_zone(BT_FIRE, r.x, r.y, r.w, r.h);
    const int a = B.hover >= 0 ? B.hover : B.aim, cx = r.x + r.w / 2;
    const bool armed = B.aim >= 0 && !B.lp_hold;
    gfx_fill_rect(r.x, r.y, r.w, r.h, armed ? rgb(112, 26, 16) : rgb(30, 24, 28));
    gfx_frame_rect(r.x, r.y, r.w, r.h, 3, armed ? rgb(255, 128, 64) : rgb(96, 88, 92));
    mf_text_c(&FIRE_F1, cx, r.y + FY1, "FIRE", armed ? C_YEL : C_DIM, 255, 1);
    if (a >= 0) {
        char s[4];
        sq_name(s, a % N, a / N);
        mf_text_c(&FIRE_F2, cx, r.y + FY2, s, B.hover >= 0 ? C_TXT : C_YEL, 255, 1);
    } else {
        const mfont_t *f = mf_width(&MF_LABEL, "SQUARE", 1) <= r.w - 8 ? &MF_LABEL : &MF_SMALL;
        mf_text_c(f, cx, r.y + FYH, "TAP A", C_DIM, 255, 1);
        mf_text_c(f, cx, r.y + FYH + f->line, "SQUARE", C_DIM, 255, 1);
    }
    if (B.lp_hold) {
        mf_text_c(&MF_LABEL, cx, r.y + FY3,
                  B.waiting              ? "..."
                  : B.shot_res == R_MISS ? "MISS"
                  : B.shot_res == R_HIT  ? "HIT"
                                         : "SUNK",
                  C_TXT, 255, 1);
    }
}
#endif
// the aim screen: the small grid (the region of the loupe in yellow, the aim in white), the loupe, FIRE (the Watcher:
// the aim and the push under the loupe)
static void draw_aim(float t)
{
    const grid_t s = lp_small();
    draw_grid(&s, false, t);
#if BOARD_BAR_SCREEN || BOARD_UI_TABLET
    grid_names(&s, &s, false, true);
#elif !BOARD_ROUND
    mf_text_c(&MF_TINY, s.x + N * s.c / 2, s.y + N * s.c + 13, "ENEMY", C_DIM, 255, 1);
#endif
    gfx_frame_rect(s.x + B.lp_x * s.c - 2, s.y + B.lp_y * s.c - 2, LP_COLS * s.c + 5, LP_ROWS * s.c + 5, 2, C_YEL);
    if (B.aim >= 0) {
        gfx_frame_rect(s.x + (B.aim % N) * s.c - 1, s.y + (B.aim / N) * s.c - 1, s.c + 3, s.c + 3, 1,
                       rgb(255, 255, 255));
    }
    draw_loupe(t);
#if BOARD_ROUND
    {
        // the line of the aim (in the gap over FIRE), then FIRE: the aim in it, or the answer of the shot
        const int fw = gfx_w * 36 / 100, fh = gfx_w * 11 / 100, fx = (gfx_w - fw) / 2, fy = LPZ_Y1 + FIRE_GAP,
                  cx = gfx_w / 2;
        const int a = B.hover >= 0 ? B.hover : B.aim;
        const bool armed = B.aim >= 0 && !B.lp_hold;
        const char *hint = B.lp_hold   ? ""
                           : B.aim < 0 ? (BOARD_WHEEL ? "TAP A SQUARE OR TURN THE WHEEL" : "TAP A SQUARE TO AIM")
                                       : (BOARD_WHEEL ? "TAP FIRE OR PUSH THE WHEEL" : "TAP FIRE TO SHOOT");
        mf_text_c(&MF_SMALL, cx, LPZ_Y1 + FIRE_GAP / 2 + 5, hint, C_DIM, 255, 1);
        add_zone(BT_FIRE, fx, fy, fw, fh);
        ui_rbox(fx, fy, fw, fh, fh / 2, armed ? rgb(112, 26, 16) : rgb(30, 24, 28),
                armed ? rgb(255, 128, 64) : rgb(96, 88, 92), 3);
        char s[24], nm[4] = "";
        if (a >= 0) {
            sq_name(nm, a % N, a / N);
        }
        if (B.lp_hold) {
            snprintf(s, sizeof s, "%s",
                     B.waiting              ? "..."
                     : B.shot_res == R_MISS ? "MISS"
                     : B.shot_res == R_HIT  ? "HIT!"
                                            : "SUNK!");
        } else {
            snprintf(s, sizeof s, a >= 0 ? "FIRE  %s" : "FIRE", nm);
        }
        const mfont_t *f = mf_width(&MF_N24, s, 1) <= fw - fh ? &MF_N24 : &MF_N20;
        mf_text_c(f, cx, fy + (fh + f->asc) / 2, s, armed || B.lp_hold ? C_YEL : C_DIM, 255, 1);
    }
#else
    draw_fire();
#endif
}
// the placing: the fleet (the picked ship in a frame), the words, TURN, SHUFFLE, READY (the Watcher: SHUFFLE and READY
// above the fleet; a tap or the wheel picks, a push does it)
static void draw_place(void)
{
    if (B.my_ready) {
        panel_lines("WAIT FOR THE ENEMY", "THE ENEMY PLACES ITS FLEET");
        return;
    }
#if BOARD_BAR_SCREEN
    mf_text_c(&MF_N20, 422, 40, "PLACE YOUR FLEET", C_YEL, 255, 1);
    mf_text_c(&MF_LABEL, 422, 64, "TAP A SHIP, THEN TURN", C_TXT, 255, 1);
#elif BOARD_ROUND
    {
        // SHUFFLE and READY above the fleet, TURN (with the name of the picked ship) under it; a tap does it (the
        // Watcher: the wheel picks too, and a push does it)
        const rect_t a = {PB_SHUFFLE}, b = {PB_READY}, r = {PB_TURN};
        add_zone(BT_SHUFFLE, a.x, a.y, a.w, a.h);
        add_zone(BT_READY, b.x, b.y, b.w, b.h);
        add_zone(BT_TURN, r.x, r.y, r.w, r.h);
        box(a.x, a.y, a.w, a.h, false, B.pick == FOCUS_SHUFFLE);
        box(b.x, b.y, b.w, b.h, true, B.pick == FOCUS_READY);
        mf_text_c(&MF_N16, a.x + a.w / 2, a.y + (a.h + MF_N16.asc) / 2, "SHUFFLE", C_TXT, 255, 1);
        mf_text_c(&MF_N16, b.x + b.w / 2, b.y + (b.h + MF_N16.asc) / 2, "READY", rgb(250, 216, 96), 255, 1);
        const bool ship = B.pick >= 0 && B.pick < NSHIP;
        box(r.x, r.y, r.w, r.h, false, ship);
        char s[24];
        snprintf(s, sizeof s, ship ? "TURN %s" : "TAP A SHIP", ship ? SHIP_NAME[B.pick] : "");
        const mfont_t *f = mf_width(&MF_N16, s, 1) <= r.w - r.h ? &MF_N16 : &MF_LABEL;
        mf_text_c(f, r.x + r.w / 2, r.y + (r.h + f->asc) / 2, s, ship ? C_YEL : C_DIM, 255, 1);
    }
    return;
#elif BOARD_UI_TABLET
    mf_text_c(&MF_N20, 320, 24, "PLACE YOUR FLEET", C_YEL, 255, 1);
    mf_text_c(&MF_LABEL, 320, 46, "TAP A SHIP, THEN TURN", C_TXT, 255, 1);
#else
    mf_text_c(&MF_N16, 98, 28, "PLACE YOUR FLEET", C_YEL, 255, 1);
    mf_text_c(&MF_LABEL, 98, 52, "TAP A SHIP, THEN TURN", C_TXT, 255, 1);
#endif
#if !BOARD_ROUND
    turn_button();
    button(BT_SHUFFLE, PB_SHUFFLE, "SHUFFLE", false, NULL);
    button(BT_READY, PB_READY, "READY", true, NULL);
#endif
}
static void sb_render(int64_t now)
{
    const float t = (float)(now % 100000) / 1000.0f;
    gfx_fill(C_BG);
#if BOARD_ROUND
    rd_ocean(t);
#endif
    s_nbtn = 0;
    grid_t gm, gf;
    bool sm, sf;
    grids(&gm, &gf, &sm, &sf);
    char a[48], b[64];
    if (B.g == G_TITLE || B.g == G_WAIT) {
        // the sea at the back, the title, the choices; the invites of friends
#if !BOARD_ROUND
        draw_sea(&gm, t);
        for (int i = 0; i < NSHIP; i++) {
            draw_ship(&gm, &B.me.ship[i], false, i);
        }
        draw_sea(&gf, t);
#endif
        if (B.g == G_WAIT) {
            panel_lines("WAITING FOR A FRIEND", "YOUR FRIENDS SEE YOUR INVITE");
            button(BT_CANCEL, TB_CANCEL, "CANCEL", false, NULL);
        } else {
            snprintf(b, sizeof b, "VS COMPUTER: WON %d  LOST %d", B.wins, B.losses);
            panel_lines("SEA BATTLE", b);
            title_buttons();
        }
        return;
    }
    if (B.g == G_PLAY && B.lp_on) {
        draw_aim(t);
        return;
    }
    if (sm) {
        draw_grid(&gm, true, t);
    }
    if (sf) {
        draw_grid(&gf, false, t);
    }
    grid_names(&gm, &gf, sm, sf);
    if (B.g == G_PLACE) {
        draw_place();
    } else if (B.g == G_PLAY) {
        snprintf(a, sizeof a, "%s",
                 B.my_turn && !B.waiting ? "YOUR SHOT"
                 : B.waiting             ? "THE SHELL FLIES ..."
                                         : "THE ENEMY SHOOTS");
        snprintf(b, sizeof b, "YOU SANK %d  LOST %d", B.foe_sunk, B.me_sunk);
        panel_lines(a, B.msg_t > 0 ? B.msg : b);
#if BOARD_BAR_SCREEN
        if (B.msg_t > 0) {
            mf_text_c(&MF_TINY, 320, 86, b, C_DIM, 255, 1);
        }
        if (B.my_turn && !B.waiting) {
            mf_text_c(&MF_SMALL, 320, 160, "TAP THE ENEMY WATERS TO AIM", C_DIM, 255, 1);
        }
#elif BOARD_ROUND
        // the cursor of the wheel (a turn or a push opens the loupe there)
        if (BOARD_WHEEL && sf && B.my_turn && !B.waiting) {
            const int x = gf.x + (B.cur % N) * gf.c, y = gf.y + (B.cur / N) * gf.c;
            gfx_frame_rect(x, y, gf.c + 1, gf.c + 1, 2, ((int)(t * 4) & 1) ? rgb(255, 230, 90) : rgb(255, 255, 255));
        }
#endif
    } else {
        snprintf(b, sizeof b, "YOU SANK %d  LOST %d", B.foe_sunk, B.me_sunk);
        panel_lines(B.won == 1   ? "YOU WIN!"
                    : B.won == 0 ? "YOU LOSE"
                                 : "YOUR FRIEND LEFT",
                    B.gt > 1.5f ? "TAP FOR THE TITLE" : b);
    }
}

// ---------------------------------------------------------------- the app
static void touch_reset(void);
// The settings panel (sysui.h): THE TITLE. In a match (the placing, the play) it is a hold of 1 s (PI_ACT2: the match
// is lost); in the wait for a friend and on the end card it is one tap (PI_ACT). With a friend it ends the line as the
// end of a match does (duel_stop: a BYE; the friend sees YOUR FRIEND LEFT). All the state of the match goes back to the
// start (the wins and the losses stay). The radio stays on for the lobby.
enum {
    SI_TITLE_RUN,
    SI_TITLE
};
static const pitem_t SB_ITEMS[] = {
        {PI_ACT2, SI_TITLE_RUN, 0, 0, {"THE TITLE", "מסך הפתיחה"}, NULL, NULL},
        {PI_ACT, SI_TITLE, 0, 0, {"THE TITLE", "מסך הפתיחה"}, NULL, NULL},
};
static bool sb_item_shown(int id)
{
    if (id == SI_TITLE_RUN) {
        return B.g == G_PLACE || B.g == G_PLAY;
    }
    return B.g == G_WAIT || B.g == G_OVER;
}
static void sb_item_set(int id, int v)
{
    (void)id;
    (void)v;
    duel_stop();  // (a BYE in a match or a join; no invite after it)
    const int w = B.wins, l = B.losses;
    memset(&B, 0, sizeof B);
    B.aim = B.hover = B.pick = -1;
    B.wins = w;
    B.losses = l;
    B.g = G_TITLE;
    shuffle(&B.me);
    touch_reset();
    guard_set(-1000, -1000, s_last - 100000);
    (void)input_wheel();
    (void)input_wheel_presses();
    audio_tune_stop();
}
static const pitems_t SB_PANEL = {
        SB_ITEMS, sizeof SB_ITEMS / sizeof SB_ITEMS[0], sb_item_shown, NULL, sb_item_set, NULL, NULL};
static void sb_enter(int64_t now)
{
    C_TXT = rgb(232, 238, 244);
    C_DIM = rgb(150, 170, 190);
    C_YEL = rgb(250, 210, 80);
    C_BG = rgb(8, 16, 28);
    art_load();
    memset(&B, 0, sizeof B);
    B.aim = B.hover = B.pick = -1;
    B.wins = (int)store_get("sb_w", 0);
    B.losses = (int)store_get("sb_l", 0);
    shuffle(&B.me);
    s_last = now;
    s_ninv = 0;
    guard_set(-1000, -1000, now - 100000);
    touch_reset();
    duel_open('S');
    (void)input_wheel();
    (void)input_wheel_presses();
    sysui_panel_items(&SB_PANEL);
    JPLAY(SB_OPEN);
}
// the 3D pictures out of the memory (the app closes)
static void art_free(void)
{
    for (int i = 0; i < SCACHE; i++) {
        for (int k = 0; k < NSHIP; k++) {
            for (int d = 0; d < 2; d++) {
                free(s_sc[i].p[k][d].c);
                s_sc[i].p[k][d].c = NULL;
            }
        }
    }
    for (int k = 0; k < NSHIP; k++) {
        for (int d = 0; d < 2; d++) {
            free(s_ship_src[k][d].idx);
            s_ship_src[k][d].idx = NULL;
        }
    }
    free(s_sea_src.idx);
    s_sea_src.idx = NULL;
    memset(s_sc, 0, sizeof s_sc);
    s_art_ok = false;
}
static void sb_leave(void)
{
    duel_close();
    audio_tune_stop();
    art_free();
}
static void to_title(void)
{
    if (!B.vs_cpu) {
        duel_stop();
    }
    B.g = G_TITLE;
    shuffle(&B.me);
    lp_close();
}
// TURN: the picked ship turns (a red frame, a shake and "no" when it cannot)
static void turn_pick(void)
{
    if (B.pick < 0 || B.pick >= NSHIP) {
        no_sound();
        return;
    }
    B.pick_t = 0.45f;
    B.pick_ok = turn_ship(&B.me, B.pick);
    if (B.pick_ok) {
        sfx_play(SFX_CLICK);
    } else {
        no_sound();
    }
}
// FIRE: a shot at the aim (no aim, a square that was shot: "no")
static void fire_aim(void)
{
    if (!B.lp_on || B.lp_hold || B.aim < 0 || B.foe.shot[B.aim / N][B.aim % N]) {
        no_sound();
        return;
    }
    if (!fire(B.aim % N, B.aim / N)) {
        no_sound();
    } else if (B.lp_on) {
        B.lp_hold = true;  // (the last shot of a match: the loupe is already shut)
    }
}
// the action of a button (the touch: guard says if it changes the screen)
static bool act(int id)
{
    if (id == BT_CPU) {
        start_place(true);
        return true;
    }
    if (id == BT_FRIEND) {
        duel_invite();
        B.g = G_WAIT;
        sfx_play(SFX_CLICK);
        return true;
    }
    if (id == BT_CANCEL) {
        duel_stop();
        B.g = G_TITLE;
        sfx_play(SFX_BACK);
        return true;
    }
    if (id >= BT_JOIN0 && id < BT_JOIN0 + 3 && id - BT_JOIN0 < s_ninv) {
        duel_join(s_inv[id - BT_JOIN0]);
        B.g = G_WAIT;
        sfx_play(SFX_CLICK);
        return true;
    }
    if (id == BT_SHUFFLE) {
        shuffle(&B.me);
        if (B.pick < NSHIP) {
            B.pick = -1;
        }
        B.pick_t = 0;
        sfx_play(SFX_CLICK);
        return false;
    }
    if (id == BT_TURN) {
        turn_pick();
        return false;
    }
    if (id == BT_FIRE) {
        fire_aim();
        return false;
    }
    if (id == BT_READY) {
        B.my_ready = true;
        B.pick = -1;
        B.pick_t = 0;
        sfx_play(SFX_CLICK);
        if (B.vs_cpu) {
            shuffle(&s_cpu);
            start_play();
            return true;
        }
        const uint8_t m[1] = {M_READY};
        duel_send(m, 1);
        if (B.foe_ready) {
            start_play();
        }
        return true;
    }
    return false;
}
// a tap (the mean point of a still touch, when the finger came off the glass at the time ms)
static void tap_at(int x, int y, int64_t ms)
{
    if (B.g == G_OVER) {
        if (B.gt > 1.5f) {
            to_title();
            guard_set(x, y, ms);
        }
        return;
    }
    const int id = s_nbtn ? button_at(x, y) : -1;
    if (id >= 0) {
        if (act(id)) {
            guard_set(x, y, ms);
        }
        return;
    }
    grid_t gm, gf;
    bool sm, sf;
    grids(&gm, &gf, &sm, &sf);
    if (B.g == G_PLACE) {
        // a tap near the fleet picks the nearest ship (TURN turns it)
        if (B.my_ready || !sm || !in_fleet(&gm, x, y)) {
            return;
        }
        const int i = ship_near(&gm, x, y);
        if (i >= 0 && i != B.pick) {
            B.pick = i;
            B.pick_t = 0;
            sfx_play(SFX_CLICK);
        }
        return;
    }
    if (B.g != G_PLAY || !B.my_turn || B.waiting) {
        return;
    }
    if (B.lp_on) {
        if (B.lp_hold) {
            return;
        }
        // a big square: the aim (a shot square: "no"); the small grid: the loupe goes there
        const int c = lp_cell_at(x, y);
        if (c >= 0) {
            if (B.foe.shot[c / N][c % N]) {
                no_sound();
            } else if (c != B.aim) {
                B.aim = B.cur = c;
                sfx_play(SFX_CLICK);
            }
            return;
        }
        const int m = small_cell_at(x, y);
        if (m >= 0) {
            lp_center(m);
            sfx_play(SFX_CLICK);
        }
        return;
    }
    // the first tap of a shot: on the enemy grid or near it: the loupe opens there (no aim, no shot)
    if (!sf) {
        return;
    }
    const int c = foe_cell_at(&gf, x, y);
    if (c >= 0) {
        lp_open(c);
        guard_set(x, y, ms);
    }
}
// THE TOUCH: one finger (the events of input.c follow the first finger). The mean point of the still points, each one
// weighted by its time (IN_TAP and IN_LONG are not used: no IN_TAP comes for a touch of 300-350 ms). A touch that moves
// more than SLOP px is a drag: on the loupe the sea follows the finger; on the small grid the loupe follows the finger.
enum {
    DR_NONE,
    DR_PAN,
    DR_SCRUB
};
static struct {
    bool on, drag, skip;  // down; a drag; skip: the guard (or a change of the screen) ignores this touch
    int x0, y0, x, y, kind,
            scr;            // the first point, the last point; kind: what a drag moves; scr: the screen at the start
    int64_t t, sx, sy, sw;  // the time of the last point; the sums of the still points (weight: ms)
    int lx0, ly0;           // the loupe at the start of a drag
} T;
static void touch_reset(void)
{
    memset(&T, 0, sizeof T);
}
static int screen_id(void)
{
    return B.g * 32 + B.lp_on * 16 + B.lp_hold * 8 + B.my_turn * 4 + B.my_ready * 2 + B.waiting;
}
static void mean_pt(int *x, int *y)
{
    if (T.sw > 0) {
        *x = (int)((T.sx + T.sw / 2) / T.sw);
        *y = (int)((T.sy + T.sw / 2) / T.sw);
    } else {
        *x = T.x;
        *y = T.y;
    }
}
static void add_still(int64_t ms)
{
    const int64_t w = ms - T.t;
    if (w > 0 && !T.drag) {
        T.sx += (int64_t)T.x * w;
        T.sy += (int64_t)T.y * w;
        T.sw += w;
    }
}
static void drag_to(int x, int y)
{
    if (T.kind == DR_PAN) {
        B.lp_x = clampi(T.lx0 - (int)lroundf((float)(x - T.x0) / LP_C), 0, N - LP_COLS);
        B.lp_y = clampi(T.ly0 - (int)lroundf((float)(y - T.y0) / LP_C), 0, N - LP_ROWS);
    } else if (T.kind == DR_SCRUB) {
        const int c = small_cell_at(x, y);
        if (c >= 0) {
            lp_center(c);
        }
    }
}
static void touch_down(const input_event_t *ev)
{
    memset(&T, 0, sizeof T);
    T.on = true;
    T.x0 = T.x = ev->x;
    T.y0 = T.y = ev->y;
    T.t = ev->t_ms;
    T.scr = screen_id();
    T.skip = guarded(ev->x, ev->y, ev->t_ms) || (B.g == G_PLAY && B.lp_hold);
    B.hover = -1;
    if (T.skip || B.g != G_PLAY || !B.lp_on || !B.my_turn || B.waiting) {
        return;
    }
    if (in_loupe(ev->x, ev->y)) {
        T.kind = DR_PAN;
        T.lx0 = B.lp_x;
        T.ly0 = B.lp_y;
        B.hover = lp_cell_at(ev->x, ev->y);
    } else if (small_cell_at(ev->x, ev->y) >= 0) {
        T.kind = DR_SCRUB;
    }
}
static void touch_move(const input_event_t *ev)
{
    if (!T.on || T.skip) {
        return;
    }
    add_still(ev->t_ms);
    T.x = ev->x;
    T.y = ev->y;
    T.t = ev->t_ms;
    if (!T.drag && (abs(T.x - T.x0) > SLOP || abs(T.y - T.y0) > SLOP)) {
        T.drag = true;
        B.hover = -1;
    }
    if (screen_id() != T.scr) {
        T.skip = true;
        B.hover = -1;
        return;
    }
    if (T.drag) {
        drag_to(T.x, T.y);
    } else if (T.kind == DR_PAN) {
        int mx, my;
        mean_pt(&mx, &my);
        B.hover = lp_cell_at(mx, my);
    }
}
static void touch_up(const input_event_t *ev)
{
    if (!T.on) {
        return;
    }
    T.on = false;
    B.hover = -1;
    if (T.skip || screen_id() != T.scr) {
        return;
    }
    add_still(ev->t_ms);
    T.t = ev->t_ms;
    if (T.drag) {
        drag_to(ev->x, ev->y);
        return;
    }
    int x, y;
    mean_pt(&x, &y);
    tap_at(x, y, ev->t_ms);
}
static void sb_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        touch_down(ev);
    } else if (ev->kind == IN_MOVE) {
        touch_move(ev);
    } else if (ev->kind == IN_UP) {
        touch_up(ev);
    }
}
// the Watcher: the loupe opens at the cursor, with the aim on it (on the first square from it that was not shot)
static void lp_open_cursor(void)
{
    for (int k = 0; k < N * N && B.foe.shot[B.cur / N][B.cur % N]; k++) {
        B.cur = (B.cur + 1) % (N * N);
    }
    lp_open(B.cur);
    B.aim = B.cur;
}
// the settings panel (main.c has a weak default): no duel with a friend = a solo game, which stops under the panel
bool sea_solo(void)
{
    return duel_state() == DUEL_OFF;
}

static void sb_update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    B.gt += dt;
    B.shot_t += dt;
    if (B.msg_t > 0) {
        B.msg_t -= dt;
    }
    if (B.pick_t > 0) {
        B.pick_t -= dt;
    }
    duel_poll(now);
    s_ninv = duel_invites(s_inv, 3);
    const int ds = duel_state();
    if (!B.vs_cpu || B.g == G_WAIT) {
        if (B.g == G_WAIT && ds == DUEL_ON) {
            start_place(false);
        }
        if (B.g == G_WAIT && ds == DUEL_GONE) {
            B.g = G_TITLE;
        }
        if ((B.g == G_PLACE || B.g == G_PLAY) && !B.vs_cpu && ds != DUEL_ON) {
            end_match(-1);
        }
        if (!B.vs_cpu && (B.g == G_PLACE || B.g == G_PLAY)) {
            net_step();
        }
    }
    if (B.g == G_PLAY && B.vs_cpu && !B.my_turn) {
        B.cpu_t += dt;
        if (B.cpu_t > 0.9f) {
            cpu_fire();
        }
    }
    // the Watcher: the wheel moves the aim (the loupe opens and follows it) or the pick of the placing; a push fires,
    // turns the picked ship, or does SHUFFLE or READY
    const int ws = input_wheel(), wp = input_wheel_presses();
    const bool aiming = B.g == G_PLAY && B.my_turn && !B.waiting && !B.lp_hold;
    if (ws) {
        if (B.g == G_PLACE && !B.my_ready) {
            B.pick = B.pick < 0 ? 0 : ((B.pick + ws) % NFOCUS + NFOCUS) % NFOCUS;
            B.pick_t = 0;
        } else if (aiming && !B.lp_on) {
            lp_open_cursor();
        } else if (aiming) {
            B.aim = B.aim < 0 ? B.cur : ((B.aim + ws) % (N * N) + N * N) % (N * N);
            B.cur = B.aim;
            lp_follow();
        } else {
            B.cur = ((B.cur + ws) % (N * N) + N * N) % (N * N);
        }
    }
    if (wp > 0) {
        if (B.g == G_PLACE && !B.my_ready) {
            if (B.pick == FOCUS_SHUFFLE) {
                act(BT_SHUFFLE);
            } else if (B.pick == FOCUS_READY) {
                act(BT_READY);
            } else {
                turn_pick();
            }
        } else if (aiming && !B.lp_on) {
            lp_open_cursor();
        } else if (aiming) {
            fire_aim();
        } else if (B.g == G_OVER && B.gt > 1.5f) {
            to_title();
        }
    }
    // the loupe closes when the turn ends (after the shot showed in it), at the end and at a link loss
    if (B.lp_on &&
        (B.g != G_PLAY || (B.lp_hold ? !B.shot_mine || (B.shot_t > 1.0f && !B.waiting) : !B.my_turn || B.waiting))) {
        lp_close();
    }
}
#include "tfdraw/tfdraw.h"
// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set in each app that draws an icon; all static and unused is no warning)
#define IC_FN static __attribute__((unused))
static struct {
    uint16_t *fb;
    int w, h, c[4];
} ic_keep __attribute__((unused));
// start an icon: tfdraw draws on the screen and only in the box; ic_end gives tfdraw its old target and clip back
IC_FN void ic_begin(int x, int y, int s)
{
    ic_keep.fb = tfd_fb;
    ic_keep.w = tfd_w;
    ic_keep.h = tfd_h;
    ic_keep.c[0] = tfd_cx0;
    ic_keep.c[1] = tfd_cy0;
    ic_keep.c[2] = tfd_cx1;
    ic_keep.c[3] = tfd_cy1;
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
}
IC_FN void ic_end(void)
{
    tfd_target(ic_keep.fb, ic_keep.w, ic_keep.h);
    tfd_clip(ic_keep.c[0], ic_keep.c[1], ic_keep.c[2], ic_keep.c[3]);
}
// the rows j0 .. j1 - 1 of the round tile (the corner radius of the menu frame), a gradient from c0 to c1
IC_FN void ic_band(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    int r = s / 8 + 2;
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        uint16_t c = tfd_mix(c0, c1, j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        if (dy <= 0) {
            tfd_hspan(x, x + s, y + j, c, 256);
            continue;
        }
        int in = tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy)));
        tfd_hspan(x + in + 1, x + s - in - 1, y + j, c, 256);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, y + j, c, a);
            tfd_px(x + s - 1 - k, y + j, c, a);
        }
    }
}
// the box inside the round corners (a shape in it does not cover a corner)
IC_FN void ic_inner(int x, int y, int s)
{
    int k = (s / 8 + 2) * 3 / 10 + 1;
    tfd_clip(x + k, y + k, x + s - k, y + s - k);
}
// a filled ellipse with a smooth edge
IC_FN void ic_oval(float cx, float cy, float rx, float ry, uint16_t c, int a)
{
    if (rx < 0.5f || ry < 0.5f) {
        return;
    }
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = fabsf(yy + 0.5f - cy), v = ry + 0.5f - d;
        if (v <= 0) {
            continue;
        }
        float q = d < ry ? d / ry : 1, hw = rx * sqrtf(1 - q * q), l = cx - hw, r = cx + hw;
        int aa = v >= 1 ? a : (int)(a * v);
        int il = tfd_ce(l), ir = tfd_fl(r);
        if (il > ir) {
            tfd_px(ir, yy, c, (int)(aa * (r - l)));
            continue;
        }
        tfd_hspan(il, ir, yy, c, aa);
        tfd_px(il - 1, yy, c, (int)(aa * (il - l)));
        tfd_px(ir, yy, c, (int)(aa * (r - ir)));
    }
}
// a ball in the light from the upper left: the dark rim, the body, the shine
IC_FN void ic_ball(float cx, float cy, float r, uint16_t dark, uint16_t body, uint16_t shine)
{
    tfd_disc(cx, cy, r, dark, 256);
    tfd_disc(cx - r * 0.1f, cy - r * 0.12f, r * 0.84f, body, 256);
    tfd_disc(cx - r * 0.34f, cy - r * 0.38f, r * 0.27f, shine, 200);
}
// a shape of n points (x, y pairs round 0, 0), turned by ang, k times bigger, at cx, cy
IC_FN void ic_shape(const float *p, int n, float cx, float cy, float k, float ang, uint16_t c, int a)
{
    float q[64], cs = fcos(ang) * k, sn = fsin(ang) * k;
    if (n > 32) {
        n = 32;
    }
    for (int i = 0; i < n; i++) {
        q[2 * i] = cx + p[2 * i] * cs - p[2 * i + 1] * sn;
        q[2 * i + 1] = cy + p[2 * i] * sn + p[2 * i + 1] * cs;
    }
    tfd_poly(q, n, c, a);
}
// A store app shows the frames of icon.bin, with at most 255 colours for all 16 frames: mk_app.py drops the low bits
// of each colour until they fit (most icons: 3 bits of red, 4 of green, 3 of blue). A colour on that grid stays as it
// is: ic_g gives the nearest one. ic_dband makes a gradient of grid colours with an ordered dither (few colours, no
// bands).
IC_FN uint16_t ic_g(int r, int g, int b)
{
    r = (r + 16) / 32 * 32;
    g = (g + 8) / 16 * 16;
    b = (b + 16) / 32 * 32;
    return rgb((uint8_t)(r > 224 ? 224 : r), (uint8_t)(g > 240 ? 240 : g), (uint8_t)(b > 224 ? 224 : b));
}
IC_FN void ic_dband(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    static const uint8_t IC_BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int r0, g0, b0, r1, g1, b1, r = s / 8 + 2;
    tfd_unrgb(c0, &r0, &g0, &b0);
    tfd_unrgb(c1, &r1, &g1, &b1);
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        int yy = y + j;
        if (yy < tfd_cy0 || yy >= tfd_cy1 || (unsigned)yy >= (unsigned)tfd_h) {
            continue;
        }
        // (the colour of the row in 1/16 steps of a colour value; a grid step is 32 = 512 for red and blue, 16 = 256
        // for green)
        int t = j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0;
        int fr = r0 * 16 + ((r1 - r0) * 16 * t >> 8), fg = g0 * 16 + ((g1 - g0) * 16 * t >> 8),
            fb = b0 * 16 + ((b1 - b0) * 16 * t >> 8);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        int in = dy > 0 ? tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy))) : -1;
        uint16_t *row = tfd_fb + yy * tfd_w;
        for (int i = in + 1; i < s - in - 1; i++) {
            int xx = x + i;
            if (xx < tfd_cx0 || xx >= tfd_cx1 || (unsigned)xx >= (unsigned)tfd_w) {
                continue;
            }
            int th = IC_BAYER[(j & 3) * 4 + (i & 3)];
            int lr = (fr >> 9) + (((fr >> 5) & 15) > th), lg = (fg >> 8) + (((fg >> 4) & 15) > th),
                lb = (fb >> 9) + (((fb >> 5) & 15) > th);
            row[xx] = rgb((uint8_t)(lr > 7 ? 224 : lr * 32), (uint8_t)(lg > 15 ? 240 : lg * 16),
                          (uint8_t)(lb > 7 ? 224 : lb * 32));
        }
        uint16_t c = ic_g(fr >> 4, fg >> 4, fb >> 4);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, yy, c, a);
            tfd_px(x + s - 1 - k, yy, c, a);
        }
    }
}
// a span of one row from l to r (in pixels, with soft ends)
IC_FN void ic_span(float l, float r, int yy, uint16_t c, int a)
{
    if (r <= l) {
        return;
    }
    int il = tfd_ce(l), ir = tfd_fl(r);
    if (il > ir) {
        tfd_px(ir, yy, c, (int)(a * (r - l)));
        return;
    }
    tfd_hspan(il, ir, yy, c, a);
    tfd_px(il - 1, yy, c, (int)(a * (il - l)));
    tfd_px(ir, yy, c, (int)(a * (r - ir)));
}
// the ring of an ellipse (rx, ry: the outer radii; w: its width), made of spans: cheap also when it is big
IC_FN void ic_ering(float cx, float cy, float rx, float ry, float w, uint16_t c, int a)
{
    float ix = rx - w, iy = ry - w;
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = yy + 0.5f - cy, qo = d / ry;
        if (qo * qo >= 1) {
            continue;
        }
        float ho = rx * sqrtf(1 - qo * qo), qi = iy > 0 ? d / iy : 2;
        if (qi * qi >= 1) {
            ic_span(cx - ho, cx + ho, yy, c, a);
            continue;
        }
        float hi = ix * sqrtf(1 - qi * qi);
        ic_span(cx - ho, cx - hi, yy, c, a);
        ic_span(cx + hi, cx + ho, yy, c, a);
    }
}
// the icon: the sea with its grid and the 3D battleship; the aim finds a square of the ship, a shell falls on it, it
// explodes, the ship burns and smokes (a loop of 2 s). It is drawn live in the menu: spans, discs and the ship picture.
static void sb_icon(int x, int y, int s)
{
    art_load();
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f;
    ic_band(x, y, s, 0, s, rgb(26, 74, 136), rgb(12, 40, 86));
    ic_inner(x, y, s);
    for (int k = 1; k < 5; k++) {
        tfd_rect(x + k * s / 5, y, 1, s, rgb(10, 34, 70), 256);
        tfd_rect(x, y + k * s / 5, s, 1, rgb(10, 34, 70), 256);
    }
    // (small waves that move)
    for (int k = 0; k < 6; k++) {
        float wx = x + fmodf(k * 31 + ph * 40, 90) * u + 5 * u, wy = y + (8 + (k * 37) % 84) * u;
        tfd_rect((int)wx, (int)wy, (int)(5 * u), (int)(1 * u) + 1, rgb(70, 130, 190), 256);
    }
    tfd_clip(x, y, x + s, y + s);
    // the ship (the picture of the game)
    const grid_t g = {x, y, s / 5, 0};
    s_cl_x0 = x;
    s_cl_y0 = y;
    s_cl_x1 = x + s;
    s_cl_y1 = y + s;
    const ship_t sh = {0, 2, 0, 4, 0};
    draw_ship(&g, &sh, false, 1);
    s_cl_x0 = s_cl_y0 = 0;
    s_cl_x1 = s_cl_y1 = 4096;
    // the aim and the shell (falls in the first 0.5 s), the blast, then fire and smoke on the ship
    float tx = x + 3.5f * s / 5, ty = y + 2.5f * s / 5;
    if (ph < 0.45f) {
        float t = ph / 0.45f, rr = (14 - 6 * t) * u;
        tfd_ring(tx, ty, rr, 1.6f * u, rgb(255, 240, 200), 256);
        tfd_rect((int)(tx - rr - 4 * u), (int)(ty - 0.6f * u), (int)(2 * rr + 8 * u), (int)(1.4f * u) + 1,
                 rgb(255, 240, 200), 256);
        tfd_rect((int)(tx - 0.6f * u), (int)(ty - rr - 4 * u), (int)(1.4f * u) + 1, (int)(2 * rr + 8 * u),
                 rgb(255, 240, 200), 256);
        float sx = tx + (1 - t) * 30 * u, sy = ty - (1 - t) * 46 * u;
        tfd_rect((int)(sx + 1 * u), (int)(sy - 8 * u), (int)(1.6f * u) + 1, (int)(7 * u), rgb(200, 220, 240), 150);
        tfd_disc(sx, sy, (2 + 1.6f * t) * u, rgb(30, 30, 36), 256);
    } else if (ph < 0.75f) {
        float t = (ph - 0.45f) / 0.3f;
        tfd_disc(tx, ty, (6 + 16 * t) * u, rgb(255, 120, 30), (int)(256 * (1 - t * 0.6f)));
        tfd_disc(tx, ty, (4 + 9 * t) * u, rgb(255, 210, 80), 256);
        tfd_disc(tx, ty, (2 + 4 * t) * u, rgb(255, 250, 220), 256);
        for (int k = 0; k < 6; k++) {
            float a = k * 1.047f + 0.5f, d = (8 + 18 * t) * u;
            tfd_disc(tx + fcos(a) * d, ty + fsin(a) * d - t * 6 * u, 1.6f * u, rgb(255, 180, 60), 256);
        }
    }
    if (ph >= 0.6f) {
        float t = (ph - 0.6f) / 0.4f, fl = fsin(ph * 60) * 1.2f * u;
        for (int k = 0; k < 3; k++) {
            float st = fmodf(t + k * 0.33f, 1.0f);
            tfd_disc(tx - 2 * u + st * 6 * u, ty - 6 * u - st * 26 * u, (3 + st * 5) * u, rgb(70, 74, 82),
                     (int)(200 * (1 - st)));
        }
        tfd_disc(tx, ty - 2 * u, 5 * u + fl, rgb(240, 90, 30), 256);
        tfd_disc(tx, ty - 1 * u, 2.6f * u, rgb(255, 220, 90), 256);
    }
    ic_end();
}
const app_t app_sea = {
        .name = "SEA BATTLE",
        .landscape = true,
        .enter = sb_enter,
        .leave = sb_leave,
        .input = sb_input,
        .update = sb_update,
        .render = sb_render,
        .draw_icon = sb_icon,
        .round = APP_ROUND_HI,
        .wheel = true,
};
