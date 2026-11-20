// The change of the flash layout over the air. An OTA partition change is the only way to keep the apps and to
// upgrade the 3.49 boards in the field. See layout.h; MANUAL.md "The change of the layout
// over the air".
//
// The two tables (16 MB of flash):   old (partitions.csv)        slim (partitions_slim.csv)
//   nvs 0x9000, phy_init 0xF000      the same                    the same
//   ota_0                            0x10000   6 MB              0x10000   3.5 MB
//   ota_1                            0x610000  6 MB              0x390000  3.5 MB
//   otadata                          0xC10000                    0x710000
//   storage (/data)                  0xC20000  3.875 MB          0x720000  8.875 MB
// The steps. The board does them at the start, before the WiFi and the apps, and each one can run again after a power cut:
//   1. the firmware must run from ota_0 (0x10000): from ota_1 it copies itself (the image and its signature trailer)
//      to ota_0, checks the copy and boots it; never again after a copy of the same build that did not start well;
//   2. it packs the files of /data into an archive at 0x390000 (the slim ota_1: outside the old /data and the new
//      /data, after the firmware in ota_0), reads the archive back and checks each CRC;
//   3. it erases the slim otadata (blank = the bootloader starts ota_0), writes the slim table at 0x8000 and reads it
//      back (when it is wrong: the old table goes back); a restart;
//   4. with the slim table: it erases the new /data, formats it, unpacks the archive and checks each file.
// Only step 3 has a short unsafe time: the erase and the write of one 4 KB sector (about 50 ms). A power cut exactly then
// needs USB.
// THE WIDE LAYOUT (partitions_wide.csv; the CoreS3): a slim board with a firmware for the wide table
// changes the same way (the plan PLAN_WIDE): ota_0 0x10000 4.5 MB, ota_1 0x490000 4.5 MB, otadata 0x910000, /data
// 0x920000 6.875 MB. The archive is at 0x390000 again (the slim ota_1, under the old otadata: 3.5 MB at most). The new
// otadata is in the old /data: step 3 does not erase it (a failed table write keeps the old /data whole); step 4 does. NVS (settings store): lay_st (0 nothing, 1 packed, 2 the table is written), lay_len, lay_crc (the archive),
// lay_test (a test stop at one step, one time).

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <sys/stat.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_flash.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "esp_image_format.h"
#include "esp_rom_crc.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sdkconfig.h"
#include "layout.h"
#include "settings.h"
#include "storage.h"
#include "board.h"
#include "gfx.h"
#include "app.h"
#include "ota.h"
#include "ota_sig.h"
#include "device_guard.h"
#include "tfn/theme.h"

#include "layout_table.inc"

static const char *TAG = "layout";
#define TABLE_ADDR   0x8000
#define TABLE_LEN    0xC00
#define OTA0_ADDR    0x10000
#define SLIM_SLOT    0x380000
#define ARC_ADDR     0x390000
#define ARC_CAP      0x380000
#define SLIM_OTADATA 0x710000
#define BLK          0x10000
#define BUF          4096
#define ARC_MAGIC    "TFNARC01"
#define ARC_END      "TFNEND01"
// the table of this firmware: a firmware for the slim table changes an old board, a firmware for the wide table changes a
// slim board; a firmware for the old table never changes a board
#define WANT_SLIM    (strcmp(CONFIG_PARTITION_TABLE_CUSTOM_FILENAME, "partitions_slim.csv") == 0)
#define WANT_WIDE    (strcmp(CONFIG_PARTITION_TABLE_CUSTOM_FILENAME, "partitions_wide.csv") == 0)
#define WIDE_OTADATA 0x910000

// one change: the new table, its otadata, the room of the archive, the otadata inside the old /data (erase in step 4)
typedef struct {
    const char *name;
    const uint8_t *table;
    uint32_t otadata;
    uint32_t arc_cap;
    bool late_otadata;
} plan_t;
static const plan_t PLAN_SLIM = {"slim", LAYOUT_SLIM_TABLE, SLIM_OTADATA, ARC_CAP, false};
static const plan_t PLAN_WIDE = {"wide", LAYOUT_WIDE_TABLE, WIDE_OTADATA, SLIM_OTADATA - ARC_ADDR, true};
static const plan_t *s_plan = &PLAN_SLIM;

typedef struct {
    char magic[8];
    uint32_t n, len;
} arc_head_t;  // len = the bytes of the whole archive
typedef struct {
    uint16_t plen, dir;
    uint32_t size, crc;
} arc_ent_t;  // then the path, the data; padded to 4
typedef struct {
    char *path;
    uint32_t size, crc;
    bool dir, skip;
    char *mem;
} fent_t;  // (path under /data; mem = data in RAM)
#define DROP_NAME "apps_dropped.txt"
#define DROP_FILE STORAGE_ROOT "/" DROP_NAME

static bool s_wait;  // the board changes at a restart (layout_tick)
static const char *s_state = "";
static int64_t s_last;
static uint8_t *s_buf;
static uint8_t *s_wbuf;             // (the writer has its own buffer: s_buf reads the files)
static uint32_t s_bn, s_at, s_crc;  // the writer / the reader: the bytes in the buffer, the flash address, the CRC
static fent_t *s_f;
static int s_nf, s_capf;

// ---------------------------------------------------------------- the screen of the steps
static void screen(const char *title, const char *line, int pct)
{
    gfx_fill_rect(0, 0, gfx_w, gfx_h, THEME_LAYOUT_BG);
    int y = gfx_h / 2 - 40;
    gfx_text_center(y, title, 2, THEME_LAYOUT_TITLE);
    gfx_text_center(y + 26, line, 1, THEME_LAYOUT_TEXT);
    int w = gfx_w * 2 / 3, x = (gfx_w - w) / 2;
    gfx_fill_rect(x, y + 44, w, 10, THEME_LAYOUT_TRACK);
    if (pct > 0) {
        gfx_fill_rect(x, y + 44, w * (pct > 100 ? 100 : pct) / 100, 10, THEME_LAYOUT_BAR);
    }
    gfx_text_center(y + 64, "DO NOT TURN THE BOARD OFF", 1, THEME_LAYOUT_WARN);
    board_lcd_flush(gfx_swap(), gfx_orient() | gfx_frame_bits());
    board_backlight(200);
}

// a test stop: lay_test holds the steps as digits, the next one last (54321 = a stop at 1, then at 2, ... then at 5)
static void test_stop(int n)
{
    uint32_t v = store_get("lay_test", 0);
    if (!v || (int)(v % 10) != n) {
        return;
    }
    store_set("lay_test", v / 10);
    ESP_LOGW(TAG, "TEST STOP at step %d (a restart, as a power cut); next stops %lu", n, (unsigned long)(v / 10));
    esp_restart();
}

// ---------------------------------------------------------------- the layout of this board
static const esp_partition_t *part(esp_partition_type_t t, esp_partition_subtype_t s, const char *label)
{
    return esp_partition_find_first(t, s, label);
}
int layout_now(void)
{
    const esp_partition_t *o0 = part(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *o1 = part(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
    const esp_partition_t *od = part(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, NULL);
    const esp_partition_t *sg = part(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    uint32_t fsz = 0;
    if (!o0 || !o1 || !od || !sg || esp_flash_get_size(NULL, &fsz) != ESP_OK || fsz < 0x1000000) {
        return LAYOUT_OTHER;
    }
    if (o0->address == 0x10000 && o0->size == 0x600000 && o1->address == 0x610000 && o1->size == 0x600000 &&
        od->address == 0xC10000 && sg->address == 0xC20000 && sg->size == 0x3E0000) {
        return LAYOUT_OLD;
    }
    if (o0->address == 0x10000 && o0->size == 0x380000 && o1->address == ARC_ADDR && o1->size == 0x380000 &&
        od->address == SLIM_OTADATA && sg->address == 0x720000 && sg->size == 0x8E0000) {
        return LAYOUT_SLIM;
    }
    if (o0->address == 0x10000 && o0->size == 0x480000 && o1->address == 0x490000 && o1->size == 0x480000 &&
        od->address == WIDE_OTADATA && sg->address == 0x920000 && sg->size == 0x6E0000) {
        return LAYOUT_WIDE;
    }
    return LAYOUT_OTHER;
}
static const char *layout_name(int k)
{
    return k == LAYOUT_OLD ? "old" : k == LAYOUT_SLIM ? "slim" : k == LAYOUT_WIDE ? "wide" : "other";
}

static bool app_valid(void)
{
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) != ESP_OK) {
        return true;  // (a USB flash)
    }
    return st == ESP_OTA_IMG_VALID || st == ESP_OTA_IMG_UNDEFINED;
}
static bool power_ok(void)
{
    bool chg = false;
    int p = board_battery(&chg, NULL);
    return p < 0 || chg || p >= 25;
}
static bool image_len(const esp_partition_t *p, uint32_t *len)
{
    esp_partition_pos_t pos = {.offset = p->address, .size = p->size};
    esp_image_metadata_t md;
    if (esp_image_verify(ESP_IMAGE_VERIFY_SILENT, &pos, &md) != ESP_OK) {
        return false;
    }
    *len = md.image_len;
    return true;
}
static bool erase(uint32_t addr, uint32_t len, const char *title, const char *line, int p0, int p1)
{
    for (uint32_t o = 0; o < len; o += BLK) {
        if (esp_flash_erase_region(NULL, addr + o, BLK) != ESP_OK) {
            return false;
        }
        if ((o / BLK) % 8 == 0) {
            screen(title, line, p0 + (int)((uint64_t)(p1 - p0) * o / len));
        }
    }
    return true;
}

// ---------------------------------------------------------------- step 1: the firmware to ota_0
// Step 1 again? Not when ota_0 has a copy of this same build that did not start well (the bootloader or a rollback
// marked it): a new copy fails the same way, and the board restarts for ever (ota_sig_copy_again).
static bool copy_allowed(const esp_partition_t *run)
{
    const esp_partition_t *o0 = part(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    const esp_partition_t *bad = esp_ota_get_last_invalid_partition();
    esp_app_desc_t d;
    bool same = o0 && esp_ota_get_partition_description(o0, &d) == ESP_OK &&
                !memcmp(d.app_elf_sha256, esp_app_get_description()->app_elf_sha256, sizeof d.app_elf_sha256);
    bool o0_bad = bad && o0 && bad->address == o0->address;
    if (ota_sig_copy_again(o0_bad, same)) {
        return true;
    }
    ESP_LOGE(TAG,
             "step 1: ota_0 has a copy of this firmware that did not start well: no new copy. The board stays on %s "
             "with the old table; a next update can change it",
             run->label);
    s_state = "copyfail";
    return false;
}

// The copy: the image and, for a signed release, the trailer TFNOTA1 directly after it (ota_sig_copy_len). A firmware
// in ENFORCE checks its own trailer before it marks itself good in ota_0: a copy with no trailer fails that self-test.
static void copy_to_ota0(const esp_partition_t *run, uint32_t len)
{
    const char *T = "STORAGE UPDATE 1/4";
    const esp_partition_t *o0 = part(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    uint8_t *tr = s_wbuf;  // (the writer buffer: not used in step 1)
    uint32_t all = len;
    if (esp_partition_read(run, len, tr, OTA_SIG_TRAILER) == ESP_OK) {
        all = ota_sig_copy_len(tr, OTA_SIG_TRAILER, len);
    }
    char code[24];
    if (all > SLIM_SLOT || !ota_slot_sig_ok(run, code, sizeof code)) {
        ESP_LOGE(TAG,
                 "step 1: the firmware in %s cannot start from ota_0 (%lu bytes, the own trailer %s): no change. The "
                 "board stays on %s with the old table",
                 run->label, (unsigned long)all, all > SLIM_SLOT ? "does not fit" : code, run->label);
        s_state = "copyfail";
        return;
    }
    uint32_t er = (all + BLK - 1) & ~(BLK - 1);
    ESP_LOGW(TAG, "step 1: the firmware (%lu bytes%s) moves from %s to ota_0", (unsigned long)len,
             all > len ? " + the trailer of 1024 bytes" : ", no trailer", run->label);
    if (!erase(o0->address, er, T, "THE FIRMWARE MOVES", 0, 30)) {
        ESP_LOGE(TAG, "step 1: the erase failed");
        return;
    }
    for (uint32_t o = 0; o < all; o += BUF) {
        uint32_t k = all - o < BUF ? all - o : BUF;
        if (esp_partition_read(run, o, s_buf, k) != ESP_OK || esp_partition_write(o0, o, s_buf, k) != ESP_OK) {
            ESP_LOGE(TAG, "step 1: the copy failed at %lu", (unsigned long)o);
            return;
        }
        if ((o / BUF) % 64 == 0) {
            screen(T, "THE FIRMWARE MOVES", 30 + (int)((uint64_t)60 * o / all));
        }
    }
    test_stop(1);
    uint32_t l2 = 0;
    if (!image_len(o0, &l2) || l2 != len) {
        ESP_LOGE(TAG, "step 1: the copy in ota_0 is not good: no change");
        return;
    }
    if (all > len &&
        (esp_partition_read(o0, len, s_buf, OTA_SIG_TRAILER) != ESP_OK || memcmp(s_buf, tr, OTA_SIG_TRAILER) != 0)) {
        ESP_LOGE(TAG, "step 1: the trailer in ota_0 is not the trailer of %s: no change", run->label);
        return;
    }
    if (!ota_slot_sig_ok(o0, code, sizeof code)) {
        ESP_LOGE(TAG, "step 1: the copy in ota_0 does not pass the check of its trailer (%s): no change", code);
        s_state = "copyfail";
        return;
    }
    ESP_LOGW(TAG, "step 1: the copy in ota_0 is good (the image, the trailer: %s)", all > len ? code : "none");
    if (esp_ota_set_boot_partition(o0) != ESP_OK) {
        ESP_LOGE(TAG, "step 1: ota_0 cannot be the boot slot");
        return;
    }
    ESP_LOGW(TAG, "step 1 done: a restart into ota_0");
    screen(T, "A RESTART", 100);
    esp_restart();
}

// ---------------------------------------------------------------- step 2: the archive
static bool add_ent(const char *path, bool dir, uint32_t size)
{
    if (s_nf == s_capf) {
        int nc = s_capf ? s_capf * 2 : 128;
        fent_t *n = heap_caps_realloc(s_f, nc * sizeof(fent_t), MALLOC_CAP_SPIRAM);
        if (!n) {
            return false;
        }
        s_f = n;
        s_capf = nc;
    }
    const char *rel = path + strlen(STORAGE_ROOT) + 1;
    s_f[s_nf].path = heap_caps_malloc(strlen(rel) + 1, MALLOC_CAP_SPIRAM);
    if (!s_f[s_nf].path) {
        return false;
    }
    strcpy(s_f[s_nf].path, rel);
    s_f[s_nf].size = size;
    s_f[s_nf].dir = dir;
    s_f[s_nf].crc = 0;
    s_f[s_nf].skip = false;
    s_f[s_nf].mem = NULL;
    s_nf++;
    return true;
}
static bool walk(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d) {
        return true;
    }
    struct dirent *de;
    bool ok = true;
    char *p = malloc(300);
    if (!p) {
        closedir(d);
        return false;
    }
    while (ok && (de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        snprintf(p, 300, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(p, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            ok = add_ent(p, true, 0) && (depth > 8 || walk(p, depth + 1));
        } else {
            ok = add_ent(p, false, (uint32_t)st.st_size);
        }
    }
    free(p);
    closedir(d);
    return ok;
}
static void ents_free(void)
{
    for (int i = 0; i < s_nf; i++) {
        free(s_f[i].path);
        free(s_f[i].mem);
    }
    free(s_f);
    s_f = NULL;
    s_nf = s_capf = 0;
}
static bool file_crc(const char *rel, uint32_t *crc, uint32_t *size)
{
    char p[320];  // (the root, a "/", a name of up to 299 bytes)
    snprintf(p, sizeof p, "%s/%s", STORAGE_ROOT, rel);
    FILE *f = fopen(p, "rb");
    if (!f) {
        return false;
    }
    uint32_t c = 0, n = 0;
    size_t k;
    while ((k = fread(s_buf, 1, BUF, f)) > 0) {
        c = esp_rom_crc32_le(c, s_buf, k);
        n += k;
    }
    fclose(f);
    *crc = c;
    if (size) {
        *size = n;
    }
    return true;
}
static uint32_t pad4(uint32_t n)
{
    return (n + 3) & ~3u;
}
static bool wr(const void *p, uint32_t n)
{
    const uint8_t *b = p;
    s_crc = esp_rom_crc32_le(s_crc, b, n);
    while (n) {
        uint32_t k = BUF - s_bn < n ? BUF - s_bn : n;
        memcpy(s_wbuf + s_bn, b, k);
        s_bn += k;
        b += k;
        n -= k;
        if (s_bn == BUF) {
            esp_err_t e = esp_flash_write(NULL, s_wbuf, s_at, BUF);
            s_at += BUF;
            s_bn = 0;
            if (e != ESP_OK) {
                return false;
            }
        }
    }
    return true;
}
static bool wr_end(void)
{
    if (!s_bn) {
        return true;
    }
    esp_err_t e = esp_flash_write(NULL, s_wbuf, s_at, s_bn);
    s_at += s_bn;
    s_bn = 0;
    return e == ESP_OK;
}
static bool wr_pad(uint32_t written)
{
    static const uint8_t z[4];
    return wr(z, pad4(written) - written);
}

// the reader of the archive (from the flash): rd() takes the next n bytes (to dst, or only into the CRC)
static uint32_t s_rp, s_rend;
static bool rd(void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > BUF ? BUF : n;
        if (s_rp + k > s_rend || esp_flash_read(NULL, s_wbuf, s_rp, k) != ESP_OK) {
            return false;
        }
        s_crc = esp_rom_crc32_le(s_crc, s_wbuf, k);
        if (d) {
            memcpy(d, s_wbuf, k);
            d += k;
        }
        s_rp += k;
        n -= k;
    }
    return true;
}
// check the archive at ARC_ADDR (len bytes, crc of all the bytes before the end mark); unpack = write the files to /data
static bool arc_read(uint32_t len, uint32_t crc_all, bool unpack, const char *T)
{
    s_rp = ARC_ADDR;
    s_rend = ARC_ADDR + len;
    s_crc = 0;
    arc_head_t h;
    if (len < sizeof h + 12 || !rd(&h, sizeof h) || memcmp(h.magic, ARC_MAGIC, 8) || h.len != len) {
        return false;
    }
    char path[300], full[310];
    for (uint32_t i = 0; i < h.n; i++) {
        arc_ent_t e;
        if (!rd(&e, sizeof e) || e.plen == 0 || e.plen >= sizeof path - 1 || !rd(path, e.plen)) {
            return false;
        }
        path[e.plen] = 0;
        if (!rd(NULL, pad4(sizeof e + e.plen) - (sizeof e + e.plen))) {
            return false;
        }
        snprintf(full, sizeof full, "%s/%s", STORAGE_ROOT, path);
        if (e.dir) {
            if (unpack) {
                mkdir(full, 0777);
            }
            continue;
        }
        FILE *f = unpack ? fopen(full, "wb") : NULL;
        if (unpack && !f) {
            ESP_LOGE(TAG, "cannot write %s", full);
            return false;
        }
        uint32_t c = 0, left = e.size;
        uint8_t *tmp = s_buf;
        while (left) {
            uint32_t k = left > BUF ? BUF : left;
            if (!rd(tmp, k)) {
                if (f) {
                    fclose(f);
                }
                return false;
            }
            c = esp_rom_crc32_le(c, tmp, k);
            if (f && fwrite(tmp, 1, k, f) != k) {
                fclose(f);
                ESP_LOGE(TAG, "the write of %s failed", full);
                return false;
            }
            left -= k;
        }
        if (f) {
            fclose(f);
        }
        if (c != e.crc) {
            ESP_LOGE(TAG, "%s: a wrong CRC in the archive", path);
            return false;
        }
        if (!rd(NULL, pad4(e.size) - e.size)) {
            return false;
        }
        if (unpack) {
            uint32_t c2 = 0, sz = 0;
            if (!file_crc(path, &c2, &sz) || c2 != e.crc || sz != e.size) {
                ESP_LOGE(TAG, "%s: the file is not the same after the unpack", path);
                return false;
            }
            if (i % 4 == 0) {
                screen(T, "THE FILES GO BACK", 50 + (int)(50 * (uint64_t)i / (h.n ? h.n : 1)));
            }
            if (i == h.n / 2) {
                test_stop(5);
            }
        }
    }
    uint32_t mine = s_crc;
    char end[8];
    uint32_t c3;
    if (!rd(end, 8) || memcmp(end, ARC_END, 8) || !rd(&c3, 4)) {
        return false;
    }
    return mine == crc_all && c3 == crc_all && s_rp == s_rend;
}
// pack /data (mounted) into the archive. Returns its length (0 = it failed / no room) and *crc
static uint32_t arc_cap(void)
{
    uint32_t c = store_get("lay_cap", 0);  // (a test: a smaller room; the console "lay cap <bytes>")
    return c && c < s_plan->arc_cap ? c : s_plan->arc_cap;
}

static uint32_t stage(uint32_t *crc_out)
{
    const char *T = "STORAGE UPDATE 2/4";
    screen(T, "THE FILES ARE COUNTED", 0);
    if (!walk(STORAGE_ROOT, 0)) {
        ESP_LOGE(TAG, "step 2: no memory for the list of files");
        ents_free();
        return 0;
    }
    uint32_t total = 0;
    for (int i = 0; i < s_nf; i++) {
        if (!s_f[i].dir) {
            uint32_t sz;
            if (!file_crc(s_f[i].path, &s_f[i].crc, &sz)) {
                ESP_LOGE(TAG, "step 2: cannot read %s", s_f[i].path);
                ents_free();
                return 0;
            }
            s_f[i].size = sz;
        }
    }
    for (int pass = 0; pass < 2; pass++) {
        total = sizeof(arc_head_t) + 12;
        for (int i = 0; i < s_nf; i++) {
            if (!s_f[i].skip) {
                total += pad4(sizeof(arc_ent_t) + strlen(s_f[i].path)) + pad4(s_f[i].size);
            }
        }
        if (total <= arc_cap() || pass) {
            break;
        }
        // too big: the app files stay out (the store has them); an entry in RAM lists their ids (and an older list)
        char *ids = heap_caps_calloc(1, 8192, MALLOC_CAP_SPIRAM);
        if (!ids) {
            break;
        }
        int n = 0, ni = 0;
        for (int i = 0; i < s_nf; i++) {
            const char *p = s_f[i].path;
            if (!strcmp(p, DROP_NAME)) {  // the list of an older change: its ids stay on the list
                s_f[i].skip = true;
                FILE *f = fopen(DROP_FILE, "rb");
                if (f) {
                    n += fread(ids + n, 1, 4000, f);
                    fclose(f);
                    if (n && ids[n - 1] != '\n') {
                        ids[n++] = '\n';
                    }
                }
                continue;
            }
            if (strncmp(p, "apps/", 5)) {
                continue;
            }
            s_f[i].skip = true;
            if (s_f[i].dir && !strchr(p + 5, '/') && p[5] != '.' && n < 8000) {
                n += snprintf(ids + n, 8192 - n, "%s\n", p + 5);
                ni++;
            }
        }
        if (add_ent(STORAGE_ROOT "/" DROP_NAME, false, (uint32_t)n)) {
            s_f[s_nf - 1].mem = ids;
            s_f[s_nf - 1].crc = esp_rom_crc32_le(0, (uint8_t *)ids, n);
        } else {
            free(ids);
        }
        ESP_LOGW(
                TAG,
                "step 2: all of /data does not fit (%lu bytes): %d app files stay out (the keeper installs them again)",
                (unsigned long)total, ni);
    }
    ESP_LOGW(TAG, "step 2: %d files and folders, an archive of %lu bytes (room %lu)", s_nf, (unsigned long)total,
             (unsigned long)arc_cap());
    if (total > arc_cap()) {
        s_state = "noroom";
        ents_free();
        return 0;
    }
    if (!erase(ARC_ADDR, (total + BLK - 1) & ~(BLK - 1), T, "THE FILES ARE PACKED", 0, 20)) {
        ESP_LOGE(TAG, "step 2: the erase failed");
        ents_free();
        return 0;
    }
    s_at = ARC_ADDR;
    s_bn = 0;
    s_crc = 0;
    uint32_t nin = 0;
    for (int i = 0; i < s_nf; i++) {
        nin += !s_f[i].skip;
    }
    arc_head_t h = {.n = nin, .len = total};
    memcpy(h.magic, ARC_MAGIC, 8);
    bool ok = wr(&h, sizeof h);
    for (int i = 0; ok && i < s_nf; i++) {
        if (s_f[i].skip) {
            continue;
        }
        arc_ent_t e = {
                .plen = (uint16_t)strlen(s_f[i].path), .dir = s_f[i].dir, .size = s_f[i].size, .crc = s_f[i].crc};
        ok = wr(&e, sizeof e) && wr(s_f[i].path, e.plen) && wr_pad(sizeof e + e.plen);
        if (ok && s_f[i].mem) {
            ok = wr(s_f[i].mem, e.size) && wr_pad(e.size);
        } else if (ok && !e.dir) {
            char p[300];
            snprintf(p, sizeof p, "%s/%s", STORAGE_ROOT, s_f[i].path);
            FILE *f = fopen(p, "rb");
            uint32_t n = 0;
            size_t k;
            if (!f) {
                ok = false;
            }
            while (ok && (k = fread(s_buf, 1, BUF, f)) > 0) {
                ok = wr(s_buf, k);
                n += k;
            }
            if (f) {
                fclose(f);
            }
            ok = ok && n == e.size && wr_pad(e.size);
        }
        if (i % 4 == 0) {
            screen(T, "THE FILES ARE PACKED", 20 + (int)(60 * (uint64_t)i / (s_nf ? s_nf : 1)));
        }
        if (i == s_nf / 2) {
            if (!wr_end()) {
                ok = false;
            }
            test_stop(2);
        }
    }
    uint32_t crc = s_crc;
    ok = ok && wr(ARC_END, 8) && wr(&crc, 4) && wr_end() && s_at == ARC_ADDR + total;
    ents_free();
    if (!ok) {
        ESP_LOGE(TAG, "step 2: the archive was not written");
        return 0;
    }
    screen(T, "THE ARCHIVE IS CHECKED", 85);
    if (!arc_read(total, crc, false, T)) {
        ESP_LOGE(TAG, "step 2: the archive is not good when read back");
        return 0;
    }
    *crc_out = crc;
    return total;
}

// ---------------------------------------------------------------- step 3: the table
static void write_table(uint32_t len, uint32_t crc)
{
    const char *T = "STORAGE UPDATE 3/4";
    uint8_t *old = malloc(TABLE_LEN), *nw = malloc(TABLE_LEN), *chk = malloc(TABLE_LEN);
    if (!old || !nw || !chk) {
        free(old);
        free(nw);
        free(chk);
        return;
    }
    memset(nw, 0xFF, TABLE_LEN);
    memcpy(nw, s_plan->table, LAYOUT_TABLE_USED);
    screen(T, "THE NEW LAYOUT IS WRITTEN", 10);
    // (the wide otadata is in the old /data: step 4 erases it)
    if (esp_flash_read(NULL, old, TABLE_ADDR, TABLE_LEN) != ESP_OK ||
        (!s_plan->late_otadata && esp_flash_erase_region(NULL, s_plan->otadata, 0x2000) != ESP_OK)) {
        ESP_LOGE(TAG, "step 3: the flash did not answer: no change");
        goto out;
    }
    store_set("lay_len", len);
    store_set("lay_crc", crc);
    store_set("lay_st", 2);
    test_stop(3);
    ESP_LOGW(TAG, "step 3: the %s table goes to 0x8000 (about 50 ms)", s_plan->name);
    bool good = false;
    for (int t = 0; t < 2 && !good; t++) {
        if (esp_flash_erase_region(NULL, TABLE_ADDR, 0x1000) == ESP_OK &&
            esp_flash_write(NULL, nw, TABLE_ADDR, TABLE_LEN) == ESP_OK &&
            esp_flash_read(NULL, chk, TABLE_ADDR, TABLE_LEN) == ESP_OK) {
            good = memcmp(chk, nw, TABLE_LEN) == 0;
        }
    }
    if (!good) {
        ESP_LOGE(TAG, "step 3: the new table is not right when read back: the old table goes back");
        esp_flash_erase_region(NULL, TABLE_ADDR, 0x1000);
        esp_flash_write(NULL, old, TABLE_ADDR, TABLE_LEN);
        store_set("lay_st", 0);
        goto out;
    }
    ESP_LOGW(TAG, "step 3 done: a restart with the %s table", s_plan->name);
    screen(T, "A RESTART", 100);
    esp_restart();
out:
    free(old);
    free(nw);
    free(chk);
}

// ---------------------------------------------------------------- step 4: the new /data
static void finish(void)
{
    const char *T = "STORAGE UPDATE 4/4";
    uint32_t len = store_get("lay_len", 0), crc = store_get("lay_crc", 0);
    const esp_partition_t *sg = part(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "storage");
    ESP_LOGW(TAG, "step 4: the archive (%lu bytes) goes into the new /data", (unsigned long)len);
    screen(T, "THE ARCHIVE IS CHECKED", 0);
    if (s_plan->late_otadata) {
        // (the otadata of the new table was a part of the old /data: blank = the bootloader starts ota_0, this one)
        esp_flash_erase_region(NULL, s_plan->otadata, 0x2000);
    }
    bool arc = arc_read(len, crc, false, T);
    if (!arc) {
        ESP_LOGE(TAG, "step 4: THE ARCHIVE IS DAMAGED: /data starts empty");
    }
    for (uint32_t o = 0; o < sg->size; o += BLK) {
        esp_partition_erase_range(sg, o, BLK);
        if ((o / BLK) % 8 == 0) {
            screen(T, "THE NEW STORAGE IS MADE", 5 + (int)(40 * (uint64_t)o / sg->size));
        }
        if (o == sg->size / 2) {
            test_stop(4);
        }
    }
    if (!storage_init()) {
        ESP_LOGE(TAG, "step 4: the new /data cannot be mounted");
        return;
    }
    if (arc && !arc_read(len, crc, true, T)) {
        ESP_LOGE(TAG, "step 4: the unpack failed: a new try at the next start");
        return;
    }
    store_set("lay_st", 0);
    store_set("lay_cap", 0);
    s_state = "done";
    ESP_LOGW(TAG, "step 4 done: the %s layout, /data %u KB free", s_plan->name, (unsigned)(storage_free() / 1024));
}

// ---------------------------------------------------------------- the API
void layout_boot(void)
{
    // Protected layouts are provisioned with verified encrypted backups. The
    // legacy migration writes raw flash and cannot preserve that protection.
    if (device_guard_enabled()) {
        return;
    }
    int k = layout_now();
    uint32_t st = store_get("lay_st", 0);
    const int want = WANT_WIDE ? LAYOUT_WIDE : WANT_SLIM ? LAYOUT_SLIM : LAYOUT_OLD;
    s_plan = want == LAYOUT_WIDE ? &PLAN_WIDE : &PLAN_SLIM;
    ESP_LOGI(TAG, "the layout: %s, the firmware is for the %s table, step %lu", layout_name(k), layout_name(want),
             (unsigned long)st);
    // a change: old -> slim, or slim -> wide (the board on the table of the firmware: step 4, or nothing)
    const bool change = (k == LAYOUT_OLD && want == LAYOUT_SLIM) || (k == LAYOUT_SLIM && want == LAYOUT_WIDE);
    if (k == want && st != 2) {
        if (st) {
            store_set("lay_st", 0);
        }
        return;
    }
    if (k != want && !change) {
        return;
    }
    s_buf = heap_caps_malloc(BUF, MALLOC_CAP_DEFAULT);
    s_wbuf = heap_caps_malloc(BUF, MALLOC_CAP_DEFAULT);
    if (!s_buf || !s_wbuf) {
        goto out;
    }
    if (k == want) {
        finish();
        goto out;
    }
    // the change: not at the first start after an OTA (the firmware must mark itself good first)
    if (!app_valid()) {
        s_wait = true;
        goto out;
    }
    if (!power_ok()) {
        s_state = "battery";
        s_wait = true;
        goto out;
    }
    const esp_partition_t *run = esp_ota_get_running_partition();
    uint32_t len = 0, crc = 0;
    if (!image_len(run, &len) || len > SLIM_SLOT) {
        ESP_LOGW(TAG, "the firmware is not a slim image (%lu bytes): no change", (unsigned long)len);
        goto out;
    }
    if (run->address != OTA0_ADDR) {
        if (copy_allowed(run)) {
            copy_to_ota0(run, len);
        }
        goto out;
    }
    if (!storage_init()) {
        goto out;
    }
    uint32_t alen = stage(&crc);
    storage_deinit();
    if (!alen) {
        goto out;
    }
    store_set("lay_st", 1);
    write_table(alen, crc);
out:
    free(s_buf);
    free(s_wbuf);
    s_buf = s_wbuf = NULL;
}

// the bytes of the files under a folder
static uint64_t dir_bytes(const char *dir, int depth)
{
    DIR *d = opendir(dir);
    if (!d) {
        return 0;
    }
    uint64_t n = 0;
    struct dirent *de;
    char p[300];
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        snprintf(p, sizeof p, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(p, &st) != 0) {
            continue;
        }
        n += S_ISDIR(st.st_mode) ? (depth < 4 ? dir_bytes(p, depth + 1) : 0) : (uint64_t)st.st_size;
    }
    closedir(d);
    return n;
}

void layout_drop_done(const char *id)
{
    FILE *f = fopen(DROP_FILE, "rb");
    if (!f) {
        return;
    }
    char *b = calloc(1, 4097), *o = calloc(1, 4097);
    if (!b || !o) {
        fclose(f);
        free(b);
        free(o);
        return;
    }
    size_t n = fread(b, 1, 4096, f);
    fclose(f);
    b[n] = 0;
    int k = 0;
    for (char *ln = strtok(b, "\n"); ln; ln = strtok(NULL, "\n")) {
        if (*ln && strcmp(ln, id)) {
            k += snprintf(o + k, 4097 - k, "%s\n", ln);
        }
    }
    if (!k) {
        remove(DROP_FILE);
    } else if ((f = fopen(DROP_FILE, "wb"))) {
        fwrite(o, 1, k, f);
        fclose(f);
    }
    free(b);
    free(o);
}

int layout_drop_json(char *o, int max)
{
    int k = snprintf(o, max, "[");
    FILE *f = fopen(DROP_FILE, "rb");
    if (f) {
        char ln[48];
        bool first = true;
        while (fgets(ln, sizeof ln, f) && k < max - 50) {
            ln[strcspn(ln, "\r\n")] = 0;
            if (!*ln || *ln == '.') {
                continue;
            }
            k += snprintf(o + k, max - k, "%s\"%s\"", first ? "" : ",", ln);
            first = false;
        }
        fclose(f);
    }
    k += snprintf(o + k, max - k, "]");
    return k;
}

void layout_tick(int64_t now)
{
    if (!s_wait || now - s_last < 1000) {
        return;
    }
    s_last = now;
    if (!app_valid() || ota_busy() || app_current() != &app_menu) {
        return;
    }
    if (!power_ok()) {
        s_state = "battery";
        return;
    }
    if (storage_used() - dir_bytes(STORAGE_ROOT "/apps", 0) + 256 * 1024 > s_plan->arc_cap) {
        s_state = "noroom";
        s_wait = false;
        ESP_LOGW(TAG, "/data has too many files for the archive: no change");
        return;
    }
    ESP_LOGW(TAG, "the firmware is good: a restart for the change of the layout");
    screen("STORAGE UPDATE", "THE BOARD RESTARTS", 0);
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
}

const char *layout_state(void)
{
    return s_wait && !*s_state ? "wait" : s_state;
}

void layout_console(const char *args)
{
    if (device_guard_enabled()) {
        puts("LAY protected: use the verified owner provisioning/recovery image");
        return;
    }
    while (args && *args == ' ') {
        args++;
    }
    if (args && !strncmp(args, "cap", 3)) {
        store_set("lay_cap", (uint32_t)atoi(args + 3));
        printf("LAY CAP %lu (a test: the room of the archive)\n", (unsigned long)store_get("lay_cap", 0));
        return;
    }
    if (args && !strncmp(args, "test", 4)) {
        int n = atoi(args + 4);
        store_set("lay_test", (uint32_t)n);
        printf("LAY TEST STOPS %d (the last digit first)\n", n);
        return;
    }
    int k = layout_now();
    printf("LAY layout=%s fw=%s st=%lu state=%s used=%lu cap=%lu test=%lu\n", layout_name(k),
           WANT_WIDE   ? "wide"
           : WANT_SLIM ? "slim"
                       : "old",
           (unsigned long)store_get("lay_st", 0), *layout_state() ? layout_state() : "-", (unsigned long)storage_used(),
           (unsigned long)s_plan->arc_cap, (unsigned long)store_get("lay_test", 0));
}
