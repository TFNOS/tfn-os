// TFN OS: the scan: the read of a manifest, the scan of the app folders, the rescan, the tick, the clean-up of the upload folders (main/tfn_app.c has the rest; tfn_app_priv.h).
#include "tfn_app_priv.h"

static const char *TAG = "tfn";

// ---------------------------------------------------------------- the scan
// the manifest of a folder: true when it is an app of this board kind (e->state tells if it can run)
bool tfa_read_entry(const char *dir, int place, tfn_entry_t *e)
{
    char path[72];
    uint8_t *m = NULL, *sig = NULL;
    size_t ml = 0, sl = 0;
    snprintf(path, sizeof path, "%s/manifest.txt", dir);
    if (!tfa_read_all(path, &m, &ml, 4096)) {
        return false;
    }
    memset(e, 0, sizeof *e);
    const char *t = (const char *)m;
    char kind[16];
    tfa_mval(t, "id", e->id, sizeof e->id);
    tfa_mval(t, "name", e->name, sizeof e->name);
    tfa_mval(t, "ver", e->ver, sizeof e->ver);
    tfa_mval(t, "group", e->group, sizeof e->group);
    tfa_mval(t, "saves", e->saves, sizeof e->saves);
    tfa_mval(t, "elf_sha256", e->elf_sha, sizeof e->elf_sha);
    tfa_mval(t, "watch_sha256", e->watch_sha, sizeof e->watch_sha);
    e->watch_min = (uint16_t)tfa_mnum(t, "watch");
    char fm[4], oa[4];
    tfa_mval(t, "form", fm, sizeof fm);
    tfa_mval(t, "oauth", oa, sizeof oa);
    e->form = fm[0] || oa[0];  // (a web login alone also has the page)
    tfa_mval(t, "kind", kind, sizeof kind);
    e->code = (uint32_t)tfa_mnum(t, "code");
    e->abi = (uint16_t)tfa_mnum(t, "abi");
    e->rev = (uint16_t)tfa_mnum(t, "rev");
    char pl[96];
    bool unknown = false;
    tfa_mval(t, "perms", pl, sizeof pl);
    e->perms = tfn_perms_parse(pl, &unknown);
    e->bytes = (uint32_t)tfa_mnum(t, "bytes");
    e->ram = (uint32_t)tfa_mnum(t, "ram");
    e->place = (uint8_t)place;
    snprintf(e->dir, sizeof e->dir, "%s", dir);
    if (!tfa_id_ok(e->id) || !e->name[0] || strcmp(kind, TFN_KIND)) {
        free(m);
        return false;
    }
    // the signatures: "sig" (the store; a built-in key first, as before the publisher list), then "sigs" (the
    // developers: an added TEST key, or a DEV key = NOT REVIEWED). tfn_pub.h
    uint8_t *sigs = NULL, pid[TFN_PUB_ID];
    size_t gl = 0;
    snprintf(path, sizeof path, "%s/sig", dir);
    tfa_read_all(path, &sig, &sl, 256);
    if (tfn_pub_manifest_pub(t, ml, pid)) {  // (no publisher= in the manifest: no read of "sigs", as before the list)
        snprintf(path, sizeof path, "%s/sigs", dir);
        tfa_read_all(path, &sigs, &gl, TFN_PUB_SIGS_FILE);
    }
    int pv = tfn_pub_app_check(m, ml, sig, sl, (const char *)sigs, gl, e->pub);
    free(sigs);
    e->pub_st = (uint8_t)pv;
    bool signed_ok = pv == TFN_PUB_OK_BUILTIN || pv == TFN_PUB_OK_STORE || pv == TFN_PUB_OK_TEST;
    struct stat st;
    snprintf(path, sizeof path, "%s/app.elf", dir);
    bool file_ok = stat(path, &st) == 0 && (uint32_t)st.st_size == e->bytes && strlen(e->elf_sha) == 64;
    if (!file_ok) {
        e->state = TFN_BAD_FILE;
    } else if (e->abi != TFN_ABI_VERSION || e->rev > TFN_ABI_REV || unknown) {
        e->state = TFN_BAD_ABI;  // (a newer firmware)
    } else if (!signed_ok) {
        e->state = tfn_dev_mode() ? TFN_DEV : TFN_BAD_SIG;
    } else {
        e->state = TFN_OK;
    }
    char key[16];
    tfa_crash_key(e->id, key);
    if (tfn_runs(e) && store_get(key, 0) >= 2) {
        e->state = TFN_STOPPED;
    }
    // the menu item
    e->stub.name = e->name;
    e->stub.landscape = tfa_mnum(t, "landscape") != 0;
    e->stub.native = tfa_mnum(t, "native") != 0;
    e->stub.round = (uint8_t)tfa_mnum(t, "round");  // (APP_ROUND_HI = 2 stays)
    e->stub.round_fit = tfa_mnum(t, "round_fit") != 0;
    e->stub.wheel = tfa_mnum(t, "wheel") != 0;
    e->stub.key = tfa_mnum(t, "key") != 0;
    e->is_new = tfa_mnum(t, "new") != 0;
    e->stub.run = tfa_mnum(t, "run") != 0;
    e->stub.needs = (uint8_t)tfa_mnum(t, "needs");
    e->stub.enter = tfa_stub_enter;
    e->stub.input = tfa_stub_input;
    e->stub.update = tfa_stub_frame;
    e->stub.render = tfa_stub_frame;
    free(m);
    free(sig);
    return true;
}

static void scan_dir(const char *root, int place)
{
    DIR *d = opendir(root);
    if (!d) {
        return;
    }
    struct dirent *de;
    while ((de = readdir(d))) {
        if (de->d_name[0] == '.' || strlen(de->d_name) > 20) {
            continue;
        }
        // (the scan stops at TFN_MAX. The log names each app that has no place, and the app list of /api/apps counts
        // them: "unlisted".)
        if (tfa_n >= TFN_MAX) {
            bool known = false;
            for (int i = 0; i < tfa_n; i++) {
                if (!strcmp(tfa_e[i].id, de->d_name)) {
                    known = true;
                }
            }
            if (!known) {
                tfa_dropped++;
                ESP_LOGW(TAG, "%s/%s: no place in the list of %d installed apps: the menu does not show it", root,
                         de->d_name, TFN_MAX);
            }
            continue;
        }
        char dir[40];
        snprintf(dir, sizeof dir, "%s/%s", root, de->d_name);
        tfn_entry_t e;
        if (!tfa_read_entry(dir, place, &e)) {
            continue;
        }
        int same = -1;
        for (int i = 0; i < tfa_n; i++) {
            if (!strcmp(tfa_e[i].id, e.id)) {
                same = i;
            }
        }
        if (same >= 0) {  // the same app in two places: the one that can run, then the newer, then the board
            tfn_entry_t *o = &tfa_e[same];
            bool better = (tfn_runs(&e) && !tfn_runs(o)) || (tfn_runs(&e) == tfn_runs(o) && e.code > o->code);
            if (better) {
                *o = e;
            }
            continue;
        }
        tfa_e[tfa_n++] = e;
    }
    closedir(d);
}

// the list is read and changed only on the app task (main.c): another task (the web server) asks for a scan, and
// tfn_tick() does it when no installed app is open (an open app keeps its entry)
static volatile bool s_rescan_req;
static void do_rescan_locked(void);
void tfa_do_rescan(void)
{
    LOCK();
    do_rescan_locked();
    UNLOCK();
}
static void do_rescan_locked(void)
{
    if (!tfa_e) {
        return;
    }
#if TFA_ICON_BIG
    tfa_icon_gen++;
#endif
    int64_t t0 = esp_timer_get_time();
    for (int k = 0; k < TFN_MAX; k++) {
        free(tfa_icon[k]);
        tfa_icon[k] = NULL;
        tfa_icon_bad[k] = false;
    }
    tfa_icon_bytes = 0;
    for (int i = 0; i < DEC_SLOTS; i++) {
        tfa_dec[i].b = NULL;  // (the icons go: no slot keeps their place)
    }
    tfa_n = 0;
    tfa_dropped = 0;
    if (!tfa_safe) {
        scan_dir(TFN_DATA_DIR, TFN_BOARD);
        if (board_sd_mounted()) {
            scan_dir(TFN_CARD_DIR, TFN_CARD);
        }
    }
    for (int i = 0; i < tfa_n; i++) {
        tfa_e[i].stub.name = tfa_e[i].name;  // (read_entry pointed it at its own copy: here is the list place)
        tfa_e[i].stub.draw_icon = tfa_icon_fns[i];
    }
    tfa_gen++;
    tfa_sig_cache_save();  // (the new good signatures of this scan: the next start needs no check)
    int ok = 0;
    for (int i = 0; i < tfa_n; i++) {
        ok += tfn_runs(&tfa_e[i]);
    }
    ESP_LOGI(TAG, "%d installed apps (%d can run), card %s, developer mode %s (%lld ms)", tfa_n, ok,
             board_sd_mounted() ? "in" : "out", tfn_dev_mode() ? "on" : "off",
             (long long)((esp_timer_get_time() - t0) / 1000));
}

void tfn_rescan(void)
{
    s_rescan_req = true;
}

const tfn_entry_t *tfn_entry_of(const app_t *a);
void tfn_tick(void)
{
    tfn_pub_tick();  // (the prompt of a key that waits for a tap of the board owner)
    if (!s_rescan_req || tfn_entry_of(app_current())) {
        return;
    }
    s_rescan_req = false;
    bool all = true;
    for (int j = 0; j < 2; j++) {
        if (!tfa_unload(j)) {
            all = false;  // (none of them is open)
        }
    }
    if (!all) {
        s_rescan_req = true;
        return;
    }  // (a task of an app still runs: later)
    tfa_do_rescan();
}

// The upload folders ".tmp_*" of installs that did not end (a full storage, a broken link, a commit that never
// came) would stay for ever and fill the board storage (2.5 MB of 9 MB), and each new install would fail with
// "the board storage is full". No install runs at the start: they all go. tfn_tmp_drop() removes only the files of
// a folder: a subfolder (a deep tree in a folder of the card) stays, and the start needs no deep stack.
static void tmp_clean(const char *root)
{
    DIR *d = opendir(root);
    if (!d) {
        return;
    }
    char names[8][32];
    int n = 0;
    struct dirent *de;
    while ((de = readdir(d)) && n < 8) {
        if (!strncmp(de->d_name, ".tmp_", 5) && strlen(de->d_name) < sizeof names[0]) {
            snprintf(names[n++], sizeof names[0], "%s", de->d_name);
        }
    }
    closedir(d);
    for (int i = 0; i < n; i++) {
        char p[96];
        snprintf(p, sizeof p, "%.40s/%.31s", root, names[i]);
        ESP_LOGI(TAG, "the folder of an install that did not end: %s %s", p,
                 tfn_tmp_drop(p) ? "removed" : "NOT removed (a folder in it?)");
    }
}
void tfn_tmp_clean(void)
{
    tmp_clean(TFN_DATA_DIR);
    if (board_sd_mounted()) {
        tmp_clean(TFN_CARD_DIR);
    }
}
