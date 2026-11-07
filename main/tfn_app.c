// TFN OS: the installed apps.
//
// An installed app is a folder: /data/apps/<id>/ (the board) or /sdcard/apps/<id>/ (the card), with
//   manifest.txt   key=value lines (id, name, ver, code, abi, kind, group, the app_t flags, bytes, ram, the hashes)
//   app.elf        the app for this board kind: an ELF shared object (sdk/tools/mk_app.py), from an app source of
//                  main/ without a change
//   icon.bin       the menu icon: frames that the host drew with the draw_icon() of the app (no code load for the menu)
//   sig            the ECDSA P-256 signature of the manager (the private key stays on the manager server) over manifest.txt
// The manifest has the SHA-256 of app.elf and icon.bin, so the signature covers all files.
//
// The menu gets a menu item (an app_t "stub") for each app that can run: its name, its flags and an icon function.
// When the user opens it, main.c asks tfn_resolve(): the loader (components/elf_loader) copies the app to PSRAM,
// links its calls to the firmware names of the ABI table (main/tfn_abi.inc, sdk/tools/abi_gen.py), runs its C++
// constructors (.init_array) and its entry app_main(argc, argv) gives its app_t. After leave() the app is freed.
//
// The crash guard: the id of the open app is in RTC memory; after a panic or a watchdog reset in it, a count goes up
// (NVS); two such resets with no clean close in between stop the app (TFN_STOPPED). The safe mode: the BOOT key of the
// 3.49 held at start: no installed app loads.
#include "tfn_app_priv.h"
#define N_BUILTIN_MAX 128  // (the most built-in app names in /api/apps)

static const char *TAG = "tfn";

// the shared state of the parts (tfn_app_priv.h)
own_t tfa_own[2];
int tfa_owner = -1;          // the slot of the open app file (its calls are noted there), -1 = none
SemaphoreHandle_t tfa_lock;  // the list: changed on the app task (do_rescan), read by the web task too
RTC_NOINIT_ATTR tfa_guard_t tfa_guard;

// ---------------------------------------------------------------- the ABI table (the names that apps can call)
// An asm name gives the address of each name without its C type, so this file needs none of their headers. The table
// has no names (the raw ESP-IDF names of main/tfn_abi_raw.json need tens of KB of strings):
// each entry is the FNV-1a 64 hash of a name (sdk/tools/abi_gen.py) and its address. tfn_init() sorts a copy by the
// hash, and the loader finds each name of an app with tfn_resolve_sym() (elf_set_symbol_resolver).
#define ELFSYM(n, lo, hi) extern char tfnsym_##n __asm__(#n);
#include "tfn_abi.inc"
#undef ELFSYM
typedef struct {
    uint32_t lo, hi;
    const void *sym;
} tfn_sym_t;
#define ELFSYM(n, lo, hi) {lo, hi, &tfnsym_##n},
static const tfn_sym_t s_syms[] = {
#include "tfn_abi.inc"
};
#undef ELFSYM
#define N_SYMS ((int)(sizeof s_syms / sizeof s_syms[0]))

// The permission classes (sdk/docs/raw.md). A name can need a class: the loader links it only for an app whose
// manifest has that class (perms=); the STORE shows the text of the classes before an install. The classes and the
// names of each class are a POLICY: the default comes from the build (ELFPERM and ELFNEED of
// tfn_abi.inc); the manager sends newer ones, signed with its key (the tab PERMISSIONS; /data/tfn_policy.txt and
// .sig). A bit of perms is the place of a class in the policy that the board uses now.
#define ELFSYM(n, lo, hi)
#define ELFPERM(b, id, txt) {b, id, txt},
static const struct {
    uint8_t bit;
    const char *id, *text;
} s_perm_def[] = {
#include "tfn_abi.inc"
        {0, NULL, NULL}};
#undef ELFPERM
#define ELFNEED(n, lo, hi, b) {lo, hi, b},
static const struct {
    uint32_t lo, hi;
    uint8_t bit;
} s_need_def[] = {
#include "tfn_abi.inc"
        {0, 0, 0}};
#undef ELFNEED
#undef ELFSYM
#define CLS_MAX  32
#define CLS_NONE 0xFF
static tfn_sym_t *s_tab;  // PSRAM: s_syms sorted by the hash, with the versions that note what an app holds
static uint8_t *s_cls;    // PSRAM: the class of each entry of s_tab (CLS_NONE: free for each app)
static int s_ncls;        // the classes of the policy now
static char (*s_cls_id)[24], (*s_cls_text)[64];
int tfa_pol_ver;      // the version of the policy now (0: the default of the build)
bool tfa_link_watch;  // the loader links a watch.elf now: no names of the screen, the sound, the touch
// the names that a watcher (watch.h) cannot have: it runs while another app draws and plays
static bool watch_denied(const char *n)
{
    static const char *const P[] = {"gfx_",
                                    "ui_",
                                    "mf_",
                                    "kf_",
                                    "g_fb",
                                    "sfx_",
                                    "audio_",
                                    "input_",
                                    "app_open",
                                    "board_lcd",
                                    "board_backlight",
                                    "board_speaker",
                                    "menu_",
                                    "sysui_",
                                    "kbd_",
                                    "midi_bg_",
                                    "imu_",
                                    "tfd_"};
    for (size_t i = 0; i < sizeof P / sizeof P[0]; i++) {
        if (!strncmp(n, P[i], strlen(P[i]))) {
            return true;
        }
    }
    return false;
}
uint32_t tfa_link_perms;  // the classes of the app that the loader links now
char tfa_denied[48];      // the first name that the link refused (a class that the app does not have)

void tfa_fnv64(const char *s, uint32_t *lo, uint32_t *hi)
{
    uint64_t h = 0xcbf29ce484222325ULL;  // (FNV-1a 64: h64() of sdk/tools/abi_gen.py)
    while (*s) {
        h ^= (uint8_t)*s++;
        h *= 0x100000001b3ULL;
    }
    *lo = (uint32_t)h;
    *hi = (uint32_t)(h >> 32);
}

static int sym_cmp(const void *a, const void *b)
{
    const tfn_sym_t *x = a, *y = b;
    if (x->hi != y->hi) {
        return x->hi < y->hi ? -1 : 1;
    }
    return x->lo < y->lo ? -1 : x->lo > y->lo;
}

static int sym_find(uint32_t lo, uint32_t hi)
{
    int a = 0, b = N_SYMS - 1;
    while (s_tab && a <= b) {
        int m = (a + b) / 2;
        const tfn_sym_t *e = &s_tab[m];
        if (e->hi == hi && e->lo == lo) {
            return m;
        }
        if (e->hi < hi || (e->hi == hi && e->lo < lo)) {
            a = m + 1;
        } else {
            b = m - 1;
        }
    }
    return -1;
}

static int sym_find_name(const char *name)
{
    uint32_t lo, hi;
    tfa_fnv64(name, &lo, &hi);
    return sym_find(lo, hi);
}

// the resolver of the loader: the address of an ABI name, or 0 (not in the ABI, or a class that the app does not have)
uintptr_t tfa_resolve_sym(const char *name)
{
    if (tfa_link_watch && watch_denied(name)) {
        if (!tfa_denied[0]) {
            snprintf(tfa_denied, sizeof tfa_denied, "%s", name);
        }
        return 0;
    }
    int i = sym_find_name(name);
    if (i < 0) {
        return 0;
    }
    uint8_t c = s_cls ? s_cls[i] : CLS_NONE;
    if (c != CLS_NONE && !(tfa_link_perms & (1u << c))) {
        if (!tfa_denied[0]) {
            snprintf(tfa_denied, sizeof tfa_denied, "%s", name);
        }
        return 0;
    }
    return (uintptr_t)s_tab[i].sym;
}

int tfa_cls_of(const char *id, int l)
{
    for (int i = 0; i < s_ncls; i++) {
        if ((int)strlen(s_cls_id[i]) == l && !strncmp(s_cls_id[i], id, l)) {
            return i;
        }
    }
    return -1;
}

uint32_t tfn_perms_parse(const char *list, bool *unknown)
{
    uint32_t bits = 0;
    if (unknown) {
        *unknown = false;
    }
    for (const char *p = list; p && *p;) {
        const char *c = strchr(p, ',');
        int l = c ? (int)(c - p) : (int)strlen(p);
        int i = l > 0 ? tfa_cls_of(p, l) : -1;
        if (i >= 0) {
            bits |= 1u << i;
        } else if (l > 0 && unknown) {
            *unknown = true;
        }
        p = c ? c + 1 : NULL;
    }
    return bits;
}

void tfn_perms_text(uint32_t bits, char *out, int len)
{
    int k = 0;
    out[0] = 0;
    for (int i = 0; i < s_ncls && k < len - 1; i++) {
        if (bits & (1u << i)) {
            k += snprintf(out + k, len - k, "%s%s", k ? ", " : "", s_cls_text[i]);
        }
    }
}

int tfn_policy_ver(void)
{
    return tfa_pol_ver;
}

// elf_loader 1.3.5 drops the addend of R_XTENSA_GLOB_DAT / R_XTENSA_JMP_SLOT (S + A in the ABI): an app that reads a
// firmware table at an offset (newlib _ctype_ + 1, a font + n) reads the wrong place. The Tactility project found it.
// The link option --wrap=esp_elf_arch_relocate (main/CMakeLists.txt) puts this function in front of the loader's one.
// The loader also writes to the place of a relocation without a check: a place in a section that it did not load (an
// app file that mk_app.py did not make) gives an address of 0 and a fault. Such a relocation is refused and counted
// here (the loader does not look at the result), and tfn_resolve() stops the load.
int tfa_reloc_bad;
#if !CONFIG_IDF_TARGET_ARCH_XTENSA
// (RISC-V: the loader places the file as ONE block (its PT_LOAD segments, from address 0), not as sections: the end
// of that block in the file addresses, set by seg_end_set() before each relocation)
static uint32_t s_seg_end;
void tfa_seg_end_set(const uint8_t *file)
{
    const elf32_hdr_t *eh = (const elf32_hdr_t *)file;
    const elf32_phdr_t *ph = (const elf32_phdr_t *)(file + eh->phoff);
    s_seg_end = 0;
    for (int i = 0; i < eh->phnum; i++) {
        if (ph[i].type == PT_LOAD && ph[i].vaddr + ph[i].memsz > s_seg_end) {
            s_seg_end = ph[i].vaddr + ph[i].memsz;
        }
    }
}
#else
void tfa_seg_end_set(const uint8_t *file)
{
    (void)file;
}
#endif
int __real_esp_elf_arch_relocate(esp_elf_t *elf, const elf32_rela_t *rela, const elf32_sym_t *sym, uint32_t addr);
int __wrap_esp_elf_arch_relocate(esp_elf_t *elf, const elf32_rela_t *rela, const elf32_sym_t *sym, uint32_t addr)
{
    uint32_t type = ELF_R_TYPE(rela->info);
#if CONFIG_IDF_TARGET_ARCH_XTENSA
    if (type != 2 && !esp_elf_map_sym(elf, rela->offset)) {  // (2 = R_XTENSA_RTLD writes nothing)
        if (!tfa_reloc_bad++) {
            ESP_LOGE("tfn", "a relocation at 0x%lx is out of the loaded sections", (unsigned long)rela->offset);
        }
        return -EINVAL;
    }
    if (type == 3 || type == 4) {
        addr += rela->addend;  // R_XTENSA_GLOB_DAT, R_XTENSA_JMP_SLOT
    }
#else
    // (RISC-V, the Tab5: the types are other numbers, and the loader of RISC-V adds the addends itself; only the check
    // of the place: inside the one loaded block. Checked on the Tab5)
    if (type != 0 && rela->offset + 4 > s_seg_end) {  // (0 = R_RISCV_NONE)
        if (!tfa_reloc_bad++) {
            ESP_LOGE("tfn", "a relocation at 0x%lx is out of the loaded sections", (unsigned long)rela->offset);
        }
        return -EINVAL;
    }
#endif
    return __real_esp_elf_arch_relocate(elf, rela, sym, addr);
}

// ---------------------------------------------------------------- what an app file holds
// Many built-in apps keep their buffers after leave() (a run through all apps cut the free PSRAM in the menu from
// 4.89 MB to 3.43 MB). For an app file that is a leak: its code goes, its buffers stay. So the ABI
// table gives the app files these versions of malloc, free, fopen and the tasks: they note what the open app holds,
// and the unload frees the memory and closes the files. A task of the app that still runs keeps the app in memory.

// THE WATCHERS (watch.h): watch.elf of an app, loaded on the watch task. Their memory and files go into their own sets
// (a check runs while another app is open: the close of that app must not free them).

wslot_t tfa_w[WMAX];
int tfa_wcur = -1;  // the watcher of the watch task now (its load, its init, its check)
TaskHandle_t tfa_wtask;
bool tfa_on_watch_task(void)
{
    return tfa_wtask && xTaskGetCurrentTaskHandle() == tfa_wtask;
}
// the set of what the caller holds: a watcher on the watch task, else the open app file
static own_t *own_now(void)
{
    if (tfa_wcur >= 0 && tfa_on_watch_task()) {
        return &tfa_w[tfa_wcur].own;
    }
    return tfa_owner >= 0 ? &tfa_own[tfa_owner] : NULL;
}

static uint32_t own_h(void *p)
{
    return (uint32_t)(((uintptr_t)p >> 3) * 2654435761u);
}
static void own_add(void *p);
static bool own_grow(own_t *o)
{
    int nc = o->cap ? o->cap * 2 : 256;
    void **nm = heap_caps_calloc((size_t)nc, sizeof(void *), MALLOC_CAP_SPIRAM);
    if (!nm) {
        return false;
    }
    void **old = o->mem;
    int oc = o->cap;
    o->mem = nm;
    o->cap = nc;
    o->used = o->live = 0;
    for (int i = 0; i < oc; i++) {
        void *p = old[i];
        if (p && p != OWN_GONE) {
            uint32_t h = own_h(p) & (uint32_t)(nc - 1);
            while (nm[h]) {
                h = (h + 1) & (uint32_t)(nc - 1);
            }
            nm[h] = p;
            o->used++;
            o->live++;
        }
    }
    heap_caps_free(old);
    return true;
}
static portMUX_TYPE s_own_mux = portMUX_INITIALIZER_UNLOCKED;
static void own_add_locked(void *p);
static void own_del_locked(void *p);
static void own_add(void *p)
{
    if (!p || !own_now()) {
        return;
    }
    portENTER_CRITICAL(&s_own_mux);
    own_add_locked(p);
    portEXIT_CRITICAL(&s_own_mux);
}
static void own_del(void *p)
{
    if (!p) {
        return;
    }
    portENTER_CRITICAL(&s_own_mux);
    own_del_locked(p);
    portEXIT_CRITICAL(&s_own_mux);
}
static void own_add_locked(void *p)
{
    own_t *o = own_now();
    if (!o) {
        return;
    }
    if ((o->used + 1) * 2 > o->cap && !own_grow(o)) {
        return;
    }
    uint32_t m = (uint32_t)(o->cap - 1), h = own_h(p) & m;
    while (o->mem[h] && o->mem[h] != OWN_GONE) {
        h = (h + 1) & m;
    }
    if (!o->mem[h]) {
        o->used++;
    }
    o->mem[h] = p;
    o->live++;
}
static void own_del_locked(void *p)
{
    for (int j = 0; j < 2 + WMAX; j++) {
        own_t *o = j < 2 ? &tfa_own[j] : &tfa_w[j - 2].own;
        if (!o->cap) {
            continue;
        }
        uint32_t m = (uint32_t)(o->cap - 1), h = own_h(p) & m;
        for (int n = 0; o->mem[h] && n < o->cap; n++, h = (h + 1) & m) {
            if (o->mem[h] == p) {
                o->mem[h] = OWN_GONE;
                o->live--;
                return;
            }
        }
    }
}
static void *tfn_malloc(size_t n)
{
    void *p = malloc(n);
    own_add(p);
    return p;
}
static void *tfn_calloc(size_t a, size_t b)
{
    void *p = calloc(a, b);
    own_add(p);
    return p;
}
// A block of an app goes: first each sound that uses it stops (main/audio.c: a sample voice reads its samples until its
// end, and the leave of an app can free them before a late audio task ends the fade). So no sound reads or writes
// freed memory, also with the app files of an older SDK. A stop here means that the app did not end the sound itself:
// the log shows it.
static void own_free_sound(void *p)
{
    if (p) {
        const int k = audio_mem_check(p, heap_caps_get_allocated_size(p), true);
        if (k) {
            ESP_LOGW(TAG, "a free of the app stopped %d sound(s) that still used the block", k);
        }
    }
}
static void *tfn_realloc(void *q, size_t n)
{
    uintptr_t qk = (uintptr_t)q;  // (the old block as a number: after realloc it is only a key of the set)
    own_del(q);
    own_free_sound(q);  // (realloc can move the block or free it)
    void *p = realloc(q, n);
    own_add(p ? p : (n ? (void *)qk : NULL));  // (a refused realloc keeps the old block)
    return p;
}
static void tfn_free(void *p)
{
    own_del(p);
    own_free_sound(p);
    free(p);
}
static void *tfn_heap_caps_malloc(size_t n, uint32_t caps)
{
    void *p = heap_caps_malloc(n, caps);
    own_add(p);
    return p;
}
static void *tfn_heap_caps_calloc(size_t a, size_t b, uint32_t caps)
{
    void *p = heap_caps_calloc(a, b, caps);
    own_add(p);
    return p;
}
static void tfn_heap_caps_free(void *p)
{
    own_del(p);
    own_free_sound(p);
    heap_caps_free(p);
}
static FILE *tfn_fopen(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    own_t *o = own_now();
    if (f && o) {
        for (int i = 0; i < OWN_FILES; i++) {
            if (!o->files[i]) {
                o->files[i] = f;
                break;
            }
        }
    }
    return f;
}
#if CONFIG_TFN_DEVICE_PROTECTION
static int tfn_accounts_read(char *out, size_t cap)
{
    const char *id = tfa_secret_owner();
    return id && !strcmp(id, "ai_limits") ? device_guard_accounts_read(out, cap) : -1;
}
static int tfn_accounts_write(const char *text, size_t len)
{
    const char *id = tfa_secret_owner();
    return id && !strcmp(id, "ai_limits") ? device_guard_accounts_write(text, len) : -1;
}
#endif
static int tfn_fclose(FILE *f)
{
    for (int j = 0; j < 2 + WMAX; j++) {
        own_t *o = j < 2 ? &tfa_own[j] : &tfa_w[j - 2].own;
        for (int i = 0; i < OWN_FILES; i++) {
            if (o->files[i] == f) {
                o->files[i] = NULL;
            }
        }
    }
    return fclose(f);
}
static void own_task(TaskHandle_t t)
{
    if (t && tfa_owner >= 0) {
        for (int i = 0; i < OWN_TASKS; i++) {
            if (!tfa_own[tfa_owner].tasks[i]) {
                tfa_own[tfa_owner].tasks[i] = t;
                break;
            }
        }
    }
}
static void own_task_gone(TaskHandle_t t)
{
    if (!t) {
        t = xTaskGetCurrentTaskHandle();
    }
    for (int j = 0; j < 2; j++) {
        for (int i = 0; i < OWN_TASKS; i++) {
            if (tfa_own[j].tasks[i] == t) {
                tfa_own[j].tasks[i] = NULL;
            }
        }
    }
}
static BaseType_t tfn_xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, const uint32_t stack, void *arg,
                                              UBaseType_t prio, TaskHandle_t *out, const BaseType_t core)
{
    TaskHandle_t t = NULL;
    if (tfa_on_watch_task()) {
        return pdFAIL;  // (a watcher has no task of its own: watch.h)
    }
    BaseType_t r = xTaskCreatePinnedToCore(fn, name, stack, arg, prio, &t, core);
    if (r == pdPASS) {
        own_task(t);
    }
    if (out) {
        *out = t;
    }
    return r;
}
static BaseType_t tfn_xTaskCreatePinnedToCoreWithCaps(TaskFunction_t fn, const char *name,
                                                      const configSTACK_DEPTH_TYPE stack, void *arg, UBaseType_t prio,
                                                      TaskHandle_t *out, const BaseType_t core, UBaseType_t caps)
{
    TaskHandle_t t = NULL;
    if (tfa_on_watch_task()) {
        return pdFAIL;
    }
    BaseType_t r = xTaskCreatePinnedToCoreWithCaps(fn, name, stack, arg, prio, &t, core, caps);
    if (r == pdPASS) {
        own_task(t);
    }
    if (out) {
        *out = t;
    }
    return r;
}
static void tfn_vTaskDelete(TaskHandle_t t)
{
    own_task_gone(t);
    vTaskDelete(t);
}
static void tfn_vTaskDeleteWithCaps(TaskHandle_t t)
{
    own_task_gone(t);
    vTaskDeleteWithCaps(t);
}
int tfa_capture_owner = -1;
static int32_t tfn_wifi_capture_begin(const wifi_capture_config_t *c, wifi_capture_rx_cb_t cb, void *ctx)
{
    int32_t err = wifi_capture_begin(c, cb, ctx);
    if (!err) {
        tfa_capture_owner = tfa_owner;
    }
    return err;
}
static int32_t tfn_wifi_capture_end(void)
{
    if (tfa_capture_owner >= 0 && tfa_capture_owner != tfa_owner) {
        return ESP_ERR_INVALID_STATE;
    }
    int32_t err = wifi_capture_end();
    tfa_capture_owner = -1;
    return err;
}
// The modules tfsfx and tfphys are in the firmware (ABI names): what they hold is not in the set of the app. So the
// loader notes the app of the last tfsfx_load (its unload calls tfsfx_free: the loader task of tfsfx must not read the
// names or the data of an app that went), and the memory that tp_init gets goes into the set of the app (the unload
// frees it when the app did not call tp_free).
int tfa_sfx_owner;  // (the slot of that app + 1; 0 = none: no .data, the variable is in PSRAM)
static void tfn_tfsfx_load(const char *dir, const char *const *names, int n, uint8_t group)
{
    if (tfa_on_watch_task()) {
        return;  // (a watcher has no effects)
    }
    tfsfx_load(dir, names, n, group);
    tfa_sfx_owner = tfa_owner + 1;
}
static void tfn_tfsfx_load_mem(const uint8_t *const *data, const uint32_t *len, const char *const *names, int n,
                               uint8_t group)
{
    if (tfa_on_watch_task()) {
        return;
    }
    tfsfx_load_mem(data, len, names, n, group);
    tfa_sfx_owner = tfa_owner + 1;
}
static tp_world_t *tfn_tp_init(const tp_world_def_t *d)
{
    tp_world_t *w = tp_init(d);
    if (w && w->own_mem) {
        own_add(w->mem);
    }
    return w;
}
static void tfn_tp_free(tp_world_t *w)
{
    if (w && w->own_mem) {
        own_del(w->mem);
    }
    tp_free(w);
}
static const struct {
    const char *name;
    const void *fn;
} OWN_WRAP[] = {
#if CONFIG_TFN_DEVICE_PROTECTION
        {"device_guard_accounts_read", tfn_accounts_read},
        {"device_guard_accounts_write", tfn_accounts_write},
#endif
        {"wifi_capture_begin", tfn_wifi_capture_begin},
        {"wifi_capture_end", tfn_wifi_capture_end},
        {"malloc", tfn_malloc},
        {"calloc", tfn_calloc},
        {"realloc", tfn_realloc},
        {"free", tfn_free},
        {"heap_caps_malloc", tfn_heap_caps_malloc},
        {"heap_caps_calloc", tfn_heap_caps_calloc},
        {"heap_caps_free", tfn_heap_caps_free},
        {"fopen", tfn_fopen},
        {"fclose", tfn_fclose},
        {"xTaskCreatePinnedToCore", tfn_xTaskCreatePinnedToCore},
        {"xTaskCreatePinnedToCoreWithCaps", tfn_xTaskCreatePinnedToCoreWithCaps},
        {"vTaskDelete", tfn_vTaskDelete},
        {"vTaskDeleteWithCaps", tfn_vTaskDeleteWithCaps},
        {"tfsfx_load", tfn_tfsfx_load},
        {"tfsfx_load_mem", tfn_tfsfx_load_mem},
        {"tp_init", tfn_tp_init},
        {"tp_free", tfn_tp_free},
};

// true when a task of the app in slot j still runs
bool tfa_own_tasks_run(int j)
{
    for (int i = 0; i < OWN_TASKS; i++) {
        TaskHandle_t t = tfa_own[j].tasks[i];
        if (!t) {
            continue;
        }
        eTaskState st = eTaskGetState(t);
        if (st == eDeleted || st == eInvalid) {
            tfa_own[j].tasks[i] = NULL;
        } else {
            return true;
        }
    }
    return false;
}

// the unload: free the blocks and close the files that the app still holds; returns the blocks freed
int tfa_own_release_o(own_t *o, size_t *bytes)
{
    int n = 0;
    size_t b = 0;
    for (int i = 0; i < o->cap; i++) {
        void *p = o->mem[i];
        if (p && p != OWN_GONE) {
            b += heap_caps_get_allocated_size(p);
            own_free_sound(p);
            free(p);
            n++;
        }
    }
    for (int i = 0; i < OWN_FILES; i++) {
        if (o->files[i]) {
            fclose(o->files[i]);
            o->files[i] = NULL;
        }
    }
    heap_caps_free(o->mem);
    o->mem = NULL;
    o->cap = o->used = o->live = 0;
    if (bytes) {
        *bytes = b;
    }
    return n;
}
int tfa_own_release(int j, size_t *bytes)
{
    return tfa_own_release_o(&tfa_own[j], bytes);
}

// the built-in public keys of the app store (main/tfn_defaults.h): uncompressed P-256 points. Key 1 is the store key
// of the build; key 2 is room for a new store key (all zeros = no key). main/tfn_pub.c adds the keys of the board
// owner.
static const uint8_t TFN_PUBKEY[65] = {TFN_STORE_PUBKEY_BYTES};
static const uint8_t TFN_PUBKEY2[65] = {TFN_STORE_PUBKEY2_BYTES};
const uint8_t *tfa_builtin_key(int i)
{
    return i == 0 ? TFN_PUBKEY : i == 1 ? TFN_PUBKEY2 : NULL;
}

// ---------------------------------------------------------------- the list
tfn_entry_t *tfa_e;  // PSRAM, TFN_MAX places

int tfa_n;
int tfa_dropped;  // the apps of the last scan that had no place in the list
int tfn_dropped(void)
{
    return tfa_dropped;
}
uint32_t tfa_gen = 1;
bool tfa_safe;  // the safe mode (the BOOT key at the start: no installed app loads)
static bool s_inited;

// the loaded apps (one open app, and one more while main.c changes from one installed app to the next)

loaded_t tfa_ld[2] = {{.idx = -1}, {.idx = -1}};

// the crash guard

static char s_crash_id[24];  // the last app that stopped the board (for /api/apps), "" = none
static int s_crash_reason;

const char *tfn_kind(void)
{
    return TFN_KIND;
}
int tfn_abi(void)
{
    return TFN_ABI_VERSION;
}
int tfn_abi_rev(void)
{
    return TFN_ABI_REV;
}
bool tfn_safe_mode(void)
{
    return tfa_safe;
}
uint32_t tfn_gen(void)
{
    return tfa_gen;
}
int tfn_count(void)
{
    return tfa_n;
}
const tfn_entry_t *tfn_entry(int i)
{
    return i >= 0 && i < tfa_n ? &tfa_e[i] : NULL;
}
bool tfn_runs(const tfn_entry_t *e)
{
    return e && (e->state == TFN_OK || e->state == TFN_DEV);
}
bool tfn_dev_mode(void)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    return false;
#else
    return store_get("tfn_dev", 0) != 0;
#endif
}
bool tfn_inst_place_card(void)
{
    return store_get("st_place", 0) == 0;
}
void tfn_set_dev_mode(bool on)
{
#if CONFIG_TFN_DEVICE_PROTECTION
    if (device_guard_enabled()) {
        return;
    }
#endif
    store_set("tfn_dev", on ? 1 : 0);
    tfn_rescan();
}

// the NVS key of the crash count of an app ("tfx" + 8 hex digits of a hash of its id)
void tfa_crash_key(const char *id, char key[16])
{
    uint32_t h = 2166136261u;
    for (const char *p = id; *p; p++) {
        h = (h ^ (uint8_t)*p) * 16777619u;
    }
    snprintf(key, 16, "tfx%08lx", (unsigned long)h);
}

// ---------------------------------------------------------------- files and checks
bool tfa_read_all(const char *path, uint8_t **out, size_t *len, size_t max)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || (size_t)n > max) {
        fclose(f);
        return false;
    }
    uint8_t *b = heap_caps_malloc((size_t)n + 1, MALLOC_CAP_SPIRAM);
    bool ok = b && fread(b, 1, (size_t)n, f) == (size_t)n;
    fclose(f);
    if (!ok) {
        free(b);
        return false;
    }
    b[n] = 0;
    *out = b;
    *len = (size_t)n;
    return true;
}

void tfa_sha256_hex(const uint8_t *d, size_t n, char hex[65])
{
    uint8_t h[32];
    size_t ol = 0;
    if (psa_hash_compute(PSA_ALG_SHA_256, d, n, h, sizeof h, &ol) != PSA_SUCCESS) {
        memset(h, 0, sizeof h);
    }
    for (int i = 0; i < 32; i++) {
        sprintf(hex + i * 2, "%02x", h[i]);
    }
}

// the signatures that were good already (a rescan after each install must not check all apps again: an ECDSA
// check takes time on the app task): the SHA-256 of the manifest, the signature and the public key together.
// The cache also stays in SIG_FILE (the start of a slim 3.49 with 66 app files takes about 20 s with no cache, nearly
// all ECDSA checks). The file has an HMAC-SHA256 with a random key in the NVS (tfn_hk0..7): a write into /data alone
// cannot add a hash. The card files are checked as before (only their hash is in the cache, with the key).
#define SIG_CACHE (TFN_MAX + 32)
#define SIG_FILE  STORAGE_ROOT "/tfn_sigok.bin"
static uint8_t (*s_sig_ok)[32];  // PSRAM, SIG_CACHE places (internal RAM is short on these boards)
static int s_nsig_ok, s_sig_next;
static bool s_sig_dirty;
// the signatures that were bad: the same hash (the manifest, the signature and the key) always gives the same result,
// so a rescan does not check them again. A new copy of the app, or a key that comes into the list, makes a new hash
// and gets a full check. In RAM only: the first scan after a start checks them one time.
#define SIG_BAD (SIG_CACHE * 4)   // (each app can fail with each key of the list: the built-in keys and the publishers)
static uint8_t (*s_sig_bad)[32];  // PSRAM, SIG_BAD places
static int s_nsig_bad, s_sig_bad_next;

// the key of the signature cache: read (or made) one time in each start. A full NVS can refuse a new key: then the
// board makes room once and writes again. If the NVS still refuses it, the key holds only until the next start (the
// cache file of this start does not check after it), and the log tells it.
static uint8_t s_hk[32];
static bool s_hk_got;
static void sig_key(uint8_t key[32])
{
    if (!s_hk_got) {
        bool room = false, lost = false;
        for (int i = 0; i < 8; i++) {
            char kn[12];
            snprintf(kn, sizeof kn, "tfn_hk%d", i);
            uint32_t v = store_get(kn, 0);
            if (!v) {
                v = esp_random() | 1;
                store_set(kn, v);
                if (store_get(kn, 0) != v && !room) {
                    room = true;
                    store_make_room();
                    store_set(kn, v);
                }
                lost = lost || store_get(kn, 0) != v;
            }
            memcpy(s_hk + 4 * i, &v, 4);
        }
        if (lost) {
            ESP_LOGW(TAG, "the NVS has no room for the key of the signature cache: all signatures are checked again "
                          "at the next start");
        }
        s_hk_got = true;
    }
    memcpy(key, s_hk, 32);
}

// the HMAC-SHA256 of d with the key of this board
static bool sig_mac(const uint8_t *d, size_t n, uint8_t mac[32])
{
    uint8_t key[32];
    sig_key(key);
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&at, 256);
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&at, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_key_id_t k = 0;
    bool ok = psa_import_key(&at, key, sizeof key, &k) == PSA_SUCCESS;
    memset(key, 0, sizeof key);
    size_t ol = 0;
    if (ok) {
        ok = psa_mac_compute(k, PSA_ALG_HMAC(PSA_ALG_SHA_256), d, n, mac, 32, &ol) == PSA_SUCCESS && ol == 32;
    }
    if (k) {
        psa_destroy_key(k);
    }
    return ok;
}

// the file: "TSC1", the count (4 bytes), the hashes (32 bytes each), the HMAC of all before it
static void sig_cache_load(void)
{
    uint8_t *b = NULL, mac[32];
    size_t n = 0;
    if (!tfa_read_all(SIG_FILE, &b, &n, 8 + SIG_CACHE * 32 + 32)) {
        return;
    }
    uint32_t cnt = 0;
    if (n >= 40 && !memcmp(b, "TSC1", 4)) {
        memcpy(&cnt, b + 4, 4);
    }
    if (cnt && cnt <= SIG_CACHE && n == 8 + cnt * 32 + 32 && sig_mac(b, n - 32, mac) && !memcmp(mac, b + n - 32, 32)) {
        memcpy(s_sig_ok, b + 8, cnt * 32);
        s_nsig_ok = (int)cnt;
        s_sig_next = (int)(cnt % SIG_CACHE);
    } else {
        ESP_LOGW(TAG, "the signature cache file is not right: all signatures are checked again");
    }
    free(b);
}

void tfa_sig_cache_save(void)
{
    if (!s_sig_dirty || !s_sig_ok || !s_nsig_ok) {
        return;
    }
    size_t n = 8 + (size_t)s_nsig_ok * 32 + 32;
    uint8_t *b = heap_caps_malloc(n, MALLOC_CAP_SPIRAM);
    if (!b) {
        return;
    }
    uint32_t cnt = (uint32_t)s_nsig_ok;
    memcpy(b, "TSC1", 4);
    memcpy(b + 4, &cnt, 4);
    memcpy(b + 8, s_sig_ok, (size_t)s_nsig_ok * 32);
    if (sig_mac(b, n - 32, b + n - 32) && storage_write(SIG_FILE, b, n)) {
        s_sig_dirty = false;
    }
    free(b);
}

static bool sig_ok_raw(const uint8_t *pk, const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl);
// A signature with one key, through the cache. The cache hash holds the key, so a key that goes out of the list never
// hits the cache again. guard: built-in key 1 (a protected build also takes the signature of its app key, and the
// cache hash of key 1 is the same as before the publisher list: the cache of the board stays good).
static bool sig_ok_key(const uint8_t *pk, bool guard, const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl)
{
    if (sl != 64) {
        return false;
    }
    (void)guard;
    uint8_t h[32];
    size_t ol = 0;
    psa_hash_operation_t op = PSA_HASH_OPERATION_INIT;
    bool hashed =
            psa_hash_setup(&op, PSA_ALG_SHA_256) == PSA_SUCCESS && psa_hash_update(&op, msg, n) == PSA_SUCCESS &&
            psa_hash_update(&op, sig, sl) == PSA_SUCCESS && psa_hash_update(&op, pk, 65) == PSA_SUCCESS &&
#if CONFIG_TFN_DEVICE_PROTECTION
            (!guard || !device_guard_enabled() || psa_hash_update(&op, device_guard_app_key(), 65) == PSA_SUCCESS) &&
#endif
            psa_hash_finish(&op, h, sizeof h, &ol) == PSA_SUCCESS;
    if (!hashed) {
        psa_hash_abort(&op);
    }
    if (!s_sig_ok) {
        s_sig_ok = heap_caps_calloc(SIG_CACHE, 32, MALLOC_CAP_SPIRAM);
        if (s_sig_ok) {
            sig_cache_load();
        }
    }
    for (int i = 0; hashed && s_sig_ok && i < s_nsig_ok; i++) {
        if (!memcmp(s_sig_ok[i], h, 32)) {
            return true;
        }
    }
    if (!s_sig_bad) {
        s_sig_bad = heap_caps_calloc(SIG_BAD, 32, MALLOC_CAP_SPIRAM);
    }
    for (int i = 0; hashed && s_sig_bad && i < s_nsig_bad; i++) {
        if (!memcmp(s_sig_bad[i], h, 32)) {
            return false;
        }
    }
    bool ok = sig_ok_raw(pk, msg, n, sig, sl);
#if CONFIG_TFN_DEVICE_PROTECTION
    if (!ok && guard) {
        ok = device_guard_verify_app(msg, n, sig, sl);
    }
#endif
    if (ok && hashed && s_sig_ok) {
        memcpy(s_sig_ok[s_sig_next], h, 32);
        s_sig_next = (s_sig_next + 1) % SIG_CACHE;
        if (s_nsig_ok < SIG_CACHE) {
            s_nsig_ok++;
        }
        s_sig_dirty = true;
    }
    if (!ok && hashed && s_sig_bad) {
        memcpy(s_sig_bad[s_sig_bad_next], h, 32);
        s_sig_bad_next = (s_sig_bad_next + 1) % SIG_BAD;
        if (s_nsig_bad < SIG_BAD) {
            s_nsig_bad++;
        }
    }
    return ok;
}

// the built-in keys only (the permission policy, and the first check of each app): key 1, then key 2 when it is set
bool tfa_sig_ok(const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl)
{
    return sig_ok_key(TFN_PUBKEY, true, msg, n, sig, sl) ||
           (TFN_PUBKEY2[0] == 0x04 && sig_ok_key(TFN_PUBKEY2, false, msg, n, sig, sl));
}

// a signature with a key of the publisher list (main/tfn_pub.c)
bool tfa_sig_ok_pk(const uint8_t *pk, const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl)
{
    return sig_ok_key(pk, false, msg, n, sig, sl);
}

static bool sig_ok_raw(const uint8_t *pk, const uint8_t *msg, size_t n, const uint8_t *sig, size_t sl)
{
    psa_key_attributes_t at = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&at, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&at, 256);
    psa_set_key_usage_flags(&at, PSA_KEY_USAGE_VERIFY_MESSAGE);
    psa_set_key_algorithm(&at, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
    psa_key_id_t k = 0;
    if (psa_import_key(&at, pk, 65, &k) != PSA_SUCCESS) {
        return false;
    }
    psa_status_t st = psa_verify_message(k, PSA_ALG_ECDSA(PSA_ALG_SHA_256), msg, n, sig, sl);
    psa_destroy_key(k);
    return st == PSA_SUCCESS;
}

// ---------------------------------------------------------------- the permission policy
// The DEFAULT policy comes from the build (ELFPERM, ELFNEED of tfn_abi.inc). The manager tab PERMISSIONS sends
// CHANGES on top of it, signed with the manager key (the signature covers all the bytes of the text):
//   TFNPOL1
//   ver=<n>                 bigger = newer; the board keeps the newest one
//   abi=<ABI version>       a policy of another ABI version does not count
//   class=<id>|<text>       the text of a class of the build, or a new class (at the end of the list)
//   need=<name>|<id>        a name that needs this class now
//   free=<name>             a name that needs no class now
// A name that the policy does not name keeps the class of the build (also a name of a newer firmware). A bit of perms
// is the place of a class in the list (the classes of the build in their order, then the new ones).
#define POL_FILE STORAGE_ROOT "/tfn_policy.txt"
#define POL_SIG  STORAGE_ROOT "/tfn_policy.sig"

// the default of the build into id, tx, cls; gives the number of classes
static int policy_default(char (*id)[24], char (*tx)[64], uint8_t *cls)
{
    int n = 0, idx[CLS_MAX];
    for (int b = 0; b < CLS_MAX; b++) {
        idx[b] = -1;
        for (int i = 0; s_perm_def[i].id; i++) {
            if (s_perm_def[i].bit == b && n < CLS_MAX) {
                snprintf(id[n], 24, "%s", s_perm_def[i].id);
                snprintf(tx[n], 64, "%s", s_perm_def[i].text);
                idx[b] = n++;
            }
        }
    }
    memset(cls, CLS_NONE, N_SYMS);
    for (int i = 0; s_need_def[i].lo || s_need_def[i].hi; i++) {
        int k = sym_find(s_need_def[i].lo, s_need_def[i].hi);
        if (k >= 0 && s_need_def[i].bit < CLS_MAX && idx[s_need_def[i].bit] >= 0) {
            cls[k] = (uint8_t)idx[s_need_def[i].bit];
        }
    }
    tfa_pol_ver = 0;
    return n;
}

static int pol_num(const char *t, const char *key)
{
    char k[12];
    snprintf(k, sizeof k, "\n%s=", key);
    const char *p = strstr(t, k);
    return p ? atoi(p + strlen(k)) : -1;
}

// apply a policy text (0-ended) on top of the default. false: not a good policy (nothing changes)
static bool policy_apply(char *t, char *msg, int len)
{
    if (strncmp(t, "TFNPOL1\n", 8)) {
        snprintf(msg, len, "not a policy");
        return false;
    }
    int ver = pol_num(t, "ver"), abi = pol_num(t, "abi");
    if (ver <= 0 || abi != TFN_ABI_VERSION) {
        snprintf(msg, len, "policy ver %d for ABI %d (this board: ABI %d)", ver, abi, TFN_ABI_VERSION);
        return false;
    }
    char (*id)[24] = heap_caps_calloc(CLS_MAX, 24, MALLOC_CAP_SPIRAM);
    char (*tx)[64] = heap_caps_calloc(CLS_MAX, 64, MALLOC_CAP_SPIRAM);
    uint8_t *cls = heap_caps_malloc(N_SYMS, MALLOC_CAP_SPIRAM);
    if (!id || !tx || !cls) {
        free(id);
        free(tx);
        free(cls);
        snprintf(msg, len, "no memory");
        return false;
    }
    int keep = tfa_pol_ver;
    int n = policy_default(id, tx, cls), changed = 0, miss = 0;
    tfa_pol_ver = keep;
    for (char *l = t; l && *l;) {  // the classes first: their places
        char *e = strchr(l, '\n');
        char *bar = !strncmp(l, "class=", 6) ? strchr(l + 6, '|') : NULL;
        if (bar && (!e || bar < e) && bar - l - 6 > 0 && bar - l - 6 < 24) {
            int il = (int)(bar - l - 6), c = -1;
            for (int i = 0; i < n; i++) {
                if ((int)strlen(id[i]) == il && !strncmp(id[i], l + 6, il)) {
                    c = i;
                }
            }
            if (c < 0 && n < CLS_MAX) {
                c = n++;
                snprintf(id[c], 24, "%.*s", il, l + 6);
            }
            if (c >= 0) {
                snprintf(tx[c], 64, "%.*s", e ? (int)(e - bar - 1) : (int)strlen(bar + 1), bar + 1);
            }
        }
        l = e ? e + 1 : NULL;
    }
    char name[64];
    for (char *l = t; l && *l;) {  // then the names
        char *e = strchr(l, '\n');
        bool need = !strncmp(l, "need=", 5), fr = !strncmp(l, "free=", 5);
        if (need || fr) {
            char *bar = need ? strchr(l + 5, '|') : NULL;
            char *end = need ? bar : (e ? e : l + strlen(l));
            if (end && (!e || end <= e) && end - l - 5 > 0 && end - l - 5 < (int)sizeof name) {
                snprintf(name, sizeof name, "%.*s", (int)(end - l - 5), l + 5);
                int k = sym_find_name(name), c = -1;
                if (need) {
                    int cl = e ? (int)(e - bar - 1) : (int)strlen(bar + 1);
                    for (int i = 0; i < n; i++) {
                        if ((int)strlen(id[i]) == cl && !strncmp(id[i], bar + 1, cl)) {
                            c = i;
                        }
                    }
                }
                if (k >= 0 && (fr || c >= 0)) {
                    cls[k] = fr ? CLS_NONE : (uint8_t)c;
                    changed++;
                } else {
                    miss++;
                }
            }
        }
        l = e ? e + 1 : NULL;
    }
    // (the loader and the list read these on the app task: under the lock)
    LOCK();
    free(s_cls_id);
    free(s_cls_text);
    free(s_cls);
    s_cls_id = id;
    s_cls_text = tx;
    s_cls = cls;
    s_ncls = n;
    tfa_pol_ver = ver;
    UNLOCK();
    snprintf(msg, len, "policy %d: %d classes, %d names changed (%d not in this firmware)", ver, n, changed, miss);
    return true;
}

static void policy_load(void)
{
    uint8_t *t = NULL, *sig = NULL;
    size_t n = 0, sl = 0;
    char msg[96];
    if (tfa_read_all(POL_FILE, &t, &n, 256 * 1024) && tfa_read_all(POL_SIG, &sig, &sl, 256) &&
        tfa_sig_ok(t, n, sig, sl)) {
        char *z = heap_caps_malloc(n + 1, MALLOC_CAP_SPIRAM);
        if (z) {
            memcpy(z, t, n);
            z[n] = 0;
            if (policy_apply(z, msg, sizeof msg)) {
                ESP_LOGI(TAG, "%s (%s)", msg, POL_FILE);
            } else {
                ESP_LOGW(TAG, "%s: %s: the default of the build", POL_FILE, msg);
            }
            free(z);
        }
    } else if (t) {
        ESP_LOGW(TAG, "%s: no good signature: the default policy of the build", POL_FILE);
    }
    free(t);
    free(sig);
}

// a policy from the manager: "TFNP1\n" + the signature (128 hex digits) + "\n" + the text. The board keeps it when it
// is newer than its policy, and reads its apps again (their classes)
bool tfn_policy_blob(const char *b, size_t n, char *msg, int len)
{
    if (n < 6 + 129 || strncmp(b, "TFNP1\n", 6) || b[6 + 128] != '\n') {
        snprintf(msg, len, "not a policy file");
        return false;
    }
    uint8_t sig[64];
    for (int i = 0; i < 64; i++) {
        unsigned v;
        if (sscanf(b + 6 + 2 * i, "%2x", &v) != 1) {
            snprintf(msg, len, "a bad signature");
            return false;
        }
        sig[i] = (uint8_t)v;
    }
    const char *t = b + 6 + 129;
    size_t tn = n - 6 - 129;
    if (!tfa_sig_ok((const uint8_t *)t, tn, sig, 64)) {
        snprintf(msg, len, "the signature of the manager is not right");
        return false;
    }
    char *z = heap_caps_malloc(tn + 1, MALLOC_CAP_SPIRAM);
    if (!z) {
        snprintf(msg, len, "no memory");
        return false;
    }
    memcpy(z, t, tn);
    z[tn] = 0;
    int ver = pol_num(z, "ver");
    if (ver <= tfa_pol_ver) {
        free(z);
        snprintf(msg, len, "the board has policy %d", tfa_pol_ver);
        return true;
    }
    bool ok = policy_apply(z, msg, len);
    free(z);
    if (!ok) {
        return false;
    }
    FILE *f = fopen(POL_FILE, "wb"), *g = f ? fopen(POL_SIG, "wb") : NULL;
    bool w = f && g && fwrite(t, 1, tn, f) == tn && fwrite(sig, 1, 64, g) == 64;
    if (f) {
        fclose(f);
    }
    if (g) {
        fclose(g);
    }
    if (!w) {
        ESP_LOGW(TAG, "cannot keep the policy in %s (it holds until the next start)", POL_FILE);
    }
    ESP_LOGI(TAG, "%s", msg);
    tfn_rescan();  // (the classes of the installed apps)
    return true;
}

// the console "apps policy reset" (tests): the policy files go, the default of the build comes back
void tfn_policy_reset(void)
{
    unlink(POL_FILE);
    unlink(POL_SIG);
    LOCK();
    if (s_cls_id && s_cls_text && s_cls) {
        s_ncls = policy_default(s_cls_id, s_cls_text, s_cls);
    }
    UNLOCK();
    tfn_rescan();
}

// the console "apps policy": the policy and its classes (and the class of a name: "apps policy <name>")
void tfn_policy_log(const char *name)
{
    ESP_LOGI(TAG, "policy %d, %d classes", tfa_pol_ver, s_ncls);
    for (int i = 0; i < s_ncls; i++) {
        int n = 0;
        for (int k = 0; s_cls && k < N_SYMS; k++) {
            n += s_cls[k] == i;
        }
        ESP_LOGI(TAG, "  %2d %-12s %3d names  %s", i, s_cls_id[i], n, s_cls_text[i]);
    }
    if (name && name[0]) {
        int k = sym_find_name(name);
        ESP_LOGI(TAG, "  %s: %s", name,
                 k < 0                            ? "not in the ABI"
                 : !s_cls || s_cls[k] == CLS_NONE ? "free"
                                                  : s_cls_id[s_cls[k]]);
    }
}

bool tfa_id_ok(const char *id)
{
    if (!id[0] || strlen(id) > 20) {
        return false;
    }
    for (const char *p = id; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_')) {
            return false;
        }
    }
    return true;
}

// the value of a key in the manifest text ("" when it is not there)
void tfa_mval(const char *txt, const char *key, char *out, int max)
{
    size_t kl = strlen(key);
    out[0] = 0;
    for (const char *p = txt; p && *p;) {
        if (!strncmp(p, key, kl) && p[kl] == '=') {
            const char *v = p + kl + 1;
            int k = 0;
            while (v[k] && v[k] != '\n' && v[k] != '\r' && k < max - 1) {
                out[k] = v[k];
                k++;
            }
            out[k] = 0;
            return;
        }
        p = strchr(p, '\n');
        if (p) {
            p++;
        }
    }
}
long tfa_mnum(const char *txt, const char *key)
{
    char v[24];
    tfa_mval(txt, key, v, sizeof v);
    return strtol(v, NULL, 10);
}

static int group_cat(const char *g)
{
    if (!strcasecmp(g, "UTIL")) {
        return 1;
    }
    if (!strcasecmp(g, "SETTINGS")) {
        return 2;
    }
    return 0;
}

void tfn_init(void)
{
    if (s_inited) {
        return;
    }
    s_inited = true;
    psa_crypto_init();
    tfn_store_init();
    tfn_pub_init();  // (the publisher list: the scan below checks the apps with it)
    tfa_lock = xSemaphoreCreateRecursiveMutex();
#if TFA_ICON_BIG
    // (the stack in internal RAM: some icons are in the flash (/data), and a flash read turns the cache off)
    xTaskCreatePinnedToCore(tfa_icon_prefetch_task, "iconpre", 6144, NULL, 1, NULL, 0);
#endif
    tfa_e = heap_caps_calloc(TFN_MAX, sizeof(tfn_entry_t), MALLOC_CAP_SPIRAM);
    // the table that the loader uses: the ABI names sorted by their hash, with the versions that note what an app
    // holds (OWN_WRAP); then the policy (the default of the build, or the newer one of the manager)
    int wrapped = 0;
    s_tab = heap_caps_malloc(sizeof s_syms, MALLOC_CAP_SPIRAM);
    s_cls = heap_caps_malloc(N_SYMS, MALLOC_CAP_SPIRAM);
    s_cls_id = heap_caps_calloc(CLS_MAX, 24, MALLOC_CAP_SPIRAM);
    s_cls_text = heap_caps_calloc(CLS_MAX, 64, MALLOC_CAP_SPIRAM);
    if (s_tab) {
        memcpy(s_tab, s_syms, sizeof s_syms);
        qsort(s_tab, N_SYMS, sizeof s_tab[0], sym_cmp);
        for (size_t w = 0; w < sizeof OWN_WRAP / sizeof OWN_WRAP[0]; w++) {
            int i = sym_find_name(OWN_WRAP[w].name);
            if (i >= 0) {
                s_tab[i].sym = OWN_WRAP[w].fn;
                wrapped++;
            }
        }
    }
    if (s_cls_id && s_cls_text && s_cls) {
        s_ncls = policy_default(s_cls_id, s_cls_text, s_cls);
    }
    policy_load();
    elf_set_symbol_resolver(tfa_resolve_sym);
    ESP_LOGI(TAG, "ABI %d rev %d: %d firmware names for the apps, %d of them noted; policy %d (%d classes)",
             TFN_ABI_VERSION, TFN_ABI_REV, N_SYMS, wrapped, tfa_pol_ver, s_ncls);
    tfa_safe = board_boot_key_down();
    // the crash guard: a reset while an installed app was open
    esp_reset_reason_t why = esp_reset_reason();
    if (tfa_guard.magic == GUARD_MAGIC && !tfa_watch_crashed()) {  // (a crash in a check of a watcher: watch_init)
        tfa_guard.id[sizeof tfa_guard.id - 1] = 0;
        bool crash = why == ESP_RST_PANIC || why == ESP_RST_INT_WDT || why == ESP_RST_TASK_WDT || why == ESP_RST_WDT;
        if (crash && tfa_id_ok(tfa_guard.id)) {
            char key[16];
            tfa_crash_key(tfa_guard.id, key);
            uint32_t n = store_get(key, 0) + 1;
            store_set(key, n);
            snprintf(s_crash_id, sizeof s_crash_id, "%s", tfa_guard.id);
            s_crash_reason = (int)why;
            ESP_LOGE(TAG, "the installed app %s stopped the board (reset %d, %lu times)", tfa_guard.id, (int)why,
                     (unsigned long)n);
        }
    }
    tfa_guard.magic = 0;
    if (!board_sd_mounted()) {
        board_sd_mount();  // (no card: false after about 0.2 s)
    }
    tfn_tmp_clean();  // (after the mount: the upload folders of the card go too; the web server is not on yet)
    if (!tfa_e) {
        ESP_LOGE(TAG, "no memory for the list");
        return;
    }
    if (tfa_safe) {
        ESP_LOGW(TAG, "SAFE MODE (the BOOT key): no installed app loads");
    }
    tfa_do_rescan();
}

// ---------------------------------------------------------------- the state
int tfn_ver_cmp(const char *a, const char *b)
{
    bool na = a && a[0] >= '0' && a[0] <= '9', nb = b && b[0] >= '0' && b[0] <= '9';
    if (!na || !nb) {
        return na == nb ? 0 : na ? -1 : 1;
    }
    while (*a || *b) {
        long x = strtol(a, (char **)&a, 10), y = strtol(b, (char **)&b, 10);
        if (x != y) {
            return x < y ? -1 : 1;
        }
        if (*a == '.') {
            a++;
        }
        if (*b == '.') {
            b++;
        }
        if ((*a && (*a < '0' || *a > '9')) || (*b && (*b < '0' || *b > '9'))) {
            break;
        }
    }
    return 0;
}
bool tfn_newer_than_fw(const char *ver)
{
    return tfn_ver_cmp(ver, ota_version()) > 0;
}

int tfn_json_locked(char *o, int max);
int tfn_json(char *o, int max)
{
    LOCK();
    int k = tfn_json_locked(o, max);
    UNLOCK();
    return k;
}
int tfn_json_locked(char *o, int max)
{
    static const char *const ST[] = {"ok", "dev", "bad_sig", "bad_abi", "bad_kind", "bad_file", "stopped"};
    uint32_t ct = 0, cf = 0;
    bool card = board_sd_mounted() && board_sd_info(&ct, &cf);
    uint64_t dt = 0, df = 0;
    esp_vfs_fat_info(STORAGE_ROOT, &dt, &df);
    int k = snprintf(o, max,
                     "{\"kind\":\"%s\",\"abi\":%d,\"rev\":%d,\"policy\":%d,\"dev\":%s,\"safe\":%s,\"board\":{\"total\":"
                     "%llu,\"free\":%llu},"
                     "\"card\":{\"in\":%s,\"total\":%llu,\"free\":%llu},\"crash\":\"%s\",\"crash_reason\":%d,"
                     "\"mgr\":%s,\"profile\":\"%s\",\"inst\":\"%s\",\"max\":%d,\"unlisted\":%d,\"apps\":[",
                     TFN_KIND, TFN_ABI_VERSION, TFN_ABI_REV, tfa_pol_ver, tfn_dev_mode() ? "true" : "false",
                     tfa_safe ? "true" : "false", (unsigned long long)dt, (unsigned long long)df,
                     card ? "true" : "false", (unsigned long long)ct * 1024, (unsigned long long)cf * 1024, s_crash_id,
                     s_crash_reason, tfn_store_configured() ? "true" : "false", tfn_store_profile(),
                     tfn_inst_place_card() ? "card" : "board", TFN_MAX, tfa_dropped);
    for (int i = 0; i < tfa_n && k < max - 300; i++) {
        const tfn_entry_t *e = &tfa_e[i];
        bool loaded = tfa_ld[0].idx == i || tfa_ld[1].idx == i;
        k += snprintf(o + k, max - k,
                      "%s{\"id\":\"%s\",\"name\":\"%s\",\"ver\":\"%s\",\"code\":%lu,\"group\":\"%s\",\"place\":\"%s\","
                      "\"state\":\"%s\",\"bytes\":%lu,\"ram\":%lu,\"loaded\":%s,\"req\":%s,\"rev\":%u,\"perms\":%lu,"
                      "\"form\":%s,\"watch\":%u}",
                      i ? "," : "", e->id, e->name, e->ver, (unsigned long)e->code, e->group,
                      e->place == TFN_CARD ? "card" : "board", ST[e->state], (unsigned long)e->bytes,
                      (unsigned long)e->ram, loaded ? "true" : "false", tfn_store_required(e->id) ? "true" : "false",
                      (unsigned)e->rev, (unsigned long)e->perms, e->form ? "true" : "false", (unsigned)e->watch_min);
        if (e->pub[0] && k > 0 && k < max - 48) {  // (a key of the publisher list: its name and the kind of pass)
            static const char *const PS[] = {"", "", "store", "test", "not_reviewed"};
            k--;  // (in place of the "}" of the entry)
            k += snprintf(o + k, max - k, ",\"pub\":\"%s\",\"pub_st\":\"%s\"}", e->pub,
                          PS[e->pub_st < 5 ? e->pub_st : 0]);
        }
    }
    // the apps of this firmware and its version (the manager installs no copy of a built-in app that is not newer)
    const char *bn[N_BUILTIN_MAX];
    int nb = menu_builtin_names(bn, N_BUILTIN_MAX);
    int lay = layout_now();
    k += snprintf(o + k, max - k, "],\"fw\":\"%s\",\"layout\":\"%s\",\"mig\":\"%s\",\"dropped\":", ota_version(),
                  lay == LAYOUT_OLD    ? "old"
                  : lay == LAYOUT_SLIM ? "slim"
                                       : "other",
                  layout_state());
    if (max - k > 600) {
        k += layout_drop_json(o + k, 512);
    } else {
        k += snprintf(o + k, max - k, "[]");  // (a full buffer: the JSON stays valid)
    }
    k += snprintf(o + k, max - k, ",\"builtin\":[");
    for (int i = 0; i < nb && k < max - 40; i++) {
        k += snprintf(o + k, max - k, "%s\"%s\"", i ? "," : "", bn[i]);
    }
    k += snprintf(o + k, max - k, "]}");
    return k < max ? k : max - 1;
}

int tfn_group_of(const app_t *a)
{
    const tfn_entry_t *e = tfn_entry_of(a);
    return e ? group_cat(e->group) : 0;
}
