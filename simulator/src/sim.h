// The simulator: the shared state of its parts (not a header of the firmware).
#pragma once
#include <stdbool.h>
#include <stdint.h>

#ifdef _WIN32
#define SIM_API __declspec(dllexport)
#else
#define SIM_API __attribute__((visibility("default")))
#endif

// The state of the simulated board (sim_state())
enum { SIM_RUN, SIM_OFF, SIM_RESTART, SIM_HALT };
void sim_set_state(int state, const char *why);
int sim_get_state(void);
// The calling thread stops for good (after a power off or a restart: the window starts a new simulator; sim_rtos.c)
void sim_park(void) __attribute__((noreturn));

// sim_rtos.c
void sim_rtos_init(void);
void sim_sleep_us(int64_t us);
void sim_rtos_adopt(const char *name);  // the calling thread becomes a task (its handle, its notifications)
// The fixed clock and the fixed random values (tfnsim_options: TFNSIM_FIXED): seed for esp_random() and rand(),
// epoch_s = the time of day of the board at the start (seconds since 1970, UTC)
void sim_rtos_fixed(uint32_t seed, int64_t epoch_s);
// The name of the task that runs now with the fixed clock ("" when none)
const char *sim_rtos_running(void);

// sim_sched.c: the clock, and the lockstep scheduler of the fixed clock (one task runs at a time)
enum { SIM_TASK_READY, SIM_TASK_RUN, SIM_TASK_WAIT, SIM_TASK_DEAD };
#include "sim_os.h"
typedef struct {
    int state;
    int prio;
    uint64_t seq;                   // the order in which the ready tasks run
    int64_t wake_us;                // the end of a wait (-1: none)
    const void *on;                 // the object of a wait (NULL: a sleep)
    bool timed_out;
    sim_cond_t go;                  // the task can run
} sim_sched_task_t;
void sim_sched_set_fixed(void);
bool sim_sched_fixed(void);
int64_t sim_now_us(void);               // the clock of the board (us since the start)
void sim_sched_add(sim_sched_task_t *t, int prio);  // a new task (it is ready; the caller runs on)
void sim_sched_enter(sim_sched_task_t *t);          // the thread of a new task waits for its first turn
// Wait for sim_sched_wake(on) or the time until_us (-1: no time). The result is false at the time.
bool sim_sched_block(sim_sched_task_t *t, const void *on, int64_t until_us);
void sim_sched_wake(const void *on);
void sim_sched_yield(sim_sched_task_t *t);
void sim_sched_exit(sim_sched_task_t *t);           // the task ends: the next one runs
// The clock moves us; the result comes when all tasks wait at the new time (true), or after real_limit_us of the PC
// (false: a task does not stop)
bool sim_sched_run(int64_t us, int64_t real_limit_us);
const sim_sched_task_t *sim_sched_current(void);

// sim_fs.c: a path of the board ("/data/x", "/sdcard/x") to a path of the PC (UTF-8, '/' separators)
void sim_fs_init(const char *root);
const char *sim_fs_root(void);
bool sim_fs_map(const char *path, char *out, int len);
void sim_fs_sd_present(bool on);
bool sim_fs_sd_is_present(void);

// sim_nvs.c
void sim_nvs_init(const char *file);

// sim_board.c
void sim_board_touch(int n, const int *x, const int *y);
void sim_board_key(int key, bool down);
int sim_board_view(uint8_t *rgb, int cap, int pose, int width, int *w, int *h);
int sim_board_shot(uint8_t *rgb, int cap, int pose, int *w, int *h);     // native size, no backlight
void sim_board_lcd(int *w, int *h);         // the physical panel (portrait): LCD_W x LCD_H, round: the square
void sim_board_frame_size(int orient, int *w, int *h);  // the logical size of a frame (the bits of board_lcd_flush)
int sim_board_view_pose(int orient);        // the pose of the view (BOARD_FIXED_ORIENT: always portrait)
uint32_t sim_board_frames(void);
int sim_board_backlight(void);
bool sim_board_asleep(void);
bool sim_board_speaker_on(void);
void sim_board_battery(int pct, bool usb);
void sim_board_fps(int fps);

// sim_imu.c
void sim_imu_pose(int orient);
void sim_imu_tilt(float tx, float ty);
void sim_imu_shake(float g);
void sim_imu_dial(float rad);
void sim_imu_event(int kind);

// sim_audio.c (headless: no sound card, only the pace of the I2S)
void sim_audio_headless(void);
void sim_audio_config(bool on, float master);
void sim_audio_mic(float level, float tone_hz);
int sim_audio_level(void);              // the output level now (0..100), for the meter of the window
// The checks of the sound memory (simulator/sound_check.py): the audio task is late by ms after each block (a flash
// write on the board, a busy PC); a hold stops it after its block (out of its lock) until the hold ends, and
// sim_audio_parked() is true while it waits there.
void sim_audio_late(int ms);
void sim_audio_hold(bool on);
bool sim_audio_parked(void);

// sim_rec.c: the recorder of the screen and the sound (tfnsim_rec)
void sim_rec_frame(const uint16_t *fb, int w, int h);
void sim_rec_audio(const int16_t *lr, int frames, int channels);

// sim_log (sim_rtos.c): the last lines for the window
int sim_log_take(char *out, int cap);

// sim_crash.c (Windows), sim_crash_posix.c: a fault of a firmware thread stops the board, not the window (called in
// the thread of the window)
void sim_crash_init(void);
// (sim_crash_*.c) the board stops after a fault: "<what> in <the app>"
void sim_crash_stop_board(const char *what);

// sim_sound_win.c (Windows: waveOut), sim_sound_posix.c (none): the sound card of the PC. sim_sound_open() is false
// when there is none. sim_sound_frames() writes stereo frames times gain; it waits while the buffers of the card are
// full (this is the pace of the audio task).
bool sim_sound_open(int rate);
void sim_sound_frames(const int16_t *lr, int n, float gain);
