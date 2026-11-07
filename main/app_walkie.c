// WALKIE: a walkie-talkie between two boards (ESP-NOW, net2.c; the friend from FRIENDS).
//
// - THE FRIENDS: the friends that are online are tiles (NEARBY / LAN / VPN); a tap calls the friend,
//   also when its board has another app open (walkie_call.c). The panel from the top edge: what this board does
//   when a friend calls (OPEN WALKIE / ASK ME / IGNORE) and the ring. END in the talk screen ends the talk
//   (two taps: the first asks TAP AGAIN).
// - PUSH TO TALK: hold the big button and talk; the other board plays it. One talker at a time:
//   a press while the other board talks gives a busy sound. The release plays a short "over" sound.
// - MONITOR: "I LISTEN TO THEM" (the microphone of the other board comes to this board) and
//   "THEY LISTEN TO ME" (the other way). Each board sets both switches, for both boards (a parent
//   and a kid: the parent manages it from either board). A talk pauses the monitor (no feedback).
// - The sound: the microphone (24 kHz) to 8 kHz, IMA ADPCM (4 bits), 50 ms in a packet (20 packets
//   a second). The other board decodes it, makes 24 kHz again and plays it through the voice
//   stream of audio.c, after a start buffer of 120 ms. A lost packet = 50 ms of silence.
// - The HOST (net2: the lower MAC) keeps the state (who talks, the two monitor switches) and sends
//   it each 150 ms and at each change; the CLIENT sends its requests.

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "esp_log.h"
#include "audio.h"
#include "friends.h"
#include "gfx.h"
#include "input.h"
#include "kfont.h"
#include "lan.h"
#include "link.h"
#include "mfont.h"
#include "net2.h"
#include "settings.h"
#include "uikit.h"
#include "walkie_call.h"
#include "web_walkie.h"

#define NET_ID  'W'
#define K_CMD   (N2_K_APP + 0)  // the CLIENT: a request {CMD_*, the value}
#define K_VOICE (N2_K_APP + 1)  // 50 ms of voice
#define K_ALIVE (N2_K_APP + 2)  // the CLIENT: "still here" (the HOST gets no other packet when all is quiet)
enum {
    CMD_TALK = 1,
    CMD_MON_H2C,
    CMD_MON_C2H
};
enum {
    TK_NONE,
    TK_HOST,
    TK_CLIENT
};

#define SR8      8000
#define PKT_N    400  // samples at 8 kHz in a packet (50 ms)
#define START_MS 120  // the start buffer of the receiver

typedef struct {
    uint8_t talk;              // TK_*
    uint8_t mon_h2c, mon_c2h;  // the monitor switches (HOST to CLIENT, CLIENT to HOST)
    uint8_t seq;               // a new number at each change
} wstate_t;

typedef struct {
    uint16_t seq;
    int16_t pred;
    uint8_t idx, n2;  // the ADPCM state at the start; n2 = the bytes of data
    uint8_t data[PKT_N / 2];
} wvoice_t;

static wstate_t s_ws;             // the state (the HOST owns it; the CLIENT gets it)
static bool friend_on_lan(void);  // (QUIET: below)
static n2_role_t s_role;
static int64_t s_now, s_state_at, s_rx_at, s_busy_at, s_ask_at, s_alive_at;
static bool s_down;  // the finger is on the talk button
static bool s_want;  // the CLIENT asked to talk
static char s_peer_name[FRIEND_NAME];
static float s_rx_level, s_tx_level;

// ---------------------------------------------------------------- IMA ADPCM
static const int16_t STEP[89] = {
        7,    8,     9,     10,    11,    12,    13,    14,    16,    17,    19,    21,    23,    25,   28,
        31,   34,    37,    41,    45,    50,    55,    60,    66,    73,    80,    88,    97,    107,  118,
        130,  143,   157,   173,   190,   209,   230,   253,   279,   307,   337,   371,   408,   449,  494,
        544,  598,   658,   724,   796,   876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878, 2066,
        2272, 2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,  5894,  6484,  7132,  7845, 8630,
        9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};
static const int8_t IDX[16] = {-1, -1, -1, -1, 2, 4, 6, 8, -1, -1, -1, -1, 2, 4, 6, 8};
typedef struct {
    int pred, idx;
} adpcm_t;

static int adpcm_step(adpcm_t *s, int code)
{
    int st = STEP[s->idx], d = st >> 3;
    if (code & 4) {
        d += st;
    }
    if (code & 2) {
        d += st >> 1;
    }
    if (code & 1) {
        d += st >> 2;
    }
    s->pred += (code & 8) ? -d : d;
    if (s->pred > 32767) {
        s->pred = 32767;
    }
    if (s->pred < -32768) {
        s->pred = -32768;
    }
    s->idx += IDX[code];
    if (s->idx < 0) {
        s->idx = 0;
    }
    if (s->idx > 88) {
        s->idx = 88;
    }
    return s->pred;
}
static int adpcm_code(adpcm_t *s, int x)
{
    int st = STEP[s->idx], diff = x - s->pred, code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    if (diff >= st) {
        code |= 4;
        diff -= st;
    }
    if (diff >= st >> 1) {
        code |= 2;
        diff -= st >> 1;
    }
    if (diff >= st >> 2) {
        code |= 1;
    }
    adpcm_step(s, code);
    return code;
}

// ---------------------------------------------------------------- the microphone (the mic task)
// 24 kHz to 8 kHz: the mean of 3 samples, with a gain of 2 (the microphone is quiet)
#define MRING 4096
static int16_t s_mring[MRING];
static volatile uint32_t s_mw, s_mr;
static int s_macc, s_mcnt;
static void mic_tap(const int16_t *s, int n)
{
    uint32_t w = s_mw;
    for (int i = 0; i < n; i++) {
        s_macc += s[i];
        if (++s_mcnt < 3) {
            continue;
        }
        int v = s_macc * 2 / 3;
        s_macc = s_mcnt = 0;
        if (v > 32767) {
            v = 32767;
        }
        if (v < -32768) {
            v = -32768;
        }
        if (w - s_mr < MRING) {
            s_mring[w++ % MRING] = (int16_t)v;
        }
    }
    s_mw = w;
}

// ---------------------------------------------------------------- send and receive
static adpcm_t s_enc;
static uint16_t s_tx_seq;
static int me(void)
{
    return s_role == N2_HOST ? TK_HOST : TK_CLIENT;
}
static bool mon_from_me(void)
{
    return s_role == N2_HOST ? s_ws.mon_h2c : s_ws.mon_c2h;
}
static bool mon_from_them(void)
{
    return s_role == N2_HOST ? s_ws.mon_c2h : s_ws.mon_h2c;
}
static int them(void)
{
    return s_role == N2_HOST ? TK_CLIENT : TK_HOST;
}
// this board sends its microphone: it talks, or the monitor to the other board is on (no talk)
static bool i_send(void)
{
    return s_ws.talk == me() || (s_ws.talk == TK_NONE && mon_from_me());
}
// this board plays the voice of the other board
static bool i_play(void)
{
    return s_ws.talk == them() || (s_ws.talk == TK_NONE && mon_from_them());
}

static void send_voice(void)
{
    // whole packets of what the microphone has; nothing when this board does not send
    while (s_mw - s_mr >= PKT_N) {
        if (!i_send()) {
            s_mr = s_mw;
            return;
        }
        wvoice_t p;
        p.seq = s_tx_seq++;
        p.pred = (int16_t)s_enc.pred;
        p.idx = (uint8_t)s_enc.idx;
        p.n2 = PKT_N / 2;
        float e = 0;
        for (int i = 0; i < PKT_N; i += 2) {
            int a = s_mring[s_mr++ % MRING], b = s_mring[s_mr++ % MRING];
            e += (float)a * a + (float)b * b;
            p.data[i / 2] = (uint8_t)(adpcm_code(&s_enc, a) | adpcm_code(&s_enc, b) << 4);
        }
        s_tx_level = sqrtf(e / PKT_N) / 32768.0f;
        net2_send(K_VOICE, &p, (int)sizeof p);
    }
}

static uint16_t s_rx_seq;
static bool s_rx_on;
static int16_t s_last;  // the last sample (for the line to the next one)
static void play8(const int16_t *x, int n)
{
    // 8 kHz to 24 kHz: a straight line between the samples
    int16_t out[PKT_N * 3];
    int k = 0;
    for (int i = 0; i < n; i++) {
        int a = s_last, b = x[i];
        out[k++] = (int16_t)((2 * a + b) / 3);
        out[k++] = (int16_t)((a + 2 * b) / 3);
        out[k++] = (int16_t)b;
        s_last = x[i];
    }
    audio_stream_write(out, k);
}
static void play_silence(int n8)
{
    int16_t z[PKT_N];
    memset(z, 0, sizeof z);
    while (n8 > 0) {
        int n = n8 > PKT_N ? PKT_N : n8;
        play8(z, n);
        n8 -= n;
    }
}
static void rx_voice(const wvoice_t *p)
{
    if (!i_play()) {
        return;  // (this board talks: no sound back)
    }
    if (!s_rx_on || s_now - s_rx_at > 400) {
        // a new talk: the start buffer
        audio_stream_clear();
        s_last = 0;
        play_silence(SR8 * START_MS / 1000);
        s_rx_on = true;
    } else {
        uint16_t gap = (uint16_t)(p->seq - s_rx_seq - 1);
        if (gap > 0 && gap < 6) {
            play_silence(gap * PKT_N);  // lost packets
        }
    }
    s_rx_seq = p->seq;
    s_rx_at = s_now;
    // the clock of the other board can be a little faster: keep the buffer short
    if (audio_stream_fill() > AUDIO_SAMPLE_RATE * 4 / 10) {
        return;
    }
    adpcm_t d = {p->pred, p->idx};
    int16_t x[PKT_N];
    float e = 0;
    int n = p->n2 * 2 > PKT_N ? PKT_N : p->n2 * 2;
    for (int i = 0; i < n; i += 2) {
        x[i] = (int16_t)adpcm_step(&d, p->data[i / 2] & 15);
        x[i + 1] = (int16_t)adpcm_step(&d, p->data[i / 2] >> 4);
        e += (float)x[i] * x[i] + (float)x[i + 1] * x[i + 1];
    }
    s_rx_level = sqrtf(e / (float)n) / 32768.0f;
    play8(x, n);
}

// ---------------------------------------------------------------- the state
static void snd_chirp(void)
{
    sfx_tone(1400, 1900, 70, W_SINE, 26);
}
static void snd_over(void)
{
    sfx_tone(3000, 2000, 90, W_NOISE, 18);
}
static void snd_busy(void)
{
    sfx_tone(330, 330, 160, W_SQUARE, 24);
}

static void state_changed(const wstate_t *old)
{
    // the sounds of a talk of the other board, on this board
    if (old->talk != them() && s_ws.talk == them()) {
        snd_chirp();
    }
    if (old->talk == them() && s_ws.talk != them()) {
        snd_over();
    }
    if (old->talk == me() && s_ws.talk != me()) {
        s_want = false;
    }
    if (s_ws.talk != old->talk) {
        s_mr = s_mw;
        s_rx_on = false;
    }
    ESP_LOGI("walkie", "state: talk %d, monitor host>client %d, client>host %d", s_ws.talk, s_ws.mon_h2c, s_ws.mon_c2h);
}
static void host_send(void)
{
    net2_send_state(&s_ws, sizeof s_ws, s_now);
    s_state_at = s_now;
}
static void host_apply(int who, int cmd, int val)
{
    wstate_t old = s_ws;
    switch (cmd) {
    case CMD_TALK:
        if (val && s_ws.talk == TK_NONE) {
            s_ws.talk = (uint8_t)who;
        }
        if (!val && s_ws.talk == who) {
            s_ws.talk = TK_NONE;
        }
        break;
    case CMD_MON_H2C:
        s_ws.mon_h2c = val ? 1 : 0;
        break;
    case CMD_MON_C2H:
        s_ws.mon_c2h = val ? 1 : 0;
        break;
    }
    if (memcmp(&old, &s_ws, sizeof s_ws)) {
        s_ws.seq++;
        state_changed(&old);
        host_send();
    }
}
// a request of this board (the HOST does it, the CLIENT asks)
static void request(int cmd, int val)
{
    if (s_role == N2_HOST) {
        host_apply(TK_HOST, cmd, val);
    } else if (s_role == N2_CLIENT) {
        uint8_t b[2] = {(uint8_t)cmd, (uint8_t)(val ? 1 : 0)};
        net2_send(K_CMD, b, 2);
        s_ask_at = s_now;
    }
}

static void on_rx(uint8_t kind, const uint8_t *data, int len)
{
    if (kind == K_CMD && len >= 2 && s_role == N2_HOST) {
        host_apply(TK_CLIENT, data[0], data[1]);
    } else if (kind == K_VOICE && len >= (int)sizeof(wvoice_t) - PKT_N / 2) {
        wvoice_t p;  // (a copy: the packet data is not aligned)
        memset(&p, 0, sizeof p);
        memcpy(&p, data, (size_t)(len < (int)sizeof p ? len : (int)sizeof p));
        if (p.n2 * 2 > (len - ((int)sizeof p - PKT_N / 2)) * 2) {
            p.n2 = (uint8_t)(len - ((int)sizeof p - PKT_N / 2));
        }
        rx_voice(&p);
    } else if (kind == N2_K_STATE && len >= (int)sizeof(wstate_t) && s_role == N2_CLIENT) {
        wstate_t old = s_ws;
        memcpy(&s_ws, data, sizeof s_ws);
        if (memcmp(&old, &s_ws, sizeof s_ws)) {
            state_changed(&old);
        }
    }
}

// ---------------------------------------------------------------- the screen
// The look: the friends that are online are tiles, with a
// colour for the way to each (NEARBY = the radio, LAN = the home network, VPN = the tunnel) and its name on the border
// of the tile. A tap on a tile calls the friend (walkie_call.c). A board that is not a friend gets a tile to add it.
// The settings panel slides down from the top edge (WHEN A FRIEND CALLS: OPEN WALKIE / ASK ME / IGNORE, THE RING).
// The talk screen: the friend at the top (END ends the talk), the big talk button, the two monitor switches.
typedef struct {
    int x, y, w, h;
} rc_t;
enum {
    VIA_NEAR,
    VIA_LAN,
    VIA_VPN
};
typedef struct {
    int f;  // the friend (-1 = a board that is not a friend)
    uint8_t via, mac[6];
    char name[FRIEND_NAME], app[12];
} tile_t;
#define TILES_MAX (FRIENDS_MAX + FRIENDS_NEW_MAX)
static tile_t s_tl[TILES_MAX];
static int s_nt, s_page, s_pages, s_dx0, s_dy0;
static int64_t s_gather_at, s_added_at, s_tick_at;
static bool s_fdown, s_panel;
static float s_pan;  // the panel: 0 = closed, 1 = open
static char s_added[FRIEND_NAME];
static wkc_state_t s_cs_last;
static bool s_simple;  // TALKIE (the simple screen; any friend pairs at once)

static const uint16_t *pal(void)
{
    static uint16_t c[8];
    if (!c[0]) {
        c[0] = rgb(12, 14, 26);     // the ground
        c[1] = rgb(255, 196, 96);   // the title
        c[2] = rgb(150, 156, 190);  // dim text
        c[3] = rgb(245, 246, 255);  // text
        c[4] = rgb(70, 210, 130);   // NEARBY
        c[5] = rgb(80, 160, 255);   // LAN
        c[6] = rgb(185, 125, 255);  // VPN
        c[7] = rgb(24, 26, 44);     // a card
    }
    return c;
}
#define C_BG    (pal()[0])
#define C_TITLE (pal()[1])
#define C_DIM   (pal()[2])
#define C_TXT   (pal()[3])
#define C_CARD  (pal()[7])
static uint16_t via_col(int v)
{
    return pal()[4 + v];
}
static const char *via_txt(int v)
{
    return v == VIA_NEAR ? "NEARBY" : v == VIA_LAN ? "LAN" : "VPN";
}

// the friends that are online now (and the boards that are not friends)
static void gather(void)
{
    int near[FRIENDS_MAX], nn = friends_near(near, FRIENDS_MAX);
    lan_peer_t lp[LAN_PEERS];
    int nl = lan_peers(lp, LAN_PEERS);
    s_nt = 0;
    for (int i = 0; i < friends_count() && s_nt < TILES_MAX; i++) {
        const friend_t *fr = friends_get(i);
        int via = -1;
        const char *app = "";
        for (int k = 0; k < nn; k++) {
            if (near[k] == i) {
                via = VIA_NEAR;
            }
        }
        for (int k = 0; k < nl; k++) {
            if (memcmp(lp[k].mac, fr->mac, 6)) {
                continue;
            }
            if (via < 0) {
                via = lp[k].vpn ? VIA_VPN : VIA_LAN;
            }
            app = lp[k].app;
            // a new name (NICKNAME in the manager): the friend list takes it (saved once)
            if (lp[k].name[0] && strncmp(lp[k].name, fr->name, sizeof lp[k].name - 1)) {
                friends_add(fr->mac, lp[k].name);
            }
        }
        if (via < 0) {
            continue;
        }
        tile_t *t = &s_tl[s_nt++];
        t->f = i;
        t->via = (uint8_t)via;
        memcpy(t->mac, fr->mac, 6);
        snprintf(t->name, sizeof t->name, "%s", fr->name);
        snprintf(t->app, sizeof t->app, "%s", app);
    }
    friend_new_t nb[FRIENDS_NEW_MAX];
    int k = friends_new_near(nb, FRIENDS_NEW_MAX);
    for (int i = 0; i < k && s_nt < TILES_MAX; i++) {
        tile_t *t = &s_tl[s_nt++];
        *t = (tile_t){.f = -1, .via = VIA_NEAR};
        memcpy(t->mac, nb[i].mac, 6);
        snprintf(t->name, sizeof t->name, "%s", nb[i].name);
    }
    for (int i = 0; i < nl && s_nt < TILES_MAX; i++) {
        if (friends_find(lp[i].mac) >= 0) {
            continue;
        }
        bool dup = false;
        for (int j = 0; j < s_nt; j++) {
            if (s_tl[j].f < 0 && !memcmp(s_tl[j].mac, lp[i].mac, 6)) {
                dup = true;
            }
        }
        if (dup) {
            continue;
        }
        tile_t *t = &s_tl[s_nt++];
        *t = (tile_t){.f = -1, .via = (uint8_t)(lp[i].vpn ? VIA_VPN : VIA_LAN)};
        memcpy(t->mac, lp[i].mac, 6);
        snprintf(t->name, sizeof t->name, "%s", lp[i].name[0] ? lp[i].name : "A BOARD");
        snprintf(t->app, sizeof t->app, "%s", lp[i].app);
    }
}
// the way to friend f now (-1 = not online)
static int via_of(int f)
{
    for (int i = 0; i < s_nt; i++) {
        if (s_tl[i].f == f) {
            return s_tl[i].via;
        }
    }
    return -1;
}

// ---- the places on the screen
// the big look (bigger fonts and icons): the native frame of the AMOLED, and the Tab5 frame (640 x 360)
#if BOARD_UI_TABLET
#define WK_TAB5 1
#else
#define WK_TAB5 0
#endif
#define WK_BIG() (WK_TAB5 || gfx_native())
typedef struct {
    rc_t area;
    int cols, rows, gap;
} grid_t;
// the round frame of the panel (APP_ROUND_HI): 412 x 412 on the Watcher, 466 x 466 on the AMOLED-1.75C; the numbers of
// the round layout are for 412, WR() scales them (on the Watcher they stay the same)
#define WR(v) ((v) * gfx_w / 412)
static grid_t grid(void)
{
#if BOARD_BAR_SCREEN
    return (grid_t){{12, 44, 616, 116}, 4, 1, 12};
#else
    if (gfx_round()) {
        return (grid_t){{WR(78), WR(118), WR(256), WR(196)}, 2, 2, WR(10)};
    }
    if (gfx_native()) {
        return (grid_t){{16, 78, 416, 262}, 2, 2, 14};
    }
    if (WK_TAB5) {
        return (grid_t){{16, 72, gfx_w - 32, gfx_h - 96}, 3, 2, 14};  // (the Tab5: 6 friends on a page)
    }
    return (grid_t){{10, 50, 300, 168}, 2, 2, 10};
#endif
}
// the place of tile slot of a page with n tiles (a row that is not full is in the middle)
static rc_t tile_rc(const grid_t *g, int slot, int n)
{
    int c = slot % g->cols, r = slot / g->cols;
    int tw = (g->area.w - (g->cols - 1) * g->gap) / g->cols, th = (g->area.h - (g->rows - 1) * g->gap) / g->rows;
    int in_row = n - r * g->cols, dx = in_row < g->cols ? (g->cols - in_row) * (tw + g->gap) / 2 : 0;
    return (rc_t){g->area.x + dx + c * (tw + g->gap), g->area.y + r * (th + g->gap), tw, th};
}
static const char *peer_name(void)
{
    return s_peer_name;
}
static int top_edge(void)
{
    return gfx_round() ? WR(96) : BOARD_BAR_SCREEN ? 36 : 44;
}  // a swipe down from here opens the panel

// ---- the panel
#define P_ROWS 2
static const char *const P_ROW[P_ROWS] = {"WHEN A FRIEND CALLS", "THE RING OF A CALL"};
static const int P_N[P_ROWS] = {3, 2};
static const char *const P_OPT[P_ROWS][3] = {{"OPEN WALKIE", "ASK ME", "IGNORE"}, {"ON", "OFF"}};
static const char *const P_HELP[3] = {"WALKIE OPENS BY ITSELF", "A QUESTION WITH A RING: ANSWER OR NOT NOW",
                                      "THE CALLER SEES \"NOT NOW\""};
static int p_value(int r)
{
    return r == 0 ? walkie_call_mode() : !walkie_call_ring();
}
static int panel_h(void)
{
#if BOARD_BAR_SCREEN
    return 160;
#else
    return gfx_round() ? WR(300) : gfx_native() ? 280 : WK_TAB5 ? 290 : 210;
#endif
}
static rc_t p_button(int r, int k, int y0)
{
    int n = P_N[r], x0, w, y, h, gap = 8;
#if BOARD_BAR_SCREEN
    x0 = 214;
    w = 640 - x0 - 14;
    y = y0 + 14 + r * 58;
    h = 34;
#else
    if (gfx_round()) {
        x0 = WR(70);
        w = WR(272);
        y = y0 + WR(108) + r * WR(88);
        h = WR(52);
        gap = WR(6);
    } else if (gfx_native()) {
        x0 = 20;
        w = 408;
        y = y0 + 52 + r * 96;
        h = 56;
    }  // (round 3: 56 px = 4.4 mm, +8 px each side)
    else if (WK_TAB5) {
        x0 = 20;
        w = gfx_w - 40;
        y = y0 + 52 + r * 104;
        h = 60;
        gap = 12;
    }  // (the Tab5)
    else {
        x0 = 10;
        w = 300;
        y = y0 + 32 + r * 76;
        h = 36;
        gap = 6;
    }
#endif
    int gw = (w - (n - 1) * gap) / n;
    return (rc_t){x0 + k * (gw + gap), y, gw, h};
}
static void draw_panel(void)
{
    int H = panel_h(), y0 = (int)(-H * (1 - s_pan));
    if (y0 <= -H) {
        return;
    }
    int bot = y0 + H;
    if (bot > 0) {
        gfx_darken_rect(0, bot > gfx_h ? gfx_h : bot, gfx_w, gfx_h, 1);
    }
    ui_rrect(0, y0 - 20, gfx_w, H + 20, 18, rgb(20, 22, 38));
    gfx_fill_rect(0, bot - 2, gfx_w, 2, rgb(60, 66, 100));
    ui_rrect(gfx_w / 2 - 18, bot - 10, 36, 4, 2, C_DIM);  // the handle
    for (int r = 0; r < P_ROWS; r++) {
        rc_t b0 = p_button(r, 0, y0);
#if BOARD_BAR_SCREEN
        mf_text(&MF_LABEL, 14, b0.y + 22, P_ROW[r], C_TXT, 255, 1);
#else
        if (gfx_round()) {
            mf_text_c(&MF_TINY, gfx_w / 2, b0.y - 6, P_ROW[r], C_TXT, 255, 1);
        } else {
            mf_text(WK_BIG() ? &MF_LABEL : &MF_TINY, b0.x + 2, b0.y - 6, P_ROW[r], C_TXT, 255, 1);
        }
#endif
        for (int k = 0; k < P_N[r]; k++) {
            rc_t b = p_button(r, k, y0);
            bool on = p_value(r) == k;
            ui_rbox(b.x, b.y, b.w, b.h, 10, on ? C_TITLE : rgb(34, 38, 62), on ? C_TITLE : rgb(70, 76, 110), 1);
            const mfont_t *f = WK_BIG() && !gfx_round() ? &MF_LABEL : &MF_TINY;
            mf_text_c(f, b.x + b.w / 2, b.y + b.h / 2 + f->asc / 2, P_OPT[r][k], on ? rgb(30, 24, 10) : C_TXT, 255, 1);
        }
        if (r == 0) {
            rc_t b = p_button(0, 0, y0), e = p_button(0, P_N[0] - 1, y0);
            int cx = (b.x + e.x + e.w) / 2;
            mf_text_c(&MF_TINY, cx, b.y + b.h + 14, P_HELP[walkie_call_mode()], C_DIM, 255, 1);
        }
    }
    char me[48];
    snprintf(me, sizeof me, "THIS BOARD: %s", friends_my_name());
    int ny = y0 + H - (gfx_round() ? WR(34) : 22);  // (the round frame: under the ON / OFF row)
#if BOARD_BAR_SCREEN
    ui_text(&KF_LABEL, 14, y0 + 52 + 58, me, C_DIM);
    mf_text(&MF_TINY, 14, y0 + 52 + 58 + 18, "SWIPE UP: CLOSE", C_DIM, 255, 1);
#else
    ui_text_c(&KF_LABEL, gfx_w / 2, ny, me, C_DIM);
#endif
}
static bool panel_tap(int x, int y)
{
    for (int r = 0; r < P_ROWS; r++) {
        for (int k = 0; k < P_N[r]; k++) {
            rc_t b = p_button(r, k, 0);
            if (x < b.x - 4 || x >= b.x + b.w + 4 || y < b.y - 8 || y >= b.y + b.h + 8) {
                continue;
            }
            if (r == 0) {
                walkie_call_set_mode(k);
            } else {
                walkie_call_set_ring(k == 0);
            }
            sfx_play(SFX_SWITCH);
            return true;
        }
    }
    return y < panel_h();
}

// ---- the lobby: the tiles
static void avatar(int cx, int cy, int r, const char *name, uint16_t col, bool plus)
{
    ui_circle(cx, cy, r, col, 255);
    if (plus) {
        int a = r / 2, t = r > 20 ? 4 : 3;
        gfx_fill_rect(cx - a, cy - t / 2, 2 * a, t, C_BG);
        gfx_fill_rect(cx - t / 2, cy - a, t, 2 * a, C_BG);
        return;
    }
    char ini[4];
    ui_initial(name, ini);
    const kfont_t *f = r >= 30 ? &KF_TITLE : r >= 20 ? &KF_BIG : &KF_TEXT;
    ui_text_c(f, cx, cy + f->asc / 2, ini, rgb(24, 22, 40));
}
// the name in the width w: the biggest font that fits, else the smallest font with the end cut ("..")
static void name_text(int cx, int base, const char *name, int w, uint16_t c)
{
    static const kfont_t *const NF[] = {&KF_TEXT, &KF_NAME, &KF_LABEL};  // (17, 15, 13 px)
    const kfont_t *f = ui_text_fit(NF, 3, name, w);
    if (ui_text_w(f, name) <= w) {
        ui_text_c(f, cx, base, name, c);
        return;
    }
    char b[FRIEND_NAME + 3];
    int n = (int)strlen(name);
    b[0] = 0;
    while (n > 0) {
        n--;
        while (n > 0 && ((uint8_t)name[n] & 0xC0) == 0x80) {
            n--;  // (a whole UTF-8 letter)
        }
        snprintf(b, sizeof b, "%.*s..", n, name);
        if (ui_text_w(f, b) <= w) {
            break;
        }
    }
    ui_text_c(f, cx, base, b, c);
}
static void tile_state(const tile_t *t, int64_t now, char *out, int n, uint16_t *col)
{
    int cf;
    int64_t since;
    wkc_state_t cs = walkie_call_state(&cf, &since);
    *col = C_DIM;
    if (t->f < 0) {
        snprintf(out, (size_t)n, "TAP TO ADD");
        *col = C_TITLE;
        return;
    }
    if (cf == t->f && cs == WKC_CALLING) {
        if ((now - since) / 1500 % 2) {
            snprintf(out, (size_t)n, "TAP TO STOP");
        } else {
            snprintf(out, (size_t)n, "CALLING%.*s", (int)(now / 350 % 4), "...");
        }
        *col = C_TXT;
        return;
    }
    if (cf == t->f && cs != WKC_IDLE && now - since < 3500) {
        snprintf(out, (size_t)n, "%s", cs == WKC_BUSY ? "BUSY NOW" : cs == WKC_DECLINED ? "NOT NOW" : "NO ANSWER");
        *col = rgb(255, 140, 140);
        return;
    }
    if (!strcmp(t->app, "WALKIE") || !strcmp(t->app, "TALKIE")) {
        snprintf(out, (size_t)n, "READY TO TALK");
        *col = via_col(VIA_NEAR);
    } else if (!strcmp(t->app, "MENU")) {
        snprintf(out, (size_t)n, "IN THE MENU");
    } else if (t->app[0]) {
        snprintf(out, (size_t)n, "IN %s", t->app);
    } else {
        snprintf(out, (size_t)n, "NEAR YOU");
    }
}
static void draw_tile(const tile_t *t, rc_t r, int64_t now)
{
    uint16_t col = via_col(t->via);
    int cf;
    bool calling = t->f >= 0 && walkie_call_state(&cf, NULL) == WKC_CALLING && cf == t->f;
    int rad = r.h >= 100 ? 16 : 12;
    if (t->f < 0) {
        // a board that is not a friend: a dashed frame
        ui_rrect(r.x, r.y, r.w, r.h, rad, ui_mix(C_BG, col, 24));
        for (int x = r.x + rad; x < r.x + r.w - rad; x += 12) {
            gfx_fill_rect(x, r.y, 6, 2, col);
            gfx_fill_rect(x, r.y + r.h - 2, 6, 2, col);
        }
        for (int y = r.y + rad; y < r.y + r.h - rad; y += 12) {
            gfx_fill_rect(r.x, y, 2, 6, col);
            gfx_fill_rect(r.x + r.w - 2, y, 2, 6, col);
        }
    } else {
        int glow = calling ? 70 + (int)(40 * sinf(now / 180.0f)) : 46;
        ui_rbox(r.x, r.y, r.w, r.h, rad, ui_mix(C_BG, col, glow), calling ? C_TXT : col, 2);
    }
    // the way, on the top border
    const char *lab = via_txt(t->via);
    int lw = mf_width(&MF_TINY, lab, 1) + 12;
    ui_rrect(r.x + 12, r.y - 7, lw, 15, 6, col);
    mf_text(&MF_TINY, r.x + 18, r.y + 4, lab, rgb(10, 12, 22), 255, 1);
    // the avatar over the name (a tall tile), or at the left (a low tile)
    bool tall = r.h >= 70;
    int ar, acx, acy, tcx, tw, nby, sby;
    if (tall && r.h >= 100) {
        ar = r.h / 5 < r.w / 5 ? r.h / 5 : r.w / 5;
        acx = r.x + r.w / 2;
        acy = r.y + 12 + ar + 2;
        tcx = acx;
        tw = r.w - 12;
        nby = acy + ar + 8 + KF_TEXT.asc;
        sby = nby + (r.h >= 120 ? 20 : 16);
    } else if (tall) {  // a small tile: a small avatar, the name below it in the whole width
        ar = r.h / 6;
        acx = r.x + r.w / 2;
        acy = r.y + 9 + ar;
        tcx = acx;
        tw = r.w - 10;
        nby = acy + ar + 4 + KF_NAME.asc;
        sby = nby + 14;
    } else {
        ar = r.h * 20 / 100;
        acx = r.x + 10 + ar;
        acy = r.y + r.h / 2 + 2;
        int tx = acx + ar + 8;
        tw = r.x + r.w - 6 - tx;
        tcx = tx + tw / 2;
        nby = acy + 2;
        sby = acy + 18;
    }
    if (calling) {
        for (int k = 0; k < 2; k++) {
            float ph = fmodf(now / 1100.0f + k * 0.5f, 1.0f);
            ui_ring(acx, acy, ar + 3 + (int)(ph * ar * 0.7f), 2, C_TXT, (int)(220 * (1 - ph)));
        }
    }
    avatar(acx, acy, ar, t->name, t->f < 0 ? ui_mix(C_BG, col, 150) : ui_name_colour(t->name), t->f < 0);
    name_text(tcx, nby, t->name, tw, C_TXT);
    char st[24];
    uint16_t sc;
    tile_state(t, now, st, sizeof st, &sc);
    mf_text_c(&MF_TINY, tcx, sby, st, sc, 255, 1);
}
static void draw_empty(const grid_t *g, int64_t now)
{
    int cx = gfx_w / 2, cy = g->area.y + g->area.h * 30 / 100, u = WK_BIG() ? 2 : 1;
    // two radios, and a dot that goes from one to the other
    for (int s = -1; s <= 1; s += 2) {
        int x = cx + s * 46 * u;
        ui_rbox(x - 10 * u, cy - 14 * u, 20 * u, 30 * u, 5 * u, rgb(40, 44, 70), C_DIM, 1);
        gfx_fill_rect(x + s * 6 * u, cy - 24 * u, 2 * u, 11 * u, C_DIM);
        ui_circle(x, cy + 4 * u, 4 * u, C_TITLE, 200);
    }
    float ph = fmodf(now / 1400.0f, 1.0f);
    for (int k = 0; k < 3; k++) {
        float q = fmodf(ph + k / 3.0f, 1.0f);
        ui_circle(cx - 30 * u + (int)(60 * u * q), cy - 6 * u - (int)(10 * u * sinf(q * 3.14159f)), 2 * u,
                  via_col(VIA_LAN), (int)(255 * sinf(q * 3.14159f)));
    }
    int ty = cy + 22 * u + KF_TEXT.asc;
    ui_text_c(&KF_TEXT, cx, ty, "NO FRIEND IS ONLINE", C_TXT);
    const char *l2 = friends_count() ? "A FRIEND SHOWS HERE" : "ADD A FRIEND IN SETTINGS > FRIENDS";
    const char *l3 = friends_count() ? "WHEN ITS BOARD IS ON" : "OR IN THE MANAGER";
    ui_text_c(&KF_LABEL, cx, ty + KF_TEXT.line, l2, C_DIM);
    ui_text_c(&KF_LABEL, cx, ty + KF_TEXT.line + KF_LABEL.line, l3, C_DIM);
}
static void draw_top(int64_t now)
{
    (void)now;
#if BOARD_BAR_SCREEN
    mf_text(&MF_N20, 14, 27, "WALKIE", C_TITLE, 255, 1);
    char me[40];
    snprintf(me, sizeof me, "%s", friends_my_name());
    ui_text(&KF_LABEL, gfx_w - 14 - ui_text_w(&KF_LABEL, me), 24, me, C_DIM);
    ui_rrect(gfx_w / 2 - 18, 4, 36, 4, 2, C_DIM);
    mf_text_c(&MF_TINY, gfx_w / 2, 22, "SETTINGS", C_DIM, 255, 1);
#else
    if (gfx_round()) {
        ui_rrect(gfx_w / 2 - 18, WR(30), 36, 4, 2, C_DIM);
        mf_text_c(&MF_TINY, gfx_w / 2, WR(50), "SETTINGS", C_DIM, 255, 1);
        mf_text_c(&MF_N24, gfx_w / 2, WR(84), "WALKIE", C_TITLE, 255, 1);
        return;
    }
    const mfont_t *tf = WK_BIG() ? &MF_N28 : &MF_N20;
    int base = WK_BIG() ? 48 : 30;
    mf_text(tf, WK_BIG() ? 18 : 12, base, "WALKIE", C_TITLE, 255, 1);
    ui_rrect(gfx_w / 2 - 18, 4, 36, 4, 2, C_DIM);
    char me[40];
    snprintf(me, sizeof me, "%s", friends_my_name());
    ui_text(&KF_LABEL, gfx_w - (WK_BIG() ? 18 : 10) - ui_text_w(&KF_LABEL, me), base - 2, me, C_DIM);
#endif
}
static void draw_lobby(int64_t now)
{
    grid_t g = grid();
    draw_top(now);
    int per = g.cols * g.rows;
    s_pages = s_nt ? (s_nt + per - 1) / per : 1;
    if (s_page >= s_pages) {
        s_page = s_pages - 1;
    }
    if (!s_nt) {
        draw_empty(&g, now);
    }
    int on_page = s_nt - s_page * per < per ? s_nt - s_page * per : per;
    for (int i = 0; i < on_page; i++) {
        draw_tile(&s_tl[s_page * per + i], tile_rc(&g, i, on_page), now);
    }
    if (s_pages > 1) {  // the page dots
        int y = g.area.y + g.area.h + (BOARD_BAR_SCREEN ? 6 : 10);
        for (int p = 0; p < s_pages; p++) {
            ui_circle(gfx_w / 2 + (p * 2 - s_pages + 1) * 7, y, 3, p == s_page ? C_TXT : C_DIM, 255);
        }
    }
    if (s_added_at && now - s_added_at < 5000) {
        char b[48];
        snprintf(b, sizeof b, "%s IS A FRIEND NOW", s_added);
        const char *b2 = "ADD THIS BOARD ON IT TOO";
        int w = ui_text_w(&KF_LABEL, b), w2 = ui_text_w(&KF_LABEL, b2);
        w = (w > w2 ? w : w2) + 24;
        if (w > gfx_w - 8) {
            w = gfx_w - 8;
        }
        int y = g.area.y + g.area.h / 2 - 22;
        ui_rbox(gfx_w / 2 - w / 2, y, w, 44, 10, rgb(30, 60, 40), via_col(VIA_NEAR), 1);
        ui_text_c(&KF_LABEL, gfx_w / 2, y + 18, b, C_TXT);
        ui_text_c(&KF_LABEL, gfx_w / 2, y + 36, b2, C_DIM);
    }
    if (net2_failed() && gfx_round()) {
        mf_text_c(&MF_TINY, gfx_w / 2, WR(352), "THE RADIO DID NOT START", rgb(255, 140, 140), 255, 1);
    } else if (net2_failed()) {
        mf_text_c(&MF_TINY, gfx_w / 2, gfx_h - 6, "THE RADIO DID NOT START (LAN AND VPN STILL WORK)",
                  rgb(255, 140, 140), 255, 1);
    }
}

// ---- the talk screen
// END (round 3, the thumb audit A28): 100 x 48 px or more (the 3.49: the whole head strip of 44 px; the AMOLED native:
// 124 x 68 = 9.8 x 5.4 mm; the 320 x 240 frame: 104 x 48, in the circle of the Watcher), and a SECOND TAP in END_ARM_MS
// ends the talk: the first tap only asks "TAP AGAIN".
typedef struct {
    rc_t head, talk, mon[2], end;
    bool big;
} play_t;
static play_t play_lay(void)
{
#if BOARD_BAR_SCREEN
    return (play_t){
            {0, 0, 640, 44}, {14, 50, 360, 116}, {{388, 54, 226, 52}, {388, 114, 226, 52}}, {528, 1, 108, 42}, false};
#else
    if (gfx_round()) {
        return (play_t){{0, WR(40), gfx_w, WR(52)},
                        {WR(80), WR(98), WR(252), WR(116)},
                        {{WR(80), WR(220), WR(122), WR(72)}, {WR(210), WR(220), WR(122), WR(72)}},
                        {WR(146), WR(300), WR(120), WR(56)},
                        false};
    }
    if (gfx_native()) {
        return (play_t){{0, 0, 448, 72},
                        {16, 80, 416, 160},
                        {{16, 250, 200, 104}, {232, 250, 200, 104}},
                        {314, 2, 124, 68},
                        true};
    }
    // (the Tab5, 640 x 360: the head, a wide talk button, the two switches side by side under it)
    if (WK_TAB5) {
        return (play_t){{0, 0, 640, 68},
                        {16, 76, 608, 166},
                        {{16, 256, 296, 92}, {328, 256, 296, 92}},
                        {500, 4, 130, 60},
                        true};
    }
    return (play_t){
            {0, 0, 320, 52}, {10, 58, 300, 100}, {{10, 166, 145, 66}, {165, 166, 145, 66}}, {206, 2, 104, 48}, false};
#endif
}
#define END_ARM_MS     2500  // the second tap on END must come in this time
#define END_ARM_MIN_MS 200   // (a second tap sooner than this does nothing: no double tap by a bounce)
static int64_t s_end_arm;    // the time of the first tap on END (0 = END does not wait)
static void mic_icon(int cx, int cy, int s, uint16_t c)
{
    ui_rrect(cx - s / 3, cy - s, 2 * (s / 3) + 1, s + s / 3, s / 3, c);  // the head
    gfx_fill_rect(cx - s * 2 / 3, cy - s / 6, 2, s / 2, c);              // the cradle
    gfx_fill_rect(cx + s * 2 / 3 - 1, cy - s / 6, 2, s / 2, c);
    gfx_fill_rect(cx - s * 2 / 3, cy + s / 3, s * 4 / 3 + 1, 2, c);
    gfx_fill_rect(cx - 1, cy + s / 3, 2, s / 2, c);  // the stem and the foot
    gfx_fill_rect(cx - s / 3, cy + s * 5 / 6, 2 * (s / 3) + 1, 2, c);
}
static void draw_play(int64_t now)
{
    play_t l = play_lay();
    // the friend at the top, and END
    int f = friends_chosen(), via = f >= 0 ? via_of(f) : -1;
    int hy = l.head.y + l.head.h / 2, ar = l.head.h * 30 / 100;
    int hx = gfx_round() ? gfx_w / 2 - WR(90) : 14 + ar;
    avatar(hx, hy, ar, peer_name(), ui_name_colour(peer_name()), false);
    const kfont_t *nf = l.big ? &KF_BIG : &KF_TEXT;
    int room = (gfx_round() ? gfx_w : l.end.x) - (hx + ar + 10) - 8;  // (the name stays left of END)
    if (ui_text_w(nf, peer_name()) > room) {
        nf = &KF_LABEL;
    }
    ui_text(nf, hx + ar + 10, hy + nf->asc / 2 - 1, peer_name(), C_TXT);
    if (via >= 0 && !gfx_round()) {
        int x = hx + ar + 20 + ui_text_w(nf, peer_name()), lw = mf_width(&MF_TINY, via_txt(via), 1) + 12;
        if (x + lw < l.end.x - 6) {
            ui_rrect(x, hy - 8, lw, 16, 7, via_col(via));
            mf_text(&MF_TINY, x + 6, hy + 4, via_txt(via), rgb(10, 12, 22), 255, 1);
        }
    }
    // END: red; after the first tap brighter with "TAP AGAIN" and a bar that gets shorter (the time for the second tap)
    bool arm = s_end_arm && now - s_end_arm <= END_ARM_MS;
    ui_rbox(l.end.x, l.end.y, l.end.w, l.end.h, 10, arm ? rgb(170, 36, 48) : rgb(60, 24, 32), rgb(230, 90, 100),
            arm ? 2 : 1);
    if (arm) {
        const mfont_t *af = mf_width(&MF_N16, "TAP AGAIN", 1) <= l.end.w - 12 ? &MF_N16 : &MF_LABEL;
        mf_text_c(af, l.end.x + l.end.w / 2, l.end.y + l.end.h / 2 + af->asc / 2 - 2, "TAP AGAIN", rgb(255, 236, 160),
                  255, 1);
        int left = END_ARM_MS - (int)(now - s_end_arm), bw = (l.end.w - 16) * left / END_ARM_MS;
        ui_rrect(l.end.x + 8, l.end.y + l.end.h - 8, bw > 2 ? bw : 2, 4, 2, rgb(255, 200, 120));
    } else {
        const mfont_t *ef = l.big ? &MF_N24 : &MF_N16;
        mf_text_c(ef, l.end.x + l.end.w / 2, l.end.y + l.end.h / 2 + ef->asc / 2, "END", rgb(255, 200, 205), 255, 1);
    }
    // the talk button
    bool mine = s_ws.talk == me(), theirs = s_ws.talk != TK_NONE && !mine, wait = s_want && !mine;
    uint16_t bg = mine ? rgb(190, 50, 60) : theirs ? rgb(30, 130, 80) : wait ? rgb(120, 100, 40) : rgb(44, 60, 120);
    uint16_t edge = mine     ? rgb(255, 170, 170)
                    : theirs ? rgb(150, 255, 190)
                    : wait   ? rgb(255, 220, 120)
                             : rgb(120, 150, 255);
    rc_t b = l.talk;
    ui_rbox(b.x, b.y, b.w, b.h, l.big ? 26 : 20, bg, edge, 2);
    const char *t = mine ? "TALKING" : theirs ? "THEY TALK" : wait ? "WAIT..." : "HOLD TO TALK";
    const mfont_t *tf = l.big ? &MF_N34 : (b.w >= 300 ? &MF_N28 : &MF_N24);
    int tw = mf_width(tf, t, 1), ms = l.big ? 16 : 11, gx = (b.w - tw - ms * 3) / 2;
    int ty = b.y + b.h / 2 + tf->asc / 2 - (l.big ? 6 : 4);
    mic_icon(b.x + gx + ms, ty - tf->asc / 2, ms, rgb(255, 255, 255));
    mf_text(tf, b.x + gx + ms * 3, ty, t, rgb(255, 255, 255), 255, 1);
    // the level: mine when I send, theirs when I play (12 small bars)
    float lv = i_send() ? s_tx_level : (now - s_rx_at < 300 ? s_rx_level : 0);
    int nseg = 12, sw = (b.w - 60) / nseg, lit = (int)(fminf(1, lv * 6) * nseg + 0.5f),
        sy = b.y + b.h - (l.big ? 30 : 20);
    for (int i = 0; i < nseg; i++) {
        ui_rrect(b.x + 30 + i * sw, sy, sw - 4, l.big ? 10 : 7, 3,
                 i < lit ? rgb(150, 255, 180) : ui_mix(bg, rgb(0, 0, 0), 110));
    }
    // one line of state at the top of the button
    const char *st = NULL;
    uint16_t sc = rgb(255, 236, 160);
    if (now - s_busy_at < 1200) {
        st = "BUSY: THE OTHER BOARD TALKS";
    } else if (!mine && i_send()) {
        st = "MONITOR: YOUR MIC GOES TO THEM";
        sc = rgb(255, 210, 210);
    } else if (!theirs && i_play()) {
        st = "MONITOR: YOU HEAR THEM";
        sc = rgb(210, 255, 225);
    } else if (g_settings.mute) {
        st = "SOUND IS OFF!";
        sc = rgb(255, 140, 140);
    }
    if (st) {
        mf_text_c(&MF_TINY, b.x + b.w / 2, b.y + (l.big ? 26 : 18), st, sc, 255, 1);
    }
    // the monitor switches: a label and a switch
    static const char *const MN[2] = {"I HEAR THEM", "THEY HEAR ME"};
    bool on[2] = {mon_from_them(), mon_from_me()};
    for (int i = 0; i < 2; i++) {
        rc_t m = l.mon[i];
        uint16_t c = on[i] ? rgb(110, 220, 255) : rgb(90, 96, 130);
        ui_rbox(m.x, m.y, m.w, m.h, 14, on[i] ? rgb(24, 64, 90) : C_CARD, c, on[i] ? 2 : 1);
        const mfont_t *lf = l.big ? &MF_LABEL : &MF_TINY;
        int pw = l.big ? 60 : 40, ph = l.big ? 30 : 20;
        bool side = m.w >= 230;
        int px = side ? m.x + m.w - pw - 12 : m.x + m.w / 2 - pw / 2,
            py = side ? m.y + m.h / 2 - ph / 2 : m.y + m.h - ph - 10;
        if (side) {
            mf_text(lf, m.x + 14, m.y + m.h / 2 + lf->asc / 2, MN[i], C_TXT, 255, 1);
        } else {
            mf_text_c(lf, m.x + m.w / 2, m.y + 8 + lf->asc, MN[i], C_TXT, 255, 1);
        }
        ui_rrect(px, py, pw, ph, ph / 2, on[i] ? rgb(60, 200, 120) : rgb(60, 64, 90));
        ui_circle(on[i] ? px + pw - ph / 2 : px + ph / 2, py + ph / 2, ph / 2 - 3, rgb(250, 250, 255), 255);
    }
}

static void walkie_render(int64_t now)
{
    gfx_fill(C_BG);
    if (s_role == N2_LOBBY || s_role == N2_OFF) {
        draw_lobby(now);
        net2_draw_lost(now);
        draw_panel();
        return;
    }
    draw_play(now);
    net2_draw_lost(now);
}

// ---------------------------------------------------------------- the app
static void web_requests(void);  // (the requests of the web page, below)
bool walkie_paired(void)
{
    return (app_current() == &app_walkie || app_current() == &app_talkie) && (s_role == N2_HOST || s_role == N2_CLIENT);
}
static void back_to_lobby(void)
{
    memset(&s_ws, 0, sizeof s_ws);
    s_want = s_down = false;
    s_end_arm = 0;
    s_peer_name[0] = 0;
    audio_stream_clear();
    int c = friends_chosen();  // (net2_open sets "any friend": WALKIE keeps its own choice)
    net2_close();
    net2_open(NET_ID, s_simple ? 0x8000 : 0);
    friends_choose(c);
    s_role = net2_role();
    gather();
}
static void walkie_enter(int64_t now)
{
    s_now = now;
    memset(&s_ws, 0, sizeof s_ws);
    s_down = s_want = false;
    s_rx_on = false;
    s_end_arm = 0;
    s_mw = s_mr = 0;
    s_macc = s_mcnt = 0;
    s_enc = (adpcm_t){0, 0};
    s_peer_name[0] = 0;
    s_page = 0;
    s_panel = s_fdown = false;
    s_pan = 0;
    s_added_at = 0;
    s_cs_last = WKC_IDLE;
    if (!web_audio_active()) {
        audio_stream_clear();  // (a browser that talks opened WALKIE: keep its voice)
    }
    audio_stream_gain(512);  // (the voice: a little louder)
    audio_mic_start();
    audio_mic_tap(mic_tap);
    net2_open(NET_ID, s_simple ? 0x8000 : 0);  // (TALKIE: the "auto" flag in its HELLO)
    // nobody until a tap (or the friend that this board answered: it pairs at once); after net2_open (it sets "any").
    // TALKIE: any friend, at once.
    int a = walkie_call_take_answer();
    friends_choose(a >= 0 ? a : s_simple ? -1 : FRIENDS_NOBODY);
    s_role = net2_role();
    gather();
}
static void walkie_leave(void)
{
    if (s_ws.talk == me() || s_want) {
        request(CMD_TALK, 0);
    }
    walkie_call_stop();
    friends_choose(-1);  // (the games: any friend that is near)
    audio_mic_tap(NULL);
    audio_mic_stop();
    audio_stream_clear();
    audio_stream_gain(256);
    net2_close();
}
static void walkie_update(int64_t now)
{
    float dt = s_now ? (now - s_now) / 1000.0f : 0;
    s_now = now;
    s_pan += (s_panel ? 1 : -1) * dt * 6;
    if (s_pan < 0) {
        s_pan = 0;
    }
    if (s_pan > 1) {
        s_pan = 1;
    }
    n2_event_t e = net2_poll(now, on_rx, NULL);
    s_role = net2_role();
    if (e == N2_EV_PAIRED) {
        memset(&s_ws, 0, sizeof s_ws);
        s_end_arm = 0;
        walkie_call_done();
        s_panel = false;
        s_pan = 0;
        snd_chirp();
        s_state_at = 0;
    }
    if (e == N2_EV_LOST || e == N2_EV_PEER_LOBBY) {
        // the other board ended the talk (END, or WALKIE closed): back to the tiles; it went away (the radio, the
        // network): the choice stays, so the two boards pair again when it comes back
        if (e == N2_EV_PEER_LOBBY || net2_last_bye()) {
            friends_choose(s_simple ? -1 : FRIENDS_NOBODY);
        }
        back_to_lobby();
        return;
    }
    if (s_role == N2_LOBBY || s_role == N2_OFF) {
        int a = walkie_call_take_answer();
        if (a >= 0) {
            friends_choose(a);  // a call came: pair with the caller
        }
        if (now - s_gather_at > 300) {
            s_gather_at = now;
            gather();
        }
        // the sounds of a call of this board: a soft tone while it rings there, a low tone when it ends
        int cf;
        int64_t since;
        wkc_state_t cs = walkie_call_state(&cf, &since);
        if (cs == WKC_CALLING && now - s_tick_at > 1500) {
            s_tick_at = now;
            sfx_tone(880, 880, 120, W_SINE, 16);
        }
        if (cs != s_cs_last && (cs == WKC_DECLINED || cs == WKC_BUSY || cs == WKC_NO_ANSWER)) {
            snd_busy();
        }
        s_cs_last = cs;
    }
    bool quiet = (s_role == N2_HOST || s_role == N2_CLIENT) && !strcmp(app_screen_state(), "off") &&
                 s_ws.talk == TK_NONE && !s_ws.mon_h2c && !s_ws.mon_c2h && friend_on_lan();
    link_quiet(quiet);  // (no change: nothing happens; a new link starts with the radio on)
    if (s_role == N2_HOST && now - s_state_at > (quiet ? 500 : 150)) {
        host_send();
    }
    if (s_role == N2_HOST || s_role == N2_CLIENT) {
        if (!s_peer_name[0]) {
            int c = friends_chosen();
            snprintf(s_peer_name, FRIEND_NAME, "%.15s", c >= 0 ? friends_get(c)->name : friends_name_of(net2_peer()));
        }
        if (s_role == N2_CLIENT && now - s_alive_at > (quiet ? 500 : 250)) {
            uint8_t z = 0;
            net2_send(K_ALIVE, &z, 1);
            s_alive_at = now;
        }
        web_requests();
        // the CLIENT: a lost request is sent again (the HOST state shows what it got)
        if (s_role == N2_CLIENT && now - s_ask_at > 300) {
            if (s_want && s_down && s_ws.talk == TK_NONE) {
                request(CMD_TALK, 1);
            } else if (!s_down && s_ws.talk == TK_CLIENT) {
                request(CMD_TALK, 0);
            }
        }
        send_voice();
        if (now - s_gather_at > 1000) {
            s_gather_at = now;
            gather();
        }  // (the way to the friend, at the top)
    } else {
        s_mr = s_mw;
    }
}
static bool in_rc(rc_t r, int x, int y, int m)
{
    return x >= r.x - m && x < r.x + r.w + m && y >= r.y - m && y < r.y + r.h + m;
}
static void lobby_input(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
    if (ev->kind == IN_DOWN) {
        s_fdown = true;
        s_dx0 = x;
        s_dy0 = y;
        return;
    }
    if (ev->kind == IN_UP && s_fdown) {
        s_fdown = false;
        int dx = x - s_dx0, dy = y - s_dy0;
        if (!s_panel && s_dy0 < top_edge() && dy > 36 && dy > abs(dx)) {
            s_panel = true;
            return;
        }
        if (s_panel && dy < -36 && -dy > abs(dx)) {
            s_panel = false;
            return;
        }
        if (!s_panel && abs(dx) > 50 && abs(dx) > abs(dy) && s_pages > 1) {  // the pages
            s_page = (s_page + (dx < 0 ? 1 : s_pages - 1)) % s_pages;
            sfx_ui(SFX_TICK);
        }
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    if (s_panel) {
        if (!panel_tap(x, y)) {
            s_panel = false;
        }
        return;
    }
    grid_t g = grid();
    int per = g.cols * g.rows, on_page = s_nt - s_page * per < per ? s_nt - s_page * per : per;
    for (int i = 0; i < on_page; i++) {
        int k = s_page * per + i;
        if (!in_rc(tile_rc(&g, i, on_page), x, y, g.gap / 2)) {
            continue;
        }
        const tile_t *t = &s_tl[k];
        if (t->f < 0) {
            if (friends_add(t->mac, t->name)) {
                snprintf(s_added, sizeof s_added, "%s", t->name);
                s_added_at = s_now;
                sfx_ui(SFX_COIN);
                gather();
            } else {
                sfx_play(SFX_BUMP);  // the list is full (SETTINGS > FRIENDS)
            }
            return;
        }
        int cf;
        if (walkie_call_state(&cf, NULL) == WKC_CALLING && cf == t->f) {
            walkie_call_stop();
        } else {
            walkie_call_start(t->f);
            s_tick_at = s_now;
        }
        sfx_ui(SFX_TICK);
        return;
    }
}
static void walkie_input(const input_event_t *ev)
{
    int x = ev->x, y = ev->y;
    if (s_role == N2_LOBBY || s_role == N2_OFF) {
        lobby_input(ev);
        return;
    }
    play_t l = play_lay();
    bool on_end = in_rc(l.end, x, y, 2);
    if (s_end_arm && (ev->kind == IN_DOWN || ev->kind == IN_TAP) && !on_end) {
        s_end_arm = 0;  // another place: END waits no more
    }
    if (ev->kind == IN_TAP && on_end) {
        // END: the first tap asks TAP AGAIN; the second tap (in END_ARM_MS) ends the talk on both boards (BYE)
        int64_t dt = s_end_arm ? ev->t_ms - s_end_arm : -1;
        if (dt >= 0 && dt < END_ARM_MIN_MS) {
            return;
        }
        if (dt < 0 || dt > END_ARM_MS) {
            s_end_arm = ev->t_ms ? ev->t_ms : 1;
            sfx_ui(SFX_TICK);
            return;
        }
        s_end_arm = 0;
        if (s_ws.talk == me() || s_want) {
            request(CMD_TALK, 0);
        }
        sfx_ui(SFX_CLICK);
        friends_choose(FRIENDS_NOBODY);
        back_to_lobby();
        return;
    }
    if (ev->kind == IN_DOWN && in_rc(l.talk, x, y, 4)) {
        s_down = true;
        if (s_ws.talk != TK_NONE && s_ws.talk != me()) {
            snd_busy();
            s_busy_at = s_now;
            return;
        }
        s_want = true;
        sfx_ui(SFX_CLICK);
        request(CMD_TALK, 1);
        return;
    }
    if (ev->kind == IN_UP && s_down) {
        s_down = false;
        if (s_want || s_ws.talk == me()) {
            s_want = false;
            request(CMD_TALK, 0);
            snd_over();
        }
        return;
    }
    if (ev->kind == IN_TAP) {
        for (int i = 0; i < 2; i++) {
            if (!in_rc(l.mon[i], x, y, 4)) {
                continue;
            }
            // 0: I listen to them = the other board sends; 1: they listen to me = this board sends
            bool host = s_role == N2_HOST;
            int cmd = i == 0 ? (host ? CMD_MON_C2H : CMD_MON_H2C) : (host ? CMD_MON_H2C : CMD_MON_C2H);
            request(cmd, !(i == 0 ? mon_from_them() : mon_from_me()));
            sfx_play(SFX_SWITCH);
        }
    }
}
// ---------------------------------------------------------------- the web page (web_walkie.c)
static volatile int8_t s_web_req[2] = {-1, -1};  // a switch request of the web page: -1 = none, 0, 1

int walkie_web_state(char *out, int max)
{
    bool open = app_current() == &app_walkie || app_current() == &app_talkie,
         paired = open && (s_role == N2_HOST || s_role == N2_CLIENT);
    char peer[FRIEND_NAME];
    int k = 0;
    for (const char *p = s_peer_name; *p && k < FRIEND_NAME - 1; p++) {
        peer[k++] = (*p == '"' || *p == '\\') ? ' ' : *p;
    }
    peer[k] = 0;
    const char *talk = !paired || s_ws.talk == TK_NONE ? "none" : s_ws.talk == me() ? "this board" : "the other board";
    return snprintf(out, (size_t)max,
                    "{\"open\":%s,\"paired\":%s,\"peer\":\"%s\",\"talk\":\"%s\",\"hear_them\":%d,\"they_hear_me\":%d}",
                    open ? "true" : "false", paired ? "true" : "false", peer, talk, paired && mon_from_them(),
                    paired && mon_from_me());
}

void walkie_web_set(int which, bool on)
{
    if (which == 0 || which == 1) {
        s_web_req[which] = on ? 1 : 0;
    }
}

// a request of the web page, as a tap on the switch
static void web_requests(void)
{
    for (int i = 0; i < 2; i++) {
        int v = s_web_req[i];
        if (v < 0) {
            continue;
        }
        s_web_req[i] = -1;
        bool host = s_role == N2_HOST;
        int cmd = i == 0 ? (host ? CMD_MON_C2H : CMD_MON_H2C) : (host ? CMD_MON_H2C : CMD_MON_C2H);
        request(cmd, v);
    }
}

// QUIET (the idle power mode): paired, the screen off, nobody talks, no monitor and the friend is on the
// LAN / VPN list: the radio saves power (link_quiet) and the state / "alive" packets go each 500 ms, not 150 / 250 ms.
// A board NEARBY with no WiFi network (ESP-NOW only) and the MONITOR keep the radio on all the time.
static bool friend_on_lan(void)
{
    const uint8_t *m = net2_peer();
    if (!m) {
        return false;
    }
    lan_peer_t p[LAN_PEERS];
    int n = lan_peers(p, LAN_PEERS);
    for (int i = 0; i < n; i++) {
        if (!memcmp(p[i].mac, m, 6)) {
            return true;
        }
    }
    return false;
}

// The screen stays on while somebody talks (a talk of the other board also turns it on). While
// the boards are paired, the app runs with the screen off: the monitor and the talks go on.
static bool walkie_awake(void)
{
    return (s_role == N2_HOST || s_role == N2_CLIENT) && s_ws.talk != TK_NONE;
}
static bool walkie_background(void)
{
    return s_role == N2_HOST || s_role == N2_CLIENT;
}
#include "../libs/tficon/tficon.h"
// the menu icon: a walkie-talkie; its light turns on and radio waves go out of its antenna
static void walkie_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_box_t b = tfi_tile(x, y, s, tfi_hex(0xffc93c), tfi_hex(0xf06a00));
    float u = s / 100.0f, ph = tfi_ph(2);
    int bx = x + (int)(34 * u), by = y + (int)(34 * u), bw = (int)(36 * u), bh = (int)(52 * u);
    // the antenna and the waves
    tfi_rbox(bx + (int)(5 * u), y + (int)(16 * u), (int)(6 * u), (int)(22 * u), (int)(3 * u), tfi_hex(0x4a4f5c),
             tfi_hex(0x2a2e38), 256);
    float ax = bx + 8 * u, ay = y + 18 * u;
    tfd_clip(b.x, b.y, b.x + b.w, b.y + b.h);
    for (int k = 0; k < 2; k++) {
        float q = ph * 2 + k * 0.5f;
        q -= (float)(int)q;
        int a = (int)(240 * (1 - q));
        tfi_arc(ax, ay, (8 + q * 22) * u, 3 * u, -1.2f, -0.1f, rgb(255, 255, 255), a);
        tfi_arc(ax, ay, (8 + q * 22) * u, 3 * u, 0.6f - 3.14159f, -1.95f, rgb(255, 255, 255), a * 2 / 3);
    }
    tfd_clip(x, y, x + s, y + s);
    // the body, the screen, the speaker, the light
    tfi_drop(bx, by, bw, bh, (int)(8 * u), (int)(3 * u), 70);
    tfi_gloss_box(bx, by, bw, bh, (int)(8 * u), tfi_hex(0x535a6b), tfi_hex(0x1d2029));
    tfi_rbox(bx + (int)(6 * u), by + (int)(8 * u), bw - (int)(12 * u), (int)(13 * u), (int)(3 * u), tfi_hex(0xa8ffb0),
             tfi_hex(0x3fcf6a), 256);
    int lh = (int)(2.6f * u) > 1 ? (int)(2.6f * u) : 1;
    for (int k = 0; k < 3; k++) {
        tfd_rect(bx + (int)(8 * u), by + (int)((29 + k * 6) * u), bw - (int)(16 * u), lh, 0, 120);
    }
    float on = ph < 0.5f ? 1.0f : 0.25f;
    tfd_glow(bx + bw - 7 * u, by - 1 * u, 8 * u, tfi_hex(0xff4040), (int)(220 * on));
    tfd_disc(bx + bw - 7 * u, by + 0 * u, 3.2f * u, tfd_mix(tfi_hex(0x7a1010), tfi_hex(0xff5050), (int)(256 * on)),
             256);
    tfi_end();
}

const app_t app_walkie = {
        .name = "WALKIE",
        .landscape = true,
        .needs = 0,
        .enter = walkie_enter,
        .leave = walkie_leave,
        .input = walkie_input,
        .update = walkie_update,
        .render = walkie_render,
        .draw_icon = walkie_icon,
        .awake = walkie_awake,
        .background = walkie_background,
        .native = true,
        .round = APP_ROUND_HI,  // the round frame of the panel (466 on the AMOLED-1.75C, 412 on the Watcher)
        .run = true,            // the settings panel: a call goes on under it
};

// ---------------------------------------------------------------- TALKIE: the simple walkie
// A simpler walkie: almost the entire screen is one push to talk button
// (in Hebrew). It is for a child: give it one friend in the manager, and the profile. The same engine and
// protocol as WALKIE (it pairs with WALKIE, with another TALKIE and with the manager): it pairs at once with any
// friend (its HELLO has the "auto" flag: a WALKIE that it names takes it as a call). No tiles, no panel, no switches.
#define TALKIE_INFO 0x8000  // net2 info: "auto" (net2.c: a WALKIE with nobody chosen takes it)
static const char *const T_TALK = "\xd7\x9c\xd7\x97\xd7\xa5 \xd7\x95\xd7\x93\xd7\x91\xd7\xa8";          // לחץ ודבר
static const char *const T_THEY = "\xd7\x9e\xd7\x93\xd7\x91\xd7\xa8 \xd7\x90\xd7\x9c\xd7\x99\xd7\x9a";  // מדבר אליך
static const char *const T_ME = "\xd7\x9e\xd7\x93\xd7\x91\xd7\xa8...";                                  // מדבר...
static const char *const T_WAIT = "\xd7\x9e\xd7\x97\xd7\xa4\xd7\xa9 \xd7\x97\xd7\x91\xd7\xa8...";       // מחפש חבר...
static const char *const T_NONE = "\xd7\x90\xd7\x99\xd7\x9f \xd7\x97\xd7\x91\xd7\xa8\xd7\x99\xd7\x9d";  // אין חברים
static const char *const T_BUSY = "\xd7\x94\xd7\x97\xd7\x91\xd7\xa8 \xd7\x9e\xd7\x93\xd7\x91\xd7\xa8";  // החבר מדבר
static rc_t t_button(void)
{
#if BOARD_BAR_SCREEN
    return (rc_t){8, 26, 624, 140};
#else
    if (gfx_round()) {
        return (rc_t){WR(64), WR(84), WR(284), WR(244)};
    }
    if (gfx_native()) {
        return (rc_t){14, 44, 420, 310};
    }
    if (WK_TAB5) {
        return (rc_t){16, 40, gfx_w - 32, gfx_h - 56};  // (the Tab5)
    }
    return (rc_t){8, 30, 304, 202};
#endif
}
static void talkie_render(int64_t now)
{
    gfx_fill(C_BG);
    rc_t b = t_button();
    bool paired = s_role == N2_HOST || s_role == N2_CLIENT;
    bool mine = paired && s_ws.talk == me(), theirs = paired && s_ws.talk != TK_NONE && !mine,
         wait = paired && s_want && !mine;
    // the friend at the top
    char top[48];
    snprintf(top, sizeof top, "%s", paired ? peer_name() : friends_count() ? "" : "");
    int ty = gfx_round() ? WR(70) : 20;
    if (paired) {
        ui_text_c(&KF_NAME, gfx_w / 2, ty, top, C_DIM);
    } else if (net2_failed()) {
        mf_text_c(&MF_TINY, gfx_w / 2, ty, "THE RADIO DID NOT START", rgb(255, 140, 140), 255, 1);
    }
    uint16_t bg = !paired  ? rgb(30, 34, 56)
                  : mine   ? rgb(200, 52, 62)
                  : theirs ? rgb(34, 150, 90)
                  : wait   ? rgb(130, 108, 40)
                           : rgb(48, 84, 200);
    uint16_t edge = !paired  ? rgb(70, 76, 110)
                    : mine   ? rgb(255, 180, 180)
                    : theirs ? rgb(160, 255, 200)
                             : rgb(150, 180, 255);
    int r = b.h / 5;
    ui_rbox(b.x, b.y, b.w, b.h, r, bg, edge, 3);
    // a soft pulse while it waits for the friend
    if (!paired) {
        float ph = fmodf(now / 1400.0f, 1.0f);
        ui_ring(gfx_w / 2, b.y + b.h / 2, (int)(b.h * (0.18f + 0.30f * ph)), 3, rgb(120, 140, 220),
                (int)(140 * (1 - ph)));
    }
    const char *t = !paired ? (friends_count() ? T_WAIT : T_NONE) : mine ? T_ME : theirs ? T_THEY : T_TALK;
    if (now - s_busy_at < 1200) {
        t = T_BUSY;
    }
    static const kfont_t *const TF[] = {&KF_TITLE, &KF_BIG, &KF_TEXT};
    const kfont_t *f = ui_text_fit(TF, 3, t, b.w - 24);
    int cy = b.y + b.h / 2 + f->asc / 2;
    // (the big words: twice the title face on the big panels is too wide for a child's eye? no: the title face is 34 px)
    ui_text_c(f, gfx_w / 2, cy, t, rgb(255, 255, 255));
    // the level: a row of dots under the words
    float lv = i_send() ? s_tx_level : (now - s_rx_at < 300 ? s_rx_level : 0);
    if (paired) {
        int n = 9, gap = b.w / 16, lit = (int)(fminf(1, lv * 6) * n + 0.5f), y = b.y + b.h - b.h / 7, dr = gap / 4 + 1;
        for (int i = 0; i < n; i++) {
            ui_circle(gfx_w / 2 + (i - n / 2) * gap, y, dr, i < lit ? rgb(255, 255, 255) : ui_mix(bg, rgb(0, 0, 0), 90),
                      255);
        }
    }
    if (g_settings.mute) {
        mf_text_c(&MF_TINY, gfx_w / 2, gfx_round() ? WR(360) : gfx_h - 4, "THE SOUND IS OFF", rgb(255, 130, 130), 255,
                  1);
    }
    net2_draw_lost(now);
}
static void talkie_input(const input_event_t *ev)
{
    rc_t b = t_button();
    if (s_role != N2_HOST && s_role != N2_CLIENT) {
        return;
    }
    if (ev->kind == IN_DOWN && in_rc(b, ev->x, ev->y, 4)) {
        s_down = true;
        if (s_ws.talk != TK_NONE && s_ws.talk != me()) {
            snd_busy();
            s_busy_at = s_now;
            return;
        }
        s_want = true;
        sfx_ui(SFX_CLICK);
        request(CMD_TALK, 1);
        return;
    }
    if (ev->kind == IN_UP && s_down) {
        s_down = false;
        if (s_want || s_ws.talk == me()) {
            s_want = false;
            request(CMD_TALK, 0);
            snd_over();
        }
    }
}
static void talkie_enter(int64_t now)
{
    s_simple = true;
    walkie_enter(now);
}
static void talkie_leave(void)
{
    walkie_leave();
    s_simple = false;
}
// the menu icon: a round red talk button with a microphone; sound rings go out of it
static void talkie_icon(int x, int y, int s)
{
    tfi_begin(x, y, s);
    tfi_box_t b = tfi_tile(x, y, s, tfi_hex(0xff6b6b), tfi_hex(0xb3001b));
    float u = s / 100.0f, ph = tfi_ph(2), cx = x + 50 * u, cy = y + 50 * u;
    tfd_clip(b.x, b.y, b.x + b.w, b.y + b.h);
    float q = ph * 2;
    q -= (float)(int)q;
    if (q < 0.92f) {
        tfi_ring(cx, cy, (27 + q * 15) * u, 2.8f * u, rgb(255, 255, 255), (int)(230 * (1 - q)));
    }
    tfd_clip(x, y, x + s, y + s);
    float pr = 1 - 0.04f * (0.5f + 0.5f * fsin(ph * 12.566f));
    tfi_disc(cx, cy, 25 * u, rgb(255, 255, 255), tfi_hex(0xc8c8d8), 256);
    tfi_disc(cx, cy, 21 * u * pr, tfi_hex(0xff5a5a), tfi_hex(0xb5001a), 256);
    // the microphone
    tfi_rbox((int)(cx - 5 * u), (int)(cy - 13 * u), (int)(10 * u), (int)(18 * u), (int)(5 * u), rgb(255, 255, 255),
             tfi_hex(0xffd6d6), 256);
    tfi_arc(cx, cy - 1 * u, 8.5f * u, 2.6f * u, 0.15f, 2.99f, rgb(255, 255, 255), 256);
    tfd_rect((int)(cx - 1.3f * u), (int)(cy + 8 * u), (int)(2.6f * u), (int)(4.5f * u), rgb(255, 255, 255), 256);
    tfi_spark(cx - 9 * u, cy - 11 * u, 3.5f * u, 110);
    tfi_end();
}
const app_t app_talkie = {
        .name = "TALKIE",
        .landscape = true,
        .needs = 0,
        .enter = talkie_enter,
        .leave = talkie_leave,
        .input = talkie_input,
        .update = walkie_update,
        .render = talkie_render,
        .draw_icon = talkie_icon,
        .awake = walkie_awake,
        .background = walkie_background,
        .native = true,
        .round = APP_ROUND_HI,  // the round frame of the panel (466 on the AMOLED-1.75C, 412 on the Watcher)
        .run = true,            // the settings panel: a call goes on under it
};
