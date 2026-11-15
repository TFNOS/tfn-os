#pragma once
// STANDBY: the parts that its files share (standby.h tells the parts). Not an API of the apps.
#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"
#include "tfn/tz.h"

// ---------------------------------------------------------------- the colours (a system screen uses names)
#define THEME_STANDBY_TEXT      rgb(255, 255, 255)  // the time and the main text
#define THEME_STANDBY_TEXT_DIM  rgb(214, 220, 240)  // the date lines
#define THEME_STANDBY_SHADOW    rgb(0, 0, 0)        // the shadow under the text on the sky
#define THEME_STANDBY_TILE      rgb(10, 12, 32)     // a slot of GLANCE (with alpha on the sky)
#define THEME_STANDBY_TILE_HI   rgb(60, 66, 110)    // a slot under the finger
#define THEME_STANDBY_TILE_EDGE rgb(200, 210, 255)  // the edge of a slot (with alpha)
#define THEME_STANDBY_NIGHT     rgb(201, 115, 58)   // the clock of NIGHT (dim and warm)
#define THEME_STANDBY_NIGHT_BG  rgb(0, 0, 0)        // the black of NIGHT (an AMOLED pixel is off)
#define THEME_STANDBY_OUTSIDE   rgb(0, 0, 0)        // a round frame: outside the circle
#define THEME_STANDBY_ICON_SUN  rgb(255, 211, 106)  // the small weather icons
#define THEME_STANDBY_ICON_CL   rgb(230, 232, 255)
#define THEME_STANDBY_ICON_MN   rgb(217, 220, 255)
#define THEME_STANDBY_ICON_RN   rgb(150, 200, 255)

// ---------------------------------------------------------------- the settings (SETUP > STANDBY)
enum {
    SB_FACE_SKY,
    SB_FACE_GLANCE,
    SB_FACE_NIGHT,
    SB_FACES
};
// the kinds of a slot (the order of the SETUP pill)
enum {
    SL_WEATHER,
    SL_HEBREW,
    SL_NOTICES,
    SL_NEXT,
    SL_BATTERY,
    SL_STOCKS,
    SL_FRIENDS,
    SL_NONE,
    SL_KINDS
};
#define SB_SLOTS   4
#define SB_ACCENTS 5
typedef struct {
    uint8_t on;          // 1 = STANDBY shows (the default)
    uint8_t face;        // SB_FACE_*
    uint8_t autonight;   // 1 = NIGHT in the night hours (the default)
    uint8_t nfrom, nto;  // the night hours (23 and 6 by default)
    uint8_t accent;      // 0 .. SB_ACCENTS - 1
    uint8_t slot[SB_SLOTS];
} sb_cfg_t;
extern sb_cfg_t sb_cfg;
void sb_cfg_load(void);
void sb_cfg_save(void);
uint16_t sb_accent(void);            // the accent colour of the settings
const char *sb_accent_name(int i);   // AMBER, SKY, MINT, ROSE, WHITE
const char *sb_slot_name(int kind);  // the name of a slot kind (SETUP)
const char *sb_face_name(int face);
bool sb_he(void);  // the system language is Hebrew (SETUP > APP LANGUAGE)

// ---------------------------------------------------------------- the data (standby_data.c)
#define SBW_HOURS 8
typedef struct {
    uint32_t ver;
    int64_t at;  // unix s of the fetch (0 = no data)
    float lat, lon;
    int32_t off;  // the UTC offset of the place (s)
    float t, hi, lo, cloud, wind, wdir;
    int16_t code;  // the WMO weather code
    uint8_t day;
    uint8_t nh;
    int64_t h0;  // unix s of the first hour
    float ht[SBW_HOURS];
    uint8_t hcode[SBW_HOURS];
} sbw_t;

typedef struct {
    bool ok;
    float lat, lon;
    char name[48];
    bool israel;   // the holidays of Israel
    uint8_t cmin;  // the candle minutes before the sunset
} sb_place_t;

// the NEXT item: a running timer > an alarm in the next 12 h > the candles / havdalah > the sunrise / sunset
enum {
    NX_NONE,
    NX_TIMER,
    NX_ALARM,
    NX_CANDLES,
    NX_HAVDALAH,
    NX_SUNRISE,
    NX_SUNSET
};
typedef struct {
    bool time_ok;    // the board knows the time
    int64_t utc;     // unix s
    int off;         // the offset of the home zone now (s)
    tz_tm_t tm;      // the local time
    int hy, hm, hd;  // the Hebrew date (after the sunset: the next day)
    bool hleap;      // a Hebrew leap year (Adar I / Adar II)
    float elev;      // the height of the sun (degrees; 30 with no place)
    float moon;      // the age of the moon 0..1
    int nx_kind;     // NX_*
    int64_t nx_at;   // unix s of the NEXT item (a timer: its end)
    int shab_kind;   // the coming Shabbat or holiday: NX_CANDLES or NX_HAVDALAH (NX_NONE: no place)
    int64_t shab_at;
} sb_now_t;

void sb_data_init(void);
void sb_data_tick(int64_t now_ms, bool screen_on);  // the weather service (the main task)
void sb_data_fetch(void);                           // a fetch at the next tick (the console)
const sbw_t *sb_wx(void);                           // the last weather (at == 0: none)
const sb_place_t *sb_place(void);
void sb_now(sb_now_t *o);  // the moment (a cache of the day parts: cheap in each frame)
int sb_data_log(char *b, int n);

// the text of the weather code; the kind of the sky
const char *sb_cond(int code, bool he);
enum {
    SK_CLEAR,
    SK_PART,
    SK_CLOUD,
    SK_FOG,
    SK_DRIZZLE,
    SK_RAIN,
    SK_HEAVY,
    SK_SNOW,
    SK_STORM
};
int sb_sky_kind(int code);

// ---------------------------------------------------------------- the sky (standby_sky.c)
typedef struct {
    float elev, cloud, rain, snow, fog, storm, wind, moon;
} sb_sky_t;
void sb_sky_target(const sbw_t *w, const sb_now_t *n);  // the sky that it glides to
void sb_sky_step(float dt);
bool sb_sky_moves(void);                                // clouds, rain or snow move (10 frames a second; else 1)
void sb_sky_draw(float t_s);                            // the whole frame (a round frame: the circle; black outside)
void sb_sky_free(void);                                 // the PSRAM of the masks (STANDBY ends)
uint16_t sb_px_mix(uint16_t dst, uint16_t src, int a);  // a mix of two gfx colours (a = 0..256)
void sb_hdate(const sb_now_t *n, char *o, int max, bool year);  // "כ״ב בחשוון" (+ " תשפ״ז")

// ---------------------------------------------------------------- the frame (standby.c, standby_draw.c)
typedef struct {
    int16_t x, y, w, h;
    uint8_t kind;  // SL_*
} sb_hit_t;
extern int sb_hit_n;  // the slots of the last frame (the touch targets)
extern sb_hit_t sb_hit[SB_SLOTS];
extern int sb_period_ms;  // the time to the next frame (standby_draw.c sets it)
int sb_pressed(void);     // the slot under the finger (-1 = none)
void sb_draw_frame(int64_t now, float dt);
void sb_night_backlight(bool night);  // NIGHT: a low backlight
void sb_preview(void);                // SETUP > STANDBY > SHOW IT NOW
