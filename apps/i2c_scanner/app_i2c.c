// I2C SCANNER: the devices on an I2C bus of the board: a map of the addresses, the list of the devices with their
// names, IDENTIFY and READ THE REGISTERS of one device.
// The task: the app task of TFN OS calls enter, input, update, render and leave (app_i2c at the end of this file);
// the bus work is on the task of i2c_bus.c. The memory: the file statics of this file; no heap.
//
// The scan probes each address 0x08 .. 0x77 (LIVE: each 1.5 s, so a module that comes or goes shows at once); the map
// of the addresses (as i2cdetect) and the list of the devices with their names: a chip of the board (rfp_board_i2c:
// the data of docs/headers.md), or the usual chips at that address (i2c_db.c). A tap on a device: its addresses,
// IDENTIFY (the ID registers of the candidates: a known value names the chip), READ THE REGISTERS (0x00 .. 0xFF).
//
// THE LAYERS (each one knows only the layer under it):
//   libs/rfpulse   rfp_board_i2c(): the buses of each board kind (the pins, the port, the chips of the board);
//                  rfp_sketch.inc: the wiring sketch
//   i2c_bus.c      one task on the bus (the I2C master driver of ESP-IDF); i2c_db.c the data base of the chips
//   this app       the screens (i2c_draw.inc), the settings
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "esp_timer.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "tfdraw/tfdraw.h"
#include "rfpulse/rfp_board.h"
#include "i2c_bus.h"
#include "i2c_db.h"

static bool s_he;  // the language: false English, true Hebrew
#include "i2c_text.inc"
#include "i2c_fonts.inc"

#define BAR BOARD_BAR_SCREEN
static int64_t s_now;  // ms
static int64_t ms_now(void)
{
    return esp_timer_get_time() / 1000;
}

// ================================================================ the state
static int s_nbus;
static int s_busi;  // the bus in use (rfp_board_i2c)
static int s_khz = 100;
static bool s_live = true;
static bool s_open;
static ib_state_t s_ib;
static uint8_t s_prev[16];  // the found bits of the scan before
static bool s_have_prev;
static int64_t s_t_new[128];  // ms: an address came (the row lights for 3 s)
static uint32_t s_scans_seen;

enum {
    SC_SCAN,
    SC_DEV,
    SC_WIRE
};
static int s_scr;
static int s_view;           // the 320 x 240 frame: 0 LIST, 1 MAP
static uint8_t s_dev;        // the address of SC_DEV
static uint32_t s_req_seen;  // the req_done of the last result that the app took
static int s_pend;           // the request that the task does now: 0 none, 1 IDENTIFY, 2 REGISTERS
static uint8_t s_pend_addr;
static int16_t s_ident_of[128];                     // the result of IDENTIFY of each address: -2 not asked, -3 asked,
                                                    // -1 no known value, else the chip (I2C_DB)
static int s_ident = -2;                            // SC_DEV: the same for s_dev
static bool s_regs_have, s_regs_wait, s_regs_jump;  // (jump: scroll to the registers at their first frame)
static float s_scroll;                              // the list (px), or the page of a device (px)
static int64_t s_toast_t;
static char s_toast[96];
static void toast(const char *s)
{
    snprintf(s_toast, sizeof s_toast, "%s", s);
    s_toast_t = s_now;
}

static const rfp_i2c_t *bus(void)
{
    return rfp_board_i2c(s_busi);
}
static bool found(uint8_t a)
{
    return (s_ib.found[a >> 3] >> (a & 7)) & 1;
}

// the chip of the board at an address (NULL = none)
static const char *board_chip(uint8_t a)
{
    for (const rfp_chip_t *c = bus() ? bus()->chips : NULL; c && c->addr; c++) {
        if (c->addr == a) {
            return c->name;
        }
    }
    return NULL;
}
// the most likely chip at an address (NULL = none): the chip that IDENTIFY found, else the entry of the data base
// with the fewest addresses (a chip with one address is a better guess than a family with 8)
static const char *db_first(uint8_t a)
{
    if (s_ident_of[a] >= 0) {
        return I2C_DB[s_ident_of[a]].name;
    }
    int best = -1;
    for (int i = 0; i < I2C_DB_N; i++) {
        if (a >= I2C_DB[i].lo && a <= I2C_DB[i].hi &&
            (best < 0 || I2C_DB[i].hi - I2C_DB[i].lo < I2C_DB[best].hi - I2C_DB[best].lo)) {
            best = i;
        }
    }
    return best >= 0 ? I2C_DB[best].name : NULL;
}

// IDENTIFY can run at an address with no confirmation from the person if: it is not a chip of the board, it is not
// where a write of a byte can change a device, and a chip of the data base there has an ID register
static bool ident_auto(uint8_t a)
{
    if (board_chip(a) || i2c_db_risky(a)) {
        return false;
    }
    for (int i = 0; i < I2C_DB_N; i++) {
        if (I2C_DB[i].has_id && a >= I2C_DB[i].lo && a <= I2C_DB[i].hi) {
            return true;
        }
    }
    return false;
}

static void ask_ident(uint8_t a)
{
    ib_ident(a);
    s_pend = 1;
    s_pend_addr = a;
    s_ident_of[a] = -3;
}

static int s_list[112];  // the found addresses, low to high
static int s_nlist;

static void bus_open(void)
{
    s_open = bus() && ib_open(bus(), s_khz);
    ib_live(s_live);
    s_have_prev = false;
    s_scans_seen = 0;
    s_pend = 0;
    for (int i = 0; i < 128; i++) {
        s_ident_of[i] = -2;
    }
    memset(s_t_new, 0, sizeof s_t_new);
    memset(&s_ib, 0, sizeof s_ib);
}

static void set_bus(int i)
{
    if (i < 0 || i >= s_nbus) {
        return;
    }
    ib_close();
    s_busi = i;
    store_set("i2c_bus", (uint32_t)i);
    s_scr = SC_SCAN;
    s_scroll = 0;
    bus_open();
}

static void open_dev(uint8_t a)
{
    s_dev = a;
    s_scr = SC_DEV;
    s_scroll = 0;
    s_regs_have = s_regs_wait = false;
    // (IDENTIFY at once where it can run alone, if the background did not do it yet)
    if (s_ident_of[a] == -2 && ident_auto(a) && !s_pend) {
        ask_ident(a);
    }
    s_ident = s_ident_of[a];
}

// ================================================================ the settings panel
enum {
    PI_LANG,
    PI_BUS,
    PI_SPEED,
    PI_LIVE,
    PI_WIRE,
    PI_SCAN
};
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *s_bus_vals[8];
static const char *const SPEED_VALS[] = {"100 KHZ", "100 KHZ", "400 KHZ", "400 KHZ"};
static const char *const ONOFF[] = {"OFF", "כבוי", "ON", "פועל"};
static pitem_t s_items[] = {
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_VALS, "i2c_lang"},
        {PI_PICK, PI_BUS, 2, 0, {"BUS", "אפיק"}, s_bus_vals, NULL},
        {PI_PICK, PI_SPEED, 2, 0, {"SPEED", "מהירות"}, SPEED_VALS, NULL},
        {PI_PICK, PI_LIVE, 2, 0, {"LIVE", "חי"}, ONOFF, NULL},
        {PI_ACT, PI_WIRE, 0, 0, {"THE WIRING", "החיווט"}, NULL, NULL},
        {PI_ACT, PI_SCAN, 0, 0, {"SCAN NOW", "סריקה עכשיו"}, NULL, NULL},
};
#define NITEMS ((int)(sizeof(s_items) / sizeof(s_items[0])))
static bool item_shown(int id)
{
    if (id == PI_BUS) {
        return s_nbus > 1;
    }
    if (id == PI_SPEED) {
        return bus() && !bus()->shared;  // (the bus of the system keeps its speed)
    }
    return true;
}
static int item_get(int id)
{
    return id == PI_BUS ? s_busi : id == PI_SPEED ? s_khz == 400 : id == PI_LIVE ? s_live : 0;
}
static void item_set(int id, int v)
{
    switch (id) {
    case PI_LANG:
        s_he = v == 1;
        break;
    case PI_BUS:
        set_bus(v);
        break;
    case PI_SPEED:
        s_khz = v ? 400 : 100;
        store_set("i2c_khz", (uint32_t)s_khz);
        set_bus(s_busi);
        break;
    case PI_LIVE:
        s_live = v;
        store_set("i2c_live", (uint32_t)s_live);
        ib_live(s_live);
        break;
    case PI_WIRE:
        s_scr = SC_WIRE;
        break;
    case PI_SCAN:
        ib_scan();
        s_scr = SC_SCAN;
        break;
    }
}
static bool s_down, s_drag;
static void panel_opened(void)
{
    s_down = s_drag = false;
}
static const pitems_t PANEL = {s_items, NITEMS, item_shown, item_get, item_set, panel_opened, panel_opened};

#include "i2c_draw.inc"

// ================================================================ the touches
static int s_dy0;
static float s_scroll0;

static void input(const input_event_t *ev)
{
    const int x = ev->x, y = ev->y;
    switch (ev->kind) {
    case IN_DOWN:
        s_down = true;
        s_drag = false;
        s_dy0 = y;
        s_scroll0 = s_scroll;
        break;
    case IN_MOVE:
        if (s_down && abs(y - s_dy0) > 8 && y > s_top_h) {
            s_drag = true;
            input_tap_cancel();
        }
        if (s_drag) {
            s_scroll = s_scroll0 - (float)(y - s_dy0);
            if (s_scroll < 0) {
                s_scroll = 0;
            }
        }
        break;
    case IN_UP:
        s_down = s_drag = false;
        break;
    case IN_TAP: {
        const int h = hit_at(x, y);
        if (h >= 0) {
            hit_do(h);
        }
        break;
    }
    default:
        break;
    }
}

// ================================================================ enter / leave / update

static void enter(int64_t now)
{
    (void)now;
    s_now = ms_now();
    s_he = store_get("i2c_lang", 1) == 1;  // (Hebrew when nothing is saved)
    s_nbus = 0;
    while (s_nbus < 4 && rfp_board_i2c(s_nbus)) {
        s_bus_vals[2 * s_nbus] = rfp_board_i2c(s_nbus)->name[0];
        s_bus_vals[2 * s_nbus + 1] = rfp_board_i2c(s_nbus)->name[1];
        s_nbus++;
    }
    s_items[1].n = (uint8_t)(s_nbus < 2 ? 2 : s_nbus);
    s_busi = (int)store_get("i2c_bus", 0);
    if (s_busi < 0 || s_busi >= s_nbus) {
        s_busi = 0;
    }
    s_khz = store_get("i2c_khz", 100) == 400 ? 400 : 100;
    s_live = store_get("i2c_live", 1) == 1;
    s_scr = SC_SCAN;
    s_view = 0;
    s_scroll = 0;
    s_toast[0] = 0;
    s_down = s_drag = false;
    sysui_panel_items(&PANEL);
    bus_open();
}

static void leave(void)
{
    ib_close();
    store_set("i2c_lang", s_he);
}

static void update(int64_t now)
{
    (void)now;
    s_now = ms_now();
#ifndef ESP_PLATFORM
    ib_pc_round();
#endif
    ib_get(&s_ib);
    if (s_ib.scans != s_scans_seen) {  // a new scan: the new addresses light, the list again
        s_scans_seen = s_ib.scans;
        s_nlist = 0;
        int nnew = -1;
        for (int a = 0x08; a <= 0x77; a++) {
            if (!found((uint8_t)a)) {
                continue;
            }
            s_list[s_nlist++] = a;
            if (s_have_prev && !((s_prev[a >> 3] >> (a & 7)) & 1)) {
                s_t_new[a] = s_now;
                nnew = a;
            }
        }
        if (nnew >= 0) {
            char b[64];
            snprintf(b, sizeof b, "%s: 0x%02X", TX(T_NEW), nnew);
            toast(b);
            sfx_ui(SFX_CLICK);
        }
        memcpy(s_prev, s_ib.found, sizeof s_prev);
        s_have_prev = true;
    }
    if (s_pend && s_ib.req_done != s_req_seen) {  // a result of the task
        s_req_seen = s_ib.req_done;
        const uint8_t a = s_ib.req_addr;
        if (s_pend == 1) {
            s_ident_of[a] = (int16_t)s_ib.ident;
        }
        if (s_pend == 2 && s_scr == SC_DEV && a == s_dev) {
            s_regs_have = s_regs_jump = true;
        }
        s_pend = 0;
        s_regs_wait = false;
    }
    if (s_scr == SC_DEV) {
        s_ident = s_ident_of[s_dev];
    }
    // IDENTIFY in the background: one address at a time, when the task is free
    if (!s_pend && s_ib.state == IB_IDLE) {
        for (int i = 0; i < s_nlist; i++) {
            if (s_ident_of[s_list[i]] == -2 && ident_auto((uint8_t)s_list[i])) {
                ask_ident((uint8_t)s_list[i]);
                break;
            }
        }
    }
}

static bool awake(void)
{
    return true;
}  // (a tool that one watches: the screen stays on)

const app_t app_i2c = {
        .name = "I2C SCANNER",
        .landscape = true,
        .round = APP_ROUND_HI,  // (the round boards: the native round frame, the middle box of i2c_draw.inc)
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
};
