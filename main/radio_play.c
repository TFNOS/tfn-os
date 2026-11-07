// PLAY BY RADIO and the radio invite (see radio_play.h).
#include "radio_play.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "app.h"
#include "friends.h"
#include "lan.h"
#include "link.h"
#include "settings.h"
#include "sysui.h"
#include "wifi.h"

static const char *TAG = "radio";

static bool s_no;            // the user closed the question in this wait of a lobby
static bool s_self;          // this part closes its own question now (not an answer of the user)
static bool s_radio;         // PLAY BY RADIO runs: the board left its WiFi
static const app_t *s_app;   // the app of PLAY BY RADIO
static bool s_link_seen;     // the link of that app was on after PLAY BY RADIO
static int64_t s_radio_at;   // the time of PLAY BY RADIO
static int64_t s_off_at;     // the link is off since this time (0 = it is on)
#define RADIO_START_MS 3000  // a lobby that starts its link after the tap: the time to start it
#define RADIO_OFF_MS   2000  // the link is off for this time: the session ended (an app can stop and start it again)

static void hint_done(int k)
{
    if (k == 1) {
        s_radio = true;
        s_app = app_current();
        s_link_seen = link_ready();
        s_radio_at = esp_timer_get_time() / 1000;
        s_off_at = 0;
        ESP_LOGI(TAG, "PLAY BY RADIO: the board leaves its WiFi for the game (the saved networks stay)");
        wifi_disconnect();
        return;
    }
    if (!s_self) {
        s_no = true;  // (NOT NOW, or a tap out of the buttons: not again in this wait)
    }
}

static void hint_show(void)
{
    const bool he = settings_lang_all() == 1;
    if (he) {
        sysui_ask("החבר שלך ברשת WIFI אחרת?", "הלוח יכול לעזוב את ה-WIFI למשחק ולחזור אליו בסוף", "שחק ברדיו",
                  "לא עכשיו", 0, hint_done);
    } else {
        sysui_ask("YOUR FRIEND IS ON ANOTHER WIFI?",
                  "THE BOARD CAN LEAVE ITS WIFI FOR THE GAME AND JOIN IT AGAIN AFTER", "PLAY BY RADIO", "NOT NOW", 0,
                  hint_done);
    }
    sysui_ask_radio();
    sysui_ask_closable();
    ESP_LOGI(TAG, "the question: the friend on another WiFi?");
}

// PLAY BY RADIO: the end of the 2-board session (the link stops, or the app closes): the board joins its WiFi again
static void radio_tick(int64_t now)
{
    const bool link = link_ready();
    s_link_seen |= link;
    if (link) {
        s_off_at = 0;
    } else if (!s_off_at) {
        s_off_at = now;
    }
    const bool gone = app_current() != s_app;
    const bool ended = s_link_seen ? s_off_at && now - s_off_at > RADIO_OFF_MS : now - s_radio_at > RADIO_START_MS;
    if (!gone && !ended) {
        return;
    }
    s_radio = false;
    s_app = NULL;
    ESP_LOGI(TAG, "PLAY BY RADIO ends (%s): the board joins its WiFi again",
             gone ? "the app closed" : "the link stopped");
    if (wifi_user_off()) {
        wifi_reconnect();
    }
}

// WALKIE and TALKIE wait for calls of friends far away (the LAN, the VPN): no question there
static bool hint_app(void)
{
    const app_t *a = app_current();
    return a && a->name && strncmp(a->name, "WALKIE", 6) && strncmp(a->name, "TALKIE", 6);
}

static void hint_tick(void)
{
    const bool waits = friends_lobby_waits() && hint_app();
    if (!waits) {
        s_no = false;
    }
    int g[1];
    const bool want = radio_hint_want(waits, friends_lobby_age(), wifi_state() == WIFI_CONNECTED,
                                      friends_in_game(g, 1) > 0, link_in_use());
    const bool mine = sysui_ask_is(hint_done);
    if (mine && !want) {
        s_self = true;
        sysui_ask_close();  // (a friend came, the wait ended, or the WiFi went)
        s_self = false;
    } else if (!mine && want && !s_no && !sysui_active()) {
        hint_show();
    }
}

void radio_play_update(int64_t now_ms, bool menu)
{
    link_listen(menu);
    uint8_t mac[6];
    char app[FRIENDS_APP_NAME];
    if (friends_radio_invite_take(mac, app)) {
        ESP_LOGI(TAG, "a radio invite of a friend: %s", app);
        lan_invite_notice(mac, app);
    }
    if (s_radio) {
        radio_tick(now_ms);
        return;
    }
    hint_tick();
}

bool radio_play_on(void)
{
    return s_radio;
}
