// Main menu, a tree of groups (menu_tree.c: the layout; the manager sends one from a profile):
// - the built-in layout: the top level has the groups FAVORITES (when there are favorites), GAMES, UTIL (tools and
//   teaching apps) and SETTINGS. A tap opens the group. A group with one app (SETTINGS) opens its app at once.
// - a group: BACK (to the level above), then its items (apps and groups), the favorites first.
// - a layout can put apps and groups on the top level, groups in groups (32 groups), one of 24 looks for each group,
//   one app in more groups, the rest of a built-in group (*GAMES: the apps that it does not name), and the
//   favorites as an item (GROUP), on the top level (TOP) or not at all (OFF).
// - in a group in a group, the title shows the path ("GAMES / QUESTS"); BACK says where it goes ("TO GAMES") and
//   has the look of that level.
// The look is "Spotlight" (menu_spot.c): one item at a time in the light, big and animated, with
// its name in a marquee face; its neighbours wait smaller and dimmer at both sides. The room takes the
// colour of the group. The round panel (the Watcher) has its own carousel (menu_round.c).
// Hold the middle app (HOLD_MS) to add it to the favorites or to remove it: a star shows on it. The
// favorites are kept in NVS by a hash of the app name, so new apps do not move them. The menu shows
// only the apps that the board can run (app_t.needs, app_board_caps()). It keeps the level and the
// selection when an app opens, so that the user comes back to the same place.
// The Tab5 has its own menu (menu_tab5.c): the groups as tabs in a rail at the left, the apps of the
// group as a grid of big icons, the time and the battery at the top right. The other boards do not change.

#include "app_menu_priv.h"

// Each app and its group, in the menu order: the apps of this build. The list is main/apps.json (a new app = one
// entry there; ARCADE_APPS_LIST in app.h). There, a NEW game or a game IN PROGRESS goes to the front of the games
// (a test needs no scroll); the order of GAMES: the new games, then action, puzzle, toys and music, two
// players, tap games, real-world games, 3D. The menu itself (group SYSTEM) is not an item.
#define ARCADE_IN_GAMES(sym)    {&sym, CAT_GAMES},
#define ARCADE_IN_UTIL(sym)     {&sym, CAT_UTIL},
#define ARCADE_IN_SETTINGS(sym) {&sym, CAT_SETTINGS},
#define ARCADE_IN_SYSTEM(sym)
static const aent_t BUILT[] = {
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) ARCADE_IN_##group(sym)
#include ARCADE_APPS_LIST
#undef ARCADE_APP
};
_Static_assert(N_BUILT == (int)(sizeof(BUILT) / sizeof(BUILT[0])), "N_BUILT");
// TFN OS: the menu list =the built-in apps (an installed copy of the same name takes their place) and
// then the installed apps (main/tfn_app.c). N_CAP sizes the arrays; N_ALL is the count now.
aent_t men_all[N_CAP];
int men_nall;
static uint32_t s_all_gen;
static char (*s_all_nm)[24];  // (PSRAM) the name of each ALL[] item at the last all_build()
const char *men_all_name(int i)
{
    return s_all_nm ? s_all_nm[i] : men_all[i].app->name;
}
static void all_names_keep(void)
{
    if (!s_all_nm) {
        s_all_nm = heap_caps_calloc(N_CAP, 24, MALLOC_CAP_SPIRAM);
    }
    if (s_all_nm) {
        for (int i = 0; i < men_nall; i++) {
            snprintf(s_all_nm[i], 24, "%s", men_all[i].app->name);
        }
    }
}
static int new_index(const tfn_entry_t *e);
static int new_count(void);
// (the order: the new apps first, built in and installed ("new" in main/apps.json; an installed app: new=1 in its
// manifest), then the others. The installed apps come after all the built-in apps, but a new installed app that this
// firmware does not know (not in its apps_all.inc: newer than the firmware) comes FIRST, the newest package first, so
// that a new store app is not at the far end of GAMES)
static bool all_named(const tfn_entry_t *e)
{
    for (int q = 0; q < N_BUILT; q++) {
        if (!strcasecmp(BUILT[q].app->name, e->name)) {
            return true;
        }
    }
    return false;
}
void men_all_build(void)
{
    men_nall = 0;
    // the new installed apps that the firmware does not know, the highest code (the newest package) first
    {
        bool used[N_CAP] = {false};
        for (;;) {
            int best = -1;
            for (int i = 0; i < tfn_count() && i < N_CAP; i++) {
                const tfn_entry_t *e = tfn_entry(i);
                if (used[i] || !tfn_runs(e) || !e->is_new || new_index(e) >= 0 || all_named(e)) {
                    continue;
                }
                if (best < 0 || e->code > tfn_entry(best)->code) {
                    best = i;
                }
            }
            if (best < 0 || men_nall >= N_CAP) {
                break;
            }
            used[best] = true;
            const tfn_entry_t *e = tfn_entry(best);
            men_all[men_nall++] = (aent_t){&e->stub, (uint8_t)tfn_group_of(&e->stub)};
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < N_BUILT; i++) {
            if (men_builtin_new(BUILT[i].app) != (pass == 0)) {
                continue;
            }
            const app_t *inst = tfn_find(BUILT[i].app->name);
            men_all[men_nall++] = (aent_t){inst ? inst : BUILT[i].app, BUILT[i].cat};
        }
        // (the new installed apps that the firmware knows, in the order of main/apps.json: the newest first; the ones
        // that it does not know are at the start of the list already)
        const int nsym = new_count();
        for (int k = 0; k < (pass == 0 ? nsym : 1); k++) {
            for (int i = 0; i < tfn_count() && men_nall < N_CAP; i++) {
                const tfn_entry_t *e = tfn_entry(i);
                if (!tfn_runs(e) || men_tfn_new(e) != (pass == 0)) {
                    continue;
                }
                if (pass == 0 && new_index(e) != k) {
                    continue;
                }
                if (!all_named(e)) {
                    men_all[men_nall++] = (aent_t){&e->stub, (uint8_t)tfn_group_of(&e->stub)};
                }
            }
        }
    }
    s_all_gen = tfn_gen();
    all_names_keep();
}
// TFN OS: the apps of this firmware (the store shows them as BUILT IN; the manager installs no copy of them)
bool menu_builtin(const char *name)
{
    for (int i = 0; i < N_BUILT; i++) {
        if (!strcasecmp(BUILT[i].app->name, name)) {
            return true;
        }
    }
    return false;
}
int menu_builtin_names(const char **out, int max)
{
    int n = 0;
    for (int i = 0; i < N_BUILT && n < max; i++) {
        out[n++] = BUILT[i].app->name;
    }
    return n;
}
// the new apps (a NEW pill under the name: "new" in main/apps.json), then NULL (the list can have no app)
#define ARCADE_NEW_0(sym)
#define ARCADE_NEW_1(sym) &sym,
static const app_t *const NEW_APPS[] = {
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) ARCADE_NEW_##is_new(sym)
#include ARCADE_APPS_LIST
#undef ARCADE_APP
        NULL};
bool men_builtin_new(const app_t *a)
{
    for (unsigned i = 0; i < sizeof NEW_APPS / sizeof NEW_APPS[0]; i++) {
        if (NEW_APPS[i] == a) {
            return true;
        }
    }
    return false;
}
// the new apps of the whole list (apps_all.inc: also the apps that are only files of the store), by their symbol
// "app_<id>": an installed app is new when its id is here or when its manifest has new=1
#define ARCADE_NEWSYM_0(sym)
#define ARCADE_NEWSYM_1(sym) #sym,
static const char *const NEW_SYMS[] = {
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) ARCADE_NEWSYM_##is_new(sym)
#include "apps_all.inc"
#undef ARCADE_APP
        NULL};
static int new_index(const tfn_entry_t *e)
{
    for (int i = 0; NEW_SYMS[i]; i++) {
        if (!strncmp(NEW_SYMS[i], "app_", 4) && !strcasecmp(NEW_SYMS[i] + 4, e->id)) {
            return i;
        }
    }
    return -1;
}
bool men_tfn_new(const tfn_entry_t *e)
{
    return e && (e->is_new || new_index(e) >= 0);
}
static int new_count(void)
{
    return (int)(sizeof NEW_SYMS / sizeof NEW_SYMS[0]) - 1;
}

item_t men_items[N_CAP + G_MAX + 4];
int men_n_items;
int men_level = -1;            // -1 = the top level, else the group (an index of the layout, or LV_FAV)
int men_cat_count[G_MAX + 1];  // the apps in each group (and its groups) that this board can run; [LV_FAV]: favorites

// the favorites: hashes of the app names, in the order the user added them
static uint32_t s_fav[FAV_MAX];
static int s_nfav;

// ---------------------------------------------------------------- the state
float men_pos;              // the row place now (in items)
int men_sel;                // the item that the row goes to
int men_sel_of[G_MAX + 2];  // the selection of each level (the favorites = G_MAX, the top level = G_MAX + 1)
float men_vel;              // items per second (a flick)
bool men_touch, men_drag;
int men_a0, men_last_a;  // the finger along the row: at the press, the last
int men_x0, men_y0;      // the press point (the swipe up / down to the next group)
bool men_vsw;            // a swipe up / down is on
int64_t men_vsw_at;      // the time of the last group change by a swipe
int men_vsw_dir;         // its way: 1 = to the next group (a swipe up)
int64_t men_bump_at;     // a swipe that found no other group: the words bump (spot); 0 = none
float men_pos0;
int64_t men_last_t, men_prev, men_now;
int men_pressed = -1;  // the item under the finger
int64_t men_down_t;
bool men_hold_done;
char men_pop_text[32];
int64_t men_pop_at = -100000;
float men_tilt, men_tilt_base, men_glow_t;
int men_shown = -1;  // the middle item of the last frame (a tick when it changes)
int men_was_w, men_was_h;

men_star_t men_stars[N_STARS];

// ---------------------------------------------------------------- favorites

static uint32_t name_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) {
        h = (h ^ (uint8_t)*s++) * 16777619u;
    }
    return h ? h : 1;  // 0 means "no entry" in NVS
}

// the hash of an app for the favorites
static uint32_t app_hash(const app_t *a)
{
    return name_hash(a->name);
}

int men_fav_index(const app_t *a)
{
    uint32_t h = app_hash(a);
    for (int i = 0; i < s_nfav; i++) {
        if (s_fav[i] == h) {
            return i;
        }
    }
    return -1;
}

static void fav_load(void)
{
    s_nfav = 0;
    int n = (int)store_get("fav_n", 0);
    if (n > FAV_MAX) {
        n = FAV_MAX;
    }
    for (int i = 0; i < n; i++) {
        char key[8];
        snprintf(key, sizeof(key), "fav_%d", i);
        uint32_t h = store_get(key, 0);
        if (h) {
            s_fav[s_nfav++] = h;
        }
    }
}

static void fav_save(void)
{
    for (int i = 0; i < s_nfav; i++) {
        char key[8];
        snprintf(key, sizeof(key), "fav_%d", i);
        store_set(key, s_fav[i]);
    }
    store_set("fav_n", (uint32_t)s_nfav);
}

// returns true when the app is a favorite now
bool men_fav_toggle(const app_t *a)
{
    int i = men_fav_index(a);
    if (i >= 0) {
        memmove(&s_fav[i], &s_fav[i + 1], (size_t)(s_nfav - i - 1) * sizeof(s_fav[0]));
        s_nfav--;
        fav_save();
        return false;
    }
    if (s_nfav >= FAV_MAX) {
        memmove(&s_fav[0], &s_fav[1], (FAV_MAX - 1) * sizeof(s_fav[0]));  // drop the oldest
        s_nfav--;
    }
    s_fav[s_nfav++] = app_hash(a);
    fav_save();
    return true;
}

// ---------------------------------------------------------------- the items of a level

bool men_can_run(int i)
{
    return (men_all[i].app->needs & ~app_board_caps()) == 0;
}

// the apps that this board can run, in the menu order, with the group (0 GAMES, 1 UTIL, 2 SETTINGS): the manager
// page shows them as an icon grid (web_walkie.c /api/ctl)
int menu_list(const char **names, uint8_t *cats, int max)
{
    int n = 0;
    for (int i = 0; i < N_ALL && n < max; i++) {
        if (!men_can_run(i)) {
            continue;
        }
        names[n] = men_all[i].app->name;
        cats[n++] = men_all[i].cat;
    }
    return n;
}

// HOME 2.0 (menu_home.c)
bool men_home_later;  // the question of a new profile was not answered: ask again at the next enter

void men_add_app(const app_t *a)
{
    men_items[men_n_items++] = (item_t){IT_APP, 0, a};
}
bool men_in_items(const app_t *a)
{
    for (int i = 0; i < men_n_items; i++) {
        if (men_items[i].kind == IT_APP && men_items[i].app == a) {
            return true;
        }
    }
    return false;
}
// the favorites of the layout, in the order the user added them: all (parent -2) or the ones right in a group
static void add_favs(int parent)
{
    for (int f = 0; f < s_nfav; f++) {
        for (int k = 0; k < men_nview; k++) {
            const lent_t *e = &men_view[k];
            if (e->kind == E_APP && men_can_run(e->ref) && app_hash(men_all[e->ref].app) == s_fav[f] &&
                (parent == -2 || e->parent == parent) && !men_in_items(men_all[e->ref].app)) {
                men_add_app(men_all[e->ref].app);
            }
        }
    }
}

void men_build_items(void)
{
    men_n_items = 0;
    men_count_groups();
#if BOARD_UI_TABLET
    if (men_level == LV_NEW) {
        men_tb_new_items();
        return;
    }
#endif
    if (men_level != LV_FAV && men_level >= men_lay.ng) {
        men_level = -1;  // (a new layout has fewer groups)
    }
    if (men_level == LV_FAV) {
        men_items[men_n_items++] = (item_t){IT_BACK, LV_FAV, NULL};
        add_favs(-2);
        return;
    }
    if (men_level >= 0) {
        men_items[men_n_items++] = (item_t){IT_BACK, (uint8_t)men_level, NULL};
        if (men_fav_on()) {
            add_favs(men_level);  // the favorites first
        }
    } else if (men_lay.fav == FAVM_TOP) {
        add_favs(-2);  // the favorites on the top level
    }
    for (int k = 0; k < men_nview; k++) {
        const lent_t *e = &men_view[k];
        if (e->parent != men_level) {
            continue;
        }
        if (e->kind == E_FAV) {
            if (men_level < 0 && men_lay.fav == FAVM_GROUP && men_cat_count[LV_FAV] > 0) {
                men_items[men_n_items++] = (item_t){IT_CAT, LV_FAV, NULL};
            }
        } else if (e->kind == E_GRP) {
            if (men_cat_count[e->ref] > 0) {
                men_items[men_n_items++] = (item_t){IT_CAT, (uint8_t)e->ref, NULL};
            }
        } else if (men_can_run(e->ref) && !men_in_items(men_all[e->ref].app)) {
            men_add_app(men_all[e->ref].app);
        }
    }
}

void men_go_level(int level)
{
    int from = men_level;
    men_sel_of[men_level_slot()] = men_sel;
    men_level = level;
    men_build_items();
    men_spot_level_changed();
    if (gfx_round()) {
        men_r_level_changed(from);
    }
#if BOARD_UI_TABLET
    men_tb_level_changed(from);
#endif
}

// The PWR key in the menu (main.c): up one level. Returns false at the top level.
bool menu_back(void);
bool menu_back(void)
{
    if (men_home_active()) {
        men_home_back();
        return true;
    }
#if BOARD_UI_TABLET
    return men_tb_back();  // (the tabs are the top level: only a group in a group goes up)
#endif
    if (men_level < 0) {
        return false;
    }
    men_go_level(men_gparent(men_level));
    return true;
}

void men_pop(const char *t)
{
    snprintf(men_pop_text, sizeof(men_pop_text), "%s", t);
    men_pop_at = men_now;
}

// HOME 2.0 (menu_home.c, TFN OS phase 6): a hold opens the sheet of an app or a group; DELETE of an installed app
// asks here
const app_t *men_hold_app;
char men_hold_id[24];  // the id of the app of the DELETE question (the list can change under the question)
void men_inst_remove_answer(int a)
{
    if (a != 1 || !men_hold_id[0]) {
        return;
    }
    char msg[96];
    bool ok = tfn_remove(men_hold_id, msg, sizeof msg);
    sfx_play(ok ? SFX_CUT : SFX_BACK);
    men_pop(ok ? "REMOVED" : "NOT REMOVED");
}
// tests (the console "menusel NAME [hold]", for the pictures of the menu): the menu goes to the app (its group, its
// place in the row); hold = the action of a hold on it. The console task asks; menu_update() does it.
static char s_tsel[24];
static volatile int s_tsel_req;  // 1 = select, 2 = select and hold
void menu_test_select(const char *name, bool hold);
void menu_test_select(const char *name, bool hold)
{
    snprintf(s_tsel, sizeof s_tsel, "%s", name);
    s_tsel_req = hold ? 2 : 1;
}
static void test_select_now(void)
{
    int req = s_tsel_req;
    s_tsel_req = 0;
    for (int k = 0; k < men_nview; k++) {
        const lent_t *e = &men_view[k];
        if (e->kind != E_APP || strcasecmp(men_all[e->ref].app->name, s_tsel)) {
            continue;
        }
        const app_t *app = men_all[e->ref].app;
        if (men_level != e->parent) {
            men_go_level(e->parent);
        }
        for (int i = 0; i < men_n_items; i++) {
            if (men_items[i].kind == IT_APP && men_items[i].app == app) {
                men_sel = i;
                men_pos = (float)i;
            }
        }
        men_spot_clamp();
#if BOARD_UI_TABLET
        men_tb_reveal(men_sel);
#endif
        if (req == 2) {
            int i = men_sel;
            if (gfx_round()) {
                for (int j = 0; j < men_n_items; j++) {
                    if (men_items[j].kind == IT_APP && men_items[j].app == app) {
                        i = j;
                    }
                }
            }
            men_home_hold_item(i, men_level);
        }
        ESP_LOGI("menu", "test: %s selected%s", app->name, req == 2 ? " and held" : "");
        return;
    }
    ESP_LOGW("menu", "test: no app %s in the menu", s_tsel);
}

// tests (the console "menuedit"): the edit mode of HOME 2.0 at the top level, as a hold on the background
static volatile bool s_tedit_req;
void menu_test_edit(void);
void menu_test_edit(void)
{
    s_tedit_req = true;
}

static void menu_init(void)
{
    men_all_build();
    fav_load();
    men_lay_load();
    men_build_items();
    for (int i = 0; i < N_STARS; i++) {
        men_stars[i].x = (int16_t)(esp_random() % 640);
        men_stars[i].y = (int16_t)(esp_random() % 640);
        men_stars[i].speed = (uint8_t)(1 + esp_random() % 3);
        men_stars[i].bright = (uint8_t)(60 + esp_random() % 120);
    }
}

#if BOARD_UI_TABLET
void menu_redraw_all(void)
{
    men_drawn_reset();
    men_tb_redraw_all();
}
#else
void menu_redraw_all(void)
{
    men_drawn_reset();
}
#endif
static void menu_enter(int64_t now)
{
    men_pressed = -1;
    men_touch = men_drag = false;
    men_vel = 0;
    men_prev = men_now = now;
    men_pop_at = -100000;
    if (men_home_later) {
        men_home_later = false;
        men_home_ask = true;
    }  // (HOME 2.0: the question of a new profile again)
    if (s_all_gen != tfn_gen()) {  // TFN OS: an app came or went: the list and the layout again
        men_all_build();
        men_lay_reload = true;
    }
    if (men_lay_reload) {
        // a new layout came while an app was open: the top level of the new tree
        men_lay_reload = false;
        men_lay_load();
        men_level = -1;
        memset(men_sel_of, 0, sizeof men_sel_of);
        men_sel = 0;
    }
    men_build_items();  // the level and the selection stay as the user left them
    men_spot_clamp();
    men_pos = (float)men_sel;
    men_shown = men_sel;
    men_drawn_reset();
    if (gfx_round()) {
        men_r_enter();
    }
#if BOARD_UI_TABLET
    men_tb_enter(now);
#endif
}

void men_open_item(int i)
{
    const item_t *it = &men_items[i];
    if (it->kind == IT_APP) {
        sfx_ui(SFX_CLICK);
        app_open(it->app);
    } else if (it->kind == IT_BACK) {
        sfx_ui(SFX_BACK);
        men_go_level(men_gparent(men_level));
    } else if (it->cat != LV_FAV && men_cat_count[it->cat] == 1) {
        // a group with one app (also in a group in it): open the app at once
        for (int k = 0; k < men_nview; k++) {
            const lent_t *e = &men_view[k];
            if (e->kind == E_APP && men_can_run(e->ref) && men_g_under(e->parent, it->cat)) {
                sfx_ui(SFX_CLICK);
                app_open(men_all[e->ref].app);
                break;
            }
        }
    } else {
        sfx_ui(SFX_CLICK);
        men_go_level(it->cat);
    }
}

static void menu_input(const input_event_t *ev)
{
    if (s_all_gen != tfn_gen() && (ev->kind == IN_TAP || ev->kind == IN_LONG)) {
        return;  // (the list changes now)
    }
    if (men_home_active()) {
        if (ev->kind == IN_KEY) {  // (a keyboard: ESC closes the sheet or the edit mode)
            if (ev->x == INK_ESC) {
                men_home_close();
            }
            return;
        }
        men_home_input(ev);
        return;
    }
    if (gfx_round()) {
        men_round_input(ev);
        return;
    }
#if BOARD_UI_TABLET
    men_tb_input(ev);
    return;
#endif
    men_spot_input(ev);
}

static void menu_update(int64_t now)
{
    float dt = (now - men_prev) / 1000.0f;
    men_prev = now;
    men_now = now;
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    if (men_home_active()) {  // HOME 2.0: a screen over the menu (a new layout from the manager waits for its end)
        if (s_all_gen != tfn_gen()) {
            men_home_gen_changed();
        }
        if (men_home_active()) {
            s_tsel_req = 0;
            s_tedit_req = false;
            men_home_update(now, dt);
            return;
        }
    }
    if (s_all_gen != tfn_gen()) {  // TFN OS: an install or a removal while the menu is open
        men_all_build();
        men_lay_reload = true;
    }
    if (men_lay_reload) {
        // a new layout from the manager: the top level of the new tree
        men_lay_reload = false;
        men_lay_load();
        men_level = -1;
        memset(men_sel_of, 0, sizeof men_sel_of);
        men_build_items();
        men_sel = 0;
        men_spot_level_changed();
        if (gfx_round()) {
            men_r_level_changed(-1);
        }
    }
    if (s_tsel_req) {
        test_select_now();
    }
#if BOARD_UI_TABLET
    if (s_tedit_req && men_level == LV_NEW) {
        men_level = -1;  // (the edit mode has no level for the NEW tab: the top level)
    }
#endif
    if (s_tedit_req) {
        s_tedit_req = false;
        men_home_test_edit(now);
        return;
    }
    men_home_menu_tick();
    if (gfx_round()) {
        men_round_update(now, dt);
        return;
    }
#if BOARD_UI_TABLET
    men_tb_update(now, dt);
    return;
#endif
    men_spot_update(now, dt);
}

static void menu_render(int64_t now)
{
#if BOARD_UI_TABLET
    if (men_home_active()) {
        men_tb_redraw_all();
        men_home_render(now);
        return;
    }  // (the frames of the sheet: tb_render() draws all after it)
#else
    if (men_home_active()) {
        men_home_render(now);
        return;
    }
#endif
    if (gfx_round()) {
        men_round_render(now);
        return;
    }
#if BOARD_UI_TABLET
    men_tb_render(now);
    return;
#endif
    men_spot_render(now);
}

static void menu_icon(int x, int y, int size)
{
    float t = icon_time();
    int hi = (int)(t * 2.0f) & 3;  // one tile of the grid lights in turn
    gfx_fill_rect(x, y, size, size, rgb(24, 22, 52));
    for (int i = 0; i < 4; i++) {
        gfx_fill_rect(x + size / 8 + (i & 1) * size * 7 / 16, y + size / 8 + (i >> 1) * size * 7 / 16, size * 3 / 8,
                      size * 3 / 8, i == hi ? rgb(250, 215, 30) : rgb(70, 84, 130));
    }
}

// the rooms of the menu need about 0.2-0.3 MB each of PSRAM: give it back to the app
static void menu_leave(void)
{
    men_home_leave();
    men_rooms_free();
#if BOARD_UI_TABLET
    men_tb_rooms_free();
    men_tb_redraw_all();
#endif
}

const app_t app_menu = {
        .name = "MENU",
        .landscape = true,
        .init = menu_init,
        .enter = menu_enter,
        .leave = menu_leave,
        .input = menu_input,
        .update = menu_update,
        .render = menu_render,
        .draw_icon = menu_icon,
        .native = true,
        .round = APP_ROUND_HI,  // (the 1.75C draws the menu at 466, no scale in the flush)
        .wheel = true,
};
