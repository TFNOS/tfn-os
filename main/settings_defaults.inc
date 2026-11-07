// The one-time reset of the saved choices of the apps (settings.c includes this file; settings_load() calls
// defaults_once() after the NVS start).
//
// The defaults: all the apps with English and Hebrew start in Hebrew, and all the optional tilts are ON, on all
// the boards, with no exceptions (also PADS and the Watcher). A new default in an app does not change a board that has
// an old choice saved (QUEST and SUNSET save the language at each save). Thus this step writes the new values ONE time on
// each board, at the first start of the new firmware. Then the key "def_v" is 1, and the step does not run again: a
// later choice of the user (EN, TILT OFF) stays.
//
// A next reset: add a step "if (v < 2) { ... }" below, and make DEFAULTS_V 2.
// This file uses only store_get() and store_set(): the host test of the reset includes it with
// its own store.

#define DEFAULTS_V 1            // the number of the last step: the value of "def_v" after all the steps
#define DEF_NONE   0xEEEEEEEEu  // (store_get(): no value saved; no app writes this value to these keys)

// the language keys of the apps with English and Hebrew (0 = English, 1 = Hebrew)
static const char *const DEF_LANG_KEYS[] = {
        "q_lang",    // QUEST (WHITE CITY)
        "sun_lang",  // SUNSET
        "k_lang",    // KEEPER
        "r_lang",    // ROOM
        "lf_lang",   // FRAME
        "ls_lang",   // STAND-IN
        "sx_lang",   // STATION X
        "wr_lang",   // WRONG ROOM
        "lw_lang",   // LAST SHOW
        "lb_lang",   // SIDE B
};

static void defaults_once(void)
{
    const uint32_t v = store_get("def_v", 0);
    if (v >= DEFAULTS_V) {
        return;
    }
    if (v < 1) {
        // step 1: Hebrew in all the apps with English and Hebrew
        for (unsigned i = 0; i < sizeof DEF_LANG_KEYS / sizeof DEF_LANG_KEYS[0]; i++) {
            store_set(DEF_LANG_KEYS[i], 1);
        }
        // ... and TILT ON in all the optional tilts
        store_set("pb_tilt", 1);                        // TEAM BUILDING
        store_set("wr_tilt", 1);                        // WRONG ROOM
        store_set("jr_tilt", 1);                        // JUNGLE RUN
        store_set("ten_ctl", 2);                        // TENNIS: the control + 1 (1 = DRAG, 2 = TILT)
        uint32_t w = store_get("pads_set3", DEF_NONE);  // PADS: bit 8 of its settings word (the other bits stay)
        if (w != DEF_NONE) {
            store_set("pads_set3", w | (1u << 8));
        }
        w = store_get("drm_set", 0);  // DRUMS: LOCK (bit 22: the tilt does not change the tune) off
        if (w & 0x80000000u) {
            store_set("drm_set", w & ~(1u << 22));  // (bit 31: the word is valid)
        }
    }
    store_set("def_v", DEFAULTS_V);
}
