// Arcade for the Waveshare ESP32-S3-Touch-LCD-3.49: a menu with games and a
// settings screen.
//
// PWR key: a short push goes back to the menu. Hold it for 2 s to power off.
// Screensaver: no activity for the time in SETUP -> dim, 15 s later -> off.
// Activity = touch, PWR key, a change of the board position, a shake, music.

#include "power.h"
#include "main_priv.h"
#include "tfn/theme.h"
#include "tfn_id.h"
#include "tfn_pool.h"
#include "radio_play.h"
#include "standby.h"

static const char *TAG = "main";

static float s_app_fps;  // the frames a second of the app (each 300 frames), for /api/ctl
float app_fps(void)
{
    return s_app_fps;
}

// All apps of this build: the menu, then the menu order (main/apps.json; ARCADE_APPS_LIST in app.h)
static const app_t *const ALL_APPS[] = {
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) &sym,
#include ARCADE_APPS_LIST
#undef ARCADE_APP
};

const app_t *mai_app;
static const app_t *volatile s_next_app;  // app_open() (also from the web server task)

// The settings panel (sysui.h, item 2.d): while it shows, the app stops (no update, no render) and its clock stops: the
// app gets the time less the time that it was stopped (s_frozen_ms), so its timers do not jump. An app with app_t.run
// (a timer, a music tool, a call) runs on under the panel with no touches, and so does each app while a radio link is on
// (the other board would end the link). s_frozen_ms goes back to 0 when an app opens and when a radio link starts (the
// link code mixes the app time with the time of the radio).
// TEAM BUILDING and SEA BATTLE start the radio when they open (their lobby): with no other board in a match (a match
// against the bots or the board) they stop under the panel all the same (app_solo). CHESS runs on in all its modes (its
// board turn in 2 PLAYERS uses the time of the search, not the app time).
static int64_t s_frozen_ms, s_frozen_at;  // s_frozen_at: the time the app stopped (0 = it runs)
static bool s_link_was;
bool mai_app_is(const char *name)
{
    return mai_app && mai_app->name && !strcmp(mai_app->name, name);
}

// A MATCH WITH ANOTHER BOARD (app.h): a game of 2 BOARDS (net2: HOST or CLIENT), a turn game (duel: on, or a
// join), or the own link of an app (PONG, FIGHT, TEAM BUILDING, BOULEVARD, MORSE) with another board that answers now
// (link_in_use: a packet of the same app in the last 5 s). It looks at the state of the link, not at the app: the games
// are app files on the slim firmware, and a check with the app_t of a built-in app (ARCADE_APP_ON(CHESS)) does not work
// there (the app is not in the firmware). With such a check, a turn game over the LAN or the VPN lets the screen go
// off, the app stops, and the other board ends the match.
bool app_in_match(void)
{
    if (!link_ready()) {
        return false;
    }
    const n2_role_t r = net2_role();
    if (r == N2_HOST || r == N2_CLIENT) {
        return true;
    }
    const int d = duel_state();
    if (d == DUEL_ON || d == DUEL_JOIN) {
        return true;
    }
    return link_in_use();
}
static bool app_solo(void)  // the radio is on for the lobby, but no other board is in a match now
{
    if (mai_app_is("TEAM BUILDING") || mai_app_is("SEA BATTLE")) {
        return !app_in_match() && !friends_lobby_waits();  // (an invite that waits runs on: the friend finds it)
    }
    return false;
}
bool mai_app_runs(void)
{
    return mai_app->run || (link_ready() && !app_solo());
}
// Each touch (IN_DOWN to IN_UP, and the IN_TAP after it) has one handler: the panel when it showed at the IN_DOWN, else
// the app. When the panel comes over a touch of the app, the app gets an IN_UP at once, and the rest of that touch goes
// to nobody.
enum {
    OWN_NONE,
    OWN_APP,
    OWN_PANEL,
    OWN_DROP
};
static uint8_t s_own;
static bool s_own_down;
static int16_t s_own_x, s_own_y;

#if BOARD_UI_TABLET
#include "home_bar.inc"  // the home bar: a swipe up from the bottom edge goes to the menu (the Tab5)
#endif

// the console "shot" (tests) and /api/screen: a copy of the next complete frame, in PSRAM.
// g_shot_busy counts the readers (they add 1 before the request and take it off after the send).
// The buffer (150-340 KB) is freed 10 s after its last use, so the quests can use that PSRAM.
volatile int g_shot_req, g_shot_ready, g_shot_w, g_shot_h, g_shot_busy;
uint16_t *g_shot_buf;
static void shot_copy(void)
{
    static size_t cap;
    static int64_t last_use;
    const int64_t now = tfn_now_ms();
    if (__atomic_load_n(&g_shot_busy, __ATOMIC_SEQ_CST) > 0) {
        last_use = now;
    }
    if (!g_shot_req) {
        if (g_shot_buf && now - last_use > 10000 && __atomic_load_n(&g_shot_busy, __ATOMIC_SEQ_CST) == 0) {
            free(g_shot_buf);
            g_shot_buf = NULL;
            cap = 0;
        }
        return;
    }
    size_t n = (size_t)gfx_w * gfx_h;
    // a bigger frame needs a new buffer: not while another reader still sends the old one (the requester counts 1)
    if (n * 2 > cap && g_shot_buf && __atomic_load_n(&g_shot_busy, __ATOMIC_SEQ_CST) > 1) {
        return;
    }
    g_shot_req = 0;
    last_use = now;
    if (n * 2 > cap) {
        free(g_shot_buf);
        g_shot_buf = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
        cap = g_shot_buf ? n * 2 : 0;
    }
    if (g_shot_buf && gfx_hi()) {
        // (the HI frame of the Tab5: one buffer pixel of each 2 x 2, the logical frame)
        const int s = gfx_hi_stride();
        for (int y = 0; y < gfx_h; y++) {
            for (int x = 0; x < gfx_w; x++) {
                g_shot_buf[y * gfx_w + x] = g_fb[2 * y * s + 2 * x];
            }
        }
        g_shot_w = gfx_w;
        g_shot_h = gfx_h;
        g_shot_ready = 1;
    } else if (g_shot_buf) {
        memcpy(g_shot_buf, g_fb, n * 2);
        g_shot_w = gfx_w;
        g_shot_h = gfx_h;
        g_shot_ready = 1;
    }
}

int app_names(const char **out, int max)
{
    int n = 0;
    for (size_t i = 0; i < sizeof(ALL_APPS) / sizeof(ALL_APPS[0]) && n < max; i++) {
        if (!tfn_find(ALL_APPS[i]->name)) {
            out[n++] = ALL_APPS[i]->name;  // (an installed copy is in the list below)
        }
    }
    for (int i = 0; i < tfn_count() && n < max; i++) {  // TFN OS: the installed apps
        if (tfn_runs(tfn_entry(i))) {
            out[n++] = tfn_entry(i)->name;
        }
    }
    return n;
}

const app_t *app_current(void)
{
    return mai_app;
}

static char s_next_arg[64], s_arg[64];  // app_open_arg(): the argument of the next app, of the app now
void app_open(const app_t *app)
{
    s_next_arg[0] = 0;
    s_next_app = app;
}
void app_open_arg(const app_t *app, const char *arg)
{
    snprintf(s_next_arg, sizeof s_next_arg, "%s", arg ? arg : "");
    s_next_app = app;
}
const char *app_launch_arg(void)
{
    return s_arg;
}

const app_t *app_find(const char *name)
{
    const app_t *inst = tfn_find(name);  // TFN OS: an installed app wins over a built-in one of the same name
    if (inst) {
        return inst;
    }
    for (size_t i = 0; i < sizeof(ALL_APPS) / sizeof(ALL_APPS[0]); i++) {
        if (!strcasecmp(ALL_APPS[i]->name, name)) {
            return ALL_APPS[i];
        }
    }
    return NULL;
}

uint8_t app_board_caps(void)
{
#if !BOARD_BAR_SCREEN
    // a 320 x 240 screen (the AMOLED: scaled); the IMU only on the full CoreS3 (the SE has none)
    // and the AMOLED (the Watcher has none)
    return imu_ok() ? APP_NEEDS_IMU : 0;
#else
    return APP_NEEDS_BAR | APP_NEEDS_IMU;
#endif
}

static void switch_app(const app_t *app, int64_t t)
{
    // TFN OS: the menu item of an installed app loads its file here. Another app: the old app leaves FIRST, so its
    // memory is free for the new file (two big apps in memory at the same time can fail to load: NO MEMORY). A fault of the load opens the menu with the message.
    char msg[96];
    const app_t *old = mai_app;
    bool left = false;
    if (old && !tfn_same_app(app, old)) {
        if (old->leave) {
            old->leave();
        }
        tfn_left(old);  // (an installed app: its file is freed)
        left = true;
        mai_app = &app_menu;  // (while the new file loads: not the old app, so the loader can free it; the menu is
                              // always valid for the other tasks; its enter() did not run, nothing draws now)
    }
    const app_t *real = tfn_resolve(app, msg, sizeof msg);
    if (!real) {
        ESP_LOGW(TAG, "%s", msg);
        sysui_message(msg, 6);
        if (mai_app && !left) {
            return;
        }
        real = &app_menu;
    }
    app = real;
    if (!left && mai_app && mai_app->leave) {
        mai_app->leave();
    }
    if (!left && old != app) {
        tfn_left(old);
    }
    power_hold(false);  // (app_standby_hold: only for the app that asked)
    mai_pocket_ms = 0;
    mai_app = app;
    gfx_set_native(BOARD_ROUND ? app->round : app->native);
    gfx_hi_want(false);  // (the Tab5: a new app starts with the normal frame)
    if (app != &app_menu) {
        tfn_icon_cache_free();  // (the decoded icon frames of the menu: their PSRAM to the app)
    }
    gfx_hi_frame(false);
#if BOARD_ROUND
    board_round_fit(app->round_fit);
#endif
    orient_t o = app->landscape ? ORIENT_LAND_A : ORIENT_PORTRAIT;
    if (BOARD_FIXED_ORIENT) {
        // no screen turn (the Watcher: the wheel is a virtual tilt, not the board position)
    } else if (g_settings.auto_rotate && app == &app_menu) {
        o = imu_upright_any(gfx_orient(), 0.25f);  // the menu has a portrait layout too
    } else if (g_settings.auto_rotate) {
        o = imu_upright(app->landscape, gfx_orient(), 0.25f);
    } else if (orient_is_landscape(gfx_orient()) == app->landscape) {
        o = gfx_orient();
    }
    gfx_set_orient(o);
    input_flush();
    imu_shake_ticks(false);  // an app with a shake turns the ticks on in its enter()
    sysui_panel_reset(t);    // (the panel goes; the grip shows for 4 s)
#if BOARD_UI_TABLET
    hb_switched(app, t);  // (the home bar: brighter for a while; its card goes on into the menu)
#endif
    s_frozen_ms = s_frozen_at = 0;
    input_time_shift(0);
    s_own = OWN_NONE;
    s_own_down = false;
    snprintf(s_arg, sizeof s_arg, "%s", s_next_arg);
    s_next_arg[0] = 0;
    notify_center_close();  // (an app opens: the notification center goes)
    app->enter(t);
    notify_seen_app(app->name);  // (the badge of the app goes)
    ESP_LOGI(TAG, "open %s%s%s", app->name, s_arg[0] ? " at " : "", s_arg);
}

// In the menu, follow the board when the user turns it over.
static void menu_auto_rotate(int64_t t)
{
    static int64_t since;
    if (mai_app != &app_menu || !g_settings.auto_rotate || BOARD_FIXED_ORIENT || sysui_panel_on()) {
        since = 0;
        return;
    }
    orient_t want = imu_upright_any(gfx_orient(), 0.5f);  // also portrait: the vertical menu
    if (want == gfx_orient()) {
        since = 0;
    } else if (since == 0) {
        since = t;
    } else if (t - since >= ROTATE_HOLD_MS) {
        gfx_set_orient(want);
        input_flush();
        since = 0;
    }
}

// The panel begins to show: the app loses the touch that it has now (an IN_UP at once; the fingers that are down go to
// nobody until they go up, so that a tap on the panel gets the events); an app that stops gets one more render (the
// picture under the panel), and its two drones stop (as the screen saver does).
static void panel_start(int64_t t)
{
    input_cut();
    if (s_own == OWN_APP && s_own_down) {
        input_event_t up = {.kind = IN_UP, .x = s_own_x, .y = s_own_y, .t_ms = t - s_frozen_ms};
        mai_app->input(&up);
        s_own = OWN_DROP;
    }
    if (!mai_app_runs()) {
        gfx_clip_reset();
        gfx_hi_frame(false);  // (the Tab5: the copy under the panel needs the normal frame)
        mai_app->render(t - s_frozen_ms);
        sysui_panel_snap();
        audio_drone(0, 0, 0, W_SAW);
        audio_drone(1, 0, 0, W_SAW);
    }
}

static void main_frame(void);
static bool s_backlight_on;  // (the backlight comes on after the first frame)
static int s_frames;
static int64_t s_fps_t0;

// Each memory request that fails goes to the log, with its size and its kind of memory (it helps to find the
// biggest block that the BLE start needs). It only prints (esp_rom_printf): it can run in any task.
static void alloc_failed(size_t size, uint32_t caps, const char *fn)
{
    esp_rom_printf("ALLOC FAILED: %u bytes, caps 0x%x, %s; internal %u free, %u block\n", (unsigned)size,
                   (unsigned)caps, fn ? fn : "?", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                   (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
}

void app_main(void)
{
    heap_caps_register_failed_alloc_callback(alloc_failed);
    evlog_init();     // the Tab5: the event log (evlog.h); nothing on the other boards
    tab5_kbd_init();  // the Tab5 keyboard (tab5_kbd.h); nothing on the other boards
    device_guard_log_init();
    if (!device_guard_random_init()) {
        abort();
    }
    rfn_early();  // Start the radio after the guard entropy setup.
    gfx_init();
    settings_load();
    web_auth_init();  // the web password (before the WiFi starts the web server)
    friends_init();
    walkie_call_init();  // WALKIE calls over the LAN and the VPN
    vpn_init();
    ha_init();
    stocks_init();
    board_init();
    layout_boot();  // the change of the flash layout over the air (main/layout.c; it can mount /data)
    if (!storage_ok()) {
        storage_init();
    }
    store_make_room();  // (the NVS keeps room for the Bluetooth pairings and the WiFi: settings.c)
    if (!device_guard_vault_init()) {
        ESP_LOGE(TAG, "Account vault could not be verified; owner recovery is required");
        gfx_fill(THEME_RECOVERY_BG);
        gfx_text_center(gfx_h / 2 - 12, "OWNER RECOVERY REQUIRED", 1, THEME_RECOVERY_TITLE);
        gfx_text_center(gfx_h / 2 + 12, "CONNECT YOUR OWNER PC", 1, THEME_RECOVERY_TEXT);
        board_lcd_flush(gfx_swap(), gfx_orient() | gfx_frame_bits());
        board_backlight(150);
        console_init();
        // Keep authenticated USB repair available. Apps, Wi-Fi and generic
        // file viewers must not run when the vault migration fails.
        for (;;) {
            device_guard_tick();
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    audio_init();
    imu_init();
    input_init();
    settings_apply();
    usb_audio_boot();    // SOUND TO USB (the setting): the board as a USB sound input
    board_backlight(0);  // on after the first frame

    for (size_t i = 0; i < sizeof(ALL_APPS) / sizeof(ALL_APPS[0]); i++) {
        if (ALL_APPS[i]->init) {
            ALL_APPS[i]->init();
        }
    }
    notify_init();   // the notices of /data/notify.bin (notify.h)
    standby_init();  // STANDBY: the settings and the last weather (standby.h)
    tfn_init();      // TFN OS: the installed apps
    watch_init();    // the background checks of the app files (watch.h) (their init() runs when each one loads)
    ESP_LOGI(TAG, "board V%d, volume %d, brightness %d, internal RAM free %u", (int)board_version(), g_settings.volume,
             g_settings.brightness, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    console_init();
    if (!rfn_radio_boot()) {  // (a radio start: the receiver belongs to the radio, no WiFi)
        wifi_autoconnect();
        vpn_start_task();
    }
    tfn_id_init();  // the device identity (tfn_id.h): at the first start, a short task makes the key after the WiFi start
    tfn_pool_init();  // the pool (tfn_pool.h): the roster file and the NVS secrets (no pool: as before)

    vTaskDelay(pdMS_TO_TICKS(20));  // first IMU samples
    switch_app(&app_menu, tfn_now_ms());
    sysui_splash();  // the start picture, when the manager set one
    if (tfn_safe_mode()) {
        sysui_message("SAFE MODE: THE INSTALLED APPS DO NOT LOAD UNTIL THE NEXT START", 10);
    }
    const app_t *start_app = menu_start_app();  // the startup app of the profile (the manager)
    if (start_app) {
        switch_app(start_app, tfn_now_ms());
    }
    if (rfn_radio_boot() && rfn_boot_app()[0] && app_find(rfn_boot_app())) {
        switch_app(app_find(rfn_boot_app()), tfn_now_ms());
    }
    mai_active_at = mai_touch_at = mai_hand_at = tfn_now_ms();
    imu_gravity_phys(&mai_ref_g[0], &mai_ref_g[1], &mai_ref_g[2]);
    power_init();
    alarm_init();  // the alarms of CLOCK and the clock chip (alarm.h)
    oref_init();   // HOME FRONT ALERTS (oref.h; off by default)

    s_fps_t0 = tfn_now_ms();
    for (;;) {
        main_frame();
    }
}

// The screen saver part of a frame: a touch or a key wakes the screen; with the screen off, the keys, a call and the
// background work of the app. Returns true when the frame ends here (the screen is off).
static bool frame_saver(int64_t t)
{
    if (mai_saver != SAVER_ON) {
        // a touch wakes the screen and does nothing else
        input_event_t ev;
        bool touched = false;
        while (input_get(&ev)) {
            touched = true;
        }
        int wk, wm;
        bool keyed = false;
        while (input_key_pop(&wk, &wm)) {
            keyed = true;
        }
        if (touched || keyed) {
            mai_touch_at = mai_hand_at = t;
            mai_wake(t, keyed ? "a key" : "a touch");
            input_flush();
        }
    }
    if (mai_saver == SAVER_OFF) {
        mai_pwr_key_poll(t);
        mai_boot_key_poll(t);
        walkie_call_update(t);                       // (a call or a WAKE: it wakes the screen, opens WALKIE / TALKIE)
        radio_play_update(t, mai_app == &app_menu);  // (a radio invite also while the screen is off)
        if (s_next_app) {                            // (the web page opens WALKIE with the screen off)
            const app_t *a = s_next_app;
            s_next_app = NULL;
            switch_app(a, t);
        }
        bool bg = mai_app_background();
        if (bg != mai_bg_on || webcam_active() != mai_cam_on) {
            mai_deep_saver(true);  // the app starts or stops its background work
        }
        if (power_tick(t)) {  // POWER OFF (SETUP, the manager): long in standby on the battery
            settings_save();
            board_power_off();
        }
        if (bg) {
            // no drawing while the screen is off; the app clock; an app that stops under the panel stays stopped
            if (mai_app->background && mai_app->background() && !(sysui_panel_on() && !mai_app_runs())) {
                mai_app->update(t - s_frozen_ms);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        } else {
            vTaskDelay(pdMS_TO_TICKS(power_x() & PWX_LOOP ? 250 : 100));  // (fewer wakes: the keys, the motion)
        }
        return true;
    }
    return false;
}

// The settings panel and the clock of the app: the pull, the fingers that the app sees, a radio link. Returns true
// when the app stops (under the panel, the notices or the card).
static bool frame_panel(int64_t t)
{
    menu_auto_rotate(t);

    // the settings panel: the zone, a pull and its finger, the motion (sysui.c)
#if BOARD_UI_TABLET
    int pev = sysui_panel_tick(t, mai_pull_allow() && !hb_busy());  // (no pull while the home bar has the finger)
#else
    int pev = sysui_panel_tick(t, mai_pull_allow());
#endif
    if (pev == PANEL_START) {
        standby_hide();  // (the panel comes over the menu, not over STANDBY)
        panel_start(t);
    } else if (pev == PANEL_END) {
        mai_key_presses = 0;  // (the BOOT key in the panel is not the app's)
        input_cut();          // (a finger on the panel when it closes is not the app's)
    }
#if BOARD_UI_TABLET
    input_hide(sysui_panel_on() || hb_card());  // (the app sees no finger under the panel or the card)
#else
    input_hide(sysui_panel_on());  // (the app sees no finger while the panel shows)
#endif
    if (link_ready() && !s_link_was && s_frozen_ms) {
        s_frozen_ms = 0;  // (a radio link starts: the app clock = the time again)
        input_time_shift(0);
    }
    s_link_was = link_ready();
    // (the notification center as the panel: an app with app_t.run or in a match runs on: HOURGLASS kept its time
    // stopped, and a match ended on the other board after 3 s)
#if BOARD_UI_TABLET
    const bool frozen =
            (sysui_panel_on() || notify_center_on() || hb_card() || standby_on()) && !mai_app_runs();  // (and the card)
#else
    const bool frozen = (sysui_panel_on() || notify_center_on() || standby_on()) && !mai_app_runs();
#endif
    if (frozen && !s_frozen_at) {
        s_frozen_at = t;
    }
    if (!frozen && s_frozen_at) {
        s_frozen_ms += t - s_frozen_at;  // the app clock goes on from where it stopped
        s_frozen_at = 0;
        input_time_shift(s_frozen_ms);
    }
    return frozen;
}

// THE KEYS of a keyboard (the Tab5 keyboard, tab5_kbd.c): IN_KEY to the app (input.h). Ctrl+Esc: back to the menu (as
// the PWR key). The apps with app_t.wheel (not the menu: it has its own keys) also get the arrows as wheel steps and
// Enter as a wheel push. The panel or the notices: Esc closes them.
static void frame_keys(int64_t t)
{
    int key, mods;
    while (input_key_pop(&key, &mods)) {
        mai_active_at = mai_hand_at = t;
        if (standby_on()) {
            standby_hide();  // (a key: the menu)
            continue;
        }
        if (sysui_panel_on() || notify_center_on()) {
            if (key == INK_ESC) {
                if (notify_center_on()) {
                    notify_center_close();
                } else {
                    sysui_panel_key(SK_BOOT);
                }
            }
            continue;
        }
        if (key == INK_ESC && (mods & INM_CTRL)) {
            sfx_ui(SFX_BACK);
            if (mai_app != &app_menu) {
                app_open(&app_menu);
            } else {
                menu_back();
            }
            continue;
        }
        if (mai_app->wheel && mai_app != &app_menu) {
            if (key == INK_UP || key == INK_LEFT) {
                input_wheel_add(-1);
            } else if (key == INK_DOWN || key == INK_RIGHT) {
                input_wheel_add(1);
            } else if (key == INK_ENTER) {
                input_wheel_press_add();
            }
        }
        input_event_t ke = {.kind = IN_KEY, .x = (int16_t)key, .y = (int16_t)mods, .t_ms = t - s_frozen_ms};
        mai_app->input(&ke);
        if (s_next_app) {
            break;
        }
    }
}

// The touches of a frame: each touch goes to one part (a message, the notices, the panel, the home bar or the app).
static void frame_touches(int64_t t)
{
    input_event_t ev;
    while (input_get(&ev)) {
        mai_active_at = t;
        mai_touch_at = mai_hand_at = t;
        int lx, ly;
        gfx_touch_to_logical(ev.x, ev.y, &lx, &ly);
        ev.x = (int16_t)lx;
        ev.y = (int16_t)ly;
        if (sysui_input(&ev)) {
            continue;  // a message or the locate sound takes the touch
        }
        if (!sysui_panel_on() && s_own != OWN_PANEL && notify_input(&ev)) {
            continue;  // the banner, the center
        }
        if (!sysui_panel_on() && s_own != OWN_PANEL && standby_input(&ev)) {
            continue;  // STANDBY (standby.h)
        }
        if (ev.kind == IN_DOWN) {
            s_own = sysui_panel_on() ? OWN_PANEL : OWN_APP;
            s_own_down = true;
        } else if (ev.kind == IN_UP) {
            s_own_down = false;
        }
        if (s_own == OWN_PANEL || sysui_panel_on()) {
            sysui_panel_input(&ev);  // the panel takes all touches while it shows
            continue;
        }
#if BOARD_UI_TABLET
        if ((s_own == OWN_APP || s_own == OWN_DROP) && hb_event(&ev)) {
            continue;  // (the home bar)
        }
#endif
        if (s_own == OWN_DROP) {
            continue;  // (the rest of a touch that the panel cut)
        }
        s_own_x = ev.x;
        s_own_y = ev.y;
        ev.t_ms -= s_frozen_ms;  // (the app clock)
        mai_app->input(&ev);
        if (s_next_app) {
            break;  // the rest of the events belong to the old app
        }
    }
}

#if !CONFIG_TFN_SELFTEST
// The layers of a frame over the app: the panel, the notices, the home bar, the messages, the overlays (at: the app
// clock; frozen: the app stops).
static void frame_layers(int64_t t, int64_t at, bool frozen)
{
    radio_play_update(t, mai_app == &app_menu);  // (PLAY BY RADIO, the radio invite: radio_play.h)
    sysui_update(t);
    notify_tick(t);
    gfx_clip_reset();
#if BOARD_HI_FRAME
    // the HI frame (gfx.h) only when no system layer draws over the app (they keep copies of the frame)
#if BOARD_UI_TABLET
    gfx_hi_frame(!sysui_panel_on() && !notify_center_on() && !notify_banner_on() && !sysui_active() && !hb_busy() &&
                 !standby_on());
#else
    gfx_hi_frame(!sysui_panel_on() && !notify_center_on() && !notify_banner_on() && !sysui_active() && !standby_on());
#endif
#endif
    if (sysui_panel_on()) {
        const bool redraw = sysui_panel_redraw();
        if (!frozen) {
            mai_app->render(at);  // (an app that runs: its live frame under the panel)
        } else if (redraw) {      // (an item of the panel changed the app: the picture under it again)
            gfx_clip_reset();
            mai_app->render(at);
            sysui_panel_snap();
        }
        sysui_panel_draw(!frozen);
    } else if (notify_center_on()) {
        notify_draw(t);  // the notification center (the whole frame)
    } else if (standby_on() && !sysui_active()) {
        standby_draw(t);  // STANDBY: the whole frame (standby.h)
        notify_draw(t);   // the banner of a new notice
    } else if (!sysui_active()) {
#if BOARD_UI_TABLET
        const bool card = hb_card();  // (the home bar has the finger: the app stops under its card)
        if (!card || !frozen || s_hb.snap_need) {
            mai_app->render(at);
        }
        if (!card) {
            sysui_grip_draw(t);
            notify_draw(t);
        }
        hb_draw(t, !frozen);  // the bar, the card, the card that goes into the menu
        if (hb_busy()) {
            menu_redraw_all();  // (the menu draws only what changes: all of it after the card)
        }
#else
        mai_app->render(at);  // (while a message shows, the layer draws the whole frame)
        sysui_grip_draw(t);   // the grip of the panel (4 s after an app opens)
        notify_draw(t);       // the banner of a new notice
#endif
    }
    sysui_draw();          // a message from the manager, the locate sound
    web_walkie_overlay();  // "WEB LISTENS" while a browser hears the microphone
    webcam_overlay();
    ota_overlay();  // the firmware update panel
    shot_copy();
}
#endif

// The end of a frame: the backlight after the first frame, the keys, the power report, the frame rate in the log.
static void frame_end(int64_t t)
{
    if (!s_backlight_on) {
        board_backlight(g_settings.brightness);
        s_backlight_on = true;
    }

    mai_pwr_key_poll(t);
    mai_boot_key_poll(t);
#if CONFIG_TFN_SELFTEST
    mai_selftest(t);
#endif

    if (power_tick(t)) {  // (the samples of the power report; POWER OFF from the manager)
        settings_save();
        board_power_off();
    }
    if (++s_frames == 300) {
        int64_t t1 = tfn_now_ms();
        s_app_fps = (float)(300000.0 / (double)(t1 - s_fps_t0));
        ESP_LOGI(TAG, "%s: %.1f fps", mai_app->name, (double)s_app_fps);
        s_fps_t0 = t1;
        s_frames = 0;
    }
}

// One frame of the app task: the screen saver, the keys, the settings panel, the touches, the app, the layers, the LCD.
static void main_frame(void)
{
#if BOARD_EVLOG
    // (the event log: a frame of more than 150 ms gets a line with its parts; the cyan flicker came after a wake with
    // 2 s of no frames)
    const int64_t fr0 = esp_timer_get_time();
    int64_t fr1 = 0, fr2 = 0, fr3 = 0;
#endif
    device_guard_tick();
    {
        int64_t t = tfn_now_ms();
        tfn_tick();  // TFN OS: a scan of the installed apps that another task asked for

        if (mai_wake_req) {
            mai_wake_req = false;
            mai_touch_at = mai_hand_at = t;
            mai_wake(t, "the web page");
        }
        mai_check_motion(t);
        mai_saver_update(t);
        ota_tick(t);
        layout_tick(t);
        bt_pad_tick(t);  // (the BLE stack stops after the app that started it: its RAM comes back)
        oref_tick(t);    // HOME FRONT ALERTS: the settings, an alert, the siren (also with the screen off)
        standby_tick(t,
                     mai_saver == SAVER_ON    ? 2
                     : mai_saver == SAVER_DIM ? 1
                                              : 0,
                     mai_hand_at);  // STANDBY (standby.h)
        if (frame_saver(t)) {
            return;
        }
        const bool frozen = frame_panel(t);
        frame_keys(t);
        frame_touches(t);
        if (s_next_app) {
            const app_t *a = s_next_app;
            s_next_app = NULL;
            switch_app(a, t);
        }
        if (!g_shot_req && standby_skip(t)) {  // (a picture of the screen waits for a new frame)
            frame_end(t);                      // (STANDBY: no new frame is due; the LCD keeps the last one)
            vTaskDelay(pdMS_TO_TICKS(20));
            return;
        }

#if CONFIG_TFN_SELFTEST
        static int64_t pu, pr, pf;
        static int pn;
        int64_t p0 = esp_timer_get_time();
        mai_app->update(t);
        int64_t p1 = esp_timer_get_time();
        gfx_clip_reset();
        mai_app->render(t);
        web_walkie_overlay();
        webcam_overlay();
        ota_overlay();
        int64_t p2 = esp_timer_get_time();
        board_lcd_flush(gfx_swap(), gfx_orient() | gfx_frame_bits());
        int64_t p3 = esp_timer_get_time();
        pu += p1 - p0;
        pr += p2 - p1;
        pf += p3 - p2;
        if (++pn == 100) {
            ESP_LOGI(TAG, "%s: update %d us, render %d us, flush %d us", mai_app->name, (int)(pu / 100),
                     (int)(pr / 100), (int)(pf / 100));
            pu = pr = pf = 0;
            pn = 0;
        }
#else
        const int64_t at = t - s_frozen_ms;  // the app clock
#if BOARD_EVLOG
        fr1 = esp_timer_get_time();
#endif
        if (!frozen) {
            mai_app->update(at);
        }
#if BOARD_EVLOG
        fr2 = esp_timer_get_time();
#endif
        frame_layers(t, at, frozen);
#if BOARD_EVLOG
        fr3 = esp_timer_get_time();
#endif
        board_lcd_flush(gfx_swap(), gfx_orient() | gfx_frame_bits());
#endif
#if BOARD_EVLOG
        {
            const int64_t fr4 = esp_timer_get_time();
            if (fr1 && fr4 - fr0 > 150000) {
                evlog_add("t5: slow frame %d ms (%s): before the app %d, update %d, draw %d, flush %d",
                          (int)((fr4 - fr0) / 1000), mai_app->name, (int)((fr1 - fr0) / 1000),
                          (int)((fr2 - fr1) / 1000), (int)((fr3 - fr2) / 1000), (int)((fr4 - fr3) / 1000));
            }
        }
#endif

        frame_end(t);

        vTaskDelay(1);
    }
}
