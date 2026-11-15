#pragma once

// The power of the board (three modes):
//  1. ON (the screen on or dim): the CPU at full speed (a PM lock: 240 MHz).
//  2. STANDBY (the screen off): the CPU at 80 MHz. With no background app also the automatic light sleep: the WiFi
//     stays connected (the calls and the WAKE of the manager come as UDP over the LAN / VPN), the CPU sleeps between
//     the wakes of the radio. main.c also stops the sound (audio_sleep), slows the IMU (imu_slow) and the touch.
//  3. OFF (optional: the setting POWER OFF, 0 = never): after that long in STANDBY with no background app and on the
//     battery, board_power_off() (a real power off on the battery; a key starts the board again).
// The report (power_json, /api/power, the console "pw"): the time in each mode, the wakes by cause, the CPU idle
// share of each core in each mode, the light sleep share.

#include <stdbool.h>
#include <stdint.h>

enum {
    PW_ON,
    PW_DIM,
    PW_OFF,
    PW_MODES
};

void power_init(void);
// The screen state (PW_*) and, with PW_OFF, whether an app works in the background (WALKIE paired, the web camera).
void power_mode(int mode, bool background);
// A wake of the screen (main.c wake()): counted by its cause (a fixed string).
void power_wake(const char *why);
// Each loop of main.c: the samples of the report; with the screen off and no background work: the POWER OFF timer.
// Returns true when the board must power off now.
bool power_tick(int64_t now_ms);
// true = no power off after the STANDBY time (app_standby_hold of the open app); POWER OFF of the manager still works
void power_hold(bool on);
// the same for an armed alarm in the next 24 hours (alarm.c): the board must be on to ring
void power_alarm_hold(bool on);
void power_oref_hold(bool on);  // oref.c: HOME FRONT ALERTS watch (no automatic POWER OFF)
// POWER OFF now, from the manager (/api/ctl?poweroff=1): the next power_tick() gives true
void power_off_ask(void);
// true = the board runs on its battery (no charger, no PC). false = external power, or not known yet (then the board
// keeps the normal behaviour). The AXP2101 boards read VBUS; the 3.49 uses the trend of the voltage in STANDBY.
bool power_on_battery(void);
// The report as JSON (returns the length).
int power_json(char *b, int n);
// One line for the console.
void power_log(void);

// POWER OFF: the minutes in STANDBY before the board powers off (0 = never). POWER_OFF_CHOICES values.
#define POWER_OFF_CHOICES 6
extern const uint16_t POWER_OFF_MIN[POWER_OFF_CHOICES];
extern const char *const POWER_OFF_NAMES[POWER_OFF_CHOICES];
int power_off_choice(void);
void power_set_off_choice(int i);
// DEEP IDLE: only on the battery (the voltage falls, or no VBUS) AND under 80 % (power.c save_ok()), in the MENU in STANDBY with no background work for
// 1 min, the WiFi radio stops. Each POWER_DEEP_S[i] seconds it starts for a short check: the board connects, sends its
// beacons (with the DEEP flag), and stays on while the manager reads it or sends work (an OTA, an install), while a
// watcher (mail) is due, and while a call comes. The touch, the IMU and the main loop also read less often. A key
// always wakes the screen (the radio starts at once). OFF = the old STANDBY. The default is 60 s (fewer reconnects: each check costs about 4.5 s of radio). A battery test on a
// 3.49: the radio is about 60 % of the STANDBY drain.
#define POWER_DEEP_CHOICES 4
extern const uint16_t POWER_DEEP_S[POWER_DEEP_CHOICES];
extern const char *const POWER_DEEP_NAMES[POWER_DEEP_CHOICES];
int power_deep_choice(void);
void power_set_deep_choice(int i);
bool power_deep_now(void);  // DEEP IDLE acts now (the radio is off, or on for a check)
// The light sleep in STANDBY (on by default; the console and /api/power can turn it off).
bool power_light_sleep(void);
void power_set_light_sleep(bool on);

// The power test switches (/api/power?x=<bits>&xm=<min>; RAM only, 0 after a reset). They act only in
// STANDBY with no background app: PWX_TOUCH = the touch read each 500 ms (not 50), PWX_IMU = the IMU read once a
// second (not 25 times), PWX_LOOP = the main loop each 0.25 s (not 0.1 s), PWX_RADIO = the WiFi radio stops for xm
// minutes (then it starts and connects again; the bit clears itself; a screen wake also ends it). DEEP IDLE sets
// PWX_TOUCH, PWX_IMU and PWX_LOOP while it acts (power_x() gives them).
enum {
    PWX_TOUCH = 1,
    PWX_IMU = 2,
    PWX_LOOP = 4,
    PWX_RADIO = 8,
};
int power_x(void);  // the test bits that act now (0 with the screen on or a background app)
void power_set_x(int bits, int minutes);
