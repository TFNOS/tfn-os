// COMPANIONS: THE UPDATE OF A COMPANION BY RADIO. The firmware of the companions comes from the store of the board
// (comp_fw_start / comp_fw_poll of the service: a signed index and a file for each chip), or from a file on the board
// (any name that ends with .bin in /sdcard/companion/ or /data/companion/: the way with no WiFi). The file itself
// tells its chip and its version (tc_img_info). A companion with firmware 3 or later can get a newer one (or the same one again)
// by radio; the old firmware stays when the new one does not reach its master (the rollback of the companion).

// the newest file of each chip
static struct {
    char path[80];
    int fw, size;
} s_img[TC_CHIP_N];

static void img_scan(void)
{
    memset(s_img, 0, sizeof s_img);
    static const char *const DIRS[] = {"/sdcard/companion", "/data/companion"};
    for (int d = 0; d < 2; d++) {
        DIR *dp = opendir(DIRS[d]);
        if (!dp) {
            continue;
        }
        struct dirent *e;
        while ((e = readdir(dp)) != NULL) {
            const size_t n = strlen(e->d_name);
            if (n < 5 || strcasecmp(e->d_name + n - 4, ".bin")) {
                continue;
            }
            char path[80];
            snprintf(path, sizeof path, "%s/%s", DIRS[d], e->d_name);
            FILE *f = fopen(path, "rb");
            if (!f) {
                continue;
            }
            uint8_t h[96];
            const int got = (int)fread(h, 1, sizeof h, f);
            fseek(f, 0, SEEK_END);
            const long size = ftell(f);
            fclose(f);
            int chip = 0, fw = 0;
            if (tc_img_info(h, got, &chip, &fw) && chip > 0 && chip < TC_CHIP_N && fw > s_img[chip].fw && size > 0 &&
                size < 0x1E0000) {
                snprintf(s_img[chip].path, sizeof s_img[chip].path, "%s", path);
                s_img[chip].fw = fw;
                s_img[chip].size = (int)size;
            }
        }
        closedir(dp);
    }
}

// THE STORE: the newest version of the chip of the companion on the page (asked once for each chip)
static struct {
    int chip;
    int state;  // 0 not asked, 1 the question runs, 2 fw is known, -1 no answer
    int fw;
} s_sfw;

static const char *chip_target(int chip)
{
    static const char *const T[TC_CHIP_N] = {"", "esp32", "esp32s3", "esp32c3", "esp32c6", "esp32s2"};
    return chip > 0 && chip < TC_CHIP_N ? T[chip] : "";
}

static void store_ask(const tcm_comp_t *c)
{
    if (!c || !c->has_info || c->chip <= 0 || c->chip >= TC_CHIP_N || s_sfw.chip == c->chip) {
        return;
    }
    if (comp_fw_start(chip_target(c->chip), false)) {
        s_sfw.chip = c->chip;
        s_sfw.state = 1;
        s_sfw.fw = 0;
    }
}

// the store version of the chip of c (0: none known)
static int store_fw(const tcm_comp_t *c)
{
    return c && s_sfw.state == 2 && s_sfw.chip == c->chip ? s_sfw.fw : 0;
}

// the file for a companion (NULL: none)
static const char *img_for(const tcm_comp_t *c, int *fw)
{
    if (!c || c->chip <= 0 || c->chip >= TC_CHIP_N || !s_img[c->chip].fw) {
        return NULL;
    }
    if (fw) {
        *fw = s_img[c->chip].fw;
    }
    return s_img[c->chip].path;
}

// the newest firmware for a companion: the store or the card (false: none)
static bool best_fw(const tcm_comp_t *c, int *fw)
{
    int card = 0;
    img_for(c, &card);
    const int st = store_fw(c);
    *fw = st > card ? st : card;
    return *fw > 0;
}

// THE UPDATE NOW
enum {
    U_SEND,   // the firmware goes by radio
    U_FETCH,  // the file comes from the store
    U_WAIT,   // the companion starts the new firmware: wait for its INFO
    U_OK,
    U_FAIL
};
static struct {
    int state, fw, size;
    uint8_t *buf;  // the file (PSRAM)
    int64_t t;     // the start of the state
    bool asked;    // U_WAIT: the INFO request went out
    int why;       // U_FAIL: the text
} s_up;

static void up_free(void)
{
    if (s_up.buf) {
        heap_caps_free(s_up.buf);
        s_up.buf = NULL;
    }
}

static void up_fail(int why)
{
    up_free();
    s_up.state = U_FAIL;
    s_up.why = why;
    audio_tune(TUNE_FAIL, 1);
}

static void up_send(void)
{
    const tcm_comp_t *c = cur();
    int chip = 0, v = 0;
    if (!c || !tc_img_info(s_up.buf, s_up.size, &chip, &v) || chip != c->chip) {
        up_fail(T_UPD_READ);
        return;
    }
    s_up.fw = v;
    s_up.state = U_SEND;
    if (!tcm_ota(s_ci, s_up.buf, s_up.size, v)) {
        up_fail(T_UPD_FAIL);
    }
}

// the file of the card, and the start of the update
static void up_from_card(void);

// start the update: the file of the store when it is the newest one, else the file of the card
static void up_start(void)
{
    const tcm_comp_t *c = cur();
    int card = 0;
    img_for(c, &card);
    memset(&s_up, 0, sizeof s_up);
    s_up.t = s_now;
    go(S_UPDATE);
    if (store_fw(c) >= card && store_fw(c) > 0 && comp_fw_start(chip_target(c->chip), true)) {
        s_up.state = U_FETCH;
        return;
    }
    up_from_card();
}

static void up_from_card(void)
{
    const tcm_comp_t *c = cur();
    int fw = 0;
    const char *path = img_for(c, &fw);
    if (!path) {
        up_fail(T_UPD_READ);
        return;
    }
    const int size = s_img[c->chip].size;
    s_up.buf = heap_caps_malloc((size_t)size, MALLOC_CAP_SPIRAM);
    FILE *f = s_up.buf ? fopen(path, "rb") : NULL;
    const int got = f ? (int)fread(s_up.buf, 1, (size_t)size, f) : 0;
    if (f) {
        fclose(f);
    }
    if (got != size) {
        up_fail(T_UPD_READ);
        return;
    }
    s_up.size = size;
    up_send();
}

static void up_tick(void)
{
    // the store: the question of the newest version (once for each chip), its answer
    if (s_scr == S_COMP && s_ci >= 0 && s_ci < tcm_count()) {
        store_ask(cur());
    }
    if (s_sfw.state == 1 && s_up.state != U_FETCH) {
        int fw = 0;
        const int r = comp_fw_poll(&fw, NULL, NULL, NULL, 0);
        if (r != 0) {
            s_sfw.state = r > 0 ? 2 : -1;
            s_sfw.fw = r > 0 ? fw : 0;
        }
    }
    if (s_scr != S_UPDATE || s_ci < 0 || s_ci >= tcm_count()) {
        return;
    }
    const tcm_comp_t *c = cur();
    if (s_up.state == U_FETCH) {
        uint8_t *img = NULL;
        int len = 0;
        const int r = comp_fw_poll(NULL, &img, &len, NULL, 0);
        if (r > 0 && img) {
            s_up.buf = img;
            s_up.size = len;
            up_send();
        } else if (r != 0) {
            int card = 0;
            if (img_for(c, &card)) {
                up_from_card();  // (no store now: the file of the card)
            } else {
                up_fail(T_UPD_NET);
            }
        }
        return;
    }
    if (s_up.state == U_SEND && tcm_req_type(s_ci) == TCM_R_OTA) {
        const int st = tcm_req(s_ci);
        // (a lost answer of the END: the companion has all the bytes and maybe starts the new firmware already)
        if (st == TCM_DONE || (st == TCM_FAIL && tcm_ota_done(s_ci) == s_up.size)) {
            up_free();
            s_up.state = U_WAIT;
            s_up.t = s_now;
            s_up.asked = false;
        } else if (st == TCM_FAIL) {
            up_fail(T_UPD_FAIL);
        }
        return;
    }
    if (s_up.state == U_WAIT) {
        // the new firmware: the companion searches its master again (a few seconds), then its INFO tells the version
        if (!s_up.asked && s_now - s_up.t > 3000 && tcm_online(s_ci) && tcm_req(s_ci) != TCM_BUSY) {
            tcm_info(s_ci);
            s_up.asked = true;
        } else if (s_up.asked && tcm_req_type(s_ci) == TCM_R_INFO && tcm_req(s_ci) != TCM_BUSY) {
            if (tcm_req(s_ci) == TCM_DONE && c->info.fw == s_up.fw) {
                s_up.state = U_OK;
                audio_tune(TUNE_PAIRED, 3);
            } else if (tcm_req(s_ci) == TCM_DONE) {
                up_fail(T_UPD_FAIL);  // (the old version: the new firmware did not start)
            } else {
                s_up.asked = false;
            }
        }
        if (s_up.state == U_WAIT && s_now - s_up.t > 60000) {
            up_fail(T_UPD_FAIL);
        }
    }
}

static void draw_update(void)
{
    const tcm_comp_t *c = cur();
    if (!c) {
        go(S_LIST);
        return;
    }
    tu_begin(1, s_now);
    int x, w;
    int y = T_.by0 + TU_MM(2);
    tu_row_x(y, TU_MM(10), &x, &w);
    char big[32];
    uint16_t col = TU_TEXT;
    int pct = 0;
    if (s_up.state == U_SEND && s_up.size > 0) {
        pct = (int)((int64_t)tcm_ota_done(s_ci) * 100 / s_up.size);
        snprintf(big, sizeof big, "%d %%", pct);
    } else if (s_up.state == U_WAIT || s_up.state == U_FETCH) {
        snprintf(big, sizeof big, "%s", "...");
        pct = s_up.state == U_WAIT ? 100 : 0;
    } else if (s_up.state == U_OK) {
        snprintf(big, sizeof big, "%s %d", T(T_FIRMWARE), c->info.fw);
        col = TU_ACC;
        pct = 100;
    } else {
        snprintf(big, sizeof big, "%s", T(T_REFUSED));
        col = TU_BAD;
    }
    tu_text_c(T_.f_big, x + w / 2, w, y + T_.f_big->asc, big, col);
    y += T_.f_big->line + TU_MM(1.5f);
    // the bar
    tu_row_x(y, TU_MM(3), &x, &w);
    ui_rbox(x, y, w, TU_MM(3), TU_MM(1.5f), TU_SURF, TU_LINE, 1);
    if (pct > 0) {
        ui_rbox(x, y, w * pct / 100 > TU_MM(3) ? w * pct / 100 : TU_MM(3), TU_MM(3), TU_MM(1.5f),
                s_up.state == U_FAIL ? TU_BAD : TU_ACC, TU_ACC, 0);
    }
    y += TU_MM(5);
    const int tip = s_up.state == U_SEND    ? T_UPD_SEND
                    : s_up.state == U_FETCH ? T_UPD_FETCH
                    : s_up.state == U_WAIT  ? T_UPD_WAIT
                    : s_up.state == U_OK    ? T_UPD_OK
                                            : s_up.why;
    tu_para(T_.f_small, T(tip), T_.bx0, T_.bx1 - T_.bx0, y, T_.by1, s_up.state == U_FAIL ? TU_WARN : TU_DIM);
    s_tu_content = 0;
    char title[48];
    snprintf(title, sizeof title, "%s: %s", T(T_UPD_TITLE), c->name);
    tu_chrome(title, s_up.state == U_OK || s_up.state == U_FAIL, NULL, 0, NULL, 0);
    tu_act(0, T(s_up.state == U_SEND || s_up.state == U_WAIT || s_up.state == U_FETCH ? T_CANCEL : T_DONE_UPD),
           TU_K_NORMAL);
}

// CANCEL or DONE: back to the companion (a cancel ends the request; the companion drops its half update after 15 s)
static void up_leave(void)
{
    if (s_up.state == U_SEND && s_ci >= 0 && tcm_req_type(s_ci) == TCM_R_OTA) {
        tcm_info(s_ci);
    }
    up_free();
    go(S_COMP);
}
