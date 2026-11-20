#ifdef HOST_NOMEM
#undef malloc  // (NOMEM=1: the stubs are the test frame, they use the real functions)
#undef calloc
#undef realloc
#endif
#include <stdio.h>
// Stubs for the hardware: audio, IMU, NVS, board.
#include <stdlib.h>
#include <string.h>
#include "audio.h"
#include "imu.h"
#include "settings.h"
#include "app.h"
#include "gfx.h"
#include "mfont.h"

settings_t g_settings = {80, 200, false, true};
float sim_tx, sim_ty, sim_shake, sim_ax, sim_ay;
long n_voices, n_sfx;
void (*sim_tick)(void);
const app_t *sim_opened;

void sfx_play(sfx_t s) { if (s >= SFX_COUNT) abort(); n_sfx++; }
void sfx_ui(sfx_t s) { if (s >= SFX_COUNT) abort(); n_sfx++; }
bool audio_ui_sounds(void) { return true; }
void audio_set_ui_sounds(bool on) { (void)on; }
int sim_wakes;
void app_screen_wake(void) { sim_wakes++; }
__attribute__((weak)) void app_standby_hold(bool on) { (void)on; }
__attribute__((weak)) bool app_in_match(void) { return false; }      // (main.c: a match with another board)
const char *app_screen_state(void) { return "on"; }
int sim_pocket_ms;
__attribute__((weak)) void app_screen_pocket(int ms) { sim_pocket_ms = ms; }
int sim_key_presses;
bool sim_key_down;
int app_key_presses(void) { int n = sim_key_presses; sim_key_presses = 0; return n; }
bool app_key_down(void) { return sim_key_down; }
void sfx_tone(uint16_t a, uint16_t b, uint16_t d, wave_t w, uint8_t v) { n_sfx++; }
void audio_set_mute(bool m) {}
bool audio_muted(void) { return false; }
void audio_set_volume(int v) {}
void audio_drone(int ch, float hz, int vol, wave_t w) { if (ch < 0 || ch > 1 || hz != hz) abort(); }
void (*sim_voice_hook)(const audio_voice_t *v);  // (a test can mix the voices itself)
void audio_voice(const audio_voice_t *v)
{
    if (sim_voice_hook) sim_voice_hook(v);
    // (a looped sample voice can last for hours: the stream of ON AIR plays its ring of samples that way)
    bool stream = v->wave == W_SAMPLE && v->loop && v->sample && v->sample_len > 1;
    if (v->f0 != v->f0 || v->f0 < 0 || v->f0 > 20000 || v->dur == 0 || (v->dur > 10 * AUDIO_SAMPLE_RATE && !stream) || v->delay > AUDIO_SAMPLE_RATE) {
        printf("bad voice f=%f dur=%u delay=%u", v->f0, (unsigned)v->dur, (unsigned)v->delay);
        puts("");
        abort();
    }
    n_voices++;
}
void audio_release_group(uint8_t g, uint16_t ms) {}
void audio_kill_group(uint8_t g) {}
void audio_set_tick(void (*cb)(void)) { sim_tick = cb; }

bool imu_ok(void) { return true; }
// a test can set the physical gravity directly (sim_g_on)
float sim_g[3];
bool sim_g_on;
void imu_gravity_phys(float *x, float *y, float *z)
{
    if (sim_g_on) { *x = sim_g[0]; *y = sim_g[1]; *z = sim_g[2]; return; }
    *x = sim_tx; *y = 0.7f; *z = 0.7f;
}
void imu_gravity_logical(float *x, float *y) { *x = sim_tx; *y = 0.8f + sim_ty; }
void imu_set_level(void) {}
void imu_wheel_shift(int steps) { (void)steps; }
static float sim_ascale = 1.0f;
static bool sim_ascale_auto = true;
float imu_accel_scale(void) { return sim_ascale; }
bool imu_accel_scale_auto(void) { return sim_ascale_auto; }
void imu_set_accel_scale(float k) { sim_ascale_auto = k <= 0; if (k > 0) sim_ascale = k < 0.7f ? 0.7f : (k > 1.4f ? 1.4f : k); }
float imu_raw_g(void) { return 1.0f; }
void imu_accel_cal_set(const float off[3], const float gain[3]) { (void)off; (void)gain; }
bool imu_accel_cal_on(void) { return false; }
void imu_accel_cal_get(float off[3], float gain[3]) { for (int k = 0; k < 3; k++) { off[k] = 0; gain[k] = 1; } }
void imu_raw(float a[3], float w[3]) { a[0] = 0; a[1] = 0; a[2] = 1; w[0] = w[1] = w[2] = 0; }
void input_touch_cal_set(const float m[6]) { (void)m; }
bool input_touch_cal_get(float m[6]) { m[0] = 1; m[1] = 0; m[2] = 0; m[3] = 0; m[4] = 1; m[5] = 0; return false; }
int input_points_raw(int16_t *x, int16_t *y, int max) { (void)x; (void)y; (void)max; return 0; }
void input_tap_cancel(void) {}
const char *ota_version(void) { return "1.0.0"; }
const char *ota_slot(void) { return "ota_0"; }
const char *tfn_id_str(void) { return ""; }  // (the device identity: no ID in a host build)
void tfn_id_init(void) {}  // (no key is made in a host build)
// (each event of a host build is by a finger: the simulator has the real input.c)
bool input_injected(const input_event_t *ev) { (void)ev; return false; }
void imu_tilt_logical(float *x, float *y) { *x = sim_tx; *y = sim_ty; }
void imu_linear_logical(float *x, float *y, float *z) { *x = sim_ax; *y = sim_ay; *z = 0; }
float imu_shake(void) { return sim_shake; }
void imu_shake_ticks(bool on) { (void)on; }
void imu_velocity_phys(float v[3]) { v[0] = sim_ax * 0.5f; v[1] = sim_tx * 0.4f; v[2] = sim_ay * 0.5f; }
void imu_linacc_phys(float a[3]) { a[0] = sim_ax * 3.0f; a[1] = sim_tx * 2.0f; a[2] = sim_ay * 3.0f; }
// the dial (rad, + = clockwise) and the step count: a test sets them directly
float sim_dial, sim_turn_rate;
uint32_t sim_steps;
float imu_dial(void) { return sim_dial; }
float imu_turn_rate(void) { return sim_turn_rate; }
uint32_t imu_steps(void) { return sim_steps; }
orient_t imu_upright(bool l, orient_t c, float m) { return l ? ORIENT_LAND_A : ORIENT_PORTRAIT; }
orient_t imu_upright_any(orient_t c, float m) { return rand() % 50 ? c : (orient_t)(rand() % 4); }

// the settings store: a small table, as the NVS of the board keeps the values (a save, a CONTINUE)
static struct { char k[16]; uint32_t v; } s_store[96];
static int s_store_n;
uint32_t store_get(const char *k, uint32_t d)
{
    for (int i = 0; i < s_store_n; i++) if (!strncmp(s_store[i].k, k, 15)) return s_store[i].v;
    return d;
}
void store_set(const char *k, uint32_t v)
{
    for (int i = 0; i < s_store_n; i++) if (!strncmp(s_store[i].k, k, 15)) { s_store[i].v = v; return; }
    if (s_store_n < 96) { snprintf(s_store[s_store_n].k, 16, "%s", k); s_store[s_store_n].v = v; s_store_n++; }
}
void settings_save(void) {}
void settings_load(void) {}
void settings_apply(void) {}
int settings_lang_all(void) { uint32_t v = store_get("lang_all", 0xFF); return v > 1 ? -1 : (int)v; }
void settings_lang_all_set(int lang) { store_set("lang_all", lang ? 1 : 0); }
void board_backlight(uint8_t l) {}
void app_open(const app_t *a) { sim_opened = a; }
static char sim_arg[64];
void app_open_arg(const app_t *a, const char *arg) { sim_opened = a; snprintf(sim_arg, sizeof sim_arg, "%s", arg ? arg : ""); }
const char *app_launch_arg(void) { return sim_arg; }
// the notifications (notify.c is not in the host build): a board with no notices, the switches in the stub store
#include "notify.h"
static uint32_t nt_bits(const char *app) { char k[16]; snprintf(k, sizeof k, "na_%.12s", app); return store_get(k, 0); }
static void nt_bits_set(const char *app, uint32_t m, uint32_t v) { char k[16]; snprintf(k, sizeof k, "na_%.12s", app); store_set(k, (store_get(k, 0) & ~m) | (v & m)); }
int notify_count(void) { return 0; }
__attribute__((weak)) uint32_t notify_post(const char *app, const char *t, const char *x, const char *l) { (void)app; (void)t; (void)x; (void)l; return 1; }
int notify_unread(const char *app) { (void)app; return 0; }
int notify_apps(char names[][NT_NAME], int max) { if (max < 1) return 0; snprintf(names[0], NT_NAME, "WEATHER"); return 1; }
int notify_mode(const char *app) { return (int)(nt_bits(app) & 3) % 3; }
void notify_set_mode(const char *app, int m) { nt_bits_set(app, 3, (uint32_t)m); }
bool notify_wake(const char *app) { return (nt_bits(app) & 4) != 0; }
void notify_set_wake(const char *app, bool on) { nt_bits_set(app, 4, on ? 4 : 0); }
bool notify_sound(const char *app) { return !(nt_bits(app) & 8); }
void notify_set_sound(const char *app, bool on) { nt_bits_set(app, 8, on ? 0 : 8); }
bool notify_all_on(void) { return store_get("nt_off", 0) == 0; }
void notify_set_all(bool on) { store_set("nt_off", on ? 0 : 1); }
int notify_dnd(void) { return (int)store_get("nt_dnd", 0) % 3; }
void notify_set_dnd(int v) { store_set("nt_dnd", (uint32_t)v); }
bool notify_in_games(void) { return store_get("nt_game", 0) != 0; }
void notify_set_in_games(bool on) { store_set("nt_game", on ? 1 : 0); }
void notify_center_open(void) {}
void notify_init(void) {}
void notify_tick(int64_t now) { (void)now; }
bool notify_input(const input_event_t *ev) { (void)ev; return false; }
void notify_draw(int64_t now) { (void)now; }
bool notify_center_on(void) { return false; }
void notify_center_close(void) {}
void notify_seen_app(const char *app) { (void)app; }
bool notify_banner_on(void) { return false; }
int watch_every(void) { uint32_t v = store_get("wt_every", 5); return (int)v; }
void watch_set_every(int min) { store_set("wt_every", (uint32_t)min); }
uint32_t tfn_notify(const char *title, const char *text, const char *link) { (void)title; (void)text; (void)link; return 1; }
const app_t *app_current(void) { return sim_opened; }
void app_saver_now(bool off) { (void)off; }
uint8_t sim_caps = APP_NEEDS_BAR | APP_NEEDS_IMU;
uint8_t app_board_caps(void) { return sim_caps; }
const uint16_t SLEEP_SECONDS[SLEEP_CHOICES] = {0, 30, 60, 120, 300};
const char *const SLEEP_NAMES[SLEEP_CHOICES] = {"NEVER", "30 S", "1 MIN", "2 MIN", "5 MIN"};

// WiFi: not on the PC
#include "wifi.h"
bool wifi_start(void) { return true; }
wifi_state_t wifi_state(void) { return WIFI_IDLE; }
int wifi_rssi(void) { return -60; }         // (the menu tests draw the WiFi signal)
const char *wifi_ip(void) { return ""; }

// the secret store of an app (main/watch.h): a small table, as the NVS of a board (the tests set values)
static char s_sec_k[16][32], s_sec_v[16][400];
int tfn_secret_get(const char *key, char *out, int max)
{
    for (int i = 0; i < 16; i++)
        if (s_sec_k[i][0] && !strcmp(s_sec_k[i], key)) {
            snprintf(out, (size_t)max, "%s", s_sec_v[i]);
            return (int)strlen(s_sec_v[i]);
        }
    return -1;
}
bool tfn_secret_set(const char *key, const char *value)
{
    int f = -1;
    for (int i = 0; i < 16; i++) {
        if (s_sec_k[i][0] && !strcmp(s_sec_k[i], key)) { f = i; break; }
        if (!s_sec_k[i][0] && f < 0) f = i;
    }
    if (f < 0) return false;
    if (!value || !*value) { s_sec_k[f][0] = 0; return true; }
    snprintf(s_sec_k[f], sizeof s_sec_k[f], "%s", key);
    snprintf(s_sec_v[f], sizeof s_sec_v[f], "%s", value);
    return true;
}

const char *wifi_ssid(void) { return "TEST"; }
void wifi_scan(void) {}
bool wifi_scanning(void) { return false; }
int wifi_scan_results(wifi_ap_t *out, int max)
{
    int n = max < 6 ? max : 6;
    for (int i = 0; i < n; i++) {
        snprintf(out[i].ssid, sizeof(out[i].ssid), "NET%d", i);
        out[i].rssi = (int8_t)(-40 - i * 8);
        out[i].secure = i & 1;
    }
    return n;
}
void wifi_connect(const char *s, const char *p, bool save) {}
void wifi_connect_try(const char *s, const char *p) { (void)s; (void)p; }
void wifi_forget(void) {}
bool wifi_saved(void) { return false; }
void wifi_forget_all(void) {}
void wifi_disconnect(void) {}
void wifi_reconnect(void) {}
bool wifi_user_off(void) { return false; }
bool wifi_saved_auto(const char *ssid) { (void)ssid; return true; }
bool wifi_saved_set_auto(const char *ssid, bool on) { (void)ssid; (void)on; return true; }
void wifi_forget_name(const char *ssid) { (void)ssid; }
// TFN OS (main/tfn_app.c): no installed apps on the host
#include "tfn_app.h"
int tfn_count(void) { return 0; }
void tfn_icon_ask(bool on) { (void)on; }  // (the icon reader of the menus: no icon files on the host)
bool tfn_icon_missed(void) { return false; }
void tfn_icon_want(const app_t *a) { (void)a; }
void tfn_icon_moving(void) {}
// the ears (tfn_listen.h): no ears here (no TALK watcher)
#include "tfn_listen.h"
bool listen_offer(const listen_ops_t *ops) { (void)ops; return false; }
bool listen_ready(void) { return false; }
bool listen_start(const char *brain) { (void)brain; return false; }
void listen_stop(void) {}
void listen_cancel(void) {}
int listen_state(void) { return LS_OFF; }
float listen_level(void) { return 0.0f; }
void listen_text(char *out, int cap) { if (cap > 0) { out[0] = 0; } }
void listen_answer(char *out, int cap) { if (cap > 0) { out[0] = 0; } }
bool listen_ask(const char *brain, const char *text) { (void)brain; (void)text; return false; }
void listen_handler(listen_handler_fn fn) { (void)fn; }
bool tfa_listen_task_now(int *w) { (void)w; return false; }
void tfa_listen_slot_gone(int w) { (void)w; }
void tfa_listen_quiet(void) {}
bool listen_key_down(void) { return false; }
void listen_key_held(int ms) { (void)ms; }
void listen_key_up(void) {}
void listen_tick(int64_t now) { (void)now; }
void listen_overlay(void) {}
void listen_app_changed(void) {}
bool listen_shows(void) { return false; }
bool listen_awake(void) { return false; }
bool listen_calm(void) { return false; }
void listen_console(const char *a, const char *b) { (void)a; (void)b; }
// the voice (tfn/speech.h, main/tfn_speech.c): no voice on the host
#include "tfn_speech.h"
bool speech_offer(const speech_ops_t *ops) { (void)ops; return false; }
bool speech_ready(void) { return false; }
void speech_say(const char *text) { (void)text; }
void speech_stop(void) {}
bool speech_make(const char *text, const char *path) { (void)text; (void)path; return false; }
bool speech_file_ok(const char *path, const char *text) { (void)path; (void)text; return false; }
int speech_play_file(const char *path) { (void)path; return 0; }
bool speech_read_open(const speech_read_cfg_t *c) { (void)c; return false; }
void speech_read_play(bool on) { (void)on; }
void speech_read_close(void) {}
void speech_read_get(speech_read_t *st) { memset(st, 0, sizeof *st); }
#if BOARD_TTS
bool speech_calm(void) { return false; }
bool speech_notices_on(void) { return false; }
void speech_set_notices(bool on) { (void)on; }
void speech_notice(const char *app, const char *title, const char *text) { (void)app; (void)title; (void)text; }
const char *speech_state(void) { return "NO VOICE ON THE HOST"; }
void speech_init(void) {}
#endif
const tfn_entry_t *tfn_entry(int i) { (void)i; return NULL; }
bool tfn_runs(const tfn_entry_t *e) { (void)e; return false; }
const app_t *tfn_find(const char *name) { (void)name; return NULL; }
int tfn_group_of(const app_t *a) { (void)a; return 0; }
uint32_t tfn_gen(void) { return 1; }
__attribute__((weak)) void tfn_rescan(void) {}
int tfn_ver_cmp(const char *a, const char *b) { return strcmp(a ? a : "", b ? b : ""); }
bool tfn_newer_than_fw(const char *ver) { (void)ver; return false; }
// (the permission classes of TFN OS: none on the pc)
__attribute__((weak)) uint32_t tfn_perms_parse(const char *list, bool *unknown) { (void)list; if (unknown) *unknown = false; return 0; }
__attribute__((weak)) void tfn_perms_text(uint32_t bits, char *out, int len) { (void)bits; if (len) out[0] = 0; }
__attribute__((weak)) int tfn_abi_rev(void) { return 0; }
__attribute__((weak)) int tfn_policy_ver(void) { return 0; }
__attribute__((weak)) bool tfn_policy_blob(const char *b, size_t n, char *msg, int len) { (void)b; (void)n; if (len) msg[0] = 0; return false; }
__attribute__((weak)) const app_t *app_find(const char *name) { (void)name; return NULL; }
// the web password (main/web_auth.c is board code): the on / off state for the SETUP row
static bool s_web_pw;
void web_auth_init(void) {}
bool web_auth_on(void) { return s_web_pw; }
bool web_auth_set(const char *pw, char *msg, int len)
{
    size_t n = pw ? strlen(pw) : 0;
    if (n && (n < 4 || n > 63)) { if (len) snprintf(msg, (size_t)len, "THE PASSWORD NEEDS 4 TO 63 CHARACTERS"); return false; }
    s_web_pw = n > 0;
    if (len) snprintf(msg, (size_t)len, n ? "THE WEB PASSWORD IS ON" : "THE WEB PASSWORD IS OFF");
    return true;
}
void web_auth_register(void *server) { (void)server; }
static bool s_tfn_dev;
bool tfn_dev_mode(void) { return s_tfn_dev; }
bool tfn_inst_place_card(void) { return true; }
void tfn_set_dev_mode(bool on) { s_tfn_dev = on; }
bool tfn_is_stub(const app_t *a) { (void)a; return false; }
const tfn_entry_t *tfn_entry_of(const app_t *a) { (void)a; return NULL; }
bool tfn_remove(const char *id, char *msg, int len) { (void)id; if (len) msg[0] = 0; return false; }
// the trusted publisher list (main/tfn_pub.c is board code): no keys on the pc; the page of SETUP shows ADD only
#include "tfn_pub.h"
__attribute__((weak)) int tfn_pub_count(void) { return 0; }
__attribute__((weak)) int tfn_pub_builtin_count(void) { return 0; }
__attribute__((weak)) bool tfn_pub_builtin_id(int i, uint8_t id[TFN_PUB_ID]) { (void)i; (void)id; return false; }
__attribute__((weak)) bool tfn_pub_get(int i, tfn_pub_t *out) { (void)i; (void)out; return false; }
__attribute__((weak)) bool tfn_pub_waiting(tfn_pub_t *out, int *secs, uint32_t *seq) { (void)out; (void)secs; (void)seq; return false; }
__attribute__((weak)) void tfn_pub_key_id_of(const tfn_pub_t *p, uint8_t id[TFN_PUB_ID]) { (void)p; memset(id, 0, TFN_PUB_ID); }
__attribute__((weak)) bool tfn_pub_answer(bool allow, uint32_t seq, char *msg, int len) { (void)allow; (void)seq; if (len) snprintf(msg, (size_t)len, "no key waits"); return false; }
__attribute__((weak)) bool tfn_pub_remove_id(const uint8_t id[TFN_PUB_ID], char *msg, int len) { (void)id; if (len) msg[0] = 0; return false; }
__attribute__((weak)) bool tfn_pub_request_card(char *msg, int len) { if (len) snprintf(msg, (size_t)len, "no SD card"); return false; }
// the pool service (main/tfn_pool.c is board code): the "no pool" answers. SETUP > POOL shows a fake pool with the
// store key pool_fake (pool_page.c, host only).
#include "tfn_pool.h"
__attribute__((weak)) void tfn_pool_init(void) {}
__attribute__((weak)) void tfn_pool_register(void *server) { (void)server; }
__attribute__((weak)) void con_pool(char *cmd) { (void)cmd; }
__attribute__((weak)) bool tfn_pool_on(void) { return false; }
__attribute__((weak)) bool tfn_pool_strict(void) { return false; }
__attribute__((weak)) bool tfn_pool_legacy(void) { return true; }
__attribute__((weak)) tfn_pool_state_t tfn_pool_state(void) { return TFN_POOL_NONE; }
__attribute__((weak)) uint8_t tfn_pool_ptag(void) { return 0; }
__attribute__((weak)) const char *tfn_pool_name(void) { return ""; }
__attribute__((weak)) int tfn_pool_my_role(void) { return -1; }
__attribute__((weak)) uint32_t tfn_pool_gen(void) { return 0; }
__attribute__((weak)) bool tfn_pool_plain_beacon(void) { return true; }
__attribute__((weak)) int tfn_pool_member(const uint8_t mac[6]) { (void)mac; return -1; }
__attribute__((weak)) int tfn_pool_role(int slot) { (void)slot; return -1; }
__attribute__((weak)) int tfn_pool_members(void) { return 0; }
__attribute__((weak)) bool tfn_pool_member_get(int slot, tfn_pool_member_t *out) { (void)slot; memset(out, 0, sizeof *out); return false; }
__attribute__((weak)) int tfn_pool_frame_seal(uint8_t *frame, int len, int cap) { (void)frame; (void)len; (void)cap; return -1; }
__attribute__((weak)) int tfn_pool_frame_open(const uint8_t mac[6], const uint8_t *frame, int len) { (void)mac; (void)frame; (void)len; return TFN_POOL_DROP_OTHER; }
__attribute__((weak)) int tfn_pool_udp_seal(uint8_t *pkt, int len, int cap) { (void)pkt; (void)len; (void)cap; return -1; }
__attribute__((weak)) int tfn_pool_udp_open(const uint8_t mac[6], const uint8_t *pkt, int len) { (void)mac; (void)pkt; (void)len; return TFN_POOL_DROP_OTHER; }
__attribute__((weak)) void tfn_pool_counts(uint32_t out[TFN_POOL_CNT_N]) { memset(out, 0, TFN_POOL_CNT_N * sizeof out[0]); }
__attribute__((weak)) bool tfn_pool_beacon_seal(const uint8_t *plain, int n, uint8_t *out, int *on) { (void)plain; (void)n; (void)out; *on = 0; return false; }
__attribute__((weak)) bool tfn_pool_beacon_open(const uint8_t *pkt, int n, uint8_t *plain, int *pn) { (void)pkt; (void)n; (void)plain; *pn = 0; return false; }
__attribute__((weak)) int tfn_pool_join_beacon(uint8_t *out, int cap) { (void)out; (void)cap; return 0; }
__attribute__((weak)) bool tfn_pool_web_ok(const char *method, const char *uri, const char *hdr) { (void)method; (void)uri; (void)hdr; return false; }
__attribute__((weak)) bool tfn_pool_req_mgr(void *req) { (void)req; return false; }
__attribute__((weak)) bool tfn_pool_mgr_ok(void *req) { (void)req; return true; }
__attribute__((weak)) bool tfn_pool_join_start(void) { return false; }
__attribute__((weak)) void tfn_pool_join_stop(void) {}
__attribute__((weak)) tfn_pool_join_t tfn_pool_join_step(void) { return TFN_POOL_JOIN_OFF; }
__attribute__((weak)) int tfn_pool_join_left_s(void) { return 0; }
__attribute__((weak)) const char *tfn_pool_join_why(void) { return ""; }
__attribute__((weak)) bool tfn_pool_join_sas(char digits[TFN_POOL_SAS_LEN], char mgr[17]) { digits[0] = 0; mgr[0] = 0; return false; }
__attribute__((weak)) bool tfn_pool_join_allow(const char *digits) { (void)digits; return false; }
__attribute__((weak)) void tfn_pool_join_no(void) {}
__attribute__((weak)) bool tfn_pool_leave(bool by_screen) { (void)by_screen; return false; }
// the source of the store (main/tfn_sstore.c is board code): the setting stays in memory; the pc knows no manager
#include "tfn_sstore.h"
#include "tfn_defaults.h"
static int s_stub_ssrc = TFN_STORE_SOURCE_DEFAULT;
__attribute__((weak)) int tfn_sstore_mode(void) { return s_stub_ssrc; }
__attribute__((weak)) const char *tfn_sstore_url(void) { return TFN_STATIC_STORE_URL; }
__attribute__((weak)) bool tfn_sstore_active(void) { return s_stub_ssrc != TFN_SS_MGR; }
__attribute__((weak)) bool tfn_sstore_set(int mode, const char *url, char *msg, int len) { (void)url; if (mode < TFN_SS_AUTO || mode > TFN_SS_STATIC) { if (len) snprintf(msg, (size_t)len, "bad source"); return false; } s_stub_ssrc = mode; if (len) snprintf(msg, (size_t)len, "saved"); return true; }
// FILES (app_files.c): no install on the PC
bool tfn_install(const char *dir, int place, char *msg, int len) { (void)dir; (void)place; snprintf(msg, len, "no app install on the pc"); return false; }
bool tfn_store_required(const char *id) { (void)id; return false; }
const char *tfn_store_profile(void) { return "FRIENDS"; }
bool tfn_icon_draw(const uint8_t *b, size_t len, int x, int y, int s) { (void)b; (void)len; (void)x; (void)y; (void)s; return false; }
void tfn_icon_plain(const char *name, int x, int y, int s)
{
    gfx_fill_rect(x + s / 10, y + s / 10, s - s / 5, s - s / 5, rgb(70, 60, 120));
    char t[2] = {name && name[0] ? name[0] : '?', 0};
    const mfont_t *f = s >= 80 ? &MF_N34 : s >= 50 ? &MF_N24 : &MF_N16;
    mf_text_c(f, x + s / 2, y + s / 2 + f->asc / 2, t, rgb(255, 255, 255), 255, 1);
}
// three saved networks (the SAVED page of SETUP: a short, a long and a very long name)
static const char *const SAVED_NETS[] = {"HOME", "OFFICE-5G", "A VERY LONG NETWORK NAME 1234567"};
int wifi_saved_count(void) { return 3; }
const char *wifi_saved_name(int i) { return i >= 0 && i < 3 ? SAVED_NETS[i] : ""; }
bool wifi_saved_pass(const char *ssid, char *pass, int len) { (void)ssid; (void)pass; (void)len; return false; }
void wifi_autoconnect(void) {}

// capture, storage and export
static int16_t *cap_buf;
static uint32_t cap_max, cap_n;
void audio_capture(int16_t *buf, uint32_t max, uint32_t skip) { cap_buf = buf; cap_max = max; cap_n = 0; }
void audio_capture_stop(void) { cap_buf = NULL; }
bool audio_capturing(void) { return cap_buf != NULL; }
uint32_t audio_captured(void) { return cap_n; }
// called by the fuzz loop for each 5 ms block
void sim_capture_block(void)
{
    for (int i = 0; cap_buf && i < AUDIO_BLOCK; i++) {
        if (cap_n >= cap_max) { cap_buf = NULL; break; }
        cap_buf[cap_n++] = (int16_t)i;
    }
}
#include "storage.h"
bool storage_ok(void) { return true; }
#include "export.h"
long n_exports;
void export_next_name(const char *prefix, char *out, int len) { snprintf(out, len, "/data/exports/%s_001", prefix); }
bool export_on_card(void) { return false; }
void export_path(const char *sub, const char *name, bool rd, char *out, int len)
{
    (void)rd;
    snprintf(out, len, "/data/%s/%s", sub, name);
}
bool export_place_ask(void) { return false; }
void export_place_set(bool card) { (void)card; }
bool export_save_async(const char *base, int16_t *samples, uint32_t n, uint32_t rate, midi_t *midi, int bpm)
{
    for (uint32_t i = 0; i < n; i++) (void)samples[i];      // ASan: the whole buffer must be valid
    free(samples);
    if (midi) {
        for (int i = 0; i < midi->n; i++) {
            if ((midi->ev[i].status & 0x0F) > 15 || midi->ev[i].d1 > 127) abort();
        }
        free(midi->ev);
        midi->ev = NULL; midi->n = midi->cap = 0;
    }
    n_exports++;
    return true;
}
int export_progress(void) { return 100; }
bool midi_begin(midi_t *m, int cap) { m->ev = malloc(sizeof(midi_ev_t) * cap); m->n = 0; m->cap = cap; return m->ev != NULL; }
void midi_note(midi_t *m, uint32_t t0, uint32_t t1, int ch, int note, int vel)
{
    if (ch < 0 || ch > 15 || note < 0 || note > 127 || t1 < t0) { printf("bad midi note ch %d note %d\n", ch, note); abort(); }
    if (m->n + 2 <= m->cap) { m->ev[m->n++] = (midi_ev_t){t0, (uint8_t)(0x90 | ch), (uint8_t)note, (uint8_t)vel}; m->ev[m->n++] = (midi_ev_t){t1, (uint8_t)(0x80 | ch), (uint8_t)note, 0}; }
}
void midi_program(midi_t *m, int ch, int p) { if (m->n < m->cap) m->ev[m->n++] = (midi_ev_t){0, (uint8_t)(0xC0 | ch), (uint8_t)p, 0}; }
void midi_end(midi_t *m) { free(m->ev); m->ev = NULL; m->n = m->cap = 0; }

// full voices from any task, the microphone and IMU hits
#include <math.h>
// the start tunes: check the notes (wave, length, volume)
int n_tunes;
void audio_tune(const tune_note_t *notes, int n)
{
    // the real player takes any count and uint16 times (MORSE plays whole words with it)
    if (!notes || n <= 0 || n > 4096) { fprintf(stderr, "bad tune: %d notes\n", n); abort(); }
    for (int i = 0; i < n; i++) {
        uint8_t w = notes[i].wave & ~TUNE_HOLD;
        if (notes[i].dur == 0 || notes[i].vol > 100 ||
            w > W_SINE || (int)notes[i].note + notes[i].bend < 0 || (int)notes[i].note + notes[i].bend > 127) {
            printf("bad tune note %d\n", i);
            abort();
        }
    }
    n_tunes++;
}
// audio_alert (the alarm through the mute): the same checks of the notes
void audio_alert(const tune_note_t *notes, int n, int vol) { (void)vol; audio_tune(notes, n); }
void audio_tune_stop(void) {}
// the TIA voice: check each call (the channel, the steps and their registers), and keep the last call of each
// channel for the tests
int n_tia, n_tia_off;
const tia_step_t *sim_tia_steps[2];
int sim_tia_n[2], sim_tia_times[2];
uint32_t sim_tia_skip[2];
static void tia_check(int ch, const tia_step_t *s, int n)
{
    if (ch < 0 || ch > 1 || !s || n <= 0 || n > 4096) { fprintf(stderr, "bad TIA call: ch %d, %d steps\n", ch, n); abort(); }
    for (int i = 0; i < n; i++)
        if (s[i].audc > 15 || s[i].audf > 31 || s[i].audv > 15) { fprintf(stderr, "bad TIA step %d\n", i); abort(); }
    volatile uint16_t x = s[n - 1].clocks;     // ASan: the steps must be valid
    (void)x;
}
void audio_tia_play(int ch, const tia_step_t *steps, int n, uint32_t skip)
{
    tia_check(ch, steps, n);
    sim_tia_steps[ch] = steps; sim_tia_n[ch] = n; sim_tia_skip[ch] = skip; sim_tia_times[ch] = 0;
    n_tia++;
}
void audio_tia_loop(int ch, const tia_step_t *steps, int n, int times)
{
    tia_check(ch, steps, n);
    sim_tia_steps[ch] = steps; sim_tia_n[ch] = n; sim_tia_skip[ch] = 0; sim_tia_times[ch] = times;
    n_tia++;
}
void audio_tia_off(void) { n_tia_off++; sim_tia_steps[0] = sim_tia_steps[1] = NULL; }
void audio_play(const audio_voice_t *v)
{
    if (sim_voice_hook) sim_voice_hook(v);
    // (a looping sample can play for any time: an ambience)
    if (v->f0 != v->f0 || v->f0 < 0 || v->f0 > 20000 || v->dur == 0 ||
        (v->dur > 30 * AUDIO_SAMPLE_RATE && !(v->wave == W_SAMPLE && v->loop))) {
        printf("bad audio_play f=%f dur=%u\n", v->f0, (unsigned)v->dur);
        abort();
    }
    if (v->wave == W_SAMPLE && v->sample && v->sample_len) {
        volatile int16_t x = v->sample[v->sample_len - 1];     // ASan: the sample must be valid
        (void)x;
    }
    n_voices++;
}
float audio_midi_hz(float note) { return 440.0f * powf(2.0f, (note - 69.0f) / 12.0f); }
float sim_mic;                  // mic level 0..1 for tests
static bool mic_on;
static int16_t *rec_buf;
static uint32_t rec_max, rec_n;
bool audio_mic_start(void) { mic_on = true; return true; }
void audio_mic_stop(void) { mic_on = false; rec_buf = NULL; }
bool audio_mic_on(void) { return mic_on; }
float audio_mic_level(void) { return mic_on ? sim_mic : 0; }
void audio_mic_record(int16_t *buf, uint32_t max) { rec_buf = buf; rec_max = max; rec_n = 0; }
void audio_mic_record_stop(void) { rec_buf = NULL; }
bool audio_mic_recording(void) { return rec_buf != NULL; }
uint32_t audio_mic_recorded(void) { return rec_n; }
// the microphone tap and the voice stream (WALKIE): a test calls sim_mic_tap; the stream counts
void (*sim_mic_tap)(const int16_t *s, int n);
int sim_stream_fill, sim_stream_total, sim_stream_peak;
void audio_mic_tap(void (*cb)(const int16_t *s, int n)) { sim_mic_tap = cb; }
void audio_mic_web(void (*cb)(const int16_t *s, int n)) { (void)cb; }
void audio_out_tap(void (*cb)(const int16_t *s, int n)) { (void)cb; }
void audio_speaker_off(bool off) { (void)off; }
bool web_audio_active(void) { return false; }
// WALKIE: the LAN and the system layer (weak: a test can give its own)
#include "lan.h"
__attribute__((weak)) int lan_peers(lan_peer_t *out, int max) { (void)out; (void)max; return 0; }
__attribute__((weak)) bool lan_send_call(const uint8_t to[6], uint8_t op) { (void)to; (void)op; return false; }
__attribute__((weak)) bool lan_peer_deep(const uint8_t mac[6]) { (void)mac; return false; }
__attribute__((weak)) void lan_set_call_rx(void (*cb)(const uint8_t from[6], uint8_t op)) { (void)cb; }
__attribute__((weak)) void sysui_ask(const char *t, const char *l, const char *y, const char *n, int s, void (*d)(int)) { (void)t; (void)l; (void)y; (void)n; (void)s; (void)d; }
__attribute__((weak)) void sysui_ask_close(void) {}
__attribute__((weak)) void sysui_message(const char *utf8, int secs) { (void)utf8; (void)secs; }
__attribute__((weak)) void sysui_ask_closable(void) {}
__attribute__((weak)) void sysui_ask_finger(void) {}
// the keyboard (HOME 2.0 RENAME in app_menu.c): weak stubs for the tests that do not link main/kbd.c
#include "kbd.h"
__attribute__((weak)) void kbd_open(const char *t, char *b, int m, bool s) { (void)t; (void)b; (void)m; (void)s; }
__attribute__((weak)) void kbd_set_ok_question(const char *q) { (void)q; }
__attribute__((weak)) int kbd_input(const input_event_t *ev) { (void)ev; return KBD_CANCEL; }
__attribute__((weak)) void kbd_render(int64_t now) { (void)now; }
__attribute__((weak)) bool sysui_ask_back(void) { return false; }
__attribute__((weak)) bool sysui_active(void) { return false; }
// the settings panel (round 3, stage B): the items of the app and the opens (weak; the real sysui.c wins)
#include "sysui.h"
__attribute__((weak)) const pitems_t *sim_panel_items;
__attribute__((weak)) int sim_panel_opens;
__attribute__((weak)) void sysui_panel_items(const pitems_t *set) { sim_panel_items = set; }
__attribute__((weak)) void sysui_panel_open(void) { sim_panel_opens++; }
__attribute__((weak)) bool sysui_panel_on(void) { return false; }
// Home Assistant (ha.c): 6 test entities of different kinds, and their states
#include "ha.h"
static const char *const SIM_HA[6][3] = {
    {"light.living_room", "Living Room Light", "on"}, {"switch.heater", "Heater", "off"},
    {"scene.evening", "Evening", "scening"}, {"cover.blind", "Bedroom Blind", "open"},
    {"climate.ac", "Air Conditioner", "cool"}, {"sensor.outside_temperature", "Outside Temperature", "23.4"},
};
void ha_init(void) {}
const char *ha_url(void) { return "http://ha.test:8123"; }
bool ha_token_set(void) { return true; }
bool ha_ready(void) { return true; }
bool ha_set_config(const char *url, const char *token) { (void)url; (void)token; return true; }
int ha_items(ha_item_t *out, int max)
{
    int n = max < 6 ? max : 6;
    for (int i = 0; i < n; i++) {
        snprintf(out[i].id, HA_ID_LEN, "%s", SIM_HA[i][0]);
        snprintf(out[i].name, HA_NAME_LEN, "%s", SIM_HA[i][1]);
    }
    return n;
}
void ha_set_items(const ha_item_t *items, int n) { (void)items; (void)n; }
int ha_get_api(char *out, int max) { snprintf(out, max, "{\"message\":\"API running.\"}"); return 200; }
int sim_ha_calls;
int ha_template(const char *tpl, char *out, int max)
{
    if (strstr(tpl, "hvac_modes")) {           // the lists of a climate
        snprintf(out, max, "cool,heat,dry,fan_only,auto,off|auto,low,medium,high,turbo|off,on|auto|off|16|32|None\n");
        return 200;
    }
    int k = 0;
    for (int i = 0; i < 6 && k < max - 120; i++)
        k += snprintf(out + k, max - k, "%s|%s|%s|%s|%s|%s|%s\n", SIM_HA[i][0], SIM_HA[i][2], i == 0 ? "200" : "None",
                      i == 4 ? "24.5" : "None", i == 4 ? "22" : "None", i == 3 ? "60" : "None", i == 5 ? "C" : "None");
    return 200;
}
int ha_call(const char *domain, const char *service, const char *entity, const char *extra_json)
{
    (void)domain; (void)service; (void)entity; (void)extra_json;
    sim_ha_calls++;
    return 200;
}
void ha_clean(char *s) { (void)s; }
// STOCKS (stocks.c): sim_stk_mode 0 no key, 1 no stocks, 2 closed (a Sunday, 05:30 UTC), 3 open (Mon 11:00 NY),
// 4 pre-market with a trade, 5 after hours with no trade, 6 a fault; the time rules are the real stocks_time.c
#include "stocks.h"
#if CONFIG_TFN_EXTRA_STOCKS  // (with the extra off, stocks.h has the empty inline hooks)
int sim_stk_mode = 2;
static const struct { const char *s, *n; float c, d, dp, pc; } SIM_STK[3] = {
    {"AAPL", "Apple Inc", 343.46f, 1.46f, 0.4269f, 342.0f}, {"NVDA", "NVIDIA Corp", 1412.18f, -18.2f, -1.2725f, 1430.38f},
    {"SPY", "State Street SPDR S&P 500 ETF Trust", 763.25f, -8.54f, -1.1065f, 771.79f},
};
static int64_t sim_stk_t(void)
{
    static bool env;
    if (!env) { env = true; if (getenv("SIM_STK")) sim_stk_mode = atoi(getenv("SIM_STK")); }      // (the picture tools)
    // a Sunday 05:30 UTC; a Monday 15:00 UTC (11:00 NY), 10:00 UTC (06:00 NY), 21:00 UTC (17:00 NY)
    static const int64_t T[9] = {0, 0, 1792301400, 1792422000, 1792404000, 1792443600, 1792422000, 1792422000, 1792422000};
    return T[sim_stk_mode % 9];
}
void stocks_init(void) {}
void stocks_live(void) {}
void stocks_stop(void) {}
int64_t stocks_now(void) { return sim_stk_t(); }
int stocks_get(stk_quote_t *out, stk_state_t *st)
{
    const int n = sim_stk_mode >= 2 ? 3 : 0;
    const int64_t now = sim_stk_t();
    for (int i = 0; i < n && out; i++) {
        stk_quote_t *q = &out[i];
        memset(q, 0, sizeof *q);
        snprintf(q->sym, sizeof q->sym, "%s", SIM_STK[i].s);
        snprintf(q->name, sizeof q->name, "%s", SIM_STK[i].n);
        q->c = SIM_STK[i].c; q->d = SIM_STK[i].d; q->dp = SIM_STK[i].dp; q->pc = SIM_STK[i].pc;
        q->hi = q->c * 1.012f; q->lo = q->c * 0.991f;
        q->qt = sim_stk_mode == 3 || sim_stk_mode >= 6 ? now - 30 : 1792180800;
        if (sim_stk_mode == 4) { q->ext = q->c * (i == 1 ? 0.982f : 1.007f); q->ext_t = now - 40; }
        if (i == 0) { q->al_lo = 330; q->al_hi = 350; q->sound = 1; q->ref = 343.46f; }
        if (i == 0 && sim_stk_mode == 8) { q->fired = STK_AL_ABOVE; q->fired_t = now - 600; q->fired_p = 351.2f; q->fired_lim = 350; q->c = 351.2f; }
        if (sim_stk_mode == 3 || sim_stk_mode == 4 || sim_stk_mode >= 7) {
            q->nspark = 60;
            for (int k = 0; k < 60; k++) q->spark[k] = q->pc + (q->c - q->pc) * (float)k / 59.0f + q->c * 0.004f * sinf((float)k * 0.4f + (float)i);
        }
    }
    if (st) {
        memset(st, 0, sizeof *st);
        st->n = n;
        st->key_set = sim_stk_mode >= 1;
        st->wifi = true;
        st->now = now;
        st->updated = now - 20;
        st->interval_min = 1;
        if (now) {
            st->session = stk_session(now, &st->sess_start, &st->sess_end);
            st->next_open = stk_next_open(now);
        }
        if (sim_stk_mode == 6) snprintf(st->err, sizeof st->err, "Finnhub: too many requests (60 a minute)");
        snprintf(st->tz, sizeof st->tz, "IST-2IDT,M3.4.4/26,M10.5.0");
    }
    return n;
}
// the alerts: AAPL has 330 - 350 (sound on); SIM_STK 7 = an alert of AAPL above 350 waits for the app (once); 8 = AAPL
// crossed its limit (the offer); the editor and the offers call the real stocks_alert.c
int sim_stk_alert_sets;
bool stocks_set_alert(const char *sym, float lo, float hi, bool sound, char *msg, int mmax)
{
    (void)sym; (void)sound;
    if (lo > 0 && hi > 0 && lo >= hi) { snprintf(msg, mmax, "the low limit must be under the high limit"); return false; }
    sim_stk_alert_sets++;
    snprintf(msg, mmax, "saved");
    return true;
}
bool stocks_keep(const char *sym) { (void)sym; return true; }
bool stocks_take_alert(stk_alert_t *a)
{
    static bool given;
    if (sim_stk_mode != 7 || given) return false;
    given = true;
    memset(a, 0, sizeof *a);
    snprintf(a->sym, sizeof a->sym, "AAPL");
    a->kind = STK_AL_ABOVE; a->sound = 1; a->lim = 350; a->price = 351.2f; a->dp = 2.31f; a->t = sim_stk_t();
    return true;
}
float stocks_price(const stk_quote_t *q, const stk_state_t *st)
{
    if (st->session == STK_OPEN) return q->qt ? q->c : 0;
    if (st->session == STK_PRE || st->session == STK_POST) return q->ext > 0 && q->ext_t >= st->sess_start ? q->ext : 0;
    return q->ext > 0 && q->ext_t > q->qt ? q->ext : q->c;
}
void stocks_chime(int kind) { (void)kind; }
bool stocks_test_alert(const char *sym) { (void)sym; return true; }
bool stocks_set_key(const char *key, char *msg, int mmax) { (void)key; snprintf(msg, mmax, "ok"); return true; }
bool stocks_add(const char *sym, char *msg, int mmax) { (void)sym; snprintf(msg, mmax, "ok"); return true; }
bool stocks_del(const char *sym) { (void)sym; return true; }
bool stocks_order(const char *csv) { (void)csv; return true; }
bool stocks_set_interval(int m) { (void)m; return true; }
bool stocks_set_tz(const char *p) { (void)p; return true; }
int stocks_json(char *out, int max) { return snprintf(out, max, "{}"); }
#endif
void audio_stream_write(const int16_t *s, int n)
{
    for (int i = 0; i < n; i++) if (abs(s[i]) > sim_stream_peak) sim_stream_peak = abs(s[i]);
    sim_stream_fill += n;
    sim_stream_total += n;
}
int audio_stream_fill(void) { return sim_stream_fill; }
void audio_stream_clear(void) { sim_stream_fill = 0; }
void audio_stream_gain(int q8) { (void)q8; }
// called by a test for each 5 ms block: the mic writes 120 samples
void sim_mic_block(void)
{
    for (int i = 0; rec_buf && i < AUDIO_BLOCK; i++) {
        if (rec_n >= rec_max) { rec_buf = NULL; break; }
        rec_buf[rec_n++] = (int16_t)((rand() % 2001 - 1000) * sim_mic * 20);
    }
}
imu_hit_t sim_hits[16];
int sim_nhits;
bool imu_pop_hit(imu_hit_t *h)
{
    if (sim_nhits <= 0) return false;
    *h = sim_hits[0];
    for (int i = 1; i < sim_nhits; i++) sim_hits[i - 1] = sim_hits[i];
    sim_nhits--;
    return true;
}
void imu_hits_clear(void) { sim_nhits = 0; }
void sim_add_hit(float x, float y, float z, float g)
{
    if (sim_nhits < 16) sim_hits[sim_nhits++] = (imu_hit_t){x, y, z, g, 0};
}
imu_snap_t sim_snaps[8];
int sim_nsnaps;
bool imu_pop_snap(imu_snap_t *s)
{
    if (sim_nsnaps <= 0) return false;
    *s = sim_snaps[0];
    for (int i = 1; i < sim_nsnaps; i++) sim_snaps[i - 1] = sim_snaps[i];
    sim_nsnaps--;
    return true;
}
void imu_snaps_clear(void) { sim_nsnaps = 0; }
void sim_add_snap(float x, float y, float v)
{
    if (sim_nsnaps < 8) sim_snaps[sim_nsnaps++] = (imu_snap_t){x, y, v, 0};
}

// the two end buttons (two-player games)
#include "input.h"
input_end_t sim_ends[2];
bool sim_multi = true;
void input_ends(input_end_t out[2]) { out[0] = sim_ends[0]; out[1] = sim_ends[1]; }
bool input_multitouch(void) { return sim_multi; }
// the wheel (BOARD_WHEEL): tests add steps and pushes
int sim_wheel_steps, sim_wheel_presses;
bool sim_wheel_down;
bool input_has_wheel(void) { return BOARD_WHEEL; }
int input_wheel(void) { int d = sim_wheel_steps; sim_wheel_steps = 0; return d; }
int input_wheel_presses(void) { int n = sim_wheel_presses; sim_wheel_presses = 0; return n; }
bool input_wheel_down(void) { return sim_wheel_down; }
void input_wheel_press_add(void) { sim_wheel_presses++; }
void input_wheel_set_down(bool d) { sim_wheel_down = d; }
// tests: press or release physical end e (0 = top = the left player in ORIENT_LAND_A) at time t
void sim_end(int e, bool down, int64_t t)
{
    input_end_t *b = &sim_ends[e & 1];
    if (down && !b->down) { b->down = true; b->down_ms = t; b->presses++; }
    else if (!down && b->down) { b->down = false; b->up_ms = t; }
}

// all touch points (the gamepad) and the BLE gamepad core
int16_t sim_pt_x[INPUT_MAX_POINTS], sim_pt_y[INPUT_MAX_POINTS];
int sim_npt;
int input_points(int16_t *x, int16_t *y, int max)
{
    int n = sim_npt < max ? sim_npt : max;
    for (int i = 0; i < n; i++) { x[i] = sim_pt_x[i]; y[i] = sim_pt_y[i]; }
    return n;
}
#include "bt_pad.h"
static char sim_pad_name[30] = "TFN PAD";
long sim_pad_reports;
bool bt_pad_start(void) { return true; }
void bt_pad_stop(void) {}
void bt_pad_tick(int64_t now_ms) { (void)now_ms; }
pad_state_t bt_pad_state(void) { return PAD_READY; }
const char *bt_pad_name(void) { return sim_pad_name; }
void bt_pad_set_name(const char *n) { if (n && n[0]) snprintf(sim_pad_name, sizeof(sim_pad_name), "%s", n); }
void bt_pad_send(const pad_report_t *r)
{
    if (r->hat > 8) abort();
    sim_pad_reports++;
}
uint32_t bt_pad_sent(void) { return (uint32_t)sim_pad_reports; }
void bt_pad_forget(void) {}
// BT KEYS: the keyboard, the media keys, the mouse (the host tests count them)
long sim_kbd_reports, sim_mouse_moves;
void bt_kbd_report(uint8_t mods, const uint8_t keys[6]) { (void)mods; (void)keys; sim_kbd_reports++; }
void bt_kbd_tap(uint8_t mods, uint8_t key) { (void)mods; (void)key; sim_kbd_reports += 2; }
int bt_kbd_type(const char *text) { int n = 0; while (text && text[n]) n++; sim_kbd_reports += 2 * n; return n; }
uint8_t bt_kbd_leds(void) { return 0; }
void bt_media_tap(uint16_t usage) { (void)usage; }
void bt_mouse_move(int dx, int dy, int wheel) { (void)dx; (void)dy; (void)wheel; sim_mouse_moves++; }
void bt_mouse_buttons(uint8_t buttons) { (void)buttons; }
bool bt_pad_boot(void) { return false; }

// hotspot, web address, QR codes, free storage (SETUP)
const char *const HOTSPOT_MODE_NAMES[HOTSPOT_MODES] = {"OFF", "AUTO", "ON"};
static hotspot_mode_t sim_hs_mode = HOTSPOT_AUTO;
static bool sim_hs_on;
void wifi_hotspot_set_mode(hotspot_mode_t m) { if (m < HOTSPOT_MODES) sim_hs_mode = m; }
hotspot_mode_t wifi_hotspot_mode(void) { return sim_hs_mode; }
void wifi_hotspot_start(void) { sim_hs_on = true; }
void wifi_hotspot_stop(void) { sim_hs_on = false; }
bool wifi_hotspot_on(void) { return sim_hs_on; }
const char *wifi_hotspot_ssid(void) { return "TFN-TEST"; }
const char *wifi_hotspot_ip(void) { return "192.168.4.1"; }
int wifi_hotspot_clients(void) { return sim_hs_on ? 1 : 0; }
int wifi_hotspot_left_s(void) { return -1; }
const char *wifi_web_ip(void) { return sim_hs_on ? "192.168.4.1" : NULL; }
#include "qr.h"
int qr_side(int px, const char *text) { return text && strlen(text) < 120 ? 33 * px : 0; }
int qr_draw(int x, int y, int px, const char *text)
{
    int s = qr_side(px, text);
    if (s) {
        extern void gfx_fill_rect(int, int, int, int, uint16_t);
        gfx_fill_rect(x, y, s, s, 0xFFFF);
    }
    return s;
}
uint64_t storage_free(void) { return 12u * 1024 * 1024; }

// files: a small store in memory (the old store.c), so that save/load paths run
#include "storage.h"
#define SIM_FILES 16
static struct { char path[96]; unsigned char *data; size_t len; } sim_files[SIM_FILES];
bool storage_write(const char *path, const void *data, size_t len)
{
    int free_i = -1;
    for (int i = 0; i < SIM_FILES; i++) {
        if (sim_files[i].data && !strcmp(sim_files[i].path, path)) { free_i = i; break; }
        if (!sim_files[i].data && free_i < 0) free_i = i;
    }
    if (free_i < 0 || len > 1 << 20) return false;
    free(sim_files[free_i].data);
    sim_files[free_i].data = malloc(len ? len : 1);
    memcpy(sim_files[free_i].data, data, len);
    sim_files[free_i].len = len;
    snprintf(sim_files[free_i].path, sizeof(sim_files[free_i].path), "%s", path);
    return true;
}
FILE *storage_open_read(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (f) return f;
    char tmp[160];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    return fopen(tmp, "rb");
}
bool storage_read(const char *path, void *data, size_t max, size_t *len)
{
    for (int i = 0; i < SIM_FILES; i++) {
        if (sim_files[i].data && !strcmp(sim_files[i].path, path)) {
            size_t n = sim_files[i].len < max ? sim_files[i].len : max;
            memcpy(data, sim_files[i].data, n);
            if (len) *len = n;
            return true;
        }
    }
    return false;
}

// the radio link (MORSE): no radio on the PC; sometimes a random packet comes in
#include "link.h"
#ifndef NO_LINK_STUB
bool link_start(void) { return true; }
void link_stop(void) {}
__attribute__((weak)) bool link_ready(void) { return true; }
void link_quiet(bool quiet) { (void)quiet; }
bool link_quiet_on(void) { return false; }
// power.c (SETUP: POWER OFF)
#include "power.h"
const uint16_t POWER_OFF_MIN[POWER_OFF_CHOICES] = {0, 15, 30, 60, 120, 240};
const char *const POWER_OFF_NAMES[POWER_OFF_CHOICES] = {"NEVER", "15 MIN", "30 MIN", "1 HOUR", "2 HOURS", "4 HOURS"};
static int s_pwoff;
int power_off_choice(void) { return s_pwoff; }
void power_idle1_hold(bool hold) { (void)hold; }  // (the STANDBY fetch; power.c is not in the host build)
void power_set_off_choice(int i) { if (i >= 0 && i < POWER_OFF_CHOICES) s_pwoff = i; }
// (SETUP: DEEP IDLE; the host has no DEEP IDLE and no power test switches)
const uint16_t POWER_DEEP_S[POWER_DEEP_CHOICES] = {0, 10, 30, 60};
const char *const POWER_DEEP_NAMES[POWER_DEEP_CHOICES] = {"OFF", "10 S", "30 S", "60 S"};
static int s_pwdeep = 3;
int power_deep_choice(void) { return s_pwdeep; }
void power_set_deep_choice(int i) { if (i >= 0 && i < POWER_DEEP_CHOICES) s_pwdeep = i; }
__attribute__((weak)) bool power_deep_now(void) { return false; }
__attribute__((weak)) int power_x(void) { return 0; }
void link_stats(uint32_t *sent, uint32_t *failed) { *sent = 0; *failed = 0; }
bool link_send(uint8_t app, uint8_t kind, const void *data, int len) { return len >= 0 && len <= LINK_DATA_MAX; }
bool link_recv(link_msg_t *m)
{
    if (rand() % 20) return false;
    memset(m, 0, sizeof(*m));
    for (int i = 0; i < 6; i++) m->mac[i] = (uint8_t)(i == 5 ? rand() % 3 : 0x80);
    m->app = rand() % 4 ? (uint8_t)"MPFZRET"[rand() % 7] : (uint8_t)rand();
    m->kind = (uint8_t)(rand() % 6);
    m->len = (uint8_t)(rand() % (LINK_DATA_MAX + 1));
    for (int i = 0; i < m->len; i++) m->data[i] = (uint8_t)rand();
    m->rssi = (int8_t)(-30 - rand() % 60);
    return true;
}
bool link_set_channel(int ch) { return ch >= 1 && ch <= 13; }
bool link_force_channel(int ch) { return ch >= 1 && ch <= 13; }
void link_meet_learn(int kind) { (void)kind; }
int link_channel(void) { return 6; }
void link_my_mac(uint8_t mac[6]) { for (int i = 0; i < 6; i++) mac[i] = (uint8_t)(0x10 + i); }
#endif
// the raw frames (a TFN COMPANION): no radio; a test gives its own versions (a fake radio)
__attribute__((weak)) bool link_raw_open(uint8_t m0, uint8_t m1) { (void)m0; (void)m1; return true; }
__attribute__((weak)) void link_raw_close(void) {}
__attribute__((weak)) bool link_raw_send(const uint8_t *mac, const void *d, int n) { (void)mac; (void)d; return n >= 2 && n <= LINK_RAW_MAX; }
__attribute__((weak)) bool link_raw_recv(link_raw_t *m) { (void)m; return false; }

// the battery: a random level, sometimes no battery
#include "board.h"
// the resistance meter: on for the CoreS3 build, a random joint voltage
bool board_ohm_start(void) { return !BOARD_BAR_SCREEN; }
bool board_sd_mount(void) { return false; }
bool board_sd_check(void) { return false; }
bool board_sd_mounted(void) { return false; }
bool board_sd_info(uint32_t *t, uint32_t *f) { (void)t; (void)f; return false; }
void board_ohm_stop(void) {}
bool board_ohm_read(int *mv)
{
    if (BOARD_BAR_SCREEN || rand() % 10 == 0) return false;
    *mv = rand() % 4 ? rand() % 3400 : (rand() % 2 ? 0 : 3300);
    return true;
}
int board_battery(bool *charging, int *mv)
{
    if (charging) *charging = rand() % 2;
    if (mv) *mv = 3300 + rand() % 1000;
    return rand() % 4 ? rand() % 101 : -1;
}
// the temperature of the chip (the Tab5, SETUP): a still value for the pictures
__attribute__((weak)) float board_chip_temp(void) { return 47.5f; }

void imu_gyro_sum(float a[3]) { a[0] = 0; a[1] = 0; a[2] = sim_dial; }

// the alarms of the system (main/alarm.c): none on the PC (a test can give a fake service of its own)
#include "alarm.h"
__attribute__((weak)) int alarm_get(alarm_t *o, int m) { (void)o; (void)m; return 0; }
__attribute__((weak)) bool alarm_set(const alarm_t *a, int n) { (void)a; (void)n; return true; }
__attribute__((weak)) void alarm_tz_get(char *o, int m) { snprintf(o, (size_t)m, "IST-2IDT,M3.4.4/26,M10.5.0"); }
__attribute__((weak)) bool alarm_tz_set(const char *p) { (void)p; return true; }
__attribute__((weak)) int alarm_ringing(void) { return -1; }
__attribute__((weak)) void alarm_stop(void) {}
__attribute__((weak)) void alarm_snooze(void) {}
__attribute__((weak)) int64_t alarm_next(int *i) { if (i) *i = -1; return 0; }
__attribute__((weak)) void alarm_preview(int s, int v) { (void)s; (void)v; }
__attribute__((weak)) int timer_get(ctimer_t *o, int m) { memset(o, 0, sizeof(ctimer_t) * (size_t)(m < TIMER_MAX ? m : TIMER_MAX)); return m < TIMER_MAX ? m : TIMER_MAX; }
__attribute__((weak)) bool timer_set(const ctimer_t *t, int n) { (void)t; (void)n; return true; }
__attribute__((weak)) bool clock_time_ok(void) { return true; }
__attribute__((weak)) void clock_sync(void) {}
__attribute__((weak)) int clock_source(void) { return 2; }
__attribute__((weak)) void alarm_init(void) {}
__attribute__((weak)) bool alarm_voice_on(void) { return false; }
__attribute__((weak)) void alarm_voice_set(bool on) { (void)on; }
#include "oref.h"
#if CONFIG_TFN_EXTRA_OREF  // (with the extra off, oref.h has the empty inline hooks)
__attribute__((weak)) void oref_init(void) {}
__attribute__((weak)) void oref_tick(int64_t now) { (void)now; }
__attribute__((weak)) void oref_log(void) {}
#endif
__attribute__((weak)) void power_oref_hold(bool on) { (void)on; }

// the names that main.c calls (the simulator compiles main.c): no native radio, no match, no link use on the PC.
// They are weak, because an app set with duel.c has the real duel_state.
#include "rf_native.h"
#include "duel.h"
__attribute__((weak)) void rfn_early(void) {}
__attribute__((weak)) bool rfn_radio_boot(void) { return false; }
__attribute__((weak)) const char *rfn_boot_app(void) { return ""; }
__attribute__((weak)) int duel_state(void) { return DUEL_OFF; }
__attribute__((weak)) bool link_in_use(void) { return false; }

// the touch log of TOUCH LAB (tfn/board.h): no reports on the PC
__attribute__((weak)) void board_touch_log(bool on) { (void)on; }
__attribute__((weak)) int board_touch_reports(touch_rep_t *out, int max) { (void)out; (void)max; return 0; }
__attribute__((weak)) int board_touch_read_len_get(void) { return 0; }
