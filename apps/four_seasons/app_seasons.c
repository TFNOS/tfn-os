// FOUR SEASONS (a store app for the 3.49): a puzzle in one valley in four seasons. The goal: repair
// grandpa's kite and fly it in the autumn wind on the hill. A drag left / right changes the season (a ring); a tap on a
// place zooms into its close view (a round wipe); BACK goes out. The things of the puzzle show only in their season
// (the kite in the bare winter tree, the key in the autumn vines, the attic trunk in the low winter beam ...). The items
// go into the bag and act by themselves when the player taps the right place in the right season.
//   The scenes are pictures on the SD card (/sdcard/seasons/sNN.fsn, made by tools/bake.py from tools/sc_*.py): per season a palette and 1 byte a pixel, and layers for the things
//   that change. fs_core.inc loads them a little in each frame and draws them (a plain copy, the blend of two seasons,
//   the zoom, the round wipe). fs_geo.inc (made by bake.py) has the scenes, the hot spots and the things.
//   The controls: a tap = act or go; a drag left / right = the season; tilt = a small pan of the picture; the BOOT
//   key or the ? button = a hint (1 where, 2 when, 3 what). The panel: SEASON, LANGUAGE, MUSIC, SOUND, HINT, NEW GAME
//   (a hold), THE TITLE. Saves: fs_flags, fs_bag, fs_sea, fs_scn, fs_log, fs_lang, fs_mus, fs_snd.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "miniz.h"
#include "midi_bg/midi_bg.h"
#include "fs_music.h"
#include "tfdraw/tfdraw.h"
#include "tfsfx/tfsfx.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#endif

#include "fs_core.inc"
#include "fs_geo.inc"
// The round panels (app_t.round = APP_ROUND_HI; the AMOLED-1.75C 466 x 466, the Watcher 412 x 412): a square frame
// of 310 x 310 scaled out to the circle by libs/rndout (3 px from 2 on the 1.75C). The painted band of the scene (180
// rows) goes through the middle of the circle; the caps above and below it get the edge rows of the band, darker to
// the edge of the circle (rnd_caps). The four seasons are keys in the top cap, BACK is on the left arc, the hint key
// on the right arc, the bag in the bottom cap.
#if BOARD_ROUND
#include "rndout/rndout.h"
static rndo_t s_ro;
static bool s_rnd;  // (the round frame: gfx_round() at the enter)
#define RND s_rnd
#else
#define RND 0
#endif
static int SEA_X0 = 4;  // the left edge of the season keys (a narrow frame; the round frame: in the middle)
// the caps of the round frame: the rows over and under the band repeat its edge rows, darker to the edge
static void rnd_caps(void)
{
    if (!RND) {
        return;
    }
    for (int y = 0; y < SH; y++) {
        if (y >= VY && y < VY + VH) {
            continue;
        }
        const int src = y < VY ? VY : VY + VH - 1, dist = y < VY ? VY - y : y - (VY + VH - 1),
                  k = 32 - dist * 32 / (VY + 8);
        const uint16_t *s = g_fb + (size_t)src * SW;
        uint16_t *d = g_fb + (size_t)y * SW;
        for (int x = 0; x < SW; x++) {
            d[x] = mix565(0, s[x], k < 0 ? 0 : k);
        }
    }
}

// ================================================================ the texts
static int s_lang = 1;  // 0 English, 1 Hebrew (the default)
#define T(en, he) (s_lang ? (he) : (en))
static const char *const SEASON_NAME[NSEA][2] = {
        {"SPRING", "אביב"}, {"SUMMER", "קיץ"}, {"AUTUMN", "סתיו"}, {"WINTER", "חורף"}};
static const char *const SCENE_NAME[SC_N][2] = {
        {"THE VALLEY", "העמק"},        {"THE FARM", "החווה"},   {"THE POND", "הבריכה"},
        {"THE BIG TREE", "העץ הגדול"}, {"THE SWING", "הנדנדה"}, {"THE VINES", "הכרם"},
        {"THE SHED", "המחסן"},         {"THE SLUICE", "הסכר"},  {"THE ISLAND", "האי"},
        {"THE ATTIC", "עליית הגג"},    {"THE WELL", "הבאר"},    {"THE HIVE", "הכוורת"},
        {"THE MAILBOX", "תיבת הדואר"}, {"THE GARDEN", "הגינה"},
};

// ================================================================ the state of the game
enum {
    IT_KEY,
    IT_POLE,
    IT_KITE,
    IT_PAPER,
    IT_KITE_OK,
    IT_SEEDS,
    IT_HANDLE,
    IT_FLOWER,
    IT_STRING,
    IT_RIBBON,
    IT_N
};
static const char *const ITEM_NAME[IT_N][2] = {
        {"AN OLD KEY", "מפתח ישן"},
        {"A LONG POLE", "מוט ארוך"},
        {"GRANDPA'S KITE", "העפיפון של סבא"},
        {"KITE PAPER", "נייר לעפיפון"},
        {"THE KITE, REPAIRED", "העפיפון, מתוקן"},
        {"SUNFLOWER SEEDS", "זרעי חמנייה"},
        {"A WELL HANDLE", "ידית של באר"},
        {"A SUNFLOWER HEAD", "ראש חמנייה"},
        {"KITE STRING", "חוט לעפיפון"},
        {"A RED RIBBON", "סרט אדום"},
};
enum {
    F_KEY = 1 << 0,
    F_SHED = 1 << 1,
    F_POLE = 1 << 2,
    F_KITE = 1 << 3,
    F_PAPER = 1 << 4,
    F_FIXED = 1 << 5,
    F_HANDLE = 1 << 6,
    F_SLUICE = 1 << 7,
    F_SEEDS = 1 << 8,
    F_PLANTED = 1 << 9,
    F_NEST = 1 << 10,
    F_WATERED = 1 << 11,
    F_BLOOM = 1 << 12,
    F_FLOWER = 1 << 13,
    F_FED = 1 << 14,
    F_STRING = 1 << 15,
    F_BRIDGE = 1 << 16,
    F_RIBBON = 1 << 17,
    F_FLOWN = 1 << 18,
    F_HANDLE_SEEN = 1 << 19,
};
static uint32_t s_flags, s_bag;
static int s_season;  // 0..3
static int s_scene = SC_VALLEY;
static int s_log;  // the pond log: 0 on the bank, 1 on the ice, 2 a bridge
static bool has(int it)
{
    return (s_bag >> it) & 1;
}
static void give(int it)
{
    s_bag |= 1u << it;
}
static void take_away(int it)
{
    s_bag &= ~(1u << it);
}

// the values of the things of a scene now (V_<SCENE>_<THING> of fs_geo.inc)
static void vals_of(int sc, uint8_t *v)
{
    memset(v, 0, FS_MAXVAR);
    switch (sc) {
    case SC_VALLEY:
        v[V_VALLEY_KITE] = (s_flags & F_KITE) ? 0 : 1;
        v[V_VALLEY_POLE] = (s_flags & F_KITE) ? 1 : 0;
        v[V_VALLEY_LOG] = (uint8_t)s_log;
        break;
    case SC_POND:
        v[V_POND_LOG] = (uint8_t)s_log;
        break;
    case SC_TREE:
        v[V_TREE_KITE] = (s_flags & F_KITE) ? 0 : 1;
        break;
    case SC_SWING:
        v[V_SWING_SQUIRREL] = (s_flags & F_STRING) ? 2 : (s_flags & F_FED) ? 1 : 0;
        v[V_SWING_STRING] = (s_flags & F_STRING) ? 0 : 1;
        break;
    case SC_VINES:
        v[V_VINES_KEY] = (s_flags & F_KEY) ? 0 : 1;
        break;
    case SC_SHED:
        v[V_SHED_DOOR] = (s_flags & F_SHED) ? 1 : 0;
        v[V_SHED_POLE] = (s_flags & F_POLE) ? 0 : 1;
        break;
    case SC_SHORE:
        v[V_SHORE_SLUICE] = (s_flags & F_SLUICE) ? 1 : 0;
        break;
    case SC_ISLAND:
        v[V_ISLAND_RIBBON] = (s_flags & F_RIBBON) ? 0 : 1;
        break;
    case SC_ATTIC:
        v[V_ATTIC_TRUNK] = (s_flags & F_PAPER) ? 1 : 0;
        break;
    case SC_WELL:
        v[V_WELL_HANDLE] = (s_flags & F_HANDLE) ? 0 : 1;
        break;
    case SC_HIVE:
        v[V_HIVE_NEST] = (s_flags & F_NEST) ? 0 : 1;
        break;
    case SC_MAILBOX:
        v[V_MAILBOX_SEEDS] = (s_flags & F_SEEDS) ? 0 : 1;
        break;
    case SC_GARDEN:
        v[V_GARDEN_PLANT] = (s_flags & F_FLOWER)    ? 4
                            : (s_flags & F_BLOOM)   ? 3
                            : (s_flags & F_WATERED) ? 2
                            : (s_flags & F_PLANTED) ? 1
                                                    : 0;
        break;
    default:
        break;
    }
}
// the parent of each scene (BACK) and the hot spot of the parent that leads to it
static int parent_of(int sc)
{
    switch (sc) {
    case SC_FARM:
    case SC_POND:
    case SC_TREE:
    case SC_SWING:
    case SC_VINES:
    case SC_SHED:
        return SC_VALLEY;
    case SC_SHORE:
    case SC_ISLAND:
        return SC_POND;
    case SC_ATTIC:
    case SC_WELL:
    case SC_HIVE:
    case SC_MAILBOX:
    case SC_GARDEN:
        return SC_FARM;
    default:
        return -1;
    }
}
static int door_to(int sc)
{
    switch (sc) {
    case SC_FARM:
        return HS_VALLEY_FARM;
    case SC_POND:
        return HS_VALLEY_POND;
    case SC_TREE:
        return HS_VALLEY_TREE;
    case SC_SWING:
        return HS_VALLEY_SWING;
    case SC_VINES:
        return HS_VALLEY_VINES;
    case SC_SHED:
        return HS_VALLEY_SHED;
    case SC_SHORE:
        return HS_POND_SHORE;
    case SC_ISLAND:
        return HS_POND_ISLAND;
    case SC_ATTIC:
        return HS_FARM_HOUSE;
    case SC_WELL:
        return HS_FARM_WELL;
    case SC_HIVE:
        return HS_FARM_HIVE;
    case SC_MAILBOX:
        return HS_FARM_MAILBOX;
    case SC_GARDEN:
        return HS_FARM_GARDEN;
    default:
        return -1;
    }
}

// ================================================================ the sound and the music
static bool s_snd = true, s_music = true;
enum {
    FX_CLICK,
    FX_ZOOM,
    FX_SEASON,
    FX_TAKE,
    FX_BAD,
    FX_DOOR,
    FX_SPLASH,
    FX_ICE,
    FX_BEES,
    FX_SQUEAK,
    FX_WOOD,
    FX_PAPER,
    FX_N
};
static const char *const FXN[FX_N] = {"click",  "zoom", "season", "take",   "bad",  "door",
                                      "splash", "ice",  "bees",   "squeak", "wood", "paper"};
static void fx(int id)
{
    if (s_snd) {
        tfsfx_play_var(id, id == FX_SEASON ? 55 : 75, 0.05f);
    }
}
static float s_jingle_left;
static int s_loop = -1;       // the loop that plays (FSM_*), -1 none
static float s_season_still;  // the time since the last change of the season
static const int SEASON_TRACK[NSEA] = {FSM_SPRING, FSM_SUMMER, FSM_AUTUMN, FSM_WINTER};
static void mus_loop(int tr)
{
    if (!s_music || tr == s_loop) {
        return;
    }
    midi_bg_play_track(tr, 0, true);
    s_loop = tr;
}
static void jingle(int tr, int ms)
{
    if (!s_music) {
        return;
    }
    midi_bg_play_track(tr, 0, false);
    s_jingle_left = ms / 1000.0f - 0.03f;
    s_loop = -1;
}

// ================================================================ the saves
static void save(void)
{
    store_set("fs_flags", s_flags);
    store_set("fs_bag", s_bag);
    store_set("fs_sea", (uint32_t)s_season);
    store_set("fs_scn", (uint32_t)s_scene);
    store_set("fs_log", (uint32_t)s_log);
}
static void load(void)
{
    s_flags = store_get("fs_flags", 0);
    s_bag = store_get("fs_bag", 0);
    s_season = (int)store_get("fs_sea", 2) & 3;
    s_scene = (int)store_get("fs_scn", SC_VALLEY);
    if (s_scene < 0 || s_scene >= SC_N) {
        s_scene = SC_VALLEY;
    }
    s_log = clampi((int)store_get("fs_log", 0), 0, 2);
}
static bool has_progress(void)
{
    return s_flags != 0 || s_bag != 0;
}

// ================================================================ the view: the scene sets, the moves, the pan
enum {
    ST_TITLE,
    ST_PLAY,
    ST_IN,
    ST_OUT,
    ST_END
};
static int s_state;
static float s_state_t;
static int s_cur;                           // the set that shows (0 / 1)
static int s_go_scene = -1, s_go_hot = -1;  // a move: the new scene, the hot spot of the zoom
static float s_ox, s_oy;                    // the pan of the picture (picture px)
static float s_tx0, s_ty0;
static int s_sea_from = -1;  // the blend of a change of the season
static float s_sea_t;
static char s_msg[96];
static float s_msg_t;
static int s_hint_lv;  // 0 = none, 1..3 = the level shown now
static float s_hint_t;
static float s_found_t;  // the card of a new item
static int s_found_it = -1;
static bool s_new_ask;  // NEW GAME on the title: a second tap starts over
static void tilt_zero(void)
{
    if (imu_ok()) {
        imu_tilt_logical(&s_tx0, &s_ty0);
    }
}
static void msg(const char *en, const char *he)
{
    snprintf(s_msg, sizeof s_msg, "%s", s_lang ? he : en);
    s_msg_t = 3.2f;
}
static sset_t *cur_set(void)
{
    return &s_set[s_cur];
}
static void pan_center(int sc)
{
    s_ox = (SCENE[sc].w - SW) * 0.5f;
    s_oy = (SCENE[sc].h - VH) * 0.5f;
}
// start a move into scene sc (the zoom toward the hot spot hs of this scene: -1 = the move out)
static void go_scene(int sc, int hs)
{
    if (s_state != ST_PLAY || s_ld.st == LS_READ || s_ld.st == LS_PARSE || s_ld.st == LS_SEASON) {
        return;
    }
    s_go_scene = sc;
    s_go_hot = hs;
    uint8_t v[FS_MAXVAR];
    vals_of(sc, v);
    // (the other set may hold the scene already: BACK to the parent)
    sset_t *O = &s_set[1 - s_cur];
    if (O->ready && O->id == sc) {
        set_revalue(O, v, s_season);
    } else {
        load_begin(1 - s_cur, sc, v);
    }
    s_state = hs >= 0 ? ST_IN : ST_OUT;
    s_state_t = 0;
    s_hint_lv = 0;
    fx(FX_ZOOM);
}
static void move_done(void)
{
    s_cur = 1 - s_cur;
    s_scene = s_go_scene;
    pan_center(s_scene);
    s_state = ST_PLAY;
    s_state_t = 0;
    save();
}

// ================================================================ the seasons
// the things that a new season changes (the log, the water, the bees)
static void season_effects(int from, int to)
{
    if (from == 3 && to == 0 && s_log == 1) {
        s_log = 2;
        s_flags |= F_BRIDGE;
        msg("THE THAW JAMMED THE LOG: A BRIDGE TO THE ISLAND!", "ההפשרה תקעה את הבול: גשר אל האי!");
        jingle(FSM_J_PART, FS_J_PART_MS);
    }
    if (from == 3 && to == 2 && s_log == 1) {
        s_log = 0;
        msg("THE LOG DRIFTED BACK TO THE BANK", "הבול נסחף בחזרה לגדה");
    }
    if (to == 0 && (s_flags & F_PLANTED) && (s_flags & F_SLUICE) && !(s_flags & F_WATERED)) {
        s_flags |= F_WATERED;
    }
    if (to == 1 && (s_flags & F_WATERED) && (s_flags & F_NEST) && !(s_flags & F_BLOOM)) {
        s_flags |= F_BLOOM;
    }
}
static void season_set(int to)
{
    to &= 3;
    if (to == s_season) {
        return;
    }
    int from = s_season;
    s_sea_from = from;
    s_sea_t = 0;
    s_season = to;
    season_effects(from, to);
    uint8_t v[FS_MAXVAR];
    vals_of(s_scene, v);
    set_revalue(cur_set(), v, -1);
    // (the blend shows the old season while the new values compose: compose both now)
    if (cur_set()->dirty) {
        set_compose(cur_set(), from, v);
        set_compose(cur_set(), to, v);
        cur_set()->dirty &= (uint8_t)~((1 << from) | (1 << to));
    }
    s_season_still = 0;
    fx(FX_SEASON);
    save();
}

// ================================================================ the acts: a tap on a hot spot
static void found(int it)
{
    give(it);
    s_found_it = it;
    s_found_t = 2.6f;
    fx(FX_TAKE);
    jingle(FSM_J_FOUND, FS_J_FOUND_MS);
}
static void refresh(void)
{
    uint8_t v[FS_MAXVAR];
    vals_of(s_scene, v);
    set_revalue(cur_set(), v, s_season);
    save();
}
static void kite_part_check(void)
{
    if (has(IT_KITE_OK) && has(IT_STRING) && has(IT_RIBBON)) {
        msg("THE KITE IS READY. NOW THE WIND: THE HILL, IN AUTUMN", "העפיפון מוכן. עכשיו הרוח: הגבעה, בסתיו");
    }
}
static void act(int hs)
{
    const int sea = s_season;
    const bool sp = sea == 0, su = sea == 1, au = sea == 2, wi = sea == 3;
    switch (hs) {
    // ---- the valley
    case HS_VALLEY_TREE:
    case HS_VALLEY_KITE:
        go_scene(SC_TREE, hs);
        return;
    case HS_VALLEY_SWING:
        go_scene(SC_SWING, hs);
        return;
    case HS_VALLEY_VINES:
        go_scene(SC_VINES, hs);
        return;
    case HS_VALLEY_SHED:
        go_scene(SC_SHED, hs);
        return;
    case HS_VALLEY_POND:
        go_scene(SC_POND, hs);
        return;
    case HS_VALLEY_FARM:
        go_scene(SC_FARM, hs);
        return;
    case HS_VALLEY_HILL:
        if (has(IT_KITE_OK) && has(IT_STRING) && has(IT_RIBBON)) {
            if (au) {
                s_flags |= F_FLOWN;
                save();
                s_state = ST_END;
                s_state_t = 0;
                jingle(FSM_J_END, FS_J_END_MS);
                return;
            }
            msg("NO WIND TODAY. THE AUTUMN WIND IS THE STRONG ONE", "אין רוח היום. רוח הסתיו היא החזקה");
        } else {
            msg("GRANDPA FLEW HIS KITE FROM THIS HILL", "סבא העיף את העפיפון שלו מהגבעה הזאת");
        }
        return;
    // ---- the farm and the pond
    case HS_FARM_HOUSE:
        go_scene(SC_ATTIC, hs);
        return;
    case HS_FARM_WELL:
        go_scene(SC_WELL, hs);
        return;
    case HS_FARM_HIVE:
        go_scene(SC_HIVE, hs);
        return;
    case HS_FARM_MAILBOX:
        go_scene(SC_MAILBOX, hs);
        return;
    case HS_FARM_GARDEN:
        go_scene(SC_GARDEN, hs);
        return;
    case HS_POND_SHORE:
        go_scene(SC_SHORE, hs);
        return;
    case HS_POND_ISLAND:
        if (s_log == 2 || wi) {
            go_scene(SC_ISLAND, hs);
            return;
        }
        fx(FX_BAD);
        msg("THE WATER IS TOO DEEP TO THE ISLAND", "המים עמוקים מדי עד האי");
        return;
    case HS_POND_LOG:
        if (s_log == 0 && wi) {
            s_log = 1;
            fx(FX_ICE);
            msg("YOU PUSH THE LOG ONTO THE ICE", "אתם דוחפים את הבול אל הקרח");
            refresh();
            return;
        }
        if (s_log == 0) {
            msg("A HEAVY LOG. IT WILL NOT FLOAT TO THE ISLAND", "בול כבד. הוא לא ישוט אל האי");
            return;
        }
        if (s_log == 1) {
            msg("THE LOG LIES ON THE ICE", "הבול שוכב על הקרח");
            return;
        }
        msg("A BRIDGE OF WOOD TO THE ISLAND", "גשר עץ אל האי");
        return;
    // ---- the tree
    case HS_TREE_KITE:
        if (s_flags & F_KITE) {
            msg("ONLY BRANCHES NOW", "רק ענפים עכשיו");
            return;
        }
        if (!wi) {
            msg(sp || su ? "THE LEAVES ARE THICK. SOMETHING IS IN THERE?"
                         : "SOMETHING RED IN THE LEAVES... THEY HIDE IT",
                sp || su ? "העלים צפופים. יש שם משהו?" : "משהו אדום בין העלים... הם מסתירים אותו");
            return;
        }
        if (!has(IT_POLE)) {
            fx(FX_BAD);
            msg("GRANDPA'S KITE! IT IS TOO HIGH: YOU NEED SOMETHING LONG", "העפיפון של סבא! גבוה מדי: צריך משהו ארוך");
            return;
        }
        take_away(IT_POLE);
        s_flags |= F_KITE;
        found(IT_KITE);
        refresh();
        msg("THE KITE IS TORN: IT NEEDS PAPER, STRING AND A TAIL", "העפיפון קרוע: הוא צריך נייר, חוט וזנב");
        return;
    case HS_TREE_TRUNK:
        msg("GRANDPA CUT HIS NAME IN THE BARK", "סבא חרט את שמו בקליפה");
        return;
    // ---- the swing
    case HS_SWING_SQUIRREL:
        if (s_flags & F_STRING) {
            msg("THE SQUIRREL IS BUSY WITH ITS SEEDS", "הסנאי עסוק בזרעים שלו");
            return;
        }
        if (wi) {
            msg("THE HOLLOW IS FULL OF SNOW. THE SQUIRREL SLEEPS", "החור מלא שלג. הסנאי ישן");
            return;
        }
        if (s_flags & F_FED) {
            s_flags |= F_STRING;
            fx(FX_SQUEAK);
            found(IT_STRING);
            refresh();
            kite_part_check();
            return;
        }
        if (has(IT_FLOWER)) {
            take_away(IT_FLOWER);
            s_flags |= F_FED;
            fx(FX_SQUEAK);
            msg("THE SQUIRREL TAKES THE SEEDS AND BRINGS SOMETHING", "הסנאי לוקח את הזרעים ומביא משהו");
            refresh();
            return;
        }
        fx(FX_SQUEAK);
        msg("A HUNGRY SQUIRREL. IT GUARDS A WHITE BALL IN ITS HOLLOW", "סנאי רעב. הוא שומר על כדור לבן בחור");
        return;
    case HS_SWING_SWING:
        fx(FX_WOOD);
        msg("WHEEE!", "ויייי!");
        return;
    // ---- the vines
    case HS_VINES_KEY:
        if (s_flags & F_KEY) {
            msg("ONLY A WIRE NOW", "רק חוט ברזל עכשיו");
            return;
        }
        if (au) {
            s_flags |= F_KEY;
            found(IT_KEY);
            refresh();
            return;
        }
        msg(wi ? "SNOW COVERS THE WIRES" : "THICK LEAVES. SOMETHING CLINKS IN THE WIND",
            wi ? "שלג מכסה את החוטים" : "עלים צפופים. משהו מקשקש ברוח");
        return;
    case HS_VINES_VINES:
        msg(au ? "THE GRAPES ARE IN. THE LEAVES FELL" : "GRANDPA'S OLD VINES",
            au ? "הענבים נבצרו. העלים נשרו" : "הגפנים הישנות של סבא");
        return;
    // ---- the shed
    case HS_SHED_DOOR:
        if (s_flags & F_SHED) {
            msg("THE SHED IS OPEN", "המחסן פתוח");
            return;
        }
        if (has(IT_KEY)) {
            take_away(IT_KEY);
            s_flags |= F_SHED;
            fx(FX_DOOR);
            msg("THE OLD KEY OPENS THE SHED", "המפתח הישן פותח את המחסן");
            refresh();
            return;
        }
        fx(FX_BAD);
        msg("LOCKED", "נעול");
        return;
    case HS_SHED_POLE:
        if (!(s_flags & F_SHED)) {
            fx(FX_BAD);
            msg("LOCKED", "נעול");
            return;
        }
        if (s_flags & F_POLE) {
            return;
        }
        s_flags |= F_POLE;
        found(IT_POLE);
        refresh();
        return;
    case HS_SHED_BENCH:
        if (!(s_flags & F_SHED)) {
            fx(FX_BAD);
            msg("LOCKED", "נעול");
            return;
        }
        if (s_flags & F_FIXED) {
            msg("GRANDPA'S WORKBENCH", "שולחן העבודה של סבא");
            return;
        }
        if (has(IT_KITE) && has(IT_PAPER)) {
            if (su) {
                take_away(IT_KITE);
                take_away(IT_PAPER);
                s_flags |= F_FIXED;
                fx(FX_PAPER);
                found(IT_KITE_OK);
                jingle(FSM_J_PART, FS_J_PART_MS);
                kite_part_check();
                return;
            }
            fx(FX_BAD);
            msg("THE GLUE IS HARD AS STONE. IT IS SOFT IN THE HEAT", "הדבק קשה כמו אבן. הוא רך בחום");
            return;
        }
        if (has(IT_KITE)) {
            msg("THE KITE NEEDS NEW PAPER", "העפיפון צריך נייר חדש");
            return;
        }
        msg(su ? "A POT OF GLUE, SOFT IN THE SUN" : "A POT OF OLD GLUE", su ? "סיר דבק, רך בשמש" : "סיר של דבק ישן");
        return;
    // ---- the sluice
    case HS_SHORE_SLUICE:
        if (s_flags & F_SLUICE) {
            msg("THE DITCH TAKES THE WATER TO THE GARDEN", "התעלה מובילה את המים לגינה");
            return;
        }
        if (!has(IT_HANDLE)) {
            msg("THE WHEEL OF THE SLUICE HAS NO HANDLE", "לגלגל של הסכר אין ידית");
            return;
        }
        if (su) {
            take_away(IT_HANDLE);
            s_flags |= F_SLUICE;
            fx(FX_WOOD);
            msg("THE SLUICE IS OPEN. THE SPRING WATER WILL GO TO THE GARDEN", "הסכר פתוח. מי האביב יזרמו לגינה");
            refresh();
            return;
        }
        fx(FX_BAD);
        msg(sp   ? "THE WATER IS TOO HIGH"
            : wi ? "THE WHEEL IS FROZEN"
                 : "THE MUD IS TOO DEEP",
            sp   ? "המים גבוהים מדי"
            : wi ? "הגלגל קפוא"
                 : "הבוץ עמוק מדי");
        return;
    // ---- the island
    case HS_ISLAND_RIBBON:
        if (s_flags & F_RIBBON) {
            msg("ONLY BARE BRANCHES", "רק ענפים חשופים");
            return;
        }
        if (au) {
            s_flags |= F_RIBBON;
            found(IT_RIBBON);
            refresh();
            kite_part_check();
            return;
        }
        msg(wi ? "SOMETHING RED, FROZEN IN THE ICE" : "THE LONG LEAVES HIDE SOMETHING RED",
            wi ? "משהו אדום, קפוא בקרח" : "העלים הארוכים מסתירים משהו אדום");
        return;
    case HS_ISLAND_WILLOW:
        msg("THE WILLOW OF THE ISLAND", "הערבה של האי");
        return;
    // ---- the attic
    case HS_ATTIC_TRUNK:
        if (s_flags & F_PAPER) {
            msg("AN EMPTY TRUNK", "ארגז ריק");
            return;
        }
        if (wi) {
            s_flags |= F_PAPER;
            fx(FX_DOOR);
            found(IT_PAPER);
            refresh();
            return;
        }
        msg("TOO DARK IN THAT CORNER", "חשוך מדי בפינה הזאת");
        return;
    case HS_ATTIC_WINDOW:
        msg(wi ? "THE LOW WINTER SUN COMES IN" : "THE SUN IS HIGH: ITS LIGHT STAYS ON THE FLOOR",
            wi ? "שמש החורף הנמוכה נכנסת" : "השמש גבוהה: האור שלה נשאר על הרצפה");
        return;
    // ---- the well
    case HS_WELL_HANDLE:
    case HS_WELL_WELL:
        if (s_flags & F_HANDLE) {
            msg("THE WELL IS QUIET", "הבאר שקטה");
            return;
        }
        if (sp) {
            s_flags |= F_HANDLE;
            fx(FX_SPLASH);
            found(IT_HANDLE);
            refresh();
            return;
        }
        if (su) {
            s_flags |= F_HANDLE_SEEN;
            msg("THE SUN SHOWS AN IRON HANDLE AT THE BOTTOM. TOO DEEP", "השמש מראה ידית ברזל בתחתית. עמוק מדי");
            return;
        }
        msg(wi ? "ICE ON THE WATER" : "DARK WATER", wi ? "קרח על המים" : "מים כהים");
        return;
    // ---- the hive
    case HS_HIVE_HIVE:
        if (s_flags & F_NEST) {
            fx(FX_BEES);
            msg("THE BEES ARE HAPPY NOW", "הדבורים שמחות עכשיו");
            return;
        }
        if (wi) {
            s_flags |= F_NEST;
            fx(FX_SQUEAK);
            msg("YOU PULL A MOUSE NEST OUT. THE MICE RUN TO THE BARN", "אתם מוציאים קן עכברים. העכברים בורחים לאסם");
            refresh();
            return;
        }
        fx(FX_BEES);
        msg(sp || su ? "ANGRY BEES! SOMETHING BOTHERS THEM INSIDE" : "FEW BEES. THEY ARE TIRED",
            sp || su ? "דבורים כועסות! משהו מפריע להן בפנים" : "מעט דבורים. הן עייפות");
        return;
    // ---- the mailbox
    case HS_MAILBOX_MAILBOX:
        if (s_flags & F_SEEDS) {
            msg("AN EMPTY MAILBOX", "תיבת דואר ריקה");
            return;
        }
        if (au) {
            s_flags |= F_SEEDS;
            found(IT_SEEDS);
            refresh();
            return;
        }
        msg(wi ? "THE DOOR IS FROZEN SHUT" : "A WREN HAS ITS NEST IN THE BOX",
            wi ? "הדלת קפואה" : "גדרון בונה קן בתיבה");
        return;
    // ---- the garden
    case HS_GARDEN_BED:
        if (s_flags & F_FLOWER) {
            msg("THE GARDEN RESTS", "הגינה נחה");
            return;
        }
        if (!(s_flags & F_PLANTED)) {
            if (!has(IT_SEEDS)) {
                msg("AN EMPTY BED", "ערוגה ריקה");
                return;
            }
            if (au) {
                take_away(IT_SEEDS);
                s_flags |= F_PLANTED;
                fx(FX_WOOD);
                msg("YOU PLANT THE SEEDS. THEY WILL WAIT FOR WATER", "אתם שותלים את הזרעים. הם יחכו למים");
                refresh();
                return;
            }
            msg("GRANDPA PLANTED IN AUTUMN", "סבא שתל בסתיו");
            return;
        }
        if (s_flags & F_BLOOM) {
            if (su) {
                s_flags |= F_FLOWER;
                found(IT_FLOWER);
                refresh();
                return;
            }
            msg("THE BIG SUNFLOWER IS A SUMMER FLOWER", "החמנייה הגדולה היא פרח של קיץ");
            return;
        }
        if (s_flags & F_WATERED) {
            msg((s_flags & F_NEST) ? "A SPROUT. IT WAITS FOR THE SUMMER" : "A SMALL FLOWER. NO BEES COME TO IT",
                (s_flags & F_NEST) ? "נבט. הוא מחכה לקיץ" : "פרח קטן. אף דבורה לא באה אליו");
            return;
        }
        msg("THE SEEDS WAIT FOR WATER", "הזרעים מחכים למים");
        return;
    default:
        break;
    }
}

// ================================================================ the hint: the next step, where, when, what
typedef struct {
    int scene, hot, season, item;
    const char *what[2];
} step_t;
static bool next_step(step_t *o)
{
    memset(o, 0, sizeof *o);
    o->season = -1;
    o->item = -1;
#define STEP(sc, hs, sea, it, en, he)                                                                                  \
    do {                                                                                                               \
        o->scene = sc;                                                                                                 \
        o->hot = hs;                                                                                                   \
        o->season = sea;                                                                                               \
        o->item = it;                                                                                                  \
        o->what[0] = en;                                                                                               \
        o->what[1] = he;                                                                                               \
        return true;                                                                                                   \
    } while (0)
    uint32_t f = s_flags;
    if (!(f & F_FIXED)) {
        if (!(f & F_KEY)) {
            STEP(SC_VINES, HS_VINES_KEY, 2, IT_KEY, "A KEY IN THE VINES", "מפתח בכרם");
        }
        if (!(f & F_SHED)) {
            STEP(SC_SHED, HS_SHED_DOOR, -1, IT_KEY, "THE KEY OPENS THE SHED", "המפתח פותח את המחסן");
        }
        if (!(f & F_POLE)) {
            STEP(SC_SHED, HS_SHED_POLE, -1, IT_POLE, "A LONG POLE", "מוט ארוך");
        }
        if (!(f & F_KITE)) {
            STEP(SC_TREE, HS_TREE_KITE, 3, IT_KITE, "THE KITE IN THE BARE TREE", "העפיפון בעץ החשוף");
        }
        if (!(f & F_PAPER)) {
            STEP(SC_ATTIC, HS_ATTIC_TRUNK, 3, IT_PAPER, "THE TRUNK IN THE WINTER BEAM", "הארגז בקרן של החורף");
        }
        STEP(SC_SHED, HS_SHED_BENCH, 1, IT_KITE_OK, "SOFT GLUE ON THE BENCH", "דבק רך על השולחן");
    }
    if (!(f & F_STRING)) {
        if (!(f & F_FLOWER)) {
            if (!(f & F_HANDLE)) {
                STEP(SC_WELL, HS_WELL_HANDLE, 0, IT_HANDLE, "THE HANDLE FLOATS UP", "הידית צפה למעלה");
            }
            if (!(f & F_SLUICE)) {
                STEP(SC_SHORE, HS_SHORE_SLUICE, 1, IT_HANDLE, "THE HANDLE ON THE SLUICE WHEEL", "הידית על גלגל הסכר");
            }
            if (!(f & F_SEEDS)) {
                STEP(SC_MAILBOX, HS_MAILBOX_MAILBOX, 2, IT_SEEDS, "SEEDS IN THE MAILBOX", "זרעים בתיבת הדואר");
            }
            if (!(f & F_PLANTED)) {
                STEP(SC_GARDEN, HS_GARDEN_BED, 2, IT_SEEDS, "PLANT THE SEEDS", "שתלו את הזרעים");
            }
            if (!(f & F_NEST)) {
                STEP(SC_HIVE, HS_HIVE_HIVE, 3, -1, "A MOUSE NEST IN THE HIVE", "קן עכברים בכוורת");
            }
            if (!(f & F_WATERED)) {
                STEP(SC_GARDEN, HS_GARDEN_BED, 0, -1, "THE SPRING WATER", "מי האביב");
            }
            STEP(SC_GARDEN, HS_GARDEN_BED, 1, IT_FLOWER, "THE SUNFLOWER", "החמנייה");
        }
        STEP(SC_SWING, HS_SWING_SQUIRREL, (f & F_FED) ? -1 : 1, IT_STRING, "THE HUNGRY SQUIRREL", "הסנאי הרעב");
    }
    if (!(f & F_RIBBON)) {
        if (s_log == 0) {
            STEP(SC_POND, HS_POND_LOG, 3, -1, "THE LOG ONTO THE ICE", "הבול אל הקרח");
        }
        if (s_log == 1) {
            STEP(SC_POND, HS_POND_LOG, 0, -1, "THE THAW", "ההפשרה");
        }
        STEP(SC_ISLAND, HS_ISLAND_RIBBON, 2, IT_RIBBON, "A RED RIBBON ON THE WILLOW", "סרט אדום על הערבה");
    }
    if (!(f & F_FLOWN)) {
        STEP(SC_VALLEY, HS_VALLEY_HILL, 2, IT_KITE_OK, "FLY THE KITE", "העיפו את העפיפון");
    }
#undef STEP
    return false;
}
// the hot spot of this scene on the way to the step (-1: BACK)
static int way_hot(int target_scene, int target_hot)
{
    if (target_scene == s_scene) {
        return target_hot;
    }
    // up the tree of the target: is the scene now one of its parents?
    for (int sc = target_scene; sc >= 0; sc = parent_of(sc)) {
        if (parent_of(sc) == s_scene) {
            return door_to(sc);
        }
    }
    return -1;
}

// ================================================================ the drawing over the scene
static void item_icon(int it, float cx, float cy, float s)
{
    float u = s / 32.0f;
    switch (it) {
    case IT_KEY:
        tfd_ring(cx - 7 * u, cy, 5 * u, 2.4f * u, tfd_rgb(120, 110, 100), 256);
        tfd_rect((int)(cx - 2 * u), (int)(cy - 1.2f * u), (int)(14 * u), (int)(2.6f * u), tfd_rgb(120, 110, 100), 256);
        tfd_rect((int)(cx + 8 * u), (int)cy, (int)(2.4f * u), (int)(5 * u), tfd_rgb(120, 110, 100), 256);
        break;
    case IT_POLE:
        tfd_line(cx - 12 * u, cy + 10 * u, cx + 10 * u, cy - 10 * u, 2.6f * u, tfd_rgb(150, 110, 60), 256);
        tfd_arc(cx + 8 * u, cy - 13 * u, 4 * u, 2 * u, 0, 3.4f, tfd_rgb(110, 110, 120), 256);
        break;
    case IT_KITE:
    case IT_KITE_OK: {
        float q[8] = {cx, cy - 13 * u, cx + 9 * u, cy - 2 * u, cx, cy + 13 * u, cx - 9 * u, cy - 2 * u};
        tfd_poly(q, 4, it == IT_KITE_OK ? tfd_rgb(230, 60, 50) : tfd_rgb(170, 90, 80), 256);
        float h[6] = {cx, cy - 13 * u, cx + 9 * u, cy - 2 * u, cx, cy - 2 * u};
        tfd_poly(h, 3, it == IT_KITE_OK ? tfd_rgb(255, 210, 60) : tfd_rgb(180, 160, 110), 256);
        tfd_line(cx, cy - 13 * u, cx, cy + 13 * u, 1, tfd_rgb(90, 60, 40), 256);
        tfd_line(cx - 9 * u, cy - 2 * u, cx + 9 * u, cy - 2 * u, 1, tfd_rgb(90, 60, 40), 256);
        if (it == IT_KITE) {
            tfd_line(cx - 4 * u, cy + 2 * u, cx + 2 * u, cy + 7 * u, 1.6f, tfd_rgb(40, 30, 30), 256);
        }
        break;
    }
    case IT_PAPER:
        tfd_rect((int)(cx - 9 * u), (int)(cy - 11 * u), (int)(18 * u), (int)(22 * u), tfd_rgb(240, 225, 190), 256);
        for (int k = 0; k < 4; k++) {
            tfd_rect((int)(cx - 6 * u), (int)(cy - 6 * u + k * 4 * u), (int)(12 * u), 1, tfd_rgb(170, 150, 120), 256);
        }
        break;
    case IT_SEEDS:
        tfd_rrect((int)(cx - 8 * u), (int)(cy - 11 * u), (int)(16 * u), (int)(22 * u), 2, tfd_rgb(220, 190, 120), 256);
        tfd_disc(cx, cy - 2 * u, 5 * u, tfd_rgb(250, 200, 40), 256);
        tfd_disc(cx, cy - 2 * u, 2.4f * u, tfd_rgb(110, 70, 30), 256);
        break;
    case IT_HANDLE:
        tfd_line(cx - 10 * u, cy + 6 * u, cx + 4 * u, cy - 6 * u, 3 * u, tfd_rgb(100, 100, 110), 256);
        tfd_line(cx + 4 * u, cy - 6 * u, cx + 10 * u, cy - 2 * u, 3 * u, tfd_rgb(100, 100, 110), 256);
        tfd_rect((int)(cx + 8 * u), (int)(cy - 4 * u), (int)(3 * u), (int)(8 * u), tfd_rgb(150, 110, 60), 256);
        break;
    case IT_FLOWER:
        for (int k = 0; k < 12; k++) {
            float a = k * 0.5236f;
            tfd_disc(cx + cosf(a) * 9 * u, cy + sinf(a) * 9 * u, 4 * u, tfd_rgb(255, 205, 40), 256);
        }
        tfd_disc(cx, cy, 7 * u, tfd_rgb(110, 70, 30), 256);
        break;
    case IT_STRING:
        tfd_disc(cx, cy, 9 * u, tfd_rgb(240, 240, 235), 256);
        for (int k = -1; k <= 1; k++) {
            tfd_line(cx - 8 * u, cy + k * 4 * u, cx + 8 * u, cy + k * 4 * u + 3 * u, 1, tfd_rgb(180, 180, 175), 256);
        }
        tfd_line(cx + 8 * u, cy + 3 * u, cx + 14 * u, cy + 10 * u, 1, tfd_rgb(220, 220, 215), 256);
        break;
    case IT_RIBBON:
        for (int k = 0; k < 6; k++) {
            tfd_line(cx - 12 * u + k * 4 * u, cy + sinf(k * 1.3f) * 5 * u, cx - 8 * u + k * 4 * u,
                     cy + sinf((k + 1) * 1.3f) * 5 * u, 3 * u, tfd_rgb(220, 40, 50), 256);
        }
        break;
    default:
        break;
    }
}
static void season_glyph(int s, float cx, float cy, float r, int a)
{
    switch (s) {
    case 0:  // a blossom
        for (int k = 0; k < 5; k++) {
            float an = k * 1.2566f - 1.57f;
            tfd_disc(cx + cosf(an) * r * 0.5f, cy + sinf(an) * r * 0.5f, r * 0.42f, tfd_rgb(255, 170, 200), a);
        }
        tfd_disc(cx, cy, r * 0.28f, tfd_rgb(255, 230, 120), a);
        break;
    case 1:  // the sun
        for (int k = 0; k < 8; k++) {
            float an = k * 0.785f;
            tfd_line(cx + cosf(an) * r * 0.62f, cy + sinf(an) * r * 0.62f, cx + cosf(an) * r, cy + sinf(an) * r, 1.6f,
                     tfd_rgb(255, 200, 60), a);
        }
        tfd_disc(cx, cy, r * 0.48f, tfd_rgb(255, 210, 70), a);
        break;
    case 2: {  // a leaf
        float q[8] = {cx, cy - r, cx + r * 0.7f, cy, cx, cy + r, cx - r * 0.7f, cy};
        tfd_poly(q, 4, tfd_rgb(230, 110, 40), a);
        tfd_line(cx, cy - r * 0.8f, cx, cy + r, 1, tfd_rgb(140, 60, 20), a);
        break;
    }
    default:  // a snowflake
        for (int k = 0; k < 3; k++) {
            float an = k * 1.047f;
            tfd_line(cx - cosf(an) * r, cy - sinf(an) * r, cx + cosf(an) * r, cy + sinf(an) * r, 1.6f,
                     tfd_rgb(220, 240, 255), a);
        }
        tfd_disc(cx, cy, r * 0.2f, tfd_rgb(240, 250, 255), a);
        break;
    }
}
static void text_box(int cx, int base, const char *s, uint16_t col)
{
    // (the 17 px font when it fits in the free width; else the 13 px font)
    const int lim = RND ? SW - 50 : NARROW ? SW - 12 : SW - 230;
    int wt = ui_text_w(&KF_TEXT, s) + 18;
    if (wt <= lim) {
        tfd_rrect(cx - wt / 2, base - 17, wt, 24, 6, tfd_rgb(20, 18, 24), 190);
        ui_text_c(&KF_TEXT, cx, base + 1, s, col);
        return;
    }
    int w = ui_text_w(&KF_LABEL, s) + 18;
    if (w > lim) {
        w = lim;
    }
    tfd_rrect(cx - w / 2, base - 14, w, 20, 6, tfd_rgb(20, 18, 24), 190);
    ui_text_c(&KF_LABEL, cx, base, s, col);
}
// the buttons: BACK (bottom left), the hint ? (top right)
// (a narrow frame: BACK in the bottom bar, the hint at the right of the top bar; enter() sets them)
static int BK_X = 6, BK_Y = 126, BK_W = 96, BK_H = 40, HN_X = 588, HN_Y = 4, HN_W = 46, HN_H = 44;
#define SEA_CW 44  // a season button in the top bar of a narrow frame
static void ui_draw(void)
{
    if (NARROW) {
        // (the top bar: the four seasons as buttons, the name of this one)
        for (int s = 0; s < NSEA; s++) {
            const int y0 = RND ? 28 : 2, y1 = RND ? VY - 2 : VY - 2;  // (the round frame: inside the top cap)
            if (s == s_season) {
                tfd_rrect(SEA_X0 + s * SEA_CW, y0, SEA_CW - 4, y1 - y0, 7, tfd_rgb(255, 230, 150), 90);
            }
            season_glyph(s, SEA_X0 + s * SEA_CW + (SEA_CW - 4) / 2, (y0 + y1) / 2, 9, s == s_season ? 256 : 150);
        }
        if (!RND) {  // (the round frame: no room for the name; the panel and the glyph show the season)
            ui_text_c(&KF_LABEL, 4 + NSEA * SEA_CW + 50, 21, SEASON_NAME[s_season][s_lang], tfd_rgb(255, 250, 235));
        }
    } else {
        // the season (top left): the glyph and the name
        tfd_rrect(6, 6, 104, 30, 8, tfd_rgb(20, 18, 24), 150);
        season_glyph(s_season, 22, 21, 10, 256);
        ui_text_c(&KF_LABEL, 70, 26, SEASON_NAME[s_season][s_lang], tfd_rgb(255, 250, 235));
    }
    // the hint button
    tfd_rrect(HN_X, HN_Y, HN_W, HN_H, 10, tfd_rgb(20, 18, 24), 160);
    ui_text_c(&KF_TEXT, HN_X + HN_W / 2, HN_Y + HN_H / 2 + 8, "?", tfd_rgb(255, 230, 150));
    // BACK
    if (parent_of(s_scene) >= 0) {
        tfd_rrect(BK_X, BK_Y, BK_W, BK_H, 10, tfd_rgb(20, 18, 24), 170);
        ui_text_c(NARROW ? &KF_LABEL : &KF_TEXT, BK_X + BK_W / 2, BK_Y + BK_H / 2 + (NARROW ? 5 : 7), T("BACK", "חזרה"),
                  tfd_rgb(255, 250, 235));
    }
    // the bag (bottom right): the items that are in it now
    int n = 0;
    for (int it = 0; it < IT_N; it++) {
        n += has(it);
    }
    if (n) {
        const int sp = NARROW ? 28 : 34, bh = NARROW ? 28 : 36;  // (a narrow frame: smaller, in the bottom bar)
        int w = n * sp + 8, x = RND ? SW / 2 - w / 2 : SW - 6 - w, by = RND ? VY + VH + 4 : SH - bh - (NARROW ? 1 : 6);
        tfd_rrect(x, by, w, bh, 9, tfd_rgb(20, 18, 24), 150);  // (the round frame: the middle of the bottom cap)
        int k = 0;
        for (int it = 0; it < IT_N; it++) {
            if (has(it)) {
                item_icon(it, x + sp / 2 + 4 + k * sp, by + bh - (NARROW ? 14 : 18), NARROW ? 22 : 26);
                k++;
            }
        }
    }
    // a message (bottom middle)
    if (s_msg_t > 0 && s_msg[0]) {
        int a = (int)(256 * clampf(s_msg_t * 3, 0, 1));
        // (above the row of the bag and BACK)
        // (the clues are body text: the 17 px font in one line; the 13 px font in two lines only when one line of
        // 17 px does not fit: a plate of two big lines would touch the card of a new item or the bag)
        int w = ui_text_w(&KF_LABEL, s_msg) + 22, my = NARROW ? VY + VH - 30 : 96;
        int wt = ui_text_w(&KF_TEXT, s_msg) + 22;
        const int mw = RND ? SW - 60 : SW - 12;  // (the round frame: the width of the circle at the band)
        if (wt <= mw) {
            tfd_rrect(SW / 2 - wt / 2, my - 2, wt, 28, 8, tfd_rgb(20, 18, 24), a * 200 / 256);
            ui_text_c(&KF_TEXT, SW / 2, my + 18, s_msg, tfd_rgb(255, 250, 235));
        } else if (w > mw) {
            // (a narrow frame: two lines, cut at the space nearest to the middle)
            char l1[96], l2[96];
            int n = (int)strlen(s_msg), cut = -1;
            for (int i = 1; i < n; i++) {
                if (s_msg[i] == ' ' && (cut < 0 || abs(i - n / 2) < abs(cut - n / 2))) {
                    cut = i;
                }
            }
            if (cut < 0) {
                cut = n / 2;
            }
            snprintf(l1, sizeof l1, "%.*s", cut, s_msg);
            snprintf(l2, sizeof l2, "%s", s_msg + cut + (s_msg[cut] == ' '));
            int w1 = ui_text_w(&KF_LABEL, l1), w2 = ui_text_w(&KF_LABEL, l2);
            w = (w1 > w2 ? w1 : w2) + 22;
            if (w > mw + 8) {
                w = mw + 8;
            }
            my -= 18;
            tfd_rrect(SW / 2 - w / 2, my, w, 44, 8, tfd_rgb(20, 18, 24), a * 200 / 256);
            ui_text_c(&KF_LABEL, SW / 2, my + 18, l1, tfd_rgb(255, 250, 235));
            ui_text_c(&KF_LABEL, SW / 2, my + 36, l2, tfd_rgb(255, 250, 235));
        } else {
            tfd_rrect(SW / 2 - w / 2, my, w, 26, 8, tfd_rgb(20, 18, 24), a * 200 / 256);
            ui_text_c(&KF_LABEL, SW / 2, my + 18, s_msg, tfd_rgb(255, 250, 235));
        }
    }
    // the hint
    if (s_hint_lv > 0 && s_hint_t > 0) {
        step_t st;
        if (next_step(&st)) {
            int hs = way_hot(st.scene, st.hot);
            float p = 0.6f + 0.4f * sinf(s_hint_t * 6);
            if (hs >= 0) {
                const hot_t *H = &HOT[hs];
                float cx = H->x + H->w * 0.5f - s_ox, cy = H->y + H->h * 0.5f - s_oy + VY,
                      r = fmaxf(H->w, H->h) * 0.6f + 6;
                tfd_ring(cx, cy, r, 3, tfd_rgb(255, 230, 120), (int)(220 * p));
            } else if (parent_of(s_scene) >= 0) {
                tfd_ring(BK_X + BK_W / 2, BK_Y + BK_H / 2, 34, 3, tfd_rgb(255, 230, 120), (int)(220 * p));
            }
            char b[96];
            if (s_hint_lv == 1) {
                snprintf(b, sizeof b, "%s", SCENE_NAME[st.scene][s_lang]);
            } else if (s_hint_lv == 2) {
                snprintf(b, sizeof b, "%s  -  %s", SCENE_NAME[st.scene][s_lang],
                         st.season >= 0 ? SEASON_NAME[st.season][s_lang] : T("ANY SEASON", "בכל עונה"));
            } else {
                snprintf(b, sizeof b, "%s", st.what[s_lang]);
            }
            text_box(SW / 2, VY + 44, b, tfd_rgb(255, 230, 150));
            if (s_hint_lv >= 2 && st.season >= 0 && !NARROW) {
                int tw = ui_text_w(&KF_TEXT, b);
                if (tw + 18 > SW - 230) {
                    tw = ui_text_w(&KF_LABEL, b);  // (the font of text_box)
                }
                season_glyph(st.season, SW / 2 + tw / 2 + 22, 38, 8, 256);
            }
        }
    }
    // a new item: its card
    if (s_found_t > 0 && s_found_it >= 0) {
        float k = clampf(s_found_t * 2, 0, 1) * clampf((2.6f - s_found_t) * 5, 0, 1);
        int a = (int)(256 * k);
        // (over the message plate of y 96: the card ends at y 90)
        tfd_rrect(SW / 2 - 130, VY + 28, 260, 60, 12, tfd_rgb(250, 244, 228), a * 240 / 256);
        item_icon(s_found_it, SW / 2 - 92, VY + 58, 40);
        ui_text_c(&KF_TEXT, SW / 2 + 26, VY + 65, ITEM_NAME[s_found_it][s_lang], tfd_rgb(60, 40, 30));
    }
}

// ================================================================ the scene in the frame
static int blend_k(void)
{
    return s_sea_from >= 0 ? clampi((int)(s_sea_t / 0.8f * 32), 0, 32) : 32;
}
static void draw_scene_plain(const sset_t *S, int ox, int oy)
{
    if (s_sea_from >= 0 && blend_k() < 32) {
        draw_blend(S, s_sea_from, s_season, blend_k(), ox, oy);
    } else {
        draw_plain(S, s_season, ox, oy);
    }
}
// the end: the kite flies from the hill; then the valley in the four seasons at once
static void end_draw(void)
{
    sset_t *S = cur_set();
    float t = s_state_t;
    if (t < 9) {
        draw_plain(S, 2, (int)s_ox, (int)s_oy);
        // the kite rises in the wind (from the hill to the sky); the string from the hill
        float hx = HOT[HS_VALLEY_HILL].x + HOT[HS_VALLEY_HILL].w * 0.5f - s_ox,
              hy = HOT[HS_VALLEY_HILL].y + 24 - s_oy + VY;
        float f = smooth01(t / 6), kx = hx + 40 + f * 170 + sinf(t * 1.7f) * 10,
              ky = hy - 20 - f * 70 + sinf(t * 2.3f) * 6;
        for (int k = 0; k < 16; k++) {
            float a = k / 16.0f, b = (k + 1) / 16.0f, sag = 18 * (1 - f * 0.5f);
            tfd_line(hx + (kx - hx) * a, hy + (ky - hy) * a + sinf(a * 3.14159f) * sag, hx + (kx - hx) * b,
                     hy + (ky - hy) * b + sinf(b * 3.14159f) * sag, 1, tfd_rgb(250, 250, 245), 220);
        }
        for (int k = 0; k < 7; k++) {
            float rx = kx - 4 - k * 6, ry = ky + 18 + k * 4 + sinf(t * 6 + k) * 3;
            tfd_line(rx, ry, rx - 6, ry + 4 + sinf(t * 6 + k + 1) * 3, 3, tfd_rgb(220, 40, 50), 256);
        }
        float tilt = sinf(t * 1.9f) * 0.25f, ca = cosf(tilt), sa = sinf(tilt);
        float P[4][2] = {{0, -18}, {13, -3}, {0, 18}, {-13, -3}}, q[8];
        for (int i = 0; i < 4; i++) {
            q[2 * i] = kx + P[i][0] * ca - P[i][1] * sa;
            q[2 * i + 1] = ky + P[i][0] * sa + P[i][1] * ca;
        }
        tfd_poly(q, 4, tfd_rgb(230, 60, 50), 256);
        float h[6] = {q[0], q[1], q[2], q[3], kx, ky};
        tfd_poly(h, 3, tfd_rgb(255, 210, 60), 256);
        tfd_line(q[0], q[1], q[4], q[5], 1, tfd_rgb(90, 60, 40), 256);
        tfd_line(q[2], q[3], q[6], q[7], 1, tfd_rgb(90, 60, 40), 256);
        rnd_caps();
        if (t > 2) {
            text_box(SW / 2,
                     RND      ? VY - 8
                     : NARROW ? 20
                              : 36,
                     T("IT FLIES, GRANDPA!", "הוא עף, סבא!"), tfd_rgb(255, 240, 200));
        }
    } else {
        // the four seasons at once: four bands that slide
        float sh = fmodf((t - 9) * 30, (float)SW);
        for (int s = 0; s < NSEA; s++) {
            int x0 = (int)(s * SW / 4 + sh) % SW, w = SW / 4;
            for (int y = 0; y < VH; y++) {
                const uint8_t *src = S->pic[s] + (size_t)(y + (int)s_oy) * S->w + (int)s_ox;
                uint16_t *d = g_fb + (VY + y) * SW;
                for (int x = 0; x < w; x++) {
                    int fx = (x0 + x) % SW;
                    d[fx] = S->pal[s][src[fx]];
                }
            }
        }
        rnd_caps();
        text_box(SW / 2, RND ? VY - 8 : NARROW ? 20 : 40, T("FOUR SEASONS", "ארבע עונות"), tfd_rgb(255, 240, 200));
        if (t > 12) {
            text_box(SW / 2,
                     RND      ? VY + VH - 8
                     : NARROW ? SH - 8
                              : 160,
                     T("THANK YOU FOR PLAYING. TAP FOR THE TITLE", "תודה ששיחקתם. געו למסך הראשי"),
                     tfd_rgb(230, 230, 220));
        }
    }
}

// ================================================================ the title
static float s_title_sea;  // the season of the title moves by itself
static void title_draw(void)
{
    sset_t *S = cur_set();
    int a = (int)s_title_sea % NSEA, b = (a + 1) % NSEA;
    float f = s_title_sea - floorf(s_title_sea);
    int k = f < 0.75f ? 0 : (int)((f - 0.75f) / 0.25f * 32);
    draw_blend(S, a, b, k, (int)s_ox, (int)s_oy);
    // (a narrow frame: the plate narrower; PLAY and NEW GAME under each other, in the middle)
    const int pw = RND      ? 240
                   : NARROW ? SW - 20
                            : 340,
              py = RND      ? 156
                   : NARROW ? 150
                            : 116,
              ny = RND      ? 206
                   : NARROW ? 198
                            : 120,
              nx = NARROW ? SW / 2 - 80 : SW - 170;
#if BOARD_UI_TABLET
    // (the Tab5: t5_show() puts the top bar (the rows 0-29) over the band 2 times as big: the plate and its words go
    // under the bar, in the band, else the words came at the top edge and the plate under them)
    const int ty = 14;
#else
    const int ty = RND ? 44 : 0;  // (the round frame: the plate under the top of the circle)
#endif
    rnd_caps();
    tfd_rrect(SW / 2 - pw / 2, 18 + ty, pw, 64, 14, tfd_rgb(250, 244, 228), 225);
    ui_text_c(ui_text_w(&KF_TITLE, T("FOUR SEASONS", "ארבע עונות")) <= pw - 16 ? &KF_TITLE : &KF_BIG, SW / 2, 64 + ty,
              T("FOUR SEASONS", "ארבע עונות"), tfd_rgb(70, 50, 40));
    for (int s = 0; s < NSEA; s++) {
        season_glyph(s, SW / 2 - 60 + s * 40, 98 + (NARROW && !RND ? 10 : 0) + ty, 9, 256);
    }
    // the buttons: PLAY / CONTINUE, NEW GAME (a second tap)
    bool cont = has_progress();
    tfd_rrect(SW / 2 - 110, py, 220, 44, 12, tfd_rgb(60, 130, 70), 240);
    ui_text_c(&KF_TEXT, SW / 2, py + 29, cont ? T("CONTINUE", "המשך") : T("PLAY", "משחק"), tfd_rgb(255, 255, 255));
    if (cont) {
        tfd_rrect(nx, ny, 160, 40, 10, s_new_ask ? tfd_rgb(190, 70, 50) : tfd_rgb(40, 36, 44), 220);
        ui_text_c(&KF_LABEL, nx + 80, ny + 25, s_new_ask ? T("TAP AGAIN", "געו שוב") : T("NEW GAME", "משחק חדש"),
                  tfd_rgb(255, 250, 235));
    }
}

// ================================================================ the app
static bool s_ok;
static int64_t s_last;
static void title_go(void);
static void play_start(bool fresh)
{
    if (fresh) {
        s_flags = 0;
        s_bag = 0;
        s_season = 2;
        s_scene = SC_VALLEY;
        s_log = 0;
        save();
    }
    uint8_t v[FS_MAXVAR];
    vals_of(s_scene, v);
    // (the scene of the title is the valley: the same set when the play starts there)
    if (cur_set()->ready && cur_set()->id == s_scene) {
        set_revalue(cur_set(), v, s_season);
    } else {
        load_begin(s_cur, s_scene, v);
        while (!load_step(1000000)) {
        }
    }
    pan_center(s_scene);
    s_sea_from = -1;
    s_state = ST_PLAY;
    s_state_t = 0;
    s_msg_t = 0;
    s_hint_lv = 0;
    tilt_zero();
    if (fresh) {
        msg("FIND GRANDPA'S KITE. A DRAG CHANGES THE SEASON", "מצאו את העפיפון של סבא. גרירה מחליפה עונה");
    }
    s_loop = -1;
    s_season_still = 2;
}
static void title_go(void)
{
    uint8_t v[FS_MAXVAR];
    int keep_sc = s_scene;
    s_scene = SC_VALLEY;
    vals_of(SC_VALLEY, v);
    s_scene = keep_sc;
    if (!(cur_set()->ready && cur_set()->id == SC_VALLEY)) {
        load_begin(s_cur, SC_VALLEY, v);
        while (!load_step(1000000)) {
        }
    } else {
        set_revalue(cur_set(), v, -1);
    }
    pan_center(SC_VALLEY);
    s_title_sea = (float)s_season;
    s_new_ask = false;
    s_state = ST_TITLE;
    s_state_t = 0;
    s_loop = -1;
    mus_loop(FSM_TITLE);
}

enum {
    PI_SEA,
    PI_LANG,
    PI_MUS,
    PI_SND,
    PI_HINT,
    PI_NEW,
    PI_TITLE
};
static const char *const SEA_V[] = {"SPRING", "אביב", "SUMMER", "קיץ", "AUTUMN", "סתיו", "WINTER", "חורף"};
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const pitem_t ITEMS[] = {
        {PI_PICK, PI_SEA, 4, 0, {"SEASON", "עונה"}, SEA_V, NULL},
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "fs_lang"},
        {PI_PICK, PI_MUS, 2, 0, {"MUSIC", "מוזיקה"}, ONOFF_V, "fs_mus"},
        {PI_PICK, PI_SND, 2, 0, {"SOUND", "צלילים"}, ONOFF_V, "fs_snd"},
        {PI_ACT, PI_HINT, 0, 0, {"HINT", "רמז"}, NULL, NULL},
        {PI_ACT2, PI_NEW, 0, 0, {"NEW GAME", "משחק חדש"}, NULL, NULL},
        {PI_ACT, PI_TITLE, 0, 0, {"THE TITLE", "המסך הראשי"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == PI_SEA || id == PI_HINT || id == PI_TITLE) {
        return s_state == ST_PLAY;
    }
    if (id == PI_NEW) {
        return s_state == ST_PLAY || s_state == ST_TITLE;
    }
    return true;
}
static int item_get(int id)
{
    return id == PI_SEA ? s_season : 0;
}
static void hint_next(void)
{
    if (s_state != ST_PLAY) {
        return;
    }
    s_hint_lv = s_hint_lv >= 3 ? 1 : s_hint_lv + 1;
    s_hint_t = 6;
    fx(FX_CLICK);
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_SEA:
        if (s_state == ST_PLAY) {
            season_set(v);
        }
        break;
    case PI_LANG:
        s_lang = v & 1;
        break;
    case PI_MUS:
        s_music = v == 0;
        if (!s_music) {
            midi_bg_stop(300);
        } else {
            midi_bg_start();
            s_loop = -1;
        }
        break;
    case PI_SND:
        s_snd = v == 0;
        break;
    case PI_HINT:
        hint_next();
        break;
    case PI_NEW:
        play_start(true);
        break;
    case PI_TITLE:
        title_go();
        break;
    }
}
static void item_resume(void)
{
    tilt_zero();
}
static const pitems_t PANEL = {ITEMS, 7, item_shown, item_get, item_set, NULL, item_resume};

#if BOARD_UI_TABLET
// ---- the Tab5 (640 x 360): the app draws the CoreS3 layout (320 x 240: a bar of 30 rows, the band of the scene of 180
// rows, a bar of 30 rows) into its own frame. t5_show() puts the band 2 times as big on the whole screen, and the two
// bars 2 times as big over the top 60 and the bottom 60 rows of it: where a bar has its plain colour, the scene shows
// through it, darker. A touch goes back to the point of the CoreS3 layout (t5_point).
#define T5_VW  320
#define T5_VH  240
#define T5_BAR 30  // (the rows of each bar in the CoreS3 layout)
static uint16_t *s_vfb;
static void t5_show(void)
{
    const uint16_t bar = rgbc(24, 21, 28);
    for (int y = 0; y < gfx_h; y++) {
        uint16_t *d = g_fb + (size_t)y * gfx_w;
        const uint16_t *band = s_vfb + (size_t)(T5_BAR + y / 2) * T5_VW;
        const uint16_t *top = y < 2 * T5_BAR ? s_vfb + (size_t)(y / 2) * T5_VW : NULL;
        const uint16_t *bot = y >= gfx_h - 2 * T5_BAR
                                      ? s_vfb + (size_t)(T5_VH - T5_BAR + (y - (gfx_h - 2 * T5_BAR)) / 2) * T5_VW
                                      : NULL;
        const uint16_t *over = top ? top : bot;
        for (int x = 0; x < gfx_w; x++) {
            uint16_t c = band[x >> 1];
            if (over) {
                uint16_t o = over[x >> 1];
                c = o == bar ? mix565(c, 0, 13) : o;
            }
            d[x] = c;
        }
    }
}
static void t5_point(int *x, int *y)
{
    const int Y = *y;
    *x /= 2;
    if (Y < 2 * T5_BAR) {
        *y = Y / 2;
    } else if (Y >= gfx_h - 2 * T5_BAR) {
        *y = T5_VH - T5_BAR + (Y - (gfx_h - 2 * T5_BAR)) / 2;
    } else {
        *y = T5_BAR + Y / 2;
    }
}
#endif

static void enter(int64_t now)
{
    s_last = now;
    s_ok = false;
    s_err[0] = 0;
#if BOARD_UI_TABLET
    SW = T5_VW;
    SH = T5_VH;  // (the Tab5: the CoreS3 layout in its own frame; see t5_show)
#elif BOARD_ROUND
    s_rnd = gfx_round() && rndo_open(&s_ro, 310);
    SW = s_rnd ? 310 : gfx_w;
    SH = s_rnd ? 310 : gfx_h;
#else
    SW = gfx_w;
    SH = gfx_h;
#endif
    SEA_X0 = 4;
    if (NARROW) {
        VH = 180;
        VY = (SH - VH) / 2;
        BK_X = 4;
        BK_Y = VY + VH + 2;
        BK_W = 84;
        BK_H = SH - BK_Y - 2;
        HN_W = 40;
        HN_X = SW - HN_W - 2;
        HN_Y = 1;
        HN_H = VY - 2;
        if (RND) {  // (the round frame: BACK on the left arc, the hint on the right arc, the seasons in the middle)
            BK_X = 8;
            BK_W = 62;
            BK_H = 40;
            BK_Y = SH / 2 - BK_H / 2;
            HN_W = 44;
            HN_H = 44;
            HN_X = SW - 10 - HN_W;
            HN_Y = SH / 2 - HN_H / 2;
            SEA_X0 = SW / 2 - NSEA * SEA_CW / 2 + 2;
        }
    } else {
        VY = 0;
        VH = SH;
        BK_X = 6;
        BK_Y = 126;
        BK_W = 96;
        BK_H = 40;
        HN_X = 588;
        HN_Y = 4;
        HN_W = 46;
        HN_H = 44;
    }
    s_lang = (int)store_get("fs_lang", 1) ? 1 : 0;
    s_music = store_get("fs_mus", 0) == 0;
    s_snd = store_get("fs_snd", 0) == 0;
    load();
    s_set[0].id = s_set[1].id = -1;
    if (!s_infl) {
        s_infl = fs_alloc(sizeof(tinfl_decompressor));
    }
    if (!s_ltmp) {
        s_ltmp = fs_alloc(LTMP_MAX);
        s_ltmp_n = s_ltmp ? LTMP_MAX : 0;
    }
    if (!s_infl || !s_ltmp) {
        snprintf(s_err, sizeof s_err, "NO MEMORY");
        return;
    }
    sysui_panel_items(&PANEL);
    tfsfx_load(FS_DIR "sfx", FXN, FX_N, 41);
    midi_bg_init(FS_TRACKS, FS_TRACK_COUNT, FS_MUSIC_VOICES);
    midi_bg_volume(70);
    if (s_music) {
        midi_bg_start();
    }
    s_cur = 0;
    title_go();
    if (s_ld.st == LS_FAIL) {
        return;  // (no data pack: the message of s_err shows)
    }
    s_ok = true;
}
static void leave(void)
{
    if (s_ok) {
        save();
    }
    load_cancel();
    midi_bg_end();
#if BOARD_ROUND
    rndo_close(&s_ro);
    s_rnd = false;
#endif
    tfsfx_free();
    for (int i = 0; i < 2; i++) {
        set_free(&s_set[i]);
    }
    free(s_infl);
    s_infl = NULL;
    free(s_ltmp);
    s_ltmp = NULL;
    s_ltmp_n = 0;
#if BOARD_UI_TABLET
    free(s_vfb);
    s_vfb = NULL;
#endif
    s_ok = false;
}

// ---- the input: a tap acts; a drag left / right changes the season
static bool s_fdown, s_dragged;
static int s_dx0, s_dy0;
static float s_ox0;  // (a narrow frame: the pan at the start of the drag)
static void tap_play(int x, int y)
{
    if (x >= HN_X - 4 && x < HN_X + HN_W + 8 && y >= HN_Y - 4 && y < HN_Y + HN_H + 4) {
        hint_next();
        return;
    }
    if (NARROW && y < VY) {  // (a season button of the top bar)
        int s = (x - SEA_X0 + 4) / SEA_CW;
        if (x < SEA_X0 - 4) {
            return;
        }
        if (s >= 0 && s < NSEA && s != s_season) {
            season_set(s);
        }
        return;
    }
    const bool on_back = x >= BK_X - 6 && x < BK_X + BK_W + 6 && y > BK_Y - 6 && y < BK_Y + BK_H + 6;
    if (NARROW && y >= VY + VH && !(parent_of(s_scene) >= 0 && on_back)) {
        return;  // (the bottom bar)
    }
    if (parent_of(s_scene) >= 0 && on_back) {
        go_scene(parent_of(s_scene), -1);
        return;
    }
    // the hot spots of this scene (the nearest box that holds the point, with a margin of 8 px for a thumb)
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < HS_N; i++) {
        const hot_t *H = &HOT[i];
        if (H->scene != s_scene) {
            continue;
        }
        float px = x + s_ox, py = y - VY + s_oy;
        if (px < H->x - 8 || px > H->x + H->w + 8 || py < H->y - 8 || py > H->y + H->h + 8) {
            continue;
        }
        float d = fabsf(px - (H->x + H->w * 0.5f)) / H->w + fabsf(py - (H->y + H->h * 0.5f)) / H->h;
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    if (best >= 0) {
        fx(FX_CLICK);
        act(best);
    }
}
static void input(const input_event_t *ev)
{
    if (!s_ok) {
        return;
    }
    int x = ev->x, y = ev->y;
#if BOARD_UI_TABLET
    t5_point(&x, &y);
#elif BOARD_ROUND
    if (s_rnd) {
        rndo_point(&s_ro, &x, &y);  // (a point of the panel to a point of the square frame)
    }
#endif
    if (ev->kind == IN_DOWN) {
        s_fdown = true;
        s_dragged = false;
        s_dx0 = x;
        s_dy0 = y;
        s_ox0 = s_ox;
        return;
    }
    if (NARROW && ev->kind == IN_MOVE && s_fdown && s_state == ST_PLAY) {
        // (a narrow frame: the drag pans the picture)
        if (abs(x - s_dx0) > 8) {
            if (!s_dragged) {
                input_tap_cancel();
            }
            s_dragged = true;
        }
        if (s_dragged) {
            s_ox = clampf(s_ox0 - (x - s_dx0), 0, (float)(cur_set()->w - SW));
        }
        return;
    }
    if (ev->kind == IN_MOVE && s_fdown && s_state == ST_PLAY && !s_dragged) {
        int dx = x - s_dx0, dy = y - s_dy0;
        if (abs(dx) > 70 && abs(dx) > abs(dy) * 2) {
            s_dragged = true;
            season_set(s_season + (dx < 0 ? 1 : 3));
        }
        return;
    }
    if (ev->kind == IN_UP) {
        s_fdown = false;
        return;
    }
    if (ev->kind != IN_TAP || s_dragged) {
        return;
    }
    switch (s_state) {
    case ST_TITLE: {
        bool cont = has_progress();
        if (cont && (NARROW ? y > (RND ? 203 : 194) : x > SW - 176 && y > 112)) {
            if (s_new_ask) {
                fx(FX_CLICK);
                play_start(true);
            } else {
                s_new_ask = true;
                fx(FX_CLICK);
            }
            return;
        }
        s_new_ask = false;
        fx(FX_CLICK);
        play_start(!cont);
        break;
    }
    case ST_PLAY:
        tap_play(x, y);
        break;
    case ST_END:
        if (s_state_t > 12) {
            title_go();
        }
        break;
    default:
        break;
    }
}
static void update(int64_t now)
{
    float dt = clampf((now - s_last) / 1000.0f, 0, 0.05f);
    s_last = now;
    if (!s_ok) {
        return;
    }
    midi_bg_service();
    s_state_t += dt;
    if (s_msg_t > 0) {
        s_msg_t -= dt;
    }
    if (s_hint_t > 0) {
        s_hint_t -= dt;
        if (s_hint_t <= 0) {
            s_hint_lv = 0;
        }
    }
    if (s_found_t > 0) {
        s_found_t -= dt;
    }
    if (s_sea_from >= 0) {
        s_sea_t += dt;
        if (s_sea_t > 0.8f) {
            s_sea_from = -1;
        }
    }
    // the music: a jingle, then the loop of the season (or the title)
    if (s_jingle_left > 0) {
        s_jingle_left -= dt;
        if (s_jingle_left <= 0) {
            s_jingle_left = 0;
            if (s_state == ST_END) {
                midi_bg_stop(400);
            }
        }
    }
    s_season_still += dt;
    if (s_jingle_left <= 0 && s_state != ST_END) {
        if (s_state == ST_TITLE) {
            mus_loop(FSM_TITLE);
        } else if (s_season_still > 1.5f) {
            mus_loop(SEASON_TRACK[s_season]);
        }
    }
    // the BOOT key: a hint
    if (app_key_presses() > 0) {
        hint_next();
    }
    // the loader and the compose of the other seasons
    load_step(9000);
    set_tick(cur_set());
    if (s_state == ST_TITLE) {
        s_title_sea = fmodf(s_title_sea + dt / 4.0f, (float)NSEA);
        s_ox = (SCENE[SC_VALLEY].w - SW) * (0.5f + 0.45f * sinf(s_state_t * 0.15f));
    } else if (s_state == ST_PLAY && NARROW) {
        s_oy = (SCENE[s_scene].h - VH) * 0.5f;  // (a narrow frame: the drag pans; no tilt pan)
    } else if (s_state == ST_PLAY) {
        // the tilt pans the picture a little (the position at the start is the zero)
        float tx = 0, ty = 0;
        if (imu_ok()) {
            imu_tilt_logical(&tx, &ty);
            tx -= s_tx0;
            ty -= s_ty0;
        }
        float wx = (SCENE[s_scene].w - SW) * clampf(0.5f + tx * 4, 0, 1),
              wy = (SCENE[s_scene].h - VH) * clampf(0.5f + ty * 4, 0, 1);
        s_ox += (wx - s_ox) * clampf(dt * 4, 0, 1);
        s_oy += (wy - s_oy) * clampf(dt * 4, 0, 1);
    } else if (s_state == ST_IN || s_state == ST_OUT) {
        // the move ends when the new scene is ready and the zoom (0.45 s) and the wipe (0.45 s) are done
        bool ready = s_set[1 - s_cur].ready && s_set[1 - s_cur].id == s_go_scene;
        if (s_ld.st == LS_FAIL) {
            s_state = ST_PLAY;
            msg(s_err, s_err);
        } else if (ready && s_state_t > 0.9f) {
            move_done();
        }
    }
}
#if BOARD_UI_TABLET
static void render_body(int64_t now);
static void render(int64_t now)
{
    if (!s_vfb) {
        s_vfb = fs_alloc((size_t)T5_VW * T5_VH * 2);
    }
    if (!s_vfb) {
        gfx_fill(rgbc(30, 26, 34));
        return;
    }
    uint16_t *real = g_fb;
    const int rw = gfx_w, rh = gfx_h;
    g_fb = s_vfb;
    gfx_w = T5_VW;
    gfx_h = T5_VH;
    gfx_clip_reset();
    render_body(now);
    g_fb = real;
    gfx_w = rw;
    gfx_h = rh;
    gfx_clip_reset();
    tfd_screen();
    t5_show();
}
static void render_body(int64_t now)
#elif BOARD_ROUND
static void render_body(int64_t now);
static void render(int64_t now)
{
    if (s_rnd) {
        rndo_enter(&s_ro, true);
        render_body(now);
        rndo_leave(&s_ro);
        rndo_out(&s_ro);
        return;
    }
    render_body(now);
}
static void render_body(int64_t now)
#else
static void render(int64_t now)
#endif
{
    (void)now;
    tfd_screen();
    tfd_noclip();
    if (!s_ok) {
        gfx_fill(rgbc(30, 26, 34));
        ui_text_c(&KF_LABEL, SW / 2, 80, s_err[0] ? s_err : "...", rgbc(255, 220, 200));
        ui_text_c(&KF_LABEL, SW / 2, 104, T("INSTALL THE DATA PACK FROM THE STORE", "התקינו את חבילת הנתונים מהחנות"),
                  rgbc(200, 200, 210));
        return;
    }
    sset_t *S = cur_set();
    if (!S->ready) {
        gfx_fill(rgbc(30, 26, 34));
        return;
    }
    if (VY > 0) {  // (a narrow frame: the bars over and under the band)
        gfx_fill_rect(0, 0, SW, VY, rgbc(24, 21, 28));
        gfx_fill_rect(0, VY + VH, SW, SH - VY - VH, rgbc(24, 21, 28));
    }
#ifdef ESP_PLATFORM
    int64_t t0 = esp_timer_get_time();
#endif
    switch (s_state) {
    case ST_TITLE:
        title_draw();
        break;
    case ST_END:
        end_draw();
        break;
    case ST_IN:
    case ST_OUT: {
        // the zoom toward the hot spot (in) or from the middle (out) for 0.45 s, then the round wipe of the new scene
        float t = s_state_t;
        float z = s_state == ST_IN ? 1 + smooth01(t / 0.45f) * 1.6f : 1 + smooth01(t / 0.45f) * 0.25f;
        float cx = s_ox + SW * 0.5f, cy = s_oy + VH * 0.5f, fx_ = SW * 0.5f, fy_ = VH * 0.5f;
        if (s_state == ST_IN && s_go_hot >= 0) {
            const hot_t *H = &HOT[s_go_hot];
            cx = H->x + H->w * 0.5f;
            cy = H->y + H->h * 0.5f;
            fx_ = lerpf(cx - s_ox, SW * 0.5f, smooth01(t / 0.45f));
            fy_ = lerpf(cy - s_oy, VH * 0.5f, smooth01(t / 0.45f));
        }
        draw_zoom_rows(S, s_season, s_season, 0, cx, cy, fx_, fy_, z, 0, VH);
        const sset_t *N = &s_set[1 - s_cur];
        if (N->ready && N->id == s_go_scene && t > 0.45f) {
            float r = smooth01((t - 0.45f) / 0.45f) * 360;
            int nox = (N->w - SW) / 2, noy = (N->h - VH) / 2;
            draw_circle_in(N, s_season, s_season, 0, nox, noy, fx_, fy_, r);
        }
        rnd_caps();
        break;
    }
    default:
        draw_scene_plain(S, (int)s_ox, (int)s_oy);
        rnd_caps();
        ui_draw();
        break;
    }
#ifdef ESP_PLATFORM
    static int64_t s_sum;
    static int s_n;
    s_sum += esp_timer_get_time() - t0;
    if (++s_n == 150) {
        ESP_LOGI("seasons", "frame %d us (draw), scene %d, load %d ms", (int)(s_sum / s_n), s_scene, s_load_ms);
        s_sum = 0;
        s_n = 0;
    }
#endif
}
#include "fs_icon.inc"

const app_t app_seasons = {
        .name = "FOUR SEASONS",
        .landscape = true,
        .round = APP_ROUND_HI,  // (the round panels: a square frame scaled to the circle, the band in the middle)
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = fs_icon,
        .key = true,
};
