// COIL RIDER (GAMES): a soft-body motorbike trial in the genre of the old
// stretchy-bike trials: hand-built, hard terrain (polygons with overhangs, ceilings, walls, gaps, a loop), wheels on
// STRETCHY rods (the stretch of the suspension is a skill: a pump, a bounce up a step), and tilt to move. An original
// game: own name, own art (three worlds: DUSK canyon, NIGHT frost, EMBER volcano; a dark inside with a bright edge),
// own levels, own pickups (crystals) and own exit (a gate that lights when all the crystals are taken).
// The controls: TILT the board toward the face of the bike = gas, the other way = the brake, then back (a dead zone of
// about 3 degrees, the full gas at about 15; the zero is the position at the start of a level). HOLD the left half of
// the screen = the bike leans to the left (back when it faces right), the right half = to the right. A DOUBLE TAP = the
// rider turns round. TILT OFF in the panel: the right half is the gas, the left half the brake, their top parts lean.
// Class A (code-drawn 2D, sdk/docs/graphics.md). MANUAL.md "COIL RIDER".
#if defined(ESP_PLATFORM) && defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("fp-contract=off")  // (no fused multiply-add: the physics gives the same steps as the PC test)
#endif
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif
#include "esp_log.h"
#include "esp_timer.h"
#include "tfn/app.h"
#ifndef APP_ROUND_HI
#define APP_ROUND_HI 2
#endif
#include "tfn/audio.h"
#include "jingle/jingle.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "midi_bg/midi_bg.h"
#include "cr_music.h"
#include "cr_levels.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static const char *TAG = "coil";
#define SW     gfx_w
#define SH     gfx_h
#define CR_KEY "coil_"  // (the manifest: saves=coil_)

static int s_lang;
#define TR(en, he) (s_lang ? (he) : (en))

#include "cr_gfx.inc"
#include "cr_phys.inc"
#include "cr_game.inc"
#include "cr_draw.inc"
#include "cr_ui.inc"
