#pragma once
// STOCKS: the prices of up to 10 US stocks from Finnhub (finnhub.io), for the STOCKS app.
// The settings (the API key, the stocks, the update time, the local time zone) come from the board web page /stocks
// (web_stocks.c) and stay in the NVS (namespace "stocks"). The key never leaves the board again.
// A task fetches while the app shows (stocks_live): each round the market state (the server clock), the quote of each
// stock (the regular session: c, d, dp, pc) and, once in 12 h, the holidays of the exchange. In the pre-market and the
// after-hours session it also opens the Finnhub trade stream (a websocket) for some seconds: the free quote has no
// extended-hours price, the trades do.
#include <stdbool.h>
#include <stdint.h>
#include "tfn_extras.h"
#include "stocks_alert.h"
#include "stocks_time.h"

#define STK_MAX      10
#define STK_SYM_LEN  16
#define STK_NAME_LEN 40
#define STK_SPARK    120  // the samples of the small chart (one each round)
#define STK_TZ_LEN   48

typedef struct {
    char sym[STK_SYM_LEN];
    char name[STK_NAME_LEN];
    float c, d, dp, pc, hi, lo;  // the quote: the last price of the regular session, its change, the last close
    int64_t qt;                  // the time of the quote (unix s), 0 = no quote yet
    float ext;                   // the last trade of a pre-market / after-hours session (0 = none)
    int64_t ext_t;               // its time (unix s)
    uint16_t nspark;
    float spark[STK_SPARK];  // the price at each round of this trading day (the oldest first)
    // the alerts (stocks_alert.h; kept in the NVS)
    float al_lo, al_hi;        // the limits (0 = off)
    float ref;                 // the price when the limits were set
    float quiet;               // the price of the last KEEP of an offer (0 = none)
    uint8_t sound;             // the alert plays a sound
    int8_t lo_arm, hi_arm;     // (stk_watch_t)
    uint8_t fired;             // the last alert: STK_AL_ABOVE / STK_AL_BELOW (0 = none since the start)
    int64_t fired_t;           // its time (unix s)
    float fired_p, fired_lim;  // the price then, the limit
} stk_quote_t;

// An alert for the app (the app shows its own card; with the app closed, stocks.c shows a system message).
typedef struct {
    char sym[STK_SYM_LEN];
    uint8_t kind, sound;   // STK_AL_*, the sound is on
    float lim, price, dp;  // the limit, the price then, today's change in %
    int64_t t;
} stk_alert_t;

typedef struct {
    int n;  // the stocks
    bool key_set;
    bool wifi;                     // the board is on the WiFi
    int64_t now;                   // the time now (unix s; the clock of the server), 0 = not known yet
    int64_t updated;               // the last good round (unix s), 0 = none
    int64_t trades_at;             // the last time the trade stream was open (unix s), 0 = never
    int session;                   // STK_* now (stocks_time.c)
    int64_t sess_start, sess_end;  // its bounds (unix s)
    int64_t next_open;             // the next regular opening (09:30 New York; unix s), 0 = not known
    int interval_min;              // the update time
    char err[64];                  // the last fault ("" = none)
    char tz[STK_TZ_LEN];           // the local zone (POSIX TZ)
} stk_state_t;

// An extra (CONFIG_TFN_EXTRA_STOCKS): when it is off, stocks_init() is empty and the other functions are not there.
#if CONFIG_TFN_EXTRA_STOCKS
void stocks_init(void);
#else
static inline void stocks_init(void)
{
}
#endif

// The app: it shows the prices now (call it each frame). The task starts with the first call; it fetches only while
// the calls come (not with the screen off). stocks_stop: the app closes (the task ends after its round).
void stocks_live(void);
void stocks_stop(void);
// A copy of the data (out: STK_MAX entries). Returns the count.
int stocks_get(stk_quote_t *out, stk_state_t *st);
// The time now (unix s): the clock of the board, corrected by the server time of the last round. 0 = not known.
int64_t stocks_now(void);

// The settings (the web page). The answers: true = done; msg = the reason or the result (English).
bool stocks_set_key(const char *key, char *msg, int mmax);  // checks the key with Finnhub first; "" = no key
bool stocks_add(const char *sym, char *msg, int mmax);      // checks that the stock exists (msg = its name)
bool stocks_del(const char *sym);
bool stocks_order(const char *csv);     // the same symbols in a new order
bool stocks_set_interval(int minutes);  // 1, 2, 5, 10, 15 or 30
bool stocks_set_tz(const char *posix);  // a POSIX TZ text: it sets the home zone of the board (alarm.h)
// The alert limits of a stock (0 = off; lo < hi when both are on) and its sound; the price now becomes the reference of
// the offers, and the KEEP price goes. From the app task or the web server.
bool stocks_set_alert(const char *sym, float lo, float hi, bool sound, char *msg, int mmax);
// KEEP: the user does not want the offered limits; no new offer until the price moves STK_MOVE from the price now.
bool stocks_keep(const char *sym);
// The next alert for the app (true), with the app on the screen (stocks_live in the last 3 s); false = none.
bool stocks_take_alert(stk_alert_t *a);
// The live price of a quote for the alerts and the offers: the regular price in the open session, the extended price in a
// pre-market / after-hours session (0 when no trade of that session came), else the last price.
float stocks_price(const stk_quote_t *q, const stk_state_t *st);
// The sound of an alert (STK_AL_ABOVE: up, STK_AL_BELOW: down). Any task.
void stocks_chime(int kind);
// TEST (the web page): one alert of the stock on the real way, with its sound. False = no such stock.
bool stocks_test_alert(const char *sym);
// the settings and the data as JSON for the web page (never the key); returns the length
int stocks_json(char *out, int max);
