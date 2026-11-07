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
// The light sleep in STANDBY (on by default; the console and /api/power can turn it off).
bool power_light_sleep(void);
void power_set_light_sleep(bool on);
