// TFN OS: install and remove: the upload of a package, its checks, its place, the remove (main/tfn_app.c has the rest; tfn_app_priv.h).
#include "tfn_app_priv.h"

static const char *TAG = "tfn";

// ---------------------------------------------------------------- install and remove
// The folders under an app folder that a remove goes into. An app folder has no subfolders, but the file API can make
// a deep tree in it: each level needs a stack frame, and the limit keeps the stack of the task that removes it safe.
#define RM_TREE_DEPTH 8

// a folder with all its content, depth folders under the first one (the path buffer is on the heap, not the stack)
static bool rm_tree_at(const char *dir, int depth)
{
    if (depth > RM_TREE_DEPTH) {
        return false;
    }
    DIR *d = opendir(dir);
    if (!d) {
        return errno == ENOENT;
    }
    char *p = malloc(300);
    struct dirent *de;
    while (p && (de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
            continue;
        }
        snprintf(p, 300, "%s/%s", dir, de->d_name);
        struct stat st;
        if (stat(p, &st) == 0 && S_ISDIR(st.st_mode)) {
            rm_tree_at(p, depth + 1);
        } else {
            unlink(p);
        }
    }
    closedir(d);
    free(p);
    return rmdir(dir) == 0;
}

bool tfa_rm_tree(const char *dir)
{
    return rm_tree_at(dir, 0);
}

bool tfn_tmp_drop(const char *tmp)
{
    const char *name = strrchr(tmp, '/');
    if (!name || strncmp(name + 1, ".tmp_", 5)) {
        return false;  // (only an upload folder: never the folder of an installed app)
    }
    DIR *d = opendir(tmp);
    if (!d) {
        return errno == ENOENT;
    }
    struct dirent *de;
    struct stat st;
    char p[96];
    while ((de = readdir(d))) {
        // (the files only: a remove that goes into each subfolder needs a stack frame for each level, and a deep
        // tree in a folder of the card would then stop the task that removes it)
        const int n = snprintf(p, sizeof p, "%s/%s", tmp, de->d_name);
        if (n > 0 && n < (int)sizeof p && !(stat(p, &st) == 0 && S_ISDIR(st.st_mode))) {
            unlink(p);
        }
    }
    closedir(d);
    return rmdir(tmp) == 0;
}

// ---------------------------------------------------------------- one install of an app at a time
// More than one task installs apps: the web server (an upload, a move, a remove), the STORE worker (a download, a
// move), the console (a USB upload, "apps get") and FILES. Two of them with the same app at the same time would change
// the same folders, and the file system has no lock for a file that another task has open (CONFIG_FATFS_FS_LOCK is
// 0): a remove of an open file can damage the file system. So each install, move, store download and remove marks its
// app id first, and a second one on another task stops with a clear text.
#define INST_MAX 6  // (one row for each task that installs, and two more)
static struct {
    char id[24];
    TaskHandle_t task;
    int depth;
} s_inst[INST_MAX];
static portMUX_TYPE s_inst_mux = portMUX_INITIALIZER_UNLOCKED;

bool tfn_inst_begin(const char *id, char *msg, int len)
{
    const TaskHandle_t me = xTaskGetCurrentTaskHandle();
    const size_t n = strlen(id);
    int mine = -1, spare = -1, other = 0;
    if (!n || n >= sizeof s_inst[0].id) {
        snprintf(msg, len, "a bad id");
        return false;
    }
    portENTER_CRITICAL(&s_inst_mux);
    for (int i = 0; i < INST_MAX; i++) {
        if (!s_inst[i].id[0]) {
            spare = spare < 0 ? i : spare;
        } else if (!strcmp(s_inst[i].id, id)) {
            mine = s_inst[i].task == me ? i : mine;
            other |= s_inst[i].task != me;
        }
    }
    if (mine >= 0) {
        s_inst[mine].depth++;
    } else if (!other && spare >= 0) {
        memcpy(s_inst[spare].id, id, n + 1);
        s_inst[spare].task = me;
        s_inst[spare].depth = 1;
    }
    portEXIT_CRITICAL(&s_inst_mux);
    if (mine < 0 && (other || spare < 0)) {
        snprintf(msg, len,
                 other ? "%s: another install, move or remove of this app runs now: try again"
                       : "%s: too many installs at a time: try again",
                 id);
        ESP_LOGW(TAG, "%s", msg);
        return false;
    }
    return true;
}

void tfn_inst_end(const char *id)
{
    const TaskHandle_t me = xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&s_inst_mux);
    for (int i = 0; i < INST_MAX; i++) {
        if (s_inst[i].id[0] && s_inst[i].task == me && !strcmp(s_inst[i].id, id) && --s_inst[i].depth <= 0) {
            s_inst[i].id[0] = 0;
        }
    }
    portEXIT_CRITICAL(&s_inst_mux);
}

bool tfn_inst_busy(void)
{
    bool busy = false;
    portENTER_CRITICAL(&s_inst_mux);
    for (int i = 0; i < INST_MAX; i++) {
        busy |= s_inst[i].id[0] != 0;
    }
    portEXIT_CRITICAL(&s_inst_mux);
    return busy;
}

static bool copy_file(const char *from, const char *to)
{
    FILE *a = fopen(from, "rb"), *b = a ? fopen(to, "wb") : NULL;
    char *buf = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM);
    bool ok = a && b && buf;
    size_t n;
    while (ok && (n = fread(buf, 1, 8192, a)) > 0) {
        ok = fwrite(buf, 1, n, b) == n;
    }
    if (a) {
        fclose(a);
    }
    if (b && fclose(b)) {
        ok = false;
    }
    free(buf);
    return ok;
}

static bool open_now(const char *id)
{
    const tfn_entry_t *e = tfn_entry_of(app_current());
    return e && !strcmp(e->id, id);
}

// the text of an install with no manifest.txt in dir. When the upload folder of the same id in the other place has a
// manifest.txt, the files went there (a put to one place, the commit to the other). Else the files of dir tell the
// cause, for example "app.elf 505764 B" alone: a package that is not complete.
static void why_no_manifest(const char *dir, char *msg, int len)
{
    static const char *const FILES[] = TFN_APP_FILES;
    const char *name = strrchr(dir, '/');
    const bool card = !strncmp(dir, TFN_CARD_DIR "/", sizeof TFN_CARD_DIR);
    char p[72];
    struct stat st;
    if (name && !strncmp(name + 1, ".tmp_", 5) && (card || board_sd_mounted())) {
        const int n = snprintf(p, sizeof p, "%s%s", card ? TFN_DATA_DIR : TFN_CARD_DIR, name);
        if (n > 0 && n < (int)sizeof p - 14) {
            snprintf(p + n, sizeof p - n, "/manifest.txt");
            if (stat(p, &st) == 0) {
                p[n] = 0;
                snprintf(msg, len,
                         "no manifest.txt in %s, but %s has one: the files and the commit need the same place", dir, p);
                return;
            }
        }
    }
    const bool folder = stat(dir, &st) == 0;
    int k = snprintf(msg, len, "no manifest.txt in %s (it has: ", dir);
    int have = 0;
    for (int i = 0; i < TFN_APP_NFILES && folder && k < len; i++) {
        snprintf(p, sizeof p, "%s/%s", dir, FILES[i]);
        if (stat(p, &st) == 0) {
            k += snprintf(msg + k, len - k, "%s%s %ld B", have++ ? ", " : "", FILES[i], (long)st.st_size);
        }
    }
    if (k < len) {
        snprintf(msg + k, len - k, "%s): an install sends manifest.txt first",
                 have ? "" : (folder ? "no file" : "no folder"));
    }
}

// why read_entry() refused a folder (a clear text for each fault; one text for all faults, "not an app of this board
// kind", would hide for example a manifest that did not come). noinline: its buffers stay out of the frame of
// tfn_install(), which is on the stack of the web task and of the STORE worker during the whole install.
static __attribute__((noinline)) void entry_why(const char *dir, char *msg, int len)
{
    char path[72];
    snprintf(path, sizeof path, "%s/manifest.txt", dir);
    struct stat st;
    if (stat(path, &st) != 0) {
        why_no_manifest(dir, msg, len);
        return;
    }
    if (st.st_size > 4096) {
        snprintf(msg, len, "manifest.txt is %ld bytes: the board reads 4096 at most", (long)st.st_size);
        return;
    }
    char kind[16] = "", name[24] = "", id[24] = "";
    uint8_t *m = NULL;
    size_t ml = 0;
    if (!tfa_read_all(path, &m, &ml, 4096)) {
        snprintf(msg, len, "the board cannot read %s", path);
        return;
    }
    tfa_mval((const char *)m, "kind", kind, sizeof kind);
    tfa_mval((const char *)m, "name", name, sizeof name);
    tfa_mval((const char *)m, "id", id, sizeof id);
    free(m);
    if (strcmp(kind, TFN_KIND)) {
        snprintf(msg, len, "%s is an app for the board kind %s; this board is %s", id[0] ? id : "the app",
                 kind[0] ? kind : "(none)", TFN_KIND);
    } else if (!name[0]) {
        snprintf(msg, len, "the manifest of %s has no name", id[0] ? id : "the app");
    } else {
        snprintf(msg, len, "the manifest of %s has a bad id (a-z 0-9 _, 20 at most)", name);
    }
}

// the install of the entry e that tfa_read_entry() read from the folder tmp (the caller has the mark of e->id)
static bool install_entry(const tfn_entry_t *e, const char *tmp, int place, char *msg, int len)
{
    if (e->state == TFN_BAD_SIG && e->pub_st == TFN_PUB_NOT_REVIEWED) {
        snprintf(msg, len, "%s: BY %s, NOT REVIEWED (no store signature; the developer mode is off)", e->id, e->pub);
        return false;
    }
    if (e->state == TFN_BAD_SIG) {
        snprintf(msg, len, "%s has no good signature of a trusted store (the developer mode is off)", e->id);
        return false;
    }
    if (e->state == TFN_BAD_ABI) {
        if (e->abi == TFN_ABI_VERSION) {
            snprintf(msg, len, "%s needs a newer firmware (ABI 1 rev %u; this firmware has rev %d)", e->id, e->rev,
                     TFN_ABI_REV);
        } else {
            snprintf(msg, len, "%s is for ABI %u; this firmware has ABI %d", e->id, e->abi, TFN_ABI_VERSION);
        }
        return false;
    }
    // the hashes of the files (read in pieces: before, a read of all of app.elf into one block failed while an app
    // held the memory, and the message said "does not match")
    char path[72], hex[65], want[65];
    snprintf(path, sizeof path, "%s/app.elf", tmp);
    if (!tfa_sha256_file(path, 4 << 20, hex)) {
        snprintf(msg, len, "%s: cannot read app.elf (or it is over 4 MB)", e->id);
        return false;
    }
    if (strcmp(hex, e->elf_sha)) {
        snprintf(msg, len, "%s: app.elf does not match the manifest", e->id);
        return false;
    }
    uint8_t *m = NULL;
    size_t ml = 0;
    snprintf(path, sizeof path, "%s/manifest.txt", tmp);
    tfa_read_all(path, &m, &ml, 4096);
    tfa_mval(m ? (const char *)m : "", "icon_sha256", want, sizeof want);
    free(m);
    snprintf(path, sizeof path, "%s/icon.bin", tmp);
    if (want[0] && !tfa_sha256_file(path, 2 << 20, hex)) {
        snprintf(msg, len, "%s: cannot read icon.bin (or it is over 2 MB)", e->id);
        return false;
    }
    if (want[0] && strcmp(hex, want)) {
        snprintf(msg, len, "%s: icon.bin does not match the manifest", e->id);
        return false;
    }
    if (open_now(e->id)) {
        snprintf(msg, len, "%s is open: close it first", e->name);
        return false;
    }
    bool known = false;  // (a new app needs a free place in the list: else the scan
    for (int i = 0; i < tfa_n; i++) {
        if (!strcmp(tfa_e[i].id, e->id)) {
            known = true;  // drops it with no message)
        }
    }
    if (!known && tfa_n >= TFN_MAX) {
        snprintf(msg, len, "%s: the board has %d installed apps, the most that it can list", e->id, TFN_MAX);
        return false;
    }
    const char *root = place == TFN_CARD ? TFN_CARD_DIR : TFN_DATA_DIR;
    if (place == TFN_CARD && !board_sd_mounted() && !board_sd_mount()) {
        snprintf(msg, len, "no card");
        return false;
    }
    mkdir(root, 0777);
    char dst[40], old[48];
    snprintf(dst, sizeof dst, "%s/%s", root, e->id);
    snprintf(old, sizeof old, "%s/.old_%s", root, e->id);
    tfa_rm_tree(old);
    rename(dst, old);  // (the old version, back if the move fails)
    if (rename(tmp, dst) != 0) {
        // another file system (a card): a copy of each file
        mkdir(dst, 0777);
        const char *files[] = TFN_APP_FILES;
        bool ok = true;
        for (int i = 0; i < TFN_APP_NFILES && ok; i++) {
            char a[72], c[72];
            snprintf(a, sizeof a, "%s/%s", tmp, files[i]);
            snprintf(c, sizeof c, "%s/%s", dst, files[i]);
            struct stat st;
            if (stat(a, &st) == 0) {
                ok = copy_file(a, c);
            }
        }
        if (!ok) {
            tfa_rm_tree(dst);
            rename(old, dst);
            snprintf(msg, len, "%s: the files did not copy (full?)", e->id);
            return false;
        }
        tfa_rm_tree(tmp);
    }
    tfa_rm_tree(old);
    // the same app in the other place goes (one copy of an app)
    char other[40];
    snprintf(other, sizeof other, "%s/%s", place == TFN_CARD ? TFN_DATA_DIR : TFN_CARD_DIR, e->id);
    tfa_rm_tree(other);
    char key[16];
    tfa_crash_key(e->id, key);
    if (store_get(key, 0)) {
        store_set(key, 0);
    }
    layout_drop_done(e->id);  // (an app that a change of the layout left out is back)
    tfn_rescan();
    snprintf(msg, len, "%s %s is installed (%s)%s", e->name, e->ver, place == TFN_CARD ? "card" : "board",
             e->state == TFN_DEV ? ", developer mode" : "");
    ESP_LOGI(TAG, "%s", msg);
    return true;
}

bool tfn_install(const char *tmp, int place, char *msg, int len)
{
    tfn_entry_t e;
    if (!tfa_read_entry(tmp, place, &e)) {
        entry_why(tmp, msg, len);
        ESP_LOGW(TAG, "install: %s", msg);
        return false;
    }
    if (!tfn_inst_begin(e.id, msg, len)) {
        return false;
    }
    const bool ok = install_entry(&e, tmp, place, msg, len);
    tfn_inst_end(e.id);
    return ok;
}

// a move with the mark of id: a copy to the move folder of the other place, then the normal install (it checks the
// files again and removes the copy in the first place)
static bool move_marked(const char *id, int place, char *msg, int len)
{
    if (open_now(id)) {
        snprintf(msg, len, "%s is open: close it first", id);
        return false;
    }
    if (place == TFN_CARD && !board_sd_mounted() && !board_sd_mount()) {
        snprintf(msg, len, "no card");
        return false;
    }
    char src[40], tmp[48];
    snprintf(src, sizeof src, "%s/%s", place == TFN_CARD ? TFN_DATA_DIR : TFN_CARD_DIR, id);
    struct stat st;
    if (stat(src, &st) != 0) {
        snprintf(msg, len, "%s is not in the %s", id, place == TFN_CARD ? "board storage" : "card");
        return false;
    }
    const char *root = place == TFN_CARD ? TFN_CARD_DIR : TFN_DATA_DIR;
    mkdir(root, 0777);
    snprintf(tmp, sizeof tmp, "%s/" TFN_TMP_MOVE "%s", root, id);
    tfn_tmp_drop(tmp);
    mkdir(tmp, 0777);
    const char *files[] = TFN_APP_FILES;
    for (int i = 0; i < TFN_APP_NFILES; i++) {
        char a[72], c[72];
        snprintf(a, sizeof a, "%s/%s", src, files[i]);
        snprintf(c, sizeof c, "%s/%s", tmp, files[i]);
        if (stat(a, &st) == 0 && !copy_file(a, c)) {
            tfn_tmp_drop(tmp);
            snprintf(msg, len, "%s: the copy failed (full?)", id);
            return false;
        }
    }
    const bool ok = tfn_install(tmp, place, msg, len);
    if (!ok) {
        tfn_tmp_drop(tmp);  // (the copy of a move that failed takes no space)
    }
    return ok;
}

// move an installed app to the other place
bool tfn_move(const char *id, int place, char *msg, int len)
{
    if (!tfa_id_ok(id)) {
        snprintf(msg, len, "a bad id");
        return false;
    }
    if (!tfn_inst_begin(id, msg, len)) {
        return false;
    }
    const bool ok = move_marked(id, place, msg, len);
    tfn_inst_end(id);
    return ok;
}

// a remove with the mark of id: the folders of the id in both places (the list can be a step behind the folders)
static bool remove_marked(const char *id, char *msg, int len)
{
    if (open_now(id)) {
        snprintf(msg, len, "%s is open: close it first", id);
        return false;
    }
    char dir[40];
    struct stat st;
    int found = 0, gone = 0;
    for (int p = 0; p < 2; p++) {
        snprintf(dir, sizeof dir, "%s/%s", p ? TFN_CARD_DIR : TFN_DATA_DIR, id);
        if (stat(dir, &st) != 0) {
            continue;
        }
        found++;
        gone += tfa_rm_tree(dir);
    }
    tfn_rescan();
    if (!found) {
        snprintf(msg, len, "no installed app %s", id);
    } else {
        snprintf(msg, len, gone == found ? "removed %s" : "%s: not all files went", id);
    }
    return found && gone == found;
}

bool tfn_remove(const char *id, char *msg, int len)
{
    if (!tfa_id_ok(id)) {
        snprintf(msg, len, "a bad id");
        return false;
    }
    if (!tfn_inst_begin(id, msg, len)) {
        return false;
    }
    const bool ok = remove_marked(id, msg, len);
    tfn_inst_end(id);
    return ok;
}
