// The simulator: the interface of the tfnsim library for Python (simulator/tfnsim.py, ctypes). tfnsim_start() starts
// the firmware: app_main() of main/main.c in a task of its own, as the main task of the board. The window then reads
// the frames of the LCD and gives the touch points, the keys and the sensor values. With the real clock the firmware
// runs on the clock of the PC; with the fixed clock (tfnsim_options) it runs only in tfnsim_run(), as fast as the PC
// can, and the same input gives the same frames on each run.
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "gfx.h"
#include "sysui.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sim.h"
#include "audio.h"
#include "midi_bg.h"
#include "tunes.h"

void app_main(void);

static volatile int s_state = SIM_RUN;
static char s_why[160];
static volatile bool s_started;
static char s_root[1024];

void sim_set_state(int state, const char *why)
{
    if (s_state == SIM_RUN || state == SIM_HALT) {
        snprintf(s_why, sizeof s_why, "%s", why ? why : "");
        s_state = state;
    }
}
int sim_get_state(void) { return s_state; }

static void firmware_task(void *arg)
{
    (void)arg;
    app_main();                     // (it never returns)
    sim_set_state(SIM_HALT, "app_main() returned");
    vTaskDelete(NULL);
}

void sim_crash_stop_board(const char *what)
{
    const app_t *a = app_current();
    char why[200];
    snprintf(why, sizeof why, "%s in %s", what, a && a->name ? a->name : "the system");
    sim_set_state(SIM_HALT, why);
}

// The apps of this build (the list of main/apps.json), for the menu of the window
static const app_t *const ALL[] = {
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) &sym,
#include ARCADE_APPS_LIST
#undef ARCADE_APP
};
static const struct { int16_t x, y; int32_t ms; } STEP[] = {
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) {x349, y349, test_ms},
#include ARCADE_APPS_LIST
#undef ARCADE_APP
};
#define N_ALL ((int)(sizeof(ALL) / sizeof(ALL[0])))

SIM_API int tfnsim_abi(void) { return 3; }

// Before tfnsim_start(). flags: TFNSIM_HEADLESS (no sound card), TFNSIM_FIXED (the fixed clock: the firmware runs only
// in tfnsim_run(); esp_random() and rand() start from seed; time() starts at epoch_s, seconds since 1970 UTC).
#define TFNSIM_HEADLESS 1
#define TFNSIM_FIXED    2
SIM_API void tfnsim_options(int flags, unsigned seed, long long epoch_s)
{
    if (flags & TFNSIM_HEADLESS) {
        sim_audio_headless();
    }
    if (flags & TFNSIM_FIXED) {
        sim_rtos_fixed(seed, epoch_s);
    }
}

// The fixed clock moves ms; the result comes when all tasks wait at the new time: 1, or 0 when a task did not stop in
// real_ms of the PC (its name is in the log). With the real clock: a real sleep of ms.
SIM_API int tfnsim_run(int ms, int real_ms)
{
    if (!sim_sched_fixed()) {
        sim_os_sleep_us((int64_t)ms * 1000);
        return 1;
    }
    if (sim_sched_run((int64_t)ms * 1000, (int64_t)real_ms * 1000)) {
        return 1;
    }
    ESP_LOGE("sim", "the task %s did not stop in %d ms of the PC", sim_rtos_running(), real_ms);
    return 0;
}

// the physical panel of the board (portrait): the 3.49 172 x 640, the CoreS3 240 x 320, the Watcher 412 x 412, the
// AMOLED-1.75C 466 x 466, the Tab5 720 x 1280; the touch points of tfnsim_touch() are points of it
SIM_API void tfnsim_lcd(int *w, int *h) { sim_board_lcd(w, h); }

// the clock of the board (ms since the start)
SIM_API long long tfnsim_now_ms(void) { return sim_now_us() / 1000; }

// root: the data folder of this board (it gets data/, sdcard/ and nvs.txt). fps: the most frames a second.
SIM_API int tfnsim_start(const char *root, int fps)
{
    if (s_started) return 0;
    s_started = true;
    sim_rtos_init();
    sim_crash_init();
    snprintf(s_root, sizeof s_root, "%s", root);
    sim_fs_init(root);
    char nvs[sizeof s_root + 16];
    snprintf(nvs, sizeof nvs, "%s/nvs.txt", sim_fs_root());
    sim_nvs_init(nvs);
    sim_board_fps(fps);
    ESP_LOGI("sim", "TFN OS simulator: %d apps, data in %s", N_ALL, sim_fs_root());
    if (xTaskCreatePinnedToCore(firmware_task, "main", 2u << 20, NULL, 1, NULL, 0) != pdPASS) {
        return -1;
    }
    return 1;
}

// 0 runs, 1 off (board_power_off), 2 restart (esp_restart), 3 stopped (a fault); why = the reason
SIM_API int tfnsim_state(char *why, int len)
{
    if (why && len > 0) snprintf(why, (size_t)len, "%s", s_why);
    return s_state;
}

SIM_API unsigned tfnsim_frames(void) { return sim_board_frames(); }
// the picture of the panel in the position pose (orient_t), width px wide: RGB bytes, *w x *h
SIM_API int tfnsim_view(uint8_t *rgb, int cap, int pose, int width, int *w, int *h)
{
    return sim_board_view(rgb, cap, pose, width, w, h);
}
// the picture of the panel for a test: each pixel once, no backlight (RGB bytes, *w x *h)
SIM_API int tfnsim_shot(uint8_t *rgb, int cap, int pose, int *w, int *h)
{
    return sim_board_shot(rgb, cap, pose, w, h);
}
SIM_API int tfnsim_backlight(void) { return sim_board_asleep() ? 0 : sim_board_backlight(); }

// the touch points in panel coordinates (the 3.49: 0..171, 0..639), n = 0 when no finger is down
SIM_API void tfnsim_touch(int n, const int *x, const int *y) { sim_board_touch(n, x, y); }
// key 0 = PWR, 1 = BOOT
SIM_API void tfnsim_key(int key, int down) { sim_board_key(key, down != 0); }

SIM_API void tfnsim_pose(int orient) { sim_imu_pose(orient); }
SIM_API void tfnsim_tilt(float tx, float ty) { sim_imu_tilt(tx, ty); }
SIM_API void tfnsim_shake(float g) { sim_imu_shake(g); }
SIM_API void tfnsim_dial(float rad) { sim_imu_dial(rad); }
SIM_API void tfnsim_motion(int kind) { sim_imu_event(kind); }
SIM_API void tfnsim_mic(float level, float tone_hz) { sim_audio_mic(level, tone_hz); }
SIM_API void tfnsim_sound(int on, float master) { sim_audio_config(on != 0, master); }
SIM_API int tfnsim_sound_level(void) { return sim_audio_level(); }
// the meters of the mix (main/audio.c, libs/midi_bg): the peak before the VOLUME, the limiter, the notes over 100 %
SIM_API int tfnsim_peak_take(void) { return audio_peak_take(); }
SIM_API int tfnsim_limiter_take(void) { return audio_limiter_take(); }
// the checks of the sound memory (simulator/sound_check.py): the audio task is late by ms after each block; the voices
// that play now
SIM_API void tfnsim_audio_late(int ms)
{
    sim_audio_late(ms);
}
SIM_API int tfnsim_voices_on(void)
{
    return audio_voices_on();
}
// (weak: a build with no app that uses libs/midi_bg has no MIDI_BG notes; the real function takes its place)
__attribute__((weak)) int midi_bg_over_take(void) { return 0; }
SIM_API int tfnsim_over_take(void) { return midi_bg_over_take(); }
// a start tune of a game (main/tfn/tunes.h tune_t); the result is its length in ms (the audio golden test)
SIM_API int tfnsim_tune(int t)
{
    tune_play((tune_t)t);
    return tune_ms((tune_t)t);
}
SIM_API void tfnsim_battery(int pct, int usb) { sim_board_battery(pct, usb != 0); }
SIM_API void tfnsim_sd(int present) { sim_fs_sd_present(present != 0); }
SIM_API void tfnsim_fps(int fps) { sim_board_fps(fps); }

// the app that is open, and the orientation of its frame (orient_t)
SIM_API const char *tfnsim_app(void)
{
    const app_t *a = app_current();
    return a && a->name ? a->name : "";
}
// (a board with BOARD_FIXED_ORIENT shows each frame upright: the view stays in portrait)
SIM_API int tfnsim_orient(void) { return sim_board_view_pose((int)gfx_orient()); }

SIM_API int tfnsim_app_count(void) { return N_ALL; }
SIM_API const char *tfnsim_app_name(int i) { return i >= 0 && i < N_ALL ? ALL[i]->name : ""; }
// the tap point and the time of the self-test step of the app (main/apps.json test349, test_ms); x < 0 = none
SIM_API int tfnsim_app_test(int i, int *x, int *y)
{
    if (i < 0 || i >= N_ALL) return 0;
    *x = STEP[i].x;
    *y = STEP[i].y;
    return STEP[i].ms;
}
SIM_API int tfnsim_app_landscape(int i) { return i >= 0 && i < N_ALL ? ALL[i]->landscape : 0; }
SIM_API int tfnsim_app_needs(int i) { return i >= 0 && i < N_ALL ? ALL[i]->needs : 0; }

// open an app by its name (as the web page does: main.c switches at the next frame)
SIM_API int tfnsim_open(const char *name)
{
    const app_t *a = app_find(name);
    if (!a) return 0;
    app_open(a);
    return 1;
}

SIM_API int tfnsim_log(char *out, int cap) { return sim_log_take(out, cap); }

// the settings panel of the system layer shows (a pull from the top edge)
SIM_API int tfnsim_panel_on(void) { return sysui_panel_on(); }

// (the check of the simulator) a task of the firmware makes an access violation: the board must stop, not the window
static void fault_task(void *arg)
{
    (void)arg;
    *(volatile int *)(uintptr_t)8 = 1;
    vTaskDelete(NULL);
}
SIM_API void tfnsim_test_fault(void) { xTaskCreate(fault_task, "fault_test", 4096, NULL, 3, NULL); }
