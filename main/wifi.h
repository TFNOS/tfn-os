#pragma once
// The OS part of this header: the public part (the API of the apps) is in tfn/wifi.h.
#include "tfn/wifi.h"

#include <stdbool.h>
#include <stdint.h>

// WiFi station, started on demand. When it is connected, a small web server
// lists the files in /data (exports, samples) for download in a browser.

typedef struct {
    char ssid[33];
    int8_t rssi;
    bool secure;
} wifi_ap_t;

// Start the WiFi driver (no connection yet). Returns false if there is not enough memory.
bool wifi_start(void);
// (link.c) the radio is stuck: the driver stop + start (full = also its deinit + init), the channel ch, a connect again
// on the last channel
void wifi_radio_restart(int ch, bool full);
int wifi_rssi(void);          // the signal of the network in use in dBm (-30 strong .. -90 weak); 0 = not connected
const char *wifi_ssid(void);  // the network in use or the saved one

void wifi_scan(void);
bool wifi_scanning(void);
int wifi_scan_results(wifi_ap_t *out, int max);

// Connect; save = keep the name and password in NVS for the next start.
void wifi_connect(const char *ssid, const char *pass, bool save);
void wifi_connect_try(const char *ssid, const char *pass);  // saved only when it connects (SETUP)
void wifi_forget(void);                   // the network in use (or the first saved one); the others stay
void wifi_forget_name(const char *ssid);  // one saved network (the network in use: the board leaves it)
void wifi_forget_all(void);
// Leave the network but keep it saved; no automatic try until wifi_reconnect(), a connect, or the next start-up.
void wifi_disconnect(void);
void wifi_reconnect(void);  // the saved networks again (as at start-up)
bool wifi_user_off(void);   // true after wifi_disconnect()
bool wifi_saved(void);
// The saved networks (up to 10, the last one in use first): the count, the name of number i ("" = none), and the
// password of a saved network (false = not saved).
int wifi_saved_count(void);
const char *wifi_saved_name(int i);
bool wifi_saved_pass(const char *ssid, char *pass, int len);
// AUTO JOIN of a saved network (default on): off = the network stays saved, but the board joins it only by hand (SETUP,
// CONNECT, the console), never at the start or in a new try. set: false when the network is not saved.
bool wifi_saved_auto(const char *ssid);
bool wifi_saved_set_auto(const char *ssid, bool on);
// At start-up: connect with a saved network (with two or more: a scan first, then the first saved network in it), and
// start the hotspot when the hotspot mode asks for it.
void wifi_autoconnect(void);

// ---- hotspot: the board's own open WiFi network, for the web page with no WiFi ----
// OFF: only the START button starts it (until the next start-up).
// AUTO: it starts when no WiFi network is saved or the connection fails, ONE time after each start-up, for 2 min
//       (a phone on it keeps it open; 2 min after the last phone left it stops), and it stops when the WiFi
//       connects (if no phone is on the hotspot).
// ON: always on (together with the WiFi connection).
typedef enum {
    HOTSPOT_OFF,
    HOTSPOT_AUTO,
    HOTSPOT_ON,
    HOTSPOT_MODES
} hotspot_mode_t;
extern const char *const HOTSPOT_MODE_NAMES[HOTSPOT_MODES];
void wifi_hotspot_set_mode(hotspot_mode_t m);  // saved in NVS
hotspot_mode_t wifi_hotspot_mode(void);
void wifi_hotspot_start(void);  // now, in any mode
void wifi_hotspot_stop(void);
bool wifi_hotspot_on(void);
// The screen is off: a WiFi station saves more power (not the hotspot: phones need its radio).
void wifi_saver(bool on);
// A transfer runs (an OTA, an app install or upload): no WiFi power save, also with the screen off (main_saver.c).
// false: the power save of the screen state comes back.
void wifi_xfer(bool on);
// Set the power save of the station for the state now: the screen (wifi_saver), a transfer (wifi_xfer) and the radio
// link (link.c). The hotspot keeps the radio on: then no change.
void wifi_ps_apply(void);
const char *wifi_hotspot_ip(void);  // "192.168.4.1"
int wifi_hotspot_clients(void);     // phones on the hotspot now
int wifi_hotspot_left_s(void);      // the 2-minute hotspot of AUTO: seconds left; -1 = no limit or off

// The header and the menu of the board web page (for other pages of the board).
struct httpd_req;  // (esp_http_server.h)
void wifi_send_page_head(struct httpd_req *req);
// The web routes of wifi.c: the main page, the files of /data, the MIDI upload, the gamepad page (web_server.c).
void wifi_web_register(void *httpd);
