// TFN OS: the icons: the icon files of the apps, their decode, the menu stub of each app, the prefetch task of the Tab5 (main/tfn_app.c has the rest; tfn_app_priv.h).
#include "tfn_app_priv.h"

static const char *TAG = "tfn";

// the icon of each entry: the file bytes in PSRAM (NULL = not read yet), s_icon_bad = no usable icon
uint8_t *tfa_icon[TFN_MAX];
size_t tfa_icon_len[TFN_MAX];
bool tfa_icon_bad[TFN_MAX];
// (The icon files in PSRAM keep ICON_BUDGET at most (the icon used last goes last), and the load of an app frees them
// all. Without that limit, a walk through the menu, the edit mode or a sheet of a group with many tiles can fill the
// PSRAM with about 140 icon files (30-145 KB each), and a big app gets no memory.
// The GAMES page of the Tab5 shows more than 1.5 MB of icons at one time. If the trim freed icons of the screen,
// each frame would read them from the card again, and the menu would make about 1 frame a second. So an icon that was
// drawn in the last ICON_KEEP_MS stays; only the icons that are not on the screen go.)

uint32_t tfa_icon_used[TFN_MAX];  // (the ms time of the last draw)
#if TFA_ICON_TASK
volatile bool tfa_icon_busy[TFN_MAX];  // the prefetch task reads this icon now (the menu draws a plain tile)
volatile uint32_t tfa_icon_gen;        // + 1 at each rescan (a read of the task for an old list goes)
#endif
size_t tfa_icon_bytes;

// ---------------------------------------------------------------- the icons
// icon.bin: "TFI1", u16 size, u16 frames, u16 ms, u16 colors, the palette (colors x u16, the pixel values of g_fb;
// index 0 = no pixel), then for each frame: u32 length and the run-length bytes of size x size indexes (a byte n < 128:
// n + 1 indexes follow; n >= 128: the next index (257 - n) times).
// (The cost of the animated icons: with ONE decoded frame, each icon of the menu would decode its frame again in
// each render (12 icons x 16 K indexes a frame). So each icon has a slot: an icon decodes only when its frame changes,
// 8 times a second.)
dec_t tfa_dec[DEC_SLOTS];
static uint32_t s_dec_clock;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24));
}

// The decoded frame f of the icon b (its slot), or NULL.
static const uint8_t *icon_frame(const uint8_t *b, size_t blen, int f, int size)
{
    dec_t *d = NULL, *old = &tfa_dec[0];
    for (int i = 0; i < DEC_SLOTS; i++) {
        if (tfa_dec[i].b == b && tfa_dec[i].blen == blen) {
            d = &tfa_dec[i];
            break;
        }
        if (tfa_dec[i].used < old->used) {
            old = &tfa_dec[i];
        }
    }
    if (d && d->f == f) {
        d->used = ++s_dec_clock;
        return d->idx;
    }
    if (!d) {
        d = old;  // (a new icon: the slot that was not used for the longest time)
        d->b = NULL;
    }
    if (!d->idx && !(d->idx = heap_caps_malloc(128 * 128, MALLOC_CAP_SPIRAM))) {
        return NULL;
    }
    uint8_t *dec = d->idx;
    d->b = NULL;  // (until the frame is complete)
    int colors = rd16(b + 10);
    size_t off = 12 + (size_t)colors * 2;
    for (int i = 0; i < f; i++) {
        if (off + 4 > blen) {
            return NULL;
        }
        uint32_t l = rd32(b + off);
        if (l > blen - off - 4) {
            return NULL;
        }
        off += 4 + l;
    }
    if (off + 4 > blen) {
        return NULL;
    }
    uint32_t len = rd32(b + off);
    if (len > blen - off - 4) {
        return NULL;
    }
    const uint8_t *p = b + off + 4, *q = p + len;
    int n = 0, total = size * size;
    while (p < q && n < total) {
        int c = *p++;
        if (c < 128) {
            for (int i = 0; i <= c && p < q && n < total; i++) {
                dec[n++] = *p++;
            }
        } else if (p < q) {
            int r = 257 - c, v = *p++;
            while (r-- > 0 && n < total) {
                dec[n++] = (uint8_t)v;
            }
        }
    }
    if (n < total) {
        memset(dec + n, 0, (size_t)(total - n));
    }
    d->b = b;
    d->blen = blen;
    d->f = f;
    d->used = ++s_dec_clock;
    return dec;
}

// free the icon file of the entry k (and the decoded frames of it)
static void icon_file_free(int k)
{
    if (!tfa_icon[k]) {
        return;
    }
    for (int i = 0; i < DEC_SLOTS; i++) {
        if (tfa_dec[i].b == tfa_icon[k]) {
            tfa_dec[i].b = NULL;
        }
    }
    tfa_icon_bytes -= tfa_icon_len[k] < tfa_icon_bytes ? tfa_icon_len[k] : tfa_icon_bytes;
    free(tfa_icon[k]);
    tfa_icon[k] = NULL;
}

// free the icon files that were not used for the longest time, until the files keep ICON_BUDGET at most (not keep)
static void icon_files_trim(int keep)
{
    const uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    while (tfa_icon_bytes > ICON_BUDGET) {
        int old = -1;
        for (int k = 0; k < TFN_MAX; k++) {
            if (tfa_icon[k] && k != keep && now - tfa_icon_used[k] > ICON_KEEP_MS &&
                (old < 0 || tfa_icon_used[k] < tfa_icon_used[old])) {
                old = k;
            }
        }
        if (old < 0) {
            break;
        }
        icon_file_free(old);
    }
}

// all icon files go (an app loads: it gets the PSRAM; the menu reads the icons again when it comes back)
void tfa_icon_files_free(void)
{
    for (int k = 0; k < TFN_MAX; k++) {
        icon_file_free(k);
    }
    tfa_icon_bytes = 0;
}

void tfn_icon_cache_free(void)
{
#if TFA_ICON_BIG
    // (the Tab5: the decoded frames stay while the PSRAM has room: the menu comes back with no new decode)
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > ICON_KEEP_FREE) {
        return;
    }
#endif
    for (int i = 0; i < DEC_SLOTS; i++) {
        heap_caps_free(tfa_dec[i].idx);
        tfa_dec[i] = (dec_t){0};
    }
}

bool tfn_icon_ok(const uint8_t *b, size_t len)
{
    return b && len >= 12 && !memcmp(b, "TFI1", 4) && rd16(b + 4) >= 8 && rd16(b + 4) <= 128 && rd16(b + 6) > 0 &&
           rd16(b + 10) <= 255 && len >= 12u + 2u * rd16(b + 10);
}

// (the Tab5: true = the icon shows its first frame; icon_draw sets it: one icon of the menu animates at a time)
static bool s_icon_still;

bool tfn_icon_draw(const uint8_t *b, size_t blen, int x, int y, int s)
{
    if (s <= 0 || !tfn_icon_ok(b, blen)) {
        return false;
    }
    int size = rd16(b + 4), frames = rd16(b + 6), ms = rd16(b + 8) ? rd16(b + 8) : 125;
    const uint16_t *pal = (const uint16_t *)(b + 12);  // (the file is little-endian, as the ESP32-S3)
    int f = s_icon_still ? 0 : (int)((esp_timer_get_time() / 1000 / ms) % frames);
    const uint8_t *dec = icon_frame(b, blen, f, size);
    if (!dec) {
        return false;
    }
    int x0 = x < 0 ? -x : 0, y0 = y < 0 ? -y : 0;
    int x1 = x + s > gfx_w ? gfx_w - x : s, y1 = y + s > gfx_h ? gfx_h - y : s;
    // the column of the icon for each column of the tile (no division for each pixel)
    static uint8_t xm[512];
    if (x1 > 512) {
        x1 = 512;
    }
    for (int dx = x0; dx < x1; dx++) {
        xm[dx] = (uint8_t)(dx * size / s);
    }
    for (int dy = y0; dy < y1; dy++) {
        const uint8_t *row = dec + (dy * size / s) * size;
        uint16_t *dst = g_fb + (y + dy) * gfx_w + x;
        for (int dx = x0; dx < x1; dx++) {
            uint8_t v = row[xm[dx]];
            if (v) {
                dst[dx] = pal[v - 1];  // (index 0 = a hole; the palette starts at index 1)
            }
        }
    }
    return true;
}

void tfn_icon_plain(const char *name, int x, int y, int s)
{
    // no icon file: a tile with the first letter of the name
    gfx_fill_rect(x + s / 10, y + s / 10, s - s / 5, s - s / 5, rgb(70, 60, 120));
    char t[2] = {name && name[0] ? name[0] : '?', 0};
    const mfont_t *f = s >= 80 ? &MF_N34 : s >= 50 ? &MF_N24 : &MF_N16;  // (the display font, not the stroke font)
    mf_text_c(f, x + s / 2, y + s / 2 + f->asc / 2, t, rgb(255, 255, 255), 255, 1);
}

#if TFA_ICON_ASK
// THE ASK READER (the grid menu): the menu asks for the icon files that are not in memory; a low task reads them one at
// a time (outside the lock, as the prefetch task of the Tab5), so that a frame of the menu never waits for the card.
// The task ends after 3 s with no work (its 4 KB of internal RAM go back to the app that opens); the next ask starts it
// again.
#define ASK_Q 16
static bool s_ask_on, s_missed, s_ask_task;
static QueueHandle_t s_ask_q;
typedef struct {
    int k;
    uint32_t gen;
} ask_t;
static void icon_ask_task(void *arg)
{
    (void)arg;
    for (;;) {
        ask_t a;
        if (xQueueReceive(s_ask_q, &a, pdMS_TO_TICKS(3000)) != pdTRUE) {
            LOCK();
            const bool end = uxQueueMessagesWaiting(s_ask_q) == 0;
            if (end) {
                s_ask_task = false;
            }
            UNLOCK();
            if (end) {
                vTaskDelete(NULL);
            }
            continue;
        }
        char path[64];
        LOCK();
        const bool want = a.gen == tfa_icon_gen && a.k < tfa_n && !tfa_icon[a.k] && !tfa_icon_bad[a.k];
        if (want) {
            snprintf(path, sizeof path, "%s/icon.bin", tfa_e[a.k].dir);
        } else {
            tfa_icon_busy[a.k] = false;
        }
        UNLOCK();
        if (!want) {
            continue;
        }
        uint8_t *b = NULL;
        size_t len = 0;
        const bool ok = tfa_read_all(path, &b, &len, 2 << 20) && tfn_icon_ok(b, len);
        LOCK();
        if (a.gen == tfa_icon_gen && !tfa_icon[a.k]) {
            if (ok) {
                tfa_icon[a.k] = b;
                tfa_icon_len[a.k] = len;
                tfa_icon_bytes += len;
                tfa_icon_used[a.k] = (uint32_t)(esp_timer_get_time() / 1000);
                b = NULL;
            } else {
                tfa_icon_bad[a.k] = true;
            }
        }
        tfa_icon_busy[a.k] = false;
        UNLOCK();
        free(b);
    }
}
void tfn_icon_ask(bool on)
{
    s_ask_on = on;
}
bool tfn_icon_missed(void)
{
    // (the trim of the files that the task read: here, in the task of the menu, so that no file goes while an icon
    // draws from it)
    LOCK();
    icon_files_trim(-1);
    UNLOCK();
    const bool m = s_missed;
    s_missed = false;
    return m;
}
// true: the read of the icon k goes to the task (the caller draws a plain tile)
static bool icon_ask(int k)
{
    if (!s_ask_on) {
        return false;
    }
    s_missed = true;
    if (tfa_icon_busy[k]) {
        return true;
    }
    LOCK();
    if (!s_ask_q) {
        s_ask_q = xQueueCreate(ASK_Q, sizeof(ask_t));
    }
    if (s_ask_q && !s_ask_task) {
        // (the stack in internal RAM: some icons are in the flash (/data), and a flash read turns the cache off)
        s_ask_task = xTaskCreatePinnedToCore(icon_ask_task, "iconask", 4096, NULL, 1, NULL, 0) == pdPASS;
    }
    // (a full queue: the icon asks again in a later frame)
    const ask_t a = {k, tfa_icon_gen};
    tfa_icon_busy[k] = s_ask_task && xQueueSend(s_ask_q, &a, 0) == pdTRUE;
    const bool ok = s_ask_q != NULL;
    UNLOCK();
    if (!ok) {
        s_ask_on = false;
    }
    return ok;
}
#endif

static void icon_draw(int k, int x, int y, int s)
{
    if (k >= tfa_n || s <= 0) {
        return;
    }
#if TFA_ICON_ASK
    if (!tfa_icon[k] && !tfa_icon_bad[k] && icon_ask(k)) {
        tfn_icon_plain(tfa_e[k].name, x, y, s);
        return;
    }
#endif
#if TFA_ICON_BIG
    if (!tfa_icon[k] && tfa_icon_busy[k]) {  // (the prefetch task reads it now: no wait)
        tfn_icon_plain(tfa_e[k].name, x, y, s);
        return;
    }
    // (the lock: a try only; an install of the web can hold it for seconds: then a plain tile this frame)
    bool locked = false;
    if (!tfa_icon[k] && !tfa_icon_bad[k] && tfa_lock) {
        if (xSemaphoreTakeRecursive(tfa_lock, 0) != pdTRUE) {
            tfn_icon_plain(tfa_e[k].name, x, y, s);
            return;
        }
        locked = true;
    }
#endif
    if (!tfa_icon[k] && !tfa_icon_bad[k]) {
        char path[64];
        snprintf(path, sizeof path, "%s/icon.bin", tfa_e[k].dir);
        const int64_t r0 = esp_timer_get_time();
        const bool rd = tfa_read_all(path, &tfa_icon[k], &tfa_icon_len[k], 2 << 20);
        evlog_add("icon read %s %u KB %d ms", tfa_e[k].name, (unsigned)(tfa_icon_len[k] / 1024),
                  (int)((esp_timer_get_time() - r0) / 1000));
        if (!rd || !tfn_icon_ok(tfa_icon[k], tfa_icon_len[k])) {
            free(tfa_icon[k]);
            tfa_icon[k] = NULL;
            tfa_icon_bad[k] = true;
        } else {
            tfa_icon_bytes += tfa_icon_len[k];
            icon_files_trim(k);
        }
    }
#if TFA_ICON_BIG
    if (locked) {
        xSemaphoreGiveRecursive(tfa_lock);
    }
#endif
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    tfa_icon_used[k] = now;
#if BOARD_UI_TABLET
    // (the Tab5 menu shows about 40 icons; to animate all of them is too much work for each frame.
    // So one icon animates at a time: each ICON_TURN_MS the turn goes to the next icon of the screen (drawn in the last
    // 300 ms) that has more than one frame; the others show their first frame, which the decode cache keeps.)
    enum {
        ICON_TURN_MS = 2000
    };
    static int turn = -1;
    static uint32_t turn_end;
    if ((int32_t)(now - turn_end) >= 0 || turn < 0 || turn >= TFN_MAX || !tfa_icon[turn] ||
        now - tfa_icon_used[turn] > 300) {
        int next = -1;
        for (int i = 1; i <= TFN_MAX && next < 0; i++) {
            int j = ((turn < 0 ? -1 : turn) + i) % TFN_MAX;
            if (j < 0) {
                j += TFN_MAX;
            }
            if (tfa_icon[j] && now - tfa_icon_used[j] <= 300 && tfa_icon_len[j] >= 12 && rd16(tfa_icon[j] + 6) > 1) {
                next = j;
            }
        }
        turn = next;
        turn_end = now + ICON_TURN_MS;
    }
    s_icon_still = k != turn;
#endif
    if (!tfn_icon_draw(tfa_icon[k], tfa_icon_len[k], x, y, s)) {
        tfn_icon_plain(tfa_e[k].name, x, y, s);
    }
    s_icon_still = false;
}

// one icon function for each place of the list (an app_t has no context pointer): icon_0 .. icon_159
// clang-format off
#define ICONF(k) static void icon_##k(int x, int y, int s) { icon_draw(k, x, y, s); }
#define ICONF10(t) ICONF(t##0) ICONF(t##1) ICONF(t##2) ICONF(t##3) ICONF(t##4) ICONF(t##5) ICONF(t##6) ICONF(t##7) \
                   ICONF(t##8) ICONF(t##9)
ICONF(0) ICONF(1) ICONF(2) ICONF(3) ICONF(4) ICONF(5) ICONF(6) ICONF(7) ICONF(8) ICONF(9)
ICONF10(1) ICONF10(2) ICONF10(3) ICONF10(4) ICONF10(5) ICONF10(6) ICONF10(7) ICONF10(8) ICONF10(9) ICONF10(10)
ICONF10(11) ICONF10(12) ICONF10(13) ICONF10(14) ICONF10(15)
_Static_assert(TFN_MAX == 160, "one ICONF and one ICON_FNS entry for each place of the list");
#define IFN10(t) icon_##t##0, icon_##t##1, icon_##t##2, icon_##t##3, icon_##t##4, icon_##t##5, icon_##t##6, icon_##t##7, \
                 icon_##t##8, icon_##t##9
void (*const tfa_icon_fns[TFN_MAX])(int, int, int) = {
    icon_0, icon_1, icon_2, icon_3, icon_4, icon_5, icon_6, icon_7, icon_8, icon_9,
    IFN10(1), IFN10(2), IFN10(3), IFN10(4), IFN10(5), IFN10(6), IFN10(7), IFN10(8), IFN10(9), IFN10(10),
    IFN10(11), IFN10(12), IFN10(13), IFN10(14), IFN10(15)};
// clang-format on
void tfa_stub_enter(int64_t now)
{
    (void)now;
}
void tfa_stub_input(const input_event_t *ev)
{
    (void)ev;
}
void tfa_stub_frame(int64_t now)
{
    (void)now;
}

#if TFA_ICON_BIG
// THE ICON PREFETCH (the Tab5): a low task reads the icon file of each entry that has none, one at a time, so that the
// first scroll of the menu reads nothing from the card. The read is outside the lock; the lock only claims an icon and
// hands it over (a rescan in that time: the read goes). It stops at the icon budget. Again after each rescan.
void tfa_icon_prefetch_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(3000));  // (after the start of the menu)
    uint32_t done_gen = UINT32_MAX;
    for (;;) {
        if (done_gen == tfa_icon_gen) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        const uint32_t gen = tfa_icon_gen;
        int got = 0;
        int64_t t0 = esp_timer_get_time();
        for (int k = 0; k < TFN_MAX; k++) {
            char path[64];
            LOCK();
            bool want = gen == tfa_icon_gen && k < tfa_n && !tfa_icon[k] && !tfa_icon_bad[k] && !tfa_icon_busy[k] &&
                        tfa_icon_bytes < ICON_BUDGET - 256 * 1024 &&
                        heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > ICON_KEEP_FREE + 1024 * 1024;
            if (want) {
                tfa_icon_busy[k] = true;
                snprintf(path, sizeof path, "%s/icon.bin", tfa_e[k].dir);
            }
            UNLOCK();
            if (!want) {
                continue;
            }
            uint8_t *b = NULL;
            size_t len = 0;
            bool ok = tfa_read_all(path, &b, &len, 2 << 20) && tfn_icon_ok(b, len);
            LOCK();
            if (ok && gen == tfa_icon_gen && !tfa_icon[k]) {
                tfa_icon_len[k] = len;
                tfa_icon[k] = b;
                tfa_icon_bytes += len;
                tfa_icon_used[k] = 0;  // (not drawn yet: the trim takes it first)
                b = NULL;
                got++;
            } else if (!ok && gen == tfa_icon_gen && !tfa_icon[k]) {
                tfa_icon_bad[k] = true;
            }
            tfa_icon_busy[k] = false;
            UNLOCK();
            free(b);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        evlog_add("icon prefetch: %d icons in %d ms (%u KB kept)", got, (int)((esp_timer_get_time() - t0) / 1000),
                  (unsigned)(tfa_icon_bytes / 1024));
        done_gen = gen;
    }
}
#endif
