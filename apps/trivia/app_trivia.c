// TRIVIA (GAMES): a trivia game in Hebrew with 3 levels and a game with a friend.
// 684 own questions in 12 subjects (tools/bank, tools/build_bank.py), 3 levels, a
// round of 10 questions with a timer, points for the speed and a streak; no question comes again until its level asked
// all of them. One player, two players on one board (turns), or a friend on a second board (the same question at the
// same time; the first right answer gets a bonus). The parts: tv_text.inc (the Hebrew text), tv_draw.inc (the look),
// tv_data.inc (the bank, the save), tv_game.inc (the rules), tv_net.inc (two boards), tv_ui.inc (the screens).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_random.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "miniz.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/net2.h"
#include "tfn/settings.h"
#include "tfn/storage.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "jingle/jingle.h"
#include "tfsfx/tfsfx.h"
#include "tv_art.h"
#include "tv_bank.h"
#include "tv_sfx.h"

enum {
    FX_TAP,
    FX_RIGHT,
    FX_WRONG,
    FX_TICK,
    FX_WHOOSH,
    FX_STREAK,
    FX_WIN,
    FX_LOSE,
    FX_FIRST,
    FX_N
};
static const char *const FX_NAMES[FX_N] = {"tap", "right", "wrong", "tick", "whoosh", "streak", "win", "lose", "first"};

#include "tv_text.inc"
#include "tv_draw.inc"
#include "tv_data.inc"
#include "tv_game.inc"
#include "tv_net.inc"
#include "tv_ui.inc"

// the opening tune of TRIVIA: a quick rising call of a game show (a bright pulse over a triangle bass)
static const tune_note_t OPENING[] = {
        JN(0, 110, J_G4, W_PULSE, 22),         JN(110, 110, J_C5, W_PULSE, 22), JN(220, 110, J_E5, W_PULSE, 22),
        JN(330, 160, J_G5, W_PULSE, 24),       JN(520, 110, J_E5, W_PULSE, 20), JN(630, 520, J_C6, JHOLD(W_PULSE), 22),
        JN(0, 220, J_C3, W_TRI, 50),           JN(330, 160, J_G3, W_TRI, 50),   JN(630, 520, J_C3, JHOLD(W_TRI), 50),
        JN(630, 520, J_E5, JHOLD(W_SINE), 16),
};

// ---------------------------------------------------------------- the panel
enum {
    MI_SOUND,
    MI_TIME,
    MI_QUIT
};
static const char *const SOUND_VALS[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const TIME_VALS[] = {"NORMAL", "רגיל", "LONG", "ארוך"};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_SOUND, 2, 0, {"SOUND", "צלילים"}, SOUND_VALS, "trivia_snd"},
        {PI_PICK, MI_TIME, 2, 0, {"TIME", "זמן לשאלה"}, TIME_VALS, "trivia_time"},
        {PI_ACT2, MI_QUIT, 0, 0, {"END ROUND", "סיום הסבב"}, NULL, NULL},
};
static int64_t s_now;
static bool item_shown(int id)
{
    return id != MI_QUIT || s_st == ST_Q || s_st == ST_PASS || s_st == ST_END || s_st == ST_LOBBY;
}
static void item_set(int id, int v)
{
    if (id == MI_SOUND) {
        s_sound = v == 0;
    } else if (id == MI_TIME) {
        s_time_mul = v == 1 ? 16 : 10;
    } else if (id == MI_QUIT) {
        if (G.mode != MODE_NET && (s_st == ST_Q || s_st == ST_PASS)) {
            round_drop();
        }
        net_off();
        part_clear();
        theme_to(-1);
        go(ST_TITLE, s_now);
    }
}
static const pitems_t PANEL = {ITEMS, 3, item_shown, NULL, item_set, NULL, NULL};

// ---------------------------------------------------------------- the app
static int64_t s_last, s_touch;

static void enter(int64_t now)
{
    s_now = s_last = s_touch = now;
    s_sound = store_get("trivia_snd", 0) == 0;
    s_time_mul = store_get("trivia_time", 0) == 1 ? 16 : 10;
    s_net = N2_OFF;
    s_me = 0;
    memset(&G, 0, sizeof G);
    lay_make();
    bokeh_init();
    part_clear();
    theme_to(-1);
    for (int i = 0; i < 3; i++) {
        s_th[i] = s_th_to[i];
    }
    save_load();
    const bool ok = bank_load() && art_load(L.isz_badge) && art_load(L.isz_btn) && art_load(0) && art_load(3) &&
                    art_load(TV_ART_SIZES);
    if (L.kind == LK_TAB || L.kind == LK_ROUND) {
        art_load(2);
    }
    if (L.kind != LK_BAR) {
        art_load(1);
    }
    quest_get(-1, &s_q);
    go(ok ? ST_TITLE : ST_NOMEM, now);
    tfsfx_load_mem(TV_SFX_DATA, TV_SFX_LEN, FX_NAMES, FX_N, 41);
    if (s_sound) {
        JPLAY(OPENING);
    }
    sysui_panel_items(&PANEL);
    // the invite notice of a friend (the argument "friends"): the lobby at once
    if (ok && !strcmp(app_launch_arg(), "friends")) {
        s_mode_pick = MODE_NET;
        G.lvl = s_lvl = 0;
        net_lobby(now);
    }
}
static void leave(void)
{
    if (G.mode != MODE_NET && G.n && s_st == ST_Q) {
        save_round();
    } else {
        save_write();
    }
    net_off();
    audio_tune_stop();
    tfsfx_free();
    art_free();
    free(s_bank);
    s_bank = NULL;
}
static void input_ev(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        s_touch = ev->t_ms;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    const int x = ev->x, y = ev->y;
    const int64_t now = ev->t_ms;
    switch (s_st) {
    case ST_TITLE:
        input_title(x, y, now);
        break;
    case ST_LEVEL:
        input_level(x, y, now);
        break;
    case ST_LOBBY:
        if (!net2_lobby_tap(x, y)) {
            net_off();
            go(ST_TITLE, now);
        }
        break;
    case ST_PASS:
        if (now - s_st_t0 > 300) {
            sfx(FX_TAP, 55);
            q_begin(now);
        }
        break;
    case ST_Q:
        input_q(x, y, now);
        break;
    case ST_END:
        input_end(x, y, now);
        break;
    default:
        net_off();
        theme_to(-1);
        go(ST_TITLE, now);
        break;
    }
}
// a slow tap (a press of 350 - 900 ms) on each screen (all of them are buttons) acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    input_ev(ev);
    if (slow) {
        input_ev(&tap);
    }
}
static void update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    s_now = now;
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    const bool client = net_update(now);
    if (!client && s_st == ST_Q) {
        q_update(now);
    }
    net_after(now);
    bg_update(dt);
    const int live = part_update(dt);
    // a screen that waits (the title, the level, the end) and no touch for 20 s: few frames (the battery)
    const bool idle = (s_st == ST_TITLE || s_st == ST_LEVEL || s_st == ST_END) && now - s_touch > 20000 && !live;
#ifdef ESP_PLATFORM
    if (idle) {
        vTaskDelay(pdMS_TO_TICKS(120));
    }
#else
    (void)idle;
#endif
}
static void render(int64_t now)
{
    holes(now);
    bg_draw(s_st != ST_Q);  // (a question: the panels cover the lights; the board draws faster with none)
    switch (s_st) {
    case ST_TITLE:
        render_title(now);
        break;
    case ST_LEVEL:
        render_level(now);
        break;
    case ST_LOBBY:
        render_lobby(now);
        break;
    case ST_PASS:
        render_pass(now);
        break;
    case ST_Q:
        render_q(now);
        break;
    case ST_END:
        render_end(now);
        break;
    case ST_OTHER:
        render_msg("לחבר יש גרסה אחרת של המשחק", "עדכנו את טריוויה בשני הלוחות");
        break;
    default:
        render_msg("אין מספיק זיכרון", "הפעילו את הלוח מחדש ונסו שוב");
        break;
    }
    render_lost(now);
}
// the screen stays on while a question waits for an answer
static bool awake(void)
{
    return s_st == ST_Q || s_st == ST_LOBBY;
}

// the menu icon: a card with a big question mark that tilts, and four answer chips
static void draw_icon(int x, int y, int s)
{
    gfx_fill_rect(x, y, s, s, rgb(46, 26, 96));
    const float t = icon_time();
    for (int j = 0; j < s; j += 2) {
        gfx_fill_rect(x, y + j, s, 2,
                      rgb((uint8_t)(70 - j * 30 / s), (uint8_t)(40 - j * 20 / s), (uint8_t)(140 - j * 50 / s)));
    }
    const int cw = s * 7 / 10, ch = s * 4 / 10, cx = x + (s - cw) / 2, cy = y + s / 10;
    ui_rrect(cx, cy, cw, ch, s / 12, rgb(250, 244, 255));
    const int qx = x + s / 2 + (int)(s / 30.0f * fsin(t * 2.4f));
    gfx_fill_circle(qx, cy + ch / 2, ch / 3, rgb(255, 170, 60));
    gfx_fill_circle(qx, cy + ch / 2, ch / 6, rgb(250, 244, 255));
    gfx_fill_rect(qx - ch / 3, cy + ch / 2, ch / 3, ch / 3, rgb(250, 244, 255));
    gfx_fill_circle(qx, cy + ch - ch / 8, s / 30 + 1, rgb(255, 170, 60));
    static const uint8_t A[4][3] = {{226, 70, 110}, {50, 128, 236}, {232, 160, 36}, {136, 86, 222}};
    const int bw = s * 34 / 100, bh = s * 16 / 100;
    const int hot = (int)(t * 1.5f) % 4;
    for (int i = 0; i < 4; i++) {
        const int bx = x + s / 2 + (i % 2 ? -bw - s / 40 : s / 40), by = y + s * 56 / 100 + (i / 2) * (bh + s / 25);
        const uint8_t *c = A[i];
        ui_rrect(bx, by, bw, bh, bh / 3, i == hot ? rgb(60, 210, 110) : rgb(c[0], c[1], c[2]));
    }
}

const app_t app_trivia = {
        .name = "TRIVIA",
        .landscape = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = draw_icon,
        .awake = awake,
        .round = APP_ROUND_HI,
};
