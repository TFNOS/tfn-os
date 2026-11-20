// CONTROL: the drawing of each screen (each frame paints the whole frame; libs/tapui) and the menu icon

// a name that starts a device: the red key
static bool power_name(const char *n)
{
    return !strcmp(n, "POWER") || !strcmp(n, "הפעלה") || !strcmp(n, "ON/OFF");
}

static void waves(int cx, int cy, int r, uint16_t c)
{
    const float t = (float)(s_now % 1300) / 1300.0f;
    for (int k = 0; k < 3; k++) {
        float f = t + (float)k / 3.0f;
        f -= (float)(int)f;
        ui_ring(cx, cy, (int)((float)r * (0.3f + 0.7f * f)), TU_MM(0.5f) + 1, c, (int)(255 * (1.0f - f)));
    }
    ui_circle(cx, cy, r / 5, c, 256);
}

// a companion with at least one module in its wiring
static bool any_module(void)
{
    for (int i = 0; i < tcm_count(); i++) {
        if (tcm_get(i)->has_info && tcm_get(i)->info.nwire) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------- HOME: the remotes
static void draw_home(void)
{
    const bool round = T_.fk == TU_FK_ROUND;
    tu_begin(round ? 1 : 0, s_now);
    if (!s_nrem && !any_module()) {
        // (no companion with a module: one big key to COMPANIONS)
        // (tu_empty: the key always shows in full; before, on the 320 x 240 frame it covered the last line)
        tu_empty(T(tcm_count() ? T_NO_LED : T_NO_COMP), T(T_SETUP_COMP), TU_ID(TU_H_SUB, 0));
        s_tu_content = 0;
    } else {
        tu_grid_t g = tu_grid(s_nrem + 1, T_.fk == TU_FK_BAR ? 22 : 24, 11, 22, T_.by0);
        for (int r = 0; r <= s_nrem; r++) {
            int x, y, w, h;
            if (!tu_cell(&g, r, &x, &y, &w, &h)) {
                continue;
            }
            if (r == s_nrem) {
                char b[40];
                snprintf(b, sizeof b, "+  %s", T(T_NEW_REMOTE));
                tu_key(x, y, w, h, b, s_nrem ? TU_K_GHOST : TU_K_PRI, TU_ID(TU_H_ITEM, 900), true);
                continue;
            }
            const int id = TU_ID(TU_H_ITEM, r);
            tu_box(x, y, w, h, TU_K_NORMAL, s_tu_down == id);
            const int ci = comp_of_title(r);
            tu_dot(T_.he ? x + TU_MM(2) : x + w - TU_MM(2), y + TU_MM(2), TU_MM(0.8f),
                   tcm_online(ci) ? TU_ACC : TU_FAINT);
            const kfont_t *f = h > TU_MM(15) ? T_.f_big : T_.f_key;
            tu_text_c(f, x + w / 2, w - TU_MM(4), y + h / 2 + f->asc / 2 - TU_MM(1), s_rem[r].name, TU_TEXT);
            char sub[32];
            snprintf(sub, sizeof sub, "%d %s", s_rem[r].nb, T(T_BUTTONS));
            tu_text_c(T_.f_tiny, x + w / 2, w - TU_MM(2), y + h - TU_MM(1.6f), sub, TU_DIM);
            tu_hit(x, y, w, h, id, true);
        }
    }
    tu_chrome(T(T_APP), false, round ? NULL : T(T_COMPANIONS), TU_K_NORMAL, NULL, 0);
    if (round) {
        tu_act(0, T(T_COMPANIONS), TU_K_NORMAL);
    }
}

// ---------------------------------------------------------------- A REMOTE: its buttons
// a SWITCH key or a SENSOR tile: the name, and the value of its module (a lit key for an active one)
static void draw_mod_key(const btn_t *b, int x, int y, int w, int h, int id)
{
    const int ci = tcm_find(b->mac);
    int v = 0;
    const bool has = tcm_value(ci, b->pin, &v, NULL);
    const int role = btn_role(b);
    const bool adc = role == TC_R_ANALOG;
    const bool lit = has && !adc && v && (b->kind == K_SENSOR || b->act != A_PRESS);
    int style = b->kind == K_SWITCH ? TU_K_NORMAL : TU_K_GHOST;
    if (s_edit) {
        style = TU_K_WARN;
    } else if (lit) {
        style = b->kind == K_SWITCH ? TU_K_PRI : TU_K_ACC;
    }
    tu_box(x, y, w, h, style, s_tu_down == id);
    const kfont_t *f = h > TU_MM(13) ? T_.f_key : T_.f_small;
    // the name at the top, the value under it
    tu_text_c(f, x + w / 2, w - TU_MM(3), y + h / 2 - TU_MM(0.6f), b->name, lit ? TU_BG : TU_TEXT);
    char val[24];
    if (!has) {
        snprintf(val, sizeof val, "%s", role == TC_R_NONE ? "?" : "...");
    } else if (adc) {
        snprintf(val, sizeof val, "%d mV", v);
    } else if (b->kind == K_SWITCH && b->act == A_PRESS) {
        snprintf(val, sizeof val, "%s", T(T_PRESS));
    } else {
        snprintf(val, sizeof val, "%s", state_word(role, v));
    }
    const kfont_t *fv = h > TU_MM(13) ? T_.f_small : T_.f_tiny;
    tu_text_c(fv, x + w / 2, w - TU_MM(2), y + h / 2 + fv->asc + TU_MM(0.4f), val, lit ? TU_BG : TU_DIM);
    tu_hit(x, y, w, h, id, true);
}

// ---------------------------------------------------------------- PICK: the module of a new key
static void draw_pick(void)
{
    tu_begin(0, s_now);
    const int rh = tu_row_h() + TU_MM(1);
    int y = T_.by0 - (int)tu_scroll();
    int n = 0;
    for (int ci = 0; ci < tcm_count(); ci++) {
        const tcm_comp_t *c = tcm_get(ci);
        if (!c->has_info) {
            continue;
        }
        for (int k = 0; k < c->info.nwire && k < 16; k++) {
            const int cls = tc_role_class(c->info.wire[k].role);
            const bool want = s_pick.kind == K_SWITCH ? cls == TC_C_OUT : (cls == TC_C_IN || cls == TC_C_ADC);
            if (!want) {
                continue;
            }
            char t[40], e[16];
            snprintf(t, sizeof t, "%s", role_word(c->info.wire[k].role));
            snprintf(e, sizeof e, "GPIO %d", c->info.wire[k].pin);
            tu_card(y, rh - TU_MM(1.4f), t, c->name, e, TU_DIM, tcm_online(ci) ? TU_ACC : TU_FAINT,
                    TU_ID(TU_H_ITEM, ci << 4 | k), true);
            y += rh;
            n++;
        }
    }
    if (!n) {
        y = tu_para(T_.f_small, T(T_NO_MODULE), T_.bx0, T_.bx1 - T_.bx0, y + TU_MM(1), T_.by1, TU_DIM) + TU_MM(2);
        int x, w;
        const int kh = TU_MM(11);
        tu_row_x(y, kh, &x, &w);
        const int kw = w > TU_MM(60) ? TU_MM(60) : w;
        tu_key(x + (w - kw) / 2, y, kw, kh, T(T_COMPANIONS), TU_K_PRI, TU_ID(TU_H_SUB, 0), true);
        y += kh;
    }
    s_tu_content = (float)(y + (int)tu_scroll() - T_.by0);
    tu_chrome(T(s_pick.kind == K_SWITCH ? T_PICK_SWITCH : T_PICK_SENSOR), true, NULL, 0, NULL, 0);
}

static void draw_remote(void)
{
    rem_t *m = &s_rem[s_rem_i];
    const int ci = comp_of_title(s_rem_i);
    const bool round = T_.fk == TU_FK_ROUND;
    tu_begin(round ? 1 : 0, s_now);
    int y0 = T_.by0;
    if (s_edit) {
        y0 = tu_para(T_.f_tiny, T(T_EDIT_TIP), T_.bx0, T_.bx1 - T_.bx0, T_.by0 - (int)tu_scroll(), T_.by1, TU_WARN) +
             TU_MM(1) + (int)tu_scroll();
    }
    const int n = m->nb + (s_edit ? 0 : 1);
    tu_grid_t g = tu_grid(n, T_.fk == TU_FK_BAR ? 17 : 18, T_.fk == TU_FK_BAR ? 6.6f : 10, 18, y0);
    for (int k = 0; k < n; k++) {
        int x, y, w, h;
        if (!tu_cell(&g, k, &x, &y, &w, &h)) {
            continue;
        }
        const int id = TU_ID(TU_H_ITEM, k == m->nb ? 900 : k);
        if (k == m->nb) {
            char b[32];
            snprintf(b, sizeof b, "+  %s", s_he ? "הוספה" : "ADD");
            tu_key(x, y, w, h, b, m->nb ? TU_K_GHOST : TU_K_PRI, id, true);
            continue;
        }
        const btn_t *b = &m->b[k];
        if (b->kind != K_IR) {
            draw_mod_key(b, x, y, w, h, id);
            continue;
        }
        const bool lit = s_press_btn == k && s_press_sent;
        int style = power_name(b->name) ? TU_K_BAD : TU_K_NORMAL;
        if (s_edit) {
            style = TU_K_WARN;
        }
        if (lit) {
            style = TU_K_HOT;
        }
        tu_box(x, y, w, h, style, s_tu_down == id);
        if (lit) {
            ui_ring(x + w / 2, y + h / 2, h / 2 + (int)(s_now % 300) * TU_MM(1) / 300, 2, TU_HOT, 140);
        }
        const kfont_t *f = h > TU_MM(13) ? T_.f_big : T_.f_key;
        if (s_edit) {
            tu_text_c(f, x + w / 2, w - TU_MM(3), y + h / 2 + f->asc / 2 - TU_MM(1.2f), b->name, TU_TEXT);
            tu_text_c(T_.f_tiny, x + w / 2, w - TU_MM(2), y + h - TU_MM(1.4f),
                      b->kind == K_RF ? (b->act ? "RF 315" : "RF 433")
                      : b->nraw       ? "RAW"
                                      : ird_name(b->code.proto),
                      TU_DIM);
        } else {
            tu_text_c(f, x + w / 2, w - TU_MM(3), y + h / 2 + f->asc / 2 - 1, b->name, TU_TEXT);
        }
        tu_hit(x, y, w, h, id, true);
    }
    const bool on = tcm_online(ci);
    tu_chrome(m->name, true, round ? NULL : T(s_edit ? T_DONE : T_EDIT), s_edit ? TU_K_ACC : TU_K_NORMAL,
              T(on ? T_ONLINE : T_OFFLINE), on ? TU_ACC : TU_FAINT);
    if (round) {
        tu_act(0, T(s_edit ? T_DONE : T_EDIT), s_edit ? TU_K_ACC : TU_K_NORMAL);
    }
}

// ---------------------------------------------------------------- LEARN
static void code_line(char *o, int n)
{
    if (s_lmode == LM_RF) {
        char hx[40];
        rfd_hex(&s_lrf, hx, sizeof hx);
        if (s_lrf.kind == RFD_K_RAW) {
            snprintf(o, (size_t)n, "%d %s", s_lnf, T(T_TIMES));
        } else {
            snprintf(o, (size_t)n, "%d BITS  %s  %d us", s_lrf.nbits, hx, s_lrf.t_short);
        }
        return;
    }
    if (s_lcode.proto == IRD_P_UNKNOWN) {
        snprintf(o, (size_t)n, "%d %s", s_lnf, T(T_TIMES));
    } else if (ird_has_fields(s_lcode.proto)) {
        snprintf(o, (size_t)n, "%s 0x%lX   %s 0x%lX", T(T_ADDRESS), (unsigned long)s_lcode.address, T(T_COMMAND),
                 (unsigned long)s_lcode.command);
    } else {
        char hx[40];
        ird_raw_hex(&s_lcode, hx, sizeof hx);
        snprintf(o, (size_t)n, "%d BITS  %s", s_lcode.nbits, hx);
    }
}

static void draw_learn(void)
{
    const int ci = s_lci;
    const bool got = s_lstate == L_GOT;
    tu_begin(s_lstate == L_FAIL ? 1 : 0, s_now);
    if (got) {
        const bool side = T_.fk == TU_FK_BAR;
        const int bx0 = T_.bx0, bx1 = T_.bx1;
        int y = side ? T_.by0 + TU_MM(1.5f) : T_.by0 - (int)tu_scroll();
        if (side) {
            T_.bx1 = bx0 + (bx1 - bx0) * 34 / 100;  // (the code in the left part)
        }
        char rfname[32];
        const bool unknown = s_lmode == LM_RF ? s_lrf.kind == RFD_K_RAW : s_lcode.proto == IRD_P_UNKNOWN;
        if (s_lmode == LM_RF) {
            snprintf(rfname, sizeof rfname, s_lrf.kind == RFD_K_RCSWITCH ? "RF %s %d" : "RF %s",
                     rfd_kind_name(s_lrf.kind), s_lrf.proto);
        }
        const char *proto = unknown ? T(T_GOT_RAW) : s_lmode == LM_RF ? rfname : ird_name(s_lcode.proto);
        int x, w;
        tu_row_x(y, T_.f_big->line, &x, &w);
        tu_text_c(T_.f_big, x + w / 2, w, y + T_.f_big->asc, proto, unknown ? TU_WARN : TU_ACC);
        y += T_.f_big->line;
        char line[64];
        code_line(line, sizeof line);
        tu_row_x(y, T_.f_small->line, &x, &w);
        tu_text_c(T_.f_small, x + w / 2, w, y + T_.f_small->asc, line, TU_DIM);
        y += T_.f_small->line + TU_MM(1);
        const int dup = same_code();
        if (dup >= 0) {
            // (the key may be the wrong one: the remote has this code already)
            char warn[160];
            snprintf(warn, sizeof warn, T(T_DUP), s_rem[s_rem_i].b[dup].name);
            y = tu_para(T_.f_small, warn, T_.bx0, T_.bx1 - T_.bx0, y, T_.by1 + 9999, TU_WARN) + TU_MM(0.8f);
        }
        // the chips: TEST, (SAVE of LEARN AGAIN,) AGAIN, OTHER NAME, then the names (a tap saves)
        if (s_relearn < 0) {
            y = tu_para(T_.f_tiny, T(T_NAME_IT), T_.bx0, T_.bx1 - T_.bx0, y, T_.by1 + 9999, TU_TEXT) + TU_MM(0.6f);
        }
        if (side) {
            T_.bx0 = T_.bx1 + TU_MM(1.5f);  // (the chips in the right part, the whole height)
            T_.bx1 = bx1;
            y = T_.by0 - (int)tu_scroll();
        }
        int idx[NNAMES];
        const int nf = s_relearn < 0 ? free_names(idx) : 0;
        const int lead = s_relearn < 0 ? 2 : 3;
        const int n = lead + nf + (s_relearn < 0 ? 1 : 0);
        tu_grid_t g = tu_grid(n, side ? 14 : 12, side ? 6.6f : 8, 11, y + (int)tu_scroll());
        for (int k = 0; k < n; k++) {
            int cx, cy, cw, chh;
            if (!tu_cell(&g, k, &cx, &cy, &cw, &chh)) {
                continue;
            }
            if (k == 0) {
                tu_key(cx, cy, cw, chh, T(T_TEST), TU_K_HOT, TU_ID(TU_H_SUB, 1), true);
            } else if (k == 1 && s_relearn >= 0) {
                tu_key(cx, cy, cw, chh, T(T_SAVE), TU_K_PRI, TU_ID(TU_H_SUB, 3), true);
            } else if (k == lead - 1) {
                tu_key(cx, cy, cw, chh, T(T_AGAIN), TU_K_GHOST, TU_ID(TU_H_SUB, 2), true);
            } else if (k == lead && s_relearn < 0) {  // (OTHER NAME next to AGAIN: no scroll to it)
                tu_key(cx, cy, cw, chh, T(T_OTHER_NAME), TU_K_GHOST, TU_ID(TU_H_SUB, 0), true);
            } else {
                const int i = idx[k - lead - (s_relearn < 0 ? 1 : 0)];
                tu_key(cx, cy, cw, chh, NAMES[i][s_he ? 1 : 0], power_name(NAMES[i][0]) ? TU_K_BAD : TU_K_NORMAL,
                       TU_ID(TU_H_ITEM, i), true);
            }
        }
        T_.bx0 = bx0;
        T_.bx1 = bx1;
    } else if (s_lstate == L_FAIL) {
        tu_para(T_.f_key, T(s_lfail), T_.bx0, T_.bx1 - T_.bx0, (T_.by0 + T_.by1) / 2 - T_.f_key->line, T_.by1, TU_WARN);
        s_tu_content = 0;
    } else {
        const bool ready = ci >= 0 && tcm_capture_ready(ci);
        const char *say = T(ready ? (s_lnext ? T_NEXT_KEY : s_lmode == LM_RF ? T_RF_POINT : T_POINT) : T_CONNECTING);
        if (T_.fk == TU_FK_BAR) {
            const int r = (T_.by1 - T_.by0) * 3 / 10;
            waves(T_.bx0 + r + TU_MM(4), (T_.by0 + T_.by1) / 2, r, ready ? TU_HOT : TU_LINE);
            const int x = T_.bx0 + 2 * r + TU_MM(8);
            tu_para(T_.f_big, say, x, T_.bx1 - x, T_.by0 + TU_MM(4), T_.by1, ready ? TU_TEXT : TU_DIM);
        } else {
            const int r = (T_.by1 - T_.by0) / 4;
            waves(gfx_w / 2, T_.by0 + r + TU_MM(1), r, ready ? TU_HOT : TU_LINE);
            tu_para(T_.f_key, say, T_.bx0, T_.bx1 - T_.bx0, T_.by0 + 2 * r + TU_MM(2.5f), T_.by1,
                    ready ? TU_TEXT : TU_DIM);
        }
        s_tu_content = 0;
    }
    char title[48];
    if (s_relearn >= 0) {
        snprintf(title, sizeof title, "%s: %s", T(T_LEARN_AGAIN), s_rem[s_rem_i].b[s_relearn].name);
    } else {
        snprintf(title, sizeof title, "%s: %s", T(T_LEARN), s_rem[s_rem_i].name);
    }
    const bool on = ci >= 0 && tcm_online(ci);
    tu_chrome(title, true, NULL, 0, T(on ? T_ONLINE : T_OFFLINE), on ? TU_ACC : TU_FAINT);
    if (s_lstate == L_FAIL) {
        tu_act(0, T(T_COMPANIONS), TU_K_PRI);
    }
}

static void render(int64_t now)
{
    if (s_typing) {
        kbd_render(now);
        return;
    }
    s_now = esp_timer_get_time() / 1000;
    if (!s_rem) {
        tu_begin(0, s_now);
        tu_chrome(T(T_APP), false, NULL, 0, NULL, 0);
        tu_end();
        return;
    }
    switch (s_scr) {
    case S_REMOTE:
        draw_remote();
        break;
    case S_LEARN:
        draw_learn();
        break;
    case S_PICK:
        draw_pick();
        break;
    default:
        draw_home();
        break;
    }
    tu_menu_draw();
    tu_end();
}

// ---------------------------------------------------------------- the menu icon: a remote that sends
static void icon(int x, int y, int s)
{
    gfx_fill_rect(x, y, s, s, rgb(18, 24, 40));
    const int bw = s * 34 / 100, bh = s * 70 / 100, bx = x + s * 22 / 100, by = y + s * 22 / 100;
    ui_rbox(bx, by, bw, bh, bw / 3, rgb(44, 54, 80), rgb(110, 124, 160), s > 60 ? 2 : 1);
    ui_circle(bx + bw / 2, by + bh * 18 / 100, bw / 6 > 2 ? bw / 6 : 2, rgb(232, 88, 76), 256);
    for (int k = 0; k < 3; k++) {
        for (int j = 0; j < 2; j++) {
            ui_circle(bx + bw * (30 + 40 * j) / 100, by + bh * (42 + 17 * k) / 100, bw / 10 > 1 ? bw / 10 : 1,
                      rgb(200, 210, 230), 256);
        }
    }
    // the IR beam from the top of the remote
    const float t = icon_time();
    const int ex = bx + bw / 2, ey = by - 2;
    for (int k = 0; k < 3; k++) {
        float f = t * 0.9f + (float)k / 3.0f;
        f -= (float)(int)f;
        const int r = (int)((float)s * (0.10f + 0.42f * f));
        ui_ring(ex + r * 3 / 4, ey - r * 3 / 4 + 2, r / 3 + 1, s > 60 ? 3 : 2, rgb(255, 120, 60), (int)(255 * (1 - f)));
    }
}
