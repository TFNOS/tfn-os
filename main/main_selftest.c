// The self-test of TFN OS (CONFIG_TFN_SELFTEST): it opens each app in turn, taps once and logs the frame rate,
// then it saves a test tone and a MIDI file.
#include "main_priv.h"

#if CONFIG_TFN_SELFTEST
static const char *TAG = "main";

static void selftest_export(void);

// Open each app in turn, tap once to start the game, log the frame rate.
void mai_selftest(int64_t t)
{
    // the logical tap point that starts each app and the time of its step: main/apps.json (test349 on the 3.49,
    // tests3 on the 320 x 240 boards, test_ms); x < 0 = no step (the menu)
#if BOARD_BAR_SCREEN
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) {&sym, x349, y349, test_ms},
#else
#define ARCADE_APP(id, sym, group, is_new, x349, y349, xs3, ys3, test_ms) {&sym, xs3, ys3, test_ms},
#endif
    static const struct {
        const app_t *app;
        int16_t x, y;
        int32_t ms;
    } STEPS[] = {
#include ARCADE_APPS_LIST
    };
#undef ARCADE_APP
    static int step = -1;
    static int64_t at;
    static int frames;
    static bool tapped;
    if (step >= 1000) {
        return;  // done
    }
    static settings_t saved;
    if (step < 0 && frames == 0) {
        // keep the user's settings: the test taps must not change them for good
        saved = g_settings;
        // tests run without sound: amplifier off and codec volume 0, but the
        // synth runs, so the log shows its load
        board_speaker(false);
        audio_set_volume(0);
    }
    frames++;
    // the most voices in this step (the sound is off, but the synth runs)
    static int max_voices;
    int vo = audio_voices_on();
    if (vo > max_voices) {
        max_voices = vo;
    }
    // a check of the tune player: a tune at 1.5 s, its voices 0.4 s later
    static int64_t tune_at;
    if (step < 0 && tune_at == 0 && t > 1500) {
        tune_play(TUNE_KNIGHT);
        tune_at = t;
    } else if (step < 0 && tune_at > 0 && t - tune_at >= 400) {
        ESP_LOGI(TAG, "selftest tune: %d voices 0.4 s after the start (3 expected)", audio_voices_on());
        tune_at = -1;
    }
    if (step >= 0 && !tapped && t - at > 400) {
        int px, py;
        // logical -> physical for the current orientation (inverse of gfx_touch_to_logical)
        int lx = STEPS[step].x, ly = STEPS[step].y;
        if (lx > gfx_w - 1) {
            lx = gfx_w - 1;  // the taps are for the 3.49 screen
        }
        if (ly > gfx_h - 1) {
            ly = gfx_h - 1;
        }
        gfx_logical_to_touch(lx, ly, &px, &py);
        input_event_t d = {.kind = IN_DOWN, .x = (int16_t)px, .y = (int16_t)py, .t_ms = t};
        input_event_t u = {.kind = IN_UP, .x = (int16_t)px, .y = (int16_t)py, .t_ms = t + 50};
        input_event_t tp = {.kind = IN_TAP, .x = (int16_t)px, .y = (int16_t)py, .t_ms = t + 50};
        input_inject(&d);
        input_inject(&u);
        input_inject(&tp);
        tapped = true;
        frames = 0;
        at = t;
    }
    // the time of the step (test_ms in main/apps.json: CRUISE runs a bench of its shots and missions, 24 s)
    int64_t step_ms = step >= 0 ? STEPS[step].ms : 6000;
    if (step < 0 ? t > 3000 : (tapped && t - at > step_ms)) {
        if (step >= 0) {
            ESP_LOGI(TAG, "selftest %-6s %.1f fps, audio %d%% (%d voices, %d at most), internal free %u, psram free %u",
                     STEPS[step].app->name, frames * 1000.0 / (double)(t - at), audio_load(), audio_voices_on(),
                     max_voices, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
        step++;
        max_voices = 0;
        // skip the apps that this board cannot run (the menu does not show them), and the menu (no tap point)
        while (step < (int)(sizeof(STEPS) / sizeof(STEPS[0])) &&
               (STEPS[step].x < 0 || (STEPS[step].app->needs & ~app_board_caps()))) {
            step++;
        }
        if (step >= (int)(sizeof(STEPS) / sizeof(STEPS[0]))) {
            ESP_LOGI(TAG, "selftest done");
            selftest_export();
            step = 1000;
            g_settings = saved;
            settings_save();
            board_speaker(true);
            settings_apply();
            app_open(&app_menu);
            return;
        }
        if (step < 1000) {
            app_open(STEPS[step].app);
            at = t;
            tapped = false;
        }
    }
}

// Save a 5 s tone and a short MIDI file with the background save task.
static void selftest_export(void)
{
    uint32_t n = 5 * AUDIO_SAMPLE_RATE;
    int16_t *buf = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    midi_t m;
    if (!buf || !midi_begin(&m, 64)) {
        ESP_LOGE(TAG, "selftest export: no memory");
        free(buf);
        return;
    }
    for (uint32_t i = 0; i < n; i++) {
        buf[i] = (int16_t)(6000 * fsin(i * (6.2831853f * 440.0f / AUDIO_SAMPLE_RATE)));
    }
    for (int k = 0; k < 8; k++) {
        midi_note(&m, k * 96, k * 96 + 90, 0, 60 + k * 2, 100);
    }
    char name[48];
    export_next_name("selftest", name, sizeof(name));
    int64_t t0 = esp_timer_get_time();
    bool ok = export_save_async(name, buf, n, AUDIO_SAMPLE_RATE, &m, 120);
    while (ok && export_progress() >= 0 && export_progress() < 100) {
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGI(TAG, "selftest export %s: %s in %d ms", name, ok && export_progress() == 100 ? "ok" : "FAILED",
             (int)((esp_timer_get_time() - t0) / 1000));
    // the test files must not stay in the user's exports
    char path[64];
    snprintf(path, sizeof(path), "%s.wav", name);
    remove(path);
    snprintf(path, sizeof(path), "%s.mid", name);
    remove(path);
}
#endif
