// RING SEQUENCER: the touch. A tap acts at once; a twist (a finger that turns round the centre of the circle) or a
// drag changes a value; a hold mutes a ring, gives an accent or copies to a slot. (Part of app_ringseq.c.)

enum {
    M_NONE,
    M_TWIST,  // a value turns with the angle of the finger round the centre
    M_ROT,    // the steps of the ring turn with the finger
    M_VAL,    // the pitch (a melodic ring) or the level (a drum ring) of a step
    M_VDRAG,  // a value goes up and down with the finger (the side keys)
    M_SWIPE,  // V_RING core: the next ring
    M_PICK,   // V_MAIN: the finger slides over the rings; the ring under it lights up, the release opens it
};
typedef struct {
    bool down, moved, edit;
    int id, arg, step, mode;
    float x0, y0, x, y;
    float a_last, acc, d0;  // the twist: the last angle, the sum of the turn; the distance at the start
    int base, applied;
    int64_t t0;
} touch_t;
static touch_t T;
static int64_t s_touch_ms;  // the last touch (the idle rest)

static void edit_start(void)
{
    if (!T.edit) {
        undo_push();
        T.edit = true;
    }
    s_dirty = true;
}
static void view_go(int v)
{
    s_view = v;
    s_act = -1;
    lay_build();
    bg_all();
}
static void ring_open(int r)
{
    s_zoom_from = L.kind == L_ROUND ? ring_r(r) : L.zr;
    s_sel = r;
    view_go(V_RING);
    s_zoom_t0 = s_now;
}
static void play_toggle(void)
{
    play_set(!s_run);
    if (!s_run) {
        save_now();
    }
    bg_hit(hit_find(E_CENTRE, 0));
    bg_hit(hit_find(B_PLAY, 0));
}
// the zones that show a new value after a change of the pattern
static void bg_values(void)
{
    bg_all();
}

// ---------------------------------------------------------------- the values of a ring

static int param_get(int i)
{
    const ring_t *g = &P->r[s_sel];
    return i == 0 ? ring_hits(g) : i == 1 ? g->n : i == 2 ? g->snd : g->lvl;
}
static void param_set(int i, int v)
{
    ring_t *g = &P->r[s_sel];
    if (i == 0) {
        v = v < 0 ? 0 : v > g->n ? g->n : v;
        if (v != ring_hits(g)) {
            edit_start();
            ring_euclid(g, v);
        }
    } else if (i == 1) {
        v = v < NMIN ? NMIN : v > NS ? NS : v;
        if (v != g->n) {
            edit_start();
            ring_resize(g, v);
        }
    } else if (i == 2) {
        int n = snd_count(s_sel);
        v = ((v % n) + n) % n;
        if (v != g->snd) {
            edit_start();
            g->snd = (uint8_t)v;
            if (!s_run) {
                int k = ring_first(g);
                if (!g->vel[k]) {
                    g->vel[k] = 2;
                    preview(s_sel, k);
                    g->vel[k] = 0;
                } else {
                    preview(s_sel, k);
                }
            }
        }
    } else {
        v = v < 0 ? 0 : v > 100 ? 100 : v;
        if (v != g->lvl) {
            edit_start();
            g->lvl = (uint8_t)v;
        }
    }
    bg_values();
}
// the twist step of each value (radians for one unit)
static float twist_unit(int id)
{
    switch (id) {
    case E_CENTRE:
        return rad(3.5f);
    case P_HITS:
        return rad(20);
    case P_STEPS:
        return rad(22);
    case P_SND:
        return rad(32);
    case B_SWING:
        return rad(7);
    default:
        return rad(4);  // LEVEL: 1 for each 4 degrees (the base gets steps of 1)
    }
}
static void twist_apply(void)
{
    int u = (int)(T.acc / twist_unit(T.id) + (T.acc < 0 ? -0.5f : 0.5f));
    if (T.id == E_CENTRE) {
        int b = T.base + u;
        b = b < 40 ? 40 : b > 240 ? 240 : b;
        if (b != P->bpm) {
            P->bpm = (uint8_t)b;
            s_dirty = true;
            bg_hit(hit_find(E_CENTRE, 0));
            bg_hit(hit_find(B_PLAY, 0));
        }
    } else if (T.id == B_SWING) {
        int b = T.base + u;
        b = b < 50 ? 50 : b > 75 ? 75 : b;
        if (b != P->swing) {
            edit_start();
            P->swing = (uint8_t)b;
            bg_hit(hit_find(B_SWING, 0));
        }
    } else {
        int i = T.id - P_HITS;
        param_set(i, T.base + u);
    }
}

// ---------------------------------------------------------------- the steps

// the step under (x, y) on the big ring or the lane; -1 = none
static int step_at(float x, float y)
{
    const ring_t *g = &P->r[s_sel];
    if (L.kind == L_BAR) {
        int k = (int)((x - L.lx) * g->n / L.lw);
        return k < 0 ? 0 : k >= g->n ? g->n - 1 : k;
    }
    if (dist_of(x, y) < L.zi - 8 * L.s) {
        return -1;
    }
    float a = ang_of(x, y) / 6.2831853f * g->n + 0.5f;
    int k = (int)(a < 0 ? a + g->n : a) % g->n;
    return k;
}
static void step_tap(int k)
{
    ring_t *g = &P->r[s_sel];
    if (k < 0 || k >= g->n) {
        return;
    }
    edit_start();
    if (g->vel[k]) {
        g->vel[k] = 0;
    } else {
        g->vel[k] = 2;
        if (melodic(s_sel) && ring_hits(g) == 1) {
            g->note[k] = s_lastnote[s_sel];
        }
        if (!s_run) {
            preview(s_sel, k);
        }
    }
    bg_values();
}
static void step_value(int k, int v)
{
    ring_t *g = &P->r[s_sel];
    if (melodic(s_sel)) {
        v = v < note_lo(s_sel) ? note_lo(s_sel) : v > note_hi(s_sel) ? note_hi(s_sel) : v;
        if (v != g->note[k] || !g->vel[k]) {
            edit_start();
            g->note[k] = (int8_t)v;
            g->vel[k] = g->vel[k] ? g->vel[k] : 2;
            s_lastnote[s_sel] = (int8_t)v;
            preview(s_sel, k);
            bg_values();
        }
    } else {
        v = v < 1 ? 1 : v > 3 ? 3 : v;
        if (v != g->vel[k]) {
            edit_start();
            g->vel[k] = (uint8_t)v;
            if (!s_run) {
                preview(s_sel, k);
            }
            bg_values();
        }
    }
}

// ---------------------------------------------------------------- the taps

static void slot_go(int i, bool copy)
{
    save_now();
    if (copy || !S->used[i]) {
        S->slot[i] = *P;
        S->used[i] = 1;
    }
    S->cur = (uint8_t)i;
    s_pl = P;
    s_undo_n = 0;
    s_dirty = true;
    char b[32];
    snprintf(b, sizeof b, "%s %d", tx("SLOT", "תבנית"), i + 1);
    toast(b, copy ? tx("COPIED HERE", "הועתק לכאן") : NULL);
    bg_all();
}
static void do_vary(void)
{
    undo_push();
    int w = vary(P);
    if (w < 0) {
        s_undo_n--;
        return;
    }
    s_dirty = true;
    toast(tx("VARIATION", "שינוי"), RNAME[w][s_he]);
    bg_all();
}
static void do_export(void)
{
    char name[40];
    bool card;
    if (s_run) {
        // a flash write stops the sound for some ms: the play stops first (no stutter)
        play_set(false);
        bg_hit(hit_find(E_CENTRE, 0));
        bg_hit(hit_find(B_PLAY, 0));
    }
    if (midi_export(name, sizeof name, &card)) {
        toast(name, card ? tx("SD CARD + BOARD WEB", "כרטיס SD + אתר הלוח") : tx("BOARD WEB: MIDI", "אתר הלוח: MIDI"));
    } else {
        toast(tx("NO ROOM", "אין מקום"), tx("THE FILE WAS NOT SAVED", "הקובץ לא נשמר"));
    }
}
static void ring_mute(int r)
{
    undo_push();
    P->r[r].mute = !P->r[r].mute;
    s_dirty = true;
    toast(RNAME[r][s_he], P->r[r].mute ? tx("MUTE", "מושתק") : tx("ON", "פועל"));
    bg_all();
}
static void tap_main(int id, int arg, float x, float y)
{
    switch (id) {
    case E_CENTRE:
        play_toggle();
        break;
    case E_RINGS:
        ring_open(s_pick >= 0 ? s_pick : ring_at(dist_of(x, y)));
        s_pick = -1;
        break;
    case B_RING:
        ring_open(arg);
        break;
    case B_PLAY:
        play_toggle();
        break;
    case B_KEY:
        view_go(V_KEY);
        break;
    case B_SLOTS:
        view_go(V_SLOTS);
        break;
    case B_SWING: {
        static const uint8_t SW[] = {50, 54, 58, 62, 66, 70, 75};
        int i = 0;
        while (i < 7 && SW[i] <= P->swing) {
            i++;
        }
        undo_push();
        P->swing = SW[i % 7];
        s_dirty = true;
        bg_hit(hit_find(B_SWING, 0));
        break;
    }
    case B_MODE:
        undo_push();
        P->poly = !P->poly;
        s_dirty = true;
        toast(P->poly ? tx("METER", "משקלים") : tx("RHYTHM", "מקצבים"),
              P->poly ? tx("EACH STEP = 1/16", "כל צעד = 1/16") : tx("EACH RING = 1 BAR", "כל טבעת = תיבה"));
        bg_all();
        break;
    case B_UNDO:
        if (undo_pop()) {
            toast(tx("UNDO", "בטל"), NULL);
            bg_all();
        }
        break;
    case B_VARY:
        do_vary();
        break;
    case B_EXPORT:
        do_export();
        break;
    }
}
static void ring_solo(void)
{
    ring_t *g = &P->r[s_sel];
    edit_start();
    g->solo = !g->solo;
    if (g->solo) {
        g->mute = 0;
    }
    toast(RNAME[s_sel][s_he], g->solo ? tx("SOLO", "סולו") : tx("SOLO OFF", "סולו כבוי"));
    bg_values();
}
static void tap_ring(int id, int arg)
{
    if (L.kind == L_SIDE && id >= P_HITS && id <= P_LVL) {
        const hit_t *h = hit_find(id, 0);
        int i = id - P_HITS, dir = (h && T.x0 < h->a + h->c / 2) ? -1 : 1;
        param_set(i, param_get(i) + dir * (i == 3 ? 5 : 1));
        return;
    }
    switch (id) {
    case B_MUTE:
        edit_start();
        P->r[s_sel].mute = !P->r[s_sel].mute;
        bg_values();
        break;
    case B_SOLO:
        ring_solo();
        break;
    case E_CENTRE:
    case B_BACK:
        view_go(V_MAIN);
        break;
    case E_BAND:
    case E_LANE:
        step_tap(T.step);
        break;
    case P_HITS:
        edit_start();
        ring_euclid(&P->r[s_sel], ring_hits(&P->r[s_sel]));  // the hits spread evenly
        bg_values();
        break;
    case P_STEPS:
        param_set(1, P->r[s_sel].n >= NS ? NMIN : P->r[s_sel].n + 1);
        break;
    case P_SND:
        param_set(2, P->r[s_sel].snd + 1);
        break;
    case P_LVL:
        edit_start();
        P->r[s_sel].mute = !P->r[s_sel].mute;
        bg_values();
        break;
    case B_MINUS:
    case B_PLUS:
        param_set(arg, param_get(arg) + (id == B_PLUS ? 1 : -1) * (arg == 3 ? 5 : 1));
        break;
    case B_PREV:
    case B_NEXT:
        s_sel = (s_sel + (id == B_NEXT ? 1 : NR - 1)) % NR;
        view_go(V_RING);
        break;
    }
}
static void tap_other(int id, int arg)
{
    if (id == K_KEY || id == K_MODE) {
        undo_push();
        if (id == K_KEY) {
            P->key = (uint8_t)arg;
        } else {
            P->mode = (uint8_t)arg;
        }
        s_dirty = true;
        if (!s_run && !audio_muted()) {
            ring_t *g = &P->r[R_CHORD];
            uint8_t v = g->vel[0];
            int8_t no = g->note[0];
            g->vel[0] = 2;
            g->note[0] = 0;
            preview(R_CHORD, 0);
            g->vel[0] = v;
            g->note[0] = no;
        }
        bg_all();
    } else if (id == K_DONE || id == E_CENTRE || id == B_BACK) {
        view_go(V_MAIN);
    } else if (id == S_SLOT) {
        slot_go(arg, false);
    }
}

// ---------------------------------------------------------------- the events

static bool twistable(int id)
{
    return id == E_CENTRE ? s_view == V_MAIN : id == B_SWING || (L.kind == L_ROUND && id >= P_HITS && id <= P_LVL);
}
static void on_down(const input_event_t *ev)
{
    const hit_t *h = hit_at(ev->x, ev->y);
    memset(&T, 0, sizeof T);
    T.down = true;
    T.id = h ? h->id : E_NONE;
    T.arg = h ? h->arg : 0;
    T.x0 = T.x = ev->x;
    T.y0 = T.y = ev->y;
    T.t0 = ev->t_ms;
    T.a_last = ang_of(ev->x, ev->y);
    T.d0 = dist_of(ev->x, ev->y);
    T.step = (T.id == E_BAND || T.id == E_LANE) ? step_at(ev->x, ev->y) : -1;
    if (T.id == E_BAND && T.step < 0) {
        T.id = E_NONE;
    }
    if (T.id != E_NONE && T.id != E_RINGS && T.id != E_BAND && T.id != E_LANE) {
        s_act = T.id;
        s_act_arg = T.arg;
        bg_hit(h);
    }
    if (T.id == S_SLOT) {
        s_hold_slot = T.arg;
        s_hold_t0 = s_now;
    }
    if (T.id == E_RINGS) {
        s_pick = ring_at(T.d0);
        s_pick_low = T.y0 > L.cy;
    }
}
static void move_start(void)
{
    float dx = T.x - T.x0, dy = T.y - T.y0;
    if (twistable(T.id)) {
        T.mode = M_TWIST;
        T.base = T.id == E_CENTRE ? P->bpm : T.id == B_SWING ? P->swing : param_get(T.id - P_HITS);
    } else if (T.id >= P_HITS && T.id <= P_LVL) {
        T.mode = M_VDRAG;
        T.base = param_get(T.id - P_HITS);
    } else if (T.id == E_CENTRE && s_view == V_RING) {
        T.mode = M_SWIPE;
    } else if (T.id == E_RINGS) {
        T.mode = M_PICK;
    } else if (T.id == E_BAND || T.id == E_LANE) {
        float tang, radl;
        if (L.kind == L_BAR) {
            tang = dx < 0 ? -dx : dx;
            radl = dy < 0 ? -dy : dy;
        } else {
            float d = dist_of(T.x, T.y) - T.d0, da = ang_of(T.x, T.y) - T.a_last;
            da = da > 3.14159f ? da - 6.2831853f : da < -3.14159f ? da + 6.2831853f : da;
            tang = (da < 0 ? -da : da) * T.d0;
            radl = d < 0 ? -d : d;
        }
        T.mode = tang > radl ? M_ROT : M_VAL;
        const ring_t *g = &P->r[s_sel];
        T.base = melodic(s_sel) ? g->note[T.step] : (g->vel[T.step] ? g->vel[T.step] : 2);
    } else {
        T.mode = M_NONE;
    }
    if (T.mode != M_NONE) {
        input_tap_cancel();
    }
    s_hold_slot = -1;
}
static void on_move(const input_event_t *ev)
{
    T.x = ev->x;
    T.y = ev->y;
    float dx = T.x - T.x0, dy = T.y - T.y0, thr = 1.6f / L.mm;
    if (!T.moved) {
        if (dx * dx + dy * dy < thr * thr) {
            return;
        }
        T.moved = true;
        move_start();
    }
    if (T.mode == M_TWIST || T.mode == M_ROT) {
        float a = ang_of(T.x, T.y), da = a - T.a_last;
        da = da > 3.14159f ? da - 6.2831853f : da < -3.14159f ? da + 6.2831853f : da;
        // (near the centre the angle of the finger jumps: a dead zone)
        if (dist_of(T.x, T.y) > (s_view == V_RING ? L.zc : 0.6f * L.rc)) {
            T.acc += da;
        }
        T.a_last = a;
    }
    if (T.mode == M_PICK) {
        float d = dist_of(T.x, T.y);
        s_pick = d > L.rc && d < L.R ? ring_at(d) : -1;
    } else if (T.mode == M_TWIST) {
        twist_apply();
    } else if (T.mode == M_ROT) {
        ring_t *g = &P->r[s_sel];
        float unit = L.kind == L_BAR ? L.lw / g->n : 6.2831853f / g->n;
        float q = (L.kind == L_BAR ? dx : T.acc) / unit;
        int st = (int)(q + (q < 0 ? -0.5f : 0.5f));
        if (st != T.applied) {
            edit_start();
            ring_rotate(g, st - T.applied);
            T.applied = st;
            bg_values();
        }
    } else if (T.mode == M_VAL) {
        float d = L.kind == L_BAR ? -dy : dist_of(T.x, T.y) - T.d0;
        float unit = (melodic(s_sel) ? 2.0f : 3.0f) / L.mm;
        float q = d / unit;
        step_value(T.step, T.base + (int)(q + (q < 0 ? -0.5f : 0.5f)));
    } else if (T.mode == M_VDRAG) {
        int i = T.id - P_HITS;
        float q = -dy / (2.6f / L.mm);
        param_set(i, T.base + (int)(q + (q < 0 ? -0.5f : 0.5f)) * (i == 3 ? 5 : 1));
    }
}
static void on_up(void)
{
    if (T.mode == M_PICK && s_pick >= 0) {
        ring_open(s_pick);
    }
    if (T.id != E_RINGS || T.mode == M_PICK) {
        s_pick = -1;
    }
    if (T.mode == M_SWIPE) {
        float dx = T.x - T.x0;
        if (dx > 5 / L.mm || dx < -5 / L.mm) {
            s_sel = (s_sel + (dx < 0 ? 1 : NR - 1)) % NR;
            view_go(V_RING);
            s_zoom_t0 = s_now;
            s_zoom_from = L.zc;
        }
    }
    if (s_act >= 0) {
        bg_hit(hit_find(s_act, s_act_arg));
    }
    s_act = -1;
    T.down = false;
    s_hold_slot = -1;
}
static void on_long(void)
{
    if (T.moved) {
        return;
    }
    if (s_view == V_MAIN && T.id == E_RINGS) {
        ring_mute(s_pick >= 0 ? s_pick : ring_at(T.d0));
        s_pick = -1;
        T.id = E_NONE;
    } else if (s_view == V_MAIN && T.id == B_RING) {
        ring_mute(T.arg);
        T.id = E_NONE;
    } else if ((T.id == E_BAND || T.id == E_LANE) && P->r[s_sel].vel[T.step]) {
        ring_t *g = &P->r[s_sel];
        edit_start();
        g->vel[T.step] = g->vel[T.step] == 3 ? 2 : 3;
        bg_values();
        T.id = E_NONE;
    } else if (T.id == P_LVL && L.kind != L_SIDE) {
        ring_solo();
        T.id = E_NONE;
    }
}
static void input(const input_event_t *ev)
{
    s_touch_ms = s_now;
    if (ev->kind == IN_DOWN) {
        on_down(ev);
    } else if (ev->kind == IN_MOVE && T.down) {
        on_move(ev);
    } else if (ev->kind == IN_UP) {
        on_up();
    } else if (ev->kind == IN_LONG && T.down) {
        on_long();
    } else if (ev->kind == IN_TAP && !T.moved && T.id != E_NONE) {
        int id = T.id;
        T.id = E_NONE;
        if (s_view == V_MAIN) {
            tap_main(id, T.arg, T.x0, T.y0);
        } else if (s_view == V_RING) {
            tap_ring(id, T.arg);
        } else {
            tap_other(id, T.arg);
        }
    }
}
// the hold on a slot: 1 s copies the pattern there
static void hold_update(void)
{
    if (s_hold_slot >= 0 && T.down && !T.moved && s_now - s_hold_t0 >= 1000) {
        int i = s_hold_slot;
        s_hold_slot = -1;
        T.id = E_NONE;
        slot_go(i, true);
    }
}
