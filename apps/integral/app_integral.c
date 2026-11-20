// INTEGRALS (UTIL, learning): the techniques of integration, explained and shown step by step on medium to advanced
// integrals, with the basics and the derivatives first. The app never asks the user to solve: one tap = the next
// step (the changed part in a colour), and the
// user can change the number of an example (- / + or a drag on the graph) and see each step and the area change.
// Each step of each value is checked with sympy and mpmath on the PC (tools/mk_data.py).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_heap_caps.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "miniz.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfdraw/tfdraw.h"
#include "kscroll/kscroll.h"
#include "tfsfx/tfsfx.h"
#include "ig_font.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define IG_REST() vTaskDelay(pdMS_TO_TICKS(140))
#else
#define IG_REST() ((void)0)
#endif
#include "ig_sfx.h"

static bool s_he;  // Hebrew
static bool s_snd = true;

#include "ig_data.inc"
#include "ig_draw.inc"
#include "ig_type.inc"
#include "ig_text.inc"
#include "ig_graph.inc"

enum {
    FX_STEP,
    FX_OPEN,
    FX_DONE,
    FX_BACK
};
static const char *const FXN[] = {"step", "open", "done", "back"};
static void fx(int id)
{
    if (s_snd && !audio_muted()) {
        tfsfx_play(id, 70, 1.0f);
    }
}

#include "ig_ui.inc"
#include "ig_toc.inc"
#include "ig_les.inc"
#include "ig_main.inc"

// ---------------------------------------------------------------- the panel
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const SND_VALS[] = {"ON", "פועל", "OFF", "כבוי"};
enum {
    MI_LANG,
    MI_SND,
    MI_TOC
};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, LANG_VALS, "integral_lang"},
        {PI_PICK, MI_SND, 2, 0, {"SOUND", "צליל"}, SND_VALS, "integral_snd"},
        {PI_ACT, MI_TOC, 0, 0, {"CONTENTS", "תוכן העניינים"}, NULL, NULL},
};
static void item_set(int id, int v)
{
    if (id == MI_LANG) {
        s_he = v == 1;
        ui_relayout();
    } else if (id == MI_SND) {
        s_snd = v == 0;
    } else if (id == MI_TOC) {
        go(ST_TOC);
    }
}
static const pitems_t PANEL = {ITEMS, 3, NULL, NULL, item_set, NULL, NULL};

// ---------------------------------------------------------------- the app
static void enter(int64_t now)
{
    s_he = store_get("integral_lang", 0) == 1;
    s_snd = store_get("integral_snd", 0) == 0;
    sysui_panel_items(&PANEL);
    tfsfx_load_mem(IG_SFX_DATA, IG_SFX_LEN, FXN, 4, 44);
    ui_enter(now);
}

static void leave(void)
{
    ui_save();
    tfsfx_free();
    ui_leave();
    db_free();
}

static void icon(int x, int y, int s)
{
    // a soft curve with its area that sweeps from the left to the right and back, and the integral sign of the app
    const float t = icon_time();
    for (int k = 0; k < s; k++) {
        gfx_fill_rect(x, y + k, s, 1, ui_mix(rgb(18, 24, 46), rgb(6, 9, 20), k * 255 / s));
    }
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
    const float bx = x + s * 0.08f, by = y + s * 0.84f, w = s * 0.84f;
    const float a = 0.5f + 0.5f * fsin(t * 3.14159f);  // the right limit of the area moves (a period of 2 s)
    const float lo = 0.14f, hi = 0.40f + 0.48f * a;
    float px = 0, py = 0;
    for (int i = 0; i <= (int)w; i++) {
        const float u = i / w;
        const float yy = by - s * (0.10f + 0.46f * fsin(u * 3.14159f) * (0.85f + 0.15f * fcos(u * 8.0f)));
        if (u >= lo && u <= hi) {
            for (int k = (int)yy; k < (int)by; k++) {
                tfd_px((int)(bx + i), k, rgb(255, 186, 92), 70 + 90 * (k - (int)yy) / (int)(by - yy + 1));
            }
        }
        if (i) {
            tfd_line(px, py, bx + i, yy, s / 48.0f + 1.0f, rgb(255, 214, 140), 255);
        }
        px = bx + i;
        py = yy;
    }
    tfd_line(bx, by, bx + w, by, 1.0f, rgb(120, 140, 190), 200);
    // the sign: the tall glyph of the STIX integral that is nearest to 0.62 of the box
    const int want = s * 62 / 100;
    int best = -1;
    for (int i = 0; i < IG_TALL_N; i++) {
        if (IG_TALL[i].kind == IGT_INT && (best < 0 || abs(IG_TALL[i].h - want) < abs(IG_TALL[best].h - want))) {
            best = i;
        }
    }
    if (best >= 0) {
        const ig_tall_t *g = &IG_TALL[best];
        clip_set(x, y, x + s, y + s);
        tfd_glow(x + s * 0.5f, y + s * 0.42f, s * 0.3f, rgb(255, 186, 92), 50);
        glyph_a4(IG_TALL_DATA, g->off, x + (s - g->w) / 2, y + s * 42 / 100 - g->h / 2, g->w, g->h, rgb(250, 242, 226),
                 255);
        clip_off();
    }
    tfd_noclip();
}

static bool awake(void)
{
    return false;
}

const app_t app_integral = {
        .name = "INTEGRALS",
        .landscape = true,
        .enter = enter,
        .leave = leave,
        .input = ui_input,
        .update = ui_update,
        .render = ui_render,
        .draw_icon = icon,
        .awake = awake,
        .key = true,
        .round_fit = true,
        .round = APP_ROUND_HI,  // the round frame: its own layout (ig_ui.inc rnd())
};
