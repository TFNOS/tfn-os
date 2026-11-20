// The console commands of the system: the apps, the files, the store keys, the screen, the power, the sound, the memory, the clock, the tests of the screen (the table: console.c).
#include "console_priv.h"
#include "board.h"
#include "standby.h"
#include "tfn_sstore.h"

static const char *TAG = "console";

// One line for each file under dir (folders first go down, at most 6 levels): the size and
// the CRC-32 (the zlib CRC, the same as Python zlib.crc32).
static void list_dir(const char *dir, int depth, int *n, uint64_t *sum)
{
    DIR *d = opendir(dir);
    if (!d || depth > 6) {
        if (d) {
            closedir(d);
        }
        return;
    }
    struct dirent *e;
    char path[300];
    static uint8_t buf[2048];
    while ((e = readdir(d)) != NULL) {
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
        if (e->d_type == DT_DIR) {
            ESP_LOGI(TAG, "ls dir  %s", path);
            list_dir(path, depth + 1, n, sum);
            continue;
        }
        FILE *f = fopen(path, "rb");
        if (!f) {
            ESP_LOGI(TAG, "ls file %s (cannot open)", path);
            continue;
        }
        uint32_t crc = 0;
        size_t len = 0, k;
        while ((k = fread(buf, 1, sizeof(buf), f)) > 0) {
            crc = esp_rom_crc32_le(crc, buf, k);
            len += k;
        }
        fclose(f);
        ESP_LOGI(TAG, "ls file %s %u %08lx", path, (unsigned)len, (unsigned long)crc);
        (*n)++;
        *sum += len;
    }
    closedir(d);
}

#define USBFS_CRC esp_rom_crc32_le
#include "usb_fs.inc"
#include "device_guard_usb.inc"
#include "tfn_speech.h"
#include "tfn_listen.h"
#include "alarm.h"

#if defined(CONFIG_TFN_DEVICE_PROTECTION) && CONFIG_TFN_DEVICE_PROTECTION
// "maint"
void con_maint(char *cmd)
{
    guard_maintenance(strtok(NULL, "\r\n"));
    return;
}
#endif

// "sdusb"
void con_sdusb(char *cmd)
{
    usbfs_command(strtok(NULL, "\r\n"));
    return;
}

// "apps src [auto|mgr|static] [<https URL>]": the source of the store (main/tfn_sstore.h). With no value: show it.
static void con_apps_src(const char *mode, const char *url)
{
    static const char *const NAMES[] = {"auto", "mgr", "static"};
    char msg[96] = "";
    if (mode) {
        int m = -1;
        for (int i = 0; i < 3; i++) {
            m = strcmp(mode, NAMES[i]) ? m : i;
        }
        tfn_sstore_set(m, url, msg, sizeof msg);
    }
    int m = tfn_sstore_mode();
    ESP_LOGI(TAG, "apps: store source %s (now %s), static store %s %s", NAMES[m],
             tfn_sstore_active() ? "the static store" : "the manager", tfn_sstore_url(), msg);
}

// "apps": TFN OS: apps | apps scan | apps rm <id> | apps dev 0|1 | apps src [auto|mgr|static] [url]
void con_apps(char *cmd)
{
    char *a = strtok(NULL, " \t\r\n");
    char *b = strtok(NULL, " \t\r\n");
    char msg[160] = "";
    if (a && !strcmp(a, "scan")) {
        tfn_rescan();
    } else if (a && !strcmp(a, "cat")) {  // the catalog of this board from the manager (tests)
        tfn_cat_t *cat = heap_caps_calloc(TFN_CAT_MAX, sizeof(tfn_cat_t), MALLOC_CAP_SPIRAM);
        if (!cat) {
            return;
        }
        int n = tfn_store_catalog(cat, TFN_CAT_MAX, msg, sizeof msg);
        for (int i = 0; i < n; i++) {
            ESP_LOGI(TAG, "apps: store %-12s %-8s %lu bytes, state %d, desc %d+%d", cat[i].id, cat[i].ver,
                     (unsigned long)cat[i].bytes, cat[i].state, (int)strlen(tfn_store_desc(&cat[i], false)),
                     cat[i].desc ? (int)strlen(cat[i].desc + strlen(cat[i].desc) + 1) : 0);
        }
        ESP_LOGI(TAG, "apps: catalog %d (profile %s) %s", n, tfn_store_profile(), msg);
        if (n > 0) {
            tfn_store_cat_free(cat, n);
        }
        free(cat);
        return;
    } else if (a && !strcmp(a, "get") && b) {  // apps get <id> [card]: download and install (tests)
        char *c = strtok(NULL, " \t\r\n");
        int64_t t0 = esp_timer_get_time();
        bool ok = tfn_store_install(b, c && !strcmp(c, "card") ? TFN_CARD : TFN_BOARD, NULL, msg, sizeof msg);
        ESP_LOGI(TAG, "apps: get %s: %s %s (%lld ms)", b, ok ? "ok" : "FAILED", msg,
                 (long long)((esp_timer_get_time() - t0) / 1000));
        return;
    } else if (a && !strcmp(a, "commit") && b) {
        // apps commit <id> [card]: install the files that came over USB ("sdusb put" into <apps>/.tmp_<id>/: the
        // same files and the same install as the web op=put + op=commit)
        char *c = strtok(NULL, " \t\r\n");
        const bool card = c && !strcmp(c, "card");
        char tmp[48];
        // (an id has no '-': the folder of another installer (TFN_TMP_WEB, TFN_TMP_STORE, TFN_TMP_MOVE) is not
        // a folder of this command)
        if (strlen(b) > 20 || strspn(b, "abcdefghijklmnopqrstuvwxyz0123456789_") != strlen(b)) {
            ESP_LOGW(TAG, "apps: commit %s: FAILED a bad id (a-z 0-9 _, 20 at most)", b);
            return;
        }
        snprintf(tmp, sizeof tmp, "%s/" TFN_TMP_USB "%s", card ? TFN_CARD_DIR : TFN_DATA_DIR, b);
        bool ok = tfn_install(tmp, card ? TFN_CARD : TFN_BOARD, msg, sizeof msg);
        ESP_LOGI(TAG, "apps: commit %s: %s %s", b, ok ? "ok" : "FAILED", msg);
        return;
    } else if (a && !strcmp(a, "rm") && b) {
        tfn_remove(b, msg, sizeof msg);
    } else if (a && !strcmp(a, "dev") && b) {
        tfn_set_dev_mode(b[0] == '1');
    } else if (a && !strcmp(a, "src")) {
        con_apps_src(b, strtok(NULL, " \t\r\n"));
        return;
    } else if (a && !strcmp(a, "vpn") && b) {
        tfn_store_vpn_only(b[0] == '1');     // (tests: the store only over the VPN)
    } else if (a && !strcmp(a, "policy")) {  // apps policy [<name>] | apps policy reset (the permission policy)
        if (b && !strcmp(b, "reset")) {
            tfn_policy_reset();
        }
        tfn_policy_log(b && strcmp(b, "reset") ? b : NULL);
        return;
    }
    if (msg[0]) {
        ESP_LOGI(TAG, "apps: %s", msg);
    }
    for (int i = 0; i < tfn_count(); i++) {
        const tfn_entry_t *e = tfn_entry(i);
        ESP_LOGI(TAG, "apps: %-12s %-8s %s state %d %lu bytes", e->id, e->ver, e->place ? "card" : "board", e->state,
                 (unsigned long)e->bytes);
    }
    ESP_LOGI(TAG, "apps: %d installed, ABI %d, %s, developer mode %s%s", tfn_count(), tfn_abi(), tfn_kind(),
             tfn_dev_mode() ? "on" : "off", tfn_safe_mode() ? ", SAFE MODE" : "");
    return;
}

// "notify"
void con_notify(char *cmd)
{
    // the notices (tests; notify.h): "notify <app>|<title>|<text>[|<link>|<a1>|<a2>|<a3>]" posts one (a notice with
    // buttons gets the answer key "test"); "notify list" = the notices and the answers; "notify clear" = none;
    // "notify rm <id>" = one notice out (a test takes away its own notice)
    char *rest = strtok(NULL, "\r\n");
    if (rest && !strncmp(rest, "rm ", 3)) {
        notify_remove((uint32_t)strtoul(rest + 3, NULL, 10));
        ESP_LOGI(TAG, "notify: %s out", rest + 3);
        return;
    }
    if (!rest || !strcmp(rest, "list")) {
        char *b = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        if (b) {
            notify_json(b, 4096, false);
            ESP_LOGI(TAG, "notify: %s", b);
            free(b);
        }
    } else if (!strcmp(rest, "clear")) {
        notify_clear();
        ESP_LOGI(TAG, "notify: no notices");
    } else {
        char *f[7] = {0};
        int n = 0;
        for (char *q = rest; n < 7;) {
            f[n++] = q;
            char *bar = strchr(q, '|');
            if (!bar) {
                break;
            }
            *bar = 0;
            q = bar + 1;
        }
        const char *act[NT_NACT] = {n > 4 ? f[4] : NULL, n > 5 ? f[5] : NULL, n > 6 ? f[6] : NULL};
        uint32_t id = notify_post_ex(f[0], n > 1 ? f[1] : "", n > 2 ? f[2] : "", n > 3 ? f[3] : "", act,
                                     n > 4 ? "test" : NULL);
        ESP_LOGI(TAG, "notify: id %lu", (unsigned long)id);
    }
}

// "watch"
void con_watch(char *cmd)
{
    // the background checks (watch.h): "watch" = the loaded watchers; "watch now" = a round now with all of them
    char *a = strtok(NULL, " \t\r\n");
    if (a && !strcmp(a, "now")) {
        tfn_watch_now();
    }
    char b[640];
    tfn_watch_json(b, sizeof b);
    ESP_LOGI(TAG, "watch: %s%s", b, a && !strcmp(a, "now") ? " (a round comes within 5 s)" : "");
}

// "lay"
void con_lay(char *cmd)
{
    // the change of the flash layout over the air (main/layout.c): the state; "lay test <n>" = a test stop
    char *a = strtok(NULL, "\r\n");
    layout_console(a);
}

// "mmu"
void con_mmu(char *cmd)
{
    // the MMU map (for the update check): the largest free blocks, then each mapped block
    size_t fd = 0, fp = 0, fx = 0;
    esp_mmu_map_get_max_consecutive_free_block_size(MMU_MEM_CAP_READ | MMU_MEM_CAP_8BIT, MMU_TARGET_FLASH0, &fd);
    esp_mmu_map_get_max_consecutive_free_block_size(MMU_MEM_CAP_READ | MMU_MEM_CAP_8BIT, MMU_TARGET_PSRAM0, &fp);
    esp_mmu_map_get_max_consecutive_free_block_size(MMU_MEM_CAP_EXEC, MMU_TARGET_FLASH0, &fx);
    printf("MMU FREE flash data %u KB, psram data %u KB, flash exec %u KB\n", (unsigned)(fd / 1024),
           (unsigned)(fp / 1024), (unsigned)(fx / 1024));
    esp_mmu_map_dump_mapped_blocks(stdout);
    printf("MMU END\n");
}

// "shot"
void con_shot(char *cmd)
{
    // tests: the next frame in base64 lines (the PC makes a picture of it)
    extern volatile int g_shot_req, g_shot_ready, g_shot_w, g_shot_h, g_shot_busy;
    extern uint16_t *g_shot_buf;
    __atomic_add_fetch(&g_shot_busy, 1, __ATOMIC_SEQ_CST);  // (the main loop keeps the buffer while we use it)
    g_shot_ready = 0;
    g_shot_req = 1;
    for (int i = 0; i < 100 && !g_shot_ready; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!g_shot_ready) {
        printf("SHOT FAILED\n");
    } else {
        static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const uint8_t *p = (const uint8_t *)g_shot_buf;
        int n = g_shot_w * g_shot_h * 2;
        // no log lines of the other tasks while the picture goes out: they would wait for the console,
        // and the main loop with them (a game of 2 BOARDS then loses its link)
        esp_log_level_set("*", ESP_LOG_NONE);
        printf("SHOT %d %d\n", g_shot_w, g_shot_h);
        char line[2 + 1024 + 2];
        for (int i = 0; i < n;) {
            int k = 0;
            line[k++] = '#';
            line[k++] = 'S';
            for (int j = 0; j < 256 && i < n; j++, i += 3) {  // 768 bytes: 1024 characters
                uint32_t v =
                        (uint32_t)p[i] << 16 | (uint32_t)(i + 1 < n ? p[i + 1] : 0) << 8 | (i + 2 < n ? p[i + 2] : 0);
                line[k++] = B64[(v >> 18) & 63];
                line[k++] = B64[(v >> 12) & 63];
                line[k++] = i + 1 < n ? B64[(v >> 6) & 63] : '=';
                line[k++] = i + 2 < n ? B64[v & 63] : '=';
            }
            line[k++] = '\n';
            line[k] = 0;
            fputs(line, stdout);
            vTaskDelay(pdMS_TO_TICKS(2));  // the game (and the idle task) run between the lines
        }
        printf("SHOT END\n");
        fflush(stdout);
        esp_log_level_set("*", ESP_LOG_INFO);
    }
    __atomic_sub_fetch(&g_shot_busy, 1, __ATOMIC_SEQ_CST);
}

// "wake"
void con_wake(char *cmd)
{
    app_screen_wake();  // tests: the screen on (as a touch)
    ESP_LOGI(TAG, "wake");
}

// "dispoff": a test: the display off of the DEEP IDLE now (the screen must be off; a key or "wake" ends it)
void con_dispoff(char *cmd)
{
    board_display_off();
    ESP_LOGI(TAG, "dispoff");
}

// "panel"
void con_panel(char *cmd)
{
    sysui_panel_open();  // tests: the settings panel comes down (as a pull from the top)
    ESP_LOGI(TAG, "panel");
}

// "evlog": the Tab5: the last n lines of the event log (evlog.h; default 40)
void con_evlog(char *cmd)
{
    char *a = strtok(NULL, " \t\r\n");
    size_t n = a ? (size_t)strtoul(a, NULL, 10) : 40, end = evlog_count(), pos = end > n ? end - n : 0;
    char b[1024];
    size_t k;
    while ((k = evlog_read(&pos, b, sizeof b - 1)) > 0) {
        b[k] = 0;
        printf("%s", b);
    }
    printf("evlog end %u\n", (unsigned)end);
}

// "sleepnow"
void con_sleepnow(char *cmd)
{
    // the screen saver now: "sleepnow" dims (off 15 s later), "sleepnow off" = off at once
    char *a = strtok(NULL, " \t\r\n");
    app_saver_now(a && !strcmp(a, "off"));
    ESP_LOGI(TAG, "sleepnow");
}

#if BOARD_BOOT_MENU && CONFIG_BOOTLOADER_CUSTOM_RESERVE_RTC
// "bootslot"
void con_bootslot(char *cmd)
{
    // tests of the boot menu (Tab5Boot): "bootslot N" = one start of the OTA slot N (0 tfn_a, 1 tfn_b, 2 dos), now
    extern void board_boot_slot(int slot);
    char *a = strtok(NULL, " \t\r\n");
    if (a) {
        board_boot_slot(atoi(a));
    }
}
#endif

// "menusel": tests: the menu goes to an app (and "hold": the action of a hold)
void con_menusel(char *cmd)
{
    extern void menu_test_select(const char *name, bool hold);
    char *a = strtok(NULL, " \t\r\n");
    char *b = strtok(NULL, " \t\r\n");
    if (a) {
        menu_test_select(a, b && !strcmp(b, "hold"));
    }
}

// "menuedit": tests: the edit mode of HOME 2.0 (as a hold on the background of the menu)
void con_menuedit(char *cmd)
{
    extern void menu_test_edit(void);
    menu_test_edit();
}

// "open"
void con_open(char *cmd)
{
    char *a = strtok(NULL, "\r\n");
    const app_t *app = a ? app_find(a) : NULL;
    if (app) {
        app_open(app);
    }
    ESP_LOGI(TAG, "open %s: %s", a ? a : "?", app ? "ok" : "no such app");
}

#if CONFIG_TFN_EXTRA_WEBCAM
// "wcam"
void con_wcam(char *cmd)
{
    // the Watcher: one AT command to the Himax AI chip (the camera), "wcam reset", "wcam pic" (webcam.c)
    char *a = strtok(NULL, "\r\n");
    while (a && *a == ' ') {
        a++;
    }
    webcam_console(a);
}
#endif

// "quiet"
void con_quiet(char *cmd)
{
    // tests from the PC without sound: "quiet 1" = speaker off, "quiet 0" = the settings again
    char *a = strtok(NULL, " \t\r\n");
    bool q = !a || atoi(a);
    board_speaker(!q);
    if (q) {
        audio_set_volume(0);
    } else {
        settings_apply();
    }
    ESP_LOGI(TAG, "quiet %d", q);
}

// the voice reader (tfn/speech.h): "say read <text file> <cache folder> [hold s] [line]", "say rd" (its state),
// "say play", "say pause", "say close"; a voice file: "say make <path> <text>", "say file <path>" (play it);
// SPEAK AT AN ALARM: "say alarm on|off", a test timer "say timer <s> [label]", "say ringoff" (stop the ring)
static bool con_say_more(char *a)
{
    if (!strncmp(a, "read ", 5)) {
        char t[128], c[128];
        int hold = 0, line = 0;
        if (sscanf(a + 5, "%127s %127s %d %d", t, c, &hold, &line) >= 2) {
            speech_read_cfg_t cfg = {.text = t, .cache = c, .line = line, .hold_s = hold, .play = true};
            ESP_LOGI(TAG, "say read: %s", speech_read_open(&cfg) ? "open" : "FAILED");
        }
    } else if (!strcmp(a, "rd")) {
        speech_read_t s;
        speech_read_get(&s);
        ESP_LOGI(TAG, "reader: state %d play %d line %d/%d made %d ahead %d ms wait %d ms rate %d %%", s.state, s.play,
                 s.line, s.lines, s.made, s.ahead_ms, s.wait_ms, s.rate_pc);
    } else if (!strcmp(a, "play") || !strcmp(a, "pause")) {
        speech_read_play(a[1] == 'l');
    } else if (!strcmp(a, "close")) {
        speech_read_close();
    } else if (!strncmp(a, "make ", 5)) {
        char p[128];
        int k = 0;
        if (sscanf(a + 5, "%127s %n", p, &k) == 1 && k > 0) {
            ESP_LOGI(TAG, "say make: %s", speech_make(a + 5 + k, p) ? "queued" : "FAILED");
        }
    } else if (!strncmp(a, "file ", 5)) {
        ESP_LOGI(TAG, "say file: %d ms", speech_play_file(a + 5));
    } else if (!strcmp(a, "alarm on") || !strcmp(a, "alarm off")) {
        alarm_voice_set(a[7] == 'n');
        ESP_LOGI(TAG, "speak at an alarm %s", alarm_voice_on() ? "on" : "off");
    } else if (!strncmp(a, "timer ", 6)) {  // (a test: a timer of N s, as CLOCK makes it)
        ctimer_t t[TIMER_MAX];
        const int n = timer_get(t, TIMER_MAX);
        int k = 0, len = 0, at = 0;
        while (k < n && t[k].state != TIMER_FREE) {
            k++;
        }
        if (k < n && sscanf(a + 6, "%d %n", &len, &at) >= 1 && len > 0) {
            memset(&t[k], 0, sizeof t[k]);
            t[k].state = TIMER_RUN;
            t[k].vol = 30;
            t[k].len_s = (uint32_t)len;
            t[k].end_ms = esp_timer_get_time() / 1000 + len * 1000LL;
            snprintf(t[k].label, sizeof t[k].label, "%s", at > 0 ? a + 6 + at : "");
            timer_set(t, n);
        }
        ESP_LOGI(TAG, "say timer: %s", k < n && len > 0 ? "set" : "no free timer");
    } else if (!strcmp(a, "ringoff")) {
        alarm_stop();
    } else {
        return false;
    }
    return true;
}

// "say <text>": the voice says the text (tfn_speech.h: a board with the app VOICE); "say": its state; "say stop";
// "listen": the ears and the small brains (tfn_listen.h): "listen", "listen ask <text>", "listen key <text>",
// "listen down" / "listen up" (a virtual talk key)
void con_listen(char *cmd)
{
    (void)cmd;
    char *a = strtok(NULL, " \r\n");
    char *b = strtok(NULL, "\r\n");
    listen_console(a ? a : "", b);
}

// "say on" / "say off": the setting READ NOTICES ALOUD; the reader and the voice files: con_say_more()
void con_say(char *cmd)
{
    (void)cmd;
    char *a = strtok(NULL, "\r\n");
    if (!a || !a[0]) {
        ESP_LOGI(TAG, "say: %s, read notices aloud %s", speech_state(), speech_notices_on() ? "on" : "off");
    } else if (con_say_more(a)) {
        return;
    } else if (!strcmp(a, "stop")) {
        speech_stop();
    } else if (!strcmp(a, "on") || !strcmp(a, "off")) {
        speech_set_notices(a[1] == 'n');
        ESP_LOGI(TAG, "read notices aloud %s", a[1] == 'n' ? "on" : "off");
    } else {
        speech_say(a);
    }
}

// "snd"
void con_snd(char *cmd)
{
    // the level of the mix (also with quiet 1: the meter is before the VOLUME): "snd" = since the last call,
    // "snd N" = during the next N seconds. peak = the sum of the sounds, percent of full scale (over 100 = the
    // limiter works); limiter = its lowest gain; notes = the loudest note of MIDI_BG, percent (over 100 = cut)
    char *a = strtok(NULL, " \t\r\n");
    int secs = a ? atoi(a) : 0;
    if (secs > 0) {
        audio_peak_take();
        audio_limiter_take();
        midi_bg_over_take();
        vTaskDelay(pdMS_TO_TICKS(secs * 1000));
    }
    int pk = audio_peak_take(), lim = audio_limiter_take(), ov = midi_bg_over_take();
    ESP_LOGI(TAG, "snd: peak %d %%, limiter %d %%, notes %d %%, volume %d", pk, lim, ov, g_settings.volume);
}

// "clock"
void con_clock(char *cmd)
{
    clock_log();  // the clock of the board and its chip (alarm.c)
}

// "standby": the state of STANDBY; "standby show|hide|fetch", "standby face|on|night <n>" (standby.h)
void con_standby(char *cmd)
{
    standby_console(NULL);  // (the arguments: the rest of the line, strtok)
}

#if CONFIG_TFN_EXTRA_OREF
// "oref"
void con_oref(char *cmd)
{
    oref_log();  // HOME FRONT ALERTS: the watch (oref.c)
}
#endif

// "pw"
void con_pw(char *cmd)
{
    // the power report (power.c); "pw off N" = POWER OFF (the choice 0..5), "pw ls 0/1" = the light sleep
    char *a = strtok(NULL, " \t\r\n"), *v = strtok(NULL, " \t\r\n");
    if (a && v && !strcmp(a, "off")) {
        power_set_off_choice(atoi(v));
    }
    if (a && v && !strcmp(a, "ls")) {
        power_set_light_sleep(atoi(v) != 0);
    }
    power_log();
}

#if BOARD_PMU_LOG
// "pmu"
void con_pmu(char *cmd)
{
    // the power log of the AXP2101: the sources of the last turn-on and turn-off, the voltages; "pmu N": N s
    char *a = strtok(NULL, " \t\r\n"), *b = strtok(NULL, " \t\r\n");
    if (a && b && !strcmp(a, "a3")) {
        board_pmu_a3(!strcmp(b, "on"));  // "pmu a3 on|off": the heat guard
    } else {
        board_pmu_log(a ? atoi(a) : 0);
    }
}
#endif

// "bat"
void con_bat(char *cmd)
{
    bool chg;
    int mv;
    int pct = board_battery(&chg, &mv);
    ESP_LOGI(TAG, "battery %d %% (-1 = none), %d mV, %s", pct, mv, chg ? "charging / USB" : "not charging");
}

// "ls"
void con_ls(char *cmd)
{
    // all the files under a folder (default /data) with the size and the CRC-32, for a
    // check of the files against a copy on the PC
    char *a = strtok(NULL, "\r\n");
    int n = 0;
    uint64_t sum = 0;
    list_dir(a ? a : STORAGE_ROOT, 0, &n, &sum);
    ESP_LOGI(TAG, "ls: %d files, %llu bytes", n, (unsigned long long)sum);
}

// "ota"
void con_ota(char *cmd)
{
    ota_log_info();  // the app slots (the firmware update)
}

// "id": the device identity (tfn_id.h), only public values
void con_id(char *cmd)
{
    const uint8_t *pk = tfn_id_pk(), *xs = tfn_id_xs();
    printf("ID %s ALG p256 PK ", pk ? tfn_id_str() : "-");
    for (int i = 0; pk && i < TFN_ID_PK_LEN; i++) {
        printf("%02x", pk[i]);
    }
    printf("%s X25519 %s XS ", pk ? "" : "-", xs ? tfn_id_x25519() : "-");
    for (int i = 0; xs && i < TFN_ID_XS_LEN; i++) {
        printf("%02x", xs[i]);
    }
    printf("%s\n", xs ? "" : "-");
}

// "menu"
void con_menu(char *cmd)
{
    // the menu layout (a profile of the manager): "menu" prints it, "menu reset" = the built-in layout
    char *a = strtok(NULL, " \r\n");
    if (a && !strcmp(a, "reset")) {
        char err[64];
        menu_layout_set("RESET", 5, err, sizeof err);
        ESP_LOGI(TAG, "menu: the built-in layout");
    } else if (a && !strcmp(a, "drop")) {
        menu_home_drop();  // HOME 2.0: the own home of the board goes; the profile shows
        ESP_LOGI(TAG, "menu: the own home is gone");
    } else {
        char *b = heap_caps_malloc(4096, MALLOC_CAP_SPIRAM);
        if (b) {
            menu_layout_get(b, 4096);
            ESP_LOGI(TAG, "menu: profile \"%s\", hash %lu%s", menu_profile(), (unsigned long)menu_profile_hash(),
                     menu_home_own() ? ", the own home of the board" : "");
            for (char *l = strtok(b, "\n"); l; l = strtok(NULL, "\n")) {
                ESP_LOGI(TAG, "  %s", l);
            }
            free(b);
        }
    }
}

// "df"
void con_df(char *cmd)
{
    uint64_t total = 0, free_b = 0;
    esp_vfs_fat_info(STORAGE_ROOT, &total, &free_b);
    ESP_LOGI(TAG, "df %s: %llu KB total, %llu KB free", STORAGE_ROOT, (unsigned long long)(total / 1024),
             (unsigned long long)(free_b / 1024));
}

// "sdformat"
void con_sdformat(char *cmd)
{
    // (a new FAT file system on the SD card, as FILES > CARD TOOLS > FORMAT; ALL its files go)
    extern bool board_sd_format(void);
    ESP_LOGI(TAG, "sdformat: %s", board_sd_format() ? "SD FORMAT OK" : "SD FORMAT FAILED");
}

// "store"
void con_store(char *cmd)
{
    // a value of the settings store (the saves of the apps): "store k_place" reads, "store k_place 6" writes
    char *k = strtok(NULL, " \t\r\n"), *v = strtok(NULL, " \t\r\n");
    if (!k || strlen(k) > 15) {
        ESP_LOGI(TAG, "use: store <key> [value]");
    } else {
        if (v) {
            store_set(k, (uint32_t)strtoul(v, NULL, 0));
        }
        ESP_LOGI(TAG, "store %s = %lu (0x%lx)", k, (unsigned long)store_get(k, 0), (unsigned long)store_get(k, 0));
    }
}

// "storeall"
void con_storeall(char *cmd)
{
    // tests: every saved value of the settings store ("storeall <key> <value>"), then "storeall end <n>"
    int n = 0;
    nvs_handle_t h;
    if (nvs_open("arcade", NVS_READONLY, &h) == ESP_OK) {
        nvs_iterator_t it = NULL;
        esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, "arcade", NVS_TYPE_U32, &it);
        while (e == ESP_OK) {
            nvs_entry_info_t info;
            nvs_entry_info(it, &info);
            uint32_t v = 0;
            if (nvs_get_u32(h, info.key, &v) == ESP_OK) {
                printf("storeall %s %lu\n", info.key, (unsigned long)v);
                n++;
            }
            e = nvs_entry_next(&it);
        }
        nvs_release_iterator(it);
        nvs_close(h);
    }
    for (int i = 0; i < store_spill_count(); i++) {  // (the keys of the spill store too)
        char sk[16];
        uint32_t sv;
        if (store_spill_get(i, sk, &sv)) {
            printf("storeall %s %lu\n", sk, (unsigned long)sv);
            n++;
        }
    }
    printf("storeall end %d\n", n);
    fflush(stdout);
}

// "storedel"
void con_storedel(char *cmd)
{
    // tests: remove one saved value ("storedel <key>"), so a test can put back a key that was not there
    char *k = strtok(NULL, " \t\r\n");
    nvs_handle_t h;
    esp_err_t e = ESP_FAIL;
    if (k && strlen(k) <= 15 && nvs_open("arcade", NVS_READWRITE, &h) == ESP_OK) {
        e = nvs_erase_key(h, k);
        if (e == ESP_OK) {
            nvs_commit(h);
        }
        nvs_close(h);
    }
    if (k && store_spill_del(k)) {
        e = ESP_OK;  // (a key of the spill store)
    }
    ESP_LOGI(TAG, "storedel %s: %s", k ? k : "?", esp_err_to_name(e));
}

// "nvs"
void con_nvs(char *cmd)
{
    // the use of the NVS (the names and the counts only, no values): the entries of each namespace by type, the
    // size of the blobs and strings, and the keys of the spill store
    nvs_stats_t stt;
    if (nvs_get_stats(NULL, &stt) == ESP_OK) {
        printf("nvs: %u used, %u free, %u total entries, %u namespaces\n", (unsigned)stt.used_entries,
               (unsigned)stt.free_entries, (unsigned)stt.total_entries, (unsigned)stt.namespace_count);
    }
    nvs_iterator_t it = NULL;
    esp_err_t e = nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &it);
    while (e == ESP_OK) {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);
        size_t len = 0;
        if (info.type == NVS_TYPE_BLOB || info.type == NVS_TYPE_STR) {
            nvs_handle_t hh;
            if (nvs_open(info.namespace_name, NVS_READONLY, &hh) == ESP_OK) {
                if (info.type == NVS_TYPE_BLOB) {
                    nvs_get_blob(hh, info.key, NULL, &len);
                } else {
                    nvs_get_str(hh, info.key, NULL, &len);
                }
                nvs_close(hh);
            }
        }
        if (info.type != NVS_TYPE_U32 || strcmp(info.namespace_name, "arcade")) {
            printf("nvs: %s %s type %d %u bytes\n", info.namespace_name, info.key, (int)info.type, (unsigned)len);
        }
        e = nvs_entry_next(&it);
    }
    nvs_release_iterator(it);
    printf("nvs: the spill store has %d keys\n", store_spill_count());
    printf("nvs end\n");
    fflush(stdout);
}

#if CONFIG_TFN_EXTRA_RF_NATIVE
// "rf"
void con_rf(char *cmd)
{
    // the native radio (main/rf_test.c): rf boot / exit / open / cap / scan
    extern void rf_console(char *args);
    rf_console(strtok(NULL, "\r\n"));
}
#endif

// "mem"
void con_mem(char *cmd)
{
    char *a = strtok(NULL, " \t\r\n");
    ESP_LOGI(TAG, "mem: internal %u free (%u min, %u block), PSRAM %u free",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    if (a && !strcmp(a, "tasks")) {
        // "mem tasks": each task and its stack (internal RAM or PSRAM, the size, the bytes never used)
        UBaseType_t n = uxTaskGetNumberOfTasks() + 4;
        TaskStatus_t *ts = heap_caps_malloc(n * sizeof(TaskStatus_t), MALLOC_CAP_SPIRAM);
        if (ts) {
            n = uxTaskGetSystemState(ts, n, NULL);
            size_t sum = 0;
            for (UBaseType_t i = 0; i < n; i++) {
                bool in = esp_ptr_internal(ts[i].pxStackBase);
                size_t sz = heap_caps_get_allocated_size(ts[i].pxStackBase);
                if (in) {
                    sum += sz;
                }
                // (the state: R runs, r ready, B waits (blocked), S stopped, D deleted)
                static const char ST[] = "RrBSD?";
                const unsigned e = (unsigned)ts[i].eCurrentState < 5 ? (unsigned)ts[i].eCurrentState : 5;
                printf("memtask %-16s prio %2u %c stack %s %5u (%u never used)\n", ts[i].pcTaskName,
                       (unsigned)ts[i].uxCurrentPriority, ST[e], in ? "INT  " : "PSRAM", (unsigned)sz,
                       (unsigned)ts[i].usStackHighWaterMark);
            }
            printf("memtask end: %u tasks, %u bytes of stacks in internal RAM\n", (unsigned)n, (unsigned)sum);
            heap_caps_free(ts);
        }
    } else if (a && !strcmp(a, "heap")) {
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL);  // "mem heap": each region of the internal RAM
    }
}
