// The settings panel (sysui.h): the pull, the sliders, the buttons, the chips of the app, the keys, the Watcher
// wheel, the drawing of the panel and the grip (the layouts of each frame kind: sysui_layout.c).
#include "sysui_priv.h"
#include "tfn/theme.h"

// ---------------------------------------------------------------- the settings panel
// sysui.h. A pull from the zone (input.c holds a new finger there; a move down is a pull) brings the panel: it follows the
// finger; a release past 35 % of its height, or a flick down, opens it, else it goes back. Open: VOLUME and BRIGHTNESS (a
// drag on the row: the value follows the finger), RESUME (also a tap on the game under the panel, a swipe up on the panel,
// the BOOT key of the 3.49), MENU (SETUP in the menu). The Watcher: the wheel moves a ring over the four controls; a push
// on a slider starts and ends its change (a turn = 5 %). All the controls are 5 mm or more on the glass; RESUME and MENU
// are far apart (a thumb that misses RESUME does not leave the game), and they act at the lift of the finger.
static const kfont_t *const KFS[] = {&KF_LABEL, &KF_NAME, &KF_TEXT, &KF_BIG, &KF_TITLE};  // (13, 15, 17, 23, 34 px)

// the text of the panel: English, or Hebrew when the app speaks Hebrew (its language key in the store: 1 = Hebrew)
enum {
    PT_VOL,
    PT_BRI,
    PT_RESUME,
    PT_MENU,
    PT_SETUP,
    PT_MUTE,
    PT_HOLD,
    PT_AGAIN,
    PT_MORE,
    PT_N
};
static const char *const PTXT[2][PT_N] = {
        {"VOLUME", "BRIGHTNESS", "RESUME", "MENU", "SETUP", "MUTE", "HOLD", "PUSH AGAIN", "MORE"},
        {"עוצמת קול", "בהירות", "המשך", "תפריט", "הגדרות", "מושתק", "להחזיק", "לחיצה נוספת", "עוד"},
};
static const pitems_t *s_items;  // (the items of the app: see below)
static bool app_hebrew(const app_t *a)
{
    static const struct {
        const app_t *app;
        const char *key;
    } L[] = {
#if ARCADE_APP_ON(QUEST)
            {&app_quest, "q_lang"},
#endif
#if ARCADE_APP_ON(SUNSET)
            {&app_sunset, "sun_lang"},
#endif
#if ARCADE_APP_ON(KEEPER)
            {&app_keeper, "k_lang"},
#endif
#if ARCADE_APP_ON(ROOM)
            {&app_room, "r_lang"},
#endif
#if ARCADE_APP_ON(FRAME)
            {&app_frame, "lf_lang"},
#endif
#if ARCADE_APP_ON(SUBST)
            {&app_subst, "ls_lang"},
#endif
#if ARCADE_APP_ON(STATION)
            {&app_station, "sx_lang"},
#endif
#if ARCADE_APP_ON(WRONG)
            {&app_wrong, "wr_lang"},
#endif
#if ARCADE_APP_ON(SHOW)
            {&app_show, "lw_lang"},
#endif
#if ARCADE_APP_ON(SIDEB) && ARCADE_HAS_SIDEB
            {&app_sideb, "lb_lang"},
#endif
#if ARCADE_APP_ON(STORE)
            {&app_store, "st_lang"},  // (the language of the descriptions on the app page)
#endif
            {NULL, NULL},
    };
    for (unsigned i = 0; L[i].app; i++) {
        if (L[i].app == a) {
            return store_get(L[i].key, 1) == 1;
        }
    }
    // another app (a store app too): its own LANGUAGE item of the panel (a PI_PICK with a store key) tells it
    if (s_items) {
        for (int i = 0; i < s_items->n; i++) {
            const pitem_t *it = &s_items->items[i];
            if (it->kind != PI_PICK || !it->key || !it->vals || !it->name[0] || strcmp(it->name[0], "LANGUAGE")) {
                continue;
            }
            uint32_t v = store_get(it->key, it->def);
            return v < it->n && !strcmp(it->vals[2 * v], "HEBREW");
        }
    }
    return false;
}

// THE TWO-STEP PULL: the instruments that read all fingers in each frame and sound at the touch have a pad under the
// zone (DRUMS, PADS): a hold there would make the hit late. CHESS fills the screen with its board, and a one-finger pan
// that starts at the top edge must stay a pan. In them a finger in the zone is not held (it is the app's at once); a pull
// only shows the tab for TAB_MS, and a touch on the tab opens the panel.
#define TAB_MS 2500
static bool app_two_step(const app_t *a)
{
#if ARCADE_APP_ON(DRUMS)
    if (a == &app_drums) {
        return true;
    }
#endif
#if ARCADE_APP_ON(PADS)
    if (a == &app_pads) {
        return true;
    }
#endif
#if ARCADE_APP_ON(CHESS)
    if (a == &app_chess) {
        return true;
    }
#endif
    // the board games of the store (app files: no pointer to compare; a drag of a piece from the top middle
    // of the CHECKERS board must not open the panel)
    if (a && a->name && (!strcmp(a->name, "CHECKERS") || !strcmp(a->name, "CHESS"))) {
        return true;
    }
    // GUMMY SIEGE: a finger down anywhere starts the pull of the sling (and in the flight a touch anywhere is the
    // power): a pull of the sling that starts at the top middle must stay a pull of the sling
    if (a && a->name && !strcmp(a->name, "GUMMY SIEGE")) {
        return true;
    }
    return false;
}
static int64_t s_tab_until;  // the tab of the two-step pull shows until this time
static bool s_from_tab;      // the panel came from the tab: it opens at the lift of the finger

enum {
    PS_OFF,
    PS_DRAG,
    PS_OPENING,
    PS_OPEN,
    PS_CLOSING
};
static uint8_t s_pst = PS_OFF;
static float s_pan;    // the part of the panel that shows (0..1)
static int s_drag_y0;  // the pull: where its finger came down (logical y)
static uint32_t s_pull_seq;
static int64_t s_ptick, s_grip_until;
static bool s_open_req, s_hold_open;    // sysui_panel_open(), a hold of the wheel button (MENU in the ring)
static bool s_phe, s_pmenu;             // the panel speaks Hebrew; the app is the menu (SETUP in place of MENU)
static int s_grab = -1, s_press = -1;   // the slider under the finger; the button under it (the lift acts)
static int64_t s_press_t, s_hold_hint;  // the time of the touch on a button; MENU says HOLD until this time
static int s_press_item = -2;           // the item of the chip at the touch (the chips can move under the finger)
#define MENU_HOLD_MS 600                // MENU (SETUP) acts only after a still hold: a thumb that misses a knob
static bool s_below, s_swipe;           // the touch began under the panel (RESUME); on its body (a swipe up closes)
static int s_swipe_y, s_touch_x, s_touch_y;
static bool s_touch;      // a finger of the panel is down
static int s_focus = -1;  // the wheel ring (-1: none)
static bool s_adjust, s_wheel_used, s_pchanged, s_vol_moved;
static uint16_t *s_psnap;  // the frozen picture under the panel (PSRAM), its size
static int s_psnap_w, s_psnap_h;
static input_zone_t s_zone_now;

bool sysui_panel_on(void)
{
    return s_pst != PS_OFF;
}
void sysui_panel_open(void)
{
    s_open_req = true;
}

// ---- the items of the app (sysui.h): the chips of the page now, the layout now (PDEF, or PDEFI with the chips)
#define ARM_MS       3000  // a PI_ACT2: a short tap shows HOLD this long; the wheel: the second push
#define ACT2_HOLD_MS 1000  // a PI_ACT2 acts after a still hold of this long (at the lift)
static const pitems_t *s_items;
static int s_ni;               // the chips on the screen now
static int8_t s_ci[SLOT_MAX];  // the item of each chip (an index of s_items->items; -1: MORE; CHIP_NOTE)
#define CHIP_NOTE (-3)         // the chip NOTICES: it opens the notification center (notify.h)
static pbox_t s_cb[SLOT_MAX];  // the box of each chip
static int s_ipage, s_npages;  // the page of the chips, the pages
static int s_arm = -1;         // the PI_ACT2 item that a short tap (HOLD) or a first push of the wheel marked
static bool s_arm_wheel;       // (the mark came from the wheel: its second push acts)
static int64_t s_arm_t;
static bool s_redraw;  // an item changed the app: main.c draws the picture under the panel again
static pdef_t s_L;     // the layout now
void sysui_panel_items(const pitems_t *set)
{
    s_items = set;
    s_ipage = 0;
    s_arm = -1;
}
bool sysui_panel_redraw(void)
{
    const bool r = s_redraw;
    s_redraw = false;
    return r;
}
static bool item_shown(int i)
{
    return !s_items->shown || s_items->shown(s_items->items[i].id);
}
static int item_val(const pitem_t *it)
{
    int v = it->key ? (int)store_get(it->key, it->def) : s_items->get ? s_items->get(it->id) : 0;
    return v < 0 ? 0 : v >= it->n ? it->n - 1 : v;
}
static const pdef_t *lay_raw(void);
// the layout for the frame and the items now (each frame: an item can show or go with the state of the app)
static const pdef_t *lay(void)
{
    lay_raw();
    if (sui_rs(BOARD_NATIVE_W) != BOARD_NATIVE_W) {
        sui_rs_def(&s_L);
        for (int i = 0; i < s_ni; i++) {
            sui_rs_box(&s_cb[i]);
        }
    }
    return &s_L;
}
static const pdef_t *lay_raw(void)
{
    const int k = sui_pl_kind();
    s_L = SUI_PDEF[k];
    s_ni = 0;
    int vis[17], m = 0;
    if (notify_count() > 0) {
        vis[m++] = CHIP_NOTE;  // (the board keeps notices: the first chip opens the center)
    }
    if (s_items) {
        for (int i = 0; i < s_items->n && m < 17; i++) {
            if (item_shown(i)) {
                vis[m++] = i;
            }
        }
    }
    if (!m) {
        return &s_L;
    }
    const pdefi_t *q = &SUI_PDEFI[k];
    s_L.H = q->H;
    s_L.tx0 = q->tx0;
    s_L.tx1 = q->tx1;
    s_L.ty0 = q->ty0;
    s_L.ty1 = q->ty1;
    memcpy(s_L.c, q->c, sizeof s_L.c);
    s_L.ft = q->ft;
    s_L.fl = q->fl;
    s_L.fb = q->fb;
    const int per = m <= q->nslot ? q->nslot : q->nslot - 1, pages = (m + per - 1) / per;
    if (s_ipage >= pages) {
        s_ipage = 0;
    }
    s_npages = pages;
    for (int j = 0; j < per && s_ipage * per + j < m; j++) {
        s_ci[s_ni] = (int8_t)vis[s_ipage * per + j];
        s_cb[s_ni] = q->slot[s_ni];
        s_ni++;
    }
    if (pages > 1) {
        s_ci[s_ni] = -1;
        s_cb[s_ni] = q->slot[q->nslot - 1];
        s_ni++;
    }
#if BOARD_UI_GRID
    // the native portrait panel: one row of chips (2 or fewer): RESUME and MENU come up a row (no empty band)
    if (k == PL_NAT_P && s_ni <= 2 && q->nslot >= 3) {
        const int up = q->slot[2].y - q->slot[0].y;
        s_L.c[PC_RESUME].y = (int16_t)(s_L.c[PC_RESUME].y - up);
        s_L.c[PC_MENU].y = (int16_t)(s_L.c[PC_MENU].y - up);
        s_L.H = (int16_t)(s_L.H - up);
    }
#endif
    return &s_L;
}
// the box of the control c: PC_* or PC_N + a chip
static const pbox_t *cbox(int c)
{
    return c < PC_N ? &s_L.c[c] : &s_cb[c - PC_N];
}

// the tab of the two-step pull (logical px; its top is the top of the grip): drawn TAB_W x TAB_H, a touch 12 px around it
#define TAB_W 72
#define TAB_H 26
// the zone in the touch frame (input.c) for the frame now; on = a pull may start
static void zone_update(bool on, int64_t now)
{
    input_zone_t z = {0};
    if (on) {
        const pdef_t *d = sui_pdef_now();
        const bool two = app_two_step(app_current()), tab = two && now < s_tab_until;
        int lx0 = d->zx0, lx1 = d->zx1, ly1 = d->zy1;
        if (tab) {
            const int cx = (d->zx0 + d->zx1) / 2;
            lx0 = cx - TAB_W / 2 - 12;
            lx1 = cx + TAB_W / 2 + 12;
            ly1 = d->grip_y + TAB_H + 12;
        }
        int ax, ay, bx, by;
        gfx_logical_to_touch(lx0, 0, &ax, &ay);
        gfx_logical_to_touch(lx1 - 1, ly1 - 1, &bx, &by);
        z.on = true;
        z.mode = tab ? ZONE_TAKE : two ? ZONE_WATCH : ZONE_HOLD;
        z.x0 = (int16_t)(ax < bx ? ax : bx);
        z.x1 = (int16_t)((ax < bx ? bx : ax) + 1);
        z.y0 = (int16_t)(ay < by ? ay : by);
        z.y1 = (int16_t)((ay < by ? by : ay) + 1);
        // "down" of the frame in the touch frame, and the thresholds (1.6 mm) in touch px
        int cx = (d->zx0 + d->zx1) / 2, p0x, p0y, p1x, p1y;
        gfx_logical_to_touch(cx, 0, &p0x, &p0y);
        gfx_logical_to_touch(cx, 100, &p1x, &p1y);
        int ddx = p1x - p0x, ddy = p1y - p0y;
        if (abs(ddx) >= abs(ddy)) {
            z.dx = (int8_t)(ddx > 0 ? 1 : -1);
        } else {
            z.dy = (int8_t)(ddy > 0 ? 1 : -1);
        }
        int span = abs(ddx) + abs(ddy);  // touch px for 100 logical px
        int pull = (int)(1.6f / sui_mm_px() * span / 100.0f + 0.5f);
        z.pull_px = (int16_t)(pull < 6 ? 6 : pull);
        z.side_px = z.pull_px;
    }
    if (memcmp(&z, &s_zone_now, sizeof z)) {
        s_zone_now = z;
        input_pull_zone(&z);
    }
}

// the values of the sliders (0..1) and their change
static float slider_get(int c)
{
    return c == PC_VOL ? g_settings.volume / (float)AUDIO_VOL_MAX : (g_settings.brightness - 10) / 245.0f;
}
static void slider_set(int c, float v)
{
    if (v < 0) {
        v = 0;
    }
    if (v > 1) {
        v = 1;
    }
    if (c == PC_VOL) {
        int n = (int)lroundf(v * AUDIO_VOL_MAX);  // (over 100: the boost of the mix, audio_set_volume)
        if (n > 97 && n < 103) {
            n = 100;  // (a small catch at the real 100 %)
        }
        if (n != g_settings.volume || g_settings.mute) {
            g_settings.volume = (uint8_t)n;
            if (g_settings.mute) {  // (a change of the volume ends MUTE: else nothing is heard)
                g_settings.mute = false;
                audio_set_mute(false);
            }
            audio_set_volume(n);
            s_pchanged = s_vol_moved = true;
        }
    } else {
        int n = 10 + (int)lroundf(v * 245);
        if (n != g_settings.brightness) {
            g_settings.brightness = (uint8_t)n;
            board_backlight((uint8_t)n);
            s_pchanged = true;
        }
    }
}
static void slider_end(void)
{
    if (s_vol_moved) {
        sfx_play(SFX_COIN);  // hear the new volume (the full level)
    }
    s_vol_moved = false;
}
static void slider_track(const pbox_t *b, int *x0, int *x1)
{
    int kr = b->h / 4 < 10 ? 10 : b->h / 4 > 18 ? 18 : b->h / 4;
    *x0 = b->x + kr + 2;
    *x1 = b->x + b->w - kr - 2;
}
static void slider_at(int c, int x)
{
    const pbox_t *b = &s_L.c[c];
    int x0, x1;
    slider_track(b, &x0, &x1);
    slider_set(c, (float)(x - x0) / (float)(x1 - x0));
}

static void panel_close(void)
{
    if (s_pst == PS_OFF) {
        return;
    }
    if (s_grab >= 0) {
        slider_end();
    }
    s_grab = s_press = -1;
    s_adjust = false;
    s_pst = PS_CLOSING;
}
static void panel_menu(void)
{
    sfx_ui(SFX_BACK);
    if (s_pmenu) {
#if ARCADE_APP_ON(SETTINGS)
        app_open(&app_settings);
#else
        panel_close();
#endif
    } else {
        app_open(&app_menu);
    }
}
// the panel went away (or an app opens): the save, the memory, the grip
static void panel_gone(int64_t now, bool save)
{
    if (save && s_pchanged) {
        settings_save();
    }
    s_pchanged = false;
    if (s_grab >= 0 && save) {
        slider_end();
    }
    s_grab = s_press = -1;
    s_pst = PS_OFF;
    s_pan = 0;
    s_touch = s_below = s_swipe = s_adjust = s_from_tab = false;
    s_focus = -1;
    heap_caps_free(s_psnap);
    s_psnap = NULL;
    s_grip_until = now + 4000;
    s_tab_until = 0;
    // (the wheel of the Watcher is a tilt in an app that does not read it: imu_wheel_shift() keeps that tilt still
    // through the panel, so the close needs no new level)
    s_wheel_used = false;
    s_hold_hint = 0;
    s_arm = -1;
    s_ipage = 0;
    (void)input_wheel_presses();  // (the pushes in the panel are not the app's)
    menu_redraw_all();
}
void sysui_panel_reset(int64_t now)
{
    panel_gone(now, true);
    s_open_req = s_hold_open = false;
    s_items = NULL;  // (the app that opens gives its own items in its enter)
    s_redraw = false;
}

int sysui_panel_tick(int64_t now, bool allow)
{
    int ev = PANEL_NONE;
    float dt = (float)(now - s_ptick) / 1000.0f;
    if (dt < 0 || dt > 0.05f) {
        dt = 0.05f;
    }
    s_ptick = now;
    allow = allow && !sysui_active();
    const bool take = s_zone_now.on && s_zone_now.mode == ZONE_TAKE;  // (the zone of the report: the tab)
    zone_update(allow && s_pst == PS_OFF, now);
    const pdef_t *d = lay();
    input_pull_t p;
    bool got = input_pull_get(&p);
    if (got && p.seq != s_pull_seq) {
        s_pull_seq = p.seq;
        if (p.watch) {
            if (s_pst == PS_OFF && allow) {
                s_tab_until = now + TAB_MS;  // the two-step pull: the tab shows
            }
        } else if (s_pst == PS_OFF && allow) {
            int lx, ly;
            gfx_touch_to_logical(p.x0, p.y0, &lx, &ly);
            s_drag_y0 = ly;
            s_pst = PS_DRAG;
            s_pan = 0;
            s_from_tab = take;
            s_tab_until = 0;
            ev = PANEL_START;
        }
    }
    if ((s_open_req || s_hold_open) && s_pst == PS_OFF && allow) {
        s_pst = PS_OPENING;
        s_pan = 0;
        ev = PANEL_START;
        if (s_hold_open) {
            s_focus = PC_MENU;
        }
    }
    s_open_req = s_hold_open = false;
    if (ev == PANEL_START) {
        s_phe = app_hebrew(app_current());
        s_pmenu = app_current() == &app_menu;
        s_grip_until = 0;
        s_pchanged = s_wheel_used = false;
        s_arm = -1;
        s_ipage = 0;
        if (s_items && s_items->opened) {
            s_items->opened();
        }
        d = lay();
#if BOARD_WHEEL
        (void)input_wheel();  // (the steps before the panel are the app's: no shift)
#endif
    }
    if (s_pst == PS_DRAG && got) {
        int lx, ly;
        gfx_touch_to_logical(p.x, p.y, &lx, &ly);
        float pan = (float)(ly - s_drag_y0 + 24) / d->H;
        s_pan = pan < 0 ? 0 : pan > 1 ? 1 : pan;
        if (!p.down) {
            // a release past 35 %, or a flick down (50 mm/s in the last 80 ms or more), opens it
            int lxp, lyp;
            gfx_touch_to_logical(p.xp, p.yp, &lxp, &lyp);
            float v = p.t_ms > p.tp_ms ? (float)(ly - lyp) * 1000.0f / (float)(p.t_ms - p.tp_ms) : 0;
            s_pst = (s_from_tab || s_pan >= 0.35f || v * sui_mm_px() >= 50.0f) ? PS_OPENING : PS_CLOSING;
        }
    }
    if (s_pst == PS_OPENING) {
        s_pan += dt / 0.18f;
        if (s_pan >= 1) {
            s_pan = 1;
            s_pst = PS_OPEN;
        }
    } else if (s_pst == PS_CLOSING) {
        s_pan -= dt / 0.16f;
        if (s_pan <= 0) {
            panel_gone(now, true);
            ev = PANEL_END;
            if (s_items && s_items->resume) {
                s_items->resume();  // (back to the app: a tilt game takes its zero)
            }
        }
    }
#if BOARD_WHEEL
    if (s_pst == PS_OPEN) {
        int w = input_wheel();
        imu_wheel_shift(w);  // (the virtual tilt of the app stays where it was)
        if (w) {
            s_wheel_used = true;
            if (s_focus < 0) {
                s_focus = PC_RESUME;
            } else if (s_adjust) {
                slider_set(s_focus, slider_get(s_focus) + 0.05f * (float)w);
            } else {
                // the ring goes in the order of the screen (top to bottom, then left to right), round: one turn back
                // from the first control is the last one (the chips too)
                const int nc = PC_N + s_ni;
                int ord[PC_N + SLOT_MAX], k = 0, at = 0;
                for (int c = 0; c < nc; c++) {
                    int p = k++;
                    while (p > 0 && (cbox(ord[p - 1])->y > cbox(c)->y ||
                                     (cbox(ord[p - 1])->y == cbox(c)->y && cbox(ord[p - 1])->x > cbox(c)->x))) {
                        ord[p] = ord[p - 1];
                        p--;
                    }
                    ord[p] = c;
                }
                for (int i = 0; i < nc; i++) {
                    if (ord[i] == s_focus) {
                        at = i;
                    }
                }
                s_focus = ord[((at + w) % nc + nc) % nc];
            }
        }
    } else if (s_pst != PS_OFF) {
        imu_wheel_shift(input_wheel());  // (the turns while it moves: not the app's)
    }
#endif
    return ev;
}

// the chip c (PC_N + k) acts: a tap at its lift (held: the time on the glass), or a push of the wheel (wheel). A PI_ACT2
// acts only after a still hold of 1 s (a fill shows it; a short tap only shows HOLD: a thumb that misses a neighbour,
// also twice, loses nothing), or after a second push of the wheel (no thumb error there).
static void chip_act(int c, int64_t held, bool wheel)
{
    const int i = s_ci[c - PC_N];
    if (i == CHIP_NOTE) {  // NOTICES: the panel goes, the center comes
        sfx_ui(SFX_CLICK);
        panel_close();
        notify_center_open();
        return;
    }
    if (i < 0) {  // MORE: the next page
        s_ipage++;
        s_arm = -1;
        s_focus = -1;
        sfx_ui(SFX_CLICK);
        return;
    }
    const pitem_t *it = &s_items->items[i];
    if (it->kind == PI_PICK) {
        const int v = (item_val(it) + 1) % (it->n ? it->n : 1);
        if (it->key) {
            store_set(it->key, (uint32_t)v);
        }
        if (s_items->set) {
            s_items->set(it->id, v);
        }
        s_arm = -1;
        s_redraw = true;
        s_phe = app_hebrew(app_current());  // (a LANGUAGE item: the panel speaks the new language at once)
        sfx_ui(SFX_CLICK);
        return;
    }
    if (it->kind == PI_ACT2 &&
        (wheel ? !(s_arm == i && s_arm_wheel && s_ptick - s_arm_t < ARM_MS) : held < ACT2_HOLD_MS)) {
        s_arm = i;  // nothing is lost: the chip says HOLD (or PUSH AGAIN)
        s_arm_wheel = wheel;
        s_arm_t = s_ptick;
        sfx_tone(500, 400, 60, W_SQUARE, 26);
        return;
    }
    s_arm = -1;
    sfx_ui(SFX_CLICK);
    if (s_items->set) {
        s_items->set(it->id, 0);
    }
    s_redraw = true;
    panel_close();
}
bool sysui_panel_key(int key)
{
    if (key == SK_HOLD && s_pst == PS_OFF) {
        s_hold_open = true;  // (it opens in the next frame, MENU in the ring)
        return true;
    }
    if (s_pst == PS_OFF) {
        return false;
    }
    if (s_pst != PS_OPEN) {
        return true;
    }
    if (key == SK_BOOT) {
        panel_close();
    } else if (key == SK_HOLD) {
        if (s_pmenu) {
            panel_close();
        } else {
            panel_menu();
        }
    } else if (key == SK_PUSH) {
        if (s_focus < 0 || s_focus == PC_RESUME) {
            sfx_ui(SFX_CLICK);
            panel_close();
        } else if (s_focus == PC_MENU) {
            panel_menu();
        } else if (s_focus >= PC_N) {
            if (s_focus < PC_N + s_ni) {
                chip_act(s_focus, 0, true);
            }
        } else {
            s_adjust = !s_adjust;  // a slider: a push starts the change, the next push ends it
            if (!s_adjust) {
                slider_end();
            }
            sfx_ui(SFX_CLICK);
        }
    }
    return true;
}

static bool in_pbox(const pbox_t *b, int x, int y, int m)
{
    return x >= b->x - m && x < b->x + b->w + m && y >= b->y - m && y < b->y + b->h + m;
}
bool sysui_panel_input(const input_event_t *ev)
{
    if (s_pst != PS_OPEN) {
        if (ev->kind == IN_UP) {
            s_touch = false;
        }
        return true;  // (it moves: no control yet)
    }
    const pdef_t *d = lay();
    const int x = ev->x, y = ev->y;
    switch (ev->kind) {
    case IN_DOWN:
        s_touch = true;
        s_touch_x = x;
        s_touch_y = y;
        s_grab = s_press = -1;
        s_below = s_swipe = false;
        for (int c = 0; c < PC_N + s_ni; c++) {
            if (!in_pbox(cbox(c), x, y, c < PC_RESUME ? 6 : 0)) {
                continue;
            }
            if (c < PC_RESUME) {
                s_grab = c;
                s_focus = -1;
                slider_at(c, x);
            } else {
                s_press = c;
                s_press_t = s_ptick;
                s_press_item = c >= PC_N ? s_ci[c - PC_N] : -2;
            }
            break;
        }
        if (s_grab < 0 && s_press < 0) {
            if (y >= d->H) {
                s_below = true;
            } else {
                s_swipe = true;
                s_swipe_y = y;
            }
        }
        if (s_arm >= 0 && !(s_press >= PC_N && s_ci[s_press - PC_N] == s_arm)) {
            s_arm = -1;  // (a touch elsewhere: not armed)
        }
        break;
    case IN_MOVE:
        if (s_grab >= 0) {
            slider_at(s_grab, x);
        } else if (s_swipe && s_swipe_y - y >= 24) {  // a swipe up on the panel closes it
            s_swipe = false;
            panel_close();
        } else if ((s_press == PC_MENU || s_press >= PC_N) &&
                   (abs(x - s_touch_x) > (int)(1.8f / sui_mm_px()) || abs(y - s_touch_y) > (int)(1.8f / sui_mm_px()))) {
            s_press = -1;  // a finger that moves is no hold of MENU (nor a tap of a chip)
        }
        break;
    case IN_UP:
        if (!s_touch) {
            break;
        }
        s_touch = false;
        if (s_grab >= 0) {
            slider_end();
            s_grab = -1;
        } else if (s_press >= 0) {
            // a button acts at the lift, when the finger is still on it (the point of the lift, 8 px around the box)
            int c = s_press;
            s_press = -1;
            if (c >= PC_N) {
                if (c < PC_N + s_ni && s_ci[c - PC_N] == s_press_item && in_pbox(cbox(c), x, y, 8)) {
                    chip_act(c, s_ptick - s_press_t, false);
                }
            } else if (in_pbox(&d->c[c], x, y, 8)) {
                if (c == PC_RESUME) {
                    sfx_ui(SFX_CLICK);
                    panel_close();
                } else if (s_ptick - s_press_t >= MENU_HOLD_MS) {
                    panel_menu();  // only after a still hold (a fill shows it)
                } else {
                    s_hold_hint = s_ptick + 1500;  // a short tap: MENU says HOLD for a moment
                }
            }
        } else if (s_below && y >= d->H && abs(x - s_touch_x) < 40 && abs(y - s_touch_y) < 40) {
            sfx_ui(SFX_CLICK);
            panel_close();  // a tap on the game under the panel
        }
        s_below = s_swipe = false;
        break;
    default:
        break;
    }
    return true;
}

void sysui_panel_snap(void)
{
#if BOARD_HI_FRAME
    gfx_hi_flatten();  // (a HI picture: first the normal layout)
#endif
    heap_caps_free(s_psnap);
    size_t n = (size_t)gfx_w * gfx_h;
    s_psnap = heap_caps_malloc(n * 2, MALLOC_CAP_SPIRAM);
    s_psnap_w = gfx_w;
    s_psnap_h = gfx_h;
    if (!s_psnap) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        uint16_t v = sw16(g_fb[i]);
        v = (uint16_t)((v >> 1) & 0x7BEF);  // half of the light
        s_psnap[i] = sw16(v);
    }
}

// ---- the drawing
static void text_right(const kfont_t *f, int x1, int base, const char *s, uint16_t c)
{
    ui_text(f, x1 - ui_text_w(f, s), base, s, c);
}
static void draw_slider(const pdef_t *d, int c, int oy)
{
    pbox_t b = d->c[c];
    b.y = (int16_t)(b.y + oy);
    const bool vol = c == PC_VOL, muted = vol && g_settings.mute;
    const float v = slider_get(c);
    char val[16];
    if (muted) {
        snprintf(val, sizeof val, "%s", PTXT[s_phe][PT_MUTE]);
    } else {
        snprintf(val, sizeof val, "%d%%", vol ? (int)g_settings.volume : (int)lroundf(v * 100));
    }
    const char *lab = PTXT[s_phe][vol ? PT_VOL : PT_BRI];
    // the font of the two rows: the small one when a name and "100%" do not fit (a narrow row)
    const kfont_t *f = KFS[d->fl];
    int vw = ui_text_w(f, "100%"), mw = ui_text_w(f, PTXT[s_phe][PT_MUTE]);
    for (int k = PT_VOL; k <= PT_BRI; k++) {
        if (ui_text_w(KFS[d->fl], PTXT[s_phe][k]) + (vw > mw ? vw : mw) + 24 > b.w) {
            f = &KF_LABEL;
        }
    }
    const int base = b.y + f->asc + 3;
    const uint16_t accent = vol ? THEME_PANEL_VOL : THEME_PANEL_BRI;
    const uint16_t boost = THEME_PANEL_BOOST;  // (the VOLUME over 100: the boost of the mix)
    const bool boosted = vol && !muted && g_settings.volume > 100;
    if (s_phe) {  // Hebrew: the name at the right, the value at the left
        text_right(f, b.x + b.w - 6, base, lab, THEME_PANEL_TEXT);
        ui_text(f, b.x + 6, base, val, muted ? THEME_PANEL_TEXT_DIM : boosted ? boost : accent);
    } else {
        ui_text(f, b.x + 6, base, lab, THEME_PANEL_TEXT);
        text_right(f, b.x + b.w - 6, base, val, muted ? THEME_PANEL_TEXT_DIM : boosted ? boost : accent);
    }
    int x0, x1;
    slider_track(&b, &x0, &x1);
    const int kr = b.h / 4 < 10 ? 10 : b.h / 4 > 18 ? 18 : b.h / 4;
#if BOARD_UI_GRID
    const int cy = b.y + b.h - kr - 3, th = 14;  // (the soft look: a thick round track)
#else
    const int cy = b.y + b.h - kr - 3, th = kr < 14 ? 8 : 10;
#endif
    const int kx = x0 + (int)lroundf(v * (float)(x1 - x0));
    ui_rrect(x0 - th / 2, cy - th / 2, x1 - x0 + th, th, th / 2, THEME_PANEL_TRACK);
    if (vol) {
        // the real 100 %: a mark on the track; the part over it (the boost) in orange
        const int x100 = x0 + (x1 - x0) * 100 / AUDIO_VOL_MAX;
        if (boosted) {
            ui_rrect(x0 - th / 2, cy - th / 2, kx - x0 + th, th, th / 2, boost);
        }
        ui_rrect(x0 - th / 2, cy - th / 2, (kx < x100 ? kx : x100) - x0 + th, th, th / 2,
                 muted ? THEME_PANEL_TEXT_DIM : accent);
        ui_rrect(x100 - 1, cy - th / 2 - 4, 3, th + 8, 1, THEME_PANEL_TEXT);
    } else {
        ui_rrect(x0 - th / 2, cy - th / 2, kx - x0 + th, th, th / 2, muted ? THEME_PANEL_TEXT_DIM : accent);
    }
    const uint16_t knob = boosted ? boost : accent;
    const bool grab = s_grab == c || (s_adjust && s_focus == c);
#if BOARD_UI_GRID
    // the soft look: a white knob with a soft halo of the colour of the slider (bigger while a finger moves it)
    (void)knob;
    ui_circle(kx, cy, kr + (grab ? 8 : 4), muted ? THEME_PANEL_TEXT_DIM : accent, grab ? 110 : 60);
    ui_circle(kx, cy, kr, THEME_PANEL_KNOB, 255);
#else
    if (grab) {
        ui_circle(kx, cy, kr + 5, knob, 90);
    }
    ui_circle(kx, cy, kr, THEME_PANEL_KNOB, 255);
    ui_circle(kx, cy, kr - 4, muted ? THEME_PANEL_TEXT_DIM : knob, 255);
#endif
}
static void draw_button(const pdef_t *d, int c, int oy)
{
    pbox_t b = d->c[c];
    b.y = (int16_t)(b.y + oy);
    const bool res = c == PC_RESUME, down = s_press == c;
    uint16_t fill = res ? THEME_PANEL_RESUME : THEME_PANEL_BTN,
             edge = res ? THEME_PANEL_RESUME_EDGE : THEME_PANEL_BTN_EDGE;
    if (down) {
        fill = res ? THEME_PANEL_RESUME_DOWN : THEME_PANEL_BTN_DOWN;
    }
#if BOARD_UI_GRID
    // the soft look: a round pill with no edge line
    (void)edge;
    if (!down) {
        fill = res ? THEME_PANEL_GR_RESUME : THEME_PANEL_GR_BTN;
    }
    ui_rrect(b.x, b.y, b.w, b.h, b.h / 2, fill);
    if (down && !res) {  // the fill of the hold of MENU
        int k = (int)((s_ptick - s_press_t) * (b.w - 8) / MENU_HOLD_MS);
        ui_rrect(b.x + 4, b.y + 4, k < b.w - 8 ? k : b.w - 8, b.h - 8, (b.h - 8) / 2, THEME_PANEL_HOLD);
    }
#else
    ui_rbox(b.x, b.y, b.w, b.h, 14, fill, edge, 2);
    if (down && !res) {  // the fill of the hold of MENU
        int k = (int)((s_ptick - s_press_t) * (b.w - 8) / MENU_HOLD_MS);
        ui_rrect(b.x + 4, b.y + 4, k < b.w - 8 ? k : b.w - 8, b.h - 8, 10, THEME_PANEL_HOLD);
    }
#endif
    const char *s = PTXT[s_phe][res                                ? PT_RESUME
                                : (!down && s_ptick < s_hold_hint) ? PT_HOLD
                                : s_pmenu                          ? PT_SETUP
                                                                   : PT_MENU];
    const kfont_t *const F[] = {KFS[d->fb], &KF_BIG, &KF_TEXT, &KF_NAME};
    const kfont_t *f = ui_text_fit(F, 4, s, b.w - 16);
    if (f->asc > b.h - 12) {
        f = &KF_TEXT;
    }
    ui_text_c(f, b.x + b.w / 2, b.y + (b.h + f->asc) / 2 - 1, s, res ? THEME_PANEL_RESUME_TEXT : THEME_PANEL_TEXT);
}
// a font of the list (big to small) whose width of s is w or less and whose height is asc or less (the last one if none)
static const kfont_t *fit_wh(const kfont_t *const *f, int n, const char *s, int w, int asc)
{
    for (int i = 0; i < n; i++) {
        if (f[i]->asc <= asc && ui_text_w(f[i], s) <= w) {
            return f[i];
        }
    }
    return f[n - 1];
}
// a chip: PI_PICK = the name (small) over the value; PI_ACT = the name; PI_ACT2 = the name, red after a short tap (HOLD
// over it; the wheel: PUSH AGAIN) and a fill while a finger holds it; MORE = the page
static void draw_chip(const pdef_t *d, int c, int oy)
{
    pbox_t b = s_cb[c - PC_N];
    b.y = (int16_t)(b.y + oy);
    const int i = s_ci[c - PC_N];
    const pitem_t *it = i >= 0 ? &s_items->items[i] : NULL;
    const bool down = s_press == c, loss = it && it->kind == PI_ACT2;
    const bool armed = loss && s_arm == i && s_ptick - s_arm_t < ARM_MS;
    uint16_t fill = armed ? THEME_PANEL_CHIP_ARMED : down ? THEME_PANEL_CHIP_DOWN : THEME_PANEL_CHIP;
    const uint16_t edge = armed                                   ? THEME_PANEL_CHIP_ARMED_EDGE
                          : loss                                  ? THEME_PANEL_CHIP_LOSS_EDGE
                          : i == CHIP_NOTE && notify_unread(NULL) ? THEME_PANEL_CHIP_UNREAD_EDGE
                                                                  : THEME_PANEL_CHIP_EDGE;
#if BOARD_UI_GRID
    // the soft look: a round box; an edge line only when it tells something (unread notices, a hold acts, armed)
    if (!armed && !down) {
        fill = THEME_PANEL_GR_BTN;
    }
    if (edge == THEME_PANEL_CHIP_EDGE) {
        ui_rrect(b.x, b.y, b.w, b.h, 18, fill);
    } else {
        ui_rbox(b.x, b.y, b.w, b.h, 18, fill, edge, 2);
    }
#else
    ui_rbox(b.x, b.y, b.w, b.h, 12, fill, edge, 2);
#endif
    if (down && loss) {  // the fill of a hold (1 s acts)
        int k = (int)((s_ptick - s_press_t) * (b.w - 8) / ACT2_HOLD_MS);
        ui_rrect(b.x + 4, b.y + 4, k < b.w - 8 ? k : b.w - 8, b.h - 8, 9, THEME_PANEL_CHIP_HOLD);
    }
    const kfont_t *const F[] = {KFS[d->fb], &KF_BIG, &KF_TEXT, &KF_NAME, &KF_LABEL};
    const int cx = b.x + b.w / 2, w = b.w - 12;
    const char *top = NULL, *main = NULL;
    char page[16];
    if (i == CHIP_NOTE) {
        snprintf(page, sizeof page, "%d", notify_count());
        top = s_phe ? "התראות" : "NOTICES";
        main = page;
    } else if (!it) {
        snprintf(page, sizeof page, "%d/%d", s_ipage + 1, s_npages);
        top = page;
        main = PTXT[s_phe][PT_MORE];
    } else if (it->kind == PI_PICK) {
        top = it->name[s_phe];
        main = it->vals ? it->vals[2 * item_val(it) + s_phe] : "";
    } else {
        top = armed ? PTXT[s_phe][s_arm_wheel ? PT_AGAIN : PT_HOLD] : NULL;
        main = it->name[s_phe];
    }
    if (top) {
        const kfont_t *ft = fit_wh(F + 3, 2, top, w, b.h / 3);
        const kfont_t *fm = fit_wh(F, 5, main, w, b.h - ft->asc - 14);
        const int gap = (b.h - ft->asc - fm->asc) / 3;
        ui_text_c(ft, cx, b.y + gap + ft->asc, top, armed ? THEME_PANEL_CHIP_ARMED_TOP : THEME_PANEL_TEXT_DIM);
        ui_text_c(fm, cx, b.y + b.h - gap, main, armed ? THEME_PANEL_CHIP_ARMED_TEXT : THEME_PANEL_TEXT);
    } else {
        const kfont_t *fm = fit_wh(F, 5, main, w, b.h - 14);
        ui_text_c(fm, cx, b.y + (b.h + fm->asc) / 2 - 1, main, loss ? THEME_PANEL_CHIP_LOSS_TEXT : THEME_PANEL_TEXT);
    }
}
static void draw_ring(const pdef_t *d, int c, int oy)
{
    const pbox_t *b = cbox(c);
    (void)d;
    // (under the control: a round band 3 px wide around it)
    ui_rrect(b->x - 6, b->y + oy - 6, b->w + 12, b->h + 12, 18,
             s_adjust ? THEME_PANEL_FOCUS_ADJUST : THEME_PANEL_FOCUS);
    ui_rrect(b->x - 3, b->y + oy - 3, b->w + 6, b->h + 6, 15, THEME_PANEL_BG);
}
void sysui_panel_draw(bool live)
{
    if (s_pst == PS_OFF) {
        return;
    }
    const pdef_t *d = lay();
    const int H = d->H, shown = (int)lroundf(s_pan * H), oy = shown - H;
    // under the panel: the frozen picture (half the light), or the live frame of an app that runs (half the light)
    if (live) {
        if (shown < gfx_h) {
            gfx_darken_rect(0, shown < 0 ? 0 : shown, gfx_w, gfx_h - (shown < 0 ? 0 : shown), 1);
        }
    } else if (s_psnap && s_psnap_w == gfx_w && s_psnap_h == gfx_h) {
        memcpy(g_fb, s_psnap, (size_t)gfx_w * gfx_h * 2);
    } else {
        gfx_fill(THEME_PANEL_BACK);
    }
    if (shown <= 0) {
        return;
    }
#if BOARD_UI_GRID
    // the soft look: a near-black card with big round corners, a soft shade under it, no edge line
    for (int j = 0; j < 16 && shown + j < gfx_h; j++) {
        ui_rrect_a(0, shown - 14 + j, gfx_w, 1, 0, THEME_PANEL_GR_SHADE, 150 - j * 9);
    }
    ui_rrect(0, oy - 32, gfx_w, H + 32, 28, THEME_PANEL_GR_BG);
    ui_rrect(gfx_w / 2 - 22, shown - 12, 44, 6, 3, THEME_PANEL_TEXT_DIM);  // the handle
#else
    ui_rrect(0, oy - 24, gfx_w, H + 24, 18, THEME_PANEL_BG);
    gfx_fill_rect(18, shown - 2, gfx_w - 36, 2, THEME_PANEL_EDGE);
    ui_rrect(gfx_w / 2 - 20, shown - (gfx_round() ? 44 : 9), 40, 5, 2,
             THEME_PANEL_TEXT_DIM);  // the handle (round: where the circle is wide)
#endif
    // the app name
    const app_t *a = app_current();
    if (a && a->name && d->tx1 > d->tx0) {  // (the round layouts: no name)
        const kfont_t *const F[] = {KFS[d->ft], &KF_TEXT, &KF_NAME, &KF_LABEL};
        const kfont_t *f = ui_text_fit(F, 4, a->name, d->tx1 - d->tx0);
#if BOARD_UI_GRID
        ui_text_c(f, (d->tx0 + d->tx1) / 2, oy + (d->ty0 + d->ty1 + f->asc) / 2, a->name, THEME_PANEL_TEXT);
#else
        ui_text_c(f, (d->tx0 + d->tx1) / 2, oy + (d->ty0 + d->ty1 + f->asc) / 2, a->name, THEME_PANEL_APP_NAME);
#endif
    }
    if (s_focus >= 0 && s_pst == PS_OPEN) {
        draw_ring(d, s_focus, oy);
    }
    draw_slider(d, PC_VOL, oy);
    draw_slider(d, PC_BRI, oy);
    draw_button(d, PC_RESUME, oy);
    draw_button(d, PC_MENU, oy);
    for (int c = PC_N; c < PC_N + s_ni; c++) {
        draw_chip(d, c, oy);
    }
}

static bool s_grip_was;
void sysui_grip_draw(int64_t now)
{
    // (the menu draws only what changes: while the grip shows, and once after it, the menu draws all)
    const bool menu = app_current() == &app_menu;
    const pdef_t *d = sui_pdef_now();
    if (s_pst == PS_OFF && s_zone_now.on && now < s_tab_until) {
        // the tab of the two-step pull: a dark box with a handle and an arrow down (a touch on it opens the panel)
        int left = (int)(s_tab_until - now), a = left < 400 ? left * 255 / 400 : 255;
        int cx = (d->zx0 + d->zx1) / 2, x = cx - TAB_W / 2, y = d->grip_y;
        ui_rrect_a(x - 2, y - 2, TAB_W + 4, TAB_H + 4, 10, THEME_PANEL_TAB_EDGE, a);
        ui_rrect_a(x, y, TAB_W, TAB_H, 9, THEME_PANEL_BG, a);
        ui_rrect_a(cx - 14, y + 5, 28, 4, 2, THEME_PANEL_TAB_HANDLE, a);
        for (int k = 0; k < 7; k++) {  // the arrow: two short slopes
            ui_rrect_a(cx - 8 + k, y + 13 + k, 2, 2, 1, THEME_PANEL_TAB_ARROW, a);
            ui_rrect_a(cx + 6 - k, y + 13 + k, 2, 2, 1, THEME_PANEL_TAB_ARROW, a);
        }
        if (menu) {
            menu_redraw_all();
        }
        s_grip_was = true;
        return;
    }
    if (s_pst != PS_OFF || !s_zone_now.on || now >= s_grip_until) {
        if (s_grip_was && menu) {
            menu_redraw_all();
        }
        s_grip_was = false;
        return;
    }
    int left = (int)(s_grip_until - now), a = left < 600 ? left * 255 / 600 : 255;
    int cx = (d->zx0 + d->zx1) / 2, y = d->grip_y;
    ui_rrect_a(cx - 26, y - 2, 52, 11, 5, THEME_PANEL_GRIP_EDGE, a * 2 / 3);  // (a dark edge: it shows on a light game)
    ui_rrect_a(cx - 22, y, 44, 7, 3, THEME_PANEL_GRIP, a * 9 / 10);
    if (menu) {
        menu_redraw_all();
    }
    s_grip_was = true;
}
