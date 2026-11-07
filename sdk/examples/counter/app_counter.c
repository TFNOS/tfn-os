// TFN OS SDK example 3: COUNTER, a tally counter (a utility).
//
// "+" and "-" change the value by the step. RESET needs a hold of 1 s with a fill bar: no action that you cannot undo
// comes from one tap. The value stays in the saves, also after the board restarts.
//
// What this example shows:
//   - a utility with big keys (visual.md section 5): the thumb keys are 150 x 150 px on the 3.49
//   - the project rule for an action that you cannot undo: a hold of 1 s with a fill (not a tap, not a long press of 350 ms)
//   - the saves: the value is written 1.5 s after the last change and in leave(), not in each frame (behave.md section 4)
//   - the settings panel: two choices with save keys (LANGUAGE, STEP) (behave.md section 7)
//   - English and Hebrew: the Rubik faces KF_* draw Hebrew, the Bungee faces MF_* draw the number (behave.md section 8)
//   - the layouts of the 3.49 bar and of the 320 x 240 frame, and a render() that paints the whole frame
//
// Build (from tfn-os/):
//   python sdk/tools/mk_app.py sdk/examples/counter/app_counter.c --sym app_counter --name COUNTER --group UTIL --kinds lcd349,cores3 --nosign
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"

// ---------------------------------------------------------------- the layout of each board kind
typedef struct { int x, y, w, h; } rect_t;

#if BOARD_BAR_SCREEN
static const rect_t R_MINUS = {16, 14, 160, 152};
static const rect_t R_PLUS  = {464, 14, 160, 152};
static const rect_t R_NUM   = {190, 14, 260, 98};
static const rect_t R_RESET = {250, 122, 140, 44};
#define NUM_BASE    80                      // the baselines of the texts
#define STEP_BASE   104
#define SIGN_LEN    64                      // the length of the arms of the + and - signs
#define SIGN_THICK  16
#else
static const rect_t R_MINUS = {16, 106, 140, 70};
static const rect_t R_PLUS  = {164, 106, 140, 70};
static const rect_t R_NUM   = {16, 8, 288, 88};
static const rect_t R_RESET = {90, 188, 140, 44};
#define NUM_BASE    69
#define STEP_BASE   90
#define SIGN_LEN    44
#define SIGN_THICK  12
#endif

#define HOLD_MS     1000                    // the hold of RESET
#define SAVE_DELAY  1500                    // ms after the last change: write the value
#define MAX_VALUE   99999

#define KEY_VAL     "counter_val"           // the save keys start with the prefix of the app (<id>_)
#define KEY_STEP    "counter_step"
#define KEY_LANG    "counter_lang"

enum { B_NONE, B_MINUS, B_PLUS, B_RESET };

// the settings panel
enum { HI_LANG, HI_STEP };
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const STEP_VALS[] = {"1", "1", "5", "5", "10", "10"};
static const int STEPS[3] = {1, 5, 10};
static const pitem_t ITEMS[] = {
    {PI_PICK, HI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_VALS, KEY_LANG},       // the default is Hebrew (def = 1)
    {PI_PICK, HI_STEP, 3, 0, {"STEP", "צעד"}, STEP_VALS, KEY_STEP},
};

// ---------------------------------------------------------------- the state
static int s_value;
static int s_step_i;                        // the index in STEPS
static bool s_he;
static bool s_dirty;                        // the value changed, and the save did not come yet
static int64_t s_change_ms;
static int s_down;                          // the key that the finger holds now (B_*)
static int64_t s_hold_start;                // the time when the hold of RESET started (0: no hold)
static int64_t s_hint_ms = -10000;          // a short touch on RESET: show the hint for 1.2 s
static int64_t s_bump_ms = -10000;          // the time of the last change (the number moves)
static int s_bump_dir;                      // + 1 or - 1
static int64_t s_flash_ms = -10000;         // the time of the last reset

// the text of the current language
static const char *tr(const char *en, const char *he) { return s_he ? he : en; }

static bool in_rect(const rect_t *r, int x, int y, int m)
{
    return x >= r->x - m && x < r->x + r->w + m && y >= r->y - m && y < r->y + r->h + m;
}

// The key under a touch. On the 320 x 240 frame the two thumb keys are side by side with a gap of 8 px: the left half
// of the row is "-" and the right half is "+" (the gap does not lose a touch).
static int key_at(int x, int y)
{
    if (in_rect(&R_RESET, x, y, 6)) return B_RESET;
#if BOARD_BAR_SCREEN
    if (in_rect(&R_MINUS, x, y, 6)) return B_MINUS;
    if (in_rect(&R_PLUS, x, y, 6)) return B_PLUS;
#else
    if (y >= R_MINUS.y - 6 && y < R_MINUS.y + R_MINUS.h + 6) {
        if (x >= R_MINUS.x - 6 && x < gfx_w / 2) return B_MINUS;
        if (x >= gfx_w / 2 && x < R_PLUS.x + R_PLUS.w + 6) return B_PLUS;
    }
#endif
    return B_NONE;
}

static void save_now(void)
{
    if (!s_dirty) return;
    store_set(KEY_VAL, (uint32_t)s_value);  // store_set() writes the flash only when the value changes
    s_dirty = false;
}

static void change(int dir, int64_t now)
{
    int v = s_value + dir * STEPS[s_step_i];
    if (v < 0 || v > MAX_VALUE) {           // the end of the range: a low sound, no change
        sfx_play(SFX_BUMP);
        return;
    }
    s_value = v;
    s_dirty = true;
    s_change_ms = now;
    s_bump_ms = now;
    s_bump_dir = dir;
    sfx_tone(dir > 0 ? 880 : 620, dir > 0 ? 1100 : 480, 45, W_PULSE, 30);
}

static void do_reset(int64_t now)
{
    s_value = 0;
    s_dirty = true;
    s_change_ms = now;
    s_flash_ms = now;
    sfx_play(SFX_BACK);
}

// ---------------------------------------------------------------- the app functions
static void item_set(int id, int v)
{
    if (id == HI_LANG) s_he = v == 1;       // (the panel wrote the save key already)
    if (id == HI_STEP) s_step_i = v;
}
static const pitems_t PANEL = {ITEMS, 2, NULL, NULL, item_set, NULL, NULL};

static void enter(int64_t now)
{
    (void)now;
    s_value = (int)store_get(KEY_VAL, 0);   // the value from the last open
    if (s_value < 0 || s_value > MAX_VALUE) s_value = 0;
    s_step_i = (int)store_get(KEY_STEP, 0);
    if (s_step_i < 0 || s_step_i > 2) s_step_i = 0;
    s_he = store_get(KEY_LANG, 1) == 1;
    s_dirty = false;
    s_down = B_NONE;
    s_hold_start = 0;
    s_hint_ms = s_bump_ms = s_flash_ms = -10000;
    sysui_panel_items(&PANEL);
}

static void leave(void)
{
    save_now();                             // the value must stay: write it before the app closes
}

static void input(const input_event_t *ev)
{
    switch (ev->kind) {
    case IN_DOWN: {
        // "+" and "-" can be undone by the other key, so they act at once (a counter must feel fast)
        int k = key_at(ev->x, ev->y);
        s_down = k;
        if (k == B_PLUS) change(+1, ev->t_ms);
        else if (k == B_MINUS) change(-1, ev->t_ms);
        else if (k == B_RESET) s_hold_start = ev->t_ms;      // RESET: only the hold counts
        break;
    }
    case IN_MOVE:
        // the finger left the RESET key: the hold stops, nothing happens
        if (s_down == B_RESET && key_at(ev->x, ev->y) != B_RESET) { s_hold_start = 0; s_down = B_NONE; }
        break;
    case IN_UP:
        // a touch that ends before 1 s only shows the hint: a short tap changes nothing
        if (s_down == B_RESET && s_hold_start && ev->t_ms - s_hold_start < HOLD_MS) s_hint_ms = ev->t_ms;
        s_hold_start = 0;
        s_down = B_NONE;
        break;
    default:
        break;
    }
}

static void update(int64_t now)
{
    if (s_hold_start && now - s_hold_start >= HOLD_MS) {
        s_hold_start = 0;                   // done: the finger can stay down, nothing repeats
        do_reset(now);
    }
    if (s_dirty && now - s_change_ms > SAVE_DELAY) save_now();      // not in each frame, not in each tap
}

// ---------------------------------------------------------------- drawing
static void draw_sign(const rect_t *r, bool plus, uint16_t col)
{
    int cx = r->x + r->w / 2, cy = r->y + r->h / 2;
    gfx_fill_rect(cx - SIGN_LEN / 2, cy - SIGN_THICK / 2, SIGN_LEN, SIGN_THICK, col);
    if (plus) gfx_fill_rect(cx - SIGN_THICK / 2, cy - SIGN_LEN / 2, SIGN_THICK, SIGN_LEN, col);
}

static void draw_key(const rect_t *r, int which, uint16_t base, uint16_t dark)
{
    bool down = s_down == which;
    ui_rrect(r->x, r->y + 5, r->w, r->h, 18, dark);                    // a hard shadow under the key
    ui_rrect(r->x, r->y + (down ? 4 : 0), r->w, r->h, 18, down ? ui_mix(base, dark, 110) : base);
    rect_t s = {r->x, r->y + (down ? 4 : 0), r->w, r->h};
    draw_sign(&s, which == B_PLUS, rgb(255, 255, 255));
}

static void render(int64_t now)
{
    gfx_fill(rgb(14, 22, 30));                                          // the whole frame, first

    // the number panel; the number jumps 5 px on a change and comes back in 150 ms
    bool flash = now - s_flash_ms < 400;
    ui_rrect(R_NUM.x, R_NUM.y, R_NUM.w, R_NUM.h, 18, flash ? rgb(120, 50, 56) : rgb(26, 42, 56));
    int jump = 0;
    if (now - s_bump_ms < 150) jump = -s_bump_dir * (int)(5 * (150 - (now - s_bump_ms)) / 150);
    char b[16];
    snprintf(b, sizeof b, "%d", s_value);
    mf_text_oc(&MF_N34, R_NUM.x + R_NUM.w / 2, NUM_BASE + jump, b, rgb(255, 255, 255), rgb(10, 16, 22), 2, 2);
    snprintf(b, sizeof b, "%s %d", tr("STEP", "צעד"), STEPS[s_step_i]);
    ui_text_c(&KF_LABEL, R_NUM.x + R_NUM.w / 2, STEP_BASE, b, rgb(150, 180, 200));

    draw_key(&R_MINUS, B_MINUS, rgb(80, 130, 245), rgb(30, 50, 110));
    draw_key(&R_PLUS, B_PLUS, rgb(60, 190, 120), rgb(20, 80, 52));

    // the RESET key: the red fill grows from the left while the finger holds
    ui_rrect(R_RESET.x, R_RESET.y, R_RESET.w, R_RESET.h, 14, rgb(70, 28, 34));
    if (s_hold_start) {
        int w = (int)((now - s_hold_start) * R_RESET.w / HOLD_MS);
        if (w > R_RESET.w) w = R_RESET.w;
        if (w >= 28) ui_rrect(R_RESET.x, R_RESET.y, w, R_RESET.h, 14, rgb(235, 80, 90));
    }
    const char *label = tr("RESET", "איפוס");
    if (s_hold_start) label = tr("HOLD", "החזק");
    else if (now - s_hint_ms < 1200) label = tr("HOLD 1 S", "החזק שנייה");   // a short touch: the hint
    ui_text_c(&KF_NAME, R_RESET.x + R_RESET.w / 2, R_RESET.y + R_RESET.h / 2 + 6, label, rgb(255, 255, 255));
}

// The menu icon: tally marks that appear one by one, then the slash, then it starts again (a period of 2 s).
// Only gfx_* shapes; the whole box.
static void icon(int x, int y, int s)
{
    float u = fmodf(icon_time(), 2.0f);                                 // 0..2 s
    gfx_fill_rect(x, y, s, s, rgb(24, 40, 52));
    gfx_fill_rect(x + s / 12, y + s / 12, s - s / 6, s / 10, rgb(60, 190, 120));      // a green top strip
    int bw = s / 12 > 3 ? s / 12 : 3;
    for (int i = 0; i < 4; i++) {
        if (u < 0.15f + i * 0.28f) continue;
        gfx_fill_rect(x + s * 22 / 100 + i * s * 15 / 100, y + s * 30 / 100, bw, s * 46 / 100, rgb(240, 240, 250));
    }
    if (u > 1.27f) {                                                    // the slash grows along the diagonal
        int n = (int)((u - 1.27f) / 0.5f * 14);
        if (n > 14) n = 14;
        for (int i = 0; i < n; i++) {
            int px = x + s * 14 / 100 + i * s * 70 / 100 / 14, py = y + s * 70 / 100 - i * s * 38 / 100 / 14;
            gfx_fill_circle(px, py, s / 20 > 2 ? s / 20 : 2, rgb(255, 110, 110));
        }
    }
}

const app_t app_counter = {
    .name = "COUNTER",
    .landscape = true,                      // 640 x 172 on the 3.49, 320 x 240 on the other boards
    .enter = enter,
    .leave = leave,
    .input = input,
    .update = update,
    .render = render,
    .draw_icon = icon,
    .round_fit = true,
};
