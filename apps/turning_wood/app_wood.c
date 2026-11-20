// THE TURNING WOOD (GAMES): an adventure of the seasons after "The Lost City". Grandma is gone
// and the year stopped in autumn. The player finds the three Season Stones, sets them in the Great Wheel inside the
// Turning Tree and turns the whole wood to the season that a place needs. 25 painted places (a picture for each season),
// 18 objects, 13 puzzles, a journal of the clues, a map that takes the player to any place that is open, hints.
// The pictures are on the SD card (the data pack, TW_DIR; made by a bake script); the rules are
// tw_game.inc (made by a script from the one source of the design, proved by its solver).
// MANUAL.md "THE TURNING WOOD". THE TOUCH: a tap on a thing looks or acts; a tap on a way out walks; a drag looks round
// the picture; the BAG (top left) holds the objects: tap one to hold it, then tap a thing to use it on that thing.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "miniz.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "tfsfx/tfsfx.h"
#include "tw_sfx.h"

#define TAG    "wood"
#define SW     gfx_w
#define SH     gfx_h
#define TW_KEY "wood_"  // (the manifest: saves=wood_)
#define PW     640      // the size of a place picture
#define PH     240
#if BOARD_UI_TABLET
// (the Tab5: the Tab5 pack has pictures of 640 x 360 = the same scenes with a taller
// view. Its rows T5Y .. T5Y + PH are the normal picture, so the touch spots, the layers and the animation of the tables
// stay correct: s_pic points at the row T5Y of the buffer, and the view on the Tab5 starts at the row -T5Y)
#define T5Y 60
#else
#define T5Y 0
#endif
#define PHB (PH + 2 * T5Y)  // the rows of the picture buffer
#if BOARD_ROUND
// the round panels (tw_round.inc): the view of the scene, then the HUD at the size of the panel
#include "rndview/rndview.h"
static rv_t s_rv;
static bool s_rnd;       // the round frame (gfx_round() at the enter)
static bool s_rv_logic;  // the code of the frame runs now with a point of the frame (not of the panel)
#define RND   s_rnd
#define RND_D rv_panel(&s_rv)  // (the size of the panel, also inside the frame of the scene)
#else
#define RND        0
#define RND_D      SW
#define s_rv_logic 0
#endif

static int s_lang;
static bool s_snd = true, s_music = true;
#define TR(en, he) (s_lang ? (he) : (en))

#include "tw_pack.inc"
#include "tw_logic.inc"
#include "tw_stream.inc"

// ---------------------------------------------------------------- the sound effects (MP3 in tw_sfx.c, libs/tfsfx)
enum {
    FX_STEP,
    FX_STEP2,
    FX_SNOW,
    FX_SNOW2,
    FX_CREAK,
    FX_WINCH,
    FX_LOG,
    FX_SPLASH,
    FX_BOULDER,
    FX_GATE,
    FX_LIGHT,
    FX_BUMP,
    FX_YEAR,
    FX_N
};
static const char *const FXN[FX_N] = {"step",   "step2",   "snow", "snow2", "creak", "winch", "log",
                                      "splash", "boulder", "gate", "light", "bump",  "year"};
static void fx(int id, int vol)
{
    if (s_snd) {
        tfsfx_play_var(id, vol, 0.06f);
    }
}
static void tone(float f0, float f1, int ms, int vol, int wave)
{
    if (!s_snd) {
        return;
    }
    audio_voice_t v = {0};
    v.dur = (uint32_t)(ms * AUDIO_SAMPLE_RATE / 1000);
    v.f0 = f0;
    v.f1 = f1;
    v.wave = (uint8_t)wave;
    v.vol = (uint8_t)vol;
    v.shape = AUDIO_PERC;
    audio_play(&v);
}
static void chime(void)
{
    tone(880, 880, 260, 18, W_SINE);
    tone(1320, 1320, 400, 12, W_SINE);
}

// ---------------------------------------------------------------- the screen and the view
enum {
    M_TITLE,
    M_PLACE,
    M_CU,
    M_JOURNAL,
    M_MAP,
    M_END,
    M_NODATA
};
enum {
    CU_CHEST,
    CU_MOONDOOR,
    CU_STARBOX,
    CU_GEARS,
    CU_RINGS
};
static int s_mode;
static bool s_wide;       // the 3.49 strip (640 x 172): the view pans up and down; else left and right
static int s_vx, s_vy;    // the view in the picture
static uint16_t *s_pic;   // the picture now (640 x 240, PSRAM): a place with its layers, or a close-up
static uint8_t *s_idx;    // a scratch for the indices of a picture
static uint16_t *s_pbuf;  // the buffer of the picture (PHB rows); s_pic = its row T5Y
static float s_t;         // the time (s)
static int64_t s_last;
// the walk: a fade to dark, the new place, a fade in
static int s_to = -1;
static float s_fade;
static int s_fade_dir;
static float s_shake;
// a line of text at the bottom (what the player sees or does)
static char s_msg[240];
static float s_msg_t;
// the bag
static bool s_bag;
static int s_sel = -1;  // the object in the hand (-1: none)
static int s_bag_scroll;
// the drag of the view
static bool s_drag;
static int s_dx0, s_dy0, s_vx0, s_vy0;
// the sprites: the icons of the objects and of the buttons; the frames of the mill wheel; the close-up parts
static tw_spr_t s_icon[TW_NITEM], s_btn[3];
static tw_spr_t s_anim[3];
static int s_nanim;
static tw_spr_t s_cs[32];
static int s_ncs;
// the puzzles in a place: the ice (the row reached), the crystals and the statues (the step of the order)
static int s_ice = -1, s_cry, s_sta;
static uint8_t s_cry_lit, s_sta_lit;
// the Great Wheel: the angle of the ring (deg), the turn in progress
static float s_wang, s_wgoal;
static int s_wturn;  // 0: still; -1 / 1: a turn to a season; 2: the year turns (two full turns)
static int s_wtarget;
// a close-up: the parts
static int s_cu;
static uint8_t s_part[9];
static float s_solved_t;
// the journal and the map
static int s_jpage, s_hint_lvl;
// BEGINNER mode (the settings panel; OFF by default): only then the journal (the clues, the hints) and the map
static bool s_beg;
// the feedback with no text: a ring where the finger was, a sparkle where an action acted, an object that flies into the bag
static float s_ring_t, s_spark_t, s_fly_t;
static int s_tap_x, s_tap_y, s_spark_x, s_spark_y, s_fly_item = -1, s_fly_x0, s_fly_y0;
// the swipe: the time when the finger came down
static int64_t s_down_ms;

// a label at the foot of the screen (the name of the object in the hand)
static void label(const char *text)
{
    if (!text || !text[0]) {
        return;
    }
    snprintf(s_msg, sizeof s_msg, "%s", text);
    s_msg_t = 2.5f;
}
// (no text in the play, so that it does not feel like a quest. The picture and the
// sound give the feedback; the texts stay in the design for the hints of the journal.)
static void say(const char *text)
{
    (void)text;
}

#include "tw_anim.inc"

#if BOARD_UI_TABLET
// (the Tab5 backlog item 12) the Tab5 frame (640 x 360) shows the whole picture of the Tab5 pack: the view starts at the
// row -T5Y of the old picture (the taps and the layers keep their places)
static void tab5_view(void)
{
    s_vy = -T5Y;
    if (s_vx > PW - SW) {
        s_vx = PW - SW;
    }
    if (s_vx < 0) {
        s_vx = 0;
    }
}
#endif
// a picture of the pack into the buffer of the picture (the Tab5: its taller picture)
static bool pic_load(const char *name)
{
    return pk_picture(name, s_pbuf, PW, PHB, s_idx);
}
// the view: the default for the place (the 3.49: rows 40.. of the picture; the 320 frame: round its focus)
static void view_default(void)
{
    if (s_wide) {
        s_vx = 0;
        s_vy = TW_PLACES[T.pos].vy349;
    } else {
        s_vy = 0;
        s_vx = TW_PLACES[T.pos].focus - SW / 2;
    }
    if (s_vx < 0) {
        s_vx = 0;
    }
    if (s_vx > PW - SW) {
        s_vx = PW - SW;
    }
    if (s_vy < 0) {
        s_vy = 0;
    }
    if (s_vy > PH - SH) {
        s_vy = PH - SH;
    }
#if BOARD_UI_TABLET
    tab5_view();
#endif
}
static void view_fixed(void)  // (a close-up: the middle of the picture)
{
    s_vx = (PW - SW) / 2;
    s_vy = s_wide ? 34 : (PH - SH) / 2;
#if BOARD_UI_TABLET
    tab5_view();
#endif
}

// ---------------------------------------------------------------- a place: its picture and its layers
static void anim_free(void)
{
    for (int i = 0; i < s_nanim; i++) {
        spr_free(&s_anim[i]);
    }
    s_nanim = 0;
}
static void cs_free(void)
{
    for (int i = 0; i < s_ncs; i++) {
        spr_free(&s_cs[i]);
    }
    s_ncs = 0;
}
// the Great Wheel: 24 pictures of the ring (one each 15 degrees) and the 4 stones. Only the stones and the frame of
// the angle now load at the entry (a load of all 24 took 0.9 s on the board); the others load one in each frame after
// it, or at once when the ring needs one.
static uint32_t s_wring_tried;
static void wheel_frame(int f)
{
    if (s_wring_tried >> f & 1) {
        return;
    }
    s_wring_tried |= 1u << f;
    char n[32];
    snprintf(n, sizeof n, "s_wring_%02d.twl", f);
    pk_sprite(n, &s_cs[f]);
}
static void load_wheel_parts(void)
{
    cs_free();
    s_wring_tried = 0;
    for (int i = 0; i < 24; i++) {
        memset(&s_cs[i], 0, sizeof s_cs[i]);
    }
    s_ncs = 24;
    char n[32];
    static const char *const ST[4] = {"spring", "summer", "autumn", "winter"};
    for (int i = 0; i < 4; i++) {
        snprintf(n, sizeof n, "s_wstone_%s.twl", ST[i]);
        pk_sprite(n, &s_cs[s_ncs++]);
    }
}
static void wheel_more(void)
{
    for (int f = 0; f < 24; f++) {
        if (!(s_wring_tried >> f & 1)) {
            wheel_frame(f);
            return;
        }
    }
}
static float wheel_angle_of(int season)  // the ring angle (deg) that brings the sign of the season under the owl
{
    static const float MID[4] = {90, 180, 270, 0};  // (the middle of each zone on the ring: SP, SU, AU, WI)
    return fmodf(360 - MID[season], 360);
}
static void compose(void)
{
    const int64_t t0 = esp_timer_get_time();
    anim_free();
    const tw_place_t *P = &TW_PLACES[T.pos];
    pic_load(P->pic[T.season]);
    for (int i = P->lay0; i < P->lay0 + P->nlay; i++) {
        const tw_layer_t *Lr = &TW_LAYERS[i];
        if (!is_done(Lr->act) || (Lr->season != 255 && Lr->season != T.season)) {
            continue;
        }
        tw_spr_t s;
        if (Lr->spring_frame) {
            if (s_nanim < 3 && pk_sprite(Lr->file, &s_anim[s_nanim])) {
                s_nanim++;
            }
            continue;
        }
        if (pk_sprite(Lr->file, &s)) {
            spr_to_buf(&s, s_pbuf, PW, PHB, 0, T5Y);
            spr_free(&s);
        }
    }
    if (T.pos == P_WHEEL) {
        if (!s_ncs) {
            load_wheel_parts();
        }
        if (!s_wturn) {
            s_wang = wheel_angle_of(T.season);
        }
    } else {
        cs_free();
    }
    ESP_LOGI(TAG, "place %d season %d: paint %d ms", T.pos, T.season, (int)((esp_timer_get_time() - t0) / 1000));
    (void)t0;
}
static void enter_place(void)
{
    T.seen |= 1u << T.pos;
    s_ice = -1;
    s_cry = 0;
    s_sta = 0;
    s_cry_lit = 0;
    s_sta_lit = 0;
    compose();
    view_default();
    anim_enter();
    if (T.pos == P_WHEEL && act_ready(A_SEEWHEEL)) {
        act_apply(A_SEEWHEEL);
        say(TW_ACTS[A_SEEWHEEL].text[s_lang]);
    }
    tw_save();
}
static void walk_to(int to)
{
    if (s_to >= 0) {
        return;
    }
    s_to = to;
    s_fade_dir = 1;
    fx(T.season == 3 ? FX_SNOW : FX_STEP, 50);
}
static void walk_done(void)
{
    T.pos = (uint8_t)s_to;
    s_to = -1;
    enter_place();
    fx(T.season == 3 ? FX_SNOW2 : FX_STEP2, 45);
    s_fade_dir = -1;
}

// ---------------------------------------------------------------- the actions
static void open_cu(int cu);
static void year_turns(void);
static void do_act(int a)
{
    const tw_act_t *A = &TW_ACTS[a];
    if (A->takes && s_sel >= 0 && bit64(A->takes, s_sel)) {
        s_sel = -1;
    }
    act_apply(a);
    s_spark_t = 0.9f;
    s_spark_x = s_tap_x;
    s_spark_y = s_tap_y;
    for (int i = 0; i < TW_NITEM; i++) {  // (a new object flies into the bag)
        if (bit64(A->gives, i)) {
            s_fly_item = i;
            s_fly_t = 0.75f;
            s_fly_x0 = s_tap_x;
            s_fly_y0 = s_tap_y;
            break;
        }
    }
    switch (A->kind) {
    case K_TAKE:
        fx(FX_LIGHT, 40);
        tone(660, 990, 120, 14, W_TRI);
        break;
    case K_SEE:
        tone(520, 780, 200, 12, W_SINE);
        break;
    case K_PUZZLE:
        chime();
        break;
    default:
        fx(FX_GATE, 40);
        break;
    }
    if (a == A_GATE || a == A_TOWER) {
        fx(FX_GATE, 60);
    }
    if (a == A_LIGHT) {
        fx(FX_LIGHT, 60);
    }
    if (a == A_GEARS) {
        fx(FX_WINCH, 60);
    }
    compose();
    tw_save();
}
// can the object in the hand act on action a (an action that uses objects)?
static bool sel_fits(int a)
{
    const tw_act_t *A = &TW_ACTS[a];
    return s_sel >= 0 && bit64(A->need, s_sel);
}
// a tap on a thing: the actions of the thing that can act now, else its look
static void tap_thing(const tw_thing_t *th)
{
    for (int k = 0; k < th->nact; k++) {
        const int a = th->act[k];
        const tw_act_t *A = &TW_ACTS[a];
        if (is_done(a)) {
            continue;
        }
        if (A->kind == K_USE) {
            if (!sel_fits(a)) {
                continue;
            }
            if (act_ready(a)) {
                do_act(a);
                return;
            }
            say(TR("Not now. Something else is needed, or another season.", "לא עכשיו. צריך עוד משהו, או עונה אחרת."));
            return;
        }
        if (!act_ready(a)) {
            continue;
        }
        if (A->kind == K_TAKE || A->kind == K_SEE) {
            do_act(a);
            return;
        }
        if (A->kind == K_PUZZLE) {
            switch (a) {
            case A_CHEST:
                open_cu(CU_CHEST);
                return;
            case A_MOONDOOR:
                open_cu(CU_MOONDOOR);
                return;
            case A_STARBOX:
                open_cu(CU_STARBOX);
                return;
            case A_GEARS:
                open_cu(CU_GEARS);
                return;
            case A_RINGS:
                open_cu(CU_RINGS);
                return;
            default:
                break;  // (the ice, the crystals, the statues: in the place)
            }
        }
    }
    fx(FX_BUMP, s_sel >= 0 ? 40 : 18);  // (nothing happens: a soft knock)
}

// ---------------------------------------------------------------- the puzzles in a place
// the ice: 7 rows of 3 tiles (the same places as the picture: paint/places_inner.py), the thick tile of each row
static const int8_t ICE_SAFE[7] = {1, 0, 1, 2, 1, 1, 0};
static void ice_tile(int row, int col, float *x, float *y, float *rx)
{
    const float q = row / 6.0f, sc = 1 - q * 0.6f;
    *x = 220 + col * 100 * sc + q * 130;
    *y = 232 - q * 76;
    *rx = 34 * sc;
}
static bool ice_tap(int px, int py)
{
    if (is_done(A_CROSS)) {
        return false;
    }
    const int row = s_ice + 1;
    int br = -1, bc = -1;
    float bd = 1.3f;
    for (int r = 0; r < 7; r++) {  // (the nearest tile for its shape: 34 x 9, smaller far)
        for (int c = 0; c < 3; c++) {
            float x, y, rx;
            ice_tile(r, c, &x, &y, &rx);
            const float dx = (px - x) / rx, dy = (py - y) / (rx * 0.27f + 3);
            const float d = dx * dx + dy * dy;
            if (d < bd) {
                bd = d;
                br = r;
                bc = c;
            }
        }
    }
    for (int r = br; r >= 0 && r == br; r = -1) {
        for (int c = bc; c == bc; c = -1) {
            if (!has_fact(F_CLUE_ICE)) {
                say(TR("Which ice is thick? I need a sign.", "איזה קרח עבה? אני צריך סימן."));
                return true;
            }
            if (r != row) {
                say(TR("One step at a time, from the shore.", "צעד אחד בכל פעם, מהחוף."));
                return true;
            }
            if (c == ICE_SAFE[r]) {
                s_ice = r;
                fx(FX_SNOW, 45);
                if (r == 6) {
                    do_act(A_CROSS);
                }
            } else {
                fx(FX_SPLASH, 50);
                fx(FX_BUMP, 60);
                s_shake = 0.4f;
                s_ice = -1;
                say(TR("The ice cracks! Back to the shore.", "הקרח נסדק! חזרה לחוף."));
            }
            return true;
        }
    }
    return false;
}
// the crystals: tap the clusters in the order of the colours of the flowers
static const int16_t CRY_X[4] = {120, 210, 430, 520};
static bool cry_tap(int px, int py)
{
    if (is_done(A_CRYSTALS)) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (abs(px - CRY_X[i]) > 40 || py < 140 || py > 236) {
            continue;
        }
        if (!act_ready(A_CRYSTALS)) {
            say(TR("The crystals are dark. A light and an order are needed.", "הבדולחים חשוכים. צריך אור וסדר."));
            return true;
        }
        if (i == s_cry) {
            s_cry_lit |= 1 << i;
            s_cry++;
            tone(660 + i * 220, 660 + i * 220, 400, 14, W_SINE);
            if (s_cry == 4) {
                do_act(A_CRYSTALS);
            }
        } else {
            s_cry = 0;
            s_cry_lit = 0;
            fx(FX_BUMP, 40);
            say(TR("The light goes out. Another order.", "האור כבה. סדר אחר."));
        }
        return true;
    }
    return false;
}
// the statues: in the order of the light spots of the sundial (summer, winter, autumn, spring)
static const int16_t STA_X[4] = {40, 600, 470, 170}, STA_Y[4] = {180, 180, 165, 165};  // SP, SU, AU, WI
static const int8_t STA_ORDER[4] = {1, 3, 2, 0};
static bool sta_tap(int px, int py)
{
    if (is_done(A_STATUES) || !is_done(A_MOONDOOR)) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        if (abs(px - STA_X[i]) > 30 || abs(py - STA_Y[i]) > 48) {
            continue;
        }
        if (i == 3 && !is_done(A_WSTONE)) {
            return false;  // (the warden of winter: its stone first)
        }
        if (!act_ready(A_STATUES)) {
            say(TR("The statues turn, then turn back. In which order?", "הפסלים מסתובבים וחוזרים. באיזה סדר?"));
            fx(FX_CREAK, 40);
            return true;
        }
        if (i == STA_ORDER[s_sta]) {
            s_sta_lit |= 1 << i;
            s_sta++;
            fx(FX_CREAK, 55);
            if (s_sta == 4) {
                do_act(A_STATUES);
            }
        } else {
            s_sta = 0;
            s_sta_lit = 0;
            fx(FX_BUMP, 50);
            say(TR("The wardens turn back.", "השומרים חוזרים למקומם."));
        }
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- the Great Wheel
static const int16_t SOCK_X[4] = {358, 320, 282, 320}, SOCK_Y[4] = {120, 158, 120, 82};  // SP, SU, AU, WI
static const uint8_t SOCK_ACT[4] = {A_SETSPRING, A_SETSUMMER, 255, A_SETWINTER};
static const uint8_t SOCK_ITEM[4] = {I_SPSTONE, I_SUSTONE, 255, I_WSTONE};
static void wheel_turn(int dir)
{
    // the year forward (dir 1): spring -> summer -> autumn -> winter; only the seasons whose stone is set
    int s = T.season;
    for (int k = 0; k < 4; k++) {
        s = (s + dir + 4) % 4;
        if (season_open(s)) {
            break;
        }
    }
    if (s == T.season) {
        say(TR("The ring does not move: only the stone of autumn is set.", "הטבעת לא זזה: רק אבן הסתיו במקומה."));
        fx(FX_BUMP, 40);
        return;
    }
    s_wtarget = s;
    s_wgoal = wheel_angle_of(s);
    s_wturn = dir > 0 ? -1 : 1;  // (the year forward: the ring turns back)
    fx(FX_CREAK, 60);
}
static bool wheel_tap(int px, int py)
{
    if (s_wturn) {
        return true;
    }
    for (int i = 0; i < 4; i++) {
        if (SOCK_ACT[i] == 255 || (px - SOCK_X[i]) * (px - SOCK_X[i]) + (py - SOCK_Y[i]) * (py - SOCK_Y[i]) > 16 * 16) {
            continue;
        }
        const int a = SOCK_ACT[i];
        if (is_done(a)) {
            say(TR("The stone is in its socket.", "האבן בשקע שלה."));
            return true;
        }
        if (has_item(SOCK_ITEM[i]) && act_ready(a)) {
            do_act(a);
            fx(FX_LOG, 50);
            return true;
        }
        say(TR("An empty socket. Its stone is somewhere in the wood.", "שקע ריק. האבן שלו נמצאת אי שם ביער."));
        return true;
    }
    const float d = sqrtf((float)(px - 320) * (px - 320) + (float)(py - 120) * (py - 120));
    if (d < 22) {
        if (act_ready(A_YEAR)) {
            year_turns();
            return true;
        }
        say(TR("The hub of the Wheel. It does not turn the whole year yet.",
               "ציר הגלגל. הוא עוד לא מסובב את כל השנה."));
        return true;
    }
    if (d > 58 && d < 88) {
        wheel_turn(px >= 320 ? 1 : -1);
        return true;
    }
    return false;
}
static void year_turns(void)
{
    s_wturn = 2;
    s_wgoal = s_wang - 720;
    fx(FX_YEAR, 70);
}
static void wheel_step(float dt)
{
    if (!s_wturn) {
        return;
    }
    const float sp = (s_wturn == 2 ? 300 : 200) * dt;
    if (s_wturn == 2) {
        s_wang -= sp;
        if (s_wang <= s_wgoal) {
            s_wturn = 0;
            s_wang = fmodf(s_wgoal + 1440, 360);
            do_act(A_YEAR);
            say(TR("The Wheel turns by itself. The year turns again. Far away, a door opens.",
                   "הגלגל מסתובב לבד. השנה מסתובבת שוב. רחוק משם, נפתחת דלת."));
        }
        return;
    }
    float d = fmodf(s_wgoal - s_wang + 720, 360);  // (the way left in the direction of the turn)
    if (s_wturn < 0) {
        d = 360 - d;
    }
    if (d < 0.5f || d > 359.5f || d <= sp) {
        s_wang = s_wgoal;
        s_wturn = 0;
        T.season = (uint8_t)s_wtarget;
        static const char *const SN[4][2] = {
                {"SPRING", "אביב"}, {"SUMMER", "קיץ"}, {"AUTUMN", "סתיו"}, {"WINTER", "חורף"}};
        char b[96];
        snprintf(b, sizeof b, TR("The wood turns to %s.", "היער עובר ל%s."), SN[T.season][s_lang]);
        say(b);
        chime();
        tw_save();
        return;
    }
    s_wang += s_wturn * sp;
    s_wang = fmodf(s_wang + 360, 360);
}

// ---------------------------------------------------------------- the close-ups
static const char *const CU_BG[5] = {"c_chest.twp", "c_moondoor.twp", "c_starbox.twp", "c_gears.twp", "c_rings.twp"};
static void open_cu(int cu)
{
    s_cu = cu;
    memset(s_part, 0, sizeof s_part);
    s_solved_t = 0;
    cs_free();
    pic_load(CU_BG[cu]);
    char n[32];
    switch (cu) {
    case CU_CHEST: {
        static const char *const S[4] = {"spring", "summer", "autumn", "winter"};
        for (int i = 0; i < 4; i++) {
            snprintf(n, sizeof n, "s_dial_%s.twl", S[i]);
            pk_sprite(n, &s_cs[s_ncs++]);
        }
        break;
    }
    case CU_MOONDOOR: {
        static const char *const M[4] = {"new", "crescent", "half", "full"};
        for (int i = 0; i < 4; i++) {
            snprintf(n, sizeof n, "s_moon_%s.twl", M[i]);
            pk_sprite(n, &s_cs[s_ncs++]);
        }
        break;
    }
    case CU_STARBOX:
        pk_sprite("s_star.twl", &s_cs[s_ncs++]);
        break;
    case CU_GEARS: {
        static const char *const G3[3] = {"small", "middle", "big"};
        for (int i = 0; i < 3; i++) {
            snprintf(n, sizeof n, "s_gear_%s.twl", G3[i]);
            pk_sprite(n, &s_cs[s_ncs++]);
        }
        break;
    }
    case CU_RINGS:
        for (int i = 0; i < 3; i++) {
            for (int k = 0; k < 8; k++) {
                snprintf(n, sizeof n, "s_ring%d_%d.twl", i, k);
                pk_sprite(n, &s_cs[s_ncs++]);
            }
        }
        break;
    }
    s_mode = M_CU;
    view_fixed();
    fx(FX_CREAK, 40);
}
static void close_cu(void)
{
    s_mode = M_PLACE;
    cs_free();
    compose();
    view_default();
}
// the codes (the clues): the chest = the seasons of the things on the pegs (the rake: autumn, the sled: winter, the seed
// basket: spring, the straw hat: summer); the moon door = the carving (full,
// half, crescent, new); the star box = the lit stones (1, 3, 4, 7); the gears = big, small, middle; the rings = the
// rubbing (2, 5, 7). The values of s_part: the index of the sprite (seasons SP SU AU WI; moons new, crescent, half, full)
static bool cu_solved(void)
{
    switch (s_cu) {
    case CU_CHEST:
        return s_part[0] == 2 && s_part[1] == 3 && s_part[2] == 0 && s_part[3] == 1;
    case CU_MOONDOOR:
        return s_part[0] == 3 && s_part[1] == 2 && s_part[2] == 1 && s_part[3] == 0;
    case CU_STARBOX: {
        int m = 0;
        for (int i = 0; i < 9; i++) {
            m |= s_part[i] << i;
        }
        return m == ((1 << 1) | (1 << 3) | (1 << 4) | (1 << 7));
    }
    case CU_GEARS:
        return s_part[0] == 3 && s_part[1] == 1 && s_part[2] == 2;
    case CU_RINGS:
        return s_part[0] == 2 && s_part[1] == 5 && s_part[2] == 7;
    }
    return false;
}
static void star_hole(int k, float *x, float *y)
{
    const float a = k / 9.0f * 6.2832f + 0.3f;
    *x = 320 + fcos(a) * 112;
    *y = 134 + fsin(a) * 44;
}
static void cu_tap(int px, int py)
{
    if (s_solved_t > 0) {
        return;
    }
    bool hit = false;
    switch (s_cu) {
    case CU_CHEST:
        for (int i = 0; i < 4; i++) {
            if (abs(px - (230 + i * 60)) < 28 && abs(py - 125) < 30) {
                s_part[i] = (s_part[i] + 1) % 4;
                hit = true;
            }
        }
        break;
    case CU_MOONDOOR:
        for (int i = 0; i < 4; i++) {
            if (abs(px - (245 + i * 50)) < 24 && abs(py - 120) < 26) {
                s_part[i] = (s_part[i] + 1) % 4;
                hit = true;
            }
        }
        break;
    case CU_STARBOX:
        for (int k = 0; k < 9; k++) {
            float x, y;
            star_hole(k, &x, &y);
            if ((px - x) * (px - x) + (py - y) * (py - y) < 18 * 18) {
                s_part[k] ^= 1;
                hit = true;
            }
        }
        break;
    case CU_GEARS:
        for (int i = 0; i < 3; i++) {
            if (abs(px - (240 + i * 80)) < 38 && abs(py - 120) < 40) {
                s_part[i] = (s_part[i] + 1) % 4;
                hit = true;
            }
        }
        break;
    case CU_RINGS: {
        const float d = sqrtf((float)(px - 320) * (px - 320) + (float)(py - 120) * (py - 120));
        const int r = d > 68 && d < 98 ? 0 : d > 42 && d < 68 ? 1 : d > 18 && d < 42 ? 2 : -1;
        if (r >= 0) {
            s_part[r] = (s_part[r] + 1) % 8;
            hit = true;
        }
        break;
    }
    }
    if (hit) {
        tone(1200, 900, 40, 12, W_SQUARE);
        if (cu_solved()) {
            s_solved_t = 1.2f;
        }
    }
}
static void cu_draw(void)
{
    switch (s_cu) {
    case CU_CHEST:
        for (int i = 0; i < 4; i++) {
            spr_to_fb(&s_cs[s_part[i]], 230 + i * 60 - 320 - s_vx, 125 - 120 - s_vy);
        }
        break;
    case CU_MOONDOOR:
        for (int i = 0; i < 4; i++) {
            spr_to_fb(&s_cs[s_part[i]], 245 + i * 50 - 320 - s_vx, 120 - 120 - s_vy);
        }
        break;
    case CU_STARBOX:
        for (int k = 0; k < 9; k++) {
            if (s_part[k]) {
                float x, y;
                star_hole(k, &x, &y);
                spr_to_fb(&s_cs[0], (int)x - 320 - s_vx, (int)y - 120 - s_vy);
            }
        }
        break;
    case CU_GEARS:
        for (int i = 0; i < 3; i++) {
            if (s_part[i]) {
                spr_to_fb(&s_cs[s_part[i] - 1], 240 + i * 80 - 320 - s_vx, 120 - 120 - s_vy);
            }
        }
        break;
    case CU_RINGS:
        for (int i = 0; i < 3; i++) {
            spr_to_fb(&s_cs[i * 8 + s_part[i]], -s_vx, -s_vy);
        }
        break;
    }
}

// ---------------------------------------------------------------- drawing helpers
static void fb_dark(int x, int y, int w, int h, int k)  // darken a box of the frame (k/8 of the light stays)
{
    for (int yy = y < 0 ? 0 : y; yy < y + h && yy < SH; yy++) {
        for (int xx = x < 0 ? 0 : x; xx < x + w && xx < SW; xx++) {
            const uint16_t v = (uint16_t)(g_fb[yy * SW + xx] >> 8 | g_fb[yy * SW + xx] << 8);
            const int r = (v >> 11) * k / 8, g = ((v >> 5) & 63) * k / 8, b = (v & 31) * k / 8;
            const uint16_t o = (uint16_t)(r << 11 | g << 5 | b);
            g_fb[yy * SW + xx] = (uint16_t)(o >> 8 | o << 8);
        }
    }
}
static void fade_frame(float a)
{
    if (a <= 0) {
        return;
    }
    fb_dark(0, 0, SW, SH, (int)((1 - a) * 8 + 0.5f));
}
// a text in lines that fit the width w (split at the spaces); base = the first baseline; returns the lines
static bool s_wrap_dry;  // (text_wrap counts the lines only: the hint fit)
static int text_wrap(const kfont_t *f, int cx, int base, int w, const char *text, uint16_t col, int line_h)
{
    char line[128] = "", word[64];
    int n = 0;
    const char *p = text;
    while (*p) {
        int k = 0;
        while (*p == ' ') {
            p++;
        }
        while (*p && *p != ' ' && k < 63) {
            word[k++] = *p++;
        }
        word[k] = 0;
        if (!k) {
            break;
        }
        char test[192];
        snprintf(test, sizeof test, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && ui_text_w(f, test) > w) {
            if (!s_wrap_dry) {
                ui_text_c(f, cx, base + n * line_h, line, col);
            }
            n++;
            snprintf(line, sizeof line, "%s", word);
        } else {
            size_t len = strlen(test);
            if (len >= sizeof line) {
                len = sizeof line - 1;
            }
            memcpy(line, test, len);
            line[len] = 0;
        }
    }
    if (line[0]) {
        if (!s_wrap_dry) {
            ui_text_c(f, cx, base + n * line_h, line, col);
        }
        n++;
    }
    return n;
}
static void draw_msg(void)
{
    if (s_msg_t <= 0 || !s_msg[0]) {
        return;
    }
#if BOARD_ROUND
    if (s_rnd) {  // (the round panels: a band in the bottom cap, the text in one or two lines inside the circle)
        const int d = rv_panel(&s_rv), y = d * 77 / 100, hw = rv_half(d, y + 50, 14);
        fb_dark(d / 2 - hw, y, 2 * hw, 50, 2);
        s_wrap_dry = true;
        const int n = text_wrap(&KF_TEXT, d / 2, 0, 2 * hw - 24, s_msg, 0, 21);
        s_wrap_dry = false;
        text_wrap(&KF_TEXT, d / 2, y + (n > 1 ? 20 : 31), 2 * hw - 24, s_msg, rgb(250, 240, 216), 21);
        return;
    }
#endif
    const int h = s_wide ? 40 : 52, y = SH - h;
    fb_dark(0, y, SW, h, 2);
    text_wrap(&KF_TEXT, SW / 2, y + (s_wide ? 16 : 20), SW - 40, s_msg, rgb(250, 240, 216), s_wide ? 17 : 19);
}
// the buttons: the bag (top left), the journal and the map (top right)
static void btn_xy(int i, int *x, int *y)
{
#if BOARD_ROUND
    if (s_rnd) {  // (the round panels: the top cap; the points of the panel)
        const int d = rv_panel(&s_rv);
        *y = i == 0 ? d * 11 / 100 : d * 14 / 100;
        *x = d / 2 + (i == 0 ? 0 : i == 1 ? d * 15 / 100 : -d * 15 / 100);
        return;
    }
#endif
    *y = 26;
    *x = i == 0 ? 28 : i == 1 ? SW - 28 : SW - 80;
}
static void draw_buttons(void)
{
    for (int i = 0; i < 3; i++) {
        if (i && !s_beg) {
            continue;
        }
        int x, y;
        btn_xy(i, &x, &y);
        const int br = RND ? 28 : 23;
        ui_circle(x, y, br, rgb(20, 16, 12), 170);
        ui_ring(x, y, br, 2, rgb(201, 162, 79), 220);
        if (s_btn[i].ix) {
            spr_to_fb(&s_btn[i], x - 24, y - 24);
        }
        if (i == 0 && s_sel >= 0 && s_icon[s_sel].ix) {  // (the object in the hand, small, on the bag)
            ui_circle(x + 16, y + 16, 13, rgb(201, 162, 79), 255);
            const tw_spr_t *ic = &s_icon[s_sel];
            for (int yy = 0; yy < 48; yy += 2) {
                for (int xx = 0; xx < 48; xx += 2) {
                    const int sx = xx - ic->x, sy = yy - ic->y;
                    if (sx < 0 || sy < 0 || sx >= ic->w || sy >= ic->h || !ic->ix[sy * ic->w + sx]) {
                        continue;
                    }
                    const int fx_ = x + 4 + xx / 2, fy_ = y + 4 + yy / 2;
                    if (fx_ >= 0 && fy_ >= 0 && fx_ < SW && fy_ < SH) {
                        g_fb[fy_ * SW + fx_] = ic->lut[ic->ix[sy * ic->w + sx]];
                    }
                }
            }
        }
    }
}
static int btn_hit(int x, int y)
{
    if (s_rv_logic) {
        return -1;
    }
    for (int i = 0; i < 3; i++) {
        if (i && !s_beg) {
            continue;
        }
        int bx, by;
        btn_xy(i, &bx, &by);
        if ((x - bx) * (x - bx) + (y - by) * (y - by) < 30 * 30) {
            return i;
        }
    }
    return -1;
}
// the bag: a strip at the bottom with the objects
static int bag_items(int *out)
{
    int n = 0;
    for (int i = 0; i < TW_NITEM; i++) {
        if (has_item(i) && i != I_JOURNAL) {
            out[n++] = i;
        }
    }
    return n;
}
// the top row of the bag strip, its first cell and the largest scroll
static int bag_top(void)
{
#if BOARD_ROUND
    if (s_rnd) {  // (the round panels: the bottom cap)
        return rv_panel(&s_rv) * 74 / 100;
    }
#endif
    return SH - 62;
}
static int bag_x0(void)
{
#if BOARD_ROUND
    if (s_rnd) {
        const int d = rv_panel(&s_rv);
        return d / 2 - rv_half(d, d * 84 / 100 + 26, 4) + 28;
    }
#endif
    return 30;
}
static int bag_scroll_max(int n)
{
    return n * 56 + 2 * bag_x0() - 56 - (RND ? RND_D : SW);
}
static void bag_cell(int k, int *x, int *y)
{
    *x = bag_x0() + k * 56 - s_bag_scroll;
    *y = RND ? RND_D * 84 / 100 : SH - 32;
}
static void draw_bag(void)
{
    int it[TW_NITEM];
    const int n = bag_items(it);
    fb_dark(0, bag_top(), SW, SH - bag_top(), 2);
    for (int k = 0; k < n; k++) {
        int x, y;
        bag_cell(k, &x, &y);
        if (x < -30 || x > SW + 30 || (RND && !gfx_in_circle(x, y, 20))) {
            continue;
        }
        if (it[k] == s_sel) {
            ui_circle(x, y, 26, rgb(201, 162, 79), 200);
        }
        ui_circle(x, y, 24, rgb(40, 32, 24), 230);
        if (s_icon[it[k]].ix) {
            spr_to_fb(&s_icon[it[k]], x - 24, y - 24);
        }
    }
    if (s_sel >= 0) {
        ui_text_c(RND ? &KF_NAME : &KF_LABEL, SW / 2, bag_top() - (RND ? 8 : 4), TW_ITEM_NAME[s_sel][s_lang],
                  rgb(250, 230, 170));
    }
    if (!n) {
        ui_text_c(&KF_TEXT, SW / 2, RND ? RND_D * 86 / 100 : SH - 26, TR("The bag is empty.", "התיק ריק."),
                  rgb(230, 220, 200));
    }
}
static bool bag_tap(int px, int py)
{
    if (py < bag_top()) {
        s_bag = false;
        return false;
    }
    int it[TW_NITEM];
    const int n = bag_items(it);
    for (int k = 0; k < n; k++) {
        int x, y;
        bag_cell(k, &x, &y);
        if (abs(px - x) > 26) {
            continue;
        }
        const int i = it[k];
        if (s_sel >= 0 && s_sel != i) {  // (two objects: put them together?)
            for (int a = 0; a < TW_NACT; a++) {
                if (TW_ACTS[a].kind == K_COMBINE && bit64(TW_ACTS[a].need, s_sel) && bit64(TW_ACTS[a].need, i) &&
                    act_ready(a)) {
                    s_sel = -1;
                    do_act(a);
                    return true;
                }
            }
        }
        s_sel = s_sel == i ? -1 : i;
        if (s_sel >= 0) {  // (the object is in the hand: the bag closes)
            label(TW_ITEM_NAME[i][s_lang]);
            s_bag = false;
        }
        tone(700, 900, 40, 10, W_TRI);
        return true;
    }
    return true;
}

// ---------------------------------------------------------------- the place: the frame and the taps
static void draw_place(void)
{
    for (int y = 0; y < SH; y++) {
        memcpy(g_fb + y * SW, s_pic + (y + s_vy) * PW + s_vx, (size_t)SW * 2);
    }
    if (s_nanim && T.season == 0) {  // (the mill wheel turns in spring)
        const int f = (int)(s_t * 8) % (s_nanim + 1);
        if (f) {
            spr_to_fb(&s_anim[f - 1], -s_vx, -s_vy);
        }
    }
    anim_draw();
    if (T.pos == P_ICE && s_ice >= 0) {
        for (int r = 0; r <= s_ice; r++) {
            float x, y, rx;
            ice_tile(r, ICE_SAFE[r], &x, &y, &rx);
            ui_circle((int)x - s_vx, (int)y - s_vy, (int)(rx * 0.25f) + 2, rgb(255, 210, 120), 200);
        }
    }
    if (T.pos == P_GROTTO) {
        for (int i = 0; i < 4; i++) {
            if (s_cry_lit >> i & 1) {
                ui_circle(CRY_X[i] - s_vx, 180 - s_vy, 30, rgb(255, 255, 255), 90 + (int)(fsin(s_t * 6) * 40));
            }
        }
    }
    if (T.pos == P_HALL) {
        for (int i = 0; i < 4; i++) {
            if (s_sta_lit >> i & 1) {
                ui_circle(STA_X[i] - s_vx, STA_Y[i] - s_vy - 20, 14, rgb(255, 220, 140), 120);
            }
        }
    }
    if (T.pos == P_WHEEL && s_ncs >= 28) {
        float an = fmodf(s_wang, 360);
        if (an < 0) {
            an += 360;
        }
        const int f = ((int)(an / 15.0f + 0.5f)) % 24;
        wheel_frame(f);
        spr_to_fb(&s_cs[f], -s_vx, -s_vy);
        static const uint8_t SET_F[4] = {F_SET_SPRING, F_SET_SUMMER, 255, F_SET_WINTER};
        for (int s = 0; s < 4; s++) {
            if (SET_F[s] == 255 || has_fact(SET_F[s])) {
                spr_to_fb(&s_cs[24 + s], -s_vx, -s_vy);
            }
        }
    }
}
static float s_hit_d;  // the distance of the last hit, relative to the size of the spot
static int hit_exit(int px, int py)
{
    int best = -1;
    float bd = 1;
    for (int i = 0; i < TW_NEXIT; i++) {
        const tw_exit_t *e = &TW_EXITS[i];
        if (e->place != T.pos) {
            continue;
        }
        const float d = sqrtf((float)(px - e->x) * (px - e->x) + (float)(py - e->y) * (py - e->y)) / e->r;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    s_hit_d = bd;
    return best;
}
static int hit_thing(int px, int py)
{
    int best = -1;
    float bd = 1;
    for (int i = 0; i < TW_NTHING; i++) {
        const tw_thing_t *t = &TW_THINGS[i];
        if (t->place != T.pos) {
            continue;
        }
        const float d = sqrtf((float)(px - t->x) * (px - t->x) + (float)(py - t->y) * (py - t->y)) / t->r;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    s_hit_d = bd;
    return best;
}
static void place_tap(int px, int py)
{
    if (s_to >= 0 || s_fade_dir) {
        return;
    }
    if (T.pos == P_WHEEL && wheel_tap(px, py)) {
        return;
    }
    if (T.pos == P_ICE && ice_tap(px, py)) {
        return;
    }
    if (T.pos == P_GROTTO && cry_tap(px, py)) {
        return;
    }
    if (T.pos == P_HALL && sta_tap(px, py)) {
        return;
    }
    if (T.pos == P_INNER && is_done(A_YEAR) && hit_thing(px, py) >= 0) {
        s_mode = M_END;
        s_t = 0;
        return;
    }
    // (the spot nearest to the tap, for its size, wins: a way out that is open, or a thing)
    const int e = hit_exit(px, py);
    const float de = s_hit_d;
    const int t = hit_thing(px, py);
    const float dt = s_hit_d;
    const char *why = NULL;
    const bool open = e >= 0 && path_state(TW_EXITS[e].to, &why) == 1;
    if (open && (t < 0 || de <= dt * 1.25f)) {
        walk_to(TW_EXITS[e].to);
        return;
    }
    if (t >= 0) {
        tap_thing(&TW_THINGS[t]);
        return;
    }
    if (e >= 0) {
        say(why);
        fx(FX_BUMP, 40);
        s_shake = 0.2f;
    }
}

// a swipe walks: the way out in the direction of the finger (up = forward, down = back, left, right). The class of a way
// out from its place in the picture: near a side edge = left / right, low in the picture = back, else forward. Two ways out
// of one class: the one nearer to the start of the finger.
static int exit_class(const tw_exit_t *e)
{
    if (e->x < 100) {
        return 2;
    }
    if (e->x > 540) {
        return 3;
    }
    if (e->y >= 205) {
        return 1;
    }
    return 0;
}
// the signs of the ways out (they make the ways out easy to see): a soft double chevron at each way out of the place, in the direction of its swipe. An open way: warm white, it
// pulses and moves a little to the way; a closed way with a visible cause (for example the ice): faint and still. A way
// out of the view shows at the edge of the view.
static float seg_d(float x, float y, float ax, float ay, float bx, float by)
{
    const float vx = bx - ax, vy = by - ay;
    float u = ((x - ax) * vx + (y - ay) * vy) / (vx * vx + vy * vy);
    if (u < 0) {
        u = 0;
    }
    if (u > 1) {
        u = 1;
    }
    const float dx = x - ax - vx * u, dy = y - ay - vy * u;
    return sqrtf(dx * dx + dy * dy);
}
static void px_mix(int x, int y, int r, int g, int b, int a)  // (ui_px is not in the ABI of an app file)
{
    if (a <= 0) {
        return;
    }
    if (a > 255) {
        a = 255;
    }
    uint16_t *p = &g_fb[y * SW + x];
    const uint16_t v = (uint16_t)(*p >> 8 | *p << 8);
    const int r0 = (v >> 11) << 3, g0 = ((v >> 5) & 63) << 2, b0 = (v & 31) << 3;
    *p = rgb((uint8_t)(r0 + (r - r0) * a / 255), (uint8_t)(g0 + (g - g0) * a / 255),
             (uint8_t)(b0 + (b - b0) * a / 255));  // (rgb() gives the frame order)
}
static void chevron(float cx, float cy, float ux, float uy, float k, int alpha)
{
    // (local points: s across the way, f along it; the tip points to the way)
    const float px = -uy, py = ux;
    const float ax = cx + px * -10 * k + ux * -4 * k, ay = cy + py * -10 * k + uy * -4 * k;
    const float bx = cx + ux * 6 * k, by = cy + uy * 6 * k;
    const float qx = cx + px * 10 * k + ux * -4 * k, qy = cy + py * 10 * k + uy * -4 * k;
    const int x0 = (int)(cx - 16 * k), x1 = (int)(cx + 16 * k), y0 = (int)(cy - 16 * k), y1 = (int)(cy + 16 * k);
    for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < SH; y++) {
        for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < SW; x++) {
            const float d1 = seg_d((float)x, (float)y, ax, ay, bx, by), d2 = seg_d((float)x, (float)y, bx, by, qx, qy);
            const float d = d1 < d2 ? d1 : d2;
            const float out = 4.6f * k - d, in = 2.4f * k - d;  // (the dark edge, then the light line; 1 px soft)
            if (out <= -0.5f) {
                continue;
            }
            if (in > -0.5f) {
                const float c = in >= 0.5f ? 1 : in + 0.5f;
                px_mix(x, y, 18, 12, 6, alpha * 3 / 5);
                px_mix(x, y, 255, 244, 214, (int)(alpha * c));
            } else {
                px_mix(x, y, 18, 12, 6, (int)(alpha * 3 / 5 * (out >= 0.5f ? 1 : out + 0.5f)));
            }
        }
    }
}
static int exit_class(const tw_exit_t *e);
static void exit_signs(void)
{
    if (s_to >= 0 || s_fade_dir || s_bag) {
        return;
    }
    float putx[8], puty[8];
    int nput = 0;
    for (int i = 0; i < TW_NEXIT; i++) {
        const tw_exit_t *e = &TW_EXITS[i];
        if (e->place != T.pos) {
            continue;
        }
        const char *why = NULL;
        const int st = path_state(e->to, &why);
        if (st != 1 && !why) {
            continue;  // (a hidden way stays hidden)
        }
        static const float DIR[4][2] = {{0, -1}, {0, 1}, {-1, 0}, {1, 0}};
        int guard = 0;
        const int c = exit_class(e);
        const float ux = DIR[c][0], uy = DIR[c][1];
        float x = (float)(e->x - s_vx), y = (float)(e->y - s_vy);
        if (x < 24) {
            x = 24;
        }
        if (x > SW - 24) {
            x = (float)(SW - 24);
        }
        if (y < 24) {
            y = 24;
        }
        if (y > SH - 24) {
            y = (float)(SH - 24);
        }
        if (RND) {  // (the round view: the sign inside the circle, and not under the keys of the top cap)
            const float c = SW / 2.0f, r = SW / 2.0f - 22, dx = x - c, dy = y - c, d2 = dx * dx + dy * dy;
            if (d2 > r * r) {
                const float k = r / sqrtf(d2);
                x = c + dx * k;
                y = c + dy * k;
            }
            if (y < SW * 0.21f && fabsf(x - c) < SW * 0.26f) {
                y = SW * 0.21f;
            }
        } else if (y < 58 && (x < 60 || x > SW - 110)) {
            y = 58;  // (not under the buttons)
        }
        for (int k = 0; k < nput; k++) {  // (not on an earlier sign)
            if (fabsf(x - putx[k]) < 40 && fabsf(y - puty[k]) < 40) {
                y = y + 44 <= SH - 24 ? puty[k] + 44 : puty[k] - 44;
                k = -1;
                if (++guard > 6) {
                    break;
                }
            }
        }
        if (nput < 8) {
            putx[nput] = x;
            puty[nput] = y;
            nput++;
        }
        if (st == 1) {
            const float ph = fsin(s_t * 3.2f + i);
            const float bob = 3 + 3 * ph;
            const int a = (int)(200 + 55 * ph);
            ui_circle((int)x, (int)y, 24, rgb(255, 226, 160), 34 + (int)(16 * ph));
            chevron(x - ux * 6 + ux * bob, y - uy * 5 + uy * bob, ux, uy, 1.15f, a);
            chevron(x + ux * 7 + ux * bob, y + uy * 6 + uy * bob, ux, uy, 1.15f, a * 2 / 3);
        } else {
            chevron(x, y, ux, uy, 0.9f, 95);
        }
    }
}
static void swipe_go(int dx, int dy, int px, int py)
{
    if (s_to >= 0 || s_fade_dir) {
        return;
    }
    const int want = abs(dy) >= abs(dx) ? (dy < 0 ? 0 : 1) : (dx < 0 ? 2 : 3);
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < TW_NEXIT; i++) {
        const tw_exit_t *e = &TW_EXITS[i];
        if (e->place != T.pos || exit_class(e) != want) {
            continue;
        }
        const int d = (e->x - px) * (e->x - px) + (e->y - py) * (e->y - py);
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best < 0) {
        fx(FX_BUMP, 22);
        return;
    }
    const char *why = NULL;
    if (path_state(TW_EXITS[best].to, &why) == 1) {
        walk_to(TW_EXITS[best].to);
    } else {
        fx(FX_BUMP, 40);
        s_shake = 0.2f;
    }
}

// ---------------------------------------------------------------- the journal and the map
typedef struct {
    uint8_t fact;
    const char *spr;
    const char *text[2];
} tw_clue_t;
static const tw_clue_t CLUES[] = {
        {F_CLUE_PLATES,
         "j_plates.twl",
         {"Grandma's things on the pegs. In which season did she use each one?",
          "הדברים של סבתא על הווים. באיזו עונה היא השתמשה בכל אחד?"}},
        {F_SAW_WHEEL, "j_wheel.twl", {"The Great Wheel: three sockets are empty.", "הגלגל הגדול: שלושה שקעים ריקים."}},
        {F_CLUE_MOON,
         "j_moon.twl",
         {"The carving under the roots: four moons in a row.", "החריטה מתחת לשורשים: ארבעה ירחים בשורה."}},
        {F_CLUE_GEARS,
         "j_gears.twl",
         {"The gears painted on the mill: big, small, middle.", "הגלגלים המצוירים בטחנה: גדול, קטן, בינוני."}},
        {F_CLUE_RINGS,
         "j_rings.twl",
         {"The rubbing of the owl rock: three rings, a mark on each.",
          "ההעתקה מסלע הינשוף: שלוש טבעות, סימן בכל אחת."}},
        {F_CLUE_STARS,
         "j_stars.twl",
         {"The low winter sun shines through four stones of the circle.",
          "שמש החורף הנמוכה מאירה דרך ארבע אבני המעגל."}},
        {F_CLUE_ICE, "j_ice.twl", {"The fox walks only on the thick ice.", "השועל הולך רק על הקרח העבה."}},
        {F_CLUE_COLOURS,
         "j_colours.twl",
         {"The bulbs bloomed: red, yellow, violet, white.", "הפקעות פרחו: אדום, צהוב, סגול, לבן."}},
        {F_CLUE_STATUES,
         "j_statues.twl",
         {"The sundial: one to four spots of light by the signs.", "שעון השמש: אחת עד ארבע נקודות אור ליד הסימנים."}},
};
#define NCLUE ((int)(sizeof CLUES / sizeof CLUES[0]))
static tw_spr_t s_jspr;
static int s_jspr_for = -1;
static int journal_pages(int *out)
{
    int n = 0;
    for (int i = 0; i < NCLUE; i++) {
        if (has_fact(CLUES[i].fact)) {
            out[n++] = i;
        }
    }
    return n;
}
// the hints of BEGINNER mode: the hand-written texts (tw_hints.inc) and the shared engine (libs/advhint). (No "cannot
// act yet" line here: this game has no text in the play.)
#include "tw_hints.inc"
#define HN_ACT(a)     (&TW_ACTS[a])
#define HN_AMASK(A)   ((A)->smask)
#define HN_NACT       TW_NACT
#define HN_NITEM      TW_NITEM
#define HN_NFACT      TW_NFACT
#define HN_NPLACE     TW_NPLACE
#define HN_FACTBIT(f) TW_BIT_FACT(f)
#define HN_PLACE(p)   TW_PLACES[p].name[s_lang]
#define HN_ITEM(i)    TW_ITEM_NAME[i][s_lang]
#define HN_MODE_OK(m) season_ok(m)
#define HN_HAS_WAY    0
#include "advhint/advhint.inc"

static void draw_journal(void)
{
    gfx_fill(rgb(232, 218, 186));
    for (int y = 0; y < SH; y += 4) {
        gfx_fill_rect(0, y, SW, 1, rgb(224, 208, 174));
    }
    int pg[NCLUE];
    const int n = journal_pages(pg);
    const int total = n + 1;  // (the FIRST page: the hint)
    if (s_jpage >= total) {
        s_jpage = total - 1;
    }
    int hi = 0, hn = 0;
    char h[640];
    if (s_jpage == 0) {
        hint_text(s_hint_lvl, h, sizeof h, TR("The year turns. Go to Grandma.", "השנה מסתובבת. לכו אל סבתא."), &hi,
                  &hn);
    }
    char b[64];
    snprintf(b, sizeof b, "%d / %d", s_jpage + 1, total);
    ui_text_c(&KF_LABEL, SW / 2, SH - 8, b, rgb(120, 90, 60));
    ui_text_c(&KF_NAME, 24, SH / 2 + 6, "<", rgb(90, 60, 40));
    ui_text_c(&KF_NAME, SW - 24, SH / 2 + 6, ">", rgb(90, 60, 40));
    if (!RND) {  // (the round panels: the X is in the top band, tw_round.inc)
        ui_circle(SW - 26, 24, 18, rgb(200, 180, 140), 255);
        ui_text_c(&KF_LABEL, SW - 26, 29, "X", rgb(90, 60, 40));
    }
    if (s_jpage > 0) {
        const tw_clue_t *c = &CLUES[pg[s_jpage - 1]];
        if (s_jspr_for != pg[s_jpage - 1]) {
            spr_free(&s_jspr);
            pk_sprite(c->spr, &s_jspr);
            s_jspr_for = pg[s_jpage - 1];
        }
        if (s_jspr.ix) {
            spr_to_fb(&s_jspr, SW / 2 - s_jspr.w / 2 - s_jspr.x, (s_wide ? 10 : 30) - s_jspr.y);
        }
        text_wrap(&KF_TEXT, SW / 2, SH - (s_wide ? 30 : 46), SW - 80, c->text[s_lang], rgb(60, 40, 24), 18);
    } else {
        char t[40];
        if (hn > 1) {
            snprintf(t, sizeof t, "%s %d/%d", TR("A HINT", "רמז"), hi, hn);
        } else {
            snprintf(t, sizeof t, "%s", TR("A HINT", "רמז"));
        }
        ui_text_c(&KF_NAME, SW / 2, s_wide ? 30 : 40, t, rgb(90, 50, 30));
        hint_draw(h, SW / 2, s_wide ? 58 : 76, SH - 46, SW - 92, rgb(60, 40, 24), hi < hn);
    }
}
static void journal_tap(int x, int y)
{
    int pg[NCLUE];
    const int total = journal_pages(pg) + 1;
    if (!RND && x > SW - 56 && y < 50) {
        s_mode = M_PLACE;
        spr_free(&s_jspr);
        s_jspr_for = -1;
        return;
    }
    if (x < SW / 4) {
        if (s_jpage > 0) {
            s_jpage--;
        }
        s_hint_lvl = 0;
    } else if (x > SW * 3 / 4) {
        if (s_jpage < total - 1) {
            s_jpage++;
        }
        s_hint_lvl = 0;
    } else if (s_jpage == 0 && s_hint_lvl < 4) {
        s_hint_lvl++;  // (the hint page: the next part of the hint)
    }
    tone(500, 600, 30, 8, W_TRI);
}
// the map: the places that the player knows, their paths; a tap on an open place goes there
static const int16_t MAP_X[TW_NPLACE] = {70,  70,  196, 318, 430, 430,  430, 548, 560, 660, 660, 800, 790,
                                         910, 970, 960, 960, 920, 1000, 560, 680, 810, 810, 810, 950};
static const int16_t MAP_Y[TW_NPLACE] = {330, 455, 455, 455, 380, 270, 520, 290, 420, 480, 575, 575, 440,
                                         440, 360, 280, 200, 525, 525, 170, 110, 110, 215, 330, 110};
static int s_map_sel = -1;  // the place that the player tapped once on the map
static void map_label(int x, int y, const char *t, uint16_t col)
{
    const int w = ui_text_w(&KF_LABEL, t);
    if (x < w / 2 + 6) {
        x = w / 2 + 6;
    }
    if (x > SW - w / 2 - 6) {
        x = SW - w / 2 - 6;
    }
    ui_text_c(&KF_LABEL, x, y, t, col);
}
static void map_xy(int p, int *x, int *y)
{
    *x = 20 + MAP_X[p] * (SW - 40) / 1040;
    *y = 14 + MAP_Y[p] * (SH - 34) / 620;
}
static void draw_map(void)
{
    gfx_fill(rgb(226, 210, 172));
    const uint32_t can = reachable_now();
    for (int i = 0; i < TW_NPATH; i++) {
        const tw_path_t *p = &TW_PATHS[i];
        if (!(T.seen >> p->a & 1) || !(T.seen >> p->b & 1)) {
            continue;
        }
        int x0, y0, x1, y1;
        map_xy(p->a, &x0, &y0);
        map_xy(p->b, &x1, &y1);
        const int n = abs(x1 - x0) + abs(y1 - y0);
        for (int k = 0; k <= n; k += 2) {
            gfx_fill_rect(x0 + (x1 - x0) * k / (n ? n : 1), y0 + (y1 - y0) * k / (n ? n : 1), 1, 1, rgb(140, 100, 60));
        }
    }
    for (int p = 0; p < TW_NPLACE; p++) {
        if (!(T.seen >> p & 1)) {
            continue;
        }
        int x, y;
        map_xy(p, &x, &y);
        const bool here = p == T.pos, go = can >> p & 1;
        if (p == s_map_sel) {
            ui_circle(x, y, 10, rgb(255, 210, 120), 255);
        }
        ui_circle(x, y, here ? 7 : 5, here ? rgb(200, 60, 40) : go ? rgb(90, 60, 30) : rgb(170, 150, 120), 255);
        if (here) {
            map_label(x, y - 10, TW_PLACES[p].name[s_lang], rgb(160, 40, 30));
        }
    }
    if (s_map_sel >= 0) {
        const bool go = (can >> s_map_sel & 1) && s_map_sel != T.pos;
        char b[96];
        snprintf(b, sizeof b, "%s%s", TW_PLACES[s_map_sel].name[s_lang],
                 s_map_sel == T.pos ? TR(" (HERE)", " (כאן)")
                 : go               ? TR(": TAP AGAIN", ": הקישו שוב")
                                    : TR(": NOT OPEN NOW", ": סגור עכשיו"));
        fb_dark(0, SH - 26, SW, 26, 5);
        ui_text_c(&KF_LABEL, SW / 2, SH - 8, b, go ? rgb(120, 40, 20) : rgb(90, 70, 50));
    }
    if (!RND) {  // (the round panels: the X is in the top band, tw_round.inc)
        ui_circle(SW - 26, 24, 18, rgb(200, 180, 140), 255);
        ui_text_c(&KF_LABEL, SW - 26, 29, "X", rgb(90, 60, 40));
    }
}
static void map_tap(int x, int y)
{
    if (!RND && x > SW - 56 && y < 50) {
        s_mode = M_PLACE;
        s_map_sel = -1;
        return;
    }
    const uint32_t can = reachable_now();
    int best = -1, bd = 24 * 24;
    for (int p = 0; p < TW_NPLACE; p++) {
        if (!(T.seen >> p & 1)) {
            continue;
        }
        int px, py;
        map_xy(p, &px, &py);
        const int d = (px - x) * (px - x) + (py - y) * (py - y);
        if (d < bd) {
            bd = d;
            best = p;
        }
    }
    if (best < 0) {
        return;
    }
    if (best == s_map_sel && best != T.pos && (can >> best & 1)) {
        s_mode = M_PLACE;
        s_map_sel = -1;
        walk_to(best);
        return;
    }
    s_map_sel = best;
    tone(600, 700, 30, 8, W_TRI);
}

// ---------------------------------------------------------------- the title and the end
static bool s_has;
static float s_title_t;
static int s_title_dn = -1;
static float s_new_t;
static void title_btn(int i, int *x, int *y)
{
#if BOARD_ROUND
    if (s_rnd) {  // (the round panels: the bottom of the circle; the points of the panel)
        const int d = rv_panel(&s_rv);
        *y = d * 74 / 100;
        *x = s_has ? d / 2 + (i ? 1 : -1) * d * 15 / 100 : d / 2;
        return;
    }
#endif
    *y = s_wide ? SH - 36 : SH - 46;
    *x = s_has ? SW / 2 + (i ? 1 : -1) * 70 : SW / 2;
}
static void title_go(void)
{
    s_mode = M_TITLE;
    s_title_t = 0;
    s_has = tw_load();
    if (!s_has) {
        tw_new();
    }
    s_pack_err = 0;
    pic_load(TW_PLACES[P_TREE].pic[2]);
    if (s_pack_err) {
        s_mode = M_NODATA;
    }
    s_vx = s_wide ? 0 : 330 - SW / 2;
    s_vy = s_wide ? 40 : 0;
#if BOARD_UI_TABLET
    tab5_view();
#endif
}
static void new_game(void)
{
    tw_new();
    s_sel = -1;
    s_mode = M_PLACE;
    enter_place();
    s_fade = 1;
    s_fade_dir = -1;
}
static void continue_game(void)
{
    s_mode = M_PLACE;
    enter_place();
    s_fade = 1;
    s_fade_dir = -1;
}
static void draw_title(void)
{
    for (int y = 0; y < SH; y++) {
        memcpy(g_fb + y * SW, s_pic + (y + s_vy) * PW + s_vx, (size_t)SW * 2);
    }
    const int ty = s_wide ? 48 : 70;
    const char *name = s_lang ? "היער הסובב" : "THE TURNING WOOD";
    if (ui_text_w(&KF_TITLE, name) <= SW - 24) {
        ui_text_c(&KF_TITLE, SW / 2 + 2, ty + 2, name, rgb(20, 14, 10));
        ui_text_c(&KF_TITLE, SW / 2, ty, name, rgb(250, 238, 210));
    } else {
        ui_text_c(&KF_TITLE, SW / 2 + 2, ty - 14, TR("THE TURNING", "היער"), rgb(20, 14, 10));
        ui_text_c(&KF_TITLE, SW / 2, ty - 16, TR("THE TURNING", "היער"), rgb(250, 238, 210));
        ui_text_c(&KF_TITLE, SW / 2 + 2, ty + 20, TR("WOOD", "הסובב"), rgb(20, 14, 10));
        ui_text_c(&KF_TITLE, SW / 2, ty + 18, TR("WOOD", "הסובב"), rgb(250, 238, 210));
    }
    for (int i = 0; i < 2; i++) {
        if (i == 0 && !s_has) {
            continue;
        }
        int x, y;
        title_btn(i, &x, &y);
        const bool armed = i == 1 && s_title_dn == 1;
        ui_circle(x, y, 26, armed ? rgb(200, 120, 60) : rgb(30, 22, 14), 220);
        ui_ring(x, y, 26, 2, rgb(201, 162, 79), 255);
        ui_text_c(&KF_LABEL, x, y + 5,
                  i == 0 || !s_has ? TR("PLAY", "שחק")
                  : armed          ? TR("AGAIN?", "שוב?")
                                   : TR("NEW", "חדש"),
                  rgb(250, 238, 210));
    }
}
static void title_tap(int x, int y)
{
    if (s_title_t < 0.4f) {
        return;
    }
    for (int i = 0; i < 2; i++) {
        if (i == 0 && !s_has) {
            continue;
        }
        int bx, by;
        title_btn(i, &bx, &by);
        if ((x - bx) * (x - bx) + (y - by) * (y - by) > (RND ? 42 * 42 : 34 * 34)) {
            continue;
        }
        if (i == 0 && s_has) {
            continue_game();
            tone(600, 800, 120, 18, W_TRI);
        } else if (!s_has || (s_title_dn == 1 && s_new_t > 0)) {
            new_game();
            tone(600, 800, 120, 18, W_TRI);
        } else {
            s_title_dn = 1;
            s_new_t = 2.0f;
            tone(500, 500, 60, 14, W_TRI);
        }
        return;
    }
}
static void draw_end(void)
{
    for (int y = 0; y < SH; y++) {
        memcpy(g_fb + y * SW, s_pic + (y + s_vy) * PW + s_vx, (size_t)SW * 2);
    }
    if (s_t < 1.5f) {
        fb_dark(0, 0, SW, SH, 2 + (int)(s_t * 4));  // (the light comes up)
    }
    // (the words go in a dark band at the top and THE END at the side or the bottom: Grandma in the middle stays seen)
    if (s_t > 1.5f) {
        const int bh = s_wide ? 50 : 70;
        fb_dark(0, 0, SW, bh, 2);
        text_wrap(&KF_TEXT, SW / 2, 20, SW - 40,
                  TR("Grandma opens her eyes. \"The wood turns again. Thank you, my dear.\"",
                     "סבתא פותחת את עיניה. \"היער מסתובב שוב. תודה, יקירי.\""),
                  rgb(255, 244, 220), 20);
    }
    if (s_t > 3.0f) {
        if (s_wide) {
            ui_text_c(&KF_TITLE, SW / 6, SH / 2 + 30, TR("THE END", "הסוף"), rgb(255, 230, 170));
        } else {
            fb_dark(0, SH - 34, SW, 34, 3);
            ui_text_c(&KF_TITLE, SW / 2, SH - 8, TR("THE END", "הסוף"), rgb(255, 230, 170));
        }
    }
}
static void draw_nodata(void)
{
    gfx_fill(rgb(20, 16, 12));
    text_wrap(&KF_TEXT, SW / 2, SH / 2 - 10, SW - 40,
              TR("The pictures of the game are not on the SD card. Install its data from the STORE.",
                 "תמונות המשחק אינן בכרטיס הזיכרון. התקינו את המידע שלו מהחנות."),
              rgb(255, 220, 180), 20);
}

// ---------------------------------------------------------------- the panel
enum {
    PI_LANG,
    PI_SND,
    PI_MUS,
    PI_BEG,
    PI_NEWG,
    PI_TITLE
};
static const char *const BEG_V[] = {"OFF", "כבוי", "ON", "פועל"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const pitem_t ITEMS[] = {
        {PI_PICK, PI_LANG, 2, 0, {"LANGUAGE", "שפה"}, LANG_V, TW_KEY "lang"},
        {PI_PICK, PI_SND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, TW_KEY "snd"},
        {PI_PICK, PI_MUS, 2, 0, {"MUSIC", "מוזיקה"}, ONOFF_V, TW_KEY "mus"},
        {PI_PICK, PI_BEG, 2, 0, {"BEGINNER (JOURNAL, MAP)", "מתחילים (יומן, מפה)"}, BEG_V, TW_KEY "beg"},
        {PI_ACT2, PI_NEWG, 0, 0, {"NEW GAME", "משחק חדש"}, NULL, NULL},
        {PI_ACT, PI_TITLE, 0, 0, {"THE TITLE", "המסך הראשי"}, NULL, NULL},
};
static bool item_shown(int id)
{
    return (id != PI_NEWG && id != PI_TITLE) || (s_mode != M_TITLE && s_mode != M_NODATA);
}
static void item_set(int id, int v)
{
    if (!s_pic) {
        return;
    }
    switch (id) {
    case PI_LANG:
        s_lang = v & 1;
        break;
    case PI_SND:
        s_snd = v == 0;
        break;
    case PI_MUS:
        s_music = v == 0;
        break;
    case PI_BEG:
        s_beg = v == 1;
        if (!s_beg && (s_mode == M_JOURNAL || s_mode == M_MAP)) {
            s_mode = M_PLACE;
        }
        break;
    case PI_NEWG:
        new_game();
        break;
    case PI_TITLE:
        cs_free();
        title_go();
        break;
    }
}
static const pitems_t PANEL = {ITEMS, sizeof ITEMS / sizeof ITEMS[0], item_shown, NULL, item_set, NULL, NULL};

// ---------------------------------------------------------------- the app
static int s_frames;
static int64_t s_fps_t0;
static void draw_feedback(void);
static void input_main(const input_event_t *ev);
#include "tw_round.inc"
static void enter(int64_t now)
{
    s_wide = SW > SH * 2;
    if (!s_pbuf) {
        s_pbuf = heap_caps_malloc((size_t)PW * PHB * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_idx) {
        s_idx = heap_caps_malloc((size_t)PW * PHB, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!s_pbuf || !s_idx) {
        ESP_LOGE(TAG, "no memory");
        heap_caps_free(s_pbuf);
        heap_caps_free(s_idx);
        s_pbuf = NULL;
        s_pic = NULL;
        s_idx = NULL;
        return;
    }
    s_pic = s_pbuf + T5Y * PW;
    s_lang = store_get(TW_KEY "lang", 0) ? 1 : 0;
    s_snd = store_get(TW_KEY "snd", 0) == 0;
    s_music = store_get(TW_KEY "mus", 0) == 0;
    s_beg = store_get(TW_KEY "beg", 0) == 1;
    sysui_panel_items(&PANEL);
    s_last = now;
    s_t = 0;
    s_to = -1;
    s_fade = 0;
    s_fade_dir = 0;
    s_msg_t = 0;
    s_bag = false;
    s_sel = -1;
    s_wturn = 0;
    tfsfx_load_mem(TW_SFX_DATA, TW_SFX_LEN, FXN, FX_N < TW_SFX_N ? FX_N : TW_SFX_N, 45);  // (in the app file: no card)
    mu_start();
    for (int i = 0; i < TW_NITEM; i++) {
        pk_sprite(TW_ITEM_ICON[i], &s_icon[i]);
    }
    pk_sprite("i_b_bag.twl", &s_btn[0]);
    pk_sprite("i_b_journal.twl", &s_btn[1]);
    pk_sprite("i_b_map.twl", &s_btn[2]);
#if BOARD_ROUND
    s_rnd = gfx_round() && rv_open(&s_rv, 360 * 240);
    s_rv_lw = 0;
    if (s_rnd) {
        rnd_logic_on();  // (the title: the view of the round frame)
        title_go();
        rnd_logic_off();
        return;
    }
#endif
    title_go();
}
static void leave(void)
{
    if (!s_pic) {
        return;
    }
    mu_end();  // (the music stops before the effects go)
    tfsfx_free();
    anim_free();
    cs_free();
    spr_free(&s_jspr);
    s_jspr_for = -1;
    for (int i = 0; i < TW_NITEM; i++) {
        spr_free(&s_icon[i]);
    }
    for (int i = 0; i < 3; i++) {
        spr_free(&s_btn[i]);
    }
    heap_caps_free(s_pbuf);
    s_pbuf = NULL;
    s_pic = NULL;
    heap_caps_free(s_idx);
    s_idx = NULL;
#if BOARD_ROUND
    rv_close(&s_rv);
    s_rnd = false;
#endif
}
static void input_main(const input_event_t *ev)
{
    const int x = ev->x, y = ev->y;
    if (s_mode == M_PLACE && s_bag && !s_rv_logic && y >= bag_top()) {  // (a drag in the bag scrolls its objects)
        if (ev->kind == IN_DOWN) {
            s_drag = false;
            s_dx0 = x;
            s_vx0 = s_bag_scroll;
            return;
        }
        if (ev->kind == IN_MOVE) {
            if (!s_drag && abs(x - s_dx0) > 10) {
                s_drag = true;
                input_tap_cancel();
            }
            if (s_drag) {
                int it[TW_NITEM];
                const int mx = bag_scroll_max(bag_items(it));
                s_bag_scroll = s_vx0 - (x - s_dx0);
                if (s_bag_scroll > mx) {
                    s_bag_scroll = mx;
                }
                if (s_bag_scroll < 0) {
                    s_bag_scroll = 0;
                }
            }
            return;
        }
    }
    if (s_mode == M_PLACE && !s_bag) {  // (a drag looks round the picture; a quick swipe walks)
        if (ev->kind == IN_DOWN) {
            s_drag = false;
            s_dx0 = x;
            s_dy0 = y;
            s_vx0 = s_vx;
            s_vy0 = s_vy;
            s_down_ms = ev->t_ms;
            return;
        }
        if (ev->kind == IN_UP && s_drag && ev->t_ms - s_down_ms < 300 && abs(x - s_dx0) + abs(y - s_dy0) > 45) {
            s_vx = s_vx0;
            s_vy = s_vy0;  // (the swipe is not a look: the view goes back)
            swipe_go(x - s_dx0, y - s_dy0, s_dx0 + s_vx, s_dy0 + s_vy);
            return;
        }
        if (ev->kind == IN_MOVE) {
            if (!s_drag && abs(x - s_dx0) + abs(y - s_dy0) > 10) {
                s_drag = true;
                input_tap_cancel();
            }
            if (s_drag) {
                if (s_wide) {
                    s_vy = s_vy0 - (y - s_dy0);
                    if (s_vy < 0) {
                        s_vy = 0;
                    }
                    if (s_vy > PH - SH) {
                        s_vy = PH - SH;
                    }
                } else {
                    s_vx = s_vx0 - (x - s_dx0);
                    if (s_vx < 0) {
                        s_vx = 0;
                    }
                    if (s_vx > PW - SW) {
                        s_vx = PW - SW;
                    }
                }
            }
            return;
        }
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    s_tap_x = x + s_vx;
    s_tap_y = y + s_vy;
    if (s_mode == M_PLACE) {
        s_ring_t = 0.35f;
    }
    switch (s_mode) {
    case M_TITLE:
        title_tap(x, y);
        return;
    case M_NODATA:
        return;
    case M_END:
        if (s_t > 3) {
            store_set(TW_KEY "has", 0);
            title_go();
        }
        return;
    case M_JOURNAL:
        journal_tap(x, y);
        return;
    case M_MAP:
        map_tap(x, y);
        return;
    case M_CU:
        if (!RND && x < 60 && y > SH - 60) {
            close_cu();
            return;
        }
        cu_tap(x + s_vx, y + s_vy);
        return;
    default:
        break;
    }
    if (s_msg_t > 0 && s_msg_t < 3.5f) {
        s_msg_t = 0;  // (a tap ends a line of text that was read)
    }
    const int b = btn_hit(x, y);
    if (b == 0) {
        s_bag = !s_bag;
        tone(400, 520, 40, 10, W_TRI);
        return;
    }
    if (b == 1) {
        s_mode = M_JOURNAL;
        s_jpage = 0;
        s_hint_lvl = 0;
        tone(400, 520, 40, 10, W_TRI);
        return;
    }
    if (b == 2) {
        s_mode = M_MAP;
        tone(400, 520, 40, 10, W_TRI);
        return;
    }
    if (s_bag && !s_rv_logic && bag_tap(x, y)) {
        return;
    }
    place_tap(x + s_vx, y + s_vy);
}
static void input_ev(const input_event_t *ev)
{
    if (!s_pic) {
        return;
    }
#if BOARD_ROUND
    if (s_rnd) {
        rnd_input(ev);
        return;
    }
#endif
    input_main(ev);
}
// a slow tap (a press of 350 - 900 ms) on each screen (the quest has no holds) acts as a tap (libs/slowtap), on the
// round frame too
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
static void update_main(int64_t now);
static void update(int64_t now)
{
#if BOARD_ROUND
    if (s_rnd) {
        rnd_logic_on();
        update_main(now);
        rnd_logic_off();
        return;
    }
#endif
    update_main(now);
}
static void update_main(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    s_t += dt;
    s_title_t += dt;
    if (s_new_t > 0 && (s_new_t -= dt) <= 0) {
        s_title_dn = -1;
    }
    if (s_msg_t > 0) {
        s_msg_t -= dt;
    }
    if (s_shake > 0) {
        s_shake -= dt;
    }
    if (s_fade_dir > 0) {
        s_fade += dt / 0.25f;
        if (s_fade >= 1) {
            s_fade = 1;
            walk_done();
        }
    } else if (s_fade_dir < 0) {
        s_fade -= dt / 0.3f;
        if (s_fade <= 0) {
            s_fade = 0;
            s_fade_dir = 0;
        }
    }
    if (s_mode == M_PLACE) {
        anim_step(dt);
    }
    if (s_ring_t > 0) {
        s_ring_t -= dt;
    }
    if (s_spark_t > 0) {
        s_spark_t -= dt;
    }
    if (s_fly_t > 0 && (s_fly_t -= dt) <= 0) {
        s_fly_item = -1;
    }
    if (s_mode == M_PLACE && T.pos == P_WHEEL) {
        wheel_step(dt);
        if (s_ncs >= 28 && s_fade_dir == 0) {
            wheel_more();
        }
    }
    // the music: the title piece on the title, the piece of the season in the wood, the end piece at the end
    mu_play(!s_music || s_mode == M_NODATA ? -1
            : s_mode == M_TITLE            ? TWM_TITLE
            : s_mode == M_END              ? TWM_END
                                           : TWM_SEASON0 + T.season);
    if (s_mode == M_CU && s_solved_t > 0 && (s_solved_t -= dt) <= 0) {
        static const uint8_t ACT[5] = {A_CHEST, A_MOONDOOR, A_STARBOX, A_GEARS, A_RINGS};
        const int a = ACT[s_cu];
        close_cu();
        do_act(a);
    }
}
// the feedback with no text: the ring of the tap, the sparkle of an action, the object that flies to the bag
static void draw_feedback(void)
{
    if (s_ring_t > 0) {
        ui_ring(s_tap_x - s_vx, s_tap_y - s_vy, (int)(10 + (0.35f - s_ring_t) * 40), 2, rgb(255, 248, 230),
                (int)(s_ring_t * 400));
    }
    if (s_spark_t > 0) {
        const float u = 1 - s_spark_t / 0.9f;
        for (int k = 0; k < 8; k++) {
            const float a = k * 0.785f + u;
            const int r0 = (int)(6 + u * 18), r1 = (int)(10 + u * 30);
            for (int r = r0; r < r1; r += 2) {
                ui_circle(s_spark_x - s_vx + (int)(fcos(a) * r), s_spark_y - s_vy + (int)(fsin(a) * r), 1,
                          rgb(255, 236, 170), (int)(220 * (1 - u)));
            }
        }
        ui_circle(s_spark_x - s_vx, s_spark_y - s_vy, (int)(6 + u * 10), rgb(255, 246, 210), (int)(140 * (1 - u)));
    }
    if (s_fly_item >= 0 && s_icon[s_fly_item].ix) {
        int bx, by;
        btn_xy(0, &bx, &by);
#if BOARD_ROUND
        if (s_rnd) {
            rv_point(&s_rv, &bx, &by);  // (the bag key of the panel, in the view)
        }
#endif
        const float u = 1 - s_fly_t / 0.75f, e = u * u * (3 - 2 * u);
        const int x = (int)(s_fly_x0 - s_vx + (bx - (s_fly_x0 - s_vx)) * e),
                  y = (int)(s_fly_y0 - s_vy + (by - (s_fly_y0 - s_vy)) * e - fsin(u * 3.14f) * 30);
        spr_to_fb(&s_icon[s_fly_item], x - 24, y - 24);
    }
}
static void render(int64_t now)
{
    // (the frames a second in the log, each 5 s)
    s_frames++;
    if (now - s_fps_t0 >= 5000) {
        if (s_fps_t0) {
            ESP_LOGI(TAG, "mode %d place %d: %.1f fps", s_mode, T.pos, s_frames * 1000.0f / (float)(now - s_fps_t0));
        }
        s_fps_t0 = now;
        s_frames = 0;
    }
    if (!s_pic) {
        gfx_fill(rgb(0, 0, 0));
        ui_text_c(&KF_LABEL, SW / 2, SH / 2, "NO MEMORY", rgb(255, 200, 160));
        return;
    }
#if BOARD_ROUND
    if (s_rnd) {
        rnd_render();
        return;
    }
#endif
    switch (s_mode) {
    case M_TITLE:
        draw_title();
        return;
    case M_NODATA:
        draw_nodata();
        return;
    case M_END:
        draw_end();
        return;
    case M_JOURNAL:
        draw_journal();
        return;
    case M_MAP:
        draw_map();
        return;
    case M_CU:
        for (int y = 0; y < SH; y++) {
            memcpy(g_fb + y * SW, s_pic + (y + s_vy) * PW + s_vx, (size_t)SW * 2);
        }
        cu_draw();
        ui_circle(30, SH - 30, 22, rgb(20, 16, 12), 200);
        ui_text_c(&KF_LABEL, 30, SH - 25, "X", rgb(250, 230, 190));
        if (s_solved_t > 0) {
            ui_circle(SW / 2, SH / 2, (int)(120 * (1.2f - s_solved_t)), rgb(255, 240, 200), (int)(160 * s_solved_t));
        }
        return;
    default:
        break;
    }
    draw_place();
    if (s_shake > 0) {
        const int dx = (int)(fsin(s_shake * 60) * 2);
        if (dx) {
            for (int y = 0; y < SH; y++) {
                memmove(g_fb + y * SW + (dx > 0 ? dx : 0), g_fb + y * SW + (dx > 0 ? 0 : -dx),
                        (size_t)(SW - abs(dx)) * 2);
            }
        }
    }
    exit_signs();
    draw_feedback();
    if (s_bag) {
        draw_bag();
    } else {
        draw_msg();
    }
    draw_buttons();
    fade_frame(s_fade);
}
#include "tfdraw/tfdraw.h"
// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set in each app; all static and unused is no warning)
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
// the icon: the magic tree of the wood with the four seasons in its crown (spring, summer, autumn, winter); a ring of
// sparks turns round it, an autumn leaf and a snowflake fall (a loop of 2 s)
static void icon(int x, int y, int s)
{
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f, an = ph * 6.2831853f;
    ic_dband(x, y, s, 0, s * 80 / 100, ic_g(64, 32, 112), ic_g(32, 96, 96));
    ic_dband(x, y, s, s * 80 / 100, s, ic_g(32, 96, 48), ic_g(16, 48, 32));
    // the dark pines at the sides
    static const float PINE[] = {0, -20, 7, -4, 4, -4, 10, 10, -10, 10, -4, -4, -7, -4};
    ic_shape(PINE, 7, x + 13 * u, y + 64 * u, u * 1.1f, 0, ic_g(16, 48, 48), 256);
    ic_shape(PINE, 7, x + 88 * u, y + 60 * u, u * 1.3f, 0, ic_g(16, 48, 48), 256);
    // the trunk with its roots
    float cx = x + 50 * u, cy = y + 40 * u;
    float tr[] = {cx - 4 * u,  cy + 14 * u, cx + 4 * u,  cy + 14 * u, cx + 6 * u, cy + 40 * u,
                  cx + 14 * u, cy + 46 * u, cx - 14 * u, cy + 46 * u, cx - 6 * u, cy + 40 * u};
    ic_oval(cx, cy + 46 * u, 20 * u, 3.6f * u, ic_g(16, 32, 16), 200);
    tfd_poly(tr, 6, ic_g(96, 64, 32), 256);
    tfd_line(cx + 2.5f * u, cy + 16 * u, cx + 4.5f * u, cy + 42 * u, 2.4f * u, ic_g(64, 32, 16), 256);
    tfd_line(cx - 2 * u, cy + 18 * u, cx - 3 * u, cy + 38 * u, 1.6f * u, ic_g(160, 112, 64), 256);
    // the crown: a dark rim, then blobs that take the colour of their season (by the side of the crown)
    static const int8_t BL[13][3] = {{-16, -10, 11}, {0, -16, 12}, {16, -10, 11}, {-21, 4, 10}, {21, 4, 10},
                                     {-12, 14, 10},  {12, 14, 10}, {0, 16, 9},    {-8, -4, 12}, {8, -4, 12},
                                     {-6, 6, 11},    {6, 6, 11},   {0, -2, 10}};
    for (int k = 0; k < 13; k++) {
        tfd_disc(cx + BL[k][0] * u + 1.5f * u, cy + BL[k][1] * u + 2 * u, (BL[k][2] + 1.5f) * u, ic_g(16, 32, 32), 256);
    }
    for (int k = 0; k < 13; k++) {
        float bx = cx + BL[k][0] * u, by = cy + BL[k][1] * u, r = BL[k][2] * u;
        int q = BL[k][1] < 0 ? (BL[k][0] < 0 ? 0 : 1) : (BL[k][0] < 0 ? 3 : 2);
        if (BL[k][0] == 0) {
            q = BL[k][1] < 0 ? 1 : 3;
        }
        static const uint8_t C[4][2][3] = {{{160, 224, 96}, {96, 176, 64}},
                                           {{64, 176, 64}, {32, 112, 48}},
                                           {{224, 160, 32}, {192, 96, 32}},
                                           {{224, 240, 224}, {160, 192, 224}}};
        tfd_disc(bx, by, r, ic_g(C[q][1][0], C[q][1][1], C[q][1][2]), 256);
        tfd_disc(bx - r * 0.2f, by - r * 0.25f, r * 0.72f, ic_g(C[q][0][0], C[q][0][1], C[q][0][2]), 256);
    }
    // (blossoms in spring, apples in summer, a white edge of snow in winter)
    tfd_disc(cx - 18 * u, cy - 12 * u, 1.8f * u, ic_g(224, 160, 192), 256);
    tfd_disc(cx - 10 * u, cy - 7 * u, 1.8f * u, ic_g(224, 160, 192), 256);
    tfd_disc(cx - 20 * u, cy + 2 * u, 1.8f * u, ic_g(224, 160, 192), 256);
    tfd_disc(cx + 12 * u, cy - 12 * u, 2 * u, ic_g(224, 64, 64), 256);
    tfd_disc(cx + 18 * u, cy - 4 * u, 2 * u, ic_g(224, 64, 64), 256);
    tfd_disc(cx - 9 * u, cy + 16 * u, 1.6f * u, ic_g(128, 192, 224), 256);
    tfd_disc(cx - 15 * u, cy + 10 * u, 1.6f * u, ic_g(128, 192, 224), 256);
    // the ring of sparks: six that turn round the crown (a sixth of a turn in each loop)
    for (int k = 0; k < 6; k++) {
        float a = k * 1.0471976f + an / 6, sx = cx + fcos(a) * 33 * u, sy = cy + 2 * u + fsin(a) * 12 * u;
        if (fsin(a) < -0.2f) {
            continue;
        }
        float r = (1.6f + 1.2f * fsin(an * 2 + k)) * u + 0.6f;
        tfd_disc(sx, sy, r * 1.8f, ic_g(224, 224, 128), 90);
        tfd_line(sx - r * 1.6f, sy, sx + r * 1.6f, sy, 1 * u, ic_g(224, 240, 192), 256);
        tfd_line(sx, sy - r * 1.6f, sx, sy + r * 1.6f, 1 * u, ic_g(224, 240, 192), 256);
    }
    // an autumn leaf that falls and sways, a snowflake in the winter side
    float lx = cx + 22 * u + 4 * u * fsin(an * 2), ly = cy + 10 * u + ph * 34 * u;
    static const float LEAF[] = {0, -3, 2.4f, 0, 0, 3, -2.4f, 0};
    ic_shape(LEAF, 4, lx, ly, u * 1.3f, an * 2, ic_g(224, 112, 32), 256);
    float fx = cx - 26 * u - 3 * u * fsin(an), fy = cy + 4 * u + fmodf(ph + 0.5f, 1.0f) * 36 * u;
    for (int k = 0; k < 3; k++) {
        float a = k * 1.0471976f + an * 0.5f;
        tfd_line(fx - fcos(a) * 2.6f * u, fy - fsin(a) * 2.6f * u, fx + fcos(a) * 2.6f * u, fy + fsin(a) * 2.6f * u,
                 1 * u, ic_g(224, 240, 240), 256);
    }
    ic_end();
}
static bool awake(void)
{
    return s_mode != M_TITLE;
}
const app_t app_wood = {
        .name = "THE TURNING WOOD",
        .landscape = true,
        .round = APP_ROUND_HI,  // (the round panels: the place in the whole circle, tw_round.inc)
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
};
