// INTEGRALS: the dispatch of the screens: the fades between them, the touch, the BOOT key, the rest when nothing moves.

static void screen_enter(int st)
{
    switch (st) {
    case ST_TITLE:
        title_layout();
        break;
    case ST_TOC:
        toc_layout();
        break;
    case ST_CHAP:
        chap_layout();
        break;
    case ST_LES:
        lesson_enter();
        break;
    case ST_GRAPH:
        les_geom();
        graph_place(false);
        s_ar = s_L[s_Lc].ar;
        g_labels(2);
        break;
    default:
        break;
    }
}

static void go(int st)
{
    if (s_tr >= 1.0f) {
        s_tr = 0.0f;
    }
    s_next = st;
    s_touch_t = s_now;
}

// the language changed: the same screen again
static void ui_relayout(void)
{
    if (s_db) {
        screen_enter(s_st);
        if (s_st == ST_LES) {
            s_si = s_si < var_at(s_cc, s_li, s_vi)->nsteps ? s_si : 0;
        }
    }
}

static void ui_enter(int64_t now)
{
    s_now = s_last = s_touch_t = now;
    s_tcol[TC_INK] = C_INK;
    s_tcol[TC_HL] = C_HL;
    s_tcol[TC_DIM] = C_DIM;
    s_st = ST_FAIL;
    s_next = -1;
    s_tr = 1.0f;
    s_drag = false;  // (a drag of the last open must not go on: its arenas are gone)
    s_pdrag = false;
    tarena_t **all[5] = {&s_arL[0], &s_arL[1], &s_arT, &s_arC, &s_arH};
    bool ok = db_load();
    for (int i = 0; i < 5 && ok; i++) {
        *all[i] = heap_caps_malloc(sizeof(tarena_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        ok = *all[i] != NULL;
        if (ok) {
            (*all[i])->n = 0;
        }
    }
    s_ar = s_arH;
    if (!ok) {
        return;
    }
    bg_make();
    s_total_les = s_nles;
    load_save();
    s_gT.n = 0;
    s_st = ST_TITLE;
    title_layout();
    s_tr = 0.5f;
    s_next = ST_TITLE;
}

static void ui_leave(void)
{
    tarena_t **all[5] = {&s_arL[0], &s_arL[1], &s_arT, &s_arC, &s_arH};
    for (int i = 0; i < 5; i++) {
        heap_caps_free(*all[i]);
        *all[i] = NULL;
    }
    s_L[0].ar = s_L[1].ar = NULL;
    s_pdrag = false;
    heap_caps_free(s_bg);
    s_bg = NULL;
    heap_caps_free(s_gc);
    s_gc = NULL;
    s_gc_ok = false;
    s_g_new.ok = s_g_old.ok = false;
}

// ---------------------------------------------------------------- the touch
static grid_t *grid_now(void)
{
    return s_st == ST_TOC ? &s_gT : s_st == ST_CHAP ? &s_gC : NULL;
}

static void param_from_x(int x)
{
    const igd_lesson_t *l = les_at(s_cc, s_li);
    const rc_t g = mir(mini_graph() && s_st == ST_LES ? s_G.mini : s_G.graph);
    int k = (x - g.x) * l->nvals / (g.w > 0 ? g.w : 1);
    k = k < 0 ? 0 : k >= l->nvals ? l->nvals - 1 : k;
    if (s_he) {
        k = l->nvals - 1 - k;
    }
    param_set(k);
}

static void les_tap(int x, int y)
{
    const rc_t back = back_rc();
    if (in_rc(&back, x, y)) {
        fx(FX_BACK);
        if (s_st == ST_GRAPH) {
            go(ST_LES);
            return;
        }
        ui_save();
        go(ST_CHAP);
        return;
    }
    if (s_G.has_p) {
        const rc_t mi = mir(s_G.minus), pl = mir(s_G.plus);
        if (in_rc(&mi, x, y)) {
            param_set(s_vi - 1);
            return;
        }
        if (in_rc(&pl, x, y)) {
            param_set(s_vi + 1);
            return;
        }
        const rc_t va = mir(s_G.val);
        if (in_rc(&va, x, y)) {
            return;
        }
    }
    if (s_st == ST_GRAPH) {
        const rc_t g = mir(s_G.graph);
        if (s_G.has_p && in_rc(&g, x, y)) {
            param_from_x(x);  // a tap on the big graph: the value of that place
        }
        return;
    }
    if (mini_graph() && s_G.has_g) {
        const rc_t mm = mir(s_G.mini);
        if (in_rc(&mm, x, y)) {
            fx(FX_OPEN);
            go(ST_GRAPH);
            return;
        }
    }
    if (s_end) {
        const int bw = (s_G.endc.w - 10) / 2;
        const rc_t again = mir(RC(s_G.endc.x, s_G.endc.y, bw, s_G.endc.h));
        const rc_t next = mir(RC(s_G.endc.x + bw + 10, s_G.endc.y, bw, s_G.endc.h));
        if (in_rc(&again, x, y)) {
            fx(FX_OPEN);
            lesson_open(s_cc, s_li, s_vi, 0);
        } else if (in_rc(&next, x, y)) {
            fx(FX_OPEN);
            if (s_li + 1 < s_ch[s_cc].n) {
                lesson_open(s_cc, s_li + 1, les_at(s_cc, s_li + 1)->def, 0);
            } else if (s_cc + 1 < s_nch) {
                lesson_open(s_cc + 1, 0, les_at(s_cc + 1, 0)->def, 0);
            } else {
                go(ST_TOC);
            }
        }
        return;
    }
    const rc_t pv = mir(s_G.prev);
    if (in_rc(&pv, x, y)) {
        step_go(-1);
        return;
    }
    if (s_G.has_g && !mini_graph()) {
        const rc_t g = mir(s_G.graph);
        if (in_rc(&g, x, y)) {
            if (s_G.has_p) {
                param_from_x(x);  // a tap on the graph: the value of that place
            }
            return;
        }
    }
    step_go(1);
}

static void list_tap(int x, int y)
{
    const rc_t back = back_rc();
    if (in_rc(&back, x, y)) {
        fx(FX_BACK);
        go(s_st == ST_TOC ? ST_TITLE : ST_TOC);
        return;
    }
    grid_t *G = grid_now();
    if (!G || !ks_tap_ok(&G->ks)) {
        return;
    }
    const int i = grid_hit(G, x, y);
    if (i < 0) {
        return;
    }
    fx(FX_OPEN);
    if (s_st == ST_TOC) {
        s_cc = i;
        s_li = s_has_save && s_sv_cc == i ? s_sv_li : 0;
        go(ST_CHAP);
    } else {
        const igd_lesson_t *l = les_at(s_cc, i);
        const bool same = s_has_save && s_sv_cc == s_cc && s_sv_li == i;
        lesson_open(s_cc, i, same ? s_sv_vi : l->def, 0);
    }
}

static void ui_input(const input_event_t *ev)
{
    s_touch_t = s_now;
    if (s_tr < 1.0f || s_st == ST_FAIL) {
        return;
    }
    grid_t *G = grid_now();
    switch (ev->kind) {
    case IN_DOWN:
        s_dn_x = ev->x;
        s_dn_y = ev->y;
        s_drag = false;
        s_pdrag = false;
        if (G) {
            ks_down(&G->ks, grid_axis(G, ev->x, ev->y), ev->t_ms);
        }
        if ((s_st == ST_LES || s_st == ST_GRAPH) && s_G.has_p) {
            const rc_t g = mir(mini_graph() && s_st == ST_LES ? s_G.mini : s_G.graph);
            s_pdrag = in_rc(&g, ev->x, ev->y);
        }
        break;
    case IN_MOVE:
        if (G) {
            if (ks_move(&G->ks, grid_axis(G, ev->x, ev->y), ev->t_ms)) {
                input_tap_cancel();
            }
        } else if (s_pdrag) {
            if (abs(ev->x - s_dn_x) > 8 && !s_drag) {
                s_drag = true;
                input_tap_cancel();
            }
            if (s_drag) {
                param_from_x(ev->x);
            }
        } else if (s_st == ST_LES && !s_drag && abs(ev->x - s_dn_x) > 34 &&
                   abs(ev->x - s_dn_x) > 2 * abs(ev->y - s_dn_y)) {
            s_drag = true;
            input_tap_cancel();
        }
        break;
    case IN_UP:
        if (G) {
            ks_up(&G->ks, ev->t_ms);
        } else if (s_drag && !s_pdrag && s_st == ST_LES) {
            const int dx = ev->x - s_dn_x;
            step_go((dx < 0) != s_he ? 1 : -1);
        }
        break;
    case IN_TAP:
        if (s_st == ST_TITLE) {
            title_tap(ev->x, ev->y);
        } else if (G) {
            list_tap(ev->x, ev->y);
        } else {
            les_tap(ev->x, ev->y);
        }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------- the frame
static void ui_update(int64_t now)
{
    float dt = (float)(now - s_last) / 1000.0f;
    s_last = now;
    s_now = now;
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    const int keys = app_key_presses();
    if (keys && s_st == ST_LES && s_tr >= 1.0f) {
        s_touch_t = now;
        step_go(1);
    }
    bool moving = false;
    if (s_tr < 1.0f) {
        const float before = s_tr;
        s_tr += dt / 0.36f;
        if (before < 0.5f && s_tr >= 0.5f && s_next >= 0) {
            s_st = s_next;
            screen_enter(s_st);
        }
        if (s_tr > 1.0f) {
            s_tr = 1.0f;
            s_next = -1;
        }
        moving = true;
    }
    if (s_lt < 1.0f) {
        s_lt += dt / 0.28f;
        s_lt = s_lt > 1.0f ? 1.0f : s_lt;
        moving = true;
    }
    if (s_end && s_end_t < 1.0f) {
        s_end_t += dt / 0.3f;
        moving = true;
    }
    if (s_g_t < 1.0f) {
        g_update(dt);
        moving = true;
    }
    grid_t *G = grid_now();
    if (G && ks_update(&G->ks, now)) {
        moving = true;
    }
    if (now - s_hl_t0 < 1500 || (s_st == ST_LES && now - s_open_t < 6000)) {
        moving = true;
    }
    // the title moves all the time; the other screens rest when nothing moves (about 7 frames a second)
    if (!moving && now - s_touch_t > 2500 && (s_st != ST_TITLE || now - s_touch_t > 60000)) {
        IG_REST();
    }
}

static void fail_render(void)
{
    const char *t = s_he ? "אין מספיק זיכרון: הפעילו מחדש את הלוח" : "NO MEMORY: RESTART THE BOARD";
    kf_line_c(&KF_TEXT, gfx_w / 2, gfx_h / 2, t, C_INK, 255);
}

static void ui_render(int64_t now)
{
    (void)now;
    bg_draw();
    tfd_screen();
    tfd_noclip();
    clip_off();
    const float a = s_tr < 0.5f ? 1.0f - s_tr * 2.0f : (s_tr - 0.5f) * 2.0f;
    s_fade = (int)(255 * smooth01(a));
    switch (s_st) {
    case ST_TITLE:
        title_render(255);
        break;
    case ST_TOC:
        toc_render(255);
        break;
    case ST_CHAP:
        chap_render(255);
        break;
    case ST_LES:
        les_render(255);
        break;
    case ST_GRAPH:
        graph_render(255);
        break;
    default:
        s_fade = 255;
        fail_render();
        break;
    }
    s_fade = 255;
}
