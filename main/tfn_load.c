// TFN OS: open and close: the load of an installed app, its start and its close (main/tfn_app.c has the rest; tfn_app_priv.h).
#include "tfn_app_priv.h"

static const char *TAG = "tfn";

#define TICK_END_MS 60  // the time of the last tick of an app that left (a tick that ends the notes and clears itself)

// ---------------------------------------------------------------- open and close
const tfn_entry_t *tfn_entry_of(const app_t *a)
{
    for (int i = 0; i < tfa_n; i++) {
        if (&tfa_e[i].stub == a) {
            return &tfa_e[i];
        }
    }
    for (int j = 0; j < 2; j++) {
        if (tfa_ld[j].idx >= 0 && &tfa_ld[j].real == a) {
            return &tfa_e[tfa_ld[j].idx];
        }
    }
    return NULL;
}

bool tfn_is_stub(const app_t *a)
{
    for (int i = 0; i < tfa_n; i++) {
        if (&tfa_e[i].stub == a) {
            return true;
        }
    }
    return false;
}

const app_t *tfn_find(const char *name)
{
    for (int i = 0; i < tfa_n; i++) {
        if (tfn_runs(&tfa_e[i]) && !strcasecmp(tfa_e[i].name, name)) {
            return &tfa_e[i].stub;
        }
    }
    return NULL;
}

// the memory of an app file goes: first each sound that uses it stops (a sample voice of its data, its tune, its TIA
// steps), so the audio task never reads the freed file
static void unload_sounds(const esp_elf_t *elf)
{
#if CONFIG_ELF_LOADER_BUS_ADDRESS_MIRROR
    void *const blocks[] = {elf->pdata, elf->ptext};
#else
    void *const blocks[] = {elf->psegment};
#endif
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        if (blocks[i]) {
            audio_stop_mem(blocks[i], heap_caps_get_allocated_size(blocks[i]));
        }
    }
}

// free an app file: its memory, its files, its code. false: a task of the app still runs (the code stays)
bool tfa_unload(int j)
{
    if (tfa_ld[j].idx < 0) {
        return true;
    }
    const char *id = tfa_e[tfa_ld[j].idx].id;
    if (tfa_own_tasks_run(j)) {
        ESP_LOGW(TAG, "%s: a task of the app still runs: the app stays in memory", id);
        return false;
    }
    if (tfa_capture_owner == j) {
        wifi_capture_end();  // drain app callbacks before any owned buffers/code go
        tfa_capture_owner = -1;
    }
    if (tfa_sfx_owner == j + 1) {
        tfsfx_free();  // (the effects of the app: the loader task stops before the names and the data go)
        tfa_sfx_owner = 0;
    }
    size_t bytes = 0;
    int blocks = tfa_own_release(j, &bytes);
    if (blocks) {
        ESP_LOGI(TAG, "%s: freed %d blocks (%u bytes) that the app kept", id, blocks, (unsigned)bytes);
    }
    unload_sounds(&tfa_ld[j].elf);
    esp_elf_deinit(&tfa_ld[j].elf);
    free(tfa_ld[j].file);
    tfa_ld[j].file = NULL;
    tfa_ld[j].idx = -1;
    if (tfa_owner == j) {
        tfa_owner = -1;
    }
    return true;
}

const app_t *tfn_resolve(const app_t *a, char *msg, int len)
{
    int k = -1;
    for (int i = 0; i < tfa_n; i++) {
        if (&tfa_e[i].stub == a) {
            k = i;
        }
    }
    if (k < 0) {
        return a;  // (a built-in app, or an app that is loaded)
    }
    tfn_entry_t *e = &tfa_e[k];
    if (!tfn_runs(e)) {
        snprintf(msg, len,
                 e->state == TFN_STOPPED ? "%s STOPPED THE BOARD TWO TIMES. INSTALL IT AGAIN." : "%s CANNOT RUN",
                 e->name);
        return NULL;
    }
    for (int j = 0; j < 2; j++) {
        if (tfa_ld[j].idx == k) {
            return &tfa_ld[j].real;
        }
    }
    // First free each loaded app that is not open. At its close, a task of the app (a sound loader, a worker) can
    // still run, so the app stays in memory; a new big app next to it can then fail with no memory ("Failed to malloc
    // ... bytes"). Its tasks get up to 1 s to end.
    for (int q = 0; q < 2; q++) {
        if (tfa_ld[q].idx < 0 || &tfa_ld[q].real == app_current()) {
            continue;
        }
        for (int w = 0; w < 100 && tfa_own_tasks_run(q); w++) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        if (!tfa_own_tasks_run(q)) {
            char id[24];
            snprintf(id, sizeof id, "%s", tfa_e[tfa_ld[q].idx].id);
            if (tfa_unload(q)) {
                ESP_LOGI(TAG, "freed %s before the load, PSRAM free %u", id,
                         (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
            }
        }
    }
    // a free place: free a loaded app that is not open
    int j = tfa_ld[0].idx < 0 ? 0 : tfa_ld[1].idx < 0 ? 1 : -1;
    if (j < 0) {
        j = &tfa_ld[0].real == app_current() ? 1 : 0;
        // (a task of the app in that place still runs: use the other place, if its app is not open. Else the board
        // would refuse each new app while one app keeps a task, e.g. HOME)
        if (tfa_own_tasks_run(j) && &tfa_ld[j ^ 1].real != app_current() && !tfa_own_tasks_run(j ^ 1)) {
            j ^= 1;
        }
        if (!tfa_unload(j)) {
            snprintf(msg, len, "THE BOARD IS BUSY (A TASK OF AN APP STILL RUNS). TRY AGAIN.");
            return NULL;
        }
    }
    loaded_t *L = &tfa_ld[j];
#if TFA_ICON_BIG
    // (the Tab5: the icon files stay while the PSRAM has room: the menu came back with 1.4 s of card reads)
    if (heap_caps_get_free_size(MALLOC_CAP_SPIRAM) <= ICON_KEEP_FREE) {
        tfa_icon_files_free();
    }
#else
    tfa_icon_files_free();
#endif
    int64_t t0 = esp_timer_get_time();
    size_t ps0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM), in0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    char path[72];
    size_t n = 0;
    snprintf(path, sizeof path, "%s/app.elf", e->dir);
    if (!tfa_read_all(path, &L->file, &n, 4 << 20)) {
        // (a file that is there but does not fit in the free PSRAM is a memory fault, not a card fault)
        struct stat st;
        if (!stat(path, &st) && st.st_size > 0 &&
            (size_t)st.st_size + 1 > heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)) {
            snprintf(msg, len, "NO MEMORY FOR %s: RESTART THE BOARD", e->name);
        } else {
            snprintf(msg, len, "CANNOT READ %s%s", e->name, e->place == TFN_CARD ? " (IS THE CARD IN?)" : "");
        }
        return NULL;
    }
    char hex[65];
    tfa_sha256_hex(L->file, n, hex);
    if (strcmp(hex, e->elf_sha)) {
        free(L->file);
        L->file = NULL;
        snprintf(msg, len, "THE FILE OF %s IS DAMAGED. INSTALL IT AGAIN.", e->name);
        ESP_LOGE(TAG, "%s: app.elf hash %.16s, the manifest says %.16s", e->id, hex, e->elf_sha);
        return NULL;
    }
    memset(&L->elf, 0, sizeof L->elf);
    esp_elf_init(&L->elf);
    LOCK();  // (one link at a time: a watcher can link on the watch task)
    tfa_reloc_bad = 0;
    tfa_link_perms = e->perms;  // (tfn_resolve_sym: a name of a class that the manifest does not give: 0)
    tfa_denied[0] = 0;
    tfa_seg_end_set(L->file);
    int r = esp_elf_relocate(&L->elf, L->file);
    if (!r && (tfa_reloc_bad || tfa_denied[0])) {
        r = -EINVAL;
    }
    UNLOCK();
    if (r) {
        esp_elf_deinit(&L->elf);
        free(L->file);
        L->file = NULL;
        if (tfa_denied[0]) {
            snprintf(msg, len, "%s ASKS FOR %s WITHOUT ITS PERMISSION", e->name, tfa_denied);
            ESP_LOGE(TAG, "%s: %s needs a permission class that the manifest does not have (policy %d)", e->id,
                     tfa_denied, tfa_pol_ver);
            return NULL;
        }
        // (-ENOMEM: the loader got no memory for a section; the old text said "A MISSING NAME" for it too)
        if (r == -ENOMEM) {
            snprintf(msg, len, "NO MEMORY FOR %s: RESTART THE BOARD", e->name);
        } else {
            snprintf(msg, len,
                     tfa_reloc_bad ? "THE FILE OF %s IS NOT A GOOD APP FILE"
                                   : "%s NEEDS A NEWER FIRMWARE (A MISSING NAME)",
                     e->name);
        }
        ESP_LOGE(TAG, "%s: the loader refused it (%d): see the lines above", e->id, r);
        return NULL;
    }
    L->idx = k;
    memset(&tfa_own[j], 0, sizeof tfa_own[j]);
    tfa_owner = j;  // (from the constructors on, the app's memory is noted)
    // the guard is on from the constructors (the entry runs them): a fault there counts too
    tfa_guard.magic = GUARD_MAGIC;
    snprintf(tfa_guard.id, sizeof tfa_guard.id, "%s", e->id);
    const app_t *got = NULL;
    char *argv[2] = {(char *)&got, (char *)(intptr_t)TFN_ABI_VERSION};
    esp_elf_request(&L->elf, 0, 2, argv);
    if (!got || !got->enter || !got->update || !got->render || !got->input) {
        tfa_guard.magic = 0;
        tfa_unload(j);
        snprintf(msg, len, "%s GAVE NO APP", e->name);
        return NULL;
    }
    L->real = *got;
    free(L->file);  // (the loader copied the code and the data out of it)
    L->file = NULL;
    if (L->real.init) {
        L->real.init();
    }
    ESP_LOGI(TAG, "loaded %s %s (%s): %u bytes in %lld ms, PSRAM %d bytes, internal RAM %d bytes", e->id, e->ver,
             e->place == TFN_CARD ? "card" : "board", (unsigned)n, (long long)((esp_timer_get_time() - t0) / 1000),
             (int)(ps0 - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
             (int)(in0 - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)));
    return &L->real;
}

bool tfn_same_app(const app_t *item, const app_t *running)
{
    if (item == running) {
        return true;
    }
    for (int k = 0; k < tfa_n; k++) {
        if (&tfa_e[k].stub != item) {
            continue;
        }
        for (int j = 0; j < 2; j++) {
            if (tfa_ld[j].idx == k && &tfa_ld[j].real == running) {
                return true;
            }
        }
    }
    return false;
}

void tfn_left(const app_t *a)
{
    for (int j = 0; j < 2; j++) {
        if (tfa_ld[j].idx < 0 || &tfa_ld[j].real != a) {
            continue;
        }
        tfn_entry_t *e = &tfa_e[tfa_ld[j].idx];
        tfa_guard.magic = 0;  // a clean close
        char key[16];
        tfa_crash_key(e->id, key);
        if (store_get(key, 0)) {
            store_set(key, 0);
        }
        if (tfa_owner == j) {
            tfa_owner = -1;
        }
        audio_tune_stop();  // (a tune or TIA steps of the app point into its data)
        audio_tia_off();
        audio_tick_end(TICK_END_MS);  // (a last tick of the app is in its code: it runs or goes before the unload)
        // (a task of the app that still runs needs its code: the app stays in memory, freed at a later load)
        char id[24];
        snprintf(id, sizeof id, "%s", e->id);
        if (tfa_unload(j)) {
            ESP_LOGI(TAG, "closed %s, PSRAM free %u", id, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
    }
}
