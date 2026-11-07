// FRIENDS (see friends.h): the list in NVS (one blob), who is near, the rule of the lobbies.

#include <stdio.h>
#include "arc_mac.h"
#include <string.h>
#include "esp_timer.h"
#ifdef ESP_PLATFORM
#include "esp_mac.h"
#endif
#include "friends.h"
#include "link.h"
#include "tfn_pool_core.h"  // (only the pure rules of the core)
#include "app.h"
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "nvs.h"
#include "lan.h"
#else  // (the host tests: one task)
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(m)        ((void)(m))
#define portEXIT_CRITICAL(m)         ((void)(m))
#endif

typedef struct {
    uint8_t ver, n;
    char my[FRIEND_NAME];
    friend_t f[FRIENDS_MAX];
} store_t;
// The NVS: "list" = the layout of the older firmware (version 1, the first 8 friends: a board that goes
// back to an older firmware keeps them), "more" = the friends 9 to 16.
typedef struct {
    uint8_t ver, n;
    char my[FRIEND_NAME];
    friend_t f[FRIENDS_OLD_MAX];
} store_v1_t;
typedef struct {
    uint8_t n;
    friend_t f[FRIENDS_MAX - FRIENDS_OLD_MAX];
} store_more_t;
_Static_assert(sizeof(store_v1_t) == 2 + 16 + 8 * 22, "the blob of an older firmware");

static store_t s_st;
static volatile int64_t s_seen[FRIENDS_MAX];
static volatile int64_t s_game[FRIENDS_MAX];  // the last packet of an app that this board sends for (friends_in_game)
static volatile int8_t s_rssi[FRIENDS_MAX];
static int s_chosen = -1;
static int64_t s_beacon_at;
static struct {
    uint8_t mac[6];
    char name[FRIEND_NAME];
    volatile int64_t seen;
} s_new[FRIENDS_NEW_MAX];

#define FR_APP    FRIENDS_LINK_APP  // (friends_page.c: kinds 1, 2)
#define FR_K_NAME FRIENDS_K_NAME

// The pinned device IDs (friends_pin). The NVS blob "ids" has the MAC in each entry, so that a list that an older
// firmware changed does not give an ID to the wrong friend.
#define FID_KEYED 0x01
typedef struct {
    uint8_t id[8];
    uint8_t flags;  // FID_KEYED
} fid_t;
typedef struct {
    uint8_t mac[6];
    uint8_t id[8];
    uint8_t flags;
} store_id_t;
typedef struct {
    uint8_t ver, n;
    store_id_t e[FRIENDS_MAX];
} store_ids_t;
static fid_t s_fid[FRIENDS_MAX];
_Static_assert((int)FRIENDS_IN_V3 == (int)TFN_POOL_IN_V3 && (int)FRIENDS_IN_PEER == (int)TFN_POOL_IN_PEER &&
                       (int)FRIENDS_IN_WAKE1 == (int)TFN_POOL_IN_WAKE1 &&
                       (int)FRIENDS_IN_WAKE2 == (int)TFN_POOL_IN_WAKE2,
               "the values of the receive rule");
static tfn_pool_legacy_t s_legacy;  // the legacy peers of each app (the WiFi and the lan tasks write it)
static portMUX_TYPE s_legacy_mux = portMUX_INITIALIZER_UNLOCKED;

static void save_ids(void)
{
#ifdef ESP_PLATFORM
    store_ids_t a;
    memset(&a, 0, sizeof a);
    a.ver = 1;
    for (int i = 0; i < s_st.n; i++) {
        if (s_fid[i].flags & FID_KEYED) {
            memcpy(a.e[a.n].mac, s_st.f[i].mac, 6);
            memcpy(a.e[a.n].id, s_fid[i].id, 8);
            a.e[a.n].flags = s_fid[i].flags;
            a.n++;
        }
    }
    nvs_handle_t h;
    if (nvs_open("friends", NVS_READWRITE, &h) == ESP_OK) {
        if (a.n) {
            nvs_set_blob(h, "ids", &a, sizeof a);
        } else {
            nvs_erase_key(h, "ids");
        }
        nvs_commit(h);
        nvs_close(h);
    }
#endif
}

#ifdef ESP_PLATFORM
static void load_ids(nvs_handle_t h)
{
    store_ids_t a;
    size_t n = sizeof a;
    if (nvs_get_blob(h, "ids", &a, &n) != ESP_OK || n != sizeof a || a.ver != 1 || a.n > FRIENDS_MAX) {
        return;
    }
    for (int k = 0; k < a.n; k++) {
        int i = friends_find(a.e[k].mac);
        if (i >= 0) {
            memcpy(s_fid[i].id, a.e[k].id, 8);
            s_fid[i].flags = a.e[k].flags;
        }
    }
}
#endif

static void save(void)
{
#ifdef ESP_PLATFORM
    store_v1_t a;
    memset(&a, 0, sizeof a);
    a.ver = 1;
    a.n = (uint8_t)(s_st.n < FRIENDS_OLD_MAX ? s_st.n : FRIENDS_OLD_MAX);
    memcpy(a.my, s_st.my, sizeof a.my);
    memcpy(a.f, s_st.f, sizeof a.f);
    store_more_t b;
    memset(&b, 0, sizeof b);
    b.n = (uint8_t)(s_st.n > FRIENDS_OLD_MAX ? s_st.n - FRIENDS_OLD_MAX : 0);
    memcpy(b.f, s_st.f + FRIENDS_OLD_MAX, sizeof b.f);
    nvs_handle_t h;
    if (nvs_open("friends", NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_blob(h, "list", &a, sizeof a);
        if (b.n) {
            nvs_set_blob(h, "more", &b, sizeof b);
        } else {
            nvs_erase_key(h, "more");
        }
        nvs_commit(h);
        nvs_close(h);
    }
#endif
}

void friends_init(void)
{
    memset(&s_st, 0, sizeof s_st);
#ifdef ESP_PLATFORM
    nvs_handle_t h;
    if (nvs_open("friends", NVS_READONLY, &h) == ESP_OK) {
        store_v1_t a;
        size_t n = sizeof a;
        if (nvs_get_blob(h, "list", &a, &n) == ESP_OK && n == sizeof a && a.ver == 1 && a.n <= FRIENDS_OLD_MAX) {
            s_st.n = a.n;
            memcpy(s_st.my, a.my, sizeof a.my);
            memcpy(s_st.f, a.f, sizeof a.f);
            store_more_t b;
            n = sizeof b;
            if (a.n == FRIENDS_OLD_MAX && nvs_get_blob(h, "more", &b, &n) == ESP_OK && n == sizeof b &&
                b.n <= FRIENDS_MAX - FRIENDS_OLD_MAX) {
                memcpy(s_st.f + FRIENDS_OLD_MAX, b.f, sizeof b.f);
                s_st.n = (uint8_t)(FRIENDS_OLD_MAX + b.n);
            }
        }
        memset(s_fid, 0, sizeof s_fid);
        if (s_st.n <= FRIENDS_MAX) {
            load_ids(h);
        }
        nvs_close(h);
    }
#endif
    if (s_st.n > FRIENDS_MAX) {
        s_st.n = 0;
    }
    s_st.ver = 1;
    // the first name: TFN and the end of the MAC address. The first name of an older firmware (ARCADE and the same
    // end) becomes the new one; an own name stays.
    uint8_t mac[6] = {0};
#ifdef ESP_PLATFORM
    arc_mac(mac);  // (the WiFi may be off now)
#else
    link_my_mac(mac);
#endif
    char first_old[16];
    snprintf(first_old, sizeof first_old, "ARCADE %02X%02X", mac[4], mac[5]);
    if (!s_st.my[0] || !strcmp(s_st.my, first_old)) {
        snprintf(s_st.my, sizeof s_st.my, "TFN %02X%02X", mac[4], mac[5]);
    }
    for (int i = 0; i < s_st.n; i++) {
        s_st.f[i].name[FRIEND_NAME - 1] = 0;
    }
}

int friends_count(void)
{
    return s_st.n;
}
const friend_t *friends_get(int i)
{
    return i >= 0 && i < s_st.n ? &s_st.f[i] : NULL;
}
const char *friends_my_name(void)
{
    return s_st.my;
}

void friends_set_my_name(const char *name)
{
    snprintf(s_st.my, sizeof s_st.my, "%s", name && name[0] ? name : "TFN");
    save();
}

int friends_find(const uint8_t mac[6])
{
    for (int i = 0; i < s_st.n; i++) {
        if (!memcmp(s_st.f[i].mac, mac, 6)) {
            return i;
        }
    }
    return -1;
}

bool friends_add(const uint8_t mac[6], const char *name)
{
    int i = friends_find(mac);
    if (i < 0) {
        if (s_st.n >= FRIENDS_MAX) {
            return false;
        }
        i = s_st.n++;
        memcpy(s_st.f[i].mac, mac, 6);
        s_seen[i] = 0;
        s_game[i] = 0;
        memset(&s_fid[i], 0, sizeof s_fid[i]);
    }
    snprintf(s_st.f[i].name, FRIEND_NAME, "%s", name && name[0] ? name : "FRIEND");
    save();
    return true;
}

void friends_remove(int i)
{
    if (i < 0 || i >= s_st.n) {
        return;
    }
    const bool keyed = s_fid[i].flags & FID_KEYED;
    for (int k = i; k < s_st.n - 1; k++) {
        s_st.f[k] = s_st.f[k + 1];
        s_seen[k] = s_seen[k + 1];
        s_game[k] = s_game[k + 1];
        s_rssi[k] = s_rssi[k + 1];
        s_fid[k] = s_fid[k + 1];
    }
    s_st.n--;
    memset(&s_fid[s_st.n], 0, sizeof s_fid[s_st.n]);
    if (s_chosen == i) {
        s_chosen = -1;
    } else if (s_chosen > i) {
        s_chosen--;
    }
    save();
    if (keyed) {
        save_ids();
    }
}

bool friends_pin(const uint8_t mac[6], const uint8_t id[8])
{
    int i = friends_find(mac);
    if (i < 0 || ((s_fid[i].flags & FID_KEYED) && !memcmp(s_fid[i].id, id, 8))) {
        return false;
    }
    memcpy(s_fid[i].id, id, 8);
    s_fid[i].flags |= FID_KEYED;
    save_ids();
    return true;
}

bool friends_keyed(int i, uint8_t id[8])
{
    if (i < 0 || i >= s_st.n || !(s_fid[i].flags & FID_KEYED)) {
        return false;
    }
    if (id) {
        memcpy(id, s_fid[i].id, 8);
    }
    return true;
}

bool friends_pool_take(int in, const uint8_t mac[6], bool pool, bool strict, int role)
{
    const int i = friends_find(mac);
    tfn_pool_peer_t p = {
            .pool = pool,
            .strict = strict,
            .member = role >= 0,
            .manager = role == TFN_POOL_ROLE_MANAGER,
            .is_friend = i >= 0,
            // (a keyed friend that is not in this pool: a board of another pool)
            .keyed_other = i >= 0 && (s_fid[i].flags & FID_KEYED) && role < 0,
    };
    return tfn_pool_take(in, &p);
}

void friends_legacy_note(uint8_t app, uint32_t now_ms)
{
    portENTER_CRITICAL(&s_legacy_mux);
    tfn_pool_legacy_note(&s_legacy, app, now_ms);
    portEXIT_CRITICAL(&s_legacy_mux);
}

bool friends_legacy_on(uint8_t app, uint32_t now_ms)
{
    portENTER_CRITICAL(&s_legacy_mux);
    const bool on = tfn_pool_legacy_on(&s_legacy, app, now_ms);
    portEXIT_CRITICAL(&s_legacy_mux);
    return on;
}

void friends_seen(const uint8_t mac[6], int8_t rssi)
{
    int i = friends_find(mac);
    if (i < 0) {
        return;
    }
    s_seen[i] = esp_timer_get_time() / 1000;
    s_rssi[i] = rssi;
}

void friends_game_seen(const uint8_t mac[6])
{
    int i = friends_find(mac);
    if (i >= 0) {
        s_game[i] = esp_timer_get_time() / 1000;
    }
}

int friends_in_game(int *idx, int max)
{
    int64_t now = esp_timer_get_time() / 1000;
    int n = 0;
    for (int i = 0; i < s_st.n && n < max; i++) {
        if (s_game[i] && now - s_game[i] < FRIENDS_NEAR_MS) {
            idx[n++] = i;
        }
    }
    return n;
}

// THE RADIO INVITE (friends.h): the last invites that came (a friend and an app), and the one invite that waits for
// friends_radio_invite_take (the radio task writes it, the app task takes it)
static struct {
    uint8_t mac[6];
    char app[FRIENDS_APP_NAME];
    uint32_t at;
} s_rinv[4];
static uint8_t s_rinv_mac[6];
static char s_rinv_app[FRIENDS_APP_NAME];
static volatile int s_rinv_new;

// a name beacon of friend mac by radio with the app part (after the 0 of the name): a radio invite
static void radio_invite_in(const uint8_t mac[6], const uint8_t *data, int len)
{
    int n = 0;
    while (n < len && data[n]) {
        n++;
    }
    if (n + 1 >= len) {
        return;  // (a plain beacon: no wait of a lobby)
    }
    char app[FRIENDS_APP_NAME];
    int k = len - n - 1 < FRIENDS_APP_NAME - 1 ? len - n - 1 : FRIENDS_APP_NAME - 1;
    memcpy(app, data + n + 1, (size_t)k);
    app[k] = 0;
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    int slot = 0;
    for (int i = 0; i < 4; i++) {
        if (s_rinv[i].at && !memcmp(s_rinv[i].mac, mac, 6) && !strcmp(s_rinv[i].app, app)) {
            if (now - s_rinv[i].at < FRIENDS_RADIO_GAP_MS) {
                return;  // (one notice for each friend and app in this time)
            }
            slot = i;
            break;
        }
        if (s_rinv[i].at < s_rinv[slot].at) {
            slot = i;  // (else the oldest)
        }
    }
    if (__atomic_load_n(&s_rinv_new, __ATOMIC_ACQUIRE)) {
        return;  // (the app task has not taken the last one yet: the beacon comes again in 1 s)
    }
    memcpy(s_rinv[slot].mac, mac, 6);
    snprintf(s_rinv[slot].app, sizeof s_rinv[slot].app, "%s", app);
    s_rinv[slot].at = now ? now : 1;
    memcpy(s_rinv_mac, mac, 6);
    snprintf(s_rinv_app, sizeof s_rinv_app, "%s", app);
    __atomic_store_n(&s_rinv_new, 1, __ATOMIC_RELEASE);
}

bool friends_radio_invite_take(uint8_t mac[6], char app[FRIENDS_APP_NAME])
{
    if (!__atomic_load_n(&s_rinv_new, __ATOMIC_ACQUIRE)) {
        return false;
    }
    memcpy(mac, s_rinv_mac, 6);
    snprintf(app, FRIENDS_APP_NAME, "%s", s_rinv_app);
    __atomic_store_n(&s_rinv_new, 0, __ATOMIC_RELEASE);
    return true;
}

bool friends_on_packet(const uint8_t mac[6], uint8_t app, uint8_t kind, const uint8_t *data, int len, int8_t rssi)
{
    if (rssi != 0) {
        friends_seen(mac, rssi);  // "near" = the radio only (a LAN packet comes with rssi 0)
    }
    if (app != FR_APP || kind != FR_K_NAME) {
        return false;
    }
    if (friends_find(mac) >= 0) {
        if (rssi != 0) {
            radio_invite_in(mac, data, len);
        }
        return true;
    }
    int64_t now = esp_timer_get_time() / 1000;
    int k = -1;
    for (int i = 0; i < FRIENDS_NEW_MAX && k < 0; i++) {
        if (s_new[i].seen && !memcmp(s_new[i].mac, mac, 6)) {
            k = i;
        }
    }
    for (int i = 0; i < FRIENDS_NEW_MAX && k < 0; i++) {
        if (!s_new[i].seen || now - s_new[i].seen > FRIENDS_NEAR_MS) {
            k = i;
        }
    }
    if (k < 0) {
        return true;
    }
    s_new[k].seen = 0;
    memcpy(s_new[k].mac, mac, 6);
    int n = len < FRIEND_NAME - 1 ? len : FRIEND_NAME - 1;
    memcpy(s_new[k].name, data, (size_t)(n > 0 ? n : 0));
    s_new[k].name[n > 0 ? n : 0] = 0;
    if (!s_new[k].name[0]) {
        snprintf(s_new[k].name, FRIEND_NAME, "A BOARD");
    }
    s_new[k].seen = now;
    return true;
}

void friends_beacon(int64_t now)
{
    if (s_beacon_at && now - s_beacon_at < 1000 && now >= s_beacon_at) {
        return;
    }
    s_beacon_at = now;
    // the name; while a lobby waits also a 0 and the menu name of its app (the radio invite; an older firmware reads
    // the name up to the 0)
    uint8_t b[FRIEND_NAME + FRIENDS_APP_NAME];
    int n = (int)strnlen(s_st.my, FRIEND_NAME - 1);
    memcpy(b, s_st.my, (size_t)n);
    const app_t *a = friends_lobby_waits() ? app_current() : NULL;
    if (a && a->name && strncmp(a->name, "WALKIE", 6) && strncmp(a->name, "TALKIE", 6)) {
        b[n++] = 0;
        const int k = (int)strnlen(a->name, FRIENDS_APP_NAME - 1);
        memcpy(b + n, a->name, (size_t)k);
        n += k;
    }
    link_send(FR_APP, FR_K_NAME, b, n);
}

// friends_lobby_waits: the last call of friends_invite and the start of the wait (ms, 32 bits: the timer task of the
// WiFi reads them while the app task writes them; 0 = none)
static volatile uint32_t s_wait_at, s_wait_since;
static uint32_t lobby_now(void)
{
    const uint32_t t = (uint32_t)(esp_timer_get_time() / 1000);
    return t ? t : 1;
}
bool friends_lobby_waits(void)
{
    const uint32_t at = s_wait_at;
    return at && lobby_now() - at < FRIENDS_WAIT_MS;
}
uint32_t friends_lobby_age(void)
{
    const uint32_t since = s_wait_since;
    return friends_lobby_waits() && since ? lobby_now() - since : 0;
}

void friends_invite(int64_t now)
{
    const uint32_t t = lobby_now();  // (the clock of the app can stop under the panel: not now)
    if (!friends_lobby_waits()) {
        s_wait_since = t;
    }
    s_wait_at = t;
#ifdef ESP_PLATFORM
    static int64_t at;
    if (at && now - at < 5000 && now >= at) {
        return;
    }
    at = now;
    const app_t *a = app_current();
    if (a && a->name && strncmp(a->name, "WALKIE", 6) && strncmp(a->name, "TALKIE", 6)) {
        lan_send_invite(a->name);
    }
#else
    (void)now;
#endif
}

int friends_new_near(friend_new_t *out, int max)
{
    int64_t now = esp_timer_get_time() / 1000;
    int n = 0;
    for (int i = 0; i < FRIENDS_NEW_MAX && n < max; i++) {
        int64_t t = s_new[i].seen;
        if (!t || now - t > FRIENDS_NEAR_MS || friends_find(s_new[i].mac) >= 0) {
            continue;
        }
        memcpy(out[n].mac, s_new[i].mac, 6);
        memcpy(out[n].name, s_new[i].name, FRIEND_NAME);
        out[n].name[FRIEND_NAME - 1] = 0;
        n++;
    }
    return n;
}

int friends_near(int *idx, int max)
{
    int64_t now = esp_timer_get_time() / 1000;
    int n = 0;
    for (int i = 0; i < s_st.n && n < max; i++) {
        if (s_seen[i] && now - s_seen[i] < FRIENDS_NEAR_MS) {
            idx[n++] = i;
        }
    }
    return n;
}

bool friends_accept(const uint8_t mac[6])
{
#ifdef FRIENDS_ANY
    return true;  // (the host tests of the games: any board)
#endif
    int i = friends_find(mac);
    if (i < 0) {
        return false;
    }
    return s_chosen == -1 || s_chosen == i;
}

int friends_hello_list(uint8_t *out)
{
    int n = 0;
    if (s_chosen >= 0) {
        memcpy(out + 1, s_st.f[s_chosen].mac, 6);
        n = 1;
    } else if (s_chosen == -1) {
        // two friends or more in this game: the user picks one first (the list stays empty). Else the friends in the
        // game first, then the others (at most FRIENDS_OLD_MAX: the HELLO buffers of the app files)
        int g[FRIENDS_MAX], ng = friends_in_game(g, FRIENDS_MAX);
        if (ng < 2) {
            bool put[FRIENDS_MAX] = {false};
            for (int k = 0; k < ng; k++) {
                memcpy(out + 1 + 6 * n++, s_st.f[g[k]].mac, 6);
                put[g[k]] = true;
            }
            for (int i = 0; i < s_st.n && n < FRIENDS_OLD_MAX; i++) {
                if (!put[i]) {
                    memcpy(out + 1 + 6 * n++, s_st.f[i].mac, 6);
                }
            }
        }
    }
    out[0] = (uint8_t)n;
    return 1 + 6 * n;
}

bool friends_hello_ok(const uint8_t *list, int len)
{
#ifdef FRIENDS_ANY
    return true;
#endif
    if (len < 1 || len < 1 + 6 * list[0]) {
        return false;
    }
    uint8_t me[6];
    link_my_mac(me);
    for (int i = 0; i < list[0]; i++) {
        if (!memcmp(list + 1 + 6 * i, me, 6)) {
            return true;
        }
    }
    return false;
}

void friends_choose(int i)
{
    s_chosen = i == FRIENDS_NOBODY ? FRIENDS_NOBODY : i >= 0 && i < s_st.n ? i : -1;
}
int friends_chosen(void)
{
    return s_chosen;
}

const char *friends_name_of(const uint8_t mac[6])
{
    int i = friends_find(mac);
    return i >= 0 ? s_st.f[i].name : "A BOARD";
}
