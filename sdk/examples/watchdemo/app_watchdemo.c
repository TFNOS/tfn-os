// TFN OS SDK example 5: WATCH DEMO, an app with a watcher (main/watch.h) and a secret.
//
// What this example shows:
//   - a second part of the app, watch_watchdemo.c, that the board runs in the background rounds (watch.elf): mk_app.py
//     builds it with --watch, writes watch= into the manifest and the permission class "background"
//   - the secret store (the class "secret"): the app keeps a value, its watcher reads it (each app sees only its own)
//   - the notices (tfn_notify, the class "notify") and the open at an item: a tap on a notice of the watcher opens
//     this app with app_launch_arg() = "n=<the check>"
//
// Build (from tfn-os/; the board needs a firmware with ABI rev 5 or newer):
//   python sdk/tools/mk_app.py sdk/examples/watchdemo/app_watchdemo.c --sym app_watchdemo --name "WATCH DEMO" --group UTIL
//       --watch sdk/examples/watchdemo/watch_watchdemo.c --watch-sym watch_watchdemo --watch-every 5 --kinds lcd349 --nosign
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/settings.h"
#include "tfn/uikit.h"
#include "tfn/watch.h"

static uint32_t s_from;                     // the check of the notice that opened the app (0 = an open from the menu)

static void enter(int64_t now)
{
    (void)now;
    const char *a = app_launch_arg();
    s_from = !strncmp(a, "n=", 2) ? (uint32_t)atoi(a + 2) : 0;
    tfn_secret_set("demo", "a value that only this app and its watcher can read");
}

static void leave(void) {}
static void update(int64_t now) { (void)now; }
static void input(const input_event_t *ev) { (void)ev; }

static void render(int64_t now)
{
    (void)now;
    gfx_fill(rgb(14, 18, 30));
    const int cx = gfx_w / 2, big = gfx_w >= 480;
    char b[64];
    ui_text_c(&KF_BIG, cx, big ? 40 : 50, "WATCH DEMO", rgb(240, 214, 120));
    snprintf(b, sizeof b, "BACKGROUND CHECKS: %lu", (unsigned long)store_get("watchdemo_n", 0));
    ui_text_c(&KF_TEXT, cx, big ? 78 : 100, b, rgb(230, 236, 248));
    if (s_from) snprintf(b, sizeof b, "A NOTICE OPENED THIS: CHECK %lu", (unsigned long)s_from);
    else snprintf(b, sizeof b, "OPENED FROM THE MENU");
    ui_text_c(&KF_NAME, cx, big ? 108 : 140, b, s_from ? rgb(120, 230, 150) : rgb(160, 170, 190));
    ui_text_c(&KF_LABEL, cx, big ? 140 : 180, "THE TIME OF THE CHECKS: SETUP > NOTIFICATIONS > BACKGROUND CHECKS",
              rgb(150, 160, 185));
}

static void icon(int x, int y, int s)
{
    gfx_fill_rect(x, y, s, s, rgb(30, 40, 70));
    const int cx = x + s / 2, cy = y + s / 2, r = s * 3 / 10;
    ui_circle(cx, cy, r, rgb(240, 214, 120), 255);
    ui_circle(cx, cy, r - s / 14 - 1, rgb(30, 40, 70), 255);
    gfx_fill_rect(cx - 1, cy - r + s / 10, s / 18 + 2, r - s / 10, rgb(240, 214, 120));
    gfx_fill_rect(cx - 1, cy - 1, r * 2 / 3, s / 18 + 2, rgb(240, 214, 120));
}

const app_t app_watchdemo = {
    .name = "WATCH DEMO",
    .landscape = true,
    .enter = enter,
    .leave = leave,
    .update = update,
    .input = input,
    .render = render,
    .draw_icon = icon,
};
