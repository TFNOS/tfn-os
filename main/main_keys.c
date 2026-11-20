// The keys of the board: the BOOT key (CHAT, the key of an app, the screen key), the PWR key (back to the menu,
// the power off; the wheel of the Watcher), and the rule that lets a pull from the top open the settings panel.
#include "main_priv.h"
#include "tfn_listen.h"
#include "standby.h"

static const char *TAG = "main";

// TENNIS (app_tennis.c) gives the strong version: true in a match of 2 players on one board (no pull: the top edge is
// the side of player 2). This weak one is for a build without it.
__attribute__((weak)) bool tennis_two_players(void)
{
    return false;
}

static void screen_key(int64_t t, const char *why);
int64_t g_vkey_until_ms;  // (console.c "key ms", tests: the BOOT key is down until this time)
int64_t g_vpwr_until_ms;  // (console.c "key pwr ms", tests: the PWR key)
int mai_key_presses;      // the BOOT key presses for an app with app_t.key (app_key_presses)
static bool s_key_eaten;  // a BOOT press that the panel took: not the app's until the key goes up
static bool boot_key_now(void)
{
    return board_boot_key_down() || esp_timer_get_time() / 1000 < g_vkey_until_ms;
}

// The calm wait of the voice (tfn_speech.h: the main loop draws about 10 frames a second while the voice makes a
// line). A frame reads the keys one time: a short push between two frames was lost (PWR did not go back to the
// menu while WIKI read aloud). The wait reads the keys each 10 ms and ends at a push or a touch; the push stays
// for the next read of the frame (also when the key is up again then).
static volatile bool s_pwr_seen, s_boot_seen;
void mai_calm_wait(int ms)
{
    for (int i = 0; i < ms; i += 10) {
        const bool p = board_pwr_key_down() || esp_timer_get_time() / 1000 < g_vpwr_until_ms, b = boot_key_now();
        if (p || b) {
            s_pwr_seen = s_pwr_seen || p;
            s_boot_seen = s_boot_seen || b;
            return;
        }
        if (input_pending()) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// the PWR key is down now, or a push came in the calm wait
static bool pwr_key_now(void)
{
    const bool d = board_pwr_key_down() || esp_timer_get_time() / 1000 < g_vpwr_until_ms || s_pwr_seen;
    s_pwr_seen = false;
    return d;
}
// The BOOT key of the 3.49: a long press (0.7 s) opens CHAT and listens (in CHAT: the same as a tap). In an app with
// app_t.key (WRONG ROOM: a jump) each press goes to the app, and the key opens no CHAT.
void mai_boot_key_poll(int64_t t)
{
    static int64_t down_at, up_at;
    static bool done, talk;
    bool own = mai_app && mai_app->key;
    const bool seen = s_boot_seen;  // (a push in the calm wait: down for this read)
    s_boot_seen = false;
    if (!boot_key_now() && !seen) {
        // a short press that nothing else took (not the panel, not an app with the key, not the long press of CHAT)
        if (down_at && !done && !own && t - down_at >= PWR_SHORT_MS && store_get("scr_key", 0)) {
            screen_key(t, "the BOOT key");
        }
        if (down_at) {
            up_at = t;
            if (talk) {
                listen_key_up();  // (a short press: the recording goes; a long one: the turn ends)
            }
        }
        down_at = 0;
        talk = false;
        s_key_eaten = false;
        return;
    }
    if (!down_at) {
        down_at = t;
        done = false;
        if (sysui_panel_on() && t - up_at >= 25) {
            done = true;  // the settings panel shows: the key closes it (RESUME)
            s_key_eaten = true;
            sysui_panel_key(SK_BOOT);
            mai_wake(t, "the BOOT key");
        } else if (own && t - up_at >= 25) {  // (a shorter gap is contact bounce)
            mai_key_presses++;
            mai_wake(t, "the BOOT key");
        } else if (!own) {
            talk = listen_key_down();  // THE TALK KEY (tfn_listen.h): it records from the push
        }
    } else if (talk) {
        listen_key_held((int)(t - down_at));
        if (!done && t - down_at >= 700) {
            done = true;  // (the long press talks: no CHAT, no short press)
            mai_wake(t, "the talk key");
        }
    } else if (!done && !own && t - down_at >= 700) {
        done = true;
        mai_wake(t, "the BOOT key");
        chat_open_and_talk();
    }
}
// THE SCREEN KEY: a short press of BOOT (the 3.49) turns the screen off now and the next one on again, as
// the side key of a phone; a short press of PWR at the top level of the menu (where it did nothing) turns it off. An off by the key is the user's: no awake app, keep_on app or radio
// link turns the screen on again, only a touch, a key, a call or (with WAKE ON MOVE) a move.
// It is an option: SETUP > SCREEN KEY (store "scr_key"), OFF by default (the keys act as on a board with no such option).
static void screen_key(int64_t t, const char *why)
{
    if (mai_saver == SAVER_ON) {
        app_saver_now(true);
        mai_key_off = true;
        ESP_LOGI(TAG, "screen off: %s", why);
    } else {
        mai_touch_at = mai_hand_at = t;
        mai_wake(t, why);
    }
}
int app_key_presses(void)
{
    int n = mai_key_presses;
    mai_key_presses = 0;
    return mai_app && mai_app->key ? n : 0;
}
bool app_key_down(void)
{
    return mai_app && mai_app->key && !sysui_panel_on() && !s_key_eaten && boot_key_now();
}

#if BOARD_WHEEL
// The wheel button (the Watcher). A short push = the main action: an app with app_t.wheel reads it
// (input_wheel_presses()); for the other apps it is a tap in the middle of the screen. A hold of
// WHEEL_BACK_MS = the settings panel with MENU in its ring (a push then = back to the menu; in a menu group the hold is
// up one level; in an app with no panel it is back); in the panel a push acts on its ring and a hold = MENU. A hold of
// WHEEL_BACK_MS + PWR_HOLD_MS = power off.
#define WHEEL_BACK_MS 600
void mai_pwr_key_poll(int64_t t)
{
    static bool armed = false, back_done = false;
    static int64_t down_at = 0;
    bool down = pwr_key_now();
    input_wheel_set_down(down && armed && !sysui_panel_on());
    if (down) {
        if (!armed) {
            return;
        }
        mai_touch_at = mai_hand_at = t;
        if (mai_saver != SAVER_ON) {
            mai_wake(t, "the key");  // this push only wakes the screen
            armed = false;
            down_at = 0;
            return;
        }
        mai_active_at = t;
        if (down_at == 0) {
            down_at = t;
            back_done = false;
        } else if (!back_done && t - down_at >= WHEEL_BACK_MS) {
            back_done = true;
            if (sysui_panel_on()) {
                sysui_panel_key(SK_HOLD);  // the panel shows: MENU (in the menu: the panel goes)
            } else if (mai_app == &app_menu && menu_back()) {
                sfx_ui(SFX_BACK);  // in a menu group: up one level
            } else if (mai_pull_allow()) {
                sfx_ui(SFX_CLICK);
                sysui_panel_key(SK_HOLD);  // the settings panel, MENU in the ring
            } else if (mai_app != &app_menu) {
                sfx_ui(SFX_BACK);
                app_open(&app_menu);
            }
        } else if (t - down_at >= WHEEL_BACK_MS + PWR_HOLD_MS) {
            settings_save();
            board_power_off();
        }
        return;
    }
    if (armed && down_at && !back_done && t - down_at >= PWR_SHORT_MS) {
        if (notify_center_on()) {
            notify_center_close();  // (the PWR key closes the notification center)
        } else if (sysui_ask_back()) {
            sfx_ui(SFX_BACK);  // a closable question (the hold question of the menu) goes
        } else if (sysui_panel_on()) {
            sysui_panel_key(SK_PUSH);  // the settings panel: the control in the ring
        } else if (mai_app->wheel) {
            input_wheel_press_add();
        } else {
            // a tap in the middle of the screen (logical points: BOARD_TOUCH_LOGICAL)
            input_event_t e = {.kind = IN_DOWN, .x = (int16_t)(gfx_w / 2), .y = (int16_t)(gfx_h / 2), .t_ms = t};
            input_inject(&e);
            e.kind = IN_UP;
            input_inject(&e);
            e.kind = IN_TAP;
            input_inject(&e);
        }
    }
    armed = true;
    down_at = 0;
}
#else
void mai_pwr_key_poll(int64_t t)
{
    // The key can still be down from the power-on push. Use it after the first release.
    static bool armed = false;
    static int64_t down_at = 0;

    if (pwr_key_now()) {
        if (!armed) {
            return;
        }
        mai_touch_at = mai_hand_at = t;
        if (mai_saver != SAVER_ON) {
            mai_wake(t, "the key");  // this push only wakes the screen
            armed = false;
            down_at = 0;
            return;
        }
        mai_active_at = t;
        if (down_at == 0) {
            down_at = t;
        } else if (t - down_at >= PWR_HOLD_MS) {
            settings_save();
            board_power_off();
        }
        return;
    }
    if (armed && down_at && t - down_at >= PWR_SHORT_MS) {
        if (notify_center_on()) {
            notify_center_close();  // (the PWR key closes the notification center)
        } else if (sysui_ask_back()) {
            sfx_ui(SFX_BACK);  // a closable question (the hold question of the menu) goes
        } else if (standby_on()) {
            sfx_ui(SFX_BACK);
            standby_hide();  // STANDBY: the menu
        } else if (mai_app != &app_menu) {
            sfx_ui(SFX_BACK);
            app_open(&app_menu);
        } else if (menu_back()) {
            sfx_ui(SFX_BACK);  // in the menu: up one level
        } else if (store_get("scr_key", 0)) {
            screen_key(t, "the PWR key");  // at the top level of the menu: the screen off
        }
    }
    armed = true;
    down_at = 0;
}
#endif

// A pull from the top may open the settings panel now: not in the self-test, not with the screen off, not in SETUP (it
// has the sliders), not in TOUCH LAB (it must see each finger), not in a TENNIS match of 2 players on one board.
bool mai_pull_allow(void)
{
#if CONFIG_TFN_SELFTEST
    return false;
#else
    if (!mai_app || mai_saver != SAVER_ON) {
        return false;
    }
#if ARCADE_APP_ON(SETTINGS)
    if (mai_app == &app_settings) {
        return false;
    }
#endif
#if ARCADE_APP_ON(TOUCHLAB)
    if (mai_app == &app_touchlab) {
        return false;
    }
#endif
#if ARCADE_APP_ON(TENNIS)
    if (mai_app == &app_tennis && tennis_two_players()) {
        return false;
    }
#endif
    return true;
#endif
}
