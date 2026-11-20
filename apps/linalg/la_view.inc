// LINEAR ALGEBRA: the screen of a chapter, the app hooks (enter, input, update, render), the panel and the icon.

// ---------------------------------------------------------------- the screen of a chapter
// the chip of a number: its name over its value
static void chip_draw(int i)
{
    const topic_t *tp = s_tp_now;
    rect_t r = Y.chip[i];
    bool sel = i == s_sel;
    uint16_t pc = part_col(tp->part);
    tfd_noclip();
    tfd_rrect(r.x, r.y, r.w, r.h, 8, sel ? ui_mix(rgb(30, 34, 56), pc, 60) : rgb(26, 29, 48), 256);
    if (sel) {
        tfd_rrect(r.x + 3, r.y + r.h - 3, r.w - 6, 2, 1, pc, 230);
    }
    char v[64];
    val_txt(tp, i, v, sizeof v);
    bool named = tp->vn[i] != NULL;
    int keep = s_mbase;
    // the name (small) and the value (big): side by side when the chip is low, else one over the other
    s_mbase = s_tsz - 1 < 0 ? 0 : s_tsz - 1;
    bx_t bn = m_measure(tp->p[i].sym);
    s_mbase = s_tsz + 1 > 5 ? 5 : s_tsz + 1;
    bx_t bv = m_measure(v);
    while (bv.w / 4 > r.w - 8 && s_mbase > 0) {
        s_mbase--;
        bv = m_measure(v);
    }
    const mfont_t *nf = SZ[s_tsz];  // a named value: a word (English or Hebrew), not mathematics
    if (named) {
        bv.w = say_w(nf, v) * 4;
        bv.a = nf->asc;
        bv.d = 2;
    }
    ty_clip(r.x, r.y, r.x + r.w, r.y + r.h);
    uint16_t vc = i == s_hot && s_now - s_hot_t < 2500 ? COL_ACC : COL_TEXT;
    int need = bn.a + bn.d + bv.a + bv.d + 4;
    if (need <= r.h - 2) {
        int top = r.y + (r.h - need) / 2;
        int vb = s_mbase;
        s_mbase = s_tsz - 1 < 0 ? 0 : s_tsz - 1;
        const char *p = tp->p[i].sym;
        m_list(&p, 0, (r.x + r.w / 2) * 4 - bn.w / 2, top + bn.a, true, sel ? pc : COL_DIM);
        s_mbase = vb;
        if (named) {
            say_c(nf, r.x + r.w / 2, top + bn.a + bn.d + 4 + bv.a, v, vc);
        } else {
            p = v;
            m_list(&p, 0, (r.x + r.w / 2) * 4 - bv.w / 2, top + bn.a + bn.d + 4 + bv.a, true, vc);
        }
    } else {
        // side by side: "a = 2" (a named value: only the name)
        if (named) {
            say_c(nf, r.x + r.w / 2, r.y + r.h / 2 + nf->asc / 2, v, vc);
        } else {
            char line[96];
            snprintf(line, sizeof line, "%s = %s", tp->p[i].sym, v);
            s_mbase = s_tsz;
            bx_t b = m_measure(line);
            const char *p = line;
            m_list(&p, 0, (r.x + r.w / 2) * 4 - b.w / 2, r.y + r.h / 2 + (b.a - b.d) / 2, true, vc);
        }
    }
    ty_noclip();
    s_mbase = keep;
}
static void key_draw(rect_t r, bool plus, bool can)
{
    bool down = s_drag == 3 && s_dkey == (plus ? 1 : -1);
    key_box(r, down, COL_ACC);
    float cx = r.x + r.w / 2.0f, cy = r.y + r.h / 2.0f, L = (r.h < r.w ? r.h : r.w) * 0.22f;
    uint16_t c = can ? COL_TEXT : rgb(70, 76, 100);
    tfd_line(cx - L, cy, cx + L, cy, 3.0f, c, 256);
    if (plus) {
        tfd_line(cx, cy - L, cx, cy + L, 3.0f, c, 256);
    }
}
// the dots of the steps (from x, to the right; or to the left in Hebrew)
static void dots_draw(int x0, int x1, int y, int n, int k, uint16_t pc)
{
    float gap = 9.0f;
    if (n * gap > (x1 - x0)) {
        gap = (float)(x1 - x0) / n;
    }
    for (int i = 0; i < n; i++) {
        float x = s_he ? x1 - 4 - i * gap : x0 + 4 + i * gap;
        if (i == k) {
            tfd_disc(x, (float)y, 3.2f, pc, 256);
        } else {
            tfd_disc(x, (float)y, 2.0f, i < k ? ui_mix(COL_DIM, pc, 120) : rgb(70, 76, 100), 256);
        }
    }
}
// THE PAGES of a step: the text of a step that is too tall for its box goes on 2 or more pages (a tap shows the next
// page, then the next step). text_layout() chooses the faces (the normal ones; one size smaller when that makes one
// page) and the first source line of each page.
static int pages_count(void)
{
    int n = 0, from = 0;
    s_lay_bad = false;
    while (from >= 0 && n < PG_MAX) {
        s_pg_at[n++] = from;
        int next = -1;
        s_overflow = false;
        la_flow(s_tx, Y.body, false, 0, from, &next);
        s_lay_bad |= s_overflow;
        from = next;
    }
    if (from >= 0) {
        s_lay_bad = true;  // (more than PG_MAX pages)
    }
    return n;
}
static void text_layout(void)
{
    s_tn = 0;
    s_tx[0] = 0;
    s_tp_now->text(s_step);
    int keep_t = s_tsz, keep_m = s_msz;
    s_npg = pages_count();
    bool bad = s_lay_bad;
    s_lay_t = s_tsz;
    s_lay_m = s_msz;
    if ((s_npg > 1 || bad) && s_tsz > 1) {
        s_tsz--;
        s_msz--;
        int n2 = pages_count();
        if ((n2 == 1 && !s_lay_bad) || (bad && !s_lay_bad)) {
            s_npg = n2;
            s_lay_t = s_tsz;
            s_lay_m = s_msz;
            bad = s_lay_bad;
        } else {
            s_tsz = keep_t;
            s_msz = keep_m;
            s_npg = pages_count();
        }
    }
    s_lay_bad = bad;
    s_tsz = keep_t;
    s_msz = keep_m;
    if (s_pg_last) {
        s_pg = s_npg - 1;
        s_pg_last = false;
    }
    if (s_pg >= s_npg) {
        s_pg = s_npg - 1;
    }
    if (s_pg < 0) {
        s_pg = 0;
    }
}
// for the PC test: the text of the step now has no line wider or taller than its box
static bool text_fits(void)
{
    text_layout();
    return !s_lay_bad;
}
static void text_draw(int64_t now)
{
    text_layout();
    // the slide of a new step or page: 220 ms, from the side of the reading
    float a = clamp01((now - s_step_t) / 220.0f);
    float e = ease_io(a);
    int dx = (int)((1 - e) * 18 * s_dir * (s_he ? -1 : 1));
    s_talpha = (int)(e * 255);
    s_tcol = COL_TEXT;
    rect_t b = Y.body;
    ty_clip(Y.txt.x + 2, b.y - 2, Y.txt.x + Y.txt.w - 2, b.y + b.h + 2);
    int keep_t = s_tsz, keep_m = s_msz;
    s_tsz = s_lay_t;
    s_msz = s_lay_m;
    s_overflow = false;
    la_flow(s_tx, b, true, dx, s_pg_at[s_pg], NULL);
    s_overflow = s_lay_bad;
    s_tsz = keep_t;
    s_msz = keep_m;
    s_talpha = 255;
    ty_noclip();
    // more pages: a small mark at the bottom (the page now of all)
    if (s_npg > 1) {
        float y = (float)(Y.txt.y + Y.txt.h - 6), x = (float)(Y.txt.x + Y.txt.w / 2 - (s_npg - 1) * 6);
        if (Y.round) {
            y = (float)(Y.body.y + Y.body.h - 2);
        }
        for (int i = 0; i < s_npg; i++) {
            tfd_rrect((int)(x + i * 12 - 4), (int)y - 1, 8, 3, 1, i == s_pg ? COL_ACC : rgb(70, 76, 100), 256);
        }
    }
}
// the next page, or the next step (d = 1); the page before, or the step before (d = -1)
static void page_go(int d)
{
    if (d > 0 && s_pg + 1 < s_npg) {
        s_pg++;
        s_dir = 1;
        s_step_t = s_now;
        fx(FX_PAGE, 140);
        return;
    }
    if (d < 0 && s_pg > 0) {
        s_pg--;
        s_dir = -1;
        s_step_t = s_now;
        fx(FX_PAGEB, 140);
        return;
    }
    int keep = s_step, kt = s_ti;
    step_go(d);
    if (s_step != keep || s_ti != kt) {
        s_pg = 0;
        s_pg_last = d < 0 && s_ti == kt;
    }
}
// THE LAYER: all of the chapter screen but the picture (the background, the text, the chips, the keys) is drawn
// into the frame only when it changes, and kept in a PSRAM copy (s_layer); in the other frames that copy comes back
// into the frame (all but the inside of the picture) and only the live picture is drawn again.
static uint16_t *s_layer;
static uint32_t s_layer_key;
static uint32_t layer_key(int64_t now)
{
    uint32_t h = 2166136261u;
#define MIXK(v) (h = (h ^ (uint32_t)(v)) * 16777619u)
    MIXK(s_ti + 1);
    MIXK(s_step);
    MIXK(s_pg);
    MIXK(s_sel);
    MIXK(s_he);
    MIXK(s_big);
    MIXK(gfx_w);
    MIXK(gfx_h);
    for (int i = 0; i < 4; i++) {
        MIXK(P[i] + 100);
    }
    MIXK(s_hot);
    MIXK(now - s_hot_t < 2500);
    MIXK(s_drag == 3 ? s_dkey + 7 : 0);
#undef MIXK
    return h | 1;
}
// the frame from the layer: each row, all but the inside of the picture (its round corners come from the layer)
static void layer_put(void)
{
    int x0 = Y.pic.x + 14, x1 = Y.pic.x + Y.pic.w - 14, y0 = Y.pic.y, y1 = Y.pic.y + Y.pic.h;
    for (int y = 0; y < gfx_h; y++) {
        uint16_t *d = &g_fb[y * gfx_w], *s = &s_layer[y * gfx_w];
        if (y < y0 || y >= y1 || x1 <= x0) {
            memcpy(d, s, (size_t)gfx_w * 2);
            continue;
        }
        memcpy(d, s, (size_t)x0 * 2);
        memcpy(d + x1, s + x1, (size_t)(gfx_w - x1) * 2);
    }
}
static void topic_static(int64_t now);
static void topic_draw(int64_t now)
{
    const topic_t *tp = s_tp_now;
    bool anim = now - s_step_t < 260 || now - s_scr_t < 220;
    uint32_t key = layer_key(now);
    if (s_layer && !anim && key == s_layer_key) {
        layer_put();
    } else {
        topic_static(now);
        if (s_layer && !anim) {
            memcpy(s_layer, g_fb, (size_t)gfx_w * gfx_h * 2);
            s_layer_key = key;
        } else {
            s_layer_key = 0;
        }
    }
    // the picture: its round box is in the layer; the inside is a plain fill each frame
    gfx_fill_rect(Y.pic.x + 14, Y.pic.y, Y.pic.w - 28, Y.pic.h, PIC_BG);
    gfx_fill_rect(Y.pic.x + 1, Y.pic.y + 12, Y.pic.w - 2, Y.pic.h - 24, PIC_BG);
    rect_t pi = R(Y.pic.x + 2, Y.pic.y + 2, Y.pic.w - 4, Y.pic.h - 4);
    tfd_clip(pi.x, pi.y, pi.x + pi.w, pi.y + pi.h);
    tp->pic(pi, s_step, s_t);
    tfd_noclip();
    ty_noclip();
    // the fade at the open
    float f = clamp01((now - s_scr_t) / 180.0f);
    if (f < 1) {
        gfx_darken_rect(0, 0, gfx_w, gfx_h, f < 0.33f ? 2 : 1);
    }
}
static void topic_static(int64_t now)
{
    const topic_t *tp = s_tp_now;
    uint16_t pc = part_col(tp->part);
    bg_draw();
    pic_bg(Y.pic);
    tfd_noclip();
    // the text panel
    rect_t t = Y.txt;
    if (Y.round) {
        tfd_rrect(t.x, t.y, t.w, t.h, 24, rgb(20, 23, 40), 256);
    } else {
        tfd_rrect(t.x, t.y, t.w, t.h, 10, rgb(20, 23, 40), 256);
    }
    // the head: the number and the name of the chapter, the dots of the steps
    if (!Y.round) {
        rect_t hd = Y.head;
        char line[96];
        snprintf(line, sizeof line, "%d  %s", s_ti + 1, tp->name[s_he]);
        const mfont_t *hf = bface(0);
        int dots_w = tp->ns * 9 + 8;
        int w = say_w(hf, line);
        if (w > hd.w) {
            // a long name: a smaller face, then the end cut with "…"
            hf = SZ[s_tsz];
            w = say_w(hf, line);
            while (w > hd.w && strlen(line) > 4) {
                int L = (int)strlen(line) - 1;
                while (L > 0 && ((uint8_t)line[L] & 0xC0) == 0x80) {
                    L--;
                }
                strcpy(line + L, "");
                char t[100];
                snprintf(t, sizeof t, "%s…", line);
                w = say_w(hf, t);
                if (w <= hd.w) {
                    strcpy(line, t);
                    break;
                }
            }
        }
        bool two = w + dots_w > hd.w;  // the dots under the name when there is no room
        int base = hd.y + hf->asc + (two ? 2 : (hd.h - hf->asc) / 2);
        ty_clip(hd.x, 0, hd.x + hd.w, gfx_h);
        if (s_he) {
            say_c(hf, hd.x + hd.w - w / 2, base, line, pc);
            if (two) {
                dots_draw(hd.x, hd.x + hd.w, base + 8, tp->ns, s_step, pc);
            } else {
                dots_draw(hd.x, hd.x + hd.w - w - 8, hd.y + hd.h / 2, tp->ns, s_step, pc);
            }
        } else {
            say_c(hf, hd.x + w / 2, base, line, pc);
            if (two) {
                dots_draw(hd.x, hd.x + hd.w, base + 8, tp->ns, s_step, pc);
            } else {
                dots_draw(hd.x + w + 8, hd.x + hd.w, hd.y + hd.h / 2, tp->ns, s_step, pc);
            }
        }
        ty_noclip();
        key_box(Y.chap, false, pc);
        icon_chapters(Y.chap, COL_DIM);
    } else {
        dots_draw(gfx_w / 2 - tp->ns * 9 / 2 - 4, gfx_w / 2 + tp->ns * 9 / 2 + 4, Y.txt.y + Y.txt.h + 2, tp->ns, s_step,
                  pc);
        key_box(Y.chap, false, pc);
        icon_chapters(Y.chap, COL_DIM);
    }
    text_draw(now);
    // the chips and the keys
    for (int i = 0; i < tp->np; i++) {
        chip_draw(i);
    }
    if (tp->np) {
        const par_t *p = &tp->p[s_sel];
        key_draw(Y.minus, false, P[s_sel] > p->lo);
        key_draw(Y.plus, true, P[s_sel] < p->hi);
    }
}
// a tap in the text: the start side (left in English, right in Hebrew; 28 % of the box) = the step before
static void text_tap(int x)
{
    rect_t t = Y.txt;
    int edge = t.w * 28 / 100;
    bool back = s_he ? x > t.x + t.w - edge : x < t.x + edge;
    page_go(back ? -1 : 1);
}
static void topic_input(const input_event_t *ev)
{
    const topic_t *tp = s_tp_now;
    int x = ev->x, y = ev->y;
    switch (ev->kind) {
    case IN_DOWN:
        s_drag = 0;
        s_dx0 = x;
        s_dy0 = y;
        s_dlast = y;
        s_dkey = 0;
        if (tp->np && in_r(&Y.plus, x, y, 2)) {
            s_dkey = 1;
            s_drag = 3;
        } else if (tp->np && in_r(&Y.minus, x, y, 2)) {
            s_dkey = -1;
            s_drag = 3;
        }
        break;
    case IN_MOVE: {
        if (s_drag == 3) {
            break;
        }
        int dx = x - s_dx0, dy = y - s_dy0;
        if (!s_drag) {
            for (int i = 0; i < tp->np; i++) {
                if (in_r(&Y.chip[i], s_dx0, s_dy0, 2) && abs(dy) > 10 && abs(dy) > abs(dx)) {
                    s_drag = 1;
                    if (s_sel != i) {
                        s_sel = i;
                    }
                    s_dlast = y;
                    input_tap_cancel();
                }
            }
            if (!s_drag && in_r(&Y.txt, s_dx0, s_dy0, 0) && abs(dx) > 26 && abs(dx) > abs(dy) * 2) {
                s_drag = 2;
                input_tap_cancel();
                // a swipe as the pages of a book: the finger to the start side = the next step
                bool next = s_he ? dx > 0 : dx < 0;
                page_go(next ? 1 : -1);
            }
        }
        if (s_drag == 1) {
            // the scrub: 14 px up = one step up
            int st = BOARD_UI_TABLET ? 10 : 14;
            while (s_dlast - y >= st) {
                par_change(s_sel, 1);
                s_dlast -= st;
            }
            while (y - s_dlast >= st) {
                par_change(s_sel, -1);
                s_dlast += st;
            }
        }
        break;
    }
    case IN_LONG:
        if (s_drag == 3) {
            s_rep_t = s_now;  // the hold of a key: the value goes on (update())
            par_change(s_sel, s_dkey);
        }
        break;
    case IN_UP:
        if (s_drag != 3) {
            s_drag = 0;
        }
        break;
    case IN_TAP:
        if (s_drag == 3 && s_rep_t == 0) {
            par_change(s_sel, s_dkey);
            s_drag = 0;
            break;
        }
        if (s_drag) {
            s_drag = 0;
            break;
        }
        if (in_r(&Y.chap, x, y, 4)) {
            fx(FX_PAGEB, 150);
            home_open();
            return;
        }
        for (int i = 0; i < tp->np; i++) {
            if (in_r(&Y.chip[i], x, y, 2)) {
                if (s_sel != i) {
                    s_sel = i;
                    fx(FX_PICK, 110);
                }
                return;
            }
        }
        if (in_r(&Y.txt, x, y, 0)) {
            text_tap(x);
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- the panel
static const char *const LANG_V[] = {"ENGLISH", "אנגלית", "HEBREW", "עברית"};
static const char *const ONOFF_V[] = {"ON", "פועל", "OFF", "כבוי"};
static const char *const SIZE_V[] = {"NORMAL", "רגיל", "LARGE", "גדול"};
enum {
    MI_LANG,
    MI_SIZE,
    MI_SND,
    MI_CHAP
};
static const pitem_t ITEMS[] = {
        {PI_PICK, MI_LANG, 2, 0, {"LANGUAGE", "שפה"}, LANG_V, "linalg_lang"},
        {PI_PICK, MI_SIZE, 2, 0, {"TEXT SIZE", "גודל הטקסט"}, SIZE_V, "linalg_size"},
        {PI_PICK, MI_SND, 2, 0, {"SOUND", "צליל"}, ONOFF_V, "linalg_snd"},
        {PI_ACT, MI_CHAP, 0, 0, {"CHAPTERS", "פרקים"}, NULL, NULL},
};
static void item_set(int id, int v)
{
    if (id == MI_LANG) {
        s_he = v == 1;
    } else if (id == MI_SIZE) {
        s_big = v == 1;
    } else if (id == MI_SND) {
        s_snd = v == 0;
    } else if (id == MI_CHAP) {
        home_open();
    }
    faces_set();
}
static const pitems_t PANEL = {ITEMS, 4, NULL, NULL, item_set, NULL, NULL};

// ---------------------------------------------------------------- the hooks
static void enter(int64_t now)
{
    s_now = s_last = s_touch_t = now;
    s_he = store_get("linalg_lang", 0) == 1;
    s_big = store_get("linalg_size", 0) == 1;
    s_snd = store_get("linalg_snd", 0) == 0;
    faces_set();
    save_load();
    calc_all();
    memset(&s_ks, 0, sizeof s_ks);
    s_drag = 0;
    home_open();
    if (!s_layer) {
        s_layer = heap_caps_malloc((size_t)gfx_w * gfx_h * 2, MALLOC_CAP_SPIRAM);  // (NULL: each frame draws all)
    }
    s_layer_key = 0;
    tfsfx_load_mem(LA_SFX_DATA, LA_SFX_LEN, FXN, FX_N < LA_SFX_N ? FX_N : LA_SFX_N, 48);
    sysui_panel_items(&PANEL);
}
static void leave(void)
{
    if (s_dirty) {
        save_now();
    }
    tfsfx_free();
    if (s_layer) {
        heap_caps_free(s_layer);
        s_layer = NULL;
    }
}
static void input(const input_event_t *ev)
{
    s_touch_t = ev->t_ms;
    if (s_scr == SC_HOME) {
        home_input(ev);
    } else {
        topic_input(ev);
    }
}
static void update(int64_t now)
{
    float dt = (now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.1f) {
        dt = 0.1f;
    }
    if (dt < 0) {
        dt = 0;
    }
    s_now = now;
    s_t += dt;
    s_ek = s_snap ? 1.0f : 1.0f - expf(-dt * 7.0f);
    if (s_scr == SC_HOME) {
        ks_update(&s_ks, now);
    }
    // the hold of + or -: a new step each 110 ms
    if (s_drag == 3 && s_rep_t && now - s_rep_t > 110) {
        s_rep_t = now;
        par_change(s_sel, s_dkey);
    }
    if (s_drag != 3) {
        s_rep_t = 0;
    }
    if (s_dirty && now - s_dirty_t > 2500) {
        save_now();
    }
    // idle: about 10 frames a second after 30 s with no touch (the pictures move slower)
#ifdef ESP_PLATFORM
    if (now - s_touch_t > 30000) {
        vTaskDelay(pdMS_TO_TICKS(90));
    }
#endif
}
static void render(int64_t now)
{
    tfd_screen();
    s_rtl = s_he;
    if (s_scr == SC_HOME) {
        home_draw(now);
    } else {
        topic_draw(now);
    }
    s_snap = false;
}

// ---------------------------------------------------------------- the icon: a grid that a matrix turns, its two
// eigenvectors (the loop: 2 s)
static void draw_icon(int x, int y, int s)
{
    tfd_screen();
    gfx_fill_rect(x, y, s, s, rgb(14, 16, 30));
    float t = icon_time() * 3.14159265f;  // one turn in 2 s
    rect_t r = R(x, y, s, s);
    s_pr = r;
    s_vs = s / 3.6f;
    s_vcx = x + s * 0.5f;
    s_vcy = y + s * 0.5f;
    tfd_clip(x, y, x + s, y + s);
    float k = 0.5f + 0.5f * fsin(t);
    float a = 1 + 0.6f * k, b = 0.5f * k, d = 1 - 0.35f * k;
    grid_map(a, b, b, d, 4, rgb(70, 100, 170), 140);
    ellipse(a, b, b, d, 0, 0, s / 40.0f + 1, rgb(176, 146, 255), 230);
    // the axes of the ellipse: the eigenvectors of [a b; b d]
    float tr = a + d, det = a * d - b * b, disc = sqrtf(tr * tr / 4 - det);
    float l1 = tr / 2 + disc, l2 = tr / 2 - disc;
    float vx = b, vy = l1 - a, L = sqrtf(vx * vx + vy * vy);
    if (L < 1e-4f) {
        vx = 1;
        vy = 0;
        L = 1;
    }
    vx /= L;
    vy /= L;
    arrow(0, 0, vx * l1, vy * l1, rgb(84, 204, 255), s / 30.0f + 1, 256);
    arrow(0, 0, -vy * l2, vx * l2, rgb(255, 112, 172), s / 30.0f + 1, 256);
    tfd_noclip();
}

const app_t app_linalg = {
        .name = "LINEAR ALGEBRA",
        .landscape = true,
        .enter = enter,
        .leave = leave,
        .input = input,
        .update = update,
        .render = render,
        .draw_icon = draw_icon,
        .round = APP_ROUND_HI,
};
