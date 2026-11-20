// CONTROL (UTIL): remotes that work through the TFN COMPANIONS (small ESP32 boards with modules on their pins:
// companion/ of the repo; the app COMPANIONS pairs them and sets their wiring). A remote is a grid of keys of three
// kinds: an IR key (a tap sends one press, a finger that stays sends the repeat frames of the protocol, as the real
// remote), a SWITCH key (a relay or a light of a companion: ON / OFF, ON, OFF or a PRESS of half a second) and a
// SENSOR tile (a button, a motion sensor, a door contact or an analog sensor: its live value).
// LEARN: the IR receiver of a companion catches one key of the real remote, this board decodes it (libs/irdecode:
// NEC, SAMSUNG, SONY, RC5, RC6 ... or an exact copy of an unknown code), a tap on a name saves it, and the next key
// can come at once. A long press on a remote, or a tap on a button in EDIT, opens its menu.
// THE LAYERS: the service of TFN OS (tfn/companion.h: the paired companions, the radio, the requests), libs/tapui (the
// touch UI in mm), this app (rm_*.inc).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "irdecode/irdecode.h"
#include "rfdecode/rfdecode.h"
#include "tapui/tapui.h"
#include "tfn/app.h"
#include "tfn/audio.h"
#include "tfn/companion.h"
#include "tfn/gfx.h"
#include "tfn/input.h"
#include "tfn/kbd.h"
#include "tfn/settings.h"
#include "tfn/storage.h"
#include "tfn/sysui.h"
#include "tfn/uikit.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#define RM_SLEEP(ms) vTaskDelay(pdMS_TO_TICKS(ms))
#else
#define RM_SLEEP(ms) ((void)0)
#endif

#define PRESS_MS  130  // a finger on a button for this time sends the press (a drag before it scrolls)
#define REPEAT_MS 350  // a finger that stays this long: the repeat (a HOLD to the companion each 100 ms)

static bool s_he;
static const char *TAG = "control";
#include "rm_text.inc"
#include "rm_store.inc"

enum {
    S_HOME,
    S_REMOTE,
    S_LEARN,
    S_PICK  // the module of a new SWITCH key or SENSOR tile
};
static int s_scr;
static int s_rem_i = -1;     // the remote now
static bool s_edit;          // EDIT: a tap on a button opens its menu
static int s_relearn = -1;   // LEARN AGAIN of this button (-1: LEARN of new buttons)
static int s_menu_btn = -1;  // the button of the menu
static int64_t s_now, s_touch_t;

// THE SOUNDS (the boards do not vibrate: the ear tells that a press went out)
enum {
    SND_GOT,   // LEARN caught a key
    SND_SAME,  // LEARN caught a key that the remote has already
    SND_FAIL   // a press got no answer
};
static const tune_note_t TUNE_GOT[] = {{0, 70, 86, 0, W_SINE, 15}, {80, 160, 93, 0, W_SINE | TUNE_HOLD, 14}};
static const tune_note_t TUNE_SAME[] = {{0, 90, 83, 0, W_TRI, 15}, {150, 90, 83, 0, W_TRI, 15}};
static const tune_note_t TUNE_FAIL[] = {{0, 260, 64, -5, W_TRI | TUNE_HOLD, 16}};
static void snd(int what)
{
    audio_tune(what == SND_GOT ? TUNE_GOT : what == SND_SAME ? TUNE_SAME : TUNE_FAIL, what == SND_FAIL ? 1 : 2);
}

static void go(int scr)
{
    s_scr = scr;
    tu_scroll_reset();
}

static int comp_of_rem(int r)
{
    return r >= 0 && r < s_nrem && s_rem[r].pin != 0xFF ? tcm_find(s_rem[r].mac) : -1;
}

// the companion of a key (-1: not paired any more)
static int comp_of_btn(int r, const btn_t *b)
{
    return b->kind == K_IR ? comp_of_rem(r) : tcm_find(b->mac);
}

// the companion that the title of a remote tells about: the IR one, else the one of the first key
static int comp_of_title(int r)
{
    const int ci = comp_of_rem(r);
    return ci >= 0 || !s_rem[r].nb ? ci : comp_of_btn(r, &s_rem[r].b[0]);
}

// the role of the module of a key (TC_R_NONE: the module is not in the wiring now)
static int btn_role(const btn_t *b)
{
    const tcm_comp_t *c = tcm_get(tcm_find(b->mac));
    if (!c || !c->has_info) {
        return TC_R_NONE;
    }
    for (int k = 0; k < c->info.nwire; k++) {
        if (c->info.wire[k].pin == b->pin) {
            return c->info.wire[k].role;
        }
    }
    return TC_R_NONE;
}

static void open_companions(const char *arg)
{
    const app_t *a = app_find("COMPANIONS");
    if (a) {
        app_open_arg(a, arg);
    }
}

// ---------------------------------------------------------------- the keyboard
enum {
    KB_NEW_REMOTE,
    KB_REM_NAME,
    KB_BTN_NEW,
    KB_BTN_NAME,
    KB_MOD_NAME  // the name of a new SWITCH key or SENSOR tile
};
// the module of a new SWITCH key or SENSOR tile (S_PICK)
static struct {
    int kind;  // K_SWITCH or K_SENSOR
    int ci, pin, role, act;
} s_pick;
static bool s_typing;
static int s_kfor;
static char s_kbuf[24];

static void kbd_for(int what, const char *title, const char *def, int max)
{
    snprintf(s_kbuf, sizeof s_kbuf, "%s", def);
    s_kfor = what;
    s_typing = true;
    kbd_open(title, s_kbuf, max, false);
    kbd_set_hebrew(s_he);
}

// one line of the log for a code (the checks on a board: the codes of each key)
static void log_code(const char *what, const char *name, const ird_code_t *c, int nraw)
{
    char hx[40];
    ird_raw_hex(c, hx, sizeof hx);
    ESP_LOGI(TAG, "%s \"%s\": %s address 0x%lX command 0x%lX bits %d raw %s times %d", what, name ? name : "",
             ird_name(c->proto), (unsigned long)c->address, (unsigned long)c->command, c->nbits, hx, nraw);
}

static void log_remotes(void)
{
    for (int r = 0; r < s_nrem; r++) {
        for (int k = 0; k < s_rem[r].nb; k++) {
            log_code(s_rem[r].name, s_rem[r].b[k].name, &s_rem[r].b[k].code, s_rem[r].b[k].nraw);
        }
    }
}

// ---------------------------------------------------------------- THE LEARN: a capture on the IR receiver of a
// companion, the decode, a name
enum {
    L_WAIT,
    L_CAP,
    L_GOT,
    L_FAIL
};
static int s_lstate, s_lfail, s_lci = -1;
static ird_code_t s_lcode;
// the kind of the learn: IR (the IR receiver) or RF (a 433 or 315 MHz receiver; s_lrf, the band)
enum {
    LM_IR,
    LM_RF
};
static int s_lmode, s_lband;
static rfd_code_t s_lrf;
static int32_t s_lframe[TC_VALS_MAX];
static int s_lnf;
static bool s_lnext;  // a key was saved: "press the next key"

static int same_code(void);

// the packet of a RF receiver -> s_lrf: a code of a known coding, or (a long clean packet) an exact copy
static bool rf_decode(void)
{
    static rfd_packet_t p;
    memset(&p, 0, sizeof p);
    for (int i = 0; i < s_lnf && p.n < RFD_MAX_DUR; i++) {
        if (p.n == 0 && s_lframe[i] < 0) {
            continue;  // (the packet starts high)
        }
        p.d[p.n++] = s_lframe[i];
    }
    p.gap_after = 10000;
    if (p.n < 16 || !rfd_decode(&p, RFD_MODE_AUTO, 0, &s_lrf)) {
        return false;
    }
    if (s_lrf.kind == RFD_K_RAW && p.n < 24) {
        return false;  // (a short unknown packet: noise of the receiver)
    }
    // (the frame of the key: the packet itself, from its first high)
    memmove(s_lframe, p.d, (size_t)p.n * sizeof p.d[0]);
    s_lnf = p.n;
    ESP_LOGI(TAG, "learned RF: %s proto %d bits %d short %d gap %lu, %d times", rfd_kind_name(s_lrf.kind), s_lrf.proto,
             s_lrf.nbits, s_lrf.t_short, (unsigned long)s_lrf.t_gap, s_lnf);
    return true;
}

static void learn_start(void)
{
    s_lstate = L_WAIT;
}

static void learn_tick(void)
{
    if (s_lstate == L_WAIT) {
        int rx = -1;
        if (s_lmode == LM_RF) {
            s_lband = 0;
            s_lci = comp_with_role(TC_R_RF433_RX, &rx);
            if (s_lci < 0) {
                s_lband = 1;
                s_lci = comp_with_role(TC_R_RF315_RX, &rx);
            }
        } else {
            s_lci = comp_with_role(TC_R_IR_RX, &rx);
        }
        if (s_lci < 0) {
            s_lstate = L_FAIL;
            s_lfail = s_lmode == LM_RF ? T_NO_RF_RX : T_NO_RX;
            return;
        }
        if (tcm_online(s_lci) && tcm_req(s_lci) != TCM_BUSY) {
            // (RF: a packet ends at the sync gap of the next repeat, 5 ms or more)
            tcm_capture(s_lci, rx, 60, s_lmode == LM_RF ? 5 : 8);
            s_lstate = L_CAP;
        }
        return;
    }
    if (s_lstate != L_CAP || tcm_req_type(s_lci) != TCM_R_CAPTURE) {
        return;
    }
    const int st = tcm_req(s_lci);
    if (st == TCM_DONE) {
        s_lnf = tcm_frame(s_lframe, TC_VALS_MAX, NULL);
        if (s_lmode == LM_RF ? !rf_decode() : !ird_decode(s_lframe, s_lnf, false, &s_lcode)) {
            learn_start();
            return;
        }
        s_lstate = L_GOT;
        if (s_lmode == LM_IR) {
            log_code("learned", NULL, &s_lcode, s_lnf);
        }
        snd(same_code() >= 0 ? SND_SAME : SND_GOT);
        s_lnext = false;
        tu_scroll_reset();
    } else if (st == TCM_FAIL) {
        learn_start();  // (no key in 60 s, or no answer: the companion listens again)
    }
}

// the learned key under a name; then the next key
// the first IR key of a remote: the IR LED of a companion sends its keys
static void rem_bind_ir(void)
{
    if (s_rem[s_rem_i].pin != 0xFF) {
        return;
    }
    int pin = -1;
    const tcm_comp_t *c = tcm_get(comp_with_role(TC_R_IR_TX, &pin));
    if (c) {
        memcpy(s_rem[s_rem_i].mac, c->mac, 6);
        s_rem[s_rem_i].pin = (uint8_t)pin;
        mark_dirty();
    }
}

// the transmitter of the band of the learn (-1: none); *pin = its GPIO
static int rf_tx(int band, int *pin)
{
    return comp_with_role(band ? TC_R_RF315_TX : TC_R_RF433_TX, pin);
}

static void learn_save_rf(const char *name)
{
    int pin = -1;
    const int ci = rf_tx(s_lband, &pin);
    if (ci < 0) {
        tu_toast(T(T_NO_RF_TX), TU_WARN);
    }
    if (s_relearn >= 0) {
        btn_t *b = &s_rem[s_rem_i].b[s_relearn];
        if (b->raw) {
            heap_caps_free(b->raw);
            b->raw = NULL;
            b->nraw = 0;
        }
        rf_fill(b, &s_lrf, s_lframe, s_lnf);
        b->act = (uint8_t)s_lband;
        mark_dirty();
        tu_toast(T(T_RELEARNED), TU_ACC);
        s_relearn = -1;
        go(S_REMOTE);
        return;
    }
    if (btn_add_rf(s_rem_i, name, ci, pin, s_lband, &s_lrf, s_lframe, s_lnf) < 0) {
        tu_toast(T(T_FULL), TU_BAD);
    } else if (ci >= 0) {
        tu_toast(T(T_SAVED), TU_ACC);
    }
    learn_start();
    s_lnext = true;
}

static void learn_save(const char *name)
{
    if (s_lmode == LM_RF) {
        learn_save_rf(name);
        return;
    }
    rem_bind_ir();
    if (s_relearn >= 0) {
        btn_relearn(s_rem_i, s_relearn, &s_lcode, s_lframe, s_lnf);
        tu_toast(T(T_RELEARNED), TU_ACC);
        s_relearn = -1;
        go(S_REMOTE);
        return;
    }
    if (btn_add(s_rem_i, name, &s_lcode, s_lframe, s_lnf) < 0) {
        tu_toast(T(T_FULL), TU_BAD);
    } else {
        tu_toast(T(T_SAVED), TU_ACC);
    }
    learn_start();
    s_lnext = true;
}

// the button of this remote with the same code as the learned key (-1: none; LEARN AGAIN: not the button itself)
static int same_code(void)
{
    const rem_t *m = &s_rem[s_rem_i];
    for (int k = 0; k < m->nb; k++) {
        const btn_t *b = &m->b[k];
        if (k == s_relearn) {
            continue;
        }
        if (s_lmode == LM_RF) {
            const rfd_code_t *c = &s_lrf;
            if (b->kind == K_RF && b->rf.kind == c->kind && b->rf.proto == c->proto && b->rf.nbits == c->nbits &&
                c->kind != RFD_K_RAW && !memcmp(b->rf.bits, c->bits, (size_t)(c->nbits + 7) / 8)) {
                return k;
            }
            continue;
        }
        if (b->kind != K_IR) {
            continue;
        }
        if (raw_only(&s_lcode) ? b->nraw && b->code.hash == s_lcode.hash : !b->nraw && ird_same(&b->code, &s_lcode)) {
            return k;
        }
    }
    return -1;
}

// the names of NAMES that the remote does not have yet
static bool name_used(const char *n)
{
    const rem_t *m = &s_rem[s_rem_i];
    for (int k = 0; k < m->nb; k++) {
        if (!strcmp(m->b[k].name, n)) {
            return true;
        }
    }
    return false;
}

static int free_names(int *out)
{
    int k = 0;
    for (int i = 0; i < NNAMES; i++) {
        if (!name_used(NAMES[i][s_he ? 1 : 0])) {
            out[k++] = i;
        }
    }
    return k;
}

// ---------------------------------------------------------------- THE PRESS of a button: the plan of its protocol,
// SEND, then HOLD while the finger stays
static int s_press_btn = -1;
static bool s_press_sent, s_send_wait;
static int64_t s_press_t, s_hold_next;
static int s_press_ci = -1;

static bool send_code(int ci, int pin, btn_t *b)
{
    log_code("send", b->name, &b->code, b->nraw);
    static int32_t d[1500];
    tc_ir_plan_t pl;
    bool ok;
    if (b->nraw) {
        static int32_t lv[RAW_MAX];
        const int n = tc_levels_from_vals(b->raw, b->nraw, lv, RAW_MAX);
        ok = tc_ir_plan_raw(lv, n, b->carrier_khz, d, 1500, &pl);
    } else {
        ird_code_t k = b->code;
        if (k.proto == IRD_P_RC5 || k.proto == IRD_P_RC6) {
            // (a new press of a RC5 / RC6 key flips the toggle bit; a hold keeps it)
            ird_make(k.proto, k.address, k.command, b->toggle, k.nbits, &k);
            b->toggle ^= 1;
        }
        ok = tc_ir_plan(&k, d, 1500, &pl);
    }
    return ok && tcm_send(ci, pin, pl.carrier_khz, d, pl.n, pl.rep_at, pl.rep_ms);
}

// a SWITCH key: the new value of its output (ON / OFF turns the value now; no value yet: ON)
static bool switch_send(int ci, const btn_t *b)
{
    int v = 0;
    const bool has = tcm_value(ci, b->pin, &v, NULL);
    switch (b->act) {
    case A_ON:
        return tcm_set(ci, b->pin, 1, 0);
    case A_OFF:
        return tcm_set(ci, b->pin, 0, 0);
    case A_PRESS:
        return tcm_set(ci, b->pin, 1, PRESS_OUT_MS);
    default:
        return tcm_set(ci, b->pin, has ? !v : 1, 0);
    }
}

#define RF_REPEATS 10
static bool send_rf(int ci, const btn_t *b)
{
    static int32_t one[RAW_MAX], d[TC_VALS_MAX];
    int n1 = 0;
    if (b->rf.kind == RFD_K_RCSWITCH && b->rf.nbits <= 64) {
        uint64_t v = 0;
        for (int i = 0; i < b->rf.nbits; i++) {
            v = v << 1 | (uint64_t)((b->rf.bits[i >> 3] >> (7 - (i & 7))) & 1);
        }
        n1 = rfd_enc_rcswitch(b->rf.proto, v, b->rf.nbits, b->rf.t_short, one, RAW_MAX);
    }
    if (n1 <= 0 && b->nraw) {
        n1 = tc_levels_from_vals(b->raw, b->nraw, one, RAW_MAX - 1);
        // (the gap after the packet: the sync of the next repeat)
        const int32_t gap = -(int32_t)(b->rf.t_gap ? b->rf.t_gap : 10000);
        if (n1 > 0 && one[n1 - 1] < 0) {
            one[n1 - 1] = gap;
        } else if (n1 > 0) {
            one[n1++] = gap;
        }
    }
    if (n1 <= 0) {
        return false;
    }
    int n = 0;
    int64_t us = 0;
    for (int r = 0; r < RF_REPEATS && n + n1 <= TC_VALS_MAX; r++) {
        memcpy(d + n, one, (size_t)n1 * sizeof one[0]);
        n += n1;
    }
    for (int i = 0; i < n; i++) {
        us += d[i] < 0 ? -d[i] : d[i];
    }
    const int rep_ms = (int)(us / 1000) + 2;
    return tcm_send(ci, b->pin, 0, d, n, 0, rep_ms < 60000 ? rep_ms : 60000);
}

static void press_send(void)
{
    const btn_t *pb = s_press_btn >= 0 && s_press_btn < s_rem[s_rem_i].nb ? &s_rem[s_rem_i].b[s_press_btn] : NULL;
    if (pb && pb->kind == K_SENSOR) {
        sfx_ui(SFX_TICK);
        s_press_btn = -1;  // (a tile: it only shows)
        return;
    }
    const int ci = pb ? comp_of_btn(s_rem_i, pb) : -1;
    if (pb && pb->kind == K_RF) {
        if (ci < 0 || pb->pin == 0xFF) {
            snd(SND_FAIL);
            tu_toast(T(ci < 0 ? T_NO_ANSWER : T_NO_RF_TX), TU_BAD);
            s_press_btn = -1;
            return;
        }
        sfx_ui(SFX_CLICK);
        s_press_sent = true;
        s_press_ci = ci;
        s_hold_next = s_now + REPEAT_MS;
        s_send_wait = send_rf(ci, pb);
        return;
    }
    if (pb && pb->kind == K_SWITCH && ci >= 0) {
        sfx_ui(SFX_CLICK);
        s_press_sent = true;
        s_press_ci = ci;
        s_hold_next = INT64_MAX;  // (no repeat)
        s_send_wait = switch_send(ci, pb);
        return;
    }
    if (ci < 0 || !pb) {
        snd(SND_FAIL);
        tu_toast(T(T_NO_ANSWER), TU_BAD);
        s_press_btn = -1;
        return;
    }
    sfx_ui(SFX_CLICK);  // (at the moment of the send: the "feel" of a real key)
    s_press_sent = true;
    s_press_ci = ci;
    s_hold_next = s_now + REPEAT_MS;
    s_send_wait = send_code(ci, s_rem[s_rem_i].pin, &s_rem[s_rem_i].b[s_press_btn]);
}

static void press_end(void)
{
    const bool ir = s_press_btn >= 0 && s_press_btn < s_rem[s_rem_i].nb &&
                    (s_rem[s_rem_i].b[s_press_btn].kind == K_IR || s_rem[s_rem_i].b[s_press_btn].kind == K_RF);
    if (ir && s_press_sent && s_press_ci >= 0 && s_now - s_press_t >= REPEAT_MS) {
        tcm_stop(s_press_ci);
    }
    s_press_btn = -1;
    s_press_sent = false;
}

static void press_tick(void)
{
    // (an IR key sends while the finger stays, as a real remote; a SWITCH key acts only at the tap: a scroll that
    // starts on it must never turn a relay)
    if (s_press_btn >= 0 && !s_press_sent && s_now - s_press_t >= PRESS_MS &&
        (s_rem[s_rem_i].b[s_press_btn].kind == K_IR || s_rem[s_rem_i].b[s_press_btn].kind == K_RF)) {
        press_send();
    }
    if (s_press_btn >= 0 && s_press_sent && s_now >= s_hold_next) {
        s_hold_next += 100;
        tcm_hold(s_press_ci);
    }
    const int rt = s_press_ci >= 0 ? tcm_req_type(s_press_ci) : TCM_R_NONE;
    if (s_send_wait && (rt == TCM_R_SEND || rt == TCM_R_SET) && tcm_req(s_press_ci) != TCM_BUSY) {
        s_send_wait = false;
        if (tcm_req(s_press_ci) == TCM_FAIL) {
            snd(SND_FAIL);
            tu_toast(T(T_NO_ANSWER), TU_BAD);
        }
    }
}

// ---------------------------------------------------------------- the menus and the questions
enum {
    MENU_REMOTE = 1,
    MENU_BUTTON,
    MENU_ADD,  // + ADD of a remote: LEARN AN IR KEY, A SWITCH, A SENSOR, COMPANIONS
    MENU_ACT   // what a new SWITCH key does
};
enum {
    BM_RENAME,
    BM_LEARN,
    BM_BACK,
    BM_ON,
    BM_DELETE
};
static int s_bm[5], s_nbm;  // the rows of the menu of a button

static void on_delete_remote(int yes)
{
    if (yes == 1 && s_rem_i >= 0) {
        rem_delete(s_rem_i);
        s_rem_i = -1;
        go(S_HOME);
    }
}

static void on_delete_btn(int yes)
{
    if (yes == 1 && s_menu_btn >= 0) {
        btn_delete(s_rem_i, s_menu_btn);
        s_menu_btn = -1;
    }
}

static void menu_remote(int r)
{
    s_rem_i = r;
    tu_menu_open(MENU_REMOTE, s_rem[r].name);
    tu_menu_add(T(T_RENAME), TU_K_NORMAL);
    tu_menu_add(T(T_DELETE), TU_K_BAD);
}

static void menu_button(int k)
{
    s_menu_btn = k;
    const rem_t *m = &s_rem[s_rem_i];
    tu_menu_open(MENU_BUTTON, m->b[k].name);
    s_nbm = 0;
    s_bm[s_nbm++] = BM_RENAME;
    tu_menu_add(T(T_RENAME), TU_K_NORMAL);
    if (m->b[k].kind == K_IR || m->b[k].kind == K_RF) {
        s_bm[s_nbm++] = BM_LEARN;
        tu_menu_add(T(T_LEARN_AGAIN), TU_K_NORMAL);
    }
    if (k > 0) {
        s_bm[s_nbm++] = BM_BACK;
        tu_menu_add(T(T_MOVE_BACK), TU_K_NORMAL);
    }
    if (k + 1 < m->nb) {
        s_bm[s_nbm++] = BM_ON;
        tu_menu_add(T(T_MOVE_ON), TU_K_NORMAL);
    }
    s_bm[s_nbm++] = BM_DELETE;
    tu_menu_add(T(T_DELETE), TU_K_BAD);
}

static void menu_add(void)
{
    tu_menu_open(MENU_ADD, T(T_ADD));
    tu_menu_add(T(T_ADD_IR), TU_K_HOT);
    tu_menu_add(T(T_ADD_RF), TU_K_HOT);
    tu_menu_add(T(T_ADD_SWITCH), TU_K_PRI);
    tu_menu_add(T(T_ADD_SENSOR), TU_K_NORMAL);
    tu_menu_add(T(T_COMPANIONS), TU_K_GHOST);
}

static void name_new_module(void)
{
    char def[20];
    snprintf(def, sizeof def, "%s", role_word(s_pick.role));
    kbd_for(KB_MOD_NAME, T(T_NAME), def, 20);
}

static void on_menu(int owner, int row)
{
    if (owner == MENU_ADD) {
        if (row == 0 || row == 1) {
            s_relearn = -1;
            s_lmode = row == 1 ? LM_RF : LM_IR;
            learn_start();
            go(S_LEARN);
        } else if (row == 2 || row == 3) {
            s_pick.kind = row == 2 ? K_SWITCH : K_SENSOR;
            go(S_PICK);
        } else if (row == 4) {
            open_companions("control");
        }
        return;
    }
    if (owner == MENU_ACT) {
        if (row >= 0 && row <= A_PRESS) {
            s_pick.act = row;
            name_new_module();
        }
        return;
    }
    if (owner == MENU_REMOTE) {
        if (row == 0) {
            kbd_for(KB_REM_NAME, T(T_RENAME), s_rem[s_rem_i].name, 24);
        } else {
            sysui_ask(s_rem[s_rem_i].name, T(T_DELETE_REM_Q), T(T_DELETE), T(T_CANCEL), 0, on_delete_remote);
        }
        return;
    }
    const int k = s_menu_btn;
    if (row < 0 || row >= s_nbm || k < 0) {
        return;
    }
    switch (s_bm[row]) {
    case BM_RENAME:
        kbd_for(KB_BTN_NAME, T(T_RENAME), s_rem[s_rem_i].b[k].name, 20);
        break;
    case BM_LEARN:
        s_relearn = k;
        s_lmode = s_rem[s_rem_i].b[k].kind == K_RF ? LM_RF : LM_IR;
        learn_start();
        go(S_LEARN);
        break;
    case BM_BACK:
        btn_move(s_rem_i, k, -1);
        break;
    case BM_ON:
        btn_move(s_rem_i, k, 1);
        break;
    default:
        sysui_ask(s_rem[s_rem_i].b[k].name, T(T_DELETE_BTN_Q), T(T_DELETE), T(T_CANCEL), 0, on_delete_btn);
        break;
    }
}

static void kbd_done(bool ok)
{
    if (!ok) {
        return;
    }
    switch (s_kfor) {
    case KB_NEW_REMOTE: {
        // (the IR LED of a companion, when one has it; else the first IR key finds one)
        int pin = -1;
        const int ci = comp_with_role(TC_R_IR_TX, &pin);
        const tcm_comp_t *c = tcm_get(ci);
        static const uint8_t NO_MAC[6];
        const int r = rem_new(s_kbuf[0] ? s_kbuf : T(T_REMOTE), c ? c->mac : NO_MAC, c ? pin : 0xFF);
        if (r < 0) {
            tu_toast(T(T_FULL), TU_BAD);
            break;
        }
        s_rem_i = r;
        s_edit = false;
        s_relearn = -1;
        go(S_REMOTE);
        menu_add();
        break;
    }
    case KB_MOD_NAME:
        if (btn_add_mod(s_rem_i, s_kbuf[0] ? s_kbuf : role_word(s_pick.role), s_pick.kind, s_pick.ci, s_pick.pin,
                        s_pick.act) < 0) {
            tu_toast(T(T_FULL), TU_BAD);
        } else {
            tu_toast(T(T_SAVED), TU_ACC);
        }
        go(S_REMOTE);
        break;
    case KB_REM_NAME:
        snprintf(s_rem[s_rem_i].name, sizeof s_rem[0].name, "%s", s_kbuf);
        mark_dirty();
        break;
    case KB_BTN_NEW:
        learn_save(s_kbuf[0] ? s_kbuf : T(T_KEY));
        break;
    case KB_BTN_NAME:
        snprintf(s_rem[s_rem_i].b[s_menu_btn].name, sizeof s_rem[0].b[0].name, "%s", s_kbuf);
        mark_dirty();
        break;
    default:
        break;
    }
}

#include "rm_draw.inc"

// ---------------------------------------------------------------- the input
static void new_remote(void)
{
    if (!tcm_count()) {
        open_companions("control add");
        return;
    }
    char def[24];
    snprintf(def, sizeof def, "%s %d", T(T_REMOTE), s_nrem + 1);
    kbd_for(KB_NEW_REMOTE, T(T_NEW_REMOTE), def, 24);
}

// the test of a learned key (it is not saved yet)
static void learn_test(void)
{
    btn_t b;
    memset(&b, 0, sizeof b);
    if (s_lmode == LM_RF) {
        int pin = -1;
        const int ci = rf_tx(s_lband, &pin);
        b.kind = K_RF;
        b.pin = (uint8_t)pin;
        if (ci < 0 || !rf_fill(&b, &s_lrf, s_lframe, s_lnf)) {
            tu_toast(T(T_NO_RF_TX), TU_BAD);
            return;
        }
        if (send_rf(ci, &b)) {
            tu_toast(T(T_SENT), TU_HOT);
        }
        heap_caps_free(b.raw);
        return;
    }
    b.code = s_lcode;
    b.carrier_khz = (uint8_t)((s_lcode.carrier_hz ? s_lcode.carrier_hz : 38000) / 1000);
    static uint16_t v[RAW_MAX];
    if (raw_only(&s_lcode)) {
        b.nraw = (uint16_t)tc_vals_from_levels(s_lframe, s_lnf, v, RAW_MAX);
        b.raw = v;
    }
    rem_bind_ir();
    const int ci = comp_of_rem(s_rem_i);
    if (ci >= 0 && send_code(ci, s_rem[s_rem_i].pin, &b)) {
        tu_toast(T(T_SENT), TU_HOT);
    }
}

static void tap_learn(int k, int i)
{
    if (s_lstate == L_FAIL) {
        open_companions("control");
        return;
    }
    if (s_lstate != L_GOT) {
        return;
    }
    if (k == TU_H_ITEM) {
        learn_save(NAMES[i][s_he ? 1 : 0]);
    } else if (k == TU_H_SUB && i == 0) {
        char def[20];
        snprintf(def, sizeof def, "%s %d", T(T_KEY), s_rem[s_rem_i].nb + 1);
        kbd_for(KB_BTN_NEW, T(T_NAME), def, 20);
    } else if (k == TU_H_SUB && i == 1) {
        learn_test();
    } else if (k == TU_H_SUB && i == 2) {
        learn_start();  // AGAIN
    } else if (k == TU_H_SUB && i == 3 && s_relearn >= 0) {
        learn_save(NULL);
    }
}

static void on_tap(int id)
{
    const int k = TU_KIND(id), i = TU_IDX(id);
    if (k == TU_H_BACK) {
        if (s_scr == S_LEARN) {
            s_relearn = -1;
            if (s_lci >= 0 && tcm_req_type(s_lci) == TCM_R_CAPTURE) {
                tcm_info(s_lci);  // (a new request ends the capture)
            }
            go(s_rem_i >= 0 ? S_REMOTE : S_HOME);
        } else if (s_scr == S_REMOTE) {
            s_edit = false;
            go(S_HOME);
        } else if (s_scr == S_PICK) {
            go(S_REMOTE);
        }
        return;
    }
    switch (s_scr) {
    case S_HOME:
        if (k == TU_H_TOPKEY || k == TU_H_ACT || k == TU_H_SUB) {
            open_companions(tcm_count() ? "control" : "control add");
        } else if (k == TU_H_ITEM && i == 900) {
            new_remote();
        } else if (k == TU_H_ITEM && i < s_nrem) {
            s_rem_i = i;
            s_edit = false;
            go(S_REMOTE);
        }
        break;
    case S_REMOTE:
        if (k == TU_H_TOPKEY || k == TU_H_ACT) {
            s_edit = !s_edit;
        } else if (k == TU_H_ITEM && i == 900) {
            menu_add();
        } else if (k == TU_H_ITEM && s_edit && i < s_rem[s_rem_i].nb) {
            menu_button(i);
        }
        break;
    case S_LEARN:
        tap_learn(k, i);
        break;
    case S_PICK:
        if (k == TU_H_SUB) {
            open_companions("control");
        } else if (k == TU_H_ITEM) {
            const tcm_comp_t *c = tcm_get(i >> 4);
            if (!c || (i & 15) >= c->info.nwire) {
                break;
            }
            s_pick.ci = i >> 4;
            s_pick.pin = c->info.wire[i & 15].pin;
            s_pick.role = c->info.wire[i & 15].role;
            s_pick.act = A_TOGGLE;
            if (s_pick.kind == K_SWITCH) {
                tu_menu_open(MENU_ACT, T(T_WHAT_DO));
                tu_menu_add(T(T_TOGGLE), TU_K_PRI);
                tu_menu_add(T(T_ON), TU_K_NORMAL);
                tu_menu_add(T(T_OFF), TU_K_NORMAL);
                tu_menu_add(T(T_PRESS), TU_K_NORMAL);
            } else {
                name_new_module();
            }
        }
        break;
    default:
        break;
    }
}

static void input(const input_event_t *ev)
{
    if (s_typing) {
        const int r = kbd_input(ev);
        if (r == KBD_OK || r == KBD_CANCEL) {
            s_typing = false;
            kbd_done(r == KBD_OK);
        }
        return;
    }
    s_touch_t = s_now;
    tu_items_silent(s_scr == S_REMOTE && !s_edit);  // (a key of a remote clicks when it sends)
    const tu_ev_t e = tu_input(ev);
    const bool btn = s_scr == S_REMOTE && !s_edit && TU_KIND(e.id) == TU_H_ITEM && s_rem_i >= 0 &&
                     TU_IDX(e.id) < s_rem[s_rem_i].nb;
    switch (e.kind) {
    case TU_E_DOWN:
        if (btn) {
            s_press_btn = TU_IDX(e.id);
            s_press_t = s_now;
            s_press_sent = false;
        }
        break;
    case TU_E_CANCEL:
        if (!s_press_sent) {
            s_press_btn = -1;  // (a scroll, no press)
        }
        break;
    case TU_E_UP:
        if (s_press_btn >= 0 && s_press_sent) {
            press_end();
        }
        break;
    case TU_E_TAP:
        if (s_press_btn >= 0) {
            if (!s_press_sent) {
                press_send();  // (a short tap: one press)
            }
            press_end();
            break;
        }
        on_tap(e.id);
        break;
    case TU_E_LONG:
        if (s_scr == S_HOME && TU_KIND(e.id) == TU_H_ITEM && TU_IDX(e.id) < s_nrem) {
            menu_remote(TU_IDX(e.id));
        } else if (s_scr == S_REMOTE && s_edit && TU_KIND(e.id) == TU_H_ITEM && TU_IDX(e.id) < s_rem[s_rem_i].nb) {
            menu_button(TU_IDX(e.id));
        }
        break;
    case TU_E_MENU:
        on_menu(e.owner, e.id);
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- enter / leave / update
static const char *const LANG_VALS[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const pitem_t ITEMS[] = {{PI_PICK, 0, 2, 0, {"LANGUAGE", "שפה"}, LANG_VALS, "control_lang"}};
static void item_set(int id, int v)
{
    (void)id;
    s_he = v == 1;
    tu_lang(s_he);
}
static const pitems_t PANEL = {ITEMS, 1, NULL, NULL, item_set, NULL, NULL};

static void enter(int64_t now)
{
    (void)now;
    s_now = esp_timer_get_time() / 1000;
    s_he = store_get("control_lang", store_get("remote_lang", 0)) == 1;
    tu_init(s_he);
    if (!s_rem) {
        s_rem = heap_caps_calloc(RM_MAX, sizeof(rem_t), MALLOC_CAP_SPIRAM);
    }
    if (s_rem) {
        rem_load();
        log_remotes();
    }
    comp_open();
    s_scr = S_HOME;
    s_typing = false;
    s_press_btn = -1;
    s_rem_i = -1;
    s_relearn = -1;
    s_edit = false;
    s_touch_t = s_now;
    sysui_panel_items(&PANEL);
}

static void leave(void)
{
    if (s_dirty) {
        rem_save();
    }
    comp_close();
    rem_free_all();
    heap_caps_free(s_rem);  // (no memory of the app stays after its close)
    s_rem = NULL;
}

static void update(int64_t now)
{
    (void)now;
    s_now = esp_timer_get_time() / 1000;
    comp_poll();
    const bool moving = tu_update(s_now);
    press_tick();
    if (s_scr == S_LEARN) {
        learn_tick();
    }
    if (s_dirty && s_now - s_dirty_t > 1500) {
        rem_save();
    }
    // a still screen: fewer frames
    const bool live = moving || s_typing || s_scr == S_LEARN || s_press_btn >= 0 || tu_toast_on() || tu_menu_on();
    if (!live && s_now - s_touch_t > 4000) {
        RM_SLEEP(60);
    }
}

static bool awake(void)
{
    return s_scr == S_LEARN;
}

const app_t app_control = {
        .name = "CONTROL",
        .landscape = true,
        .round = APP_ROUND_HI,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = icon,
        .awake = awake,
};
