#pragma once

// THE ALARMS OF THE BOARD (main/alarm.c, for the CLOCK app): a small service of the system, so an alarm
// rings also when another app is open or the screen is off. The CLOCK app writes the alarms and the home time zone;
// the service keeps them (NVS), finds the time of each alarm, rings (the alarm sound plays also when the board is
// muted, the screen wakes, the CLOCK app opens with the argument "ring"), and stops or snoozes on the word of CLOCK.
// With no alarm the service does nothing (no task wake).
// THE CLOCK OF THE BOARD: the board takes the time from the network (SNTP) when the CLOCK app asks for it (or when an
// alarm is on), and keeps it in the clock chip of the board (PCF85063 / PCF8563 / BM8563): at the start the board
// reads the chip, so the time is right also with no network.
// THE HOME ZONE (alarm_tz_get/set): the one time zone of the board: CLOCK, the alarms, the menus, STOCKS (its page
// /stocks sets it too) and DO NOT DISTURB use it.
#include <stdbool.h>
#include <stdint.h>

#define ALARM_MAX 8

typedef struct {
    uint8_t on;         // 1 = armed
    uint8_t hour, min;  // the local time (the home zone)
    uint8_t days;       // bit 0 = Sunday .. bit 6 = Saturday; 0 = once (then it turns off)
    uint8_t sound;      // 0 .. ALARM_SOUNDS - 1
    uint8_t vol;        // 10 .. 100 (the alarm rises to this level in 20 s)
    uint8_t snooze;     // minutes (1 .. 30)
    uint8_t pad;
    char label[24];  // UTF-8 ("" = none)
} alarm_t;

// THE TIMERS (CLOCK > TIMER): up to TIMER_MAX count-downs. They ring as the alarms (the RING screen of CLOCK,
// also in another app or with the screen off); alarm_ringing() gives ALARM_MAX + the timer for a timer. They count on the
// clock of the board since its start (not the time of day), so they are not saved: a restart clears them.
#define TIMER_MAX 4
enum {
    TIMER_FREE,
    TIMER_RUN,
    TIMER_PAUSE,
    TIMER_DONE
};
typedef struct {
    uint8_t state;  // TIMER_*
    uint8_t sound;  // 0 .. ALARM_SOUNDS - 1
    uint8_t vol;    // 10 .. 100
    uint8_t pad;
    uint32_t len_s;   // the length that the user set (s)
    int64_t end_ms;   // TIMER_RUN: the end (esp_timer ms)
    int64_t left_ms;  // TIMER_PAUSE: the time that is left (ms)
    char label[24];   // UTF-8 ("" = none)
} ctimer_t;
int timer_get(ctimer_t *out, int max);     // the timers (always TIMER_MAX; a free one has state TIMER_FREE)
bool timer_set(const ctimer_t *t, int n);  // all the timers (n <= TIMER_MAX; the others turn free)

int alarm_get(alarm_t *out, int max);     // the alarms (the count)
bool alarm_set(const alarm_t *a, int n);  // all the alarms (saved at once)
void alarm_tz_get(char *out, int max);    // the home zone, a POSIX TZ text ("IST-2IDT,M3.4.4/26,M10.5.0")
bool alarm_tz_set(const char *posix);     // set and save the home zone (false = a bad or too long text)
int alarm_ringing(void);                  // the alarm that rings now (its index), -1 = none
void alarm_stop(void);                    // stop (a once alarm turns off; a timer stays DONE)
void alarm_snooze(void);                  // stop, and ring again after its snooze minutes (a timer: 1 minute more)
int64_t alarm_next(int *idx);             // the next ring (epoch s; a snooze too), 0 = none
void alarm_preview(int sound, int vol);   // play a sound once (the edit page of CLOCK)
bool clock_time_ok(void);                 // the board knows the time (the network or the clock chip)
void clock_sync(void);                    // start SNTP (once; the time comes when the WiFi is up)
int clock_source(void);                   // 0 none, 1 the clock chip, 2 the network
