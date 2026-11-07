// The screen saver of TFN OS: the dim and the off of the screen, the deep saver (the screen off), the wake, the
// motion and the radio link that keep the screen on, the API of the screen (app.h).
#include "main_priv.h"
#include "tfn_web.h"

static const char *TAG = "main";

int mai_saver = SAVER_ON;
int64_t mai_active_at;
int64_t mai_touch_at;  // the last touch, key or movement (the limit of app_t.awake)
int64_t mai_hand_at;   // the last touch or key, not a movement (the limit of WAKE ON MOVE OFF)
bool mai_key_off;      // the screen went off by the key: no app keeps it on or wakes it (wake() clears it)
bool mai_cam_on;       // the web camera was on at the last deep_saver() (full WiFi)
bool mai_bg_on;        // the screen is off and the app runs in the background
float mai_ref_g[3];

static volatile int s_saver_req;  // app_saver_now(): 1 = dim, 2 = off (the main loop does it)
void app_saver_now(bool off)
{
    s_saver_req = off ? 2 : 1;
}

volatile bool mai_wake_req;  // app_screen_wake() (the web server task; the main loop does it)
void app_screen_wake(void)
{
    mai_wake_req = true;
}

void app_standby_hold(bool on)
{
    power_hold(on);
}

const char *app_screen_state(void)
{
    return mai_saver == SAVER_ON ? "on" : (mai_saver == SAVER_DIM ? "dim" : "off");
}

int mai_pocket_ms;  // app_screen_pocket: 0 = the normal screen saver
void app_screen_pocket(int ms)
{
    mai_pocket_ms = ms > 0 ? ms : 0;
}

// the screen off: the LCD sleeps, the amplifier is off, the touch panel and WiFi save power, the sound stops, the IMU
// reads 25 times a second (accelerometer only), the CPU goes to 80 MHz (and to light sleep: power.c).
// An app in the background (app_t.background) keeps the amplifier, the sound and the full WiFi (no light sleep).
bool mai_app_background(void)
{
    return (mai_app && mai_app->background && mai_app->background()) || web_audio_active();
}
static bool s_net;  // a transfer runs (mai_net_update): with the screen off, no light sleep
void mai_deep_saver(bool on)
{
    mai_bg_on = on && mai_app_background();
    mai_cam_on = webcam_active();
    board_speaker_hold(mai_bg_on);
    board_screen_sleep(on);
    input_slow(on);
    wifi_saver(on && !mai_bg_on && !mai_cam_on);
    audio_sleep(on && !mai_bg_on);
    imu_slow(on);
    power_mode(on ? PW_OFF : PW_ON, mai_bg_on || mai_cam_on || (on && s_net));
}

// A transfer runs: an OTA, an install (also a store download, a move, a remove) or a web upload of a package. The
// screen saver must not stop it: with the screen off the WiFi has the most power save, and the data of an OTA stopped
// for 60 s. The hold stays NET_HOLD_MS after the transfer, so the installs of a list (one after the other) keep one
// hold. While it runs: no WiFi power save (wifi_xfer), and with the screen off no light sleep (power_mode).
#define NET_HOLD_MS 5000
static void mai_net_update(int64_t t)
{
    static int64_t s_until;
    if (ota_busy() || tfn_inst_busy() || tfn_web_upload_busy()) {
        s_until = t + NET_HOLD_MS;
    }
    const bool net = t < s_until;
    if (net == s_net) {
        return;
    }
    s_net = net;
    wifi_xfer(net);
    if (mai_saver == SAVER_OFF) {
        mai_deep_saver(true);  // (the light sleep and the power off of the standby: off while it runs)
    }
}

void mai_wake(int64_t t, const char *why)
{
    mai_active_at = t;
    mai_key_off = false;
    if (mai_saver != SAVER_ON) {
        if (mai_saver == SAVER_OFF) {
            mai_deep_saver(false);
        }
        mai_saver = SAVER_ON;
        power_mode(PW_ON, false);
        power_wake(why);
        board_backlight(g_settings.brightness);
        ESP_LOGI(TAG, "screen on: %s", why);
    }
}

// the board was picked up, turned or shaken; music plays
void mai_check_motion(int64_t t)
{
    if (mai_pocket_ms) {
        return;  // (the pocket mode: a movement is no activity)
    }
    float g[3];
    imu_gravity_phys(&g[0], &g[1], &g[2]);
    // the direction only: the size of the vector changes slowly while the board is still (the AUTO accelerometer
    // scale, the filter at 25 Hz), and a test of the size could wake a still board ("gravity change 0.121 g, shake 0.01 g")
    float gn = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
    float rn = sqrtf(mai_ref_g[0] * mai_ref_g[0] + mai_ref_g[1] * mai_ref_g[1] + mai_ref_g[2] * mai_ref_g[2]);
    if (gn < 0.3f || rn < 0.3f) {
        gn = rn = 1.0f;  // (no reading yet: the plain difference)
    }
    float dx = g[0] / gn - mai_ref_g[0] / rn, dy = g[1] / gn - mai_ref_g[1] / rn, dz = g[2] / gn - mai_ref_g[2] / rn;
    bool moved = dx * dx + dy * dy + dz * dz > MOVE_G * MOVE_G;
    if (moved) {
        mai_ref_g[0] = g[0];
        mai_ref_g[1] = g[1];
        mai_ref_g[2] = g[2];
    }
    if (moved || imu_shake() > 0.4f) {
        // WAKE ON MOVE OFF (SETUP, store "wake_mv" 0): a move wakes no dim or dark screen, and it keeps a lit screen on
        // only within MOVE_KEEP_MS of the last touch or key, so a board in a bag goes dark (a tilt game still plays)
        if (!store_get("wake_mv", 1) && (mai_saver != SAVER_ON || t - mai_hand_at > MOVE_KEEP_MS)) {
            return;
        }
        mai_touch_at = t;
        static int64_t s_mlog;
        if (t - s_mlog > 2000) {  // the motion is activity: a log at most each 2 s
            s_mlog = t;
            ESP_LOGI(TAG, "motion: gravity change %.3f g, shake %.2f g", sqrtf(dx * dx + dy * dy + dz * dz),
                     imu_shake());
        }
        mai_wake(t, "motion");
    } else if (!mai_key_off && (g_music.playing || (mai_app && mai_app->awake && mai_app->awake())) &&
               t - mai_touch_at < AWAKE_MAX_MS) {
        mai_wake(t, "an awake app");
    }
}

// A RADIO LINK KEEPS THE SCREEN ON only when another board is really there (CHESS, SEA BATTLE and
// TEAM BUILDING have the radio on for friends all the time that they are open, so the radio alone is no reason): a 2 BOARDS
// game, a match of the turn games, the own link of an app with another board (app_in_match), a lobby with a board to
// choose (net2), MORSE. Solo play with the radio only listening lets the screen saver work. (With the screen off the app
// does not run, so a linked board would send nothing and the other board would end the link.)
static bool radio_peer(void)
{
    if (!link_ready() || !mai_app) {
        return false;
    }
    // a STANDING match of CHESS goes on after a screen sleeps (AWAY, then REJOIN): only a join keeps the screen on
    if (mai_app_is("CHESS")) {
        return duel_state() == DUEL_JOIN;
    }
    if (mai_app_is("MORSE")) {
        return true;  // (MORSE takes any board, also over the LAN: its link keeps it on)
    }
    if (app_in_match()) {
        return true;  // (a game of 2 BOARDS, a turn game, the own link of an app)
    }
    if (net2_role() == N2_LOBBY) {
        return net2_lobby_choosing();
    }
    return false;  // (solo play with the radio only listening: the screen saver works)
}

// A LOBBY THAT WAITS FOR A FRIEND (friends_lobby_waits: a 2 BOARDS lobby, the invite of a turn game, an open table):
// the screen can dim, but it does not go off for LOBBY_KEEP_MS after the last touch or the start of the wait. With the
// screen off the app does not run: the lobby would send nothing, and a friend who opens the game later would not find
// it. When the friend comes, the match wakes the screen (radio_peer).
#define LOBBY_KEEP_MS (5 * 60 * 1000)
static bool lobby_keep(int64_t t)
{
    if (mai_key_off || !link_ready() || !friends_lobby_waits()) {
        return false;
    }
    return t - mai_hand_at < LOBBY_KEEP_MS || friends_lobby_age() < LOBBY_KEEP_MS;
}

void mai_saver_update(int64_t t)
{
    mai_net_update(t);
    // 2 BOARDS (a game, a lobby with a board): the screen stays on (radio_peer)
    if (!mai_key_off && radio_peer() && !(mai_app && mai_app->background)) {  // (WALKIE runs with the screen off)
        mai_active_at = t;
        if (mai_saver != SAVER_ON) {
            mai_wake(t, "a radio link");
        }
    }
    // an app that keeps the screen on (SCENES with its SCREEN SAVER set to OFF); not while it is stopped under the
    // panel (its game clock does not run then, so it would keep the screen on until the battery is empty)
    if (!mai_key_off && mai_app && mai_app->keep_on && !(sysui_panel_on() && !mai_app_runs()) && mai_app->keep_on()) {
        mai_active_at = t;
        if (mai_saver != SAVER_ON) {
            mai_wake(t, "the app keeps the screen on");
        }
    }
    uint32_t timeout = SLEEP_SECONDS[g_settings.sleep] * 1000u;
    if (mai_pocket_ms) {
        timeout = (uint32_t)mai_pocket_ms;  // (the pocket mode of the app: its own time)
    }
    if (s_saver_req && (mai_saver == SAVER_ON || (mai_saver == SAVER_DIM && s_saver_req == 2))) {
        if (timeout == 0) {
            timeout = 30000;
        }
        mai_active_at = t - timeout - 1 - (s_saver_req == 2 ? DIM_TO_OFF_MS : 0);
        mai_touch_at = t - AWAKE_MAX_MS - 1;
        if (s_saver_req == 2) {
            mai_saver = SAVER_DIM;  // (then off below)
        }
    }
    s_saver_req = 0;
    if (timeout == 0) {
        return;
    }
    int64_t idle = t - mai_active_at;
    const bool keep = lobby_keep(t);
    if (mai_saver == SAVER_ON && idle > timeout && !(keep && g_settings.brightness <= DIM_LOW_LEVEL)) {
        if (g_settings.brightness <= DIM_LOW_LEVEL) {
            mai_saver = SAVER_OFF;
            board_backlight(0);
            audio_drone(0, 0, 0, W_SAW);
            audio_drone(1, 0, 0, W_SAW);
            mai_deep_saver(true);
        } else {
            mai_saver = SAVER_DIM;
            power_mode(PW_DIM, false);
            int dim = g_settings.brightness / 6;
            board_backlight((uint8_t)(dim < 8 ? 8 : dim));
        }
        ESP_LOGI(TAG, "screensaver: %s", mai_saver == SAVER_OFF ? "off" : "dim");
    } else if (mai_saver == SAVER_DIM && idle > timeout + DIM_TO_OFF_MS && !keep) {
        mai_saver = SAVER_OFF;
        board_backlight(0);
        audio_drone(0, 0, 0, W_SAW);
        audio_drone(1, 0, 0, W_SAW);
        mai_deep_saver(true);
        ESP_LOGI(TAG, "screensaver: off");
    }
}
