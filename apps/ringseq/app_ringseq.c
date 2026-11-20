// RING SEQUENCER (UTIL): a beat and melody machine of concentric rings for the round screen. Each ring is
// one track (KICK, SNARE, HAT, PERC, BASS, CHORDS, LEAD) with its own step count (3..16), so a ring of 5 against a
// ring of 4 makes a polyrhythm by itself. An arm turns round the centre and plays each lit step that it passes. Tools:
// Euclid (HITS), turn the ring, a note for each step in the key and the mode, the accent, mute and solo, the tempo by a
// twist at the centre, the swing, 8 pattern slots, a random variation, and the export as a MIDI file.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/storage.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "drumkit/drumkit.h"
#include "instr/instr.h"
#include "bidi/bidi.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define RS_REST() vTaskDelay(pdMS_TO_TICKS(140))
#else
#define RS_REST() ((void)0)
#endif

#include "rs_model.inc"
#include "rs_audio.inc"
#include "rs_midi.inc"
#include "rs_gfx.inc"
#include "rs_lay.inc"
#include "rs_draw.inc"
#include "rs_input.inc"

// ---------------------------------------------------------------- the panel

static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
enum {
    MI_LANG,
    MI_DEMO,
    MI_CLEAR
};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, LANG_VALS, "ringseq_lang"},
        {PI_ACT2, MI_DEMO, 0, 0, {"DEMO PATTERN", "תבנית הדגמה"}, NULL, NULL},
        {PI_ACT2, MI_CLEAR, 0, 0, {"CLEAR PATTERN", "נקה תבנית"}, NULL, NULL},
};
static void item_set(int id, int v)
{
    if (id == MI_LANG) {
        s_he = v == 1;
    } else {
        undo_push();
        if (id == MI_DEMO) {
            pat_demo(P);
        } else {
            uint8_t bpm = P->bpm, key = P->key, mode = P->mode;
            pat_empty(P);
            P->bpm = bpm;
            P->key = key;
            P->mode = mode;
        }
        s_dirty = true;
    }
    lay_build();
    bg_all();
}
static void panel_opened(void)
{
    save_now();
}
static const pitems_t PANEL = {ITEMS, 3, NULL, NULL, item_set, panel_opened, NULL};

// ---------------------------------------------------------------- the app

static int s_fps_n;
static int64_t s_fps_t0, s_last;
static bool s_panel, s_sysui;
static bool s_ok;

static void enter(int64_t now)
{
    s_now = now;
    s_ok = false;
    s_he = store_get("ringseq_lang", 0) == 1;
    if (!S) {
        S = heap_caps_calloc(1, sizeof *S, MALLOC_CAP_SPIRAM);
    }
    if (!s_undo) {
        s_undo = heap_caps_malloc(sizeof(pat_t) * NUNDO, MALLOC_CAP_SPIRAM);
    }
    if (!S || !s_undo || !gfx_alloc()) {
        return;
    }
    s_ok = true;
    save_load();
    s_undo_n = 0;
    s_dirty = false;
    int t = (int)store_get("ringseq_tst", 0);
    s_tst = t != 0;
    s_tev_n = 0;
    if (t) {
        pat_test(P, t);
    }
    s_pl = P;
    for (int r = 0; r < NR; r++) {
        s_lastnote[r] = 0;
        s_seen[r] = s_hitn[r];
        s_flash_t[r] = 0;
    }
    s_view = V_MAIN;
    s_act = -1;
    s_hold_slot = -1;
    s_toast_until = 0;
    s_zoom_t0 = -10000;
    memset(&T, 0, sizeof T);
    lay_build();
    bg_all();
    s_tk_n = s_tk_gap = s_tk_late = s_ev_n = 0;
    s_tk_last = 0;
    s_fps_t0 = s_last = s_touch_ms = now;
    s_fps_n = 0;
    s_ph = 0;
    play_set(t != 0);
    audio_set_tick(tick);
    sysui_panel_items(&PANEL);
}
static void leave(void)
{
    play_set(false);
    audio_set_tick(NULL);
    for (int r = 0; r < NR; r++) {
        audio_release_group((uint8_t)(G_RING + r), 60);
    }
    s_pl = NULL;
    if (s_ok && !store_get("ringseq_tst", 0)) {
        save_now();
    }
    gfx_free();
    free(S);
    S = NULL;
    free(s_undo);
    s_undo = NULL;
}
// the place of the arm: it follows the position of the audio task, smooth between the blocks
static void phase_update(int64_t now, float dt_ms)
{
    if (!s_run) {
        s_ph = 0;
        return;
    }
    int bpm = bpm_ok(P);
    int64_t el = esp_timer_get_time() - s_pub_t;
    el = el < 0 ? 0 : el > 40000 ? 40000 : el;
    float target = s_pub_bar * 16.0f + (float)s_pub_u / SIX_U + el * (bpm / 15.0e6f);
    s_ph += dt_ms * bpm / 15000.0f;
    float err = target - s_ph;
    if (err > 3 || err < -3) {
        s_ph = target;
    } else {
        s_ph += err * 0.2f;
    }
    (void)now;
}
// the test patterns: the sample of each step (the PC test compares them with the grid of the pattern)
static void log_steps(void)
{
    static bool done;
    if (!s_tst || done || s_tev_n < TEV_N) {
        return;
    }
    done = true;
    char b[16];
    for (int i = 0; i < TEV_N; i++) {
        snprintf(b, sizeof b, "%u", (unsigned)s_tev[i]);
        ESP_LOGI("ringseq", "step %d ring %d sample %s", i, s_tev_r[i], b);
    }
}
static void log_stats(int64_t now)
{
    log_steps();
    s_fps_n++;
    if (now - s_fps_t0 < 10000) {
        return;
    }
    ESP_LOGI("ringseq", "RING SEQUENCER: %d fps, play %d, blocks %u, longest gap %u us, gaps over 15 ms %u, steps %u",
             (int)(s_fps_n * 1000 / (now - s_fps_t0)), s_run ? 1 : 0, (unsigned)s_tk_n, (unsigned)s_tk_gap,
             (unsigned)s_tk_late, (unsigned)s_ev_n);
    s_fps_t0 = now;
    s_fps_n = 0;
    s_tk_gap = 0;
}
static void update(int64_t now)
{
    float dt = (float)(now - s_last);
    s_last = now;
    s_now = now;
    if (!s_ok) {
        return;
    }
    if (gfx_w != s_W || gfx_h != s_H) {
        if (!gfx_alloc()) {
            s_ok = false;
            return;
        }
        lay_build();
        bg_all();
    }
    phase_update(now, dt > 100 ? 100 : dt);
    for (int r = 0; r < NR; r++) {
        uint32_t n = s_hitn[r];
        if (n != s_seen[r]) {
            s_seen[r] = n;
            s_flash_t[r] = now;
            s_flash_k[r] = s_hitk[r];
        }
    }
    hold_update();
    if (s_dirty && !s_run && now - s_touch_ms > 4000 && !T.down) {
        save_now();
    }
    // the system drew over the frame (the panel, a message): all of the frame again
    bool pn = sysui_panel_on(), sa = sysui_active();
    if (pn != s_panel || sa != s_sysui) {
        full_both();
    }
    s_panel = pn;
    s_sysui = sa;
    bool busy = s_run || T.down || now < s_toast_until + 400 || now - s_zoom_t0 < 400 || now - s_touch_ms < 3000;
    if (!busy) {
        RS_REST();
    }
}
static void render(int64_t now)
{
    s_now = now;
    if (!s_ok || !s_bg) {
        gfx_fill(0);
        ui_text_c(&KF_NAME, gfx_w / 2, gfx_h / 2, "NO MEMORY", rgb(255, 120, 120));
        return;
    }
    bg_flush();
    int sl = buf_slot();
    frame_restore(sl);
    target_frame(sl);
    draw_moving();
    log_stats(now);
}
static bool awake(void)
{
    return s_run;
}

static void draw_icon(int x, int y, int sz)
{
    gfx_fill_rect(x, y, sz, sz, rgb(6, 6, 14));
    float t = icon_time(), cx = x + sz / 2.0f, cy = y + sz / 2.0f;
    static const uint8_t N[4] = {4, 5, 3, 6};
    for (int i = 0; i < 4; i++) {
        int r = sz * (12 + 9 * i) / 100;
        gfx_ring((int)cx, (int)cy, r + 1, r - 1, rgb(RCOL[i * 2][0] / 3, RCOL[i * 2][1] / 3, RCOL[i * 2][2] / 3));
        for (int k = 0; k < N[i]; k++) {
            float a = 6.2831853f * k / N[i];
            int d = sz / 28 + 1;
            gfx_fill_circle((int)(cx + r * fsin(a)), (int)(cy - r * fcos(a)), d,
                            rgb(RCOL[i * 2][0], RCOL[i * 2][1], RCOL[i * 2][2]));
        }
    }
    float a = t * 6.2831853f * 0.5f;
    int r = sz * 42 / 100;
    for (int k = 0; k < r; k++) {
        gfx_fill_rect((int)(cx + k * fsin(a)), (int)(cy - k * fcos(a)), 2, 2, rgb(230, 235, 255));
    }
}

// (no memory at the open: render shows NO MEMORY, and a touch must not reach the pattern and the layout)
static void input_ok(const input_event_t *ev)
{
    if (s_ok) {
        input(ev);
    }
}

const app_t app_ringseq = {
        .name = "RING SEQUENCER",
        .landscape = true,
        .enter = enter,
        .leave = leave,
        .input = input_ok,
        .update = update,
        .render = render,
        .draw_icon = draw_icon,
        .awake = awake,
        .round_fit = true,
        .round = APP_ROUND_HI,  // the round frame: its own layout (rs_lay.inc lay_round())
};
