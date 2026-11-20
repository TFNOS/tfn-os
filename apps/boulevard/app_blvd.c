// BOULEVARD (שדרות): a property game on the streets of Tel Aviv, for 1 to 6 players. Hebrew is the default.
//   PLAY: you against 1 to 5 bots on this board.  FRIENDS: a table of 2 to 6 boards by radio (ESP-NOW; each friend
//   plays on his own board; empty seats can be bots).
//   A turn: ROLL (the button, the BOOT key, or a shake of the board); the token hops; on a free street the card comes
//   up: BUY or PASS. MANAGE: floors, towers, mortgages. TRADE: an offer to another player. MAP: all the board.
// The files: bv_data.inc (the board, the cards), bv_rules.inc (the rules, tested on the PC),
// bv_bot.inc (the bots), bv_view.inc (the drawing), bv_net.inc (the radio), bv_music.c (the music).
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/board.h"
#include "tfn/gfx.h"
#include "tfn/imu.h"
#include "tfn/input.h"
#include "tfn/kfont.h"
#include "tfn/friends.h"
#include "tfn/link.h"
#include "tfn/mfont.h"
#include "tfn/settings.h"
#include "esp_heap_caps.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#include "slowtap/slowtap.h"
#include "midi_bg/midi_bg.h"
#include "bv_music.h"
#include "bv_art.h"
#include "bv_sfx.h"
#ifdef ESP_PLATFORM
#include "tfn/tjpgd_rom.h"
#endif
#include "tfdraw/tfdraw.h"
#include "tfsfx/tfsfx.h"
#ifdef ESP_PLATFORM
#include <sys/stat.h>
#include "esp_random.h"
#define BV_DIR BOARD_SD_ROOT "/blvd"
#else
#define BV_DIR "blvd_card"
uint32_t esp_random(void);
#endif

#include "bv_data.inc"
#include "bv_rules.inc"
#include "bv_bot.inc"

// ---------------------------------------------------------------- the state of the app
static int s_lang = 1;  // 0 English, 1 Hebrew
static int64_t s_now;   // ms
static bgame_t g_v;     // the game: the host runs the rules on it; a client fills it
enum {
    SCR_TITLE,
    SCR_SETUP,
    SCR_LOBBY,
    SCR_ROOM,
    SCR_PLAY,
    SCR_MANAGE,
    SCR_TRADE,
    SCR_MAP,
    SCR_END
};
static int s_scr = SCR_TITLE;
static int s_setup_n = 4, s_setup_lvl = 1, s_setup_mode = MODE_FULL, s_my_look;
static uint32_t s_ev_have;  // the newest event that this board has (the host: g_v.evn)
static uint32_t s_vev;      // the newest event that the view played
static int s_music = 1, s_speed;
enum {
    FX_DICE,
    FX_HOP,
    FX_COIN,
    FX_BUY,
    FX_CARD,
    FX_JAM,
    FX_CLICK,
    FX_JOIN,
    FX_BUILD,
    FX_BAD,
    FX_N
};
static const char *const FXN[FX_N] = {"dice", "hop", "coin", "buy", "card", "jam", "click", "join", "build", "bad"};
static bool s_snd = true;
static void fx(int id)
{
    if (s_snd) {
        tfsfx_play(id, id == FX_HOP ? 45 : 80, 1.0f);
    }
}
// the dice: a clack at a bounce (k 0..1: the speed of the hit), a soft tick while a die rolls (synthesized)
static void die_sound(float k, bool roll)
{
    if (!s_snd) {
        return;
    }
    audio_voice_t v = {0};
    v.shape = AUDIO_PERC;
    if (roll) {
        v.dur = AUDIO_SAMPLE_RATE * 6 / 1000;
        v.f0 = v.f1 = 3200;
        v.wave = W_NOISE;
        v.vol = 7;
        audio_play(&v);
        return;
    }
    v.dur = AUDIO_SAMPLE_RATE * 14 / 1000;
    v.f0 = v.f1 = 4000;
    v.wave = W_NOISE;
    v.vol = (uint8_t)(10 + 30 * k);
    audio_play(&v);
    v.dur = AUDIO_SAMPLE_RATE * 30 / 1000;
    v.f0 = 2300 + 500 * k;
    v.f1 = 1700;
    v.wave = W_TRI;
    v.vol = (uint8_t)(8 + 22 * k);
    audio_play(&v);
}
static void host_apply(int who, const bin_t *in);
static void client_started(void);
static void manage_open(void);
static void trade_open(void);
static int s_map_sel = -1;
static int s_map_board = 1;  // the MAP: 1 = the board from the top (as a real board), 0 = the list of the tiles
static int64_t s_jingle_until;
static const char *T(const char *en, const char *he)
{
    return s_lang ? he : en;
}

#include "bv_view.inc"
#include "bv_net.inc"

static const char *pname(int p)
{
    return BOT_NAME[g_v.P[p].look % NLOOK][s_lang];
}

// a text with shekel signs: "₪" becomes NIS (English) or ש"ח (Hebrew) (the faces have no shekel sign)
static void sh_text(char *out, int n, const char *in)
{
    int k = 0;
    for (const char *s = in; *s && k < n - 8; s++) {
        if ((uint8_t)s[0] == 0xE2 && (uint8_t)s[1] == 0x82 && (uint8_t)s[2] == 0xAA) {
            const char *r = s_lang ? "ש\"ח" : "NIS ";
            while (*r && k < n - 1) {
                out[k++] = *r++;
            }
            s += 2;
        } else {
            out[k++] = *s;
        }
    }
    out[k] = 0;
}
// a text of several lines in a box (the words: a cut at a space)
static void text_box(const kfont_t *f, int cx, int y0, int w, int lh, const char *s, uint16_t col, int maxl)
{
    char line[128];
    const char *p = s;
    for (int l = 0; l < maxl && *p; l++) {
        int best = 0, i = 0;
        while (p[i]) {
            while (p[i] && p[i] != ' ') {
                i++;
            }
            char t[128];
            int m = i < 127 ? i : 127;
            memcpy(t, p, (size_t)m);
            t[m] = 0;
            if (ui_text_w(f, t) > w && best) {
                break;
            }
            best = i;
            if (!p[i]) {
                break;
            }
            i++;
        }
        if (!best) {
            best = (int)strlen(p);
        }
        int m = best < 127 ? best : 127;
        memcpy(line, p, (size_t)m);
        line[m] = 0;
        ui_text_c(f, cx, y0 + l * lh, line, col);
        p += best;
        while (*p == ' ') {
            p++;
        }
    }
}

// ---------------------------------------------------------------- buttons
typedef struct {
    int x, y, w, h;
} rc_t;
static bool in_rc(rc_t r, int x, int y)
{
    return x >= r.x - 4 && x < r.x + r.w + 4 && y >= r.y - 4 && y < r.y + r.h + 4;
}
// the round frame (466 x 466 on the 1.75C, 412 on the Watcher): the sizes are parts of the frame width
#define RND   gfx_round()
#define PC(n) (gfx_w * (n) / 100)
// the half width of the circle at the row y (0 out of the circle)
static int r_half(int y)
{
    int r = gfx_w / 2, dy = y - gfx_h / 2, q = r * r - dy * dy;
    return q > 0 ? (int)sqrtf((float)q) : 0;
}
// a box of h rows at the row y, in the middle: the width of the circle at its narrower edge less 2 * pad, at most maxw
static rc_t r_mid(int y, int h, int pad, int maxw)
{
    int a = r_half(y), b = r_half(y + h), w = 2 * (a < b ? a : b) - 2 * pad;
    if (w > maxw) {
        w = maxw;
    }
    return (rc_t){gfx_w / 2 - w / 2, y, w, h};
}
static int s_press_x = -1, s_press_y = -1;
static bool ascii_only(const char *s);
static void button(rc_t r, const char *txt, uint16_t bg, bool on)
{
    bool pr = s_press_x >= 0 && in_rc(r, s_press_x, s_press_y) && on;
    uint16_t c = on ? bg : tfd_rgb(70, 74, 90);
    tfd_rrect(r.x, r.y + 2, r.w, r.h, 9, tfd_rgb(10, 14, 26), 140);
    tfd_rrect(r.x, r.y + (pr ? 2 : 0), r.w, r.h, 9, c, 256);
    tfd_rrect(r.x + 3, r.y + 2 + (pr ? 2 : 0), r.w - 6, 3, 2, tfd_rgb(255, 255, 255), on ? 70 : 30);
    const kfont_t *f = ui_text_w(&KF_TEXT, txt) <= r.w - 10 ? &KF_TEXT : &KF_LABEL;
    if (f == &KF_LABEL && ui_text_w(f, txt) > r.w - 6 && ascii_only(txt)) {
        mf_text_c(&MF_TINY, r.x + r.w / 2, r.y + r.h / 2 + 4 + (pr ? 2 : 0), txt,
                  on ? tfd_rgb(255, 255, 255) : tfd_rgb(160, 164, 180), 255, 0);
        return;
    }
    ui_text_c(f, r.x + r.w / 2, r.y + r.h / 2 + (f == &KF_TEXT ? 7 : 5) + (pr ? 2 : 0), txt,
              on ? tfd_rgb(255, 255, 255) : tfd_rgb(160, 164, 180));
}
// one line in a width of maxw: KF_LABEL; when it is too wide, the tiny face (ASCII only), then a cut with a dot
static bool ascii_only(const char *s)
{
    for (; *s; s++) {
        if ((unsigned char)*s >= 128) {
            return false;
        }
    }
    return true;
}
static void text_fit(int cx, int base, int maxw, const char *s, uint16_t col)
{
    if (ui_text_w(&KF_LABEL, s) <= maxw) {
        ui_text_c(&KF_LABEL, cx, base, s, col);
        return;
    }
    if (!ascii_only(s)) {
        // (Hebrew: the first letters that fit, cut on a whole letter of UTF-8)
        char h[48];
        snprintf(h, sizeof h, "%s", s);
        int n = (int)strlen(h);
        while (n > 2 && ui_text_w(&KF_LABEL, h) > maxw) {
            do {
                n--;
            } while (n > 0 && ((unsigned char)h[n] & 0xC0) == 0x80);
            h[n] = 0;
        }
        ui_text_c(&KF_LABEL, cx, base, h, col);
        return;
    }
    char b[48];
    snprintf(b, sizeof b, "%s", s);
    int n = (int)strlen(b);
    while (n > 2 && mf_width(&MF_TINY, b, 0) > maxw) {
        b[--n] = 0;
        b[n - 1] = '.';
    }
    mf_text_c(&MF_TINY, cx, base - 1, b, col, 255, 0);
}
static uint16_t col_go(void)
{
    return tfd_rgb(43, 182, 115);
}
static uint16_t col_blue(void)
{
    return tfd_rgb(59, 120, 220);
}
static uint16_t col_red(void)
{
    return tfd_rgb(220, 70, 60);
}
static uint16_t col_dim(void)
{
    return tfd_rgb(70, 80, 110);
}

// ---------------------------------------------------------------- the game: start, save, the host step
static void view_snap(void)
{
    for (int p = 0; p < MAXP; p++) {
        s_spos[p] = (int8_t)g_v.P[p].pos;
        s_scash[p] = g_v.P[p].cash;
        s_scash_f[p] = (float)g_v.P[p].cash;
        s_mood[p] = 0;
    }
    s_vev = g_v.evn;
    s_turn_shown = g_v.cur;
    s_camx = s_spos[g_v.cur] * TW + TW / 2 + 40;
    s_hop_p = -1;
    s_dice_on = false;
    s_card = CD_NONE;
    s_sum_t = -1;
    memset(s_notes, 0, sizeof s_notes);
    memset(s_pops, 0, sizeof s_pops);
}
static void save_game(void)
{
    if (s_role != ROLE_LOCAL || g_v.magic != GAME_MAGIC) {
        return;
    }
#ifdef ESP_PLATFORM
    mkdir(BV_DIR, 0777);
#endif
    FILE *f = fopen(BV_DIR "/save.bin", "wb");
    if (!f) {
        return;
    }
    fwrite(&g_v, sizeof g_v, 1, f);
    fclose(f);
}
static bool load_game(bgame_t *g)
{
    FILE *f = fopen(BV_DIR "/save.bin", "rb");
    if (!f) {
        return false;
    }
    bool ok = fread(g, sizeof *g, 1, f) == 1 && g->magic == GAME_MAGIC && g->phase != PH_OVER && g->n >= 2 &&
              g->n <= MAXP;
    fclose(f);
    return ok;
}
// the friends game of the host (net.bin): the table (its number, the seats with the boards) and the game
#define NET_MAGIC 0x4E425631u
typedef struct {
    uint32_t magic;
    uint16_t tid;
    uint8_t nseats, mode, kind[MAXP], look[MAXP], mac[MAXP][3];
    bgame_t g;
} netsave_t;
static void save_net(void)
{
    if (s_role != ROLE_HOST || g_v.magic != GAME_MAGIC) {
        return;
    }
#ifdef ESP_PLATFORM
    mkdir(BV_DIR, 0777);
#endif
    static netsave_t ns;
    memset(&ns, 0, sizeof ns);
    ns.magic = NET_MAGIC;
    ns.tid = s_tid;
    ns.nseats = (uint8_t)s_nseats;
    ns.mode = (uint8_t)s_setup_mode;
    memcpy(ns.kind, s_seat_kind, MAXP);
    memcpy(ns.look, s_seat_look, MAXP);
    memcpy(ns.mac, s_seat_mac, sizeof ns.mac);
    ns.g = g_v;
    FILE *f = fopen(BV_DIR "/net.bin", "wb");
    if (!f) {
        return;
    }
    fwrite(&ns, sizeof ns, 1, f);
    fclose(f);
}
static bool load_net(netsave_t *ns)
{
    FILE *f = fopen(BV_DIR "/net.bin", "rb");
    if (!f) {
        return false;
    }
    bool ok = fread(ns, sizeof *ns, 1, f) == 1 && ns->magic == NET_MAGIC && ns->g.magic == GAME_MAGIC &&
              ns->g.phase != PH_OVER && ns->g.n >= 2 && ns->g.n <= MAXP && ns->nseats <= MAXP;
    fclose(f);
    return ok;
}
static void forget_net(void)
{
    FILE *f = fopen(BV_DIR "/net.bin", "wb");
    if (f) {
        fclose(f);
    }
}
static void forget_save(void)
{
    FILE *f = fopen(BV_DIR "/save.bin", "wb");
    if (f) {
        fclose(f);
    }
}
static void music_board(void)
{
    if (s_music) {
        midi_bg_play_track(BVM_DAY, 0, true);
    }
}
static void game_begin(void)
{
    view_snap();
    s_scr = SCR_PLAY;
    bot_reset();
    if (s_music) {
        midi_bg_play_track(BVM_J_START, 0, false);
    }
    s_jingle_until = s_now + BV_J_START_MS;
}
static void local_start(void)
{
    uint8_t kinds[MAXP], looks[MAXP];
    for (int p = 0; p < s_setup_n; p++) {
        kinds[p] = p == 0 ? PK_HUMAN : s_setup_lvl ? PK_GOOD : PK_EASY;
        looks[p] = (uint8_t)((s_my_look + p) % NLOOK);
    }
    g_new(&g_v, s_setup_n, kinds, looks, esp_random(), s_setup_mode);
    s_role = ROLE_LOCAL;
    s_my = 0;
    s_ev_have = g_v.evn;
    game_begin();
    save_game();
}
static void host_start(void)
{
    uint8_t kinds[MAXP], looks[MAXP];
    int n = 0;
    uint8_t map[MAXP];
    for (int i = 0; i < s_nseats; i++) {
        if (s_seat_kind[i] == SK_EMPTY) {
            continue;
        }
        kinds[n] = s_seat_kind[i] == SK_HUMAN ? PK_HUMAN : s_setup_lvl ? PK_GOOD : PK_EASY;
        looks[n] = s_seat_look[i];
        map[n] = (uint8_t)i;
        n++;
    }
    // (the seats close up: seat k of the game = the k-th taken seat of the room)
    uint8_t kind2[MAXP], look2[MAXP], mac2[MAXP][3];
    for (int k = 0; k < n; k++) {
        kind2[k] = s_seat_kind[map[k]];
        look2[k] = s_seat_look[map[k]];
        memcpy(mac2[k], s_seat_mac[map[k]], 3);
    }
    memset(s_seat_kind, 0, sizeof s_seat_kind);
    memcpy(s_seat_kind, kind2, (size_t)n);
    memcpy(s_seat_look, look2, (size_t)n);
    memcpy(s_seat_mac, mac2, (size_t)n * 3);
    s_nseats = n;
    g_new(&g_v, n, kinds, looks, esp_random(), s_setup_mode);
    s_my = 0;
    for (int i = 0; i < MAXP; i++) {
        s_seen[i] = s_now;
    }
    s_ev_have = g_v.evn;
    game_begin();
    save_net();
    send_table();
    send_state();
    send_events();
}
static void client_started(void)
{
    s_ev_have = g_v.evn;
    game_begin();
}
// an intent on the host (of this board, of a bot, of a client)
static void host_apply(int who, const bin_t *in)
{
    if (g_apply(&g_v, who, in)) {
        s_ev_have = g_v.evn;
        if (s_role == ROLE_HOST) {
            send_state();
            send_events();
        }
        if (g_v.phase == PH_ROLL || g_v.phase == PH_OVER) {
            save_game();
            save_net();
        }
        if (g_v.phase == PH_OVER) {
            if (s_role == ROLE_HOST) {
                forget_net();
            } else {
                forget_save();
            }
        }
    }
}
// an intent of the seat of this board
static void my_intent(int kind, int arg)
{
    bin_t in;
    memset(&in, 0, sizeof in);
    in.kind = (uint8_t)kind;
    in.arg = (uint8_t)arg;
    fx(FX_CLICK);
    if (s_role == ROLE_CLIENT) {
        net_intent(&in);
    } else {
        host_apply(s_my, &in);
    }
}
static void my_intent_full(const bin_t *in)
{
    fx(FX_CLICK);
    if (s_role == ROLE_CLIENT) {
        net_intent(in);
    } else {
        host_apply(s_my, in);
    }
}

// ---------------------------------------------------------------- the view: play the events
static bool view_busy(void)
{
    if (s_hop_p >= 0 || (s_dice_on && s_sum_t < 0.6f) || s_card == CD_DECK || s_card == CD_INFO) {
        return true;
    }
    return s_vev < s_ev_have;
}
static float s_ev_wait;  // the time that the event now still takes
static void ev_play(const bev_t *e)
{
    s_ev_wait = 0.15f;
    switch (e->type) {
    case EV_TURN:
        s_turn_shown = e->a;
        s_ev_wait = 0.35f;
        if (e->a == s_my && s_scr == SCR_PLAY) {
            fx(FX_CLICK);
        }
        break;
    case EV_ROLL:
        die_throw(&s_dice[0], e->b, 0);
        die_throw(&s_dice[1], e->c, 1);
        s_dice_on = true;
        s_sum_t = -1;
        fx(FX_DICE);
        s_ev_wait = 0;
        break;
    case EV_MOVE: {
        int p = e->a, from = e->b, to = e->c;
        s_hop_p = p;
        s_hop_t = 0;
        s_hop_back = (e->v & MV_BACK) != 0;
        s_hop_jump = !(e->v & MV_STEP);
        int steps = s_hop_back ? (from - to + NT) % NT : (to - from + NT) % NT;
        s_hop_from = s_spos[p];
        s_hop_to = s_hop_back ? s_hop_from - steps : s_hop_from + steps;
        if (s_hop_jump) {
            s_hop_to = s_hop_from + ((to - from + NT) % NT);
        }
        s_ev_wait = 0;
        break;
    }
    case EV_LAND:
        s_shake = fmaxf(s_shake, 0.8f);
        s_ev_wait = 0.25f;
        break;
    case EV_PAY: {
        int from = e->a, to = e->b;
        if (from < MAXP) {
            s_scash[from] -= e->v;
            s_mood[from] = -1;
            pop_add(from, e->v, false);
        }
        if (to < MAXP) {
            s_scash[to] += e->v;
            s_mood[to] = 1;
            pop_add(to, e->v, true);
        }
        if (e->c != PW_START || e->v != START_CASH) {
            notes_fly(from, to, e->v);
        }
        fx(FX_COIN);
        s_ev_wait = e->c == PW_RENT ? 0.7f : 0.4f;
        break;
    }
    case EV_BUY:
        fx(FX_BUY);
        s_mood[e->a] = 1;
        s_ev_wait = 0.5f;
        break;
    case EV_CARD:
        s_card = CD_DECK;
        s_card_deck = e->b;
        s_card_id = e->c;
        s_card_t = 0;
        fx(FX_CARD);
        s_ev_wait = 0;
        break;
    case EV_JAIL:
        s_bubble_p = e->a;
        s_bubble_t = 2.2f;
        snprintf(s_bubble, sizeof s_bubble, "%s", T("TRAFFIC JAM!", "פקק!"));
        fx(FX_JAM);
        s_ev_wait = 1.0f;
        break;
    case EV_STAY:
        s_bubble_p = e->a;
        s_bubble_t = 1.6f;
        snprintf(s_bubble, sizeof s_bubble, "%s", T("STILL STUCK", "עדיין תקועים"));
        s_ev_wait = 0.6f;
        break;
    case EV_FREE:
        s_bubble_p = e->a;
        s_bubble_t = 1.6f;
        snprintf(s_bubble, sizeof s_bubble, "%s", T("OUT OF THE JAM", "יוצאים מהפקק"));
        s_ev_wait = 0.6f;
        break;
    case EV_BUILD:
        s_new_house = e->b;
        s_new_house_t = 0;
        fx(FX_BUILD);
        s_ev_wait = 0.35f;
        break;
    case EV_TRADE:
        s_bubble_p = e->a;
        s_bubble_t = 2.0f;
        snprintf(s_bubble, sizeof s_bubble, "%s",
                 e->c == TR_OFFER     ? T("AN OFFER", "הצעה")
                 : e->c == TR_ACCEPT  ? T("A DEAL!", "עסקה!")
                 : e->c == TR_DECLINE ? T("NO DEAL", "אין עסקה")
                                      : T("CANCELED", "בוטל"));
        s_ev_wait = 0.8f;
        break;
    case EV_DEBT:
        s_bubble_p = e->a;
        s_bubble_t = 2.0f;
        snprintf(s_bubble, sizeof s_bubble, "%s", T("NOT ENOUGH CASH", "אין מספיק מזומן"));
        fx(FX_BAD);
        s_ev_wait = 0.8f;
        break;
    case EV_BROKE:
        s_bubble_p = e->a;
        s_bubble_t = 2.4f;
        snprintf(s_bubble, sizeof s_bubble, "%s", T("BANKRUPT", "פשט רגל"));
        if (s_music) {
            midi_bg_play_track(BVM_J_BROKE, 0, false);
            s_jingle_until = s_now + BV_J_BROKE_MS;
        }
        s_ev_wait = 1.5f;
        break;
    case EV_WIN:
        if (s_music) {
            midi_bg_play_track(BVM_J_WIN, 0, false);
            s_jingle_until = 0;
        }
        s_ev_wait = 2.0f;
        break;
    case EV_CARDGET:
        s_bubble_p = e->a;
        s_bubble_t = 2.0f;
        snprintf(s_bubble, sizeof s_bubble, "%s", T("A FREE RIDE CARD", "כרטיס נסיעה חינם"));
        s_ev_wait = 0.4f;
        break;
    default:
        break;
    }
}
static void view_step(float dt)
{
    // the things that move
    if (s_dice_on) {
        die_step(&s_dice[0], dt);
        die_step(&s_dice[1], dt);
        if (s_dice[0].rest && s_dice[1].rest && s_dice[0].t > 1.2f) {
            if (s_sum_t < 0) {
                s_sum_t = 0;
            } else {
                s_sum_t += dt;
            }
        }
        if (s_sum_t > 3.5f) {
            s_dice_on = false;
        }
    }
    if (s_hop_p >= 0) {
        float speed = s_speed ? 6.0f : 3.6f;
        float before = s_hop_t;
        s_hop_t += dt * (s_hop_jump ? 1.2f : speed);
        int steps = abs(s_hop_to - s_hop_from);
        if (!s_hop_jump && (int)s_hop_t != (int)before) {
            fx(FX_HOP);
        }
        if (s_hop_t >= (s_hop_jump ? 1 : steps)) {
            s_spos[s_hop_p] = (int8_t)(((s_hop_to % NT) + NT) % NT);
            s_hop_p = -1;
            s_hop_t = -1;
            s_shake = fmaxf(s_shake, 0.8f);
        }
    }
    if (s_card == CD_DECK) {
        s_card_t += dt;
        if (s_card_t > (s_speed ? 2.2f : 3.4f)) {
            s_card = CD_NONE;
        }
    }
    for (int i = 0; i < NNOTE; i++) {
        if (s_notes[i].t < 1) {
            s_notes[i].t += dt / 0.8f;
        }
    }
    for (int i = 0; i < NPOP; i++) {
        if (s_pops[i].on) {
            s_pops[i].life += dt;
        }
    }
    if (s_bubble_t > 0) {
        s_bubble_t -= dt;
    }
    s_new_house_t += dt;
    for (int p = 0; p < MAXP; p++) {
        s_mood[p] *= expf(-dt * 0.5f);
        float d = s_scash[p] - s_scash_f[p];
        s_scash_f[p] += d * fminf(1, dt * 6);
        if (fabsf(d) < 1) {
            s_scash_f[p] = (float)s_scash[p];
        }
    }
    s_shake *= expf(-dt * 7);
    // the next event
    if (s_ev_wait > 0) {
        s_ev_wait -= dt * (s_speed ? 1.6f : 1);
    }
    bool moving = s_hop_p >= 0 || (s_dice_on && s_sum_t < 0.7f) || s_card == CD_DECK;
    // (a client that missed many events: it jumps to the state)
    if (s_vev + EVLOG - 8 < s_ev_have) {
        view_snap();
    }
    if (!moving && s_ev_wait <= 0 && s_vev < s_ev_have) {
        s_vev++;
        ev_play(&s_evlog[s_vev % EVLOG]);
    }
    if (!view_busy()) {
        // all shown: the shown state is the state
        for (int p = 0; p < g_v.n; p++) {
            s_spos[p] = (int8_t)g_v.P[p].pos;
            s_scash[p] = g_v.P[p].cash;
        }
        s_turn_shown = g_v.cur;
    }
    // the camera: the token of the turn (with the card: a little to the left)
    int me = s_hop_p >= 0 ? s_hop_p : s_turn_shown;
    float want = token_x(me) + ((g_v.phase == PH_BUY && !view_busy()) ? 150 : 40);
    // (the strip goes round: the camera takes the short way)
    s_camx += (want - s_camx) * (1 - expf(-dt * (s_hop_p >= 0 ? 4 : 2)));
    float wz = ((g_v.phase == PH_BUY && !view_busy()) ? 1.06f : s_hop_p >= 0 ? 1.1f : 1.0f) * ZB;
    s_camz += (wz - s_camz) * (1 - expf(-dt * 2));
}

// ---------------------------------------------------------------- the host: the bots and the clock
static float s_bot_wait;
static void host_step(float dt)
{
    if (s_role == ROLE_CLIENT || g_v.magic != GAME_MAGIC || g_v.phase == PH_OVER) {
        return;
    }
    if (g_v.mode == MODE_TIME && s_scr != SCR_TITLE) {
        g_tick(&g_v, (uint32_t)(dt * 1000));
    }
    if (view_busy()) {
        s_bot_wait = s_speed ? 0.25f : 0.6f;
        return;
    }
    int a = g_actor(&g_v);
    if (a == NOBODY) {
        return;
    }
    bool human_here = a == s_my && g_v.P[a].kind == PK_HUMAN && s_role != ROLE_CLIENT;
    bool remote = s_role == ROLE_HOST && g_v.P[a].kind == PK_HUMAN && a != s_my && !g_v.P[a].away;
    if (human_here || remote) {
        return;
    }
    if ((s_bot_wait -= dt) > 0) {
        return;
    }
    bin_t in;
    memset(&in, 0, sizeof in);
    if (bot_act(&g_v, &in)) {
        host_apply(g_actor(&g_v) == a ? a : g_actor(&g_v), &in);
        s_bot_wait = s_speed ? 0.25f : 0.7f;
    }
}

// ---------------------------------------------------------------- the texts
static const char *phase_text(void)
{
    static char b[64];
    int a = g_actor(&g_v);
    if (g_v.phase == PH_OVER) {
        return T("THE GAME IS OVER", "המשחק נגמר");
    }
    if (a == s_my) {
        switch (g_v.phase) {
        case PH_ROLL:
            return g_v.P[s_my].jail ? T("THE JAM: ROLL A DOUBLE, OR PAY", "פקק: גלגלו כפול, או שלמו")
                                    : T("YOUR TURN: ROLL THE DICE", "התור שלכם: גלגלו את הקוביות");
        case PH_BUY:
            return T("BUY THE STREET?", "לקנות את הרחוב?");
        case PH_END:
            return g_v.again ? T("A DOUBLE: ROLL AGAIN", "כפול: גלגלו שוב") : T("END YOUR TURN", "סיימו את התור");
        case PH_DEBT:
            return T("YOU OWE MONEY", "אתם חייבים כסף");
        case PH_TRADE:
            return T("AN OFFER FOR YOU", "יש לכם הצעה");
        }
    }
    snprintf(b, sizeof b, s_lang ? "התור של %s" : "%s PLAYS", a < MAXP ? pname(a) : "");
    return b;
}

// ---------------------------------------------------------------- the play screen: the buttons
static rc_t R_ROLL = {500, 122, 132, 44}, R_MANAGE = {8, 126, 92, 40}, R_TRADE = {106, 126, 92, 40},
            R_MAP = {204, 126, 74, 40};
static rc_t R_BAIL = {356, 122, 136, 44};
// the done and cancel buttons of MANAGE and TRADE, TITLE of the end (the 3.49: at the right of the bottom row)
static rc_t rc_done(void)
{
    if (RND) {
        return (rc_t){VW / 2 + 6, PC(70), PC(30), 50};
    }
    if (TAB5) {
        return (rc_t){VW - 150, FH - 56, 142, 48};
    }
    return NARROW ? (rc_t){VW - 136, FH - 46, 132, 42} : (rc_t){VW - 140, 128, 132, 40};
}
static rc_t rc_cancel(void)
{
    if (RND) {
        return (rc_t){VW / 2 - 6 - PC(30), PC(70), PC(30), 50};
    }
    if (TAB5) {
        return (rc_t){8, FH - 56, 142, 48};
    }
    return NARROW ? (rc_t){4, FH - 46, 132, 42} : (rc_t){VW - 280, 128, 132, 40};
}
static rc_t rc_end_title(void)
{
    if (RPLAY) {
        return (rc_t){VW / 2 - 80, FH * 76 / 100, 160, 50};
    }
    if (TAB5) {
        return (rc_t){VW / 2 - 75, FH - 60, 150, 52};
    }
    return NARROW ? (rc_t){VW / 2 - 66, FH - 50, 132, 44} : (rc_t){VW - 140, 128, 132, 40};
}
static rc_t R_BUY = {454, 128, 84, 38}, R_PASS = {546, 128, 84, 38};
static bool my_turn_ready(void)
{
    return !view_busy() && g_v.phase != PH_OVER && g_actor(&g_v) == s_my && !(s_role == ROLE_CLIENT && s_in_pending);
}
static void card_street_draw(int pr, bool buttons)
{
    const tile_t *Tt = PT(pr);
    int x = NARROW ? VW - 196 : 446, y = TAB5 ? 96 : 32, w = 190, h = 136;
    if (RPLAY) {
        x = VW / 2 - w / 2;
        y = FH * 25 / 100;
    }
    tfd_rrect(x + 3, y + 4, w, h, 8, tfd_rgb(20, 30, 60), 90);
    tfd_rrect(x, y, w, h, 8, tfd_rgb(251, 247, 238), 256);
    uint16_t gc = tfd_rgb(GROUP_RGB[Tt->group][0], GROUP_RGB[Tt->group][1], GROUP_RGB[Tt->group][2]);
    tfd_rrect(x + 6, y + 6, w - 12, 30, 4, gc, 256);
    if (ui_text_w(&KF_TEXT, Tt->name[s_lang]) <= w - 22) {
        ui_text_c(&KF_TEXT, x + w / 2, y + 27, Tt->name[s_lang], tfd_rgb(255, 255, 255));
    } else {
        text_fit(x + w / 2, y + 26, w - 18, Tt->name[s_lang], tfd_rgb(255, 255, 255));
    }
    money_at(&MF_N16, x + w / 2, y + 52, Tt->price, tfd_rgb(31, 42, 68), tfd_rgb(251, 247, 238));
    // the rents: no floor, one floor, the tower (a street); the transport and the services: their rule
    uint16_t ink = tfd_rgb(90, 100, 120);
    if (Tt->kind == TK_STREET) {
        static const char *const RL[3][2] = {{"RENT", "שכירות"}, {"1 FLOOR", "קומה 1"}, {"TOWER", "מגדל"}};
        int ri[3] = {0, 1, 5};
        for (int k = 0; k < 3; k++) {
            int ly = y + 66 + k * 12;
            ui_text_c(&KF_LABEL, s_lang ? x + w - 44 : x + 40, ly, RL[k][s_lang], ink);
            money_at(&MF_TINY, s_lang ? x + 46 : x + w - 40, ly, Tt->rent[ri[k]], ink, tfd_rgb(251, 247, 238));
        }
    } else if (Tt->kind == TK_TRANS) {
        text_box(&KF_LABEL, x + w / 2, y + 66, w - 16, 11,
                 T("RENT: 25, 50, 100, 200 BY THE TRANSPORT YOU OWN", "שכירות: 25, 50, 100, 200 לפי התחבורה שבבעלותכם"),
                 ink, 3);
    } else {
        text_box(&KF_LABEL, x + w / 2, y + 66, w - 16, 11,
                 T("RENT: THE DICE x4, OR x10 WITH BOTH", "שכירות: הקוביות כפול 4, או כפול 10 עם שניהם"), ink, 3);
    }
    if (buttons) {
        R_BUY = (rc_t){x + 8, y + h - 40, 84, 36};
        R_PASS = (rc_t){x + w - 92, y + h - 40, 84, 36};
        if (s_lang) {
            rc_t t = R_BUY;
            R_BUY = R_PASS;
            R_PASS = t;
        }
        button(R_BUY, T("BUY", "קנייה"), col_go(), g_v.P[s_my].cash >= Tt->price);
        button(R_PASS, T("PASS", "ויתור"), tfd_rgb(150, 145, 135), true);
    } else {
        char b[48];
        snprintf(b, sizeof b, s_lang ? "%s חושב/ת..." : "%s THINKS...", pname(g_v.cur));
        ui_text_c(&KF_LABEL, x + w / 2, y + h - 14, b, ink);
    }
}
static void card_deck_draw(void)
{
    int x = NARROW ? (VW - 300) / 2 : 170, y = TAB5 ? 104 : RPLAY ? FH * 26 / 100 : 40, w = 300, h = 100;
    float k = fminf(1, s_card_t / 0.3f);
    y += (int)((1 - k) * 80);
    tfd_rrect(x + 3, y + 4, w, h, 8, tfd_rgb(20, 30, 60), 90);
    tfd_rrect(x, y, w, h, 8, tfd_rgb(251, 247, 238), 256);
    tfd_rrect(x + 6, y + 6, w - 12, 24, 4, s_card_deck ? tfd_rgb(255, 196, 40) : tfd_rgb(70, 120, 200), 256);
    ui_text_c(&KF_TEXT, x + w / 2, y + 24, DECK_NAME[s_card_deck][s_lang],
              s_card_deck ? tfd_rgb(60, 40, 0) : tfd_rgb(255, 255, 255));
    char b[200];
    sh_text(b, sizeof b, CARDS[s_card_deck][s_card_id % NCARD].text[s_lang]);
    text_box(&KF_LABEL, x + w / 2, y + 48, w - 20, 14, b, tfd_rgb(31, 42, 68), 4);
}
// BV_PROF: the time of each part of the play frame in the log (each 3 s)
#if defined(BV_PROF) && defined(ESP_PLATFORM)
#include "esp_log.h"
#include "esp_timer.h"
static int64_t s_pf[6], s_pf_t0;
static int s_pf_n;
#define PF(i)                                                                                                          \
    do {                                                                                                               \
        int64_t _t = esp_timer_get_time();                                                                             \
        s_pf[i] += _t - s_pf_t0;                                                                                       \
        s_pf_t0 = _t;                                                                                                  \
    } while (0)
#else
#define PF(i)                                                                                                          \
    do {                                                                                                               \
    } while (0)
#endif
static void play_draw(void)
{
#if defined(BV_PROF) && defined(ESP_PLATFORM)
    s_pf_t0 = esp_timer_get_time();
#endif
    sky_draw();
    PF(0);
    tfd_rect(0, 98 + SKY_Y0, VW, 8, tfd_rgb(255, 240, 215), 90);
    board_draw();
    PF(1);
    tfd_rect(0, (int)YN + 9, VW, FH - (int)YN - 9, tfd_rgb(22, 48, 79), 256);  // (a narrow frame: the bar too)
    if (s_dice_on) {
        die_draw(&s_dice[0], false);
        die_draw(&s_dice[1], true);
        if (s_sum_t >= 0) {
            char b[4];
            snprintf(b, sizeof b, "%d", s_dice[0].v + s_dice[1].v);
            float sc = s_sum_t < 0.4f ? 1 + (0.4f - s_sum_t) * 1.2f : 1;
            (void)sc;
            mf_text_oc(&MF_N28, (int)((s_dice[0].x + s_dice[1].x) / 2), 96 + DY, b, tfd_rgb(255, 210, 63),
                       tfd_rgb(31, 42, 68), 2, 1);
        }
    }
    PF(2);
    panels_draw();
    PF(3);
    notes_draw();
    // the line of the turn
    const char *pt = phase_text();
    int tw = ui_text_w(&KF_LABEL, pt) + 16;
    int ty = g_v.n <= 4 ? 4 : 30;
    int tx = g_v.n <= 4 ? VW - 6 - tw : VW / 2 - tw / 2;
    if (g_v.n > 4) {
        ty = 30;
    }
    if (NARROW) {
        ty = g_v.n <= 4 ? 30 : 56;
        tx = VW / 2 - tw / 2;
    }  // (under the panels)
    if (RPLAY) {
        int py, pw;
        panel_box(g_v.n - 1, g_v.n, &tx, &py, &pw);
        ty = py + 28;
        tx = VW / 2 - tw / 2;
    }
    tfd_rrect(tx, ty, tw, 17, 5, tfd_rgb(15, 22, 40), 170);
    ui_text_c(&KF_LABEL, tx + tw / 2, ty + 13, pt, tfd_rgb(255, 255, 255));
    if (s_card == CD_DECK) {
        card_deck_draw();
    }
#if defined(BV_PROF) && defined(ESP_PLATFORM)
    PF(4);
    if (++s_pf_n >= 90) {
        ESP_LOGI("blvd", "prof us: sky %d board %d dice %d panels %d text %d", (int)(s_pf[0] / s_pf_n),
                 (int)(s_pf[1] / s_pf_n), (int)(s_pf[2] / s_pf_n), (int)(s_pf[3] / s_pf_n), (int)(s_pf[4] / s_pf_n));
        ESP_LOGI("blvd", "prof board us: tiles %d text %d houses %d pawns %d", (int)(s_bd[0] / s_pf_n),
                 (int)(s_bd[1] / s_pf_n), (int)(s_bd[2] / s_pf_n), (int)(s_bd[3] / s_pf_n));
        memset(s_bd, 0, sizeof s_bd);
        memset(s_pf, 0, sizeof s_pf);
        s_pf_n = 0;
    }
#endif
    bool ready = my_turn_ready();
    if (g_v.phase == PH_BUY && !view_busy() && g_v.buy_prop < NPROP) {
        card_street_draw(g_v.buy_prop, ready && g_v.cur == s_my);
    }
    if (!ready || g_v.phase == PH_BUY) {
        if (!view_busy() || g_v.phase == PH_OVER) {
            button(R_MAP, T("MAP", "מפה"), col_dim(), true);
        }
        return;
    }
    // the buttons of my turn
    button(R_MAP, T("MAP", "מפה"), col_dim(), true);
    bool can_manage = g_can_manage(&g_v, s_my);
    button(R_MANAGE, T("MANAGE", "ניהול"), col_dim(), can_manage);
    button(R_TRADE, T("TRADE", "החלפה"), col_dim(), g_v.phase == PH_ROLL || g_v.phase == PH_END);
    if (g_v.phase == PH_ROLL) {
        button(R_ROLL, T("ROLL", "גלגול"), col_go(), true);
        const bpl_t *P = &g_v.P[s_my];
        if (P->jail && P->cards) {
            button(R_BAIL, T("FREE CARD", "כרטיס חינם"), col_blue(), true);
        } else if (P->jail) {
            button(R_BAIL, T("PAY 50", "תשלום 50"), col_blue(), P->cash >= BAIL);
        }
    } else if (g_v.phase == PH_END) {
        button(R_ROLL, T("END TURN", "סיום תור"), col_blue(), true);
    } else if (g_v.phase == PH_DEBT) {
        const bdebt_t *d = &g_v.debt[0];
        char b[64];
        snprintf(b, sizeof b, s_lang ? "חוב: %ld" : "DEBT: %ld", (long)d->amount);
        const int dx = NARROW ? VW / 2 : 395;
        tfd_rrect(dx - 105, 92 + DY, 210, 26, 6, tfd_rgb(15, 22, 40), 200);
        ui_text_c(&KF_TEXT, dx, 111 + DY, b, tfd_rgb(255, 160, 140));
        bool can_pay = g_v.P[s_my].cash >= d->amount;
        if (can_pay) {
            button(R_ROLL, T("PAY", "תשלום"), col_go(), true);
        } else if (g_liquid(&g_v, s_my) < d->amount) {
            button(R_ROLL, T("BANKRUPT", "פשיטת רגל"), col_red(), true);
        } else {
            button(R_ROLL, T("SELL FIRST", "קודם למכור"), col_dim(), false);
        }
    } else if (g_v.phase == PH_TRADE) {
        // an offer for me: what I give, what I get
        const btrade_t *t = &g_v.tr;
        if (t->to == s_my) {
            int ox = NARROW ? 6 : 150, ow = NARROW ? VW - 12 : 340, oy = 0;
            if (RPLAY) {
                ox = VW * 12 / 100;
                ow = VW - 2 * ox;
                oy = FH * 25 / 100 - 40;
            }
            const int cx = ox + ow / 2;
            tfd_rrect(ox, 40 + oy, ow, 80, 8, tfd_rgb(251, 247, 238), 245);
            char b[96];
            snprintf(b, sizeof b, s_lang ? "%s מציע/ה:" : "%s OFFERS:", pname(t->from));
            ui_text_c(&KF_TEXT, cx, 58 + oy, b, tfd_rgb(31, 42, 68));
            char give[96] = "", get[96] = "";
            int ng = 0, nt = 0;
            for (int i = 0; i < NPROP; i++) {
                if (t->give & (1u << i)) {
                    ng += snprintf(get + ng, sizeof get - (size_t)ng, "%s%s", ng ? ", " : "", PT(i)->name[s_lang]);
                }
                if (t->get & (1u << i)) {
                    nt += snprintf(give + nt, sizeof give - (size_t)nt, "%s%s", nt ? ", " : "", PT(i)->name[s_lang]);
                }
            }
            if (t->cash_give) {
                ng += snprintf(get + ng, sizeof get - (size_t)ng, "%s%ld", ng ? ", " : "", (long)t->cash_give);
            }
            if (t->cash_get) {
                nt += snprintf(give + nt, sizeof give - (size_t)nt, "%s%ld", nt ? ", " : "", (long)t->cash_get);
            }
            char l1[120], l2[120];
            snprintf(l1, sizeof l1, s_lang ? "אתם מקבלים: %s" : "YOU GET: %s", ng ? get : "-");
            snprintf(l2, sizeof l2, s_lang ? "אתם נותנים: %s" : "YOU GIVE: %s", nt ? give : "-");
            if (ui_text_w(&KF_LABEL, l1) <= ow - 10) {
                ui_text_c(&KF_LABEL, cx, 80 + oy, l1, tfd_rgb(40, 120, 70));
            } else {
                text_fit(cx, 80 + oy, ow - 10, l1, tfd_rgb(40, 120, 70));
            }
            if (ui_text_w(&KF_LABEL, l2) <= ow - 10) {
                ui_text_c(&KF_LABEL, cx, 98 + oy, l2, tfd_rgb(170, 60, 50));
            } else {
                text_fit(cx, 98 + oy, ow - 10, l2, tfd_rgb(170, 60, 50));
            }
            button(R_BAIL, T("NO", "לא"), col_red(), true);
            button(R_ROLL, T("ACCEPT", "מסכימים"), col_go(), true);
        }
    }
}
static void play_tap(int x, int y)
{
    if (in_rc(R_MAP, x, y) && (!view_busy() || g_v.phase == PH_OVER)) {
        s_scr = SCR_MAP;
        s_map_sel = -1;
        fx(FX_CLICK);
        return;
    }
    if (s_card == CD_DECK && s_card_t > 0.5f) {
        s_card_t = 99;
        return;
    }  // (a tap closes the card)
    if (!my_turn_ready()) {
        return;
    }
    if (g_v.phase == PH_BUY && g_v.cur == s_my) {
        if (in_rc(R_BUY, x, y) && g_v.P[s_my].cash >= PT(g_v.buy_prop)->price) {
            my_intent(IN_BUY, 0);
        } else if (in_rc(R_PASS, x, y)) {
            my_intent(IN_PASS, 0);
        }
        return;
    }
    if (in_rc(R_MANAGE, x, y) && g_can_manage(&g_v, s_my)) {
        manage_open();
        return;
    }
    if (in_rc(R_TRADE, x, y) && (g_v.phase == PH_ROLL || g_v.phase == PH_END)) {
        trade_open();
        return;
    }
    if (g_v.phase == PH_ROLL) {
        if (in_rc(R_ROLL, x, y)) {
            my_intent(IN_ROLL, 0);
        } else if (in_rc(R_BAIL, x, y) && g_v.P[s_my].jail) {
            if (g_v.P[s_my].cards) {
                my_intent(IN_CARD, 0);
            } else if (g_v.P[s_my].cash >= BAIL) {
                my_intent(IN_BAIL, 0);
            }
        }
    } else if (g_v.phase == PH_END) {
        if (in_rc(R_ROLL, x, y)) {
            my_intent(IN_END, 0);
        }
    } else if (g_v.phase == PH_DEBT) {
        if (in_rc(R_ROLL, x, y)) {
            if (g_v.P[s_my].cash >= g_v.debt[0].amount) {
                my_intent(IN_PAY, 0);
            } else if (g_liquid(&g_v, s_my) < g_v.debt[0].amount) {
                my_intent(IN_BROKE, 0);
            }
        }
    } else if (g_v.phase == PH_TRADE && g_v.tr.to == s_my) {
        if (in_rc(R_ROLL, x, y)) {
            my_intent(IN_ANSWER, 1);
        } else if (in_rc(R_BAIL, x, y)) {
            my_intent(IN_ANSWER, 0);
        }
    }
}

// ---------------------------------------------------------------- MANAGE: the floors, the towers, the mortgages
static uint8_t s_plan[NPROP];
static int s_mng_scroll, s_mng_n, s_mng_list[NPROP];
static void manage_open(void)
{
    plan_now(&g_v, s_plan);
    s_mng_n = 0;
    for (int i = 0; i < NPROP; i++) {
        if (g_v.R[i].owner == s_my) {
            s_mng_list[s_mng_n++] = i;
        }
    }
    s_mng_scroll = 0;
    s_scr = SCR_MANAGE;
    fx(FX_CLICK);
}
#define MNG_W 150
#define MNG_Y (TAB5 ? 110 : 30)  // the top of the cards
// the card k of MANAGE and its buttons: - and + (the floors of a street), MORTGAGE / REDEEM. The round frame: bigger
// cards (48 px buttons), two in the middle band of the circle, the next ones at its sides
// the round frame: a row of dots under a row of cards that scrolls (n cards, the dots of the cards in the view are
// light)
static void scroll_dots(int n, int first, int vis, int y)
{
    if (n <= vis) {
        return;
    }
    const int sp = n > 20 ? 9 : 12;
    for (int k = 0; k < n; k++) {
        bool on = k >= first && k < first + vis;
        tfd_disc((float)(VW / 2 - (n - 1) * sp / 2 + k * sp), (float)y, on ? 3.5f : 2.5f,
                 on ? tfd_rgb(230, 235, 250) : tfd_rgb(90, 100, 130), 256);
    }
}
static int mng_w(void)
{
    return RND ? PC(40) : MNG_W;
}
static void mng_rects(int k, rc_t *card, rc_t *mi, rc_t *pl, rc_t *mo)
{
    const int w = mng_w(), bs = RND ? 44 : 36, by = RND ? 54 : 50;
    int x0 = 8, y = MNG_Y;
    if (RND) {
        x0 = s_mng_n == 1 ? VW / 2 - w / 2 : VW / 2 - w - 3;
        y = PC(32);
    }
    const int x = x0 + k * (w + 6) - s_mng_scroll;
    *card = (rc_t){x, y, w, RND ? 104 : 92};
    *mi = (rc_t){x + 4, y + by, bs, bs};
    *pl = (rc_t){x + w - 4 - bs, y + by, bs, bs};
    if (PT(s_mng_list[k])->kind == TK_STREET) {
        *mo = (rc_t){x + bs + 7, y + by, w - 2 * (bs + 7), bs};
    } else {
        *mo = (rc_t){x + 20, y + by, w - 40, bs};
    }
}
static void manage_draw(void)
{
    tfd_rect(0, 0, VW, FH, tfd_rgb(18, 26, 44), 256);
    int32_t cost = 0;
    bool ok = plan_ok(&g_v, s_my, s_plan, &cost);
    // the top line: the cash after the plan
    char b[64];
    ui_text_c(&KF_TEXT,
              RND      ? VW / 2
              : NARROW ? 70
                       : 120,
              RND ? PC(15) : 20, T("MANAGE", "ניהול נכסים"), tfd_rgb(255, 255, 255));
    snprintf(b, sizeof b, s_lang ? "מזומן אחרי: %ld" : "CASH AFTER: %ld", (long)(g_v.P[s_my].cash - cost));
    ui_text_c(&KF_LABEL,
              RND      ? VW / 2
              : NARROW ? VW - 80
                       : 330,
              RND ? PC(22) : 19, b, ok ? tfd_rgb(160, 230, 170) : tfd_rgb(255, 130, 110));
    if (!s_mng_n) {
        ui_text_c(&KF_TEXT, VW / 2,
                  RND    ? PC(46)
                  : TAB5 ? 160
                         : 90,
                  T("YOU HAVE NO PROPERTY YET", "עוד אין לכם נכסים"), tfd_rgb(200, 205, 220));
    }
    for (int k = 0; k < s_mng_n; k++) {
        rc_t cr, mi, pl, mo;
        mng_rects(k, &cr, &mi, &pl, &mo);
        const int pr = s_mng_list[k], x = cr.x, y = cr.y, w = cr.w;
        if (x > VW || x + w < 0) {
            continue;
        }
        const tile_t *Tt = PT(pr);
        int lv = s_plan[pr] & 7;
        bool mort = (s_plan[pr] & 0x80) != 0;
        const int hh = RND ? 22 : 18, fy = RND ? 32 : 30;
        tfd_rrect(x, y, w, cr.h, 6, mort ? tfd_rgb(90, 80, 80) : tfd_rgb(251, 247, 238), 256);
        tfd_rrect(x + 4, y + 4, w - 8, hh, 3,
                  tfd_rgb(GROUP_RGB[Tt->group][0], GROUP_RGB[Tt->group][1], GROUP_RGB[Tt->group][2]), 256);
        text_fit(x + w / 2, y + hh - 1, w - 10, Tt->name[s_lang], tfd_rgb(255, 255, 255));
        // the floors: small houses (a tower: one big)
        if (Tt->kind == TK_STREET) {
            if (lv == 5) {
                tfd_rrect(x + w / 2 - 14, y + fy - 2, 28, 18, 2, tfd_rgb(200, 60, 60), 256);
            } else {
                for (int j = 0; j < 4; j++) {
                    tfd_rrect((RND ? x + w / 2 - 52 : x + 22) + j * 28, y + fy, 20, 14, 2,
                              j < lv ? tfd_rgb(70, 160, 90) : tfd_rgb(210, 205, 195), 256);
                }
            }
            button(mi, "-", col_dim(), lv > 0);
            button(pl, "+", col_go(), lv < 5 && !mort);
        }
        button(mo, mort ? T("REDEEM", "פדייה") : T("MORTGAGE", "משכון"), mort ? col_blue() : tfd_rgb(150, 110, 60),
               lv == 0);
    }
    if (RND) {
        scroll_dots(s_mng_n, (s_mng_scroll + mng_w() / 2) / (mng_w() + 6), 2, PC(32) + 118);
    }
    rc_t done = rc_done(), cancel = rc_cancel();
    button(done, T("DONE", "אישור"), col_go(), ok);
    button(cancel, T("CANCEL", "ביטול"), col_dim(), true);
    if (!ok) {
        ui_text_c(&KF_LABEL, NARROW ? VW / 2 : 160,
                  RND      ? PC(64)
                  : NARROW ? 142
                  : TAB5   ? 236
                           : 152,
                  T("BUILD EVENLY; KEEP ENOUGH CASH", "בונים באופן שווה; צריך מספיק מזומן"), tfd_rgb(255, 160, 140));
    }
}
static int s_drag_x0 = -1, s_scroll0;
static void manage_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        s_drag_x0 = ev->x;
        s_scroll0 = s_mng_scroll;
        return;
    }
    rc_t c0, m0, p0, o0;
    if (s_mng_n) {
        mng_rects(0, &c0, &m0, &p0, &o0);
    } else {
        c0 = (rc_t){0, MNG_Y, 0, 92};
    }
    if (ev->kind == IN_MOVE && s_drag_x0 >= 0 && ev->y > c0.y - 2 && ev->y < c0.y + c0.h + 2) {
        int mx = s_mng_n * (mng_w() + 6) - (RND ? 2 * mng_w() + 12 : VW - 16);
        s_mng_scroll = s_scroll0 - (ev->x - s_drag_x0);
        if (s_mng_scroll > mx) {
            s_mng_scroll = mx;
        }
        if (s_mng_scroll < 0) {
            s_mng_scroll = 0;
        }
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    int x = ev->x, y = ev->y;
    rc_t done = rc_done(), cancel = rc_cancel();
    if (in_rc(cancel, x, y)) {
        s_scr = SCR_PLAY;
        fx(FX_CLICK);
        return;
    }
    if (in_rc(done, x, y)) {
        int32_t cost;
        if (!plan_ok(&g_v, s_my, s_plan, &cost)) {
            fx(FX_BAD);
            return;
        }
        bin_t in;
        memset(&in, 0, sizeof in);
        in.kind = IN_PLAN;
        memcpy(in.plan, s_plan, NPROP);
        my_intent_full(&in);
        s_scr = SCR_PLAY;
        return;
    }
    for (int k = 0; k < s_mng_n; k++) {
        int pr = s_mng_list[k];
        const tile_t *Tt = PT(pr);
        int lv = s_plan[pr] & 7;
        bool mort = (s_plan[pr] & 0x80) != 0;
        rc_t cr, mi, pl, mo;
        mng_rects(k, &cr, &mi, &pl, &mo);
        if (Tt->kind == TK_STREET) {
            if (in_rc(mi, x, y) && lv > 0) {
                s_plan[pr] = (uint8_t)((s_plan[pr] & 0x80) | (lv - 1));
                fx(FX_CLICK);
                return;
            }
            if (in_rc(pl, x, y) && lv < 5 && !mort) {
                s_plan[pr] = (uint8_t)(lv + 1);
                fx(FX_BUILD);
                return;
            }
        }
        if (in_rc(mo, x, y) && lv == 0) {
            s_plan[pr] ^= 0x80;
            fx(FX_CLICK);
            return;
        }
    }
}

// ---------------------------------------------------------------- TRADE: an offer to one player
static btrade_t s_tr;
static int s_tr_scroll;
static void trade_open(void)
{
    memset(&s_tr, 0, sizeof s_tr);
    s_tr.from = (uint8_t)s_my;
    s_tr.to = NOBODY;
    for (int k = 1; k < g_v.n; k++) {
        int p = (s_my + k) % g_v.n;
        if (!g_v.P[p].out) {
            s_tr.to = (uint8_t)p;
            break;
        }
    }
    s_tr_scroll = 0;
    s_scr = SCR_TRADE;
    fx(FX_CLICK);
}
// the chips of the properties of the two players, in one row: mine (give) at the left, the other's (get) at the right
static int trade_chips(int *out, int *side)
{
    int n = 0;
    for (int s = 0; s < 2; s++) {
        for (int i = 0; i < NPROP; i++) {
            if (g_v.R[i].owner == (s ? s_tr.to : s_my) && !g_v.R[i].level) {
                out[n] = i;
                side[n] = s;
                n++;
            }
        }
    }
    return n;
}
#define TR_CW 96
// the places of TRADE: the partner, the - and + of the cash given (g) and got (t), the row of the chips (its top)
static rc_t tr_who(void)
{
    if (RND) {
        return r_mid(PC(14), 44, 10, PC(56));
    }
    return NARROW ? (rc_t){86, 2, VW - 90, 36} : (rc_t){130, 2, 180, 38};
}
static rc_t tr_cash(int k)
{
    static const int16_t X[4] = {336, 382, 478, 524};
    if (RND) {
        int cx = k < 2 ? VW / 2 - PC(22) : VW / 2 + PC(22);
        return (rc_t){cx - 52 + (k & 1) * 56, PC(31), 48, 46};
    }
    if (!NARROW) {
        return (rc_t){X[k], 26, 40, 38};
    }
    int cx = k < 2 ? VW / 4 : 3 * VW / 4;
    return (rc_t){(int16_t)(cx - 42 + (k & 1) * 44), 60, 40, 34};
}
#define TR_Y (RND ? PC(48) : NARROW ? 104 : TAB5 ? 150 : 72)
// the chip k of the row (the round frame: bigger, three in the middle band)
static int tr_cw(void)
{
    return RND ? PC(26) : TR_CW;
}
static rc_t tr_chip(int k)
{
    const int w = tr_cw(), x0 = RND ? VW / 2 - (3 * w + 8) / 2 : 8;
    return (rc_t){x0 + k * (w + 4) - s_tr_scroll, TR_Y, w, RND ? 56 : 48};
}
static void trade_draw(void)
{
    tfd_rect(0, 0, VW, FH, tfd_rgb(18, 26, 44), 256);
    ui_text_c(&KF_TEXT,
              RND      ? VW / 2
              : NARROW ? 42
                       : 70,
              RND ? PC(11) : 20, T("TRADE", "החלפה"), tfd_rgb(255, 255, 255));
    // the partner: the other players (tap = the next one)
    if (s_tr.to < MAXP) {
        rc_t who = tr_who();
        char b[48];
        snprintf(b, sizeof b, s_lang ? "עם %s" : "WITH %s", pname(s_tr.to));
        button(who, b, C3(SEAT_RGB[s_tr.to]), true);
    }
    // the cash both ways
    char b[64];
    snprintf(b, sizeof b, s_lang ? "אתם נותנים %ld" : "YOU GIVE %ld", (long)s_tr.cash_give);
    ui_text_c(&KF_LABEL,
              RND      ? VW / 2 - PC(22)
              : NARROW ? VW / 4
                       : 380,
              RND      ? PC(29)
              : NARROW ? 54
                       : 17,
              b, tfd_rgb(255, 170, 150));
    snprintf(b, sizeof b, s_lang ? "אתם מקבלים %ld" : "YOU GET %ld", (long)s_tr.cash_get);
    ui_text_c(&KF_LABEL,
              RND      ? VW / 2 + PC(22)
              : NARROW ? 3 * VW / 4
                       : 522,
              RND      ? PC(29)
              : NARROW ? 54
                       : 17,
              b, tfd_rgb(160, 230, 170));
    rc_t gm = tr_cash(0), gp = tr_cash(1), tm = tr_cash(2), tp = tr_cash(3);
    button(gm, "-", tfd_rgb(170, 70, 60), s_tr.cash_give > 0);
    button(gp, "+", tfd_rgb(170, 70, 60), s_tr.cash_give + 50 <= g_v.P[s_my].cash);
    button(tm, "-", tfd_rgb(50, 140, 80), s_tr.cash_get > 0);
    button(tp, "+", tfd_rgb(50, 140, 80), s_tr.to < MAXP && s_tr.cash_get + 50 <= g_v.P[s_tr.to].cash);
    int list[2 * NPROP], side[2 * NPROP];
    int n = s_tr.to < MAXP ? trade_chips(list, side) : 0;
    for (int k = 0; k < n; k++) {
        rc_t cr = tr_chip(k);
        const int pr = list[k], x = cr.x, y = cr.y, cw = cr.w, ty = RND ? 31 : 27, ly = RND ? 48 : 42;
        if (x > VW || x + cw < 0) {
            continue;
        }
        bool sel = side[k] ? (s_tr.get >> pr) & 1 : (s_tr.give >> pr) & 1;
        const tile_t *Tt = PT(pr);
        tfd_rrect(x, y, cw, cr.h, 5,
                  sel ? (side[k] ? tfd_rgb(60, 160, 90) : tfd_rgb(190, 80, 60)) : tfd_rgb(240, 235, 225), 256);
        tfd_rrect(x + 4, y + 4, cw - 8, 8, 2,
                  tfd_rgb(GROUP_RGB[Tt->group][0], GROUP_RGB[Tt->group][1], GROUP_RGB[Tt->group][2]), 256);
        text_fit(x + cw / 2, y + ty, cw - 8, Tt->name[s_lang], sel ? tfd_rgb(255, 255, 255) : tfd_rgb(31, 42, 68));
        ui_text_c(&KF_LABEL, x + cw / 2, y + ly, side[k] ? T("GET", "מקבלים") : T("GIVE", "נותנים"),
                  sel ? tfd_rgb(255, 255, 255) : tfd_rgb(120, 125, 140));
    }
    if (RND) {
        scroll_dots(n, (s_tr_scroll + tr_cw() / 2) / (tr_cw() + 4), 3, TR_Y + 74);
    }
    bool ok = s_tr.to < MAXP && (s_tr.give || s_tr.get || s_tr.cash_give || s_tr.cash_get) && trade_ok(&g_v, &s_tr);
    rc_t send = rc_done(), cancel = rc_cancel();
    button(send, T("SEND", "שליחה"), col_go(), ok);
    button(cancel, T("CANCEL", "ביטול"), col_dim(), true);
}
static void trade_input(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        s_drag_x0 = ev->x;
        s_scroll0 = s_tr_scroll;
        return;
    }
    if (ev->kind == IN_MOVE && s_drag_x0 >= 0 && ev->y > TR_Y - 4 && ev->y < TR_Y + tr_chip(0).h + 4) {
        int list[2 * NPROP], side[2 * NPROP];
        int n = s_tr.to < MAXP ? trade_chips(list, side) : 0;
        int mx = n * (tr_cw() + 4) - (RND ? 3 * tr_cw() + 8 : VW - 16);
        s_tr_scroll = s_scroll0 - (ev->x - s_drag_x0);
        if (s_tr_scroll > mx) {
            s_tr_scroll = mx;
        }
        if (s_tr_scroll < 0) {
            s_tr_scroll = 0;
        }
        return;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    int x = ev->x, y = ev->y;
    rc_t send = rc_done(), cancel = rc_cancel(), who = tr_who();
    rc_t gm = tr_cash(0), gp = tr_cash(1), tm = tr_cash(2), tp = tr_cash(3);
    if (in_rc(cancel, x, y)) {
        s_scr = SCR_PLAY;
        fx(FX_CLICK);
        return;
    }
    if (in_rc(who, x, y)) {
        for (int k = 1; k < g_v.n; k++) {
            int p = (s_tr.to + k) % g_v.n;
            if (p != s_my && !g_v.P[p].out) {
                s_tr.to = (uint8_t)p;
                break;
            }
        }
        s_tr.get = 0;
        s_tr.cash_get = 0;
        s_tr_scroll = 0;
        fx(FX_CLICK);
        return;
    }
    if (in_rc(gm, x, y) && s_tr.cash_give > 0) {
        s_tr.cash_give -= 50;
        if (s_tr.cash_give < 0) {
            s_tr.cash_give = 0;
        }
        return;
    }
    if (in_rc(gp, x, y) && s_tr.cash_give + 50 <= g_v.P[s_my].cash) {
        s_tr.cash_give += 50;
        return;
    }
    if (in_rc(tm, x, y) && s_tr.cash_get > 0) {
        s_tr.cash_get -= 50;
        if (s_tr.cash_get < 0) {
            s_tr.cash_get = 0;
        }
        return;
    }
    if (in_rc(tp, x, y) && s_tr.to < MAXP && s_tr.cash_get + 50 <= g_v.P[s_tr.to].cash) {
        s_tr.cash_get += 50;
        return;
    }
    if (in_rc(send, x, y)) {
        if (!(s_tr.to < MAXP && trade_ok(&g_v, &s_tr))) {
            fx(FX_BAD);
            return;
        }
        bin_t in;
        memset(&in, 0, sizeof in);
        in.kind = IN_TRADE;
        in.tr = s_tr;
        my_intent_full(&in);
        s_scr = SCR_PLAY;
        return;
    }
    int list[2 * NPROP], side[2 * NPROP];
    int n = s_tr.to < MAXP ? trade_chips(list, side) : 0;
    for (int k = 0; k < n; k++) {
        rc_t c = tr_chip(k);
        if (!in_rc(c, x, y)) {
            continue;
        }
        if (side[k]) {
            s_tr.get ^= 1u << list[k];
        } else {
            s_tr.give ^= 1u << list[k];
        }
        fx(FX_CLICK);
        return;
    }
}

// ---------------------------------------------------------------- MAP: all 40 tiles (4 rows of 10), a tap = the details
// the board from the top: a ring of the 40 tiles round the screen (as a real board, the way of the pawns clockwise):
// START at the bottom right, 16 tiles along the bottom (right to left), 4 up the left side, 16 along the top, 4 down
// the right side; in the middle the name, the players and their cash, LIST and BACK
#define MB_W  40
#define MB_H  (TAB5 ? 64 : 34)
#define MB_SH (TAB5 ? 58 : 26)
#define MB_DY (TAB5 ? 15 : 0)  // (the Tab5: the text in the middle of the taller tiles)
#define MB_MY (TAB5 ? 80 : 0)  // (the Tab5: the middle of the ring is lower)
static void mb_rect(int t, int *x, int *y, int *w, int *h, int *side)
{
    *w = MB_W;
    *h = MB_H;
    if (t < 16) {
        *x = VW - MB_W - t * MB_W;
        *y = VH - MB_H;
        *side = 0;
    } else if (t < 20) {
        *x = 0;
        *y = VH - MB_H - (t - 15) * MB_SH;
        *h = MB_SH;
        *side = 1;
    } else if (t < 36) {
        *x = (t - 20) * MB_W;
        *y = 0;
        *side = 2;
    } else {
        *x = VW - MB_W;
        *y = MB_H + (t - 36) * MB_SH;
        *h = MB_SH;
        *side = 3;
    }
}
static rc_t R_MB_LIST = {404, 101 + MB_MY, 92, 36}, R_MB_BACK = {502, 101 + MB_MY, 92, 36};
static void map_board_draw(void)
{
    tfd_rect(0, 0, VW, VH, tfd_rgb(196, 222, 206), 256);
    for (int t = 0; t < NT; t++) {
        int x, y, w, h, side;
        mb_rect(t, &x, &y, &w, &h, &side);
        const tile_t *Tt = &TILES[t];
        int pr = PROP_OF[t];
        tfd_rect(x, y, w, h, t == s_map_sel ? tfd_rgb(255, 250, 225) : tfd_rgb(240, 233, 214), 256);
        tfd_rect(x, y, w, 1, tfd_rgb(60, 70, 90), 256);
        tfd_rect(x, y, 1, h, tfd_rgb(60, 70, 90), 256);
        // the band of the group on the inner side of the tile
        if (Tt->group < G_NONE) {
            uint16_t gc = tfd_rgb(GROUP_RGB[Tt->group][0], GROUP_RGB[Tt->group][1], GROUP_RGB[Tt->group][2]);
            if (side == 0) {
                tfd_rect(x + 1, y + 1, w - 1, 6, gc, 256);
            } else if (side == 2) {
                tfd_rect(x + 1, y + h - 6, w - 1, 6, gc, 256);
            } else if (side == 1) {
                tfd_rect(x + w - 6, y + 1, 6, h - 1, gc, 256);
            } else {
                tfd_rect(x + 1, y + 1, 6, h - 1, gc, 256);
            }
        }
        char b[40];
        snprintf(b, sizeof b, "%s", Tt->board[s_lang]);
        char *bar = strchr(b, '|');
        if (bar) {
            *bar = 0;
        }
        int ty = (side == 0 ? y + 20 : side == 2 ? y + 17 : y + 15) + MB_DY;
        text_fit(x + w / 2 + (side == 1 ? -3 : side == 3 ? 3 : 0), ty, w - 9, b, tfd_rgb(31, 42, 68));
        // the holder of the street, the floors (the outer side), the pawns
        int oy = side == 0 ? y + h - 6 : side == 2 ? y + 4 : y + h - 5;
        if (pr >= 0 && g_v.R[pr].owner < MAXP) {
            tfd_disc(x + 6, oy, 3, C3(SEAT_RGB[g_v.R[pr].owner]), 256);
            for (int j = 0; j < g_v.R[pr].level && j < 5; j++) {
                tfd_rect(x + 11 + j * 5, oy - 2, 4, 4, j == 4 ? tfd_rgb(200, 60, 60) : tfd_rgb(70, 160, 90), 256);
            }
            if (g_v.R[pr].mort) {
                tfd_line(x + 2, y + 2, x + w - 2, y + h - 2, 1.5f, tfd_rgb(160, 60, 50), 200);
            }
        }
        for (int p = 0, k = 0; p < g_v.n; p++) {
            if (!g_v.P[p].out && g_v.P[p].pos == t) {
                tfd_disc(x + w - 6 - (k % 3) * 7, y + h / 2 + (k / 3) * 7 - 2, 3.5f, C3(SEAT_RGB[p]), 256);
                k++;
            }
        }
    }
    // the middle: the name, the players with their cash
    ui_text_c(&KF_TITLE, 222, 76 + MB_MY, s_lang ? "שדרות" : "BOULEVARD", tfd_rgb(30, 70, 150));
    int n = g_v.n, sp = n > 0 ? (int)fminf(80, 520.0f / n) : 80;
    for (int p = 0; p < n; p++) {
        int cx = 60 + sp / 2 + p * sp;
        if (cx > 380) {
            cx = 380;
        }
        float fy = 112 + MB_MY;
        face(p, (float)cx, fy, 9);
        if (!g_v.P[p].out) {
            money_at(&MF_TINY, cx, 133 + MB_MY, g_v.P[p].cash, tfd_rgb(31, 42, 68), tfd_rgb(196, 222, 206));
        }
        if (p == g_v.cur) {
            tfd_ring((float)cx, fy, 12, 2, tfd_rgb(255, 200, 60), 256);
        }
    }
}
// THE ROUND FRAME: the board as a ring of the 40 tiles along the edge of the circle (the way of the pawns clockwise,
// START at the bottom right); the band of the group on the inner side, the holder and the floors at the outer side,
// the pawns in the middle of the tile; a mark for the tiles with no street. In the middle: the name, the players with
// their cash, LIST and BACK. A tap on a tile: its details.
#define RG_A0 0.7853982f   // the angle of the middle of START (the bottom right)
#define RG_DA 0.15707963f  // the angle of one tile (2 pi / 40)
static float rg_ro(void)
{
    return gfx_w / 2.0f - 3;
}
static float rg_ri(void)
{
    return rg_ro() - PC(14);
}
// the point at the radius r and the angle a (the middle of the frame is the middle of the ring)
static void rg_pt(float r, float a, float *x, float *y)
{
    *x = gfx_w / 2.0f + r * fcos(a);
    *y = gfx_h / 2.0f + r * fsin(a);
}
// a part of the ring: from the radius r0 to r1, the angles a0 .. a1 (a polygon of 3 points on each arc)
static void rg_part(float r0, float r1, float a0, float a1, uint16_t c, int al)
{
    float p[12];
    for (int i = 0; i < 3; i++) {
        rg_pt(r1, a0 + (a1 - a0) * i / 2, &p[2 * i], &p[2 * i + 1]);
        rg_pt(r0, a1 - (a1 - a0) * i / 2, &p[6 + 2 * i], &p[7 + 2 * i]);
    }
    tfd_poly(p, 6, c, al);
}
// the tile under (x, y), -1 = none
static int rg_hit(int x, int y)
{
    float dx = x - gfx_w / 2.0f, dy = y - gfx_h / 2.0f, d = sqrtf(dx * dx + dy * dy);
    if (d < rg_ri() - 6 || d > rg_ro() + 6) {
        return -1;
    }
    float a = atan2f(dy, dx) - RG_A0 + RG_DA / 2;
    while (a < 0) {
        a += 6.2831853f;
    }
    int t = (int)(a / RG_DA);
    return t >= 0 && t < NT ? t : -1;
}
// the mark of a tile with no street (a short sign in the middle of the tile)
static const char *rg_mark(int kind)
{
    switch (kind) {
    case TK_START:
        return ">>";
    case TK_SURP:
        return "?";
    case TK_CITY:
        return "!";
    case TK_TAX:
        return "%";
    case TK_JAM:
        return "#";
    case TK_BEACH:
        return "~";
    case TK_WORKS:
        return "X";
    default:
        return NULL;
    }
}
// the holder of a street (the outer side), its floors, a mortgage (a cross line)
static void rg_holder(int pr, float am, float a0, float a1)
{
    const float ro = rg_ro(), ri = rg_ri();
    float x, y;
    rg_pt(ro - 8, am, &x, &y);
    tfd_disc(x, y, 4.5f, C3(SEAT_RGB[g_v.R[pr].owner]), 256);
    for (int j = 0; j < g_v.R[pr].level && j < 5; j++) {
        rg_pt(ro - 19, am + (j - 2) * RG_DA * 0.17f, &x, &y);
        tfd_rect((int)x - 2, (int)y - 2, 5, 5, j == 4 ? tfd_rgb(200, 60, 60) : tfd_rgb(70, 160, 90), 256);
    }
    if (g_v.R[pr].mort) {
        float x1, y1;
        rg_pt(ro - 2, a0, &x, &y);
        rg_pt(ri + 2, a1, &x1, &y1);
        tfd_line(x, y, x1, y1, 2, tfd_rgb(160, 60, 50), 200);
    }
}
static void map_ring_draw(void)
{
    tfd_rect(0, 0, VW, FH, tfd_rgb(196, 222, 206), 256);
    const float ro = rg_ro(), ri = rg_ri(), gb = PC(2), gap = 0.006f;
    for (int t = 0; t < NT; t++) {
        const tile_t *Tt = &TILES[t];
        const int pr = PROP_OF[t];
        const float am = RG_A0 + t * RG_DA, a0 = am - RG_DA / 2 + gap, a1 = am + RG_DA / 2 - gap;
        uint16_t bg = t == s_map_sel ? tfd_rgb(255, 250, 225)
                      : t % 10 == 0  ? tfd_rgb(250, 236, 196)
                                     : tfd_rgb(240, 233, 214);
        rg_part(ri, ro, a0, a1, bg, 256);
        if (Tt->group < G_NONE) {
            rg_part(ri, ri + gb, a0, a1,
                    tfd_rgb(GROUP_RGB[Tt->group][0], GROUP_RGB[Tt->group][1], GROUP_RGB[Tt->group][2]), 256);
        }
        const char *mk = rg_mark(Tt->kind);
        float x, y;
        if (mk) {
            rg_pt((ri + ro) / 2, am, &x, &y);
            ui_text_c(&KF_LABEL, (int)x, (int)y + 5, mk, tfd_rgb(120, 90, 60));
        }
        if (pr >= 0 && g_v.R[pr].owner < MAXP) {
            rg_holder(pr, am, a0, a1);
        }
        for (int p = 0, k = 0; p < g_v.n; p++) {
            if (!g_v.P[p].out && g_v.P[p].pos == t) {
                rg_pt(ri + gb + 13 + (k / 2) * 10, am + ((k & 1) ? 0.25f : -0.25f) * RG_DA, &x, &y);
                tfd_disc(x, y, 5, tfd_rgb(31, 42, 68), 256);
                tfd_disc(x, y, 4, C3(SEAT_RGB[p]), 256);
                k++;
            }
        }
        if (t == s_map_sel) {
            rg_part(ro - 3, ro, a0, a1, tfd_rgb(255, 180, 40), 256);
        }
    }
    // the middle: the name, the players with their cash (the one of the turn in a ring), a hint
    const int c = VW / 2;
    ui_text_c(&KF_TITLE, c, PC(35), s_lang ? "שדרות" : "BOULEVARD", tfd_rgb(30, 70, 150));
    int n = g_v.n, sp = n > 0 ? PC(54) / n : PC(13);
    if (sp > PC(13)) {
        sp = PC(13);
    }
    for (int p = 0; p < n; p++) {
        int cx = c - (n - 1) * sp / 2 + p * sp;
        float fy = PC(44);
        face(p, (float)cx, fy, 12);
        if (!g_v.P[p].out) {
            money_at(&MF_TINY, cx, PC(44) + 26, g_v.P[p].cash, tfd_rgb(31, 42, 68), tfd_rgb(196, 222, 206));
        }
        if (p == g_v.cur) {
            tfd_ring((float)cx, fy, 15, 2.5f, tfd_rgb(255, 200, 60), 256);
        }
    }
    text_fit(c, PC(58), PC(56), T("TAP A TILE", "הקישו על משבצת"), tfd_rgb(70, 90, 100));
}
// the box of tile t in the list of the round frame: rows in the width of the circle (3 to 6 tiles in a row)
static void rl_box(int t, int *x, int *y, int *w, int *h)
{
    const int rh = PC(8), cw = PC(17);
    for (int row = 0, k = 0; row < 12; row++) {
        int yy = PC(9) + row * rh, a = r_half(yy), b = r_half(yy + rh - 3), n = (2 * (a < b ? a : b) - 12) / cw;
        n = n > 6 ? 6 : n < 1 ? 1 : n;
        if (t < k + n) {
            int col = t - k;
            *x = VW / 2 - n * cw / 2 + (s_lang ? n - 1 - col : col) * cw;
            *y = yy;
            *w = cw - 3;
            *h = rh - 3;
            return;
        }
        k += n;
    }
    *x = *y = *w = *h = 0;
}
// the BOARD and BACK buttons under the list
static rc_t map_list_btn(bool back)
{
    if (RND) {
        return (rc_t){back ? VW / 2 + 5 : VW / 2 - PC(23) - 5, PC(82), PC(23), 44};
    }
    return back ? (rc_t){VW - 96, FH - 40, 92, 36} : (rc_t){VW - 196, FH - 40, 92, 36};
}
// the box of tile t in the list: 10 x 4 on the 3.49, 5 x 8 on a narrow frame (the columns from the right in Hebrew)
#define ML_COLS (NARROW ? 5 : 10)
#define ML_RH   (NARROW ? 24 : TAB5 ? 70 : 34)
static void ml_box(int t, int *x, int *y, int *w, int *h)
{
    if (RND) {
        rl_box(t, x, y, w, h);
        return;
    }
    int col = t % ML_COLS, row = t / ML_COLS;
    *x = 4 + (s_lang ? ML_COLS - 1 - col : col) * 63;
    *y = 4 + row * ML_RH;
    *w = 60;
    *h = ML_RH - 3;
}
static void map_draw(void)
{
    if (NARROW && !RND) {
        s_map_board = 0;  // (the ring of 40 tiles is too small on 320 px: the list)
    }
    if (s_map_board) {
        if (RND) {
            map_ring_draw();
        } else {
            map_board_draw();
        }
        if (s_map_sel >= 0) {
            goto details;
        }
        button(R_MB_LIST, T("LIST", "רשימה"), col_dim(), true);
        button(R_MB_BACK, T("BACK", "חזרה"), col_dim(), true);
        return;
    }
    tfd_rect(0, 0, VW, FH, tfd_rgb(18, 26, 44), 256);
    for (int t = 0; t < NT; t++) {
        int x, y, w, h;
        ml_box(t, &x, &y, &w, &h);
        const tile_t *Tt = &TILES[t];
        int pr = PROP_OF[t];
        tfd_rrect(x, y, w, h, 4, t == s_map_sel ? tfd_rgb(255, 250, 230) : tfd_rgb(235, 228, 210), 256);
        if (Tt->group < G_NONE) {
            tfd_rrect(x + 2, y + 2, w - 4, 6, 2,
                      tfd_rgb(GROUP_RGB[Tt->group][0], GROUP_RGB[Tt->group][1], GROUP_RGB[Tt->group][2]), 256);
        }
        char b[40];
        snprintf(b, sizeof b, "%s", Tt->board[s_lang]);
        char *bar = strchr(b, '|');
        if (bar) {
            *bar = 0;
        }
        const bool sm = NARROW && !RND;  // (the small tiles of a narrow frame)
        text_fit(x + w / 2, y + (sm ? 16 : TAB5 ? 38 : 21), w - 4, b, tfd_rgb(31, 42, 68));
        if (pr >= 0 && g_v.R[pr].owner < MAXP) {
            tfd_disc(x + 7, y + h - (sm ? 4 : 6), sm ? 3 : 4, C3(SEAT_RGB[g_v.R[pr].owner]), 256);
            for (int j = 0; j < g_v.R[pr].level && j < 5; j++) {
                tfd_rect(x + 14 + j * 7, y + h - 8, 5, 5, j == 4 ? tfd_rgb(200, 60, 60) : tfd_rgb(70, 160, 90), 256);
            }
            if (g_v.R[pr].mort) {
                tfd_line(x + 2, y + 2, x + w - 2, y + h - 2, 1.5f, tfd_rgb(160, 60, 50), 200);
            }
        }
        for (int p = 0; p < g_v.n; p++) {
            if (!g_v.P[p].out && g_v.P[p].pos == t) {
                tfd_disc(x + w - 6 - p * 6, y + h - 6, 3, C3(SEAT_RGB[p]), 256);
            }
        }
    }
    {
        rc_t back = map_list_btn(true), brd = map_list_btn(false);
        if (s_map_sel < 0) {
            if (!NARROW || RND) {
                button(brd, T("BOARD", "לוח"), col_dim(), true);
            }
            button(back, T("BACK", "חזרה"), col_dim(), true);
            return;
        }
    }
details:
    if (s_map_sel >= 0) {
        // the details of a tile (a tap anywhere closes them)
        const tile_t *Tt = &TILES[s_map_sel];
        int pr = PROP_OF[s_map_sel];
        const int dx = RND      ? VW / 2 - PC(33)
                       : NARROW ? 6
                                : 150,
                  dw = RND      ? PC(66)
                       : NARROW ? VW - 12
                                : 340,
                  cx = dx + dw / 2,
                  oy = RND      ? FH / 2 - 78
                       : NARROW ? 40
                       : TAB5   ? 94
                                : 0;
        tfd_rrect(dx, 30 + oy, dw, 96, 8, tfd_rgb(251, 247, 238), 250);
        ui_text_c(&KF_TEXT, cx, 54 + oy, Tt->name[s_lang], tfd_rgb(31, 42, 68));
        if (pr >= 0) {
            char b[96];
            const bpr_t *R = &g_v.R[pr];
            if (R->owner < MAXP) {
                snprintf(b, sizeof b, s_lang ? "בעלים: %s" : "OWNER: %s", pname(R->owner));
            } else {
                snprintf(b, sizeof b, "%s", T("FOR SALE", "למכירה"));
            }
            ui_text_c(&KF_LABEL, cx, 74 + oy, b, tfd_rgb(70, 80, 100));
            money_at(&MF_N16, cx, 98 + oy, R->owner < MAXP ? rent_of(&g_v, pr, 7, 1) : Tt->price, tfd_rgb(31, 42, 68),
                     tfd_rgb(251, 247, 238));
            ui_text_c(&KF_LABEL, cx, 116 + oy,
                      R->owner < MAXP ? T("RENT NOW (DICE 7 FOR SERVICES)", "השכירות עכשיו, בקוביות 7 לשירותים")
                                      : T("PRICE", "מחיר"),
                      tfd_rgb(120, 125, 140));
        }
    }
}
static void map_input(const input_event_t *ev)
{
    if (ev->kind != IN_TAP) {
        return;
    }
    if (s_map_sel >= 0) {
        s_map_sel = -1;
        fx(FX_CLICK);
        return;
    }  // (the details close)
    if (s_map_board) {
        if (in_rc(R_MB_BACK, ev->x, ev->y)) {
            s_scr = g_v.phase == PH_OVER ? SCR_END : SCR_PLAY;
            fx(FX_CLICK);
            return;
        }
        if (in_rc(R_MB_LIST, ev->x, ev->y)) {
            s_map_board = 0;
            store_set("bv_mapv", 0);
            fx(FX_CLICK);
            return;
        }
        if (RND) {
            int t = rg_hit(ev->x, ev->y);
            if (t >= 0) {
                s_map_sel = t;
                fx(FX_CLICK);
            }
            return;
        }
        for (int t = 0; t < NT; t++) {
            int x, y, w, h, side;
            mb_rect(t, &x, &y, &w, &h, &side);
            if (ev->x >= x && ev->x < x + w && ev->y >= y && ev->y < y + h) {
                s_map_sel = t;
                fx(FX_CLICK);
                return;
            }
        }
        return;
    }
    rc_t back = map_list_btn(true), brd = map_list_btn(false);
    if (in_rc(back, ev->x, ev->y)) {
        s_scr = g_v.phase == PH_OVER ? SCR_END : SCR_PLAY;
        fx(FX_CLICK);
        return;
    }
    if ((!NARROW || RND) && in_rc(brd, ev->x, ev->y)) {
        s_map_board = 1;
        store_set("bv_mapv", 1);
        fx(FX_CLICK);
        return;
    }
    for (int t = 0; t < NT; t++) {
        int x, y, w, h;
        ml_box(t, &x, &y, &w, &h);
        if (ev->x >= x && ev->x < x + (RND ? w + 3 : 63) && ev->y >= y && ev->y < y + (RND ? h + 3 : ML_RH)) {
            s_map_sel = s_map_sel == t ? -1 : t;
            fx(FX_CLICK);
            return;
        }
    }
}

// ---------------------------------------------------------------- TITLE, SETUP, the room of the friends, END
static rc_t R_T_PLAY = {380, 40, 120, 52}, R_T_FRIENDS = {508, 40, 124, 52}, R_T_CONT = {380, 104, 252, 40};
static bool s_has_save;
static int s_title_msg;  // 1: the host of the friends game left, the game waits for it
static int64_t s_title_msg_t;
static void title_draw(void)
{
    // the board in a slow pan under the skyline (an empty board: no owner, no pawn)
    s_camx = fmodf((float)(s_now % 1000000) * 0.03f, NT * TW);
    s_camz = ZB;
    sky_draw();
    tfd_rect(0, 98 + SKY_Y0, VW, 8, tfd_rgb(255, 240, 215), 90);
    {
        static bgame_t keep;
        keep = g_v;
        for (int i = 0; i < NPROP; i++) {
            g_v.R[i].owner = NOBODY;
            g_v.R[i].level = 0;
            g_v.R[i].mort = 0;
        }
        g_v.n = 0;
        board_draw();
        g_v = keep;
    }
    tfd_rect(0, (int)YN + 9, VW, FH - (int)YN - 9, tfd_rgb(22, 48, 79), 256);
    // the name: the street sign of Tel Aviv (blue, white letters); a narrow frame: over the whole width
    int sx = NARROW ? 6 : 18, sw = NARROW ? VW - 12 : 330, scx = sx + sw / 2, sy = 26;
    if (RND) {
        // (the round frame: the sign in the top cap, the line under it on the sky)
        rc_t r = r_mid(PC(12), 76, 14, PC(72));
        sx = r.x;
        sw = r.w;
        sy = r.y;
    }
    tfd_rrect(sx, sy, sw, 76, 10, tfd_rgb(255, 255, 255), 250);
    tfd_rrect(sx + 6, sy + 6, sw - 12, 64, 7, tfd_rgb(30, 70, 150), 256);
    ui_text_c(&KF_TITLE, scx, sy + 44, s_lang ? "שדרות" : "BOULEVARD", tfd_rgb(255, 255, 255));
    ui_text_c(&KF_LABEL, scx, sy + 64, s_lang ? "BOULEVARD" : "שדרות", tfd_rgb(200, 215, 255));
    const char *tag = T("BUY TEL AVIV, STREET BY STREET", "קונים את תל אביב, רחוב אחרי רחוב");
    if (RND) {
        int tw = ui_text_w(&KF_LABEL, tag) + 24;
        tfd_rrect(scx - tw / 2, sy + 82, tw, 22, 6, tfd_rgb(15, 22, 40), 190);
        ui_text_c(&KF_LABEL, scx, sy + 97, tag, tfd_rgb(220, 230, 255));
    } else {
        tfd_rrect(sx + 12, 2, sw - 24, 21, 6, tfd_rgb(15, 22, 40), 190);
        ui_text_c(&KF_LABEL, scx, 17, tag, tfd_rgb(220, 230, 255));
    }
    button(R_T_PLAY, T("PLAY", "משחק"), col_go(), true);
    button(R_T_FRIENDS, T("2 BOARDS", "2 לוחות"), col_blue(), true);  // (2 to 6 boards; the name of the other games)
    if (s_has_save) {
        button(R_T_CONT, NARROW ? T("CONTINUE", "המשך") : T("CONTINUE THE GAME", "המשך המשחק"), tfd_rgb(200, 140, 40),
               true);
    }
    if (s_title_msg == 1 && s_now - s_title_msg_t < 12000 && RND) {
        rc_t r = r_mid(PC(46), 46, 12, PC(86));
        tfd_rrect(r.x, r.y, r.w, r.h, 8, tfd_rgb(15, 22, 40), 220);
        text_fit(r.x + r.w / 2, r.y + 19, r.w - 12,
                 T("THE HOST LEFT. THE GAME WAITS FOR IT:", "המארח יצא. המשחק מחכה לו:"), tfd_rgb(255, 220, 150));
        text_fit(r.x + r.w / 2, r.y + 38, r.w - 12,
                 T("2 BOARDS > GO BACK, WHEN IT OPENS IT AGAIN", "2 לוחות > לחזור, כשהוא יפתח אותו שוב"),
                 tfd_rgb(220, 225, 240));
    } else if (s_title_msg == 1 && s_now - s_title_msg_t < 12000) {
        tfd_rrect(sx + 12, 112, sw - 24 + (NARROW ? 0 : 24), 44, 8, tfd_rgb(15, 22, 40), 220);
        ui_text_c(&KF_LABEL, NARROW ? scx : 195, 130,
                  T("THE HOST LEFT. THE GAME WAITS FOR IT:", "המארח יצא. המשחק מחכה לו:"), tfd_rgb(255, 220, 150));
        ui_text_c(&KF_LABEL, NARROW ? scx : 195, 148,
                  T("2 BOARDS > GO BACK, WHEN IT OPENS IT AGAIN", "2 לוחות > לחזור, כשהוא יפתח אותו שוב"),
                  tfd_rgb(220, 225, 240));
    }
}
static rc_t R_S_LOOKL = {8, 40, 40, 48}, R_S_LOOKR = {150, 40, 40, 48}, R_S_NM = {206, 40, 44, 40},
            R_S_NP = {312, 40, 44, 40}, R_S_LVL = {206, 84, 150, 40}, R_S_MODE = {364, 40, 150, 40},
            R_S_START = {520, 40, 112, 84}, R_S_BACK = {8, 128, 92, 38};
static const char *mode_name(int m)
{
    return m == MODE_FULL   ? T("FULL GAME", "משחק מלא")
           : m == MODE_TIME ? T("30 MINUTES", "30 דקות")
                            : T("FIRST TO 4000", "הראשון ל-4000");
}
static void look_draw(int look, int cx, int cy, float r)
{
    // (the face of a look with no seat: a seat colour of the look)
    bgame_t keep = g_v;
    g_v.P[0].look = (uint8_t)look;
    face(0, (float)cx, (float)cy, r);
    g_v = keep;
}
static void setup_draw(void)
{
    tfd_rect(0, 0, VW, FH, tfd_rgb(18, 26, 44), 256);
    ui_text_c(&KF_TEXT, VW / 2, RND ? PC(16) : 22, T("A GAME ON THIS BOARD", "משחק על הלוח הזה"),
              tfd_rgb(255, 255, 255));
    const int lx = (R_S_LOOKL.x + R_S_LOOKR.x + R_S_LOOKR.w) / 2;  // (the face between its arrows)
    look_draw(s_my_look, lx, R_S_LOOKL.y + 24, 24);
    ui_text_c(&KF_TEXT, lx, R_S_LOOKL.y + 72, BOT_NAME[s_my_look][s_lang], tfd_rgb(255, 255, 255));
    button(R_S_LOOKL, "<", col_dim(), true);
    button(R_S_LOOKR, ">", col_dim(), true);
    char b[48];
    // the number of the players: - and + at the two sides of the number
    button(R_S_NM, "-", col_dim(), s_setup_n > 2);
    button(R_S_NP, "+", col_dim(), s_setup_n < MAXP);
    snprintf(b, sizeof b, "%d", s_setup_n);
    const int nx = (R_S_NM.x + R_S_NP.x + R_S_NP.w) / 2;  // (the number between - and +)
    ui_text_c(&KF_TEXT, nx, R_S_NM.y + (RND ? 32 : 20), b, tfd_rgb(255, 255, 255));
    ui_text_c(&KF_LABEL, nx, R_S_NM.y + (RND ? 72 : 36), T("PLAYERS", "שחקנים"), tfd_rgb(190, 195, 210));
    button(R_S_LVL, s_setup_lvl ? T("BOTS: GOOD", "בוטים: חכמים") : T("BOTS: EASY", "בוטים: קלים"), col_dim(), true);
    button(R_S_MODE, mode_name(s_setup_mode), col_dim(), true);
    button(R_S_START, T("START", "התחלה"), col_go(), true);
    button(R_S_BACK, T("BACK", "חזרה"), col_dim(), true);
}
static void setup_tap(int x, int y)
{
    if (in_rc(R_S_LOOKL, x, y)) {
        s_my_look = (s_my_look + NLOOK - 1) % NLOOK;
    } else if (in_rc(R_S_LOOKR, x, y)) {
        s_my_look = (s_my_look + 1) % NLOOK;
    } else if (in_rc(R_S_NM, x, y) && s_setup_n > 2) {
        s_setup_n--;
    } else if (in_rc(R_S_NP, x, y) && s_setup_n < MAXP) {
        s_setup_n++;
    } else if (in_rc(R_S_LVL, x, y)) {
        s_setup_lvl ^= 1;
    } else if (in_rc(R_S_MODE, x, y)) {
        s_setup_mode = (s_setup_mode + 1) % 3;
    } else if (in_rc(R_S_START, x, y)) {
        store_set("bv_look", (uint32_t)s_my_look);
        store_set("bv_n", (uint32_t)s_setup_n);
        store_set("bv_lvl", (uint32_t)s_setup_lvl);
        store_set("bv_mode", (uint32_t)s_setup_mode);
        local_start();
        return;
    } else if (in_rc(R_S_BACK, x, y)) {
        s_scr = SCR_TITLE;
    } else {
        return;
    }
    fx(FX_CLICK);
}
// the room of the friends: the tables that this board hears, NEW TABLE; in a table: its seats
static rc_t R_L_NEW = {460, 40, 172, 48}, R_L_BACK = {8, 128, 92, 38}, R_L_RESUME = {460, 96, 172, 44};
static bool s_has_net;  // the host has a saved friends game (net.bin)
// the button of the table k in the list (two tables at most)
static rc_t lobby_row(int k)
{
    if (RND) {
        return r_mid(PC(21) + k * PC(11), PC(9), 14, PC(84));
    }
    return (rc_t){8, 34 + k * 44, NARROW ? VW - 16 : 440, 40};
}
static void lobby_draw(void)
{
    tfd_rect(0, 0, VW, FH, tfd_rgb(18, 26, 44), 256);
    const int lcx = NARROW ? VW / 2 : 230, ly = RND ? PC(31) : 80;
    ui_text_c(&KF_TEXT, lcx, RND ? PC(15) : 22, T("TABLES NEAR YOU", "שולחנות קרובים"), tfd_rgb(255, 255, 255));
    if (!s_net_on) {
        ui_text_c(&KF_LABEL, lcx, ly, T("THE RADIO DID NOT START", "הרדיו לא התחיל"), tfd_rgb(255, 130, 110));
    }
    int k = 0;
    for (int i = 0; i < NTAB; i++) {
        table_t *t = &s_tabs[i];
        if (!t->seen || s_now - t->seen > 3000) {
            continue;
        }
        rc_t r = lobby_row(k);
        int host_look = t->look[0];
        char b[64];
        int seats = 0;
        for (int j = 0; j < MAXP; j++) {
            seats += t->kind[j] != SK_EMPTY;
        }
        if (t->started) {
            snprintf(b, sizeof b, s_lang ? "המשחק של %s: לחזור" : "THE GAME OF %s: GO BACK",
                     BOT_NAME[host_look % NLOOK][s_lang]);
        } else {
            snprintf(b, sizeof b, s_lang ? "השולחן של %s (%d)" : "THE TABLE OF %s (%d)",
                     BOT_NAME[host_look % NLOOK][s_lang], seats);
        }
        button(r, b, t->started ? col_go() : col_blue(), true);
        if (++k >= 2) {
            break;
        }
    }
    if (!k) {
        if (NARROW) {
            text_box(&KF_LABEL, lcx, ly + (RND ? 26 : 0), RND ? PC(76) : VW - 24, RND ? 20 : 16,
                     T("NO TABLE YET: OPEN ONE, OR WAIT FOR A FRIEND", "עוד אין שולחן: פתחו אחד, או חכו לחבר"),
                     tfd_rgb(190, 195, 210), 2);
        } else {
            ui_text_c(&KF_LABEL, 230, 90,
                      T("NO TABLE YET: OPEN ONE, OR WAIT FOR A FRIEND", "עוד אין שולחן: פתחו אחד, או חכו לחבר"),
                      tfd_rgb(190, 195, 210));
        }
    }
    button(R_L_NEW, T("NEW TABLE", "שולחן חדש"), col_go(), s_net_on);
    if (s_has_net) {
        button(R_L_RESUME, T("OUR LAST GAME", "המשחק האחרון שלנו"), tfd_rgb(200, 140, 40), s_net_on);
    }
    button(R_L_BACK, T("BACK", "חזרה"), col_dim(), true);
}
static void lobby_tap(int x, int y)
{
    if (in_rc(R_L_BACK, x, y)) {
        net_stop();
        s_scr = SCR_TITLE;
        fx(FX_CLICK);
        return;
    }
    if (s_has_net && s_net_on && in_rc(R_L_RESUME, x, y)) {
        // the saved friends game: the same table, the same seats; the boards come back from their lobby (until then a
        // bot plays an empty seat: AWAY after 15 s)
        static netsave_t ns;
        if (!load_net(&ns)) {
            s_has_net = false;
            return;
        }
        s_role = ROLE_HOST;
        s_tid = ns.tid;
        s_nseats = ns.nseats;
        s_setup_mode = ns.mode;
        memcpy(s_seat_kind, ns.kind, MAXP);
        memcpy(s_seat_look, ns.look, MAXP);
        memcpy(s_seat_mac, ns.mac, sizeof s_seat_mac);
        g_v = ns.g;
        s_my = 0;
        for (int i = 0; i < MAXP; i++) {
            s_seen[i] = s_now;
        }
        s_ev_have = g_v.evn;
        fx(FX_CLICK);
        game_begin();
        send_table();
        send_state();
        return;
    }
    if (in_rc(R_L_NEW, x, y) && s_net_on) {
        s_role = ROLE_HOST;
        s_tid = (uint16_t)(esp_random() | 1);
        memset(s_seat_kind, 0, sizeof s_seat_kind);
        s_seat_kind[0] = SK_HUMAN;
        s_seat_look[0] = (uint8_t)s_my_look;
        memcpy(s_seat_mac[0], s_my_mac + 3, 3);
        s_nseats = 1;
        s_my = 0;
        s_scr = SCR_ROOM;
        send_table();
        fx(FX_CLICK);
        return;
    }
    int k = 0;
    for (int i = 0; i < NTAB; i++) {
        table_t *t = &s_tabs[i];
        if (!t->seen || s_now - t->seen > 3000) {
            continue;
        }
        rc_t r = lobby_row(k);
        if (in_rc(r, x, y)) {
            s_role = ROLE_CLIENT;
            s_tid = t->tid;
            s_my = -1;
            // (a game in play: the seat of this board at once, from the table that it heard)
            s_nseats = t->n;
            memcpy(s_seat_kind, t->kind, MAXP);
            memcpy(s_seat_look, t->look, MAXP);
            memcpy(s_seat_mac, t->mac, sizeof s_seat_mac);
            for (int j = 0; j < MAXP; j++) {
                if (s_seat_kind[j] == SK_HUMAN && !memcmp(s_seat_mac[j], s_my_mac + 3, 3)) {
                    s_my = j;
                }
            }
            g_v.magic = 0;
            s_last_rx = s_now;
            s_last_table = 0;
            s_scr = SCR_ROOM;
            fx(FX_CLICK);
            return;
        }
        if (++k >= 2) {
            break;
        }
    }
}
static rc_t R_R_BOTP = {8, 128, 100, 38}, R_R_BOTM = {114, 128, 100, 38}, R_R_START = {500, 122, 132, 44},
            R_R_LEAVE = {390, 128, 100, 38};
// the LEAVE button (the round frame: in the middle for a guest, who has no START)
static rc_t room_leave(void)
{
    rc_t r = R_R_LEAVE;
    if (RND && s_role != ROLE_HOST) {
        r.x = VW / 2 - r.w / 2;
    }
    return r;
}
// a seat in the round frame: two rows of 3 in the middle of the circle
static void room_seat_draw_round(int i)
{
    const int w = PC(22), h = PC(19), x = VW / 2 - (3 * w + 12) / 2 + (i % 3) * (w + 6), y = PC(21) + (i / 3) * (h + 6);
    tfd_rrect(x, y, w, h, 8, i < s_nseats && s_seat_kind[i] != SK_EMPTY ? tfd_rgb(34, 52, 86) : tfd_rgb(26, 32, 48),
              256);
    if (i >= s_nseats || s_seat_kind[i] == SK_EMPTY) {
        ui_text_c(&KF_LABEL, x + w / 2, y + h * 58 / 100, T("FREE", "פנוי"), tfd_rgb(110, 120, 140));
        return;
    }
    look_draw(s_seat_look[i], x + w / 2, y + h * 28 / 100, 17);
    text_fit(x + w / 2, y + h * 70 / 100, w - 8, BOT_NAME[s_seat_look[i] % NLOOK][s_lang], tfd_rgb(255, 255, 255));
    ui_text_c(&KF_LABEL, x + w / 2, y + h * 90 / 100,
              s_seat_kind[i] == SK_BOT ? T("BOT", "בוט")
              : i == s_my              ? T("YOU", "אתם")
                                       : T("FRIEND", "חבר/ה"),
              tfd_rgb(170, 180, 200));
}
static void room_draw(void)
{
    tfd_rect(0, 0, VW, FH, tfd_rgb(18, 26, 44), 256);
    const char *head = s_role == ROLE_HOST ? T("YOUR TABLE: FRIENDS CAN JOIN", "השולחן שלכם: חברים יכולים להצטרף")
                                           : T("WAIT FOR THE START", "חכו להתחלה");
    if (RND) {
        text_box(&KF_LABEL, VW / 2, PC(11), PC(60), 18, head, tfd_rgb(255, 255, 255), 2);
    } else {
        ui_text_c(NARROW ? &KF_LABEL : &KF_TEXT, VW / 2, 20, head, tfd_rgb(255, 255, 255));
    }
    for (int i = 0; i < MAXP; i++) {
        if (RND) {
            room_seat_draw_round(i);
            continue;
        }
        // (a narrow frame: 2 rows of 3 seats)
        int x = NARROW ? 8 + (i % 3) * 104 : 8 + i * 104, y = NARROW ? 30 + (i / 3) * 76 : TAB5 ? 96 : 34;
        tfd_rrect(x, y, 100, NARROW ? 72 : 84, 8,
                  i < s_nseats && s_seat_kind[i] != SK_EMPTY ? tfd_rgb(34, 52, 86) : tfd_rgb(26, 32, 48), 256);
        if (i >= s_nseats || s_seat_kind[i] == SK_EMPTY) {
            ui_text_c(&KF_LABEL, x + 50, y + 46, T("FREE", "פנוי"), tfd_rgb(110, 120, 140));
            continue;
        }
        look_draw(s_seat_look[i], x + 50, y + (NARROW ? 24 : 30), NARROW ? 15 : 18);
        ui_text_c(&KF_LABEL, x + 50, y + (NARROW ? 54 : 64), BOT_NAME[s_seat_look[i] % NLOOK][s_lang],
                  tfd_rgb(255, 255, 255));
        ui_text_c(&KF_LABEL, x + 50, y + (NARROW ? 67 : 78),
                  s_seat_kind[i] == SK_BOT ? T("BOT", "בוט")
                  : i == s_my              ? T("YOU", "אתם")
                                           : T("FRIEND", "חבר/ה"),
                  tfd_rgb(170, 180, 200));
    }
    if (s_role == ROLE_HOST) {
        int taken = 0;
        for (int i = 0; i < s_nseats; i++) {
            taken += s_seat_kind[i] != SK_EMPTY;
        }
        button(R_R_BOTP, T("+ BOT", "+ בוט"), col_dim(), taken < MAXP);
        button(R_R_BOTM, T("- BOT", "- בוט"), col_dim(), true);
        button(R_R_START, T("START", "התחלה"), col_go(), taken >= 2);
    }
    button(room_leave(), T("LEAVE", "יציאה"), col_red(), true);
}
static void room_tap(int x, int y)
{
    if (in_rc(room_leave(), x, y)) {
        if (s_role == ROLE_CLIENT) {
            uint8_t b[2] = {(uint8_t)s_tid, (uint8_t)(s_tid >> 8)};
            link_send(LINK_APP, K_LEAVE, b, 2);
        } else {
            uint8_t b[2] = {(uint8_t)s_tid, (uint8_t)(s_tid >> 8)};
            link_send(LINK_APP, K_BYE, b, 2);
        }
        s_role = ROLE_LOCAL;
        s_tid = 0;
        s_scr = SCR_LOBBY;
        fx(FX_CLICK);
        return;
    }
    if (s_role != ROLE_HOST) {
        return;
    }
    if (in_rc(R_R_BOTP, x, y)) {
        for (int i = 0; i < MAXP; i++) {
            if (s_seat_kind[i] == SK_EMPTY || i >= s_nseats) {
                s_seat_kind[i] = SK_BOT;
                s_seat_look[i] = (uint8_t)((s_my_look + i * 3 + 1) % NLOOK);
                if (i >= s_nseats) {
                    s_nseats = i + 1;
                }
                break;
            }
        }
        send_table();
        fx(FX_CLICK);
    } else if (in_rc(R_R_BOTM, x, y)) {
        for (int i = MAXP - 1; i >= 0; i--) {
            if (i < s_nseats && s_seat_kind[i] == SK_BOT) {
                s_seat_kind[i] = SK_EMPTY;
                break;
            }
        }
        while (s_nseats > 1 && s_seat_kind[s_nseats - 1] == SK_EMPTY) {
            s_nseats--;
        }
        send_table();
        fx(FX_CLICK);
    } else if (in_rc(R_R_START, x, y)) {
        int taken = 0;
        for (int i = 0; i < s_nseats; i++) {
            taken += s_seat_kind[i] != SK_EMPTY;
        }
        if (taken >= 2) {
            host_start();
        }
    }
}
static void end_draw(void)
{
    play_draw();
    tfd_rect(0, 0, VW, FH, tfd_rgb(10, 14, 28), 215);
    int w = g_v.winner < MAXP ? g_v.winner : 0;
    char b[64];
    snprintf(b, sizeof b, s_lang ? "%s מנצח/ת!" : "%s WINS!", pname(w));
    const int ey = TAB5 ? 80 : RPLAY ? FH * 22 / 100 : 0;  // (the Tab5 and the round frame: all lower, in the middle)
    ui_text_c(&KF_TITLE, VW / 2, 48 + ey / 2, b, tfd_rgb(255, 214, 90));
    // the ranks
    int k = 0;
    for (int r = 1; r <= g_v.n; r++) {
        for (int p = 0; p < g_v.n; p++) {
            if (g_v.P[p].rank != r) {
                continue;
            }
            // (the left part only: the TITLE button is at the right; the first place at the right in Hebrew)
            const int sp = RPLAY ? VW * 72 / 100 / g_v.n : NARROW ? (VW - 20) / g_v.n : g_v.n > 4 ? 470 / g_v.n : 110;
            const int kk = s_lang ? g_v.n - 1 - k : k;
            const int x = (NARROW ? VW / 2 : 245) + kk * sp - (g_v.n - 1) * sp / 2 - 20;
            face(p, x + 20, 90 + ey, 14);
            // the place: a number in a gold disc (no number in the text: the order of a Hebrew line)
            tfd_disc(x + 38, 78 + ey, 8, r == 1 ? tfd_rgb(255, 200, 60) : tfd_rgb(150, 160, 180), 256);
            snprintf(b, sizeof b, "%d", r);
            mf_text_c(&MF_TINY, x + 38, 82 + ey, b, tfd_rgb(20, 24, 40), 255, 0);
            ui_text_c(&KF_LABEL, x + 20, 120 + ey, pname(p), tfd_rgb(255, 255, 255));
            if (!g_v.P[p].out) {
                money_at(&MF_TINY, x + 20, 134 + ey, g_worth(&g_v, p), tfd_rgb(255, 226, 122), tfd_rgb(10, 14, 28));
            }
            k++;
        }
    }
    rc_t again = rc_end_title();
    button(again, T("TITLE", "למסך הראשי"), col_go(), true);
}

// ---------------------------------------------------------------- the panel
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const ONOFF_V[] = {"OFF", "כבוי", "ON", "פועל"};
static const char *const SPEED_V[] = {"NORMAL", "רגיל", "FAST", "מהיר"};
enum {
    PI_LANG,
    PI_MUSIC,
    PI_SOUND,
    PI_SPEED,
    PI_RESIGN,
    PI_TITLE
};
static const pitem_t ITEMS[] = {
        {PI_PICK, PI_LANG, 2, 1, {"LANGUAGE", "שפה"}, LANG_V, "bv_lang"},
        {PI_PICK, PI_MUSIC, 2, 1, {"MUSIC", "מוזיקה"}, ONOFF_V, "bv_music"},
        {PI_PICK, PI_SOUND, 2, 1, {"SOUND", "צלילים"}, ONOFF_V, "bv_snd"},
        {PI_PICK, PI_SPEED, 2, 0, {"SPEED", "מהירות"}, SPEED_V, "bv_speed"},
        {PI_ACT2, PI_RESIGN, 0, 0, {"RESIGN", "פרישה"}, NULL, NULL},
        {PI_ACT, PI_TITLE, 0, 0, {"THE TITLE", "המסך הראשי"}, NULL, NULL},
};
static bool item_shown(int id)
{
    if (id == PI_RESIGN) {
        return s_scr == SCR_PLAY && g_v.phase != PH_OVER && s_my >= 0 && !g_v.P[s_my].out;
    }
    if (id == PI_TITLE) {
        return s_scr != SCR_TITLE;
    }
    return true;
}
static void go_title(void);
static void item_set(int id, int v)
{
    if (id == PI_LANG) {
        s_lang = v;
    } else if (id == PI_MUSIC) {
        s_music = v;
        if (!v) {
            midi_bg_stop(300);
        } else if (s_scr == SCR_PLAY) {
            music_board();
        } else {
            midi_bg_play_track(BVM_TITLE, 0, true);
        }
    } else if (id == PI_SOUND) {
        s_snd = v != 0;
    } else if (id == PI_SPEED) {
        s_speed = v;
    } else if (id == PI_RESIGN) {
        my_intent(IN_RESIGN, 0);
    } else if (id == PI_TITLE) {
        go_title();
    }
}
static const pitems_t PANEL = {ITEMS, 6, item_shown, NULL, item_set, NULL, NULL};

// ---------------------------------------------------------------- the app
static void go_title(void)
{
    if (s_role != ROLE_LOCAL) {
        net_stop();
    } else {
        save_game();
    }
    s_role = ROLE_LOCAL;
    s_scr = SCR_TITLE;
    bgame_t t;
    s_has_save = load_game(&t);
    static netsave_t ns;
    s_has_net = load_net(&ns);
    if (s_music) {
        midi_bg_play_track(BVM_TITLE, 0, true);
    }
}
// the places of the buttons for this frame (the 3.49: the places at their definitions)
static void layout_rest(void);
static void layout_round(void);
static void layout(void)
{
    s_vw = gfx_w;
    if (gfx_round()) {
        // (the round frame: the play buttons on the bottom arc; the other screens: layout_round())
        layout_round();
        const int by = FH * 72 / 100, bw = VW * 20 / 100;
        R_MAP = (rc_t){VW / 2 - 3 * bw / 2 - 6, by, bw, 44};
        R_MANAGE = (rc_t){VW / 2 - bw / 2, by, bw, 44};
        R_TRADE = (rc_t){VW / 2 + bw / 2 + 6, by, bw, 44};
        R_ROLL = (rc_t){VW / 2 - VW * 21 / 100, FH * 82 / 100, VW * 42 / 100, 50};
        R_BAIL = (rc_t){VW / 2 - 70, FH * 26 / 100, 140, 40};
        return;
    }
    layout_rest();
}
static void layout_rest(void)
{
    if (TAB5) {
        // (the Tab5: the buttons in the bar under the board, and at the bottom of the other screens)
        const int by = FH - 58;
        R_MANAGE = (rc_t){8, by, 128, 50};
        R_TRADE = (rc_t){142, by, 118, 50};
        R_MAP = (rc_t){266, by, 96, 50};
        R_BAIL = (rc_t){368, by, 128, 50};
        R_ROLL = (rc_t){502, by - 2, 130, 54};
        R_T_CONT = (rc_t){8, by, 300, 50};
        R_T_PLAY = (rc_t){376, by, 124, 50};
        R_T_FRIENDS = (rc_t){506, by, 126, 50};
        R_S_LOOKL = (rc_t){8, 100, 40, 48};
        R_S_LOOKR = (rc_t){150, 100, 40, 48};
        R_S_NM = (rc_t){206, 100, 44, 40};
        R_S_NP = (rc_t){312, 100, 44, 40};
        R_S_LVL = (rc_t){206, 144, 150, 40};
        R_S_MODE = (rc_t){364, 100, 150, 40};
        R_S_START = (rc_t){VW - 150, FH - 64, 142, 56};
        R_S_BACK = (rc_t){8, FH - 56, 110, 48};
        R_L_BACK = (rc_t){8, FH - 56, 110, 48};
        R_R_BOTP = (rc_t){8, FH - 56, 110, 48};
        R_R_BOTM = (rc_t){124, FH - 56, 110, 48};
        R_R_LEAVE = (rc_t){370, FH - 56, 120, 48};
        R_R_START = (rc_t){500, FH - 60, 132, 54};
        return;
    }
    if (!NARROW) {
        return;
    }
    const int by = FH - 50;  // (the bar under the street view)
    R_MAP = (rc_t){4, by + 4, 64, 42};
    R_MANAGE = (rc_t){72, by + 4, 78, 42};
    R_TRADE = (rc_t){154, by + 4, 70, 42};
    R_ROLL = (rc_t){228, by, VW - 232, 48};
    R_BAIL = (rc_t){VW - 140, 124, 136, 40};
    R_T_PLAY = (rc_t){4, by, 96, 48};
    R_T_FRIENDS = (rc_t){104, by, 100, 48};
    R_T_CONT = (rc_t){208, by, VW - 212, 48};
    R_S_LOOKL = (rc_t){6, 40, 40, 48};
    R_S_LOOKR = (rc_t){94, 40, 40, 48};
    R_S_NM = (rc_t){146, 40, 44, 40};
    R_S_NP = (rc_t){VW - 50, 40, 44, 40};
    R_S_LVL = (rc_t){146, 88, VW - 152, 40};
    R_S_MODE = (rc_t){146, 134, VW - 152, 40};
    R_S_START = (rc_t){VW - 124, FH - 52, 120, 48};
    R_S_BACK = (rc_t){4, FH - 46, 92, 42};
    R_L_NEW = (rc_t){VW - 176, FH - 50, 172, 46};
    R_L_RESUME = (rc_t){8, 124, VW - 16, 40};
    R_L_BACK = (rc_t){4, FH - 46, 92, 42};
    R_R_BOTP = (rc_t){4, FH - 46, 76, 42};
    R_R_BOTM = (rc_t){84, FH - 46, 76, 42};
    R_R_LEAVE = (rc_t){164, FH - 46, 70, 42};
    R_R_START = (rc_t){238, FH - 50, VW - 242, 46};
}
// the round frame: the title, the setup, the friends and the MAP (each row in the width of the circle there; the
// buttons on the bottom arc)
static void layout_round(void)
{
    const int c = VW / 2;
    R_T_PLAY = r_mid(PC(71), PC(10), 12, PC(80));
    R_T_PLAY.w = R_T_PLAY.w / 2 - 5;
    R_T_FRIENDS = (rc_t){c + 5, R_T_PLAY.y, R_T_PLAY.w, R_T_PLAY.h};
    R_T_CONT = r_mid(PC(82), 44, 10, PC(50));
    // (the setup: the face between its arrows at the left, the number of the players at the right)
    R_S_LOOKL = (rc_t){PC(28) - PC(12) - 24, PC(28), 48, 48};
    R_S_LOOKR = (rc_t){PC(28) + PC(12) - 24, PC(28), 48, 48};
    R_S_NM = (rc_t){PC(72) - PC(12) - 24, PC(28), 48, 48};
    R_S_NP = (rc_t){PC(72) + PC(12) - 24, PC(28), 48, 48};
    R_S_LVL = (rc_t){c - PC(39) - 5, PC(50), PC(39), 46};
    R_S_MODE = (rc_t){c + 5, PC(50), PC(39), 46};
    R_S_START = r_mid(PC(64), 54, 0, PC(48));
    R_S_BACK = r_mid(PC(79), 44, 10, PC(30));
    R_L_RESUME = r_mid(PC(44), 44, 14, PC(70));
    R_L_NEW = r_mid(PC(56), 50, 14, PC(50));
    R_L_BACK = r_mid(PC(76), 44, 14, PC(30));
    R_R_BOTP = (rc_t){c - PC(25) - 5, PC(62), PC(25), 46};
    R_R_BOTM = (rc_t){c + 5, PC(62), PC(25), 46};
    R_R_LEAVE = (rc_t){c - PC(30) - 5, PC(74), PC(30), 48};
    R_R_START = (rc_t){c + 5, PC(74), PC(30), 48};
    R_MB_LIST = (rc_t){c - PC(24) - 5, PC(62), PC(24), 44};
    R_MB_BACK = (rc_t){c + 5, PC(62), PC(24), 44};
}
static void enter(int64_t now)
{
    s_now = now;
    layout();
    tables_init();
    s_lang = store_get("bv_lang", 1) ? 1 : 0;
    s_music = (int)store_get("bv_music", 1);
    s_snd = store_get("bv_snd", 1) != 0;
    s_speed = (int)store_get("bv_speed", 0);
    s_my_look = (int)(store_get("bv_look", 0) % NLOOK);
    s_map_board = (int)store_get("bv_mapv", 1) != 0;
    s_setup_n = (int)store_get("bv_n", 4);
    if (s_setup_n < 2 || s_setup_n > MAXP) {
        s_setup_n = 4;
    }
    s_setup_lvl = store_get("bv_lvl", 1) != 0;  // (the bots and the length of the game: saved too)
    s_setup_mode = (int)(store_get("bv_mode", MODE_FULL) % 3);
    if (!s_sky) {
        s_sky = heap_caps_malloc((size_t)SKY_W * SKY_H * 2, MALLOC_CAP_SPIRAM);
    }
    if (!s_sky) {
        s_sky = malloc((size_t)SKY_W * SKY_H * 2);
    }
    s_srng = 616;
    sky_make();
    sysui_panel_items(&PANEL);
    tfsfx_load_mem(BV_SFX_DATA, BV_SFX_LEN, FXN, FX_N < BV_SFX_N ? FX_N : BV_SFX_N, 41);  // (in the app file: no card)
    midi_bg_init(BV_TRACKS, BV_TRACK_COUNT, BV_MUSIC_VOICES);
    s_role = ROLE_LOCAL;
    memset(&g_v, 0, sizeof g_v);
    go_title();
    // the notice of an invite of a friend (friends_invite): straight to the lobby of FRIENDS
    if (!strcmp(app_launch_arg(), "friends")) {
        net_start();
        memset(s_tabs, 0, sizeof s_tabs);
        s_tid = 0;
        s_scr = SCR_LOBBY;
    }
}
static void leave(void)
{
    if (s_role == ROLE_LOCAL && g_v.magic == GAME_MAGIC && g_v.phase != PH_OVER && s_scr != SCR_TITLE) {
        save_game();
    }
    net_stop();
    midi_bg_end();
    tfsfx_free();
    if (s_sky) {
        heap_caps_free(s_sky);
        s_sky = NULL;
    }
}
static int64_t s_last;
static void update(int64_t now)
{
    float dt = s_last ? (now - s_last) / 1000.0f : 0.016f;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    s_last = now;
    s_now = now;
    midi_bg_service();
    if (s_jingle_until && now >= s_jingle_until) {
        s_jingle_until = 0;
        music_board();
    }
    if (s_scr == SCR_LOBBY || s_scr == SCR_ROOM || s_role != ROLE_LOCAL) {
        net_step();
    }
    if (s_role == ROLE_CLIENT && s_host_gone) {
        s_host_gone = false;
        bool paused = s_host_paused;
        s_host_paused = false;
        go_title();
        if (paused) {
            s_title_msg = 1;
            s_title_msg_t = s_now;
        }
        return;
    }
    if (s_role != ROLE_CLIENT) {
        s_ev_have = g_v.evn;
    }
    if (g_v.magic == GAME_MAGIC &&
        (s_scr == SCR_PLAY || s_scr == SCR_MANAGE || s_scr == SCR_TRADE || s_scr == SCR_MAP || s_scr == SCR_END)) {
        view_step(dt);
        host_step(dt);
        if (g_v.phase == PH_OVER && !view_busy() && s_scr == SCR_PLAY && s_vev >= s_ev_have) {
            s_scr = SCR_END;
        }
        // the BOOT key or a shake: ROLL, END TURN (my turn on the board)
        int presses = app_key_presses();
        bool shake = imu_ok() && imu_shake() > 1.8f;
        if ((presses || shake) && s_scr == SCR_PLAY && my_turn_ready()) {
            if (g_v.phase == PH_ROLL) {
                my_intent(IN_ROLL, 0);
            } else if (g_v.phase == PH_END && presses) {
                my_intent(IN_END, 0);
            }
        }
    } else {
        app_key_presses();
    }
}
static void render(int64_t now)
{
    (void)now;
    tfd_screen();
    tfd_noclip();
    switch (s_scr) {
    case SCR_TITLE:
        title_draw();
        break;
    case SCR_SETUP:
        setup_draw();
        break;
    case SCR_LOBBY:
        lobby_draw();
        break;
    case SCR_ROOM:
        room_draw();
        break;
    case SCR_PLAY:
        play_draw();
        break;
    case SCR_MANAGE:
        manage_draw();
        break;
    case SCR_TRADE:
        trade_draw();
        break;
    case SCR_MAP:
        map_draw();
        break;
    case SCR_END:
        end_draw();
        break;
    }
}
static void input_ev(const input_event_t *ev)
{
    if (ev->kind == IN_DOWN) {
        s_press_x = ev->x;
        s_press_y = ev->y;
    }
    if (ev->kind == IN_UP) {
        s_press_x = -1;
    }
    switch (s_scr) {
    case SCR_MANAGE:
        manage_input(ev);
        return;
    case SCR_TRADE:
        trade_input(ev);
        return;
    case SCR_MAP:
        map_input(ev);
        return;
    default:
        break;
    }
    if (ev->kind != IN_TAP) {
        return;
    }
    int x = ev->x, y = ev->y;
    switch (s_scr) {
    case SCR_TITLE:
        if (in_rc(R_T_PLAY, x, y)) {
            s_scr = SCR_SETUP;
            fx(FX_CLICK);
        } else if (in_rc(R_T_FRIENDS, x, y)) {
            net_start();
            memset(s_tabs, 0, sizeof s_tabs);
            s_tid = 0;
            s_scr = SCR_LOBBY;
            fx(FX_CLICK);
        } else if (s_has_save && in_rc(R_T_CONT, x, y) && load_game(&g_v)) {
            s_role = ROLE_LOCAL;
            s_my = 0;
            s_ev_have = g_v.evn;
            game_begin();
        }
        break;
    case SCR_SETUP:
        setup_tap(x, y);
        break;
    case SCR_LOBBY:
        lobby_tap(x, y);
        break;
    case SCR_ROOM:
        room_tap(x, y);
        break;
    case SCR_PLAY:
        play_tap(x, y);
        break;
    case SCR_END: {
        rc_t again = rc_end_title();
        if (in_rc(again, x, y)) {
            go_title();
        }
        break;
    }
    }
}
// a slow tap (a press of 350 - 900 ms) on each screen (the game has no holds) acts as a tap (libs/slowtap)
static slowtap_t s_slowtap;
static void input(const input_event_t *ev)
{
    input_event_t tap;
    const bool slow = st_slow(&s_slowtap, ev, &tap);
    input_ev(ev);
    if (slow) {
        input_ev(&tap);
    }
}
static bool awake(void)
{
    return s_scr == SCR_PLAY || s_scr == SCR_ROOM;
}

// ---------------------------------------------------------------- the icon: smooth shapes (tfdraw) in a round tile
// (the same small set in each app; all static and unused is no warning)
#define IC_FN static __attribute__((unused))
static struct {
    uint16_t *fb;
    int w, h, c[4];
} ic_keep __attribute__((unused));
// start an icon: tfdraw draws on the screen and only in the box; ic_end gives tfdraw its old target and clip back
IC_FN void ic_begin(int x, int y, int s)
{
    ic_keep.fb = tfd_fb;
    ic_keep.w = tfd_w;
    ic_keep.h = tfd_h;
    ic_keep.c[0] = tfd_cx0;
    ic_keep.c[1] = tfd_cy0;
    ic_keep.c[2] = tfd_cx1;
    ic_keep.c[3] = tfd_cy1;
    tfd_screen();
    tfd_clip(x, y, x + s, y + s);
}
IC_FN void ic_end(void)
{
    tfd_target(ic_keep.fb, ic_keep.w, ic_keep.h);
    tfd_clip(ic_keep.c[0], ic_keep.c[1], ic_keep.c[2], ic_keep.c[3]);
}
// the rows j0 .. j1 - 1 of the round tile (the corner radius of the menu frame), a gradient from c0 to c1
IC_FN void ic_band(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    int r = s / 8 + 2;
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        uint16_t c = tfd_mix(c0, c1, j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        if (dy <= 0) {
            tfd_hspan(x, x + s, y + j, c, 256);
            continue;
        }
        int in = tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy)));
        tfd_hspan(x + in + 1, x + s - in - 1, y + j, c, 256);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, y + j, c, a);
            tfd_px(x + s - 1 - k, y + j, c, a);
        }
    }
}
// the box inside the round corners (a shape in it does not cover a corner)
IC_FN void ic_inner(int x, int y, int s)
{
    int k = (s / 8 + 2) * 3 / 10 + 1;
    tfd_clip(x + k, y + k, x + s - k, y + s - k);
}
// a filled ellipse with a smooth edge
IC_FN void ic_oval(float cx, float cy, float rx, float ry, uint16_t c, int a)
{
    if (rx < 0.5f || ry < 0.5f) {
        return;
    }
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = fabsf(yy + 0.5f - cy), v = ry + 0.5f - d;
        if (v <= 0) {
            continue;
        }
        float q = d < ry ? d / ry : 1, hw = rx * sqrtf(1 - q * q), l = cx - hw, r = cx + hw;
        int aa = v >= 1 ? a : (int)(a * v);
        int il = tfd_ce(l), ir = tfd_fl(r);
        if (il > ir) {
            tfd_px(ir, yy, c, (int)(aa * (r - l)));
            continue;
        }
        tfd_hspan(il, ir, yy, c, aa);
        tfd_px(il - 1, yy, c, (int)(aa * (il - l)));
        tfd_px(ir, yy, c, (int)(aa * (r - ir)));
    }
}
// a ball in the light from the upper left: the dark rim, the body, the shine
IC_FN void ic_ball(float cx, float cy, float r, uint16_t dark, uint16_t body, uint16_t shine)
{
    tfd_disc(cx, cy, r, dark, 256);
    tfd_disc(cx - r * 0.1f, cy - r * 0.12f, r * 0.84f, body, 256);
    tfd_disc(cx - r * 0.34f, cy - r * 0.38f, r * 0.27f, shine, 200);
}
// a shape of n points (x, y pairs round 0, 0), turned by ang, k times bigger, at cx, cy
IC_FN void ic_shape(const float *p, int n, float cx, float cy, float k, float ang, uint16_t c, int a)
{
    float q[64], cs = fcos(ang) * k, sn = fsin(ang) * k;
    if (n > 32) {
        n = 32;
    }
    for (int i = 0; i < n; i++) {
        q[2 * i] = cx + p[2 * i] * cs - p[2 * i + 1] * sn;
        q[2 * i + 1] = cy + p[2 * i] * sn + p[2 * i + 1] * cs;
    }
    tfd_poly(q, n, c, a);
}
// A store app shows the frames of icon.bin, with at most 255 colours for all 16 frames: mk_app.py drops the low bits
// of each colour until they fit (most icons: 3 bits of red, 4 of green, 3 of blue). A colour on that grid stays as it
// is: ic_g gives the nearest one. ic_dband makes a gradient of grid colours with an ordered dither (few colours, no
// bands).
IC_FN uint16_t ic_g(int r, int g, int b)
{
    r = (r + 16) / 32 * 32;
    g = (g + 8) / 16 * 16;
    b = (b + 16) / 32 * 32;
    return rgb((uint8_t)(r > 224 ? 224 : r), (uint8_t)(g > 240 ? 240 : g), (uint8_t)(b > 224 ? 224 : b));
}
IC_FN void ic_dband(int x, int y, int s, int j0, int j1, uint16_t c0, uint16_t c1)
{
    static const uint8_t IC_BAYER[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    int r0, g0, b0, r1, g1, b1, r = s / 8 + 2;
    tfd_unrgb(c0, &r0, &g0, &b0);
    tfd_unrgb(c1, &r1, &g1, &b1);
    if (j0 < 0) {
        j0 = 0;
    }
    if (j1 > s) {
        j1 = s;
    }
    for (int j = j0; j < j1; j++) {
        int yy = y + j;
        if (yy < tfd_cy0 || yy >= tfd_cy1 || (unsigned)yy >= (unsigned)tfd_h) {
            continue;
        }
        // (the colour of the row in 1/16 steps of a colour value; a grid step is 32 = 512 for red and blue, 16 = 256
        // for green)
        int t = j1 - j0 > 1 ? (j - j0) * 256 / (j1 - j0 - 1) : 0;
        int fr = r0 * 16 + ((r1 - r0) * 16 * t >> 8), fg = g0 * 16 + ((g1 - g0) * 16 * t >> 8),
            fb = b0 * 16 + ((b1 - b0) * 16 * t >> 8);
        float dy = j < r ? r - (j + 0.5f) : j >= s - r ? (j + 0.5f) - (s - r) : 0;
        int in = dy > 0 ? tfd_ce(r - sqrtf(tfd_max(0, r * r - dy * dy))) : -1;
        uint16_t *row = tfd_fb + yy * tfd_w;
        for (int i = in + 1; i < s - in - 1; i++) {
            int xx = x + i;
            if (xx < tfd_cx0 || xx >= tfd_cx1 || (unsigned)xx >= (unsigned)tfd_w) {
                continue;
            }
            int th = IC_BAYER[(j & 3) * 4 + (i & 3)];
            int lr = (fr >> 9) + (((fr >> 5) & 15) > th), lg = (fg >> 8) + (((fg >> 4) & 15) > th),
                lb = (fb >> 9) + (((fb >> 5) & 15) > th);
            row[xx] = rgb((uint8_t)(lr > 7 ? 224 : lr * 32), (uint8_t)(lg > 15 ? 240 : lg * 16),
                          (uint8_t)(lb > 7 ? 224 : lb * 32));
        }
        uint16_t c = ic_g(fr >> 4, fg >> 4, fb >> 4);
        for (int k = 0; k <= in; k++) {
            float dx = r - (k + 0.5f), cov = r + 0.5f - sqrtf(dx * dx + dy * dy);
            if (cov <= 0) {
                continue;
            }
            int a = cov >= 1 ? 256 : (int)(256 * cov);
            tfd_px(x + k, yy, c, a);
            tfd_px(x + s - 1 - k, yy, c, a);
        }
    }
}
// a span of one row from l to r (in pixels, with soft ends)
IC_FN void ic_span(float l, float r, int yy, uint16_t c, int a)
{
    if (r <= l) {
        return;
    }
    int il = tfd_ce(l), ir = tfd_fl(r);
    if (il > ir) {
        tfd_px(ir, yy, c, (int)(a * (r - l)));
        return;
    }
    tfd_hspan(il, ir, yy, c, a);
    tfd_px(il - 1, yy, c, (int)(a * (il - l)));
    tfd_px(ir, yy, c, (int)(a * (r - ir)));
}
// the ring of an ellipse (rx, ry: the outer radii; w: its width), made of spans: cheap also when it is big
IC_FN void ic_ering(float cx, float cy, float rx, float ry, float w, uint16_t c, int a)
{
    float ix = rx - w, iy = ry - w;
    for (int yy = tfd_fl(cy - ry); yy <= tfd_ce(cy + ry); yy++) {
        float d = yy + 0.5f - cy, qo = d / ry;
        if (qo * qo >= 1) {
            continue;
        }
        float ho = rx * sqrtf(1 - qo * qo), qi = iy > 0 ? d / iy : 2;
        if (qi * qi >= 1) {
            ic_span(cx - ho, cx + ho, yy, c, a);
            continue;
        }
        float hi = ix * sqrtf(1 - qi * qi);
        ic_span(cx - ho, cx - hi, yy, c, a);
        ic_span(cx + hi, cx + ho, yy, c, a);
    }
}
// the icon: a white Bauhaus house of Tel Aviv (round balconies) on its square of the board, with the blue street sign
// and a palm; the gold coin of the rent turns over it and its windows light one after another (a loop of 2 s)
static void icon(int x, int y, int size)
{
    int s = size;
    ic_begin(x, y, s);
    float u = s / 100.0f, ph = fmodf(icon_time(), 2.0f) * 0.5f, an = ph * 6.2831853f;
    ic_dband(x, y, s, 0, s * 76 / 100, ic_g(32, 128, 224), ic_g(160, 224, 224));
    // the square of the board (green, with the blue band of the street at its top edge)
    ic_band(x, y, s, s * 76 / 100, s, ic_g(64, 160, 96), ic_g(32, 112, 64));
    tfd_rect(x, (int)(y + 76 * u), s, (int)(4 * u), ic_g(32, 64, 192), 256);
    // the palm
    tfd_line(x + 16 * u, y + 78 * u, x + 20 * u, y + 44 * u, 2.6f * u, ic_g(128, 80, 48), 256);
    for (int k = 0; k < 5; k++) {
        float a = 3.6f + k * 0.55f + 0.08f * fsin(an + k), lx = x + 20 * u, ly = y + 44 * u;
        float lf[] = {lx,
                      ly,
                      lx + fcos(a - 0.12f) * 9 * u,
                      ly + fsin(a - 0.12f) * 9 * u,
                      lx + fcos(a) * 16 * u,
                      ly + fsin(a) * 16 * u + 3 * u,
                      lx + fcos(a + 0.12f) * 9 * u,
                      ly + fsin(a + 0.12f) * 9 * u};
        tfd_poly(lf, 4, ic_g(32, 144, 64), 256);
    }
    // the house: a shadow, the white body, the shade at the right, the round balconies, the windows, the roof
    float bx = x + 34 * u, by = y + 30 * u, bw = 48 * u, bh = 50 * u;
    ic_oval(bx + bw / 2 + 4 * u, by + bh, bw * 0.6f, 3 * u, ic_g(32, 96, 64), 220);
    tfd_rect((int)bx, (int)by, (int)bw, (int)bh, ic_g(224, 224, 208), 256);
    tfd_rect((int)(bx + bw * 0.78f), (int)by, (int)(bw * 0.22f), (int)bh, ic_g(176, 176, 176), 256);
    tfd_rect((int)(bx - 2 * u), (int)(by - 3 * u), (int)(bw + 4 * u), (int)(3.4f * u), ic_g(160, 160, 160), 256);
    for (int f = 0; f < 3; f++) {
        float fy = by + 5 * u + f * 15 * u;
        for (int w = 0; w < 3; w++) {
            int n = f * 3 + w;
            bool lit = (int)(ph * 9) == (n * 4) % 9 || (int)(ph * 9) == (n * 4 + 1) % 9;
            tfd_rect((int)(bx + 5 * u + w * 14 * u), (int)fy, (int)(9 * u), (int)(7 * u),
                     lit ? ic_g(224, 192, 64) : ic_g(64, 112, 160), 256);
        }
        // (a round balcony at the left of each floor)
        tfd_rrect((int)(bx - 5 * u), (int)(fy + 7 * u), (int)(30 * u), (int)(4.2f * u), (int)(2 * u),
                  ic_g(224, 240, 240), 256);
        tfd_rect((int)(bx - 3 * u), (int)(fy + 10 * u), (int)(26 * u), (int)(1.2f * u), ic_g(160, 160, 176), 256);
    }
    tfd_rect((int)(bx + 18 * u), (int)(by + bh - 11 * u), (int)(8 * u), (int)(11 * u), ic_g(96, 64, 48), 256);
    // the blue street sign on the wall
    tfd_rrect((int)(bx + 30 * u), (int)(by + bh - 14 * u), (int)(16 * u), (int)(8 * u), (int)(1.5f * u),
              ic_g(224, 224, 224), 256);
    tfd_rrect((int)(bx + 31 * u), (int)(by + bh - 13 * u), (int)(14 * u), (int)(6 * u), (int)(1 * u), ic_g(32, 64, 160),
              256);
    tfd_rect((int)(bx + 33 * u), (int)(by + bh - 11 * u), (int)(10 * u), (int)(1.2f * u), ic_g(224, 224, 224), 256);
    // the coin of the rent: it turns (its width), it shines, it floats
    float cx = x + 58 * u, cy = y + 15 * u + 2 * u * fsin(an), cw = fabsf(fcos(an)) * 11 * u + 1.2f * u;
    ic_oval(cx, cy, cw + 1.2f * u, 12.2f * u, ic_g(160, 96, 0), 256);
    ic_oval(cx, cy, cw, 11 * u, ic_g(224, 192, 32), 256);
    if (cw > 5 * u) {
        ic_oval(cx, cy, cw * 0.62f, 7 * u, ic_g(224, 160, 0), 256);
        ic_oval(cx - cw * 0.3f, cy - 4 * u, cw * 0.22f, 2.4f * u, ic_g(224, 240, 160), 256);
    }
    ic_end();
}

const app_t app_blvd = {
        .name = "BOULEVARD",
        .landscape = true,
        .round = APP_ROUND_HI,
        .key = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
};
